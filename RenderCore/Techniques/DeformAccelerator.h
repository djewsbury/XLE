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
#include <future>

namespace RenderCore { class IDevice; class IThreadContext; class VertexBufferView; class IResourceView; class IResource; }
namespace Assets { class DependencyValidation; }
namespace Utility { class ParameterBox; }
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}

namespace RenderCore { namespace Techniques
{
	class DeformAccelerator;
	class IDeformGeoAttachment;
	class IDeformUniformsAttachment;
	class ICompiledLayoutPool;
	class IDrawablesPool;

	class IDeformAcceleratorPool
	{
	public:
		virtual std::shared_ptr<DeformAccelerator> CreateDeformAccelerator() = 0;
		virtual void Attach(
			DeformAccelerator& deformAccelerator,
			std::shared_ptr<IDeformGeoAttachment> deformAttachment) = 0;

		virtual void Attach(
			DeformAccelerator& deformAccelerator,
			std::shared_ptr<IDeformUniformsAttachment> deformAttachment) = 0;

		virtual std::shared_ptr<IDeformGeoAttachment> GetDeformGeoAttachment(DeformAccelerator& deformAccelerator) = 0;
		virtual std::shared_ptr<IDeformUniformsAttachment> GetDeformUniformsAttachment(DeformAccelerator& deformAccelerator) = 0;

		virtual void ReadyInstances(IThreadContext&) = 0;
		virtual void SetVertexInputBarrier(IThreadContext&) const = 0;
		virtual void OnFrameBarrier() = 0;

		virtual std::shared_ptr<IResource> GetDynamicPageResource() const = 0;

		unsigned GetGUID() const { return _guid; }
		virtual const std::shared_ptr<IDevice>& GetDevice() const = 0;
		virtual const std::shared_ptr<ICompiledLayoutPool>& GetCompiledLayoutPool() const = 0;

		struct ReadyInstancesMetrics;
		virtual ReadyInstancesMetrics GetMetrics() const = 0;

		IDeformAcceleratorPool();
		virtual ~IDeformAcceleratorPool();
	private:
		uint64_t _guid;
	};

	std::shared_ptr<IDeformAcceleratorPool> CreateDeformAcceleratorPool(std::shared_ptr<IDevice>, std::shared_ptr<IDrawablesPool>, std::shared_ptr<ICompiledLayoutPool>);

	void EnableInstanceDeform(DeformAccelerator& accelerator, unsigned instanceIdx);

	class IGeoDeformer;
	struct DeformerToRendererBinding;
	class IDeformGeoAttachment
	{
	public:
		///@{ interface for clients for providing animation state data & initializing renderers
		virtual std::vector<std::shared_ptr<IGeoDeformer>> GetOperations(size_t typeId) = 0;
		virtual BufferUploads::CommandListID GetCompletionCommandList() const = 0;
		virtual const DeformerToRendererBinding& GetDeformerToRendererBinding() const = 0;
		virtual std::shared_future<void> GetInitializationFuture() const = 0;
		///@}

		///@{ interface used by IDeformAcceleratorPool to manage this attachment
		virtual void ReserveBytesRequired(unsigned instanceCount, unsigned& gpuBufferBytes, unsigned& cpuBufferBytes) = 0;
		virtual void Execute(
			IThreadContext& threadContext, 
			IteratorRange<const unsigned*> instanceIdx, 
			IResourceView& dstVB,
			IteratorRange<void*> cpuBufferOutputRange,
			IDeformAcceleratorPool::ReadyInstancesMetrics& metrics) = 0;
		///@}
		virtual ~IDeformGeoAttachment();
	};

	struct UniformDeformerToRendererBinding;
	struct AnimatedUniform;
	class IDeformUniformsAttachment
	{
	public:
		///@{ interface for clients for providing animation state data & initializing renderers
		virtual void SetInputValues(unsigned instanceIdx, IteratorRange<const void*> data) = 0;
		virtual IteratorRange<const AnimatedUniform*> GetInputValuesLayout() const = 0;
		virtual const UniformDeformerToRendererBinding& GetDeformerToRendererBinding() const = 0;
		///@}

		///@{ interface used by IDeformAcceleratorPool to manage this attachment
		virtual void ReserveBytesRequired(unsigned instanceCount, unsigned& gpuBufferBytes, unsigned& cpuBufferBytes) = 0;
		virtual void Execute(
			IteratorRange<const unsigned*> instanceIdx,
			IteratorRange<void*> dst) = 0;
		///@}

		virtual ~IDeformUniformsAttachment();
	};

	struct IDeformAcceleratorPool::ReadyInstancesMetrics
	{
		unsigned _acceleratorsReadied = 0;
		unsigned _deformersReadied = 0;
		unsigned _instancesReadied = 0;
		unsigned _cpuDeformAllocation = 0;
		unsigned _gpuDeformAllocation = 0;
		unsigned _uniformDeformAllocation = 0;
		unsigned _dispatchCount = 0;
		unsigned _vertexCount = 0;
		unsigned _descriptorSetWrites = 0;
		unsigned _constantDataSize = 0;
		unsigned _inputStaticDataSize = 0;
	};

	namespace Internal
	{
		VertexBufferView GetOutputVBV(DeformAccelerator& accelerator, unsigned instanceIdx);
		unsigned GetUniformPageBufferOffset(DeformAccelerator& accelerator, unsigned instanceIdx);
	}

}}
