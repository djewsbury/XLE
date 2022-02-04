// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformAccelerator.h"
// #include "DeformOperationFactory.h"
// #include "DeformInternal.h"
#include "Services.h"
#include "GPUTrackerHeap.h"
#include "CommonUtils.h"
#include "CommonResources.h"
// #include "../Assets/ModelScaffold.h"
// #include "../Assets/ModelScaffoldInternal.h"
// #include "../Assets/ModelImmutableData.h"
// #include "../GeoProc/MeshDatabase.h"        // for GeoProc::Copy
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Resource.h"
#include "../IDevice.h"
#include "../BufferView.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../Utility/BitUtils.h"
#include <vector>

#include "../Vulkan/Metal/CmdListAttachedStorage.h"

namespace RenderCore { namespace Techniques
{
	class DeformAcceleratorPool : public IDeformAcceleratorPool
	{
	public:
		std::shared_ptr<DeformAccelerator> CreateDeformAccelerator() override;
		void Attach(
			DeformAccelerator& deformAccelerator,
			std::shared_ptr<IDeformAcceleratorAttachment> deformAttachment) override;

		/*void AttachGeometryDeformers(
			DeformAccelerator& newAccelerator,
			IteratorRange<const DeformOperationFactorySet::Deformer*> deformers,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
			const std::string& modelScaffoldName) override;

		const DeformerToRendererBinding& GetDeformerToRendererBinding(DeformAccelerator& accelerator) const override;
		const DeformerToDescriptorSetBinding& GetDeformerToDescriptorSetBinding(DeformAccelerator& accelerator) const override;

		std::vector<std::shared_ptr<IGeoDeformer>> GetOperations(DeformAccelerator& accelerator, uint64_t typeId) override;*/

		void EnableInstance(DeformAccelerator& accelerator, unsigned instanceIdx) override;
		void ReadyInstances(IThreadContext&) override;
		void SetVertexInputBarrier(IThreadContext&) const override;
		void OnFrameBarrier() override;
		ReadyInstancesMetrics GetMetrics() const override;
		const std::shared_ptr<IDevice>& GetDevice() const override;

		DeformAcceleratorPool(std::shared_ptr<IDevice>);
		~DeformAcceleratorPool();

	private:
		std::vector<std::weak_ptr<DeformAccelerator>> _accelerators;
		std::shared_ptr<IDevice> _device;
		std::unique_ptr<RenderCore::Metal_Vulkan::TemporaryStorageManager> _temporaryStorageManager;
		std::shared_ptr<RenderCore::Metal_Vulkan::IAsyncTracker> _asyncTracker;
		std::vector<RenderCore::Metal_Vulkan::CmdListAttachedStorage> _currentFrameAttachedStorage;
		mutable bool _pendingVertexInputBarrier = false;
		RenderCore::Metal_Vulkan::NamedPage _cbNamedPage = ~0u;

		Threading::Mutex _acceleratorsLock;
		std::thread::id _boundThread;

		ReadyInstancesMetrics _readyInstancesMetrics;
		ReadyInstancesMetrics _lastFrameReadyInstancesMetrics;

		void ExecuteCBInstance(IThreadContext& threadContext, DeformAccelerator& a, IteratorRange<const unsigned*> instanceIdx, IteratorRange<void*> outputPartRange);
	};

	enum AllocationType { AllocationType_GPUVB, AllocationType_CPUVB, AllocationType_CB, AllocationType_Max };

	class DeformAccelerator
	{
	public:
		std::vector<uint64_t> _enabledInstances;
		std::vector<uint64_t> _readiedInstances;
		unsigned _minEnabledInstance = ~0u;
		unsigned _maxEnabledInstance = 0;

		unsigned _reversationPerInstance[AllocationType_Max] = {0,0,0};
		std::vector<unsigned> _instanceToReadiedOffset[AllocationType_Max];
		VertexBufferView _outputVBV;

		std::shared_ptr<IDeformAcceleratorAttachment> _attachment;
		// std::vector<std::shared_ptr<IGeoDeformer>> _cbWriteOps;

		#if defined(_DEBUG)
			DeformAcceleratorPool* _containingPool = nullptr;
		#endif

		void Execute(
			IThreadContext& threadContext, 
			IteratorRange<const unsigned*> instanceIdx, 
			IResourceView& dstVB,
			IteratorRange<void*> cpuBufferOutputRange,
			IteratorRange<void*> cbBufferOutputRange,
			IDeformAcceleratorPool::ReadyInstancesMetrics& metrics)
		{
			_attachment->Execute(threadContext, instanceIdx, dstVB, cpuBufferOutputRange, cbBufferOutputRange, metrics);
		}
	};

