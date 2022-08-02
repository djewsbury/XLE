// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderLab_LightingEngine.h"
#include "../../RenderCore/LightingEngine/LightingDelegateUtil.h"
#include "../../RenderCore/LightingEngine/LightingEngineInitialization.h"
#include "../../RenderCore/LightingEngine/LightingEngineIterator.h"
#include "../../RenderCore/LightingEngine/ForwardPlusLightScene.h"
#include "../../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../../RenderCore/LightingEngine/SSAOOperator.h"
#include "../../RenderCore/LightingEngine/HierarchicalDepths.h"
#include "../../RenderCore/LightingEngine/ScreenSpaceReflections.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/DeferredShaderResource.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Assets/TextureCompiler.h"
#include "../../Tools/ToolsRig/ShaderLab.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/DeferredConstruction.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../Math/Vector.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Streams/FormatterUtils.h"
#include <memory>

namespace ToolsRig
{
	using namespace RenderCore;
	static const uint64_t s_shadowTemplate = Utility::Hash64("ShadowTemplate");

	class PrepareForwardLightScene : public std::enable_shared_from_this<PrepareForwardLightScene>
	{
	public:
		void DoShadowPrepare(LightingEngine::LightingTechniqueIterator& iterator, LightingEngine::LightingTechniqueSequence& sequence)
		{
			sequence.Reset();
			_preparedShadows.clear();
			_preparedDominantShadow = nullptr;
			if (_lightScene->_shadowPreparers->_preparers.empty()) return;

			_preparedShadows.reserve(_lightScene->_dynamicShadowProjections.size());
			LightingEngine::ILightScene::LightSourceId prevLightId = ~0u; 
			for (unsigned c=0; c<_lightScene->_dynamicShadowProjections.size(); ++c) {
				_preparedShadows.push_back(std::make_pair(
					_lightScene->_dynamicShadowProjections[c]._lightId,
					LightingEngine::Internal::SetupShadowPrepare(
						iterator, sequence, 
						*_lightScene->_dynamicShadowProjections[c]._desc, 
						*_lightScene, _lightScene->_dynamicShadowProjections[c]._lightId,
						_shadowDescSetPipelineType,
						*_shadowGenFrameBufferPool, *_shadowGenAttachmentPool)));

				// shadow entries must be sorted by light id
				assert(prevLightId == ~0u || prevLightId < _lightScene->_dynamicShadowProjections[c]._lightId);
				prevLightId = _lightScene->_dynamicShadowProjections[c]._lightId;
			}

			if (_lightScene->_dominantShadowProjection._desc) {
				assert(_lightScene->_dominantLightSet._lights.size() == 1);
				_preparedDominantShadow =
					LightingEngine::Internal::SetupShadowPrepare(
						iterator, sequence, 
						*_lightScene->_dominantShadowProjection._desc, 
						*_lightScene, _lightScene->_dominantLightSet._lights[0]._id,
						_shadowDescSetPipelineType,
						*_shadowGenFrameBufferPool, *_shadowGenAttachmentPool);
			}
		}

		void ConfigureParsingContext(Techniques::ParsingContext& parsingContext)
		{
			_lightScene->ConfigureParsingContext(parsingContext);
			if (_preparedDominantShadow) {
				// find the prepared shadow associated with the dominant light (if it exists) and make sure it's descriptor set is accessible
				assert(!parsingContext._extraSequencerDescriptorSet.second);
				parsingContext._extraSequencerDescriptorSet = { s_shadowTemplate, _preparedDominantShadow->GetDescriptorSet() };
			}
		}

		std::shared_ptr<LightingEngine::ForwardPlusLightScene> _lightScene;

		PrepareForwardLightScene(std::shared_ptr<IDevice> device, std::shared_ptr<LightingEngine::ILightScene> lightScene, PipelineType shadowDescSetPipelineType)
		{
			_lightScene = std::dynamic_pointer_cast<LightingEngine::ForwardPlusLightScene>(std::move(lightScene));
			if (!_lightScene)
				Throw(std::runtime_error("No light scene, or light scene is of wrong type (ForwardPlusLightScene required)"));

			_shadowGenAttachmentPool = std::make_shared<Techniques::AttachmentPool>(device);
			_shadowGenFrameBufferPool = Techniques::CreateFrameBufferPool();
			_shadowDescSetPipelineType = shadowDescSetPipelineType;
		}

		std::shared_ptr<Techniques::FrameBufferPool> _shadowGenFrameBufferPool;
		std::shared_ptr<Techniques::AttachmentPool> _shadowGenAttachmentPool;
		PipelineType _shadowDescSetPipelineType;

		// frame temporaries
		std::vector<std::pair<unsigned, std::shared_ptr<LightingEngine::IPreparedShadowResult>>> _preparedShadows;
		std::shared_ptr<LightingEngine::IPreparedShadowResult> _preparedDominantShadow;
	};

