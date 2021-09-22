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
#include "ILightScene.h"
#include "StandardLightScene.h"
#include "StandardLightOperators.h"
#include "LightingDelegateUtil.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/PipelineCollection.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/Drawables.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../UniformsStream.h"
#include "../../Assets/Assets.h"
#include "../../Utility/MemoryUtils.h"
#include "../../xleres/FileList.h"


#include "../Metal/DeviceContext.h"
#include "../Metal/ObjectFactory.h"
#include "../Metal/InputLayout.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/Techniques.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/Console.h"

namespace RenderCore { namespace LightingEngine
{
	class DeferredLightScene : public Internal::StandardLightScene
	{
	public:
		std::shared_ptr<LightResolveOperators> _lightResolveOperators;
		std::shared_ptr<DynamicShadowPreparationOperators> _shadowPreparationOperators;

		virtual LightSourceId CreateLightSource(LightOperatorId opId) override
		{
			auto desc = _lightResolveOperators->CreateLightSource(opId);
			return AddLightSource(opId, std::move(desc));
		}

		virtual ShadowProjectionId CreateShadowProjection(ShadowOperatorId opId, LightSourceId associatedLight) override
		{
			auto desc = _shadowPreparationOperators->CreateShadowProjection(opId);
			return AddShadowProjection(opId, associatedLight, std::move(desc));
		}

		virtual ShadowProjectionId CreateShadowProjection(ShadowOperatorId op, IteratorRange<const LightSourceId*> associatedLights) override
		{
			assert(0);
			return ~0u;
		}
	};

	class DeferredLightingCaptures
	{
	public:
		std::vector<PreparedShadow> _preparedShadows;
		std::shared_ptr<DynamicShadowPreparationOperators> _shadowPreparationOperators;
		std::shared_ptr<LightResolveOperators> _lightResolveOperators;
		std::shared_ptr<Techniques::FrameBufferPool> _shadowGenFrameBufferPool;
		std::shared_ptr<Techniques::AttachmentPool> _shadowGenAttachmentPool;
		std::shared_ptr<DeferredLightScene> _lightScene;
		std::shared_ptr<Techniques::PipelinePool> _pipelineCollection;
		std::shared_ptr<ICompiledPipelineLayout> _lightingOperatorLayout;

		void DoShadowPrepare(LightingTechniqueIterator& iterator);
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

