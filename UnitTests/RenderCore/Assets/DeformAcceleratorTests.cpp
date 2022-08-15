// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "TechniqueTestsHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Assets/ModelScaffold.h"
#include "../../../RenderCore/Techniques/DeformAccelerator.h"
#include "../../../RenderCore/Techniques/DeformGeoInternal.h"
#include "../../../RenderCore/Techniques/DeformerConstruction.h"
#include "../../../RenderCore/Techniques/DeformGeometryInfrastructure.h"
#include "../../../RenderCore/Techniques/SkinDeformer.h"
#include "../../../RenderCore/Techniques/SkinDeformerInternal.h"
#include "../../../RenderCore/Techniques/Services.h"
#include "../../../RenderCore/Techniques/CommonUtils.h"
#include "../../../RenderCore/Techniques/PipelineCollection.h"
#include "../../../RenderCore/Assets/ModelRendererConstruction.h"
#include "../../../RenderCore/Metal/DeviceContext.h"		// required for memory barrier
#include "../../../RenderCore/GeoProc/MeshDatabase.h"
#include "../../../RenderCore/GeoProc/NascentModel.h"
#include "../../../RenderCore/GeoProc/NascentAnimController.h"
#include "../../../RenderCore/GeoProc/NascentCommandStream.h"
#include "../../../RenderCore/GeoProc/NascentObjectsSerialize.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/IArtifact.h"
#include "../../../ConsoleRig/Console.h"
#include "../../../xleres/FileList.h"

#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
using namespace Catch::literals;

namespace UnitTests
{
	using namespace RenderCore;
	using namespace RenderCore::Techniques;

	namespace GeoProc = RenderCore::Assets::GeoProc;

	static const Float3 s_cubeCorners[] {
		Float3{-1, -1, -1},
		Float3{ 1, -1, -1},
		Float3{ 1,  1, -1},
		Float3{-1,  1, -1},
		Float3{-1, -1,  1},
		Float3{ 1, -1,  1},
		Float3{ 1,  1,  1},
		Float3{-1,  1,  1}
	};

	static std::shared_ptr<GeoProc::MeshDatabase> CreateCubeMeshDatabase()
	{
		Float3 normals[dimof(s_cubeCorners)];
		Float4 tangents[dimof(s_cubeCorners)];
		for (unsigned c=0; c<dimof(s_cubeCorners); ++c) {
			normals[c] = Normalize(s_cubeCorners[c]);
			tangents[c] = Float4{Normalize(Cross(normals[c], Float3(0,1,0))), 1};
		}
		auto result = std::make_shared<GeoProc::MeshDatabase>();
		result->AddStream(
			GeoProc::CreateRawDataSource(s_cubeCorners, ArrayEnd(s_cubeCorners), Format::R32G32B32_FLOAT),
			{}, "POSITION", 0);
		result->AddStream(
			GeoProc::CreateRawDataSource(normals, ArrayEnd(normals), Format::R32G32B32_FLOAT),
			{}, "NORMAL", 0);
		result->AddStream(
			GeoProc::CreateRawDataSource(tangents, ArrayEnd(tangents), Format::R32G32B32A32_FLOAT),
			{}, "TEXTANGENT", 0);
		return result;
	}

