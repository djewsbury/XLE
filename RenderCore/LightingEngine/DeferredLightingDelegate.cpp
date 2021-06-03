// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeferredLightingDelegate.h"
#include "DeferredLightingResolve.h"
#include "LightingEngineInternal.h"
#include "LightingEngineApparatus.h"
#include "LightUniforms.h"
#include "ShadowPreparer.h"
#include "RenderStepFragments.h"
#include "LightScene.h"
#include "StandardLightScene.h"
#include "StandardLightOperators.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/PipelineCollection.h"
#include "../Techniques/PipelineOperators.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../UniformsStream.h"
#include "../../Assets/Assets.h"
#include "../../Utility/MemoryUtils.h"
#include "../../xleres/FileList.h"


#include "../Metal/DeviceContext.h"
#include "../Metal/ObjectFactory.h"
#include "../Metal/InputLayout.h"
#include "../Techniques/CommonResources.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/Console.h"

namespace RenderCore { namespace LightingEngine
{
	class LightResolveOperators;

	class DeferredLightingCaptures
	{
	public:
		std::vector<std::pair<ILightScene::LightSourceId, std::shared_ptr<IPreparedShadowResult>>> _preparedShadows;
		std::shared_ptr<ShadowPreparationOperators> _shadowPreparationOperators;
		std::shared_ptr<LightResolveOperators> _lightResolveOperators;
		std::shared_ptr<Techniques::FrameBufferPool> _shadowGenFrameBufferPool;
		std::shared_ptr<Techniques::AttachmentPool> _shadowGenAttachmentPool;
		std::shared_ptr<StandardLightScene> _lightScene;

		void DoShadowPrepare(LightingTechniqueIterator& iterator);
		void DoLightResolve(LightingTechniqueIterator& iterator);
		void DoToneMap(LightingTechniqueIterator& iterator);
		void GenerateDebuggingOutputs(LightingTechniqueIterator& iterator);
	};

	class BuildGBufferResourceDelegate : public Techniques::IShaderResourceDelegate
	{
	public:
		virtual const UniformsStreamInterface& GetInterface() { return _interf; }

        virtual void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst)
		{
			assert(bindingFlags == 1<<0);
			dst[0] = _normalsFitting.get();
		}

		BuildGBufferResourceDelegate(Techniques::DeferredShaderResource& normalsFittingResource)
		{
			_interf.BindResourceView(0, Utility::Hash64("NormalsFittingTexture"));
			_normalsFitting = normalsFittingResource.GetShaderResource();
		}
		UniformsStreamInterface _interf;
		std::shared_ptr<IResourceView> _normalsFitting;
	};

