// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformAccelerator.h"
#include "SimpleModelDeform.h"
#include "DeformAcceleratorInternal.h"
#include "Services.h"
#include "GPUTrackerHeap.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/ModelImmutableData.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Resource.h"
#include "../IDevice.h"
#include "../BufferView.h"
#include "../../Utility/ArithmeticUtils.h"
#include <vector>

#include "../Vulkan/Metal/CmdListAttachedStorage.h"

namespace RenderCore { namespace Techniques
{
	class DeformAcceleratorPool : public IDeformAcceleratorPool
	{
	public:
		std::shared_ptr<DeformAccelerator> CreateDeformAccelerator(
			StringSection<> initializer,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold) override;

		IteratorRange<const RendererGeoDeformInterface*> GetRendererGeoInterface(DeformAccelerator& accelerator) const override;

		std::vector<std::shared_ptr<ICPUDeformOperator>> GetOperations(DeformAccelerator& accelerator, uint64_t typeId) override;
		void EnableInstance(DeformAccelerator& accelerator, unsigned instanceIdx) override;
		void ReadyInstances(IThreadContext&) override;
		void OnFrameBarrier() override;

		VertexBufferView GetOutputVBV(DeformAccelerator& accelerator, unsigned instanceIdx) const override;

		DeformAcceleratorPool(std::shared_ptr<IDevice>);
		~DeformAcceleratorPool();

	private:
		std::vector<std::weak_ptr<DeformAccelerator>> _accelerators;
		std::shared_ptr<IDevice> _device;
		std::unique_ptr<RenderCore::Metal_Vulkan::TemporaryStorageManager> _temporaryStorageManager;
		std::shared_ptr<RenderCore::Metal_Vulkan::IAsyncTracker> _asyncTracker;
		std::vector<RenderCore::Metal_Vulkan::CmdListAttachedStorage> _currentFrameAttachedStorage;

		void ExecuteInstance(IThreadContext& threadContext, DeformAccelerator& a, unsigned instanceIdx, IteratorRange<void*> outputPartRange);
	};

	class DeformAccelerator
	{
	public:
		std::vector<uint64_t> _enabledInstances;
		std::vector<uint64_t> _readiedInstances;
		unsigned _minEnabledInstance = ~0u;
		unsigned _maxEnabledInstance = 0;

		std::vector<Internal::NascentDeformForGeo::CPUOp> _cpuDeformOps;
		std::vector<RendererGeoDeformInterface> _rendererGeoInterface;

		std::vector<uint8_t> _deformStaticDataInput;
		std::vector<uint8_t> _deformTemporaryBuffer;

		unsigned _outputVBSize = 0;
		VertexBufferView _outputVBV;
		
		#if defined(_DEBUG)
			DeformAcceleratorPool* _containingPool = nullptr;
		#endif
	};

