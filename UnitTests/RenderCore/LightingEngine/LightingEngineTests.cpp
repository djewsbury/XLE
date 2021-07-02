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
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../../RenderCore/Techniques/CommonBindings.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../RenderCore/Techniques/PipelineCollection.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../../Math/Transformations.h"
#include "../../../Math/ProjectionMath.h"
#include "../../../Assets/IAsyncMarker.h"
#include "../../../Assets/Assets.h"
#include "../../../xleres/FileList.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	static RenderCore::LightingEngine::ILightScene::LightSourceId CreateTestLight(RenderCore::LightingEngine::ILightScene& lightScene)
	{
		using namespace RenderCore::LightingEngine;
		auto lightId = lightScene.CreateLightSource(0);

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

	static RenderCore::LightingEngine::ILightScene::ShadowProjectionId CreateTestShadowProjection(RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId lightSourceId)
	{
		using namespace RenderCore::LightingEngine;
		auto shadowId = lightScene.CreateShadowProjection(0, lightSourceId);

		auto* projections = lightScene.TryGetShadowProjectionInterface<IOrthoShadowProjections>(shadowId);
		REQUIRE(projections);

		auto camToWorld = MakeCameraToWorld(Float3{0.f, -1.0f, 0.f}, Float3{0.f, 0.0f, 1.f}, Float3{0.f, 10.0f, 0.f});
		projections->SetWorldToOrthoView(InvertOrthonormalTransform(camToWorld));

		IOrthoShadowProjections::OrthoSubProjection subProj[] = {
			{ Float3{-shadowFrustumWidth/2.0f, shadowFrustumWidth/2.0f, 0.0f}, Float3{shadowFrustumWidth/2.0f, -shadowFrustumWidth/2.0f, shadowDepthRange} }
		};
		projections->SetOrthoSubProjections(MakeIteratorRange(subProj));

		IShadowPreparer::Desc desc;
		desc._worldSpaceResolveBias = 0.f;
        desc._tanBlurAngle = 0.00436f;
        desc._minBlurSearch = 0.5f;
        desc._maxBlurSearch = 25.f;
		auto* preparer = lightScene.TryGetShadowProjectionInterface<IShadowPreparer>(shadowId);
		REQUIRE(preparer);
		preparer->SetDesc(desc);

		return shadowId;
	}

	static void ConfigureLightScene(RenderCore::LightingEngine::ILightScene& lightScene)
	{
		auto srcId = CreateTestLight(lightScene);
		CreateTestShadowProjection(lightScene, srcId);
	}

	static RenderCore::LightingEngine::ILightScene::ShadowProjectionId CreateSphereShadowProjection(RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId lightSourceId)
	{
		using namespace RenderCore::LightingEngine;
		auto shadowId = lightScene.CreateShadowProjection(0, lightSourceId);
		
		auto* positional = lightScene.TryGetLightSourceInterface<IPositionalLightSource>(lightSourceId);
		REQUIRE(positional);

		auto* finite = lightScene.TryGetLightSourceInterface<IFiniteLightSource>(lightSourceId);
		REQUIRE(finite);

		// Build 6 projection for the cube faces
		// Using DirectX conventions for face order here:
		//		+X, -X
		//		+Y, -Y
		//		+Z, -Z
		Float3 faceForward[] {
			Float3{1.f, 0.f, 0.f},
			Float3{-1.f, 0.f, 0.f},
			Float3{0.f, 1.f, 0.f},
			Float3{0.f, -1.f, 0.f},
			Float3{0.f, 0.f, 1.f},
			Float3{0.f, 0.f, -1.f}
		};
		Float3 faceUp[] = {
			Float3{0.f, 1.f, 0.f},
			Float3{0.f, 1.f, 0.f},
			Float3{0.f, 0.f, -1.f},
			Float3{0.f, 0.f, 1.f},
			Float3{0.f, 1.f, 0.f},
			Float3{0.f, 1.f, 0.f}
		};
		Float4x4 worldToCamera[6];
		Float4x4 cameraToProjection[6];
		for (unsigned c=0; c<6; ++c) {
			cameraToProjection[c] = PerspectiveProjection(
				gPI/2.0f, 1.0f, 0.01f, finite->GetCutoffRange(), 
				GeometricCoordinateSpace::RightHanded,
				RenderCore::Techniques::GetDefaultClipSpaceType());
			auto camToWorld = MakeCameraToWorld(faceForward[c], faceUp[c], ExtractTranslation(positional->GetLocalToWorld()));
			worldToCamera[c] = InvertOrthonormalTransform(camToWorld);
		}

		auto* projections = lightScene.TryGetShadowProjectionInterface<IArbitraryShadowProjections>(shadowId);
		REQUIRE(projections);
		projections->SetArbitrarySubProjections(MakeIteratorRange(worldToCamera), MakeIteratorRange(cameraToProjection));
		return shadowId;
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

	TEST_CASE( "LightingEngine-ExecuteTechnique", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();

		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(256, 256, RenderCore::Format::R8G8B8A8_UNORM),
			"temporary-out");
		
		auto threadContext = testHelper->_device->GetImmediateContext();
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);

		// auto drawableWriter = CreateSphereDrawablesWriter(*testHelper, *testApparatus._pipelineAcceleratorPool);
		auto drawableWriter = ToolsRig::CreateShapeStackDrawableWriter(*testHelper->_device, *testApparatus._pipelineAcceleratorPool);

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

		auto parsingContext = InitializeParsingContext(*testApparatus._techniqueContext, targetDesc, camera);
		parsingContext.GetTechniqueContext()._attachmentPool->Bind(Techniques::AttachmentSemantics::ColorLDR, fbHelper.GetMainTarget());

		testHelper->BeginFrameCapture();

		///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if 0
		SECTION("Forward lighting")
		{
			auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
			auto lightingTechniqueFuture = LightingEngine::CreateForwardLightingTechnique(
				testHelper->_device,
				testApparatus._pipelineAcceleratorPool, testApparatus._techDelBox,
				stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps);
			auto lightingTechnique = StallAndRequireReady(*lightingTechniqueFuture);
			ConfigureLightScene(LightingEngine::GetLightScene(*lightingTechnique));

			// stall until all resources are ready
			{
				RenderCore::LightingEngine::LightingTechniqueInstance prepareLightingIterator(*testApparatus._pipelineAcceleratorPool, *lightingTechnique);
				ParseScene(prepareLightingIterator, *drawableWriter);
				auto prepareMarker = prepareLightingIterator.GetResourcePreparationMarker();
				if (prepareMarker) {
					prepareMarker->StallWhilePending();
					REQUIRE(prepareMarker->GetAssetState() == ::Assets::AssetState::Ready);
				}
			}

			{
				RenderCore::LightingEngine::LightingTechniqueInstance lightingIterator(
					*threadContext, parsingContext, *testApparatus._pipelineAcceleratorPool, *lightingTechnique);
				ParseScene(lightingIterator, *drawableWriter);
			}

			fbHelper.SaveImage(*threadContext, "forward-lighting-output");
		}
#endif

		///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		SECTION("Deferred lighting")
		{
			LightingOperatorsPipelineLayout pipelineLayout(*testHelper);

			LightingEngine::LightSourceOperatorDesc resolveOperators[] {
				LightingEngine::LightSourceOperatorDesc{}
			};
			LightingEngine::ShadowOperatorDesc shadowOp;
			shadowOp._projectionMode = LightingEngine::ShadowProjectionMode::Ortho;

			float wsDepthResolution = shadowDepthRange / 16384.f;
			float wsXYRange = shadowFrustumWidth / 2048.f;
			float ratio0 = wsXYRange / wsDepthResolution;
			float ratio1 = std::sqrt(wsXYRange*wsXYRange + wsXYRange*wsXYRange) / wsDepthResolution;
			shadowOp._rasterDepthBias = (int)std::ceil(ratio1);
			shadowOp._slopeScaledBias = 0.5f;

			LightingEngine::ShadowOperatorDesc shadowGenerator[] {
				shadowOp
			};

			auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
			auto lightingTechniqueFuture = LightingEngine::CreateDeferredLightingTechnique(
				testHelper->_device,
				testApparatus._pipelineAcceleratorPool, testApparatus._sharedDelegates, pipelineLayout._pipelineCollection, pipelineLayout._pipelineLayoutFile,
				MakeIteratorRange(resolveOperators), MakeIteratorRange(shadowGenerator), 
				stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps);
			auto lightingTechnique = StallAndRequireReady(*lightingTechniqueFuture);
			ConfigureLightScene(LightingEngine::GetLightScene(*lightingTechnique));

			testApparatus._bufferUploads->Update(*threadContext);
			Threading::Sleep(16);
			testApparatus._bufferUploads->Update(*threadContext);

			// stall until all resources are ready
			{
				RenderCore::LightingEngine::LightingTechniqueInstance prepareLightingIterator(*testApparatus._pipelineAcceleratorPool, *lightingTechnique);
				ParseScene(prepareLightingIterator, *drawableWriter);
				auto prepareMarker = prepareLightingIterator.GetResourcePreparationMarker();
				if (prepareMarker) {
					prepareMarker->StallWhilePending();
					REQUIRE(prepareMarker->GetAssetState() == ::Assets::AssetState::Ready);
				}
			}

			{
				RenderCore::LightingEngine::LightingTechniqueInstance lightingIterator(
					*threadContext, parsingContext, *testApparatus._pipelineAcceleratorPool, *lightingTechnique);
				ParseScene(lightingIterator, *drawableWriter);
			}

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
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(2048, 2048, RenderCore::Format::R8G8B8A8_UNORM),
			"temporary-out");
		
		auto threadContext = testHelper->_device->GetImmediateContext();
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);

		auto drawableWriter = ToolsRig::CreateStonehengeDrawableWriter(*testHelper->_device, *testApparatus._pipelineAcceleratorPool);

		RenderCore::Techniques::CameraDesc camera;
		camera._cameraToWorld = MakeCameraToWorld(-Normalize(Float3{-8.0f, 5.f, 0.f}), Float3{0.0f, 1.0f, 0.0f}, Float3{-8.0f, 5.f, 0.f});
		
		auto parsingContext = InitializeParsingContext(*testApparatus._techniqueContext, targetDesc, camera);
		parsingContext.GetTechniqueContext()._attachmentPool->Bind(Techniques::AttachmentSemantics::ColorLDR, fbHelper.GetMainTarget());

		testHelper->BeginFrameCapture();

		{
			LightingOperatorsPipelineLayout pipelineLayout(*testHelper);

			LightingEngine::LightSourceOperatorDesc resolveOperators[] {
				LightingEngine::LightSourceOperatorDesc {
					LightingEngine::LightSourceShape::Sphere
				}
			};
			LightingEngine::ShadowOperatorDesc shadowOpDesc;
			shadowOpDesc._projectionMode = LightingEngine::ShadowProjectionMode::ArbitraryCubeMap;
			shadowOpDesc._normalProjCount = 6;
			shadowOpDesc._width = 256;
			shadowOpDesc._height = 256;
			LightingEngine::ShadowOperatorDesc shadowGenerator[] {
				shadowOpDesc
			};

			auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
			auto lightingTechniqueFuture = LightingEngine::CreateDeferredLightingTechnique(
				testHelper->_device,
				testApparatus._pipelineAcceleratorPool, testApparatus._sharedDelegates, pipelineLayout._pipelineCollection, pipelineLayout._pipelineLayoutFile,
				MakeIteratorRange(resolveOperators), MakeIteratorRange(shadowGenerator), 
				stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps);
			auto lightingTechnique = StallAndRequireReady(*lightingTechniqueFuture);

			auto& lightScene = LightingEngine::GetLightScene(*lightingTechnique);
			auto lightId = CreateTestLight(lightScene);
			CreateSphereShadowProjection(lightScene, lightId);

			testApparatus._bufferUploads->Update(*threadContext);
			Threading::Sleep(16);
			testApparatus._bufferUploads->Update(*threadContext);

			// stall until all resources are ready
			{
				RenderCore::LightingEngine::LightingTechniqueInstance prepareLightingIterator(*testApparatus._pipelineAcceleratorPool, *lightingTechnique);
				ParseScene(prepareLightingIterator, *drawableWriter);
				auto prepareMarker = prepareLightingIterator.GetResourcePreparationMarker();
				if (prepareMarker) {
					prepareMarker->StallWhilePending();
					REQUIRE(prepareMarker->GetAssetState() == ::Assets::AssetState::Ready);
				}
			}

			{
				RenderCore::LightingEngine::LightingTechniqueInstance lightingIterator(
					*threadContext, parsingContext, *testApparatus._pipelineAcceleratorPool, *lightingTechnique);
				ParseScene(lightingIterator, *drawableWriter);
			}

			fbHelper.SaveImage(*threadContext, "sphere-light-shadows-output");
		}

		testHelper->EndFrameCapture();
	}

}
