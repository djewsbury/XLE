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

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine
{
	class DeferredLightScene : public Internal::StandardLightScene
	{
	public:
		std::shared_ptr<LightResolveOperators> _lightResolveOperators;
		std::shared_ptr<DynamicShadowPreparers> _shadowPreparers;
		std::shared_ptr<Internal::DynamicShadowProjectionScheduler> _shadowScheduler;

		LightSourceId CreateAmbientLightSource() override
		{
			Throw(std::runtime_error("Configurable ambient light source not supported"));
		}

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

		void* QueryInterface(uint64_t typeCode) override
		{
			switch (typeCode) {
			case TypeHashCode<ISemiStaticShadowProbeScheduler>:
				return (ISemiStaticShadowProbeScheduler*)_shadowProbesManager.get();
			case TypeHashCode<IDynamicShadowProjectionScheduler>:
				return (IDynamicShadowProjectionScheduler*)_shadowScheduler.get();
			default:
				return StandardLightScene::QueryInterface(typeCode);
			}
		}
	};

	class DeferredLightingCaptures
	{
	public:
		std::shared_ptr<LightResolveOperators> _lightResolveOperators;
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
				createGBuffer.AddSubpass(std::move(subpass), std::move(defIllumDel), Techniques::BatchFlags::Opaque, std::move(box), std::move(srDelegate));
				return std::make_pair(std::move(createGBuffer), normalsFitting->GetCompletionCommandList());
			});
		return result;
	}

	static RenderStepFragmentInterface CreateLightingResolveFragment(
		std::function<void(LightingTechniqueIterator&)>&& fn,
		bool precisionTargets = false)
	{
		RenderStepFragmentInterface fragment { RenderCore::PipelineType::Graphics };
		auto depthTarget = fragment.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).InitialState(LoadStore::Retain_StencilClear).FinalState(BindFlag::DepthStencil);
		auto lightResolveTarget = fragment.DefineAttachment(
			Techniques::AttachmentSemantics::ColorHDR).Clear().Discard()
			.FixedFormat((!precisionTargets) ? Format::R16G16B16A16_FLOAT : Format::R32G32B32A32_FLOAT)
			.MultisamplingMode(true);

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
		std::vector<LightSourceOperatorDesc> resolveOperators { resolveOperatorsInit.begin(), resolveOperatorsInit.end() };
		std::vector<ShadowOperatorDesc> shadowOperators { shadowOperatorsInit.begin(), shadowOperatorsInit.end() };

		std::promise<std::shared_ptr<DeferredLightScene>> lightScenePromise;
		auto lightSceneFuture = lightScenePromise.get_future();
		BeginLightSceneConstruction(std::move(lightScenePromise), pipelineAccelerators, techDelBox, shadowOperatorsInit);

		auto resolution = Internal::ExtractOutputResolution(preregisteredAttachmentsInit);

		std::vector<Techniques::PreregisteredAttachment> preregisteredAttachments { preregisteredAttachmentsInit.begin(), preregisteredAttachmentsInit.end() };
		::Assets::WhenAll(std::move(buildGBufferFragment), std::move(lightSceneFuture)).ThenConstructToPromise(
			std::move(promisedTechnique),
			[pipelineAccelerators, techDelBox, resolution,
			preregisteredAttachments=std::move(preregisteredAttachments), shadowDescSet=techDelBox->_dmShadowDescSetTemplate,
			resolveOperators=std::move(resolveOperators), shadowOperators=std::move(shadowOperators), pipelineCollection, lightingOperatorLayout=techDelBox->_lightingOperatorLayout, flags](
				auto&& thatPromise, auto buildGbuffer, auto lightScene) {

				TRY {
					FrameBufferProperties fbProps { resolution[0], resolution[1], TextureSamples::Create() };
					Techniques::FragmentStitchingContext stitchingContext{preregisteredAttachments, fbProps, Techniques::CalculateDefaultSystemFormats(*pipelineAccelerators->GetDevice())};
					PreregisterAttachments(stitchingContext, GBufferType::PositionNormalParameters);

					auto lightingTechnique = std::make_shared<CompiledLightingTechnique>(lightScene);
					auto captures = std::make_shared<DeferredLightingCaptures>();
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
							captures->_lightResolveOperators->_stencilingGeometry.CompleteInitialization(*iterator._threadContext);
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
							// all lights get "SupportFiniteRange"
							for (unsigned op=0; op<resolveOperators->_operatorDescs.size(); ++op)
								captures->_lightScene->AssociateFlag(op, Internal::StandardPositionLightFlags::SupportFiniteRange);
							lightingTechnique->_depVal = resolveOperators->GetDependencyValidation();
							lightingTechnique->_completionCommandList = std::max(lightingTechnique->_completionCommandList, resolveOperators->_completionCommandList);
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

		Techniques::RenderPassInstance rpi { parsingContext, fbDesc };

		rpi.AutoNonFrameBufferBarrier({
			{0, BindFlag::ShaderResource, ShaderStage::Pixel},
			{1, BindFlag::ShaderResource, ShaderStage::Pixel}
		});

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
