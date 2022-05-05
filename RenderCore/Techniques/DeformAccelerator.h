// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../VertexUtil.h"
#include "../Types.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/ImpliedTyping.h"
#include <memory>

namespace RenderCore { class IDevice; class IThreadContext; class VertexBufferView; class IResourceView; }
namespace Assets { class DependencyValidation; }
namespace Utility { class ParameterBox; }

namespace RenderCore { namespace Techniques
{
	class DeformAccelerator;
	class IDeformAttachment;
	class IDeformParametersAttachment;

	class IDeformAcceleratorPool
	{
	public:
		virtual std::shared_ptr<DeformAccelerator> CreateDeformAccelerator() = 0;
		virtual void Attach(
			DeformAccelerator& deformAccelerator,
			std::shared_ptr<IDeformAttachment> deformAttachment) = 0;

		virtual void Attach(
			DeformAccelerator& deformAccelerator,
			std::shared_ptr<IDeformParametersAttachment> deformAttachment) = 0;

		virtual std::shared_ptr<IDeformAttachment> GetDeformAttachment(DeformAccelerator& deformAccelerator) = 0;
		virtual std::shared_ptr<IDeformParametersAttachment> GetDeformParametersAttachment(DeformAccelerator& deformAccelerator) = 0;

		virtual void EnableInstance(DeformAccelerator& accelerator, unsigned instanceIdx) = 0;
		virtual void ReadyInstances(IThreadContext&) = 0;
		virtual void SetVertexInputBarrier(IThreadContext&) const = 0;
		virtual void OnFrameBarrier() = 0;

		virtual std::shared_ptr<IResourceView> GetDynamicPageResource() const = 0;
		virtual unsigned GetDynamicPageResourceAlignment() const = 0;

		unsigned GetGUID() const { return _guid; }
		virtual const std::shared_ptr<IDevice>& GetDevice() const = 0;

		struct ReadyInstancesMetrics;
		virtual ReadyInstancesMetrics GetMetrics() const = 0;

		IDeformAcceleratorPool();
		virtual ~IDeformAcceleratorPool();
	private:
		uint64_t _guid;
	};

	class IDeformAttachment
	{
	public:
		virtual void ReserveBytesRequired(unsigned instanceCount, unsigned& gpuBufferBytes, unsigned& cpuBufferBytes) = 0;
		virtual void Execute(
			IThreadContext& threadContext, 
			IteratorRange<const unsigned*> instanceIdx, 
			IResourceView& dstVB,
			IteratorRange<void*> cpuBufferOutputRange,
			IDeformAcceleratorPool::ReadyInstancesMetrics& metrics) = 0;
		virtual ~IDeformAttachment() = default;
	};

	class IDeformParametersAttachment
	{
	public:
		struct Bindings
		{
			uint64_t _name;
			ImpliedTyping::TypeDesc _type;
			unsigned _offset;
		};
		virtual void SetInputParameters(unsigned instanceIdx, const Utility::ParameterBox& parameters) = 0;
		virtual IteratorRange<const Bindings*> GetOutputParameterBindings() const = 0;
		virtual unsigned GetOutputInstanceStride() const = 0;
		virtual void Execute(IteratorRange<const unsigned*> instanceIdx, IteratorRange<void*> dst, unsigned outputInstanceStride) = 0;
		virtual ~IDeformParametersAttachment() = default;
	};

	std::shared_ptr<IDeformAcceleratorPool> CreateDeformAcceleratorPool(std::shared_ptr<IDevice> device);

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
		IteratorRange<const void*> GetOutputParameterState(DeformAccelerator& accelerator, unsigned instanceIdx);
	}

}}
