// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformGeometryInfrastructure.h"
#include "DeformInternal.h"
#include "Services.h"
#include "CommonUtils.h"
#include "CommonResources.h"
#include "../IDevice.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/ModelScaffoldInternal.h"
#include "../Assets/ModelImmutableData.h"
#include "../GeoProc/MeshDatabase.h"        // for GeoProc::Copy
#include "../../BufferUploads/IBufferUploads.h"
#include "../../Assets/IFileSystem.h"
#include "../../Utility/StringFormat.h"

namespace RenderCore { namespace Techniques
{
	namespace Internal
	{
		static std::vector<uint8_t> GenerateDeformStaticInputForCPUDeform(
			const RenderCore::Assets::ModelScaffold& modelScaffold,
			IteratorRange<const SourceDataTransform*> inputLoadRequests,
			unsigned destinationBufferSize);
	}

	class DeformGeoInfrastructure : public IGeoDeformerInfrastructure
	{
	public:
		std::vector<std::shared_ptr<IGeoDeformer>> _deformOps;
		DeformerToRendererBinding _rendererGeoInterface;

		std::vector<uint8_t> _deformStaticDataInput;
		std::vector<uint8_t> _deformTemporaryBuffer;

		std::shared_ptr<IResource> _gpuStaticDataBuffer, _gpuTemporariesBuffer;
		std::shared_ptr<IResourceView> _gpuStaticDataBufferView, _gpuTemporariesBufferView;
		BufferUploads::TransactionMarker _gpuStaticDataBufferMarker;

		bool _isCPUDeformer = false;
		unsigned _outputVBSize = 0;

		void ReserveBytesRequired(
			unsigned instanceCount,
			unsigned& gpuBufferBytes,
			unsigned& cpuBufferBytes,
			unsigned& cbBytes) override
		{
			cpuBufferBytes += _isCPUDeformer ? _outputVBSize * instanceCount : 0;
			gpuBufferBytes += _isCPUDeformer ? 0 : _outputVBSize * instanceCount;
		}

		void Execute(
			IThreadContext& threadContext, 
			IteratorRange<const unsigned*> instanceIdx, 
			IResourceView& dstVB,
			IteratorRange<void*> cpuBufferOutputRange,
			IteratorRange<void*> cbBufferOutputRange,
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
	};

	 std::shared_ptr<IGeoDeformerInfrastructure> CreateDeformGeometryInfrastructure(
		IDevice& device,
		IteratorRange<const DeformOperationFactorySet::Deformer*> deformers,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::string& modelScaffoldName)
	{
		auto result = std::make_shared<DeformGeoInfrastructure>();
		
		////////////////////////////////////////////////////////////////////////////////////
		// Build deform streams

		Internal::DeformBufferIterators bufferIterators;
		std::vector<Internal::WorkingDeformer> workingDeformers;
		workingDeformers.reserve(deformers.size());

		result->_isCPUDeformer = !deformers.empty() && deformers[0]._factory->IsCPUDeformer();
		for (auto& d:deformers) {
			Internal::WorkingDeformer workingDeformer;
			workingDeformer._instantiations = MakeIteratorRange(d._instantiations);
			workingDeformers.push_back(std::move(workingDeformer));
			if (d._factory->IsCPUDeformer() != result->_isCPUDeformer)
				Throw(std::runtime_error("Attempting to mix CPU and GPU deformers. This isn't supported; deformations must be all CPU or all GPU"));
		};

		result->_rendererGeoInterface = Internal::CreateDeformBindings(
			MakeIteratorRange(workingDeformers), bufferIterators, result->_isCPUDeformer,
			modelScaffold, modelScaffoldName);

		// Bind the operators to linking result
		for (unsigned c=0; c<deformers.size(); ++c) {
			auto op = std::move(deformers[c]._operator);
			deformers[c]._factory->Bind(*op, workingDeformers[c]._inputBinding);
			result->_deformOps.push_back(std::move(op));
		}

		////////////////////////////////////////////////////////////////////////////////////

		if (!bufferIterators._gpuStaticDataLoadRequests.empty()) {
			std::tie(result->_gpuStaticDataBuffer, result->_gpuStaticDataBufferMarker) = LoadStaticResourcePartialAsync(
				device,
				{bufferIterators._gpuStaticDataLoadRequests.begin(), bufferIterators._gpuStaticDataLoadRequests.end()}, 
				bufferIterators._bufferIterators[Internal::VB_GPUStaticData],
				modelScaffold, BindFlag::UnorderedAccess,
				(StringMeld<64>() << "[deform]" << modelScaffoldName).AsStringSection());
			result->_gpuStaticDataBufferView = result->_gpuStaticDataBuffer->CreateBufferView(BindFlag::UnorderedAccess);
		} else {
			result->_gpuStaticDataBufferView = Techniques::Services::GetCommonResources()->_blackBufferUAV;
		}

		if (bufferIterators._bufferIterators[Internal::VB_GPUDeformTemporaries]) {
			result->_gpuTemporariesBuffer = device.CreateResource(
				RenderCore::CreateDesc(
					BindFlag::UnorderedAccess, 0, GPUAccess::Read|GPUAccess::Write,
					LinearBufferDesc::Create(bufferIterators._bufferIterators[Internal::VB_GPUDeformTemporaries]),
					(StringMeld<64>() << "[deform]" << modelScaffoldName).AsStringSection()));
			result->_gpuTemporariesBufferView = result->_gpuTemporariesBuffer->CreateBufferView(BindFlag::UnorderedAccess);
		} else {
			result->_gpuTemporariesBufferView = Techniques::Services::GetCommonResources()->_blackBufferUAV;
		}

		////////////////////////////////////////////////////////////////////////////////////

		// Create the dynamic VB and assign it to all of the slots it needs to go to
		result->_outputVBSize = bufferIterators._bufferIterators[Internal::VB_PostDeform];

		if (!bufferIterators._cpuStaticDataLoadRequests.empty()) {
			result->_deformStaticDataInput = Internal::GenerateDeformStaticInputForCPUDeform(
				*modelScaffold,
				MakeIteratorRange(bufferIterators._cpuStaticDataLoadRequests),
				bufferIterators._bufferIterators[Internal::VB_CPUStaticData]);
		}

		if (bufferIterators._bufferIterators[Internal::VB_CPUDeformTemporaries]) {
			result->_deformTemporaryBuffer.resize(bufferIterators._bufferIterators[Internal::VB_CPUDeformTemporaries], 0);
		}
		return result;
	}