	std::shared_ptr<DeformAccelerator> DeformAcceleratorPool::CreateDeformAccelerator()
	{
		auto newAccelerator = std::make_shared<DeformAccelerator>();
		newAccelerator->_enabledInstances.resize(8, 0);
		newAccelerator->_readiedInstances.resize(8, 0);
		#if defined(_DEBUG)
			newAccelerator->_containingPool = this;
		#endif

		// newAccelerator->_cbWriteOps.push_back(std::make_shared<TestCBDeformer>());
		// newAccelerator->_reversationPerInstance[AllocationType_CB] = sizeof(BasicMaterialConstants);
		// we must expand newAccelerator->_cbSizePerInstance for the alignment requirements for dynamic buffer offsets of the given
		// device
		newAccelerator->_reversationPerInstance[AllocationType_CB] = CeilToMultiple(newAccelerator->_reversationPerInstance[AllocationType_CB], 256);

		ScopedLock(_acceleratorsLock);
		_accelerators.push_back(newAccelerator);
		return newAccelerator;
	}

	void DeformAcceleratorPool::Attach(
		DeformAccelerator& accelerator,
		std::shared_ptr<IDeformAcceleratorAttachment> deformAttachment)
	{
		#if defined(_DEBUG)
			assert(accelerator._containingPool == this);
		#endif
		assert(!accelerator._attachment);		// we can't attach geometry deformers more than once to a given deform accelerator
		accelerator._attachment = std::move(deformAttachment);

		unsigned reservationGPU = 0, reservationCPU = 0, reservationCB = 0;
		accelerator._attachment->ReserveBytesRequired(1, reservationGPU, reservationCPU, reservationCB);
		accelerator._reversationPerInstance[AllocationType_GPUVB] += reservationGPU;
		accelerator._reversationPerInstance[AllocationType_CPUVB] += reservationCPU;
	}

/*
	const DeformerToRendererBinding& DeformAcceleratorPool::GetDeformerToRendererBinding(DeformAccelerator& accelerator) const
	{
		#if defined(_DEBUG)
			assert(accelerator._containingPool == this);
		#endif
		return accelerator._geoInfrastructure._rendererGeoInterface;
	}

	const DeformerToDescriptorSetBinding& DeformAcceleratorPool::GetDeformerToDescriptorSetBinding(DeformAccelerator& accelerator) const
	{
		static DeformerToDescriptorSetBinding result;
		static bool init = false;
		if (!init) {
			init = true;
			DeformerToDescriptorSetBinding::CBBinding binding;
			binding._hashName = Hash64("BasicMaterialConstants");
			binding._pageResource = _temporaryStorageManager->GetResourceForNamedPage(_cbNamedPage)->CreateBufferView(BindFlag::ConstantBuffer);
			result._dynamicCBs.push_back(std::move(binding));
		}
		return result;
	}

	std::vector<std::shared_ptr<IGeoDeformer>> DeformAcceleratorPool::GetOperations(DeformAccelerator& accelerator, size_t typeId)
	{
		#if defined(_DEBUG)
			assert(accelerator._containingPool == this);
		#endif
		std::vector<std::shared_ptr<IGeoDeformer>> result;
		accelerator.GetOperations(result, typeId);
		return result;
	}
*/

	void DeformAcceleratorPool::EnableInstance(DeformAccelerator& accelerator, unsigned instanceIdx)
	{
		assert(instanceIdx!=~0u);
		#if defined(_DEBUG)
			assert(accelerator._containingPool == this);
		#endif
		auto field = instanceIdx / 64;
		if (accelerator._enabledInstances.size() <= field)
			accelerator._enabledInstances.resize(field+1, 0);
		accelerator._enabledInstances[field] |= 1ull << (instanceIdx & (64-1));
		accelerator._minEnabledInstance = std::min(accelerator._minEnabledInstance, instanceIdx);
		accelerator._maxEnabledInstance = std::max(accelerator._maxEnabledInstance, instanceIdx);
	}

	void DeformAcceleratorPool::ExecuteCBInstance(IThreadContext& threadContext, DeformAccelerator& a, IteratorRange<const unsigned*> instanceIdx, IteratorRange<void*> outputPartRange)
	{
		//for (const auto&d:a._cbWriteOps)
			//d->ExecuteCB(instanceIdx, a._reversationPerInstance[AllocationType_CB], outputPartRange);
	}

