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
#include "../Assets/PredefinedPipelineLayout.h"
#include "../UniformsStream.h"
#include "../../Assets/Assets.h"
#include "../../Utility/MemoryUtils.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace LightingEngine
{
	static const unsigned s_shadowProbeShadowFlag = 1u<<31u;

	class DeferredLightScene : public Internal::StandardLightScene
	{
	public:
		std::shared_ptr<LightResolveOperators> _lightResolveOperators;
		std::shared_ptr<DynamicShadowPreparers> _shadowPreparers;

		LightSourceId CreateLightSource(LightOperatorId opId) override
		{
			auto desc = _lightResolveOperators->CreateLightSource(opId);
			return AddLightSource(opId, std::move(desc));
		}

		ShadowProjectionId CreateShadowProjection(ShadowOperatorId opId, LightSourceId associatedLight) override
		{
			auto preparerId = _shadowOperatorIdMapping._operatorToShadowPreparerId[opId];
			if (preparerId != ~0u) {
				auto desc = _shadowPreparers->CreateShadowProjection(preparerId);
				return AddShadowProjection(opId, associatedLight, std::move(desc));
			} else if (opId == _shadowOperatorIdMapping._operatorForStaticProbes) {
				Throw(std::runtime_error("Use the multi-light shadow projection constructor for shadow probes"));
			}
			return ~0u;
		}

		ShadowProjectionId CreateShadowProjection(ShadowOperatorId opId, IteratorRange<const LightSourceId*> associatedLights) override
		{
			if (opId == _shadowOperatorIdMapping._operatorForStaticProbes) {
				if (_shadowProbes)
					Throw(std::runtime_error("Cannot create multiple shadow probe databases in on light scene."));
				
				_shadowProbes = std::make_shared<ShadowProbes>(
					_pipelineAccelerators, *_techDelBox, _shadowOperatorIdMapping._shadowProbesCfg);
				_spPrepareDelegate = Internal::CreateShadowProbePrepareDelegate(_shadowProbes, associatedLights, this);
				ChangeLightsShadowOperator(associatedLights, opId);
				_lightsAssignedToShadowProbes = {associatedLights.begin(), associatedLights.end()};
				return s_shadowProbeShadowFlag;
			} else {
				Throw(std::runtime_error("This shadow projection operation can't be used with the multi-light constructor variation"));
			}
			return ~0u;
		}

		void DestroyShadowProjection(ShadowProjectionId projectionId) override
		{
			if (projectionId == s_shadowProbeShadowFlag) {
				_shadowProbes.reset();
				_spPrepareDelegate.reset();
				ChangeLightsShadowOperator(_lightsAssignedToShadowProbes, ~0u);
				_lightsAssignedToShadowProbes.clear();
			} else {
				return Internal::StandardLightScene::DestroyShadowProjection(projectionId);
			}
		}

		void* TryGetShadowProjectionInterface(ShadowProjectionId projectionid, uint64_t interfaceTypeCode) override
		{
			if (projectionid == s_shadowProbeShadowFlag) {
				if (interfaceTypeCode == typeid(IPreparable).hash_code()) return (IPreparable*)_spPrepareDelegate.get();
				else if (interfaceTypeCode == typeid(IShadowProbeDatabase).hash_code()) return dynamic_cast<IShadowProbeDatabase*>(_spPrepareDelegate.get());
				return nullptr;
			} else {
				return Internal::StandardLightScene::TryGetShadowProjectionInterface(projectionid, interfaceTypeCode);
			} 
		}

		struct ShadowPreparerIdMapping
		{
			std::vector<unsigned> _operatorToShadowPreparerId;
			unsigned _operatorForStaticProbes = ~0u;
			ShadowProbes::Configuration _shadowProbesCfg;
		};

		ShadowPreparerIdMapping _shadowOperatorIdMapping;
		std::shared_ptr<ShadowProbes> _shadowProbes;
		std::shared_ptr<IPreparable> _spPrepareDelegate;
		std::vector<LightSourceId> _lightsAssignedToShadowProbes;

		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<SharedTechniqueDelegateBox> _techDelBox;
	};

	class DeferredLightingCaptures
	{
	public:
		std::vector<PreparedShadow> _preparedShadows;
		std::shared_ptr<DynamicShadowPreparers> _shadowPreparers;
		std::shared_ptr<LightResolveOperators> _lightResolveOperators;
		std::shared_ptr<Techniques::FrameBufferPool> _shadowGenFrameBufferPool;
		std::shared_ptr<Techniques::AttachmentPool> _shadowGenAttachmentPool;
		std::shared_ptr<DeferredLightScene> _lightScene;
		std::shared_ptr<Techniques::PipelineCollection> _pipelineCollection;
		std::shared_ptr<ICompiledPipelineLayout> _lightingOperatorLayout;

		void DoShadowPrepare(LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence);
		void DoLightResolve(LightingTechniqueIterator& iterator);
		void DoToneMap(LightingTechniqueIterator& iterator);
		void GenerateDebuggingOutputs(LightingTechniqueIterator& iterator);
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
			BindResourceView(0, Utility::Hash64("NormalsFittingTexture"));
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

		::Assets::WhenAll(normalsFittingTexture).ThenConstructToPromise(
			std::move(promise),
			[defIllumDel = techDelBox._deferredIllumDelegate, gbufferType, precisionTargets](auto normalsFitting) {

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
				createGBuffer.AddSubpass(std::move(subpass), defIllumDel, Techniques::BatchFlags::Opaque, std::move(box), std::move(srDelegate));
				return std::make_pair(std::move(createGBuffer), normalsFitting->GetCompletionCommandList());
			});
		return result;
	}

	static RenderStepFragmentInterface CreateLightingResolveFragment(
		std::function<void(LightingTechniqueIterator&)>&& fn,
		bool precisionTargets = false)
	{
		RenderStepFragmentInterface fragment { RenderCore::PipelineType::Graphics };
		auto depthTarget = fragment.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).InitialState(LoadStore::Retain_StencilClear, 0);
		auto lightResolveTarget = fragment.DefineAttachment(
			Techniques::AttachmentSemantics::ColorHDR).Clear().Discard()
			.FixedFormat((!precisionTargets) ? Format::R16G16B16A16_FLOAT : Format::R32G32B32A32_FLOAT)
			.MultisamplingMode(true);

		TextureViewDesc justStencilWindow {
			TextureViewDesc::Aspect::Stencil,
			TextureViewDesc::All, TextureViewDesc::All,
			TextureDesc::Dimensionality::Undefined,
			TextureViewDesc::Flags::JustStencil};

		TextureViewDesc justDepthWindow {
			TextureViewDesc::Aspect::Depth,
			TextureViewDesc::All, TextureViewDesc::All,
			TextureDesc::Dimensionality::Undefined,
			TextureViewDesc::Flags::JustDepth};

		Techniques::FrameBufferDescFragment::SubpassDesc subpasses[2];
		subpasses[0].AppendOutput(lightResolveTarget);
		subpasses[0].SetDepthStencil(depthTarget);
		subpasses[0].SetName("sky");

		subpasses[1].AppendOutput(lightResolveTarget);
		subpasses[1].SetDepthStencil(depthTarget);

		auto gbufferStore = LoadStore::Retain;	// (technically only need retain when we're going to use these for debugging)
		auto diffuseAspect = (!precisionTargets) ? TextureViewDesc::Aspect::ColorSRGB : TextureViewDesc::Aspect::ColorLinear;
		subpasses[1].AppendInput(fragment.DefineAttachment(Techniques::AttachmentSemantics::GBufferDiffuse).InitialState(gbufferStore, 0), {diffuseAspect});
		subpasses[1].AppendInput(fragment.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal).InitialState(gbufferStore, 0));
		subpasses[1].AppendInput(fragment.DefineAttachment(Techniques::AttachmentSemantics::GBufferParameter).InitialState(gbufferStore, 0));
		subpasses[1].AppendInput(depthTarget, justDepthWindow);
		subpasses[1].SetName("light-resolve");

		fragment.AddSkySubpass(std::move(subpasses[0]));
		fragment.AddSubpass(std::move(subpasses[1]), std::move(fn));
		return fragment;
	}

	static RenderStepFragmentInterface CreateToneMapFragment(
		std::function<void(LightingTechniqueIterator&)>&& fn,
		bool precisionTargets = false)
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
	
	void DeferredLightingCaptures::DoShadowPrepare(LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence)
	{
		sequence.Reset();
		if (_shadowPreparers->_preparers.empty()) return;

		_preparedShadows.reserve(_lightScene->_dynamicShadowProjections.size());
		ILightScene::LightSourceId prevLightId = ~0u; 
		for (unsigned c=0; c<_lightScene->_dynamicShadowProjections.size(); ++c) {
			_preparedShadows.push_back({
				_lightScene->_dynamicShadowProjections[c]._lightId,
				_lightScene->_dynamicShadowProjections[c]._operatorId,
				Internal::SetupShadowPrepare(
					iterator, sequence, *_lightScene->_dynamicShadowProjections[c]._desc, 
					*_lightScene, _lightScene->_dynamicShadowProjections[c]._lightId,
					PipelineType::Graphics,
					*_shadowGenFrameBufferPool, *_shadowGenAttachmentPool)});

			// shadow entries must be sorted by light id
			assert(prevLightId == ~0u || prevLightId < _lightScene->_dynamicShadowProjections[c]._lightId);
			prevLightId = _lightScene->_dynamicShadowProjections[c]._lightId;
		}
	}

	void DeferredLightingCaptures::DoLightResolve(LightingTechniqueIterator& iterator)
	{
		// Light subpass
		ResolveLights(
			*iterator._threadContext, *iterator._parsingContext, iterator._rpi,
			*_lightResolveOperators, *_lightScene,
			_preparedShadows, _lightScene->_shadowProbes.get());
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
		usi.BindResourceView(0, Utility::Hash64("SubpassInputAttachment"));
		return Techniques::CreateFullViewportOperator(
			pool, Techniques::FullViewportOperatorSubType::DisableDepth,
			BASIC_PIXEL_HLSL ":copy_inputattachment",
			{}, GENERAL_OPERATOR_PIPELINE ":GraphicsMain",
			outputStates, usi);
	};

	void DeferredLightingCaptures::DoToneMap(LightingTechniqueIterator& iterator)
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

	static void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, GBufferType gbufferType, bool precisionTargets = false)
	{
		UInt2 fbSize{stitchingContext._workingProps._outputWidth, stitchingContext._workingProps._outputHeight};
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::MultisampleDepth,
				CreateDesc(
					BindFlag::DepthStencil | BindFlag::ShaderResource | BindFlag::InputAttachment,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], stitchingContext.GetSystemAttachmentFormat(Techniques::SystemAttachmentFormat::MainDepthStencil)),
					"main-depth")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::ColorHDR,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource | BindFlag::InputAttachment,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], (!precisionTargets) ? Format::R16G16B16A16_FLOAT : Format::R32G32B32A32_FLOAT),
					"color-hdr")
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
					TextureDesc::Plain2D(fbSize[0], fbSize[1], (!precisionTargets) ? Format::R8G8B8A8_UNORM_SRGB : Format::R32G32B32A32_FLOAT),
					"gbuffer-diffuse")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferNormal,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource | BindFlag::InputAttachment,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8B8A8_SNORM),
					"gbuffer-diffuse")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferParameter,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource | BindFlag::InputAttachment,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8B8A8_UNORM),
					"gbuffer-parameter")
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
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& shadowDescSet,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators)
	{
		DeferredLightScene::ShadowPreparerIdMapping shadowOperatorMapping;
		shadowOperatorMapping._operatorToShadowPreparerId.resize(shadowGenerators.size(), ~0u);
		::Assets::PtrToMarkerPtr<DynamicShadowPreparers> shadowPreparationOperatorsFuture;

		// Map the shadow operator ids onto the underlying type of shadow (dynamically generated, shadow probes, etc)
		{
			ShadowOperatorDesc dynShadowGens[shadowGenerators.size()];
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
				pipelineAccelerators, techDelBox, shadowDescSet);
		}

		using namespace std::placeholders;
		::Assets::WhenAll(shadowPreparationOperatorsFuture).ThenConstructToPromise(
			std::move(promise),
			[shadowOperatorMapping, pipelineAccelerators, techDelBox](auto shadowPreparationOperators) {
				auto lightScene = std::make_shared<DeferredLightScene>();
				lightScene->_shadowPreparers = std::move(shadowPreparationOperators);
				lightScene->_shadowOperatorIdMapping = shadowOperatorMapping;
				lightScene->_pipelineAccelerators = pipelineAccelerators;
				lightScene->_techDelBox = techDelBox;
				return lightScene;
			});
	}

	::Assets::PtrToMarkerPtr<CompiledLightingTechnique> CreateDeferredLightingTechnique(
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelineCollection,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		const std::shared_ptr<ICompiledPipelineLayout>& lightingOperatorLayout,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& shadowDescSet,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperatorsInit,
		IteratorRange<const ShadowOperatorDesc*> shadowOperatorsInit,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachmentsInit,
		const FrameBufferProperties& fbProps,
		DeferredLightingTechniqueFlags::BitField flags)
	{
		auto buildGBufferFragment = CreateBuildGBufferSceneFragment(*techDelBox, GBufferType::PositionNormalParameters);
		std::vector<LightSourceOperatorDesc> resolveOperators { resolveOperatorsInit.begin(), resolveOperatorsInit.end() };
		std::vector<ShadowOperatorDesc> shadowOperators { shadowOperatorsInit.begin(), shadowOperatorsInit.end() };

		std::promise<std::shared_ptr<DeferredLightScene>> lightScenePromise;
		auto lightSceneFuture = lightScenePromise.get_future();
		BeginLightSceneConstruction(std::move(lightScenePromise), pipelineAccelerators, techDelBox, shadowDescSet, shadowOperatorsInit);

		auto result = std::make_shared<::Assets::MarkerPtr<CompiledLightingTechnique>>("deferred-lighting-technique");
		std::vector<Techniques::PreregisteredAttachment> preregisteredAttachments { preregisteredAttachmentsInit.begin(), preregisteredAttachmentsInit.end() };
		::Assets::WhenAll(std::move(buildGBufferFragment), std::move(lightSceneFuture)).ThenConstructToPromise(
			result->AdoptPromise(),
			[pipelineAccelerators, techDelBox, fbProps, 
			preregisteredAttachments=std::move(preregisteredAttachments),
			resolveOperators=std::move(resolveOperators), shadowOperators=std::move(shadowOperators), pipelineCollection, lightingOperatorLayout, flags](
				auto&& thatPromise, auto buildGbuffer, auto lightScene) {

				TRY {
					Techniques::FragmentStitchingContext stitchingContext{preregisteredAttachments, fbProps, Techniques::CalculateDefaultSystemFormats(*pipelineAccelerators->GetDevice())};
					PreregisterAttachments(stitchingContext, GBufferType::PositionNormalParameters);

					auto lightingTechnique = std::make_shared<CompiledLightingTechnique>(lightScene);
					auto captures = std::make_shared<DeferredLightingCaptures>();
					captures->_shadowGenAttachmentPool = std::make_shared<Techniques::AttachmentPool>(pipelineAccelerators->GetDevice());
					captures->_shadowGenFrameBufferPool = Techniques::CreateFrameBufferPool();
					captures->_shadowPreparers = lightScene->_shadowPreparers;
					captures->_lightScene = lightScene;
					captures->_lightingOperatorLayout = lightingOperatorLayout;
					captures->_pipelineCollection = pipelineCollection;

					// Reset captures
					lightingTechnique->PreSequenceSetup(
						[captures](LightingTechniqueIterator& iterator) {
						});

					// Prepare shadows
					lightingTechnique->CreateDynamicSequence(
						[captures](LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence) {
							captures->DoShadowPrepare(iterator, sequence);
						});

					auto& mainSequence = lightingTechnique->CreateSequence();
					// Draw main scene
					mainSequence.CreateStep_RunFragments(std::move(buildGbuffer.first));
					lightingTechnique->_completionCommandList = std::max(lightingTechnique->_completionCommandList, buildGbuffer.second);

					// Lighting resolve (gbuffer -> HDR color image)
					auto lightingResolveFragment = CreateLightingResolveFragment(
						[captures](LightingTechniqueIterator& iterator) {
							// do lighting resolve here
							captures->DoLightResolve(iterator);
						});
					auto resolveFragmentRegistration = mainSequence.CreateStep_RunFragments(std::move(lightingResolveFragment));

					auto toneMapFragment = CreateToneMapFragment(
						[captures](LightingTechniqueIterator& iterator) {
							captures->DoToneMap(iterator);
						});
					mainSequence.CreateStep_RunFragments(std::move(toneMapFragment));

					// generate debugging outputs
					if (flags & DeferredLightingTechniqueFlags::GenerateDebuggingTextures) {
						mainSequence.CreateStep_CallFunction(
							[captures](LightingTechniqueIterator& iterator) {
								captures->GenerateDebuggingOutputs(iterator);
							});
					}

					// unbind operations
					mainSequence.CreateStep_CallFunction(
						[captures](LightingTechniqueIterator& iterator) {
							captures->_preparedShadows.clear();
						});

					// prepare-only steps
					for (const auto&shadowPreparer:captures->_shadowPreparers->_preparers) {
						mainSequence.CreatePrepareOnlyParseScene(Techniques::BatchFlags::Opaque);
						mainSequence.CreatePrepareOnlyStep_ExecuteDrawables(shadowPreparer._preparer->GetSequencerConfig().first);
					}

					lightingTechnique->CompleteConstruction(pipelineAccelerators, stitchingContext);

					//
					// Now that we've finalized the frame buffer layout, build the light resolve operators
					// And then we'll complete the technique when the future from BuildLightResolveOperators() is completed
					//
					auto resolvedFB = mainSequence.GetResolvedFrameBufferDesc(resolveFragmentRegistration);
					auto lightResolveOperators = BuildLightResolveOperators(
						*pipelineCollection, lightingOperatorLayout,
						resolveOperators, shadowOperators,
						*resolvedFB.first, resolvedFB.second+1,
						false, GBufferType::PositionNormalParameters);

					::Assets::WhenAll(lightResolveOperators).ThenConstructToPromise(
						std::move(thatPromise),
						[lightingTechnique, captures, pipelineCollection](const std::shared_ptr<LightResolveOperators>& resolveOperators) {
							captures->_lightResolveOperators = resolveOperators;
							captures->_lightScene->_lightResolveOperators = resolveOperators;
							lightingTechnique->_depVal = resolveOperators->GetDependencyValidation();
							lightingTechnique->_completionCommandList = std::max(lightingTechnique->_completionCommandList, resolveOperators->_completionCommandList);
							return lightingTechnique;
						});
				} CATCH(...) {
					thatPromise.set_exception(std::current_exception());
				} CATCH_END
			});

		return result;
	}

	::Assets::PtrToMarkerPtr<CompiledLightingTechnique> CreateDeferredLightingTechnique(
		const std::shared_ptr<LightingEngineApparatus>& apparatus,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const FrameBufferProperties& fbProps,
		DeferredLightingTechniqueFlags::BitField flags)
	{
		return CreateDeferredLightingTechnique(
			apparatus->_pipelineAccelerators,
			apparatus->_lightingOperatorCollection,
			apparatus->_sharedDelegates,
			apparatus->_lightingOperatorLayout,
			apparatus->_dmShadowDescSetTemplate,
			resolveOperators, shadowGenerators, preregisteredAttachments,
			fbProps, flags);		
	}


