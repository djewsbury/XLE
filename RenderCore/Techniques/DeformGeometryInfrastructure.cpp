// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformGeometryInfrastructure.h"
#include "DeformGeoInternal.h"
#include "DeformerConstruction.h"
#include "Services.h"
#include "CommonUtils.h"
#include "CommonResources.h"
#include "ModelRendererConstruction.h"
#include "../IDevice.h"
#include "../Assets/ModelScaffold.h"
#include "../GeoProc/MeshDatabase.h"        // for GeoProc::Copy
#include "../BufferUploads/IBufferUploads.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Utility/StringFormat.h"

namespace RenderCore { namespace Techniques
{
	namespace Internal
	{
		static std::vector<uint8_t> GenerateDeformStaticInputForCPUDeform(
			IteratorRange<const SourceDataTransform*> inputLoadRequests,
			unsigned destinationBufferSize);
	}

	class DeformGeoInfrastructure : public IDeformGeoAttachment
	{
	public:
		std::vector<std::shared_ptr<IGeoDeformer>> _deformOps;
		DeformerToRendererBinding _rendererGeoInterface;

		std::vector<uint8_t> _deformStaticDataInput;
		std::vector<uint8_t> _deformTemporaryBuffer;

		std::shared_ptr<IResource> _gpuStaticDataBuffer, _gpuTemporariesBuffer;
		std::shared_ptr<IResourceView> _gpuStaticDataBufferView, _gpuTemporariesBufferView;
		BufferUploads::CommandListID _gpuStaticDataCompletionList;
		std::shared_future<void> _initializationFuture;

		bool _isCPUDeformer = false;
		unsigned _outputVBSize = 0;

		void ReserveBytesRequired(
			unsigned instanceCount,
			unsigned& gpuBufferBytes,
			unsigned& cpuBufferBytes) override
		{
			cpuBufferBytes += _isCPUDeformer ? _outputVBSize * instanceCount : 0;
			gpuBufferBytes += _isCPUDeformer ? 0 : _outputVBSize * instanceCount;
		}

		void Execute(
			IThreadContext& threadContext, 
			IteratorRange<const unsigned*> instanceIdx, 
			IResourceView& dstVB,
			IteratorRange<void*> cpuBufferOutputRange,
			IDeformAcceleratorPool::ReadyInstancesMetrics& metrics) override
		{
			if (_isCPUDeformer) {
				auto staticDataPartRange = MakeIteratorRange(_deformStaticDataInput);
				auto temporaryDeformRange = MakeIteratorRange(_deformTemporaryBuffer);
				for (const auto&d:_deformOps)
					d->ExecuteCPU(instanceIdx, _outputVBSize, staticDataPartRange, temporaryDeformRange, cpuBufferOutputRange);
			} else {
				for (const auto&d:_deformOps) {
					IGeoDeformer::Metrics deformerMetrics;
					d->ExecuteGPU(
						threadContext,
						instanceIdx, _outputVBSize,
						*_gpuStaticDataBufferView,
						*_gpuTemporariesBufferView,
						dstVB, deformerMetrics);
					metrics._dispatchCount += deformerMetrics._dispatchCount;
					metrics._vertexCount += deformerMetrics._vertexCount;
					metrics._descriptorSetWrites += deformerMetrics._descriptorSetWrites;
					metrics._constantDataSize += deformerMetrics._constantDataSize;
					metrics._inputStaticDataSize += deformerMetrics._inputStaticDataSize;
				}
				metrics._deformersReadied += _deformOps.size();
			}
		}

		std::vector<std::shared_ptr<IGeoDeformer>> GetOperations(size_t typeId) override
		{
			std::vector<std::shared_ptr<IGeoDeformer>> result;
			for (const auto&i:_deformOps)
				if (i->QueryInterface(typeId))
					result.push_back(i);
			return result;
		}

		virtual const DeformerToRendererBinding& GetDeformerToRendererBinding() const override
		{
			return _rendererGeoInterface;
		}

		virtual BufferUploads::CommandListID GetCompletionCommandList() const override
		{
			// we must have waited on the initialization future before doing this
			assert(_initializationFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
			return _gpuStaticDataCompletionList;
		}

		virtual std::shared_future<void> GetInitializationFuture() const override
		{
			return _initializationFuture;
		}
	};