	std::shared_ptr<DeformAccelerator> DeformAcceleratorPool::CreateDeformAccelerator(
		StringSection<> initializer,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold)
	{
		auto& opFactory = Services::GetDeformOperationFactory();
		auto deformInstantiations = opFactory.CreateDeformOperations(initializer, modelScaffold);
		auto newAccelerator = std::make_shared<DeformAccelerator>();
		newAccelerator->_enabledInstances.resize(8, 0);
		newAccelerator->_readiedInstances.resize(8, 0);
		#if defined(_DEBUG)
			newAccelerator->_containingPool = this;
		#endif

		////////////////////////////////////////////////////////////////////////////////////
		// Build deform streams

		unsigned preDeformStaticDataVBIterator = 0;
		unsigned deformTemporaryGPUVBIterator = 0;
		unsigned deformTemporaryCPUVBIterator = 0;
		unsigned postDeformVBIterator = 0;
		std::vector<Internal::SourceDataTransform> deformStaticLoadDataRequests;

		std::vector<Internal::NascentDeformForGeo> geoDeformStreams;
		std::vector<Internal::NascentDeformForGeo> skinControllerDeformStreams;
		geoDeformStreams.reserve(modelScaffold->ImmutableData()._geoCount);

		for (unsigned geo=0; geo<modelScaffold->ImmutableData()._geoCount; ++geo) {
			const auto& rg = modelScaffold->ImmutableData()._geos[geo];

			unsigned vertexCount = rg._vb._size / rg._vb._ia._vertexStride;
			auto deform = Internal::BuildNascentDeformForGeo(
				deformInstantiations, geo, vertexCount,
				preDeformStaticDataVBIterator, deformTemporaryGPUVBIterator, deformTemporaryCPUVBIterator, postDeformVBIterator);

			newAccelerator->_cpuDeformOps.insert(newAccelerator->_cpuDeformOps.end(), deform._cpuOps.begin(), deform._cpuOps.end());

			deformStaticLoadDataRequests.insert(
				deformStaticLoadDataRequests.end(),
				deform._cpuStaticDataLoadRequests.begin(), deform._cpuStaticDataLoadRequests.end());
			geoDeformStreams.push_back(std::move(deform));
		}

		skinControllerDeformStreams.reserve(modelScaffold->ImmutableData()._boundSkinnedControllerCount);
		for (unsigned geo=0; geo<modelScaffold->ImmutableData()._boundSkinnedControllerCount; ++geo) {
			const auto& rg = modelScaffold->ImmutableData()._boundSkinnedControllers[geo];

			unsigned vertexCount = rg._vb._size / rg._vb._ia._vertexStride;
			auto deform = Internal::BuildNascentDeformForGeo(
				deformInstantiations, geo + (unsigned)modelScaffold->ImmutableData()._geoCount, vertexCount,
				preDeformStaticDataVBIterator, deformTemporaryGPUVBIterator, deformTemporaryCPUVBIterator, postDeformVBIterator);

			newAccelerator->_cpuDeformOps.insert(newAccelerator->_cpuDeformOps.end(), deform._cpuOps.begin(), deform._cpuOps.end());

			deformStaticLoadDataRequests.insert(
				deformStaticLoadDataRequests.end(),
				deform._cpuStaticDataLoadRequests.begin(), deform._cpuStaticDataLoadRequests.end());
			skinControllerDeformStreams.push_back(std::move(deform));
		}

		if (newAccelerator->_cpuDeformOps.empty())
			return nullptr;

		////////////////////////////////////////////////////////////////////////////////////

		// Create the dynamic VB and assign it to all of the slots it needs to go to
		newAccelerator->_outputVBSize = postDeformVBIterator;

		if (preDeformStaticDataVBIterator) {
			newAccelerator->_deformStaticDataInput = Internal::GenerateDeformStaticInputForCPUDeform(
				*modelScaffold,
				MakeIteratorRange(deformStaticLoadDataRequests),
				preDeformStaticDataVBIterator);
		}

		if (deformTemporaryCPUVBIterator) {
			newAccelerator->_deformTemporaryBuffer.resize(deformTemporaryCPUVBIterator, 0);
		}

		newAccelerator->_rendererGeoInterface.reserve(geoDeformStreams.size() + skinControllerDeformStreams.size());
		for (const auto&s:geoDeformStreams) newAccelerator->_rendererGeoInterface.push_back(s._rendererInterf);
		for (const auto&s:skinControllerDeformStreams) newAccelerator->_rendererGeoInterface.push_back(s._rendererInterf);

		_accelerators.push_back(newAccelerator);

		return newAccelerator;
	}

	IteratorRange<const RendererGeoDeformInterface*> DeformAcceleratorPool::GetRendererGeoInterface(DeformAccelerator& accelerator) const
	{
		#if defined(_DEBUG)
			assert(accelerator._containingPool == this);
		#endif
		return accelerator._rendererGeoInterface;
	}

	std::vector<std::shared_ptr<ICPUDeformOperator>> DeformAcceleratorPool::GetOperations(DeformAccelerator& accelerator, size_t typeId)
	{
		#if defined(_DEBUG)
			assert(accelerator._containingPool == this);
		#endif
		std::vector<std::shared_ptr<ICPUDeformOperator>> result;
		for (const auto&i:accelerator._cpuDeformOps)
			if (i._deformOp->QueryInterface(typeId))
				result.push_back(i._deformOp);
		return result;
	}

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
		accelerator._maxEnabledInstance = std::min(accelerator._maxEnabledInstance, instanceIdx);
	}

