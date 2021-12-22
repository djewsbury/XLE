// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "TechniqueTestsHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Assets/ModelScaffold.h"
#include "../../../RenderCore/Techniques/DeformAccelerator.h"
#include "../../../RenderCore/Techniques/DeformerInternal.h"
#include "../../../RenderCore/Techniques/SimpleModelDeform.h"
#include "../../../RenderCore/Techniques/SkinDeformer.h"
#include "../../../RenderCore/Techniques/SkinDeformerInternal.h"
#include "../../../RenderCore/Techniques/Services.h"
#include "../../../RenderCore/Techniques/CommonUtils.h"
#include "../../../RenderCore/Techniques/PipelineCollection.h"
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

#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
using namespace Catch::literals;

namespace UnitTests
{
	using namespace RenderCore;
	using namespace RenderCore::Techniques;

#if 0
	class NewRendererSkeletonInterface
	{
	public:
		void FeedInSkeletonMachineResults(
			unsigned instanceIdx,
			IteratorRange<const Float4x4*> skeletonMachineOutput)
		{
			for (const auto&d:_deformers)
				((SkinDeformer*)d._skinDeformer->QueryInterface(typeid(SkinDeformer).hash_code()))->FeedInSkeletonMachineResults(
					instanceIdx, skeletonMachineOutput, d._deformerBindings);
		}

		NewRendererSkeletonInterface(
			const RenderCore::Assets::SkeletonMachine::OutputInterface& smOutputInterface,
			IteratorRange<const std::shared_ptr<IDeformOperator>*> skinDeformers)
		{
			_deformers.resize(skinDeformers.size());
			for (auto&d:skinDeformers) {
				SkinDeformer* deformer = (SkinDeformer*)d->QueryInterface(typeid(SkinDeformer).hash_code());
				if (!deformer)
					Throw(std::runtime_error("Incorrect deformer type passed to RendererSkeletonInterface. Expecting SkinDeformer"));
				_deformers.push_back(Deformer{ d, deformer->CreateBinding(smOutputInterface) });
			}
		}

		~NewRendererSkeletonInterface()
		{}

