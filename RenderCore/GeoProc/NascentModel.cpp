// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NascentModel.h"
#include "NascentAnimController.h"
#include "GeometryAlgorithm.h"
#include "NascentObjectsSerialize.h"
#include "NascentCommandStream.h"
#include "MeshDatabase.h"
#include "../Assets/ModelCompilationConfiguration.h"
#include "../Assets/AssetUtils.h"
#include "../Assets/AnimationBindings.h"
#include "../Assets/ModelMachine.h"
#include "../../Math/MathSerialization.h"
#include "../../Math/Geometry.h"
#include "../../Assets/NascentChunk.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Utility/FastParseValue.h"
#include "../../Utility/StringFormat.h"
#include "../../Core/Exceptions.h"
#include <sstream>
#include <map>

using namespace Utility::Literals;

namespace RenderCore { namespace Assets { namespace GeoProc
{
	static const unsigned ModelScaffoldVersion = 1;
	static const unsigned ModelScaffoldLargeBlocksVersion = 0;

	auto NascentModel::FindGeometryBlock(NascentObjectGuid id) const -> const GeometryBlock*
	{
		for (auto&g:_geoBlocks)
			if (g.first == id)
				return &g.second;
		return nullptr;
	}

	auto NascentModel::FindSkinControllerBlock(NascentObjectGuid id) const -> const SkinControllerBlock*
	{
		for (auto&g:_skinBlocks)
			if (g.first == id)
				return &g.second;
		return nullptr;
	}

	auto NascentModel::FindCommand(NascentObjectGuid id) const ->  const Command* 
	{
		for (auto&g:_commands)
			if (g.first == id)
				return &g.second;
		return nullptr;
	}

	void NascentModel::Add(NascentObjectGuid id, GeometryBlock&& object)
	{
		if (FindGeometryBlock(id))
			Throw(std::runtime_error("Attempting to register a GeometryBlock for a id that is already in use"));
		if (id._namespaceId == 0)
			_nextAvailableNamespace0Id = std::max(id._objectId+1, _nextAvailableNamespace0Id);
		_geoBlocks.push_back(std::make_pair(id, std::move(object)));
	}

	void NascentModel::Add(NascentObjectGuid id, SkinControllerBlock&& object)
	{
		if (FindSkinControllerBlock(id))
			Throw(std::runtime_error("Attempting to register a SkinControllerBlock for a id that is already in use"));
		if (id._namespaceId == 0)
			_nextAvailableNamespace0Id = std::max(id._objectId+1, _nextAvailableNamespace0Id);
		_skinBlocks.push_back(std::make_pair(id, std::move(object)));
	}

	void NascentModel::Add(NascentObjectGuid id, Command&& object)
	{
		if (FindCommand(id))
			Throw(std::runtime_error("Attempting to register a Command for a id that is already in use"));
		if (id._namespaceId == 0)
			_nextAvailableNamespace0Id = std::max(id._objectId+1, _nextAvailableNamespace0Id);
		_commands.push_back(std::make_pair(id, std::move(object)));
	}

	void NascentModel::ApplyTransform(const std::string& bindingPoint, const Float4x4& transform)
	{
		for (auto&cmd:_commands) {
			if (cmd.second._localToModel == bindingPoint) {
				auto* geo = FindGeometryBlock(cmd.second._geometryBlock);
				assert(geo);
				Transform(*geo->_mesh, transform);
				cmd.second._localToModel = "identity";
			}
		}
	}

	std::vector<std::pair<std::string, std::string>> NascentModel::BuildSkeletonInterface() const
	{
		std::vector<std::pair<std::string, std::string>> result;
		for (const auto&cmd:_commands) {
			auto j = std::make_pair(std::string{}, cmd.second._localToModel);
			auto i = std::find(result.begin(), result.end(), j);
			if (i == result.end())
				result.push_back(j);
		}

		for (const auto&controller:_skinBlocks) {
			for (const auto&joint:controller.second._controller->GetJointNames()) {
				auto j = std::make_pair(controller.second._skeleton, joint);
				auto i = std::find(result.begin(), result.end(), j);
				if (i == result.end())
					result.push_back(j);
			}
		}
		return result;
	}

	static std::vector<DrawCallForGeoAlgorithm> BuildDrawCallsForGeoAlgorithm(const NascentModel::GeometryBlock& geoBlock)
	{
		std::vector<DrawCallForGeoAlgorithm> drawCalls;
		drawCalls.reserve(geoBlock._drawCalls.size());
		for (const auto& d:geoBlock._drawCalls) {
			DrawCallForGeoAlgorithm dc;
			dc._ibFormat = geoBlock._indexFormat;
			dc._topology = d._topology;
			auto bytesPerIndex = BitsPerPixel(geoBlock._indexFormat) / 8;
			dc._indices = MakeIteratorRange(
				PtrAdd(geoBlock._indices.data(), d._firstIndex * bytesPerIndex),
				PtrAdd(geoBlock._indices.data(), (d._firstIndex + d._indexCount) * bytesPerIndex));
			drawCalls.push_back(dc);
		}
		return drawCalls;
	}

	static void RemoveExcludedAttributes(const NascentModel::GeometryBlock& geoBlock, const ModelCompilationConfiguration::RawGeoRules& rules)
	{
		// remove attributes that have been excluded
		char buffer[32];
		for (auto a:rules._excludeAttributes) {
			for (unsigned str=0; str<geoBlock._mesh->GetStreams().size();) {
				StringMeldInPlace(buffer) << geoBlock._mesh->GetStreams()[str].GetSemanticName() << geoBlock._mesh->GetStreams()[str].GetSemanticIndex();
				if (Hash64(geoBlock._mesh->GetStreams()[str].GetSemanticName()) == a || Hash64(buffer) == a) {
					geoBlock._mesh->RemoveStream(str);
				} else {
					++str;
				}
			}
		}
	}