	std::shared_ptr<RenderCore::Assets::ModelScaffold> MakeTestAnimatedModel()
	{
		// Create a model scaffold from a very simple cube model
		// Each vertex has 8 weights and there are 8 joints in total
		auto meshDatabase = CreateCubeMeshDatabase();
		uint16_t indices[] {
			0, 1, 3,
			3, 1, 2,

			1, 5, 2,
			2, 5, 6,

			4, 0, 7,
			7, 0, 3,

			5, 4, 6,
			6, 4, 7,

			3, 2, 7,
			7, 2, 6,

			4, 5, 0,
			0, 5, 1
		};
		std::vector<GeoProc::NascentModel::DrawCallDesc> drawCalls {
			GeoProc::NascentModel::DrawCallDesc{0, dimof(indices), Topology::TriangleList},
		};
		std::vector<uint8_t> indicesVector{(const uint8_t*)indices, (const uint8_t*)ArrayEnd(indices)};

		GeoProc::NascentModel model;
		auto mainObjId = model.Add(
			GeoProc::NascentModel::GeometryBlock {
				meshDatabase,
				std::move(drawCalls),
				{}, Identity<Float4x4>(),
				std::move(indicesVector), Format::R16_UINT
			});

		std::vector<Float4x4> inverseBindMatrices;
		inverseBindMatrices.resize(8, Identity<Float4x4>());
		std::vector<std::string> jointNames {
			"bone-0", "bone-1", "bone-2", "bone-3",
			"bone-4", "bone-5", "bone-6", "bone-7"
		};
		auto skinController = std::make_shared<GeoProc::UnboundSkinController>(std::move(inverseBindMatrices), Identity<Float4x4>(), Identity<Float4x4>(), std::move(jointNames));
		for (unsigned vertex=0; vertex<8; ++vertex) {
			float weights[8];
			unsigned indices[] { 0, 1, 2, 3, 4, 5, 6, 7 };
			float weightTotal = 0.f;
			for (unsigned bone=0; bone<dimof(weights); ++bone) {
				float distance = Magnitude(s_cubeCorners[bone] - s_cubeCorners[vertex]);
				weightTotal += weights[bone] = std::max(0.f, 1.f - 0.25f*distance);
			}
			for (unsigned bone=0; bone<dimof(weights); ++bone)
				weights[bone] /= weightTotal;
			skinController->AddInfluences(vertex, weights, indices);
		}
		auto controllerId = model.Add(
			GeoProc::NascentModel::SkinControllerBlock {
				std::move(skinController),
				"skinning"
			});

		model.Add(
			GeoProc::NascentModel::Command {
				mainObjId, {controllerId},
				"geo-model", {}});

		GeoProc::NascentSkeleton skeleton;
		skeleton.WriteOutputMarker({}, "geo-model");
		for (unsigned bone=0; bone<8; ++bone) {
			skeleton.WritePushLocalToWorld();
			skeleton.WriteStaticTransform(AsFloat4x4(s_cubeCorners[bone]));
			skeleton.WriteOutputMarker("skinning", (StringMeld<256>() << "bone-" << bone).AsStringSection());
			skeleton.WritePopLocalToWorld();
		}

		auto serializedChunk = GeoProc::SerializeSkinToChunks("skin", model, skeleton, GeoProc::NativeVBSettings{});
		auto artifactCollection = std::make_shared<::Assets::BlobArtifactCollection>(
			MakeIteratorRange(serializedChunk),
			::Assets::AssetState::Ready, ::Assets::DependencyValidation{});

		return ::Assets::AutoConstructAsset<std::shared_ptr<RenderCore::Assets::ModelScaffold>>(*artifactCollection);
	}

	static IResourcePtr LoadStorageBuffer(
		IDevice& device,
		const RenderCore::Assets::ModelScaffold& scaffold,
		const RenderCore::Assets::VertexData& vb)
	{
		auto buffer = std::make_unique<uint8[]>(vb._size);
		{
			auto inputFile = scaffold.OpenLargeBlocks();
			inputFile->Seek(vb._offset, OSServices::FileSeekAnchor::Current);
			inputFile->Read(buffer.get(), vb._size, 1);
		}
		return device.CreateResource(
			CreateDesc(
				BindFlag::UnorderedAccess|BindFlag::TransferSrc,
				LinearBufferDesc::Create(unsigned(vb._size)),
				"vb"),
			SubResourceInitData{MakeIteratorRange(buffer.get(), PtrAdd(buffer.get(), vb._size))});
	}

	std::vector<uint8_t> LoadCPUVertexBuffer(
		const RenderCore::Assets::ModelScaffold& scaffold,
		const RenderCore::Assets::VertexData& vb)
	{
		std::vector<uint8_t> result;
		result.resize(vb._size);

		auto inputFile = scaffold.OpenLargeBlocks();
		inputFile->Seek(vb._offset, OSServices::FileSeekAnchor::Current);
		inputFile->Read(result.data(), vb._size, 1);
		return result;
	}

