// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformAccelerator.h"
#include "Services.h"
#include "SubFrameUtil.h"
#include "CommonUtils.h"
#include "Drawables.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Resource.h"
#include "../Vulkan/Metal/CmdListAttachedStorage.h"		// todo -- this must become a GFX independant interface
#include "../IDevice.h"
#include "../BufferView.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/BitUtils.h"
#include <vector>
#include <deque>

namespace RenderCore { namespace Techniques
{
	class GPUSyncedSinglePageResource;

	class DeformAcceleratorPool : public IDeformAcceleratorPool
	{
	public:
		std::shared_ptr<DeformAccelerator> CreateDeformAccelerator() override;
		void Attach(
			DeformAccelerator& deformAccelerator,
			std::shared_ptr<IGeoDeformerConductor> deformAttachment) override;

		virtual void Attach(
			DeformAccelerator& deformAccelerator,
			std::shared_ptr<IUniformsDeformerConductor> deformAttachment) override;

		std::shared_ptr<IGeoDeformerConductor> GetGeoDeformerConductor(DeformAccelerator& deformAccelerator) override;
		std::shared_ptr<IUniformsDeformerConductor> GetUniformsDeformerConductor(DeformAccelerator& deformAccelerator) override;

		void AttachSemiPersistentUniforms(DeformAccelerator& deformAccelerator, unsigned size) override;
		void SetSemiPersistentUniforms(DeformAccelerator& deformAccelerator, InstanceToken instance, IteratorRange<const void*>) override;
		void ReleaseSemiPersistentBuffer(unsigned offset, unsigned size);

		void SetVertexInputBarrier(IThreadContext&) const override;
		void OnFrameBarrier() override;
		ReadyInstancesMetrics GetMetrics() const override;
		const std::shared_ptr<IDevice>& GetDevice() const override;

		std::shared_ptr<IResource> GetDynamicPageResource() const override;
		std::shared_ptr<IResource> GetSemiPersistentPageResource() const override;

		std::shared_ptr<DeformersPacket> CreatePacket() override;

		std::atomic<unsigned> _alivePacketCount = 0;
		std::atomic<unsigned> _aliveDeformerCount = 0;

		DeformAcceleratorPool(std::shared_ptr<IDevice>, std::shared_ptr<IDrawablesPool>);
		~DeformAcceleratorPool();

	private:
		std::vector<std::weak_ptr<DeformAccelerator>> _accelerators;
		std::shared_ptr<IDevice> _device;
		std::shared_ptr<IDrawablesPool> _drawablesPool;
		std::unique_ptr<RenderCore::Metal_Vulkan::TemporaryStorageManager> _temporaryStorageManager;
		std::shared_ptr<RenderCore::Metal_Vulkan::IAsyncTracker> _asyncTracker;
		std::vector<RenderCore::Metal_Vulkan::CmdListAttachedStorage> _currentFrameAttachedStorage;
		mutable bool _pendingVertexInputBarrier = false;

		RenderCore::Metal_Vulkan::NamedPage _cbNamedPage = ~0u;
		std::shared_ptr<IResource> _cbPageResource;

		Threading::Mutex _acceleratorsLock;
		std::thread::id _boundThread;

		ReadyInstancesMetrics _readyInstancesMetrics;
		ReadyInstancesMetrics _lastFrameReadyInstancesMetrics;

		friend class DeformersPacket;
		std::deque<DeformersPacket*> _reusablePackets;

		std::unique_ptr<GPUSyncedSinglePageResource> _semiPersistentResource;

		friend void Deform(IThreadContext&, IDeformAcceleratorPool&, DeformersPacket&);

		friend RenderCore::Metal_Vulkan::TemporaryStorageResourceMap AllocateFromDynamicPageResource(IDeformAcceleratorPool& accelerators, unsigned bytes);
	};

	enum AllocationType { AllocationType_GPUVB, AllocationType_CPUVB, AllocationType_UniformBuffer, AllocationType_Max };

	class DeformAccelerator
	{
	public:
		std::vector<uint64_t> _readiedInstances;

		unsigned _reservationPerInstance[AllocationType_Max] = {0,0,0};
		std::vector<unsigned> _instanceToReadiedOffset[AllocationType_Max];
		VertexBufferView _outputVBV;
		unsigned _semiPersistentUniformsSize = 0;

		// for internal usage by the DeformAcceleratorPool
		std::shared_ptr<IGeoDeformerConductor> _geoConductor;
		std::shared_ptr<IUniformsDeformerConductor> _uniformsConductor;

		DeformAcceleratorPool* _containingPool = nullptr;		// required to release "semi-persistent" buffer

