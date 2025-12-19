// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DeferredLightingDelegate.h"
#include "StandardLightOperators.h"
#include "ILightScene.h"
#include "StencilingGeometry.h"
#include "../Metal/Forward.h"
#include "../Metal/InputLayout.h"
#include <vector>
#include <memory>

namespace RenderCore { class FrameBufferDesc; }
namespace RenderCore { namespace Techniques { class RenderPassInstance; }}
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}

namespace RenderCore { namespace LightingEngine
{
	namespace Internal { class ILightBase; }

    class LightResolveOperators
	{
	public:
		struct Pipeline
		{
			std::shared_ptr<Metal::GraphicsPipeline> _pipeline;
			PositionalLightOperatorDesc::Flags::BitField _flags = 0;
			LightSourceShape _stencilingGeoShape = LightSourceShape::Directional;
		};

		std::vector<Pipeline> _pipelines;
		std::vector<std::tuple<ILightScene::LightOperatorId, unsigned, unsigned>> _operatorToPipelineMap;
		std::vector<PositionalLightOperatorDesc> _operatorDescs;
		std::shared_ptr<RenderCore::ICompiledPipelineLayout> _pipelineLayout;
		Metal::BoundUniforms _boundUniforms;
		std::shared_ptr<RenderCore::IDescriptorSet> _fixedDescriptorSet;
        bool _debuggingOn = false;
		LightStencilingGeometry _stencilingGeometry;
		bool _enableShadowProbes = false;
		BufferUploads::CommandListID _completionCommandList = 0;

		// std::unique_ptr<Internal::ILightBase> CreateLightSource(ILightScene::LightOperatorId);

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		::Assets::DependencyValidation _depVal;
	};

	class IPreparedShadowResult;
	struct ShadowOperatorDesc;
	namespace Internal { class StandardLightScene; }
	class ShadowProbes;
	namespace Internal { class PriorityShadowProjectionScheduler; class SemiStaticShadowProbeScheduler; }

    void ResolveLights(
		IThreadContext& threadContext,
		Techniques::ParsingContext& parsingContext,
        Techniques::RenderPassInstance& rpi,
		const LightResolveOperators& lightResolveOperators,
		Internal::StandardLightScene& lightScene,
		Internal::PriorityShadowProjectionScheduler* shadowProjectionScheduler,
		ShadowProbes* shadowProbes,
		Internal::SemiStaticShadowProbeScheduler* shadowProbeScheduler);

    std::future<std::shared_ptr<LightResolveOperators>> BuildLightResolveOperators(
		Techniques::PipelineCollection& pipelineCollection,
		const std::shared_ptr<ICompiledPipelineLayout>& lightingOperatorLayout,
		IteratorRange<const PositionalLightOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowOperators,
		const FrameBufferDesc& fbDesc,
		unsigned subpassIdx,
		bool hasScreenSpaceAO,
		unsigned gbufferTypeCode);
}}
