// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../FrameBufferDesc.h"
#include "../StateDesc.h"
#include "../Types.h"
#include "../Metal/Forward.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/Threading/Mutex.h"
#include <vector>

namespace RenderCore { namespace Techniques
{
	class FrameBufferTarget
	{
	public:
		const RenderCore::FrameBufferDesc* _fbDesc;
		unsigned _subpassIdx = ~0u;
		uint64_t GetHash() const;
	};

    struct PixelOutputStates
	{
		FrameBufferTarget _fbTarget;
		DepthStencilDesc _depthStencil;
		RasterizationDesc _rasterization;
		IteratorRange<const AttachmentBlendDesc*> _attachmentBlend;

		uint64_t GetHash() const;
	};

	struct VertexInputStates
	{
		IteratorRange<const MiniInputElementDesc*> _inputLayout;
		Topology _topology;

		uint64_t GetHash() const;
	};

    class GraphicsPipelineCollection
	{
	public:
		::Assets::PtrToFuturePtr<Metal::GraphicsPipeline> CreatePipeline(
			StringSection<> vsName, StringSection<> vsDefines,
			StringSection<> psName, StringSection<> psDefines,
			const VertexInputStates& inputStates,
			const PixelOutputStates& outputStates);

		::Assets::PtrToFuturePtr<Metal::GraphicsPipeline> CreatePipeline(
			StringSection<> vsName, StringSection<> vsDefines,
			StringSection<> gsName, StringSection<> gsDefines,
			StringSection<> psName, StringSection<> psDefines,
			const VertexInputStates& inputStates,
			const PixelOutputStates& outputStates);

        const std::shared_ptr<ICompiledPipelineLayout>& GetPipelineLayout() { return _pipelineLayout; }
		const std::shared_ptr<IDevice>& GetDevice() { return _device; }

		GraphicsPipelineCollection(
			std::shared_ptr<IDevice> device,
			std::shared_ptr<ICompiledPipelineLayout> pipelineLayout);

	private:
		std::shared_ptr<IDevice> _device;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		Threading::Mutex _pipelinesLock;
		std::vector<std::pair<uint64_t, ::Assets::PtrToFuturePtr<Metal::GraphicsPipeline>>> _pipelines;

		void ConstructToFuture(
			std::shared_ptr<::Assets::FuturePtr<Metal::GraphicsPipeline>> future,
			StringSection<> vsName, StringSection<> vsDefines,
			StringSection<> psName, StringSection<> psDefines,
			const VertexInputStates& inputStates,
			const PixelOutputStates& outputStates);

		void ConstructToFuture(
			std::shared_ptr<::Assets::FuturePtr<Metal::GraphicsPipeline>> future,
			StringSection<> vsName, StringSection<> vsDefines,
			StringSection<> gsName, StringSection<> gsDefines,
			StringSection<> psName, StringSection<> psDefines,
			const VertexInputStates& inputStates,
			const PixelOutputStates& outputStates);
	};

}}