		~DeformAccelerator()
		{
			// Don't destroy a deform accelerator while there is an active DeformersPacket; because this could 
			// cause a dangling pointer within the packet's deformables list
			assert(!_containingPool || !_containingPool->_alivePacketCount);

			if (_containingPool) --_containingPool->_aliveDeformerCount;

			// problem -- we need to release the allocation in the semi-persistent buffer
			if (_semiPersistentUniformsSize != 0) {
				assert(_containingPool);
				for (auto o:_instanceToReadiedOffset[AllocationType_UniformBuffer])
					_containingPool->ReleaseSemiPersistentBuffer(o, _semiPersistentUniformsSize);
			}
		}

		DeformAccelerator() = default;
		DeformAccelerator(DeformAccelerator&&) = delete;
		DeformAccelerator& operator=(DeformAccelerator&&) = delete;
	};
	
	struct GPUSyncedSinglePage
	{
		unsigned Allocate(unsigned size, unsigned alignment)
		{
			// todo -- we could consider only the particular cmd list associated with _asyncTracker (similar
			// to how it's done with the buffer uploads staging page allocator)

			unsigned crashPoint = _totalSize;
			if (!_allocationsInfront.empty())
				crashPoint = _allocationsInfront[0]._start;

			unsigned preBufferForAlignment = CeilToMultiple(_movingPoint, alignment) - _movingPoint;
			auto headRoom = crashPoint - _movingPoint;
			if (headRoom < (size+preBufferForAlignment)) {
				// If we run out of space, check to see how many allocations we can just write over
				auto consumerMarker = _asyncTracker->GetConsumerMarker();
				while (!_allocationsInfront.empty() && _allocationsInfront.begin()->_marker < consumerMarker)
					_allocationsInfront.erase(_allocationsInfront.begin());
				crashPoint = _allocationsInfront.empty() ? _totalSize : _allocationsInfront[0]._start;
				headRoom = crashPoint - _movingPoint;

				if (_allocationsInfront.empty()) {
					// try to erase from _allocationsBehind, also
					while (!_allocationsBehind.empty() && _allocationsBehind.begin()->_marker < consumerMarker)
						_allocationsBehind.erase(_allocationsBehind.begin());
					crashPoint = _totalSize;
					headRoom = crashPoint - _movingPoint;

					// we can now choose to reset "_movingPoint" back to the start. But we should only
					// do this if we still don't have enough room for the allocation
					if (headRoom < (size+preBufferForAlignment)) {
						_allocationsInfront = std::move(_allocationsBehind);
						assert(_allocationsBehind.empty()); // Expecting all 'behind' allocations to be moved into 'infront'
						_movingPoint = 0;
						preBufferForAlignment = 0;      // always zero, since _movingPoint is zero
						headRoom = (_allocationsInfront.empty() ? _totalSize : _allocationsInfront.front()._start) - _movingPoint;
					}
				}
			}

			if (headRoom < (size+preBufferForAlignment)) return ~0u;

			auto result = _movingPoint;
			// just merge into the previous allocation marker, if we can
			if (!_allocationsBehind.empty() && (_allocationsBehind.end()-1)->_marker == ~0u) {
				(_allocationsBehind.end()-1)->_end = result+size+preBufferForAlignment;
			} else {
				_allocationsBehind.push_back(Allocation {result, result+size+preBufferForAlignment, ~0u});
			}
			_movingPoint += size+preBufferForAlignment;
			return result+preBufferForAlignment;
		}

		void Clear()
		{
			// All allocations are assigned to the current producer marker
			auto producerMarker = _asyncTracker->GetProducerMarker();
			for (auto r=_allocationsBehind.rbegin(); r!=_allocationsBehind.rend(); ++r) {
				if (r->_marker != ~0u) break;
				r->_marker = producerMarker;
			}
			for (auto r=_allocationsInfront.rbegin(); r!=_allocationsInfront.rend(); ++r) {
				if (r->_marker != ~0u) break;
				r->_marker = producerMarker;
			}
		}

		GPUSyncedSinglePage(unsigned totalSize, std::shared_ptr<RenderCore::Metal_Vulkan::IAsyncTracker> asyncTracker)
		: _asyncTracker(std::move(asyncTracker)), _movingPoint(0), _totalSize(totalSize)
		{}

		struct Allocation { unsigned _start, _end, _marker; };
		std::vector<Allocation> _allocationsBehind, _allocationsInfront;
		std::shared_ptr<RenderCore::Metal_Vulkan::IAsyncTracker> _asyncTracker;
		unsigned _movingPoint, _totalSize;
	};

