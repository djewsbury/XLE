// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelScaffold.h"
#include "AssetUtils.h"
#include "ModelMachine.h"
#include "../../Assets/ChunkFileContainer.h"
#include "../../Math/MathSerialization.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/PtrUtils.h"

namespace RenderCore { namespace Assets
{
	static const unsigned ModelScaffoldVersion = 1;
	static const unsigned ModelScaffoldLargeBlocksVersion = 0;

//////////////////////////////////////////////////////////////////////////////////////////////////

	std::pair<Float3, Float3> ModelScaffold::GetStaticBoundingBox(unsigned lodIndex) const
	{
		if (!_defaultPoseData) return {Zero<Float3>(), Zero<Float3>()};
		return _defaultPoseData->_boundingBox;
	}

	unsigned ModelScaffold::GetMaxLOD() const
	{
		if (!_rootData) return 0;
		return _rootData->_maxLOD;
	}

	const ::Assets::ArtifactRequest ModelScaffold::ChunkRequests[2]
	{
		::Assets::ArtifactRequest { "Scaffold", ChunkType_ModelScaffold, ModelScaffoldVersion, ::Assets::ArtifactRequest::DataType::BlockSerializer },
		::Assets::ArtifactRequest { "LargeBlocks", ChunkType_ModelScaffoldLargeBlocks, ModelScaffoldLargeBlocksVersion, ::Assets::ArtifactRequest::DataType::ReopenFunction }
	};

	IteratorRange<ScaffoldCmdIterator> ModelScaffold::GetOuterCommandStream() const
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

	IteratorRange<const uint64_t*>  ModelScaffold::FindCommandStreamInputInterface() const
	{
		for (auto cmd:CommandStream())
			if (cmd.Cmd() == (uint32_t)Assets::ModelCommand::InputInterface)
				return cmd.RawData().Cast<const uint64_t*>();
		return {};
	}

	std::shared_ptr<::Assets::IFileInterface> ModelScaffold::OpenLargeBlocks() const { return _largeBlocksReopen(); }

	IteratorRange<ScaffoldCmdIterator> ModelScaffold::CommandStream(uint64_t cmdStreamId) const
	{
		auto i = LowerBound(_commandStreams, cmdStreamId);
		if (i != _commandStreams.end() && i->first == cmdStreamId)
			return i->second;
		return {};
	}
	std::vector<uint64_t> ModelScaffold::CollateCommandStreams() const
	{
		std::vector<uint64_t> result;
		result.reserve(_commandStreams.size());
		for (const auto& q:_commandStreams) result.push_back(q.first);
		return result;
	}

	ModelScaffold::ModelScaffold() {}
	ModelScaffold::ModelScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: _depVal{depVal}
	{
		assert(chunks.size() == 2);
		_rawMemoryBlock = std::move(chunks[0]._buffer);
		_rawMemoryBlockSize = chunks[0]._bufferSize;
		_largeBlocksReopen = std::move(chunks[1]._reopenFunction);

		for (auto cmd:GetOuterCommandStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)ScaffoldCommand::Geo:
				{
					struct GeoRef { size_t _dataSize; const void* _data; };
					auto ref = cmd.As<GeoRef>();
					auto machine = Assets::MakeScaffoldCmdRange(MakeIteratorRange(ref._data, PtrAdd(ref._data, ref._dataSize)));
					_geoMachines.push_back(machine);
				}
				break;

			case (uint32_t)ScaffoldCommand::ModelCommandStream:
				{
					struct StreamRef { uint64_t _guid; size_t _dataSize; const void* _data; };
					auto ref = cmd.As<StreamRef>();
					_commandStreams.insert(
						LowerBound(_commandStreams, ref._guid),
						std::make_pair(ref._guid, Assets::MakeScaffoldCmdRange(MakeIteratorRange(ref._data, PtrAdd(ref._data, ref._dataSize)))));
				}
				break;

			case (uint32_t)ScaffoldCommand::DefaultPoseData:
				assert(cmd.BlockSize() == sizeof(ModelDefaultPoseData));
				_defaultPoseData = (const ModelDefaultPoseData*)cmd.RawData().begin();
				break;

			case (uint32_t)ScaffoldCommand::ModelRootData:
				assert(cmd.BlockSize() == sizeof(ModelRootData));
				_rootData = (const ModelRootData*)cmd.RawData().begin();
				break;

			case (uint32_t)ScaffoldCommand::Skeleton:
				{
					struct SkeletonRef { size_t _dataSize; const SkeletonMachine* _data; };
					assert(!_embeddedSkeleton);
					_embeddedSkeleton = cmd.As<SkeletonRef>()._data;
				}
				break;
			}
		}
	}
	ModelScaffold::~ModelScaffold() {}

//////////////////////////////////////////////////////////////////////////////////////////////////

	class SupplementGeo
	{
	public:
		unsigned			_geoId;
		GeoInputAssembly	_vbIA;
	};

	class ModelSupplementImmutableData
	{
	public:
		SupplementGeo*  _geos;
		size_t          _geoCount;
	};

	const ModelSupplementImmutableData&   ModelSupplementScaffold::ImmutableData() const                
	{
		return *(const ModelSupplementImmutableData*)::Assets::Block_GetFirstObject(_rawMemoryBlock.get());
	}

	std::shared_ptr<::Assets::IFileInterface>	ModelSupplementScaffold::OpenLargeBlocks() const { return _largeBlocksReopen(); }

	const ::Assets::ArtifactRequest ModelSupplementScaffold::ChunkRequests[]
	{
		::Assets::ArtifactRequest { "Scaffold", ChunkType_ModelScaffold, 0, ::Assets::ArtifactRequest::DataType::BlockSerializer },
		::Assets::ArtifactRequest { "LargeBlocks", ChunkType_ModelScaffoldLargeBlocks, 0, ::Assets::ArtifactRequest::DataType::ReopenFunction }
	};
	
	ModelSupplementScaffold::ModelSupplementScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		assert(chunks.size() == 2);
		_rawMemoryBlock = std::move(chunks[0]._buffer);
		_largeBlocksReopen = chunks[1]._reopenFunction;
	}

	ModelSupplementScaffold::ModelSupplementScaffold(ModelSupplementScaffold&& moveFrom) never_throws
	: _rawMemoryBlock(std::move(moveFrom._rawMemoryBlock))
	, _largeBlocksReopen(std::move(moveFrom._largeBlocksReopen))
	, _depVal(std::move(moveFrom._depVal))
	{}

	ModelSupplementScaffold& ModelSupplementScaffold::operator=(ModelSupplementScaffold&& moveFrom) never_throws
	{
		if (_rawMemoryBlock)
			ImmutableData().~ModelSupplementImmutableData();
		_rawMemoryBlock = std::move(moveFrom._rawMemoryBlock);
		_largeBlocksReopen = std::move(moveFrom._largeBlocksReopen);
		_depVal = std::move(moveFrom._depVal);
		return *this;
	}

	ModelSupplementScaffold::ModelSupplementScaffold() {}

	ModelSupplementScaffold::~ModelSupplementScaffold()
	{
		if (_rawMemoryBlock)
			ImmutableData().~ModelSupplementImmutableData();
	}

	void SerializationOperator(::Assets::BlockSerializer& serializer, const ModelDefaultPoseData& defaultPoseData)
	{
		serializer << defaultPoseData._defaultTransforms;
		serializer << defaultPoseData._boundingBox;
	}

	void SerializationOperator(::Assets::BlockSerializer& serializer, const ModelRootData& rootData)
	{
		serializer << rootData._maxLOD;
	}

}}
