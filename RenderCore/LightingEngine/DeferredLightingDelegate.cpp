// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeferredLightingDelegate.h"
#include "DeferredLightingResolve.h"
#include "LightingEngineIterator.h"
#include "LightingEngineInitialization.h"
#include "LightingEngineApparatus.h"
#include "LightUniforms.h"
#include "ShadowPreparer.h"
#include "RenderStepFragments.h"
#include "ILightScene.h"
#include "StandardLightScene.h"
#include "StandardLightOperators.h"
#include "LightingDelegateUtil.h"
#include "ShadowProbes.h"
#include "SkyOperator.h"
#include "ToneMapOperator.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/PipelineCollection.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/Drawables.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/Techniques.h"
#include "../Techniques/PipelineAccelerator.h"
#include "../Techniques/DeformAccelerator.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../UniformsStream.h"
#include "../../Assets/Assets.h"
#include "../../Utility/MemoryUtils.h"
#include "../../xleres/FileList.h"

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine
{
	class DeferredLightScene : public Internal::StandardLightScene
	{
	public:
		std::shared_ptr<LightResolveOperators> _lightResolveOperators;
		std::shared_ptr<DynamicShadowPreparers> _shadowPreparers;
		std::shared_ptr<Internal::DynamicShadowProjectionScheduler> _shadowScheduler;
		bool _ambientLightEnabled = false;

		struct ShadowPreparerIdMapping
		{
			std::vector<unsigned> _operatorToShadowPreparerId;
			unsigned _operatorForStaticProbes = ~0u;
			ShadowProbes::Configuration _shadowProbesCfg;
		};

		ShadowPreparerIdMapping _shadowOperatorIdMapping;
		std::shared_ptr<ShadowProbes> _shadowProbes;
		std::shared_ptr<Internal::SemiStaticShadowProbeScheduler> _shadowProbesManager;

		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<SharedTechniqueDelegateBox> _techDelBox;

		std::function<void*(uint64_t)> _queryInterfaceHelper;

		void FinalizeConfiguration()
		{
			if (_shadowOperatorIdMapping._operatorForStaticProbes != ~0u) {
				_shadowProbes = std::make_shared<ShadowProbes>(_pipelineAccelerators, *_techDelBox, _shadowOperatorIdMapping._shadowProbesCfg);
				_shadowProbesManager = std::make_shared<Internal::SemiStaticShadowProbeScheduler>(_shadowProbes, _shadowOperatorIdMapping._operatorForStaticProbes);
				RegisterComponent(_shadowProbesManager);
			}
			
			if (!_shadowOperatorIdMapping._operatorToShadowPreparerId.empty()) {
				_shadowScheduler = std::make_shared<Internal::DynamicShadowProjectionScheduler>(
					_pipelineAccelerators->GetDevice(), _shadowPreparers,
					_shadowOperatorIdMapping._operatorToShadowPreparerId);
				_shadowScheduler->SetDescriptorSetLayout(_techDelBox->_dmShadowDescSetTemplate, PipelineType::Graphics);
				RegisterComponent(_shadowScheduler);
			}
		}

		ILightScene::LightSourceId CreateAmbientLightSource() override
		{
			if (_ambientLightEnabled)
				Throw(std::runtime_error("Attempting to create multiple ambient light sources. Only one is supported at a time"));
			_ambientLightEnabled = true;
			return 0;
		}

		void DestroyLightSource(LightSourceId sourceId) override
		{
			if (sourceId == 0) {
				if (!_ambientLightEnabled)
					Throw(std::runtime_error("Attempting to destroy the ambient light source, but it has not been created"));
				_ambientLightEnabled = false;
			} else {
				Internal::StandardLightScene::DestroyLightSource(sourceId);
			}
		}

		void Clear() override
		{
			_ambientLightEnabled = false;
			Internal::StandardLightScene::Clear();
		}

		void* TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode) override
		{
			if (sourceId == 0) {
				switch (interfaceTypeCode) {
				case TypeHashCode<ISkyTextureProcessor>:
					return _queryInterfaceHelper(interfaceTypeCode);	// for the ambient light, get the global ISkyTextureProcessor
				default: return nullptr;
				}
			} else {
				return Internal::StandardLightScene::TryGetLightSourceInterface(sourceId, interfaceTypeCode);
			}
		}

		void* QueryInterface(uint64_t typeCode) override
		{
			switch (typeCode) {
			case TypeHashCode<ISemiStaticShadowProbeScheduler>:
				return (ISemiStaticShadowProbeScheduler*)_shadowProbesManager.get();
			case TypeHashCode<IDynamicShadowProjectionScheduler>:
				return (IDynamicShadowProjectionScheduler*)_shadowScheduler.get();
			default:
				if (_queryInterfaceHelper)
					if (auto* result = _queryInterfaceHelper(typeCode))
						return result;
				return StandardLightScene::QueryInterface(typeCode);
			}
		}

		DeferredLightScene()
		{
			// We'll maintain the first few ids for system lights (ambient surrounds, etc)
			ReserveLightSourceIds(32);
		}
	};

	class DeferredLightingCaptures
	{
	public:
		std::shared_ptr<LightResolveOperators> _lightResolveOperators;
		std::shared_ptr<DeferredLightScene> _lightScene;
		std::shared_ptr<Techniques::PipelineCollection> _pipelineCollection;
		std::shared_ptr<ICompiledPipelineLayout> _lightingOperatorLayout;
		std::shared_ptr<ToneMapAcesOperator> _acesOperator;
		std::shared_ptr<CopyToneMapOperator> _copyToneMapOperator;
		std::shared_ptr<SkyOperator> _skyOperator;
		std::shared_ptr<ISkyTextureProcessor> _skyTextureProcessor;

		RenderStepFragmentInterface CreateLightingResolveFragment(bool precisionTargets = false);

		void DoShadowPrepare(LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence);
		void DoLightResolve(LightingTechniqueIterator& iterator);
		void GenerateDebuggingOutputs(LightingTechniqueIterator& iterator);
		OnSkyTextureUpdateFn MakeOnSkyTextureUpdate() { return {}; }
		OnIBLUpdateFn MakeOnIBLUpdate() { return {}; }
	};

	class BuildGBufferResourceDelegate : public Techniques::IShaderResourceDelegate
	{
	public:
        virtual void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst)
		{
			assert(bindingFlags == 1<<0);
			dst[0] = _normalsFitting.get();
			context.RequireCommandList(_completionCmdList);
		}

		BuildGBufferResourceDelegate(Techniques::DeferredShaderResource& normalsFittingResource)
		{
			BindResourceView(0, "NormalsFittingTexture"_h);
			_normalsFitting = normalsFittingResource.GetShaderResource();
			_completionCmdList = normalsFittingResource.GetCompletionCommandList();
		}
		std::shared_ptr<IResourceView> _normalsFitting;
		BufferUploads::CommandListID _completionCmdList;
	};

	static std::future<std::pair<RenderStepFragmentInterface, BufferUploads::CommandListID>> CreateBuildGBufferSceneFragment(
		SharedTechniqueDelegateBox& techDelBox,
		GBufferType gbufferType, 
		bool precisionTargets = false)
	{
		std::promise<std::pair<RenderStepFragmentInterface, BufferUploads::CommandListID>> promise;
		auto result = promise.get_future();
		auto normalsFittingTexture = ::Assets::MakeAssetPtr<Techniques::DeferredShaderResource>(NORMALS_FITTING_TEXTURE);

		::Assets::WhenAll(normalsFittingTexture, techDelBox.GetDeferredIllumDelegate()).ThenConstructToPromise(
			std::move(promise),
			[gbufferType, precisionTargets](auto normalsFitting, auto defIllumDel) {

				// This render pass will include just rendering to the gbuffer and doing the initial
				// lighting resolve.
				//
				// Typically after this we have a number of smaller render passes (such as rendering
				// transparent geometry, performing post processing, MSAA resolve, tone mapping, etc)
				//
				// We could attempt to combine more steps into this one render pass.. But it might become
				// awkward. For example, if we know we have only simple translucent geometry, we could
				// add in a subpass for rendering that geometry.
				//
				// We can elect to retain or discard the gbuffer contents after the lighting resolve. Frequently
				// the gbuffer contents are useful for various effects.

				RenderStepFragmentInterface createGBuffer{RenderCore::PipelineType::Graphics};
				auto msDepth = createGBuffer.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).Clear().FinalState(BindFlag::DepthStencil|BindFlag::ShaderResource);
				auto diffuse = createGBuffer.DefineAttachment(Techniques::AttachmentSemantics::GBufferDiffuse).NoInitialState();
				auto normal = createGBuffer.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal).NoInitialState();
				auto parameter = createGBuffer.DefineAttachment(Techniques::AttachmentSemantics::GBufferParameter).NoInitialState();

				Techniques::FrameBufferDescFragment::SubpassDesc subpass;
				auto diffuseAspect = (!precisionTargets) ? TextureViewDesc::Aspect::ColorSRGB : TextureViewDesc::Aspect::ColorLinear;
				subpass.AppendOutput(diffuse, {diffuseAspect});
				subpass.AppendOutput(normal);
				if (gbufferType == GBufferType::PositionNormalParameters)
					subpass.AppendOutput(parameter);
				subpass.SetDepthStencil(msDepth);
				subpass.SetName("write-gbuffer");

				auto srDelegate = std::make_shared<BuildGBufferResourceDelegate>(*normalsFitting);

				ParameterBox box;
				box.SetParameter("GBUFFER_TYPE", (unsigned)gbufferType);
				box.SetParameter("FRONT_STENCIL_REF", 0xff);
				box.SetParameter("BACK_STENCIL_REF", 0xff);
				createGBuffer.AddSubpass(std::move(subpass), std::move(defIllumDel), Techniques::BatchFlags::Opaque, std::move(box), std::move(srDelegate));
				return std::make_pair(std::move(createGBuffer), normalsFitting->GetCompletionCommandList());
			});
		return result;
	}

	RenderStepFragmentInterface DeferredLightingCaptures::CreateLightingResolveFragment(bool precisionTargets)
	{
		RenderStepFragmentInterface fragment { RenderCore::PipelineType::Graphics };
		auto depthTarget = fragment.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).InitialState(LoadStore::Retain_StencilClear).FinalState(BindFlag::DepthStencil);
		auto lightResolveTarget = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).Clear().Discard();

		TextureViewDesc justStencilWindow {
			TextureViewDesc::Aspect::Stencil,
			TextureViewDesc::All, TextureViewDesc::All,
			TextureDesc::Dimensionality::Undefined,
			TextureViewDesc::Flags::SimultaneouslyDepthReadOnly};

		TextureViewDesc justDepthWindow {
			TextureViewDesc::Aspect::Depth,
			TextureViewDesc::All, TextureViewDesc::All,
			TextureDesc::Dimensionality::Undefined,
			TextureViewDesc::Flags::SimultaneouslyStencilAttachment};

		Techniques::FrameBufferDescFragment::SubpassDesc subpasses[2];
		subpasses[0].AppendOutput(lightResolveTarget);
		subpasses[0].SetDepthStencil(depthTarget);
		subpasses[0].SetName("sky");

		subpasses[1].AppendOutput(lightResolveTarget);
		subpasses[1].SetDepthStencil(depthTarget, justStencilWindow);

		auto gbufferStore = LoadStore::Retain;	// (technically only need retain when we're going to use these for debugging)
		auto diffuseAspect = (!precisionTargets) ? TextureViewDesc::Aspect::ColorSRGB : TextureViewDesc::Aspect::ColorLinear;
		subpasses[1].AppendInput(fragment.DefineAttachment(Techniques::AttachmentSemantics::GBufferDiffuse).InitialState(gbufferStore), {diffuseAspect});
		subpasses[1].AppendInput(fragment.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal).InitialState(gbufferStore));
		subpasses[1].AppendInput(fragment.DefineAttachment(Techniques::AttachmentSemantics::GBufferParameter).InitialState(gbufferStore));
		subpasses[1].AppendInput(depthTarget, justDepthWindow);
		subpasses[1].SetName("light-resolve");

		if (_skyOperator) {
			fragment.AddSubpass(
				std::move(subpasses[0]),
				[this](LightingTechniqueIterator& iterator) {
					iterator._parsingContext->GetUniformDelegateManager()->BringUpToDateGraphics(*iterator._parsingContext);
					this->_skyOperator->Execute(iterator);
				});
		} else {
			fragment.AddSkySubpass(std::move(subpasses[0]));
		}
		fragment.AddSubpass(
			std::move(subpasses[1]), 
			[this](LightingTechniqueIterator& iterator) {
				this->DoLightResolve(iterator);
			});
		return fragment;
	}
	
	void DeferredLightingCaptures::DoShadowPrepare(LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence)
	{
		if (_lightScene->_shadowScheduler)
			_lightScene->_shadowScheduler->DoShadowPrepare(iterator, sequence);
	}

	void DeferredLightingCaptures::DoLightResolve(LightingTechniqueIterator& iterator)
	{
		// Light subpass
		auto* shadowProbes = (_lightScene->_shadowProbesManager && _lightScene->_shadowProbesManager->DoneInitialBackgroundPrepare()) ? _lightScene->_shadowProbes.get() : nullptr;
		ResolveLights(
			*iterator._threadContext, *iterator._parsingContext, iterator._rpi,
			*_lightResolveOperators, *_lightScene,
			_lightScene->_shadowScheduler.get(), shadowProbes, _lightScene->_shadowProbesManager.get());
	}

	static void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, GBufferType gbufferType, bool precisionTargets = false)
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
				// Generally the deferred pixel shader will just copy information from the albedo
				// texture into the first deferred buffer. So the first deferred buffer should
				// have the same pixel format as much input textures.
				// Usually this is an 8 bit SRGB format, so the first deferred buffer should also
				// be 8 bit SRGB. So long as we don't do a lot of processing in the deferred pixel shader
				// that should be enough precision.
				//      .. however, it possible some clients might prefer 10 or 16 bit albedo textures
				//      In these cases, the first buffer should be a matching format.
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferDiffuse,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource | BindFlag::InputAttachment,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], (!precisionTargets) ? Format::R8G8B8A8_UNORM_SRGB : Format::R32G32B32A32_FLOAT)),
				"gbuffer-diffuse"
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferNormal,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource | BindFlag::InputAttachment,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8B8A8_SNORM)),
				"gbuffer-normals"
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferParameter,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource | BindFlag::InputAttachment,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8B8A8_UNORM)),
				"gbuffer-parameter"
			}
		};
		for (const auto& a:attachments)
			stitchingContext.DefineAttachment(a);
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

	static void BeginLightSceneConstruction(
		std::promise<std::shared_ptr<DeferredLightScene>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators)
	{
		DeferredLightScene::ShadowPreparerIdMapping shadowOperatorMapping;
		shadowOperatorMapping._operatorToShadowPreparerId.resize(shadowGenerators.size(), ~0u);
		std::future<std::shared_ptr<DynamicShadowPreparers>> shadowPreparationOperatorsFuture;

		// Map the shadow operator ids onto the underlying type of shadow (dynamically generated, shadow probes, etc)
		{
			VLA_UNSAFE_FORCE(ShadowOperatorDesc, dynShadowGens, shadowGenerators.size());
			unsigned dynShadowCount = 0;
			for (unsigned c=0; c<shadowGenerators.size(); ++c) {
				if (shadowGenerators[c]._resolveType == ShadowResolveType::Probe) {
					// setup shadow operator for probes
					if (shadowOperatorMapping._operatorForStaticProbes != ~0u)
						Throw(std::runtime_error("Multiple operators for shadow probes detected. Only zero or one is supported"));
					shadowOperatorMapping._operatorForStaticProbes = c;
					shadowOperatorMapping._shadowProbesCfg = MakeShadowProbeConfiguration(shadowGenerators[c]);
				} else {
					dynShadowGens[dynShadowCount] = shadowGenerators[c];
					shadowOperatorMapping._operatorToShadowPreparerId[c] = dynShadowCount;
					++dynShadowCount;
				}
			}
			shadowPreparationOperatorsFuture = CreateDynamicShadowPreparers(
				MakeIteratorRange(dynShadowGens, &dynShadowGens[dynShadowCount]),
				pipelineAccelerators, techDelBox);
		}

		using namespace std::placeholders;
		::Assets::WhenAll(std::move(shadowPreparationOperatorsFuture)).ThenConstructToPromise(
			std::move(promise),
			[shadowOperatorMapping, pipelineAccelerators, techDelBox](auto shadowPreparationOperators) {
				auto lightScene = std::make_shared<DeferredLightScene>();
				lightScene->_shadowPreparers = std::move(shadowPreparationOperators);
				lightScene->_shadowOperatorIdMapping = shadowOperatorMapping;
				lightScene->_pipelineAccelerators = pipelineAccelerators;
				lightScene->_techDelBox = techDelBox;
				lightScene->FinalizeConfiguration();
				return lightScene;
			});
	}

	struct DeferredOperatorDigest
	{
		std::optional<ToneMapAcesOperatorDesc> _tonemapAces;
		std::optional<SkyTextureProcessorDesc> _skyTextureProcessor;
		std::optional<SkyOperatorDesc> _sky;

		std::vector<LightSourceOperatorDesc> _resolveOperators;
		std::vector<ShadowOperatorDesc> _shadowOperators;

		DeferredOperatorDigest(
			IteratorRange<const LightSourceOperatorDesc*> resolveOperatorsInit,
			IteratorRange<const ShadowOperatorDesc*> shadowOperatorsInit,
			const ChainedOperatorDesc* globalOperatorsChain)
		: _resolveOperators { resolveOperatorsInit.begin(), resolveOperatorsInit.end() }
		, _shadowOperators { shadowOperatorsInit.begin(), shadowOperatorsInit.end() }
		{
			auto* chain = globalOperatorsChain;
			while (chain) {
				switch(chain->_structureType) {
				case TypeHashCode<ToneMapAcesOperatorDesc>:
					if (_tonemapAces)
						Throw(std::runtime_error("Multiple tonemap operators found, where only one expected"));
					_tonemapAces = Internal::ChainedOperatorCast<ToneMapAcesOperatorDesc>(*chain);
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
				}
				chain = chain->_next;
			}
		}
	};

	void CreateDeferredLightingTechnique(
		std::promise<std::shared_ptr<CompiledLightingTechnique>>&& promisedTechnique,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelineCollection,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperatorsInit,
		IteratorRange<const ShadowOperatorDesc*> shadowOperatorsInit,
		const ChainedOperatorDesc* globalOperators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachmentsInit,
		DeferredLightingTechniqueFlags::BitField flags)
	{
		auto buildGBufferFragment = CreateBuildGBufferSceneFragment(*techDelBox, GBufferType::PositionNormalParameters);

		DeferredOperatorDigest digest { resolveOperatorsInit, shadowOperatorsInit, globalOperators };

		std::promise<std::shared_ptr<DeferredLightScene>> lightScenePromise;
		auto lightSceneFuture = lightScenePromise.get_future();
		BeginLightSceneConstruction(std::move(lightScenePromise), pipelineAccelerators, techDelBox, shadowOperatorsInit);

		auto resolution = Internal::ExtractOutputResolution(preregisteredAttachmentsInit);

		std::vector<Techniques::PreregisteredAttachment> preregisteredAttachments { preregisteredAttachmentsInit.begin(), preregisteredAttachmentsInit.end() };
		::Assets::WhenAll(std::move(buildGBufferFragment), std::move(lightSceneFuture)).ThenConstructToPromise(
			std::move(promisedTechnique),
			[pipelineAccelerators, techDelBox, resolution,
			preregisteredAttachments=std::move(preregisteredAttachments), shadowDescSet=techDelBox->_dmShadowDescSetTemplate,
			digest=std::move(digest), pipelineCollection, lightingOperatorLayout=techDelBox->_lightingOperatorLayout, flags](
				auto&& thatPromise, auto buildGbuffer, auto lightScene) {

				TRY {
					auto lightingTechnique = std::make_shared<CompiledLightingTechnique>(lightScene);
					auto captures = std::make_shared<DeferredLightingCaptures>();
					captures->_lightScene = lightScene;
					captures->_lightingOperatorLayout = lightingOperatorLayout;
					captures->_pipelineCollection = pipelineCollection;

					if (digest._tonemapAces) {
						captures->_acesOperator = std::make_shared<ToneMapAcesOperator>(pipelineCollection, *digest._tonemapAces);
					} else {
						captures->_copyToneMapOperator = std::make_shared<CopyToneMapOperator>(pipelineCollection);
					}

					if (digest._sky)
						captures->_skyOperator = std::make_shared<SkyOperator>(pipelineCollection, *digest._sky);

					if (digest._skyTextureProcessor) {
						captures->_skyTextureProcessor = CreateSkyTextureProcessor(
							*digest._skyTextureProcessor, captures->_skyOperator,
							captures->MakeOnSkyTextureUpdate(),
							captures->MakeOnIBLUpdate());
					}

					lightingTechnique->_depVal = ::Assets::GetDepValSys().Make();

					captures->_lightScene->_queryInterfaceHelper = lightingTechnique->_queryInterfaceHelper =
						[captures=captures.get()](uint64_t typeCode) -> void* {
							switch (typeCode) {
							case TypeHashCode<IBloom>:
								return (IBloom*)captures->_acesOperator.get();
							case TypeHashCode<IExposure>:
								return (IExposure*)captures->_acesOperator.get();
							case TypeHashCode<ISkyTextureProcessor>:
								return (ISkyTextureProcessor*)captures->_skyTextureProcessor.get();
							}
							return nullptr;
						};

					// --------------------- --------------------------------------------------

					FrameBufferProperties fbProps { resolution[0], resolution[1], TextureSamples::Create() };
					Techniques::FragmentStitchingContext stitchingContext{preregisteredAttachments, fbProps, Techniques::CalculateDefaultSystemFormats(*pipelineAccelerators->GetDevice())};
					PreregisterAttachments(stitchingContext, GBufferType::PositionNormalParameters);
					if (captures->_acesOperator) captures->_acesOperator->PreregisterAttachments(stitchingContext);
					if (captures->_copyToneMapOperator) captures->_copyToneMapOperator->PreregisterAttachments(stitchingContext);

					// Reset captures
					lightingTechnique->PreSequenceSetup(
						[captures](LightingTechniqueIterator& iterator) {
						});

					// Prepare shadows
					lightingTechnique->CreateDynamicSequence(
						[captures](LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence) {
							captures->DoShadowPrepare(iterator, sequence);
							captures->_lightResolveOperators->_stencilingGeometry.CompleteInitialization(*iterator._threadContext);
							if (captures->_skyTextureProcessor)
								SkyTextureProcessorPrerender(*captures->_skyTextureProcessor);
						});

					auto& mainSequence = lightingTechnique->CreateSequence();
					mainSequence.CreateStep_CallFunction(
						[](LightingTechniqueIterator& iterator) {
							if (iterator._deformAcceleratorPool)
								iterator._deformAcceleratorPool->SetVertexInputBarrier(*iterator._threadContext);
						});

					mainSequence.CreateStep_InvalidateUniforms();
					mainSequence.CreateStep_BringUpToDateUniforms();

					// Draw main scene
					auto mainSceneFragmentRegistration = mainSequence.CreateStep_RunFragments(std::move(buildGbuffer.first));
					lightingTechnique->_completionCommandList = std::max(lightingTechnique->_completionCommandList, buildGbuffer.second);

					// Lighting resolve (gbuffer -> HDR color image)
					auto lightingResolveFragment = captures->CreateLightingResolveFragment();
					auto resolveFragmentRegistration = mainSequence.CreateStep_RunFragments(std::move(lightingResolveFragment));

					LightingTechniqueSequence::FragmentInterfaceRegistration toneMapReg;
					if (captures->_acesOperator) {
						toneMapReg = mainSequence.CreateStep_RunFragments(captures->_acesOperator->CreateFragment(stitchingContext._workingProps));
					} else {
						assert(captures->_copyToneMapOperator);
						toneMapReg = mainSequence.CreateStep_RunFragments(captures->_copyToneMapOperator->CreateFragment(stitchingContext._workingProps));
					}

					mainSequence.ResolvePendingCreateFragmentSteps();		// finish render pass

					// generate debugging outputs
					if (flags & DeferredLightingTechniqueFlags::GenerateDebuggingTextures) {
						mainSequence.CreateStep_CallFunction(
							[captures](LightingTechniqueIterator& iterator) {
								captures->GenerateDebuggingOutputs(iterator);
							});
					}

					// unbind operations
					if (captures->_lightScene->_shadowScheduler) {
						mainSequence.CreateStep_CallFunction(
							[captures](LightingTechniqueIterator& iterator) {
								captures->_lightScene->_shadowScheduler->ClearPreparedShadows();
							});
					}

					// prepare-only steps
					for (const auto&shadowPreparer:captures->_lightScene->_shadowPreparers->_preparers) {
						mainSequence.CreatePrepareOnlyParseScene(Techniques::BatchFlags::Opaque);
						mainSequence.CreatePrepareOnlyStep_ExecuteDrawables(shadowPreparer._preparer->GetSequencerConfig().first);
					}

					lightingTechnique->CompleteConstruction(pipelineAccelerators, stitchingContext);

					//
					// Now that we've finalized the frame buffer layout, build the light resolve operators
					// And then we'll complete the technique when the future from BuildLightResolveOperators() is completed
					//
					auto resolvedFB = mainSequence.GetResolvedFrameBufferDesc(resolveFragmentRegistration);

					struct SecondStageConstructionHelper
					{
						std::future<std::shared_ptr<LightResolveOperators>> _lightResolveOperators;
						std::future<std::shared_ptr<ToneMapAcesOperator>> _futureToneMapAces;
						std::future<std::shared_ptr<CopyToneMapOperator>> _futureCopyToneMap;
						std::future<std::shared_ptr<SkyOperator>> _futureSky;
					};
					auto secondStageHelper = std::make_shared<SecondStageConstructionHelper>();

					secondStageHelper->_lightResolveOperators = BuildLightResolveOperators(
						*pipelineCollection, lightingOperatorLayout,
						digest._resolveOperators, digest._shadowOperators,
						*resolvedFB.first, resolvedFB.second+1,
						false, GBufferType::PositionNormalParameters);

					if (captures->_acesOperator)
						secondStageHelper->_futureToneMapAces = Internal::SecondStageConstruction(*captures->_acesOperator, Internal::AsFrameBufferTarget(mainSequence, toneMapReg));
					if (captures->_copyToneMapOperator)
						secondStageHelper->_futureCopyToneMap = Internal::SecondStageConstruction(*captures->_copyToneMapOperator, Internal::AsFrameBufferTarget(mainSequence, toneMapReg));

					if (captures->_skyOperator)
						secondStageHelper->_futureSky = Internal::SecondStageConstruction(*captures->_skyOperator, Internal::AsFrameBufferTarget(mainSequence, mainSceneFragmentRegistration));

					::Assets::PollToPromise(
						std::move(thatPromise),
						[secondStageHelper](auto timeout) {
							auto timeoutTime = std::chrono::steady_clock::now() + timeout;
							if (Internal::MarkerTimesOut(secondStageHelper->_lightResolveOperators, timeoutTime)) return ::Assets::PollStatus::Continue;
							if (secondStageHelper->_futureToneMapAces.valid() && Internal::MarkerTimesOut(secondStageHelper->_futureToneMapAces, timeoutTime)) return ::Assets::PollStatus::Continue;
							if (secondStageHelper->_futureCopyToneMap.valid() && Internal::MarkerTimesOut(secondStageHelper->_futureCopyToneMap, timeoutTime)) return ::Assets::PollStatus::Continue;
							if (secondStageHelper->_futureSky.valid() && Internal::MarkerTimesOut(secondStageHelper->_futureSky, timeoutTime)) return ::Assets::PollStatus::Continue;
							return ::Assets::PollStatus::Finish;
						},
						[lightingTechnique, captures, pipelineCollection, secondStageHelper]() {
							if (secondStageHelper->_futureToneMapAces.valid()) secondStageHelper->_futureToneMapAces.get();
							if (secondStageHelper->_futureCopyToneMap.valid()) secondStageHelper->_futureCopyToneMap.get();
							if (secondStageHelper->_futureSky.valid()) secondStageHelper->_futureSky.get();

							captures->_lightResolveOperators = secondStageHelper->_lightResolveOperators.get();
							captures->_lightScene->_lightResolveOperators = captures->_lightResolveOperators;
							// all lights get "SupportFiniteRange"
							for (unsigned op=0; op<captures->_lightResolveOperators->_operatorDescs.size(); ++op)
								captures->_lightScene->AssociateFlag(op, Internal::StandardPositionLightFlags::SupportFiniteRange);
							lightingTechnique->_depVal.RegisterDependency(captures->_lightResolveOperators->GetDependencyValidation());
							if (captures->_acesOperator)
								lightingTechnique->_depVal.RegisterDependency(captures->_acesOperator->GetDependencyValidation());
							if (captures->_copyToneMapOperator)
								lightingTechnique->_depVal.RegisterDependency(captures->_copyToneMapOperator->GetDependencyValidation());
							if (captures->_skyOperator)
								lightingTechnique->_depVal.RegisterDependency(captures->_skyOperator->GetDependencyValidation());
							lightingTechnique->_completionCommandList = std::max(lightingTechnique->_completionCommandList, captures->_lightResolveOperators->_completionCommandList);
							
							return lightingTechnique;
						});
				} CATCH(...) {
					thatPromise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

///////////////////////////////////////////////////////////////////////////////////////////////////
		//   D E B U G G I N G   &   P R O F I L I N G    //
///////////////////////////////////////////////////////////////////////////////////////////////////

	static void GenerateShadowingDebugTextures(
		Techniques::ParsingContext& parsingContext,
		const std::shared_ptr<Techniques::PipelineCollection>& pool,
		const ShadowOperatorDesc& shadowOpDesc,
		const IPreparedShadowResult& preparedShadowResult,
		unsigned idx)
	{
		constexpr auto cascadeIndexSemantic = "CascadeIndex"_h;
		constexpr auto sampleDensitySemantic = "ShadowSampleDensity"_h;
		Techniques::FrameBufferDescFragment fbDesc;
		Techniques::FrameBufferDescFragment::SubpassDesc sp;
		sp.AppendOutput(fbDesc.DefineAttachment(cascadeIndexSemantic).FixedFormat(Format::R8_UINT).NoInitialState().FinalState(BindFlag::ShaderResource).RequireBindFlags(BindFlag::TransferSrc));
		sp.AppendOutput(fbDesc.DefineAttachment(sampleDensitySemantic + idx).FixedFormat(Format::R32G32B32A32_FLOAT).NoInitialState().FinalState(BindFlag::ShaderResource).RequireBindFlags(BindFlag::TransferSrc));
		sp.AppendNonFrameBufferAttachmentView(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal).InitialState(BindFlag::ShaderResource));
		sp.AppendNonFrameBufferAttachmentView(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).InitialState(BindFlag::ShaderResource), BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::Aspect::Depth});
		fbDesc.AddSubpass(std::move(sp));

		// barrier input resources before we begin the render pass
		parsingContext.GetAttachmentReservation().AutoBarrier(
			parsingContext.GetThreadContext(),
			{
				{parsingContext.GetAttachmentReservation().MapSemanticToName(Techniques::AttachmentSemantics::GBufferNormal), BindFlag::ShaderResource, ShaderStage::Pixel},
				{parsingContext.GetAttachmentReservation().MapSemanticToName(Techniques::AttachmentSemantics::MultisampleDepth), BindFlag::ShaderResource, ShaderStage::Pixel}
			});

		Techniques::RenderPassInstance rpi { parsingContext, fbDesc };

		UniformsStreamInterface usi;
		usi.BindResourceView(0, "GBuffer_Normals"_h);
		usi.BindResourceView(1, "DepthTexture"_h);
		usi.BindFixedDescriptorSet(0, "ShadowTemplate"_h);
		IResourceView* srvs[] = { rpi.GetNonFrameBufferAttachmentView(0).get(), rpi.GetNonFrameBufferAttachmentView(1).get() };
		ImmediateDataStream immData { parsingContext.GetProjectionDesc()};
		UniformsStream us;
		us._resourceViews = MakeIteratorRange(srvs);
		IDescriptorSet* shadowDescSets[] = { preparedShadowResult.GetDescriptorSet() };

		ParameterBox selectors;
		Internal::MakeShadowResolveParam(shadowOpDesc).WriteShaderSelectors(selectors);
		selectors.SetParameter("LIGHT_RESOLVE_SHADER", 1);
		selectors.SetParameter("GBUFFER_SHADER_RESOURCE", 1);

		Techniques::PixelOutputStates outputStates;
		outputStates.Bind(rpi);
		outputStates.Bind(Techniques::CommonResourceBox::s_dsDisable);
		AttachmentBlendDesc blendStates[] { Techniques::CommonResourceBox::s_abOpaque, Techniques::CommonResourceBox::s_abOpaque };
		outputStates.Bind(MakeIteratorRange(blendStates));
		auto op = Techniques::CreateFullViewportOperator(pool, Techniques::FullViewportOperatorSubType::DisableDepth, CASCADE_VIS_HLSL ":detailed_visualisation", selectors, LIGHTING_OPERATOR_PIPELINE ":LightingOperatorWithAuto", outputStates, usi);
		op->StallWhilePending();
		assert(op->GetAssetState() == ::Assets::AssetState::Ready);
		op->Actualize()->Draw(parsingContext, us, MakeIteratorRange(shadowDescSets));
	}

	void DeferredLightingCaptures::GenerateDebuggingOutputs(LightingTechniqueIterator& iterator)
	{
		if (!_lightScene->_shadowScheduler) return;
		iterator._parsingContext->GetUniformDelegateManager()->BringUpToDateGraphics(*iterator._parsingContext);
		unsigned c=0;
		for (auto preparedShadow:_lightScene->_shadowScheduler->GetAllPreparedShadows()) {
			GenerateShadowingDebugTextures( 
				*iterator._parsingContext, 
				_pipelineCollection,
				_lightScene->_shadowScheduler->_shadowPreparers->_preparers[preparedShadow._preparerIdx]._desc,
				*preparedShadow._preparedResult, c);
			++c;
		}
	}

}}
