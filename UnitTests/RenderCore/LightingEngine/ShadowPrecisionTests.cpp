// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineTestHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../../RenderCore/LightingEngine/ILightScene.h"
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
#include "../../../RenderCore/Techniques/PipelineOperators.h"
#include "../../../RenderCore/Techniques/PipelineLayoutDelegate.h"
#include "../../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../RenderOverlays/OverlayContext.h"
#include "../../../RenderOverlays/DebuggingDisplay.h"
#include "../../../RenderOverlays/FontRendering.h"
#include "../../../RenderOverlays/ShapesRendering.h"
#include "../../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../../Math/Transformations.h"
#include "../../../Math/ProjectionMath.h"
#include "../../../Math/Geometry.h"
#include "../../../Assets/IAsyncMarker.h"
#include "../../../Assets/Assets.h"
#include "../../../xleres/FileList.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <fstream>
#include <filesystem>

using namespace Catch::literals;
using namespace std::chrono_literals;
using namespace Utility::Literals;

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

	static void CreateTestShadowProjection(RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId lightSourceId, float theta)
	{
		using namespace RenderCore::LightingEngine;
		lightScene.SetShadowOperator(lightSourceId, 0);

		auto* projections = lightScene.TryGetLightSourceInterface<IOrthoShadowProjections>(lightSourceId);
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

		IDepthTextureResolve::Desc desc;
		desc._worldSpaceResolveBias = 0.f;
        desc._tanBlurAngle = 0.00436f;
		desc._minBlurSearch = 3.f;
        desc._maxBlurSearch = 35.f;
		auto* preparer = lightScene.TryGetLightSourceInterface<IDepthTextureResolve>(lightSourceId);
		REQUIRE(preparer);
		preparer->SetDesc(desc);
	}

	static RenderCore::LightingEngine::ILightScene::LightSourceId ConfigureLightScene(RenderCore::LightingEngine::ILightScene& lightScene, float theta)
	{
		auto srcId = CreateTestLight(lightScene, theta);
		CreateTestShadowProjection(lightScene, srcId, theta);
		return srcId;
	}

	static RenderCore::Techniques::PreparedResourcesVisibility PrepareResources(ToolsRig::IDrawablesWriter& drawablesWriter, LightingEngineTestApparatus& testApparatus, RenderCore::LightingEngine::CompiledLightingTechnique& lightingTechnique)
	{
		// stall until all resources are ready
		auto prepareLightingIterator = RenderCore::LightingEngine::BeginPrepareResourcesInstance(*testApparatus._pipelineAccelerators, lightingTechnique);
		ParseScene(prepareLightingIterator, drawablesWriter);
		std::promise<RenderCore::Techniques::PreparedResourcesVisibility> preparePromise;
		auto prepareFuture = preparePromise.get_future();
		prepareLightingIterator.FulfillWhenNotPending(std::move(preparePromise));
		return PrepareAndStall(testApparatus, *testApparatus._metalTestHelper->_device->GetImmediateContext(), std::move(prepareFuture));
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
			float wsDepthResolution = depthRange / 16384.f;
			const float filterRadiusInPixels = 10.0f;
			const float frustumWidthWS = 2.0f;
			float wsXYRange = filterRadiusInPixels * frustumWidthWS / 2048.f;
			float ratio0 = wsXYRange / wsDepthResolution;
			float ratio1 = std::sqrt(wsXYRange*wsXYRange + wsXYRange*wsXYRange) / wsDepthResolution;
			(void)ratio0;

			LightingEngine::LightSourceOperatorDesc resolveOperators[] {
				LightingEngine::LightSourceOperatorDesc{}
			};
			LightingEngine::ShadowOperatorDesc shadowOp;
			shadowOp._projectionMode = LightingEngine::ShadowProjectionMode::Ortho;
			shadowOp._singleSidedBias._depthBias = (int)std::ceil(ratio1);
			// shadowOp._rasterDepthBias += 256;
			// const float worldSpaceExtraBias = 0.2f;
			// shadowOp._rasterDepthBias += worldSpaceExtraBias / wsDepthResolution;
			shadowOp._enableContactHardening = true;
			shadowOp._singleSidedBias._slopeScaledBias = 0.5f;
			LightingEngine::ShadowOperatorDesc shadowGenerator[] {
				shadowOp
			};

			///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			SECTION("acne precision")
			{
				const unsigned stripes = 256;
				const unsigned stripeHeight= 8;

				auto stripeTargetDesc = CreateDesc(
					BindFlag::RenderTarget | BindFlag::TransferSrc,
					TextureDesc::Plain2D(2048, stripeHeight, RenderCore::Format::R8G8B8A8_UNORM_SRGB));

				auto stitchedImageDesc = CreateDesc(
					BindFlag::TransferDst, AllocationRules::HostVisibleRandomAccess,
					TextureDesc::Plain2D(2048, stripes*stripeHeight, RenderCore::Format::R8G8B8A8_UNORM_SRGB));
				auto stitchedImage = testHelper->_device->CreateResource(stitchedImageDesc, "ShadowPrecisionTests");
				UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, stripeTargetDesc);
				auto parsingContext = BeginParsingContext(testApparatus, *threadContext, stripeTargetDesc, camera);
				parsingContext.BindAttachment(Techniques::AttachmentSemantics::ColorLDR, fbHelper.GetMainTarget(), BindFlag::RenderTarget);

				auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
				std::promise<std::shared_ptr<LightingEngine::CompiledLightingTechnique>> promisedLightingTechnique;
				auto lightingTechniqueFuture = promisedLightingTechnique.get_future();
				LightingEngine::CreateDeferredLightingTechnique(
					std::move(promisedLightingTechnique),
					testApparatus._pipelineAccelerators, testApparatus._pipelineCollection, testApparatus._sharedDelegates,
					MakeIteratorRange(resolveOperators), MakeIteratorRange(shadowGenerator), nullptr,
					stitchingContext.GetPreregisteredAttachments());
				auto lightingTechnique = lightingTechniqueFuture.get();

				auto drawableWriter = ToolsRig::DrawablesWriterHelper(*testHelper->_device, *testApparatus._drawablesPool, *testApparatus._pipelineAccelerators).CreateFlatPlaneDrawableWriter();
				auto newVisibility = PrepareResources(*drawableWriter, testApparatus, *lightingTechnique);
				parsingContext.SetPipelineAcceleratorsVisibility(newVisibility._pipelineAcceleratorsVisibility);
				parsingContext.RequireCommandList(newVisibility._bufferUploadsVisibility);

				auto& lightScene = LightingEngine::GetLightScene(*lightingTechnique);
				for (unsigned c=0; c<stripes; ++c) {
					auto lightId = ConfigureLightScene(lightScene, gPI/2.0f*c/float(stripes));

					{
						auto lightingIterator = RenderCore::LightingEngine::BeginLightingTechniquePlayback(
							parsingContext, *lightingTechnique);
						ParseScene(lightingIterator, *drawableWriter);
					}

					auto encoder = Metal::DeviceContext::Get(*threadContext)->BeginBlitEncoder();
					encoder.Copy(
						CopyPartial_Dest{ *stitchedImage, {}, UInt3{0,c*stripeHeight,0} },
						CopyPartial_Src{ *fbHelper.GetMainTarget() }.PartialSubresource(UInt3{0,0,0}, UInt3{2048,stripeHeight,1}, MakeTexturePitches(stripeTargetDesc._textureDesc)));

					lightScene.DestroyLightSource(lightId);
				}

				if (parsingContext._requiredBufferUploadsCommandList)
					testApparatus._bufferUploads->StallAndMarkCommandListDependency(*threadContext, parsingContext._requiredBufferUploadsCommandList);

				SaveImage(*threadContext, *stitchedImage, "acne-shadow-precision");
			}

			///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			SECTION("contact precision")
			{
				auto targetDesc = CreateDesc(
					BindFlag::RenderTarget | BindFlag::TransferSrc,
					TextureDesc::Plain2D(2048, 2048, RenderCore::Format::R8G8B8A8_UNORM_SRGB));

				auto parsingContext = BeginParsingContext(testApparatus, *threadContext, targetDesc, camera);
				auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
				std::promise<std::shared_ptr<LightingEngine::CompiledLightingTechnique>> promisedLightingTechnique;
				auto lightingTechniqueFuture = promisedLightingTechnique.get_future();
				LightingEngine::CreateDeferredLightingTechnique(
					std::move(promisedLightingTechnique),
					testApparatus._pipelineAccelerators, testApparatus._pipelineCollection, testApparatus._sharedDelegates,
					MakeIteratorRange(resolveOperators), MakeIteratorRange(shadowGenerator), nullptr,
					stitchingContext.GetPreregisteredAttachments());
				auto lightingTechnique = lightingTechniqueFuture.get();

				auto drawableWriter = ToolsRig::DrawablesWriterHelper(*testHelper->_device, *testApparatus._drawablesPool, *testApparatus._pipelineAccelerators).CreateSharpContactDrawableWriter();
				auto newVisibility = PrepareResources(*drawableWriter, testApparatus, *lightingTechnique);
				parsingContext.SetPipelineAcceleratorsVisibility(newVisibility._pipelineAcceleratorsVisibility);
				parsingContext.RequireCommandList(newVisibility._bufferUploadsVisibility);

				auto& lightScene = LightingEngine::GetLightScene(*lightingTechnique);
				auto lightId = ConfigureLightScene(lightScene, gPI/4.0f);

				{
					auto lightingIterator = RenderCore::LightingEngine::BeginLightingTechniquePlayback(
						parsingContext, *lightingTechnique);
					ParseScene(lightingIterator, *drawableWriter);
				}

				lightScene.DestroyLightSource(lightId);

				if (parsingContext._requiredBufferUploadsCommandList)
					testApparatus._bufferUploads->StallAndMarkCommandListDependency(*threadContext, parsingContext._requiredBufferUploadsCommandList);

				auto colorLDR = parsingContext.GetAttachmentReservation().MapSemanticToResource(Techniques::AttachmentSemantics::ColorLDR);
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
		std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> _shapesRendering;

		ImmediateDrawingHelper(MetalTestHelper& metalHelper)
		{
			_shapesRendering = std::make_shared<RenderOverlays::ShapesRenderingDelegate>();
			_immediateDrawables =  RenderCore::Techniques::CreateImmediateDrawables(metalHelper._device, _shapesRendering->GetPipelineLayoutDelegate());
			// _fontRenderingManager = std::make_shared<RenderOverlays::FontRenderingManager>(*metalHelper._device);
		}
	};

	static const RenderOverlays::ColorB cols[]= {
		RenderOverlays::ColorB(196, 230, 230),
		RenderOverlays::ColorB(255, 128, 128),
		RenderOverlays::ColorB(128, 255, 128),
		RenderOverlays::ColorB(128, 128, 255),
		RenderOverlays::ColorB(255, 255, 128),
		RenderOverlays::ColorB(128, 255, 255)
	};

	static void DrawCameraAndShadowFrustums(
		RenderCore::IThreadContext& threadContext,
		ImmediateDrawingHelper& immediateDrawingHelper,
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::LightingEngine::ILightScene& lightScene,
		RenderCore::LightingEngine::ILightScene::LightSourceId lightSourceId,
		const RenderCore::Techniques::CameraDesc& sceneCamera)
	{
		using namespace RenderCore;
		auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(
			threadContext, *immediateDrawingHelper._immediateDrawables, immediateDrawingHelper._fontRenderingManager.get());

		unsigned colorIterator = 0;
		auto* shadowProj = lightScene.TryGetLightSourceInterface<LightingEngine::IOrthoShadowProjections>(lightSourceId);
		if (shadowProj) {
			auto worldToView = shadowProj->GetWorldToOrthoView();
			auto subProjs = shadowProj->GetOrthoSubProjections();
			for (const auto& subProj:subProjs) {
				auto col = cols[(colorIterator++)%dimof(cols)];
				auto leftTopFront = subProj._leftTopFront;
				auto rightBottomBack = subProj._rightBottomBack;
				// We have to reverse the Z values, because -Z is into the camera in camera space, but we represent near and far clip values as positives
				leftTopFront[2] = -leftTopFront[2];
				rightBottomBack[2] = -rightBottomBack[2];
				RenderOverlays::DebuggingDisplay::DrawBoundingBox(
					*overlayContext, 
					std::make_tuple(leftTopFront, rightBottomBack),
					InvertOrthonormalTransform(AsFloat3x4(worldToView)),
					col, 0x2);

				col.a = 196;
				RenderOverlays::DebuggingDisplay::DrawBoundingBox(
					*overlayContext, 
					std::make_tuple(leftTopFront, rightBottomBack),
					InvertOrthonormalTransform(AsFloat3x4(worldToView)),
					col, 0x1);
			}
		}
		
		auto sceneProjDesc = RenderCore::Techniques::BuildProjectionDesc(sceneCamera, UInt2{2048, 2048});
		RenderOverlays::DebuggingDisplay::DrawFrustum(*overlayContext, sceneProjDesc._worldToProjection, RenderOverlays::ColorB(0xff, 0xff, 0xff), 0x2);

		auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parsingContext);
		std::promise<RenderCore::Techniques::PreparedResourcesVisibility> visibilityPromise;
		auto visibilityFuture = visibilityPromise.get_future();
		immediateDrawingHelper._immediateDrawables->PrepareResources(std::move(visibilityPromise), immediateDrawingHelper._shapesRendering->GetTechniqueDelegate(), rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
		auto requiredVisibility = visibilityFuture.get(); // stall();
		immediateDrawingHelper._immediateDrawables->OnFrameBarrier();
		RenderCore::Techniques::Services::GetBufferUploads().StallAndMarkCommandListDependency(*RenderCore::Techniques::GetThreadContext(), requiredVisibility._bufferUploadsVisibility);
		
		immediateDrawingHelper._immediateDrawables->ExecuteDraws(parsingContext, immediateDrawingHelper._shapesRendering->GetTechniqueDelegate(), rpi);
	}

	static void DrawCascadeColors(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::ParsingContext& parsingContext,
		const std::shared_ptr<RenderCore::Techniques::PipelineCollection>& pipelinePool)
	{
		using namespace RenderCore;
		auto rpi = Techniques::RenderPassToPresentationTarget(parsingContext);
		UniformsStreamInterface usi;
		auto cascadeIndexTexture = parsingContext.GetAttachmentReservation().MapSemanticToResource("CascadeIndex"_h+0);
		REQUIRE(cascadeIndexTexture);
		auto cascadeIndexTextureSRV = cascadeIndexTexture->CreateTextureView(BindFlag::ShaderResource);
		usi.BindResourceView(0, "PrebuiltCascadeIndexTexture"_h);
		IResourceView* srvs[] = { cascadeIndexTextureSRV.get() };
		UniformsStream us;
		us._resourceViews = MakeIteratorRange(srvs);
		Techniques::PixelOutputStates outputStates;
		outputStates.Bind(rpi);
		outputStates.Bind(Techniques::CommonResourceBox::s_dsDisable);
		AttachmentBlendDesc blendStates[] { Techniques::CommonResourceBox::s_abStraightAlpha };
		outputStates.Bind(MakeIteratorRange(blendStates));
		auto op = CreateFullViewportOperator(
			pipelinePool, Techniques::FullViewportOperatorSubType::DisableDepth, CASCADE_VIS_HLSL ":col_vis_pass", {}, GENERAL_OPERATOR_PIPELINE ":GraphicsMain", outputStates, usi);
		op->StallWhilePending();
		op->Actualize()->Draw(parsingContext, us);
	}

	static void WriteFrustumListToPLY(std::ostream& str, IteratorRange<const Float4x4*> worldToProjs)
	{
		str << "ply" << std::endl;
		str << "format ascii 1.0" << std::endl;
		str << "element vertex " << 8 * worldToProjs.size() << std::endl;
		str << "property float x" << std::endl;
		str << "property float y" << std::endl;
		str << "property float z" << std::endl;
		str << "property uchar red" << std::endl;
		str << "property uchar green" << std::endl;
		str << "property uchar blue" << std::endl;
		str << "element face " << 6 * worldToProjs.size() << std::endl;
		str << "property list uchar int vertex_index" << std::endl;
		str << "end_header" << std::endl;

		unsigned q=0;
		for (auto worldToProj:worldToProjs) {
			Float3 frustumCorners[8];
			CalculateAbsFrustumCorners(frustumCorners, worldToProj, RenderCore::Techniques::GetDefaultClipSpaceType());
			auto col = cols[(q++)%dimof(cols)];
			for (unsigned c=0; c<8; ++c)
				str << frustumCorners[c][0] << " " << frustumCorners[c][1] << " " << frustumCorners[c][2] << " " << (unsigned)col.r << " " << (unsigned)col.g << " " << (unsigned)col.b << std::endl;
		}

		const UInt4 faceIndices[] {		// these are in Z-pattern ordering
			UInt4 { 0, 1, 2, 3 },
			UInt4 { 4, 5, 0, 1 },
			UInt4 { 2, 3, 6, 7 },
			UInt4 { 6, 7, 4, 5 },
			UInt4 { 4, 0, 6, 2 },
			UInt4 { 1, 5, 3, 7 }
		};

		for (unsigned p=0; p<worldToProjs.size(); ++p)
			for (unsigned f=0; f<dimof(faceIndices); ++f)
				str << "4 " << (p*8)+faceIndices[f][0] << " " << (p*8)+faceIndices[f][1] << " " << (p*8)+faceIndices[f][3] << " " << (p*8)+faceIndices[f][2] << std::endl;
	}

	TEST_CASE( "LightingEngine-SunSourceCascades", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();
		ImmediateDrawingHelper immediateDrawingHelper(*testApparatus._metalTestHelper);

		auto threadContext = testHelper->_device->GetImmediateContext();

		RenderCore::Techniques::CameraDesc visCameras[2];
        visCameras[0]._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, -1.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, -1.0f}), Float3{0.0f, 200.0f, 0.0f});
        visCameras[0]._projection = Techniques::CameraDesc::Projection::Orthogonal;
		visCameras[0]._nearClip = 0.f;
		visCameras[0]._farClip = 400.f;
		visCameras[0]._left = 0.f;
		visCameras[0]._right = 100.f;
		visCameras[0]._top = 0.f;
		visCameras[0]._bottom = -100.f;

		visCameras[1]._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, 0.0f, -1.0f}), Normalize(Float3{0.0f, 1.0f, 0.0f}), Float3{0.0f, 0.f, 200.0f});
        visCameras[1]._projection = Techniques::CameraDesc::Projection::Orthogonal;
		visCameras[1]._nearClip = 0.f;
		visCameras[1]._farClip = 400.f;
		visCameras[1]._left = 0.f;
		visCameras[1]._right = 100.f;
		visCameras[1]._top = 50.f;
		visCameras[1]._bottom = -50.f;

		RenderCore::Techniques::CameraDesc sceneCamera;
		sceneCamera._cameraToWorld = MakeCameraToWorld(-Normalize(Float3{-25.0f, 10.0f, -25.0f}), Normalize(Float3{0.0f, 1.0f, 0.0f}), Float3{5.0f, 10.0f, 5.0f});
        sceneCamera._projection = Techniques::CameraDesc::Projection::Perspective;
		sceneCamera._nearClip = 0.05f;
		sceneCamera._farClip = 150.f;
		sceneCamera._verticalFieldOfView = Deg2Rad(50.0f);

 		const Float3 negativeLightDirection = Normalize(Float3{0.0f, 1.0f, 0.5f});