	static std::vector<Float4x4> BasePose(std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold)
	{
		REQUIRE(modelScaffold->EmbeddedSkeleton());
		std::vector<Float4x4> result;
		result.resize(modelScaffold->EmbeddedSkeleton()->GetOutputMatrixCount());
		modelScaffold->EmbeddedSkeleton()->GenerateOutputTransforms(MakeIteratorRange(result), {});
		return result;
	}

	std::vector<InputElementDesc> AsInputLayout(const RenderCore::Assets::GeoInputAssembly& ia, unsigned slot)
	{
		std::vector<InputElementDesc> result;
		result.resize(ia._elements.size());
		RenderCore::Assets::BuildLowLevelInputAssembly(MakeIteratorRange(result), ia._elements, slot);
		return result;
	}

	static const RenderCore::Assets::SkinningDataDesc* GetSkinningDataAtGeo0(const RenderCore::Assets::ModelScaffold& scaffold)
	{
		for (auto cmd:scaffold.GetGeoMachine(0)) {
			switch (cmd.Cmd()) {
			case (uint32_t)RenderCore::Assets::GeoCommand::AttachSkinningData:
				return &cmd.As<RenderCore::Assets::SkinningDataDesc>();
				break;
			}
		}
		return nullptr;
	}

	static std::vector<uint8_t> RunGPUDeformerDirectly(MetalTestHelper& testHelper, BufferUploads::IManager& bufferUploads, std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold)
	{
		std::shared_ptr<Techniques::Internal::DeformerPipelineCollection> pipelineCollection;
		{
			UniformsStreamInterface usi;
			usi.BindResourceView(0, Hash64("StaticVertexAttachments"));
			usi.BindResourceView(1, Hash64("InputAttributes"));
			usi.BindResourceView(2, Hash64("OutputAttributes"));
			usi.BindResourceView(3, Hash64("DeformTemporaryAttributes"));
			usi.BindResourceView(4, Hash64("JointTransforms"));
			usi.BindResourceView(5, Hash64("IAParams"));
			usi.BindResourceView(6, Hash64("SkinIAParams"));

			UniformsStreamInterface pushConstantsUSI;
			pushConstantsUSI.BindImmediateData(0, Hash64("InvocationParams"));

			ShaderSourceParser::InstantiationRequest instRequests { SKIN_COMPUTE_HLSL };
			uint64_t patchExpansions[] { Hash64("PerformDeform"), Hash64("GetDeformInvocationParams") };

			pipelineCollection = std::make_shared<Techniques::Internal::DeformerPipelineCollection>(
				std::make_shared<Techniques::PipelineCollection>(testHelper._device),
				SKIN_PIPELINE ":Main",
				std::move(usi), std::move(pushConstantsUSI),
				std::move(instRequests), MakeIteratorRange(patchExpansions));
		}

		auto* skinningData = GetSkinningDataAtGeo0(*modelScaffold);
		REQUIRE(skinningData);
		const auto& animVB = skinningData->_animatedVertexElements;

		std::promise<std::shared_ptr<Techniques::IGeoDeformer>> promise;
		auto future = promise.get_future();
		auto srcLayout = AsInputLayout(animVB._ia, Techniques::Internal::VB_GPUStaticData), dstLayout = AsInputLayout(animVB._ia, Techniques::Internal::VB_PostDeform);
		Techniques::GPUSkinDeformer deformer(pipelineCollection, modelScaffold, "unit-test");
		Techniques::DeformerInputBinding deformInputBinding;
		deformInputBinding._geoBindings.push_back({
			std::make_pair(0,0),
			DeformerInputBinding::GeoBinding{srcLayout, dstLayout}
		});
		for (auto& o:deformInputBinding._geoBindings[0].second._bufferOffsets) o = 0;
		for (auto& s:deformInputBinding._geoBindings[0].second._bufferStrides) s = 0;
		deformInputBinding._geoBindings[0].second._bufferStrides[Techniques::Internal::VB_GPUStaticData] = animVB._ia._vertexStride;
		deformInputBinding._geoBindings[0].second._bufferStrides[Techniques::Internal::VB_PostDeform] = CalculateVertexStrideForSlot(dstLayout, Techniques::Internal::VB_PostDeform);
		deformer.Bind(deformInputBinding);

		auto inputResource = LoadStorageBuffer(*testHelper._device, *modelScaffold, animVB);
		auto outputResource = testHelper._device->CreateResource(inputResource->GetDesc());

		REQUIRE(modelScaffold->EmbeddedSkeleton());
		deformer.FeedInSkeletonMachineResults(
			0, BasePose(modelScaffold),
			deformer.CreateBinding(modelScaffold->EmbeddedSkeleton()->GetOutputInterface()));

		auto inputView = inputResource->CreateBufferView(BindFlag::UnorderedAccess);
		auto outputView = outputResource->CreateBufferView(BindFlag::UnorderedAccess);

		// hack -- stall to ensure vertex buffers are initialized
		auto threadContext = testHelper._device->GetImmediateContext();
		for (unsigned c=0; c<32; ++c) {
			bufferUploads.Update(*threadContext);
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}

		pipelineCollection->StallForPipeline();
		
		testHelper.BeginFrameCapture();
		unsigned instances[] = {0};
		IGeoDeformer::Metrics metrics;
		deformer.ExecuteGPU(*threadContext, MakeIteratorRange(instances), outputResource->GetDesc()._linearBufferDesc._sizeInBytes, *inputView, *inputView, *outputView, metrics);
		testHelper.EndFrameCapture();

		{
			auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
			VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.pNext = nullptr;
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(
				metalContext.GetActiveCommandList().GetUnderlying().get(),
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0,
				1, &barrier,
				0, nullptr,
				0, nullptr);
		}

		return outputResource->ReadBackSynchronized(*threadContext);
	}

