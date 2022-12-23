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
#include "SSAOOperator.h"
#include "ToneMapOperator.h"
#include "LightingDelegateUtil.h"
#include "LightingEngineInitialization.h"
#include "LightingEngineIterator.h"
#include "LightingEngineApparatus.h"
#include "LightUniforms.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/PipelineAccelerator.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/Techniques.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/DeformAccelerator.h"
#include "../IDevice.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"
#include "../Utility/MemoryUtils.h"

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine
{

	static const uint64_t s_shadowTemplate = "ShadowTemplate"_h;
	static const uint64_t s_forwardLighting = "ForwardLighting"_h;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class ForwardLightingCaptures
	{
	public:
		std::shared_ptr<ForwardPlusLightScene> _lightScene;
		std::shared_ptr<SkyOperator> _skyOperator;
		std::shared_ptr<HierarchicalDepthsOperator> _hierarchicalDepthsOperator;
		std::shared_ptr<ScreenSpaceReflectionsOperator> _ssrOperator;
		std::shared_ptr<Techniques::SemiConstantDescriptorSet> _forwardLightingSemiConstant;

		void DoShadowPrepare(LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence);
		void DoToneMap(LightingTechniqueIterator& iterator);
		void ConfigureParsingContext(Techniques::ParsingContext& parsingContext);
		void ReleaseParsingContext(Techniques::ParsingContext& parsingContext);

		void ConfigureSkyOperatorBindings();

		std::vector<unsigned> _boundOnSkyTextureChange;
		~ForwardLightingCaptures()
		{
			if (_lightScene)
				for (auto b:_boundOnSkyTextureChange) _lightScene->UnbindOnChangeSkyTexture(b);
		}
	};

	void ForwardLightingCaptures::DoShadowPrepare(LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence)
	{
		if (_lightScene->_shadowScheduler)
			_lightScene->_shadowScheduler->DoShadowPrepare(iterator, sequence);
	}

	void ForwardLightingCaptures::ConfigureParsingContext(Techniques::ParsingContext& parsingContext)
	{
		_lightScene->ConfigureParsingContext(parsingContext, _ssrOperator != nullptr);
		if (auto* dominantShadow = _lightScene->GetDominantPreparedShadow())
			// find the prepared shadow associated with the dominant light (if it exists) and make sure it's descriptor set is accessible
			parsingContext.GetUniformDelegateManager()->BindFixedDescriptorSet(s_shadowTemplate, *dominantShadow->GetDescriptorSet());
		assert(_forwardLightingSemiConstant);
		parsingContext.GetUniformDelegateManager()->BindSemiConstantDescriptorSet(s_forwardLighting, _forwardLightingSemiConstant);
	}

	void ForwardLightingCaptures::ReleaseParsingContext(Techniques::ParsingContext& parsingContext)
	{
		if (auto* dominantShadow = _lightScene->GetDominantPreparedShadow())
			parsingContext.GetUniformDelegateManager()->UnbindFixedDescriptorSet(*dominantShadow->GetDescriptorSet());
		parsingContext.GetUniformDelegateManager()->UnbindSemiConstantDescriptorSet(*_forwardLightingSemiConstant);
		if (_lightScene->_shadowScheduler)
			_lightScene->_shadowScheduler->ClearPreparedShadows();
	}

	static ::Assets::PtrToMarkerPtr<Techniques::IShaderOperator> CreateToneMapOperator(
		const std::shared_ptr<Techniques::PipelineCollection>& pool, 
		Techniques::RenderPassInstance& rpi)
	{
		Techniques::PixelOutputStates outputStates;
		outputStates.Bind(rpi);
		outputStates.Bind(Techniques::CommonResourceBox::s_dsDisable);
		AttachmentBlendDesc blendStates[] { Techniques::CommonResourceBox::s_abOpaque };
		outputStates.Bind(MakeIteratorRange(blendStates));
		UniformsStreamInterface usi;
		usi.BindResourceView(0, "SubpassInputAttachment"_h);
		return Techniques::CreateFullViewportOperator(
			pool, Techniques::FullViewportOperatorSubType::DisableDepth,
			BASIC_PIXEL_HLSL ":copy_inputattachment",
			{}, GENERAL_OPERATOR_PIPELINE ":GraphicsMain",
			outputStates, usi);
	};

	void ForwardLightingCaptures::DoToneMap(LightingTechniqueIterator& iterator)
	{
		// Very simple stand-in for tonemap -- just use a copy shader to write the HDR values directly to the LDR texture
		auto pipelineFuture = CreateToneMapOperator(iterator._parsingContext->GetTechniqueContext()._graphicsPipelinePool, iterator._rpi);
		pipelineFuture->StallWhilePending();
		auto pipeline = pipelineFuture->TryActualize();
		if (pipeline) {
			UniformsStream us;
			IResourceView* srvs[] = { iterator._rpi.GetInputAttachmentView(0).get() };
			us._resourceViews = MakeIteratorRange(srvs);
			(*pipeline)->Draw(*iterator._threadContext, us);
		}
	}

	void ForwardLightingCaptures::ConfigureSkyOperatorBindings()
	{
		unsigned binding0 = _lightScene->BindOnChangeSkyTexture(
			[weakSkyOperator=std::weak_ptr<SkyOperator>(_skyOperator)](auto texture) {
				auto l=weakSkyOperator.lock();
				if (l) l->SetResource(texture ? texture->GetShaderResource() : nullptr);
			});
		unsigned binding1 = _lightScene->BindOnChangeSkyTexture(
			[weakSSROperator=std::weak_ptr<ScreenSpaceReflectionsOperator>(_ssrOperator)](auto texture) {
				auto l=weakSSROperator.lock();
				if (l) {
					// Note -- this is getting the full sky texture (not the specular IBL prefiltered texture!)
					if (texture) {
						TextureViewDesc adjustedViewDesc;
						adjustedViewDesc._mipRange._min = 2;
						auto adjustedView = texture->GetShaderResource()->GetResource()->CreateTextureView(BindFlag::ShaderResource, adjustedViewDesc);
						l->SetSpecularIBL(adjustedView);
					} else
						l->SetSpecularIBL(nullptr);
				}
			});
		_boundOnSkyTextureChange.push_back(binding0);
		_boundOnSkyTextureChange.push_back(binding1);
	}

	static RenderStepFragmentInterface CreateToneMapFragment(
		std::function<void(LightingTechniqueIterator&)>&& fn)
	{
		RenderStepFragmentInterface fragment { RenderCore::PipelineType::Graphics };
		auto hdrInput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).Discard();
		auto ldrOutput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR).NoInitialState();

		Techniques::FrameBufferDescFragment::SubpassDesc subpass;
		subpass.AppendOutput(ldrOutput);
		subpass.AppendInput(hdrInput);
		subpass.SetName("tonemap");
		fragment.AddSubpass(std::move(subpass), std::move(fn));
		return fragment;
	}

	static std::future<std::shared_ptr<SkyOperator>> CreateSkyOperator(
		const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
		const Techniques::FrameBufferTarget& fbTarget,
		const SkyOperatorDesc& desc)
	{
		return ::Assets::ConstructToFuturePtr<SkyOperator>(desc, pipelinePool, fbTarget);
	}

	static void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, bool precisionTargets = false)
	{
		UInt2 fbSize{stitchingContext._workingProps._width, stitchingContext._workingProps._height};
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::MultisampleDepth,
				CreateDesc(
					BindFlag::DepthStencil | BindFlag::ShaderResource | BindFlag::InputAttachment,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], stitchingContext.GetSystemAttachmentFormat(Techniques::SystemAttachmentFormat::MainDepthStencil))),
				"main-depth"
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::ColorHDR,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource | BindFlag::InputAttachment,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], (!precisionTargets) ? Format::R16G16B16A16_FLOAT : Format::R32G32B32A32_FLOAT)),
				"color-hdr"
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferNormal,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8B8A8_SNORM)),
				"gbuffer-normal"
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferMotion,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8_SINT)),
				"gbuffer-motion"
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
		preDepthSubpass.AppendOutput(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion).Clear().FinalState(BindFlag::ShaderResource));
		preDepthSubpass.SetDepthStencil(result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).Clear().FinalState(BindFlag::ShaderResource));
		preDepthSubpass.SetName("PreDepth");
		result.AddSubpass(std::move(preDepthSubpass), depthMotionDelegate, Techniques::BatchFlags::Opaque);
		return result;
	}

	static RenderStepFragmentInterface CreateDepthMotionNormalFragment(
		std::shared_ptr<Techniques::ITechniqueDelegate> depthMotionNormalDelegate)
	{
		RenderStepFragmentInterface result { PipelineType::Graphics };
		Techniques::FrameBufferDescFragment::SubpassDesc preDepthSubpass;
		preDepthSubpass.AppendOutput(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion).Clear().FinalState(BindFlag::ShaderResource));
		preDepthSubpass.AppendOutput(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal).Clear().FinalState(BindFlag::ShaderResource));
		preDepthSubpass.SetDepthStencil(result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).Clear().FinalState(BindFlag::ShaderResource));
		preDepthSubpass.SetName("PreDepth");

		auto srDelegateFuture = Internal::CreateBuildGBufferResourceDelegate();
		srDelegateFuture.StallWhilePending();
		result.AddSubpass(std::move(preDepthSubpass), depthMotionNormalDelegate, Techniques::BatchFlags::Opaque, {}, srDelegateFuture.Actualize());
		return result;
	}

	class MainSceneResourceDelegate : public Techniques::IShaderResourceDelegate
	{
	public:
		void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst) override
		{
			if (bindingFlags & (1ull<<uint64_t(_resourceViewsStart))) {
				assert(bindingFlags & (1ull<<uint64_t(_resourceViewsStart+1)));
				assert(context._rpi);
				dst[_resourceViewsStart] = context._rpi->GetNonFrameBufferAttachmentView(0).get();
				dst[_resourceViewsStart+1] = context._rpi->GetNonFrameBufferAttachmentView(1).get();
			}
			if (bindingFlags & (1ull<<uint64_t(_resourceViewsStart+2)))
				dst[_resourceViewsStart+2] = _noise.get();
			_lightSceneDelegate->WriteResourceViews(context, objectContext, bindingFlags, dst);
		}

		void WriteSamplers(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<ISampler**> dst) override
		{
			_lightSceneDelegate->WriteSamplers(context, objectContext, bindingFlags, dst);
		}
		void WriteImmediateData(Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
		{
			_lightSceneDelegate->WriteImmediateData(context, objectContext, idx, dst);
		}
		size_t GetImmediateDataSize(Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override
		{
			return _lightSceneDelegate->GetImmediateDataSize(context, objectContext, idx);
		}

		MainSceneResourceDelegate(std::shared_ptr<Techniques::IShaderResourceDelegate> lightSceneDelegate, bool hasSSR, Techniques::DeferredShaderResource& balanceNoiseTexture)
		: _lightSceneDelegate(std::move(lightSceneDelegate))
		{
			_interface = _lightSceneDelegate->_interface;
			_resourceViewsStart = (unsigned)_interface.GetResourceViewBindings().size();
			if (hasSSR) {
				_interface.BindResourceView(_resourceViewsStart+0, "SSR"_h);
				_interface.BindResourceView(_resourceViewsStart+1, "SSRConfidence"_h);
			}

			_noise = balanceNoiseTexture.GetShaderResource();
			_completionCmdList = balanceNoiseTexture.GetCompletionCommandList();
			_interface.BindResourceView(_resourceViewsStart+2, "NoiseTexture"_h);
		}

		std::shared_ptr<Techniques::IShaderResourceDelegate> _lightSceneDelegate;
		unsigned _resourceViewsStart = 0;
		std::shared_ptr<IResourceView> _noise;
	};

	static RenderStepFragmentInterface CreateForwardSceneFragment(
		std::shared_ptr<ForwardLightingCaptures> captures,
		std::shared_ptr<Techniques::ITechniqueDelegate> forwardIllumDelegate,
		bool hasSSR,
		Techniques::DeferredShaderResource& balanceNoiseTexture,
		std::optional<LightSourceOperatorDesc> dominantLightOperator,
		std::optional<ShadowOperatorDesc> dominantShadowOperator)
	{
		RenderStepFragmentInterface result { PipelineType::Graphics };
		auto lightResolve = result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).NoInitialState();
		auto depth = result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).InitialState(BindFlag::ShaderResource).FinalState(BindFlag::DepthStencil);
		
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

		if (hasSSR) {
			mainSubpass.AppendNonFrameBufferAttachmentView(result.DefineAttachment("SSReflection"_h).NoInitialState());
			mainSubpass.AppendNonFrameBufferAttachmentView(result.DefineAttachment("SSRConfidence"_h).NoInitialState());
		}
		mainSubpass.SetName("MainForward");

		ParameterBox box;
		if (dominantLightOperator) {
			if (dominantShadowOperator) {
				// assume the shadow operator that will be associated is index 0
				Internal::MakeShadowResolveParam(dominantShadowOperator.value()).WriteShaderSelectors(box);
				box.SetParameter("DOMINANT_LIGHT_SHAPE", (unsigned)dominantLightOperator.value()._shape | 0x20u);
			} else {
				box.SetParameter("DOMINANT_LIGHT_SHAPE", (unsigned)dominantLightOperator.value()._shape);
			}
			if (captures->_lightScene->ShadowProbesSupported())
				box.SetParameter("SHADOW_PROBE", 1);
		}

		auto resourceDelegate = std::make_shared<MainSceneResourceDelegate>(
			captures->_lightScene->CreateMainSceneResourceDelegate(),
			hasSSR, balanceNoiseTexture);

		result.AddSubpass(
			std::move(mainSubpass), forwardIllumDelegate, Techniques::BatchFlags::Opaque|Techniques::BatchFlags::Blending, std::move(box),
			std::move(resourceDelegate));
		return result;
	}

	template<typename Dest>
		const Dest& ChainedOperatorCast(const ChainedOperatorDesc& desc)
	{
		assert(desc._structureType == ctti::type_id<Dest>().hash());
		return ((const ChainedOperatorTemplate<Dest>*)&desc)->_desc;
	}

	static ShadowProbes::Configuration MakeShadowProbeConfiguration(const ShadowOperatorDesc& opDesc)
	{
		ShadowProbes::Configuration result;
		assert(opDesc._width == opDesc._height);		// expecting square probe textures
		result._staticFaceDims = opDesc._width;
		result._staticFormat = opDesc._format;
		result._singleSidedBias = opDesc._singleSidedBias;
		result._doubleSidedBias = opDesc._doubleSidedBias;
		return result;
	}

	struct OperatorDigest
	{
		std::optional<ScreenSpaceReflectionsOperatorDesc> _ssr;
		std::optional<AmbientOcclusionOperatorDesc> _ssao;
		std::optional<ToneMapAcesOperatorDesc> _tonemapAces;

		ForwardPlusLightScene::ShadowPreparerIdMapping _shadowPrepreparerIdMapping;
		std::vector<ForwardPlusLightScene::LightOperatorInfo> _lightSceneOperatorInfo;
		RasterizationLightTileOperator::Configuration _tilingConfig;

		std::optional<LightSourceOperatorDesc> _dominantLightOperator;
		std::optional<ShadowOperatorDesc> _dominantShadowOperator;

		OperatorDigest(
			IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
			IteratorRange<const ShadowOperatorDesc*> shadowOperators,
			const ChainedOperatorDesc& globalOperatorsChain)
		{
			// Given a partially opaque set of operators, try to digest and interpret them in order so we know how to create
			// the light scene, and what operators to create

			// Map the shadow operator ids onto the underlying type of shadow (dynamically generated, shadow probes, etc)
			{
				_shadowPrepreparerIdMapping._shadowPreparers.reserve(shadowOperators.size());
				_shadowPrepreparerIdMapping._operatorToShadowPreparerId.resize(shadowOperators.size(), ~0u);
				for (unsigned c=0; c<shadowOperators.size(); ++c) {
					if (shadowOperators[c]._resolveType == ShadowResolveType::Probe) {
						// setup shadow operator for probes
						if (_shadowPrepreparerIdMapping._operatorForStaticProbes != ~0u)
							Throw(std::runtime_error("Multiple operators for shadow probes detected. Only zero or one is supported"));
						_shadowPrepreparerIdMapping._operatorForStaticProbes = c;
						_shadowPrepreparerIdMapping._shadowProbesCfg = MakeShadowProbeConfiguration(shadowOperators[c]);
					} else {
						_shadowPrepreparerIdMapping._operatorToShadowPreparerId[c] = (unsigned)_shadowPrepreparerIdMapping._shadowPreparers.size();

						if (shadowOperators[c]._dominantLight) {
							if (_shadowPrepreparerIdMapping._dominantShadowOperator != ~0u)
								Throw(std::runtime_error("Multiple dominant shadow operators detected. This isn't supported -- there must be either 0 or 1"));
							_shadowPrepreparerIdMapping._dominantShadowOperator = c;
							_dominantShadowOperator = shadowOperators[c];
						}

						_shadowPrepreparerIdMapping._shadowPreparers.push_back(shadowOperators[c]);
					}
				}
			}

			_lightSceneOperatorInfo.reserve(resolveOperators.size());
			for (unsigned c=0; c<resolveOperators.size(); ++c) {
				unsigned flags = (resolveOperators[c]._flags & LightSourceOperatorDesc::Flags::DominantLight) ? 0 : Internal::StandardPositionLightFlags::SupportFiniteRange|Internal::StandardPositionLightFlags::LightTiler;
				_lightSceneOperatorInfo.push_back({flags, Internal::AsUniformShapeCode(resolveOperators[c]._shape)});		// LightTiler assigned by the light scene

				if (resolveOperators[c]._flags & LightSourceOperatorDesc::Flags::DominantLight) {
					if (_shadowPrepreparerIdMapping._dominantLightOperator != ~0u)
						Throw(std::runtime_error("Multiple dominant light operators detected. This isn't supported -- there must be either 0 or 1"));
					_shadowPrepreparerIdMapping._dominantLightOperator = c;
					_dominantLightOperator = resolveOperators[c];
				}
			}

			auto* chain = &globalOperatorsChain;
			while (chain) {
				switch(chain->_structureType) {
				case ctti::type_id<ScreenSpaceReflectionsOperatorDesc>().hash():
					if (_ssr)
						Throw(std::runtime_error("Multiple SSR operators found, where only one expected"));
					_ssr = ChainedOperatorCast<ScreenSpaceReflectionsOperatorDesc>(*chain);
					break;

				case ctti::type_id<AmbientOcclusionOperatorDesc>().hash():
					if (_ssao)
						Throw(std::runtime_error("Multiple SSAO operators found, where only one expected"));
					_ssao = ChainedOperatorCast<AmbientOcclusionOperatorDesc>(*chain);
					break;

				case ctti::type_id<ToneMapAcesOperatorDesc>().hash():
					if (_tonemapAces)
						Throw(std::runtime_error("Multiple tonemap operators found, where only one expected"));
					_tonemapAces = ChainedOperatorCast<ToneMapAcesOperatorDesc>(*chain);
					break;
				}
				chain = chain->_next;
			}
		}
	};

	template<typename MarkerType, typename Time>
		static bool MarkerTimesOut(std::future<MarkerType>& marker, Time timeoutTime) { return marker.wait_until(timeoutTime) == std::future_status::timeout; }
	template<typename MarkerType, typename Time>
		static bool MarkerTimesOut(std::shared_future<MarkerType>& marker, Time timeoutTime) { return marker.wait_until(timeoutTime) == std::future_status::timeout; }
	template<typename MarkerType, typename Time>
		static bool MarkerTimesOut(::Assets::Marker<MarkerType>& marker, Time timeoutTime)
		{
			auto remainingTime = timeoutTime - std::chrono::steady_clock::now();
			if (remainingTime.count() <= 0) return true;
			auto t = marker.StallWhilePending(std::chrono::duration_cast<std::chrono::microseconds>(remainingTime));
			return t.value_or(::Assets::AssetState::Pending) == ::Assets::AssetState::Pending;
		}

	void CreateForwardLightingTechnique(
		std::promise<std::shared_ptr<CompiledLightingTechnique>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowOperators,
		const ChainedOperatorDesc& globalOperators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachmentsInit,
		const FrameBufferProperties& fbProps)
	{
		struct ConstructionHelper
		{
			// optional operators
			::Assets::PtrToMarkerPtr<HierarchicalDepthsOperator> _hierarchicalDepthsOperatorFuture;
			::Assets::PtrToMarkerPtr<ScreenSpaceReflectionsOperator> _ssrFuture;
			::Assets::PtrToMarkerPtr<SSAOOperator> _ssaoFuture;
			::Assets::PtrToMarkerPtr<ToneMapAcesOperator> _toneMapAcesOperator;

			// main technique delegates
			SharedTechniqueDelegateBox::TechniqueDelegateFuture _depthMotionNormalRoughnessDelegate;
			SharedTechniqueDelegateBox::TechniqueDelegateFuture _depthMotionDelegate;
			SharedTechniqueDelegateBox::TechniqueDelegateFuture _forwardIllumDelegate;

			std::shared_future<std::shared_ptr<Techniques::DeferredShaderResource>> _balancedNoiseTexture;

			std::future<std::shared_ptr<ForwardPlusLightScene>> _lightSceneFuture;
		};
		auto helper = std::make_shared<ConstructionHelper>();

		helper->_balancedNoiseTexture = ::Assets::MakeAssetPtr<Techniques::DeferredShaderResource>(BALANCED_NOISE_TEXTURE);
		helper->_hierarchicalDepthsOperatorFuture = ::Assets::ConstructToMarkerPtr<HierarchicalDepthsOperator>(pipelinePool);

		OperatorDigest digest { resolveOperators, shadowOperators, globalOperators };

		helper->_lightSceneFuture = ::Assets::ConstructToFuturePtr<ForwardPlusLightScene>(
			ForwardPlusLightScene::ConstructionServices{pipelineAccelerators, pipelinePool, techDelBox},
			std::move(digest._shadowPrepreparerIdMapping), std::move(digest._lightSceneOperatorInfo),
			digest._tilingConfig);

		if (digest._ssr)
			helper->_ssrFuture = ::Assets::ConstructToMarkerPtr<ScreenSpaceReflectionsOperator>(pipelinePool, *digest._ssr);

		if (digest._ssao)
			helper->_ssaoFuture = ::Assets::ConstructToMarkerPtr<SSAOOperator>(pipelinePool, *digest._ssao, true);

		if (digest._tonemapAces)
			helper->_toneMapAcesOperator = ::Assets::ConstructToMarkerPtr<ToneMapAcesOperator>(pipelinePool, *digest._tonemapAces);

		helper->_depthMotionNormalRoughnessDelegate = techDelBox->GetDepthMotionNormalRoughnessDelegate();
		helper->_depthMotionDelegate = techDelBox->GetDepthMotionDelegate();
		helper->_forwardIllumDelegate = techDelBox->GetForwardIllumDelegate_DisableDepthWrite();

		std::vector<Techniques::PreregisteredAttachment> preregisteredAttachments { preregisteredAttachmentsInit.begin(), preregisteredAttachmentsInit.end() };

		::Assets::PollToPromise(
			std::move(promise),
			[helper](auto timeout) {
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				if (MarkerTimesOut(helper->_lightSceneFuture, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (helper->_hierarchicalDepthsOperatorFuture && MarkerTimesOut(*helper->_hierarchicalDepthsOperatorFuture, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (helper->_ssrFuture && MarkerTimesOut(*helper->_ssrFuture, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (helper->_ssaoFuture && MarkerTimesOut(*helper->_ssaoFuture, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (helper->_toneMapAcesOperator && MarkerTimesOut(*helper->_toneMapAcesOperator, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (MarkerTimesOut(helper->_depthMotionNormalRoughnessDelegate, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (MarkerTimesOut(helper->_depthMotionDelegate, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (MarkerTimesOut(helper->_forwardIllumDelegate, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (MarkerTimesOut(helper->_balancedNoiseTexture, timeoutTime)) return ::Assets::PollStatus::Continue;
				return ::Assets::PollStatus::Finish;
			},
			[helper, techDelBox, pipelineAccelerators, pipelinePool, preregisteredAttachments=std::move(preregisteredAttachments), fbProps, digest=std::move(digest)]
			(std::promise<std::shared_ptr<CompiledLightingTechnique>>&& thatPromise) {

				TRY {
					auto captures = std::make_shared<ForwardLightingCaptures>();
					captures->_lightScene = helper->_lightSceneFuture.get();
					captures->_hierarchicalDepthsOperator = helper->_hierarchicalDepthsOperatorFuture->Actualize();
					if (helper->_ssrFuture)
						captures->_ssrOperator = helper->_ssrFuture->Actualize();
					captures->_forwardLightingSemiConstant = Techniques::CreateSemiConstantDescriptorSet(
						*techDelBox->_forwardLightingDescSetTemplate, "ForwardLighting", PipelineType::Graphics, *pipelinePool->GetDevice());

					auto depthMotionNormalRoughnessDelegate = helper->_depthMotionNormalRoughnessDelegate.get();
					auto depthMotionDelegate = helper->_depthMotionDelegate.get();
					auto forwardIllumDelegate_DisableDepthWrite = helper->_forwardIllumDelegate.get();
					auto balancedNoiseTexture = helper->_balancedNoiseTexture.get();

					Techniques::FragmentStitchingContext stitchingContext { preregisteredAttachments, fbProps, Techniques::CalculateDefaultSystemFormats(*pipelinePool->GetDevice()) };
					PreregisterAttachments(stitchingContext);
					captures->_hierarchicalDepthsOperator->PreregisterAttachments(stitchingContext);
					captures->_lightScene->GetLightTiler().PreregisterAttachments(stitchingContext);
					if (captures->_ssrOperator)
						captures->_ssrOperator->PreregisterAttachments(stitchingContext);

					auto lightingTechnique = std::make_shared<CompiledLightingTechnique>(captures->_lightScene);
					lightingTechnique->_depVal = ::Assets::GetDepValSys().Make();
					lightingTechnique->_depVal.RegisterDependency(captures->_hierarchicalDepthsOperator->GetDependencyValidation());
					lightingTechnique->_depVal.RegisterDependency(captures->_lightScene->GetLightTiler().GetDependencyValidation());
					if (captures->_ssrOperator)
						lightingTechnique->_depVal.RegisterDependency(captures->_ssrOperator->GetDependencyValidation());
					lightingTechnique->_depVal.RegisterDependency(depthMotionNormalRoughnessDelegate->GetDependencyValidation());
					lightingTechnique->_depVal.RegisterDependency(depthMotionDelegate->GetDependencyValidation());
					lightingTechnique->_depVal.RegisterDependency(forwardIllumDelegate_DisableDepthWrite->GetDependencyValidation());

					// Prepare shadows
					lightingTechnique->CreateDynamicSequence(
						[captures](LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence) {
							captures->DoShadowPrepare(iterator, sequence);
							captures->_lightScene->Prerender(*iterator._threadContext);
						});

					auto& mainSequence = lightingTechnique->CreateSequence();
					mainSequence.CreateStep_CallFunction(
						[](LightingTechniqueIterator& iterator) {
							if (iterator._deformAcceleratorPool)
								iterator._deformAcceleratorPool->SetVertexInputBarrier(*iterator._threadContext);
						});

					// Pre depth
					if (captures->_ssrOperator) {
						mainSequence.CreateStep_RunFragments(CreateDepthMotionNormalFragment(depthMotionNormalRoughnessDelegate));
					} else {
						mainSequence.CreateStep_RunFragments(CreateDepthMotionFragment(depthMotionDelegate));
					}

					mainSequence.CreateStep_CallFunction(
						[](LightingTechniqueIterator& iterator) {
							iterator._parsingContext->GetUniformDelegateManager()->InvalidateUniforms();
							iterator._parsingContext->GetUniformDelegateManager()->BringUpToDateGraphics(*iterator._parsingContext);
							iterator._parsingContext->GetUniformDelegateManager()->BringUpToDateCompute(*iterator._parsingContext);
						});

					// Build hierarchical depths
					mainSequence.CreateStep_RunFragments(captures->_hierarchicalDepthsOperator->CreateFragment(stitchingContext._workingProps));

					// Light tiling & configure lighting descriptors
					mainSequence.CreateStep_RunFragments(captures->_lightScene->GetLightTiler().CreateInitFragment(stitchingContext._workingProps));
					mainSequence.CreateStep_RunFragments(captures->_lightScene->GetLightTiler().CreateFragment(stitchingContext._workingProps));

					// Calculate SSRs
					if (captures->_ssrOperator)
						mainSequence.CreateStep_RunFragments(captures->_ssrOperator->CreateFragment(stitchingContext._workingProps));

					mainSequence.CreateStep_CallFunction(
						[captures](LightingTechniqueIterator& iterator) {
							captures->ConfigureParsingContext(*iterator._parsingContext);
							captures->_lightScene->GetLightTiler().BarrierToReadingLayout(*iterator._threadContext);
						});

					// Draw main scene
					auto mainSceneFragmentRegistration = mainSequence.CreateStep_RunFragments(
						CreateForwardSceneFragment(captures, forwardIllumDelegate_DisableDepthWrite, captures->_ssrOperator!=nullptr, *balancedNoiseTexture, digest._dominantLightOperator, digest._dominantShadowOperator));

					// Post processing
					auto toneMapFragment = CreateToneMapFragment(
						[captures](LightingTechniqueIterator& iterator) {
							captures->DoToneMap(iterator);
						});
					mainSequence.CreateStep_RunFragments(std::move(toneMapFragment));

					mainSequence.CreateStep_CallFunction(
						[captures](LightingTechniqueIterator& iterator) {
							captures->ReleaseParsingContext(*iterator._parsingContext);	// almost need a "finally" step for this, because it may not be called on exception
						});

					lightingTechnique->CompleteConstruction(pipelineAccelerators, stitchingContext);

					// Any final operators that depend on the resolved frame buffer:
					auto resolvedFB = mainSequence.GetResolvedFrameBufferDesc(mainSceneFragmentRegistration);
					auto skyOpFuture = CreateSkyOperator(pipelinePool, Techniques::FrameBufferTarget{resolvedFB.first, resolvedFB.second}, SkyOperatorDesc { SkyTextureType::Equirectangular });
					::Assets::WhenAll(std::move(skyOpFuture)).ThenConstructToPromise(
						std::move(thatPromise),
						[captures, lightingTechnique](auto skyOp) {
							captures->_skyOperator = skyOp;
							captures->ConfigureSkyOperatorBindings();
							lightingTechnique->_depVal.RegisterDependency(skyOp->GetDependencyValidation());
							return lightingTechnique;
						});
				} CATCH(...) {
					thatPromise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	std::future<std::shared_ptr<CompiledLightingTechnique>> CreateForwardLightingTechnique(
		const std::shared_ptr<LightingEngineApparatus>& apparatus,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const ChainedOperatorDesc& globalOperators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachmentsInit,
		const FrameBufferProperties& fbProps)
	{
#if 0
		std::promise<std::shared_ptr<ILightScene>> lightScenePromise;
		auto lightSceneFuture = lightScenePromise.get_future();
		CreateForwardLightingScene(
			std::move(lightScenePromise),
			apparatus->_pipelineAccelerators, apparatus->_lightingOperatorCollection, apparatus->_sharedDelegates, 
			resolveOperators, shadowGenerators, ambientLightOperator);

		std::promise<std::shared_ptr<CompiledLightingTechnique>> promisedTechnique;
		auto result = promisedTechnique.get_future();
		std::vector<Techniques::PreregisteredAttachment> preregisteredAttachments { preregisteredAttachmentsInit.begin(), preregisteredAttachmentsInit.end() };
		::Assets::WhenAll(std::move(lightSceneFuture)).ThenConstructToPromise(
			std::move(promisedTechnique),
			[
				A=apparatus->_pipelineAccelerators, B=apparatus->_lightingOperatorCollection, C=apparatus->_sharedDelegates,
				preregisteredAttachments=std::move(preregisteredAttachments), fbProps
			](auto&& promise, auto lightSceneActual) {
				CreateForwardLightingTechnique(
					std::move(promise),
					A, B, C,
					lightSceneActual,
					*(const ChainedOperatorDesc*)nullptr,		// todo -- cannot be brought into the lamdba like this
					MakeIteratorRange(preregisteredAttachments), fbProps);
			});
		return result;
#endif

		std::promise<std::shared_ptr<CompiledLightingTechnique>> promisedTechnique;
		auto result = promisedTechnique.get_future();
		CreateForwardLightingTechnique(
			std::move(promisedTechnique),
			apparatus->_pipelineAccelerators, apparatus->_lightingOperatorCollection, apparatus->_sharedDelegates,
			resolveOperators, shadowGenerators, globalOperators,
			preregisteredAttachmentsInit, fbProps);
		return result;

	}

#if 0
	void CreateForwardLightingScene(
		std::promise<std::shared_ptr<ILightScene>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		IteratorRange<const LightSourceOperatorDesc*> positionalLightOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const AmbientLightOperatorDesc& ambientLightOperator)
	{
		RasterizationLightTileOperator::Configuration tilingConfig;
		std::promise<std::shared_ptr<ForwardPlusLightScene>> specialisedPromise;
		auto specializedFuture = specialisedPromise.get_future();
		ForwardPlusLightScene::ConstructToPromise(
			std::move(specialisedPromise), pipelineAccelerators, pipelinePool, techDelBox,
			positionalLightOperators, shadowGenerators, ambientLightOperator, tilingConfig);

		// transform from shared_ptr<ForwardPlusLightScene> -> shared_ptr<ILightScene>
		::Assets::WhenAll(std::move(specializedFuture)).ThenConstructToPromise(std::move(promise), [](auto ptr) { return std::static_pointer_cast<ILightScene>(std::move(ptr)); });
	}

	bool ForwardLightingTechniqueIsCompatible(
		CompiledLightingTechnique& technique,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const AmbientLightOperatorDesc& ambientLightOperator)
	{
		auto* lightScene = checked_cast<ForwardPlusLightScene*>(&technique.GetLightScene());
		return lightScene->IsCompatible(resolveOperators, shadowGenerators, ambientLightOperator);
	}
#endif

}}