	static ::Assets::PtrToFuturePtr<RenderStepFragmentInterface> CreateBuildGBufferSceneFragment(
		SharedTechniqueDelegateBox& techDelBox,
		GBufferType gbufferType, 
		bool precisionTargets = false)
	{
		auto result = std::make_shared<::Assets::FuturePtr<RenderStepFragmentInterface>>("build-gbuffer");
		auto normalsFittingTexture = ::Assets::MakeAsset<Techniques::DeferredShaderResource>(NORMALS_FITTING_TEXTURE);

		::Assets::WhenAll(normalsFittingTexture).ThenConstructToFuture(
			*result,
			[defIllumDel = techDelBox._deferredIllumDelegate, gbufferType, precisionTargets](std::shared_ptr<Techniques::DeferredShaderResource> normalsFitting) {

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
				auto msDepth = createGBuffer->DefineAttachment(
					Techniques::AttachmentSemantics::MultisampleDepth,
					// Main multisampled depth stencil
					{ RenderCore::Format::D24_UNORM_S8_UINT, AttachmentDesc::Flags::Multisampled,
						LoadStore::Clear, LoadStore::Retain, 0, BindFlag::DepthStencil | BindFlag::ShaderResource });

						// Generally the deferred pixel shader will just copy information from the albedo
						// texture into the first deferred buffer. So the first deferred buffer should
						// have the same pixel format as much input textures.
						// Usually this is an 8 bit SRGB format, so the first deferred buffer should also
						// be 8 bit SRGB. So long as we don't do a lot of processing in the deferred pixel shader
						// that should be enough precision.
						//      .. however, it possible some clients might prefer 10 or 16 bit albedo textures
						//      In these cases, the first buffer should be a matching format.
				auto diffuseAspect = (!precisionTargets) ? TextureViewDesc::Aspect::ColorSRGB : TextureViewDesc::Aspect::ColorLinear;
				auto diffuse = createGBuffer->DefineAttachment(
					Techniques::AttachmentSemantics::GBufferDiffuse,
					{ (!precisionTargets) ? Format::R8G8B8A8_UNORM_SRGB : Format::R32G32B32A32_FLOAT, AttachmentDesc::Flags::Multisampled,
						LoadStore::Clear, LoadStore::Retain });

				auto normal = createGBuffer->DefineAttachment(
					Techniques::AttachmentSemantics::GBufferNormal,
					{ (!precisionTargets) ? Format::R8G8B8A8_SNORM : Format::R32G32B32A32_FLOAT, AttachmentDesc::Flags::Multisampled,
						LoadStore::Clear, LoadStore::Retain });

				auto parameter = createGBuffer->DefineAttachment(
					Techniques::AttachmentSemantics::GBufferParameter,
					{ (!precisionTargets) ? Format::R8G8B8A8_UNORM : Format::R32G32B32A32_FLOAT, AttachmentDesc::Flags::Multisampled,
						LoadStore::Clear, LoadStore::Retain });

				Techniques::FrameBufferDescFragment::SubpassDesc subpass;
				subpass.AppendOutput(diffuse, {diffuseAspect});
				subpass.AppendOutput(normal);
				if (gbufferType == GBufferType::PositionNormalParameters)
					subpass.AppendOutput(parameter);
				subpass.SetDepthStencil(msDepth);
				subpass.SetName("write-gbuffer");

				auto srDelegate = std::make_shared<BuildGBufferResourceDelegate>(*normalsFitting);

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
		auto depthTarget = fragment.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth, LoadStore::Retain_StencilClear, LoadStore::Retain);
		auto lightResolveTarget = fragment.DefineAttachment(
			Techniques::AttachmentSemantics::ColorHDR,
			{ (!precisionTargets) ? Format::R16G16B16A16_FLOAT : Format::R32G32B32A32_FLOAT, AttachmentDesc::Flags::Multisampled, LoadStore::Clear, LoadStore::DontCare });

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

			// In the second subpass, the depth buffer is bound as stencil-only (so we can read the depth values as shader inputs)
		subpasses[1].AppendOutput(lightResolveTarget);
		// subpasses[1].SetDepthStencil(depthTarget, justStencilWindow);

		auto gbufferStore = LoadStore::Retain;	// (technically only need retain when we're going to use these for debugging)
		auto diffuseAspect = (!precisionTargets) ? TextureViewDesc::Aspect::ColorSRGB : TextureViewDesc::Aspect::ColorLinear;
		subpasses[1].AppendInput(fragment.DefineAttachment(Techniques::AttachmentSemantics::GBufferDiffuse, LoadStore::Retain, gbufferStore), {diffuseAspect});
		subpasses[1].AppendInput(fragment.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal, LoadStore::Retain, gbufferStore));
		subpasses[1].AppendInput(fragment.DefineAttachment(Techniques::AttachmentSemantics::GBufferParameter, LoadStore::Retain, gbufferStore));
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
		auto hdrInput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR, LoadStore::Retain, LoadStore::DontCare);
		auto ldrOutput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR, LoadStore::DontCare, LoadStore::Retain);

		Techniques::FrameBufferDescFragment::SubpassDesc subpass;
		subpass.AppendOutput(ldrOutput);
		subpass.AppendInput(hdrInput);
		subpass.SetName("tonemap");
		fragment.AddSubpass(std::move(subpass), std::move(fn));
		return fragment;
	}
	
