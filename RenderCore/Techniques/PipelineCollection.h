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
	class RenderPassInstance;
	struct FrameBufferTarget
	{
		const FrameBufferDesc* _fbDesc;
		unsigned _subpassIdx;
		uint64_t GetHash() const;
		FrameBufferTarget(const FrameBufferDesc* fbDesc = nullptr, unsigned subpassIdx = ~0u);
		FrameBufferTarget(const RenderPassInstance&);
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
			std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
			const GraphicsPipelineDesc& pipelineDesc,
			const ParameterBox& selectors,
			const VertexInputStates& inputStates,
			const FrameBufferTarget& fbTarget);

		const std::shared_ptr<IDevice>& GetDevice() { return _device; }
		uint64_t GetGUID() const { return _guid; }

		GraphicsPipelinePool(std::shared_ptr<IDevice> device);
		~GraphicsPipelinePool();
	private:
		std::shared_ptr<IDevice> _device;
		Threading::Mutex _pipelinesLock;
		std::vector<std::pair<uint64_t, ::Assets::PtrToFuturePtr<Metal::GraphicsPipeline>>> _pipelines;

		class SharedPools;
		std::shared_ptr<SharedPools> _sharedPools;
		uint64_t _guid = ~0ull;

		void ConstructToFuture(
			std::shared_ptr<::Assets::FuturePtr<Metal::GraphicsPipeline>> future,
			std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
			const GraphicsPipelineDesc& pipelineDesc,
			const ParameterBox& selectors,
			const VertexInputStates& inputStates,
			const FrameBufferTarget& fbTarget);
	};

}}