	void DeformAcceleratorPool::ReadyInstances(IThreadContext& threadContext)
	{
		assert(_boundThread == std::this_thread::get_id());
		auto attachedStorage = _temporaryStorageManager->BeginCmdListReservation();

		std::shared_ptr<DeformAccelerator> accelerators[_accelerators.size()];
		unsigned activeAcceleratorCount=0;
		unsigned reservationBytes[AllocationType_Max] = {0,0,0};
		unsigned maxInstanceCount = 0;
		{
			ScopedLock(_acceleratorsLock);
			for (auto a=_accelerators.begin(); a!=_accelerators.end();) {
				auto accelerator = a->lock();
				if (!accelerator) {
					a = _accelerators.erase(a);
					continue;
				}

				if (accelerator->_maxEnabledInstance < accelerator->_minEnabledInstance) { a++; continue; }
				auto fMin = accelerator->_minEnabledInstance / 64, fMax = accelerator->_maxEnabledInstance / 64;
				auto instanceCount = 0u;
				for (auto f=fMin; f<=fMax; ++f)
					instanceCount += popcount(accelerator->_enabledInstances[f] & ~accelerator->_readiedInstances[f]);
				
				if (instanceCount) {
					for (unsigned c=0; c<AllocationType_Max; ++c)
						reservationBytes[c] += accelerator->_reversationPerInstance[c] * instanceCount;
					accelerators[activeAcceleratorCount++] = std::move(accelerator);
					maxInstanceCount = std::max(maxInstanceCount, instanceCount);
				} else {
					for (auto f=fMin; f<=fMax; ++f)
						accelerator->_enabledInstances[f] = 0;
					accelerator->_minEnabledInstance = ~0u;
					accelerator->_maxEnabledInstance = 0u;
				}
				++a;
			}
		}

		if (!activeAcceleratorCount)
			return;

		const auto defaultPageSize = 8*1024*1024;
		bool atLeastOneGPUOperator = false;

		if (reservationBytes[AllocationType::AllocationType_GPUVB] || reservationBytes[AllocationType::AllocationType_CPUVB] || reservationBytes[AllocationType::AllocationType_CB]) {
			#if defined(_DEBUG)
				auto& metalContext = *Metal::DeviceContext::Get(threadContext);
				metalContext.BeginLabel("Deformers");
			#endif

			RenderCore::Metal_Vulkan::TemporaryStorageResourceMap cpuMap;
			RenderCore::Metal_Vulkan::TemporaryStorageResourceMap cbMap;
			RenderCore::Metal_Vulkan::BufferAndRange gpuBufferAndRange;
			RenderCore::VertexBufferView cpuVBV, gpuVBV;
			IteratorRange<void*> cpuDst, cbDst;

			if (reservationBytes[AllocationType::AllocationType_CPUVB]) {
				cpuMap = attachedStorage.MapStorage(reservationBytes[AllocationType::AllocationType_CPUVB], BindFlag::VertexBuffer, defaultPageSize);
				cpuVBV = cpuMap.AsVertexBufferView();
				cpuDst = cpuMap.GetData();
				assert(cpuVBV._resource);
			}
			if (reservationBytes[AllocationType::AllocationType_GPUVB]) {
				gpuBufferAndRange = attachedStorage.AllocateRange(reservationBytes[AllocationType::AllocationType_GPUVB], BindFlag::VertexBuffer|BindFlag::UnorderedAccess, defaultPageSize);
				gpuVBV = gpuBufferAndRange.AsVertexBufferView();
				assert(gpuVBV._resource);
			}
			if (reservationBytes[AllocationType::AllocationType_CB]) {
				assert(_cbNamedPage != ~0u);
				cbMap = attachedStorage.MapStorageFromNamedPage(reservationBytes[AllocationType::AllocationType_CB], _cbNamedPage);
				cbDst = cbMap.GetData();
			}

			unsigned movingOffsets[AllocationType_Max] = {0, 0, 0};
			unsigned instanceList[maxInstanceCount];

			for (unsigned c=0; c<activeAcceleratorCount; ++c) {
				auto accelerator = accelerators[c];
				if (!accelerator) continue;

				if (accelerator->_readiedInstances.size() < accelerator->_enabledInstances.size())
					accelerator->_readiedInstances.resize(accelerator->_enabledInstances.size(), 0);

				unsigned instanceCount=0, maxInstanceIdx=0;
				auto fMin = accelerator->_minEnabledInstance / 64, fMax = accelerator->_maxEnabledInstance / 64;
				for (auto f=fMin; f<=fMax; ++f) {
					auto active = accelerator->_enabledInstances[f] & ~accelerator->_readiedInstances[f];
					while (active) {
						auto bit = xl_ctz8(active);
						active ^= 1ull<<uint64_t(bit);
						instanceList[instanceCount++] = f*64+bit;
						maxInstanceIdx = std::max(maxInstanceIdx, f*64+bit);
					}
					accelerator->_readiedInstances[f] |= accelerator->_enabledInstances[f];
					accelerator->_enabledInstances[f] = 0;
				}

				accelerator->_minEnabledInstance = ~0u;
				accelerator->_maxEnabledInstance = 0u;

				auto cpuOutputRange = MakeIteratorRange(PtrAdd(cpuDst.begin(), movingOffsets[AllocationType_CPUVB]), PtrAdd(cpuDst.begin(), movingOffsets[AllocationType_CPUVB]+instanceCount*accelerator->_reversationPerInstance[AllocationType_CPUVB]));
				std::shared_ptr<IResourceView> gpuBufferView;
				if (accelerator->_reversationPerInstance[AllocationType_GPUVB] != 0) {
					gpuBufferView = gpuBufferAndRange._resource->CreateBufferView(BindFlag::UnorderedAccess, gpuVBV._offset+movingOffsets[AllocationType_GPUVB], instanceCount*accelerator->_reversationPerInstance[AllocationType_GPUVB]);
					accelerator->_outputVBV = gpuVBV;
					atLeastOneGPUOperator = true;
				}
				auto cbOutputRange = MakeIteratorRange(PtrAdd(cbDst.begin(), movingOffsets[AllocationType_CB]), PtrAdd(cbDst.begin(), movingOffsets[AllocationType_CB]+instanceCount*accelerator->_reversationPerInstance[AllocationType_CB]));

				accelerator->Execute(
					threadContext, 
					MakeIteratorRange(instanceList, &instanceList[instanceCount]),
					*gpuBufferView, cpuOutputRange, cbOutputRange, 
					_readyInstancesMetrics);

				for (unsigned allType=0; allType<AllocationType_Max; ++allType) {
					if (accelerator->_instanceToReadiedOffset[allType].size() <= maxInstanceIdx+1)
						accelerator->_instanceToReadiedOffset[allType].resize(maxInstanceIdx+1, ~0u);

					for (auto i:MakeIteratorRange(instanceList, &instanceList[instanceCount])) {
						accelerator->_instanceToReadiedOffset[allType][i] = movingOffsets[allType];
						movingOffsets[allType] += accelerator->_reversationPerInstance[allType];
					}
				}

				++_readyInstancesMetrics._acceleratorsReadied;
				_readyInstancesMetrics._instancesReadied += instanceCount;
			}

			for (unsigned allType=0; allType<AllocationType_Max; ++allType) {
				assert(movingOffsets[allType] == reservationBytes[allType]);
			}

			#if defined(_DEBUG)
				metalContext.EndLabel();
			#endif
		}

		_pendingVertexInputBarrier |= atLeastOneGPUOperator;
		_readyInstancesMetrics._cpuDeformAllocation += reservationBytes[AllocationType_CPUVB];
		_readyInstancesMetrics._gpuDeformAllocation += reservationBytes[AllocationType_GPUVB];
		_readyInstancesMetrics._cbAllocation += reservationBytes[AllocationType_CB];

		// todo - we should add a pipeline barrier for any output buffers that were written by the GPU, before they ared used
		// by the GPU (ie, written by a compute shader to be read by a vertex shader, etc)
		_currentFrameAttachedStorage.emplace_back(std::move(attachedStorage));
	}