	static void BuildIncludedAttributes(const NascentModel::GeometryBlock& geoBlock, const ModelCompilationConfiguration::RawGeoRules& rules)
	{
		// Generate any attributes that we're required to add
		unsigned maxSemanticIndex = 0;
		for (const auto& str:geoBlock._mesh->GetStreams())
			maxSemanticIndex = std::max(maxSemanticIndex, str.GetSemanticIndex());

		if (rules._rebuildTangents.value_or(false))
			for (unsigned c=0; c<=maxSemanticIndex; ++c) {
				if (auto s = geoBlock._mesh->FindElement("TEXTANGENT", c); s != ~0u) geoBlock._mesh->RemoveStream(s);
				if (auto s = geoBlock._mesh->FindElement("TEXBITANGENT", c); s != ~0u) geoBlock._mesh->RemoveStream(s);
			}
		if (rules._rebuildNormals.value_or(false))
			for (unsigned c=0; c<=maxSemanticIndex; ++c)
				if (auto s = geoBlock._mesh->FindElement("NORMAL", c); s != ~0u) geoBlock._mesh->RemoveStream(s);
		
		char buffer[32];
		for (unsigned semanticIndex=0; semanticIndex<(maxSemanticIndex+1); ++semanticIndex) {
			std::vector<uint64_t> attributesToAddThisIndex;
			for (auto a:rules._includeAttributes) {
				bool foundAttribute = false;
				for (const auto& str:geoBlock._mesh->GetStreams()) {
					if (str.GetSemanticIndex() != semanticIndex) continue;
					StringMeldInPlace(buffer) << str.GetSemanticName() << str.GetSemanticIndex();
					foundAttribute |= Hash64(str.GetSemanticName()) == a || Hash64(buffer) == a;
					if (foundAttribute) break;
				}

				// foundAttribute is true if we found an existing attribute of the correct type for this semantic index
				if (!foundAttribute)
					attributesToAddThisIndex.push_back(a);
			}

			GenerateTangentFrameFlags::BitField genTangentFrameFlags = 0;
			for (auto a:rules._includeAttributes) {
				if (a == "NORMAL"_h || a == Hash64((StringMeldInPlace(buffer) << "NORMAL" << semanticIndex).AsStringSection()))
					if (geoBlock._mesh->FindElement("NORMAL", semanticIndex) == ~0u)
						genTangentFrameFlags |= GenerateTangentFrameFlags::Normals;
			}

			if (geoBlock._mesh->FindElement("TEXCOORD", semanticIndex) != ~0u) {
				for (auto a:rules._includeAttributes) {
					if (a == "TEXTANGENT"_h || a == Hash64((StringMeldInPlace(buffer) << "TEXTANGENT" << semanticIndex).AsStringSection()))
						if (geoBlock._mesh->FindElement("TEXTANGENT", semanticIndex) == ~0u)
							genTangentFrameFlags |= GenerateTangentFrameFlags::Tangents;
					if (a == "TEXBITANGENT"_h || a == Hash64((StringMeldInPlace(buffer) << "TEXBITANGENT" << semanticIndex).AsStringSection()))
						if (geoBlock._mesh->FindElement("TEXBITANGENT", semanticIndex) == ~0u)
							genTangentFrameFlags |= GenerateTangentFrameFlags::Bitangents;
				}
			}

			if (genTangentFrameFlags) {
				auto flatTriList = BuildFlatTriList(BuildDrawCallsForGeoAlgorithm(geoBlock));
				const float equivalenceThreshold = 1e-5f;
				GenerateTangentFrame(
					*geoBlock._mesh, semanticIndex, genTangentFrameFlags,
					flatTriList,
					equivalenceThreshold);
			}
		}
	}

	static std::vector<uint32_t> MergeDuplicateVertices(MeshDatabase& mesh)
	{
		// Some data paths (particularly getting Collada data from Blender, for example) result in excessive vertex duplication
		// We must counter this by merging vertices and vertex attributes that are identical (or near identical)
		// This can also trigger after excluding attributes
		// Also, we may want to consider converting into the native format before this, because some attributes may be identical
		// in the native format, even if they aren't in the original format...?

		std::vector<unsigned> newMapping;
		std::vector<uint32_t> convertedMapping;
		const float threshold = 1e-5f;
		
		for (unsigned streamIndex=0; streamIndex<mesh.GetStreams().size(); ++streamIndex) {
			auto& stream = mesh.GetStreams()[streamIndex];

			newMapping.clear();
			newMapping.reserve(stream.GetSourceData()->GetCount());
			convertedMapping.clear();

			auto srcData = stream.GetSourceData();

			// Remove bitwise identicals, because RemoveDuplicates() scales very poorly when there are a lot of nearby
			// vertices, so best to filter out large amounts of exactly identical input data
			auto newData = RemoveBitwiseIdenticals(newMapping, *srcData);
			if (newData && newData->GetCount() < stream.GetSourceData()->GetCount()) {
				if (!stream.GetVertexMap().empty()) {
					convertedMapping.insert(convertedMapping.end(), stream.GetVertexMap().begin(), stream.GetVertexMap().end());
					for (auto& a:convertedMapping)
						a = newMapping[a];
				} else {
					convertedMapping.insert(convertedMapping.end(), newMapping.begin(), newMapping.end());
				}
				srcData = std::move(newData);
			}

			newMapping.clear();
			newData = RemoveDuplicates(newMapping, *srcData, threshold);
			if (!newData || newData->GetCount() >= stream.GetSourceData()->GetCount())
				continue;

			if (convertedMapping.empty())
				convertedMapping.insert(convertedMapping.end(), stream.GetVertexMap().begin(), stream.GetVertexMap().end());
			if (!convertedMapping.empty()) {
				for (auto& a:convertedMapping)
					a = newMapping[a];
			} else {
				convertedMapping.insert(convertedMapping.end(), newMapping.begin(), newMapping.end());
			}

			mesh.InsertStream(streamIndex, std::move(newData), std::move(convertedMapping), stream.GetSemanticName().c_str(), stream.GetSemanticIndex());
			mesh.RemoveStream(streamIndex+1);
		}

		newMapping.clear();
		auto simplifiedMesh = RemoveDuplicates(newMapping, mesh);
		if (simplifiedMesh.GetUnifiedVertexCount() < mesh.GetUnifiedVertexCount()) {
			mesh = std::move(simplifiedMesh);
			return newMapping;
		}
		return {};
	}