	void RegisterPrepareLightScene(ToolsRig::ShaderLab& shaderLab)
	{
		shaderLab.RegisterOperation(
			"PrepareLightScene",
			[](Formatters::IDynamicFormatter& formatter, ToolsRig::ShaderLab::OperationConstructorContext& context) {

				StringSection<> keyName;
				PipelineType shadowDescSetPipelineType = PipelineType::Graphics;
				while (formatter.TryKeyedItem(keyName)) {
					if (XlEqString(keyName, "ShadowDescSetPipelineType")) {
						shadowDescSetPipelineType = AsPipelineType(RequireStringValue(formatter));
					} else
						formatter.SkipValueOrElement();
				}

				auto opStep = std::make_shared<PrepareForwardLightScene>(context._drawingApparatus->_device, context._lightScene, shadowDescSetPipelineType);
				context._dynamicSequenceFunctions.emplace_back(
					[opStep](auto& iterator, auto& sequence) {
						opStep->DoShadowPrepare(iterator, sequence);
					});
				context._setupFunctions.emplace_back(
					[opStep](auto& sequence) {
						sequence.CreateStep_CallFunction(
							[opStep](auto& iterator) {
								opStep->ConfigureParsingContext(*iterator._parsingContext);
							});
					});
			});
	}

	template<typename Type, typename... Params> Type MakeFutureAndActualize(Params... initialisers)
	{
		std::promise<Type> promise;
		auto future = promise.get_future();
		::Assets::AutoConstructToPromise(std::move(promise), std::forward<Params>(initialisers)...);
		return future.get();		// stall here
	}

	void RegisterCommonLightingEngineSteps(ToolsRig::ShaderLab& shaderLab)
	{
		shaderLab.RegisterOperation(
			"HierarchicalDepths",
			[](Formatters::IDynamicFormatter& formatter, ToolsRig::ShaderLab::OperationConstructorContext& context) {
				auto opStep = MakeFutureAndActualize<std::shared_ptr<RenderCore::LightingEngine::HierarchicalDepthsOperator>>(context._drawingApparatus->_graphicsPipelinePool);
				opStep->PreregisterAttachments(context._stitchingContext);
				context._setupFunctions.push_back(
					[opStep, fbProps=context._stitchingContext._workingProps](auto& sequence) {
						sequence.CreateStep_RunFragments(opStep->CreateFragment(fbProps));
					});
				context._depVal.RegisterDependency(opStep->GetDependencyValidation());
				context._completionCommandList = std::max(opStep->GetCompletionCommandList(), context._completionCommandList);
			});

		shaderLab.RegisterOperation(
			"SSAOOperator",
			[](Formatters::IDynamicFormatter& formatter, ToolsRig::ShaderLab::OperationConstructorContext& context) {
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
				for (const auto& a:context._stitchingContext.GetPreregisteredAttachments())
					hasHierarchialDepths |= a._semantic == Techniques::AttachmentSemantics::HierarchicalDepths;

				auto opStep = MakeFutureAndActualize<std::shared_ptr<RenderCore::LightingEngine::SSAOOperator>>(context._drawingApparatus->_graphicsPipelinePool, desc, hasHierarchialDepths);
				opStep->PreregisterAttachments(context._stitchingContext);
				context._setupFunctions.push_back(
					[opStep, fbProps=context._stitchingContext._workingProps](auto& sequence) {
						sequence.CreateStep_RunFragments(opStep->CreateFragment(fbProps));
					});
				context._depVal.RegisterDependency(opStep->GetDependencyValidation());
				context._completionCommandList = std::max(opStep->GetCompletionCommandList(), context._completionCommandList);
			});

		shaderLab.RegisterOperation(
			"SSROperator",
			[](Formatters::IDynamicFormatter& formatter, ToolsRig::ShaderLab::OperationConstructorContext& context) {
				RenderCore::LightingEngine::ScreenSpaceReflectionsOperatorDesc desc;
				StringSection<> keyname;
				StringSection<> ambientCubemap;
				while (formatter.TryKeyedItem(keyname)) {
					if (XlEqString(keyname, "EnableFinalBlur"))
						desc._enableFinalBlur = RequireCastValue<bool>(formatter);
					else if (XlEqString(keyname, "SplitConfidence"))
						desc._splitConfidence = RequireCastValue<bool>(formatter);
					else if (XlEqString(keyname, "AmbientCubemap"))
						ambientCubemap = RequireStringValue(formatter);
					else
						formatter.SkipValueOrElement();
				}

				auto opStep = MakeFutureAndActualize<std::shared_ptr<RenderCore::LightingEngine::ScreenSpaceReflectionsOperator>>(context._drawingApparatus->_graphicsPipelinePool, desc);
				opStep->PreregisterAttachments(context._stitchingContext);
				context._setupFunctions.push_back(
					[opStep, fbProps=context._stitchingContext._workingProps](auto& sequence) {
						sequence.CreateStep_RunFragments(opStep->CreateFragment(fbProps));
					});
				context._depVal.RegisterDependency(opStep->GetDependencyValidation());

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

