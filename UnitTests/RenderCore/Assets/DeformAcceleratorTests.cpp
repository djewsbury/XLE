// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "TechniqueTestsHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Assets/ModelScaffold.h"
#include "../../../RenderCore/Techniques/DeformAccelerator.h"
#include "../../../RenderCore/Techniques/DeformAcceleratorInternal.h"
#include "../../../RenderCore/Techniques/SimpleModelDeform.h"
#include "../../../RenderCore/Techniques/SkinDeformer.h"
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
			IteratorRange<const std::shared_ptr<ICPUDeformOperator>*> skinDeformers)
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
			std::shared_ptr<ICPUDeformOperator> _skinDeformer;
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
		auto skinController = std::make_shared<GeoProc::UnboundSkinController>(std::move(inverseBindMatrices), Identity<Float4x4>(), std::move(jointNames));
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

	static IResourcePtr LoadVertexBuffer(
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
		return RenderCore::Techniques::CreateStaticVertexBuffer(
			device,
			MakeIteratorRange(buffer.get(), PtrAdd(buffer.get(), vb._size)));
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

	static std::vector<uint8_t> RunGPUDeformerDirectly(MetalTestHelper& testHelper, std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold)
	{
		auto pipelineCollection = std::make_shared<Techniques::PipelineCollection>(testHelper._device);

		Techniques::GPUSkinDeformer deformer { *testHelper._device, pipelineCollection, modelScaffold, 0 };

		REQUIRE(modelScaffold->ImmutableData()._boundSkinnedControllerCount == 1);
		const auto& animVB = modelScaffold->ImmutableData()._boundSkinnedControllers[0]._animatedVertexElements;
		auto inputResource = LoadVertexBuffer(*testHelper._device, *modelScaffold, animVB);
		auto outputResource = testHelper._device->CreateResource(inputResource->GetDesc());

		deformer.FeedInSkeletonMachineResults(
			0, BasePose(modelScaffold),
			deformer.CreateBinding(modelScaffold->EmbeddedSkeleton().GetOutputInterface()));

		auto inputView = inputResource->CreateBufferView(BindFlag::UnorderedAccess);
		auto outputView = outputResource->CreateBufferView(BindFlag::UnorderedAccess);

		auto threadContext = testHelper._device->GetImmediateContext();
		testHelper.BeginFrameCapture();
		deformer.ExecuteGPU(*threadContext, 0, *inputView, *inputView, *outputView);
		testHelper.EndFrameCapture();

		return outputResource->ReadBackSynchronized(*threadContext);
	}

	static std::vector<Float3> DeformPositionsOnGPU(std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold)
	{
		Techniques::SkinDeformer cpuSkinDeformer { *modelScaffold, 0 };

		REQUIRE(modelScaffold->ImmutableData()._boundSkinnedControllerCount == 1);
		auto& animVb = modelScaffold->ImmutableData()._boundSkinnedControllers[0]._animatedVertexElements;
		auto rawInputBuffer = LoadCPUVertexBuffer(*modelScaffold, animVb);

		auto inputFloat3s = AsFloat3s(
			Techniques::Internal::AsVertexElementIteratorRange(
				MakeIteratorRange(rawInputBuffer), 
				*Techniques::Internal::FindElement(animVb._ia._elements, Hash64("POSITION")), 
				animVb._ia._vertexStride));
		
		std::vector<Techniques::ICPUDeformOperator::VertexElementRange> sourceElements;
		sourceElements.push_back(
			Techniques::Internal::AsVertexElementIteratorRange(MakeIteratorRange(inputFloat3s), Format::R32G32B32_FLOAT, 0, sizeof(Float3)));

		std::vector<uint8_t> outputBufferData;
		outputBufferData.resize(sourceElements[0].size() * sizeof(Float3));
		std::vector<Techniques::ICPUDeformOperator::VertexElementRange> destinationElements;
		destinationElements.push_back(
			Techniques::Internal::AsVertexElementIteratorRange(MakeIteratorRange(outputBufferData), Format::R32G32B32_FLOAT, 0, sizeof(Float3)));

		cpuSkinDeformer.FeedInSkeletonMachineResults(
			0, BasePose(modelScaffold),
			cpuSkinDeformer.CreateBinding(modelScaffold->EmbeddedSkeleton().GetOutputInterface()));
		cpuSkinDeformer.Execute(0, sourceElements, destinationElements);

		return AsFloat3s(destinationElements[0]);
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

	TEST_CASE( "DeformAccelerators", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		TechniqueTestApparatus techniqueTestHelper{*testHelper};

		techniqueTestHelper._techniqueServices->GetDeformOperationFactory().RegisterDeformOperation("skin", Techniques::SkinDeformer::InstantiationFunction);

		auto modelScaffold = MakeTestAnimatedModel();

		auto gpuRawBuffer = RunGPUDeformerDirectly(*testHelper, modelScaffold);
		auto cpuPositions = DeformPositionsOnGPU(modelScaffold);

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
	}
	
}