	static NascentRawGeometry CompleteInstantiation(NascentModel::GeometryBlock& geoBlock, const ModelCompilationConfiguration::RawGeoRules& rules, bool buildTopologicalIndexBuffer)
	{
		RemoveExcludedAttributes(geoBlock, rules);

		if (rules._mergeDuplicateVertices.value_or(false)) {
			auto mergeMapping = MergeDuplicateVertices(*geoBlock._mesh);

			if (!mergeMapping.empty()) {
				// have to assume a densely packed index buffer here, because otherwise we'd have to deal with draw call overlaps
				std::vector<uint8_t> finalIndices = geoBlock._indices;
				RemapIndexBuffer(finalIndices, geoBlock._indices, mergeMapping, geoBlock._indexFormat);
				geoBlock._indices = std::move(finalIndices);

				if (!geoBlock._meshVertexIndexToSrcIndex.empty()) {
					for (auto&a:geoBlock._meshVertexIndexToSrcIndex)
						a = mergeMapping[a];
				} else {
					geoBlock._meshVertexIndexToSrcIndex = std::move(mergeMapping);
				}
			}
		}

		BuildIncludedAttributes(geoBlock, rules);

            // If we have normals, tangents & bitangents... then we can remove one of them
            // (it will be implied by the remaining two). We can choose to remove the 
            // normal or the bitangent... Lets' remove the binormal, because it makes it 
            // easier to do low quality rendering with normal maps turned off.
        const bool removeRedundantBitangents = true;
        if constexpr (removeRedundantBitangents)
            RemoveRedundantBitangents(*geoBlock._mesh);

		std::vector<uint8_t> adjacencyIndexBuffer;
		if (buildTopologicalIndexBuffer) {
			auto drawCalls = BuildDrawCallsForGeoAlgorithm(geoBlock);
			auto tempBuffer = BuildAdjacencyIndexBufferForUniquePositions(*geoBlock._mesh, MakeIteratorRange(drawCalls));
			adjacencyIndexBuffer = ConvertIndexBufferFormat(std::move(tempBuffer), geoBlock._indexFormat);
		}

        NativeVBLayout vbLayout = BuildDefaultLayout(*geoBlock._mesh, NativeVBSettings { rules._16BitNativeTypes.value_or(false) });
        auto nativeVB = geoBlock._mesh->BuildNativeVertexBuffer(vbLayout);

		std::vector<DrawCallDesc> drawCalls;
		for (const auto&d:geoBlock._drawCalls)
			drawCalls.push_back(DrawCallDesc{d._firstIndex, d._indexCount, 0, d._topology});

        return NascentRawGeometry {
            nativeVB, geoBlock._indices,
            RenderCore::Assets::CreateGeoInputAssembly(vbLayout._elements, (unsigned)vbLayout._vertexStride),
            geoBlock._indexFormat,
			drawCalls,
			geoBlock._geoSpaceToNodeSpace,
			geoBlock._mesh->GetUnifiedVertexCount(),
			geoBlock._meshVertexIndexToSrcIndex,
			std::move(adjacencyIndexBuffer) };
	}

	enum class CmdStreamMode { Normal, Topological };
	struct GeometrySerializationHelper
	{
		struct RawGeoEntry
		{
			NascentObjectGuid _srcGuid;
			NascentRawGeometry _geo;
			unsigned _id = ~0u, _topologicalId = ~0u;
			NascentRawGeometry::LargeResourceBlocks _blocks;
		};
		struct SkinnedGeoEntry
		{
			uint64_t _srcGuid;
			NascentBoundSkinnedGeometry _geo;
			unsigned _id = ~0u, _topologicalId = ~0u;
			NascentBoundSkinnedGeometry::LargeResourceBlocks _blocks;
		};
		std::vector<RawGeoEntry> _rawGeos;
		std::vector<SkinnedGeoEntry> _skinnedGeos;
		unsigned _nextId = 0;
	};

	struct CmdStreamSerializationHelper
	{
		unsigned RegisterInputInterfaceMarker(const std::string& skeleton, const std::string& name)
		{
			auto j = std::make_pair(skeleton, name);
			auto existing = std::find(_inputInterfaceNames.begin(), _inputInterfaceNames.end(), j);
			if (existing != _inputInterfaceNames.end()) {
				return (unsigned)std::distance(_inputInterfaceNames.begin(), existing);
			}

			auto result = (unsigned)_inputInterfaceNames.size();
			_inputInterfaceNames.push_back({skeleton, name});
			return result;
		}

		std::vector<uint64_t> BuildHashedInputInterface() const
		{
			std::vector<uint64_t> hashedInterface;
			hashedInterface.reserve(_inputInterfaceNames.size());
			for (const auto&j:_inputInterfaceNames) hashedInterface.push_back(HashCombine(Hash64(j.first), Hash64(j.second)));
			return hashedInterface;
		}

		void RegisterHashPair(uint64_t hash, StringSection<> str)
		{
			_dehashTable.insert(std::make_pair(hash, str.AsString()));
		}

		std::string TryDehash(uint64_t hashValue) const
		{
			auto i = _dehashTable.find(hashValue);
			if (i != _dehashTable.end())
				return i->second;
			return {};
		}

		std::vector<std::pair<std::string, std::string>>	_inputInterfaceNames;
		std::map<uint64_t, std::string> _dehashTable;
	};

    static std::ostream& SerializationOperator(std::ostream& stream, const GeometrySerializationHelper& geos)
    {
        stream << " --- Geos:" << std::endl;
        for (const auto& g:geos._rawGeos)
            stream << "[" << g._id << "] (0x" << std::hex << g._srcGuid._objectId << std::dec << ") Geo --- " << std::endl << g._geo << std::endl;

        stream << " --- Skinned Geos:" << std::endl;
        for (const auto& g:geos._skinnedGeos)
            stream << "[" << g._id << "] (0x" << std::hex << g._srcGuid << std::dec << ") Skinned geo --- " << std::endl << g._geo << std::endl;
        return stream;
    }

