// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ManualDrawables.h"
#include "PipelineAccelerator.h"
#include "../Assets/RawMaterial.h"
#include "../Assets/ScaffoldCmdStream.h"
#include "../Assets/AssetUtils.h"
#include "../Assets/MaterialMachine.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Assets/BlockSerializer.h"

namespace RenderCore { namespace Techniques 
{

	struct ManualDrawableGeoConstructor::Pimpl : public std::enable_shared_from_this<ManualDrawableGeoConstructor::Pimpl>
	{
		std::vector<std::shared_ptr<DrawableGeo>> _pendingGeos;
		std::shared_ptr<IDrawablesPool> _pool;
		std::shared_ptr<BufferUploads::IManager> _bufferUploads;
		std::atomic<bool> _fulfillWhenNotPendingCalled = false;

		struct PendingResAssignment { unsigned _geoIdx; DrawableStream _stream; };
		std::vector<PendingResAssignment> _pendingResAssignment;

		struct ResourceUploader : public BufferUploads::IAsyncDataSource
		{
		public:
			struct UploadPart
			{
				size_t _offset, _size;

				// One of the following will be filled in -- 
				std::optional<std::pair<size_t, size_t>> _storageSrc;
				std::vector<uint8_t> _vectorSource;
				std::shared_ptr<BufferUploads::IDataPacket> _pkt;
				std::shared_ptr<BufferUploads::IAsyncDataSource> _asyncSrc;
			};
			std::vector<UploadPart> _parts;
			size_t _uploadTotal = 0;
			std::vector<uint8_t> _storage;

			ResourceDesc _desc;

			virtual std::future<ResourceDesc> GetDesc () override;
			virtual std::future<void> PrepareData(IteratorRange<const SubResource*> subResources) override;
			virtual ::Assets::DependencyValidation GetDependencyValidation() const override;
		};

		std::shared_ptr<ResourceUploader> _vb, _ib;

		// filled in by future
		BufferUploads::CommandListID _completionCmdList = 0;
	};

	std::future<ResourceDesc> ManualDrawableGeoConstructor::Pimpl::ResourceUploader::GetDesc()
	{
		std::promise<ResourceDesc> promise;
		auto result = promise.get_future();

		// if we have any child async packets, we can't complete our desc until they are all ready
		auto asyncChildren = std::make_shared<std::vector<std::future<ResourceDesc>>>();
		asyncChildren->reserve(_parts.size());
		for (const auto&p:_parts)
			if (p._asyncSrc)
				asyncChildren->emplace_back(p._asyncSrc->GetDesc());
		
		if (!asyncChildren->empty()) {
			::Assets::PollToPromise(
				std::move(promise),
				[asyncChildren](auto timeout) {
					auto timeoutTime = std::chrono::steady_clock::now() + timeout;
					for (auto& c:*asyncChildren) {
						auto status = c.wait_until(timeoutTime);
						if (status == std::future_status::timeout)
							return ::Assets::PollStatus::Continue;
					}
					return ::Assets::PollStatus::Finish;
				},
				[desc=_desc, asyncChildren]() {
					// ideally we'd validate the sizes of the async sources here, since this is the first time we know they are all done
					for (auto& c:*asyncChildren) c.get();
					return desc;
				});
		} else {
			promise.set_value(_desc);	// can complete immediately
		}

		return result;
	}