	std::shared_ptr<IGeoDeformerInfrastructure> CreateDeformGeometryInfrastructure(
		IDevice& device,
		StringSection<> initializer,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::string& modelScaffoldName)
	{
		auto& opFactory = Services::GetDeformOperationFactorySet();
		auto deformers = opFactory.CreateDeformOperators(initializer, modelScaffold, modelScaffoldName);
		if (deformers.empty())
			return nullptr;
		
		return CreateDeformGeometryInfrastructure(device, deformers, modelScaffold, modelScaffoldName);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		static void LinkDeformers(
			/* in */ IteratorRange<const InputElementDesc*> animatedElementsInput,
			/* in */ unsigned vertexCount,
			/* in */ unsigned animatedElementsStride,
			/* in */ bool isCPUDeformer,
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

		DeformerToRendererBinding CreateDeformBindings(
			IteratorRange<WorkingDeformer*> workingDeformers,
			DeformBufferIterators& bufferIterators,
			bool isCPUDeformer,
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
					vertexCount, animatedElementsStride, isCPUDeformer,
					MakeIteratorRange(instantiations, &instantiations[workingDeformers.size()]),
					MakeIteratorRange(deformerInputBindings, &deformerInputBindings[workingDeformers.size()]),
					rendererBinding,
					bufferIterators,
					requiresGPUStaticDataLoad);

				rendererBindingResult._geoBindings.push_back(std::move(rendererBinding));

				for (unsigned d=0; d<workingDeformers.size(); ++d)
					if (instantiations[d])
						workingDeformers[d]._inputBinding._geoBindings.push_back(std::move(deformerInputBindings[d]));

				if (requiresGPUStaticDataLoad) {
					bufferIterators._gpuStaticDataLoadRequests.push_back(std::make_pair(geo._vbData->_offset, geo._vbData->_size));
					bufferIterators._bufferIterators[VB_GPUStaticData] += geo._vbData->_size;
				}
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
						assert(r->_targetFormat != Format::Unknown);
						assert(r->_targetStride != 0);
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

	void IGeoDeformer::ExecuteCB(
		IteratorRange<const unsigned*> instanceIndices,
		unsigned outputInstanceStride,
		IteratorRange<const void*> dstCB) const
	{
		assert(0);
	}

	IGeoDeformer::~IGeoDeformer() {}

}}