	static void TraceCommandStream(std::ostream& stream, IteratorRange<ScaffoldCmdIterator> cmdStream, const CmdStreamSerializationHelper* dehashHelper = nullptr)
	{
		for (auto cmd:cmdStream) {
			switch (cmd.Cmd()) {
			case (uint32_t)Assets::ModelCommand::GeoCall:
				{
					auto& geoCallDesc = cmd.As<Assets::GeoCallDesc>();
					stream << "Geo call (" << geoCallDesc._geoId << ")" << std::endl;
				}
				break;

			case (uint32_t)Assets::ModelCommand::SetMaterialAssignments:
				{
					stream << "Material assignments (";
					bool pendingComma = false;
					for (auto m:cmd.RawData().Cast<const uint64_t*>()) {
						if (pendingComma) stream << ", ";
						pendingComma = true;
						stream << std::hex << "0x" << m;
						if (std::string dehashed; dehashHelper && !(dehashed = dehashHelper->TryDehash(m)).empty())
							stream << " [" << dehashed << "]";
					}
					stream << ")" << std::dec << std::endl;
				}
				break;

			case (uint32_t)Assets::ModelCommand::SetGroups:
				{
					stream << "Groups (";
					bool pendingComma = false;
					for (auto m:cmd.RawData().Cast<const uint64_t*>()) {
						if (pendingComma) stream << ", ";
						pendingComma = true;
						stream << std::hex << "0x" << m;
						if (std::string dehashed; dehashHelper && !(dehashed = dehashHelper->TryDehash(m)).empty())
							stream << " [" << dehashed << "]";
					}
					stream << ")" << std::dec << std::endl;
				}
				break;

			case (uint32_t)Assets::ModelCommand::SetTransformMarker:
				stream << "Transform marker (" << cmd.As<unsigned>() << ")" << std::endl;
				break;

			case (uint32_t)Assets::ModelCommand::InputInterface:
				{
					auto jointNameHashes = cmd.RawData().Cast<const uint64_t*>();
					stream << "Input interface" << std::endl;
					if (dehashHelper) {
						// the full strings are not stored in the cmd stream itself
						assert(dehashHelper->_inputInterfaceNames.size() == jointNameHashes.size());
						for (unsigned c=0; c<jointNameHashes.size(); ++c) {
							assert(HashCombine(Hash64(dehashHelper->_inputInterfaceNames[c].first), Hash64(dehashHelper->_inputInterfaceNames[c].second)) == jointNameHashes[c]);
							stream << "  [" << c << "] " << dehashHelper->_inputInterfaceNames[c].first << " : " << dehashHelper->_inputInterfaceNames[c].second << ", Hashed: 0x" << std::hex << jointNameHashes[c] << std::dec << std::endl;
						}

					} else
						for (unsigned c=0; c<jointNameHashes.size(); ++c)
							stream << "  [" << c << "] 0x" << std::hex << jointNameHashes[c] << std::dec << std::endl;
				}
				break;

			default:
				stream << "Unknown command (" << cmd.Cmd() << ")" << std::endl;
			}
		}
	}

	static void TraceMetrics(std::ostream& stream, const GeometrySerializationHelper& geoObjects, IteratorRange<const ::Assets::BlockSerializer*> cmdStreams, const NascentSkeleton& skeleton, IteratorRange<const CmdStreamSerializationHelper*> dehashHelpers)
	{
		stream << "============== Geometry Objects ==============" << std::endl;
		stream << geoObjects;
		stream << std::endl;
		stream << "============== Command stream ==============" << std::endl;
		for (unsigned c=0; c<cmdStreams.size(); ++c) {
			stream << "Command stream [" << c << "]" << std::endl;
			auto block = cmdStreams[c].AsMemoryBlock();
			::Assets::Block_Initialize(block.get());
			auto* start = ::Assets::Block_GetFirstObject(block.get());
			auto range = MakeIteratorRange(start, (const void*)PtrAdd(start, cmdStreams[c].SizePrimaryBlock()));
			if (c < dehashHelpers.size()) {
				TraceCommandStream(stream, MakeScaffoldCmdRange(range), &dehashHelpers[c]);
			} else {
				TraceCommandStream(stream, MakeScaffoldCmdRange(range));
			}
		}
		stream << std::endl;
		stream << "============== Transformation Machine ==============" << std::endl;
		SerializationOperator(stream, skeleton.GetSkeletonMachine());
	}

	static uint64_t HashOfGeoAndSkinControllerIds(const NascentModel::Command& cmd)
	{
		uint64_t result = HashCombine(cmd._geometryBlock._objectId, cmd._geometryBlock._namespaceId);
		for (const auto&ctrl:cmd._skinControllerBlocks) {
			result = HashCombine(ctrl._objectId, result);
			result = HashCombine(ctrl._namespaceId, result);
		}
		return result;
	}

	static ModelCompilationConfiguration::RawGeoRules BuildNativeVBSettings(const ModelCompilationConfiguration& modelCompilationConfiguration, StringSection<> name)
	{
		return modelCompilationConfiguration.MatchRawGeoRules(name);
	}

	static uint64_t HashOrNumber(StringSection<> str, CmdStreamSerializationHelper& helper)
	{
		uint64_t guid;
		const char* parseEnd = FastParseValue(str, guid);
		if (parseEnd == str.end()) {
			return guid;
		} else {
			auto h = Hash64(str);
			helper.RegisterHashPair(h, str);
			return h;
		}
	}

