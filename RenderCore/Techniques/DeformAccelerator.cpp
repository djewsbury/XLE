// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformAccelerator.h"
#include "SimpleModelDeform.h"
#include "DeformAcceleratorInternal.h"
#include "Services.h"
#include "GPUTrackerHeap.h"
#include "CommonUtils.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/ModelScaffoldInternal.h"
#include "../Assets/ModelImmutableData.h"
#include "../GeoProc/MeshDatabase.h"        // for GeoProc::Copy
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Resource.h"
#include "../IDevice.h"
#include "../BufferView.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/ArithmeticUtils.h"
#include <vector>

#include "../Vulkan/Metal/CmdListAttachedStorage.h"

namespace RenderCore { namespace Techniques
{
	namespace Internal
	{
		static std::vector<uint8_t> GenerateDeformStaticInputForCPUDeform(
			const RenderCore::Assets::ModelScaffold& modelScaffold,
			IteratorRange<const SourceDataTransform*> inputLoadRequests,
			unsigned destinationBufferSize);
	}

	class DeformAcceleratorPool : public IDeformAcceleratorPool
	{
	public:
		std::shared_ptr<DeformAccelerator> CreateDeformAccelerator(
			StringSection<> initializer,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
			const std::string& modelScaffoldName) override;

		const DeformerToRendererBinding& GetDeformerToRendererBinding(DeformAccelerator& accelerator) const override;

		std::vector<std::shared_ptr<ICPUDeformOperator>> GetOperations(DeformAccelerator& accelerator, uint64_t typeId) override;
		void EnableInstance(DeformAccelerator& accelerator, unsigned instanceIdx) override;
		void ReadyInstances(IThreadContext&) override;
		void OnFrameBarrier() override;

		VertexBufferView GetOutputVBV(DeformAccelerator& accelerator, unsigned instanceIdx) const override;

		::Assets::DependencyValidation GetDependencyValidation(DeformAccelerator& accelerator) const override;

		DeformAcceleratorPool(std::shared_ptr<IDevice>);
		~DeformAcceleratorPool();

	private:
		std::vector<std::weak_ptr<DeformAccelerator>> _accelerators;
		std::shared_ptr<IDevice> _device;
		std::unique_ptr<RenderCore::Metal_Vulkan::TemporaryStorageManager> _temporaryStorageManager;
		std::shared_ptr<RenderCore::Metal_Vulkan::IAsyncTracker> _asyncTracker;
		std::vector<RenderCore::Metal_Vulkan::CmdListAttachedStorage> _currentFrameAttachedStorage;

		void ExecuteCPUInstance(IThreadContext& threadContext, DeformAccelerator& a, unsigned instanceIdx, IteratorRange<void*> outputPartRange);
		void ExecuteGPUInstance(IThreadContext& threadContext, DeformAccelerator& a, IteratorRange<const unsigned*> instanceIdx, IResourceView& dstVB);
	};

	class DeformAccelerator
	{
	public:
		std::vector<uint64_t> _enabledInstances;
		std::vector<uint64_t> _readiedInstances;
		unsigned _minEnabledInstance = ~0u;
		unsigned _maxEnabledInstance = 0;

		/*struct GPUOp
		{
			std::shared_ptr<IGPUDeformOperator> _operator;
			std::shared_ptr<IResourceView> _staticDataView;
		};
		std::vector<GPUOp> _gpuDeformOps;*/
		std::vector<std::shared_ptr<ICPUDeformOperator>> _cpuDeformOps;
		std::vector<std::shared_ptr<IGPUDeformOperator>> _gpuDeformOps;
		DeformerToRendererBinding _rendererGeoInterface;

		std::atomic<bool> _gpuDeformOpsReady;
		std::future<void> _gpuDeformOpsFuture;

		std::vector<uint8_t> _deformStaticDataInput;
		std::vector<uint8_t> _deformTemporaryBuffer;

		unsigned _outputVBSize = 0;
		VertexBufferView _outputVBV;

		::Assets::DependencyValidation _dependencyValidation;
		
		#if defined(_DEBUG)
			DeformAcceleratorPool* _containingPool = nullptr;
		#endif
	};

	std::shared_ptr<DeformAccelerator> DeformAcceleratorPool::CreateDeformAccelerator(
		StringSection<> initializer,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::string& modelScaffoldName)
	{
		auto& opFactory = Services::GetDeformOperationFactorySet();
		auto deformers = opFactory.CreateDeformOperations(initializer, modelScaffold, modelScaffoldName);
		auto newAccelerator = std::make_shared<DeformAccelerator>();
		newAccelerator->_enabledInstances.resize(8, 0);
		newAccelerator->_readiedInstances.resize(8, 0);
		#if defined(_DEBUG)
			newAccelerator->_containingPool = this;
		#endif
		newAccelerator->_gpuDeformOpsReady.store(false);

		////////////////////////////////////////////////////////////////////////////////////
		// Build deform streams

		Internal::DeformBufferIterators bufferIterators;
		std::vector<Internal::WorkingDeformer> workingDeformers;
		workingDeformers.reserve(deformers.size());

		for (auto& d:deformers) {
			Internal::WorkingDeformer workingDeformer;
			workingDeformer._instantiations = MakeIteratorRange(d._instantiations);
			workingDeformers.push_back(std::move(workingDeformer));
		};

		newAccelerator->_rendererGeoInterface = Internal::CreateDeformBindings(
			MakeIteratorRange(workingDeformers), bufferIterators,
			modelScaffold, modelScaffoldName);

		////////////////////////////////////////////////////////////////////////////////////

		if (!bufferIterators._gpuStaticDataLoadRequests.empty()) {
			struct Captures
			{
				BufferUploads::TransactionMarker _staticDataBufferMarker;
			};
			auto captures = std::make_shared<Captures>();
			captures->_staticDataBufferMarker = LoadStaticResourceFullyAsync(
				{bufferIterators._gpuStaticDataLoadRequests.begin(), bufferIterators._gpuStaticDataLoadRequests.end()}, 
				bufferIterators._bufferIterators[Internal::VB_GPUStaticData],
				modelScaffold, BindFlag::UnorderedAccess,
				(StringMeld<64>() << "[deform]" << modelScaffoldName).AsStringSection());
		}

		////////////////////////////////////////////////////////////////////////////////////

		// Create the dynamic VB and assign it to all of the slots it needs to go to
		newAccelerator->_outputVBSize = bufferIterators._bufferIterators[Internal::VB_PostDeform];

		if (!bufferIterators._cpuStaticDataLoadRequests.empty()) {
			newAccelerator->_deformStaticDataInput = Internal::GenerateDeformStaticInputForCPUDeform(
				*modelScaffold,
				MakeIteratorRange(bufferIterators._cpuStaticDataLoadRequests),
				bufferIterators._bufferIterators[Internal::VB_CPUStaticData]);
		}

		if (bufferIterators._bufferIterators[Internal::VB_CPUDeformTemporaries]) {
			newAccelerator->_deformTemporaryBuffer.resize(bufferIterators._bufferIterators[Internal::VB_CPUDeformTemporaries], 0);
		}

		_accelerators.push_back(newAccelerator);
		return newAccelerator;
	}

