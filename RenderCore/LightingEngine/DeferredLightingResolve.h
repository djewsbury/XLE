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
	class LightStencilingGeometry
	{
	public:
		std::shared_ptr<IResource> _geo;
		std::pair<unsigned, unsigned> _sphereOffsetAndCount;
		std::pair<unsigned, unsigned> _cubeOffsetAndCount;

		std::shared_ptr<IResource> _lowDetailHemiSphereVB;
		std::shared_ptr<IResource> _lowDetailHemiSphereIB;
		unsigned _lowDetailHemiSphereIndexCount;

		LightStencilingGeometry(IDevice& device);
		LightStencilingGeometry() = default;
	};

    class LightResolveOperators : public ILightSourceFactory
	{
	public:
		struct Pipeline
		{
			std::shared_ptr<Metal::GraphicsPipeline> _pipeline;
			LightSourceOperatorDesc::Flags::BitField _flags = 0;
			LightSourceShape _stencilingGeoShape = LightSourceShape::Directional;
		};

		std::vector<Pipeline> _pipelines;
		std::vector<std::tuple<ILightScene::LightOperatorId, ILightScene::ShadowOperatorId, unsigned>> _operatorToPipelineMap;
		std::vector<LightSourceOperatorDesc> _operatorDescs;
		std::shared_ptr<RenderCore::ICompiledPipelineLayout> _pipelineLayout;
		Metal::BoundUniforms _boundUniforms;
		std::shared_ptr<RenderCore::IDescriptorSet> _fixedDescriptorSet;
        bool _debuggingOn = false;
		LightStencilingGeometry _stencilingGeometry;

		std::unique_ptr<ILightBase> CreateLightSource(ILightScene::LightOperatorId);

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		::Assets::DependencyValidation _depVal;
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

    ::Assets::PtrToFuturePtr<LightResolveOperators> BuildLightResolveOperators(
		Techniques::PipelinePool& pipelineCollection,
		const std::shared_ptr<ICompiledPipelineLayout>& lightingOperatorLayout,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowOperators,
		const FrameBufferDesc& fbDesc,
		unsigned subpassIdx,
		bool hasScreenSpaceAO,
		GBufferType gbufferType);
}}
