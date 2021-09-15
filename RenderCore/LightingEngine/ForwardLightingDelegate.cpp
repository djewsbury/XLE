// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ForwardLightingDelegate.h"
#include "ForwardPlusLightScene.h"
#include "RenderStepFragments.h"
#include "SkyOperator.h"
#include "ShadowPreparer.h"
#include "HierarchicalDepths.h"
#include "ScreenSpaceReflections.h"
#include "LightingDelegateUtil.h"
#include "LightingEngineInternal.h"
#include "LightingEngineApparatus.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/PipelineAccelerator.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/Techniques.h"
#include "../IThreadContext.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Assets/Assets.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../xleres/FileList.h"

#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Shader.h"


namespace RenderCore { namespace LightingEngine
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class IProbeRenderingInstance;
	std::shared_ptr<IProbeRenderingInstance> PrepareStaticShadowProbes(
		CompiledLightingTechnique& lightingTechnique,
		IThreadContext& threadContext)
	{
		auto& lightScene = *checked_cast<ForwardPlusLightScene*>(&lightingTechnique.GetLightScene());
		return lightScene.PrepareStaticShadowProbes(threadContext);
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class ForwardLightingCaptures
	{
	public:
		std::shared_ptr<Techniques::FrameBufferPool> _shadowGenFrameBufferPool;
		std::shared_ptr<Techniques::AttachmentPool> _shadowGenAttachmentPool;
		std::shared_ptr<ForwardPlusLightScene> _lightScene;
		std::shared_ptr<SkyOperator> _skyOperator;

		void DoShadowPrepare(LightingTechniqueIterator& iterator);
		void DoToneMap(LightingTechniqueIterator& iterator);
	};

	void ForwardLightingCaptures::DoShadowPrepare(LightingTechniqueIterator& iterator)
	{
		if (_lightScene->_shadowPreparationOperators->_operators.empty()) return;

		_lightScene->_preparedShadows.reserve(_lightScene->_dynamicShadowProjections.size());
		ILightScene::LightSourceId prevLightId = ~0u; 
		for (unsigned c=0; c<_lightScene->_dynamicShadowProjections.size(); ++c) {
			_lightScene->_preparedShadows.push_back(std::make_pair(
				_lightScene->_dynamicShadowProjections[c]._lightId,
				Internal::SetupShadowPrepare(iterator, *_lightScene->_dynamicShadowProjections[c]._desc, *_shadowGenFrameBufferPool, *_shadowGenAttachmentPool)));

			// shadow entries must be sorted by light id
			assert(prevLightId == ~0u || prevLightId < _lightScene->_dynamicShadowProjections[c]._lightId);
			prevLightId = _lightScene->_dynamicShadowProjections[c]._lightId;
		}
	}

	class ToneMapStandin
	{
	public:
		::Assets::PtrToFuturePtr<Techniques::IShaderOperator> _operator;
		const ::Assets::DependencyValidation& GetDependencyValidation() { return _operator->GetDependencyValidation(); }
		ToneMapStandin(
			const std::shared_ptr<Techniques::PipelinePool>& pool,
			const Techniques::FrameBufferTarget& fbTarget)
		{
			UniformsStreamInterface usi;
			usi.BindResourceView(0, Utility::Hash64("SubpassInputAttachment"));
			_operator = Techniques::CreateFullViewportOperator(
				pool, Techniques::FullViewportOperatorSubType::DisableDepth,
				BASIC_PIXEL_HLSL ":copy_inputattachment",
				{}, LIGHTING_OPERATOR_PIPELINE ":LightingOperator",
				fbTarget, usi);
		}
	};

	void ForwardLightingCaptures::DoToneMap(LightingTechniqueIterator& iterator)
	{
		// Very simple stand-in for tonemap -- just use a copy shader to write the HDR values directly to the LDR texture
		auto& standin = ConsoleRig::FindCachedBox<ToneMapStandin>(
			iterator._parsingContext->GetTechniqueContext()._graphicsPipelinePool,
			Techniques::FrameBufferTarget{iterator._rpi});
		auto* pipeline = standin._operator->TryActualize();
		if (pipeline) {
			UniformsStream us;
			IResourceView* srvs[] = { iterator._rpi.GetInputAttachmentView(0).get() };
			us._resourceViews = MakeIteratorRange(srvs);
			(*pipeline)->Draw(*iterator._threadContext, us);
		} 
	}

	static RenderStepFragmentInterface CreateToneMapFragment(
		std::function<void(LightingTechniqueIterator&)>&& fn)
	{
		RenderStepFragmentInterface fragment { RenderCore::PipelineType::Graphics };
		auto hdrInput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR, LoadStore::Retain, LoadStore::DontCare);
		auto ldrOutput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR, LoadStore::DontCare, LoadStore::Retain);