	std::future<void> ManualDrawableGeoConstructor::Pimpl::ResourceUploader::PrepareData(IteratorRange<const SubResource*> subResources)
	{
		assert(subResources.size() == 1);
		assert(subResources[0]._destination.size() >= _uploadTotal);
		auto asyncChildren = std::make_shared<std::vector<std::future<void>>>();
		asyncChildren->reserve(_parts.size());
		
		size_t offsetIterator = 0;
		for (const auto&p:_parts) {
			assert((p._offset+p._size) <= subResources[0]._destination.size());
			SubResource childSubRes = subResources[0];
			childSubRes._destination = {PtrAdd(childSubRes._destination.begin(), p._offset), PtrAdd(childSubRes._destination.begin(), p._offset+p._size)};
			assert(!childSubRes._destination.empty());
				
			if (p._asyncSrc) {
				auto childFuture = p._asyncSrc->PrepareData(MakeIteratorRange(&childSubRes, &childSubRes+1));
				asyncChildren->emplace_back(std::move(childFuture));
			} else if (p._pkt) {
				auto data = p._pkt->GetData();
				assert(data.size() == childSubRes._destination.size());
				std::memcpy(childSubRes._destination.begin(), data.begin(), std::min(data.size(), childSubRes._destination.size()));
			} else if (!p._vectorSource.empty()) {
				assert(p._vectorSource.size() == childSubRes._destination.size());
				std::memcpy(childSubRes._destination.begin(), p._vectorSource.data(), std::min(p._vectorSource.size(), childSubRes._destination.size()));
			} else if (p._storageSrc.has_value()) {
				assert(p._storageSrc->second == childSubRes._destination.size());
				assert((p._storageSrc->first + p._storageSrc->second) <= _storage.size());
				std::memcpy(childSubRes._destination.begin(), PtrAdd(_storage.data(), p._storageSrc->first), std::min(p._storageSrc->second, childSubRes._destination.size()));
			} else {
				assert(0);
			}
		}

		std::promise<void> promise;
		auto result = promise.get_future();
		if (!asyncChildren->empty()) {
			::Assets::PollToPromise(
				std::move(promise),
				[asyncChildren](auto timeout) {
					auto timeoutTime = std::chrono::steady_clock::now() + timeout;
					for (auto& c:*asyncChildren) {
						auto status = c.wait_until(timeoutTime);
						if (status == std::future_status::timeout)
							return ::Assets::PollStatus::Continue;
					}
					return ::Assets::PollStatus::Finish;
				},
				[asyncChildren]() {
					// we have to call get() on all children to flush out these futures
					for (auto& c:*asyncChildren) c.get();
				});
		} else {
			promise.set_value();
		}
		return result;
	}

	::Assets::DependencyValidation ManualDrawableGeoConstructor::Pimpl::ResourceUploader::GetDependencyValidation() const { return {}; }

	static DrawablesPacket::AllocateStorageResult AllocateFrom(std::vector<uint8_t>& vector, size_t size, unsigned alignment)
	{
		unsigned preAlignmentBuffer = 0;
		if (alignment != 0) {
			preAlignmentBuffer = alignment - (vector.size() % alignment);
			if (preAlignmentBuffer == alignment) preAlignmentBuffer = 0;
		}

		assert(vector.size() + preAlignmentBuffer + size < 10 * 1024 * 1024);

		size_t startOffset = vector.size() + preAlignmentBuffer;
		vector.resize(vector.size() + preAlignmentBuffer + size);
		return {
			MakeIteratorRange(AsPointer(vector.begin() + startOffset), AsPointer(vector.begin() + startOffset + size)),
			(unsigned)startOffset
		};
	}

	DrawablesPacket::AllocateStorageResult ManualDrawableGeoConstructor::AllocateStorage(DrawablesPacket::Storage storage, size_t byteCount)
	{
		const unsigned storageAligment = 0;
		if (storage == DrawablesPacket::Storage::Vertex) {
			return AllocateFrom(_pimpl->_vb->_storage, byteCount, storageAligment);
		} else if (storage == DrawablesPacket::Storage::Index) {
			return AllocateFrom(_pimpl->_ib->_storage, byteCount, storageAligment);
		} else {
			assert(0);
			return {};
		}
	}

	unsigned ManualDrawableGeoConstructor::BeginGeo()
	{
		assert(!_pimpl->_fulfillWhenNotPendingCalled.load());
		auto result = (unsigned)_pimpl->_pendingGeos.size();
		_pimpl->_pendingGeos.emplace_back(_pimpl->_pool->CreateGeo());
		(*(_pimpl->_pendingGeos.end()-1))->_ibFormat = Format::Unknown;		// must be set with SetIndexFormat()
		return result;
	}

