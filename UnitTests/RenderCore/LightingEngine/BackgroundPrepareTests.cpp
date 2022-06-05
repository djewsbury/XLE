// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineTestHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../SceneEngine/IScene.h"
#include "../../../SceneEngine/Tonemap.h"
#include "../../../RenderCore/LightingEngine/DeferredLightingDelegate.h"
#include "../../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../../RenderCore/LightingEngine/ShadowProbes.h"
#include "../../../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/CommonBindings.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/Drawables.h"
#include "../../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../../RenderCore/Techniques/SimpleModelRenderer.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/QueryPool.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../../Math/Transformations.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/AssetTraits.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../Utility/ArithmeticUtils.h"
#include "../../../xleres/FileList.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <random>

namespace UnitTests
{
	struct PreparedSceneForShadowProbe
	{
		std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> _compiledLightingTechnique;
		std::shared_ptr<SceneEngine::ILightingStateDelegate> _lightingStateDelegate;
		std::shared_ptr<ToolsRig::IDrawablesWriter> _drawablesWriter;

		void BindLightScene()
		{
			if (_lightingStateDelegate && _compiledLightingTechnique) {
				auto& lightScene = RenderCore::LightingEngine::GetLightScene(*_compiledLightingTechnique);
				_lightingStateDelegate->BindScene(lightScene);
			}
		}

		void UnbindLightScene()
		{
			if (_lightingStateDelegate && _compiledLightingTechnique) {
				auto& lightScene = RenderCore::LightingEngine::GetLightScene(*_compiledLightingTechnique);
				_lightingStateDelegate->UnbindScene(lightScene);
			}
		}

		PreparedSceneForShadowProbe(
			std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> compiledLightingTechnique,
			std::shared_ptr<SceneEngine::ILightingStateDelegate> lightingStateDelegate,
			std::shared_ptr<ToolsRig::IDrawablesWriter> drawablesWriter)
		: _compiledLightingTechnique(std::move(compiledLightingTechnique))
		, _lightingStateDelegate(std::move(lightingStateDelegate))
		, _drawablesWriter(std::move(drawablesWriter))
		{
			BindLightScene();
		}

		~PreparedSceneForShadowProbe()
		{
			UnbindLightScene();
		}

		PreparedSceneForShadowProbe() = default;
		PreparedSceneForShadowProbe(PreparedSceneForShadowProbe&& moveFrom) = default;
		PreparedSceneForShadowProbe& operator=(PreparedSceneForShadowProbe&& moveFrom)
		{
			if (this == &moveFrom) return *this;
			UnbindLightScene();
			_compiledLightingTechnique = std::move(moveFrom._compiledLightingTechnique);
			_lightingStateDelegate = std::move(moveFrom._lightingStateDelegate);
			_drawablesWriter = std::move(moveFrom._drawablesWriter);
			return *this;
		}

		static void PrepareProbes(
			RenderCore::IThreadContext& threadContext,
			RenderCore::LightingEngine::IProbeRenderingInstance& renderingInstance,
			ToolsRig::IDrawablesWriter& drawablesWriter,
			RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		{
			while (auto nextStep = renderingInstance.GetNextStep()) {
				assert(nextStep._type == RenderCore::LightingEngine::StepType::MultiViewParseScene);
				if (nextStep._pkts.empty() || !nextStep._pkts[0]) continue;
				uint64_t viewMask = (1ull << uint64_t(nextStep._multiViewDesc.size())) - 1ull;
				drawablesWriter.WriteDrawables(*nextStep._pkts[0], viewMask);
			}
		}

		static void ConstructToPromise(
			std::promise<PreparedSceneForShadowProbe>&& promise,
			std::shared_ptr<SceneEngine::ILightingStateDelegate> lightingDelegate,
			std::shared_ptr<ToolsRig::IDrawablesWriter> drawablesWriter,
			LightingEngineTestApparatus& lightingApparatus,
			IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
			const RenderCore::FrameBufferProperties& fbProps)
		{
			SceneEngine::ILightingStateDelegate::Operators operators;
			if (lightingDelegate)
				operators = lightingDelegate->GetOperators();

			LightingOperatorsPipelineLayout pipelineLayout(*lightingApparatus._metalTestHelper);

			auto techniqueFuture = RenderCore::LightingEngine::CreateDeferredLightingTechnique(
				lightingApparatus._pipelineAccelerators, lightingApparatus._pipelinePool, lightingApparatus._sharedDelegates,
				pipelineLayout._pipelineLayout, pipelineLayout._dmShadowDescSetTemplate,
				operators._lightResolveOperators, operators._shadowResolveOperators,
				preregAttachments, fbProps);

			::Assets::WhenAll(std::move(techniqueFuture)).ThenConstructToPromise(
				std::move(promise),
				[lightingDelegate=std::move(lightingDelegate), drawablesWriter=std::move(drawablesWriter), pipelineAcceleratorPool=lightingApparatus._pipelineAccelerators](auto lightingTechnique) mutable {

					PreparedSceneForShadowProbe result{
						std::move(lightingTechnique),
						std::move(lightingDelegate),
						std::move(drawablesWriter)};

					if (result._lightingStateDelegate) {
						// BeginPrepareStep must happen after we construct PreparedScene, since that binds the light 
						// scene to the lighting delegate
						auto threadContext = RenderCore::Techniques::GetThreadContext();
						auto& lightScene = RenderCore::LightingEngine::GetLightScene(*result._compiledLightingTechnique);
						auto ri = result._lightingStateDelegate->BeginPrepareStep(lightScene, *threadContext);
						if (ri) {
							PrepareProbes(*threadContext, *ri, *result._drawablesWriter, *pipelineAcceleratorPool);
							
							// We must ensure that the relevant buffer uploads cmdlist has been submitted before
							// we call CommitCommands(). This is a little more awkward given that we're in a background
							// thread, and requires that a thread with the immediate context is pumping BufferUploads::Update()
							assert(!threadContext->IsImmediate());
							auto bufferUploadsCmdList = ri->GetRequiredBufferUploadsCommandList();
							if (bufferUploadsCmdList) {
								auto& bu = RenderCore::Techniques::Services::GetBufferUploads();
								while (!bu.IsComplete(bufferUploadsCmdList))
									std::this_thread::sleep_for(std::chrono::milliseconds(2));
							}
							threadContext->CommitCommands();
						}
					}

					return result;
				});
		}
	};

