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

	class NascentGeometryObjects
	{
	public:
		std::vector<std::pair<NascentObjectGuid, NascentRawGeometry>> _rawGeos;
		std::vector<std::pair<uint64_t, NascentBoundSkinnedGeometry>> _skinnedGeos;
	};

	static std::pair<Float3, Float3> CalculateBoundingBox
		(
			const NascentGeometryObjects& geoObjects,
			const NascentModelCommandStream& scene,
			IteratorRange<const Float4x4*> transforms
		);

	static unsigned SerializeSkin(
		::Assets::BlockSerializer& serializer, 
		LargeResourceBlockConstructor& largeResourcesConstructor,
		const NascentGeometryObjects& objs)
	{
		unsigned geoBlocksWritten = 0;
		for (const auto& geo:objs._rawGeos) {
			::Assets::BlockSerializer tempBlock;
			geo.second.SerializeWithResourceBlock(tempBlock, largeResourcesConstructor);

			serializer << (uint32_t)Assets::ScaffoldCommand::Geo;
			serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t));
			serializer << tempBlock.SizePrimaryBlock();
			serializer.SerializeSubBlock(tempBlock);
			++geoBlocksWritten;
		}
		for (const auto& geo:objs._skinnedGeos) {
			::Assets::BlockSerializer tempBlock;
			geo.second.SerializeWithResourceBlock(tempBlock, largeResourcesConstructor);

			serializer << (uint32_t)Assets::ScaffoldCommand::Geo;
			serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t));
			serializer << tempBlock.SizePrimaryBlock();
			serializer.SerializeSubBlock(tempBlock);
			++geoBlocksWritten;
		}
		return geoBlocksWritten;
	}

	static unsigned SerializeSkinTopological(
		::Assets::BlockSerializer& serializer, 
		LargeResourceBlockConstructor& largeResourcesConstructor,
		const NascentGeometryObjects& objs)
	{
		unsigned geoBlocksWritten = 0;
		for (const auto& geo:objs._rawGeos) {
			::Assets::BlockSerializer tempBlock;
			geo.second.SerializeTopologicalWithResourceBlock(tempBlock, largeResourcesConstructor);

			serializer << (uint32_t)Assets::ScaffoldCommand::Geo;
			serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t));
			serializer << tempBlock.SizePrimaryBlock();
			serializer.SerializeSubBlock(tempBlock);
			++geoBlocksWritten;
		}
		for (const auto& geo:objs._skinnedGeos) {
			::Assets::BlockSerializer tempBlock;
			geo.second.SerializeTopologicalWithResourceBlock(tempBlock, largeResourcesConstructor);

			serializer << (uint32_t)Assets::ScaffoldCommand::Geo;
			serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t));
			serializer << tempBlock.SizePrimaryBlock();
			serializer.SerializeSubBlock(tempBlock);
			++geoBlocksWritten;
		}
		return geoBlocksWritten;
	}

    static ModelDefaultPoseData CalculateDefaultPoseData(
        const NascentSkeleton& skeleton,
        const NascentModelCommandStream& cmdStream,
        const NascentGeometryObjects& geoObjects)
    {
        ModelDefaultPoseData result;

        auto skeletonOutput = skeleton.GetSkeletonMachine().GenerateOutputTransforms();

        auto skelOutputInterface = skeleton.GetSkeletonMachine().BuildHashedOutputInterface();
        auto streamInputInterface = cmdStream.BuildHashedInputInterface();
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

        result._boundingBox = CalculateBoundingBox(
            geoObjects, cmdStream, MakeIteratorRange(result._defaultTransforms));

        return result;
    }

    static std::ostream& SerializationOperator(std::ostream& stream, const NascentGeometryObjects& geos)
    {
        stream << " --- Geos:" << std::endl;
        unsigned c=0;
        for (const auto& g:geos._rawGeos)
            stream << "[" << c++ << "] (0x" << std::hex << g.first._objectId << std::dec << ") Geo --- " << std::endl << g.second << std::endl;

        stream << " --- Skinned Geos:" << std::endl;
        c=0;
        for (const auto& g:geos._skinnedGeos)
            stream << "[" << c++ << "] (0x" << std::hex << g.first << std::dec << ") Skinned geo --- " << std::endl << g.second << std::endl;
        return stream;
    }

	static void TraceMetrics(std::ostream& stream, const NascentGeometryObjects& geoObjects, const NascentModelCommandStream& cmdStream, const NascentSkeleton& skeleton)
	{
		stream << "============== Geometry Objects ==============" << std::endl;
		stream << geoObjects;
		stream << std::endl;
		stream << "============== Command stream ==============" << std::endl;
		stream << cmdStream;
		stream << std::endl;
		stream << "============== Transformation Machine ==============" << std::endl;
		SerializationOperator(stream, skeleton.GetSkeletonMachine());
	}

	std::vector<::Assets::ICompileOperation::SerializedArtifact> SerializeSkinToChunks(const std::string& name, const NascentGeometryObjects& geoObjects, const NascentModelCommandStream& cmdStream, const NascentSkeleton& skeleton)
	{
		::Assets::BlockSerializer serializer;
		auto recall = serializer.CreateRecall(sizeof(unsigned));

		LargeResourceBlockConstructor largeResourcesConstructor;
		unsigned geoBlocksWritten = 0;

		// main command streams
		{
			geoBlocksWritten += SerializeSkin(serializer, largeResourcesConstructor, geoObjects);

			::Assets::BlockSerializer tempBlock;
			tempBlock << cmdStream;

			serializer << (uint32_t)Assets::ScaffoldCommand::ModelCommandStream;
			serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t) + sizeof(uint64_t));
			serializer << 0ull;	// default cmd stream id (s_CmdStreamGuid_Default)
			serializer << tempBlock.SizePrimaryBlock();
			serializer.SerializeSubBlock(tempBlock);
		}

		// topological command streams
		{
			auto geoBlockOffset = geoBlocksWritten;
			geoBlocksWritten += SerializeSkinTopological(serializer, largeResourcesConstructor, geoObjects);

			::Assets::BlockSerializer tempBlock;
			SerializationOperator(tempBlock, cmdStream, geoBlockOffset);

			serializer << (uint32_t)Assets::ScaffoldCommand::ModelCommandStream;
			serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t) + sizeof(uint64_t));
			serializer << Hash64("adjacency");
			serializer << tempBlock.SizePrimaryBlock();
			serializer.SerializeSubBlock(tempBlock);
		}

		{
			::Assets::BlockSerializer tempBlock;
			tempBlock << skeleton;

			serializer << (uint32_t)Assets::ScaffoldCommand::Skeleton;
			serializer << (uint32_t)(sizeof(size_t) + sizeof(size_t));
			serializer << tempBlock.SizePrimaryBlock();
			serializer.SerializeSubBlock(tempBlock);
		}

		{
			auto defaultPoseData = CalculateDefaultPoseData(skeleton, cmdStream, geoObjects);
			serializer << MakeCmdAndSerializable(ScaffoldCommand::DefaultPoseData, defaultPoseData);
		}

		{
			ModelRootData rootData;
			rootData._maxLOD = cmdStream.GetMaxLOD();
			serializer << MakeCmdAndSerializable(ScaffoldCommand::ModelRootData, rootData);
		}

		serializer.PushSizeValueAtRecall(recall);

		auto largeResourcesBlock = std::make_shared<std::vector<uint8_t>>();
		largeResourcesBlock->resize(largeResourcesConstructor.CalculateSize());
		auto i = largeResourcesBlock->begin();
		for (const auto& e:largeResourcesConstructor._elements) {
			std::memcpy(AsPointer(i), e.begin(), e.size());
			i += e.size();
		}
		assert(i == largeResourcesBlock->end());

		// SerializationOperator human-readable metrics information
		std::stringstream metricsStream;
		TraceMetrics(metricsStream, geoObjects, cmdStream, skeleton);

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

	static uint64_t HashOfGeoAndSkinControllerIds(const NascentModel::Command& cmd)
	{
		uint64_t result = HashCombine(cmd._geometryBlock._objectId, cmd._geometryBlock._namespaceId);
		for (const auto&ctrl:cmd._skinControllerBlocks) {
			result = HashCombine(ctrl._objectId, result);
			result = HashCombine(ctrl._namespaceId, result);
		}
		return result;
	}

	std::vector<::Assets::ICompileOperation::SerializedArtifact> NascentModel::SerializeToChunks(const std::string& name, const NascentSkeleton& embeddedSkeleton, const NativeVBSettings& nativeSettings) const
	{
		NascentGeometryObjects geoObjects;
		NascentModelCommandStream cmdStream;

		for (const auto&cmd:_commands) {
			auto* geoBlock = FindGeometryBlock(cmd.second._geometryBlock);
			if (!geoBlock) continue;

			std::vector<uint64_t> materialGuid;
			materialGuid.reserve(cmd.second._materialBindingSymbols.size());
			for (const auto&mat:cmd.second._materialBindingSymbols) {
				NascentModelCommandStream::MaterialGuid guid = 0;
				const char* parseEnd = FastParseValue(MakeStringSection(mat), guid);
				if (parseEnd == AsPointer(mat.end())) {
					materialGuid.push_back(guid);
				} else
					materialGuid.push_back(Hash64(mat));
			}

			if (cmd.second._skinControllerBlocks.empty()) {
				auto i = std::find_if(geoObjects._rawGeos.begin(), geoObjects._rawGeos.end(),
					[&cmd](const std::pair<NascentObjectGuid, NascentRawGeometry>& p) { return p.first == cmd.second._geometryBlock; });
				if (i == geoObjects._rawGeos.end()) {
					auto rawGeo = CompleteInstantiation(*geoBlock, nativeSettings);
					geoObjects._rawGeos.emplace_back(
						std::make_pair(cmd.second._geometryBlock, std::move(rawGeo)));
					i = geoObjects._rawGeos.end ()-1;
				}

				cmdStream.Add(
					NascentModelCommandStream::GeometryInstance {
						(unsigned)std::distance(geoObjects._rawGeos.begin(), i),
						cmdStream.RegisterInputInterfaceMarker({}, cmd.second._localToModel),
						std::move(materialGuid),
						cmd.second._levelOfDetail
					});
			} else {
				auto hashedId = HashOfGeoAndSkinControllerIds(cmd.second);
				auto i = std::find_if(geoObjects._skinnedGeos.begin(), geoObjects._skinnedGeos.end(),
					[hashedId](const std::pair<uint64_t, NascentBoundSkinnedGeometry>& p) { return p.first == hashedId; });
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
							jointMatrices[c] = (uint16_t)cmdStream.RegisterInputInterfaceMarker(controllerBlock->_skeleton, controller.GetJointNames()[c]);

						controllers.emplace_back(UnboundSkinControllerAndJointMatrices { &controller, std::move(jointMatrices) });
					}

					auto boundController = BindController(std::move(rawGeo), MakeIteratorRange(controllers), "");
					geoObjects._skinnedGeos.emplace_back(std::make_pair(hashedId, std::move(boundController)));
					i = geoObjects._skinnedGeos.end()-1;
				}

				cmdStream.Add(
					NascentModelCommandStream::SkinControllerInstance {
						(unsigned)std::distance(geoObjects._skinnedGeos.begin(), i),
						cmdStream.RegisterInputInterfaceMarker({}, cmd.second._localToModel),
						std::move(materialGuid),
						cmd.second._levelOfDetail
					});
			}
		}

		return SerializeSkinToChunks(name, geoObjects, cmdStream, embeddedSkeleton);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static std::pair<Float3, Float3> CalculateBoundingBox
		(
			const NascentGeometryObjects& geoObjects,
			const NascentModelCommandStream& scene,
			IteratorRange<const Float4x4*> transforms
		)
	{
		//
		//      For all the parts of the model, calculate the bounding box.
		//      We just have to go through each vertex in the model, and
		//      transform it into model space, and calculate the min and max values
		//      found;
		//
		auto result = InvalidBoundingBox();
		// const auto finalMatrices = 
		//     _skeleton.GetSkeletonMachine().GenerateOutputTransforms(
		//         _animationSet.BuildTransformationParameterSet(0.f, nullptr, _skeleton, _objects));

		//
		//      Do the unskinned geometry first
		//

		for (auto i=scene._geometryInstances.cbegin(); i!=scene._geometryInstances.cend(); ++i) {
			const NascentModelCommandStream::GeometryInstance& inst = *i;

			if (inst._id >= geoObjects._rawGeos.size()) continue;
			const auto* geo = &geoObjects._rawGeos[inst._id].second;

			Float4x4 localToWorld = Identity<Float4x4>();
			if (inst._localToWorldId < transforms.size())
				localToWorld = transforms[inst._localToWorldId];

			localToWorld = Combine(geo->_geoSpaceToNodeSpace, localToWorld);

			const void*         vertexBuffer = geo->_vertices.data();
			const unsigned      vertexStride = geo->_mainDrawInputAssembly._vertexStride;

			auto positionDesc = FindPositionElement(
				AsPointer(geo->_mainDrawInputAssembly._elements.begin()),
				geo->_mainDrawInputAssembly._elements.size());

			if (positionDesc._nativeFormat != Format::Unknown && vertexStride) {
				AddToBoundingBox(
					result, vertexBuffer, vertexStride, 
					geo->_vertices.size() / vertexStride, positionDesc, localToWorld);
			}
		}

		//
		//      Now also do the skinned geometry. But use the default pose for
		//      skinned geometry (ie, don't apply the skinning transforms to the bones).
		//      Obvious this won't give the correct result post-animation.
		//

		for (auto i=scene._skinControllerInstances.cbegin(); i!=scene._skinControllerInstances.cend(); ++i) {
			const NascentModelCommandStream::SkinControllerInstance& inst = *i;

			if (inst._id >= geoObjects._skinnedGeos.size()) continue;
			const auto* controller = &geoObjects._skinnedGeos[inst._id].second;
			if (!controller) continue;

			Float4x4 localToWorld = Identity<Float4x4>();
			if (inst._localToWorldId < transforms.size())
				localToWorld = transforms[inst._localToWorldId];

			localToWorld = Combine(controller->_unanimatedBase._geoSpaceToNodeSpace, localToWorld);

			//  We can't get the vertex position data directly from the vertex buffer, because
			//  the "bound" object is already using an opaque hardware object. However, we can
			//  transform the local space bounding box and use that.

			const unsigned indices[][3] = 
			{
				{0,0,0}, {0,1,0}, {1,0,0}, {1,1,0},
				{0,0,1}, {0,1,1}, {1,0,1}, {1,1,1}
			};

			const Float3* A = (const Float3*)&controller->_localBoundingBox.first;
			for (unsigned c=0; c<dimof(indices); ++c) {
				Float3 position(A[indices[c][0]][0], A[indices[c][1]][1], A[indices[c][2]][2]);
				AddToBoundingBox(result, position, localToWorld);
			}
		}

		assert(!std::isinf(result.first[0]) && !std::isinf(result.first[1]) && !std::isinf(result.first[2]));
		assert(!std::isinf(result.second[0]) && !std::isinf(result.second[1]) && !std::isinf(result.second[2]));

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
