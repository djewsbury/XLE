// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingParser.h"
#include "LightingParserContext.h"
#include "SceneParser.h"
// #include "SceneEngineUtils.h"
#include "RenderStepUtils.h"
#include "RenderStep.h"
#include "RenderStep_PrepareShadows.h"
// #include "RenderStep_ResolveHDR.h"
#include "LightInternal.h"
#include "PreparedScene.h"

#include "../RenderCore/Techniques/PipelineAccelerator.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/Services.h"
#include "../RenderCore/IThreadContext.h"
#include "../RenderCore/IAnnotator.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../Assets/Assets.h"
#include "../Assets/AsyncMarkerGroup.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../ConsoleRig/Console.h"

#include <set>

#if 0
// For some of the debugging / metrics stuff towards the end
#include "MetricsBox.h"
#include "RefractionsBuffer.h"
#include "Ocean.h"
#include "MetalStubs.h"
#include "../RenderCore/Techniques/DeferredShaderResource.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderOverlays/Font.h"
#include "../xleres/FileList.h"
#endif


namespace SceneEngine
{
    using namespace RenderCore;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
	std::vector<std::shared_ptr<IRenderStep>> CreateStandardRenderSteps(LightingModel lightingModel)
	{
		std::vector<std::shared_ptr<IRenderStep>> result;

		const bool enableParametersBuffer = Tweakable("EnableParametersBuffer", true);
		const bool precisionTargets = Tweakable("PrecisionTargets", false);
		unsigned gbufferType = enableParametersBuffer?1:2;

		std::shared_ptr<IRenderStep> mainSceneRenderStep;
		if (lightingModel == LightingModel::Deferred) {
			mainSceneRenderStep = CreateRenderStep_GBuffer(gbufferType, precisionTargets);
		} else if (lightingModel == LightingModel::Forward) {
			mainSceneRenderStep = CreateRenderStep_Forward(precisionTargets);
		} else {
			mainSceneRenderStep = CreateRenderStep_Direct();
		}

		result.push_back(mainSceneRenderStep);

		// In direct mode, we rendered directly to the presentation target, so we cannot resolve lighting or tonemap
		if (lightingModel != LightingModel::Direct) {
			if (lightingModel == LightingModel::Deferred) {
				result.push_back(CreateRenderStep_LightingResolve(gbufferType, precisionTargets));
				result.push_back(CreateRenderStep_PostDeferredOpaque(precisionTargets));
			}
			auto resolveHDR = std::make_shared<RenderStep_ResolveHDR>();
			result.push_back(std::make_shared<RenderStep_SampleLuminance>(resolveHDR));
			result.push_back(resolveHDR);
		}

		return result;
	}
#endif

	class CompiledSceneTechnique
	{
	public:
		std::vector<std::shared_ptr<IViewDelegate>> _viewDelegates;
		std::vector<std::shared_ptr<ILightingParserPlugin>> _lightingPlugins;
		std::vector<std::shared_ptr<IRenderStep>> _renderSteps;

		class RenderPass
		{
		public:
			RenderCore::PipelineType _pipelineType;
			RenderCore::FrameBufferDesc _fbDesc;
			size_t _beginRenderStep = 0;
			size_t _endRenderStep = 0;

			std::vector<std::shared_ptr<RenderCore::Techniques::SequencerConfig>> _perSubpassSequencerConfigs;
			std::vector<std::pair<uint64_t, AttachmentName>> _outputAttachments;
		};
		std::vector<RenderPass> _renderPasses;

		RenderCore::TextureSamples _sampling;
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;

		std::vector<std::pair<uint64_t, std::shared_ptr<ICompiledShadowGenerator>>> _shadowGenerators;
		std::shared_ptr<RenderCore::Techniques::FrameBufferPool> _shadowGenFrameBufferPool;
		std::shared_ptr<RenderCore::Techniques::AttachmentPool> _shadowGenAttachmentPool;

		CompiledSceneTechnique(
			const SceneTechniqueDesc& techniqueDesc,
			const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
			const RenderCore::AttachmentDesc& targetAttachmentDesc);
		~CompiledSceneTechnique();
	};

