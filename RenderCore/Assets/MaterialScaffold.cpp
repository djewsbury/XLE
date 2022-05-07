// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MaterialScaffold.h"
#include "ShaderPatchCollection.h"
#include "../../Assets/ChunkFileContainer.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Assets/Assets.h"
#include "../../Utility/Streams/PathUtils.h"

#include "ModelMachine.h"

namespace RenderCore { namespace Assets
{
	class MaterialImmutableData
    {
    public:
        SerializableVector<std::pair<MaterialGuid, MaterialScaffold::Material>> _materials;
        SerializableVector<std::pair<MaterialGuid, SerializableVector<char>>> _materialNames;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

	const MaterialImmutableData& MaterialScaffold::ImmutableData() const
	{
		return *(const MaterialImmutableData*)::Assets::Block_GetFirstObject(_rawMemoryBlock.get());
	}

	auto MaterialScaffold::GetMaterial(MaterialGuid guid) const -> const Material*
	{
		const auto& data = ImmutableData();
		auto i = std::lower_bound(data._materials.begin(), data._materials.end(), guid, CompareFirst<MaterialGuid, Material>());
		if (i != data._materials.end() && i->first == guid)
			return &i->second;
		return nullptr;
	}

	StringSection<> MaterialScaffold::GetMaterialName(MaterialGuid guid) const
	{
		const auto& data = ImmutableData();
		auto i = std::lower_bound(data._materialNames.begin(), data._materialNames.end(), guid, CompareFirst<MaterialGuid, SerializableVector<char>>());
		if (i != data._materialNames.end() && i->first == guid)
			return MakeStringSection(i->second.begin(), i->second.end());
		return {};
	}

	std::shared_ptr<ShaderPatchCollection> MaterialScaffold::GetShaderPatchCollection(uint64_t hash) const
	{
		auto i = std::find_if(
			_patchCollections.begin(), _patchCollections.end(), 
			[hash](const auto& c) { return c->GetHash() == hash; });
		if (i != _patchCollections.end())
			return *i;
		return {};
	}

	const ::Assets::ArtifactRequest MaterialScaffold::ChunkRequests[]
	{
		::Assets::ArtifactRequest{
			"Scaffold", ChunkType_ResolvedMat, ResolvedMat_ExpectedVersion,
			::Assets::ArtifactRequest::DataType::BlockSerializer
		},
		::Assets::ArtifactRequest{
			"PatchCollections", ChunkType_PatchCollections, ResolvedMat_ExpectedVersion,
			::Assets::ArtifactRequest::DataType::Raw
		}
	};

	MaterialScaffold::MaterialScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		assert(chunks.size() == 2);
		_rawMemoryBlock = std::move(chunks[0]._buffer);

		InputStreamFormatter<utf8> formatter(
			StringSection<utf8>{(const utf8*)chunks[1]._buffer.get(), (const utf8*)PtrAdd(chunks[1]._buffer.get(), chunks[1]._bufferSize)});
		auto patchCollections = DeserializeShaderPatchCollectionSet(formatter, {}, depVal);
		_patchCollections.reserve(patchCollections.size());
		for (const auto&p:patchCollections)
			_patchCollections.push_back(std::make_shared<ShaderPatchCollection>(p));
	}

	MaterialScaffold::MaterialScaffold(MaterialScaffold&& moveFrom) never_throws
	: _rawMemoryBlock(std::move(moveFrom._rawMemoryBlock))
	, _depVal(std::move(moveFrom._depVal))
	, _patchCollections(std::move(moveFrom._patchCollections))
	{}

	MaterialScaffold& MaterialScaffold::operator=(MaterialScaffold&& moveFrom) never_throws
	{
		ImmutableData().~MaterialImmutableData();
		_rawMemoryBlock = std::move(moveFrom._rawMemoryBlock);
		_depVal = std::move(moveFrom._depVal);
		_patchCollections = std::move(moveFrom._patchCollections);
		return *this;
	}

