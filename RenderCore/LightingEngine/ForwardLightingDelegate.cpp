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
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/DeformAccelerator.h"
#include "../IDevice.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../xleres/FileList.h"

#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Shader.h"


namespace RenderCore { namespace LightingEngine
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class ForwardLightingCaptures
	{
	public:
		std::shared_ptr<Techniques::FrameBufferPool> _shadowGenFrameBufferPool;
		std::shared_ptr<Techniques::AttachmentPool> _shadowGenAttachmentPool;
		std::shared_ptr<ForwardPlusLightScene> _lightScene;
		std::shared_ptr<SkyOperator> _skyOperator;
		std::shared_ptr<HierarchicalDepthsOperator> _hierarchicalDepthsOperator;
		std::shared_ptr<ScreenSpaceReflectionsOperator> _ssrOperator;

		void DoShadowPrepare(LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence);
		void DoToneMap(LightingTechniqueIterator& iterator);
	};

	void ForwardLightingCaptures::DoShadowPrepare(LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence)
	{
		sequence.Reset();
		if (_lightScene->_shadowPreparationOperators->_operators.empty()) return;

		_lightScene->_preparedShadows.reserve(_lightScene->_dynamicShadowProjections.size());
		ILightScene::LightSourceId prevLightId = ~0u; 
		for (unsigned c=0; c<_lightScene->_dynamicShadowProjections.size(); ++c) {
			_lightScene->_preparedShadows.push_back(std::make_pair(
				_lightScene->_dynamicShadowProjections[c]._lightId,
				Internal::SetupShadowPrepare(
					iterator, sequence, 
					*_lightScene->_dynamicShadowProjections[c]._desc, 
					*_lightScene, _lightScene->_dynamicShadowProjections[c]._lightId,
					*_shadowGenFrameBufferPool, *_shadowGenAttachmentPool)));

			// shadow entries must be sorted by light id
			assert(prevLightId == ~0u || prevLightId < _lightScene->_dynamicShadowProjections[c]._lightId);
			prevLightId = _lightScene->_dynamicShadowProjections[c]._lightId;
		}

		if (_lightScene->_dominantShadowProjection._desc) {
			assert(_lightScene->_dominantLightSet._lights.size() == 1);
			_lightScene->_preparedDominantShadow =
				Internal::SetupShadowPrepare(
					iterator, sequence, 
					*_lightScene->_dominantShadowProjection._desc, 
					*_lightScene, _lightScene->_dominantLightSet._lights[0]._id,
					*_shadowGenFrameBufferPool, *_shadowGenAttachmentPool);
		}
	}

	class ToneMapStandin
	{
	public:
		::Assets::PtrToMarkerPtr<Techniques::IShaderOperator> _operator;
		const ::Assets::DependencyValidation& GetDependencyValidation() { return _operator->GetDependencyValidation(); }
		ToneMapStandin(
			const std::shared_ptr<Techniques::PipelineCollection>& pool,
			const Techniques::PixelOutputStates& fbTarget)
		{
			UniformsStreamInterface usi;
			usi.BindResourceView(0, Utility::Hash64("SubpassInputAttachment"));
			_operator = Techniques::CreateFullViewportOperator(
				pool, Techniques::FullViewportOperatorSubType::DisableDepth,
				BASIC_PIXEL_HLSL ":copy_inputattachment",
				{}, GENERAL_OPERATOR_PIPELINE ":GraphicsMain",
				fbTarget, usi);
		}
	};

	void ForwardLightingCaptures::DoToneMap(LightingTechniqueIterator& iterator)
	{
		// Very simple stand-in for tonemap -- just use a copy shader to write the HDR values directly to the LDR texture
		Techniques::PixelOutputStates outputStates;
		outputStates.Bind(iterator._rpi);
		outputStates.Bind(Techniques::CommonResourceBox::s_dsDisable);
		AttachmentBlendDesc blendStates[] { Techniques::CommonResourceBox::s_abOpaque };
		outputStates.Bind(MakeIteratorRange(blendStates));
		auto& standin = ConsoleRig::FindCachedBox<ToneMapStandin>(
			iterator._parsingContext->GetTechniqueContext()._graphicsPipelinePool,
			outputStates);
		auto* pipeline = standin._operator->TryActualize();
		if (pipeline) {
			UniformsStream us;
			IResourceView* srvs[] = { iterator._rpi.GetInputAttachmentView(0).get() };
			us._resourceViews = MakeIteratorRange(srvs);
			(*pipeline)->Draw(*iterator._threadContext, us);
		} 
	}