	std::vector<::Assets::SerializedArtifact> NascentModel::SerializeToChunks(
		const std::string& name,
		const NascentSkeleton& embeddedSkeleton,
		const ModelCompilationConfiguration& modelCompilationConfiguration) const
	{
		::Assets::BlockSerializer serializer;
		auto recall = serializer.CreateRecall(sizeof(unsigned));
		
		CmdStreamSerializationHelper mainStreamHelper;
		bool assignedMainStream = false;
		std::vector<::Assets::BlockSerializer> generatedCmdStreams;
		std::vector<CmdStreamSerializationHelper> cmdStreamDehashHelpers;
		GeometrySerializationHelper geoObjects;
		std::vector<std::pair<uint64_t, CmdStreamMode>> cmdStreams;
		cmdStreams.reserve(modelCompilationConfiguration._commandStreams.size());
		for (const auto& s:modelCompilationConfiguration._commandStreams) {
			auto i = std::find_if(cmdStreams.begin(), cmdStreams.end(), [q=s.first](const auto& c) { return c.first == q; });
			if (i != cmdStreams.end()) continue;		// dupe
			CmdStreamMode mode = (s.first == "adjacency"_h) ? CmdStreamMode::Topological : CmdStreamMode::Normal;
			cmdStreams.emplace_back(s.first, mode);
		}
		if (cmdStreams.empty())
			cmdStreams.emplace_back(0, CmdStreamMode::Normal);

		bool buildTopologicalIndexBuffers = std::find_if(cmdStreams.begin(), cmdStreams.end(), [](const auto& q) { return q.second == CmdStreamMode::Topological; }) != cmdStreams.end();
		for (auto cmdStream:cmdStreams) {
			::Assets::BlockSerializer cmdStreamSerializer;
			CmdStreamSerializationHelper helper;
			bool isTopologicalStream = cmdStream.second == CmdStreamMode::Topological;

			std::optional<unsigned> currentTransformMarker;
			using MaterialGuid = uint64_t;
			std::optional<std::vector<MaterialGuid>> currentMaterialAssignment;
			std::optional<std::vector<MaterialGuid>> currentGroups;

			for (const auto&cmd:_commands) {
				auto* geoBlock = FindGeometryBlock(cmd.second._geometryBlock);
				if (!geoBlock)
					Throw(std::runtime_error("Missing geometry block referenced by command list in NascentModel::SerializeToChunks"));

				// the number of material assignments in the cmd must match the number of draw calls in
				// the geometry block (ie the material binding symbols is parallel to the draw calls array)
				assert(geoBlock->_drawCalls.size() == cmd.second._materialBindingSymbols.size());

				std::vector<MaterialGuid> materials;
				materials.reserve(cmd.second._materialBindingSymbols.size());
				for (const auto&mat:cmd.second._materialBindingSymbols)
					materials.push_back(HashOrNumber(mat, helper));
				std::vector<uint64_t> groupGuids;
				groupGuids.reserve(cmd.second._groups.size());
				for (const auto&grp:cmd.second._groups)
					groupGuids.push_back(HashOrNumber(grp, helper));
				std::sort(groupGuids.begin(), groupGuids.end());
				groupGuids.erase(std::unique(groupGuids.begin(), groupGuids.end()), groupGuids.end());

				auto localToWorld = helper.RegisterInputInterfaceMarker({}, cmd.second._localToModel);

				if (!currentTransformMarker.has_value() || localToWorld != currentTransformMarker.value()) {
					cmdStreamSerializer << MakeCmdAndRawData(ModelCommand::SetTransformMarker, localToWorld);
					currentTransformMarker = localToWorld;
				}
				if (!currentMaterialAssignment || *currentMaterialAssignment != materials) {
					cmdStreamSerializer << MakeCmdAndRanged(ModelCommand::SetMaterialAssignments, materials);
					currentMaterialAssignment = std::move(materials);
				}
				if (!currentGroups || *currentGroups != groupGuids) {
					cmdStreamSerializer << MakeCmdAndRanged(ModelCommand::SetGroups, groupGuids);
					currentGroups = std::move(groupGuids);
				}

				if (cmd.second._skinControllerBlocks.empty()) {
					auto i = std::find_if(geoObjects._rawGeos.begin(), geoObjects._rawGeos.end(),
						[&cmd](const auto& p) { return p._srcGuid == cmd.second._geometryBlock; });
					if (i == geoObjects._rawGeos.end()) {
						// Convert GeometryBlock format into NascentRawGeometry, which is an intermediate format very similar to what
						// we're about to serialize out
						auto rules = BuildNativeVBSettings(modelCompilationConfiguration, geoBlock->_rulesLabel);
						auto rawGeo = CompleteInstantiation(*const_cast<GeometryBlock*>(geoBlock), rules, buildTopologicalIndexBuffers);
						geoObjects._rawGeos.push_back({cmd.second._geometryBlock, std::move(rawGeo)});
						i = geoObjects._rawGeos.end()-1;
					}

					if (!isTopologicalStream) {
						if (i->_id == ~0u) i->_id = geoObjects._nextId++;
						cmdStreamSerializer << MakeCmdAndRawData(ModelCommand::GeoCall, i->_id);
					} else {
						if (i->_topologicalId == ~0u) i->_topologicalId = geoObjects._nextId++;
						cmdStreamSerializer << MakeCmdAndRawData(ModelCommand::GeoCall, i->_topologicalId);
					}
				} else {
					auto hashedId = HashOfGeoAndSkinControllerIds(cmd.second);
					auto i = std::find_if(geoObjects._skinnedGeos.begin(), geoObjects._skinnedGeos.end(),
						[hashedId](const auto& p) { return p._srcGuid == hashedId; });
					if (i == geoObjects._skinnedGeos.end()) {
						auto rules = BuildNativeVBSettings(modelCompilationConfiguration, geoBlock->_rulesLabel);
						auto rawGeo = CompleteInstantiation(*const_cast<GeometryBlock*>(geoBlock), rules, buildTopologicalIndexBuffers);

						std::vector<UnboundSkinControllerAndJointMatrices> controllers;
						controllers.reserve(cmd.second._skinControllerBlocks.size());
						for (auto ctrllerId:cmd.second._skinControllerBlocks) {
							const auto* controllerBlock = FindSkinControllerBlock(ctrllerId);
							assert(controllerBlock);
							const auto& controller = *controllerBlock->_controller;

							std::vector<uint16_t> jointMatrices(controller.GetJointNames().size());
							for (unsigned c=0; c<controller.GetJointNames().size(); ++c)
								jointMatrices[c] = (uint16_t)helper.RegisterInputInterfaceMarker(controllerBlock->_skeleton, controller.GetJointNames()[c]);

							controllers.emplace_back(UnboundSkinControllerAndJointMatrices { &controller, std::move(jointMatrices) });
						}

						auto boundController = BindController(std::move(rawGeo), MakeIteratorRange(controllers), "");
						geoObjects._skinnedGeos.push_back({hashedId, std::move(boundController)});
						i = geoObjects._skinnedGeos.end()-1;
					}

					assert(currentMaterialAssignment.value().size() == i->_geo._unanimatedBase._mainDrawCalls.size());

					if (!isTopologicalStream) {
						if (i->_id == ~0u) i->_id = geoObjects._nextId++;
						cmdStreamSerializer << MakeCmdAndRawData(ModelCommand::GeoCall, i->_id);
					} else {
						if (i->_topologicalId == ~0u) i->_topologicalId = geoObjects._nextId++;
						cmdStreamSerializer << MakeCmdAndRawData(ModelCommand::GeoCall, i->_topologicalId);
					}
				}
			}

			auto hashedInterface = helper.BuildHashedInputInterface();
			cmdStreamSerializer << CmdAndRawData{(uint32_t)ModelCommand::InputInterface, hashedInterface};

			serializer << (uint32_t)Assets::ScaffoldCommand::ModelCommandStream;
			serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t) + sizeof(uint64_t));
			serializer << cmdStream.first;
			serializer << cmdStreamSerializer.SizePrimaryBlock();
			serializer.SerializeSubBlock(cmdStreamSerializer);

