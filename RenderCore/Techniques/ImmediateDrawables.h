// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "PipelineAccelerator.h"
#include "../Assets/MaterialScaffold.h"
#include "../Types.h"
#include "../StateDesc.h"
#include "../RenderUtils.h"		// for SharedPkt
#include "../../Math/Vector.h"
#include <memory>

namespace RenderCore { class IThreadContext; class FrameBufferDesc; class SharedPkt; class IResourceView; class ISampler; class UniformsStreamInterface; }
namespace Assets { class IAsyncMarker; }

namespace RenderCore { namespace Techniques
{
	class ParsingContext;
	class DrawableGeo;
	class DrawablesPacket;
	class RenderPassInstance;

	class RetainedUniformsStream
	{
	public:
		std::vector<std::shared_ptr<IResourceView>> _resourceViews;
		std::vector<SharedPkt> _immediateData;
		std::vector<std::shared_ptr<ISampler>> _samplers;
	};

	class ImmediateDrawableMaterial
	{
	public:
		std::shared_ptr<UniformsStreamInterface> _uniformStreamInterface;
		RetainedUniformsStream _uniforms;
		ParameterBox _shaderSelectors;
		RenderCore::Assets::RenderStateSet _stateSet;
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
			IThreadContext& context,
			ParsingContext& parserContext,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex) = 0;
		virtual void AbandonDraws() = 0;
		void ExecuteDraws(IThreadContext&, ParsingContext&, const RenderPassInstance&);
		virtual std::shared_ptr<::Assets::IAsyncMarker> PrepareResources(
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex) = 0;
		virtual DrawablesPacket* GetDrawablesPacket() = 0;
		virtual ~IImmediateDrawables();
	};

	namespace Internal { const DescriptorSetLayoutAndBinding& GetDefaultDescriptorSetLayoutAndBinding(); }

	std::shared_ptr<IImmediateDrawables> CreateImmediateDrawables(
		const std::shared_ptr<IDevice>&,
		const DescriptorSetLayoutAndBinding& matDescSetLayout = Internal::GetDefaultDescriptorSetLayoutAndBinding());
}}