	void ManualDrawableGeoConstructor::SetStreamData(DrawableStream str, DrawablesPacket::AllocateStorageResult storage)
	{
		assert(!_pimpl->_fulfillWhenNotPendingCalled.load());
		assert(!_pimpl->_pendingGeos.empty());
		assert(storage._data.size());
		auto& geo = **(_pimpl->_pendingGeos.end()-1);

		Pimpl::ResourceUploader* uploader;
		if (str == DrawableStream::IB) {
			geo._ibOffset = _pimpl->_ib->_uploadTotal;
			uploader = _pimpl->_ib.get();
		} else {
			auto strIdx = unsigned(str) - unsigned(DrawableStream::Vertex0);
			assert(strIdx < dimof(DrawableGeo::_vertexStreams));
			geo._vertexStreamCount = std::max(strIdx+1, geo._vertexStreamCount);

			geo._vertexStreams[strIdx]._vbOffset = _pimpl->_vb->_uploadTotal;
			uploader = _pimpl->_vb.get();
		}

		assert((storage._startOffset+storage._data.size()) <= uploader->_storage.size());
		Pimpl::ResourceUploader::UploadPart uploadPart;
		uploadPart._offset = uploader->_uploadTotal;
		uploadPart._size = storage._data.size();
		uploadPart._storageSrc = {storage._startOffset, storage._data.size()};
		uploader->_parts.emplace_back(std::move(uploadPart));
		uploader->_uploadTotal += storage._data.size();

		auto geoIdx = unsigned(_pimpl->_pendingGeos.size()-1);
		_pimpl->_pendingResAssignment.push_back({geoIdx, str});
	}

	void ManualDrawableGeoConstructor::SetStreamData(DrawableStream str, std::vector<uint8_t>&& sourceData)
	{
		assert(!_pimpl->_fulfillWhenNotPendingCalled.load());
		assert(!_pimpl->_pendingGeos.empty());		// call BeginGeo() first
		assert(!sourceData.empty());
		auto& geo = **(_pimpl->_pendingGeos.end()-1);

		Pimpl::ResourceUploader* uploader;
		if (str == DrawableStream::IB) {
			geo._ibOffset = _pimpl->_ib->_uploadTotal;
			uploader = _pimpl->_ib.get();
		} else {
			auto strIdx = unsigned(str) - unsigned(DrawableStream::Vertex0);
			assert(strIdx < dimof(DrawableGeo::_vertexStreams));
			geo._vertexStreamCount = std::max(strIdx+1, geo._vertexStreamCount);

			geo._vertexStreams[strIdx]._vbOffset = _pimpl->_vb->_uploadTotal;
			uploader = _pimpl->_vb.get();
		}

		Pimpl::ResourceUploader::UploadPart uploadPart;
		uploadPart._offset = uploader->_uploadTotal;
		uploadPart._size = sourceData.size();
		uploadPart._vectorSource = std::move(sourceData);
		uploader->_parts.emplace_back(std::move(uploadPart));
		uploader->_uploadTotal += sourceData.size();

		auto geoIdx = unsigned(_pimpl->_pendingGeos.size()-1);
		_pimpl->_pendingResAssignment.push_back({geoIdx, str});
	}

