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
#include "../../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../RenderCore/IThreadContext.h"
#include "../../../RenderOverlays/OverlayContext.h"
#include "../../../RenderOverlays/DebuggingDisplay.h"
#include "../../../RenderOverlays/FontRendering.h"
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

		IDepthTextureResolve::Desc desc;
		desc._worldSpaceResolveBias = 0.f;
        desc._tanBlurAngle = 0.00436f;
		desc._minBlurSearch = 3.f;
        desc._maxBlurSearch = 35.f;
		auto* preparer = lightScene.TryGetShadowProjectionInterface<IDepthTextureResolve>(shadowId);
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
		std::shared_ptr<RenderCore::Techniques::PipelinePool> _pipelineCollection;
		std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> _dmShadowDescSetTemplate;

		LightingOperatorsPipelineLayout(const MetalTestHelper& testHelper)
		{	
			auto pipelineLayoutFileFuture = ::Assets::MakeAsset<RenderCore::Assets::PredefinedPipelineLayoutFile>(LIGHTING_OPERATOR_PIPELINE);
			_pipelineLayoutFile = StallAndRequireReady(*pipelineLayoutFileFuture);

			const std::string pipelineLayoutName = "LightingOperator";
			auto pipelineInit = RenderCore::Assets::PredefinedPipelineLayout{*_pipelineLayoutFile, pipelineLayoutName}.MakePipelineLayoutInitializer(testHelper._shaderCompiler->GetShaderLanguage());
			_pipelineLayout = testHelper._device->CreatePipelineLayout(pipelineInit);

			auto i = _pipelineLayoutFile->_descriptorSets.find("DMShadow");
			if (i == _pipelineLayoutFile->_descriptorSets.end())
				Throw(std::runtime_error("Missing ShadowTemplate entry in pipeline layout file"));
			_dmShadowDescSetTemplate = i->second;

			_pipelineCollection = std::make_shared<RenderCore::Techniques::PipelinePool>(testHelper._device);
		}
	};

	static void PrepareResources(ToolsRig::IDrawablesWriter& drawablesWriter, LightingEngineTestApparatus& testApparatus, RenderCore::LightingEngine::CompiledLightingTechnique& lightingTechnique)
	{
		// stall until all resources are ready
		RenderCore::LightingEngine::LightingTechniqueInstance prepareLightingIterator(lightingTechnique);
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
					testApparatus._pipelineAcceleratorPool, testApparatus._sharedDelegates, pipelineLayout._pipelineCollection, pipelineLayout._pipelineLayout, pipelineLayout._dmShadowDescSetTemplate,
					MakeIteratorRange(resolveOperators), MakeIteratorRange(shadowGenerator), 
					stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps);
				auto lightingTechnique = StallAndRequireReady(*lightingTechniqueFuture);
				PumpBufferUploads(testApparatus);

				auto drawableWriter = ToolsRig::CreateFlatPlaneDrawableWriter(*testHelper->_device, *testApparatus._pipelineAcceleratorPool);
				PrepareResources(*drawableWriter, testApparatus, *lightingTechnique);

				auto& lightScene = LightingEngine::GetLightScene(*lightingTechnique);
				for (unsigned c=0; c<stripes; ++c) {
					auto lightId = ConfigureLightScene(lightScene, gPI/2.0f*c/float(stripes));

					{
						RenderCore::LightingEngine::LightingTechniqueInstance lightingIterator(
							*threadContext, parsingContext, *lightingTechnique);
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
					testApparatus._pipelineAcceleratorPool, testApparatus._sharedDelegates, pipelineLayout._pipelineCollection, pipelineLayout._pipelineLayout, pipelineLayout._dmShadowDescSetTemplate,
					MakeIteratorRange(resolveOperators), MakeIteratorRange(shadowGenerator), 
					stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps);
				auto lightingTechnique = StallAndRequireReady(*lightingTechniqueFuture);
				PumpBufferUploads(testApparatus);

				auto drawableWriter = ToolsRig::CreateSharpContactDrawableWriter(*testHelper->_device, *testApparatus._pipelineAcceleratorPool);
				PrepareResources(*drawableWriter, testApparatus, *lightingTechnique);

				auto& lightScene = LightingEngine::GetLightScene(*lightingTechnique);
				auto lightId = ConfigureLightScene(lightScene, gPI/4.0f);

				{
					RenderCore::LightingEngine::LightingTechniqueInstance lightingIterator(
						*threadContext, parsingContext, *lightingTechnique);
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
			_immediateDrawables =  RenderCore::Techniques::CreateImmediateDrawables(metalHelper._device);
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
		unsigned shadowProjectionId,
		const RenderCore::Techniques::CameraDesc& sceneCamera)
	{
		using namespace RenderCore;
		auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(
			threadContext, *immediateDrawingHelper._immediateDrawables, immediateDrawingHelper._fontRenderingManager.get());

		unsigned colorIterator = 0;
		auto* shadowProj = lightScene.TryGetShadowProjectionInterface<LightingEngine::IOrthoShadowProjections>(shadowProjectionId);
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

		auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, parsingContext);
		auto prepare = immediateDrawingHelper._immediateDrawables->PrepareResources(rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
		if (prepare) {
			prepare->StallWhilePending();
			REQUIRE(prepare->GetAssetState() == ::Assets::AssetState::Ready);
		}
		immediateDrawingHelper._immediateDrawables->ExecuteDraws(threadContext, parsingContext, rpi);
	}

	static void DrawCascadeColors(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::ParsingContext& parsingContext,
		const std::shared_ptr<RenderCore::Techniques::PipelinePool>& pipelinePool,
		const std::shared_ptr<RenderCore::ICompiledPipelineLayout>& pipelineLayout)
	{
		using namespace RenderCore;
		auto rpi = Techniques::RenderPassToPresentationTarget(threadContext, parsingContext);
		UniformsStreamInterface usi;
		auto cascadeIndexTexture = parsingContext.GetTechniqueContext()._attachmentPool->GetBoundResource(Hash64("CascadeIndex")+0);
		auto cascadeIndexTextureSRV = cascadeIndexTexture->CreateTextureView();
		usi.BindResourceView(0, Hash64("PrebuildCascadeIndexTexture"));
		IResourceView* srvs[] = { cascadeIndexTextureSRV.get() };
		UniformsStream us;
		us._resourceViews = MakeIteratorRange(srvs);
		auto op = CreateFullViewportOperator(
			pipelinePool, Techniques::FullViewportOperatorSubType::DisableDepth, CASCADE_VIS_HLSL ":col_vis_pass", {}, pipelineLayout, rpi, usi);
		op->StallWhilePending();
		RenderCore::Techniques::SequencerUniformsHelper uniformsHelper{parsingContext};
		op->Actualize()->Draw(threadContext, parsingContext, uniformsHelper, us);
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
//		const Float3 negativeLightDirection = Normalize(Float3{0.8f, 2.0f, 0.7f});
//		const Float3 negativeLightDirection = Normalize(Float3{-2.69884f, 0.696449f, -2.16482f});

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

				auto parsingContext = InitializeParsingContext(*testApparatus._techniqueContext, targetDesc, sceneCamera);
				auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
				auto lightingTechniqueFuture = LightingEngine::CreateDeferredLightingTechnique(
					testHelper->_device,
					testApparatus._pipelineAcceleratorPool, testApparatus._sharedDelegates, pipelineLayout._pipelineCollection, pipelineLayout._pipelineLayout, pipelineLayout._dmShadowDescSetTemplate,
					MakeIteratorRange(resolveOperators), MakeIteratorRange(shadowGenerator), 
					stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps,
					LightingEngine::DeferredLightingTechniqueFlags::GenerateDebuggingTextures);
				auto lightingTechnique = StallAndRequireReady(*lightingTechniqueFuture);
				PumpBufferUploads(testApparatus);

				const Float2 worldMins{0.f, 0.f}, worldMaxs{100.f, 100.f};
				auto drawableWriter = ToolsRig::CreateShapeWorldDrawableWriter(*testHelper->_device, *testApparatus._pipelineAcceleratorPool, worldMins, worldMaxs);
				PrepareResources(*drawableWriter, testApparatus, *lightingTechnique);

				auto& lightScene = LightingEngine::GetLightScene(*lightingTechnique);
				auto lightId = lightScene.CreateLightSource(0);
				lightScene.TryGetLightSourceInterface<LightingEngine::IPositionalLightSource>(lightId)->SetLocalToWorld(AsFloat4x4(negativeLightDirection));
				auto shadowProjectionId = LightingEngine::CreateShadowCascades(lightScene, 0, lightId, BuildProjectionDesc(sceneCamera, UInt2{2048, 2048}), sunSourceFrustumSettings);

				auto generalPipelineFuture = ::Assets::MakeAsset<RenderCore::Techniques::CompiledPipelineLayoutAsset>(testHelper->_device, GENERAL_OPERATOR_PIPELINE ":GraphicsMain");
				generalPipelineFuture->StallWhilePending();
				REQUIRE(generalPipelineFuture->GetAssetState() == ::Assets::AssetState::Ready);
				auto generalPipeline = generalPipelineFuture->Actualize();

				// draw once from the "scene camera"
				{
					{
						RenderCore::LightingEngine::LightingTechniqueInstance lightingIterator(
							*threadContext, parsingContext, *lightingTechnique);
						ParseScene(lightingIterator, *drawableWriter);
					}

					DrawCascadeColors(*threadContext, parsingContext, testApparatus._pipelinePool, generalPipeline->GetPipelineLayout());

					auto colorLDR = parsingContext.GetTechniqueContext()._attachmentPool->GetBoundResource(Techniques::AttachmentSemantics::ColorLDR);
					REQUIRE(colorLDR);

					SaveImage(*threadContext, *colorLDR, "sun-source-cascades-scene-camera");

					auto cascadeIndexTexture = parsingContext.GetTechniqueContext()._attachmentPool->GetBoundResource(Hash64("CascadeIndex")+0);
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
						RenderCore::LightingEngine::LightingTechniqueInstance lightingIterator(
							*threadContext, parsingContext, *lightingTechnique);
						ParseScene(lightingIterator, *drawableWriter);
					}

					DrawCascadeColors(*threadContext, parsingContext, testApparatus._pipelinePool, generalPipeline->GetPipelineLayout());

					// draw the camera and shadow frustums into the output image
					DrawCameraAndShadowFrustums(*threadContext, immediateDrawingHelper, parsingContext, lightScene, shadowProjectionId, sceneCamera);

					auto colorLDR = parsingContext.GetTechniqueContext()._attachmentPool->GetBoundResource(Techniques::AttachmentSemantics::ColorLDR);
					REQUIRE(colorLDR);

					SaveImage(*threadContext, *colorLDR, "sun-source-cascades-vis-camera-" + std::to_string(c));
				}

				std::vector<Float4x4> worldToProjs;
				worldToProjs.push_back(BuildProjectionDesc(sceneCamera, UInt2{2048, 2048})._worldToProjection);
				auto* orthoShadowProjections = lightScene.TryGetShadowProjectionInterface<RenderCore::LightingEngine::IOrthoShadowProjections>(shadowProjectionId);
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
}