	class LightingStateDelegate : public SceneEngine::ILightingStateDelegate
	{
	public:
		void PreRender(
			const RenderCore::Techniques::ProjectionDesc& mainSceneCameraDesc, 
			RenderCore::LightingEngine::ILightScene& lightScene) override {}
		void PostRender(RenderCore::LightingEngine::ILightScene& lightScene) override {}
		void BindScene(RenderCore::LightingEngine::ILightScene& lightScene) override 
		{
			REQUIRE(_lightSourcesId.empty()); REQUIRE(!_shadowProjectionId);
			_lightSourcesId.emplace_back(lightScene.CreateLightSource(0));
			_lightSourcesId.emplace_back(lightScene.CreateLightSource(0));
			_lightSourcesId.emplace_back(lightScene.CreateLightSource(0));
			_shadowProjectionId = lightScene.CreateShadowProjection(0, MakeIteratorRange(_lightSourcesId));

			// red
			lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(_lightSourcesId[0])->SetLocalToWorld(AsFloat4x4(Float3(50, 5, 50)));
			lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IFiniteLightSource>(_lightSourcesId[0])->SetCutoffRange(50);
			lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(_lightSourcesId[0])->SetBrightness(Float3{100.f, 0.f, 0.f});

			// green
			lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(_lightSourcesId[1])->SetLocalToWorld(AsFloat4x4(Float3(30, 5, 40)));
			lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IFiniteLightSource>(_lightSourcesId[1])->SetCutoffRange(50);
			lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(_lightSourcesId[1])->SetBrightness(Float3{0.f, 100.f, 0.f});

			// blue
			lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(_lightSourcesId[2])->SetLocalToWorld(AsFloat4x4(Float3(55, 5, 60)));
			lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IFiniteLightSource>(_lightSourcesId[2])->SetCutoffRange(50);
			lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(_lightSourcesId[2])->SetBrightness(Float3{0.f, 0.f, 100.f});
		}
		void UnbindScene(RenderCore::LightingEngine::ILightScene& lightScene) override
		{
			if (_shadowProjectionId) lightScene.DestroyShadowProjection(*_shadowProjectionId);
			for (auto light:_lightSourcesId) lightScene.DestroyLightSource(light);
			_shadowProjectionId = {};
			_lightSourcesId.clear();
		}
		std::shared_ptr<RenderCore::LightingEngine::IProbeRenderingInstance> BeginPrepareStep(
			RenderCore::LightingEngine::ILightScene& lightScene,
			RenderCore::IThreadContext& threadContext) override
		{ 
			if (_shadowProjectionId.has_value()) {
				if (auto* props = lightScene.TryGetShadowProjectionInterface<RenderCore::LightingEngine::IShadowProbeDatabase>(*_shadowProjectionId))
					props->SetNearRadius(0.2f);
				if (auto* prepareable = lightScene.TryGetShadowProjectionInterface<RenderCore::LightingEngine::IPreparable>(*_shadowProjectionId))
					return prepareable->BeginPrepare(threadContext);
			}
			return nullptr;
		}