///////////////////////////////////////////////////////////////////////////////////////////////////
		//   D E B U G G I N G   &   P R O F I L I N G    //
///////////////////////////////////////////////////////////////////////////////////////////////////

	static void GenerateShadowingDebugTextures(
		IThreadContext& threadContext,
		Techniques::ParsingContext& parsingContext,
		const std::shared_ptr<Techniques::PipelineCollection>& pool,
		const ShadowOperatorDesc& shadowOpDesc,
		const IPreparedShadowResult& preparedShadowResult,
		unsigned idx)
	{
		const auto cascadeIndexSemantic = Utility::Hash64("CascadeIndex");
		const auto sampleDensitySemantic = Utility::Hash64("ShadowSampleDensity");
		Techniques::FrameBufferDescFragment fbDesc;
		Techniques::FrameBufferDescFragment::SubpassDesc sp;
		sp.AppendOutput(fbDesc.DefineAttachment(cascadeIndexSemantic).FixedFormat(Format::R8_UINT).NoInitialState().FinalState(BindFlag::ShaderResource).RequireBindFlags(BindFlag::TransferSrc));
		sp.AppendOutput(fbDesc.DefineAttachment(sampleDensitySemantic + idx).FixedFormat(Format::R32G32B32A32_FLOAT).NoInitialState().FinalState(BindFlag::ShaderResource).RequireBindFlags(BindFlag::TransferSrc));
		sp.AppendNonFrameBufferAttachmentView(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal));
		sp.AppendNonFrameBufferAttachmentView(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth), BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::Aspect::Depth});
		fbDesc.AddSubpass(std::move(sp));

		Techniques::RenderPassInstance rpi { parsingContext, fbDesc };

		UniformsStreamInterface usi;
		usi.BindResourceView(0, Utility::Hash64("GBuffer_Normals"));
		usi.BindResourceView(1, Utility::Hash64("DepthTexture"));
		usi.BindFixedDescriptorSet(0, Utility::Hash64("ShadowTemplate"));
		IResourceView* srvs[] = { rpi.GetNonFrameBufferAttachmentView(0).get(), rpi.GetNonFrameBufferAttachmentView(1).get() };
		ImmediateDataStream immData { parsingContext.GetProjectionDesc()};
		UniformsStream us;
		us._resourceViews = MakeIteratorRange(srvs);
		IDescriptorSet* shadowDescSets[] = { preparedShadowResult.GetDescriptorSet().get() };

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
		iterator._parsingContext->GetUniformDelegateManager()->BringUpToDateGraphics(*iterator._parsingContext);
		unsigned c=0;
		for (const auto& preparedShadow:_preparedShadows) {
			auto opId = preparedShadow._shadowOpId;
			auto preparerId = _lightScene->_shadowOperatorIdMapping._operatorToShadowPreparerId[opId];
			if (preparerId == ~0u) continue;

			GenerateShadowingDebugTextures( 
				*iterator._threadContext, *iterator._parsingContext, 
				_pipelineCollection,
				_shadowPreparers->_preparers[preparerId]._desc,
				*preparedShadow._preparedResult, c);
			++c;
		}
	}

}}
