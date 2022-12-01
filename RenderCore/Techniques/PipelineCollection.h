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
#include <future>

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
		IteratorRange<const InputElementDesc*> _inputAssembly;
		IteratorRange<const MiniInputElementDesc*> _miniInputAssembly;
		Topology _topology;

		uint64_t GetHash() const;
	};

	struct PipelineLayoutOptions
	{
		std::shared_ptr<ICompiledPipelineLayout> _prebuiltPipelineLayout;
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout> _predefinedPipelineLayout;
		uint64_t _hashCode = 0;
		std::string _name;

		PipelineLayoutOptions() = default;
		PipelineLayoutOptions(std::shared_ptr<ICompiledPipelineLayout>);
		PipelineLayoutOptions(std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout>, uint64_t, std::string);
	};

	struct GraphicsPipelineAndLayout
	{
		std::shared_ptr<Metal::GraphicsPipeline> _pipeline;
		std::shared_ptr<ICompiledPipelineLayout> _layout;
		::Assets::DependencyValidation _depVal;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		#if defined(_DEBUG)
			struct DebugInfo
			{
				std::string _vsDescription, _psDescription, _gsDescription;	
			};
			DebugInfo _debugInfo;
		#endif
	};

	struct ComputePipelineAndLayout
	{
		std::shared_ptr<Metal::ComputePipeline> _pipeline;
		std::shared_ptr<ICompiledPipelineLayout> _layout;
		::Assets::DependencyValidation _depVal;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
	};

	namespace Internal { class SharedPools; class GraphicsPipelineDescWithFilteringRules; }

    class PipelineCollection
	{
	public:
		void CreateGraphicsPipeline(
			std::promise<GraphicsPipelineAndLayout>&& promise,
			PipelineLayoutOptions&& pipelineLayout,
			const std::shared_ptr<GraphicsPipelineDesc>& pipelineDesc,
			IteratorRange<const ParameterBox*const*> selectors,
			const VertexInputStates& inputStates,
			const FrameBufferTarget& fbTarget,
			const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection = nullptr);

		void CreateGraphicsPipeline(
			std::promise<GraphicsPipelineAndLayout>&& promise,
			PipelineLayoutOptions&& pipelineLayout,
			std::shared_future<std::shared_ptr<GraphicsPipelineDesc>> pipelineDescFuture,
			IteratorRange<const ParameterBox*const*> selectors,
			const VertexInputStates& inputStates,
			const FrameBufferTarget& fbTarget,
			const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection = nullptr);

		void CreateComputePipeline(
			std::promise<ComputePipelineAndLayout>&& promise,
			PipelineLayoutOptions&& pipelineLayout,
			StringSection<> shader,
			IteratorRange<const ParameterBox*const*> selectors,
			const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection = nullptr,
			IteratorRange<const uint64_t*> patchExpansions = {});

		const std::shared_ptr<IDevice>& GetDevice() { return _device; }
		uint64_t GetGUID() const { return _guid; }

		struct Metrics
		{
			unsigned _graphicsPipelineCount;
			unsigned _computePipelineCount;
		};
		Metrics GetMetrics() const;

		PipelineCollection(std::shared_ptr<IDevice> device);
		~PipelineCollection();
	private:
		std::shared_ptr<IDevice> _device;
		uint64_t _guid = ~0ull;
		std::shared_ptr<Internal::SharedPools> _sharedPools;

		void CreateGraphicsPipelineInternal(
			std::promise<GraphicsPipelineAndLayout>&& promise,
			PipelineLayoutOptions&& pipelineLayout,
			std::shared_future<std::shared_ptr<Internal::GraphicsPipelineDescWithFilteringRules>> pipelineDescWithFilteringFuture,
			IteratorRange<const ParameterBox*const*> selectors,
			const VertexInputStates& inputStates,
			const FrameBufferTarget& fbTarget,
			const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection);
	};

}}

