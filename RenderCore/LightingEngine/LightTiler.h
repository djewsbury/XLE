// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DeferredLightingResolve.h"
#include "../Metal/Forward.h"
#include "../../Assets/AssetsCore.h"
#include "../../Math/Vector.h"
#include <memory>

namespace RenderCore 
{
	class FrameBufferProperties;
	class IDevice;
	class IResourceView;
	class ICompiledPipelineLayout;
	class IThreadContext;
}

namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class PipelinePool; class ParsingContext; class SequencerUniformsHelper; }}
namespace BufferUploads { using CommandListID = uint32_t; }

namespace RenderCore { namespace LightingEngine
{
	class LightingTechniqueIterator;
	class RenderStepFragmentInterface;

	class RasterizationLightTileOperator : public std::enable_shared_from_this<RasterizationLightTileOperator>
	{
	public:
		void Execute(LightingTechniqueIterator& iterator);

		static constexpr unsigned s_gridDims = 16u;

		RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);
		RenderStepFragmentInterface CreateInitFragment(const FrameBufferProperties& fbProps);

		void PreregisterAttachments(RenderCore::Techniques::FragmentStitchingContext& stitchingContext);

		::Assets::DependencyValidation GetDependencyValidation() const { return _depVal; }

		RasterizationLightTileOperator(
			std::shared_ptr<RenderCore::Techniques::PipelinePool> pipelinePool,
			std::shared_ptr<Metal::GraphicsPipeline> prepareBitFieldPipeline,
			std::shared_ptr<ICompiledPipelineLayout> prepareBitFieldLayout);
		~RasterizationLightTileOperator();

		static void ConstructToFuture(
			::Assets::FuturePtr<RasterizationLightTileOperator>& future,
			std::shared_ptr<RenderCore::Techniques::PipelinePool> pipelinePool);

		static void Visualize(
			RenderCore::IThreadContext& threadContext, 
			RenderCore::Techniques::ParsingContext& parsingContext,
			RenderCore::Techniques::SequencerUniformsHelper& uniformHelper,
			const std::shared_ptr<RenderCore::Techniques::PipelinePool>& pipelinePool);
	
	private:
		std::shared_ptr<RenderCore::Techniques::PipelinePool> _pipelinePool;
		::Assets::DependencyValidation _depVal;
		UInt2 _lightTileBufferSize = UInt2{0,0};
		unsigned _depthLookupTableGradiations;

		std::shared_ptr<RenderCore::IResourceView> _metricsBufferUAV;
		std::shared_ptr<RenderCore::IResourceView> _metricsBufferSRV;
		std::shared_ptr<Metal::GraphicsPipeline> _prepareBitFieldPipeline;
		std::shared_ptr<ICompiledPipelineLayout> _prepareBitFieldLayout;

		LightStencilingGeometry _stencilingGeo;
	};
}}
