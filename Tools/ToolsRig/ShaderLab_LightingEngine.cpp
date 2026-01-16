// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderLab_LightingEngine.h"
#include "../../SceneEngine/BasicLightingStateDelegate.h"		// (for SetProperty)
#include "../../RenderCore/LightingEngine/LightingDelegateUtil.h"
#include "../../RenderCore/LightingEngine/Sequence.h"
#include "../../RenderCore/LightingEngine/ForwardPlusLightScene.h"
#include "../../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../../RenderCore/LightingEngine/SSAOOperator.h"
#include "../../RenderCore/LightingEngine/ToneMapOperator.h"
#include "../../RenderCore/LightingEngine/HierarchicalDepths.h"
#include "../../RenderCore/LightingEngine/ScreenSpaceReflections.h"
#include "../../RenderCore/LightingEngine/TextureCompilerUtil.h"
#include "../../RenderCore/LightingEngine/LightTiler.h"
#include "../../RenderCore/LightingEngine/SkyOperator.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/DeferredShaderResource.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../RenderCore/Assets/TextureCompiler.h"
#include "../../Tools/ToolsRig/ShaderLab.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/AssetTraits.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../Math/Vector.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/ImpliedTyping.h"
#include "../../Formatters/FormatterUtils.h"
#include <memory>

using namespace Utility::Literals;

namespace ToolsRig
{
	using namespace RenderCore;
	constexpr uint64_t s_shadowTemplate = "ShadowTemplate"_h;

	class PrepareForwardLightScene : public std::enable_shared_from_this<PrepareForwardLightScene>
	{
	public:
		void Prerender(IThreadContext& threadContext)
		{
			_lightScene->Prerender(threadContext);
		}

		void ConfigureParsingContext(Techniques::ParsingContext& parsingContext)
		{
			bool enableSSR = false;
			_lightScene->ConfigureParsingContext(parsingContext, enableSSR);
			if (auto* dominantShadow = _lightScene->GetDominantPreparedShadow())
				parsingContext.GetUniformDelegateManager()->BindFixedDescriptorSet(s_shadowTemplate, *dominantShadow->GetDescriptorSet());
			if (_lightSceneResourceDelegate)
				parsingContext.GetUniformDelegateManager()->BindShaderResourceDelegate(_lightSceneResourceDelegate);
		}

		void ReleaseParsingContext(Techniques::ParsingContext& parsingContext)
		{
			if (_lightSceneResourceDelegate)
				parsingContext.GetUniformDelegateManager()->UnbindShaderResourceDelegate(*_lightSceneResourceDelegate);
			if (auto* dominantShadow = _lightScene->GetDominantPreparedShadow())
				parsingContext.GetUniformDelegateManager()->UnbindFixedDescriptorSet(*dominantShadow->GetDescriptorSet());
			if (_lightScene->_dynamicProbeScheduler)
				_lightScene->_dynamicProbeScheduler->ClearPreparedShadows();
			if (_lightScene->_priorityShadowScheduler)
				_lightScene->_priorityShadowScheduler->ClearPreparedShadows();
		}

		std::shared_ptr<LightingEngine::ForwardPlusLightScene> _lightScene;

		PrepareForwardLightScene(std::shared_ptr<IDevice> device, std::shared_ptr<LightingEngine::ILightScene> lightScene)
		{
			_lightScene = std::dynamic_pointer_cast<LightingEngine::ForwardPlusLightScene>(std::move(lightScene));
			if (!_lightScene)
				Throw(std::runtime_error("No light scene, or light scene is of wrong type (ForwardPlusLightScene required)"));
			_lightSceneResourceDelegate = _lightScene->CreateMainSceneResourceDelegate();
		}

		std::shared_ptr<Techniques::IShaderResourceDelegate> _lightSceneResourceDelegate;
	};