	void DeformAcceleratorPool::ExecuteInstance(IThreadContext& threadContext, DeformAccelerator& a, unsigned instanceIdx, IteratorRange<void*> outputPartRange)
	{
		assert(instanceIdx!=~0u);
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);

		auto staticDataPartRange = MakeIteratorRange(a._deformStaticDataInput);
		auto temporaryDeformRange = MakeIteratorRange(a._deformTemporaryBuffer);

		for (const auto&d:a._cpuDeformOps) {
			ICPUDeformOperator::VertexElementRange inputElementRanges[16];
			assert(d._inputElements.size() <= dimof(inputElementRanges));
			for (unsigned c=0; c<d._inputElements.size(); ++c) {
				if (d._inputElements[c]._vbIdx == Internal::VB_CPUStaticData) {
					inputElementRanges[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(AsPointer(staticDataPartRange.begin()), d._inputElements[c]._offset), AsPointer(staticDataPartRange.end())),
						d._inputElements[c]._stride, d._inputElements[c]._format);
				} else {
					assert(d._inputElements[c]._vbIdx == Internal::VB_CPUTemporaryDeform);
					inputElementRanges[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(AsPointer(temporaryDeformRange.begin()), d._inputElements[c]._offset), AsPointer(temporaryDeformRange.end())),
						d._inputElements[c]._stride, d._inputElements[c]._format);
				}
			}

			ICPUDeformOperator::VertexElementRange outputElementRanges[16];
			assert(d._outputElements.size() <= dimof(outputElementRanges));
			for (unsigned c=0; c<d._outputElements.size(); ++c) {
				if (d._outputElements[c]._vbIdx == Internal::VB_PostDeform) {
					outputElementRanges[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(outputPartRange.begin(), d._outputElements[c]._offset), outputPartRange.end()),
						d._outputElements[c]._stride, d._outputElements[c]._format);
				} else {
					assert(d._outputElements[c]._vbIdx == Internal::VB_CPUTemporaryDeform);
					outputElementRanges[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(AsPointer(temporaryDeformRange.begin()), d._outputElements[c]._offset), AsPointer(temporaryDeformRange.end())),
						d._outputElements[c]._stride, d._outputElements[c]._format);
				}
			}

			// Execute the actual deform op
			d._deformOp->Execute(
				instanceIdx,
				MakeIteratorRange(inputElementRanges, &inputElementRanges[d._inputElements.size()]),
				MakeIteratorRange(outputElementRanges, &outputElementRanges[d._outputElements.size()]));
		}
	}

	void DeformAcceleratorPool::ReadyInstances(IThreadContext& threadContext)
	{
		auto attachedStorage = _temporaryStorageManager->BeginCmdListReservation();

		std::shared_ptr<DeformAccelerator> accelerators[_accelerators.size()];
		unsigned c=0;
		unsigned totalAllocationSize = 0;
		for (const auto&a:_accelerators) {
			auto accelerator = a.lock();

			if (accelerator->_maxEnabledInstance < accelerator->_minEnabledInstance) continue;
			auto fMin = accelerator->_minEnabledInstance / 64, fMax = accelerator->_maxEnabledInstance / 64;
			auto instanceCount = 0;
			for (auto f=fMin; f<=fMax; ++f)
				instanceCount += popcount(accelerator->_enabledInstances[f] & ~accelerator->_readiedInstances[f]);
			
			if (instanceCount) {
				totalAllocationSize += accelerator->_outputVBSize * instanceCount;
				accelerators[c++] = std::move(accelerator);
			} else {
				for (auto f=fMin; f<=fMax; ++f)
					accelerator->_enabledInstances[f] = 0;
				accelerator->_minEnabledInstance = ~0u;
				accelerator->_maxEnabledInstance = 0u;
			}
		}

		if (!totalAllocationSize)
			return;

		const auto defaultPageSize = 1024*1024;
		auto map = attachedStorage.MapStorage(totalAllocationSize, BindFlag::VertexBuffer, defaultPageSize);
		auto vbv = map.AsVertexBufferView();
		assert(vbv._resource);
		auto dst = map.GetData();
		unsigned movingOffset = 0;

		for (c=0; c<_accelerators.size(); ++c) {
			auto accelerator = accelerators[c];
			if (!accelerator) continue;

			accelerator->_outputVBV = vbv;
			accelerator->_outputVBV._offset += movingOffset;

			auto instanceData = MakeIteratorRange(PtrAdd(dst.begin(), movingOffset), PtrAdd(dst.begin(), movingOffset+accelerator->_outputVBSize));

			if (accelerator->_readiedInstances.size() < accelerator->_enabledInstances.size())
				accelerator->_readiedInstances.resize(accelerator->_enabledInstances.size(), 0);

			unsigned instanceCount = 0;
			auto fMin = accelerator->_minEnabledInstance / 64, fMax = accelerator->_maxEnabledInstance / 64;
			for (auto f=fMin; f<=fMax; ++f) {
				auto active = accelerator->_enabledInstances[f] & ~accelerator->_readiedInstances[f];
				while (active) {
					auto bit = xl_ctz8(active);
					active ^= 1ull<<uint64_t(bit);

					assert(instanceData.end() <= dst.end());
					auto instance = f*64+bit;
					ExecuteInstance(threadContext, *accelerator, instance, instanceData);
					instanceData.first = PtrAdd(instanceData.first, accelerator->_outputVBSize);
					instanceData.second = PtrAdd(instanceData.second, accelerator->_outputVBSize);
					++instanceCount;
				}
				accelerator->_readiedInstances[f] |= accelerator->_enabledInstances[f];
				accelerator->_enabledInstances[f] = 0;
			}

			accelerator->_minEnabledInstance = ~0u;
			accelerator->_maxEnabledInstance = 0u;
			movingOffset += instanceCount * accelerator->_outputVBSize;
		}

		assert(movingOffset == totalAllocationSize);

		// todo - we should add a pipeline barrier for any output buffers that were written by the GPU, before they ared used
		// by the GPU (ie, written by a compute shader to be read by a vertex shader, etc)
		_currentFrameAttachedStorage.emplace_back(std::move(attachedStorage));
	}

	VertexBufferView DeformAcceleratorPool::GetOutputVBV(DeformAccelerator& accelerator, unsigned instanceIdx) const
	{
		#if defined(_DEBUG)
			assert(accelerator._containingPool == this);
			auto f = instanceIdx / 64;
			// If you hit either of the following, it means the instance wasn't enabled. Each instance that will be used should
			// be enabled via EnableInstance() before usage (probably at the time it's initialized with current state data)
			assert(f < accelerator._readiedInstances.size());
			assert(accelerator._readiedInstances[f] & (1ull << uint64_t(instanceIdx & (64-1))));	
		#endif
		assert(accelerator._outputVBV._resource);
		VertexBufferView result = accelerator._outputVBV;
		result._offset += accelerator._outputVBSize * instanceIdx;
		return result;
	}

	inline void DeformAcceleratorPool::OnFrameBarrier()
	{
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
	}

	DeformAcceleratorPool::DeformAcceleratorPool(std::shared_ptr<IDevice> device)
	: _device(std::move(device))
	{
		auto* deviceVulkan = (RenderCore::IDeviceVulkan*)_device->QueryInterface(typeid(RenderCore::IDeviceVulkan).hash_code());
		if (deviceVulkan) {
			_asyncTracker = deviceVulkan->GetAsyncTracker();
			_temporaryStorageManager = std::make_unique<Metal_Vulkan::TemporaryStorageManager>(Metal::GetObjectFactory(), _asyncTracker);
		}
	}

	DeformAcceleratorPool::~DeformAcceleratorPool() {}

	static uint64_t s_nextDeformAcceleratorPool = 1;
	IDeformAcceleratorPool::IDeformAcceleratorPool() 
	: _guid(s_nextDeformAcceleratorPool++)
	{}
	IDeformAcceleratorPool::~IDeformAcceleratorPool() {}

	std::shared_ptr<IDeformAcceleratorPool> CreateDeformAcceleratorPool(std::shared_ptr<IDevice> device)
	{
		return std::make_shared<DeformAcceleratorPool>(std::move(device));
	}

}}
