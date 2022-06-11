// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderLab_LightingEngine.h"
#include "../../RenderCore/LightingEngine/LightingDelegateUtil.h"
#include "../../RenderCore/LightingEngine/LightingEngineInitialization.h"
#include "../../RenderCore/LightingEngine/LightingEngineIterator.h"
#include "../../RenderCore/LightingEngine/ForwardPlusLightScene.h"
#include "../../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
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
				parsingContext._extraSequencerDescriptorSet = {s_shadowTemplate, _preparedDominantShadow->GetDescriptorSet().get()};
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
}