	private:
		struct Deformer
		{
			std::shared_ptr<IDeformOperator> _skinDeformer;
			RenderCore::Assets::SkeletonBinding _deformerBindings;
		};
		std::vector<Deformer> _deformers;
	};
#endif

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
			// float weights[1];
			// unsigned indices[] { 0 };
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

#if 0
	void RunDeformAcceleratorsTest(
		std::shared_ptr<RenderCore::IDevice> device,
		const TestAnimatedModelResources& resources)
	{
		auto threadContext = device->GetImmediateContext();

		auto pool = Techniques::CreateDeformAcceleratorPool(device);
		auto accelerator = pool->CreateDeformAccelerator("skin", resources._modelScaffold);

		NewRendererSkeletonInterface skeletonInterface(
			resources._modelScaffold->EmbeddedSkeleton().GetOutputInterface(),
			pool->GetOperations(*accelerator, typeid(Techniques::SkinDeformer).hash_code()));
		
		pool->EnableInstance(*accelerator, 0);

		std::vector<Float4x4> defaultSMResults;
		defaultSMResults.resize(resources._modelScaffold->EmbeddedSkeleton().GetOutputMatrixCount());
		resources._modelScaffold->EmbeddedSkeleton().GenerateOutputTransforms(MakeIteratorRange(defaultSMResults), nullptr);
		
		skeletonInterface.FeedInSkeletonMachineResults(0, MakeIteratorRange(defaultSMResults));

		pool->ReadyInstances(*threadContext);
		threadContext->CommitCommands();
		pool->OnFrameBarrier();
	}
#endif

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
				BindFlag::UnorderedAccess|BindFlag::TransferSrc, 0, GPUAccess::Read|GPUAccess::Write,
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
		std::vector<Float4x4> result;
		result.resize(modelScaffold->EmbeddedSkeleton().GetOutputMatrixCount());
		modelScaffold->EmbeddedSkeleton().GenerateOutputTransforms(MakeIteratorRange(result), nullptr);
		return result;
	}

	std::vector<InputElementDesc> AsInputLayout(const RenderCore::Assets::GeoInputAssembly& ia, unsigned slot)
	{
		std::vector<InputElementDesc> result;
		result.resize(ia._elements.size());
		RenderCore::Assets::BuildLowLevelInputAssembly(MakeIteratorRange(result), ia._elements, slot);
		return result;
	}

	static std::vector<uint8_t> RunGPUDeformerDirectly(MetalTestHelper& testHelper, BufferUploads::IManager& bufferUploads, std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold)
	{
		auto pipelineCollection = std::make_shared<Techniques::SkinDeformerPipelineCollection>(std::make_shared<Techniques::PipelineCollection>(testHelper._device));
		const auto& animVB = modelScaffold->ImmutableData()._boundSkinnedControllers[0]._animatedVertexElements;

		std::promise<std::shared_ptr<Techniques::IDeformer>> promise;
		auto future = promise.get_future();
		auto srcLayout = AsInputLayout(animVB._ia, Techniques::Internal::VB_GPUStaticData), dstLayout = AsInputLayout(animVB._ia, Techniques::Internal::VB_PostDeform);
		Techniques::GPUSkinDeformer deformer(pipelineCollection, modelScaffold, "unit-test");
		Techniques::DeformerInputBinding deformInputBinding;
		deformInputBinding._geoBindings.push_back({
			0, srcLayout, dstLayout
		});
		for (unsigned c=0; c<Techniques::Internal::VB_Count; ++c) {
			deformInputBinding._geoBindings[0]._bufferStrides[c] = 0;
			deformInputBinding._geoBindings[0]._bufferOffsets[c] = 0;
		}
		deformInputBinding._geoBindings[0]._bufferStrides[Techniques::Internal::VB_GPUStaticData] = animVB._ia._vertexStride;
		deformInputBinding._geoBindings[0]._bufferStrides[Techniques::Internal::VB_PostDeform] = CalculateVertexStrideForSlot(dstLayout, Techniques::Internal::VB_PostDeform);
		deformer.Bind(deformInputBinding);

		REQUIRE(modelScaffold->ImmutableData()._boundSkinnedControllerCount == 1);
		
		auto inputResource = LoadStorageBuffer(*testHelper._device, *modelScaffold, animVB);
		auto outputResource = testHelper._device->CreateResource(inputResource->GetDesc());

		deformer.FeedInSkeletonMachineResults(
			0, BasePose(modelScaffold),
			deformer.CreateBinding(modelScaffold->EmbeddedSkeleton().GetOutputInterface()));

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
		IDeformer::Metrics metrics;
		deformer.ExecuteGPU(*threadContext, MakeIteratorRange(instances), outputResource->GetDesc()._linearBufferDesc._sizeInBytes, *inputView, *inputView, *outputView, metrics);
		testHelper.EndFrameCapture();

		return outputResource->ReadBackSynchronized(*threadContext);
	}

	static std::vector<Float3> DeformPositionsOnCPU(std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold)
	{
		Techniques::CPUSkinDeformer cpuSkinDeformer { *modelScaffold, {} };

		REQUIRE(modelScaffold->ImmutableData()._boundSkinnedControllerCount == 1);
		auto& animVb = modelScaffold->ImmutableData()._boundSkinnedControllers[0]._animatedVertexElements;
		auto rawInputBuffer = LoadCPUVertexBuffer(*modelScaffold, animVb);

		auto inputFloat3s = AsFloat3s(
			Techniques::Internal::AsVertexElementIteratorRange(
				MakeIteratorRange(rawInputBuffer), 
				*Techniques::Internal::FindElement(animVb._ia._elements, Hash64("POSITION")), 
				animVb._ia._vertexStride));

		std::vector<uint8_t> outputBufferData;
		outputBufferData.resize(inputFloat3s.size() * sizeof(Float3));

		Techniques::DeformerInputBinding::GeoBinding geoBinding;
		geoBinding._geoId = 0;
		geoBinding._inputElements.push_back({"POSITION", 0, Format::R32G32B32_FLOAT, Techniques::Internal::VB_CPUStaticData, 0});
		geoBinding._outputElements.push_back({"POSITION", 0, Format::R32G32B32_FLOAT, Techniques::Internal::VB_PostDeform, 0});
		geoBinding._bufferStrides[Techniques::Internal::VB_CPUStaticData] = sizeof(Float3);
		geoBinding._bufferStrides[Techniques::Internal::VB_PostDeform] = sizeof(Float3);
		cpuSkinDeformer._bindingHelper._inputBinding._geoBindings.push_back(std::move(geoBinding));

		cpuSkinDeformer.FeedInSkeletonMachineResults(
			0, BasePose(modelScaffold),
			cpuSkinDeformer.CreateBinding(modelScaffold->EmbeddedSkeleton().GetOutputInterface()));
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

	TEST_CASE( "SkinDeformCPUVsGPU", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		TechniqueTestApparatus techniqueTestHelper{*testHelper};

		techniqueTestHelper._techniqueServices->GetDeformOperationFactorySet().Register("skin", Techniques::CreateCPUSkinDeformerFactory());

		auto modelScaffold = MakeTestAnimatedModel();

		auto gpuRawBuffer = RunGPUDeformerDirectly(*testHelper, *techniqueTestHelper._bufferUploads, modelScaffold);
		auto cpuPositions = DeformPositionsOnCPU(modelScaffold);

		// Find the positions within the raw GPU output and convert to float3s
		auto gpuPositions = GetFloat3sFromVertexBuffer(MakeIteratorRange(gpuRawBuffer), modelScaffold->ImmutableData()._boundSkinnedControllers[0]._animatedVertexElements._ia, Hash64("POSITION"));

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

	TEST_CASE( "DeformAccelerator", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		TechniqueTestApparatus techniqueTestHelper{*testHelper};

		auto pipelineCollection = std::make_shared<Techniques::PipelineCollection>(testHelper->_device);
		techniqueTestHelper._techniqueServices->GetDeformOperationFactorySet().Register("gpu_skin", Techniques::CreateGPUSkinDeformerFactory(pipelineCollection));
		techniqueTestHelper._techniqueServices->GetDeformOperationFactorySet().Register("cpu_skin", Techniques::CreateCPUSkinDeformerFactory());
		
		auto modelScaffold = MakeTestAnimatedModel();
		
		auto pool = Techniques::CreateDeformAcceleratorPool(testHelper->_device);
		auto cpuAccelerator = pool->CreateDeformAccelerator("cpu_skin", modelScaffold);
		REQUIRE(cpuAccelerator);
		auto rendererBinding = pool->GetDeformerToRendererBinding(*cpuAccelerator);
		REQUIRE(!rendererBinding._geoBindings.empty());

		auto gpuAccelerator = pool->CreateDeformAccelerator("gpu_skin", modelScaffold);
		REQUIRE(gpuAccelerator);
		auto rendererBinding2 = pool->GetDeformerToRendererBinding(*gpuAccelerator);
		REQUIRE(!rendererBinding2._geoBindings.empty());
		REQUIRE(rendererBinding2._geoBindings[0]._generatedElements.size() == 3);
		REQUIRE(rendererBinding2._geoBindings[0]._generatedElements[0]._semanticName == "POSITION");
		REQUIRE(rendererBinding2._geoBindings[0]._generatedElements[1]._semanticName == "NORMAL");
		REQUIRE(rendererBinding2._geoBindings[0]._generatedElements[2]._semanticName == "TEXTANGENT");
	}

	class TestCPUDeformOperator : public Techniques::IDeformer
	{
	public:
		virtual void Execute(
			unsigned instanceIdx,
			IteratorRange<const VertexElementRange*> sourceElements,
			IteratorRange<const VertexElementRange*> destinationElements) const
		{
			REQUIRE(instanceIdx == 0);
			REQUIRE(sourceElements.size() == 2);
			REQUIRE(destinationElements.size() == 3);
		}
		virtual void* QueryInterface(size_t) { return nullptr; }
	};

	TEST_CASE( "CPUDeformInstantiation", "[rendercore_techniques]" )
	{
		auto modelScaffold = MakeTestAnimatedModel();
		auto vertexCount = modelScaffold->ImmutableData()._boundSkinnedControllers[0]._animatedVertexElements._size / modelScaffold->ImmutableData()._boundSkinnedControllers[0]._animatedVertexElements._ia._vertexStride;

		{
			// Single stage deform that takes POSITION & NORMAL and generates 3 arbitrary elements
			Techniques::DeformOperationInstantiation testInst0;
			testInst0._generatedElements.push_back({ "GENERATED", 0, Format::R16G16B16A16_FLOAT });
			testInst0._generatedElements.push_back({ "GENERATED2", 0, Format::R8G8B8A8_UNORM });
			testInst0._generatedElements.push_back({ "GENERATED", 1, Format::R32_UINT });
			testInst0._upstreamSourceElements.push_back({ "POSITION", 0, Format::R32G32B32_FLOAT });
			testInst0._upstreamSourceElements.push_back({ "NORMAL", 0, Format::R8G8B8A8_UNORM });
			testInst0._suppressElements.push_back(Hash64("BADSEMANTIC"));
			testInst0._geoId = 0;
			
			std::vector<DeformOperationInstantiation> instantiations;
			instantiations.push_back(testInst0);

			Techniques::Internal::WorkingDeformer workingDeformer;
			workingDeformer._instantiations = {&testInst0, &testInst0+1};

			Techniques::Internal::DeformBufferIterators bufferIterators;
			auto nascentDeform = Techniques::Internal::CreateDeformBindings(
				{&workingDeformer, &workingDeformer+1}, bufferIterators, true,
				modelScaffold, "unit-test");

			unsigned generatedVertexStride = 8 + 4 + 4;
			unsigned staticDataVertexStride = 12+4;
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_PostDeform] == generatedVertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_CPUStaticData] == staticDataVertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_GPUStaticData] == 0);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_CPUDeformTemporaries] == 0);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_GPUDeformTemporaries] == 0);

			REQUIRE(workingDeformer._inputBinding._geoBindings.size() == 1);
			REQUIRE(workingDeformer._inputBinding._geoBindings[0]._inputElements.size() == 2);
			REQUIRE(workingDeformer._inputBinding._geoBindings[0]._inputElements[0]._nativeFormat == Format::R32G32B32_FLOAT);
			REQUIRE(workingDeformer._inputBinding._geoBindings[0]._inputElements[1]._nativeFormat == Format::R8G8B8A8_UNORM);
			REQUIRE(workingDeformer._inputBinding._geoBindings[0]._inputElements[0]._inputSlot == Techniques::Internal::VB_CPUStaticData);
			REQUIRE(workingDeformer._inputBinding._geoBindings[0]._inputElements[1]._inputSlot == Techniques::Internal::VB_CPUStaticData);
			REQUIRE(workingDeformer._inputBinding._geoBindings[0]._outputElements.size() == 3);
			REQUIRE(workingDeformer._inputBinding._geoBindings[0]._outputElements[0]._nativeFormat == Format::R16G16B16A16_FLOAT);
			REQUIRE(workingDeformer._inputBinding._geoBindings[0]._outputElements[1]._nativeFormat == Format::R8G8B8A8_UNORM);
			REQUIRE(workingDeformer._inputBinding._geoBindings[0]._outputElements[2]._nativeFormat == Format::R32_UINT);
			REQUIRE(workingDeformer._inputBinding._geoBindings[0]._outputElements[0]._inputSlot == Techniques::Internal::VB_PostDeform);
			REQUIRE(workingDeformer._inputBinding._geoBindings[0]._outputElements[1]._inputSlot == Techniques::Internal::VB_PostDeform);
			REQUIRE(workingDeformer._inputBinding._geoBindings[0]._outputElements[2]._inputSlot == Techniques::Internal::VB_PostDeform);
		}

		{
			// 3 deform stages with deformers that consume outputs from previous stages
			Techniques::DeformOperationInstantiation testInst0;
			testInst0._generatedElements.push_back({ "TEMPORARY", 0, Format::R16G16B16A16_FLOAT });
			testInst0._generatedElements.push_back({ "GENERATED2", 0, Format::R8G8B8A8_UNORM });
			testInst0._upstreamSourceElements.push_back({ "POSITION", 0, Format::R32G32B32_FLOAT });
			testInst0._upstreamSourceElements.push_back({ "NORMAL", 0, Format::R8G8B8A8_UNORM });
			testInst0._suppressElements.push_back(Hash64("TANGENT"));
			testInst0._geoId = 0;

			Techniques::DeformOperationInstantiation testInst1;
			testInst1._generatedElements.push_back({ "TEMPORARY", 1, Format::R16G16B16A16_FLOAT });
			testInst1._upstreamSourceElements.push_back({ "POSITION", 0, Format::R32G32B32_FLOAT });
			testInst1._upstreamSourceElements.push_back({ "TEMPORARY", 0, Format::R16G16B16A16_FLOAT });
			testInst1._geoId = 0;

			Techniques::DeformOperationInstantiation testInst2;
			testInst2._generatedElements.push_back({ "GENERATED3", 0, Format::R16G16B16A16_FLOAT });
			testInst2._upstreamSourceElements.push_back({ "TEMPORARY", 1, Format::R16G16B16A16_FLOAT });
			testInst2._suppressElements.push_back(Hash64("TANGENT"));
			testInst2._geoId = 0;
			
			Techniques::Internal::WorkingDeformer deformers[3];
			deformers[0]._instantiations = MakeIteratorRange(&testInst0, &testInst0+1);
			deformers[1]._instantiations = MakeIteratorRange(&testInst1, &testInst1+1);
			deformers[2]._instantiations = MakeIteratorRange(&testInst2, &testInst2+1);

			Techniques::Internal::DeformBufferIterators bufferIterators;
			auto nascentDeform = Techniques::Internal::CreateDeformBindings(
				MakeIteratorRange(deformers), bufferIterators, true,
				modelScaffold, "unit-test");

			unsigned generatedVertexStride = 4+8;				// {"GENERATED2", 0}, {"GENERATED3", 0}
			unsigned staticDataVertexStride = 12+4;
			unsigned temporariesVertexStride = 8+8;				// {"TEMPORARY", 0}, {"TEMPORARY", 1}
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_PostDeform] == generatedVertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_CPUStaticData] == staticDataVertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_GPUStaticData] == 0);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_CPUDeformTemporaries] ==temporariesVertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_GPUDeformTemporaries] == 0);

			REQUIRE(deformers[0]._inputBinding._geoBindings.size() == 1);
			REQUIRE(deformers[0]._inputBinding._geoBindings[0]._inputElements.size() == 2);
			REQUIRE(deformers[0]._inputBinding._geoBindings[0]._inputElements[0]._inputSlot == Techniques::Internal::VB_CPUStaticData);
			REQUIRE(deformers[0]._inputBinding._geoBindings[0]._inputElements[1]._inputSlot == Techniques::Internal::VB_CPUStaticData);
			REQUIRE(deformers[0]._inputBinding._geoBindings[0]._outputElements.size() == 2);
			REQUIRE(deformers[0]._inputBinding._geoBindings[0]._outputElements[0]._inputSlot == Techniques::Internal::VB_CPUDeformTemporaries);
			REQUIRE(deformers[0]._inputBinding._geoBindings[0]._outputElements[1]._inputSlot == Techniques::Internal::VB_PostDeform);

			REQUIRE(deformers[1]._inputBinding._geoBindings.size() == 1);
			REQUIRE(deformers[1]._inputBinding._geoBindings[0]._inputElements.size() == 2);
			REQUIRE(deformers[1]._inputBinding._geoBindings[0]._inputElements[0]._inputSlot == Techniques::Internal::VB_CPUStaticData);
			REQUIRE(deformers[1]._inputBinding._geoBindings[0]._inputElements[1]._inputSlot == Techniques::Internal::VB_CPUDeformTemporaries);
			REQUIRE(deformers[1]._inputBinding._geoBindings[0]._outputElements.size() == 1);
			REQUIRE(deformers[1]._inputBinding._geoBindings[0]._outputElements[0]._inputSlot == Techniques::Internal::VB_CPUDeformTemporaries);

			REQUIRE(deformers[2]._inputBinding._geoBindings.size() == 1);
			REQUIRE(deformers[2]._inputBinding._geoBindings[0]._inputElements.size() == 1);
			REQUIRE(deformers[2]._inputBinding._geoBindings[0]._inputElements[0]._inputSlot == Techniques::Internal::VB_CPUDeformTemporaries);
			REQUIRE(deformers[2]._inputBinding._geoBindings[0]._outputElements.size() == 1);
			REQUIRE(deformers[2]._inputBinding._geoBindings[0]._outputElements[0]._inputSlot == Techniques::Internal::VB_PostDeform);
		}
	}

	class TestGPUDeformOperator : public Techniques::IDeformer
	{
	public:
		virtual void ExecuteGPU(
			IThreadContext& threadContext,
			unsigned instanceIdx,
			const IResourceView& srcVB,
			const IResourceView& deformTemporariesVB,
			const IResourceView& dstVB) const
		{
			REQUIRE(instanceIdx == 0);
		}
		virtual void* QueryInterface(size_t) { return nullptr; }
		virtual ::Assets::DependencyValidation GetDependencyValidation() { return {}; }
	};

	TEST_CASE( "GPUDeformInstantiation", "[rendercore_techniques]" )
	{
		auto modelScaffold = MakeTestAnimatedModel();
		auto vertexCount = modelScaffold->ImmutableData()._boundSkinnedControllers[0]._animatedVertexElements._size / modelScaffold->ImmutableData()._boundSkinnedControllers[0]._animatedVertexElements._ia._vertexStride;

		{
			// Single stage deform, but using a GPU deformer
			Techniques::DeformOperationInstantiation testInst0;
			testInst0._generatedElements.push_back({ "GENERATED2", 0, Format::R8G8B8A8_UNORM });
			testInst0._generatedElements.push_back({ "GENERATED", 1, Format::R32_UINT });
			testInst0._generatedElements.push_back({ "GENERATED", 0, Format::R16G16B16A16_FLOAT });
			testInst0._upstreamSourceElements.push_back({ "POSITION", 0, Format::R32G32B32_FLOAT });
			testInst0._upstreamSourceElements.push_back({ "NORMAL", 0, Format::R8G8B8A8_UNORM });
			/*testInst0._gpuConstructor = [](auto&& promise, auto srcVBLayout, auto deformTemporariesVBLayout, auto dstVBLayout) {
				REQUIRE(deformTemporariesVBLayout.size() == 0);
				REQUIRE(dstVBLayout.size() == 3);
				promise.set_value(std::make_shared<TestGPUDeformOperator>());
			};*/
			testInst0._geoId = 0;
			
			std::vector<DeformOperationInstantiation> instantiations;
			instantiations.push_back(testInst0);

			Techniques::Internal::WorkingDeformer workingDeformer;
			workingDeformer._instantiations = instantiations;

			Techniques::Internal::DeformBufferIterators bufferIterators;
			auto nascentDeform = Techniques::Internal::CreateDeformBindings(
				{&workingDeformer, &workingDeformer+1}, bufferIterators, false,
				modelScaffold, "unit-test");

			// The generated elements get reordered from largest to smallest element
			REQUIRE(nascentDeform._geoBindings.size() == 1);
			REQUIRE(nascentDeform._geoBindings[0]._generatedElements.size() == 3);
			REQUIRE(nascentDeform._geoBindings[0]._generatedElements[0]._semanticName == "GENERATED");
			REQUIRE(nascentDeform._geoBindings[0]._generatedElements[1]._semanticName == "GENERATED2");
			REQUIRE(nascentDeform._geoBindings[0]._generatedElements[2]._semanticName == "GENERATED");

			REQUIRE(nascentDeform._geoBindings[0]._suppressedElements.size() == 3);
			REQUIRE(std::find(nascentDeform._geoBindings[0]._suppressedElements.begin(), nascentDeform._geoBindings[0]._suppressedElements.end(), Hash64("GENERATED")) != nascentDeform._geoBindings[0]._suppressedElements.end());
			REQUIRE(std::find(nascentDeform._geoBindings[0]._suppressedElements.begin(), nascentDeform._geoBindings[0]._suppressedElements.end(), Hash64("GENERATED2")) != nascentDeform._geoBindings[0]._suppressedElements.end());
			REQUIRE(std::find(nascentDeform._geoBindings[0]._suppressedElements.begin(), nascentDeform._geoBindings[0]._suppressedElements.end(), Hash64("GENERATED")+1) != nascentDeform._geoBindings[0]._suppressedElements.end());
		
			unsigned generatedVertexStride = 8+4+4;
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_PostDeform] == generatedVertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_CPUStaticData] == 0);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_GPUStaticData] == modelScaffold->ImmutableData()._boundSkinnedControllers[0]._animatedVertexElements._ia._vertexStride*vertexCount);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_CPUDeformTemporaries] == 0);
			REQUIRE(bufferIterators._bufferIterators[Techniques::Internal::VB_GPUDeformTemporaries] == 0);
		}
	}
	
}
