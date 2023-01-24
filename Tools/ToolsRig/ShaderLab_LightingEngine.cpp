// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderLab_LightingEngine.h"
#include "../../SceneEngine/BasicLightingStateDelegate.h"		// (for SetProperty)
#include "../../RenderCore/LightingEngine/LightingDelegateUtil.h"
#include "../../RenderCore/LightingEngine/LightingEngineInitialization.h"
#include "../../RenderCore/LightingEngine/LightingEngineIterator.h"
#include "../../RenderCore/LightingEngine/ForwardPlusLightScene.h"
#include "../../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../../RenderCore/LightingEngine/SSAOOperator.h"
#include "../../RenderCore/LightingEngine/ToneMapOperator.h"
#include "../../RenderCore/LightingEngine/HierarchicalDepths.h"
#include "../../RenderCore/LightingEngine/ScreenSpaceReflections.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/DeferredShaderResource.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../RenderCore/Assets/TextureCompiler.h"
#include "../../Tools/ToolsRig/ShaderLab.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/DeferredConstruction.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../Math/Vector.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Streams/FormatterUtils.h"
#include <memory>

using namespace Utility::Literals;

namespace ToolsRig
{
	using namespace RenderCore;
	constexpr uint64_t s_shadowTemplate = "ShadowTemplate"_h;

	class PrepareForwardLightScene : public std::enable_shared_from_this<PrepareForwardLightScene>
	{
	public:
		void DoShadowPrepare(LightingEngine::LightingTechniqueIterator& iterator, LightingEngine::LightingTechniqueSequence& sequence)
		{
			if (_lightScene->_shadowScheduler)
				_lightScene->_shadowScheduler->DoShadowPrepare(iterator, sequence);
		}

		void ConfigureParsingContext(Techniques::ParsingContext& parsingContext)
		{
			bool enableSSR = false;
			_lightScene->ConfigureParsingContext(parsingContext, enableSSR);
			if (auto* dominantShadow = _lightScene->GetDominantPreparedShadow())
				parsingContext.GetUniformDelegateManager()->BindFixedDescriptorSet(s_shadowTemplate, *dominantShadow->GetDescriptorSet());
		}

		void ReleaseParsingContext(Techniques::ParsingContext& parsingContext)
		{
			if (auto* dominantShadow = _lightScene->GetDominantPreparedShadow())
				parsingContext.GetUniformDelegateManager()->UnbindFixedDescriptorSet(*dominantShadow->GetDescriptorSet());
			if (_lightScene->_shadowScheduler)
				_lightScene->_shadowScheduler->ClearPreparedShadows();
		}

		std::shared_ptr<LightingEngine::ForwardPlusLightScene> _lightScene;

		PrepareForwardLightScene(std::shared_ptr<IDevice> device, std::shared_ptr<LightingEngine::ILightScene> lightScene, PipelineType shadowDescSetPipelineType)
		{
			_lightScene = std::dynamic_pointer_cast<LightingEngine::ForwardPlusLightScene>(std::move(lightScene));
			if (!_lightScene)
				Throw(std::runtime_error("No light scene, or light scene is of wrong type (ForwardPlusLightScene required)"));
		}
	};