	static std::vector<Float3> DeformPositionsOnCPU(std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold)
	{
		Techniques::CPUSkinDeformer cpuSkinDeformer { *modelScaffold, {} };

		auto* skinningData = GetSkinningDataAtGeo0(*modelScaffold);
		REQUIRE(skinningData);
		auto& animVb = skinningData->_animatedVertexElements;
		auto rawInputBuffer = LoadCPUVertexBuffer(*modelScaffold, animVb);

		auto inputFloat3s = AsFloat3s(
			Techniques::Internal::AsVertexElementIteratorRange(
				MakeIteratorRange(rawInputBuffer), 
				*Techniques::Internal::FindElement(animVb._ia._elements, Hash64("POSITION")), 
				animVb._ia._vertexStride));

		std::vector<uint8_t> outputBufferData;
		outputBufferData.resize(inputFloat3s.size() * sizeof(Float3));

		Techniques::DeformerInputBinding::GeoBinding geoBinding;
		geoBinding._inputElements.push_back({"POSITION", 0, Format::R32G32B32_FLOAT, Techniques::Internal::VB_CPUStaticData, 0});
		geoBinding._outputElements.push_back({"POSITION", 0, Format::R32G32B32_FLOAT, Techniques::Internal::VB_PostDeform, 0});
		for (auto& o:geoBinding._bufferOffsets) o = 0;
		for (auto& s:geoBinding._bufferStrides) s = 0;
		geoBinding._bufferStrides[Techniques::Internal::VB_CPUStaticData] = sizeof(Float3);
		geoBinding._bufferStrides[Techniques::Internal::VB_PostDeform] = sizeof(Float3);
		cpuSkinDeformer._bindingHelper._inputBinding._geoBindings.push_back({std::make_pair(0,0), std::move(geoBinding)});

		REQUIRE(modelScaffold->EmbeddedSkeleton());
		cpuSkinDeformer.FeedInSkeletonMachineResults(
			0, BasePose(modelScaffold),
			cpuSkinDeformer.CreateBinding(modelScaffold->EmbeddedSkeleton()->GetOutputInterface()));
		unsigned instances[] = {0};
		cpuSkinDeformer.ExecuteCPU(MakeIteratorRange(instances), outputBufferData.size(), MakeIteratorRange(inputFloat3s), {}, outputBufferData);

		auto destinationElements = Techniques::Internal::AsVertexElementIteratorRange(MakeIteratorRange(outputBufferData), Format::R32G32B32_FLOAT, 0, sizeof(Float3));
		return AsFloat3s(destinationElements);
	}

