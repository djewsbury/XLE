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
#include "HierarchicalDepths.h"
#include "ScreenSpaceReflections.h"
#include "LightTiler.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/TechniqueDelegates.h"
#include "../Techniques/PipelineOperators.h"
#include "../IThreadContext.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"

#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Shader.h"

namespace RenderCore { namespace LightingEngine
{
	class ForwardLightingCaptures
	{
	public:
		std::vector<std::pair<unsigned, std::shared_ptr<IPreparedShadowResult>>> _preparedShadows;
		std::shared_ptr<ShadowPreparationOperators> _shadowPreparationOperators;
		std::shared_ptr<Techniques::FrameBufferPool> _shadowGenFrameBufferPool;
		std::shared_ptr<Techniques::AttachmentPool> _shadowGenAttachmentPool;
		std::shared_ptr<StandardLightScene> _lightScene;

		/*class UniformsDelegate : public Techniques::IShaderResourceDelegate
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
		std::shared_ptr<UniformsDelegate> _uniformsDelegate;*/

		void DoShadowPrepare(LightingTechniqueIterator& iterator);
		void DoToneMap(LightingTechniqueIterator& iterator);
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

	void ForwardLightingCaptures::DoShadowPrepare(LightingTechniqueIterator& iterator)
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

	void ForwardLightingCaptures::DoToneMap(LightingTechniqueIterator& iterator)
	{
		// Very simple stand-in for tonemap -- just use a copy shader to write the HDR values directly to the LDR texture
		auto& pipelineLayout = ::Assets::Actualize<Techniques::CompiledPipelineLayoutAsset>(
			iterator._threadContext->GetDevice(), 
			LIGHTING_OPERATOR_PIPELINE ":LightingOperator");
		auto& copyShader = *::Assets::Actualize<Metal::ShaderProgram>(
			pipelineLayout->GetPipelineLayout(),
			BASIC2D_VERTEX_HLSL ":fullscreen",
			BASIC_PIXEL_HLSL ":copy_inputattachment");
		auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);
		auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(pipelineLayout->GetPipelineLayout());
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