	static ::Assets::PtrToFuturePtr<RenderStepFragmentInterface> CreateBuildGBufferSceneFragment(
		SharedTechniqueDelegateBox& techDelBox,
		GBufferType gbufferType, 
		bool precisionTargets = false)
	{
		auto result = std::make_shared<::Assets::FuturePtr<RenderStepFragmentInterface>>("build-gbuffer");
		auto normalsFittingTexture = ::Assets::MakeAsset<Techniques::DeferredShaderResource>(NORMALS_FITTING_TEXTURE);

		::Assets::WhenAll(normalsFittingTexture).ThenConstructToFuture<RenderStepFragmentInterface>(
			*result,
			[defIllumDel = techDelBox._deferredIllumDelegate, gbufferType, precisionTargets](std::shared_ptr<Techniques::DeferredShaderResource> deferredShaderResource) {

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

				auto createGBuffer = std::make_shared<RenderStepFragmentInterface>(RenderCore::PipelineType::Graphics);
				auto msDepth = createGBuffer->DefineAttachmentRelativeDims(
					Techniques::AttachmentSemantics::MultisampleDepth, 1.0f, 1.0f,
					// Main multisampled depth stencil
					{ RenderCore::Format::D24_UNORM_S8_UINT, AttachmentDesc::Flags::Multisampled,
						LoadStore::Clear_ClearStencil, LoadStore::Retain });

						// Generally the deferred pixel shader will just copy information from the albedo
						// texture into the first deferred buffer. So the first deferred buffer should
						// have the same pixel format as much input textures.
						// Usually this is an 8 bit SRGB format, so the first deferred buffer should also
						// be 8 bit SRGB. So long as we don't do a lot of processing in the deferred pixel shader
						// that should be enough precision.
						//      .. however, it possible some clients might prefer 10 or 16 bit albedo textures
						//      In these cases, the first buffer should be a matching format.
				auto diffuse = createGBuffer->DefineAttachmentRelativeDims(
					Techniques::AttachmentSemantics::GBufferDiffuse, 1.0f, 1.0f,
					{ (!precisionTargets) ? Format::R8G8B8A8_UNORM_SRGB : Format::R32G32B32A32_FLOAT, AttachmentDesc::Flags::Multisampled,
						LoadStore::Clear, LoadStore::Retain });

				auto normal = createGBuffer->DefineAttachmentRelativeDims(
					Techniques::AttachmentSemantics::GBufferNormal, 1.0f, 1.0f,
					{ (!precisionTargets) ? Format::R8G8B8A8_SNORM : Format::R32G32B32A32_FLOAT, AttachmentDesc::Flags::Multisampled,
						LoadStore::Clear, LoadStore::Retain });

				auto parameter = createGBuffer->DefineAttachmentRelativeDims(
					Techniques::AttachmentSemantics::GBufferParameter, 1.0f, 1.0f,
					{ (!precisionTargets) ? Format::R8G8B8A8_UNORM : Format::R32G32B32A32_FLOAT, AttachmentDesc::Flags::Multisampled,
						LoadStore::Clear, LoadStore::Retain });

				SubpassDesc subpass;
				subpass.AppendOutput(diffuse);
				subpass.AppendOutput(normal);
				if (gbufferType == GBufferType::PositionNormalParameters)
					subpass.AppendOutput(parameter);
				subpass.SetDepthStencil(msDepth);

				auto srDelegate = std::make_shared<BuildGBufferResourceDelegate>(*deferredShaderResource);

				ParameterBox box;
				box.SetParameter("GBUFFER_TYPE", (unsigned)gbufferType);
				createGBuffer->AddSubpass(std::move(subpass), defIllumDel, Techniques::BatchFilter::General, std::move(box), std::move(srDelegate));
				return createGBuffer;
			});
		return result;
	}

	static RenderStepFragmentInterface CreateLightingResolveFragment(
		std::function<void(LightingTechniqueIterator&)>&& fn,
		bool precisionTargets = false)
	{
		RenderStepFragmentInterface fragment { RenderCore::PipelineType::Graphics };
		auto depthTarget = fragment.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth, LoadStore::Retain_ClearStencil, LoadStore::Retain_RetainStencil);
		auto lightResolveTarget = fragment.DefineAttachmentRelativeDims(
			Techniques::AttachmentSemantics::ColorHDR, 1.0f, 1.0f,
			{ (!precisionTargets) ? Format::R16G16B16A16_FLOAT : Format::R32G32B32A32_FLOAT, AttachmentDesc::Flags::Multisampled, LoadStore::Clear });

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

		SubpassDesc subpasses[2];
		subpasses[0].AppendOutput(lightResolveTarget);
		subpasses[0].SetDepthStencil(depthTarget);

			// In the second subpass, the depth buffer is bound as stencil-only (so we can read the depth values as shader inputs)
		subpasses[1].AppendOutput(lightResolveTarget);
		subpasses[1].SetDepthStencil({ depthTarget, justStencilWindow });