			generatedCmdStreams.emplace_back(std::move(cmdStreamSerializer));
			#if defined(_DEBUG)
				cmdStreamDehashHelpers.emplace_back(CmdStreamSerializationHelper{helper});
			#endif

			if (!assignedMainStream) {
				mainStreamHelper = std::move(helper);		// always assigned to the first cmdStream
				assignedMainStream = true;
			}
		}

		// "large resources" --> created from the objects in geoObjects
		auto largeResourcesBlock = std::make_shared<std::vector<uint8_t>>();
		{
			LargeResourceBlockConstructor largeResourcesConstructor;

			// Write all of the vertex buffers, then all of the index buffers, then all of the topological index buffers (since this will promote more efficient loading)
			std::sort(
				geoObjects._rawGeos.begin(), geoObjects._rawGeos.end(),
				[](const auto& lhs, const auto& rhs) {
					if (lhs._id < rhs._id) return true;
					if (lhs._id > rhs._id) return false;
					return lhs._topologicalId < rhs._topologicalId;
				});
			std::sort(
				geoObjects._skinnedGeos.begin(), geoObjects._skinnedGeos.end(),
				[](const auto& lhs, const auto& rhs) {
					if (lhs._id < rhs._id) return true;
					if (lhs._id > rhs._id) return false;
					return lhs._topologicalId < rhs._topologicalId;
				});

			// VBs
			for (auto& geo:geoObjects._rawGeos)
				geo._blocks._vb = largeResourcesConstructor.AddBlock(geo._geo._vertices);
			for (auto& geo:geoObjects._skinnedGeos)
				geo._blocks._vb = largeResourcesConstructor.AddBlock(geo._geo._unanimatedBase._vertices);
			for (auto& geo:geoObjects._skinnedGeos)
				geo._blocks._animatedVertexElements = largeResourcesConstructor.AddBlock(geo._geo._animatedVertexElements);

			// IBs
			for (auto& geo:geoObjects._rawGeos)
				geo._blocks._ib = largeResourcesConstructor.AddBlock(geo._geo._indices);
			for (auto& geo:geoObjects._skinnedGeos)
				geo._blocks._ib = largeResourcesConstructor.AddBlock(geo._geo._unanimatedBase._indices);

			// Topological IBs
			for (auto& geo:geoObjects._rawGeos)
				geo._blocks._topologicalIb = largeResourcesConstructor.AddBlock(geo._geo._adjacencyIndices);
			for (auto& geo:geoObjects._skinnedGeos)
				geo._blocks._topologicalIb = largeResourcesConstructor.AddBlock(geo._geo._unanimatedBase._adjacencyIndices);

			// Skeleton binding
			for (auto& geo:geoObjects._skinnedGeos)
				geo._blocks._skeletonBinding = largeResourcesConstructor.AddBlock(geo._geo._skeletonBinding);

			for (unsigned c=0; c<geoObjects._nextId; ++c) {
				::Assets::BlockSerializer tempBlock;

				if (auto i = std::find_if(geoObjects._rawGeos.begin(), geoObjects._rawGeos.end(), [c](const auto&q) { return q._id == c; }); i != geoObjects._rawGeos.end()) {
					i->_geo.SerializeWithResourceBlock(tempBlock, i->_blocks);
					serializer << (uint32_t)Assets::ScaffoldCommand::Geo;
					serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t));
					serializer << tempBlock.SizePrimaryBlock();
					serializer.SerializeSubBlock(tempBlock);
					continue;
				}

