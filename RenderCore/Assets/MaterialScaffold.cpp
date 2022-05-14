// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MaterialScaffold.h"
#include "ShaderPatchCollection.h"
#include "MaterialMachine.h"
#include "../../Assets/Assets.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Utility/Streams/SerializationUtils.h"

namespace RenderCore { namespace Assets
{
	const ::Assets::ArtifactRequest MaterialScaffold::ChunkRequests[]
	{
		::Assets::ArtifactRequest{
			"Scaffold", ChunkType_ResolvedMat, ResolvedMat_ExpectedVersion,
			::Assets::ArtifactRequest::DataType::BlockSerializer
		}
	};

	auto				MaterialScaffold::GetMaterialMachine(MaterialGuid guid) const -> Machine
	{
		auto i = LowerBound(_materialMachines, guid);
		if (i != _materialMachines.end() && i->first == guid)
			return i->second;
		return {};
	}

	std::vector<uint64_t> MaterialScaffold::GetMaterials() const
	{
		std::vector<uint64_t> result;
		result.reserve(_materialMachines.size());
		for (auto& c:_materialMachines) result.push_back(c.first);
		return result;
	}

	StringSection<>		MaterialScaffold::DehashMaterialName(MaterialGuid guid) const
	{
		if (_resolvedNamesBlock.empty()) return {};

		auto* resolvedNames = (const SerializableVector<std::pair<MaterialGuid, SerializableVector<char>>>*)_resolvedNamesBlock.begin();
		auto i = std::lower_bound(resolvedNames->begin(), resolvedNames->end(), guid, CompareFirst<MaterialGuid, SerializableVector<char>>{});
		if (i != resolvedNames->end() && i->first == guid)
			return MakeStringSection(i->second.begin(), i->second.end());
		return {};
	}

	std::shared_ptr<ShaderPatchCollection> MaterialScaffold::GetShaderPatchCollection(uint64_t hash) const
	{
		auto i = std::lower_bound(_patchCollections.begin(), _patchCollections.end(), hash, [](const auto& q, auto lhs) { return q->GetHash() < lhs; });
		if (i != _patchCollections.end() && (*i)->GetHash() == hash)
			return *i;
		return nullptr;
	}

	IteratorRange<ScaffoldCmdIterator> MaterialScaffold::GetOuterCommandStream() const
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

	MaterialScaffold::MaterialScaffold()
	{}

	MaterialScaffold::MaterialScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		assert(chunks.size() == 1);
		_rawMemoryBlock = std::move(chunks[0]._buffer);
		_rawMemoryBlockSize = chunks[0]._bufferSize;

		for (auto cmd:GetOuterCommandStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)ScaffoldCommand::Material:
				{
					struct MaterialRef { uint64_t _hashId; size_t _dataSize; const void* _data; };
					auto ref = cmd.As<MaterialRef>();
					auto machine = Assets::MakeScaffoldCmdRange(MakeIteratorRange(ref._data, PtrAdd(ref._data, ref._dataSize)));
					auto i = LowerBound(_materialMachines, ref._hashId);
					if (i == _materialMachines.end() || i->first != ref._hashId) {
						_materialMachines.insert(i, std::make_pair(ref._hashId, machine));
					} else
						i->second = machine;
				}
				break;
			case (uint32_t)Assets::ScaffoldCommand::ShaderPatchCollection:
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
			case (uint32_t)Assets::ScaffoldCommand::MaterialNameDehash:
				_resolvedNamesBlock = cmd.RawData();
				break;
			}
		}
	}

	MaterialScaffold::~MaterialScaffold()
	{}

}}

