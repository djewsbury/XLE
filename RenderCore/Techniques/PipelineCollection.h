// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TechniqueDelegates.h"
#include "../FrameBufferDesc.h"
#include "../StateDesc.h"
#include "../Types.h"
#include "../Metal/Forward.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/DepVal.h"
#include "../../Utility/Threading/Mutex.h"
#include <vector>

namespace RenderCore { namespace Techniques
{
	struct FrameBufferTarget
	{
		const RenderCore::FrameBufferDesc* _fbDesc;
		unsigned _subpassIdx = ~0u;
		uint64_t GetHash() const;
	};

    struct PixelOutputStates
	{
		FrameBufferTarget _fbTarget;
		// DepthStencilDesc _depthStencil;
		// RasterizationDesc _rasterization;
		// IteratorRange<const AttachmentBlendDesc*> _attachmentBlend;

		uint64_t GetHash() const;
	};

	struct VertexInputStates
	{
		IteratorRange<const MiniInputElementDesc*> _inputLayout;
		Topology _topology;

		uint64_t GetHash() const;
	};

    class GraphicsPipelinePool
	{
	public:
		::Assets::PtrToFuturePtr<Metal::GraphicsPipeline> CreatePipeline(
			const GraphicsPipelineDesc& pipelineDesc,
			const ParameterBox& selectors,
			const VertexInputStates& inputStates,
			const PixelOutputStates& outputStates);

        const std::shared_ptr<ICompiledPipelineLayout>& GetPipelineLayout() { return _pipelineLayout; }
		const std::shared_ptr<IDevice>& GetDevice() { return _device; }
		uint64_t GetGUID() const;

		GraphicsPipelinePool(
			std::shared_ptr<IDevice> device,
			std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
			const ::Assets::DependencyValidation& pipelineLayoutDepVal = {});
		~GraphicsPipelinePool();
	private:
		std::shared_ptr<IDevice> _device;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		::Assets::DependencyValidation _pipelineLayoutDepVal;
		Threading::Mutex _pipelinesLock;
		std::vector<std::pair<uint64_t, ::Assets::PtrToFuturePtr<Metal::GraphicsPipeline>>> _pipelines;

		class SharedPools;
		std::shared_ptr<SharedPools> _sharedPools;

		void ConstructToFuture(
			std::shared_ptr<::Assets::FuturePtr<Metal::GraphicsPipeline>> future,
			const GraphicsPipelineDesc& pipelineDesc,
			const ParameterBox& selectors,
			const VertexInputStates& inputStates,
			const PixelOutputStates& outputStates);
	};

}}

