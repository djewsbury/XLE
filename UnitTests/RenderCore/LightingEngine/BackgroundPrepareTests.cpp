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
				uint32_t viewMask = (1u << uint32_t(nextStep._multiViewDesc.size())) - 1u;
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
			SceneEngine::MergedLightingEngineCfg lightingEngineCfg;
			lightingDelegate->BindCfg(lightingEngineCfg);

			auto techniqueFuture = RenderCore::LightingEngine::CreateDeferredLightingTechnique(
				lightingApparatus._pipelineAccelerators, lightingApparatus._pipelinePool, lightingApparatus._sharedDelegates,
				lightingEngineCfg.GetLightOperators(), lightingEngineCfg.GetShadowOperators(),
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
		void BindScene(RenderCore::LightingEngine::ILightScene& lightScene, std::shared_ptr<::Assets::OperationContext>) override 
		{
			REQUIRE(_lightOperatorId != ~0u); REQUIRE(_shadowOperatorId != ~0u);
			REQUIRE(_lightSourcesId.empty());
			_lightSourcesId.emplace_back(lightScene.CreateLightSource(_lightOperatorId));
			_lightSourcesId.emplace_back(lightScene.CreateLightSource(_lightOperatorId));
			_lightSourcesId.emplace_back(lightScene.CreateLightSource(_lightOperatorId));
			for (auto l:_lightSourcesId)
				lightScene.SetShadowOperator(l, _shadowOperatorId);

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
			for (auto light:_lightSourcesId) lightScene.DestroyLightSource(light);
			_lightSourcesId.clear();
		}
		std::shared_ptr<RenderCore::LightingEngine::IProbeRenderingInstance> BeginPrepareStep(
			RenderCore::LightingEngine::ILightScene& lightScene,
			RenderCore::IThreadContext& threadContext) override
		{ 
			if (auto* scheduler = (RenderCore::LightingEngine::ISemiStaticShadowProbeScheduler*)lightScene.QueryInterface(typeid(RenderCore::LightingEngine::ISemiStaticShadowProbeScheduler).hash_code())) {
				scheduler->SetNearRadius(0.2f);
				return scheduler->BeginPrepare(threadContext, 64);
			}
			return nullptr;
		}

		void BindCfg(SceneEngine::MergedLightingEngineCfg& cfg) override
		{
			RenderCore::LightingEngine::LightSourceOperatorDesc lightOp;
			lightOp._shape = RenderCore::LightingEngine::LightSourceShape::Sphere;
			_lightOperatorId = cfg.Register(lightOp);

			RenderCore::LightingEngine::ShadowOperatorDesc shadowOp;
			shadowOp._resolveType = RenderCore::LightingEngine::ShadowResolveType::Probe;
			// we need some bias to avoid rampant acne
			shadowOp._singleSidedBias._depthBias = 6 * -8;
			shadowOp._singleSidedBias._slopeScaledBias = -0.75f;
			shadowOp._doubleSidedBias = shadowOp._singleSidedBias;
			shadowOp._width = shadowOp._height = 128;
			_shadowOperatorId = cfg.Register(shadowOp);
		}

		std::vector<RenderCore::LightingEngine::ILightScene::LightSourceId> _lightSourcesId;

		unsigned _lightOperatorId = ~0u, _shadowOperatorId = ~0u;
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

		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc,
			TextureDesc::Plain2D(1024, 1024, RenderCore::Format::R8G8B8A8_UNORM));
		auto parsingContext = BeginParsingContext(testApparatus, *threadContext, targetDesc, Techniques::CameraDesc{});

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
			std::promise<Techniques::PreparedResourcesVisibility> preparePromise;
			auto marker = preparePromise.get_future();
			prepareInstance.FulfillWhenNotPending(std::move(preparePromise));
			auto newVisibility = marker.get();		// stall
			if (newVisibility._bufferUploadsVisibility)
				testApparatus._bufferUploads->StallUntilCompletion(*threadContext, newVisibility._bufferUploadsVisibility);
		}

		Techniques::CameraDesc camerasToRender[3];

		camerasToRender[0]._cameraToWorld = MakeCameraToWorld(Normalize(Float3{1.f, -0.85f, 1.0f}), Normalize(Float3{0.0f, 1.0f, 0.0f}), Float3{15.0f, 25.0f, 15.0f});
		camerasToRender[0]._projection = Techniques::CameraDesc::Projection::Orthogonal;
		camerasToRender[0]._nearClip = 0.f;
		camerasToRender[0]._farClip = 100.f;		// a small far clip here reduces the impact of gbuffer reconstruction accuracy on sampling
		camerasToRender[0]._left = -20.f; camerasToRender[0]._right = 20.f;
		camerasToRender[0]._top = 20.f; camerasToRender[0]._bottom = -20.f;

		camerasToRender[1]._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, -1.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, 1.0f}), Float3{50.0f, 25.0f, 50.0f});
		camerasToRender[1]._projection = Techniques::CameraDesc::Projection::Orthogonal;
		camerasToRender[1]._nearClip = 0.f;
		camerasToRender[1]._farClip = 100.f;		// a small far clip here reduces the impact of gbuffer reconstruction accuracy on sampling
		camerasToRender[1]._left = -20.f; camerasToRender[1]._right = 20.f;
		camerasToRender[1]._top = 20.f; camerasToRender[1]._bottom = -20.f;

		camerasToRender[2]._cameraToWorld = MakeCameraToWorld(Normalize(Float3{1.f, -2.f, 1.0f}), Normalize(Float3{0.0f, 1.0f, 0.0f}), Float3{12.5f, 25.0f, 32.5f});
		camerasToRender[2]._projection = Techniques::CameraDesc::Projection::Orthogonal;
		camerasToRender[2]._nearClip = 0.f;
		camerasToRender[2]._farClip = 100.f;		// a small far clip here reduces the impact of gbuffer reconstruction accuracy on sampling
		camerasToRender[2]._left = -5.f; camerasToRender[2]._right = 5.f;
		camerasToRender[2]._top = 5.f; camerasToRender[2]._bottom = -5.f;

		for (unsigned c=0; c<dimof(camerasToRender); ++c) {
			{
				parsingContext.GetProjectionDesc() = BuildProjectionDesc(camerasToRender[c], UInt2{targetDesc._textureDesc._width, targetDesc._textureDesc._height});
				parsingContext.SetPipelineAcceleratorsVisibility(testApparatus._pipelineAccelerators->VisibilityBarrier());
				LightingEngine::LightingTechniqueInstance drawInstance{parsingContext, *scene._compiledLightingTechnique};
				ParseScene(drawInstance, *scene._drawablesWriter);
			}

			auto colorLDR = parsingContext.GetAttachmentReservation().GetSemanticResource(Techniques::AttachmentSemantics::ColorLDR);
			REQUIRE(colorLDR);
			SaveImage(*threadContext, *colorLDR, "background-probe-prepare-" + std::to_string(c));
		}

		testHelper->EndFrameCapture();
	}
}