	std::shared_ptr<IDeformGeoAttachment> CreateDeformGeoAttachment(
		IDevice& device,
		const ModelRendererConstruction& rendererConstruction,
		const DeformerConstruction& deformerConstruction)
	{
		auto result = std::make_shared<DeformGeoInfrastructure>();
		
		////////////////////////////////////////////////////////////////////////////////////
		// Build deform streams

		Internal::DeformBufferIterators bufferIterators;
		std::optional<bool> isCPUDeformer;

		auto constructionEntries = deformerConstruction.GetGeoEntries();
		std::stable_sort(
			constructionEntries.begin(), constructionEntries.end(),
			[](const auto& lhs, const auto& rhs) { 
				if (lhs._geoIdx < rhs._geoIdx) return true;
				if (lhs._geoIdx > rhs._geoIdx) return false;
				if (lhs._elementIdx < rhs._elementIdx) return true;
				if (lhs._elementIdx > rhs._elementIdx) return false;
				return false;	// no preferential ordering from here
			});

		struct PendingDeformerBind
		{
			std::shared_ptr<IGeoDeformer> _deformer;
			DeformerInputBinding::GeoBinding _binding;
			unsigned _elementIdx, _geoIdx;
		};
		std::vector<PendingDeformerBind> pendingDeformerBinds;

		std::set<std::string> uniqueScaffoldNames;

		for (auto i=constructionEntries.begin(); i!=constructionEntries.end();) {
			auto start = i;
			++i;
			while (i!=constructionEntries.end() && i->_elementIdx == start->_elementIdx && i->_geoIdx == start->_geoIdx) ++i;

			if (!isCPUDeformer.has_value()) isCPUDeformer = start->_deformer->IsCPUDeformer();
			
			// for all of the instantiations of the same deformer, of the same element, of the same geo, call Internal::CreateDeformBindings
			
			std::vector<DeformOperationInstantiation> instantiations;
			std::vector<DeformerInputBinding::GeoBinding> thisGeoDeformerBindings;
			instantiations.reserve(i-start);
			thisGeoDeformerBindings.resize(i-start);

			for (auto& d:MakeIteratorRange(start, i)) {
				instantiations.push_back(*d._instantiation);
				if (d._deformer->IsCPUDeformer() != isCPUDeformer.value())
					Throw(std::runtime_error("Attempting to mix CPU and GPU deformers. This isn't supported; deformations must be all CPU or all GPU"));
			};

			auto element = rendererConstruction.GetElement(start->_elementIdx);
			auto rendererBinding = Internal::CreateDeformBindings(
				MakeIteratorRange(thisGeoDeformerBindings),
				MakeIteratorRange(instantiations),
				bufferIterators, isCPUDeformer.value(),
				start->_geoIdx,
				element->GetModelScaffold());
			uniqueScaffoldNames.insert(element->GetModelScaffoldName());

			result->_rendererGeoInterface._geoBindings.emplace_back(std::make_pair(start->_elementIdx, start->_geoIdx), std::move(rendererBinding));

			// Queue a pending call to IGeoDeformer::Bind
			for(unsigned c=0; c<(i-start); ++c)
				pendingDeformerBinds.push_back({start[c]._deformer, std::move(thisGeoDeformerBindings[c]), start->_elementIdx, start->_geoIdx});
		}

		if (!isCPUDeformer.has_value()) return nullptr;	// nothing actually instantiated
		result->_isCPUDeformer = isCPUDeformer.value();

		// Call call Bind on all deformers, for everything calculated in CreateDeformBindings
		std::vector<std::future<void>> deformerInitFutures;
		deformerInitFutures.reserve(pendingDeformerBinds.size());
		std::sort(
			pendingDeformerBinds.begin(), pendingDeformerBinds.end(),
			[](const auto& lhs, const auto& rhs) { return lhs._deformer < rhs._deformer; });
		for (auto i=pendingDeformerBinds.begin(); i!=pendingDeformerBinds.end();) {
			auto start = i;
			++i;
			while (i!=pendingDeformerBinds.end() && i->_deformer == start->_deformer) ++i;

			DeformerInputBinding inputBinding;
			inputBinding._geoBindings.reserve(i-start);
			for (auto& c:MakeIteratorRange(start, i))
				inputBinding._geoBindings.emplace_back(std::make_pair(c._elementIdx, c._geoIdx), std::move(c._binding));

			start->_deformer->Bind(inputBinding);
			deformerInitFutures.emplace_back(start->_deformer->GetInitializationFuture());
			result->_deformOps.push_back(std::move(start->_deformer));
		}

		////////////////////////////////////////////////////////////////////////////////////

		for (auto i=uniqueScaffoldNames.begin(); i!=uniqueScaffoldNames.end(); ++i)
			if (i->empty()) { uniqueScaffoldNames.erase(i); break; }

		std::shared_future<BufferUploads::CommandListID> gpuStaticDataCompletionListFuture;
		if (!bufferIterators._gpuStaticDataLoadRequests.empty()) {
			StringMeld<64> bufferName;
			bufferName << "[deform]";
			if (uniqueScaffoldNames.size() == 1) bufferName << *uniqueScaffoldNames.begin();		// could be coming from multiple scaffolds

			BufferUploads::TransactionMarker transactionMarker; 
			std::tie(result->_gpuStaticDataBuffer, transactionMarker) = LoadStaticResourcePartialAsync(
				device,
				{bufferIterators._gpuStaticDataLoadRequests.begin(), bufferIterators._gpuStaticDataLoadRequests.end()}, 
				bufferIterators._bufferIterators[Internal::VB_GPUStaticData],
				BindFlag::UnorderedAccess, bufferName.AsStringSection());
			result->_gpuStaticDataBufferView = result->_gpuStaticDataBuffer->CreateBufferView(BindFlag::UnorderedAccess);

			std::promise<BufferUploads::CommandListID> promise;
			gpuStaticDataCompletionListFuture = promise.get_future();
			::Assets::WhenAll(std::move(transactionMarker._future)).ThenConstructToPromise(
				std::move(promise), [](const auto& locator) { return locator.GetCompletionCommandList(); });
		} else {
			result->_gpuStaticDataBufferView = Techniques::Services::GetCommonResources()->_blackBufferUAV;
		}

		if (bufferIterators._bufferIterators[Internal::VB_GPUDeformTemporaries]) {
			StringMeld<64> bufferName;
			bufferName << "[deform-t]";
			if (uniqueScaffoldNames.size() == 1) bufferName << *uniqueScaffoldNames.begin();		// could be coming from multiple scaffolds

			result->_gpuTemporariesBuffer = device.CreateResource(
				RenderCore::CreateDesc(
					BindFlag::UnorderedAccess,
					LinearBufferDesc::Create(bufferIterators._bufferIterators[Internal::VB_GPUDeformTemporaries]),
					bufferName.AsStringSection()));
			result->_gpuTemporariesBufferView = result->_gpuTemporariesBuffer->CreateBufferView(BindFlag::UnorderedAccess);
		} else {
			result->_gpuTemporariesBufferView = Techniques::Services::GetCommonResources()->_blackBufferUAV;
		}

		////////////////////////////////////////////////////////////////////////////////////

		// Create the dynamic VB and assign it to all of the slots it needs to go to
		result->_outputVBSize = bufferIterators._bufferIterators[Internal::VB_PostDeform];

		if (!bufferIterators._cpuStaticDataLoadRequests.empty()) {
			result->_deformStaticDataInput = Internal::GenerateDeformStaticInputForCPUDeform(
				MakeIteratorRange(bufferIterators._cpuStaticDataLoadRequests),
				bufferIterators._bufferIterators[Internal::VB_CPUStaticData]);
		}

		if (bufferIterators._bufferIterators[Internal::VB_CPUDeformTemporaries]) {
			result->_deformTemporaryBuffer.resize(bufferIterators._bufferIterators[Internal::VB_CPUDeformTemporaries], 0);
		}

		// Unfortunately a bit of synchronization to finish off here. Can't complete until
		//	1. all deformers have their pipelines completed
		//	2. buffer uploads has given us a completion command list id for the geometry upload
		std::promise<void> initializationPromise;
		result->_initializationFuture = initializationPromise.get_future();
		::Assets::PollToPromise(
			std::move(initializationPromise),
			[deformerInitFutures=std::move(deformerInitFutures), gpuStaticDataCompletionListFuture](auto timeout) {
				// complete when all deformers are completed, and when the gpu command list future is serviced
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				for (auto& d:deformerInitFutures)
					if (d.wait_until(timeoutTime) == std::future_status::timeout)
						return ::Assets::PollStatus::Continue;
				if (gpuStaticDataCompletionListFuture.valid())
					if (gpuStaticDataCompletionListFuture.wait_until(timeoutTime) == std::future_status::timeout)
						return ::Assets::PollStatus::Continue;
				return ::Assets::PollStatus::Finish;
			},
			[weakResult=std::weak_ptr<DeformGeoInfrastructure>{result}, gpuStaticDataCompletionListFuture]() {
				// fill in _gpuStaticDataCompletionList, now the future is complete
				auto l = weakResult.lock();
				if (l && gpuStaticDataCompletionListFuture.valid())
					l->_gpuStaticDataCompletionList = gpuStaticDataCompletionListFuture.get();
			});

		return result;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		static void LinkDeformers(
			/* in */ IteratorRange<const InputElementDesc*> animatedElementsInput,
			/* in */ unsigned vertexCount,
			/* in */ unsigned animatedElementsStride,
			/* in */ bool isCPUDeformer,
			/* in */ unsigned geoIdx,
			/* in */ const RenderCore::Assets::ModelScaffold& modelScaffold,
			/* in */ IteratorRange<const DeformOperationInstantiation*> instantiations,
			/* out */ IteratorRange<DeformerInputBinding::GeoBinding*> resultDeformerBindings,
			/* out */ DeformerToRendererBinding::GeoBinding& resultRendererBinding,
			/* in/out */ DeformBufferIterators& bufferIterators,
			/* out */ bool& gpuStaticDataLoadRequired)
		{
			// Given some input vertex format plus one or more deformer instantiations, calculate how we should
			// link together these deformers, and what vertex format should eventually be expected by the renderer
			// At this point, we're operating on a single "geo" object
			std::vector<uint64_t> workingSuppressedElements;
			std::vector<InputElementDesc> workingGeneratedElements;

			std::vector<InputElementDesc> workingTemporarySpaceElements_cpu;
			std::vector<InputElementDesc> workingTemporarySpaceElements_gpu;
			std::vector<InputElementDesc> workingSourceDataElements_cpu;
			
			for (auto d=instantiations.begin(); d!=instantiations.end(); ++d) {
				const auto&def = *d;
				unsigned dIdx = (unsigned)std::distance(instantiations.begin(), d);
				
				auto& workingTemporarySpaceElements = isCPUDeformer ? workingTemporarySpaceElements_cpu : workingTemporarySpaceElements_gpu;
				
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
						if (isCPUDeformer) {
							// If it's not generated by some deform op, we look for it in the static data
							auto existing = std::find_if(workingSourceDataElements_cpu.begin(), workingSourceDataElements_cpu.end(), 
								[&e](const auto& c) { return c._semanticName == e._semantic && c._semanticIndex == e._semanticIndex; });
							if (existing != workingSourceDataElements_cpu.end()) {
								assert(existing->_nativeFormat == e._format);		// avoid loading the same attribute twice with different formats
							} else {
								assert(e._format != Format::Unknown);
								workingSourceDataElements_cpu.push_back(InputElementDesc{e._semantic, e._semanticIndex, e._format});
							}
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
						&modelScaffold,
						geoIdx, Hash64(workingE._semanticName) + workingE._semanticIndex,
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
			vbOffsets[VB_GPUStaticData] = bufferIterators._bufferIterators[VB_GPUStaticData];

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
				const auto&def = *d;
				unsigned dIdx = (unsigned)std::distance(instantiations.begin(), d);
				auto& binding = resultDeformerBindings[dIdx];
				static_assert(dimof(DeformerInputBinding::GeoBinding::_bufferStrides) == VB_Count);
				static_assert(dimof(DeformerInputBinding::GeoBinding::_bufferOffsets) == VB_Count);
				for (unsigned c=0; c<VB_Count; ++c) {
					binding._bufferStrides[c] = vbStrides[c];
					binding._bufferOffsets[c] = vbOffsets[c];
				}

				auto& workingTemporarySpaceElements = isCPUDeformer ? workingTemporarySpaceElements_cpu : workingTemporarySpaceElements_gpu;
				auto& workingTemporarySpaceElements_firstSourceDeformer = isCPUDeformer ? workingTemporarySpaceElements_cpu_firstSourceDeformer : workingTemporarySpaceElements_gpu_firstSourceDeformer;

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
						if (isCPUDeformer) {
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
							auto ele = *q;
							ele._inputSlot = VB_GPUStaticData;
							binding._inputElements.push_back(ele);
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

		DeformerToRendererBinding::GeoBinding CreateDeformBindings(
			IteratorRange<DeformerInputBinding::GeoBinding*> resultDeformerBindings,
			IteratorRange<const DeformOperationInstantiation*> instantiations,
			DeformBufferIterators& bufferIterators,
			bool isCPUDeformer,
			unsigned geoIdx,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold)
		{
			assert(!instantiations.empty());
			assert(instantiations.size() == resultDeformerBindings.size());

			DeformerToRendererBinding::GeoBinding rendererBindingResult;

			auto geoMachine = modelScaffold->GetGeoMachine(geoIdx);
			const RenderCore::Assets::VertexData* vbData = nullptr;
			for (auto cmd:geoMachine) {
				switch (cmd.Cmd()) {
				case (uint32_t)Assets::GeoCommand::AttachRawGeometry:
					if (!vbData)	// always use the skinning input, if it exists
						vbData = &cmd.As<Assets::RawGeometryDesc>()._vb;
					break;
				case (uint32_t)Assets::GeoCommand::AttachSkinningData:
					vbData = &cmd.As<Assets::SkinningDataDesc>()._animatedVertexElements;
					break;
				}
			}
			assert(vbData);

			auto vertexCount = vbData->_size / vbData->_ia._vertexStride;
			auto animatedElementsStride = vbData->_ia._vertexStride;
			InputElementDesc animatedElements[vbData->_ia._elements.size()];
			BuildLowLevelInputAssembly(
				MakeIteratorRange(animatedElements, &animatedElements[vbData->_ia._elements.size()]),
				vbData->_ia._elements);
		
			bool requiresGPUStaticDataLoad = false;
			LinkDeformers(
				MakeIteratorRange(animatedElements, &animatedElements[vbData->_ia._elements.size()]),
				vertexCount, animatedElementsStride, isCPUDeformer, geoIdx, *modelScaffold,
				instantiations,
				resultDeformerBindings,
				rendererBindingResult,
				bufferIterators,
				requiresGPUStaticDataLoad);

			if (requiresGPUStaticDataLoad) {
				bufferIterators._gpuStaticDataLoadRequests.push_back({modelScaffold, vbData->_offset, vbData->_size});
				bufferIterators._bufferIterators[VB_GPUStaticData] += vbData->_size;
			}

			return rendererBindingResult;
		}

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
					if (lhs._modelScaffold < rhs._modelScaffold) return true;
					if (lhs._modelScaffold > rhs._modelScaffold) return false;
					return lhs._geoIdx < rhs._geoIdx;
				});

			// do each input model scaffold at a time
			for (auto q=inputLoadRequests.begin(); q!=inputLoadRequests.end();) {
				auto scaffoldStart = q;
				++q;
				while (q!=inputLoadRequests.end() && q->_modelScaffold == scaffoldStart->_modelScaffold) ++q;

				auto largeBlocks = scaffoldStart->_modelScaffold->OpenLargeBlocks();
				auto base = largeBlocks->TellP();

				for (auto i=scaffoldStart; i!=q;) {

					auto start = i;
					while (i!=q && i->_geoIdx == start->_geoIdx) ++i;
					auto end = i;

					auto geoMachine = scaffoldStart->_modelScaffold->GetGeoMachine(start->_geoIdx);
					const RenderCore::Assets::RawGeometryDesc* rawGeometry = nullptr;
					const RenderCore::Assets::SkinningDataDesc* skinningData = nullptr;
					for (auto cmd:geoMachine) {
						switch (cmd.Cmd()) {
						case (uint32_t)Assets::GeoCommand::AttachRawGeometry:
							rawGeometry = &cmd.As<Assets::RawGeometryDesc>();
							break;
						case (uint32_t)Assets::GeoCommand::AttachSkinningData:
							skinningData = &cmd.As<Assets::SkinningDataDesc>();
							break;
						}
					}

					assert(rawGeometry);

					if (rawGeometry && !skinningData) {
						auto& vb = rawGeometry->_vb;

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

					} else if (rawGeometry && skinningData) {
						std::unique_ptr<uint8_t[]> baseVB;
						std::unique_ptr<uint8_t[]> animVB;
						std::unique_ptr<uint8_t[]> skelBindVB;

						for (auto r=start; r!=end; ++r) {
							assert(r->_targetFormat != Format::Unknown);
							assert(r->_targetStride != 0);
							auto sourceEle = FindElement(MakeIteratorRange(rawGeometry->_vb._ia._elements), r->_sourceStream);

							if (sourceEle != rawGeometry->_vb._ia._elements.end()) {
								if (!baseVB.get()) {
									baseVB = std::make_unique<uint8_t[]>(rawGeometry->_vb._size);
									largeBlocks->Seek(base + rawGeometry->_vb._offset);
									largeBlocks->Read(baseVB.get(), rawGeometry->_vb._size);
								}
								ReadStaticData(MakeIteratorRange(result), MakeIteratorRange(baseVB.get(), PtrAdd(baseVB.get(), rawGeometry->_vb._size)), *r, *sourceEle, rawGeometry->_vb._ia._vertexStride);
							} else {
								sourceEle = FindElement(MakeIteratorRange(skinningData->_animatedVertexElements._ia._elements), r->_sourceStream);
								if (sourceEle != skinningData->_animatedVertexElements._ia._elements.end()) {
									if (!animVB.get()) {
										animVB = std::make_unique<uint8_t[]>(skinningData->_animatedVertexElements._size);
										largeBlocks->Seek(base + skinningData->_animatedVertexElements._offset);
										largeBlocks->Read(animVB.get(), skinningData->_animatedVertexElements._size);
									}
									ReadStaticData(MakeIteratorRange(result), MakeIteratorRange(animVB.get(), PtrAdd(animVB.get(), skinningData->_animatedVertexElements._size)), *r, *sourceEle, skinningData->_animatedVertexElements._ia._vertexStride);
								} else {
									sourceEle = FindElement(MakeIteratorRange(skinningData->_skeletonBinding._ia._elements), r->_sourceStream);
									if (sourceEle != skinningData->_skeletonBinding._ia._elements.end()) {
										if (!skelBindVB.get()) {
											skelBindVB = std::make_unique<uint8_t[]>(skinningData->_skeletonBinding._size);
											largeBlocks->Seek(base + skinningData->_skeletonBinding._offset);
											largeBlocks->Read(skelBindVB.get(), skinningData->_skeletonBinding._size);
										}
										ReadStaticData(MakeIteratorRange(result), MakeIteratorRange(skelBindVB.get(), PtrAdd(skelBindVB.get(), skinningData->_skeletonBinding._size)), *r, *sourceEle, skinningData->_skeletonBinding._ia._vertexStride);
									} else
										Throw(std::runtime_error("Could not initialize deform input element"));
								}
							}
						}
					}
				}
			}

			return result;
		}
	}

	void IGeoDeformer::ExecuteGPU(
		IThreadContext& threadContext,
		IteratorRange<const unsigned*> instanceIndices,
		unsigned outputInstanceStride,
		const IResourceView& srcVB,
		const IResourceView& deformTemporariesVB,
		const IResourceView& dstVB,
		Metrics& metrics) const
	{
		assert(0);
	}

	void IGeoDeformer::ExecuteCPU(
		IteratorRange<const unsigned*> instanceIndices,
		unsigned outputInstanceStride,
		IteratorRange<const void*> srcVB,
		IteratorRange<const void*> deformTemporariesVB,
		IteratorRange<const void*> dstVB) const
	{
		assert(0);
	}

	IGeoDeformer::~IGeoDeformer() {}

}}


