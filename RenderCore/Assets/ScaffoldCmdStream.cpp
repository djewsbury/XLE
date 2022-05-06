// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ScaffoldCmdStream.h"
#include "ModelMachine.h"
#include "AssetUtils.h"
#include "../../Assets/Assets.h"
#include "../../Assets/ContinuationUtil.h"

namespace RenderCore { namespace Assets
{
	
#if 0
	class RendererConstruction : public IScaffoldNavigation
	{
	public:
		virtual IteratorRange<ScaffoldCmdIterator> GetSubModel() override
		{
			return {};
		}

		virtual IteratorRange<ScaffoldCmdIterator> GetGeoMachine(GeoId geoId) override
		{
			auto i = LowerBound(_geoMachines, geoId);
			if (i != _geoMachines.end() && i->first == geoId)
				return MakeScaffoldCmdRange(i->second, *this);
			return {};
		}

		virtual IteratorRange<ScaffoldCmdIterator> GetMaterialMachine(MaterialId) override
		{
			assert(0);
			return {};
		}

		virtual const ShaderPatchCollection* GetShaderPatchCollection(ShaderPatchCollectionId) override
		{
			assert(0);
			return nullptr;
		}

		virtual const IteratorRange<const void*> GetGeometryBufferData(GeoId, GeoBufferType) override
		{
			assert(0);
			return {};
		}

		RendererConstruction(std::shared_ptr<ScaffoldAsset> scaffoldAsset)
		: _scaffoldAsset(std::move(scaffoldAsset))
		{
			auto cmdStream = _scaffoldAsset->GetCmdStream();
			// pass through the cmd stream once and pull out important data
			for (auto cmd:cmdStream) {
				switch (cmd.Cmd()) {
				case (uint32_t)ModelCommand::Geo:
					{
						auto data = cmd.RawData();
						assert(data.size() > sizeof(uint32_t));
						_geoMachines.emplace_back(
							*(uint32_t*)data.begin(),
							VoidRange{PtrAdd(data.begin(), sizeof(uint32_t)), data.end()});
					}
					break;
				default:
					break;
				}
			}

			std::sort(_geoMachines.begin(), _geoMachines.end());
		}

	private:
		std::shared_ptr<ScaffoldAsset> _scaffoldAsset;

		using VoidRange = IteratorRange<const void*>;
		std::vector<std::pair<GeoId, VoidRange>> _geoMachines;
		VoidRange _firstSubModel;
	};

	std::shared_ptr<IScaffoldNavigation> CreateSimpleRendererConstruction(std::shared_ptr<ScaffoldAsset> scaffoldAsset)
	{
		return std::make_shared<RendererConstruction>(std::move(scaffoldAsset));
	}
#endif

	auto RendererConstruction::Element::SetModelScaffold(StringSection<> initializer) -> Element&
	{
		assert(_internal);
		SetModelScaffold(::Assets::MakeAsset<Internal::ModelScaffoldPtr>(initializer));
		return *this;
	}
	auto RendererConstruction::Element::SetMaterialScaffold(StringSection<>) -> Element& { return *this; }
	auto RendererConstruction::Element::SetModelScaffold(const ::Assets::PtrToMarkerPtr<ScaffoldAsset>& scaffoldMarker) -> Element&
	{
		assert(_internal);
		auto i = LowerBound(_internal->_modelScaffoldMarkers, _elementId);
		if (i != _internal->_modelScaffoldMarkers.end() && i->first == _elementId) {
			i->second = scaffoldMarker;
		} else
			_internal->_modelScaffoldMarkers.insert(i, {_elementId, scaffoldMarker});
		return *this;
	}
	auto RendererConstruction::Element::SetMaterialScaffold(const ::Assets::PtrToMarkerPtr<ScaffoldAsset>&) -> Element& { return *this; }
	auto RendererConstruction::Element::SetModelScaffold(const std::shared_ptr<ScaffoldAsset>& scaffoldPtr) -> Element& 
	{
		assert(_internal);
		auto i = LowerBound(_internal->_modelScaffoldPtrs, _elementId);
		if (i != _internal->_modelScaffoldPtrs.end() && i->first == _elementId) {
			i->second = scaffoldPtr;
		} else
			_internal->_modelScaffoldPtrs.insert(i, {_elementId, scaffoldPtr});
		return *this; 
	}
	auto RendererConstruction::Element::SetMaterialScaffold(const std::shared_ptr<ScaffoldAsset>&) -> Element& { return *this; }

	auto RendererConstruction::Element::AddMorphTarget(uint64_t targetName, StringSection<> srcFile) -> Element& { return *this; }