	void ManualDrawableGeoConstructor::SetStreamData(DrawableStream str, std::shared_ptr<BufferUploads::IDataPacket>&& sourceData)
	{
		assert(!_pimpl->_fulfillWhenNotPendingCalled.load());
		assert(!_pimpl->_pendingGeos.empty());		// call BeginGeo() first
		assert(sourceData);
		auto size = sourceData->GetData().size();
		assert(size != 0);
		auto& geo = **(_pimpl->_pendingGeos.end()-1);

		Pimpl::ResourceUploader* uploader;
		if (str == DrawableStream::IB) {
			geo._ibOffset = _pimpl->_ib->_uploadTotal;
			uploader = _pimpl->_ib.get();
		} else {
			auto strIdx = unsigned(str) - unsigned(DrawableStream::Vertex0);
			assert(strIdx < dimof(DrawableGeo::_vertexStreams));
			geo._vertexStreamCount = std::max(strIdx+1, geo._vertexStreamCount);

			geo._vertexStreams[strIdx]._vbOffset = _pimpl->_vb->_uploadTotal;
			uploader = _pimpl->_vb.get();
		}

		Pimpl::ResourceUploader::UploadPart uploadPart;
		uploadPart._offset = uploader->_uploadTotal;
		uploadPart._size = size;
		uploadPart._pkt = std::move(sourceData);
		uploader->_parts.emplace_back(std::move(uploadPart));
		uploader->_uploadTotal += size;

		auto geoIdx = unsigned(_pimpl->_pendingGeos.size()-1);
		_pimpl->_pendingResAssignment.push_back({geoIdx, str});
	}

	void ManualDrawableGeoConstructor::SetStreamData(DrawableStream str, std::shared_ptr<BufferUploads::IAsyncDataSource>&& sourceData, size_t size)
	{
		assert(!_pimpl->_fulfillWhenNotPendingCalled.load());
		assert(!_pimpl->_pendingGeos.empty());		// call BeginGeo() first
		assert(sourceData);
		assert(size != 0);
		auto& geo = **(_pimpl->_pendingGeos.end()-1);

		Pimpl::ResourceUploader* uploader;
		if (str == DrawableStream::IB) {
			geo._ibOffset = _pimpl->_ib->_uploadTotal;
			uploader = _pimpl->_ib.get();
		} else {
			auto strIdx = unsigned(str) - unsigned(DrawableStream::Vertex0);
			assert(strIdx < dimof(DrawableGeo::_vertexStreams));
			geo._vertexStreamCount = std::max(strIdx+1, geo._vertexStreamCount);

			geo._vertexStreams[strIdx]._vbOffset = _pimpl->_vb->_uploadTotal;
			uploader = _pimpl->_vb.get();
		}

		Pimpl::ResourceUploader::UploadPart uploadPart;
		uploadPart._offset = uploader->_uploadTotal;
		uploadPart._size = size;
		uploadPart._asyncSrc = std::move(sourceData);
		uploader->_parts.emplace_back(std::move(uploadPart));
		uploader->_uploadTotal += size;

		auto geoIdx = unsigned(_pimpl->_pendingGeos.size()-1);
		_pimpl->_pendingResAssignment.push_back({geoIdx, str});
	}

	void ManualDrawableGeoConstructor::SetStreamData(DrawableStream str, std::shared_ptr<IResource> resource, size_t offset)
	{
		assert(!_pimpl->_fulfillWhenNotPendingCalled.load());
		assert(!_pimpl->_pendingGeos.empty());		// call BeginGeo() first
		auto& geo = **(_pimpl->_pendingGeos.end()-1);

		if (str == DrawableStream::IB) {
			geo._ibOffset = offset;
			geo._ib = std::move(resource);
		} else {
			auto strIdx = unsigned(str) - unsigned(DrawableStream::Vertex0);
			assert(strIdx < dimof(DrawableGeo::_vertexStreams));
			geo._vertexStreamCount = std::max(strIdx+1, geo._vertexStreamCount);

			geo._vertexStreams[strIdx]._vbOffset = offset;
			geo._vertexStreams[strIdx]._resource = std::move(resource);
		}
	}

	void ManualDrawableGeoConstructor::SetIndexFormat(Format fmt)
	{
		assert(!_pimpl->_fulfillWhenNotPendingCalled.load());
		assert(!_pimpl->_pendingGeos.empty());		// call BeginGeo() first
		auto& geo = **(_pimpl->_pendingGeos.end()-1);
		geo._ibFormat = fmt;
	}