	CompiledSceneTechnique::CompiledSceneTechnique(
		const SceneTechniqueDesc& techniqueDesc,
		const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const RenderCore::AttachmentDesc& targetAttachmentDesc)
	: _pipelineAccelerators(pipelineAccelerators)
	{
		_renderSteps.insert(_renderSteps.end(), techniqueDesc._renderSteps.begin(), techniqueDesc._renderSteps.end());
		_lightingPlugins.insert(_lightingPlugins.begin(), techniqueDesc._lightingPlugins.begin(), techniqueDesc._lightingPlugins.end());
		
		//
		//		Figure out the frame buffers and render passes required
		//		Iterate through each render step and check it's input/output interface
		//		Where we can merge multiple render steps into a single renderpass, do so
		//			-- however, render passes can use different pipeline types, so (for example
		//			a compute pass can break up a graphics pipeline pass
		//

		std::vector<Techniques::PreregisteredAttachment> workingAttachments = {
			Techniques::PreregisteredAttachment { 
				Techniques::AttachmentSemantics::ColorLDR,
				targetAttachmentDesc,
				Techniques::PreregisteredAttachment::State::Uninitialized
			}
		};

		auto renderStepIterator = _renderSteps.begin();
		while (renderStepIterator != _renderSteps.end()) {
			std::vector<Techniques::FrameBufferDescFragment> fragments;
			std::vector<RenderStepFragmentInterface::SubpassExtension> subpassExtensions;

			auto renderStepEnd = renderStepIterator;
			auto& firstInterface = (*renderStepEnd)->GetInterface();
			auto pipelineType = firstInterface.GetFrameBufferDescFragment()._pipelineType;
			fragments.push_back(firstInterface.GetFrameBufferDescFragment());
			subpassExtensions.insert(subpassExtensions.end(), firstInterface.GetSubpassAddendums().begin(), firstInterface.GetSubpassAddendums().end());
			assert(firstInterface.GetSubpassAddendums().size() == firstInterface.GetFrameBufferDescFragment()._subpasses.size());
			++renderStepEnd;
			for (; renderStepEnd != _renderSteps.end(); ++renderStepEnd) {
				auto& interf = (*renderStepEnd)->GetInterface();
				if (interf.GetFrameBufferDescFragment()._pipelineType != pipelineType) break;
				fragments.push_back(interf.GetFrameBufferDescFragment());
				subpassExtensions.insert(subpassExtensions.end(), interf.GetSubpassAddendums().begin(), interf.GetSubpassAddendums().end());
				assert(interf.GetSubpassAddendums().size() == interf.GetFrameBufferDescFragment()._subpasses.size());
			}
		
			UInt2 dimensionsForCompatibilityTests { (unsigned)targetAttachmentDesc._width, (unsigned)targetAttachmentDesc._height };
			auto merged = Techniques::MergeFragments(
				MakeIteratorRange(workingAttachments),
				MakeIteratorRange(fragments),
				dimensionsForCompatibilityTests);
			auto fbDesc = Techniques::BuildFrameBufferDesc(
				std::move(merged._mergedFragment), 
				FrameBufferProperties{ (unsigned)targetAttachmentDesc._width, (unsigned)targetAttachmentDesc._height });

			workingAttachments.reserve(merged._outputAttachments.size());
			for (const auto&o:merged._outputAttachments) {
				auto i = std::find_if(
					workingAttachments.begin(), workingAttachments.end(),
					[&o](const Techniques::PreregisteredAttachment& p) { return p._semantic == o.first; });
				if (i != workingAttachments.end()) {
					assert(merged._mergedFragment._attachments[o.second]._outputSemanticBinding == o.first);
					i->_desc = merged._mergedFragment._attachments[o.second]._desc;
					i->_state = i->_stencilState = Techniques::PreregisteredAttachment::State::Initialized;
				} else {
					assert(merged._mergedFragment._attachments[o.second]._outputSemanticBinding == o.first);
					workingAttachments.push_back(
						Techniques::PreregisteredAttachment { 
							o.first,
							merged._mergedFragment._attachments[o.second]._desc,
							Techniques::PreregisteredAttachment::State::Initialized,
							Techniques::PreregisteredAttachment::State::Initialized
						});
				}
			}

			RenderPass newRenderPass;
			newRenderPass._fbDesc = std::move(fbDesc);
			newRenderPass._pipelineType = pipelineType;
			newRenderPass._beginRenderStep = std::distance(_renderSteps.begin(), renderStepIterator);
			newRenderPass._endRenderStep = std::distance(_renderSteps.begin(), renderStepEnd);
			newRenderPass._outputAttachments = std::move(merged._outputAttachments);

			// Fill in the SequencerConfig for each render step
			auto subpassIterator = subpassExtensions.begin();
			for (auto step=renderStepIterator; step!=renderStepEnd; ++step) {
				for (unsigned subpass=0; subpass<unsigned(fragments[step-renderStepIterator]._subpasses.size()); ++subpass, ++subpassIterator) {
					if (subpassIterator->_techniqueDelegate) {
						auto sequencerConfig = pipelineAccelerators->CreateSequencerConfig(
							subpassIterator->_techniqueDelegate,
							subpassIterator->_sequencerSelectors,
							newRenderPass._fbDesc,
							(unsigned)(subpassIterator - subpassExtensions.begin()));
						newRenderPass._perSubpassSequencerConfigs.push_back(sequencerConfig);
					} else {
						newRenderPass._perSubpassSequencerConfigs.push_back({});
					}
				}
			}

			_renderPasses.emplace_back(std::move(newRenderPass));
			renderStepIterator = renderStepEnd;
		}

		// 
		//		Shadow Generators
		//

		_shadowGenAttachmentPool = std::make_shared<RenderCore::Techniques::AttachmentPool>(pipelineAccelerators->GetDevice());
		_shadowGenFrameBufferPool = std::make_shared<RenderCore::Techniques::FrameBufferPool>();
		if (!techniqueDesc._shadowGenerators.empty()) {
			for (const auto&shdGen:techniqueDesc._shadowGenerators) {
				auto hash = RenderCore::LightingEngine::Hash64(shdGen);
				_shadowGenerators.emplace_back(hash, CreateCompiledShadowGenerator(shdGen, _pipelineAccelerators));
			}
			std::sort(
				_shadowGenerators.begin(), _shadowGenerators.end(), 
				CompareFirst<uint64_t, std::shared_ptr<ICompiledShadowGenerator>>());
		}

		// 
		//		Other configuration & state
		//

		for (const auto&step:_renderSteps) {
			auto viewDelegate = step->CreateViewDelegate();
			if (viewDelegate)
				_viewDelegates.push_back(viewDelegate);
		}

		// _gbufferType = enableParametersBuffer?1:2;
		// _precisionTargets = precisionTargets;
		_sampling = techniqueDesc._sampling;
	}