		Operators GetOperators() override
		{
			RenderCore::LightingEngine::LightSourceOperatorDesc lightOp;
			lightOp._shape = RenderCore::LightingEngine::LightSourceShape::Sphere;
			RenderCore::LightingEngine::ShadowOperatorDesc shadowOp;
			shadowOp._resolveType = RenderCore::LightingEngine::ShadowResolveType::Probe;
			// we need some bias to avoid rampant acne
			shadowOp._singleSidedBias._depthBias = -8;
			shadowOp._singleSidedBias._slopeScaledBias = -0.5f;
			shadowOp._doubleSidedBias = shadowOp._singleSidedBias;
			Operators result;
			result._lightResolveOperators.emplace_back(lightOp);
			result._shadowResolveOperators.emplace_back(shadowOp);
			return result;
		}

		auto GetEnvironmentalLightingDesc() -> SceneEngine::EnvironmentalLightingDesc override { return {}; }
		auto GetToneMapSettings() -> SceneEngine::ToneMapSettings override { return {}; }

		std::vector<RenderCore::LightingEngine::ILightScene::LightSourceId> _lightSourcesId;
		std::optional<RenderCore::LightingEngine::ILightScene::ShadowProjectionId> _shadowProjectionId;

		const ::Assets::DependencyValidation& GetDependencyValidation() const override { return _depVal; }
		::Assets::DependencyValidation _depVal;
	};

	TEST_CASE( "LightingEngine-BackgroundShadowProbeRender", "[rendercore_lighting_engine]" )
	{
		//
		//		Construct a lighting technique that requires some GPU side prepare work (which
		//		it does in the background) before it can be used
		//

		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();
		auto threadContext = testApparatus._metalTestHelper->_device->GetImmediateContext();

		testHelper->BeginFrameCapture();

		Techniques::CameraDesc camera;
		camera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{1.f, -0.85f, 1.0f}), Normalize(Float3{0.0f, 1.0f, 0.0f}), Float3{15.0f, 25.0f, 15.0f});
		camera._projection = Techniques::CameraDesc::Projection::Orthogonal;
		camera._nearClip = 0.f;
		camera._farClip = 100.f;		// a small far clip here reduces the impact of gbuffer reconstruction accuracy on sampling
		camera._left = -20.f; camera._right = 20.f;
		camera._top = 20.f; camera._bottom = -20.f;

		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(1024, 1024, RenderCore::Format::R8G8B8A8_UNORM),
			"temporary-out");
		auto parsingContext = BeginParsingContext(testApparatus, *threadContext, targetDesc, camera);

		const Float2 worldMins{0.f, 0.f}, worldMaxs{100.f, 100.f};
		auto drawablesWriter = ToolsRig::DrawablesWriterHelper(*testHelper->_device, *testApparatus._drawablesPool, *testApparatus._pipelineAccelerators)
			.CreateShapeWorldDrawableWriter(worldMins, worldMaxs);

		auto lightingDelegate = std::make_shared<LightingStateDelegate>();

		std::promise<PreparedSceneForShadowProbe> promise;
		auto futureScene = promise.get_future();
		PreparedSceneForShadowProbe::ConstructToPromise(
			std::move(promise),
			lightingDelegate, drawablesWriter,
			testApparatus, 
			parsingContext.GetFragmentStitchingContext().GetPreregisteredAttachments(), parsingContext.GetFragmentStitchingContext()._workingProps);
		auto scene = futureScene.get();		// consider drawing some frames in the foreground while we wait for this

		{
			LightingEngine::LightingTechniqueInstance prepareInstance{*scene._compiledLightingTechnique};
			ParseScene(prepareInstance, *scene._drawablesWriter);
			auto marker = prepareInstance.GetResourcePreparationMarker();
			auto newVisibility = marker.get();		// stall
			if (newVisibility._bufferUploadsVisibility)
				testApparatus._bufferUploads->StallUntilCompletion(*threadContext, newVisibility._bufferUploadsVisibility);
		}

		{
			auto parsingContext = BeginParsingContext(testApparatus, *threadContext, targetDesc, camera);
			LightingEngine::LightingTechniqueInstance drawInstance{parsingContext, *scene._compiledLightingTechnique};
			ParseScene(drawInstance, *scene._drawablesWriter);
		}

		auto colorLDR = parsingContext.GetTechniqueContext()._attachmentPool->GetBoundResource(Techniques::AttachmentSemantics::ColorLDR);
		REQUIRE(colorLDR);
		SaveImage(*threadContext, *colorLDR, "background-probe-prepare");

		testHelper->EndFrameCapture();
	}
}
