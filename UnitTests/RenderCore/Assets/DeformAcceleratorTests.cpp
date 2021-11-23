// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "TechniqueTestsHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Assets/ModelScaffold.h"
#include "../../../RenderCore/Techniques/DeformAccelerator.h"
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
			IteratorRange<const std::shared_ptr<IDeformOperation>*> skinDeformers)
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
			std::shared_ptr<IDeformOperation> _skinDeformer;
			RenderCore::Assets::SkeletonBinding _deformerBindings;
		};
		std::vector<Deformer> _deformers;
	};

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

	struct TestAnimatedModelResources
	{
		std::shared_ptr<RenderCore::Assets::ModelScaffold> _modelScaffold;
	};
	TestAnimatedModelResources MakeTestAnimatedModelResources()
	{
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
		GeoProc::NascentObjectGuid mainObjId{0}, controllerId{1};
		model.Add(
			mainObjId, "",
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
			float weightTotal = 0.f;
			unsigned indices[] { 0, 1, 2, 3, 4, 5, 6, 7 };
			for (unsigned bone=0; bone<8; ++bone) {
				float distance = Magnitude(s_cubeCorners[bone] - s_cubeCorners[vertex]);
				weightTotal += weights[bone] = std::max(0.f, 1.f - 0.25f*distance);
			}
			for (unsigned bone=0; bone<8; ++bone)
				weights[bone] /= weightTotal;
			skinController->AddInfluences(vertex, weights, indices);
		}
		model.Add(
			controllerId, "",
			GeoProc::NascentModel::SkinControllerBlock {
				std::move(skinController),
				"skinning"
			});

		model.Add(
			3, "",
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

		TestAnimatedModelResources result;
		result._modelScaffold = ::Assets::AutoConstructAsset<std::shared_ptr<RenderCore::Assets::ModelScaffold>>(*artifactCollection);
		return result;
	}

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

	TEST_CASE( "DeformAccelerators", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		TechniqueTestApparatus techniqueTestHelper{*testHelper};

		techniqueTestHelper._techniqueServices->GetDeformOperationFactory().RegisterDeformOperation("skin", Techniques::SkinDeformer::InstantiationFunction);

		testHelper->BeginFrameCapture();

		auto resources = MakeTestAnimatedModelResources();
		// RunDeformAcceleratorsTest(testHelper->_device, resources);

		auto pipelineCollection = std::make_shared<Techniques::PipelineCollection>(testHelper->_device);

		Techniques::GPUSkinDeformer deformer { *testHelper->_device, pipelineCollection, *resources._modelScaffold, 0 };

		const auto& animVB = resources._modelScaffold->ImmutableData()._boundSkinnedControllers[0]._animatedVertexElements;
		auto inputResource = LoadVertexBuffer(*testHelper->_device, *resources._modelScaffold, animVB);
		auto outputResource = testHelper->_device->CreateResource(inputResource->GetDesc());

		Float4x4 skeletonOutput[resources._modelScaffold->EmbeddedSkeleton().GetOutputMatrixCount()];
		resources._modelScaffold->EmbeddedSkeleton().GenerateOutputTransforms(
			MakeIteratorRange(skeletonOutput, &skeletonOutput[resources._modelScaffold->EmbeddedSkeleton().GetOutputMatrixCount()]), nullptr);
		auto binding = deformer.CreateBinding(resources._modelScaffold->EmbeddedSkeleton().GetOutputInterface());
		deformer.FeedInSkeletonMachineResults(
			0, MakeIteratorRange(skeletonOutput, &skeletonOutput[resources._modelScaffold->EmbeddedSkeleton().GetOutputMatrixCount()]),
			binding);

		auto inputView = inputResource->CreateBufferView(BindFlag::UnorderedAccess);
		auto outputView = outputResource->CreateBufferView(BindFlag::UnorderedAccess);
		deformer.ExecuteGPU(*threadContext, 0, *inputView, *outputView);

		testHelper->EndFrameCapture();
	}
	
}