		auto gbufferStore = LoadStore::Retain;	// (technically only need retain when we're going to use these for debugging)
		auto diffuseAspect = (!precisionTargets) ? TextureViewDesc::Aspect::ColorSRGB : TextureViewDesc::Aspect::ColorLinear;
		subpasses[1].AppendInput(
			AttachmentViewDesc {
				fragment.DefineAttachment(Techniques::AttachmentSemantics::GBufferDiffuse, LoadStore::Retain, gbufferStore),
				{diffuseAspect}
			});
		subpasses[1].AppendInput(fragment.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal, LoadStore::Retain, gbufferStore));
		subpasses[1].AppendInput(fragment.DefineAttachment(Techniques::AttachmentSemantics::GBufferParameter, LoadStore::Retain, gbufferStore));
		subpasses[1].AppendInput(
			AttachmentViewDesc { depthTarget, justDepthWindow });

		// fragment.AddSubpasses(MakeIteratorRange(subpasses), std::move(fn));
		fragment.AddSkySubpass(std::move(subpasses[0]));
		fragment.AddSubpass(std::move(subpasses[1]), std::move(fn));
		return fragment;
	}

	static RenderStepFragmentInterface CreateToneMapFragment(
		std::function<void(LightingTechniqueIterator&)>&& fn,
		bool precisionTargets = false)
	{
		RenderStepFragmentInterface fragment { RenderCore::PipelineType::Graphics };
		auto hdrInput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR, LoadStore::Retain_RetainStencil, LoadStore::DontCare);
		auto ldrOutput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR, LoadStore::DontCare, LoadStore::Retain);

		SubpassDesc subpass;
		subpass.AppendOutput(ldrOutput);
		subpass.AppendInput(hdrInput);
		fragment.AddSubpass(std::move(subpass), std::move(fn));
		return fragment;
	}
	
	static std::shared_ptr<IPreparedShadowResult> SetupShadowPrepare(
		LightingTechniqueIterator& iterator,
		ILightBase& shadowDesc,
		ICompiledShadowPreparer& preparer,
		Techniques::FrameBufferPool& shadowGenFrameBufferPool,
		Techniques::AttachmentPool& shadowGenAttachmentPool)
	{
		auto res = preparer.CreatePreparedShadowResult();
		iterator.PushFollowingStep(
			[&preparer, &shadowDesc, &shadowGenFrameBufferPool, &shadowGenAttachmentPool](LightingTechniqueIterator& iterator) {
				iterator._rpi = preparer.Begin(
					*iterator._threadContext,
					*iterator._parsingContext,
					shadowDesc,
					shadowGenFrameBufferPool,
					shadowGenAttachmentPool);
			});
		iterator.PushFollowingStep(Techniques::BatchFilter::General);
		auto cfg = preparer.GetSequencerConfig();
		iterator.PushFollowingStep(std::move(cfg.first), std::move(cfg.second));
		iterator.PushFollowingStep(
			[res, &preparer](LightingTechniqueIterator& iterator) {
				iterator._rpi.End();
				preparer.End(
					*iterator._threadContext,
					*iterator._parsingContext,
					iterator._rpi,
					*res);
			});
		return res;
	}

	void DeferredLightingCaptures::DoShadowPrepare(LightingTechniqueIterator& iterator)
	{
		if (_shadowPreparationOperators->_operators.empty()) return;

		_preparedShadows.reserve(_lightScene->_shadowProjections.size());
		ILightScene::LightSourceId prevLightId = ~0u; 
		for (unsigned c=0; c<_lightScene->_shadowProjections.size(); ++c) {
			auto shadowOperatorId = _lightScene->_shadowProjections[c]._operatorId;
			auto& shadowPreparer = *_shadowPreparationOperators->_operators[shadowOperatorId]._preparer;
			_preparedShadows.push_back(std::make_pair(
				_lightScene->_shadowProjections[c]._lightId,
				SetupShadowPrepare(iterator, *_lightScene->_shadowProjections[c]._desc, shadowPreparer, *_shadowGenFrameBufferPool, *_shadowGenAttachmentPool)));

			// shadow entries must be sorted by light id
			assert(prevLightId == ~0u || prevLightId < _lightScene->_shadowProjections[c]._lightId);
			prevLightId = _lightScene->_shadowProjections[c]._lightId;
		}
	}

	void DeferredLightingCaptures::DoLightResolve(LightingTechniqueIterator& iterator)
	{
		// Light subpass
		ResolveLights(
			*iterator._threadContext, *iterator._parsingContext, iterator._rpi,
			*_lightResolveOperators, *_lightScene,
			_preparedShadows);
	}

	void DeferredLightingCaptures::DoToneMap(LightingTechniqueIterator& iterator)
	{
		// Very simple stand-in for tonemap -- just use a copy shader to write the HDR values directly to the LDR texture
		auto pipelineLayout = _lightResolveOperators->_pipelineLayout;
		auto& copyShader = *::Assets::Actualize<Metal::ShaderProgram>(
			pipelineLayout,
			BASIC2D_VERTEX_HLSL ":fullscreen",
			BASIC_PIXEL_HLSL ":copy");
		auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);
		auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(pipelineLayout);
		UniformsStreamInterface usi;
		usi.BindResourceView(0, Utility::Hash64("InputTexture"));
		Metal::BoundUniforms uniforms(copyShader, usi);
		encoder.Bind(copyShader);
		encoder.Bind(Techniques::CommonResourceBox::s_dsDisable);
		encoder.Bind({&Techniques::CommonResourceBox::s_abOpaque, &Techniques::CommonResourceBox::s_abOpaque+1});
		UniformsStream us;
		IResourceView* srvs[] = { iterator._rpi.GetInputAttachmentSRV(0) };
		us._resourceViews = MakeIteratorRange(srvs);
		uniforms.ApplyLooseUniforms(metalContext, encoder, us);
		encoder.Bind({}, Topology::TriangleStrip);
		encoder.Draw(4);
	}

	::Assets::PtrToFuturePtr<CompiledLightingTechnique> CreateDeferredLightingTechnique(
		const std::shared_ptr<IDevice>& device,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		const std::shared_ptr<Techniques::GraphicsPipelineCollection>& pipelineCollection,
		const std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayoutFile>& lightingOperatorsPipelineLayoutFile,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperatorsInit,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachmentsInit,
		const FrameBufferProperties& fbProps,
		DeferredLightingTechniqueFlags::BitField flags)
	{
		auto shadowDescSet = lightingOperatorsPipelineLayoutFile->_descriptorSets.find("DMShadow");
		if (shadowDescSet == lightingOperatorsPipelineLayoutFile->_descriptorSets.end())
			Throw(std::runtime_error("Could not find DMShadow descriptor set layout in the pipeline layout file"));

		auto buildGBufferFragment = CreateBuildGBufferSceneFragment(*techDelBox, GBufferType::PositionNormalParameters);
		auto shadowPreparationOperators = CreateShadowPreparationOperators(shadowGenerators, pipelineAccelerators, techDelBox, shadowDescSet->second);
		std::vector<LightSourceOperatorDesc> resolveOperators { resolveOperatorsInit.begin(), resolveOperatorsInit.end() };

		auto result = std::make_shared<::Assets::FuturePtr<CompiledLightingTechnique>>("deferred-lighting-technique");
		std::vector<Techniques::PreregisteredAttachment> preregisteredAttachments { preregisteredAttachmentsInit.begin(), preregisteredAttachmentsInit.end() };
		::Assets::WhenAll(buildGBufferFragment, shadowPreparationOperators).ThenConstructToFuture<CompiledLightingTechnique>(
			*result,
			[device, pipelineAccelerators, techDelBox, fbProps, 
			preregisteredAttachments=std::move(preregisteredAttachments),
			resolveOperators=std::move(resolveOperators), pipelineCollection, flags](
				::Assets::FuturePtr<CompiledLightingTechnique>& thatFuture,
				std::shared_ptr<RenderStepFragmentInterface> buildGbuffer,
				std::shared_ptr<ShadowPreparationOperators> shadowPreparationOperators) {

				auto lightScene = std::make_shared<StandardLightScene>();
				lightScene->_shadowProjectionFactory = shadowPreparationOperators;

				Techniques::FragmentStitchingContext stitchingContext{preregisteredAttachments, fbProps};
				auto lightingTechnique = std::make_shared<CompiledLightingTechnique>(pipelineAccelerators, stitchingContext, lightScene);
				auto captures = std::make_shared<DeferredLightingCaptures>();
				captures->_shadowGenAttachmentPool = std::make_shared<Techniques::AttachmentPool>(device);
				captures->_shadowGenFrameBufferPool = Techniques::CreateFrameBufferPool();
				captures->_shadowPreparationOperators = std::move(shadowPreparationOperators);
				captures->_lightScene = lightScene;

				// Reset captures
				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
					});

				// Prepare shadows
				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						captures->DoShadowPrepare(iterator);
					});

				// Draw main scene
				lightingTechnique->CreateStep_RunFragments(std::move(*buildGbuffer));

				// Lighting resolve (gbuffer -> HDR color image)
				auto lightingResolveFragment = CreateLightingResolveFragment(
					[captures](LightingTechniqueIterator& iterator) {
						// do lighting resolve here
						captures->DoLightResolve(iterator);
					});
				auto resolveFragmentRegistration = lightingTechnique->CreateStep_RunFragments(std::move(lightingResolveFragment));

				auto toneMapFragment = CreateToneMapFragment(
					[captures](LightingTechniqueIterator& iterator) {
						captures->DoToneMap(iterator);
					});
				lightingTechnique->CreateStep_RunFragments(std::move(toneMapFragment));

				// generate debugging outputs
				if (flags & DeferredLightingTechniqueFlags::GenerateDebuggingTextures) {
					lightingTechnique->CreateStep_CallFunction(
						[captures](LightingTechniqueIterator& iterator) {
							captures->GenerateDebuggingOutputs(iterator);
						});
				}

				// unbind operations
				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						captures->_preparedShadows.clear();
					});

				// prepare-only steps
				for (const auto&shadowPreparer:captures->_shadowPreparationOperators->_operators) {
					lightingTechnique->CreatePrepareOnlyStep_ParseScene(Techniques::BatchFilter::General);
					lightingTechnique->CreatePrepareOnlyStep_ExecuteDrawables(shadowPreparer._preparer->GetSequencerConfig().first);
				}

				lightingTechnique->CompleteConstruction();

				//
				// Now that we've finalized the frame buffer layout, build the light resolve operators
				// And then we'll complete the technique when the future from BuildLightResolveOperators() is completed
				//
				auto resolvedFB = lightingTechnique->GetResolvedFrameBufferDesc(resolveFragmentRegistration);
				std::vector<ShadowOperatorDesc> shadowOp;
				shadowOp.reserve(captures->_shadowPreparationOperators->_operators.size());
				for (const auto& c:captures->_shadowPreparationOperators->_operators) shadowOp.push_back(c._desc);
				auto lightResolveOperators = BuildLightResolveOperators(
					*pipelineCollection, 
					resolveOperators, shadowOp,
					*resolvedFB.first, resolvedFB.second+1,
					false, GBufferType::PositionNormalParameters);

				::Assets::WhenAll(lightResolveOperators).ThenConstructToFuture<CompiledLightingTechnique>(
					thatFuture,
					[lightingTechnique, captures, lightScene](const std::shared_ptr<LightResolveOperators>& resolveOperators) {
						captures->_lightResolveOperators = resolveOperators;
						lightScene->_lightSourceFactory = resolveOperators;
						lightingTechnique->_depVal = resolveOperators->GetDependencyValidation();
						return lightingTechnique;
					});
			});

		return result;
	}

	::Assets::PtrToFuturePtr<CompiledLightingTechnique> CreateDeferredLightingTechnique(
		const std::shared_ptr<LightingEngineApparatus>& apparatus,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const FrameBufferProperties& fbProps,
		DeferredLightingTechniqueFlags::BitField flags)
	{
		return CreateDeferredLightingTechnique(
			apparatus->_device,
			apparatus->_pipelineAccelerators,
			apparatus->_sharedDelegates,
			apparatus->_lightingOperatorCollection,
			apparatus->_lightingOperatorsPipelineLayoutFile,
			resolveOperators, shadowGenerators, preregisteredAttachments,
			fbProps, flags);		
	}