	// two modes for the GPUSyncedSinglePageResource
	//		MAP_SEMI_PERSISTENT == 1 uses ResourceMap{} and writes directly to the device resource at first opportunity
	//		MAP_SEMI_PERSISTENT == 0 uses BltEncoder to write synchronized with a thread context, and requires extra copies
	#define MAP_SEMI_PERSISTENT 0

	class GPUSyncedSinglePageResource
	{
	public:
		std::shared_ptr<IResource> _resource;
		std::shared_ptr<IResource> _stagingResource;
		std::shared_ptr<IDevice> _device;
		unsigned _alignment = 0;

		#if MAP_SEMI_PERSISTENT == 1
			std::shared_ptr<RenderCore::Metal_Vulkan::IAsyncTracker> _asyncTracker;
			struct PendingDeallocate
			{
				RenderCore::Metal_Vulkan::IAsyncTracker::Marker _usageMarker;
				unsigned _start, _size;
			};
			std::vector<PendingDeallocate> _pendingDeallocates;
		#else
			GPUSyncedSinglePage _stagingResourceAllocator;
		#endif

		struct PendingTransfer
		{
			unsigned _srcStart, _srcSize;
			unsigned _dst;
		};
		std::vector<PendingTransfer> _pendingTransfers[2];

		SpanningHeap<unsigned> _spanningHeap;
		Threading::Mutex _lock;

		unsigned TryAllocateAndWrite(IteratorRange<const void*> data);
		void Deallocate(unsigned offset, unsigned size);
		void CompleteTransfers(IThreadContext&);
		void OnFrameBarrier();

		GPUSyncedSinglePageResource(std::shared_ptr<IDevice> device, const ResourceDesc& desc, unsigned stagingPageSize, std::shared_ptr<RenderCore::Metal_Vulkan::IAsyncTracker> asyncTracker);
		~GPUSyncedSinglePageResource();
	};

	std::shared_ptr<DeformAccelerator> DeformAcceleratorPool::CreateDeformAccelerator()
	{
		std::shared_ptr<DeformAccelerator> newAccelerator;
		if (_drawablesPool) {
			newAccelerator = _drawablesPool->MakeProtectedPtr<DeformAccelerator>();
		} else
			newAccelerator = std::make_shared<DeformAccelerator>();
		newAccelerator->_readiedInstances.resize(8, 0);
		++_aliveDeformerCount;
		newAccelerator->_containingPool = this;

		ScopedLock(_acceleratorsLock);
		_accelerators.push_back(newAccelerator);
		return newAccelerator;
	}

	void DeformAcceleratorPool::Attach(
		DeformAccelerator& accelerator,
		std::shared_ptr<IGeoDeformerConductor> deformAttachment)
	{
		#if defined(_DEBUG)
			assert(accelerator._containingPool == this);
		#endif
		assert(!accelerator._geoConductor);		// we can't attach geometry deformers more than once to a given deform accelerator
		assert(deformAttachment);
		accelerator._geoConductor = std::move(deformAttachment);

		unsigned reservationGPU = 0, reservationCPU = 0;
		accelerator._geoConductor->ReserveBytesRequired(1, reservationGPU, reservationCPU);
		accelerator._reservationPerInstance[AllocationType_GPUVB] = reservationGPU;
		accelerator._reservationPerInstance[AllocationType_CPUVB] = reservationCPU;
	}

	void DeformAcceleratorPool::Attach(
		DeformAccelerator& accelerator,
		std::shared_ptr<IUniformsDeformerConductor> deformAttachment)
	{
		#if defined(_DEBUG)
			assert(accelerator._containingPool == this);
		#endif
		assert(!accelerator._uniformsConductor);		// we can't attach geometry deformers more than once to a given deform accelerator
		assert(deformAttachment);
		assert(accelerator._semiPersistentUniformsSize == 0);		// don't attach both semi persistent uniforms and a uniforms conductor
		accelerator._uniformsConductor = std::move(deformAttachment);

		unsigned reservationGPU = 0, reservationCPU = 0;
		accelerator._uniformsConductor->ReserveBytesRequired(1, reservationGPU, reservationCPU);
		accelerator._reservationPerInstance[AllocationType_UniformBuffer] = reservationGPU;
		assert(reservationCPU == 0);
	}

	std::shared_ptr<IGeoDeformerConductor> DeformAcceleratorPool::GetGeoDeformerConductor(DeformAccelerator& deformAccelerator)
	{
		return deformAccelerator._geoConductor;
	}

	std::shared_ptr<IUniformsDeformerConductor> DeformAcceleratorPool::GetUniformsDeformerConductor(DeformAccelerator& deformAccelerator)
	{
		return deformAccelerator._uniformsConductor;
	}