	static void ConfigureForwardLightingSelectors(ParameterBox& box, const LightingEngine::ForwardPlusLightScene& scene)
	{
		auto& lightOperatorMapping = scene.GetLightOperatorsMapping();

		std::optional<LightingEngine::ShadowOperatorDesc> dominantShadowOperator;
		if (lightOperatorMapping._dominantLightOperator != ~0u)
			if (lightOperatorMapping._dominantLightOperator < lightOperatorMapping._operatorInfos.size() && lightOperatorMapping._operatorInfos[lightOperatorMapping._dominantLightOperator]._shadowPreparerId != ~0u)
				dominantShadowOperator = lightOperatorMapping._priorityShadowPreparers[lightOperatorMapping._operatorInfos[lightOperatorMapping._dominantLightOperator]._shadowPreparerId];

		if (lightOperatorMapping._dominantLightOperator != ~0u) {
			auto uniformShapeCode = lightOperatorMapping._operatorInfos[lightOperatorMapping._dominantLightOperator]._uniformShapeCode;
			if (dominantShadowOperator) {
				// assume the shadow operator that will be associated is index 0
				LightingEngine::Internal::MakeShadowResolveParam(dominantShadowOperator.value()).WriteShaderSelectors(box);
				box.SetParameter("DOMINANT_LIGHT_SHAPE", (unsigned)uniformShapeCode | 0x20u);
			} else {
				box.SetParameter("DOMINANT_LIGHT_SHAPE", (unsigned)uniformShapeCode);
			}
			if (lightOperatorMapping._staticShadowProbesCfg) box.SetParameter("SHADOW_PROBE", 1);
			if (lightOperatorMapping._dynamicShadowProbesCfg) box.SetParameter("DYNAMIC_SHADOW_PROBE", 1);
		}
	}

