// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ManualDrawables.h"
#include "PipelineAccelerator.h"
#include "TechniqueUtils.h"
#include "CommonBindings.h"
#include "ParsingContext.h"
#include "../Assets/RawMaterial.h"
#include "../Assets/ScaffoldCmdStream.h"
#include "../Assets/AssetUtils.h"
#include "../Assets/MaterialMachine.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../UniformsStream.h"
#include "../../Math/Transformations.h"
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
			std::string _name;

			virtual std::future<ResourceDesc> GetDesc () override;
			virtual std::future<void> PrepareData(IteratorRange<const SubResource*> subResources) override;
			virtual ::Assets::DependencyValidation GetDependencyValidation() const override;
			virtual StringSection<> GetName() const override;

			std::shared_ptr<BufferUploads::IDataPacket> AsDataPacket();
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
				UNREACHABLE();
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

	std::shared_ptr<BufferUploads::IDataPacket> ManualDrawableGeoConstructor::Pimpl::ResourceUploader::AsDataPacket()
	{
		std::shared_ptr<BufferUploads::IDataPacket> result;
		if (_parts.size() == 1) {
			auto& part = _parts[0];
			assert(part._offset == 0 && part._size == _uploadTotal);
			if (part._pkt) {
				return part._pkt;
			} else if (!part._vectorSource.empty()) {
				return BufferUploads::CreateBasicPacket(std::move(part._vectorSource), std::string{_name});
			} else if (part._asyncSrc) {
				Throw(std::runtime_error("ManualDrawableGeoConstructor::ImmediateFulFill cannot be used with uploads that include a IAsyncDataSource"));
			} else {
				std::vector<uint8_t> vbData;
				vbData.resize(_uploadTotal);
				std::memcpy(vbData.data(), PtrAdd(_storage.data(), part._storageSrc->first), part._storageSrc->second);
				return BufferUploads::CreateBasicPacket(std::move(vbData), std::string{_name});
			}	
		} else {
			std::vector<uint8_t> vbData;
			vbData.resize(_uploadTotal);
			// unfortunately we have to copy the upload data to a separate buffer if we have separate parts
			for (const auto& part:_parts) {
				if (part._pkt) {
					auto data = part._pkt->GetData();
					std::memcpy(PtrAdd(vbData.data(), part._offset), data.begin(), data.size());
				} else if (!part._vectorSource.empty()) {
					std::memcpy(PtrAdd(vbData.data(), part._offset), part._vectorSource.data(), part._vectorSource.size());
				} else if (part._asyncSrc) {
					Throw(std::runtime_error("ManualDrawableGeoConstructor::ImmediateFulFill cannot be used with uploads that include a IAsyncDataSource"));
				} else {
					std::memcpy(PtrAdd(vbData.data(), part._offset), PtrAdd(_storage.data(), part._storageSrc->first), part._storageSrc->second);
				}
			}
			return BufferUploads::CreateBasicPacket(std::move(vbData), std::string{_name});
		}
	}

	::Assets::DependencyValidation ManualDrawableGeoConstructor::Pimpl::ResourceUploader::GetDependencyValidation() const { return {}; }
	StringSection<> ManualDrawableGeoConstructor::Pimpl::ResourceUploader::GetName() const { return _name; }

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
		const unsigned storageAlignment = 0;
		if (storage == DrawablesPacket::Storage::Vertex) {
			return AllocateFrom(_pimpl->_vb->_storage, byteCount, storageAlignment);
		} else if (storage == DrawablesPacket::Storage::Index) {
			return AllocateFrom(_pimpl->_ib->_storage, byteCount, storageAlignment);
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

	void ManualDrawableGeoConstructor::SetStreamData(DrawableStream str, std::vector<uint8_t>&& sourceData, std::string&& name)
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

		auto size = sourceData.size();
		Pimpl::ResourceUploader::UploadPart uploadPart;
		uploadPart._offset = uploader->_uploadTotal;
		uploadPart._size = size;
		uploadPart._vectorSource = std::move(sourceData);
		uploader->_parts.emplace_back(std::move(uploadPart));
		uploader->_uploadTotal += size;
		if (uploader->_name != name) {
			if (!uploader->_name.empty()) uploader->_name += "+" + name;
			else uploader->_name = std::move(name);
		}

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
		auto name = uploadPart._pkt->GetName().AsString();
		if (uploader->_name != name) {
			if (!uploader->_name.empty()) uploader->_name += "+" + name;
			else uploader->_name = std::move(name);
		}

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
		auto name = uploadPart._asyncSrc->GetName().AsString();
		if (uploader->_name != name) {
			if (!uploader->_name.empty()) uploader->_name += "+" + name;
			else uploader->_name = std::move(name);
		}

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
			Throw(std::runtime_error("Attempting to call DrawableGeoInitHelper fulfill method multiple times. This can only be called once"));

		struct WaitingParts
		{
			BufferUploads::TransactionMarker _vbUploadMarker, _ibUploadMarker;
		};
		auto waitingParts = std::make_shared<WaitingParts>();

		// create an upload future for both VB & IB
		if (_pimpl->_vb->_uploadTotal != 0) {
			auto vbSize = _pimpl->_vb->_uploadTotal;
			_pimpl->_vb->_desc = CreateDesc(BindFlag::VertexBuffer|BindFlag::TransferDst, LinearBufferDesc::Create(vbSize));
			waitingParts->_vbUploadMarker = _pimpl->_bufferUploads->Begin(_pimpl->_vb, _pimpl->_vb->_desc._bindFlags);
		}

		if (_pimpl->_ib->_uploadTotal != 0) {
			auto ibSize = _pimpl->_ib->_uploadTotal;
			_pimpl->_ib->_desc = CreateDesc(BindFlag::IndexBuffer|BindFlag::TransferDst, LinearBufferDesc::Create(ibSize));
			waitingParts->_ibUploadMarker = _pimpl->_bufferUploads->Begin(_pimpl->_ib, _pimpl->_ib->_desc._bindFlags);
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
						geo._completionCmdList = std::max(geo._completionCmdList, resLocator.GetCompletionCommandList());
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
						geo._completionCmdList = std::max(geo._completionCmdList, resLocator.GetCompletionCommandList());
					}
				}
				
				return Promise{std::move(pimpl)};
			});
	}

	auto ManualDrawableGeoConstructor::ImmediateFulfill() -> Promise
	{
		auto prevCalled = _pimpl->_fulfillWhenNotPendingCalled.exchange(true);
		if (prevCalled)
			Throw(std::runtime_error("Attempting to call DrawableGeoInitHelper fulfill method multiple times. This can only be called once"));

		for (auto& q:_pimpl->_vb->_parts)
			if (q._asyncSrc)
				Throw(std::runtime_error("ManualDrawableGeoConstructor::ImmediateFulFill cannot be used with uploads that include a IAsyncDataSource"));

		for (auto& q:_pimpl->_ib->_parts)
			if (q._asyncSrc)
				Throw(std::runtime_error("ManualDrawableGeoConstructor::ImmediateFulFill cannot be used with uploads that include a IAsyncDataSource"));

		if (_pimpl->_vb->_uploadTotal != 0) {
			_pimpl->_vb->_desc = CreateDesc(BindFlag::VertexBuffer|BindFlag::TransferDst, LinearBufferDesc::Create(_pimpl->_vb->_uploadTotal));
			auto vbUploadPkt = _pimpl->_vb->AsDataPacket();
			auto vb = _pimpl->_bufferUploads->ImmediateTransaction(_pimpl->_vb->_desc, std::move(vbUploadPkt));
			assert(!vb.IsEmpty());

			_pimpl->_completionCmdList = std::max(_pimpl->_completionCmdList, vb.GetCompletionCommandList());

			for (const auto& assignment:_pimpl->_pendingResAssignment) {
				if (assignment._stream == DrawableStream::IB) continue;
				auto& geo = *_pimpl->_pendingGeos[assignment._geoIdx];
				auto& stream = geo._vertexStreams[assignment._stream - DrawableStream::Vertex0];
				stream._resource = vb.GetContainingResource();
				auto offset = vb.GetRangeInContainingResource().first;
				if (offset != ~size_t(0)) stream._vbOffset += offset;
				geo._completionCmdList = std::max(geo._completionCmdList, vb.GetCompletionCommandList());
			}
		}

		if (_pimpl->_ib->_uploadTotal != 0) {
			_pimpl->_ib->_desc = CreateDesc(BindFlag::IndexBuffer|BindFlag::TransferDst, LinearBufferDesc::Create(_pimpl->_ib->_uploadTotal));
			auto ibUploadPkt = _pimpl->_ib->AsDataPacket();
			auto ib = _pimpl->_bufferUploads->ImmediateTransaction(_pimpl->_ib->_desc, std::move(ibUploadPkt));
			assert(!ib.IsEmpty());

			_pimpl->_completionCmdList = std::max(_pimpl->_completionCmdList, ib.GetCompletionCommandList());

			for (const auto& assignment:_pimpl->_pendingResAssignment) {
				if (assignment._stream != DrawableStream::IB) continue;
				auto& geo = *_pimpl->_pendingGeos[assignment._geoIdx];
				geo._ib = ib.GetContainingResource();
				auto offset = ib.GetRangeInContainingResource().first;
				if (offset != ~size_t(0)) geo._ibOffset += offset;
				geo._completionCmdList = std::max(geo._completionCmdList, ib.GetCompletionCommandList());
			}
		}

		return Promise{_pimpl};
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

	static const std::string s_manualDrawables{"manual-drawables"};

	std::pair<std::shared_ptr<Techniques::PipelineAccelerator>, std::shared_ptr<Techniques::DescriptorSetAccelerator>> CreateAccelerators(
		Techniques::IPipelineAcceleratorPool& pool,
		const RenderCore::Assets::RawMaterial& material,
		IteratorRange<const InputElementDesc*> inputAssembly,
		Topology topology)
	{
		std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> patchCollectionPtr;
		if (material._patchCollection.GetHash())
		 	patchCollectionPtr = std::make_shared<RenderCore::Assets::ShaderPatchCollection>(material._patchCollection);
		
		using Pair = std::pair<uint64_t, SamplerDesc>;
		VLA_UNSAFE_FORCE(Pair, samplers, material._samplers.size());
		for (unsigned c=0; c<material._samplers.size(); ++c)
			samplers[c] = {Hash64(material._samplers[c].first), material._samplers[c].second};

		auto materialMachine = std::make_shared<Techniques::ManualMaterialMachine>(material._uniforms, material._resources, MakeIteratorRange(samplers, &samplers[material._samplers.size()]));
		auto pipelineAccelerator = pool.CreatePipelineAccelerator(patchCollectionPtr, nullptr, material._selectors, inputAssembly, topology, material._stateSet);
		auto descriptorSetAccelerator = pool.CreateDescriptorSetAccelerator(nullptr, patchCollectionPtr, nullptr, materialMachine->GetMaterialMachine(), materialMachine, std::string{s_manualDrawables});
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		static UniformsStreamInterface MakeLocalTransformUSI()
		{
			UniformsStreamInterface result;
			result.BindImmediateData(0, Techniques::ObjectCB::LocalTransform);
			return result;
		}
		static UniformsStreamInterface s_localTransformUSI = MakeLocalTransformUSI();
	}

	IteratorRange<void*> ManualDrawableWriter::BuildDrawable(
		DrawablesPacket& pkt,
		size_t vertexCount)
	{
		// Ensure to call at least ConfigurePipeline before BuildDrawables. You may also call ConfigureDescriptorSet, but that is optional
		// ConfigurePipeline should be called only once over the lifetime of ManualDrawableWriter
		assert(_pipelineAccelerator);

		auto vbStorageRequest = pkt.AllocateStorage(DrawablesPacket::Storage::Vertex, vertexCount*_vertexStride);
		assert(vbStorageRequest._startOffset != ~0u);
		assert(vbStorageRequest._data.size() == vertexCount*_vertexStride);

		auto* geo = pkt.CreateTemporaryGeo();
		geo->_vertexStreamCount = 1;
		geo->_vertexStreams[0]._type = DrawableGeo::StreamType::PacketStorage;
		geo->_vertexStreams[0]._vbOffset = vbStorageRequest._startOffset;

		struct CustomDrawable : public RenderCore::Techniques::Drawable { unsigned _vertexCount; };
		auto* drawables = pkt._drawables.Allocate<CustomDrawable>(1);
		drawables[0]._pipeline = _pipelineAccelerator;
		drawables[0]._descriptorSet = _descriptorSetAccelerator;
		drawables[0]._geo = geo;
		drawables[0]._vertexCount = (unsigned)vertexCount;
		drawables[0]._drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
			{
				drawFnContext.Draw(((CustomDrawable&)drawable)._vertexCount);
			};

		return vbStorageRequest._data;
	}

	IteratorRange<void*> ManualDrawableWriter::BuildDrawable(
		DrawablesPacket& pkt,
		const Float4x4& localToWorld,
		size_t vertexCount)
	{
		// Ensure to call at least ConfigurePipeline before BuildDrawables. You may also call ConfigureDescriptorSet, but that is optional
		// ConfigurePipeline should be called only once over the lifetime of ManualDrawableWriter
		assert(_pipelineAccelerator);

		auto vbStorageRequest = pkt.AllocateStorage(DrawablesPacket::Storage::Vertex, vertexCount*_vertexStride);
		assert(vbStorageRequest._startOffset != ~0u);
		assert(vbStorageRequest._data.size() == vertexCount*_vertexStride);

		auto* geo = pkt.CreateTemporaryGeo();
		geo->_vertexStreamCount = 1;
		geo->_vertexStreams[0]._type = DrawableGeo::StreamType::PacketStorage;
		geo->_vertexStreams[0]._vbOffset = vbStorageRequest._startOffset;

		struct CustomDrawable : public RenderCore::Techniques::Drawable { Float4x4 _localToWorld; unsigned _vertexCount; };
		auto* drawables = pkt._drawables.Allocate<CustomDrawable>(1);
		drawables[0]._pipeline = _pipelineAccelerator;
		drawables[0]._descriptorSet = _descriptorSetAccelerator;
		drawables[0]._geo = geo;
		drawables[0]._vertexCount = (unsigned)vertexCount;
		drawables[0]._looseUniformsInterface = &Internal::s_localTransformUSI;
		drawables[0]._localToWorld = localToWorld;
		drawables[0]._drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
			{
				auto localTransform = RenderCore::Techniques::MakeLocalTransform(((CustomDrawable&)drawable)._localToWorld, ExtractTranslation(parsingContext.GetProjectionDesc()._cameraToWorld));
				drawFnContext.ApplyLooseUniforms(RenderCore::ImmediateDataStream(localTransform));
				drawFnContext.Draw(((CustomDrawable&)drawable)._vertexCount);
			};

		return vbStorageRequest._data;
	}

	auto ManualDrawableWriter::BuildDrawable(
		DrawablesPacket& pkt,
		size_t vertexCount, size_t indexCount) -> VertexAndIndexData
	{
		// Ensure to call at least ConfigurePipeline before BuildDrawables. You may also call ConfigureDescriptorSet, but that is optional
		// ConfigurePipeline should be called only once over the lifetime of ManualDrawableWriter
		assert(_pipelineAccelerator);

		auto vbStorageRequest = pkt.AllocateStorage(DrawablesPacket::Storage::Vertex, vertexCount*_vertexStride);
		assert(vbStorageRequest._startOffset != ~0u);
		assert(vbStorageRequest._data.size() == vertexCount*_vertexStride);

		auto ibStorageRequest = pkt.AllocateStorage(DrawablesPacket::Storage::Index, indexCount*sizeof(uint16_t));
		assert(ibStorageRequest._startOffset != ~0u);
		assert(ibStorageRequest._data.size() == indexCount*sizeof(uint16_t));

		auto* geo = pkt.CreateTemporaryGeo();
		geo->_vertexStreamCount = 1;
		geo->_vertexStreams[0]._type = DrawableGeo::StreamType::PacketStorage;
		geo->_vertexStreams[0]._vbOffset = vbStorageRequest._startOffset;
		geo->_ibStreamType = DrawableGeo::StreamType::PacketStorage;
		geo->_ibOffset = ibStorageRequest._startOffset;
		geo->_ibFormat = Format::R16_UINT;

		struct CustomDrawable : public RenderCore::Techniques::Drawable { unsigned _indexCount; };
		auto* drawables = pkt._drawables.Allocate<CustomDrawable>(1);
		drawables[0]._pipeline = _pipelineAccelerator;
		drawables[0]._descriptorSet = _descriptorSetAccelerator;
		drawables[0]._geo = geo;
		drawables[0]._indexCount = (unsigned)indexCount;
		drawables[0]._drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
			{
				drawFnContext.DrawIndexed(((CustomDrawable&)drawable)._indexCount);
			};

		return VertexAndIndexData { vbStorageRequest._data, ibStorageRequest._data.Cast<uint16_t*>() };
	}

	auto ManualDrawableWriter::BuildDrawable(
		DrawablesPacket& pkt,
		const Float4x4& localToWorld,
		size_t vertexCount, size_t indexCount) -> VertexAndIndexData
	{
		// Ensure to call at least ConfigurePipeline before BuildDrawables. You may also call ConfigureDescriptorSet, but that is optional
		// ConfigurePipeline should be called only once over the lifetime of ManualDrawableWriter
		assert(_pipelineAccelerator);

		auto vbStorageRequest = pkt.AllocateStorage(DrawablesPacket::Storage::Vertex, vertexCount*_vertexStride);
		assert(vbStorageRequest._startOffset != ~0u);
		assert(vbStorageRequest._data.size() == vertexCount*_vertexStride);

		auto ibStorageRequest = pkt.AllocateStorage(DrawablesPacket::Storage::Index, indexCount*sizeof(uint16_t));
		assert(ibStorageRequest._startOffset != ~0u);
		assert(ibStorageRequest._data.size() == indexCount*sizeof(uint16_t));

		auto* geo = pkt.CreateTemporaryGeo();
		geo->_vertexStreamCount = 1;
		geo->_vertexStreams[0]._type = DrawableGeo::StreamType::PacketStorage;
		geo->_vertexStreams[0]._vbOffset = vbStorageRequest._startOffset;
		geo->_ibStreamType = DrawableGeo::StreamType::PacketStorage;
		geo->_ibOffset = ibStorageRequest._startOffset;
		geo->_ibFormat = Format::R16_UINT;

		struct CustomDrawable : public RenderCore::Techniques::Drawable { Float4x4 _localToWorld; unsigned _indexCount; };
		auto* drawables = pkt._drawables.Allocate<CustomDrawable>(1);
		drawables[0]._pipeline = _pipelineAccelerator;
		drawables[0]._descriptorSet = _descriptorSetAccelerator;
		drawables[0]._geo = geo;
		drawables[0]._indexCount = (unsigned)indexCount;
		drawables[0]._looseUniformsInterface = &Internal::s_localTransformUSI;
		drawables[0]._localToWorld = localToWorld;
		drawables[0]._drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
			{
				auto localTransform = RenderCore::Techniques::MakeLocalTransform(((CustomDrawable&)drawable)._localToWorld, ExtractTranslation(parsingContext.GetProjectionDesc()._cameraToWorld));
				drawFnContext.ApplyLooseUniforms(RenderCore::ImmediateDataStream(localTransform));
				drawFnContext.DrawIndexed(((CustomDrawable&)drawable)._indexCount);
			};

		return VertexAndIndexData { vbStorageRequest._data, ibStorageRequest._data.Cast<uint16_t*>() };
	}

	ManualDrawableWriter& ManualDrawableWriter::ConfigurePipeline(
		IteratorRange<const MiniInputElementDesc*> inputAssembly,
		RenderCore::Topology topology)
	{
		// avoid calling ConfigurePipeline multiple times for the same ManualDrawableWriter
		assert(!_pipelineAccelerator);
		RenderCore::Assets::RenderStateSet stateSet{};
		_pipelineAccelerator = _pipelineAccelerators->CreatePipelineAccelerator(
			_shaderPatches, nullptr, _materialSelectors,
			inputAssembly, topology, stateSet).get();
		_vertexStride = CalculateVertexStride(inputAssembly);
		return *this;
	}

	ManualDrawableWriter& ManualDrawableWriter::ConfigurePipeline(
		PipelineAccelerator& pipeline,
		size_t vertexStride)
	{
		_pipelineAccelerator = &pipeline;
		_vertexStride = vertexStride;
		return *this;
	}

	ManualDrawableWriter& ManualDrawableWriter::ConfigureDescriptorSet(
		IteratorRange<Assets::ScaffoldCmdIterator> materialMachine,
		std::shared_ptr<void> memoryHolder)
	{
		// avoid calling ConfigureDescriptorSet multiple times for the same ManualDrawableWriter
		assert(!_descriptorSetAccelerator);

		_descriptorSetAccelerator = _pipelineAccelerators->CreateDescriptorSetAccelerator(
			nullptr, _shaderPatches, nullptr,
			materialMachine, std::move(memoryHolder),
			{}).get();
		return *this;
	}

	ManualDrawableWriter& ManualDrawableWriter::ConfigureDescriptorSet(
		DescriptorSetAccelerator& descSet)
	{
		assert(!_descriptorSetAccelerator);
		_descriptorSetAccelerator = &descSet;
		return *this;
	}

	ManualDrawableWriter::ManualDrawableWriter(std::shared_ptr<IPipelineAcceleratorPool> pipelineAccelerators)
	: _pipelineAccelerators(std::move(pipelineAccelerators))
	{}

	ManualDrawableWriter::ManualDrawableWriter(
		std::shared_ptr<IPipelineAcceleratorPool> pipelineAccelerators,
		std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> shaderPatches,
		ParameterBox&& materialSelectors)
	: _shaderPatches(std::move(shaderPatches))
	, _materialSelectors(std::move(materialSelectors))
	, _pipelineAccelerators(std::move(pipelineAccelerators))
	{}

	ManualDrawableWriter::~ManualDrawableWriter()
	{}

	MatMachineDecompositionHelper DecomposeMaterialMachine(IteratorRange<RenderCore::Assets::ScaffoldCmdIterator> matMachine)
	{
		MatMachineDecompositionHelper result;
		ParameterBox resHasParameters;
		for (auto cmd:matMachine) {
			if (cmd.Cmd() == (uint32_t)RenderCore::Assets::MaterialCommand::AttachPatchCollectionId) {
				assert(result._shaderPatchCollection == ~0u);
				result._shaderPatchCollection = *(const uint64_t*)cmd.RawData().begin();
			} else if (cmd.Cmd() == (uint32_t)RenderCore::Assets::MaterialCommand::AttachShaderResourceBindings) {
				assert(resHasParameters.GetCount() == 0);
				assert(!cmd.RawData().empty());
				auto& shaderResourceParameterBox = *(const ParameterBox*)cmd.RawData().begin();
				// Append the "RES_HAS_" constants for each resource that is both in the descriptor set and that we have a binding for
				for (const auto&r:shaderResourceParameterBox)
					resHasParameters.SetParameter(std::string{"RES_HAS_"} + r.Name().AsString(), 1);
			} else if (cmd.Cmd() == (uint32_t)RenderCore::Assets::MaterialCommand::AttachStateSet) {
				assert(cmd.RawData().size() == sizeof(RenderCore::Assets::RenderStateSet));
				result._stateSet = *(const RenderCore::Assets::RenderStateSet*)cmd.RawData().begin();
			} else if (cmd.Cmd() == (uint32_t)RenderCore::Assets::MaterialCommand::AttachSelectors) {
				assert(result._matSelectors.GetCount() == 0);
				assert(!cmd.RawData().empty());
				result._matSelectors = *(const ParameterBox*)cmd.RawData().begin();
			}
		}
		result._matSelectors.MergeIn(resHasParameters);
		return result;
	}

}}