				if (auto i = std::find_if(geoObjects._skinnedGeos.begin(), geoObjects._skinnedGeos.end(), [c](const auto&q) { return q._id == c; }); i != geoObjects._skinnedGeos.end()) {
					::Assets::BlockSerializer tempBlock;
					i->_geo.SerializeWithResourceBlock(tempBlock, i->_blocks);
					serializer << (uint32_t)Assets::ScaffoldCommand::Geo;
					serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t));
					serializer << tempBlock.SizePrimaryBlock();
					serializer.SerializeSubBlock(tempBlock);
				}

				if (auto i = std::find_if(geoObjects._rawGeos.begin(), geoObjects._rawGeos.end(), [c](const auto&q) { return q._topologicalId == c; }); i != geoObjects._rawGeos.end()) {
					i->_geo.SerializeTopologicalWithResourceBlock(tempBlock, i->_blocks);
					serializer << (uint32_t)Assets::ScaffoldCommand::Geo;
					serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t));
					serializer << tempBlock.SizePrimaryBlock();
					serializer.SerializeSubBlock(tempBlock);
					continue;
				}

				if (auto i = std::find_if(geoObjects._skinnedGeos.begin(), geoObjects._skinnedGeos.end(), [c](const auto&q) { return q._topologicalId == c; }); i != geoObjects._skinnedGeos.end()) {
					::Assets::BlockSerializer tempBlock;
					i->_geo.SerializeTopologicalWithResourceBlock(tempBlock, i->_blocks);
					serializer << (uint32_t)Assets::ScaffoldCommand::Geo;
					serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t));
					serializer << tempBlock.SizePrimaryBlock();
					serializer.SerializeSubBlock(tempBlock);
				}
			}

			largeResourcesBlock->resize(largeResourcesConstructor.CalculateSize());
			auto i = largeResourcesBlock->begin();
			for (const auto& e:largeResourcesConstructor._elements) {
				std::memcpy(AsPointer(i), e.begin(), e.size());
				i += e.size();
			}
			assert(i == largeResourcesBlock->end());
		}

		{
			::Assets::BlockSerializer tempBlock;
			tempBlock << embeddedSkeleton;

			serializer << (uint32_t)Assets::ScaffoldCommand::Skeleton;
			serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t));
			serializer << tempBlock.SizePrimaryBlock();
			serializer.SerializeSubBlock(tempBlock);
		}

		{
			auto defaultPoseData = CalculateDefaultPoseData(embeddedSkeleton, geoObjects, mainStreamHelper);
			serializer << MakeCmdAndSerializable(ScaffoldCommand::DefaultPoseData, defaultPoseData);
		}

		{
			ModelRootData rootData;
			rootData._maxLOD = 0;
			serializer << MakeCmdAndSerializable(ScaffoldCommand::ModelRootData, rootData);
		}

		serializer.PushSizeValueAtRecall(recall);

		// SerializationOperator human-readable metrics information
		std::stringstream metricsStream;
		TraceMetrics(metricsStream, geoObjects, generatedCmdStreams, embeddedSkeleton, cmdStreamDehashHelpers);

		auto scaffoldBlock = ::Assets::AsBlob(serializer);
		auto metricsBlock = ::Assets::AsBlob(metricsStream);

		return
			{
				::Assets::SerializedArtifact{
					RenderCore::Assets::ChunkType_ModelScaffold, ModelScaffoldVersion, name,
					std::move(scaffoldBlock)},
				::Assets::SerializedArtifact{
					RenderCore::Assets::ChunkType_ModelScaffoldLargeBlocks, ModelScaffoldLargeBlocksVersion, name,
					std::move(largeResourcesBlock)},
				::Assets::SerializedArtifact{
					RenderCore::Assets::ChunkType_Metrics, 0, "skin-" + name, 
					std::move(metricsBlock)}
			};
	}

	ModelDefaultPoseData NascentModel::CalculateDefaultPoseData(
        const NascentSkeleton& skeleton,
        const GeometrySerializationHelper& geoObjects,
		const CmdStreamSerializationHelper& helper) const
    {
        ModelDefaultPoseData result;

        auto skeletonOutput = skeleton.GetSkeletonMachine().GenerateOutputTransforms();

        auto skelOutputInterface = skeleton.GetSkeletonMachine().BuildHashedOutputInterface();
        auto streamInputInterface = helper.BuildHashedInputInterface();
        RenderCore::Assets::SkeletonBinding skelBinding(
            RenderCore::Assets::SkeletonMachine::OutputInterface{AsPointer(skelOutputInterface.begin()), skelOutputInterface.size()},
			MakeIteratorRange(streamInputInterface));

        auto finalMatrixCount = (unsigned)streamInputInterface.size(); // immData->_visualScene.GetInputInterface()._jointCount;
        result._defaultTransforms.resize(finalMatrixCount);
        for (unsigned c=0; c<finalMatrixCount; ++c) {
            auto machineOutputIndex = skelBinding.ModelJointToMachineOutput(c);
            if (machineOutputIndex == ~unsigned(0x0)) {
                result._defaultTransforms[c] = Identity<Float4x4>();
            } else {
                result._defaultTransforms[c] = skeletonOutput[machineOutputIndex];
            }
        }

            // if we have any non-identity internal transforms, then we should 
            // write a default set of transformations. But many models don't have any
            // internal transforms -- in this case all of the generated transforms
            // will be identity. If we find this case, they we should write zero
            // default transforms.
        bool hasNonIdentity = false;
        const float tolerance = 1e-6f;
        for (unsigned c=0; c<finalMatrixCount; ++c)
            hasNonIdentity |= !Equivalent(result._defaultTransforms[c], Identity<Float4x4>(), tolerance);
        if (!hasNonIdentity) {
            finalMatrixCount = 0u;
            result._defaultTransforms.erase(result._defaultTransforms.begin(), result._defaultTransforms.end());
        }

		// calculate bounding box
		{
			//
			//      For all the parts of the model, calculate the bounding box.
			//      We just have to go through each vertex in the model, and
			//      transform it into model space, and calculate the min and max values
			//      found
			//		We could do this with the mesh databases in GeoBlock, but we've
			//		also got the converted geo
			//
			auto boundingBox = InvalidBoundingBox();
			auto helperCopy = helper;
			for (const auto&cmd:_commands) {
				auto localToWorldId = helperCopy.RegisterInputInterfaceMarker({}, cmd.second._localToModel);
				Float4x4 localToWorld = Identity<Float4x4>();
				if (localToWorldId < result._defaultTransforms.size())
					localToWorld = result._defaultTransforms[localToWorldId];

				if (cmd.second._skinControllerBlocks.empty()) {
					auto i = std::find_if(geoObjects._rawGeos.begin(), geoObjects._rawGeos.end(), [id=cmd.second._geometryBlock](const auto& q) { return q._srcGuid == id; });
					if (i == geoObjects._rawGeos.end()) continue;

					localToWorld = Combine(i->_geo._geoSpaceToNodeSpace, localToWorld);

					auto positionDesc = FindPositionElement(i->_geo._mainDrawInputAssembly._elements);
					const auto vertexStride = i->_geo._mainDrawInputAssembly._vertexStride;
					if (positionDesc._format != Format::Unknown && vertexStride) {
						auto positions = MakeVertexIteratorRangeConst(i->_geo._vertices, positionDesc._alignedByteOffset, vertexStride, positionDesc._format);
						assert(positions.size() == MakeVertexIteratorRangeConst(i->_geo._vertices, vertexStride, Format::R8_UNORM).size());
						for (auto v:positions)
							AddToBoundingBox(boundingBox, TransformPoint(localToWorld, Truncate(v.AsFloat4())));
					}
				} else {
					auto hashedId = HashOfGeoAndSkinControllerIds(cmd.second);
					auto i = std::find_if(geoObjects._skinnedGeos.begin(), geoObjects._skinnedGeos.end(), [hashedId](const auto& q) { return q._srcGuid == hashedId; });
					if (i == geoObjects._skinnedGeos.end()) continue;

					localToWorld = Combine(i->_geo._unanimatedBase._geoSpaceToNodeSpace, localToWorld);

					//  We can't get the vertex position data directly from the vertex buffer, because
					//  the "bound" object is already using an opaque hardware object. However, we can
					//  transform the local space bounding box and use that.

					const unsigned indices[][3] = 
					{
						{0,0,0}, {0,1,0}, {1,0,0}, {1,1,0},
						{0,0,1}, {0,1,1}, {1,0,1}, {1,1,1}
					};

					const Float3* A = (const Float3*)&i->_geo._localBoundingBox.first;
					for (unsigned c=0; c<dimof(indices); ++c) {
						Float3 position(A[indices[c][0]][0], A[indices[c][1]][1], A[indices[c][2]][2]);
						AddToBoundingBox(boundingBox, TransformPoint(localToWorld, position));
					}
				}
			}

			assert(!std::isinf(boundingBox.first[0]) && !std::isinf(boundingBox.first[1]) && !std::isinf(boundingBox.first[2]));
			assert(!std::isinf(boundingBox.second[0]) && !std::isinf(boundingBox.second[1]) && !std::isinf(boundingBox.second[2]));
			result._boundingBox = boundingBox;
		}

        return result;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	bool ModelTransMachineOptimizer::CanBakeIntoOutputMatrix(unsigned outputMatrixIndex) const
    {
        if (outputMatrixIndex < unsigned(_canMergeIntoTransform.size()))
            return _canMergeIntoTransform[outputMatrixIndex];
        return false;
    }

    void ModelTransMachineOptimizer::BakeIntoOutputMatrix(unsigned outputMatrixIndex, const Float4x4& transform)
    {
        assert(CanBakeIntoOutputMatrix(outputMatrixIndex));
        _mergedTransforms[outputMatrixIndex] = Combine(
            _mergedTransforms[outputMatrixIndex], transform);
    }

    ModelTransMachineOptimizer::ModelTransMachineOptimizer(
		const NascentModel& model,
		IteratorRange<const std::pair<std::string, std::string>*> bindingNameInterface,
		bool allowTransformBake)
	: _bindingNameInterface(bindingNameInterface.begin(), bindingNameInterface.end())
    {
		auto outputMatrixCount = bindingNameInterface.size();
        _canMergeIntoTransform.resize(outputMatrixCount, false);
        _mergedTransforms.resize(outputMatrixCount, Identity<Float4x4>());

		if (allowTransformBake) {
			for (unsigned c=0; c<outputMatrixCount; ++c) {

				if (!bindingNameInterface[c].first.empty()) continue;

				bool skinAttached = false;
				bool doublyAttachedObject = false;
				bool atLeastOneAttached = false;
				for (const auto&cmd:model.GetCommands())
					if (cmd.second._localToModel == bindingNameInterface[c].second) {
						atLeastOneAttached = true;

						// if we've got a skin controller attached, we can't do any merging
						skinAttached |= !cmd.second._skinControllerBlocks.empty();

						// find all of the meshes attached, and check if any are attached in
						// multiple places
						for (const auto&cmd2:model.GetCommands())
							doublyAttachedObject |= cmd2.second._geometryBlock == cmd.second._geometryBlock && cmd2.second._localToModel != cmd.second._localToModel;
					}

				_canMergeIntoTransform[c] = atLeastOneAttached && !skinAttached && !doublyAttachedObject;
			}
		}
    }

    ModelTransMachineOptimizer::ModelTransMachineOptimizer() {}

    ModelTransMachineOptimizer::~ModelTransMachineOptimizer()
    {}

	void OptimizeSkeleton(NascentSkeleton& embeddedSkeleton, NascentModel& model, const RenderCore::Assets::ModelCompilationConfiguration::SkeletonRules& skeletonRules)
	{
		if (!skeletonRules._preserveAllOutputs.value_or(false)) {
			std::vector<std::pair<std::string, uint64_t>> filteringSkeleInterface;
			for (auto q:model.BuildSkeletonInterface())
				filteringSkeleInterface.emplace_back(q.first, Hash64(q.second));
			filteringSkeleInterface.insert(filteringSkeleInterface.begin(), std::make_pair(std::string{}, Hash64("identity")));
			for (auto s:skeletonRules._preserveOutputs)
				filteringSkeleInterface.emplace_back(std::string{}, s);
			embeddedSkeleton.GetSkeletonMachine().FilterOutputInterface(MakeIteratorRange(filteringSkeleInterface));
		}

		if (!skeletonRules._preserveAllParameters.value_or(false)) {
			// parameters will only survive if they are specified in the preserveParameters list
			embeddedSkeleton.GetSkeletonMachine().FilterParameterInterface(skeletonRules._preserveParameters);
		}

		if (skeletonRules._optimize.value_or(true)) {
			auto finalSkeleInterface = embeddedSkeleton.GetSkeletonMachine().GetOutputInterface();
			ModelTransMachineOptimizer optimizer(model, finalSkeleInterface, skeletonRules._bakeStaticTransforms.value_or(true));
			embeddedSkeleton.GetSkeletonMachine().Optimize(optimizer);
			assert(embeddedSkeleton.GetSkeletonMachine().GetOutputMatrixCount() == finalSkeleInterface.size());

			for (unsigned c=0; c<finalSkeleInterface.size(); ++c) {
				const auto& mat = optimizer.GetMergedOutputMatrices()[c];
				if (!Equivalent(mat, Identity<Float4x4>(), 1e-3f)) {
					assert(finalSkeleInterface[c].first.empty());	// this operation only makes sense for the basic structure skeleton
					model.ApplyTransform(finalSkeleInterface[c].second, mat);
				}
			}
		}
	}

}}}
