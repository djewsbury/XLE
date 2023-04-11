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

	class RetainedUniformsStream
	{
	public:
		std::vector<std::shared_ptr<IResourceView>> _resourceViews;
		std::vector<SharedPkt> _immediateData;
		std::vector<std::shared_ptr<ISampler>> _samplers;

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
		RetainedUniformsStream _uniforms;
		const ParameterBox* _shaderSelectors = nullptr;
		RenderCore::Assets::RenderStateSet _stateSet;
		std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> _patchCollection;

		// When _combinable is true, _hash must be filled in by caller
		// it is used to compare materials to know when sequential draw calls can be combined
		uint64_t _hash = 0ull;	
		bool _combinable = true;
	};

	class IImmediateDrawables
	{
	public:
		virtual IteratorRange<void*> QueueDraw(
			size_t vertexCount,
			IteratorRange<const MiniInputElementDesc*> inputAssembly,
			const ImmediateDrawableMaterial& material = {},
			Topology topology = Topology::TriangleList) = 0;
		virtual void QueueDraw(
			size_t indexOrVertexCount, size_t indexOrVertexStartLocation,
			std::shared_ptr<DrawableGeo> customGeo,
			IteratorRange<const MiniInputElementDesc*> inputAssembly,
			const ImmediateDrawableMaterial& material = {},
			Topology topology = Topology::TriangleList) = 0;
		virtual void QueueDraw(
			size_t indexOrVertexCount, size_t indexOrVertexStartLocation,
			std::shared_ptr<DrawableGeo> customGeo,
			IteratorRange<const InputElementDesc*> inputAssembly,
			const ImmediateDrawableMaterial& material = {},
			Topology topology = Topology::TriangleList) = 0;
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
		virtual void OnFrameBarrier() = 0;
		virtual ~IImmediateDrawables();
	};

	std::shared_ptr<IImmediateDrawables> CreateImmediateDrawables(std::shared_ptr<IDevice>, std::shared_ptr<IPipelineLayoutDelegate>);
}}

