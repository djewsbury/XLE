// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineTestHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../../RenderCore/LightingEngine/LightScene.h"
#include "../../../RenderCore/LightingEngine/ForwardLightingDelegate.h"
#include "../../../RenderCore/LightingEngine/DeferredLightingDelegate.h"
#include "../../../RenderCore/LightingEngine/StandardLightOperators.h"
#include "../../../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../../../RenderCore/LightingEngine/SunSourceConfiguration.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../RenderCore/Techniques/PipelineCollection.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/QueryPool.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
#include "../../../RenderCore/IThreadContext.h"
#include "../../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../../Math/Transformations.h"
#include "../../../Assets/IAsyncMarker.h"
#include "../../../Assets/Assets.h"
#include "../../../xleres/FileList.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	static RenderCore::LightingEngine::ILightScene::LightSourceId CreateTestLight(RenderCore::LightingEngine::ILightScene& lightScene, Float3 lightPosition, unsigned lightingOperator)
	{
		using namespace RenderCore::LightingEngine;
		auto lightId = lightScene.CreateLightSource(lightingOperator);

		auto* positional = lightScene.TryGetLightSourceInterface<IPositionalLightSource>(lightId);
		REQUIRE(positional);
		positional->SetLocalToWorld(AsFloat4x4(UniformScaleYRotTranslation{0.05f, 0.f, lightPosition}));

		auto* emittance = lightScene.TryGetLightSourceInterface<IUniformEmittance>(lightId);
		REQUIRE(emittance);
		emittance->SetBrightness(Float3(100.f, 100.f, 100.f));

		auto* finite = lightScene.TryGetLightSourceInterface<IFiniteLightSource>(lightId);
		if (finite)
			finite->SetCutoffRange(7.5f);

		return lightId;
	}

	template<typename Type>
		static std::shared_ptr<Type> StallAndRequireReady(::Assets::FuturePtr<Type>& future)
	{
		future.StallWhilePending();
		INFO(::Assets::AsString(future.GetActualizationLog()));
		REQUIRE(future.GetAssetState() == ::Assets::AssetState::Ready);
		return future.Actualize();
	}

	struct LightingOperatorsPipelineLayout
	{
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayoutFile> _pipelineLayoutFile;
		std::shared_ptr<RenderCore::ICompiledPipelineLayout> _pipelineLayout;
		std::shared_ptr<RenderCore::Techniques::GraphicsPipelineCollection> _pipelineCollection;

		LightingOperatorsPipelineLayout(const MetalTestHelper& testHelper)
		{	
			auto pipelineLayoutFileFuture = ::Assets::MakeAsset<RenderCore::Assets::PredefinedPipelineLayoutFile>(LIGHTING_OPERATOR_PIPELINE);
			_pipelineLayoutFile = StallAndRequireReady(*pipelineLayoutFileFuture);

			const std::string pipelineLayoutName = "LightingOperator";
			auto i = _pipelineLayoutFile->_pipelineLayouts.find(pipelineLayoutName);
			if (i == _pipelineLayoutFile->_pipelineLayouts.end())
				Throw(std::runtime_error("Did not find pipeline layout with the name " + pipelineLayoutName + " in the given pipeline layout file"));
			auto pipelineInit = i->second->MakePipelineLayoutInitializer(testHelper._shaderCompiler->GetShaderLanguage());
			_pipelineLayout = testHelper._device->CreatePipelineLayout(pipelineInit);

			_pipelineCollection = std::make_shared<RenderCore::Techniques::GraphicsPipelineCollection>(testHelper._device, _pipelineLayout);
		}
	};

	static void PrepareResources(ToolsRig::IDrawablesWriter& drawablesWriter, LightingEngineTestApparatus& testApparatus, RenderCore::LightingEngine::CompiledLightingTechnique& lightingTechnique)
	{
		// stall until all resources are ready
		RenderCore::LightingEngine::LightingTechniqueInstance prepareLightingIterator(*testApparatus._pipelineAcceleratorPool, lightingTechnique);
		ParseScene(prepareLightingIterator, drawablesWriter);
		auto prepareMarker = prepareLightingIterator.GetResourcePreparationMarker();
		if (prepareMarker) {
			prepareMarker->StallWhilePending();
			REQUIRE(prepareMarker->GetAssetState() == ::Assets::AssetState::Ready);
		}
	}

	static void PumpBufferUploads(LightingEngineTestApparatus& testApparatus)
	{
		auto& immContext= *testApparatus._metalTestHelper->_device->GetImmediateContext();
		testApparatus._bufferUploads->Update(immContext);
		Threading::Sleep(16);
		testApparatus._bufferUploads->Update(immContext);
	}

	static unsigned CountPixelShaderInvocations(
		RenderCore::IThreadContext& threadContext, 
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::LightingEngine::CompiledLightingTechnique& lightingTechnique,
		LightingEngineTestApparatus& testApparatus,
		ToolsRig::IDrawablesWriter& drawableWriter)
	{ 
		using namespace RenderCore;
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		Metal::QueryPool statsQuery { Metal::GetObjectFactory(), Metal::QueryPool::QueryType::ShaderInvocations, 8 };
		auto query = statsQuery.Begin(metalContext);

		{
			RenderCore::LightingEngine::LightingTechniqueInstance lightingIterator(
				threadContext, parsingContext, *testApparatus._pipelineAcceleratorPool, lightingTechnique);
			ParseScene(lightingIterator, drawableWriter);
		}

		statsQuery.End(metalContext, query);
		threadContext.CommitCommands();

		Metal::QueryPool::QueryResult_ShaderInvocations shaderInvocationsCount;
		if (statsQuery.GetResults_Stall(metalContext, query, MakeOpaqueIteratorRange(shaderInvocationsCount))) {
			return shaderInvocationsCount._invocations[(unsigned)ShaderStage::Pixel];
		}
		return 0;
	}

	TEST_CASE( "LightingEngine-LightShapeStencil", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();

		auto threadContext = testHelper->_device->GetImmediateContext();

		RenderCore::Techniques::CameraDesc camera;
		camera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, -1.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, 1.0f}), Float3{0.0f, 10.f, 0.0f});
		camera._projection = Techniques::CameraDesc::Projection::Orthogonal;
		camera._nearClip = 0.0f;
		camera._farClip = 100.f;		// a small far clip here reduces the impact of gbuffer reconstruction accuracy on sampling
		camera._left = -10.f;
		camera._top = 10.f;
		camera._right = 10.f;
		camera._bottom = -10.f;
		
		testHelper->BeginFrameCapture();

		{
			LightingOperatorsPipelineLayout pipelineLayout(*testHelper);

			LightingEngine::LightSourceOperatorDesc resolveOperators[] {
				LightingEngine::LightSourceOperatorDesc{ LightingEngine::LightSourceShape::Sphere },
				LightingEngine::LightSourceOperatorDesc{ LightingEngine::LightSourceShape::Tube },
				LightingEngine::LightSourceOperatorDesc{ LightingEngine::LightSourceShape::Rectangle },
				LightingEngine::LightSourceOperatorDesc{ LightingEngine::LightSourceShape::Disc },
				LightingEngine::LightSourceOperatorDesc{ LightingEngine::LightSourceShape::Sphere, LightingEngine::DiffuseModel::Disney, LightingEngine::LightSourceOperatorDesc::Flags::NeverStencil },
			};

			auto targetDesc = CreateDesc(
				BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
				TextureDesc::Plain2D(2048, 2048, RenderCore::Format::R8G8B8A8_UNORM),
				"temporary-out");

			auto parsingContext = InitializeParsingContext(*testApparatus._techniqueContext, targetDesc, camera);
			auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
			auto lightingTechniqueFuture = LightingEngine::CreateDeferredLightingTechnique(
				testHelper->_device,
				testApparatus._pipelineAcceleratorPool, testApparatus._sharedDelegates, pipelineLayout._pipelineCollection, pipelineLayout._pipelineLayoutFile,
				MakeIteratorRange(resolveOperators), {}, 
				stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps);
			auto lightingTechnique = StallAndRequireReady(*lightingTechniqueFuture);
			PumpBufferUploads(testApparatus);

			auto drawableWriter = ToolsRig::CreateFlatPlaneDrawableWriter(*testHelper->_device, *testApparatus._pipelineAcceleratorPool);
			PrepareResources(*drawableWriter, testApparatus, *lightingTechnique);

			///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			SECTION("sphere light")
			{
				auto& lightScene = LightingEngine::GetLightScene(*lightingTechnique);
				auto baseInvocations = CountPixelShaderInvocations(*threadContext, parsingContext, *lightingTechnique, testApparatus, *drawableWriter);

				auto lightId = CreateTestLight(lightScene, {0.f, 2.0f, 0.f}, 4);
				auto dontStencilCount = CountPixelShaderInvocations(*threadContext, parsingContext, *lightingTechnique, testApparatus, *drawableWriter);
				lightScene.DestroyLightSource(lightId);

				lightId = CreateTestLight(lightScene, {0.f, 2.0f, 0.f}, 0);
				auto stencilLowLight = CountPixelShaderInvocations(*threadContext, parsingContext, *lightingTechnique, testApparatus, *drawableWriter);
				lightScene.DestroyLightSource(lightId);

				lightId = CreateTestLight(lightScene, {0.f, 6.0f, 8.f}, 0);
				auto stencilMedLight = CountPixelShaderInvocations(*threadContext, parsingContext, *lightingTechnique, testApparatus, *drawableWriter);
				lightScene.DestroyLightSource(lightId);

				lightId = CreateTestLight(lightScene, {0.f, 8.0f, 0.f}, 0);
				auto stencilHighLight = CountPixelShaderInvocations(*threadContext, parsingContext, *lightingTechnique, testApparatus, *drawableWriter);
				lightScene.DestroyLightSource(lightId);

				REQUIRE(stencilHighLight == baseInvocations);		// depth bounds should prevent this "high light" from effect any pixels
				REQUIRE(stencilHighLight < stencilMedLight);
				REQUIRE(stencilMedLight < stencilLowLight);			// because were using orthogonal projection, we won't see a big different between low and mid lights -- but we shift one off the to the side a bit
				REQUIRE(stencilLowLight < dontStencilCount);
			}
		}

		testHelper->EndFrameCapture();
	}
}