	static std::vector<Float3> GetFloat3sFromVertexBuffer(
		IteratorRange<void*> rawVB,
		const RenderCore::Assets::GeoInputAssembly& ia, 
		uint64_t semanticHash)
	{
		auto* ele = Techniques::Internal::FindElement(ia._elements, Hash64("POSITION"));
		REQUIRE(ele);
		auto eleRange = Techniques::Internal::AsVertexElementIteratorRange(rawVB, *ele, ia._vertexStride);
		return AsFloat3s(eleRange);
	}

	TEST_CASE( "Deform-SkinCPUVsGPU", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		TechniqueTestApparatus techniqueTestHelper{*testHelper};

		auto modelScaffold = MakeTestAnimatedModel();

		auto gpuRawBuffer = RunGPUDeformerDirectly(*testHelper, *techniqueTestHelper._bufferUploads, modelScaffold);
		auto cpuPositions = DeformPositionsOnCPU(modelScaffold);

		// Find the positions within the raw GPU output and convert to float3s
		auto* skinningData = GetSkinningDataAtGeo0(*modelScaffold);
		REQUIRE(skinningData);
		auto gpuPositions = GetFloat3sFromVertexBuffer(MakeIteratorRange(gpuRawBuffer), skinningData->_animatedVertexElements._ia, Hash64("POSITION"));

		REQUIRE(cpuPositions.size() == gpuPositions.size());
		auto vCount = gpuPositions.size();
		for (unsigned v=0; v<vCount; ++v) {
			auto cpu = cpuPositions[v];
			auto gpu = gpuPositions[v];
			// We're not infinitely precise because the CPU path will always work with 32 bit floats,
			// but the GPU path can work with a wider variety of formats
			REQUIRE(Equivalent(cpu, gpu, 1e-3f));
		}

		globalServices->PrepareForDestruction();
	}

	static void StallWhilePending(RenderCore::Assets::ModelRendererConstruction& construction)
	{
		std::promise<std::shared_ptr<RenderCore::Assets::ModelRendererConstruction>> promise;
		auto future = promise.get_future();
		construction.FulfillWhenNotPending(std::move(promise));
		future.wait();
	}

	static void StallWhilePending(RenderCore::Techniques::DeformerConstruction& construction)
	{
		std::promise<std::shared_ptr<RenderCore::Techniques::DeformerConstruction>> promise;
		auto future = promise.get_future();
		construction.FulfillWhenNotPending(std::move(promise));
		future.wait();
	}