	void RegisterPrepareLightScene(ToolsRig::ShaderLab& shaderLab)
	{
		shaderLab.RegisterOperation(
			"PrepareLightScene",
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

				auto opStep = std::make_shared<PrepareForwardLightScene>(context._drawingApparatus->_device, context._lightScene, shadowDescSetPipelineType);
				context._technique->CreateDynamicSequence(
					[opStep](auto& iterator, auto& sequence) {
						opStep->DoShadowPrepare(iterator, sequence);
						sequence.CreateStep_CallFunction(
							[opStep=opStep.get()](auto& iterator) {
								opStep->ConfigureParsingContext(*iterator._parsingContext);
							});
					});

				context._techniqueFinalizers.emplace_back(
					[opStep](auto& context, auto*) {
						context._technique->CreateSequence().CreateStep_CallFunction(
							[opStep](auto& iterator) {
								opStep->ReleaseParsingContext(*iterator._parsingContext);
							});
					});
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
		RenderCore::LightingEngine::LightingTechniqueSequence& sequence,
		RenderCore::LightingEngine::LightingTechniqueSequence::FragmentInterfaceRegistration regId)
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
				opStep->PreregisterAttachments(context._stitchingContext);
				auto reg = sequence->CreateStep_RunFragments(opStep->CreateFragment(context._stitchingContext._workingProps));
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
						desc._searchSteps = RequireCastValue<unsigned>(formatter);
					else if (XlEqString(keyname, "MaxWorldSpaceDistance"))
						desc._maxWorldSpaceDistance = RequireCastValue<float>(formatter);
					else if (XlEqString(keyname, "SampleBothDirections"))
						desc._sampleBothDirections = RequireCastValue<bool>(formatter);
					else if (XlEqString(keyname, "LateTemporalFiltering"))
						desc._lateTemporalFiltering = RequireCastValue<bool>(formatter);
					else if (XlEqString(keyname, "EnableFiltering"))
						desc._enableFiltering = RequireCastValue<bool>(formatter);
					else if (XlEqString(keyname, "EnableHierarchicalStepping"))
						desc._enableHierarchicalStepping = RequireCastValue<bool>(formatter);
					else if (XlEqString(keyname, "ThicknessHeuristicFactor"))
						desc._thicknessHeuristicFactor = RequireCastValue<float>(formatter);
					else
						formatter.SkipValueOrElement();
				}

				bool hasHierarchialDepths = false;
				bool hasHistoryConfidence = false;
				for (const auto& a:context._stitchingContext.GetPreregisteredAttachments()) {
					hasHierarchialDepths |= a._semantic == Techniques::AttachmentSemantics::HierarchicalDepths;
					hasHistoryConfidence |= a._semantic == Techniques::AttachmentSemantics::HistoryAcc;
				}

				auto opStep = MakeFutureAndActualize<std::shared_ptr<RenderCore::LightingEngine::SSAOOperator>>(context._drawingApparatus->_graphicsPipelinePool, desc, RenderCore::LightingEngine::SSAOOperator::IntegrationParams{hasHierarchialDepths, hasHistoryConfidence});
				opStep->PreregisterAttachments(context._stitchingContext);
				auto reg = sequence->CreateStep_RunFragments(opStep->CreateFragment(context._stitchingContext._workingProps));
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

				auto opStep = MakeFutureAndActualize<std::shared_ptr<RenderCore::LightingEngine::ToneMapAcesOperator>>(context._drawingApparatus->_graphicsPipelinePool, desc);
				opStep->PreregisterAttachments(context._stitchingContext);
				auto reg = sequence->CreateStep_RunFragments(opStep->CreateFragment(context._stitchingContext._workingProps));
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
				opStep->PreregisterAttachments(context._stitchingContext);
				auto reg = sequence->CreateStep_RunFragments(opStep->CreateFragment(context._stitchingContext._workingProps));
				context._postStitchFunctions.push_back(
					[opStep, reg](auto& context, auto* sequence) {
						StallForSecondStageConstruction(*opStep, AsFrameBufferTarget(*sequence, reg));
						context._depVal.RegisterDependency(opStep->GetDependencyValidation());
					});

				// set a sky texture
				if (!ambientCubemap.IsEmpty()) {
					RenderCore::Assets::TextureCompilationRequest request2;
					request2._operation = RenderCore::Assets::TextureCompilationRequest::Operation::EquRectToCubeMap; 
					request2._srcFile = ambientCubemap.AsString();
					request2._format = Format::BC6H_UF16;
					request2._faceDim = 1024;
					request2._mipMapFilter = RenderCore::Assets::TextureCompilationRequest::MipMapFilter::FromSource;
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