	void DeferredLightingCaptures::DoShadowPrepare(LightingTechniqueIterator& iterator)
	{
		if (_shadowPreparationOperators->_operators.empty()) return;

		_preparedShadows.reserve(_lightScene->_dynamicShadowProjections.size());
		ILightScene::LightSourceId prevLightId = ~0u; 
		for (unsigned c=0; c<_lightScene->_dynamicShadowProjections.size(); ++c) {
			_preparedShadows.push_back({
				_lightScene->_dynamicShadowProjections[c]._lightId,
				_lightScene->_dynamicShadowProjections[c]._operatorId,
				Internal::SetupShadowPrepare(
					iterator, *_lightScene->_dynamicShadowProjections[c]._desc, 
					*_lightScene, _lightScene->_dynamicShadowProjections[c]._lightId,
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
			_preparedShadows);
	}

	void DeferredLightingCaptures::DoToneMap(LightingTechniqueIterator& iterator)
	{
		// Very simple stand-in for tonemap -- just use a copy shader to write the HDR values directly to the LDR texture
		auto pipelineLayout = _lightResolveOperators->_pipelineLayout;
		auto& copyShader = *::Assets::Actualize<Metal::ShaderProgram>(
			pipelineLayout,
			BASIC2D_VERTEX_HLSL ":fullscreen",
			BASIC_PIXEL_HLSL ":copy_inputattachment");
		auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);
		auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(pipelineLayout);
		UniformsStreamInterface usi;
		usi.BindResourceView(0, Utility::Hash64("SubpassInputAttachment"));
		Metal::BoundUniforms uniforms(copyShader, usi);
		encoder.Bind(copyShader);
		encoder.Bind(Techniques::CommonResourceBox::s_dsDisable);
		encoder.Bind({&Techniques::CommonResourceBox::s_abOpaque, &Techniques::CommonResourceBox::s_abOpaque+1});
		UniformsStream us;
		IResourceView* srvs[] = { iterator._rpi.GetInputAttachmentView(0).get() };
		us._resourceViews = MakeIteratorRange(srvs);
		uniforms.ApplyLooseUniforms(metalContext, encoder, us);
		encoder.Bind({}, Topology::TriangleStrip);
		encoder.Draw(4);
	}

	::Assets::PtrToFuturePtr<CompiledLightingTechnique> CreateDeferredLightingTechnique(
		const std::shared_ptr<IDevice>& device,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		const std::shared_ptr<Techniques::PipelinePool>& pipelineCollection,
		const std::shared_ptr<ICompiledPipelineLayout>& lightingOperatorLayout,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& shadowDescSet,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperatorsInit,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachmentsInit,
		const FrameBufferProperties& fbProps,
		DeferredLightingTechniqueFlags::BitField flags)
	{
		auto buildGBufferFragment = CreateBuildGBufferSceneFragment(*techDelBox, GBufferType::PositionNormalParameters);
		auto shadowPreparationOperators = CreateDynamicShadowPreparationOperators(shadowGenerators, pipelineAccelerators, techDelBox, shadowDescSet);
		std::vector<LightSourceOperatorDesc> resolveOperators { resolveOperatorsInit.begin(), resolveOperatorsInit.end() };

		auto result = std::make_shared<::Assets::FuturePtr<CompiledLightingTechnique>>("deferred-lighting-technique");
		std::vector<Techniques::PreregisteredAttachment> preregisteredAttachments { preregisteredAttachmentsInit.begin(), preregisteredAttachmentsInit.end() };
		::Assets::WhenAll(buildGBufferFragment, shadowPreparationOperators).ThenConstructToFuture(
			*result,
			[device, pipelineAccelerators, techDelBox, fbProps, 
			preregisteredAttachments=std::move(preregisteredAttachments),
			resolveOperators=std::move(resolveOperators), pipelineCollection, lightingOperatorLayout, flags](
				::Assets::FuturePtr<CompiledLightingTechnique>& thatFuture,
				std::shared_ptr<RenderStepFragmentInterface> buildGbuffer,
				std::shared_ptr<DynamicShadowPreparationOperators> shadowPreparationOperators) {

				auto lightScene = std::make_shared<DeferredLightScene>();
				lightScene->_shadowPreparationOperators = shadowPreparationOperators;

				Techniques::FragmentStitchingContext stitchingContext{preregisteredAttachments, fbProps};
				auto lightingTechnique = std::make_shared<CompiledLightingTechnique>(pipelineAccelerators, stitchingContext, lightScene);
				auto captures = std::make_shared<DeferredLightingCaptures>();
				captures->_shadowGenAttachmentPool = std::make_shared<Techniques::AttachmentPool>(device);
				captures->_shadowGenFrameBufferPool = Techniques::CreateFrameBufferPool();
				captures->_shadowPreparationOperators = std::move(shadowPreparationOperators);
				captures->_lightScene = lightScene;
				captures->_lightingOperatorLayout = lightingOperatorLayout;
				captures->_pipelineCollection = pipelineCollection;

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
					*pipelineCollection, lightingOperatorLayout,
					resolveOperators, shadowOp,
					*resolvedFB.first, resolvedFB.second+1,
					false, GBufferType::PositionNormalParameters);

				::Assets::WhenAll(lightResolveOperators).ThenConstructToFuture(
					thatFuture,
					[lightingTechnique, captures, pipelineCollection](const std::shared_ptr<LightResolveOperators>& resolveOperators) {
						captures->_lightResolveOperators = resolveOperators;
						captures->_lightScene->_lightResolveOperators = resolveOperators;
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
		const std::shared_ptr<Techniques::PipelinePool>& pool,
		const std::shared_ptr<ICompiledPipelineLayout>& lightingOperatorLayout,
		const ShadowOperatorDesc& shadowOpDesc,
		const IPreparedShadowResult& preparedShadowResult,
		unsigned idx)
	{
		const auto cascadeIndexSemantic = Utility::Hash64("CascadeIndex");
		const auto sampleDensitySemantic = Utility::Hash64("ShadowSampleDensity");
		Techniques::FrameBufferDescFragment fbDesc;
		Techniques::FrameBufferDescFragment::SubpassDesc sp;
		sp.AppendOutput(fbDesc.DefineAttachment(cascadeIndexSemantic + idx, AttachmentDesc { Format::R8_UINT, 0, LoadStore::DontCare, LoadStore::Retain, 0, BindFlag::UnorderedAccess }));
		sp.AppendOutput(fbDesc.DefineAttachment(sampleDensitySemantic + idx, AttachmentDesc { Format::R32G32B32A32_FLOAT, 0, LoadStore::DontCare, LoadStore::Retain, 0, BindFlag::UnorderedAccess }));
		sp.AppendNonFrameBufferAttachmentView(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal));
		sp.AppendNonFrameBufferAttachmentView(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth), BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::Aspect::Depth});
		fbDesc.AddSubpass(std::move(sp));

		Techniques::RenderPassInstance rpi { threadContext, parsingContext, fbDesc };

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

		auto op = Techniques::CreateFullViewportOperator(pool, Techniques::FullViewportOperatorSubType::DisableDepth, CASCADE_VIS_HLSL ":detailed_visualisation", selectors, lightingOperatorLayout, rpi, usi);
		op->StallWhilePending();
		assert(op->GetAssetState() == ::Assets::AssetState::Ready);
		Techniques::SequencerUniformsHelper uniformsHelper{ parsingContext };
		op->Actualize()->Draw(threadContext, parsingContext, uniformsHelper, us, MakeIteratorRange(shadowDescSets));
	}

	void DeferredLightingCaptures::GenerateDebuggingOutputs(LightingTechniqueIterator& iterator)
	{
		unsigned c=0;
		for (const auto& preparedShadow:_preparedShadows) {
			auto opId = preparedShadow._shadowOpId;
			GenerateShadowingDebugTextures( 
				*iterator._threadContext, *iterator._parsingContext, 
				_pipelineCollection,
				_lightingOperatorLayout,
				_shadowPreparationOperators->_operators[opId]._desc,
				*preparedShadow._preparedResult, c);
			++c;
		}
	}

}}