	void RegisterPrepareLightScene(ToolsRig::ShaderLab& shaderLab)
	{
		shaderLab.RegisterOperation(
			"PrepareShadows",
			[](auto& formatter, auto& context, auto* sequence) {
				if (sequence) Throw(std::runtime_error("ShaderLab operation expecting to be used outside of a sequence"));

				StringSection<> keyName;
				PipelineType shadowDescSetPipelineType = PipelineType::Graphics;
				while (formatter.TryKeyedItem(keyName)) {
					if (XlEqString(keyName, "ShadowDescSetPipelineType")) {
						shadowDescSetPipelineType = AsPipelineType(RequireStringValue(formatter));
					} else
						formatter.SkipValueOrElement();
				}

				LightingEngine::ForwardPlusLightScene* forwardLightScene = nullptr;
				if (context._lightScene) forwardLightScene = (LightingEngine::ForwardPlusLightScene*)context._lightScene->QueryInterface(TypeHashCode<LightingEngine::ForwardPlusLightScene>);
				if (!forwardLightScene) Throw(std::runtime_error("Missing light scene, or incorrect type in PrepareShadows"));

				context._technique->CreateDynamicSequence(
					[forwardLightScene](auto& iterator, auto& sequence) {
						if (forwardLightScene->_priorityShadowScheduler)
							forwardLightScene->_priorityShadowScheduler->DoShadowPrepare(iterator, sequence);
						if (forwardLightScene->_dynamicProbeScheduler)
							forwardLightScene->_dynamicProbeScheduler->DoShadowPrepare(iterator, sequence);
					});
			});

		shaderLab.RegisterOperation(
			"BindLightScene",
			[](auto& formatter, auto& context, auto* sequence) {
				if (!sequence) Throw(std::runtime_error("ShaderLab operation expecting to be used in a sequence"));

				auto opStep = std::make_shared<PrepareForwardLightScene>(context._drawingApparatus->_device, context._lightScene);
				sequence->CreateStep_CallFunction(
					[opStep](auto& iterator) {
						opStep->Prerender(*iterator._threadContext);
						opStep->ConfigureParsingContext(*iterator._parsingContext);
					});

				context._techniqueFinalizers.emplace_back(
					[opStep](auto& context, auto*) {
						context._technique->CreateSequence().CreateStep_CallFunction(
							[opStep](auto& iterator) {
								opStep->ReleaseParsingContext(*iterator._parsingContext);
							});
					});

				ConfigureForwardLightingSelectors(context._forwardLightingSelectors, *opStep->_lightScene);
			});

		shaderLab.RegisterOperation(
			"PrepareTiledLights",
			[](auto& formatter, auto& context, auto* sequence) {
				if (!sequence) Throw(std::runtime_error("ShaderLab operation expecting to be used in a sequence"));

				LightingEngine::ForwardPlusLightScene* forwardLightScene = nullptr;
				if (context._lightScene) forwardLightScene = (LightingEngine::ForwardPlusLightScene*)context._lightScene->QueryInterface(TypeHashCode<LightingEngine::ForwardPlusLightScene>);
				if (!forwardLightScene) Throw(std::runtime_error("Missing light scene, or incorrect type in PrepareTiledLights"));

				// tiler
				if (!forwardLightScene->GetLightTiler()) return;

				forwardLightScene->GetLightTiler()->PreregisterAttachments(context._stitchingContext, context._fbProps);

				sequence->CreateStep_CallFunction(
					[forwardLightScene](auto& iterator) {
						forwardLightScene->GetLightTiler()->UpdatePreFragmentUniforms(iterator);
					});
				sequence->CreateStep_RunFragments(forwardLightScene->GetLightTiler()->CreateInitFragment(context._fbProps));
				sequence->CreateStep_RunFragments(forwardLightScene->GetLightTiler()->CreateFragment(context._fbProps));
				sequence->CreateStep_CallFunction(
					[forwardLightScene](auto& iterator) {
						forwardLightScene->GetLightTiler()->BarrierToReadingLayout(*iterator._threadContext);
					});
				sequence->ResolvePendingCreateFragmentSteps();
			});

		shaderLab.RegisterOperation(
			"BindBaseLightingResources",
			[](auto& formatter, auto& context, auto* sequence) {
				if (!sequence) Throw(std::runtime_error("ShaderLab operation expecting to be used in a sequence"));

				struct Helper
				{
					std::shared_ptr<Techniques::IShaderResourceDelegate> _delegate;
					std::future<std::shared_ptr<Techniques::IShaderResourceDelegate>> _futureDelegate;
				};
				auto helper = std::make_shared<Helper>();

				helper->_futureDelegate = LightingEngine::Internal::CreateDefaultSequencerResourceDelegate();
				sequence->CreateStep_CallFunction(
					[helper](auto& iterator) {
						if (!helper->_delegate && helper->_futureDelegate.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
							helper->_delegate = helper->_futureDelegate.get();
						if (helper->_delegate) iterator._parsingContext->GetUniformDelegateManager()->BindShaderResourceDelegate(helper->_delegate);
					});

				context._techniqueFinalizers.emplace_back(
					[helper](auto& context, auto*) {
						context._technique->CreateSequence().CreateStep_CallFunction(
							[helper](auto& iterator) {
								if (helper->_delegate) iterator._parsingContext->GetUniformDelegateManager()->UnbindShaderResourceDelegate(*helper->_delegate);
							});
					});
			});

		shaderLab.RegisterOperation(
			"BindIBL",
			[](auto& formatter, auto& context, auto* sequence) {
				if (!sequence) Throw(std::runtime_error("ShaderLab operation expecting to be used in a sequence"));

				LightingEngine::ForwardPlusLightScene* forwardLightScene = nullptr;
				if (context._lightScene) forwardLightScene = (LightingEngine::ForwardPlusLightScene*)context._lightScene->QueryInterface(TypeHashCode<LightingEngine::ForwardPlusLightScene>);
				if (!forwardLightScene) Throw(std::runtime_error("Missing light scene, or incorrect type in BindIBL"));
				if (!forwardLightScene->_ambientResourcesScheduler) return;		// silent fail if we're not actually configured for ambient light

				LightingEngine::SkyTextureProcessorDesc processorDesc;

				StringSection<> kn;
				while (formatter.TryKeyedItem(kn)) {
					IteratorRange<const void*> data; ImpliedTyping::TypeDesc typeDesc;
					if (Formatters::TryRawValue(formatter, data, typeDesc)) {
						SceneEngine::SetProperty(processorDesc, Hash64(kn), data, typeDesc);
					} else
						formatter.SkipValueOrElement();
				}
				
				auto processor = LightingEngine::CreateSkyTextureProcessor(processorDesc);
				forwardLightScene->_ambientResourcesScheduler->BindSkyTextureProcessor(std::move(processor));
			});
	}

	template<typename Type, typename... Params> Type MakeFutureAndActualize(Params&&... initialisers)
	{
		std::promise<Type> promise;
		auto future = promise.get_future();
		::Assets::AutoConstructToPromise(std::move(promise), std::forward<Params>(initialisers)...);
		return future.get();		// stall here
	}

	template<typename Type, typename... Params> void StallForSecondStageConstruction(Type& obj, Params&&... params)
	{
		std::promise<std::shared_ptr<Type>> promise;
		auto future = promise.get_future();
		obj.SecondStageConstruction(std::move(promise), std::forward<Params>(params)...);
		future.get();	// stall here
	}

	inline RenderCore::Techniques::FrameBufferTarget AsFrameBufferTarget(
		RenderCore::LightingEngine::Sequence& sequence,
		RenderCore::LightingEngine::Sequence::FragmentInterfaceRegistration regId)
	{
		return RenderCore::LightingEngine::Internal::AsFrameBufferTarget(sequence, regId);
	}

	void RegisterCommonLightingEngineSteps(ToolsRig::ShaderLab& shaderLab)
	{
		shaderLab.RegisterOperation(
			"HierarchicalDepths",
			[](auto& formatter, auto& context, auto* sequence) {
				if (!sequence) Throw(std::runtime_error("ShaderLab operation expecting to be used within sequence"));

				auto opStep = std::make_shared<RenderCore::LightingEngine::HierarchicalDepthsOperator>(context._drawingApparatus->_graphicsPipelinePool);
				opStep->PreregisterAttachments(context._stitchingContext, context._fbProps);
				auto reg = sequence->CreateStep_RunFragments(opStep->CreateFragment(context._fbProps));
				context._postStitchFunctions.push_back(
					[opStep, reg](auto& context, auto* sequence) {
						StallForSecondStageConstruction(*opStep, AsFrameBufferTarget(*sequence, reg));
						context._depVal.RegisterDependency(opStep->GetDependencyValidation());
					});
			});

		shaderLab.RegisterOperation(
			"SSAOOperator",
			[](auto& formatter, auto& context, auto* sequence) {
				if (!sequence) Throw(std::runtime_error("ShaderLab operation expecting to be used within sequence"));

				RenderCore::LightingEngine::AmbientOcclusionOperatorDesc desc;
				StringSection<> keyname;
				while (formatter.TryKeyedItem(keyname)) {
					if (XlEqString(keyname, "SearchSteps"))
						desc._searchSteps = Formatters::RequireCastValue<unsigned>(formatter);
					else if (XlEqString(keyname, "MaxWorldSpaceDistance"))
						desc._maxWorldSpaceDistance = Formatters::RequireCastValue<float>(formatter);
					else if (XlEqString(keyname, "SampleBothDirections"))
						desc._sampleBothDirections = Formatters::RequireCastValue<bool>(formatter);
					else if (XlEqString(keyname, "LateTemporalFiltering"))
						desc._lateTemporalFiltering = Formatters::RequireCastValue<bool>(formatter);
					else if (XlEqString(keyname, "EnableFiltering"))
						desc._enableFiltering = Formatters::RequireCastValue<bool>(formatter);
					else if (XlEqString(keyname, "EnableHierarchicalStepping"))
						desc._enableHierarchicalStepping = Formatters::RequireCastValue<bool>(formatter);
					else if (XlEqString(keyname, "ThicknessHeuristicFactor"))
						desc._thicknessHeuristicFactor = Formatters::RequireCastValue<float>(formatter);
					else
						formatter.SkipValueOrElement();
				}

				bool hasHierarchialDepths = false;
				bool hasHistoryConfidence = false;
				for (const auto& a:context._stitchingContext.GetPreregisteredAttachments()) {
					hasHierarchialDepths |= a._semantic == Techniques::AttachmentSemantics::HierarchicalDepths;
					hasHistoryConfidence |= a._semantic == Techniques::AttachmentSemantics::HistoryConfidence;
				}

				auto opStep = MakeFutureAndActualize<std::shared_ptr<RenderCore::LightingEngine::SSAOOperator>>(context._drawingApparatus->_graphicsPipelinePool, desc, RenderCore::LightingEngine::SSAOOperator::IntegrationParams{hasHierarchialDepths, hasHistoryConfidence});
				opStep->PreregisterAttachments(context._stitchingContext, context._fbProps);
				auto reg = sequence->CreateStep_RunFragments(opStep->CreateFragment(context._fbProps));
				context._postStitchFunctions.push_back(
					[opStep, reg](auto& context, auto* sequence) {
						StallForSecondStageConstruction(*opStep, AsFrameBufferTarget(*sequence, reg));
						context._depVal.RegisterDependency(opStep->GetDependencyValidation());
					});
			});

		shaderLab.RegisterOperation(
			"ToneMapAcesOperator",
			[](auto& formatter, auto& context, auto* sequence) {
				if (!sequence) Throw(std::runtime_error("ShaderLab operation expecting to be used within sequence"));

				RenderCore::LightingEngine::ToneMapAcesOperatorDesc desc;
				StringSection<> keyname;
				while (formatter.TryKeyedItem(keyname)) {
					formatter.SkipValueOrElement();
				}

				RenderCore::LightingEngine::ToneMapIntegrationParams integrationParams;
				auto opStep = MakeFutureAndActualize<std::shared_ptr<RenderCore::LightingEngine::ToneMapAcesOperator>>(context._drawingApparatus->_graphicsPipelinePool, desc, integrationParams);
				opStep->PreregisterAttachments(context._stitchingContext, context._fbProps);
				auto reg = sequence->CreateStep_RunFragments(opStep->CreateFragment(context._fbProps));
				context._postStitchFunctions.push_back(
					[opStep, reg](auto& context, auto* sequence) {
						StallForSecondStageConstruction(*opStep, AsFrameBufferTarget(*sequence, reg));
						context._depVal.RegisterDependency(opStep->GetDependencyValidation());
					});
			});

		shaderLab.RegisterOperation(
			"SSROperator",
			[](auto& formatter, auto& context, auto* sequence) {
				if (!sequence) Throw(std::runtime_error("ShaderLab operation expecting to be used within sequence"));

				RenderCore::LightingEngine::ScreenSpaceReflectionsOperatorDesc desc;
				StringSection<> keyname;
				StringSection<> ambientCubemap;
				while (formatter.TryKeyedItem(keyname)) {
					if (XlEqString(keyname, "AmbientCubemap")) {
						ambientCubemap = RequireStringValue(formatter);
					} else {
						ImpliedTyping::TypeDesc type;
						auto value = RequireRawValue(formatter, type);
						SceneEngine::SetProperty(desc, Hash64(keyname), value, type);
					}
				}

				RenderCore::LightingEngine::ScreenSpaceReflectionsOperator::IntegrationParams integrationParams;
				integrationParams._specularIBLEnabled = false;
				auto opStep = MakeFutureAndActualize<std::shared_ptr<RenderCore::LightingEngine::ScreenSpaceReflectionsOperator>>(context._drawingApparatus->_graphicsPipelinePool, desc, integrationParams);
				opStep->PreregisterAttachments(context._stitchingContext, context._fbProps);
				auto reg = sequence->CreateStep_RunFragments(opStep->CreateFragment(context._fbProps));
				context._postStitchFunctions.push_back(
					[opStep, reg](auto& context, auto* sequence) {
						StallForSecondStageConstruction(*opStep, AsFrameBufferTarget(*sequence, reg));
						context._depVal.RegisterDependency(opStep->GetDependencyValidation());
					});

				// set a sky texture
				if (!ambientCubemap.IsEmpty()) {
					RenderCore::LightingEngine::EquirectToCubemap toCubemap;
					toCubemap._filterMode = RenderCore::LightingEngine::EquirectFilterMode::ToCubeMap; 
					toCubemap._format = Format::R32G32B32_FLOAT;
					toCubemap._faceDim = 1024;
					toCubemap._mipMapFilter = RenderCore::LightingEngine::EquirectToCubemap::MipMapFilter::FromSource;

					RenderCore::Assets::TextureCompilerSource srcComponent;
					srcComponent._srcFile = ambientCubemap.AsString();

					auto request2 = RenderCore::Assets::MakeTextureCompilationRequest(
						RenderCore::LightingEngine::TextureCompiler_EquirectFilter2(toCubemap, srcComponent),
						RenderCore::Format::BC6H_UF16);

					auto ambientRawCubemap = ::Assets::ConstructToMarkerPtr<Techniques::DeferredShaderResource>(request2);

					std::weak_ptr<RenderCore::LightingEngine::ScreenSpaceReflectionsOperator> weakOp = opStep;
					::Assets::WhenAll(ambientRawCubemap).Then(
						[weakOp](auto ambientRawCubemapFuture) {
							auto l = weakOp.lock();
							if (!l) return;
							auto ambientRawCubemap = ambientRawCubemapFuture.get();
							TextureViewDesc adjustedViewDesc;
							adjustedViewDesc._mipRange._min = 2;
							auto adjustedView = ambientRawCubemap->GetShaderResource()->GetResource()->CreateTextureView(BindFlag::ShaderResource, adjustedViewDesc);
							l->SetSpecularIBL(adjustedView);
						});
				}
			});
	}
}

