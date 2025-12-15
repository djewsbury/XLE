// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineTestHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../SceneEngine/IScene.h"
#include "../../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../../RenderCore/LightingEngine/ILightScene.h"
#include "../../../RenderCore/LightingEngine/ForwardLightingDelegate.h"
#include "../../../RenderCore/LightingEngine/DeferredLightingDelegate.h"
#include "../../../RenderCore/LightingEngine/StandardLightOperators.h"
#include "../../../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../../RenderCore/Techniques/CommonBindings.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../../Math/Transformations.h"
#include "../../../Math/ProjectionMath.h"
#include "../../../Assets/Assets.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	static RenderCore::LightingEngine::ILightScene::LightSourceId CreateTestLight(RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightOperatorId opId)
	{
		using namespace RenderCore::LightingEngine;
		auto lightId = lightScene.CreateLightSource(opId);

		auto* positional = lightScene.TryGetLightSourceInterface<IPositionalLightSource>(lightId);
		REQUIRE(positional);
		ScaleRotationTranslationM srt{Float3(0.03f, 0.03f, 0.03f), Identity<Float3x3>(), Float3{0.f, 1.0f, 0.f}};
		positional->SetLocalToWorld(AsFloat4x4(srt));

		auto* emittance = lightScene.TryGetLightSourceInterface<IUniformEmittance>(lightId);
		REQUIRE(emittance);
		emittance->SetBrightness(Float3(10.f, 10.f, 10.f));

		return lightId;
	}

	const float shadowDepthRange = 100.f;
	const float shadowFrustumWidth = 4.0f;

	static void CreateTestShadowProjection(RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId lightSourceId)
	{
		using namespace RenderCore::LightingEngine;
		auto* projections = lightScene.TryGetLightSourceInterface<IOrthoShadowProjections>(lightSourceId);
		REQUIRE(projections);

		auto camToWorld = MakeCameraToWorld(Float3{0.f, -1.0f, 0.f}, Float3{0.f, 0.0f, 1.f}, Float3{0.f, 10.0f, 0.f});
		projections->SetWorldToOrthoView(InvertOrthonormalTransform(camToWorld));

		IOrthoShadowProjections::OrthoSubProjection subProj[] = {
			{ Float3{-shadowFrustumWidth/2.0f, shadowFrustumWidth/2.0f, 0.0f}, Float3{shadowFrustumWidth/2.0f, -shadowFrustumWidth/2.0f, shadowDepthRange} }
		};
		projections->SetOrthoSubProjections(MakeIteratorRange(subProj));

		IDepthTextureResolve::Desc desc;
		desc._worldSpaceResolveBias = 0.f;
        desc._tanBlurAngle = 0.00436f;
        desc._minBlurSearch = 0.5f;
        desc._maxBlurSearch = 25.f;
		auto* preparer = lightScene.TryGetLightSourceInterface<IDepthTextureResolve>(lightSourceId);
		REQUIRE(preparer);
		preparer->SetDesc(desc);
	}

	static void ConfigureLightScene(RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightOperatorId opId)
	{
		auto srcId = CreateTestLight(lightScene, opId);
		CreateTestShadowProjection(lightScene, srcId);
	}

	static void CreateSphereShadowProjection(RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId lightSourceId)
	{
		using namespace RenderCore::LightingEngine;
		auto* positional = lightScene.TryGetLightSourceInterface<IPositionalLightSource>(lightSourceId);
		REQUIRE(positional);

		auto* finite = lightScene.TryGetLightSourceInterface<IFiniteLightSource>(lightSourceId);
		REQUIRE(finite);

		// Build 6 projection for the cube faces
		Float4x4 worldToCamera[6];
		Float4x4 cameraToProjection[6];
		for (unsigned c=0; c<6; ++c) {
			std::tie(worldToCamera[c], cameraToProjection[c]) =
				CubemapViewAndProjection(
					c,
					ExtractTranslation(positional->GetLocalToWorld()),
					0.01f, finite->GetCutoffRange(),
					RenderCore::Techniques::GetGeometricCoordinateSpaceForCubemaps(),
					RenderCore::Techniques::GetDefaultClipSpaceType());
		}

		auto* projections = lightScene.TryGetLightSourceInterface<IArbitraryShadowProjections>(lightSourceId);
		REQUIRE(projections);
		projections->SetArbitrarySubProjections(MakeIteratorRange(worldToCamera), MakeIteratorRange(cameraToProjection));
	}

	static void StallAndPrepareResources(
		LightingEngineTestApparatus& testApparatus,
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::LightingEngine::CompiledLightingTechnique& lightingTechnique,
		ToolsRig::IDrawablesWriter& drawablesWriter)
	{
		// stall until all resources are ready
		using namespace RenderCore;
		auto prepareLightingIterator = LightingEngine::BeginPrepareResourcesInstance(*testApparatus._pipelineAccelerators, lightingTechnique);
		ParseScene(prepareLightingIterator, drawablesWriter);
		std::promise<Techniques::PreparedResourcesVisibility> preparePromise;
		auto prepareFuture = preparePromise.get_future();
		prepareLightingIterator.FulfillWhenNotPending(std::move(preparePromise));
		auto newVisibility = PrepareAndStall(testApparatus, parsingContext.GetThreadContext(), std::move(prepareFuture));
		parsingContext.SetPipelineAcceleratorsVisibility(newVisibility._pipelineAcceleratorsVisibility);
		parsingContext.RequireCommandList(newVisibility._bufferUploadsVisibility);
	}

	TEST_CASE( "LightingEngine-ExecuteTechnique", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();

		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc,
			TextureDesc::Plain2D(256, 256, RenderCore::Format::R8G8B8A8_UNORM_SRGB));
		
		auto threadContext = testHelper->_device->GetImmediateContext();
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);

		// auto drawableWriter = CreateSphereDrawablesWriter(*testHelper, *testApparatus._pipelineAcceleratorPool);
		auto drawableWriter = ToolsRig::DrawablesWriterHelper(*testHelper->_device, *testApparatus._drawablesPool, *testApparatus._pipelineAccelerators).CreateShapeStackDrawableWriter();

		RenderCore::Techniques::CameraDesc camera;
		// camera._cameraToWorld = MakeCameraToWorld(Float3{1.0f, 0.0f, 0.0f}, Float3{0.0f, 1.0f, 0.0f}, Float3{-3.33f, 0.f, 0.f});
		camera._cameraToWorld = MakeCameraToWorld(-Normalize(Float3{-8.0f, 5.f, 0.f}), Float3{0.0f, 1.0f, 0.0f}, Float3{-8.0f, 5.f, 0.f});
		// camera._cameraToWorld = MakeCameraToWorld(-Normalize(Float3{-8.0f, 0.f, 0.f}), Float3{0.0f, 1.0f, 0.0f}, Float3{-8.0f, 0.f, 0.f});

		const bool orthogonalProjection = true;
		if (orthogonalProjection) {
			camera._projection = Techniques::CameraDesc::Projection::Orthogonal;
			camera._nearClip = 0.f;
			camera._farClip = 100.f;
			camera._left = -3.0f;
			camera._top = 3.0f;
			camera._right = 3.0f;
			camera._bottom = -3.0f;
		}

		auto parsingContext = BeginParsingContext(testApparatus, *threadContext, targetDesc, camera);
		parsingContext.BindAttachment(Techniques::AttachmentSemantics::ColorLDR, fbHelper.GetMainTarget(), BindFlag::RenderTarget);

		testHelper->BeginFrameCapture();

		LightingEngine::ShadowOperatorDesc shadowOp;
		shadowOp._projectionMode = LightingEngine::ShadowProjectionMode::Ortho;

		float wsDepthResolution = shadowDepthRange / 16384.f;
		float wsXYRange = shadowFrustumWidth / 2048.f;
		float ratio0 = wsXYRange / wsDepthResolution;
		float ratio1 = std::sqrt(wsXYRange*wsXYRange + wsXYRange*wsXYRange) / wsDepthResolution;
		shadowOp._singleSidedBias._depthBias = (int)std::ceil(ratio1);
		shadowOp._singleSidedBias._slopeScaledBias = 0.5f;
		(void)ratio0;

		SceneEngine::MergedLightingEngineCfg mergedLightingCfg;
		auto lightOpId = mergedLightingCfg.Register(LightingEngine::PositionalLightOperatorDesc{}, shadowOp);
		assert(lightOpId == 0);

		///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		SECTION("Forward lighting")
		{
			auto& stitchingContext = parsingContext.GetFragmentStitchingContext();

			std::promise<std::shared_ptr<LightingEngine::ILightScene>> promisedLightScene;
			std::shared_future<std::shared_ptr<LightingEngine::ILightScene>> futureLightScene = promisedLightScene.get_future();
			LightingEngine::CreateForwardPlusLightScene(
				std::move(promisedLightScene),
				testApparatus._pipelineAccelerators, testApparatus._pipelineCollection, testApparatus._sharedDelegates,
				mergedLightingCfg.GetChainedGlobalOperators());

			std::promise<std::shared_ptr<LightingEngine::CompiledLightingTechnique>> promisedLightingTechnique;
			auto futureLightingTechnique = promisedLightingTechnique.get_future();
			LightingEngine::CreateForwardLightingTechnique(
				std::move(promisedLightingTechnique),
				testApparatus._pipelineAccelerators, testApparatus._pipelineCollection, testApparatus._sharedDelegates,
				mergedLightingCfg.GetChainedGlobalOperators(),
				futureLightScene,
				stitchingContext.GetPreregisteredAttachments());
			auto lightingTechnique = futureLightingTechnique.get();		// stall
			auto& lightScene = *LightingEngine::TryGetLightScene(*lightingTechnique);

			ConfigureLightScene(lightScene, lightOpId);

			StallAndPrepareResources(testApparatus, parsingContext, *lightingTechnique, *drawableWriter);

			{
				auto lightingIterator = RenderCore::LightingEngine::BeginLightingTechniquePlayback(
					parsingContext, *lightingTechnique);
				ParseScene(lightingIterator, *drawableWriter);
			}

			if (parsingContext._requiredBufferUploadsCommandList)
				testApparatus._bufferUploads->StallAndMarkCommandListDependency(*threadContext, parsingContext._requiredBufferUploadsCommandList);

			fbHelper.SaveImage(*threadContext, "forward-lighting-output");
		}

		///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		SECTION("Deferred lighting")
		{
			auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
			std::promise<std::shared_ptr<LightingEngine::CompiledLightingTechnique>> promisedLightingTechnique;
			auto lightingTechniqueFuture = promisedLightingTechnique.get_future();
			LightingEngine::CreateDeferredLightingTechnique(
				std::move(promisedLightingTechnique),
				testApparatus._pipelineAccelerators, testApparatus._pipelineCollection, testApparatus._sharedDelegates,
				mergedLightingCfg.GetChainedGlobalOperators(),
				stitchingContext.GetPreregisteredAttachments());
			auto lightingTechnique = lightingTechniqueFuture.get();
			ConfigureLightScene(*LightingEngine::TryGetLightScene(*lightingTechnique), lightOpId);

			StallAndPrepareResources(testApparatus, parsingContext, *lightingTechnique, *drawableWriter);

			{
				auto lightingIterator = LightingEngine::BeginLightingTechniquePlayback(
					parsingContext, *lightingTechnique);
				ParseScene(lightingIterator, *drawableWriter);
			}

			if (parsingContext._requiredBufferUploadsCommandList)
				testApparatus._bufferUploads->StallAndMarkCommandListDependency(*threadContext, parsingContext._requiredBufferUploadsCommandList);

			fbHelper.SaveImage(*threadContext, "deferred-lighting-output");
		}

		testHelper->EndFrameCapture();
	}

	TEST_CASE( "LightingEngine-SphereLightShadows", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();

		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc,
			TextureDesc::Plain2D(2048, 2048, RenderCore::Format::R8G8B8A8_UNORM_SRGB));
		
		auto threadContext = testHelper->_device->GetImmediateContext();
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);

		auto drawableWriter = ToolsRig::DrawablesWriterHelper(*testHelper->_device, *testApparatus._drawablesPool, *testApparatus._pipelineAccelerators).CreateStonehengeDrawableWriter();

		RenderCore::Techniques::CameraDesc camera;
		camera._cameraToWorld = MakeCameraToWorld(-Normalize(Float3{-8.0f, 5.f, 0.f}), Float3{0.0f, 1.0f, 0.0f}, Float3{-8.0f, 5.f, 0.f});
		
		auto parsingContext = BeginParsingContext(testApparatus, *threadContext, targetDesc, camera);
		parsingContext.BindAttachment(Techniques::AttachmentSemantics::ColorLDR, fbHelper.GetMainTarget(), BindFlag::RenderTarget);

		testHelper->BeginFrameCapture();

		{
			LightingEngine::ShadowOperatorDesc shadowOpDesc;
			shadowOpDesc._projectionMode = LightingEngine::ShadowProjectionMode::ArbitraryCubeMap;
			shadowOpDesc._normalProjCount = 6;
			shadowOpDesc._width = 256;
			shadowOpDesc._height = 256;

			SceneEngine::MergedLightingEngineCfg mergedLightingCfg;
			auto lightOpId = mergedLightingCfg.Register(LightingEngine::PositionalLightOperatorDesc { LightingEngine::LightSourceShape::Sphere }, shadowOpDesc);
			assert(lightOpId == 0);

			auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
			std::promise<std::shared_ptr<LightingEngine::CompiledLightingTechnique>> promisedLightingTechnique;
			auto lightingTechniqueFuture = promisedLightingTechnique.get_future();
			LightingEngine::CreateDeferredLightingTechnique(
				std::move(promisedLightingTechnique),
				testApparatus._pipelineAccelerators, testApparatus._pipelineCollection, testApparatus._sharedDelegates,
				mergedLightingCfg.GetChainedGlobalOperators(),
				stitchingContext.GetPreregisteredAttachments());
			auto lightingTechnique = lightingTechniqueFuture.get();

			auto& lightScene = *LightingEngine::TryGetLightScene(*lightingTechnique);
			auto lightId = CreateTestLight(lightScene, lightOpId);
			CreateSphereShadowProjection(lightScene, lightId);

			// stall until all resources are ready
			{
				auto prepareLightingIterator = RenderCore::LightingEngine::BeginPrepareResourcesInstance(*testApparatus._pipelineAccelerators, *lightingTechnique);
				ParseScene(prepareLightingIterator, *drawableWriter);
				std::promise<Techniques::PreparedResourcesVisibility> preparePromise;
				auto prepareFuture = preparePromise.get_future();
				prepareLightingIterator.FulfillWhenNotPending(std::move(preparePromise));
				auto newVisibility = PrepareAndStall(testApparatus, *threadContext, std::move(prepareFuture));
				parsingContext.SetPipelineAcceleratorsVisibility(newVisibility._pipelineAcceleratorsVisibility);
				parsingContext.RequireCommandList(newVisibility._bufferUploadsVisibility);
			}

			{
				auto lightingIterator = RenderCore::LightingEngine::BeginLightingTechniquePlayback(
					parsingContext, *lightingTechnique);
				ParseScene(lightingIterator, *drawableWriter);
			}

			if (parsingContext._requiredBufferUploadsCommandList)
				testApparatus._bufferUploads->StallAndMarkCommandListDependency(*threadContext, parsingContext._requiredBufferUploadsCommandList);

			fbHelper.SaveImage(*threadContext, "sphere-light-shadows-output");
		}

		testHelper->EndFrameCapture();
	}

}