	void ManualDrawableGeoConstructor::FulfillWhenNotPending(std::promise<Promise>&& promise)
	{
		auto prevCalled = _pimpl->_fulfillWhenNotPendingCalled.exchange(true);
		if (prevCalled)
			Throw(std::runtime_error("Attempting to call DrawableGeoInitHelper::FulfillWhenNotPending multiple times. This can only be called once"));

		struct WaitingParts
		{
			BufferUploads::TransactionMarker _vbUploadMarker, _ibUploadMarker;
		};
		auto waitingParts = std::make_shared<WaitingParts>();

		// create an upload future for both VB & IB
		if (_pimpl->_vb->_uploadTotal != 0) {
			auto vbSize = _pimpl->_vb->_uploadTotal;
			_pimpl->_vb->_desc = CreateDesc(BindFlag::VertexBuffer, LinearBufferDesc::Create(vbSize), "[vb]");
			waitingParts->_vbUploadMarker = _pimpl->_bufferUploads->Transaction_Begin(_pimpl->_vb, _pimpl->_vb->_desc._bindFlags);
		}

		if (_pimpl->_ib->_uploadTotal != 0) {
			auto ibSize = _pimpl->_ib->_uploadTotal;
			_pimpl->_ib->_desc = CreateDesc(BindFlag::IndexBuffer, LinearBufferDesc::Create(ibSize), "[ib]");
			waitingParts->_ibUploadMarker = _pimpl->_bufferUploads->Transaction_Begin(_pimpl->_ib, _pimpl->_ib->_desc._bindFlags);
		}

		if (!waitingParts->_vbUploadMarker.IsValid() && !waitingParts->_ibUploadMarker.IsValid()) {
			promise.set_value(Promise{_pimpl});		// nothing to upload in this case
			return;
		}

		::Assets::PollToPromise(
			std::move(promise),
			[waitingParts](auto timeout) {
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				if (waitingParts->_vbUploadMarker.IsValid()) {
					auto futureStatus = waitingParts->_vbUploadMarker._future.wait_until(timeoutTime);
					if (futureStatus == std::future_status::timeout) return ::Assets::PollStatus::Continue;
				}
				if (waitingParts->_ibUploadMarker.IsValid()) {
					auto futureStatus = waitingParts->_ibUploadMarker._future.wait_until(timeoutTime);
					if (futureStatus == std::future_status::timeout) return ::Assets::PollStatus::Continue;
				}
				
				return ::Assets::PollStatus::Finish;
			},
			[waitingParts, pimpl=_pimpl]() mutable {

				// complete assignment of resource ptrs, & cmd list -- etc

				if (waitingParts->_vbUploadMarker.IsValid()) {
					auto resLocator = waitingParts->_vbUploadMarker._future.get();
					pimpl->_completionCmdList = std::max(pimpl->_completionCmdList, resLocator.GetCompletionCommandList());

					for (const auto& assignment:pimpl->_pendingResAssignment) {
						if (assignment._stream == DrawableStream::IB) continue;
						auto& geo = *pimpl->_pendingGeos[assignment._geoIdx];
						auto& stream = geo._vertexStreams[assignment._stream - DrawableStream::Vertex0];
						stream._resource = resLocator.GetContainingResource();
						auto offset = resLocator.GetRangeInContainingResource().first;
						if (offset != ~size_t(0)) stream._vbOffset += offset;
					}
				}

				if (waitingParts->_ibUploadMarker.IsValid()) {
					auto resLocator = waitingParts->_ibUploadMarker._future.get();
					pimpl->_completionCmdList = std::max(pimpl->_completionCmdList, resLocator.GetCompletionCommandList());

					for (const auto& assignment:pimpl->_pendingResAssignment) {
						if (assignment._stream != DrawableStream::IB) continue;
						auto& geo = *pimpl->_pendingGeos[assignment._geoIdx];
						geo._ib = resLocator.GetContainingResource();
						auto offset = resLocator.GetRangeInContainingResource().first;
						if (offset != ~size_t(0)) geo._ibOffset += offset;
					}
				}
				
				return Promise{std::move(pimpl)};
			});
	}

