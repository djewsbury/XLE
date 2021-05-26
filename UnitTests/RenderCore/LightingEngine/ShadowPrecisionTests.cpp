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
#include "../../../RenderCore/Techniques/CommonBindings.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../RenderCore/Techniques/RenderPassUtils.h"
#include "../../../RenderCore/Techniques/PipelineCollection.h"
#include "../../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../RenderCore/IThreadContext.h"
#include "../../../RenderOverlays/OverlayContext.h"
#include "../../../RenderOverlays/DebuggingDisplay.h"
#include "../../../RenderOverlays/FontRendering.h"
#include "../../../Math/Transformations.h"
#include "../../../Math/ProjectionMath.h"
#include "../../../Math/Geometry.h"
#include "../../../Assets/IAsyncMarker.h"
#include "../../../Assets/Assets.h"
#include "../../../xleres/FileList.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	static RenderCore::LightingEngine::ILightScene::LightSourceId CreateTestLight(RenderCore::LightingEngine::ILightScene& lightScene, float theta)
	{
		using namespace RenderCore::LightingEngine;
		auto lightId = lightScene.CreateLightSource(0);

		auto* positional = lightScene.TryGetLightSourceInterface<IPositionalLightSource>(lightId);
		REQUIRE(positional);
		ScaleRotationTranslationM srt{Float3(0.03f, 0.03f, 0.03f), Identity<Float3x3>(), Float3{std::sin(theta), std::cos(theta), 0.f}};
		positional->SetLocalToWorld(AsFloat4x4(srt));

		auto* emittance = lightScene.TryGetLightSourceInterface<IUniformEmittance>(lightId);
		REQUIRE(emittance);
		emittance->SetBrightness(Float3(10.f, 10.f, 10.f));

		return lightId;
	}

    const float depthRange = 10.f;

	static RenderCore::LightingEngine::ILightScene::ShadowProjectionId CreateTestShadowProjection(RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId lightSourceId, float theta)
	{
		using namespace RenderCore::LightingEngine;
		auto shadowId = lightScene.CreateShadowProjection(0, lightSourceId);

		auto* projections = lightScene.TryGetShadowProjectionInterface<IOrthoShadowProjections>(shadowId);
		REQUIRE(projections);

        float distanceToLight = depthRange / 2.0f;
		const float angleToWorldSpace = gPI / 4.0f;
		Float3 negativeLightDirection = SphericalToCartesian(Float3{gPI/2.0f + angleToWorldSpace, theta, 1});

		auto camToWorld = MakeCameraToWorld(-negativeLightDirection, Float3{0.f, 1.0f, 0.f}, distanceToLight / std::sqrt(2.0f) * negativeLightDirection);
		projections->SetWorldToOrthoView(InvertOrthonormalTransform(camToWorld));

		IOrthoShadowProjections::OrthoSubProjection subProj[] = {
			{ Float3{-1.f, 1.f, 0.f}, Float3{1.f, -1.f, depthRange} }
		};
		projections->SetOrthoSubProjections(MakeIteratorRange(subProj));

		IShadowPreparer::Desc desc;
		desc._worldSpaceResolveBias = 0.f;
        desc._tanBlurAngle = 0.00436f;
		desc._minBlurSearch = 3.f;
        desc._maxBlurSearch = 35.f;
		auto* preparer = lightScene.TryGetShadowProjectionInterface<IShadowPreparer>(shadowId);
		REQUIRE(preparer);
		preparer->SetDesc(desc);

		return shadowId;
	}

	static RenderCore::LightingEngine::ILightScene::LightSourceId ConfigureLightScene(RenderCore::LightingEngine::ILightScene& lightScene, float theta)
	{
		auto srcId = CreateTestLight(lightScene, theta);
		CreateTestShadowProjection(lightScene, srcId, theta);
		return srcId;
	}

	template<typename Type>
		static std::shared_ptr<Type> StallAndRequireReady(::Assets::AssetFuture<Type>& future)
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

	static void PrepareResources(IDrawablesWriter& drawablesWriter, LightingEngineTestApparatus& testApparatus, RenderCore::LightingEngine::CompiledLightingTechnique& lightingTechnique)
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

	TEST_CASE( "LightingEngine-ShadowPrecisionTests", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();

		auto threadContext = testHelper->_device->GetImmediateContext();

		RenderCore::Techniques::CameraDesc camera;
        camera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, -1.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, 1.0f}), Float3{0.0f, 5.0f, 0.0f});
        camera._projection = Techniques::CameraDesc::Projection::Orthogonal;
		camera._nearClip = 0.f;
		camera._farClip = 100.f;		// a small far clip here reduces the impact of gbuffer reconstruction accuracy on sampling
		
		testHelper->BeginFrameCapture();

		{
			LightingOperatorsPipelineLayout pipelineLayout(*testHelper);

			float wsDepthResolution = depthRange / 16384.f;
			const float filterRadiusInPixels = 10.0f;
			const float frustumWidthWS = 2.0f;
			float wsXYRange = filterRadiusInPixels * frustumWidthWS / 2048.f;
			float ratio0 = wsXYRange / wsDepthResolution;
			float ratio1 = std::sqrt(wsXYRange*wsXYRange + wsXYRange*wsXYRange) / wsDepthResolution;

			LightingEngine::LightSourceOperatorDesc resolveOperators[] {
				LightingEngine::LightSourceOperatorDesc{}
			};
			LightingEngine::ShadowOperatorDesc shadowOp;
			shadowOp._projectionMode = LightingEngine::ShadowProjectionMode::Ortho;
			shadowOp._rasterDepthBias = (int)std::ceil(ratio1);
			// shadowOp._rasterDepthBias += 256;
			// const float worldSpaceExtraBias = 0.2f;
			// shadowOp._rasterDepthBias += worldSpaceExtraBias / wsDepthResolution;
			shadowOp._enableContactHardening = true;
			shadowOp._slopeScaledBias = 0.5f;
			LightingEngine::ShadowOperatorDesc shadowGenerator[] {
				shadowOp
			};

			///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			SECTION("acne precision")
			{
				const unsigned stripes = 256;
				const unsigned stripeHeight= 8;

				auto stripeTargetDesc = CreateDesc(
					BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
					TextureDesc::Plain2D(2048, stripeHeight, RenderCore::Format::R8G8B8A8_UNORM),
					"temporary-out");

				auto stitchedImageDesc = CreateDesc(
					BindFlag::TransferDst, CPUAccess::Read, 0,
					TextureDesc::Plain2D(2048, stripes*stripeHeight, RenderCore::Format::R8G8B8A8_UNORM),
					"saved-image");
				auto stitchedImage = testHelper->_device->CreateResource(stitchedImageDesc);
				UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, stripeTargetDesc);
				auto parsingContext = InitializeParsingContext(*testApparatus._techniqueContext, stripeTargetDesc, camera);
				parsingContext.GetTechniqueContext()._attachmentPool->Bind(Techniques::AttachmentSemantics::ColorLDR, fbHelper.GetMainTarget());

				auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
				auto lightingTechniqueFuture = LightingEngine::CreateDeferredLightingTechnique(
					testHelper->_device,
					testApparatus._pipelineAcceleratorPool, testApparatus._techDelBox, pipelineLayout._pipelineCollection, pipelineLayout._pipelineLayoutFile,
					MakeIteratorRange(resolveOperators), MakeIteratorRange(shadowGenerator), 
					stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps);
				auto lightingTechnique = StallAndRequireReady(*lightingTechniqueFuture);
				PumpBufferUploads(testApparatus);

				auto drawableWriter = CreateFlatPlaneDrawableWriter(*testHelper, *testApparatus._pipelineAcceleratorPool);
				PrepareResources(*drawableWriter, testApparatus, *lightingTechnique);

				auto& lightScene = LightingEngine::GetLightScene(*lightingTechnique);
				for (unsigned c=0; c<stripes; ++c) {
					auto lightId = ConfigureLightScene(lightScene, gPI/2.0f*c/float(stripes));

					{
						RenderCore::LightingEngine::LightingTechniqueInstance lightingIterator(
							*threadContext, parsingContext, *testApparatus._pipelineAcceleratorPool, *lightingTechnique);
						ParseScene(lightingIterator, *drawableWriter);
					}

					auto encoder = Metal::DeviceContext::Get(*threadContext)->BeginBlitEncoder();
					encoder.Copy(
						Metal::BlitEncoder::CopyPartial_Dest {
							stitchedImage.get(), {}, UInt3{0,c*stripeHeight,0}
						},
						Metal::BlitEncoder::CopyPartial_Src {
							fbHelper.GetMainTarget().get(), {}, UInt3{0,0,0}, UInt3{2048,stripeHeight,1}
						});

					lightScene.DestroyLightSource(lightId);
				}

				SaveImage(*threadContext, *stitchedImage, "acne-shadow-precision");
				parsingContext.GetTechniqueContext()._attachmentPool->UnbindAll();
			}

			///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			SECTION("contact precision")
			{
				auto targetDesc = CreateDesc(
					BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
					TextureDesc::Plain2D(2048, 2048, RenderCore::Format::R8G8B8A8_UNORM),
					"temporary-out");

				auto parsingContext = InitializeParsingContext(*testApparatus._techniqueContext, targetDesc, camera);
				auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
				auto lightingTechniqueFuture = LightingEngine::CreateDeferredLightingTechnique(
					testHelper->_device,
					testApparatus._pipelineAcceleratorPool, testApparatus._techDelBox, pipelineLayout._pipelineCollection, pipelineLayout._pipelineLayoutFile,
					MakeIteratorRange(resolveOperators), MakeIteratorRange(shadowGenerator), 
					stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps);
				auto lightingTechnique = StallAndRequireReady(*lightingTechniqueFuture);
				PumpBufferUploads(testApparatus);

				auto drawableWriter = CreateSharpContactDrawableWriter(*testHelper, *testApparatus._pipelineAcceleratorPool);
				PrepareResources(*drawableWriter, testApparatus, *lightingTechnique);

				auto& lightScene = LightingEngine::GetLightScene(*lightingTechnique);
				auto lightId = ConfigureLightScene(lightScene, gPI/4.0f);

				{
					RenderCore::LightingEngine::LightingTechniqueInstance lightingIterator(
						*threadContext, parsingContext, *testApparatus._pipelineAcceleratorPool, *lightingTechnique);
					ParseScene(lightingIterator, *drawableWriter);
				}

				lightScene.DestroyLightSource(lightId);

				auto colorLDR = parsingContext.GetTechniqueContext()._attachmentPool->GetBoundResource(Techniques::AttachmentSemantics::ColorLDR);
				REQUIRE(colorLDR);

				SaveImage(*threadContext, *colorLDR, "contact-shadow-precision");
			}
		}

		testHelper->EndFrameCapture();
	}

	struct ImmediateDrawingHelper
	{
		std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> _immediateDrawables;
		std::shared_ptr<RenderOverlays::FontRenderingManager> _fontRenderingManager;

		ImmediateDrawingHelper(MetalTestHelper& metalHelper)
		{
			auto pipelineLayoutFuture = ::Assets::MakeAsset<RenderCore::Assets::PredefinedPipelineLayoutFile>(IMMEDIATE_PIPELINE);
			pipelineLayoutFuture->StallWhilePending();
			auto pipelineLayoutFile = pipelineLayoutFuture->Actualize();

			const std::string pipelineLayoutName = "ImmediateDrawables";
			auto i = pipelineLayoutFile->_pipelineLayouts.find(pipelineLayoutName);
			if (i == pipelineLayoutFile->_pipelineLayouts.end())
				Throw(std::runtime_error("Did not find pipeline layout with the name " + pipelineLayoutName + " in the given pipeline layout file"));
			auto pipelineInit = i->second->MakePipelineLayoutInitializer(metalHelper._shaderCompiler->GetShaderLanguage());
			auto compiledPipelineLayout = metalHelper._device->CreatePipelineLayout(pipelineInit);

			_immediateDrawables =  RenderCore::Techniques::CreateImmediateDrawables(
				metalHelper._device, 
				compiledPipelineLayout,
				RenderCore::Techniques::FindLayout(*pipelineLayoutFile, pipelineLayoutName, "Material"),
				RenderCore::Techniques::FindLayout(*pipelineLayoutFile, pipelineLayoutName, "Sequencer"));

			// _fontRenderingManager = std::make_shared<RenderOverlays::FontRenderingManager>(*metalHelper._device);
		}
	};

	TEST_CASE( "LightingEngine-SunSourceCascades", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();
		ImmediateDrawingHelper immediateDrawingHelper(*testApparatus._metalTestHelper);

		auto threadContext = testHelper->_device->GetImmediateContext();

		RenderCore::Techniques::CameraDesc visCamera;
        visCamera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, -1.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, -1.0f}), Float3{0.0f, 200.0f, 0.0f});
        visCamera._projection = Techniques::CameraDesc::Projection::Orthogonal;
		visCamera._nearClip = 0.f;
		visCamera._farClip = 400.f;
		visCamera._left = 0.f;
		visCamera._right = 100.f;
		visCamera._top = 0.f;
		visCamera._bottom = -100.f;

		RenderCore::Techniques::CameraDesc sceneCamera;
