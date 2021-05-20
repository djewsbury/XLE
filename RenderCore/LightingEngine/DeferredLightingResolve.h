// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DeferredLightingDelegate.h"
#include "StandardLightOperators.h"
#include "LightScene.h"
#include "../Metal/Forward.h"
#include "../Metal/InputLayout.h"
#include <vector>
#include <memory>

namespace RenderCore { class FrameBufferDesc; }
namespace RenderCore { namespace Techniques { class RenderPassInstance; }}

namespace RenderCore { namespace LightingEngine
{
    class LightResolveOperators : public ILightSourceFactory
	{
	public:
		struct Pipeline
		{
			std::shared_ptr<Metal::GraphicsPipeline> _pipeline;
		};

		std::vector<Pipeline> _pipelines;
		std::vector<std::tuple<ILightScene::LightOperatorId, ILightScene::ShadowOperatorId, unsigned>> _operatorToPipelineMap;
		std::vector<LightSourceOperatorDesc> _operatorDescs;
		std::shared_ptr<RenderCore::ICompiledPipelineLayout> _pipelineLayout;
		Metal::BoundUniforms _boundUniforms;
		std::shared_ptr<RenderCore::IDescriptorSet> _fixedDescriptorSet;
        bool _debuggingOn = false;

		std::unique_ptr<ILightBase> CreateLightSource(ILightScene::LightOperatorId);
	};

	class IPreparedShadowResult;
	class StandardLightScene;
	class ShadowOperatorDesc;

    void ResolveLights(
		IThreadContext& threadContext,
		Techniques::ParsingContext& parsingContext,
        Techniques::RenderPassInstance& rpi,
		const LightResolveOperators& lightResolveOperators,
		StandardLightScene& lightScene,
		IteratorRange<const std::pair<unsigned, std::shared_ptr<IPreparedShadowResult>>*> preparedShadows);

	enum class GBufferType { PositionNormal, PositionNormalParameters };

    ::Assets::FuturePtr<LightResolveOperators> BuildLightResolveOperators(
		Techniques::GraphicsPipelineCollection& pipelineCollection,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowOperators,
		const FrameBufferDesc& fbDesc,
		unsigned subpassIdx,
		bool hasScreenSpaceAO,
		GBufferType gbufferType);
}}
