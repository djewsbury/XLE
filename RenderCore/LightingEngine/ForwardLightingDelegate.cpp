// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ForwardLightingDelegate.h"
#include "LightingEngineInternal.h"
#include "LightingEngineApparatus.h"
#include "LightUniforms.h"
#include "ShadowPreparer.h"
#include "RenderStepFragments.h"
#include "StandardLightScene.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/TechniqueDelegates.h"
#include "../../Assets/AssetFutureContinuation.h"

namespace RenderCore { namespace LightingEngine
{
	class ForwardLightingCaptures
	{
	public:
		std::vector<std::pair<unsigned, std::shared_ptr<IPreparedShadowResult>>> _preparedShadows;
		std::shared_ptr<ICompiledShadowPreparer> _shadowPreparer;
		std::shared_ptr<Techniques::FrameBufferPool> _shadowGenFrameBufferPool;
		std::shared_ptr<Techniques::AttachmentPool> _shadowGenAttachmentPool;
		std::shared_ptr<StandardLightScene> _lightScene;

		class UniformsDelegate : public Techniques::IShaderResourceDelegate
		{
		public:
			virtual const UniformsStreamInterface& GetInterface() override { return _interface; }
			void WriteImmediateData(Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
			{
				assert(idx==0);
				assert(dst.size() == sizeof(Internal::CB_BasicEnvironment));
				*(Internal::CB_BasicEnvironment*)dst.begin() = Internal::MakeBasicEnvironmentUniforms(EnvironmentalLightingDesc{});
			}

			size_t GetImmediateDataSize(Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override
			{
				assert(idx==0);
				return sizeof(Internal::CB_BasicEnvironment);
			}
		
			UniformsDelegate(ForwardLightingCaptures& captures) : _captures(&captures)
			{
				_interface.BindImmediateData(0, Utility::Hash64("BasicLightingEnvironment"), {});
			}
			UniformsStreamInterface _interface;
			ForwardLightingCaptures* _captures;
		};
		std::shared_ptr<UniformsDelegate> _uniformsDelegate;
	};

