// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StencilingGeometry.h"
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

namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class PipelineCollection; class ParsingContext; }}
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}
namespace std { template<typename T> class promise; }

namespace RenderCore { namespace LightingEngine
{
	class SequenceIterator;
	class RenderStepFragmentInterface;

	struct RasterizationLightTileOperatorDesc
	{
		unsigned _maxLightsPerView = 256u;
		unsigned _depthLookupGradiations = 1024u;
		bool _copyOutOfSharedMemory = true;			// adds an additional copy for the light table from CPU accessible memory to GPU only memory
		uint64_t GetHash(uint64_t = DefaultSeed64) const;
	};

	class RasterizationLightTileOperator : public std::enable_shared_from_this<RasterizationLightTileOperator>
	{
	public:
		void Execute(SequenceIterator& iterator);

		struct InactiveLight
		{
			Float3 _position; float _cutoffRange;
			unsigned _srcId;
		};
		struct IntermediateLight
		{
			Float3 _position; float _cutoffRange;
			float _linearizedDepthMin, _linearizedDepthMax; 
			unsigned _srcId; unsigned _dummy;
		};
		std::vector<IntermediateLight> _activeLights[2];
		std::vector<InactiveLight> _inactiveLights[2];

		void AddLight(Float3 position, float cutoffRange, unsigned srcId);
		void UpdateLight(Float3 position, float cutoffRange, unsigned srcId);
		void RemoveLight(unsigned srcId);

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
		std::shared_ptr<RenderCore::IResource> _unmapTileableLightBuffer;
		std::shared_ptr<RenderCore::IResourceView> _tileableLightBufferUAV[dimof(_tileableLightBuffer)];
		unsigned _frameCounter = 0u;

		std::unique_ptr<Metal::BoundUniforms> _prepareBitFieldBoundUniforms;

		LightStencilingGeometry _stencilingGeo;

		RasterizationLightTileOperatorDesc _config;
		UInt2 _lightTileBufferSize = UInt2{0,0};
		::Assets::DependencyValidation _depVal;
	};
}}
