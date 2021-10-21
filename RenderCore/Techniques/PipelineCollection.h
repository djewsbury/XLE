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
	class CommonResourceBox;

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

    class PipelinePool
	{
	public:
		struct GraphicsPipelineAndLayout
		{
			std::shared_ptr<Metal::GraphicsPipeline> _pipeline;
			std::shared_ptr<ICompiledPipelineLayout> _layout;

			const ::Assets::DependencyValidation& GetDependencyValidation() const;
		};
		std::shared_ptr<::Assets::Future<GraphicsPipelineAndLayout>> CreateGraphicsPipeline(
			std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
			const GraphicsPipelineDesc& pipelineDesc,
			const ParameterBox& selectors,
			const VertexInputStates& inputStates,
			const FrameBufferTarget& fbTarget);

		struct ComputePipelineAndLayout
		{
			std::shared_ptr<Metal::ComputePipeline> _pipeline;
			std::shared_ptr<ICompiledPipelineLayout> _layout;

			const ::Assets::DependencyValidation& GetDependencyValidation() const;
		};
		std::shared_ptr<::Assets::Future<ComputePipelineAndLayout>> CreateComputePipeline(
			std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
			StringSection<> shader,
			const ParameterBox& selectors);

		std::shared_ptr<::Assets::Future<ComputePipelineAndLayout>> CreateComputePipeline(
			std::shared_ptr<::Assets::Future<RenderCore::Assets::PredefinedPipelineLayout>> futurePipelineLayout,
			StringSection<> shader,
			const ParameterBox& selectors);

		std::shared_ptr<::Assets::Future<ComputePipelineAndLayout>> CreateComputePipeline(
			StringSection<> shader,
			const ParameterBox& selectors);

		const std::shared_ptr<IDevice>& GetDevice() { return _device; }
		uint64_t GetGUID() const { return _guid; }

		PipelinePool(std::shared_ptr<IDevice> device);
		~PipelinePool();
	private:
		std::shared_ptr<IDevice> _device;
		Threading::Mutex _pipelinesLock;
		std::vector<std::pair<uint64_t, std::shared_ptr<::Assets::Future<GraphicsPipelineAndLayout>>>> _graphicsPipelines;
		std::vector<std::pair<uint64_t, std::shared_ptr<::Assets::Future<ComputePipelineAndLayout>>>> _computePipelines;

		class SharedPools;
		std::shared_ptr<SharedPools> _sharedPools;
		uint64_t _guid = ~0ull;

		void ConstructToFuture(
			::Assets::Future<GraphicsPipelineAndLayout>& future,
			std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
			const GraphicsPipelineDesc& pipelineDesc,
			const ParameterBox& selectors,
			const VertexInputStates& inputStates,
			const FrameBufferTarget& fbTarget);

		void ConstructToFuture(
			::Assets::Future<ComputePipelineAndLayout>& future,
			std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
			StringSection<> shader,
			const ParameterBox& selectors);

		void ConstructToFuture(
			::Assets::Future<ComputePipelineAndLayout>& future,
			std::shared_ptr<::Assets::Future<RenderCore::Assets::PredefinedPipelineLayout>> futurePipelineLayout,
			StringSection<> shader,
			const ParameterBox& selectors);

		void ConstructToFuture(
			::Assets::Future<ComputePipelineAndLayout>& future,
			StringSection<> shader,
			const ParameterBox& selectors);
	};

}}