	BufferUploads::CommandListID ManualDrawableGeoConstructor::Promise::GetCompletionCommandList() const
	{
		assert(_pimpl->_fulfillWhenNotPendingCalled.load());
		return _pimpl->_completionCmdList;
	}

	IteratorRange<const std::shared_ptr<DrawableGeo>*> ManualDrawableGeoConstructor::Promise::GetInstantiatedGeos()
	{
		assert(_pimpl->_fulfillWhenNotPendingCalled.load());
		return MakeIteratorRange(_pimpl->_pendingGeos);
	}

	ManualDrawableGeoConstructor::Promise::Promise(std::shared_ptr<Pimpl> pimpl) : _pimpl(std::move(pimpl)) {}

	ManualDrawableGeoConstructor::ManualDrawableGeoConstructor(std::shared_ptr<IDrawablesPool> pool, std::shared_ptr<BufferUploads::IManager> bufferUploads)
	{
		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_pool = std::move(pool);
		_pimpl->_bufferUploads = std::move(bufferUploads);
		_pimpl->_vb = std::make_shared<Pimpl::ResourceUploader>();
		_pimpl->_ib = std::make_shared<Pimpl::ResourceUploader>();
	}
	ManualDrawableGeoConstructor::~ManualDrawableGeoConstructor() {}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	std::pair<std::shared_ptr<Techniques::PipelineAccelerator>, std::shared_ptr<Techniques::DescriptorSetAccelerator>> CreateAccelerators(
		Techniques::IPipelineAcceleratorPool& pool,
		const RenderCore::Assets::RawMaterial& material,
		IteratorRange<const InputElementDesc*> inputAssembly,
		Topology topology)
	{
		std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> patchCollectionPtr;
		if (material._patchCollection.GetHash())
		 	patchCollectionPtr = std::make_shared<RenderCore::Assets::ShaderPatchCollection>(material._patchCollection);
		
		std::pair<uint64_t, SamplerDesc> samplers[material._samplers.size()];
		for (unsigned c=0; c<material._samplers.size(); ++c)
			samplers[c] = {Hash64(material._samplers[c].first), material._samplers[c].second};

		auto materialMachine = std::make_shared<Techniques::ManualMaterialMachine>(material._uniforms, material._resources, MakeIteratorRange(samplers, &samplers[material._samplers.size()]));
		auto pipelineAccelerator = pool.CreatePipelineAccelerator(patchCollectionPtr, material._selectors, inputAssembly, topology, material._stateSet);
		auto descriptorSetAccelerator = pool.CreateDescriptorSetAccelerator(nullptr, patchCollectionPtr, materialMachine->GetMaterialMachine(), materialMachine);
		return {std::move(pipelineAccelerator), std::move(descriptorSetAccelerator)};
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	IteratorRange<RenderCore::Assets::ScaffoldCmdIterator> ManualMaterialMachine::GetMaterialMachine() const
	{
		auto* start = ::Assets::Block_GetFirstObject(_dataBlock.get());
		return Assets::MakeScaffoldCmdRange({start, PtrAdd(start, _primaryBlockSize)});
	}

	ManualMaterialMachine::ManualMaterialMachine(
		const ParameterBox& constantBindings,
		const ParameterBox& resourceBindings,
		IteratorRange<const std::pair<uint64_t, SamplerDesc>*> samplerBindings)
	{
		::Assets::BlockSerializer serializer;
		serializer << Assets::MakeCmdAndSerializable(
			Assets::MaterialCommand::AttachConstants,
			constantBindings);
		serializer << Assets::MakeCmdAndSerializable(
			Assets::MaterialCommand::AttachShaderResourceBindings,
			resourceBindings);
		serializer << Assets::MakeCmdAndRanged(
			Assets::MaterialCommand::AttachSamplerBindings,
			samplerBindings);
		_dataBlock = serializer.AsMemoryBlock();
		_primaryBlockSize = serializer.SizePrimaryBlock();
		::Assets::Block_Initialize(_dataBlock.get());
	}


}}