	static RenderStepFragmentInterface CreateToneMapFragment(
		std::function<void(LightingTechniqueIterator&)>&& fn,
		bool keepHDRForNextFrame = false)
	{
		RenderStepFragmentInterface fragment { RenderCore::PipelineType::Graphics };
		auto hdrInput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).Discard();
		if (keepHDRForNextFrame)
			hdrInput.FinalState(BindFlag::ShaderResource);
		auto ldrOutput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR).NoInitialState();

		Techniques::FrameBufferDescFragment::SubpassDesc subpass;
		subpass.AppendOutput(ldrOutput);
		subpass.AppendInput(hdrInput);
		subpass.SetName("tonemap");
		fragment.AddSubpass(std::move(subpass), std::move(fn));
		return fragment;
	}

	static ::Assets::PtrToMarkerPtr<SkyOperator> CreateSkyOperator(
		const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
		const Techniques::FrameBufferTarget& fbTarget,
		const SkyOperatorDesc& desc)
	{
		return ::Assets::MakeFuturePtr<SkyOperator>(desc, pipelinePool, fbTarget);
	}

	static void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, bool precisionTargets = false)
	{
		UInt2 fbSize{stitchingContext._workingProps._outputWidth, stitchingContext._workingProps._outputHeight};
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::MultisampleDepth,
				CreateDesc(
					BindFlag::DepthStencil | BindFlag::ShaderResource | BindFlag::InputAttachment, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], stitchingContext.GetSystemAttachmentFormat(Techniques::SystemAttachmentFormat::MainDepthStencil)),
					"main-depth")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::ColorHDR,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource | BindFlag::InputAttachment, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], (!precisionTargets) ? Format::R16G16B16A16_FLOAT : Format::R32G32B32A32_FLOAT),
					"color-hdr"),
				Techniques::PreregisteredAttachment::State::Initialized			// we have to register this as initialized, because some effects can use the contents from the previous frame
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

		const bool doubleBufferNormals = true;
		if (doubleBufferNormals) {
			Techniques::PreregisteredAttachment attachments[] {
				Techniques::PreregisteredAttachment {
					Techniques::AttachmentSemantics::GBufferNormal,
					CreateDesc(
						BindFlag::RenderTarget | BindFlag::ShaderResource, 0, 0, 
						TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8B8A8_SNORM),
						"gbuffer-normal"),
					Techniques::PreregisteredAttachment::State::PingPongBuffer0
				},
				Techniques::PreregisteredAttachment {
					Techniques::AttachmentSemantics::GBufferNormalPrev,
					CreateDesc(
						BindFlag::RenderTarget | BindFlag::ShaderResource, 0, 0, 
						TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8B8A8_SNORM),
						"gbuffer-normal"),
					Techniques::PreregisteredAttachment::State::PingPongBuffer1
				}
			};
			for (const auto& a:attachments)
				stitchingContext.DefineAttachment(a);
		} else {
			Techniques::PreregisteredAttachment attachments[] {
				Techniques::PreregisteredAttachment {
					Techniques::AttachmentSemantics::GBufferNormal,
					CreateDesc(
						BindFlag::RenderTarget | BindFlag::ShaderResource, 0, 0, 
						TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8B8A8_SNORM),
						"gbuffer-normal")
				},
			};
			for (const auto& a:attachments)
				stitchingContext.DefineAttachment(a);
		}
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

	static RenderStepFragmentInterface CreateForwardSceneFragment(
		std::shared_ptr<ForwardLightingCaptures> captures,
		std::shared_ptr<Techniques::ITechniqueDelegate> forwardIllumDelegate,
		bool hasSSR)
	{
		RenderStepFragmentInterface result { PipelineType::Graphics };
		auto lightResolve = result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).NoInitialState();
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

		if (hasSSR) {
			mainSubpass.AppendNonFrameBufferAttachmentView(result.DefineAttachment(ConstHash64<'SSRe', 'flec', 'tion'>::Value).NoInitialState());
			mainSubpass.AppendNonFrameBufferAttachmentView(result.DefineAttachment(ConstHash64<'SSRC', 'onfi', 'denc', 'e'>::Value).NoInitialState());
		}
		mainSubpass.SetName("MainForward");

		ParameterBox box;
		auto dominantLightOp = captures->_lightScene->GetDominantLightOperator();
		if (dominantLightOp) {
			auto shdw = captures->_lightScene->GetDominantShadowOperator();
			if (shdw) {
				// assume the shadow operator that will be associated is index 0
				Internal::MakeShadowResolveParam(shdw.value()).WriteShaderSelectors(box);
				box.SetParameter("DOMINANT_LIGHT_SHAPE", (unsigned)dominantLightOp.value()._shape | 0x20u);
			} else {
				box.SetParameter("DOMINANT_LIGHT_SHAPE", (unsigned)dominantLightOp.value()._shape);
			}
		}

		result.AddSubpass(std::move(mainSubpass), forwardIllumDelegate, Techniques::BatchFlags::Opaque|Techniques::BatchFlags::Blending, std::move(box), captures->_lightScene->CreateMainSceneResourceDelegate());
		return result;
	}

	::Assets::PtrToMarkerPtr<CompiledLightingTechnique> CreateForwardLightingTechnique(
		const std::shared_ptr<LightingEngineApparatus>& apparatus,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const AmbientLightOperatorDesc& ambientLightOperator,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachmentsInit,
		const FrameBufferProperties& fbProps)
	{
		std::promise<std::shared_ptr<ILightScene>> lightScenePromise;
		auto lightSceneFuture = lightScenePromise.get_future();
		CreateForwardLightingScene(
			std::move(lightScenePromise),
			apparatus->_pipelineAccelerators, apparatus->_lightingOperatorCollection, apparatus->_sharedDelegates, 
			apparatus->_dmShadowDescSetTemplate,
			resolveOperators, shadowGenerators, ambientLightOperator);

		auto result = std::make_shared<::Assets::MarkerPtr<CompiledLightingTechnique>>("forward-lighting-technique");
		std::vector<Techniques::PreregisteredAttachment> preregisteredAttachments { preregisteredAttachmentsInit.begin(), preregisteredAttachmentsInit.end() };
		::Assets::WhenAll(std::move(lightSceneFuture)).ThenConstructToPromise(
			result->AdoptPromise(),
			[
				A=apparatus->_pipelineAccelerators, B=apparatus->_lightingOperatorCollection, C=apparatus->_sharedDelegates,
				preregisteredAttachments=std::move(preregisteredAttachments), fbProps
			](auto&& promise, auto lightSceneActual) {
				CreateForwardLightingTechnique(
					std::move(promise),
					A, B, C,
					lightSceneActual,
					MakeIteratorRange(preregisteredAttachments), fbProps);
			});
		return result;
	}

	void CreateForwardLightingTechnique(
		std::promise<std::shared_ptr<CompiledLightingTechnique>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		std::shared_ptr<ILightScene> lightScene,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachmentsInit,
		const FrameBufferProperties& fbProps)
	{
		auto forwardLightScene = std::dynamic_pointer_cast<ForwardPlusLightScene>(lightScene);
		if (!forwardLightScene) {
			promise.set_exception(std::make_exception_ptr(std::runtime_error("Incorrect light scene type used with forward lighting delegate")));
			return;
		}

		auto hierarchicalDepthsOperatorFuture = ::Assets::MakeFuturePtr<HierarchicalDepthsOperator>(pipelinePool);
		::Assets::PtrToMarkerPtr<ScreenSpaceReflectionsOperator> ssrFuture;
		if (forwardLightScene->GetAmbientLightOperatorDesc()._ssrOperator.has_value())
			ssrFuture = ::Assets::MakeFuturePtr<ScreenSpaceReflectionsOperator>(pipelinePool, forwardLightScene->GetAmbientLightOperatorDesc()._ssrOperator.value());

		std::vector<Techniques::PreregisteredAttachment> preregisteredAttachments { preregisteredAttachmentsInit.begin(), preregisteredAttachmentsInit.end() };

		::Assets::WhenAll(hierarchicalDepthsOperatorFuture).ThenConstructToPromise(
			std::move(promise),
			[techDelBox, pipelineAccelerators, pipelinePool, ssrFuture=std::move(ssrFuture), forwardLightScene=std::move(forwardLightScene), preregisteredAttachments=std::move(preregisteredAttachments), fbProps]
			(auto&& thatPromise, auto hierarchicalDepthsOperator) {

				TRY {
					std::shared_ptr<ScreenSpaceReflectionsOperator> ssrActual;
					if (ssrFuture) {
						ssrFuture->StallWhilePending();		// a little awkward to rely on an optional future, but since we're already on a background thread, this should be ok
						ssrActual = ssrFuture->Actualize();
					}

					auto captures = std::make_shared<ForwardLightingCaptures>();
					captures->_shadowGenAttachmentPool = std::make_shared<Techniques::AttachmentPool>(pipelineAccelerators->GetDevice());
					captures->_shadowGenFrameBufferPool = Techniques::CreateFrameBufferPool();
					captures->_lightScene = forwardLightScene;
					captures->_hierarchicalDepthsOperator = hierarchicalDepthsOperator;
					captures->_ssrOperator = ssrActual;

					Techniques::FragmentStitchingContext stitchingContext { preregisteredAttachments, fbProps, Techniques::CalculateDefaultSystemFormats(*pipelinePool->GetDevice()) };
					PreregisterAttachments(stitchingContext);
					hierarchicalDepthsOperator->PreregisterAttachments(stitchingContext);
					forwardLightScene->GetLightTiler().PreregisterAttachments(stitchingContext);
					if (ssrActual)
						ssrActual->PreregisterAttachments(stitchingContext);

					auto lightingTechnique = std::make_shared<CompiledLightingTechnique>(forwardLightScene);
					lightingTechnique->_depVal = ::Assets::GetDepValSys().Make();
					lightingTechnique->_depVal.RegisterDependency(hierarchicalDepthsOperator->GetDependencyValidation());
					lightingTechnique->_depVal.RegisterDependency(forwardLightScene->GetLightTiler().GetDependencyValidation());
					if (ssrActual)
						lightingTechnique->_depVal.RegisterDependency(ssrActual->GetDependencyValidation());

					// Reset captures
					lightingTechnique->PreSequenceSetup(
						[captures](LightingTechniqueIterator& iterator) {
							auto& stitchingContext = iterator._parsingContext->GetFragmentStitchingContext();
							PreregisterAttachments(stitchingContext);
							captures->_hierarchicalDepthsOperator->PreregisterAttachments(stitchingContext);
							captures->_lightScene->GetLightTiler().PreregisterAttachments(stitchingContext);
							if (captures->_ssrOperator)
								captures->_ssrOperator->PreregisterAttachments(stitchingContext);
						});

					// Prepare shadows
					lightingTechnique->CreateDynamicSequence(
						[captures](LightingTechniqueIterator& iterator, LightingTechniqueSequence& sequence) {
							captures->DoShadowPrepare(iterator, sequence);
						});

					auto& mainSequence = lightingTechnique->CreateSequence();
					mainSequence.CreateStep_CallFunction(
						[](LightingTechniqueIterator& iterator) {
							if (iterator._deformAcceleratorPool)
								iterator._deformAcceleratorPool->SetVertexInputBarrier(*iterator._threadContext);
						});

					// Pre depth
					if (ssrActual) {
						mainSequence.CreateStep_RunFragments(CreateDepthMotionNormalFragment(techDelBox->_depthMotionNormalRoughnessDelegate));
					} else {
						mainSequence.CreateStep_RunFragments(CreateDepthMotionFragment(techDelBox->_depthMotionDelegate));
					}

					mainSequence.CreateStep_CallFunction(
						[](LightingTechniqueIterator& iterator) {
							iterator._parsingContext->GetUniformDelegateManager()->InvalidateUniforms();
							iterator._parsingContext->GetUniformDelegateManager()->BringUpToDateGraphics(*iterator._parsingContext);
						});

					// Build hierarchical depths
					mainSequence.CreateStep_RunFragments(hierarchicalDepthsOperator->CreateFragment(stitchingContext._workingProps));

					// Light tiling & configure lighting descriptors
					mainSequence.CreateStep_RunFragments(forwardLightScene->GetLightTiler().CreateInitFragment(stitchingContext._workingProps));
					mainSequence.CreateStep_RunFragments(forwardLightScene->GetLightTiler().CreateFragment(stitchingContext._workingProps));

					// Calculate SSRs
					if (ssrActual)
						mainSequence.CreateStep_RunFragments(ssrActual->CreateFragment(stitchingContext._workingProps));

					mainSequence.CreateStep_CallFunction(
						[captures](LightingTechniqueIterator& iterator) {
							captures->_lightScene->ConfigureParsingContext(*iterator._parsingContext);
						});

					// Draw main scene
					auto mainSceneFragmentRegistration = mainSequence.CreateStep_RunFragments(
						CreateForwardSceneFragment(captures, techDelBox->_forwardIllumDelegate_DisableDepthWrite, ssrActual!=nullptr));

					// Post processing
					auto toneMapFragment = CreateToneMapFragment(
						[captures](LightingTechniqueIterator& iterator) {
							captures->DoToneMap(iterator);
						});
					mainSequence.CreateStep_RunFragments(std::move(toneMapFragment));

					mainSequence.CreateStep_CallFunction(
						[captures](LightingTechniqueIterator& iterator) {
							iterator._parsingContext->_extraSequencerDescriptorSet = {0ull, nullptr};
							captures->_lightScene->_preparedShadows.clear();
							captures->_lightScene->_preparedDominantShadow = nullptr;
						});

					lightingTechnique->CompleteConstruction(pipelineAccelerators, stitchingContext);

					// Any final operators that depend on the resolved frame buffer:
					auto resolvedFB = mainSequence.GetResolvedFrameBufferDesc(mainSceneFragmentRegistration);
					auto skyOpFuture = CreateSkyOperator(pipelinePool, Techniques::FrameBufferTarget{resolvedFB.first, resolvedFB.second}, SkyOperatorDesc { SkyTextureType::Equirectangular });
					::Assets::WhenAll(skyOpFuture).ThenConstructToPromise(
						std::move(thatPromise),
						[captures, lightingTechnique](auto skyOp) {
							captures->_skyOperator = skyOp;
							captures->_lightScene->_onChangeSkyTexture.Bind(
								[weakSkyOperator=std::weak_ptr<SkyOperator>(skyOp)](auto texture) {
									auto l=weakSkyOperator.lock();
									if (l) l->SetResource(texture ? texture->GetShaderResource() : nullptr);
								});
							captures->_lightScene->_onChangeSkyTexture.Bind(
								[weakSSROperator=std::weak_ptr<ScreenSpaceReflectionsOperator>(captures->_ssrOperator)](auto texture) {
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
							lightingTechnique->_depVal.RegisterDependency(skyOp->GetDependencyValidation());
							return lightingTechnique;
						});
				} CATCH(...) {
					thatPromise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	void CreateForwardLightingScene(
		std::promise<std::shared_ptr<ILightScene>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& shadowDescSet,
		IteratorRange<const LightSourceOperatorDesc*> positionalLightOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const AmbientLightOperatorDesc& ambientLightOperator)
	{
		RasterizationLightTileOperator::Configuration tilingConfig;
		std::promise<std::shared_ptr<ForwardPlusLightScene>> specialisedPromise;
		auto specializedFuture = specialisedPromise.get_future();
		ForwardPlusLightScene::ConstructToPromise(
			std::move(specialisedPromise), pipelineAccelerators, pipelinePool, techDelBox, shadowDescSet,
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

}}

