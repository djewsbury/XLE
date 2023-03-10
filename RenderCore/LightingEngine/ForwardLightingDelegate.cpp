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
#include "../Techniques/Services.h"
#include "../IDevice.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"
#include "../Utility/MemoryUtils.h"

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine
{

	constexpr uint64_t s_shadowTemplate = "ShadowTemplate"_h;
	constexpr uint64_t s_forwardLighting = "ForwardLighting"_h;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct OperatorDigest;

	class ForwardLightingCaptures : public std::enable_shared_from_this<ForwardLightingCaptures>
	{
	public:
		std::shared_ptr<ForwardPlusLightScene> _lightScene;
		std::shared_ptr<SkyOperator> _skyOperator;
		std::shared_ptr<HierarchicalDepthsOperator> _hierarchicalDepthsOperator;
		std::shared_ptr<ScreenSpaceReflectionsOperator> _ssrOperator;
		std::shared_ptr<SSAOOperator> _ssaoOperator;
		std::shared_ptr<ToneMapAcesOperator> _acesOperator;
		std::shared_ptr<CopyToneMapOperator> _copyToneMapOperator;
		std::shared_ptr<Techniques::SemiConstantDescriptorSet> _forwardLightingSemiConstant;
		std::shared_ptr<ISkyTextureProcessor> _skyTextureProcessor;

		void DoShadowPrepare(LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence);
		void ConfigureParsingContext(Techniques::ParsingContext& parsingContext);
		void ReleaseParsingContext(Techniques::ParsingContext& parsingContext);

		OnSkyTextureUpdateFn MakeOnSkyTextureUpdate();
		OnIBLUpdateFn MakeOnIBLUpdate();

		struct SecondStageConstructionOperators;
		auto ConstructMainSequence(
			CompiledLightingTechnique& lightingTechnique,
			std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
			const FrameBufferProperties& fbProps,
			const OperatorDigest& digest,
			Techniques::DeferredShaderResource& balancedNoiseTexture,
			std::shared_ptr<Techniques::ITechniqueDelegate> depthMotionNormalRoughnessDelegate,
			std::shared_ptr<Techniques::ITechniqueDelegate> depthMotionDelegate,
			std::shared_ptr<Techniques::ITechniqueDelegate> forwardIllumDelegate_DisableDepthWrite) -> std::shared_ptr<SecondStageConstructionOperators>;
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

	OnSkyTextureUpdateFn ForwardLightingCaptures::MakeOnSkyTextureUpdate()
	{
		if (_ssrOperator) {
			return 
				[weakSSROperator=std::weak_ptr<ScreenSpaceReflectionsOperator>(_ssrOperator)](auto texture, auto completion) {
					auto l=weakSSROperator.lock();
					if (l) {
						// Note -- this is getting the full sky texture (not the specular IBL prefiltered texture!)
						if (texture) {
							TextureViewDesc adjustedViewDesc;
							adjustedViewDesc._mipRange._min = 2;
							auto adjustedView = texture->GetResource()->CreateTextureView(BindFlag::ShaderResource, adjustedViewDesc);
							l->SetSpecularIBL(adjustedView);
						} else
							l->SetSpecularIBL(nullptr);
					}
				};
		}
		return {};
	}

	OnIBLUpdateFn ForwardLightingCaptures::MakeOnIBLUpdate()
	{
		return [this](std::shared_ptr<IResourceView> specularResource, BufferUploads::CommandListID completionCmdList, SHCoefficients& shCoefficients) {
			// Pass the updated SH coefficients into the light scene
			this->_lightScene->SetDiffuseSHCoefficients(shCoefficients);
			this->_lightScene->SetDistantSpecularIBL(std::move(specularResource), completionCmdList);
		};
	}

	static void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext)
	{
		UInt2 fbSize{stitchingContext._workingProps._width, stitchingContext._workingProps._height};
		auto samples = stitchingContext._workingProps._samples;
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::MultisampleDepth,
				CreateDesc(
					BindFlag::DepthStencil | BindFlag::ShaderResource | BindFlag::InputAttachment,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], stitchingContext.GetSystemAttachmentFormat(Techniques::SystemAttachmentFormat::MainDepthStencil), samples)),
				"main-depth"
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferNormal,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8B8A8_SNORM, samples)),
				"gbuffer-normal"
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferMotion,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8_SINT, samples)),
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
		result.AddSubpass(std::move(preDepthSubpass), depthMotionNormalDelegate, Techniques::BatchFlags::Opaque);
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
				if (_hasSSR) {
					dst[_resourceViewsStart] = context._rpi->GetNonFrameBufferAttachmentView(0).get();
					dst[_resourceViewsStart+1] = context._rpi->GetNonFrameBufferAttachmentView(1).get();
				} else {
					dst[_resourceViewsStart] = Techniques::Services::GetCommonResources()->_black2DSRV.get();
					dst[_resourceViewsStart+1] = Techniques::Services::GetCommonResources()->_black2DSRV.get();
				}
			}
			if (bindingFlags & (1ull<<uint64_t(_resourceViewsStart+2))) {
				if (_hasSSAO) {
					dst[_resourceViewsStart+2] = context._rpi->GetNonFrameBufferAttachmentView(2).get();
				} else {
					dst[_resourceViewsStart+2] = Techniques::Services::GetCommonResources()->_black2DSRV.get();
				}
			}
			if (bindingFlags & (1ull<<uint64_t(_resourceViewsStart+3)))
				dst[_resourceViewsStart+3] = _noise.get();
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

		MainSceneResourceDelegate(std::shared_ptr<Techniques::IShaderResourceDelegate> lightSceneDelegate, bool hasSSR, bool hasSSAO, Techniques::DeferredShaderResource& balanceNoiseTexture)
		: _lightSceneDelegate(std::move(lightSceneDelegate)), _hasSSR(hasSSR), _hasSSAO(hasSSAO)
		{
			_interface = _lightSceneDelegate->_interface;
			_resourceViewsStart = (unsigned)_interface.GetResourceViewBindings().size();

			_interface.BindResourceView(_resourceViewsStart+0, "SSR"_h);
			_interface.BindResourceView(_resourceViewsStart+1, "SSRConfidence"_h);
			_interface.BindResourceView(_resourceViewsStart+2, "SSAOTexture"_h);

			_noise = balanceNoiseTexture.GetShaderResource();
			_completionCmdList = balanceNoiseTexture.GetCompletionCommandList();
			_interface.BindResourceView(_resourceViewsStart+3, "NoiseTexture"_h);
		}

		std::shared_ptr<Techniques::IShaderResourceDelegate> _lightSceneDelegate;
		unsigned _resourceViewsStart = 0;
		std::shared_ptr<IResourceView> _noise;
		bool _hasSSR = false, _hasSSAO = false;
	};

	static RenderStepFragmentInterface CreateForwardSceneFragment(
		std::shared_ptr<ForwardLightingCaptures> captures,
		std::shared_ptr<Techniques::ITechniqueDelegate> forwardIllumDelegate,
		bool hasSSR, bool hasSSAO,
		bool hasDistantIBL,
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
		if (captures->_skyOperator) {
			result.AddSubpass(
				std::move(skySubpass),
				[weakCaptures = std::weak_ptr<ForwardLightingCaptures>{captures}](LightingTechniqueIterator& iterator) {
					auto l = weakCaptures.lock();
					if (l) l->_skyOperator->Execute(iterator);
				});
		} else {
			// This sends a message back to the client to draw the sky
			result.AddSkySubpass(std::move(skySubpass));
		}

		Techniques::FrameBufferDescFragment::SubpassDesc mainSubpass;
		mainSubpass.AppendOutput(lightResolve);
		mainSubpass.SetDepthStencil(depth);

		if (hasSSR) {
			mainSubpass.AppendNonFrameBufferAttachmentView(result.DefineAttachment("SSReflection"_h).InitialState(BindFlag::ShaderResource));
			mainSubpass.AppendNonFrameBufferAttachmentView(result.DefineAttachment("SSRConfidence"_h).InitialState(BindFlag::ShaderResource));
		}
		if (hasSSAO)
			mainSubpass.AppendNonFrameBufferAttachmentView(result.DefineAttachment("ao-output"_h).InitialState(BindFlag::ShaderResource));
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

		if (hasDistantIBL)
			box.SetParameter("SPECULAR_IBL", 1);

		if (hasSSR) box.SetParameter("SSR", 1);
		if (hasSSAO) box.SetParameter("SSAO", 1);

		auto resourceDelegate = std::make_shared<MainSceneResourceDelegate>(
			captures->_lightScene->CreateMainSceneResourceDelegate(),
			hasSSR, hasSSAO, balanceNoiseTexture);

		result.AddSubpass(
			std::move(mainSubpass), forwardIllumDelegate, Techniques::BatchFlags::Opaque|Techniques::BatchFlags::Blending, std::move(box),
			std::move(resourceDelegate));
		return result;
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
		std::optional<MultiSampleOperatorDesc> _msaa;
		std::optional<SkyTextureProcessorDesc> _skyTextureProcessor;
		std::optional<SkyOperatorDesc> _sky;

		ForwardPlusLightScene::ShadowPreparerIdMapping _shadowPrepreparerIdMapping;
		std::vector<ForwardPlusLightScene::LightOperatorInfo> _lightSceneOperatorInfo;
		RasterizationLightTileOperatorDesc _tilingConfig;

		std::optional<LightSourceOperatorDesc> _dominantLightOperator;
		std::optional<ShadowOperatorDesc> _dominantShadowOperator;

		OperatorDigest(
			IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
			IteratorRange<const ShadowOperatorDesc*> shadowOperators,
			const ChainedOperatorDesc* globalOperatorsChain)
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

			bool gotTiledLightingConfig = false;
			auto* chain = globalOperatorsChain;
			while (chain) {
				switch(chain->_structureType) {
				case TypeHashCode<ScreenSpaceReflectionsOperatorDesc>:
					if (_ssr)
						Throw(std::runtime_error("Multiple SSR operators found, where only one expected"));
					_ssr = Internal::ChainedOperatorCast<ScreenSpaceReflectionsOperatorDesc>(*chain);
					break;

				case TypeHashCode<AmbientOcclusionOperatorDesc>:
					if (_ssao)
						Throw(std::runtime_error("Multiple SSAO operators found, where only one expected"));
					_ssao = Internal::ChainedOperatorCast<AmbientOcclusionOperatorDesc>(*chain);
					break;

				case TypeHashCode<ToneMapAcesOperatorDesc>:
					if (_tonemapAces)
						Throw(std::runtime_error("Multiple tonemap operators found, where only one expected"));
					_tonemapAces = Internal::ChainedOperatorCast<ToneMapAcesOperatorDesc>(*chain);
					break;

				case TypeHashCode<MultiSampleOperatorDesc>:
					if (_msaa)
						Throw(std::runtime_error("Multiple antialiasing operators found, where only one expected"));
					_msaa = Internal::ChainedOperatorCast<MultiSampleOperatorDesc>(*chain);
					break;

				case TypeHashCode<SkyOperatorDesc>:
					if (_sky)
						Throw(std::runtime_error("Multiple sky operators found, where only one expected"));
					_sky = Internal::ChainedOperatorCast<SkyOperatorDesc>(*chain);
					break;

				case TypeHashCode<SkyTextureProcessorDesc>:
					if (_skyTextureProcessor)
						Throw(std::runtime_error("Multiple sky operators found, where only one expected"));
					_skyTextureProcessor = Internal::ChainedOperatorCast<SkyTextureProcessorDesc>(*chain);
					break;

				case TypeHashCode<RasterizationLightTileOperatorDesc>:
					if (gotTiledLightingConfig)
						Throw(std::runtime_error("Multiple tiled lighting operators found, where only one expected"));
					_tilingConfig = Internal::ChainedOperatorCast<RasterizationLightTileOperatorDesc>(*chain);
					gotTiledLightingConfig = false;
					break;
				}
				chain = chain->_next;
			}
		}
	};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct ForwardLightingCaptures::SecondStageConstructionOperators
	{
		std::future<std::shared_ptr<HierarchicalDepthsOperator>> _futureHierarchicalDepths;
		std::future<std::shared_ptr<ScreenSpaceReflectionsOperator>> _futureSSR;
		std::future<std::shared_ptr<SSAOOperator>> _futureSSAO;
		std::future<std::shared_ptr<ToneMapAcesOperator>> _futureAces;
		std::future<std::shared_ptr<CopyToneMapOperator>> _futureCopyToneMap;
		std::future<std::shared_ptr<SkyOperator>> _futureSky;
	};

	std::shared_ptr<ForwardLightingCaptures::SecondStageConstructionOperators> ForwardLightingCaptures::ConstructMainSequence(
		CompiledLightingTechnique& lightingTechnique,
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const FrameBufferProperties& fbProps,
		const OperatorDigest& digest,
		Techniques::DeferredShaderResource& balancedNoiseTexture,
		std::shared_ptr<Techniques::ITechniqueDelegate> depthMotionNormalRoughnessDelegate,
		std::shared_ptr<Techniques::ITechniqueDelegate> depthMotionDelegate,
		std::shared_ptr<Techniques::ITechniqueDelegate> forwardIllumDelegate_DisableDepthWrite)
	{
		Techniques::FragmentStitchingContext stitchingContext { preregisteredAttachments, fbProps, Techniques::CalculateDefaultSystemFormats(*pipelineAccelerators->GetDevice()) };
		PreregisterAttachments(stitchingContext);
		_hierarchicalDepthsOperator->PreregisterAttachments(stitchingContext);
		_lightScene->GetLightTiler().PreregisterAttachments(stitchingContext);
		if (_acesOperator) _acesOperator->PreregisterAttachments(stitchingContext);
		if (_copyToneMapOperator) _copyToneMapOperator->PreregisterAttachments(stitchingContext);
		if (_ssrOperator) _ssrOperator->PreregisterAttachments(stitchingContext);
		if (_ssaoOperator) _ssaoOperator->PreregisterAttachments(stitchingContext);

		auto& mainSequence = lightingTechnique.CreateSequence();
		mainSequence.CreateStep_CallFunction(
			[](LightingTechniqueIterator& iterator) {
				if (iterator._deformAcceleratorPool)
					iterator._deformAcceleratorPool->SetVertexInputBarrier(*iterator._threadContext);
			});

		mainSequence.CreateStep_InvalidateUniforms();
		mainSequence.CreateStep_BringUpToDateUniforms();

		// Pre depth
		if (_ssrOperator) {
			mainSequence.CreateStep_RunFragments(CreateDepthMotionNormalFragment(depthMotionNormalRoughnessDelegate));
		} else {
			mainSequence.CreateStep_RunFragments(CreateDepthMotionFragment(depthMotionDelegate));
		}

		// Build hierarchical depths
		auto hierachicalDepthsReg = mainSequence.CreateStep_RunFragments(_hierarchicalDepthsOperator->CreateFragment(stitchingContext._workingProps));

		// Light tiling & configure lighting descriptors
		mainSequence.CreateStep_RunFragments(_lightScene->GetLightTiler().CreateInitFragment(stitchingContext._workingProps));
		mainSequence.CreateStep_RunFragments(_lightScene->GetLightTiler().CreateFragment(stitchingContext._workingProps));
		mainSequence.ResolvePendingCreateFragmentSteps();

		// Calculate SSR & SSAO
		LightingTechniqueSequence::FragmentInterfaceRegistration ssrFragmentReg, ssaoFragmentReg;
		if (_ssrOperator)
			ssrFragmentReg = mainSequence.CreateStep_RunFragments(_ssrOperator->CreateFragment(stitchingContext._workingProps));
		if (_ssaoOperator)
			ssaoFragmentReg = mainSequence.CreateStep_RunFragments(_ssaoOperator->CreateFragment(stitchingContext._workingProps));

		mainSequence.CreateStep_CallFunction(
			[captures=shared_from_this()](LightingTechniqueIterator& iterator) {
				captures->ConfigureParsingContext(*iterator._parsingContext);
				captures->_lightScene->GetLightTiler().BarrierToReadingLayout(*iterator._threadContext);
			});

		// Draw main scene
		auto mainSceneFragmentRegistration = mainSequence.CreateStep_RunFragments(
			CreateForwardSceneFragment(
				shared_from_this(), forwardIllumDelegate_DisableDepthWrite,
				_ssrOperator!=nullptr, _ssaoOperator!=nullptr, digest._skyTextureProcessor.has_value(),
				balancedNoiseTexture, digest._dominantLightOperator, digest._dominantShadowOperator));

		// simplify uniforms before going into post processing steps
		mainSequence.CreateStep_CallFunction(
			[captures=shared_from_this()](LightingTechniqueIterator& iterator) {
				captures->ReleaseParsingContext(*iterator._parsingContext);	// almost need a "finally" step for this, because it may not be called on exception
			});
		mainSequence.CreateStep_BringUpToDateUniforms();

		// Post processing
		LightingTechniqueSequence::FragmentInterfaceRegistration toneMapReg;
		if (_acesOperator) {
			toneMapReg = mainSequence.CreateStep_RunFragments(_acesOperator->CreateFragment(stitchingContext._workingProps));
		} else {
			assert(_copyToneMapOperator);
			toneMapReg = mainSequence.CreateStep_RunFragments(_copyToneMapOperator->CreateFragment(stitchingContext._workingProps));
		}

		lightingTechnique.CompleteConstruction(std::move(pipelineAccelerators), stitchingContext);

		// Some operators requires a second stage construction, which must be done after we've finalized the frame buffer desc(s)
		// This is because low level graphics pipelines used the FrameBufferDesc as a construction parameter
		// It also gives the operator full context about how it's going to be used
		auto ops = std::make_shared<SecondStageConstructionOperators>();
		if (_skyOperator)
			ops->_futureSky = Internal::SecondStageConstruction(*_skyOperator, Internal::AsFrameBufferTarget(mainSequence, mainSceneFragmentRegistration));
		if (_ssrOperator)
			ops->_futureSSR = Internal::SecondStageConstruction(*_ssrOperator, Internal::AsFrameBufferTarget(mainSequence, ssrFragmentReg));
		if (_ssaoOperator)
			ops->_futureSSAO = Internal::SecondStageConstruction(*_ssaoOperator, Internal::AsFrameBufferTarget(mainSequence, ssaoFragmentReg));
		ops->_futureHierarchicalDepths = Internal::SecondStageConstruction(*_hierarchicalDepthsOperator, Internal::AsFrameBufferTarget(mainSequence, hierachicalDepthsReg));
		if (_acesOperator)
			ops->_futureAces = Internal::SecondStageConstruction(*_acesOperator, Internal::AsFrameBufferTarget(mainSequence, toneMapReg));
		if (_copyToneMapOperator)
			ops->_futureCopyToneMap = Internal::SecondStageConstruction(*_copyToneMapOperator, Internal::AsFrameBufferTarget(mainSequence, toneMapReg));
		return ops;
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void CreateForwardLightingTechnique(
		std::promise<std::shared_ptr<CompiledLightingTechnique>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowOperators,
		const ChainedOperatorDesc* globalOperators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachmentsInit)
	{
		struct ConstructionHelper
		{
			// main technique delegates
			SharedTechniqueDelegateBox::TechniqueDelegateFuture _depthMotionNormalRoughnessDelegate;
			SharedTechniqueDelegateBox::TechniqueDelegateFuture _depthMotionDelegate;
			SharedTechniqueDelegateBox::TechniqueDelegateFuture _forwardIllumDelegate;

			std::shared_future<std::shared_ptr<Techniques::DeferredShaderResource>> _balancedNoiseTexture;

			std::future<std::shared_ptr<ForwardPlusLightScene>> _lightSceneFuture;
		};
		auto helper = std::make_shared<ConstructionHelper>();

		helper->_balancedNoiseTexture = ::Assets::MakeAssetPtr<Techniques::DeferredShaderResource>(BALANCED_NOISE_TEXTURE);

		OperatorDigest digest { resolveOperators, shadowOperators, globalOperators };

		ForwardPlusLightScene::IntegrationParams lightSceneIntegrationParams;
		lightSceneIntegrationParams._specularIBLEnabled = digest._skyTextureProcessor.has_value();
		helper->_lightSceneFuture = ::Assets::ConstructToFuturePtr<ForwardPlusLightScene>(
			ForwardPlusLightScene::ConstructionServices{pipelineAccelerators, pipelinePool, techDelBox},
			std::move(digest._shadowPrepreparerIdMapping), std::move(digest._lightSceneOperatorInfo),
			digest._tilingConfig, lightSceneIntegrationParams);

		helper->_depthMotionNormalRoughnessDelegate = techDelBox->GetDepthMotionNormalRoughnessDelegate();
		helper->_depthMotionDelegate = techDelBox->GetDepthMotionDelegate();
		helper->_forwardIllumDelegate = techDelBox->GetForwardIllumDelegate_DisableDepthWrite();

		auto resolution = Internal::ExtractOutputResolution(preregisteredAttachmentsInit);

		std::vector<Techniques::PreregisteredAttachment> preregisteredAttachments { preregisteredAttachmentsInit.begin(), preregisteredAttachmentsInit.end() };

		::Assets::PollToPromise(
			std::move(promise),
			[helper](auto timeout) {
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				if (Internal::MarkerTimesOut(helper->_lightSceneFuture, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (Internal::MarkerTimesOut(helper->_depthMotionNormalRoughnessDelegate, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (Internal::MarkerTimesOut(helper->_depthMotionDelegate, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (Internal::MarkerTimesOut(helper->_forwardIllumDelegate, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (Internal::MarkerTimesOut(helper->_balancedNoiseTexture, timeoutTime)) return ::Assets::PollStatus::Continue;
				return ::Assets::PollStatus::Finish;
			},
			[helper, techDelBox, pipelineAccelerators, pipelinePool, preregisteredAttachments=std::move(preregisteredAttachments), resolution, digest=std::move(digest)]
			(std::promise<std::shared_ptr<CompiledLightingTechnique>>&& thatPromise) {

				TRY {
					auto captures = std::make_shared<ForwardLightingCaptures>();
					captures->_lightScene = helper->_lightSceneFuture.get();

					captures->_forwardLightingSemiConstant = Techniques::CreateSemiConstantDescriptorSet(
						*techDelBox->_forwardLightingDescSetTemplate, "ForwardLighting", PipelineType::Graphics, *pipelinePool->GetDevice());

					// operators
					auto msaaSamples = digest._msaa ? digest._msaa->_samples : TextureSamples::Create(); 
					captures->_hierarchicalDepthsOperator = std::make_shared<HierarchicalDepthsOperator>(pipelinePool);
					if (digest._sky)
						captures->_skyOperator = std::make_shared<SkyOperator>(pipelinePool, *digest._sky);
					if (digest._ssr)
						captures->_ssrOperator = std::make_shared<ScreenSpaceReflectionsOperator>(pipelinePool, *digest._ssr, ScreenSpaceReflectionsOperator::IntegrationParams{digest._skyTextureProcessor.has_value()});
					if (digest._ssao)
						captures->_ssaoOperator = std::make_shared<SSAOOperator>(pipelinePool, *digest._ssao, SSAOOperator::IntegrationParams{true});
					if (digest._tonemapAces) {
						captures->_acesOperator = std::make_shared<ToneMapAcesOperator>(pipelinePool, *digest._tonemapAces);
					} else {
						captures->_copyToneMapOperator = std::make_shared<CopyToneMapOperator>(pipelinePool);
					}

					if (digest._skyTextureProcessor) {
						captures->_skyTextureProcessor = CreateSkyTextureProcessor(
							*digest._skyTextureProcessor, captures->_skyOperator,
							captures->MakeOnSkyTextureUpdate(),
							captures->MakeOnIBLUpdate());
					}

					auto depthMotionNormalRoughnessDelegate = helper->_depthMotionNormalRoughnessDelegate.get();
					auto depthMotionDelegate = helper->_depthMotionDelegate.get();
					auto forwardIllumDelegate_DisableDepthWrite = helper->_forwardIllumDelegate.get();
					auto balancedNoiseTexture = helper->_balancedNoiseTexture.get();

					auto lightingTechnique = std::make_shared<CompiledLightingTechnique>(captures->_lightScene);
					lightingTechnique->_depVal = ::Assets::GetDepValSys().Make();
					lightingTechnique->_depVal.RegisterDependency(captures->_lightScene->GetDependencyValidation());
					lightingTechnique->_depVal.RegisterDependency(depthMotionNormalRoughnessDelegate->GetDependencyValidation());
					lightingTechnique->_depVal.RegisterDependency(depthMotionDelegate->GetDependencyValidation());
					lightingTechnique->_depVal.RegisterDependency(forwardIllumDelegate_DisableDepthWrite->GetDependencyValidation());
					lightingTechnique->_depVal.RegisterDependency(balancedNoiseTexture->GetDependencyValidation());
					captures->_lightScene->_queryInterfaceHelper = lightingTechnique->_queryInterfaceHelper =
						[captures=captures.get()](uint64_t typeCode) -> void* {
							switch (typeCode) {
							case TypeHashCode<IBloom>:
								return (IBloom*)captures->_acesOperator.get();
							case TypeHashCode<IExposure>:
								return (IExposure*)captures->_acesOperator.get();
							case TypeHashCode<ISkyTextureProcessor>:
								return (ISkyTextureProcessor*)captures->_skyTextureProcessor.get();
							case TypeHashCode<ISSAmbientOcclusion>:
								return (ISSAmbientOcclusion*)captures->_ssaoOperator.get();
							}
							return nullptr;
						};

					// Prepare shadows sequence
					lightingTechnique->CreateDynamicSequence(
						[captures](LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence) {
							captures->DoShadowPrepare(iterator, sequence);
							captures->_lightScene->Prerender(*iterator._threadContext);
							if (captures->_skyTextureProcessor)
								SkyTextureProcessorPrerender(*captures->_skyTextureProcessor);
						});

					// main sequence & setup second stage construction
					FrameBufferProperties fbProps { resolution[0], resolution[1], msaaSamples };
					auto secondStageHelper = captures->ConstructMainSequence(
						*lightingTechnique,
						pipelineAccelerators,
						preregisteredAttachments, fbProps, digest, *balancedNoiseTexture,
						depthMotionNormalRoughnessDelegate, depthMotionDelegate, forwardIllumDelegate_DisableDepthWrite);

					::Assets::PollToPromise(
						std::move(thatPromise),
						[secondStageHelper](auto timeout) {
							auto timeoutTime = std::chrono::steady_clock::now() + timeout;
							if (secondStageHelper->_futureHierarchicalDepths.valid() && Internal::MarkerTimesOut(secondStageHelper->_futureHierarchicalDepths, timeoutTime)) return ::Assets::PollStatus::Continue;
							if (secondStageHelper->_futureSSR.valid() && Internal::MarkerTimesOut(secondStageHelper->_futureSSR, timeoutTime)) return ::Assets::PollStatus::Continue;
							if (secondStageHelper->_futureSSAO.valid() && Internal::MarkerTimesOut(secondStageHelper->_futureSSAO, timeoutTime)) return ::Assets::PollStatus::Continue;
							if (secondStageHelper->_futureAces.valid() && Internal::MarkerTimesOut(secondStageHelper->_futureAces, timeoutTime)) return ::Assets::PollStatus::Continue;
							if (secondStageHelper->_futureCopyToneMap.valid() && Internal::MarkerTimesOut(secondStageHelper->_futureCopyToneMap, timeoutTime)) return ::Assets::PollStatus::Continue;
							if (secondStageHelper->_futureSky.valid() && Internal::MarkerTimesOut(secondStageHelper->_futureSky, timeoutTime)) return ::Assets::PollStatus::Continue;
							return ::Assets::PollStatus::Finish;
						},
						[secondStageHelper, lightingTechnique, captures]() {
							// Shake out any exceptions
							secondStageHelper->_futureHierarchicalDepths.get();
							if (secondStageHelper->_futureSSR.valid()) secondStageHelper->_futureSSR.get();
							if (secondStageHelper->_futureSSAO.valid()) secondStageHelper->_futureSSAO.get();
							if (secondStageHelper->_futureAces.valid()) secondStageHelper->_futureAces.get();
							if (secondStageHelper->_futureCopyToneMap.valid()) secondStageHelper->_futureCopyToneMap.get();
							if (secondStageHelper->_futureSky.valid()) secondStageHelper->_futureSky.get();

							// register dep vals for operators after we've done their second-stage-construction 
							lightingTechnique->_depVal.RegisterDependency(captures->_hierarchicalDepthsOperator->GetDependencyValidation());
							if (captures->_ssrOperator)
								lightingTechnique->_depVal.RegisterDependency(captures->_ssrOperator->GetDependencyValidation());
							if (captures->_ssaoOperator)
								lightingTechnique->_depVal.RegisterDependency(captures->_ssaoOperator->GetDependencyValidation());
							if (captures->_acesOperator)
								lightingTechnique->_depVal.RegisterDependency(captures->_acesOperator->GetDependencyValidation());
							if (captures->_copyToneMapOperator)
								lightingTechnique->_depVal.RegisterDependency(captures->_copyToneMapOperator->GetDependencyValidation());
							if (captures->_skyOperator)
								lightingTechnique->_depVal.RegisterDependency(captures->_skyOperator->GetDependencyValidation());

							// Everything finally finished
							return lightingTechnique;
						});
				} CATCH(...) {
					thatPromise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	void CreateForwardPlusLightScene(
		std::promise<std::shared_ptr<ILightScene>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowOperators,
		const ChainedOperatorDesc* globalOperators)
	{
		OperatorDigest digest { resolveOperators, shadowOperators, globalOperators };

		std::promise<std::shared_ptr<ForwardPlusLightScene>> specializedPromise;
		auto specializedFuture = specializedPromise.get_future();
		ForwardPlusLightScene::IntegrationParams integrationParams;
		integrationParams._specularIBLEnabled = digest._skyTextureProcessor.has_value();
		ForwardPlusLightScene::ConstructToPromise(
			std::move(specializedPromise),
			ForwardPlusLightScene::ConstructionServices{pipelineAccelerators, pipelinePool, techDelBox},
			std::move(digest._shadowPrepreparerIdMapping), std::move(digest._lightSceneOperatorInfo),
			digest._tilingConfig, integrationParams);

		// awkwardly convert promise types
		::Assets::WhenAll(std::move(specializedFuture)).ThenConstructToPromise(
			std::move(promise),
			[](auto&& input) { return std::move(input); });
	}

	bool ForwardLightingTechniqueIsCompatible(
		CompiledLightingTechnique& technique,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const ChainedOperatorDesc* globalOperators)
	{
		assert(0);	// todo -- update for chained technique operators
#if 0
		auto* lightScene = checked_cast<ForwardPlusLightScene*>(&technique.GetLightScene());
		return lightScene->IsCompatible(resolveOperators, shadowGenerators, ambientLightOperator);
#else
		return false;
#endif
	}

}}