	static void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, bool precisionTargets = false)
	{
		UInt2 fbSize{stitchingContext._workingProps._outputWidth, stitchingContext._workingProps._outputHeight};
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::MultisampleDepth,
				CreateDesc(
					BindFlag::DepthStencil | BindFlag::ShaderResource | BindFlag::UnorderedAccess | BindFlag::InputAttachment, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::D24_UNORM_S8_UINT),
					"main-depth")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::ColorHDR,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::InputAttachment | BindFlag::UnorderedAccess, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], (!precisionTargets) ? Format::R16G16B16A16_FLOAT : Format::R32G32B32A32_FLOAT),
					"color-hdr")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferNormal,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource | BindFlag::UnorderedAccess, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8B8A8_SNORM),
					"gbuffer-normal")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferMotion,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource | BindFlag::UnorderedAccess, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8_SINT),
					"gbuffer-motion")
			}
		};
		for (const auto& a:attachments)
			stitchingContext.DefineAttachment(a);
	}

	static RenderStepFragmentInterface CreatePreDepthFragment(
		std::shared_ptr<Techniques::ITechniqueDelegate> depthOnlyDelegate)
	{
		RenderStepFragmentInterface result { PipelineType::Graphics };
		auto depth = result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth, LoadStore::Clear);
		auto motion = result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion, LoadStore::Clear);

		Techniques::FrameBufferDescFragment::SubpassDesc preDepthSubpass;
		preDepthSubpass.AppendOutput(motion);
		preDepthSubpass.SetDepthStencil(depth);
		preDepthSubpass.SetName("PreDepth");
		result.AddSubpass(std::move(preDepthSubpass), depthOnlyDelegate, Techniques::BatchFilter::General);
		return result;
	}

	static RenderStepFragmentInterface CreateForwardSceneFragment(
		std::shared_ptr<Techniques::ITechniqueDelegate> forwardIllumDelegate)
	{
		RenderStepFragmentInterface result { PipelineType::Graphics };
        auto lightResolve = result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR, LoadStore::Clear);
		auto depth = result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth);

		Techniques::FrameBufferDescFragment::SubpassDesc mainSubpass;
		mainSubpass.AppendOutput(lightResolve);
		mainSubpass.SetDepthStencil(depth);
		mainSubpass.SetName("MainForward");

		ParameterBox box;
		result.AddSubpass(std::move(mainSubpass), forwardIllumDelegate, Techniques::BatchFilter::General, std::move(box));
		return result;
	}

	::Assets::PtrToFuturePtr<CompiledLightingTechnique> CreateForwardLightingTechnique(
		const std::shared_ptr<LightingEngineApparatus>& apparatus,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const FrameBufferProperties& fbProps)
	{
		return CreateForwardLightingTechnique(apparatus->_device, apparatus->_pipelineAccelerators, apparatus->_lightingOperatorCollection, apparatus->_sharedDelegates, resolveOperators, shadowGenerators, preregisteredAttachments, fbProps);
	}

	class ForwardPlusLightFactory : public ILightSourceFactory
	{
	public:
		virtual std::unique_ptr<ILightBase> CreateLightSource(ILightScene::LightOperatorId)
		{
			return std::make_unique<StandardLightDesc>(0);
		}
	};

	::Assets::PtrToFuturePtr<CompiledLightingTechnique> CreateForwardLightingTechnique(
		const std::shared_ptr<IDevice>& device,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelinePool>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachmentsInit,
		const FrameBufferProperties& fbProps)
	{
		auto shadowPreparationOperatorsFuture = CreateShadowPreparationOperators(shadowGenerators, pipelineAccelerators, techDelBox, nullptr);

		auto hierarchicalDepthsOperatorFuture = ::Assets::MakeFuture<std::shared_ptr<HierarchicalDepthsOperator>>(pipelinePool);
		auto lightTilerFuture = ::Assets::MakeFuture<std::shared_ptr<RasterizationLightTileOperator>>(pipelinePool);
		auto ssrFuture = ::Assets::MakeFuture<std::shared_ptr<ScreenSpaceReflectionsOperator>>(pipelinePool);

		auto result = std::make_shared<::Assets::FuturePtr<CompiledLightingTechnique>>("forward-lighting-technique");
		std::vector<Techniques::PreregisteredAttachment> preregisteredAttachments { preregisteredAttachmentsInit.begin(), preregisteredAttachmentsInit.end() };
		::Assets::WhenAll(shadowPreparationOperatorsFuture, hierarchicalDepthsOperatorFuture, lightTilerFuture, ssrFuture).ThenConstructToFuture(
			*result,
			[device, techDelBox, preregisteredAttachments=std::move(preregisteredAttachments), fbProps, pipelineAccelerators]
			(auto shadowPreparationOperators, auto hierarchicalDepthsOperator, auto lightTiler, auto ssr) {

				auto lightScene = std::make_shared<StandardLightScene>();
				lightScene->_shadowProjectionFactory = shadowPreparationOperators;
				lightScene->_lightSourceFactory = std::make_shared<ForwardPlusLightFactory>();

				auto captures = std::make_shared<ForwardLightingCaptures>();
				captures->_shadowGenAttachmentPool = std::make_shared<Techniques::AttachmentPool>(device);
				captures->_shadowGenFrameBufferPool = Techniques::CreateFrameBufferPool();
				captures->_lightScene = lightScene;
				captures->_shadowPreparationOperators = shadowPreparationOperators;
				// captures->_uniformsDelegate = std::make_shared<ForwardLightingCaptures::UniformsDelegate>(*captures.get());

				Techniques::FragmentStitchingContext stitchingContext { preregisteredAttachments, fbProps };
				PreregisterAttachments(stitchingContext);
				hierarchicalDepthsOperator->PreregisterAttachments(stitchingContext);
				lightTiler->PreregisterAttachments(stitchingContext);
				ssr->PreregisterAttachments(stitchingContext);

				auto lightingTechnique = std::make_shared<CompiledLightingTechnique>(pipelineAccelerators, stitchingContext, lightScene);

				// Reset captures
				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						// iterator._parsingContext->AddShaderResourceDelegate(captures->_uniformsDelegate);
					});

				// Prepare shadows
				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						captures->DoShadowPrepare(iterator);
					});

				// Pre depth
				lightingTechnique->CreateStep_RunFragments(CreatePreDepthFragment(techDelBox->_depthMotionDelegate));

				// Build hierarchical depths
				lightingTechnique->CreateStep_RunFragments(hierarchicalDepthsOperator->CreateFragment(fbProps));

				// Light tiling & configure lighting descriptors
				lightingTechnique->CreateStep_RunFragments(lightTiler->CreateInitFragment(fbProps));
				lightingTechnique->CreateStep_RunFragments(lightTiler->CreateFragment(fbProps));
				lightingTechnique->ResolvePendingCreateFragmentSteps();	// don't merge the light tiling steps with the actual draws below

				// Draw main scene
				lightingTechnique->CreateStep_RunFragments(CreateForwardSceneFragment(techDelBox->_forwardIllumDelegate_DisableDepthWrite));

				// Calculate SSRs
				lightingTechnique->CreateStep_RunFragments(ssr->CreateFragment(fbProps));

				// Post processing
				auto toneMapFragment = CreateToneMapFragment(
					[captures](LightingTechniqueIterator& iterator) {
						captures->DoToneMap(iterator);
					});
				lightingTechnique->CreateStep_RunFragments(std::move(toneMapFragment));

				/*lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						iterator._parsingContext->RemoveShaderResourceDelegate(*captures->_uniformsDelegate);
					});*/

				lightingTechnique->CompleteConstruction();

				lightingTechnique->_depVal = ::Assets::GetDepValSys().Make();
				lightingTechnique->_depVal.RegisterDependency(hierarchicalDepthsOperator->GetDependencyValidation());
				lightingTechnique->_depVal.RegisterDependency(lightTiler->GetDependencyValidation());
				lightingTechnique->_depVal.RegisterDependency(ssr->GetDependencyValidation());
					
				return lightingTechnique;
			});
		return result;
	}

}}