	CompiledSceneTechnique::~CompiledSceneTechnique()
	{
	}

	std::shared_ptr<CompiledSceneTechnique> CreateCompiledSceneTechnique(
		const SceneTechniqueDesc& techniqueDesc,
		const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const RenderCore::AttachmentDesc& targetAttachmentDesc)
	{
		return std::make_shared<CompiledSceneTechnique>(techniqueDesc, pipelineAccelerators, targetAttachmentDesc);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    void LightingParser_InitBasicLightEnv(
        RenderCore::IThreadContext& context,
        Techniques::ParsingContext& parsingContext,
		LightingParserContext& lightingParserContext);

    static LightingParserContext LightingParser_SetupContext(
        RenderCore::IThreadContext& context, 
		RenderCore::Techniques::ParsingContext& parsingContext,
        const ILightingStateDelegate& delegate,
		const CompiledSceneTechnique& technique,
        unsigned samplingPassIndex = 0, unsigned samplingPassCount = 1);

	LightingParserContext LightingParser_ExecuteScene(
        RenderCore::IThreadContext& threadContext, 
		const RenderCore::IResourcePtr& renderTarget,
        Techniques::ParsingContext& parsingContext,
		const CompiledSceneTechnique& technique,
		const ILightingStateDelegate& lightingDelegate,
        IScene& scene,
        const RenderCore::Techniques::CameraDesc& camera)
    {
		auto targetTextureDesc = renderTarget->GetDesc()._textureDesc;

		// First, setup the views so we can do the "ExecuteScene" step
		auto mainSceneProjection = RenderCore::Techniques::BuildProjectionDesc(camera, UInt2{targetTextureDesc._width, targetTextureDesc._height});

		SceneExecuteContext executeContext;
		for (auto&step:technique._viewDelegates) {
			step->Reset();
			executeContext.AddView(SceneView{SceneView::Type::Normal, mainSceneProjection}, step);
		}

		std::vector<std::shared_ptr<ViewDelegate_Shadow>> shadowViewDelegates;
		shadowViewDelegates.reserve(lightingDelegate.GetShadowProjectionCount());

		for (unsigned s=0; s<lightingDelegate.GetShadowProjectionCount(); ++s) {
			auto proj = lightingDelegate.GetShadowProjectionDesc(s, mainSceneProjection);
			// todo -- what's the correct projection to give to this view?
			auto viewDelegate = std::make_shared<ViewDelegate_Shadow>(proj);
			executeContext.AddView(
				SceneView{SceneView::Type::Shadow, /*proj*/mainSceneProjection},
				viewDelegate);
			shadowViewDelegates.push_back(std::move(viewDelegate));
		}

		// No, go ahead and execute the scene, which should generate a lot of Drawables (and potentially other scene preparation elements)
        CATCH_ASSETS_BEGIN
			scene.ExecuteScene(threadContext, executeContext);
		CATCH_ASSETS_END(parsingContext)

        // Throw in a "frame priority barrier" here, right after the prepare scene. This will
        // force all uploads started during PrepareScene to be completed when we next call
        // bufferUploads.Update(). Normally we want a gap between FramePriority_Barrier() and
        // Update(), because this gives some time for the background thread to run.
        RenderCore::Techniques::Services::GetBufferUploads().FramePriority_Barrier();

		auto lightingParserContext = LightingParser_SetupContext(threadContext, parsingContext, lightingDelegate, technique);
        LightingParser_SetGlobalTransform(threadContext, parsingContext, mainSceneProjection);

		PreparedScene preparedScene;
		lightingParserContext._preparedScene = &preparedScene;

		/*auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		ReturnToSteadyState(metalContext);
		CATCH_ASSETS_BEGIN
			SetFrameGlobalStates(metalContext);
		CATCH_ASSETS_END(parsingContext)*/

		// auto* prevPipelineAccelerator = parsingContext._pipelineAcceleratorPool;
		// parsingContext._pipelineAcceleratorPool = technique._pipelineAccelerators.get();

		// Preparation steps (including shadows prepare)
        {
            GPUAnnotation anno(threadContext, "Prepare");
            for (auto i=lightingParserContext._plugins.cbegin(); i!=lightingParserContext._plugins.cend(); ++i) {
                CATCH_ASSETS_BEGIN
                    (*i)->OnPreScenePrepare(threadContext, parsingContext, lightingParserContext);
                CATCH_ASSETS_END(parsingContext)
            }

			for (unsigned s=0; s<lightingDelegate.GetShadowProjectionCount(); ++s) {
				auto proj = lightingDelegate.GetShadowProjectionDesc(s, mainSceneProjection);
				CATCH_ASSETS_BEGIN
					auto& shadowDelegate = *shadowViewDelegates[s];
					if (shadowDelegate._general._drawables.empty())
						continue;

					assert(proj._projections.Count() == proj._shadowGeneratorDesc._arrayCount);
					assert(proj._projections._mode == proj._shadowGeneratorDesc._projectionMode);
					assert(proj._projections._useNearProj == proj._shadowGeneratorDesc._enableNearCascade);

					auto generatorHash = RenderCore::LightingEngine::Hash64(proj._shadowGeneratorDesc);
					auto existing = LowerBound(technique._shadowGenerators, generatorHash);
					if (existing != technique._shadowGenerators.end() && existing->first == generatorHash) {
						assert(0);
						// existing->second->ExecutePrepare(threadContext, parsingContext, lightingParserContext, shadowDelegate, *technique._shadowGenFrameBufferPool, *technique._shadowGenAttachmentPool);
					} else {
						Log(Warning) << "Building temporary CompiledShadowGenerator because one was not configured before hand" << std::endl;
						auto compiledShadowGenerator = CreateCompiledShadowGenerator(
							proj._shadowGeneratorDesc,
							technique._pipelineAccelerators);
						assert(0);
						// compiledShadowGenerator->ExecutePrepare(threadContext, parsingContext, lightingParserContext, shadowDelegate, *technique._shadowGenFrameBufferPool, *technique._shadowGenAttachmentPool);
					}
					
				CATCH_ASSETS_END(parsingContext)
			}
        }

        RenderCore::Techniques::Services::GetBufferUploads().Update(threadContext);

		std::vector<std::pair<uint64_t, RenderCore::IResourcePtr>> workingAttachments = {
			{ Techniques::AttachmentSemantics::ColorLDR, renderTarget }
		};
		parsingContext.GetTechniqueContext()._attachmentPool->Bind(
			Techniques::AttachmentSemantics::ColorLDR, renderTarget);

		/*parsingContext.GetTechniqueContext()._attachmentPool->Bind(
			RenderCore::FrameBufferProperties {
				targetTextureDesc._width, targetTextureDesc._height, 
				technique._sampling });*/

		technique._pipelineAccelerators->RebuildAllOutOfDatePipelines();		// (check for pipelines that need hot reloading)

		for (const auto&rp:technique._renderPasses) {
			Techniques::RenderPassInstance rpi;
			if (rp._pipelineType == PipelineType::Graphics) {
				rpi = Techniques::RenderPassInstance { threadContext, rp._fbDesc, *parsingContext.GetTechniqueContext()._frameBufferPool, *parsingContext.GetTechniqueContext()._attachmentPool };
			} else {
				// construct a "non-metal" render pass instance. This just handles attachment remapping logic, but doesn't create an renderpass
				// in the underlying graphics API
				rpi = Techniques::RenderPassInstance { rp._fbDesc, *parsingContext.GetTechniqueContext()._attachmentPool };
			}

			RenderStepFragmentInstance rpf(rpi, MakeIteratorRange(rp._perSubpassSequencerConfigs));
			for (size_t step=rp._beginRenderStep; step!=rp._endRenderStep; ++step) {
				CATCH_ASSETS_BEGIN
					IViewDelegate* viewDelegate = executeContext.GetViewDelegates()[0].get();
					technique._renderSteps[step]->Execute(threadContext, parsingContext, *technique._pipelineAccelerators, lightingParserContext, rpf, viewDelegate);
				CATCH_ASSETS_END(parsingContext)
				rpi.NextSubpass();
			}

			// Bind the output attachments from the render pass
			// Note -- we never unbind any "exhausted" attachments. All attachments
			//		that are written to end up here.
			for (const auto&w:rp._outputAttachments)
				parsingContext.GetTechniqueContext()._attachmentPool->Bind(w.first, rpi.GetResourceForAttachmentName(w.second));
		}

		// parsingContext._pipelineAcceleratorPool = prevPipelineAccelerator;

		// Bind depth to NamedResources(), so we can find it later with RenderPassToPresentationTargetWithDepthStencil()
		/*if (merged._mergedFragment._attachments[c].GetOutputSemanticBinding() == Techniques::AttachmentSemantics::MultisampleDepth)
			parsingContext.GetTechniqueContext()._attachmentPool->Bind(
				RenderCore::Techniques::AttachmentSemantics::MultisampleDepth,
				rpi.GetResource(c));*/

		return lightingParserContext;
    }

	std::shared_ptr<::Assets::IAsyncMarker> PreparePipelines(
		RenderCore::IThreadContext& threadContext,
		const CompiledSceneTechnique& technique,
		const ILightingStateDelegate& lightingDelegate,
		IScene& scene)
	{
		SceneExecuteContext executeContext;
		for (auto&step:technique._viewDelegates) {
			step->Reset();
			executeContext.AddView(SceneView{SceneView::Type::Other}, step);
		}

		for (unsigned s=0; s<lightingDelegate.GetShadowProjectionCount(); ++s) {
			executeContext.AddView(
				SceneView{SceneView::Type::Other},
				std::make_shared<ViewDelegate_Shadow>(StandardShadowProjection{}));
		}

		scene.ExecuteScene(threadContext, executeContext);

		auto pipelineAcceleratorPool = technique._pipelineAccelerators.get();
		pipelineAcceleratorPool->RebuildAllOutOfDatePipelines();

		std::set<::Assets::PtrToFuturePtr<Techniques::IPipelineAcceleratorPool::Pipeline>> pendingPipelines;

		for (const auto&rp:technique._renderPasses) {
			
			auto sequencerConfigRange = MakeIteratorRange(rp._perSubpassSequencerConfigs);

			for (size_t step=rp._beginRenderStep; step!=rp._endRenderStep; ++step) {
				IViewDelegate* viewDelegate = nullptr;
				if (step==0) viewDelegate = executeContext.GetViewDelegates()[0].get();

				// We need to know the number of subpasses in this render step in order to check what sequencer config apply to it
				// We don't save this value, so we have to query the render step again
				auto subpassCount = technique._renderSteps[step]->GetInterface().GetFrameBufferDescFragment()._subpasses.size();
				if (!subpassCount)
					continue;

				assert(subpassCount <= sequencerConfigRange.size());
				auto stepSequencerConfigs = MakeIteratorRange(sequencerConfigRange.begin(),  sequencerConfigRange.begin() + subpassCount);

				for (unsigned b=0; b<(unsigned)RenderCore::Techniques::BatchFilter::Max; ++b) {
					auto *drawablesPacket = viewDelegate ? viewDelegate->GetDrawablesPacket(RenderCore::Techniques::BatchFilter(b)) : nullptr;
					if (!drawablesPacket) continue;

					for (auto d=drawablesPacket->_drawables.begin(); d!=drawablesPacket->_drawables.end(); ++d) {
						const auto& drawable = *(RenderCore::Techniques::Drawable*)d.get();
						for (auto r:stepSequencerConfigs) {
							auto pipelineAccelerator = pipelineAcceleratorPool->GetPipeline(*drawable._pipeline, *r);
							if (pipelineAccelerator->GetAssetState() == ::Assets::AssetState::Pending)
								pendingPipelines.insert(pipelineAccelerator);
						}
					}
				}

				sequencerConfigRange.first = stepSequencerConfigs.end();
			}
		}

		if (pendingPipelines.empty())
			return nullptr;

		std::string markerName{"SceneEngine PreparePipelines"};
		auto result = std::make_shared<::Assets::AsyncMarkerGroup>();
		for (const auto&pp:pendingPipelines)
			result->Add(pp, markerName);
		
		return result;
	}

    void LightingParserContext::SetMetricsBox(MetricsBox* box)
    {
        _metricsBox = box;
    }

    void LightingParserContext::Reset()
    {
		assert(0);
        // _preparedDMShadows.clear();
        // _preparedRTShadows.clear();
    }

	LightingParserContext::LightingParserContext() {}
	LightingParserContext::~LightingParserContext() {}

	// note -- explicitly implemented because of use of PreparedDMShadowFrustum, PreparedRTShadowFrustum in a vector
	LightingParserContext::LightingParserContext(LightingParserContext&& moveFrom)
	: /*_preparedDMShadows(std::move(moveFrom._preparedDMShadows))
	, _preparedRTShadows(std::move(moveFrom._preparedRTShadows))
	, */_plugins(std::move(moveFrom._plugins))
	, _metricsBox(moveFrom._metricsBox)
	, _mainTargets(std::move(moveFrom._mainTargets))
	, _preparedScene(moveFrom._preparedScene)
	, _sampleCount(moveFrom._sampleCount)
	, _delegate(moveFrom._delegate)
	{
		moveFrom._preparedScene = nullptr;
		moveFrom._sampleCount = 0;
		moveFrom._metricsBox = nullptr;
		moveFrom._delegate = nullptr;
	}

	LightingParserContext& LightingParserContext::operator=(LightingParserContext&& moveFrom)
	{
		// _preparedDMShadows = std::move(moveFrom._preparedDMShadows);
		// _preparedRTShadows = std::move(moveFrom._preparedRTShadows);
		_plugins = std::move(moveFrom._plugins);
		_mainTargets = std::move(moveFrom._mainTargets);
		_preparedScene = moveFrom._preparedScene;
		_sampleCount = moveFrom._sampleCount;
		_metricsBox = moveFrom._metricsBox;
		_delegate = moveFrom._delegate;
		moveFrom._preparedScene = nullptr;
		moveFrom._sampleCount = 0;
		moveFrom._metricsBox = nullptr;
		moveFrom._delegate = nullptr;
		return *this;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static LightingParserContext LightingParser_SetupContext(
        RenderCore::IThreadContext& context, 
		RenderCore::Techniques::ParsingContext& parsingContext,
        const ILightingStateDelegate& delegate,
		const CompiledSceneTechnique& technique,
        unsigned samplingPassIndex, unsigned samplingPassCount)
    {
#if 0
        struct GlobalCBuffer
        {
            float _time; unsigned _samplingPassIndex; 
            unsigned _samplingPassCount; unsigned _dummy;
        } time { delegate.GetTimeValue(), samplingPassIndex, samplingPassCount, 0 };
		auto& metalContext = *Metal::DeviceContext::Get(context);
        parsingContext.SetGlobalCB(
            metalContext, Techniques::TechniqueContext::CB_GlobalState,
            &time, sizeof(time));

		LightingParserContext lightingParserContext;
		lightingParserContext._delegate = &delegate;
		lightingParserContext._plugins.insert(
			lightingParserContext._plugins.end(),
			technique._lightingPlugins.begin(), technique._lightingPlugins.end());
        LightingParser_InitBasicLightEnv(context, parsingContext, lightingParserContext);

        auto& metricsBox = ConsoleRig::FindCachedBox2<MetricsBox>();
        metalContext.ClearUInt(metricsBox._metricsBufferUAV, { 0,0,0,0 });
        lightingParserContext.SetMetricsBox(&metricsBox);

		// lightingParserContext._gbufferType = technique._gbufferType;

        return lightingParserContext;
#else
		return {};
#endif
    }

	void LightingParser_Overlays(   IThreadContext& context,
									Techniques::ParsingContext& parsingContext,
                                    LightingParserContext& lightingParserContext)
    {
#if 0
        GPUAnnotation anno(context, "Overlays");

		auto metalContext = Metal::DeviceContext::Get(context);
        ViewportDesc mainViewportDesc = metalContext->GetBoundViewport();
        auto& refractionBox = ConsoleRig::FindCachedBox2<RefractionsBuffer>(unsigned(mainViewportDesc.Width/2), unsigned(mainViewportDesc.Height/2));
        refractionBox.Build(*metalContext, parsingContext, 4.f);
        MetalStubs::GetGlobalNumericUniforms(*metalContext, ShaderStage::Pixel).Bind(MakeResourceList(12, refractionBox.GetSRV()));

		auto defaultFont0 = RenderOverlays::GetX2Font("Raleway", 16);
		SceneEngine::DrawPendingResources(context, parsingContext, defaultFont0);

        for (auto i=parsingContext._pendingOverlays.cbegin(); i!=parsingContext._pendingOverlays.cend(); ++i) {
            CATCH_ASSETS_BEGIN
                (*i)(*metalContext, parsingContext);
            CATCH_ASSETS_END(parsingContext)
        }
                    
        if (Tweakable("FFTDebugging", false)) {
            FFT_DoDebugging(metalContext.get());
        }

        if (Tweakable("MetricsRender", false) && lightingParserContext.GetMetricsBox()) {
            CATCH_ASSETS_BEGIN

                using namespace RenderCore;
                using namespace RenderCore::Metal;
                auto& metricsShader = ::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(
                        METRICS_RENDER_VERTEX_HLSL ":main:vs_*", 
                        METRICS_RENDER_GEO_HLSL ":main:gs_*",
                        METRICS_RENDER_PIXEL_HLSL ":main:ps_*",
                        "");
                metalContext->Bind(metricsShader);
                metalContext->GetNumericUniforms(ShaderStage::Pixel).Bind(MakeResourceList(
                    3, ::Assets::MakeAsset<RenderCore::Techniques::DeferredShaderResource>("xleres/DefaultResources/metricsdigits.dds:T")->Actualize()->GetShaderResource()));
                metalContext->Bind(BlendState(BlendOp::Add, Blend::One, Blend::InvSrcAlpha));
                metalContext->Bind(DepthStencilState(false));
                metalContext->GetNumericUniforms(ShaderStage::Vertex).Bind(MakeResourceList(lightingParserContext.GetMetricsBox()->_metricsBufferSRV));
                unsigned dimensions[4] = { unsigned(mainViewportDesc.Width), unsigned(mainViewportDesc.Height), 0, 0 };
                metalContext->GetNumericUniforms(ShaderStage::Vertex).Bind(MakeResourceList(MakeMetalCB(dimensions, sizeof(dimensions))));
                metalContext->GetNumericUniforms(ShaderStage::Geometry).Bind(MakeResourceList(MakeMetalCB(dimensions, sizeof(dimensions))));
                SetupVertexGeneratorShader(*metalContext);
                metalContext->Bind(Topology::PointList);
                metalContext->Draw(9);

                MetalStubs::UnbindPS<ShaderResourceView>(*metalContext, 3, 1);
                MetalStubs::UnbindVS<ShaderResourceView>(*metalContext, 0, 1);

            CATCH_ASSETS_END(parsingContext)
        }
#endif
    }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	auto MainTargets::GetSRV(Techniques::ParsingContext& context, uint64_t semantic, const RenderCore::TextureViewDesc& window) const -> RenderCore::IResourceView*
	{
		auto& namedResources = *context.GetTechniqueContext()._attachmentPool;
		RenderCore::FrameBufferDesc::Attachment requestAttachments[1];
		requestAttachments[0]._semantic = semantic;
		SubpassDesc fakeSubPass;
		fakeSubPass.AppendInput(0);
		FrameBufferDesc fbDesc(std::vector<FrameBufferDesc::Attachment>{requestAttachments[0]}, std::vector<SubpassDesc>{fakeSubPass});
		auto result = namedResources.Request(fbDesc);
		if (!result.empty())
			return namedResources.GetSRV(result[0]);
		return nullptr;
	}

	RenderCore::IResourcePtr MainTargets::GetResource(Techniques::ParsingContext& context, uint64_t semantic) const
	{
		return context.GetTechniqueContext()._attachmentPool->GetBoundResource(semantic);
	}

	UInt2		MainTargets::GetDimensions(Techniques::ParsingContext& context) const
	{
		assert(0);
		return UInt2(0,0);
		// auto& fbProps = context.GetTechniqueContext()._attachmentPool->GetFrameBufferProperties();
		// return { fbProps._outputWidth, fbProps._outputHeight };
	}

	unsigned    MainTargets::GetSamplingCount(Techniques::ParsingContext& context) const
	{
		assert(0);
		return 0;
		// auto& fbProps = context.GetTechniqueContext()._attachmentPool->GetFrameBufferProperties();
		// return fbProps._samples._sampleCount;
	}


	class SceneExecuteContext::Pimpl
	{
	public:
		std::vector<std::shared_ptr<IViewDelegate>> _viewDelegates;
		PreparedScene _preparedScene;
	};

	IteratorRange<const std::shared_ptr<IViewDelegate>*> SceneExecuteContext::GetViewDelegates()
	{
		return MakeIteratorRange(_pimpl->_viewDelegates);
	}

	RenderCore::Techniques::DrawablesPacket* SceneExecuteContext::GetDrawablesPacket(unsigned viewIndex, RenderCore::Techniques::BatchFilter batch)
	{
		return _pimpl->_viewDelegates[viewIndex]->GetDrawablesPacket(batch);
	}

	PreparedScene& SceneExecuteContext::GetPreparedScene()
	{
		return _pimpl->_preparedScene;
	}

	void SceneExecuteContext::AddView(
		const SceneView& view,
		const std::shared_ptr<IViewDelegate>& delegate)
	{
		_views.push_back(view);
		_pimpl->_viewDelegates.push_back(delegate);
		assert(_views.size() == _pimpl->_viewDelegates.size());
	}

	SceneExecuteContext::SceneExecuteContext()
	{
		_pimpl = std::make_unique<Pimpl>();
	}
	
	SceneExecuteContext::~SceneExecuteContext()
	{
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void ExecuteSceneRaw(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::ParsingContext& parserContext,
		const RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		const RenderCore::Techniques::SequencerContext& sequencerTechnique,
		const SceneView& view,
		IScene& scene)
	{
		using namespace RenderCore;
		SceneExecuteContext sceneExeContext;
		auto viewDelegate = std::make_shared<BasicViewDelegate>();
		sceneExeContext.AddView(view, viewDelegate);
		scene.ExecuteScene(threadContext, sceneExeContext);

		Techniques::Draw(
			threadContext, parserContext, 
			pipelineAccelerators,
			sequencerTechnique, 
			viewDelegate->_pkt);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	IViewDelegate::~IViewDelegate() {}
	std::shared_ptr<IViewDelegate> IRenderStep::CreateViewDelegate() { return nullptr; }
	IRenderStep::~IRenderStep() {}


	void ILightingParserPlugin::OnPreScenePrepare(
		RenderCore::IThreadContext&, RenderCore::Techniques::ParsingContext&, LightingParserContext&) const {}
    void ILightingParserPlugin::OnLightingResolvePrepare(
        RenderCore::IThreadContext&, RenderCore::Techniques::ParsingContext&, LightingParserContext&, 
		LightingResolveContext&) const {}
    void ILightingParserPlugin::OnPostSceneRender(
        RenderCore::IThreadContext&, RenderCore::Techniques::ParsingContext&, LightingParserContext&, 
		RenderCore::Techniques::BatchFilter filter, unsigned techniqueIndex) const {}
    void ILightingParserPlugin::InitBasicLightEnvironment(
        RenderCore::IThreadContext&, RenderCore::Techniques::ParsingContext&, LightingParserContext&, 
		ShaderLightDesc::BasicEnvironment& env) const {}
	ILightingParserPlugin::~ILightingParserPlugin() {}

}

            ////////////////////////////////////////////////////////////////////
                //      1. Resolve lighting
                //          -> outputs  1. postLightingResolveTexture (HDR colour)
                //                      2. depth buffer (NDC depths)
                //                      3. secondary depth buffer (contains per 
                //                          pixel/per sample stencil)
                //
            ////////............................................................
                //      2. Resolve MSAA (if needed)
                //          -> outputs  1. single sample HDR colour
                //                      2. single sample depth buffer
                //
            ////////............................................................
                //      3. Post processing operations
                //
            ////////............................................................
                //      4. Resolve HDR (tone mapping, bloom, etc)
                //          -> outputs  1. LDR SRGB colour
                //
            ////////............................................................
                //      5. Debugging / overlays
                //
            ////////////////////////////////////////////////////////////////////