	TEST_CASE( "Deform-DeformAccelerator", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		TechniqueTestApparatus techniqueTestHelper{*testHelper};

		auto pipelineCollection = std::make_shared<Techniques::PipelineCollection>(testHelper->_device);
		auto cpuConfigure = RenderCore::Techniques::CreateCPUSkinDeformerConfigure();
		auto gpuConfigure = RenderCore::Techniques::CreateGPUSkinDeformerConfigure(pipelineCollection);
		
		auto modelScaffold = MakeTestAnimatedModel();
		auto rendererConstruction = std::make_shared<RenderCore::Assets::ModelRendererConstruction>();
		rendererConstruction->AddElement().SetModelScaffold(modelScaffold);
		StallWhilePending(*rendererConstruction);
		
		auto pool = Techniques::CreateDeformAcceleratorPool(testHelper->_device, techniqueTestHelper._drawablesPool, techniqueTestHelper._compiledLayoutPool);
		
		{
			auto cpuAccelerator = pool->CreateDeformAccelerator();
			REQUIRE(cpuAccelerator);

			auto deformerConstruction = std::make_shared<Techniques::DeformerConstruction>(testHelper->_device, rendererConstruction);
			cpuConfigure->ConfigureAllElements(*deformerConstruction);
			StallWhilePending(*deformerConstruction);

			auto cpuGeoDeformAttachment = Techniques::CreateDeformGeoAttachment(*testHelper->_device, *rendererConstruction, *deformerConstruction);
			REQUIRE(cpuGeoDeformAttachment);

			auto cpuRendererBinding = cpuGeoDeformAttachment->GetDeformerToRendererBinding();
			REQUIRE(!cpuRendererBinding._geoBindings.empty());

			pool->Attach(*cpuAccelerator, cpuGeoDeformAttachment);
		}

		{
			auto gpuAccelerator = pool->CreateDeformAccelerator();
			REQUIRE(gpuAccelerator);

			auto deformerConstruction = std::make_shared<Techniques::DeformerConstruction>(testHelper->_device, rendererConstruction);
			gpuConfigure->ConfigureAllElements(*deformerConstruction);
			StallWhilePending(*deformerConstruction);
			
			auto gpuGeoDeformAttachment = Techniques::CreateDeformGeoAttachment(*testHelper->_device, *rendererConstruction, *deformerConstruction);
			REQUIRE(gpuGeoDeformAttachment);

			auto rendererBinding2 = gpuGeoDeformAttachment->GetDeformerToRendererBinding();
			REQUIRE(!rendererBinding2._geoBindings.empty());
			REQUIRE(rendererBinding2._geoBindings[0].second._generatedElements.size() == 3);
			REQUIRE(rendererBinding2._geoBindings[0].second._generatedElements[0]._semanticName == "POSITION");
			REQUIRE(rendererBinding2._geoBindings[0].second._generatedElements[1]._semanticName == "NORMAL");
			REQUIRE(rendererBinding2._geoBindings[0].second._generatedElements[2]._semanticName == "TEXTANGENT");

			pool->Attach(*gpuAccelerator, gpuGeoDeformAttachment);
		}
	}