		Techniques::FrameBufferDescFragment::SubpassDesc subpass;
		subpass.AppendOutput(ldrOutput);
		subpass.AppendInput(hdrInput);
		subpass.SetName("tonemap");
		fragment.AddSubpass(std::move(subpass), std::move(fn));
		return fragment;
	}

	static ::Assets::PtrToFuturePtr<SkyOperator> CreateSkyOperator(
		const std::shared_ptr<Techniques::PipelinePool>& pipelinePool,
		const Techniques::FrameBufferTarget& fbTarget,
		const SkyOperatorDesc& desc)
	{
		return ::Assets::MakeFuture<std::shared_ptr<SkyOperator>>(desc, pipelinePool, fbTarget);
	}

	static void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, bool precisionTargets = false)
	{
		UInt2 fbSize{stitchingContext._workingProps._outputWidth, stitchingContext._workingProps._outputHeight};
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::MultisampleDepth,
				CreateDesc(
					BindFlag::DepthStencil | BindFlag::ShaderResource | BindFlag::InputAttachment, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::D24_UNORM_S8_UINT),
					"main-depth")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::ColorHDR,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::InputAttachment | BindFlag::ShaderResource, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], (!precisionTargets) ? Format::R16G16B16A16_FLOAT : Format::R32G32B32A32_FLOAT),
					"color-hdr")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferNormal,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8B8A8_SNORM),
					"gbuffer-normal")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferMotion,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8_SINT),
					"gbuffer-motion")
			}
		};
		for (const auto& a:attachments)
			stitchingContext.DefineAttachment(a);
	}

	static RenderStepFragmentInterface CreateDepthMotionFragment(
		std::shared_ptr<Techniques::ITechniqueDelegate> depthMotionDelegate)
	{
		RenderStepFragmentInterface result { PipelineType::Graphics };
		Techniques::FrameBufferDescFragment::SubpassDesc preDepthSubpass;
		preDepthSubpass.AppendOutput(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion, LoadStore::Clear));
		preDepthSubpass.SetDepthStencil(result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth, LoadStore::Clear));
		preDepthSubpass.SetName("PreDepth");
		result.AddSubpass(std::move(preDepthSubpass), depthMotionDelegate, Techniques::BatchFilter::General);
		return result;
	}

	static RenderStepFragmentInterface CreateDepthMotionNormalFragment(
		std::shared_ptr<Techniques::ITechniqueDelegate> depthMotionNormalDelegate)
	{
		RenderStepFragmentInterface result { PipelineType::Graphics };
		Techniques::FrameBufferDescFragment::SubpassDesc preDepthSubpass;
		preDepthSubpass.AppendOutput(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion, LoadStore::Clear));
		preDepthSubpass.AppendOutput(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal, LoadStore::Clear));
		preDepthSubpass.SetDepthStencil(result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth, LoadStore::Clear));
		preDepthSubpass.SetName("PreDepth");

		auto srDelegateFuture = Internal::CreateBuildGBufferResourceDelegate();
		srDelegateFuture.StallWhilePending();
		result.AddSubpass(std::move(preDepthSubpass), depthMotionNormalDelegate, Techniques::BatchFilter::General, {}, srDelegateFuture.Actualize());
		return result;
	}

	static RenderStepFragmentInterface CreateForwardSceneFragment(
		std::shared_ptr<ForwardLightingCaptures> captures,
		std::shared_ptr<Techniques::ITechniqueDelegate> forwardIllumDelegate)
	{
		RenderStepFragmentInterface result { PipelineType::Graphics };
		auto lightResolve = result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR, LoadStore::DontCare);
		auto depth = result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth);
		
		Techniques::FrameBufferDescFragment::SubpassDesc skySubpass;
		skySubpass.AppendOutput(lightResolve);
		skySubpass.SetDepthStencil(depth);
		skySubpass.SetName("Sky");
		const bool drawIBLAsSky = true;
		if (drawIBLAsSky) {
			result.AddSubpass(
				std::move(skySubpass),
				[weakCaptures = std::weak_ptr<ForwardLightingCaptures>{captures}](LightingTechniqueIterator& iterator) {
					auto l = weakCaptures.lock();
					if (l) l->_skyOperator->Execute(iterator);
				});
		} else {
			result.AddSkySubpass(std::move(skySubpass));
		}

		Techniques::FrameBufferDescFragment::SubpassDesc mainSubpass;
		mainSubpass.AppendOutput(lightResolve);
		mainSubpass.SetDepthStencil(depth);

		const bool hasSSR = true;
		if (hasSSR) {
			auto ssr = result.DefineAttachment(Utility::Hash64("SSRReflections"), LoadStore::Retain, LoadStore::DontCare);
			mainSubpass.AppendNonFrameBufferAttachmentView(ssr);
		}
		mainSubpass.SetName("MainForward");

		ParameterBox box;
		auto dominantLightOp = captures->_lightScene->GetDominantLightOperator();
		if (dominantLightOp) {
			box.SetParameter("DOMINANT_LIGHT_SHAPE", (unsigned)dominantLightOp.value()._shape);
			auto shdw = captures->_lightScene->GetDominantShadowOperator();
			if (shdw) {
				// assume the shadow operator that will be associated is index 0
				auto resolveParam = Internal::MakeShadowResolveParam(shdw.value());
				resolveParam.WriteShaderSelectors(box);
			}
		}

		result.AddSubpass(std::move(mainSubpass), forwardIllumDelegate, Techniques::BatchFilter::General, std::move(box), captures->_lightScene->CreateMainSceneResourceDelegate());
		return result;
	}

	::Assets::PtrToFuturePtr<CompiledLightingTechnique> CreateForwardLightingTechnique(
		const std::shared_ptr<LightingEngineApparatus>& apparatus,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const AmbientLightOperatorDesc& ambientLightOperator,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const FrameBufferProperties& fbProps)
	{
		return CreateForwardLightingTechnique(
			apparatus->_pipelineAccelerators, apparatus->_lightingOperatorCollection, apparatus->_sharedDelegates, 
			apparatus->_dmShadowDescSetTemplate,
			resolveOperators, shadowGenerators, ambientLightOperator,
			preregisteredAttachments, fbProps);
	}

	::Assets::PtrToFuturePtr<CompiledLightingTechnique> CreateForwardLightingTechnique(
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelinePool>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& shadowDescSet,
		IteratorRange<const LightSourceOperatorDesc*> positionalLightOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const AmbientLightOperatorDesc& ambientLightOperator,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const FrameBufferProperties& fbProps)
	{
		RasterizationLightTileOperator::Configuration tilingConfig;
		auto lightSceneFuture = std::make_shared<::Assets::FuturePtr<ForwardPlusLightScene>>("forward-light-scene");
		ForwardPlusLightScene::ConstructToFuture(
			*lightSceneFuture, pipelineAccelerators, pipelinePool, techDelBox, shadowDescSet,
			positionalLightOperators, shadowGenerators, ambientLightOperator, tilingConfig);
		
		Techniques::FragmentStitchingContext stitchingContext { preregisteredAttachments, fbProps };
		PreregisterAttachments(stitchingContext);

		auto result = std::make_shared<::Assets::FuturePtr<CompiledLightingTechnique>>("forward-lighting-technique");
		::Assets::WhenAll(lightSceneFuture).ThenConstructToFuture(
			*result,
			[techDelBox, stitchingContextCap=std::move(stitchingContext), pipelineAccelerators, pipelinePool]
			(	::Assets::FuturePtr<CompiledLightingTechnique>& thatFuture,
				std::shared_ptr<ForwardPlusLightScene> lightScene) {

				auto captures = std::make_shared<ForwardLightingCaptures>();
				captures->_shadowGenAttachmentPool = std::make_shared<Techniques::AttachmentPool>(pipelineAccelerators->GetDevice());
				captures->_shadowGenFrameBufferPool = Techniques::CreateFrameBufferPool();
				captures->_lightScene = lightScene;

				auto stitchingContext = stitchingContextCap;
				lightScene->GetHierarchicalDepthsOperator().PreregisterAttachments(stitchingContext);
				lightScene->GetLightTiler().PreregisterAttachments(stitchingContext);
				lightScene->GetScreenSpaceReflectionsOperator().PreregisterAttachments(stitchingContext);

				auto lightingTechnique = std::make_shared<CompiledLightingTechnique>(pipelineAccelerators, stitchingContext, lightScene);
				lightingTechnique->_depVal = ::Assets::GetDepValSys().Make();
				lightingTechnique->_depVal.RegisterDependency(lightScene->GetHierarchicalDepthsOperator().GetDependencyValidation());
				lightingTechnique->_depVal.RegisterDependency(lightScene->GetLightTiler().GetDependencyValidation());
				lightingTechnique->_depVal.RegisterDependency(lightScene->GetScreenSpaceReflectionsOperator().GetDependencyValidation());

				// Reset captures
				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						auto& stitchingContext = iterator._parsingContext->GetFragmentStitchingContext();
						PreregisterAttachments(stitchingContext);
						captures->_lightScene->GetHierarchicalDepthsOperator().PreregisterAttachments(stitchingContext);
						captures->_lightScene->GetLightTiler().PreregisterAttachments(stitchingContext);
						captures->_lightScene->GetScreenSpaceReflectionsOperator().PreregisterAttachments(stitchingContext);
						captures->_lightScene->SetupProjection(*iterator._parsingContext);
					});

				// Prepare shadows
				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						captures->DoShadowPrepare(iterator);
					});

				// Pre depth
				// lightingTechnique->CreateStep_RunFragments(lightScene->CreateDepthMotionFragment(techDelBox->_depthMotionDelegate));
				lightingTechnique->CreateStep_RunFragments(CreateDepthMotionNormalFragment(techDelBox->_depthMotionNormalDelegate));

				// Build hierarchical depths
				lightingTechnique->CreateStep_RunFragments(lightScene->GetHierarchicalDepthsOperator().CreateFragment(stitchingContext._workingProps));

				// Light tiling & configure lighting descriptors
				lightingTechnique->CreateStep_RunFragments(lightScene->GetLightTiler().CreateInitFragment(stitchingContext._workingProps));
				lightingTechnique->CreateStep_RunFragments(lightScene->GetLightTiler().CreateFragment(stitchingContext._workingProps));

				// Calculate SSRs
				lightingTechnique->CreateStep_RunFragments(lightScene->GetScreenSpaceReflectionsOperator().CreateFragment(stitchingContext._workingProps));

				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						captures->_lightScene->ConfigureParsingContext(*iterator._parsingContext);
					});

				// Draw main scene
				auto mainSceneFragmentRegistration = lightingTechnique->CreateStep_RunFragments(CreateForwardSceneFragment(captures, techDelBox->_forwardIllumDelegate_DisableDepthWrite));

				// Post processing
				auto toneMapFragment = CreateToneMapFragment(
					[captures](LightingTechniqueIterator& iterator) {
						captures->DoToneMap(iterator);
					});
				lightingTechnique->CreateStep_RunFragments(std::move(toneMapFragment));

				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						iterator._parsingContext->_extraSequencerDescriptorSet = {0ull, nullptr};
						captures->_lightScene->_preparedShadows.clear();
					});

				lightingTechnique->CompleteConstruction();

				// Any final operators that depend on the resolved frame buffer:
				auto resolvedFB = lightingTechnique->GetResolvedFrameBufferDesc(mainSceneFragmentRegistration);
				auto skyOpFuture = CreateSkyOperator(pipelinePool, Techniques::FrameBufferTarget{resolvedFB.first, resolvedFB.second}, SkyOperatorDesc { SkyTextureType::Equirectangular });
				::Assets::WhenAll(skyOpFuture).ThenConstructToFuture(
					thatFuture,
					[captures, lightingTechnique](auto skyOp) {
						captures->_skyOperator = skyOp;
						captures->_lightScene->_onChangeSkyTexture.Bind(
							[weakSkyOperator=std::weak_ptr<SkyOperator>(skyOp)](std::shared_ptr<Techniques::DeferredShaderResource> texture) {
								auto l=weakSkyOperator.lock();
								if (l) l->SetResource(texture ? texture->GetShaderResource() : nullptr);
							});
						lightingTechnique->_depVal.RegisterDependency(skyOp->GetDependencyValidation());
						return lightingTechnique;
					});
			});
		return result;
	}

}}

