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
#include "../../../RenderCore/Techniques/RenderPassUtils.h"
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
#include "../../../Assets/ConfigFileContainer.h"
#include "../../../Assets/Continuation.h"
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
			IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments)
		{
			SceneEngine::MergedLightingEngineCfg lightingEngineCfg;
			lightingDelegate->BindCfg(lightingEngineCfg);

			std::promise<std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique>> promisedTechnique;
			auto techniqueFuture = promisedTechnique.get_future();
			RenderCore::LightingEngine::CreateDeferredLightingTechnique(
				std::move(promisedTechnique),
				lightingApparatus._pipelineAccelerators, lightingApparatus._pipelineCollection, lightingApparatus._sharedDelegates,
				lightingEngineCfg.GetLightOperators(), lightingEngineCfg.GetShadowOperators(), nullptr,
				preregAttachments);

			::Assets::WhenAll(std::move(techniqueFuture)).ThenConstructToPromise(
				std::move(promise),
				[lightingDelegate=std::move(lightingDelegate), drawablesWriter=std::move(drawablesWriter), pipelineAcceleratorPool=lightingApparatus._pipelineAccelerators](auto lightingTechnique) mutable {

					PreparedSceneForShadowProbe result{
						std::move(lightingTechnique),
						std::move(lightingDelegate),
						std::move(drawablesWriter)};

					// Complete preparing shadow probes before we consider the "prepared scene" complete
					auto threadContext = RenderCore::Techniques::GetThreadContext();
					auto& lightScene = RenderCore::LightingEngine::GetLightScene(*result._compiledLightingTechnique);

					auto* shadowProbes = (RenderCore::LightingEngine::ISemiStaticShadowProbeScheduler*)lightScene.QueryInterface(TypeHashCode<RenderCore::LightingEngine::ISemiStaticShadowProbeScheduler>);
					REQUIRE(shadowProbes);

					// give the probe manager an initial view position
					shadowProbes->OnFrameBarrier(Float3(0,0,0), 1000);

					const unsigned maxProbeCount = 16;
					auto ri = shadowProbes->BeginPrepare(*threadContext, maxProbeCount);
					if (ri) {
						PrepareProbes(*threadContext, *ri, *result._drawablesWriter, *pipelineAcceleratorPool);
						
						// We must ensure that the relevant buffer uploads cmdlist has been submitted before
						// we call CommitCommands(). This is a little more awkward given that we're in a background
						// thread, and requires that a thread with the immediate context is pumping BufferUploads::Update()
						assert(!threadContext->IsImmediate());
						auto bufferUploadsCmdList = ri->GetRequiredBufferUploadsCommandList();
						if (bufferUploadsCmdList) {
							auto& bu = RenderCore::Techniques::Services::GetBufferUploads();
							bu.StallAndMarkCommandListDependency(*threadContext, bufferUploadsCmdList);
						}
						threadContext->CommitCommands();
					}

					shadowProbes->EndPrepare(*threadContext);

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

			for (auto l:_lightSourcesId)
				lightScene.SetShadowOperator(l, _shadowOperatorId);
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
			if (auto* scheduler = (RenderCore::LightingEngine::ISemiStaticShadowProbeScheduler*)lightScene.QueryInterface(TypeHashCode<RenderCore::LightingEngine::ISemiStaticShadowProbeScheduler>)) {
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

		REQUIRE(testHelper->_device->GetDeviceFeatures()._cubemapArrays);		// cubemap array feature required for shadow probes

		testHelper->BeginFrameCapture();

		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc,
			TextureDesc::Plain2D(1024, 1024, RenderCore::Format::R8G8B8A8_UNORM_SRGB));
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
			parsingContext.GetFragmentStitchingContext().GetPreregisteredAttachments());

		// awkwardly, we must pump buffer uploads, because the background thread can stall waiting on a buffer uploads complete
		while (futureScene.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
			testApparatus._bufferUploads->OnFrameBarrier(*threadContext);
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}

		auto scene = futureScene.get();

		{
			auto prepareInstance = LightingEngine::BeginPrepareResourcesInstance(*testApparatus._pipelineAccelerators, *scene._compiledLightingTechnique);
			ParseScene(prepareInstance, *scene._drawablesWriter);
			std::promise<Techniques::PreparedResourcesVisibility> preparePromise;
			auto marker = preparePromise.get_future();
			prepareInstance.FulfillWhenNotPending(std::move(preparePromise));
			auto newVisibility = marker.get();		// stall
			if (newVisibility._bufferUploadsVisibility)
				testApparatus._bufferUploads->StallAndMarkCommandListDependency(*threadContext, newVisibility._bufferUploadsVisibility);
		}

		{
			// We must call OnFrameBarrier on the ISemiStaticShadowProbeScheduler at least once to
			// update the status of the probe manager (since it's cmdlist has not been committed)
			auto& lightScene = RenderCore::LightingEngine::GetLightScene(*scene._compiledLightingTechnique);
			auto* shadowProbes = (RenderCore::LightingEngine::ISemiStaticShadowProbeScheduler*)lightScene.QueryInterface(TypeHashCode<RenderCore::LightingEngine::ISemiStaticShadowProbeScheduler>);
			REQUIRE(shadowProbes);
			shadowProbes->OnFrameBarrier(Float3(0,0,0), 1000);
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
				auto drawInstance = LightingEngine::BeginLightingTechniquePlayback(parsingContext, *scene._compiledLightingTechnique);
				ParseScene(drawInstance, *scene._drawablesWriter);
			}

			if (parsingContext._requiredBufferUploadsCommandList)
				Techniques::Services::GetBufferUploads().StallAndMarkCommandListDependency(*threadContext, parsingContext._requiredBufferUploadsCommandList);

			auto colorLDR = parsingContext.GetAttachmentReservation().MapSemanticToResource(Techniques::AttachmentSemantics::ColorLDR);
			REQUIRE(colorLDR);
			SaveImage(*threadContext, *colorLDR, "background-probe-prepare-" + std::to_string(c));
		}

		testHelper->EndFrameCapture();
	}

	static RenderCore::Techniques::PreparedResourcesVisibility PrepareResources(ToolsRig::IDrawablesWriter& drawablesWriter, LightingEngineTestApparatus& testApparatus, RenderCore::LightingEngine::CompiledLightingTechnique& lightingTechnique, RenderCore::IThreadContext& threadContext)
	{
		// stall until all resources are ready
		auto prepareLightingIterator = RenderCore::LightingEngine::BeginPrepareResourcesInstance(*testApparatus._pipelineAccelerators, lightingTechnique);
		ParseScene(prepareLightingIterator, drawablesWriter);
		std::promise<RenderCore::Techniques::PreparedResourcesVisibility> preparePromise;
		auto prepareFuture = preparePromise.get_future();
		prepareLightingIterator.FulfillWhenNotPending(std::move(preparePromise));
		return PrepareAndStall(testApparatus, threadContext, std::move(prepareFuture), RenderCore::BufferUploads::MarkCommandListDependencyFlags::IrregularThreadContext);
	}

	RenderCore::Techniques::ParsingContext BeginThreadSafeParsingContext(
		LightingEngineTestApparatus& testApparatus,
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::TechniqueContext& techniqueContext,
		const RenderCore::ResourceDesc& targetDesc,
		const RenderCore::Techniques::CameraDesc& camera)
	{
		// Each thread needs it's own technique context, since some technique context objects can't be shared
		// between threads
		using namespace RenderCore;
		Techniques::PreregisteredAttachment preregisteredAttachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::ColorLDR,
				targetDesc,
				"color-ldr",
				Techniques::PreregisteredAttachment::State::Uninitialized
			}
		};
		FrameBufferProperties fbProps { targetDesc._textureDesc._width, targetDesc._textureDesc._height };

		RenderCore::Techniques::ParsingContext parsingContext{techniqueContext, threadContext};
		parsingContext.SetPipelineAcceleratorsVisibility(testApparatus._pipelineAccelerators->VisibilityBarrier());
		parsingContext.GetProjectionDesc() = BuildProjectionDesc(camera, UInt2{targetDesc._textureDesc._width, targetDesc._textureDesc._height});
		
		auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
		stitchingContext._workingProps = fbProps;
		for (const auto&a:preregisteredAttachments)
			stitchingContext.DefineAttachment(a._semantic, a._desc, a._name, a._state, a._layout);
		return parsingContext;
	}

	std::shared_ptr<RenderCore::Techniques::TechniqueContext> ForkTechniqueContext(RenderCore::Techniques::TechniqueContext& original)
	{
		using namespace RenderCore;
		auto techniqueContext = std::make_shared<Techniques::TechniqueContext>(original);
		techniqueContext->_uniformDelegateManager = Techniques::CreateUniformDelegateManager();
		techniqueContext->_attachmentPool = Techniques::CreateAttachmentPool(original._pipelineAccelerators->GetDevice());
		techniqueContext->_frameBufferPool = Techniques::CreateFrameBufferPool();
		return techniqueContext;
	}

	static void ThreadedRenderingFunction(LightingEngineTestApparatus& testApparatus)
	{
		// Construct a lighting technique & render to an offscreen texture

		using namespace RenderCore;
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc,
			TextureDesc::Plain2D(256, 256, RenderCore::Format::R8G8B8A8_UNORM_SRGB));
		auto* testHelper = testApparatus._metalTestHelper.get();

		Techniques::CameraDesc sceneCamera;
        sceneCamera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, -1.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, -1.0f}), Float3{0.0f, 200.0f, 0.0f});
        sceneCamera._projection = Techniques::CameraDesc::Projection::Orthogonal;
		sceneCamera._nearClip = 0.f;
		sceneCamera._farClip = 400.f;
		sceneCamera._left = 0.f;
		sceneCamera._right = 100.f;
		sceneCamera._top = 0.f;
		sceneCamera._bottom = -100.f;

		const Float3 negativeLightDirection = Normalize(Float3{0.0f, 1.0f, 0.5f});

		LightingEngine::LightSourceOperatorDesc resolveOperators[] {
			LightingEngine::LightSourceOperatorDesc{}
		};

		auto threadContext = Techniques::GetThreadContext();
		auto techniqueContext = ForkTechniqueContext(*testApparatus._techniqueContext);

		auto parsingContext = BeginThreadSafeParsingContext(testApparatus, *threadContext, *techniqueContext, targetDesc, sceneCamera);
		std::promise<std::shared_ptr<LightingEngine::CompiledLightingTechnique>> promisedLightingTechnique;
		auto lightingTechniqueFuture = promisedLightingTechnique.get_future();
		LightingEngine::CreateDeferredLightingTechnique(
			std::move(promisedLightingTechnique),
			testApparatus._pipelineAccelerators, testApparatus._pipelineCollection, testApparatus._sharedDelegates,
			MakeIteratorRange(resolveOperators), {}, nullptr,
			parsingContext.GetFragmentStitchingContext().GetPreregisteredAttachments());
		auto lightingTechnique = lightingTechniqueFuture.get();

		const Float2 worldMins{0.f, 0.f}, worldMaxs{100.f, 100.f};
		auto drawableWriter = ToolsRig::DrawablesWriterHelper(*testHelper->_device, *testApparatus._drawablesPool, *testApparatus._pipelineAccelerators)
			.CreateShapeWorldDrawableWriter(worldMins, worldMaxs);
		auto newVisibility = PrepareResources(*drawableWriter, testApparatus, *lightingTechnique, *threadContext);
		parsingContext.SetPipelineAcceleratorsVisibility(newVisibility._pipelineAcceleratorsVisibility);
		parsingContext.RequireCommandList(newVisibility._bufferUploadsVisibility);

		auto& lightScene = LightingEngine::GetLightScene(*lightingTechnique);
		auto lightId = lightScene.CreateLightSource(0);
		lightScene.TryGetLightSourceInterface<LightingEngine::IPositionalLightSource>(lightId)->SetLocalToWorld(AsFloat4x4(negativeLightDirection));

		// draw once, and then return
		auto lightingIterator = RenderCore::LightingEngine::BeginLightingTechniquePlayback(
			parsingContext, *lightingTechnique);
		ParseScene(lightingIterator, *drawableWriter);

		threadContext->CommitCommands();
	}

	static void ThreadedRenderingFunction2(LightingEngineTestApparatus& testApparatus, uint64_t seed)
	{
		// Without using a lighting technique, render to an offscreen texture using randomly created sequencer configs

		using namespace RenderCore;
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferDst,
			TextureDesc::Plain2D(256, 256, RenderCore::Format::R8G8B8A8_UNORM_SRGB));
		auto* testHelper = testApparatus._metalTestHelper.get();
		auto& pipelineAccelerators = testApparatus._pipelineAccelerators;

		Techniques::CameraDesc sceneCamera;
        sceneCamera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, -1.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, -1.0f}), Float3{0.0f, 200.0f, 0.0f});
        sceneCamera._projection = Techniques::CameraDesc::Projection::Orthogonal;
		sceneCamera._nearClip = 0.f;
		sceneCamera._farClip = 400.f;
		sceneCamera._left = 0.f;
		sceneCamera._right = 100.f;
		sceneCamera._top = 0.f;
		sceneCamera._bottom = -100.f;

		auto threadContext = Techniques::GetThreadContext();
		auto techniqueContext = ForkTechniqueContext(*testApparatus._techniqueContext);
		auto parsingContext = BeginThreadSafeParsingContext(testApparatus, *threadContext, *techniqueContext, targetDesc, sceneCamera);
		auto outputAttachment = testHelper->_device->CreateResource(targetDesc, "target");
		parsingContext.BindAttachment(Techniques::AttachmentSemantics::ColorLDR, outputAttachment, false);

		{
			auto rpi = Techniques::RenderPassToPresentationTarget(parsingContext, LoadStore::Clear);

			std::promise<std::shared_ptr<Techniques::ITechniqueDelegate>> promisedTechDel;
			auto futureTechDel = promisedTechDel.get_future();
			Techniques::CreateTechniqueDelegate_Utility(std::move(promisedTechDel), ::Assets::MakeAssetPtr<RenderCore::Techniques::TechniqueSetFile>(ILLUM_TECH), Techniques::UtilityDelegateType::FlatColor);
			ParameterBox params;
			params.SetParameter("RANDOMIZED", seed);
			auto cfg = testApparatus._pipelineAccelerators->CreateSequencerConfig(
				"threaded-cfg",
				futureTechDel.get(),		// stall for tech del
				std::move(params),
				rpi.GetFrameBufferDesc());

			{
				const Float2 worldMins{0.f, 0.f}, worldMaxs{100.f, 100.f};
				auto drawableWriter = ToolsRig::DrawablesWriterHelper(*testHelper->_device, *testApparatus._drawablesPool, *testApparatus._pipelineAccelerators)
					.CreateShapeWorldDrawableWriter(worldMins, worldMaxs);
				Techniques::DrawablesPacket pkt;
				drawableWriter->WriteDrawables(pkt);
				auto newVisibility = PrepareAndStall(testApparatus, *threadContext, *cfg.get(), pkt, BufferUploads::MarkCommandListDependencyFlags::IrregularThreadContext);
				parsingContext.SetPipelineAcceleratorsVisibility(newVisibility._pipelineAcceleratorsVisibility);
				parsingContext.RequireCommandList(newVisibility._bufferUploadsVisibility);
				Techniques::Draw(
					parsingContext, 
					*testApparatus._pipelineAccelerators,
					*cfg,
					pkt);
			}
		}

		threadContext->CommitCommands();
	}

	TEST_CASE( "LightingEngine-MultithreadRenderingTrash", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;

		// We're going to spawn a number of synchronous threads, each running ThreadedRenderingFunction
		// the threads will share certain resources, such as the pipeline accelerator pool, etc
		// any threading issues related to those shared resources should then be encouraged to occur
		
		const unsigned invocationCount = 1000;
		const unsigned synchronousCount = 12;
		std::mt19937_64 rng(629846298462);
		std::vector<std::thread> spawnedThreads;
		spawnedThreads.reserve(invocationCount);
		unsigned spawnedInvocations = 0;
		while (spawnedInvocations < invocationCount) {
			while (spawnedThreads.size() >= synchronousCount) {
				spawnedThreads.front().join();
				spawnedThreads.erase(spawnedThreads.begin());
			}
			spawnedThreads.emplace_back(
				[&testApparatus, seed=rng()]() {
					if (seed & 1) {
						ThreadedRenderingFunction(testApparatus);
					} else {
						ThreadedRenderingFunction2(testApparatus, seed);
					}
				});
			spawnedInvocations++;
			std::this_thread::sleep_for(std::chrono::milliseconds(2));

			// CommitCommands on the immediate context every now and again to ensure that destruction
			// queues will be pumped
			testApparatus._metalTestHelper->_device->GetImmediateContext()->CommitCommands();
		}

		for (auto& t:spawnedThreads)
			t.join();
	}
}