	static std::shared_ptr<IPreparedShadowResult> SetupShadowPrepare(
		LightingTechniqueIterator& iterator,
		ILightBase& proj,
		ICompiledShadowPreparer& preparer,
		Techniques::FrameBufferPool& shadowGenFrameBufferPool,
		Techniques::AttachmentPool& shadowGenAttachmentPool)
	{
		auto res = preparer.CreatePreparedShadowResult();
		iterator.PushFollowingStep(
			[&preparer, &proj, &shadowGenFrameBufferPool, &shadowGenAttachmentPool](LightingTechniqueIterator& iterator) {
				iterator._rpi = preparer.Begin(
					*iterator._threadContext,
					*iterator._parsingContext,
					proj,
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

	static RenderStepFragmentInterface CreateForwardSceneFragment(
		std::shared_ptr<Techniques::ITechniqueDelegate> forwardIllumDelegate,
		std::shared_ptr<Techniques::ITechniqueDelegate> depthOnlyDelegate,
		bool precisionTargets, bool writeDirectToLDR)
	{
		AttachmentDesc lightResolveAttachmentDesc =
			{	(!precisionTargets) ? Format::R16G16B16A16_FLOAT : Format::R32G32B32A32_FLOAT,
				AttachmentDesc::Flags::Multisampled,
				LoadStore::Clear };

		AttachmentDesc msDepthDesc =
            {   Format::D24_UNORM_S8_UINT,
                AttachmentDesc::Flags::Multisampled,
				LoadStore::Clear, LoadStore::Retain,
				0, BindFlag::ShaderResource };

		RenderStepFragmentInterface result(PipelineType::Graphics);
        AttachmentName output;
		if (!writeDirectToLDR)
			output = result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR, lightResolveAttachmentDesc);
		else
			output = result.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR, LoadStore::Clear);
		auto depth = result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth, msDepthDesc);

		Techniques::FrameBufferDescFragment::SubpassDesc depthOnlySubpass;
		depthOnlySubpass.SetDepthStencil(depth);
		depthOnlySubpass.SetName("DepthOnly");
		result.AddSubpass(std::move(depthOnlySubpass), depthOnlyDelegate, Techniques::BatchFilter::General);

		Techniques::FrameBufferDescFragment::SubpassDesc mainSubpass;
		mainSubpass.AppendOutput(output);
		mainSubpass.SetDepthStencil(depth);
		mainSubpass.SetName("MainForward");

		// todo -- parameters should be configured based on how the scene is set up
		ParameterBox box;
		// box.SetParameter((const utf8*)"SKY_PROJECTION", lightBindRes._skyTextureProjection);
		box.SetParameter((const utf8*)"HAS_DIFFUSE_IBL", 1);
		box.SetParameter((const utf8*)"HAS_SPECULAR_IBL", 1);
		
		result.AddSubpass(std::move(mainSubpass), forwardIllumDelegate, Techniques::BatchFilter::General, std::move(box));
		return result;
	}

	::Assets::PtrToFuturePtr<CompiledLightingTechnique> CreateForwardLightingTechnique(
		const std::shared_ptr<LightingEngineApparatus>& apparatus,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const FrameBufferProperties& fbProps)
	{
		return CreateForwardLightingTechnique(apparatus->_device, apparatus->_pipelineAccelerators, apparatus->_sharedDelegates, apparatus->_commonResources, preregisteredAttachments, fbProps);
	}

	::Assets::PtrToFuturePtr<CompiledLightingTechnique> CreateForwardLightingTechnique(
		const std::shared_ptr<IDevice>& device,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		const std::shared_ptr<Techniques::CommonResourceBox>& commonResources,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const FrameBufferProperties& fbProps)
	{
		auto lightScene = std::make_shared<StandardLightScene>();
		Techniques::FragmentStitchingContext stitchingContext { preregisteredAttachments, fbProps };
		auto lightingTechnique = std::make_shared<CompiledLightingTechnique>(pipelineAccelerators, stitchingContext, lightScene);
		auto captures = std::make_shared<ForwardLightingCaptures>();
		captures->_shadowGenAttachmentPool = std::make_shared<Techniques::AttachmentPool>(device);
		captures->_shadowGenFrameBufferPool = Techniques::CreateFrameBufferPool();
		captures->_uniformsDelegate = std::make_shared<ForwardLightingCaptures::UniformsDelegate>(*captures.get());

		ShadowOperatorDesc defaultShadowGenerator;
		auto shadowPreparerFuture = CreateCompiledShadowPreparer(defaultShadowGenerator, 0, pipelineAccelerators, techDelBox, commonResources, nullptr);

		auto result = std::make_shared<::Assets::FuturePtr<CompiledLightingTechnique>>("forward-lighting-technique");
		::Assets::WhenAll(shadowPreparerFuture).ThenConstructToFuture(
			*result,
			[device, captures, lightingTechnique, techDelBox](std::shared_ptr<ICompiledShadowPreparer> shadowPreparer) {

				// Reset captures
				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						iterator._parsingContext->AddShaderResourceDelegate(captures->_uniformsDelegate);
					});

				// Prepare shadows
				captures->_shadowPreparer = std::move(shadowPreparer);
				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {

						auto& lightScene = *captures->_lightScene;
						captures->_preparedShadows.reserve(lightScene._shadowProjections.size());
						ILightScene::LightSourceId prevLightId = ~0u; 
						for (unsigned c=0; c<lightScene._shadowProjections.size(); ++c) {
							captures->_preparedShadows.push_back(std::make_pair(
								lightScene._shadowProjections[c]._lightId,
								SetupShadowPrepare(iterator, *lightScene._shadowProjections[c]._desc, *captures->_shadowPreparer, *captures->_shadowGenFrameBufferPool, *captures->_shadowGenAttachmentPool)));

							// shadow entries must be sorted by light id
							assert(prevLightId == ~0u || prevLightId < lightScene._shadowProjections[c]._lightId);
							prevLightId = lightScene._shadowProjections[c]._lightId;
						}
					});

				// Draw main scene
				const bool writeDirectToLDR = true;
				lightingTechnique->CreateStep_RunFragments(CreateForwardSceneFragment(
					techDelBox->_forwardIllumDelegate_DisableDepthWrite,
					techDelBox->_depthOnlyDelegate, 
					false, writeDirectToLDR));

				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						iterator._parsingContext->RemoveShaderResourceDelegate(*captures->_uniformsDelegate);
					});

				lightingTechnique->CompleteConstruction();
					
				return lightingTechnique;
			});
		return result;
	}

}}