	MaterialScaffold::~MaterialScaffold()
	{
		ImmutableData().~MaterialImmutableData();
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	const ::Assets::ArtifactRequest MaterialScaffoldCmdStreamForm::ChunkRequests[]
	{
		::Assets::ArtifactRequest{
			"Scaffold", ChunkType_ResolvedMat, ResolvedMat_ExpectedVersion,
			::Assets::ArtifactRequest::DataType::BlockSerializer
		}
	};

	auto				MaterialScaffoldCmdStreamForm::GetMaterialMachine(MaterialGuid guid) const -> Machine
	{
		auto i = LowerBound(_materialMachines, guid);
		if (i != _materialMachines.end() && i->first == guid)
			return i->second;
		return {};
	}

	StringSection<>		MaterialScaffoldCmdStreamForm::DehashMaterialName(MaterialGuid guid) const
	{
		if (!_resolvedNames) return {};
		auto i = std::lower_bound(_resolvedNames->begin(), _resolvedNames->end(), guid, CompareFirst<MaterialGuid, SerializableVector<char>>{});
		if (i != _resolvedNames->end() && i->first == guid)
			return MakeStringSection(i->second.begin(), i->second.end());
		return {};
	}

	std::shared_ptr<ShaderPatchCollection> MaterialScaffoldCmdStreamForm::GetShaderPatchCollection(uint64_t hash) const
	{
		auto i = std::lower_bound(_patchCollections.begin(), _patchCollections.end(), hash, [](const auto& q, auto lhs) { return q->GetHash() < lhs; });
		if (i != _patchCollections.end() && (*i)->GetHash() == hash)
			return *i;
		return nullptr;
	}

	MaterialScaffoldCmdStreamForm::MaterialScaffoldCmdStreamForm()
	{}

	static IteratorRange<Assets::ScaffoldCmdIterator> CreateMachine(const void* src)
	{
		auto size = *(const uint32_t*)src;
		return Assets::MakeScaffoldCmdRange({
			PtrAdd(src, sizeof(uint32_t)),
			PtrAdd(src, sizeof(uint32_t)+size)});
	}

	MaterialScaffoldCmdStreamForm::MaterialScaffoldCmdStreamForm(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: ScaffoldAsset(chunks, depVal)
	{
		for (auto cmd:GetCmdStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)ModelCommand::Material:
				{
					struct MaterialRef { uint64_t _hashId; const void* _data; };
					auto ref = cmd.As<MaterialRef>();
					auto i = LowerBound(_materialMachines, ref._hashId);
					if (i == _materialMachines.end() || i->first != ref._hashId) {
						_materialMachines.insert(i, std::make_pair(ref._hashId, CreateMachine(ref._data)));
					} else
						i->second = CreateMachine(ref._data);
				}
				break;
			case (uint32_t)Assets::ModelCommand::ShaderPatchCollection:
				{
					struct SPCRef { uint64_t _hashId; size_t _blockSize; const void* _serializedBlock; };
					auto ref = cmd.As<SPCRef>();
					auto i = std::lower_bound(_patchCollections.begin(), _patchCollections.end(), ref._hashId, [](const auto& q, auto lhs) { return q->GetHash() < lhs; });
					if (i == _patchCollections.end() || (*i)->GetHash() != ref._hashId) {
						// we have to deserialize via text format
						InputStreamFormatter<> inputFormatter{MakeIteratorRange(ref._serializedBlock, PtrAdd(ref._serializedBlock, ref._blockSize))};
						auto patchCollection = std::make_shared<Assets::ShaderPatchCollection>(inputFormatter, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});
						_patchCollections.insert(i, std::move(patchCollection));
					}
				}
				break;
			case (uint32_t)Assets::ModelCommand::MaterialNameDehash:
				_resolvedNames = (decltype(_resolvedNames))cmd.RawData().begin();
				break;
			}
		}
	}

	MaterialScaffoldCmdStreamForm::~MaterialScaffoldCmdStreamForm()
	{}

}}