	const DeformerToRendererBinding& DeformAcceleratorPool::GetDeformerToRendererBinding(DeformAccelerator& accelerator) const
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
			if (i->QueryInterface(typeId))
				result.push_back(i);
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

	void DeformAcceleratorPool::ExecuteCPUInstance(IThreadContext& threadContext, DeformAccelerator& a, unsigned instanceIdx, IteratorRange<void*> outputPartRange)
	{
#if 0
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
					assert(d._inputElements[c]._vbIdx == Internal::VB_CPUDeformTemporaries);
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
					assert(d._outputElements[c]._vbIdx == Internal::VB_CPUDeformTemporaries);
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
#endif
	}

	void DeformAcceleratorPool::ExecuteGPUInstance(IThreadContext& threadContext, DeformAccelerator& a, IteratorRange<const unsigned*> instanceIdx, IResourceView& dstVB)
	{
		for (const auto&d:a._gpuDeformOps) {
			d->ExecuteGPU(
				threadContext,
				instanceIdx[0],		// todo -- all instances
				*(IResourceView*)nullptr, // *d._staticDataView,
				*(IResourceView*)nullptr,
				dstVB);
		}
	}

	void DeformAcceleratorPool::ReadyInstances(IThreadContext& threadContext)
	{
		auto attachedStorage = _temporaryStorageManager->BeginCmdListReservation();

		std::shared_ptr<DeformAccelerator> accelerators[_accelerators.size()];
		unsigned c=0;
		unsigned totalAllocationSize = 0;
		unsigned maxInstanceCount = 0;
		for (const auto&a:_accelerators) {
			auto accelerator = a.lock();
			if (!accelerator || !accelerator->_gpuDeformOpsReady) continue;

			if (accelerator->_maxEnabledInstance < accelerator->_minEnabledInstance) continue;
			auto fMin = accelerator->_minEnabledInstance / 64, fMax = accelerator->_maxEnabledInstance / 64;
			auto instanceCount = 0u;
			for (auto f=fMin; f<=fMax; ++f)
				instanceCount += popcount(accelerator->_enabledInstances[f] & ~accelerator->_readiedInstances[f]);
			
			if (instanceCount) {
				totalAllocationSize += accelerator->_outputVBSize * instanceCount;
				accelerators[c++] = std::move(accelerator);
				maxInstanceCount = std::max(maxInstanceCount, instanceCount);
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

#if 0
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
#else
		auto bufferAndRange = attachedStorage.AllocateRange(totalAllocationSize, BindFlag::VertexBuffer|BindFlag::UnorderedAccess, defaultPageSize);
		auto vbv = bufferAndRange.AsVertexBufferView();
		assert(vbv._resource);
		unsigned movingOffset = 0;

		unsigned instanceList[maxInstanceCount];

		for (c=0; c<_accelerators.size(); ++c) {
			auto accelerator = accelerators[c];
			if (!accelerator) continue;

			accelerator->_outputVBV = vbv;
			accelerator->_outputVBV._offset += movingOffset;

			if (accelerator->_readiedInstances.size() < accelerator->_enabledInstances.size())
				accelerator->_readiedInstances.resize(accelerator->_enabledInstances.size(), 0);

			unsigned instanceCount = 0;
			auto fMin = accelerator->_minEnabledInstance / 64, fMax = accelerator->_maxEnabledInstance / 64;
			for (auto f=fMin; f<=fMax; ++f) {
				auto active = accelerator->_enabledInstances[f] & ~accelerator->_readiedInstances[f];
				while (active) {
					auto bit = xl_ctz8(active);
					active ^= 1ull<<uint64_t(bit);

					instanceList[instanceCount] = f*64+bit;
					++instanceCount;
				}
				accelerator->_readiedInstances[f] |= accelerator->_enabledInstances[f];
				accelerator->_enabledInstances[f] = 0;
			}

			auto view = bufferAndRange._resource->CreateBufferView(BindFlag::UnorderedAccess, accelerator->_outputVBV._offset, instanceCount*accelerator->_outputVBSize);
			ExecuteGPUInstance(
				threadContext, *accelerator,
				MakeIteratorRange(instanceList, &instanceList[instanceCount]),
				*view);

			accelerator->_minEnabledInstance = ~0u;
			accelerator->_maxEnabledInstance = 0u;
			movingOffset += instanceCount * accelerator->_outputVBSize;
		}
#endif

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

	::Assets::DependencyValidation DeformAcceleratorPool::GetDependencyValidation(DeformAccelerator& accelerator) const
	{
		#if defined(_DEBUG)
			assert(accelerator._containingPool == this);
		#endif
		assert(accelerator._gpuDeformOpsReady);
		return accelerator._dependencyValidation;
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		static void LinkDeformers(
			/* in */ IteratorRange<const InputElementDesc*> animatedElementsInput,
			/* in */ unsigned vertexCount,
			/* in */ unsigned animatedElementsStride,
			/* in */ IteratorRange<const DeformOperationInstantiation**> instantiations,
			/* out */ IteratorRange<DeformerInputBinding::GeoBinding*> resultDeformerBindings,
			/* out */ DeformerToRendererBinding::GeoBinding& resultRendererBinding,
			/* in/out */ DeformBufferIterators& bufferIterators,
			/* out */ bool& gpuStaticDataLoadRequired)
		{
			// Given some input vertex format plus one or more deformer instantiations, calculate how we should
			// link together these deformers, and what vertex format should eventually be expected by the renderer
			// At this point, we're operating on a single "geo" object
			assert(resultRendererBinding._geoId != ~0u);
			std::vector<uint64_t> workingSuppressedElements;
			std::vector<InputElementDesc> workingGeneratedElements;

			std::vector<InputElementDesc> workingTemporarySpaceElements_cpu;
			std::vector<InputElementDesc> workingTemporarySpaceElements_gpu;
			std::vector<InputElementDesc> workingSourceDataElements_cpu;
			
			for (auto d=instantiations.begin(); d!=instantiations.end(); ++d) {
				if (!*d) continue;
				const auto&def = **d;
				unsigned dIdx = (unsigned)std::distance(instantiations.begin(), d);
				
				auto& workingTemporarySpaceElements = def._cpuDeformer ? workingTemporarySpaceElements_cpu : workingTemporarySpaceElements_gpu;
				
				/////////////////// CPU type operator ///////////////////
				for (auto&e:def._upstreamSourceElements) {
					// find a matching source element generated from another deform op
					// (note that CPU operations can only take inputs from other CPU deforms)
					auto i = std::find_if(
						workingGeneratedElements.begin(), workingGeneratedElements.end(),
						[e](const auto& wge) {
							return wge._semanticName == e._semantic && wge._semanticIndex == e._semanticIndex;
						});
					if (i != workingGeneratedElements.end()) {
						auto existing = std::find_if(workingTemporarySpaceElements.begin(), workingTemporarySpaceElements.end(), [e](const auto& wge) { return wge._semanticName == e._semantic && wge._semanticIndex == e._semanticIndex; });
						if (existing != workingTemporarySpaceElements.end()) {
							assert(existing->_nativeFormat == i->_nativeFormat);		// problems with formats changing during deform
						} else
							workingTemporarySpaceElements.push_back(*i);
						workingGeneratedElements.erase(i);
					} else {
						if (def._cpuDeformer) {
							// If it's not generated by some deform op, we look for it in the static data
							auto existing = std::find_if(workingSourceDataElements_cpu.begin(), workingSourceDataElements_cpu.end(), 
								[&e](const auto& c) { return c._semanticName == e._semantic && c._semanticIndex == e._semanticIndex; });
							if (existing != workingSourceDataElements_cpu.end()) {
								assert(existing->_nativeFormat == e._format);		// avoid loading the same attribute twice with different formats
							} else
								workingSourceDataElements_cpu.push_back(InputElementDesc{e._semantic, e._semanticIndex, e._format});
						} else {
							auto q = std::find_if(
								animatedElementsInput.begin(), animatedElementsInput.end(),
								[e](const auto& wge) {
									return wge._semanticName == e._semantic && wge._semanticIndex == e._semanticIndex;
								});
							if (q==animatedElementsInput.end())
								Throw(std::runtime_error("Could not match input element (" + e._semantic + ") for GPU deform operation"));
							gpuStaticDataLoadRequired = true;
						}
					}
				}

				// Before we add our own static data, we should remove any working elements that have been
				// suppressed
				auto i = std::remove_if(
					workingGeneratedElements.begin(), workingGeneratedElements.end(),
					[&def](const auto& wge) {
						auto hash = Hash64(wge._semanticName) + wge._semanticIndex;
						return std::find(def._suppressElements.begin(), def._suppressElements.end(), hash) != def._suppressElements.end();
					});
				workingGeneratedElements.erase(i, workingGeneratedElements.end());		// these get removed and don't go into temporary space. They are just never used

				for (const auto& e:def._generatedElements) {
					auto existing = std::find_if(workingGeneratedElements.begin(), workingGeneratedElements.end(), 
						[&e](const auto& c) { return c._semanticName == e._semantic && c._semanticIndex == e._semanticIndex; });
					if (existing != workingGeneratedElements.end())
						workingGeneratedElements.erase(existing);	// this was generated, but eventually overwritten
					workingGeneratedElements.push_back(InputElementDesc{e._semantic, e._semanticIndex, e._format, dIdx});
				}

				workingSuppressedElements.insert(
					workingSuppressedElements.end(),
					def._suppressElements.begin(), def._suppressElements.end());
			}

			// Sort the elements from largest to smallest, to promote ideal alignment 
			std::sort(workingSourceDataElements_cpu.begin(), workingSourceDataElements_cpu.end(), [](auto& lhs, auto& rhs) { return BitsPerPixel(lhs._nativeFormat) > BitsPerPixel(rhs._nativeFormat); });
			std::sort(workingTemporarySpaceElements_cpu.begin(), workingTemporarySpaceElements_cpu.end(), [](auto& lhs, auto& rhs) { return BitsPerPixel(lhs._nativeFormat) > BitsPerPixel(rhs._nativeFormat); });
			std::sort(workingTemporarySpaceElements_gpu.begin(), workingTemporarySpaceElements_gpu.end(), [](auto& lhs, auto& rhs) { return BitsPerPixel(lhs._nativeFormat) > BitsPerPixel(rhs._nativeFormat); });
			std::sort(workingGeneratedElements.begin(), workingGeneratedElements.end(), [](auto& lhs, auto& rhs) { return BitsPerPixel(lhs._nativeFormat) > BitsPerPixel(rhs._nativeFormat); });

			// put out the _inputSlot value from each input layout -- this is the index of the first deformer to write to this element
			std::vector<unsigned> workingTemporarySpaceElements_cpu_firstSourceDeformer;
			std::vector<unsigned> workingTemporarySpaceElements_gpu_firstSourceDeformer;
			std::vector<unsigned> workingGeneratedElements_firstSourceDeformer;
			workingTemporarySpaceElements_cpu_firstSourceDeformer.reserve(workingTemporarySpaceElements_cpu.size());
			for (auto&e:workingTemporarySpaceElements_cpu) workingTemporarySpaceElements_cpu_firstSourceDeformer.push_back(e._inputSlot);
			workingTemporarySpaceElements_gpu_firstSourceDeformer.reserve(workingTemporarySpaceElements_gpu.size());
			for (auto&e:workingTemporarySpaceElements_gpu) workingTemporarySpaceElements_gpu_firstSourceDeformer.push_back(e._inputSlot);
			workingGeneratedElements_firstSourceDeformer.reserve(workingGeneratedElements.size());
			for (auto&e:workingGeneratedElements) workingGeneratedElements_firstSourceDeformer.push_back(e._inputSlot);

			for (auto&e:workingTemporarySpaceElements_cpu) e._inputSlot = VB_CPUDeformTemporaries;
			for (auto&e:workingTemporarySpaceElements_gpu) e._inputSlot = VB_GPUDeformTemporaries;
			for (auto&e:workingGeneratedElements) e._inputSlot = VB_PostDeform;
			for (auto&e:workingSourceDataElements_cpu) e._inputSlot = VB_CPUStaticData;

			workingGeneratedElements = NormalizeInputAssembly(workingGeneratedElements);
			workingTemporarySpaceElements_cpu = NormalizeInputAssembly(workingTemporarySpaceElements_cpu);
			workingTemporarySpaceElements_gpu = NormalizeInputAssembly(workingTemporarySpaceElements_gpu);
			workingSourceDataElements_cpu = NormalizeInputAssembly(workingSourceDataElements_cpu);

			// Figure out how to arrange all of the input and output vertices in the 
			// deform VBs.
			// We've got 3 to use
			//		1. an input static data buffer; which contains values read directly from the source data (perhaps processed for format)
			//		2. a deform temporary buffer; which contains data written out from deform operations, and read in by others
			//		3. a final output buffer; which contains resulting vertex data that is fed into the render operation
			
			unsigned vbStrides[VB_Count] = {0};
			unsigned vbOffsets[VB_Count] = {0};
			unsigned vbSizes[VB_Count] = {0};
			{
				vbStrides[VB_CPUStaticData] = CalculateVertexStrideForSlot(workingSourceDataElements_cpu, VB_CPUStaticData);
				vbOffsets[VB_CPUStaticData] = bufferIterators._bufferIterators[VB_CPUStaticData];
				vbSizes[VB_CPUStaticData] = vbStrides[VB_CPUStaticData] * vertexCount;
				bufferIterators._bufferIterators[VB_CPUStaticData] += vbStrides[VB_CPUStaticData] * vertexCount;

				bufferIterators._cpuStaticDataLoadRequests.reserve(workingSourceDataElements_cpu.size());
				for (unsigned c=0; c<workingSourceDataElements_cpu.size(); ++c) {
					const auto& workingE = workingSourceDataElements_cpu[c];
					bufferIterators._cpuStaticDataLoadRequests.push_back({
						resultRendererBinding._geoId, Hash64(workingE._semanticName) + workingE._semanticIndex,
						workingE._nativeFormat, workingE._alignedByteOffset + vbOffsets[VB_CPUStaticData],
						vbStrides[VB_CPUStaticData], vertexCount});
				}
			}

			{
				vbStrides[VB_CPUDeformTemporaries] = CalculateVertexStrideForSlot(workingTemporarySpaceElements_cpu, VB_CPUDeformTemporaries);
				vbOffsets[VB_CPUDeformTemporaries] = bufferIterators._bufferIterators[VB_CPUDeformTemporaries];
				vbSizes[VB_CPUDeformTemporaries] = vbStrides[VB_CPUDeformTemporaries] * vertexCount;
				bufferIterators._bufferIterators[VB_CPUDeformTemporaries] += vbStrides[VB_CPUDeformTemporaries] * vertexCount;
			}

			{
				vbStrides[VB_GPUDeformTemporaries] = CalculateVertexStrideForSlot(workingTemporarySpaceElements_gpu, VB_GPUDeformTemporaries);
				vbOffsets[VB_GPUDeformTemporaries] = bufferIterators._bufferIterators[VB_GPUDeformTemporaries];
				vbSizes[VB_GPUDeformTemporaries] = vbStrides[VB_GPUDeformTemporaries] * vertexCount;
				bufferIterators._bufferIterators[VB_GPUDeformTemporaries] += vbStrides[VB_GPUDeformTemporaries] * vertexCount;
			}

			{
				vbStrides[VB_PostDeform] = CalculateVertexStrideForSlot(workingGeneratedElements, VB_PostDeform);
				vbOffsets[VB_PostDeform] = bufferIterators._bufferIterators[VB_PostDeform];
				vbSizes[VB_PostDeform] = vbStrides[VB_PostDeform] * vertexCount;
				bufferIterators._bufferIterators[VB_PostDeform] += vbStrides[VB_PostDeform] * vertexCount;
			}

			vbStrides[VB_GPUStaticData] = animatedElementsStride;
			vbOffsets[VB_GPUStaticData] = 0;

			// Configure suppressed elements
			resultRendererBinding._suppressedElements = workingSuppressedElements;
			resultRendererBinding._suppressedElements.reserve(resultRendererBinding._suppressedElements.size() + workingGeneratedElements.size());
			for (const auto&wge:workingGeneratedElements)
				resultRendererBinding._suppressedElements.push_back(Hash64(wge._semanticName) + wge._semanticIndex);		// (also suppress all elements generated by the final deform step, because they are effectively overriden)
			std::sort(resultRendererBinding._suppressedElements.begin(), resultRendererBinding._suppressedElements.end());
			resultRendererBinding._suppressedElements.erase(
				std::unique(resultRendererBinding._suppressedElements.begin(), resultRendererBinding._suppressedElements.end()),
				resultRendererBinding._suppressedElements.end());

			// build the resultDeformerBindings
			for (auto d=instantiations.begin(); d!=instantiations.end(); ++d) {
				if (!*d) continue;
				const auto&def = **d;
				unsigned dIdx = (unsigned)std::distance(instantiations.begin(), d);
				auto& binding = resultDeformerBindings[dIdx];
				binding._geoId = resultRendererBinding._geoId;
				static_assert(dimof(DeformerInputBinding::GeoBinding::_bufferStrides) == VB_Count);
				static_assert(dimof(DeformerInputBinding::GeoBinding::_bufferOffsets) == VB_Count);
				for (unsigned c=0; c<VB_Count; ++c) {
					binding._bufferStrides[c] = vbStrides[c];
					binding._bufferOffsets[c] = vbOffsets[c];
				}

				auto& workingTemporarySpaceElements = def._cpuDeformer ? workingTemporarySpaceElements_cpu : workingTemporarySpaceElements_gpu;
				auto& workingTemporarySpaceElements_firstSourceDeformer = def._cpuDeformer ? workingTemporarySpaceElements_cpu_firstSourceDeformer : workingTemporarySpaceElements_gpu_firstSourceDeformer;

				// input elements
				binding._inputElements.reserve(def._upstreamSourceElements.size());
				for (auto&e:def._upstreamSourceElements) {
					// this element must come from either animatedElementsInput or workingTemporarySpaceElements
					bool found = false;
					for (unsigned c=0; c<workingTemporarySpaceElements.size(); ++c)
						if (workingTemporarySpaceElements_firstSourceDeformer[c] < dIdx && workingTemporarySpaceElements[c]._semanticName == e._semantic && workingTemporarySpaceElements[c]._semanticIndex == e._semanticIndex) {
							found = true;
							binding._inputElements.push_back(workingTemporarySpaceElements[c]);
							break;
						}

					if (!found) {
						if (def._cpuDeformer) {
							auto q = std::find_if(
								workingSourceDataElements_cpu.begin(), workingSourceDataElements_cpu.end(),
								[e](const auto& wge) { return wge._semanticName == e._semantic && wge._semanticIndex == e._semanticIndex; });
							assert(q!=workingSourceDataElements_cpu.end());
							binding._inputElements.push_back(*q);
						} else {
							auto q = std::find_if(
								animatedElementsInput.begin(), animatedElementsInput.end(),
								[e](const auto& wge) { return wge._semanticName == e._semantic && wge._semanticIndex == e._semanticIndex; });
							assert(q!=animatedElementsInput.end());
							binding._inputElements.push_back(*q);
						}
					}
				}

				// output elements
				binding._outputElements.reserve(def._generatedElements.size());
				for (auto&e:def._generatedElements) {
					// this element must come from either generatedElements or workingTemporarySpaceElements
					bool found = false;
					for (unsigned c=0; c<workingGeneratedElements.size(); ++c)
						if (workingGeneratedElements_firstSourceDeformer[c] == dIdx && workingGeneratedElements[c]._semanticName == e._semantic && workingGeneratedElements[c]._semanticIndex == e._semanticIndex) {
							found = true;
							binding._outputElements.push_back(workingGeneratedElements[c]);
							break;
						}

					if (!found) {
						auto q = std::find_if(
							workingTemporarySpaceElements.begin(), workingTemporarySpaceElements.end(),
							[e](const auto& wge) { return wge._semanticName == e._semantic && wge._semanticIndex == e._semanticIndex; });
						assert(q!=workingTemporarySpaceElements.end());
						binding._outputElements.push_back(*q);
					}
				}
			}

			resultRendererBinding._generatedElements = std::move(workingGeneratedElements);
			resultRendererBinding._postDeformBufferOffset = vbOffsets[VB_PostDeform];
		}

		DeformerToRendererBinding CreateDeformBindings(
			IteratorRange<WorkingDeformer*> workingDeformers,
			DeformBufferIterators& bufferIterators,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
			const std::string& modelScaffoldName)
		{
			DeformerToRendererBinding rendererBindingResult;

			struct GeoInput
			{
				const RenderCore::Assets::VertexData* _vbData = nullptr;
			};
			auto geoInputCount = modelScaffold->ImmutableData()._geoCount + modelScaffold->ImmutableData()._boundSkinnedControllerCount;
			GeoInput geoInputs[geoInputCount];
			
			{
				GeoInput* geoInput = geoInputs;
				for (unsigned geo=0; geo<modelScaffold->ImmutableData()._geoCount; ++geo) {
					const auto& rg = modelScaffold->ImmutableData()._geos[geo];
					geoInput->_vbData = &rg._vb;
					++geoInput;
				}

				for (unsigned geo=0; geo<modelScaffold->ImmutableData()._boundSkinnedControllerCount; ++geo) {
					const auto& rg = modelScaffold->ImmutableData()._boundSkinnedControllers[geo];
					geoInput->_vbData = &rg._animatedVertexElements;
					++geoInput;
				}
			}

			for (unsigned geoId=0; geoId<geoInputCount; ++geoId) {
				const auto& geo = geoInputs[geoId];

				const DeformOperationInstantiation* instantiations[workingDeformers.size()];
				DeformerInputBinding::GeoBinding deformerInputBindings[workingDeformers.size()];
				bool atLeastOneInstantiation = false;
				for (unsigned d=0; d<workingDeformers.size(); ++d) {
					instantiations[d] = nullptr;
					for (const auto&i:workingDeformers[d]._instantiations)
						if (i._geoId == geoId) {
							instantiations[d] = &i;
							atLeastOneInstantiation = true;
							break;
						}
				}

				if (!atLeastOneInstantiation) continue;

				auto vertexCount = geo._vbData->_size / geo._vbData->_ia._vertexStride;
				auto animatedElementsStride = geo._vbData->_ia._vertexStride;
				InputElementDesc animatedElements[geo._vbData->_ia._elements.size()];
				BuildLowLevelInputAssembly(
					MakeIteratorRange(animatedElements, &animatedElements[geo._vbData->_ia._elements.size()]),
					geo._vbData->_ia._elements);
				
				bool requiresGPUStaticDataLoad = false;
				DeformerToRendererBinding::GeoBinding rendererBinding;
				rendererBinding._geoId = geoId;
				LinkDeformers(
					MakeIteratorRange(animatedElements, &animatedElements[geo._vbData->_ia._elements.size()]),
					vertexCount, animatedElementsStride,
					MakeIteratorRange(instantiations, &instantiations[workingDeformers.size()]),
					MakeIteratorRange(deformerInputBindings, &deformerInputBindings[workingDeformers.size()]),
					rendererBinding,
					bufferIterators,
					requiresGPUStaticDataLoad);

				rendererBindingResult._geoBindings.push_back(std::move(rendererBinding));

				for (unsigned d=0; d<workingDeformers.size(); ++d)
					if (instantiations[d])
						workingDeformers[d]._inputBinding._geoBindings.push_back(deformerInputBindings[d]);

				if (requiresGPUStaticDataLoad) {
					bufferIterators._gpuStaticDataLoadRequests.push_back(std::make_pair(geo._vbData->_offset, geo._vbData->_size));
					bufferIterators._bufferIterators[VB_GPUStaticData] += geo._vbData->_size;
				}
			}

			return rendererBindingResult;
		}

#if 0
		NascentDeformForGeo BuildNascentDeformForGeo(
			IteratorRange<const DeformOperationInstantiation*> globalDeformAttachments,
			InputLayout srcVBLayout, unsigned srcVBStride,
			unsigned geoId,
			unsigned vertexCount,
			unsigned& cpuStaticDataIterator,
			unsigned& deformTemporaryGPUVBIterator,
			unsigned& deformTemporaryCPUVBIterator,
			unsigned& postDeformVBIterator)
		{
			// Calculate which elements are suppressed by the deform operations
			std::vector<const DeformOperationInstantiation*> deformAttachments;
			for (const auto& def:globalDeformAttachments)
				if (def._geoId == geoId)
					deformAttachments.push_back(&def);
			if (!deformAttachments.size()) return {};

			std::vector<uint64_t> workingSuppressedElements;
			std::vector<InputElementDesc> workingGeneratedElements;

			std::vector<InputElementDesc> workingTemporarySpaceElements_cpu;
			std::vector<InputElementDesc> workingTemporarySpaceElements_gpu;
			std::vector<InputElementDesc> workingSourceDataElements_cpu;

			struct WorkingCPUDeformOp
			{
				std::shared_ptr<ICPUDeformOperator> _deformOp;
				std::vector<DeformOperationInstantiation::SemanticNameAndFormat> _inputStreamIds;
				std::vector<DeformOperationInstantiation::SemanticNameAndFormat> _outputStreamIds;
			};
			std::vector<WorkingCPUDeformOp> workingCPUDeformOps;

			struct WorkingGPUDeformOp
			{
				DeformOperationInstantiation::GPUDeformConstructorFN _constructor;
			};
			std::vector<WorkingGPUDeformOp> workingGPUDeformOps;

			for (auto d=deformAttachments.begin(); d!=deformAttachments.end(); ++d) {
				const auto&def = **d;
				assert((def._cpuOperator != nullptr) ^ (def._gpuConstructor != nullptr));		// we need either CPU or GPU style operator, but not both

				if (def._cpuOperator) {
					/////////////////// CPU type operator ///////////////////
					WorkingCPUDeformOp workingDeformOp;

					for (auto&e:def._upstreamSourceElements) {
						// find a matching source element generated from another deform op
						// (note that CPU operations can only take inputs from other CPU deforms)
						auto i = std::find_if(
							workingGeneratedElements.begin(), workingGeneratedElements.end(),
							[e](const auto& wge) {
								return wge._semanticName == e._semantic && wge._semanticIndex == e._semanticIndex;
							});
						if (i != workingGeneratedElements.end()) {
							workingTemporarySpaceElements_cpu.push_back(*i);
							workingGeneratedElements.erase(i);
						} else {
							// If it's not generated by some deform op, we look for it in the static data
							auto existing = std::find_if(workingSourceDataElements_cpu.begin(), workingSourceDataElements_cpu.end(), 
								[&e](const auto& c) { return c._semanticName == e._semantic && c._semanticIndex == e._semanticIndex; });
							if (existing != workingSourceDataElements_cpu.end()) {
								assert(existing->_nativeFormat == e._format);		// avoid loading the same attribute twice with different formats
							} else
								workingSourceDataElements_cpu.push_back(InputElementDesc{e._semantic, e._semanticIndex, e._format, VB_CPUStaticData});
						}
					}

					// Before we add our own static data, we should remove any working elements that have been
					// suppressed
					auto i = std::remove_if(
						workingGeneratedElements.begin(), workingGeneratedElements.end(),
						[&def](const auto& wge) {
							auto hash = Hash64(wge._semanticName) + wge._semanticIndex;
							return std::find(def._suppressElements.begin(), def._suppressElements.end(), hash) != def._suppressElements.end();
						});
					workingGeneratedElements.erase(i, workingGeneratedElements.end());		// these get removed and don't go into temporary space. They are just never used

					for (const auto& e:def._generatedElements) {
						auto existing = std::find_if(workingGeneratedElements.begin(), workingGeneratedElements.end(), 
							[&e](const auto& c) { return c._semanticName == e._semantic && c._semanticIndex == e._semanticIndex; });
						if (existing != workingGeneratedElements.end())
							workingGeneratedElements.erase(existing);
						workingGeneratedElements.push_back(InputElementDesc{e._semantic, e._semanticIndex, e._format, VB_PostDeform});
					}

					workingSuppressedElements.insert(
						workingSuppressedElements.end(),
						def._suppressElements.begin(), def._suppressElements.end());

					workingDeformOp._deformOp = std::move(def._cpuOperator);
					workingDeformOp._inputStreamIds = std::move(def._upstreamSourceElements);
					workingDeformOp._outputStreamIds = std::move(def._generatedElements);
					workingCPUDeformOps.push_back(workingDeformOp);
				} else {
					/////////////////// GPU type operator ///////////////////
					WorkingGPUDeformOp workingDeformOp;

					for (auto&e:def._upstreamSourceElements) {
						// find a matching source element generated from another deform op
						// (note that CPU operations can only take inputs from other CPU deforms)
						auto i = std::find_if(
							workingGeneratedElements.begin(), workingGeneratedElements.end(),
							[e](const auto& wge) {
								return wge._semanticName == e._semantic && wge._semanticIndex == e._semanticIndex;
							});
						if (i != workingGeneratedElements.end()) {
							workingTemporarySpaceElements_gpu.push_back(*i);
							workingGeneratedElements.erase(i);
						} else {
							auto q = std::find_if(
								srcVBLayout.begin(), srcVBLayout.end(),
								[e](const auto& wge) {
									return wge._semanticName == e._semantic && wge._semanticIndex == e._semanticIndex;
								});
							if (q==srcVBLayout.end())
								Throw(std::runtime_error("Could not match input element (" + e._semantic + ") for GPU deform operation"));
						}
					}

					// Before we add our own static data, we should remove any working elements that have been
					// suppressed
					auto i = std::remove_if(
						workingGeneratedElements.begin(), workingGeneratedElements.end(),
						[&def](const auto& wge) {
							auto hash = Hash64(wge._semanticName) + wge._semanticIndex;
							return std::find(def._suppressElements.begin(), def._suppressElements.end(), hash) != def._suppressElements.end();
						});
					workingGeneratedElements.erase(i, workingGeneratedElements.end());		// these get removed and don't go into temporary space. They are just never used

					for (const auto& e:def._generatedElements) {
						auto existing = std::find_if(workingGeneratedElements.begin(), workingGeneratedElements.end(), 
							[&e](const auto& c) { return c._semanticName == e._semantic && c._semanticIndex == e._semanticIndex; });
						if (existing != workingGeneratedElements.end())
							workingGeneratedElements.erase(existing);
						workingGeneratedElements.push_back(InputElementDesc{e._semantic, e._semanticIndex, e._format, VB_PostDeform});
					}

					workingSuppressedElements.insert(
						workingSuppressedElements.end(),
						def._suppressElements.begin(), def._suppressElements.end());

					workingDeformOp._constructor = std::move(def._gpuConstructor);
					workingGPUDeformOps.push_back(workingDeformOp);
				}
			}

			NascentDeformForGeo result;
			result._rendererInterf._suppressedElements = workingSuppressedElements;
			result._rendererInterf._suppressedElements.reserve(result._rendererInterf._suppressedElements.size() + workingGeneratedElements.size());
			for (const auto&wge:workingGeneratedElements)
				result._rendererInterf._suppressedElements.push_back(Hash64(wge._semanticName) + wge._semanticIndex);		// (also suppress all elements generated by the final deform step, because they are effectively overriden)
			std::sort(result._rendererInterf._suppressedElements.begin(), result._rendererInterf._suppressedElements.end());
			result._rendererInterf._suppressedElements.erase(
				std::unique(result._rendererInterf._suppressedElements.begin(), result._rendererInterf._suppressedElements.end()),
				result._rendererInterf._suppressedElements.end());

			workingGeneratedElements = NormalizeInputAssembly(workingGeneratedElements);
			workingTemporarySpaceElements_cpu = NormalizeInputAssembly(workingTemporarySpaceElements_cpu);
			workingTemporarySpaceElements_gpu = NormalizeInputAssembly(workingTemporarySpaceElements_gpu);
			workingSourceDataElements_cpu = NormalizeInputAssembly(workingSourceDataElements_cpu);

			for (auto&e:workingTemporarySpaceElements_cpu) e._inputSlot = VB_CPUDeformTemporaries;
			for (auto&e:workingTemporarySpaceElements_gpu) e._inputSlot = VB_GPUDeformTemporaries;
			for (auto&e:workingGeneratedElements) e._inputSlot = VB_PostDeform;
			for (auto&e:workingSourceDataElements_cpu) e._inputSlot = VB_CPUStaticData;

			// Figure out how to arrange all of the input and output vertices in the 
			// deform VBs.
			// We've got 3 to use
			//		1. an input static data buffer; which contains values read directly from the source data (perhaps processed for format)
			//		2. a deform temporary buffer; which contains data written out from deform operations, and read in by others
			//		3. a final output buffer; which contains resulting vertex data that is fed into the render operation
			
			unsigned vbStrides[VB_Count] = {0};
			{
				vbStrides[VB_CPUStaticData] = CalculateVertexStrideForSlot(workingSourceDataElements_cpu, VB_CPUStaticData);
				result._vbOffsets[VB_CPUStaticData] = cpuStaticDataIterator;
				result._vbSizes[VB_CPUStaticData] = vbStrides[VB_CPUStaticData] * vertexCount;
				cpuStaticDataIterator += vbStrides[VB_CPUStaticData] * vertexCount;

				result._cpuStaticDataLoadRequests.reserve(workingSourceDataElements_cpu.size());
				for (unsigned c=0; c<workingSourceDataElements_cpu.size(); ++c) {
					const auto& workingE = workingSourceDataElements_cpu[c];
					result._cpuStaticDataLoadRequests.push_back({
						geoId, Hash64(workingE._semanticName) + workingE._semanticIndex,
						workingE._nativeFormat, workingE._alignedByteOffset + result._vbOffsets[VB_CPUStaticData],
						vbStrides[VB_CPUStaticData], vertexCount});
				}
			}

			{
				vbStrides[VB_CPUDeformTemporaries] = CalculateVertexStrideForSlot(workingTemporarySpaceElements_cpu, VB_CPUDeformTemporaries);
				result._vbOffsets[VB_CPUDeformTemporaries] = deformTemporaryCPUVBIterator;
				result._vbSizes[VB_CPUDeformTemporaries] = vbStrides[VB_CPUDeformTemporaries] * vertexCount;
				deformTemporaryCPUVBIterator += vbStrides[VB_CPUDeformTemporaries] * vertexCount;
			}

			{
				vbStrides[VB_GPUDeformTemporaries] = CalculateVertexStrideForSlot(workingTemporarySpaceElements_gpu, VB_GPUDeformTemporaries);
				result._vbOffsets[VB_GPUDeformTemporaries] = deformTemporaryGPUVBIterator;
				result._vbSizes[VB_GPUDeformTemporaries] = vbStrides[VB_GPUDeformTemporaries] * vertexCount;
				deformTemporaryGPUVBIterator += vbStrides[VB_GPUDeformTemporaries] * vertexCount;
			}

			{
				vbStrides[VB_PostDeform] = CalculateVertexStrideForSlot(workingGeneratedElements, VB_PostDeform);
				result._vbOffsets[VB_PostDeform] = postDeformVBIterator;
				result._vbSizes[VB_PostDeform] = vbStrides[VB_PostDeform] * vertexCount;
				result._rendererInterf._postDeformBufferOffset = postDeformVBIterator;
				postDeformVBIterator += vbStrides[VB_PostDeform] * vertexCount;
			}

			// Collate the WorkingDeformOp into the SimpleModelRenderer::DeformOp format
			result._cpuOps.reserve(workingCPUDeformOps.size());
			for (const auto&wdo:workingCPUDeformOps) {
				NascentDeformForGeo::CPUOp finalDeformOp;
				// input streams
				for (auto s:wdo._inputStreamIds) {
					auto i = std::find_if(workingTemporarySpaceElements_cpu.begin(), workingTemporarySpaceElements_cpu.end(), [s](const auto& p) { return p._semanticName == s._semantic && p._semanticIndex == s._semanticIndex; });
					if (i != workingTemporarySpaceElements_cpu.end()) {
						finalDeformOp._inputElements.push_back(AsCPUOpAttribute(*i, result._vbOffsets[VB_CPUDeformTemporaries], vbStrides[VB_CPUDeformTemporaries], VB_CPUDeformTemporaries));
					} else {
						i = std::find_if(workingSourceDataElements_cpu.begin(), workingSourceDataElements_cpu.end(), [s](const auto& p) { return p._semanticName == s._semantic && p._semanticIndex == s._semanticIndex; });
						if (i != workingSourceDataElements_cpu.end()) {
							finalDeformOp._inputElements.push_back(AsCPUOpAttribute(*i, result._vbOffsets[VB_CPUStaticData], vbStrides[VB_CPUStaticData], VB_CPUStaticData));
						} else {
							assert(0);
							finalDeformOp._inputElements.push_back({});
						}
					}
				}
				// output streams
				for (auto s:wdo._outputStreamIds) {
					auto i = std::find_if(workingGeneratedElements.begin(), workingGeneratedElements.end(), [s](const auto& p) { return p._semanticName == s._semantic && p._semanticIndex == s._semanticIndex; });
					if (i != workingGeneratedElements.end()) {
						finalDeformOp._outputElements.push_back(AsCPUOpAttribute(*i, result._vbOffsets[VB_PostDeform], vbStrides[VB_PostDeform], VB_PostDeform));
					} else {
						i = std::find_if(workingTemporarySpaceElements_cpu.begin(), workingTemporarySpaceElements_cpu.end(), [s](const auto& p) { return p._semanticName == s._semantic && p._semanticIndex == s._semanticIndex; });
						if (i != workingTemporarySpaceElements_cpu.end()) {
							finalDeformOp._outputElements.push_back(AsCPUOpAttribute(*i, result._vbOffsets[VB_CPUDeformTemporaries], vbStrides[VB_CPUDeformTemporaries], VB_CPUDeformTemporaries));
						} else {
							assert(0);
							finalDeformOp._outputElements.push_back({});
						}
					}
				}
				finalDeformOp._deformOp = wdo._deformOp;
				result._cpuOps.emplace_back(std::move(finalDeformOp));
			}

			result._gpuOps.reserve(workingGPUDeformOps.size());
			for (const auto&wdo:workingGPUDeformOps) {
				std::promise<std::shared_ptr<IGPUDeformOperator>> promise;
				auto future = promise.get_future();
				DeformOperationInstantiation::GPUConstructorParameters params;
				params._srcVBLayout = srcVBLayout;
				params._deformTemporariesVBLayout = workingTemporarySpaceElements_gpu;
				params._dstVBLayout = workingGeneratedElements;
				params._srcVBStride = srcVBStride;
				params._deformTemporariesStride = vbStrides[VB_GPUDeformTemporaries];
				params._dstVBStride = vbStrides[VB_PostDeform];
				wdo._constructor(std::move(promise), std::move(params));
				result._gpuOps.push_back(std::move(future));
			}

			result._rendererInterf._generatedElements = std::move(workingGeneratedElements);
			return result;
		}
#endif

		static void ReadStaticData(
			IteratorRange<void*> destinationVB,
			IteratorRange<void*> sourceVB,
			const SourceDataTransform& transform,
			const RenderCore::Assets::VertexElement& srcElement,
			unsigned srcStride)
		{
			assert(destinationVB.size() >= transform._targetStride * transform._vertexCount);
			assert(sourceVB.size() >= srcStride * transform._vertexCount);
			auto dstRange = AsVertexElementIteratorRange(destinationVB, transform._targetFormat, transform._targetOffset, transform._targetStride);
			auto srcRange = AsVertexElementIteratorRange(sourceVB, srcElement._nativeFormat, srcElement._alignedByteOffset, srcStride);
			auto dstCount = dstRange.size();
			auto srcCount = srcRange.size();
			(void)dstCount; (void)srcCount;
			Assets::GeoProc::Copy(dstRange, srcRange, transform._vertexCount);
		}

		static std::vector<uint8_t> GenerateDeformStaticInputForCPUDeform(
			const RenderCore::Assets::ModelScaffold& modelScaffold,
			IteratorRange<const SourceDataTransform*> inputLoadRequests,
			unsigned destinationBufferSize)
		{
			if (inputLoadRequests.empty())
				return {};

			std::vector<uint8_t> result;
			result.resize(destinationBufferSize, 0);

			std::vector<SourceDataTransform> loadRequests { inputLoadRequests.begin(), inputLoadRequests.end() };
			std::stable_sort(
				loadRequests.begin(), loadRequests.end(),
				[](const SourceDataTransform& lhs, const SourceDataTransform& rhs) {
					return lhs._geoId < rhs._geoId;
				});

			auto largeBlocks = modelScaffold.OpenLargeBlocks();
			auto base = largeBlocks->TellP();

			auto& immData = modelScaffold.ImmutableData();
			for (auto i=loadRequests.begin(); i!=loadRequests.end();) {

				auto start = i;
				while (i!=loadRequests.end() && i->_geoId == start->_geoId) ++i;
				auto end = i;

				if (start->_geoId < immData._geoCount) {
					auto& geo = immData._geos[start->_geoId];
					auto& vb = geo._vb;

					auto vbData = std::make_unique<uint8_t[]>(vb._size);
					largeBlocks->Seek(base + vb._offset);
					largeBlocks->Read(vbData.get(), vb._size);

					for (auto r=start; r!=end; ++r) {
						auto sourceEle = FindElement(MakeIteratorRange(vb._ia._elements), r->_sourceStream);
						if (sourceEle != vb._ia._elements.end()) {
							ReadStaticData(MakeIteratorRange(result), MakeIteratorRange(vbData.get(), PtrAdd(vbData.get(), vb._size)), *r, *sourceEle, vb._ia._vertexStride);
						} else
							Throw(std::runtime_error("Could not initialize deform input element"));
					}

				} else {
					auto& geo = immData._boundSkinnedControllers[start->_geoId - immData._geoCount];

					std::unique_ptr<uint8_t[]> baseVB;
					std::unique_ptr<uint8_t[]> animVB;
					std::unique_ptr<uint8_t[]> skelBindVB;

					for (auto r=start; r!=end; ++r) {
						auto sourceEle = FindElement(MakeIteratorRange(geo._vb._ia._elements), r->_sourceStream);

						if (sourceEle != geo._vb._ia._elements.end()) {
							if (!baseVB.get()) {
								baseVB = std::make_unique<uint8_t[]>(geo._vb._size);
								largeBlocks->Seek(base + geo._vb._offset);
								largeBlocks->Read(baseVB.get(), geo._vb._size);
							}
							ReadStaticData(MakeIteratorRange(result), MakeIteratorRange(baseVB.get(), PtrAdd(baseVB.get(), geo._vb._size)), *r, *sourceEle, geo._animatedVertexElements._ia._vertexStride);
						} else {
							sourceEle = FindElement(MakeIteratorRange(geo._animatedVertexElements._ia._elements), r->_sourceStream);
							if (sourceEle != geo._animatedVertexElements._ia._elements.end()) {
								if (!animVB.get()) {
									animVB = std::make_unique<uint8_t[]>(geo._animatedVertexElements._size);
									largeBlocks->Seek(base + geo._animatedVertexElements._offset);
									largeBlocks->Read(animVB.get(), geo._animatedVertexElements._size);
								}
								ReadStaticData(MakeIteratorRange(result), MakeIteratorRange(animVB.get(), PtrAdd(animVB.get(), geo._animatedVertexElements._size)), *r, *sourceEle, geo._animatedVertexElements._ia._vertexStride);
							} else {
								sourceEle = FindElement(MakeIteratorRange(geo._skeletonBinding._ia._elements), r->_sourceStream);
								if (sourceEle != geo._skeletonBinding._ia._elements.end()) {
									if (!skelBindVB.get()) {
										skelBindVB = std::make_unique<uint8_t[]>(geo._skeletonBinding._size);
										largeBlocks->Seek(base + geo._skeletonBinding._offset);
										largeBlocks->Read(skelBindVB.get(), geo._skeletonBinding._size);
									}
									ReadStaticData(MakeIteratorRange(result), MakeIteratorRange(skelBindVB.get(), PtrAdd(skelBindVB.get(), geo._skeletonBinding._size)), *r, *sourceEle, geo._skeletonBinding._ia._vertexStride);
								} else
									Throw(std::runtime_error("Could not initialize deform input element"));
							}
						}
					}
				}
			}

			return result;
		}
	}
}}
