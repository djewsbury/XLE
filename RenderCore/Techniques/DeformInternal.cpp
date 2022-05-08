// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformInternal.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../IDevice.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace Techniques { namespace Internal
{
	GPUDeformEntryHelper::GPUDeformEntryHelper(const DeformerInputBinding& bindings, std::pair<unsigned, unsigned> elementAndGeoIdx)
	{
		auto binding = std::find_if(bindings._geoBindings.begin(), bindings._geoBindings.end(), [elementAndGeoIdx](const auto& c) { return c.first == elementAndGeoIdx; });
		if (binding == bindings._geoBindings.end())
			Throw(std::runtime_error("Missing deformer binding for geoId (" + std::to_string(elementAndGeoIdx.second) + ")"));

		unsigned inPositionsOffset = 0, inNormalsOffset = 0, inTangentsOffset = 0;
		unsigned outPositionsOffset = 0, outNormalsOffset = 0, outTangentsOffset = 0;
		unsigned bufferFlags = 0;
		for (const auto&ele:binding->second._inputElements) {
			assert(ele._inputSlot == Internal::VB_GPUStaticData || ele._inputSlot == Internal::VB_GPUDeformTemporaries);
			auto semanticHash = Hash64(ele._semanticName);
			if (semanticHash == CommonSemantics::POSITION && ele._semanticIndex == 0) {
				_selectors.SetParameter("IN_POSITION_FORMAT", (unsigned)ele._nativeFormat);
				inPositionsOffset = ele._alignedByteOffset + binding->second._bufferOffsets[ele._inputSlot];
				if (ele._inputSlot == Internal::VB_GPUDeformTemporaries)
					bufferFlags |= 0x1;
			} else if (semanticHash == CommonSemantics::NORMAL && ele._semanticIndex == 0) {
				_selectors.SetParameter("IN_NORMAL_FORMAT", (unsigned)ele._nativeFormat);
				inNormalsOffset = ele._alignedByteOffset + binding->second._bufferOffsets[ele._inputSlot];
				if (ele._inputSlot == Internal::VB_GPUDeformTemporaries)
					bufferFlags |= 0x2;
			} else if (semanticHash == CommonSemantics::TEXTANGENT && ele._semanticIndex == 0) {
				_selectors.SetParameter("IN_TEXTANGENT_FORMAT", (unsigned)ele._nativeFormat);
				inTangentsOffset = ele._alignedByteOffset + binding->second._bufferOffsets[ele._inputSlot];
				if (ele._inputSlot == Internal::VB_GPUDeformTemporaries)
					bufferFlags |= 0x4;
			} else {
				assert(0);
			}
		}

		for (const auto&ele:binding->second._outputElements) {
			assert(ele._inputSlot == Internal::VB_PostDeform || ele._inputSlot == Internal::VB_GPUDeformTemporaries);
			auto semanticHash = Hash64(ele._semanticName);
			if (semanticHash == CommonSemantics::POSITION && ele._semanticIndex == 0) {
				_selectors.SetParameter("OUT_POSITION_FORMAT", (unsigned)ele._nativeFormat);
				outPositionsOffset = ele._alignedByteOffset + binding->second._bufferOffsets[ele._inputSlot];
				if (ele._inputSlot == Internal::VB_GPUDeformTemporaries)
					bufferFlags |= 0x1<<16;
			} else if (semanticHash == CommonSemantics::NORMAL && ele._semanticIndex == 0) {
				_selectors.SetParameter("OUT_NORMAL_FORMAT", (unsigned)ele._nativeFormat);
				outNormalsOffset = ele._alignedByteOffset + binding->second._bufferOffsets[ele._inputSlot];
				if (ele._inputSlot == Internal::VB_GPUDeformTemporaries)
					bufferFlags |= 0x2<<16;
			} else if (semanticHash == CommonSemantics::TEXTANGENT && ele._semanticIndex == 0) {
				_selectors.SetParameter("OUT_TEXTANGENT_FORMAT", (unsigned)ele._nativeFormat);
				outTangentsOffset = ele._alignedByteOffset + binding->second._bufferOffsets[ele._inputSlot];
				if (ele._inputSlot == Internal::VB_GPUDeformTemporaries)
					bufferFlags |= 0x4<<16;
			} else {
				assert(0);
			}
		}

		_selectors.SetParameter("BUFFER_FLAGS", (unsigned)bufferFlags);

		_iaParams._inPositionsOffset = inPositionsOffset;
		_iaParams._inNormalsOffset = inNormalsOffset;
		_iaParams._inTangentsOffset = inTangentsOffset;
		_iaParams._outPositionsOffset = outPositionsOffset;
		_iaParams._outNormalsOffset = outNormalsOffset;
		_iaParams._outTangentsOffset = outTangentsOffset;
		_iaParams._inputStride = binding->second._bufferStrides[Internal::VB_GPUStaticData];
		_iaParams._outputStride = binding->second._bufferStrides[Internal::VB_PostDeform];
		_iaParams._deformTemporariesStride = binding->second._bufferStrides[Internal::VB_GPUDeformTemporaries];
		_iaParams._mappingBufferByteOffset = 0;
		_iaParams._dummy[0] = _iaParams._dummy[1] = ~0u;
	}

	auto DeformerPipelineCollection::GetPipeline(ParameterBox&& selectors) -> PipelineMarkerIdx
	{
		ScopedLock(_mutex);
		// note -- no selector filtering done here
		uint64_t hash = HashCombine(selectors.GetHash(), selectors.GetParameterNamesHash());

		auto i = std::find(_pipelineHashes.begin(), _pipelineHashes.end(), hash);
		if (i!=_pipelineHashes.end())
			return std::distance(_pipelineHashes.begin(), i);

		if (_pendingCreateSharedResources)
			RebuildSharedResources();

		auto operatorMarker = std::make_shared<::Assets::Marker<Techniques::ComputePipelineAndLayout>>();
		::Assets::WhenAll(_preparedSharedResources.ShareFuture()).ThenConstructToPromise(
			operatorMarker->AdoptPromise(),
			[pipelineCollection=_pipelineCollection, selectors, patchExpansions=_patchExpansions](auto&& promise, const auto& preparedResources) {
				const ParameterBox* sel[] { &selectors };
				pipelineCollection->CreateComputePipeline(
					std::move(promise),
					preparedResources._pipelineLayout, 
					DEFORM_ENTRY_HLSL ":frameworkEntry", MakeIteratorRange(sel),
					preparedResources._patchCollection, MakeIteratorRange(patchExpansions));
			});
		_pipelines.push_back(operatorMarker);
		_pipelineHashes.push_back(hash);
		_pipelineSelectors.emplace_back(std::move(selectors));
		return (PipelineMarkerIdx)(_pipelines.size()-1);
	}

	void DeformerPipelineCollection::StallForPipeline()
	{
		ScopedLock(_mutex);
		if (_pendingCreateSharedResources)
			RebuildSharedResources();
		_preparedSharedResources.StallWhilePending();
		for (auto& p:_pipelines)
			p->StallWhilePending();
	}

	void DeformerPipelineCollection::OnFrameBarrier()
	{
		ScopedLock(_mutex);
		bool rebuildAllPipelines = false;
		if (_pendingCreateSharedResources || ::Assets::IsInvalidated(_preparedSharedResources)) {
			RebuildSharedResources();
			rebuildAllPipelines = true;
		}

		for (unsigned c=0; c<_pipelines.size(); ++c)
			if (rebuildAllPipelines || ::Assets::IsInvalidated(*_pipelines[c])) {
				auto operatorMarker = std::make_shared<::Assets::Marker<Techniques::ComputePipelineAndLayout>>();
				::Assets::WhenAll(_preparedSharedResources.ShareFuture()).ThenConstructToPromise(
					operatorMarker->AdoptPromise(),
					[pipelineCollection=_pipelineCollection, selectors=_pipelineSelectors[c], patchExpansions=_patchExpansions](auto&& promise, const auto& preparedResources) {
						const ParameterBox* sel[] { &selectors };
						pipelineCollection->CreateComputePipeline(
							std::move(promise),
							preparedResources._pipelineLayout, 
							DEFORM_ENTRY_HLSL ":frameworkEntry", MakeIteratorRange(sel),
							preparedResources._patchCollection, MakeIteratorRange(patchExpansions));
					});
				_pipelines[c] = std::move(operatorMarker);
			}
	}

	void DeformerPipelineCollection::RebuildSharedResources()
	{
		_pendingCreateSharedResources = false;
		_preparedSharedResources = ::Assets::Marker<PreparedSharedResources>{};
		auto predefinedPipelineLayout = ::Assets::MakeAsset<std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout>>(_predefinedPipelineInitializer);
		::Assets::WhenAll(predefinedPipelineLayout).ThenConstructToPromise(
			_preparedSharedResources.AdoptPromise(),
			[device=_pipelineCollection->GetDevice(), usi0=_usi0, usi1=_usi1, instRequest=_instRequest](auto predefinedPipelineLayoutActual) mutable {
				PreparedSharedResources result;
				result._pipelineLayout = device->CreatePipelineLayout(predefinedPipelineLayoutActual->MakePipelineLayoutInitializer(Techniques::GetDefaultShaderLanguage()));
				result._boundUniforms = Metal::BoundUniforms{ result._pipelineLayout, usi0, usi1 };
				
				ShaderSourceParser::GenerateFunctionOptions generateOptions;
				generateOptions._shaderLanguage = Techniques::GetDefaultShaderLanguage();
				ShaderSourceParser::InstantiationRequest instRequests[] { std::move(instRequest) };
				auto inst = ShaderSourceParser::InstantiateShader(MakeIteratorRange(instRequests), generateOptions);
				result._patchCollection = std::make_shared<Techniques::CompiledShaderPatchCollection>(inst, Techniques::DescriptorSetLayoutAndBinding{});

				::Assets::DependencyValidationMarker depVals[] { predefinedPipelineLayoutActual->GetDependencyValidation(), result._patchCollection->GetDependencyValidation() };
				result._depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
				return result;
			});
	}

	DeformerPipelineCollection::DeformerPipelineCollection(
		std::shared_ptr<PipelineCollection> pipelineCollection,
		StringSection<> predefinedPipeline,
		UniformsStreamInterface&& usi0,
		UniformsStreamInterface&& usi1,
		ShaderSourceParser::InstantiationRequest&& instRequest,
		IteratorRange<const uint64_t*> patchExpansions)
	: _pipelineCollection(std::move(pipelineCollection))
	, _usi0(std::move(usi0))
	, _usi1(std::move(usi1))
	, _instRequest(std::move(instRequest))
	, _patchExpansions(patchExpansions.begin(), patchExpansions.end())
	, _predefinedPipelineInitializer(predefinedPipeline.begin(), predefinedPipeline.end())
	, _pendingCreateSharedResources(true)
	{
		// Don't create the shared resources immediately here; because we can end up here very early during initialization,
		// before the device second stage init. That's a problem because we call IDevice::CreatePipelineLayout
	}
	DeformerPipelineCollection::~DeformerPipelineCollection() {}

}}}
