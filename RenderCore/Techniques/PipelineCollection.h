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
		IteratorRange<const InputElementDesc*> _inputAssembly;
		IteratorRange<const MiniInputElementDesc*> _miniInputAssembly;
		Topology _topology;

		uint64_t GetHash() const;
	};

	struct PipelineLayoutOptions
	{
		std::shared_ptr<ICompiledPipelineLayout> _prebuiltPipelineLayout;
		::Assets::PtrToMarkerPtr<RenderCore::Assets::PredefinedPipelineLayout> _predefinedPipelineLayout;
		uint64_t _hashCode = 0;

		PipelineLayoutOptions() = default;
		PipelineLayoutOptions(std::shared_ptr<ICompiledPipelineLayout>);
		PipelineLayoutOptions(::Assets::PtrToMarkerPtr<RenderCore::Assets::PredefinedPipelineLayout>, uint64_t);
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
		std::shared_ptr<::Assets::Marker<GraphicsPipelineAndLayout>> CreateGraphicsPipeline(
			const PipelineLayoutOptions& pipelineLayout,
			const std::shared_ptr<GraphicsPipelineDesc>& pipelineDesc,
			IteratorRange<const ParameterBox**> selectors,
			const VertexInputStates& inputStates,
			const FrameBufferTarget& fbTarget,
			const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection = nullptr);

		std::shared_ptr<::Assets::Marker<GraphicsPipelineAndLayout>> CreateGraphicsPipeline(
			const PipelineLayoutOptions& pipelineLayout,
			const ::Assets::PtrToMarkerPtr<GraphicsPipelineDesc>& pipelineDescFuture,
			IteratorRange<const ParameterBox**> selectors,
			const VertexInputStates& inputStates,
			const FrameBufferTarget& fbTarget,
			const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection = nullptr);

		std::shared_ptr<::Assets::Marker<ComputePipelineAndLayout>> CreateComputePipeline(
			const PipelineLayoutOptions& pipelineLayout,
			StringSection<> shader,
			IteratorRange<const ParameterBox**> selectors);

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

		std::shared_ptr<::Assets::Marker<GraphicsPipelineAndLayout>> CreateGraphicsPipelineInternal(
			const PipelineLayoutOptions& pipelineLayout,
			const ::Assets::PtrToMarkerPtr<Internal::GraphicsPipelineDescWithFilteringRules>& pipelineDescWithFilteringFuture,
			IteratorRange<const ParameterBox**> selectors,
			const VertexInputStates& inputStates,
			const FrameBufferTarget& fbTarget,
			const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection);
	};

}}