	TEST_CASE( "Deform-CPUInstantiation", "[rendercore_techniques]" )
	{
		auto modelScaffold = MakeTestAnimatedModel();
		auto* skinningData = GetSkinningDataAtGeo0(*modelScaffold);
		REQUIRE(skinningData);
		auto vertexCount = skinningData->_animatedVertexElements._size / skinningData->_animatedVertexElements._ia._vertexStride;

		{
			// Single stage deform that takes POSITION & NORMAL and generates 3 arbitrary elements
			Techniques::DeformOperationInstantiation testInst0;
			testInst0._generatedElements.push_back({ "GENERATED", 0, Format::R16G16B16A16_FLOAT });
			testInst0._generatedElements.push_back({ "GENERATED2", 0, Format::R8G8B8A8_UNORM });
			testInst0._generatedElements.push_back({ "GENERATED", 1, Format::R32_UINT });
			testInst0._upstreamSourceElements.push_back({ "POSITION", 0, Format::R32G32B32_FLOAT });
			testInst0._upstreamSourceElements.push_back({ "NORMAL", 0, Format::R8G8B8A8_UNORM });
			testInst0._suppressElements.push_back(Hash64("BADSEMANTIC"));
			
			std::vector<DeformOperationInstantiation> instantiations;
			instantiations.push_back(testInst0);

			Techniques::Internal::DeformBufferIterators bufferIterators;
			std::vector<DeformerInputBinding::GeoBinding> geoBindings;
			geoBindings.resize(instantiations.size());
			auto nascentDeform = Techniques::Internal::CreateDeformBindings(
				MakeIteratorRange(geoBindings), instantiations,
				bufferIterators, true, 0,
				modelScaffold);

			unsigned generatedVertexStride = 8 + 4 + 4;
			unsigned staticDataVertexStride = 12+4;
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_PostDeform] == generatedVertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_CPUStaticData] == staticDataVertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_GPUStaticData] == 0);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_CPUDeformTemporaries] == 0);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_GPUDeformTemporaries] == 0);

			REQUIRE(geoBindings.size() == 1);
			REQUIRE(geoBindings[0]._inputElements.size() == 2);
			REQUIRE(geoBindings[0]._inputElements[0]._nativeFormat == Format::R32G32B32_FLOAT);
			REQUIRE(geoBindings[0]._inputElements[1]._nativeFormat == Format::R8G8B8A8_UNORM);
			REQUIRE(geoBindings[0]._inputElements[0]._inputSlot == Techniques::Internal::VB_CPUStaticData);
			REQUIRE(geoBindings[0]._inputElements[1]._inputSlot == Techniques::Internal::VB_CPUStaticData);
			REQUIRE(geoBindings[0]._outputElements.size() == 3);
			REQUIRE(geoBindings[0]._outputElements[0]._nativeFormat == Format::R16G16B16A16_FLOAT);
			REQUIRE(geoBindings[0]._outputElements[1]._nativeFormat == Format::R8G8B8A8_UNORM);
			REQUIRE(geoBindings[0]._outputElements[2]._nativeFormat == Format::R32_UINT);
			REQUIRE(geoBindings[0]._outputElements[0]._inputSlot == Techniques::Internal::VB_PostDeform);
			REQUIRE(geoBindings[0]._outputElements[1]._inputSlot == Techniques::Internal::VB_PostDeform);
			REQUIRE(geoBindings[0]._outputElements[2]._inputSlot == Techniques::Internal::VB_PostDeform);
		}

		{
			// 3 deform stages with deformers that consume outputs from previous stages
			Techniques::DeformOperationInstantiation testInst[3];
			testInst[0]._generatedElements.push_back({ "TEMPORARY", 0, Format::R16G16B16A16_FLOAT });
			testInst[0]._generatedElements.push_back({ "GENERATED2", 0, Format::R8G8B8A8_UNORM });
			testInst[0]._upstreamSourceElements.push_back({ "POSITION", 0, Format::R32G32B32_FLOAT });
			testInst[0]._upstreamSourceElements.push_back({ "NORMAL", 0, Format::R8G8B8A8_UNORM });
			testInst[0]._suppressElements.push_back(Hash64("TANGENT"));

			testInst[1]._generatedElements.push_back({ "TEMPORARY", 1, Format::R16G16B16A16_FLOAT });
			testInst[1]._upstreamSourceElements.push_back({ "POSITION", 0, Format::R32G32B32_FLOAT });
			testInst[1]._upstreamSourceElements.push_back({ "TEMPORARY", 0, Format::R16G16B16A16_FLOAT });

			testInst[2]._generatedElements.push_back({ "GENERATED3", 0, Format::R16G16B16A16_FLOAT });
			testInst[2]._upstreamSourceElements.push_back({ "TEMPORARY", 1, Format::R16G16B16A16_FLOAT });
			testInst[2]._suppressElements.push_back(Hash64("TANGENT"));
			
			Techniques::Internal::DeformBufferIterators bufferIterators;
			std::vector<DeformerInputBinding::GeoBinding> geoBindings;
			geoBindings.resize(dimof(testInst));

			auto nascentDeform = Techniques::Internal::CreateDeformBindings(
				MakeIteratorRange(geoBindings), testInst,
				bufferIterators, true, 0,
				modelScaffold);

			unsigned generatedVertexStride = 4+8;				// {"GENERATED2", 0}, {"GENERATED3", 0}
			unsigned staticDataVertexStride = 12+4;
			unsigned temporariesVertexStride = 8+8;				// {"TEMPORARY", 0}, {"TEMPORARY", 1}
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_PostDeform] == generatedVertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_CPUStaticData] == staticDataVertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_GPUStaticData] == 0);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_CPUDeformTemporaries] ==temporariesVertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_GPUDeformTemporaries] == 0);

			REQUIRE(geoBindings[0]._inputElements.size() == 2);
			REQUIRE(geoBindings[0]._inputElements[0]._inputSlot == Techniques::Internal::VB_CPUStaticData);
			REQUIRE(geoBindings[0]._inputElements[1]._inputSlot == Techniques::Internal::VB_CPUStaticData);
			REQUIRE(geoBindings[0]._outputElements.size() == 2);
			REQUIRE(geoBindings[0]._outputElements[0]._inputSlot == Techniques::Internal::VB_CPUDeformTemporaries);
			REQUIRE(geoBindings[0]._outputElements[1]._inputSlot == Techniques::Internal::VB_PostDeform);

			REQUIRE(geoBindings[1]._inputElements.size() == 2);
			REQUIRE(geoBindings[1]._inputElements[0]._inputSlot == Techniques::Internal::VB_CPUStaticData);
			REQUIRE(geoBindings[1]._inputElements[1]._inputSlot == Techniques::Internal::VB_CPUDeformTemporaries);
			REQUIRE(geoBindings[1]._outputElements.size() == 1);
			REQUIRE(geoBindings[1]._outputElements[0]._inputSlot == Techniques::Internal::VB_CPUDeformTemporaries);

			REQUIRE(geoBindings[2]._inputElements.size() == 1);
			REQUIRE(geoBindings[2]._inputElements[0]._inputSlot == Techniques::Internal::VB_CPUDeformTemporaries);
			REQUIRE(geoBindings[2]._outputElements.size() == 1);
			REQUIRE(geoBindings[2]._outputElements[0]._inputSlot == Techniques::Internal::VB_PostDeform);
		}
	}

	TEST_CASE( "Deform-GPUInstantiation", "[rendercore_techniques]" )
	{
		auto modelScaffold = MakeTestAnimatedModel();
		auto* skinningData = GetSkinningDataAtGeo0(*modelScaffold);
		REQUIRE(skinningData);
		auto vertexCount = skinningData->_animatedVertexElements._size / skinningData->_animatedVertexElements._ia._vertexStride;

		{
			// Single stage deform, but using a GPU deformer
			Techniques::DeformOperationInstantiation testInst0;
			testInst0._generatedElements.push_back({ "GENERATED2", 0, Format::R8G8B8A8_UNORM });
			testInst0._generatedElements.push_back({ "GENERATED", 1, Format::R32_UINT });
			testInst0._generatedElements.push_back({ "GENERATED", 0, Format::R16G16B16A16_FLOAT });
			testInst0._upstreamSourceElements.push_back({ "POSITION", 0, Format::R32G32B32_FLOAT });
			testInst0._upstreamSourceElements.push_back({ "NORMAL", 0, Format::R8G8B8A8_UNORM });
			
			Techniques::Internal::DeformBufferIterators bufferIterators;
			std::vector<DeformerInputBinding::GeoBinding> geoBindings;
			geoBindings.resize(1);
			auto nascentDeform = Techniques::Internal::CreateDeformBindings(
				MakeIteratorRange(geoBindings), {&testInst0, &testInst0+1},
				bufferIterators, false, 0,
				modelScaffold);

			// The generated elements get reordered from largest to smallest element
			REQUIRE(nascentDeform._generatedElements.size() == 3);
			REQUIRE(nascentDeform._generatedElements[0]._semanticName == "GENERATED");
			REQUIRE(nascentDeform._generatedElements[1]._semanticName == "GENERATED2");
			REQUIRE(nascentDeform._generatedElements[2]._semanticName == "GENERATED");

			REQUIRE(nascentDeform._suppressedElements.size() == 3);
			REQUIRE(std::find(nascentDeform._suppressedElements.begin(), nascentDeform._suppressedElements.end(), Hash64("GENERATED")) != nascentDeform._suppressedElements.end());
			REQUIRE(std::find(nascentDeform._suppressedElements.begin(), nascentDeform._suppressedElements.end(), Hash64("GENERATED2")) != nascentDeform._suppressedElements.end());
			REQUIRE(std::find(nascentDeform._suppressedElements.begin(), nascentDeform._suppressedElements.end(), Hash64("GENERATED")+1) != nascentDeform._suppressedElements.end());
		
			unsigned generatedVertexStride = 8+4+4;
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_PostDeform] == generatedVertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_CPUStaticData] == 0);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_GPUStaticData] == skinningData->_animatedVertexElements._ia._vertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_CPUDeformTemporaries] == 0);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_GPUDeformTemporaries] == 0);
		}
	}
	
}