///////////////////////////////////////////////////////////////////////////////////////////////////
		//   D E B U G G I N G   &   P R O F I L I N G    //
///////////////////////////////////////////////////////////////////////////////////////////////////

	static void GenerateShadowingDebugTextures(
		IThreadContext& threadContext,
		Techniques::ParsingContext& parsingContext,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const ShadowOperatorDesc& shadowOpDesc,
		const IPreparedShadowResult& preparedShadowResult,
		unsigned idx)
	{
		const auto cascadeIndexSemantic = Utility::Hash64("CascadeIndex");
		const auto sampleDensitySemantic = Utility::Hash64("ShadowSampleDensity");
		Techniques::FrameBufferDescFragment fbDesc;
		SubpassDesc sp;
		sp.AppendOutput(fbDesc.DefineAttachmentRelativeDims(cascadeIndexSemantic + idx, 1.0f, 1.0f, AttachmentDesc { Format::R8_UINT, 0, LoadStore::DontCare, LoadStore::Retain, 0, BindFlag::ShaderResource }));
		sp.AppendOutput(fbDesc.DefineAttachmentRelativeDims(sampleDensitySemantic + idx, 1.0f, 1.0f, AttachmentDesc { Format::R32G32B32A32_FLOAT, 0, LoadStore::DontCare, LoadStore::Retain, 0, BindFlag::ShaderResource }));
		sp.AppendInput(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal));
		sp.AppendInput(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth));
		fbDesc.AddSubpass(std::move(sp));

		Techniques::RenderPassInstance rpi { threadContext, parsingContext, fbDesc };

		UniformsStreamInterface usi;
		usi.BindResourceView(0, Utility::Hash64("GBuffer_Normals"));
		usi.BindResourceView(1, Utility::Hash64("DepthTexture"));
		usi.BindFixedDescriptorSet(0, Utility::Hash64("ShadowTemplate"));
		IResourceView* srvs[] = { rpi.GetInputAttachmentSRV(0), rpi.GetInputAttachmentSRV(1) };
		ImmediateDataStream immData { parsingContext.GetProjectionDesc()};
		UniformsStream us;
		us._resourceViews = MakeIteratorRange(srvs);
		IDescriptorSet* shadowDescSets[] = { preparedShadowResult.GetDescriptorSet().get() };

		auto selectors = Internal::MakeShadowResolveParam(shadowOpDesc).WriteShaderSelectors();

		auto op = Techniques::CreateFullViewportOperator(pipelineLayout, rpi, CASCADE_VIS_HLSL ":detailed_visualisation", MakeStringSection(selectors), usi);
		op->StallWhilePending();
		assert(op->GetAssetState() == ::Assets::AssetState::Ready);
		op->Actualize()->Draw(threadContext, parsingContext, us, MakeIteratorRange(shadowDescSets));
	}

	void DeferredLightingCaptures::GenerateDebuggingOutputs(LightingTechniqueIterator& iterator)
	{
		unsigned c=0;
		for (const auto& preparedShadow:_preparedShadows) {
			auto opId = preparedShadow.second->GetShadowOperatorId();
			GenerateShadowingDebugTextures( 
				*iterator._threadContext, *iterator._parsingContext, 
				_lightResolveOperators->_pipelineLayout, 
				_shadowPreparationOperators->_operators[opId]._desc,
				*preparedShadow.second, c);
			++c;
		}
	}

}}