	void DeformAcceleratorPool::SetVertexInputBarrier(IThreadContext& threadContext) const
	{
		if (_pendingVertexInputBarrier) {
			// we're expeting the output to be used as a vertex attribute; so we require a barrier here
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.pNext = nullptr;
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
			vkCmdPipelineBarrier(
				metalContext.GetActiveCommandList().GetUnderlying().get(),
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
				0,
				1, &barrier,
				0, nullptr,
				0, nullptr);
			_pendingVertexInputBarrier = false;
		}
	}

	namespace Internal
	{
		VertexBufferView GetOutputVBV(DeformAccelerator& accelerator, unsigned instanceIdx)
		{
			#if defined(_DEBUG)
				auto f = instanceIdx / 64;
				// If you hit either of the following, it means the instance wasn't enabled. Each instance that will be used should
				// be enabled via EnableInstance() before usage (probably at the time it's initialized with current state data)
				assert(f < accelerator._readiedInstances.size());
				assert(accelerator._readiedInstances[f] & (1ull << uint64_t(instanceIdx & (64-1))));	
				assert(instanceIdx < accelerator._instanceToReadiedOffset[AllocationType_GPUVB].size());
			#endif
			assert(accelerator._outputVBV._resource);
			VertexBufferView result = accelerator._outputVBV;
			result._offset += accelerator._instanceToReadiedOffset[AllocationType_GPUVB][instanceIdx];
			return result;
		}

