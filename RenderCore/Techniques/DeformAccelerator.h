// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../VertexUtil.h"
#include "../Types.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>

namespace RenderCore { class IDevice; class IThreadContext; class VertexBufferView; class IResourceView; }
namespace RenderCore { namespace Assets { class ModelScaffold; }}
namespace Assets { class DependencyValidation; }

namespace RenderCore { namespace Techniques
{
	class DeformAccelerator;
	class IDeformer;
	struct DeformerToRendererBinding;

	class IDeformAcceleratorPool
	{
	public:
		virtual std::shared_ptr<DeformAccelerator> CreateDeformAccelerator(
			StringSection<> initializer,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
			const std::string& modelScaffoldName = {}) = 0;

		virtual const DeformerToRendererBinding& GetDeformerToRendererBinding(DeformAccelerator& accelerator) const = 0;

		virtual std::vector<std::shared_ptr<IDeformer>> GetOperations(DeformAccelerator& accelerator, size_t typeId) = 0;
		virtual void EnableInstance(DeformAccelerator& accelerator, unsigned instanceIdx) = 0;
		virtual void ReadyInstances(IThreadContext&) = 0;
		virtual void SetVertexInputBarrier(IThreadContext&) const = 0;
		virtual void OnFrameBarrier() = 0;

		unsigned GetGUID() const { return _guid; }

		struct ReadyInstancesMetrics;
		virtual ReadyInstancesMetrics GetMetrics() const = 0;

		IDeformAcceleratorPool();
		virtual ~IDeformAcceleratorPool();
	private:
		uint64_t _guid;
	};

	std::shared_ptr<IDeformAcceleratorPool> CreateDeformAcceleratorPool(std::shared_ptr<IDevice> device);

	class IDeformer
	{
	public:
		struct Metrics
		{
			unsigned _dispatchCount = 0;
			unsigned _vertexCount = 0;
			unsigned _descriptorSetWrites = 0;
			unsigned _constantDataSize = 0;
			unsigned _inputStaticDataSize = 0;
		};

		virtual void ExecuteGPU(
			IThreadContext& threadContext,
			IteratorRange<const unsigned*> instanceIndices,
			unsigned outputInstanceStride,
			const IResourceView& srcVB,
			const IResourceView& deformTemporariesVB,
			const IResourceView& dstVB,
			Metrics& metrics) const;

		using VertexElementRange = IteratorRange<RenderCore::VertexElementIterator>;
		virtual void ExecuteCPU(
			IteratorRange<const unsigned*> instanceIndices,
			unsigned outputInstanceStride,
			IteratorRange<const void*> srcVB,
			IteratorRange<const void*> deformTemporariesVB,
			IteratorRange<const void*> dstVB) const;

		virtual void* QueryInterface(size_t) = 0;
		virtual ~IDeformer();
	};

	struct DeformerToRendererBinding
	{
		struct GeoBinding
		{
			unsigned _geoId = ~0u;
			std::vector<InputElementDesc> _generatedElements;
			std::vector<uint64_t> _suppressedElements;
			unsigned _postDeformBufferOffset = 0;
		};
		std::vector<GeoBinding> _geoBindings;
	};

	struct IDeformAcceleratorPool::ReadyInstancesMetrics
	{
		unsigned _acceleratorsReadied = 0;
		unsigned _deformersReadied = 0;
		unsigned _instancesReadied = 0;
		unsigned _cpuDeformAllocation = 0;
		unsigned _gpuDeformAllocation = 0;
		unsigned _dispatchCount = 0;
		unsigned _vertexCount = 0;
		unsigned _descriptorSetWrites = 0;
		unsigned _constantDataSize = 0;
		unsigned _inputStaticDataSize = 0;
	};

	namespace Internal
	{
		VertexBufferView GetOutputVBV(DeformAccelerator& accelerator, unsigned instanceIdx);
	}

}}