	auto RendererConstruction::Element::SetRootTransform(const Float4x4&) -> Element& { return *this; }

	auto RendererConstruction::Element::SetName(const std::string& name) -> Element&
	{ 
		assert(_internal);
		auto i = LowerBound(_internal->_names, _elementId);
		if (i != _internal->_names.end() && i->first == _elementId) {
			i->second = name;
		} else
			_internal->_names.insert(i, {_elementId, name});
		return *this; 
	}

	std::future<std::shared_ptr<RendererConstruction>> RendererConstruction::ReadyFuture()
	{
		std::promise<std::shared_ptr<RendererConstruction>> promise;
		auto result = promise.get_future();

		auto strongThis = shared_from_this();
		assert(strongThis);
		::Assets::PollToPromise(
			std::move(promise),
			[strongThis](auto timeout) {
				// wait until all pending scaffold markers are finished
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				for (auto& f:strongThis->_internal->_modelScaffoldMarkers) {
					auto remainingTime = timeoutTime - std::chrono::steady_clock::now();
					if (remainingTime.count() <= 0) return ::Assets::PollStatus::Continue;
					auto t = f.second->StallWhilePending(std::chrono::duration_cast<std::chrono::microseconds>(remainingTime));
					if (t.value_or(::Assets::AssetState::Pending) == ::Assets::AssetState::Pending)
						return ::Assets::PollStatus::Continue;
				}
				return ::Assets::PollStatus::Finish;
			},
			[strongThis]() {
				assert(strongThis->GetAssetState() != ::Assets::AssetState::Pending);
				return strongThis;
			});

		return result;
	}

	::Assets::AssetState RendererConstruction::GetAssetState() const
	{
		bool hasPending = false;
		for (auto& f:_internal->_modelScaffoldMarkers) {
			auto state = f.second->GetAssetState();
			if (state == ::Assets::AssetState::Invalid)
				return ::Assets::AssetState::Invalid;
			hasPending |= state == ::Assets::AssetState::Pending;
		}
		return hasPending ? ::Assets::AssetState::Pending : ::Assets::AssetState::Ready;
	}

	auto RendererConstruction::AddElement() -> Element
	{
		auto result = Element{_internal->_elementCount, *_internal.get()};
		++_internal->_elementCount;
		return result;
	}

	RendererConstruction::RendererConstruction()
	{
		_internal = std::make_unique<Internal>();
	}
	RendererConstruction::~RendererConstruction() {}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static const unsigned ModelScaffoldVersion = 1;
    static const unsigned ModelScaffoldLargeBlocksVersion = 0;

	const ::Assets::ArtifactRequest ScaffoldAsset::ChunkRequests[2]
	{
		::Assets::ArtifactRequest { "Scaffold", ChunkType_ModelScaffold, ModelScaffoldVersion, ::Assets::ArtifactRequest::DataType::BlockSerializer },
		::Assets::ArtifactRequest { "LargeBlocks", ChunkType_ModelScaffoldLargeBlocks, ModelScaffoldLargeBlocksVersion, ::Assets::ArtifactRequest::DataType::ReopenFunction }
	};

	IteratorRange<ScaffoldCmdIterator> ScaffoldAsset::GetCmdStream() const
	{
		if (_rawMemoryBlockSize <= sizeof(uint32_t)) return {};
		auto* firstObject = ::Assets::Block_GetFirstObject(_rawMemoryBlock.get());
		auto streamSize = *(const uint32_t*)firstObject;
		assert((streamSize + sizeof(uint32_t)) <= _rawMemoryBlockSize);
		auto* start = PtrAdd(firstObject, sizeof(uint32_t));
		auto* end = (const void*)PtrAdd(firstObject, sizeof(uint32_t)+streamSize);
		return {
			ScaffoldCmdIterator{MakeIteratorRange(start, end)},
			ScaffoldCmdIterator{MakeIteratorRange(end, end)}};
	}

	std::shared_ptr<::Assets::IFileInterface> ScaffoldAsset::OpenLargeBlocks() const { return _largeBlocksReopen(); }

	ScaffoldAsset::ScaffoldAsset() {}
	ScaffoldAsset::ScaffoldAsset(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: _depVal{depVal}
	{
		assert(chunks.size() == 2);
		_rawMemoryBlock = std::move(chunks[0]._buffer);
		_rawMemoryBlockSize = chunks[0]._bufferSize;
		_largeBlocksReopen = std::move(chunks[1]._reopenFunction);
		
	}
	ScaffoldAsset::~ScaffoldAsset() {}

}}
