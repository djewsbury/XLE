// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "PipelineAccelerator.h"
#include "../Assets/RawMaterial.h"
#include "../UniformsStream.h"
#include "../Types.h"
#include "../StateDesc.h"
#include "../RenderUtils.h"		// for SharedPkt
#include "../../Math/Vector.h"
#include <memory>

#include <future>

namespace RenderCore { class IThreadContext; class FrameBufferDesc; class SharedPkt; class IResourceView; class ISampler; class UniformsStreamInterface; }
namespace Assets { class IAsyncMarker; }
namespace RenderCore { namespace Assets { class ShaderPatchCollection; }}
// namespace std { template<typename T> class shared_future; }

namespace RenderCore { namespace Techniques
{
	class ParsingContext;
	class DrawableGeo;
	class DrawablesPacket;
	class RenderPassInstance;
	struct PreparedResourcesVisibility;

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
		const UniformsStreamInterface* _uniformStreamInterface = nullptr;
		RetainedUniformsStream _uniforms;
		const ParameterBox* _shaderSelectors = nullptr;
		RenderCore::Assets::RenderStateSet _stateSet;
		std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> _patchCollection;
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
			SequencerConfig& seqConfig) = 0;
		virtual void AbandonDraws() = 0;
		virtual void PrepareResources(
			std::promise<PreparedResourcesVisibility>&& promise,
			SequencerConfig& seqConfig) = 0;
		virtual DrawablesPacket* GetDrawablesPacket() = 0;
		virtual void OnFrameBarrier() = 0;
		virtual ~IImmediateDrawables();
	};

	std::shared_ptr<IImmediateDrawables> CreateImmediateDrawables(std::shared_ptr<IPipelineAcceleratorPool> pipelineAcceleratorPool);

	class SequencerConfigSet
	{
	public:
		SequencerConfig& GetSequencerConfig(const FrameBufferDesc& fbDesc, unsigned subpassIndex);
		SequencerConfig& GetSequencerConfig(const RenderPassInstance&);

		std::shared_ptr<IPipelineAcceleratorPool> GetPipelineAccelerators();

		SequencerConfigSet(std::shared_ptr<IDevice> device);
		~SequencerConfigSet();
	private:
		std::vector<std::pair<uint64_t, std::shared_ptr<SequencerConfig>>> _sequencerConfigs;
		std::shared_ptr<IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		std::shared_future<std::shared_ptr<ITechniqueDelegate>> _futureTechniqueDelegate;
	};
}}