		unsigned GetDynamicCBOffset(DeformAccelerator& accelerator, unsigned instanceIdx)
		{
			// we must return zero if we're not actually using a dynamic CB
			if (!accelerator._reversationPerInstance[AllocationType_CB]) return 0;
			#if defined(_DEBUG)
				auto f = instanceIdx / 64;
				// If you hit either of the following, it means the instance wasn't enabled. Each instance that will be used should
				// be enabled via EnableInstance() before usage (probably at the time it's initialized with current state data)
				assert(f < accelerator._readiedInstances.size());
				assert(accelerator._readiedInstances[f] & (1ull << uint64_t(instanceIdx & (64-1))));	
				assert(instanceIdx < accelerator._instanceToReadiedOffset[AllocationType_CB].size());
			#endif
			return accelerator._instanceToReadiedOffset[AllocationType_CB][instanceIdx];
		}
	}

	inline void DeformAcceleratorPool::OnFrameBarrier()
	{
		assert(_boundThread == std::this_thread::get_id());
		ScopedLock(_acceleratorsLock);
		auto i = _accelerators.begin();
		for (; i!=_accelerators.end();) {
			auto accelerator = i->lock();
			if (accelerator) {
				std::fill(accelerator->_readiedInstances.begin(), accelerator->_readiedInstances.end(), 0);
				accelerator->_outputVBV = {};
				++i;
			} else {
				i = _accelerators.erase(i);
			}
		}

		// data written by any previous ReadyInstances() is invalidated after this
		auto producerMarker = _asyncTracker->GetProducerMarker();
		for (auto& storage:_currentFrameAttachedStorage)
			storage.OnSubmitToQueue(producerMarker);
		_currentFrameAttachedStorage.clear();

		_temporaryStorageManager->FlushDestroys();

		_lastFrameReadyInstancesMetrics = _readyInstancesMetrics;
		_readyInstancesMetrics = {};
	}

	auto DeformAcceleratorPool::GetMetrics() const -> ReadyInstancesMetrics
	{
		return _lastFrameReadyInstancesMetrics;
	}

	const std::shared_ptr<IDevice>& DeformAcceleratorPool::GetDevice() const
	{
		return _device;
	}

	DeformAcceleratorPool::DeformAcceleratorPool(std::shared_ptr<IDevice> device)
	: _device(std::move(device))
	{
		auto* deviceVulkan = (RenderCore::IDeviceVulkan*)_device->QueryInterface(typeid(RenderCore::IDeviceVulkan).hash_code());
		if (deviceVulkan) {
			_asyncTracker = deviceVulkan->GetAsyncTracker();
			_temporaryStorageManager = std::make_unique<Metal_Vulkan::TemporaryStorageManager>(Metal::GetObjectFactory(), _asyncTracker);
			const unsigned cbAllocationSize = 1024*1024;
			_cbNamedPage = _temporaryStorageManager->CreateNamedPage(cbAllocationSize, BindFlag::ConstantBuffer);
		}
		_boundThread = std::this_thread::get_id();
	}

	DeformAcceleratorPool::~DeformAcceleratorPool() 
	{
		_currentFrameAttachedStorage.clear();
	}

	static uint64_t s_nextDeformAcceleratorPool = 1;
	IDeformAcceleratorPool::IDeformAcceleratorPool() : _guid(s_nextDeformAcceleratorPool++) {}
	IDeformAcceleratorPool::~IDeformAcceleratorPool() {}

	std::shared_ptr<IDeformAcceleratorPool> CreateDeformAcceleratorPool(std::shared_ptr<IDevice> device)
	{
		return std::make_shared<DeformAcceleratorPool>(std::move(device));
	}
}}