	void Deform(
		IThreadContext& threadContext,
		IDeformAcceleratorPool& ipool,
		DeformersPacket& pkt)
	{
		auto& pool = *checked_cast<DeformAcceleratorPool*>(&ipool);

		assert(pool._boundThread == std::this_thread::get_id());
		pool._semiPersistentResource->CompleteTransfers(threadContext);
		auto attachedStorage = pool._temporaryStorageManager->BeginCmdListReservation();

		std::vector<std::shared_ptr<DeformAccelerator>> accelerators;
		accelerators.resize(pool._accelerators.size());		// subframe heap candidate
		unsigned reservationBytes[AllocationType_Max] = {0,0,0};
		unsigned allocationAlignments[AllocationType_Max] = {1,1,1};
		// unsigned maxInstanceCount = 0;

		allocationAlignments[AllocationType_GPUVB] = threadContext.GetDevice()->GetDeviceLimits()._unorderedAccessBufferOffsetAlignment;

		// We need to sort the requests the pkt and figure out the exact instances to run deforms for
		// note that we could skip this step if we just stored queuing information within the DeformAccelerator itself

		std::sort(b2e(pkt._deformables), [](const auto& lhs, const auto& rhs) { 
			if (lhs._deformAccelerator < rhs._deformAccelerator) return true;
			if (lhs._deformAccelerator > rhs._deformAccelerator) return false;
			return lhs._instance < rhs._instance;
		});

		VLA(unsigned, flatInstancesBuffer, pkt._deformables.size());
		unsigned* flatInstancesBufferI = flatInstancesBuffer;

		struct DeformerAndInstances
		{
			DeformAccelerator* _accelerator;

			unsigned _minEnabledInstance = ~0u;
			unsigned _maxEnabledInstance = 0;
			IteratorRange<const unsigned*> _flatInstances;
		};
		std::vector<DeformerAndInstances> deformersAndInstances;				// candidate for subframe heap
		{
			deformersAndInstances.reserve(pkt._deformables.size());
			pkt._deformables.push_back(DeformersPacket::Deformable{nullptr, ~0u});		// sentinel
			auto i = pkt._deformables.begin();
			while (i->_deformAccelerator) {
				auto start = i; ++i; while (i->_deformAccelerator == start->_deformAccelerator) ++i;

				DeformerAndInstances op;
				op._accelerator = start->_deformAccelerator;
				IteratorRange<const DeformersPacket::Deformable*> instances = {start, i};
				for (const auto& q:instances) {
					op._minEnabledInstance = std::min(op._minEnabledInstance, q._instance);
					op._maxEnabledInstance = std::max(op._maxEnabledInstance, q._instance);
				}

				auto accelerator = start->_deformAccelerator;
				if (accelerator->_readiedInstances.size() < (op._maxEnabledInstance+1+64-1)/64)
					accelerator->_readiedInstances.resize((op._maxEnabledInstance+1+64-1)/64, 0);

				// filter out instances already readied (also removed dupes)
				auto flatInstancesBegin = flatInstancesBufferI;
				for (auto&i:instances) {
					auto i64 = i._instance/64; auto bit = 1ull<<(i._instance&0x3f);
					if (!(accelerator->_readiedInstances[i64] & bit)) {
						*flatInstancesBufferI++ = i._instance;
						accelerator->_readiedInstances[i64] |= bit;
					}
				}

				op._flatInstances = { flatInstancesBegin, flatInstancesBufferI };
				if (op._flatInstances.empty()) continue;

				for (unsigned c=0; c<AllocationType_Max; ++c)
					reservationBytes[c] += CeilToMultiple(unsigned(accelerator->_reservationPerInstance[c] * op._flatInstances.size()), allocationAlignments[c]);

				deformersAndInstances.push_back(op);
			}
		}

		bool atLeastOneGPUOperator = false;

		{
			#if defined(_DEBUG)
				auto& metalContext = *Metal::DeviceContext::Get(threadContext);
				metalContext.BeginLabel("Deformers");
			#endif

			RenderCore::Metal_Vulkan::TemporaryStorageResourceMap cpuMap;
			RenderCore::Metal_Vulkan::TemporaryStorageResourceMap cbMap;
			RenderCore::Metal_Vulkan::TemporaryStorageResourceMap uniformBufferMap;
			RenderCore::Metal_Vulkan::BufferAndRange gpuBufferAndRange;
			RenderCore::VertexBufferView cpuVBV, gpuVBV;
			IteratorRange<void*> cpuDst, cbDst, uniformBufferDst;
			unsigned uniformBufferPageOffset = 0;

			if (reservationBytes[AllocationType::AllocationType_CPUVB]) {
				cpuMap = attachedStorage.MapStorage(reservationBytes[AllocationType::AllocationType_CPUVB], BindFlag::VertexBuffer, allocationAlignments[AllocationType_CPUVB]);
				cpuVBV = cpuMap.AsVertexBufferView();
				cpuDst = cpuMap.GetData();
				assert(cpuVBV._resource);
			}
			if (reservationBytes[AllocationType::AllocationType_GPUVB]) {
				gpuBufferAndRange = attachedStorage.AllocateDeviceOnlyRange(reservationBytes[AllocationType::AllocationType_GPUVB], BindFlag::VertexBuffer|BindFlag::UnorderedAccess|BindFlag::ShaderResource, allocationAlignments[AllocationType_GPUVB]);
				gpuVBV = gpuBufferAndRange.AsVertexBufferView();
				assert(gpuVBV._resource);
			}
			if (reservationBytes[AllocationType::AllocationType_UniformBuffer]) {
				uniformBufferMap = attachedStorage.MapStorageFromNamedPage(reservationBytes[AllocationType::AllocationType_UniformBuffer], pool._cbNamedPage, allocationAlignments[AllocationType_UniformBuffer]);
				uniformBufferDst = uniformBufferMap.GetData();
				uniformBufferPageOffset = uniformBufferMap.AsConstantBufferView()._prebuiltRangeBegin;
			}

			unsigned movingOffsets[AllocationType_Max] = {0, 0, 0};

			for (auto& deformerAndInstances:deformersAndInstances) {
				auto* accelerator = deformerAndInstances._accelerator;
				unsigned instanceCount = (unsigned)deformerAndInstances._flatInstances.size();

				std::shared_ptr<IResourceView> gpuBufferView;
				if (accelerator->_reservationPerInstance[AllocationType_GPUVB] != 0) {
					gpuBufferView = gpuBufferAndRange._resource->CreateBufferView(BindFlag::UnorderedAccess, gpuVBV._offset+movingOffsets[AllocationType_GPUVB], instanceCount*accelerator->_reservationPerInstance[AllocationType_GPUVB]);
					accelerator->_outputVBV = gpuVBV;
					atLeastOneGPUOperator = true;
				}

				if (accelerator->_geoConductor) {
					auto cpuOutputRange = MakeIteratorRange(
						PtrAdd(cpuDst.begin(), movingOffsets[AllocationType_CPUVB]),
						PtrAdd(cpuDst.begin(), movingOffsets[AllocationType_CPUVB]+instanceCount*accelerator->_reservationPerInstance[AllocationType_CPUVB]));

					accelerator->_geoConductor->Execute(
						threadContext, 
						deformerAndInstances._flatInstances,
						*gpuBufferView, cpuOutputRange,
						pool._readyInstancesMetrics);
				}

				if (accelerator->_uniformsConductor) {
					auto cbOutputRange = MakeIteratorRange(
						PtrAdd(uniformBufferDst.begin(), movingOffsets[AllocationType_UniformBuffer]),
						PtrAdd(uniformBufferDst.begin(), movingOffsets[AllocationType_UniformBuffer]+instanceCount*accelerator->_reservationPerInstance[AllocationType_UniformBuffer]));

					accelerator->_uniformsConductor->Execute(deformerAndInstances._flatInstances, cbOutputRange);
				}

				// set accelerator->_instanceToReadiedOffset & advance movingOffsets
				for (auto allType:{AllocationType_GPUVB, AllocationType_CPUVB, AllocationType_UniformBuffer}) {
					if (!accelerator->_reservationPerInstance[allType]) continue;

					if (accelerator->_instanceToReadiedOffset[allType].size() <= deformerAndInstances._maxEnabledInstance+1)
						accelerator->_instanceToReadiedOffset[allType].resize(deformerAndInstances._maxEnabledInstance+1, ~0u);

					for (auto i:deformerAndInstances._flatInstances) {
						accelerator->_instanceToReadiedOffset[allType][i] = uniformBufferPageOffset + movingOffsets[allType];
						movingOffsets[allType] += accelerator->_reservationPerInstance[allType];
					}

					movingOffsets[allType] = CeilToMultiple(movingOffsets[allType], allocationAlignments[allType]);	// ensure we end up with correct offset alignment
				}

				++pool._readyInstancesMetrics._acceleratorsReadied;
				pool._readyInstancesMetrics._instancesReadied += instanceCount;
			}

			for (auto allType:{AllocationType_GPUVB, AllocationType_CPUVB, AllocationType_UniformBuffer})
				assert(movingOffsets[allType] == reservationBytes[allType]);

			#if defined(_DEBUG)
				metalContext.EndLabel();
			#endif
		}

		pool._pendingVertexInputBarrier |= atLeastOneGPUOperator;
		pool._readyInstancesMetrics._cpuDeformAllocation += reservationBytes[AllocationType_CPUVB];
		pool._readyInstancesMetrics._gpuDeformAllocation += reservationBytes[AllocationType_GPUVB];
		pool._readyInstancesMetrics._uniformDeformAllocation += reservationBytes[AllocationType_UniformBuffer];

		// todo - we should add a pipeline barrier for any output buffers that were written by the GPU, before they ared used
		// by the GPU (ie, written by a compute shader to be read by a vertex shader, etc)
		pool._currentFrameAttachedStorage.emplace_back(std::move(attachedStorage));
	}

