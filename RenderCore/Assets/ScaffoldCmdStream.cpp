// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ScaffoldCmdStream.h"
#include "ModelMachine.h"
#include "AssetUtils.h"

namespace RenderCore { namespace Assets
{
	
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