//        sceneCamera._cameraToWorld = MakeCameraToWorld(-Normalize(Float3{-40.0f, 10.0f, -40.0f}), Normalize(Float3{0.0f, 1.0f, 0.0f}), Float3{-5.0f, 10.0f, -5.0f});
        sceneCamera._cameraToWorld = MakeCameraToWorld(-Normalize(Float3{-40.0f, 10.0f, -40.0f}), Normalize(Float3{0.0f, 1.0f, 0.0f}), Float3{5.0f, 10.0f, 5.0f});
        sceneCamera._projection = Techniques::CameraDesc::Projection::Perspective;
		sceneCamera._nearClip = 0.05f;
		sceneCamera._farClip = 150.f;
		sceneCamera._verticalFieldOfView = Deg2Rad(50.0f);

		const Float3 negativeLightDirection = Normalize(Float3{0.0f, 1.0f, 0.5f});
//		const Float3 negativeLightDirection = Normalize(Float3{0.0f, 1.0f, 0.0f});

		testHelper->BeginFrameCapture();

		{
			RenderCore::LightingEngine::SunSourceFrustumSettings sunSourceFrustumSettings;
			sunSourceFrustumSettings._flags = 0;
			sunSourceFrustumSettings._maxDistanceFromCamera = 100.f;
			sunSourceFrustumSettings._focusDistance = ExtractTranslation(sceneCamera._cameraToWorld)[1] / -ExtractForward_Cam(sceneCamera._cameraToWorld)[1];
			sunSourceFrustumSettings._maxFrustumCount = 4;
			sunSourceFrustumSettings._frustumSizeFactor = 2.0f;

			LightingOperatorsPipelineLayout pipelineLayout(*testHelper);

			LightingEngine::LightSourceOperatorDesc resolveOperators[] {
				LightingEngine::LightSourceOperatorDesc{ LightingEngine::LightSourceShape::Directional }
			};
			LightingEngine::ShadowOperatorDesc shadowGenerator[] {
				CalculateShadowOperatorDesc(sunSourceFrustumSettings)
			};

			{
				auto targetDesc = CreateDesc(
					BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
					TextureDesc::Plain2D(2048, 2048, RenderCore::Format::R8G8B8A8_UNORM),
					"temporary-out");

				auto parsingContext = InitializeParsingContext(*testApparatus._techniqueContext, targetDesc, visCamera);
				auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
				auto lightingTechniqueFuture = LightingEngine::CreateDeferredLightingTechnique(
					testHelper->_device,
					testApparatus._pipelineAcceleratorPool, testApparatus._techDelBox, pipelineLayout._pipelineCollection, pipelineLayout._pipelineLayoutFile,
					MakeIteratorRange(resolveOperators), MakeIteratorRange(shadowGenerator), 
					stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps);
				auto lightingTechnique = StallAndRequireReady(*lightingTechniqueFuture);
				PumpBufferUploads(testApparatus);

				const Float2 worldMins{0.f, 0.f}, worldMaxs{100.f, 100.f}; 
				auto drawableWriter = CreateShapeWorldDrawableWriter(*testHelper, *testApparatus._pipelineAcceleratorPool, worldMins, worldMaxs);
				PrepareResources(*drawableWriter, testApparatus, *lightingTechnique);

				auto& lightScene = LightingEngine::GetLightScene(*lightingTechnique);
				auto lightId = lightScene.CreateLightSource(0);
				lightScene.TryGetLightSourceInterface<LightingEngine::IPositionalLightSource>(lightId)->SetLocalToWorld(AsFloat4x4(negativeLightDirection));
				auto shadowProjectionId = lightScene.CreateShadowProjection(0, lightId);
				LightingEngine::ConfigureShadowCascades(lightScene, shadowProjectionId, negativeLightDirection, BuildProjectionDesc(sceneCamera, UInt2{2048, 2048}), sunSourceFrustumSettings);

				{
					RenderCore::LightingEngine::LightingTechniqueInstance lightingIterator(
						*threadContext, parsingContext, *testApparatus._pipelineAcceleratorPool, *lightingTechnique);
					ParseScene(lightingIterator, *drawableWriter);
				}

				// draw the camera and shadow frustums into the output image
				{
					auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(
						*threadContext, *immediateDrawingHelper._immediateDrawables, immediateDrawingHelper._fontRenderingManager.get());

					RenderOverlays::ColorB cols[]= {
						RenderOverlays::ColorB(196, 230, 230),
						RenderOverlays::ColorB(255, 128, 128),
						RenderOverlays::ColorB(128, 255, 128),
						RenderOverlays::ColorB(128, 128, 255),
						RenderOverlays::ColorB(255, 255, 128),
						RenderOverlays::ColorB(128, 255, 255)
					};
					unsigned colorIterator = 0;
					auto* shadowProj = lightScene.TryGetShadowProjectionInterface<LightingEngine::IOrthoShadowProjections>(shadowProjectionId);
					if (shadowProj) {
						auto worldToView = shadowProj->GetWorldToOrthoView();
						auto subProjs = shadowProj->GetOrthoSubProjections();
						for (const auto& subProj:subProjs) {
							auto col = cols[(colorIterator++)%dimof(cols)];
							auto topLeftFront = subProj._topLeftFront;
							auto bottomRightBack = subProj._bottomRightBack;
							// We have to reverse the Z values, because -Z is into the camera in camera space, but we represent near and far clip values as positives
							topLeftFront[2] = -topLeftFront[2];
							bottomRightBack[2] = -bottomRightBack[2];
							RenderOverlays::DebuggingDisplay::DrawBoundingBox(
								overlayContext.get(), 
								std::make_tuple(topLeftFront, bottomRightBack),
								InvertOrthonormalTransform(AsFloat3x4(worldToView)),
								col, 0x2);

							col.a = 196;
							RenderOverlays::DebuggingDisplay::DrawBoundingBox(
								overlayContext.get(), 
								std::make_tuple(topLeftFront, bottomRightBack),
								InvertOrthonormalTransform(AsFloat3x4(worldToView)),
								col, 0x1);
						}
					}
					
					auto sceneProjDesc = RenderCore::Techniques::BuildProjectionDesc(sceneCamera, UInt2{2048, 2048});
					RenderOverlays::DebuggingDisplay::DrawFrustum(overlayContext.get(), sceneProjDesc._worldToProjection, RenderOverlays::ColorB(0xff, 0xff, 0xff), 0x2);

					auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(*threadContext, parsingContext);
					auto prepare = immediateDrawingHelper._immediateDrawables->PrepareResources(rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
					if (prepare) {
						prepare->StallWhilePending();
						REQUIRE(prepare->GetAssetState() == ::Assets::AssetState::Ready);
					}
					immediateDrawingHelper._immediateDrawables->ExecuteDraws(*threadContext, parsingContext, rpi);
				}

				auto colorLDR = parsingContext.GetTechniqueContext()._attachmentPool->GetBoundResource(Techniques::AttachmentSemantics::ColorLDR);
				REQUIRE(colorLDR);

				SaveImage(*threadContext, *colorLDR, "sun-source-cascades");
			}

		}

		testHelper->EndFrameCapture();
	}
}
