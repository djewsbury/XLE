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
namespace Assets { class DependencyValidation; }

namespace RenderCore { namespace Techniques
{
	class DeformAccelerator;
	class IDeformAcceleratorAttachment;
	// struct DeformerToRendererBinding;
	// struct DeformerToDescriptorSetBinding;

			// todo -- move this to somewhere else
	struct DeformerToDescriptorSetBinding
	{
		struct CBBinding
		{
			uint64_t _hashName;
			std::shared_ptr<IResourceView> _pageResource;
		};
		std::vector<CBBinding> _dynamicCBs;
	};

	class IDeformAcceleratorPool
	{
	public:
		virtual std::shared_ptr<DeformAccelerator> CreateDeformAccelerator() = 0;
		virtual void Attach(
			DeformAccelerator& deformAccelerator,
			std::shared_ptr<IDeformAcceleratorAttachment> deformAttachment) = 0;

		/*
		virtual void AttachGeometryDeformers(
			DeformAccelerator& accelerator,
			IteratorRange<const DeformOperationFactorySet::Deformer*> deformers,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
			const std::string& modelScaffoldName);

		virtual const DeformerToRendererBinding& GetDeformerToRendererBinding(DeformAccelerator& accelerator) const = 0;
		virtual const DeformerToDescriptorSetBinding& GetDeformerToDescriptorSetBinding(DeformAccelerator& accelerator) const = 0;
		*/

		virtual void EnableInstance(DeformAccelerator& accelerator, unsigned instanceIdx) = 0;
		virtual void ReadyInstances(IThreadContext&) = 0;
		virtual void SetVertexInputBarrier(IThreadContext&) const = 0;
		virtual void OnFrameBarrier() = 0;

		unsigned GetGUID() const { return _guid; }
		virtual const std::shared_ptr<IDevice>& GetDevice() const = 0;

		struct ReadyInstancesMetrics;
		virtual ReadyInstancesMetrics GetMetrics() const = 0;

		IDeformAcceleratorPool();
		virtual ~IDeformAcceleratorPool();
	private:
		uint64_t _guid;
	};

	class IDeformAcceleratorAttachment
	{
	public:
		virtual void ReserveBytesRequired(unsigned instanceCount, unsigned& gpuBufferBytes, unsigned& cpuBufferBytes, unsigned& cbBytes) = 0;
		virtual void Execute(
			IThreadContext& threadContext, 
			IteratorRange<const unsigned*> instanceIdx, 
			IResourceView& dstVB,
			IteratorRange<void*> cpuBufferOutputRange,
			IteratorRange<void*> cbBufferOutputRange,
			IDeformAcceleratorPool::ReadyInstancesMetrics& metrics) = 0;
		virtual ~IDeformAcceleratorAttachment() = default;
	};

	std::shared_ptr<IDeformAcceleratorPool> CreateDeformAcceleratorPool(std::shared_ptr<IDevice> device);

	struct IDeformAcceleratorPool::ReadyInstancesMetrics
	{
		unsigned _acceleratorsReadied = 0;
		unsigned _deformersReadied = 0;
		unsigned _instancesReadied = 0;
		unsigned _cpuDeformAllocation = 0;
		unsigned _gpuDeformAllocation = 0;
		unsigned _cbAllocation = 0;
		unsigned _dispatchCount = 0;
		unsigned _vertexCount = 0;
		unsigned _descriptorSetWrites = 0;
		unsigned _constantDataSize = 0;
		unsigned _inputStaticDataSize = 0;
	};

	namespace Internal
	{
		VertexBufferView GetOutputVBV(DeformAccelerator& accelerator, unsigned instanceIdx);
		unsigned GetDynamicCBOffset(DeformAccelerator& accelerator, unsigned instanceIdx);
	}

}}
