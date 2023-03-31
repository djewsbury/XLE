// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DeferredLightingResolve.h"		// for LightStencilingGeometry
#include "../Metal/Forward.h"
#include "../Metal/InputLayout.h"
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

namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class PipelineCollection; class ParsingContext; }}
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}

namespace RenderCore { namespace LightingEngine
{
	class SequenceIterator;
	class RenderStepFragmentInterface;

	struct RasterizationLightTileOperatorDesc
	{
		unsigned _maxLightsPerView = 256u;
		unsigned _depthLookupGradiations = 1024u;
		uint64_t GetHash(uint64_t = DefaultSeed64) const;
	};

	class RasterizationLightTileOperator : public std::enable_shared_from_this<RasterizationLightTileOperator>
	{
	public:
		void Execute(SequenceIterator& iterator);

		void SetLightScene(Internal::StandardLightScene& lightScene);
		Internal::StandardLightScene& GetLightScene() { return *_lightScene; }

		struct Outputs
		{
			std::vector<unsigned> _lightOrdering;
			std::vector<unsigned> _lightDepthTable;
			unsigned _lightCount = 0;
			std::shared_ptr<IResourceView> _tiledLightBitFieldSRV;
		};
		Outputs _outputs;

		RasterizationLightTileOperatorDesc GetConfiguration() const { return _config; }

		RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);
		RenderStepFragmentInterface CreateInitFragment(const FrameBufferProperties& fbProps);

		void PreregisterAttachments(RenderCore::Techniques::FragmentStitchingContext& stitchingContext, const FrameBufferProperties& fbProps);
		void CompleteInitialization(IThreadContext& threadContext);
		void BarrierToReadingLayout(IThreadContext& threadContext);

		::Assets::DependencyValidation GetDependencyValidation() const { return _depVal; }

		RasterizationLightTileOperator(
			std::shared_ptr<RenderCore::Techniques::PipelineCollection> pipelinePool,
			std::shared_ptr<Metal::GraphicsPipeline> prepareBitFieldPipeline,
			std::shared_ptr<ICompiledPipelineLayout> prepareBitFieldLayout,
			const RasterizationLightTileOperatorDesc& config);
		~RasterizationLightTileOperator();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<RasterizationLightTileOperator>>&& promise,
			std::shared_ptr<RenderCore::Techniques::PipelineCollection> pipelinePool,
			const RasterizationLightTileOperatorDesc& config);

		static void Visualize(
			RenderCore::Techniques::ParsingContext& parsingContext,
			const std::shared_ptr<RenderCore::Techniques::PipelineCollection>& pipelinePool);
	
	private:
		std::shared_ptr<RenderCore::Techniques::PipelineCollection> _pipelinePool;

		std::shared_ptr<RenderCore::IResourceView> _metricsBufferUAV;
		std::shared_ptr<RenderCore::IResourceView> _metricsBufferSRV;
		std::shared_ptr<Metal::GraphicsPipeline> _prepareBitFieldPipeline;
		std::shared_ptr<ICompiledPipelineLayout> _prepareBitFieldLayout;

		std::shared_ptr<RenderCore::IResource> _tileableLightBuffer[3];
		std::shared_ptr<RenderCore::IResourceView> _tileableLightBufferUAV[dimof(_tileableLightBuffer)];
		unsigned _pingPongCounter = 0u;

		Metal::BoundUniforms _prepareBitFieldBoundUniforms;

		LightStencilingGeometry _stencilingGeo;

		Internal::StandardLightScene* _lightScene;
		RasterizationLightTileOperatorDesc _config;
		UInt2 _lightTileBufferSize = UInt2{0,0};
		::Assets::DependencyValidation _depVal;
	};
}}
