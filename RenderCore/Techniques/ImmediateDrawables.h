// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/RawMaterial.h"		// for RenderStateSet
#include "../Types.h"
#include "../StateDesc.h"
#include <memory>

namespace RenderCore { class IDevice; class IThreadContext; class FrameBufferDesc; class SharedPkt; class IResourceView; class ISampler; class UniformsStreamInterface; }
namespace Assets { class IAsyncMarker; }
namespace RenderCore { namespace Assets { class ShaderPatchCollection; }}

namespace RenderCore { namespace Techniques
{
	class ParsingContext;
	class DrawableGeo;
	class DrawablesPacket;
	class RenderPassInstance;
	struct PreparedResourcesVisibility;
	class ITechniqueDelegate;
	class IPipelineLayoutDelegate;
	class IPipelineAcceleratorPool;
	class PipelineAccelerator;
	class DescriptorSetAccelerator;

	class RetainedUniformsStream
	{
	public:
		std::vector<std::shared_ptr<IResourceView>> _resourceViews;
		std::vector<SharedPkt> _immediateData;
		std::vector<std::shared_ptr<ISampler>> _samplers;
		uint64_t _hashForCombining = 0ull;

		RetainedUniformsStream();
		RetainedUniformsStream(const RetainedUniformsStream&);
		RetainedUniformsStream(RetainedUniformsStream&&);
		RetainedUniformsStream& operator=(const RetainedUniformsStream&);
		RetainedUniformsStream& operator=(RetainedUniformsStream&&);
		~RetainedUniformsStream();
	};

	class ImmediateDrawableMaterial
	{
	public:
		const UniformsStreamInterface* _uniformStreamInterface = nullptr;
		const ParameterBox* _shaderSelectors = nullptr;
		RenderCore::Assets::RenderStateSet _stateSet;
		std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> _patchCollection;

		// When _combinable is true, _hash must be filled in by caller
		// it is used to compare materials to know when sequential draw calls can be combined
		uint64_t _hash = 0ull;	
		bool _combinable = true;
	};

	class EncoderState
	{
	public:
		struct States
		{
			enum Flags { Scissor = 1u<<0u, Viewport = 1u<<1u, DepthBounds = 1u<<2u, StencilRef = 1u<<3u, NoScissor = 1u<<4u };
			using BitField = unsigned;
		};

		States::BitField _states = 0;
		Rect2D _scissor;
		ViewportDesc _viewport;
		std::pair<float, float> _depthBounds;
		std::pair<unsigned, unsigned> _stencilRef;

		EncoderState& SetScissor(const Rect2D&);
		EncoderState& ClearScissor();
		EncoderState& SetViewport(const ViewportDesc&);
		EncoderState& SetDepthBounds(float minDepthValue, float maxDepthValue);
		EncoderState& SetStencilRef(unsigned frontFaceStencilRef, unsigned backFaceStencilRef);
		void MergeIn(const EncoderState&);
	};

	class IImmediateDrawables
	{
	public:
		virtual IteratorRange<void*> QueueDraw(
			size_t vertexCount,
			IteratorRange<const MiniInputElementDesc*> inputAssembly,
			const ImmediateDrawableMaterial& material = {},
			RetainedUniformsStream&& uniforms = {},
			Topology topology = Topology::TriangleList) = 0;

		virtual void QueueDraw(
			size_t indexOrVertexCount, size_t indexOrVertexStartLocation,
			std::shared_ptr<DrawableGeo> customGeo,
			IteratorRange<const MiniInputElementDesc*> inputAssembly,
			const ImmediateDrawableMaterial& material = {},
			RetainedUniformsStream&& uniforms = {},
			Topology topology = Topology::TriangleList) = 0;

		virtual void QueueDraw(
			size_t indexOrVertexCount, size_t indexOrVertexStartLocation,
			std::shared_ptr<DrawableGeo> customGeo,
			IteratorRange<const InputElementDesc*> inputAssembly,
			const ImmediateDrawableMaterial& material = {},
			RetainedUniformsStream&& uniforms = {},
			Topology topology = Topology::TriangleList) = 0;

		virtual IteratorRange<void*> QueueDraw(
			size_t vertexCount, size_t vertexStride,
			PipelineAccelerator& pipelineAccelerator,
			DescriptorSetAccelerator& prebuiltDescriptorSet,
			const UniformsStreamInterface* uniformStreamInterface = nullptr,
			RetainedUniformsStream&& uniforms = {},
			Topology topology = Topology::TriangleList) = 0;

		virtual void QueueEncoderState(const EncoderState&) = 0;
		virtual IteratorRange<void*> UpdateLastDrawCallVertexCount(size_t newVertexCount) = 0;

		virtual void ExecuteDraws(
			ParsingContext& parserContext,
			const std::shared_ptr<ITechniqueDelegate>& techniqueDelegate,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex) = 0;
		virtual void AbandonDraws() = 0;
		void ExecuteDraws(ParsingContext&, const std::shared_ptr<ITechniqueDelegate>&, const RenderPassInstance&);
		virtual void PrepareResources(
			std::promise<PreparedResourcesVisibility>&& promise,
			const std::shared_ptr<ITechniqueDelegate>& techniqueDelegate,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex) = 0;

		virtual DrawablesPacket* GetDrawablesPacket() = 0;
		virtual std::shared_ptr<IPipelineAcceleratorPool> GetPipelineAcceleratorPool() = 0;
		virtual void OnFrameBarrier() = 0;

		virtual ~IImmediateDrawables();
	};

	std::shared_ptr<IImmediateDrawables> CreateImmediateDrawables(std::shared_ptr<IPipelineAcceleratorPool>);

////////////////////////////////////////////////////////////////////////////////////////////////////////////

	inline EncoderState& EncoderState::SetScissor(const Rect2D& scissor) { _states |= States::Scissor; _states &= ~States::NoScissor; _scissor = scissor; return *this; }
	inline EncoderState& EncoderState::ClearScissor() { _states |= States::NoScissor; _states &= ~States::Scissor; return *this; }
	inline EncoderState& EncoderState::SetViewport(const ViewportDesc& viewport) { _states |= States::Viewport; _viewport = viewport; return *this; }
	inline EncoderState& EncoderState::SetDepthBounds(float minDepthValue, float maxDepthValue) { _states |= States::DepthBounds; _depthBounds = {minDepthValue, maxDepthValue}; return *this; }
	inline EncoderState& EncoderState::SetStencilRef(unsigned frontFaceStencilRef, unsigned backFaceStencilRef) { _states |= States::StencilRef; _stencilRef = {frontFaceStencilRef, backFaceStencilRef}; return *this; }
	inline void EncoderState::MergeIn(const EncoderState& other)
	{
		if (other._states & States::Scissor) SetScissor(other._scissor);
		else if (other._states & States::NoScissor) ClearScissor();
		if (other._states & States::Viewport) SetViewport(other._viewport);
		if (other._states & States::DepthBounds) SetDepthBounds(other._depthBounds.first, other._depthBounds.second);
		if (other._states & States::StencilRef) SetStencilRef(other._stencilRef.first, other._stencilRef.second);
	}

}}