// 		const Float3 negativeLightDirection = Normalize(Float3{0.0f, 1.0f, 0.0f});
// 		const Float3 negativeLightDirection = Normalize(Float3{0.8f, 2.0f, 0.7f});
//		const Float3 negativeLightDirection = Normalize(Float3{-2.69884f, 0.696449f, -2.16482f});
//		const Float3 negativeLightDirection = Normalize(ExtractForward_Cam(sceneCamera._cameraToWorld) + Float3{0.f, 0.1f, 0.f});	// almost exactly into the camera

		testHelper->BeginFrameCapture();

		{
			RenderCore::LightingEngine::SunSourceFrustumSettings sunSourceFrustumSettings;
			sunSourceFrustumSettings._flags = 0;
			sunSourceFrustumSettings._maxDistanceFromCamera = 100.f;
			float A = -ExtractForward_Cam(sceneCamera._cameraToWorld)[1];
			if (!Equivalent(A, 0.0f, 1e-3f)) {
				sunSourceFrustumSettings._focusDistance = ExtractTranslation(sceneCamera._cameraToWorld)[1] / A;
			} else
				sunSourceFrustumSettings._focusDistance = 5.0f;
			sunSourceFrustumSettings._maxFrustumCount = 5;
			sunSourceFrustumSettings._frustumSizeFactor = 2.0f;

			LightingEngine::LightSourceOperatorDesc resolveOperators[] {
				LightingEngine::LightSourceOperatorDesc{ LightingEngine::LightSourceShape::Directional }
			};
			LightingEngine::ShadowOperatorDesc shadowGenerator[] {
				CalculateShadowOperatorDesc(sunSourceFrustumSettings)
			};

			{
				auto targetDesc = CreateDesc(
					BindFlag::RenderTarget | BindFlag::TransferSrc,
					TextureDesc::Plain2D(2048, 2048, RenderCore::Format::R8G8B8A8_UNORM_SRGB));

				auto parsingContext = BeginParsingContext(testApparatus, *threadContext, targetDesc, sceneCamera);
				auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
				std::promise<std::shared_ptr<LightingEngine::CompiledLightingTechnique>> promisedLightingTechnique;
				auto lightingTechniqueFuture = promisedLightingTechnique.get_future();
				LightingEngine::CreateDeferredLightingTechnique(
					std::move(promisedLightingTechnique),
					testApparatus._pipelineAccelerators, testApparatus._pipelineCollection, testApparatus._sharedDelegates,
					MakeIteratorRange(resolveOperators), MakeIteratorRange(shadowGenerator), nullptr,
					stitchingContext.GetPreregisteredAttachments(),
					LightingEngine::DeferredLightingTechniqueFlags::GenerateDebuggingTextures);
				auto lightingTechnique = lightingTechniqueFuture.get();

				const Float2 worldMins{0.f, 0.f}, worldMaxs{100.f, 100.f};
				auto drawableWriter = ToolsRig::DrawablesWriterHelper(*testHelper->_device, *testApparatus._drawablesPool, *testApparatus._pipelineAccelerators)
					.CreateShapeWorldDrawableWriter(worldMins, worldMaxs);
				auto newVisibility = PrepareResources(*drawableWriter, testApparatus, *lightingTechnique);
				parsingContext.SetPipelineAcceleratorsVisibility(newVisibility._pipelineAcceleratorsVisibility);
				parsingContext.RequireCommandList(newVisibility._bufferUploadsVisibility);

				auto& lightScene = LightingEngine::GetLightScene(*lightingTechnique);
				auto lightId = lightScene.CreateLightSource(0);
				lightScene.SetShadowOperator(lightId, 0);
				lightScene.TryGetLightSourceInterface<LightingEngine::IPositionalLightSource>(lightId)->SetLocalToWorld(AsFloat4x4(negativeLightDirection));
				LightingEngine::SetupSunSourceShadows(lightScene, lightId, sunSourceFrustumSettings);
				lightScene.TryGetLightSourceInterface<LightingEngine::ISunSourceShadows>(lightId)->FixMainSceneCamera(
					BuildProjectionDesc(sceneCamera, UInt2{2048, 2048}));

				// draw once from the "scene camera"
				{
					{
						auto lightingIterator = RenderCore::LightingEngine::BeginLightingTechniquePlayback(
							parsingContext, *lightingTechnique);
						ParseScene(lightingIterator, *drawableWriter);
					}

					DrawCascadeColors(parsingContext.GetThreadContext(), parsingContext, testApparatus._pipelineCollection);

					if (parsingContext._requiredBufferUploadsCommandList)
						testApparatus._bufferUploads->StallAndMarkCommandListDependency(*threadContext, parsingContext._requiredBufferUploadsCommandList);

					auto colorLDR = parsingContext.GetAttachmentReservation().MapSemanticToResource(Techniques::AttachmentSemantics::ColorLDR);
					REQUIRE(colorLDR);

					SaveImage(*threadContext, *colorLDR, "sun-source-cascades-scene-camera");

					auto cascadeIndexTexture = parsingContext.GetAttachmentReservation().MapSemanticToResource("CascadeIndex"_h+0);
					REQUIRE(cascadeIndexTexture);
					auto cascadeIndexReadback = cascadeIndexTexture->ReadBackSynchronized(*threadContext);
					unsigned cascadePixelCount[5] = {0,0,0,0,0};
					auto cascadeIndicies = MakeIteratorRange((const uint8_t*)AsPointer(cascadeIndexReadback.begin()), (const uint8_t*)AsPointer(cascadeIndexReadback.end()));
					for (auto i:cascadeIndicies)
						if (i < dimof(cascadePixelCount))
							++cascadePixelCount[i];
					Log(Warning) << "Cascade[0]: " << cascadePixelCount[0] << std::endl;
					Log(Warning) << "Cascade[1]: " << cascadePixelCount[1] << std::endl;
					Log(Warning) << "Cascade[2]: " << cascadePixelCount[2] << std::endl;
					Log(Warning) << "Cascade[3]: " << cascadePixelCount[3] << std::endl;
					Log(Warning) << "Cascade[4]: " << cascadePixelCount[4] << std::endl;
				}

				// and from the "vis cameras"
				for (unsigned c=0; c<dimof(visCameras); ++c) {
					parsingContext.GetProjectionDesc() = BuildProjectionDesc(visCameras[c], UInt2{targetDesc._textureDesc._width, targetDesc._textureDesc._height});
					{
						auto lightingIterator = RenderCore::LightingEngine::BeginLightingTechniquePlayback(
							parsingContext, *lightingTechnique);
						ParseScene(lightingIterator, *drawableWriter);
					}

					DrawCascadeColors(*threadContext, parsingContext, testApparatus._pipelineCollection);

					// draw the camera and shadow frustums into the output image
					DrawCameraAndShadowFrustums(*threadContext, immediateDrawingHelper, parsingContext, lightScene, lightId, sceneCamera);
					
					if (parsingContext._requiredBufferUploadsCommandList)
						testApparatus._bufferUploads->StallAndMarkCommandListDependency(*threadContext, parsingContext._requiredBufferUploadsCommandList);

					auto colorLDR = parsingContext.GetAttachmentReservation().MapSemanticToResource(Techniques::AttachmentSemantics::ColorLDR);
					REQUIRE(colorLDR);

					SaveImage(*threadContext, *colorLDR, "sun-source-cascades-vis-camera-" + std::to_string(c));
				}

				std::vector<Float4x4> worldToProjs;
				worldToProjs.push_back(BuildProjectionDesc(sceneCamera, UInt2{2048, 2048})._worldToProjection);
				auto* orthoShadowProjections = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IOrthoShadowProjections>(lightId);
				REQUIRE(orthoShadowProjections);
				auto subProjections = orthoShadowProjections->GetOrthoSubProjections();
				REQUIRE(!subProjections.empty());
				for (const auto& subProj:subProjections) {
					auto projMatrix = OrthogonalProjection(
						subProj._leftTopFront[0], subProj._leftTopFront[1], 
						subProj._rightBottomBack[0], subProj._rightBottomBack[1], 
						subProj._leftTopFront[2], subProj._rightBottomBack[2],
						Techniques::GetDefaultClipSpaceType());
					worldToProjs.push_back(Combine(orthoShadowProjections->GetWorldToOrthoView(), projMatrix));
				}
				auto outputName = std::filesystem::temp_directory_path() / "xle-unit-tests" / "sun-source-cascades.ply";
				std::ofstream plyOut(outputName);
				WriteFrustumListToPLY(plyOut, worldToProjs);
			}

		}

		testHelper->EndFrameCapture();
	}

	static std::vector<Float4x4> AsWorldToProjs(IteratorRange<const RenderCore::LightingEngine::IOrthoShadowProjections::OrthoSubProjection*> subProjections, const Float4x4& worldToOrthoView)
	{
		std::vector<Float4x4> result;
		for (const auto& subProj:subProjections) {
			auto projMatrix = OrthogonalProjection(
				subProj._leftTopFront[0], subProj._leftTopFront[1], 
				subProj._rightBottomBack[0], subProj._rightBottomBack[1], 
				subProj._leftTopFront[2], subProj._rightBottomBack[2],
				RenderCore::Techniques::GetDefaultClipSpaceType());
			result.push_back(Combine(worldToOrthoView, projMatrix));
		}
		return result;
	}

	TEST_CASE( "LightingEngine-SunSourceCascadesProjectionMath", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		using namespace RenderCore::LightingEngine;
		// test "BuildResolutionNormalizedOrthogonalShadowProjections" to ensure that the results with
		// different clip space types agree
		// This is actually a great way to shake out precision errors in the projection math, because
		// even though the different clip space types are equivalent, there's a large degree of floating
		// point precision difference between them. So if the algorithm is too sensitive to creep, we will
		// see differences appearing

		RenderCore::LightingEngine::SunSourceFrustumSettings sunSourceFrustumSettings;
		sunSourceFrustumSettings._flags = 0;
		sunSourceFrustumSettings._maxDistanceFromCamera = 100.f;
		sunSourceFrustumSettings._focusDistance = 5.0f;
		sunSourceFrustumSettings._maxFrustumCount = 5;
		sunSourceFrustumSettings._frustumSizeFactor = 2.0f;

		std::mt19937_64 rng(89125492);
		for (unsigned c=0; c<1000; ++c) {
			RenderCore::Techniques::CameraDesc sceneCamera;
			sceneCamera._cameraToWorld = MakeCameraToWorld(
				SphericalToCartesian(Float3{std::uniform_real_distribution<>(0.f, 2.f*M_PI)(rng), std::uniform_real_distribution<>(0.f, 2.f*M_PI)(rng), 1.f}),
				Normalize(Float3{0.0f, 1.0f, 0.0f}), 
				Float3{std::uniform_real_distribution<>(-1000.f, 1000.f)(rng), std::uniform_real_distribution<>(-1000.f, 1000.f)(rng), std::uniform_real_distribution<>(-1000.f, 1000.f)(rng)});
			sceneCamera._projection = Techniques::CameraDesc::Projection::Perspective;
			sceneCamera._nearClip = 0.05f;
			sceneCamera._farClip = 150.f;
			sceneCamera._verticalFieldOfView = Deg2Rad(50.0f);

			const Float3 negativeLightDirection = SphericalToCartesian(Float3{std::uniform_real_distribution<>(0.f, 2.f*M_PI)(rng), std::uniform_real_distribution<>(0.f, 2.f*M_PI)(rng), 1.f});

			ClipSpaceType clipSpaceTypes[] = {
				ClipSpaceType::PositiveRightHanded_ReverseZ,
				ClipSpaceType::PositiveRightHanded,
				ClipSpaceType::Positive_ReverseZ,
				ClipSpaceType::Positive
			};
			std::vector<IOrthoShadowProjections::OrthoSubProjection> baseline;
			Float4x4 baselineWorldToOrthoView;
			for (unsigned clipSpace=0; clipSpace<dimof(clipSpaceTypes); ++clipSpace) {
				Techniques::ProjectionDesc projDesc;
				projDesc._verticalFov = sceneCamera._verticalFieldOfView;
				projDesc._aspectRatio = 1920.f/1080.f;
				projDesc._nearClip = sceneCamera._nearClip;
				projDesc._farClip = sceneCamera._farClip;
				projDesc._cameraToProjection = PerspectiveProjection(
					sceneCamera._verticalFieldOfView, projDesc._aspectRatio,
					sceneCamera._nearClip, sceneCamera._farClip, 
					GeometricCoordinateSpace::RightHanded, 
					clipSpaceTypes[clipSpace]);
				projDesc._worldToProjection = Combine(InvertOrthonormalTransform(sceneCamera._cameraToWorld), projDesc._cameraToProjection);
				projDesc._cameraToWorld = sceneCamera._cameraToWorld;

				auto [subProjections, worldToOrthoView] = LightingEngine::Internal::TestResolutionNormalizedOrthogonalShadowProjections(
					negativeLightDirection, projDesc, sunSourceFrustumSettings, clipSpaceTypes[clipSpace]);
				if (clipSpace == 0) {
					baseline = subProjections;
					baselineWorldToOrthoView = worldToOrthoView;
				} else {
					/*if (c == 240) {
						auto a = AsWorldToProjs(baseline, baselineWorldToOrthoView);
						auto b = AsWorldToProjs(subProjections, worldToOrthoView);
						a.push_back(projDesc._worldToProjection);
						b.push_back(projDesc._worldToProjection);
						{
							auto outputName = std::filesystem::temp_directory_path() / "xle-unit-tests" / "sun-source-cascades-a.ply";
							std::ofstream plyOut(outputName);
							WriteFrustumListToPLY(plyOut, a);
						}
						{
							auto outputName = std::filesystem::temp_directory_path() / "xle-unit-tests" / "sun-source-cascades-b.ply";
							std::ofstream plyOut(outputName);
							WriteFrustumListToPLY(plyOut, b);
						}
					}*/

					REQUIRE(baseline.size() == subProjections.size());
					for (unsigned q=0; q<baseline.size(); ++q) {
						auto lhs = baseline[q];
						auto rhs = subProjections[q];
						// We should expect some differences, because we do loose a fair bit of precision with float projection matrices
						// Meaningful differences should still show up
						float precisionLeftTopFront = std::max(1e-3f, std::max(Magnitude(lhs._leftTopFront), Magnitude(rhs._leftTopFront))/100.f);
						REQUIRE(std::abs(lhs._leftTopFront[0] - rhs._leftTopFront[0]) <= precisionLeftTopFront);
						REQUIRE(std::abs(lhs._leftTopFront[1] - rhs._leftTopFront[1]) <= precisionLeftTopFront);
						REQUIRE(std::abs(lhs._leftTopFront[2] - rhs._leftTopFront[2]) <= precisionLeftTopFront);
						float precisionRightBottomBack = std::max(1e-3f, std::max(Magnitude(lhs._rightBottomBack), Magnitude(rhs._rightBottomBack))/100.f);
						REQUIRE(std::abs(lhs._rightBottomBack[0] - rhs._rightBottomBack[0]) <= precisionRightBottomBack);
						REQUIRE(std::abs(lhs._rightBottomBack[1] - rhs._rightBottomBack[1]) <= precisionRightBottomBack);
						REQUIRE(std::abs(lhs._rightBottomBack[2] - rhs._rightBottomBack[2]) <= precisionRightBottomBack);
					}
				}
			}
		}
	}
}
