// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NascentModel.h"
#include "NascentAnimController.h"
#include "GeometryAlgorithm.h"
#include "NascentObjectsSerialize.h"
#include "NascentCommandStream.h"
#include "GeoProcUtil.h"
#include "MeshDatabase.h"
#include "../Assets/AssetUtils.h"
#include "../Assets/AnimationBindings.h"
#include "../Assets/ModelMachine.h"
#include "../../Math/MathSerialization.h"
#include "../../Assets/NascentChunk.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Utility/FastParseValue.h"
#include "../../Core/Exceptions.h"
#include <sstream>

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

	static NascentRawGeometry CompleteInstantiation(const NascentModel::GeometryBlock& geoBlock, const NativeVBSettings& nativeVBSettings)
	{
		const bool generateMissingTangentsAndNormals = true;
        if constexpr (generateMissingTangentsAndNormals) {
			auto indexCount = geoBlock._indices.size() * 8 / BitsPerPixel(geoBlock._indexFormat);
            GenerateNormalsAndTangents(
                *geoBlock._mesh, 0,
				1e-3f,
                geoBlock._indices.data(), indexCount, geoBlock._indexFormat);
        }

            // If we have normals, tangents & bitangents... then we can remove one of them
            // (it will be implied by the remaining two). We can choose to remove the 
            // normal or the bitangent... Lets' remove the binormal, because it makes it 
            // easier to do low quality rendering with normal maps turned off.
        const bool removeRedundantBitangents = true;
        if constexpr (removeRedundantBitangents)
            RemoveRedundantBitangents(*geoBlock._mesh);

		std::vector<uint8_t> adjacencyIndexBuffer;
		const bool buildTopologicalIndexBuffer = true;
		if constexpr (buildTopologicalIndexBuffer) {
			// note -- assuming Topology::TriangleList here (also that all indices are going to be read in order)
			auto indexCount = geoBlock._indices.size() * 8 / BitsPerPixel(geoBlock._indexFormat);
			adjacencyIndexBuffer = BuildAdjacencyIndexBuffer(
				*geoBlock._mesh,
				geoBlock._indices.data(), indexCount, geoBlock._indexFormat);
		}

        NativeVBLayout vbLayout = BuildDefaultLayout(*geoBlock._mesh, nativeVBSettings);
        auto nativeVB = geoBlock._mesh->BuildNativeVertexBuffer(vbLayout);

		std::vector<DrawCallDesc> drawCalls;
		for (const auto&d:geoBlock._drawCalls) {
			drawCalls.push_back(DrawCallDesc{d._firstIndex, d._indexCount, 0, d._topology});
		}

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
	struct NascentGeometryObjects
	{
		struct RawGeoEntry { NascentObjectGuid _srcGuid; CmdStreamMode _cmdStreamMode; NascentRawGeometry _geo; unsigned _id = ~0u; };
		struct SkinnedGeoEntry { uint64_t _srcGuid; CmdStreamMode _cmdStreamMode; NascentBoundSkinnedGeometry _geo; unsigned _id = ~0u; };
		std::vector<RawGeoEntry> _rawGeos;
		std::vector<SkinnedGeoEntry> _skinnedGeos;
		unsigned _nextId = 0;
	};

    static std::ostream& SerializationOperator(std::ostream& stream, const NascentGeometryObjects& geos)
    {
        stream << " --- Geos:" << std::endl;
        for (const auto& g:geos._rawGeos)
            stream << "[" << g._id << "] (0x" << std::hex << g._srcGuid._objectId << std::dec << ") Geo" << (g._cmdStreamMode == CmdStreamMode::Topological ? "[Topological]" : "") << " --- " << std::endl << g._geo << std::endl;

        stream << " --- Skinned Geos:" << std::endl;
        for (const auto& g:geos._skinnedGeos)
            stream << "[" << g._id << "] (0x" << std::hex << g._srcGuid << std::dec << ") Skinned geo" << (g._cmdStreamMode == CmdStreamMode::Topological ? "[Topological]" : "") << " --- " << std::endl << g._geo << std::endl;
        return stream;
    }

	static void TraceCommandStream(std::ostream& stream, IteratorRange<ScaffoldCmdIterator> cmdStream)
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
					}
					stream << ")" << std::dec << std::endl;
				}
				break;

			case (uint32_t)Assets::ModelCommand::SetTransformMarker:
				stream << "Transform marker (" << cmd.As<unsigned>() << ")" << std::endl;
				break;

			default:
				stream << "Unknown command (" << cmd.Cmd() << ")" << std::endl;
			}
		}
	}

	static void TraceMetrics(std::ostream& stream, const NascentGeometryObjects& geoObjects, IteratorRange<const ::Assets::BlockSerializer*> cmdStreams, const NascentSkeleton& skeleton)
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
			TraceCommandStream(stream, MakeScaffoldCmdRange(range));
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

		std::vector<std::pair<std::string, std::string>>	_inputInterfaceNames;
	};

	std::vector<::Assets::ICompileOperation::SerializedArtifact> NascentModel::SerializeToChunks(const std::string& name, const NascentSkeleton& embeddedSkeleton, const NativeVBSettings& nativeSettings) const
	{
		::Assets::BlockSerializer serializer;
		auto recall = serializer.CreateRecall(sizeof(unsigned));
		
		CmdStreamSerializationHelper mainStreamHelper;
		std::vector<::Assets::BlockSerializer> generatedCmdStreams;
		NascentGeometryObjects geoObjects;
		for (auto mode:{CmdStreamMode::Normal, CmdStreamMode::Topological}) {
			::Assets::BlockSerializer cmdStreamSerializer;
			CmdStreamSerializationHelper helper;

			std::optional<unsigned> currentTransformMarker;
			using MaterialGuid = uint64_t;
			std::optional<std::vector<MaterialGuid>> currentMaterialAssignment;

			for (const auto&cmd:_commands) {
				auto* geoBlock = FindGeometryBlock(cmd.second._geometryBlock);
				if (!geoBlock)
					Throw(std::runtime_error("Missing geometry block referenced by command list in NascentModel::SerializeToChunks"));

				std::vector<MaterialGuid> materials;
				materials.reserve(cmd.second._materialBindingSymbols.size());
				for (const auto&mat:cmd.second._materialBindingSymbols) {
					MaterialGuid guid = 0;
					const char* parseEnd = FastParseValue(MakeStringSection(mat), guid);
					if (parseEnd == AsPointer(mat.end())) {
						materials.push_back(guid);
					} else
						materials.push_back(Hash64(mat));
				}

				auto localToWorld = helper.RegisterInputInterfaceMarker({}, cmd.second._localToModel);

				if (!currentTransformMarker.has_value() || localToWorld != currentTransformMarker.value()) {
					cmdStreamSerializer << MakeCmdAndRawData(ModelCommand::SetTransformMarker, localToWorld);
					currentTransformMarker = localToWorld;
				}
				if (!currentMaterialAssignment || *currentMaterialAssignment != materials) {
					cmdStreamSerializer << MakeCmdAndRanged(ModelCommand::SetMaterialAssignments, materials);
					currentMaterialAssignment = std::move(materials);
				}

				if (cmd.second._skinControllerBlocks.empty()) {
					auto i = std::find_if(geoObjects._rawGeos.begin(), geoObjects._rawGeos.end(),
						[&cmd, mode](const auto& p) { return p._srcGuid == cmd.second._geometryBlock && p._cmdStreamMode == mode; });
					if (i == geoObjects._rawGeos.end()) {
						auto rawGeo = CompleteInstantiation(*geoBlock, nativeSettings);
						geoObjects._rawGeos.push_back({cmd.second._geometryBlock, mode, std::move(rawGeo), geoObjects._nextId++});
						i = geoObjects._rawGeos.end()-1;
					}

					cmdStreamSerializer << MakeCmdAndRawData(ModelCommand::GeoCall, i->_id);
				} else {
					auto hashedId = HashOfGeoAndSkinControllerIds(cmd.second);
					auto i = std::find_if(geoObjects._skinnedGeos.begin(), geoObjects._skinnedGeos.end(),
						[hashedId, mode](const auto& p) { return p._srcGuid == hashedId && p._cmdStreamMode == mode; });
					if (i == geoObjects._skinnedGeos.end()) {
						auto rawGeo = CompleteInstantiation(*geoBlock, nativeSettings);

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
						geoObjects._skinnedGeos.push_back({hashedId, mode, std::move(boundController), geoObjects._nextId++});
						i = geoObjects._skinnedGeos.end()-1;
					}

					assert(currentMaterialAssignment.value().size() == i->_geo._unanimatedBase._mainDrawCalls.size());
					cmdStreamSerializer << MakeCmdAndRawData(ModelCommand::GeoCall, i->_id);
				}
			}

			auto hashedInterface = helper.BuildHashedInputInterface();
			cmdStreamSerializer << CmdAndRawData{(uint32_t)ModelCommand::InputInterface, hashedInterface};

			serializer << (uint32_t)Assets::ScaffoldCommand::ModelCommandStream;
			serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t) + sizeof(uint64_t));
			if (mode == CmdStreamMode::Normal) {
				serializer << 0ull;	// default cmd stream id (s_CmdStreamGuid_Default)
			} else {
				assert(mode == CmdStreamMode::Topological);
				serializer << Hash64("adjacency");
			}
			serializer << cmdStreamSerializer.SizePrimaryBlock();
			serializer.SerializeSubBlock(cmdStreamSerializer);

			if (mode == CmdStreamMode::Normal)
				mainStreamHelper = std::move(helper);
			generatedCmdStreams.emplace_back(std::move(cmdStreamSerializer));
		}

		// "large resources" --> created from the objects in geoObjects
		auto largeResourcesBlock = std::make_shared<std::vector<uint8_t>>();
		{
			LargeResourceBlockConstructor largeResourcesConstructor;
			for (unsigned c=0; c<geoObjects._nextId; ++c) {
				auto i = std::find_if(geoObjects._rawGeos.begin(), geoObjects._rawGeos.end(), [c](const auto&q) { return q._id == c; });
				if (i != geoObjects._rawGeos.end()) {
					::Assets::BlockSerializer tempBlock;
					if (i->_cmdStreamMode == CmdStreamMode::Normal) {
						i->_geo.SerializeWithResourceBlock(tempBlock, largeResourcesConstructor);
					} else {
						assert(i->_cmdStreamMode == CmdStreamMode::Topological);
						i->_geo.SerializeTopologicalWithResourceBlock(tempBlock, largeResourcesConstructor);
					}

					serializer << (uint32_t)Assets::ScaffoldCommand::Geo;
					serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t));
					serializer << tempBlock.SizePrimaryBlock();
					serializer.SerializeSubBlock(tempBlock);
				} else {
					auto i2 = std::find_if(geoObjects._skinnedGeos.begin(), geoObjects._skinnedGeos.end(), [c](const auto&q) { return q._id == c; });
					assert(i2 != geoObjects._skinnedGeos.end());

					::Assets::BlockSerializer tempBlock;
					if (i2->_cmdStreamMode == CmdStreamMode::Normal) {
						i2->_geo.SerializeWithResourceBlock(tempBlock, largeResourcesConstructor);
					} else {
						assert(i2->_cmdStreamMode == CmdStreamMode::Topological);
						i2->_geo.SerializeTopologicalWithResourceBlock(tempBlock, largeResourcesConstructor);
					}

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
		TraceMetrics(metricsStream, geoObjects, generatedCmdStreams, embeddedSkeleton);

		auto scaffoldBlock = ::Assets::AsBlob(serializer);
		auto metricsBlock = ::Assets::AsBlob(metricsStream);

		return
			{
				::Assets::ICompileOperation::SerializedArtifact{
					RenderCore::Assets::ChunkType_ModelScaffold, ModelScaffoldVersion, name,
					std::move(scaffoldBlock)},
				::Assets::ICompileOperation::SerializedArtifact{
					RenderCore::Assets::ChunkType_ModelScaffoldLargeBlocks, ModelScaffoldLargeBlocksVersion, name,
					std::move(largeResourcesBlock)},
				::Assets::ICompileOperation::SerializedArtifact{
					RenderCore::Assets::ChunkType_Metrics, 0, "skin-" + name, 
					std::move(metricsBlock)}
			};
	}

	ModelDefaultPoseData NascentModel::CalculateDefaultPoseData(
        const NascentSkeleton& skeleton,
        const NascentGeometryObjects& geoObjects,
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
					auto i = std::find_if(geoObjects._rawGeos.begin(), geoObjects._rawGeos.end(), [id=cmd.second._geometryBlock](const auto& q) { return q._srcGuid == id && q._cmdStreamMode == CmdStreamMode::Normal; });
					if (i == geoObjects._rawGeos.end()) continue;

					localToWorld = Combine(i->_geo._geoSpaceToNodeSpace, localToWorld);

					const void*         vertexBuffer = i->_geo._vertices.data();
					const unsigned      vertexStride = i->_geo._mainDrawInputAssembly._vertexStride;

					auto positionDesc = FindPositionElement(
						AsPointer(i->_geo._mainDrawInputAssembly._elements.begin()),
						i->_geo._mainDrawInputAssembly._elements.size());

					if (positionDesc._nativeFormat != Format::Unknown && vertexStride) {
						AddToBoundingBox(
							boundingBox, vertexBuffer, vertexStride, 
							i->_geo._vertices.size() / vertexStride, positionDesc, localToWorld);
					}
				} else {
					auto hashedId = HashOfGeoAndSkinControllerIds(cmd.second);
					auto i = std::find_if(geoObjects._skinnedGeos.begin(), geoObjects._skinnedGeos.end(), [hashedId](const auto& q) { return q._srcGuid == hashedId && q._cmdStreamMode == CmdStreamMode::Normal; });
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
						AddToBoundingBox(boundingBox, position, localToWorld);
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

	bool ModelTransMachineOptimizer::CanMergeIntoOutputMatrix(unsigned outputMatrixIndex) const
    {
        if (outputMatrixIndex < unsigned(_canMergeIntoTransform.size()))
            return _canMergeIntoTransform[outputMatrixIndex];
        return false;
    }

    void ModelTransMachineOptimizer::MergeIntoOutputMatrix(unsigned outputMatrixIndex, const Float4x4& transform)
    {
        assert(CanMergeIntoOutputMatrix(outputMatrixIndex));
        _mergedTransforms[outputMatrixIndex] = Combine(
            _mergedTransforms[outputMatrixIndex], transform);
    }

    ModelTransMachineOptimizer::ModelTransMachineOptimizer(
		const NascentModel& model,
		IteratorRange<const std::pair<std::string, std::string>*> bindingNameInterface)
	: _bindingNameInterface(bindingNameInterface.begin(), bindingNameInterface.end())
    {
		auto outputMatrixCount = bindingNameInterface.size();
        _canMergeIntoTransform.resize(outputMatrixCount, false);
        _mergedTransforms.resize(outputMatrixCount, Identity<Float4x4>());

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

    ModelTransMachineOptimizer::ModelTransMachineOptimizer() {}

    ModelTransMachineOptimizer::~ModelTransMachineOptimizer()
    {}

	void OptimizeSkeleton(NascentSkeleton& embeddedSkeleton, NascentModel& model)
	{
		{
			auto filteringSkeleInterface = model.BuildSkeletonInterface();
			filteringSkeleInterface.insert(filteringSkeleInterface.begin(), std::make_pair(std::string{}, "identity"));
			embeddedSkeleton.GetSkeletonMachine().FilterOutputInterface(MakeIteratorRange(filteringSkeleInterface));
		}

		{
			auto finalSkeleInterface = embeddedSkeleton.GetSkeletonMachine().GetOutputInterface();
			ModelTransMachineOptimizer optimizer(model, finalSkeleInterface);
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