	void DeformAcceleratorPool::SetVertexInputBarrier(IThreadContext& threadContext) const
	{
		if (_pendingVertexInputBarrier) {
			// we're expecting the output to be used as a vertex attribute; so we require a barrier here
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

	std::shared_ptr<DeformersPacket> DeformAcceleratorPool::CreatePacket()
	{
		auto Destroyer = [this](auto* pkt) {
			assert(pkt->_pool == this);
			pkt->_pool = nullptr;
			pkt->_deformables.clear();
			--this->_alivePacketCount;
			this->_reusablePackets.emplace_back(pkt);
		};

		if (_reusablePackets.empty()) {
			auto newPacket = std::shared_ptr<DeformersPacket>(new DeformersPacket(), Destroyer);
			newPacket->_pool = this;
			++_alivePacketCount;
			return newPacket;
		} else {
			auto result = _reusablePackets.front(); _reusablePackets.pop_front();
			assert(!result->_pool);
			result->_pool = this;
			++_alivePacketCount;
			return std::shared_ptr<DeformersPacket>(result, Destroyer);
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

		std::shared_ptr<IResourceView> GetOutputSRV(DeformAccelerator& accelerator, unsigned instanceIdx)
		{
			#if defined(_DEBUG)
				auto f = instanceIdx / 64;
				// If you hit either of the following, it means the instance wasn't enabled. Each instance that will be used should
				// be enabled via EnableInstance() before usage (probably at the time it's initialized with current state data)
				assert(f < accelerator._readiedInstances.size());
				assert(accelerator._readiedInstances[f] & (1ull << uint64_t(instanceIdx & (64-1))));	
				assert(instanceIdx < accelerator._instanceToReadiedOffset[AllocationType_GPUVB].size());
			#endif
			return ((IResource*)accelerator._outputVBV._resource)->CreateBufferView(
				BindFlag::ShaderResource, 
				accelerator._instanceToReadiedOffset[AllocationType_GPUVB][instanceIdx],
				accelerator._reservationPerInstance[AllocationType_GPUVB]);
		}

		unsigned GetUniformPageBufferOffset(DeformAccelerator& accelerator, unsigned instanceIdx)
		{
			assert((accelerator._uniformsConductor && accelerator._reservationPerInstance[AllocationType_UniformBuffer]) || accelerator._semiPersistentUniformsSize);
			return accelerator._instanceToReadiedOffset[AllocationType_UniformBuffer][instanceIdx];
		}

		void BarrierGeoDeformTemporaries(IThreadContext& threadContext, IResourceView& gpuTemporariesBufferView)
		{
			Metal::BarrierHelper{threadContext}.Add(
				*gpuTemporariesBufferView.GetResource(),
				Metal::BarrierResourceUsage::ComputeShaderWrite(),
				Metal::BarrierResourceUsage::ComputeShaderRead());
		}
	}

	void DeformAcceleratorPool::AttachSemiPersistentUniforms(DeformAccelerator& deformAccelerator, unsigned size)
	{
		assert(deformAccelerator._containingPool == this);
		assert(!deformAccelerator._uniformsConductor);		// don't attach both a uniforms conductor and semi-persistent uniforms
		assert(deformAccelerator._semiPersistentUniformsSize == 0);
		assert(size);
		deformAccelerator._semiPersistentUniformsSize = size;
	}

	void DeformAcceleratorPool::SetSemiPersistentUniforms(DeformAccelerator& deformAccelerator, InstanceToken instance, IteratorRange<const void*> data)
	{
		assert(deformAccelerator._containingPool == this);
		assert(!deformAccelerator._uniformsConductor);
		assert(deformAccelerator._semiPersistentUniformsSize != 0);
		assert(data.size() == deformAccelerator._semiPersistentUniformsSize);
		if (deformAccelerator._instanceToReadiedOffset[AllocationType_UniformBuffer].size() <= instance)
			deformAccelerator._instanceToReadiedOffset[AllocationType_UniformBuffer].resize(instance+1, ~0u);
		if (deformAccelerator._instanceToReadiedOffset[AllocationType_UniformBuffer][instance] != ~0u)
			_semiPersistentResource->Deallocate(deformAccelerator._instanceToReadiedOffset[AllocationType_UniformBuffer][instance], deformAccelerator._semiPersistentUniformsSize);
		deformAccelerator._instanceToReadiedOffset[AllocationType_UniformBuffer][instance] = _semiPersistentResource->TryAllocateAndWrite(data);
	}

	void DeformAcceleratorPool::ReleaseSemiPersistentBuffer(unsigned offset, unsigned size)
	{
		_semiPersistentResource->Deallocate(offset, size);
	}

	std::shared_ptr<IResource> DeformAcceleratorPool::GetDynamicPageResource() const { return _cbPageResource; }
	std::shared_ptr<IResource> DeformAcceleratorPool::GetSemiPersistentPageResource() const { return _semiPersistentResource->_resource; }

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
		_semiPersistentResource->OnFrameBarrier();

		_lastFrameReadyInstancesMetrics = _readyInstancesMetrics;
		_readyInstancesMetrics = {};
	}

	auto DeformAcceleratorPool::GetMetrics() const -> ReadyInstancesMetrics { return _lastFrameReadyInstancesMetrics; }
	const std::shared_ptr<IDevice>& DeformAcceleratorPool::GetDevice() const { return _device; }

	DeformAcceleratorPool::DeformAcceleratorPool(std::shared_ptr<IDevice> device, std::shared_ptr<IDrawablesPool> drawablesPool)
	: _device(std::move(device))
	, _drawablesPool(std::move(drawablesPool))
	{
		auto* deviceVulkan = (RenderCore::IDeviceVulkan*)_device->QueryInterface(TypeHashCode<RenderCore::IDeviceVulkan>);
		if (deviceVulkan) {
			_asyncTracker = deviceVulkan->GetGraphicsQueueAsyncTracker();
			_temporaryStorageManager = std::make_unique<Metal_Vulkan::TemporaryStorageManager>(Metal::GetObjectFactory(), _asyncTracker);
			const unsigned cbAllocationSize = 1024*1024;
			_cbNamedPage = _temporaryStorageManager->CreateNamedPage(cbAllocationSize, BindFlag::ConstantBuffer);
			_cbPageResource = _temporaryStorageManager->GetResourceForNamedPage(_cbNamedPage);
		}
		auto semiPersistentResourceDesc = CreateDesc(BindFlag::TransferDst|BindFlag::ConstantBuffer, AllocationRules::HostVisibleSequentialWrite, LinearBufferDesc::Create(1024*1024));
		_semiPersistentResource = std::make_unique<GPUSyncedSinglePageResource>(_device, semiPersistentResourceDesc, 128*1024, _asyncTracker);
		_boundThread = std::this_thread::get_id();
	}

	DeformAcceleratorPool::~DeformAcceleratorPool() 
	{
		assert(!_alivePacketCount);
		assert(!_aliveDeformerCount);		// deformers hold a raw pointer back to the pool; so they must be destroyed before the pool
		_currentFrameAttachedStorage.clear();
	}

	static uint64_t s_nextDeformAcceleratorPool = 1;
	IDeformAcceleratorPool::IDeformAcceleratorPool() : _guid(s_nextDeformAcceleratorPool++) {}
	IDeformAcceleratorPool::~IDeformAcceleratorPool() {}

	auto DeformersPacket::Allocate() -> Deformable&
	{
		_deformables.emplace_back();	// may invalidate previously returned items
		return _deformables.back();
	}

	DeformersPacket::DeformersPacket() { _pool = nullptr; }
	DeformersPacket::~DeformersPacket() { assert(!_pool); }

	unsigned GPUSyncedSinglePageResource::TryAllocateAndWrite(IteratorRange<const void*> data)
	{
		auto size = (unsigned)data.size();
		size = CeilToMultiple(size, _alignment);

		ScopedLock(_lock);
		unsigned allocation = _spanningHeap.Allocate(size);
		if (allocation == ~0u) return ~0u;		// out of heap space
		assert((allocation%_alignment) == 0);	// should always be aligned because the block size is always a multiple of alignment

		#if MAP_SEMI_PERSISTENT == 1
			const bool partialResourceMap = false;
			if constexpr (partialResourceMap) {
				// can't do this with Vulkan VMA resources
				Metal::ResourceMap map{*_device, *_resource, Metal::ResourceMap::Mode::WriteDiscardPrevious, allocation, size};
				std::memcpy(map.GetData().begin(), data.begin(), data.size());
			} else {
				Metal::ResourceMap map{*_device, *_resource, Metal::ResourceMap::Mode::WriteDiscardPrevious};
				std::memcpy(PtrAdd(map.GetData().begin(), allocation), data.begin(), data.size());
			}
		#else
			auto stagingOffset = _stagingResourceAllocator.Allocate(size, _alignment);
			_pendingTransfers[0].emplace_back(PendingTransfer{stagingOffset, unsigned(data.size()), allocation});

			// we actually have the "permanently mapped" flag, which should minimize overhead here
			Metal::ResourceMap map{*_device, *_stagingResource, Metal::ResourceMap::Mode::WriteDiscardPrevious, stagingOffset, size};
			std::memcpy(map.GetData().begin(), data.begin(), data.size());
		#endif
	
		return allocation;
	}

	void GPUSyncedSinglePageResource::Deallocate(unsigned offset, unsigned size)
	{
		size = CeilToMultiple(size, _alignment);
		#if MAP_SEMI_PERSISTENT == 1
			_pendingDeallocates.emplace_back(PendingDeallocate{_asyncTracker->GetProducerMarker(), offset, size});
		#else
			ScopedLock(_lock);
			_spanningHeap.Deallocate(offset, size);
		#endif
	}

	void GPUSyncedSinglePageResource::CompleteTransfers(IThreadContext& threadContext)
	{
		#if MAP_SEMI_PERSISTENT == 0
			// Note -- CompleteTransfers must always be called with the same threadContext
			// it's not truly thread context aware
			{
				ScopedLock(_lock);
				std::swap(_pendingTransfers[0], _pendingTransfers[1]);
				_stagingResourceAllocator.Clear();
			}

			// _pendingTransfers[1] and _pendingTransferBuffer[1] are only used here,
			// so don't need to be locked. 
			auto blitEncoder = Metal::DeviceContext::Get(threadContext)->BeginBlitEncoder();
			for (auto& transfer:_pendingTransfers[1]) {
				// we may want a variation that can queue multiple transfers in a single step for this
				blitEncoder.Copy(
					CopyPartial_Dest{*_resource, transfer._dst},
					CopyPartial_Src{*_stagingResource, transfer._srcStart, transfer._srcStart+transfer._srcSize});
			}
			_pendingTransfers[1].clear();
		#endif
	}

	void GPUSyncedSinglePageResource::OnFrameBarrier()
	{
		#if MAP_SEMI_PERSISTENT == 1
			unsigned consumerMarker = _asyncTracker->GetConsumerMarker();
			auto w = _pendingDeallocates.begin();
			for (auto i=_pendingDeallocates.begin(); i!=_pendingDeallocates.end(); ++i) {
				if (i->_usageMarker < consumerMarker) {
					_spanningHeap.Deallocate(i->_start, i->_size);
				} else {
					*w++ = *i++;
				}
			}
			_pendingDeallocates.erase(w, _pendingDeallocates.end());
		#endif
	}

	GPUSyncedSinglePageResource::GPUSyncedSinglePageResource(std::shared_ptr<IDevice> device, const ResourceDesc& desc, unsigned stagingPageSize, std::shared_ptr<RenderCore::Metal_Vulkan::IAsyncTracker> asyncTracker)
	: _device(std::move(device)), _stagingResourceAllocator(stagingPageSize, asyncTracker)
	{
		#if MAP_SEMI_PERSISTENT == 1
			_asyncTracker = std::move(asyncTracker);
		#endif

		_alignment = _device->GetDeviceLimits()._constantBufferOffsetAlignment;
		_alignment = std::max(_alignment, 1u);
		assert(desc._type == ResourceDesc::Type::LinearBuffer);
		_resource = _device->CreateResource(desc, "single-page-resource");
		_spanningHeap = SpanningHeap<unsigned>(desc._linearBufferDesc._sizeInBytes);

		#if MAP_SEMI_PERSISTENT == 0
			_stagingResource = _device->CreateResource(
				CreateDesc(
					BindFlag::TransferSrc, AllocationRules::HostVisibleSequentialWrite | AllocationRules::PermanentlyMapped | AllocationRules::DisableAutoCacheCoherency | AllocationRules::DedicatedPage,
					LinearBufferDesc::Create(stagingPageSize)),
				"staging-page");
		#endif
	}

	GPUSyncedSinglePageResource::~GPUSyncedSinglePageResource()
	{}

	std::shared_ptr<IDeformAcceleratorPool> CreateDeformAcceleratorPool(std::shared_ptr<IDevice> device, std::shared_ptr<IDrawablesPool> drawablesPool)
	{
		return std::make_shared<DeformAcceleratorPool>(std::move(device), std::move(drawablesPool));
	}

	IGeoDeformerConductor::~IGeoDeformerConductor() = default;
	IUniformsDeformerConductor::~IUniformsDeformerConductor() = default;
}}
