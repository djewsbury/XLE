// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SkinDeformer.h"
#include "SkinDeformerInternal.h"
#include "DeformGeoInternal.h"		// (required for some utility functions)
#include "CommonUtils.h"
#include "Services.h"
#include "SubFrameEvents.h"
#include "DeformerConstruction.h"
#include "../Assets/ModelRendererConstruction.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Assets/ModelMachine.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/TextureView.h"
#include "../BufferView.h"
#include "../IDevice.h"
#include "../ResourceDesc.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/AssetTraits.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"
#include <assert.h>

namespace RenderCore { namespace Techniques
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static const auto s_weightsEle = Hash64("WEIGHTS");
	static const auto s_jointIndicesEle = Hash64("JOINTINDICES");
	static const std::string s_positionEleName = "POSITION";
	static const std::string s_tangentEleName = "TEXTANGENT";
	static const std::string s_normalEleName = "NORMAL";

	static std::vector<uint64_t> CopyCmdStreamInputInterface(const Assets::ModelScaffold& modelScaffold)
	{
		auto src = modelScaffold.FindCommandStreamInputInterface();
		return {src.begin(), src.end()};
	}

	void CPUSkinDeformer::WriteJointTransforms(
		const Section& section,
		IteratorRange<Float3x4*>		destination,
		IteratorRange<const Float4x4*>	skeletonMachineResult) const
	{
		unsigned c=0;
		if (_skeletonBinding.GetModelJointCount()) {
			for (; c<std::min(section._jointMatrices.size(), destination.size()); ++c) {
				auto transMachineOutput = _skeletonBinding.ModelJointToMachineOutput(section._jointMatrices[c]);
				if (transMachineOutput != ~unsigned(0x0)) {
					destination[c] = Truncate(Combine(Combine(section._bindShapeByInverseBindMatrices[c], skeletonMachineResult[transMachineOutput]), section._postSkinningBindMatrix));
				} else {
					destination[c] = Truncate(Combine(section._bindShapeMatrix, section._postSkinningBindMatrix));
				}
			}
		} else {
			for (; c<std::min(section._jointMatrices.size(), destination.size()); ++c)
				destination[c] = Truncate(Combine(section._bindShapeMatrix, section._postSkinningBindMatrix));
		}

		for (; c<destination.size(); ++c)
			destination[c] = Identity<Float3x4>();
	}

	RenderCore::Assets::SkeletonBinding CPUSkinDeformer::CreateBinding(
		const RenderCore::Assets::SkeletonMachine::OutputInterface& skeletonMachineOutputInterface) const
	{
		return RenderCore::Assets::SkeletonBinding{skeletonMachineOutputInterface, _jointInputInterface};
	}

	void CPUSkinDeformer::FeedInSkeletonMachineResults(
		unsigned instanceIdx,
		IteratorRange<const Float4x4*> skeletonMachineOutput,
		const RenderCore::Assets::SkeletonBinding& binding)
	{
		_skeletonMachineOutput.clear();
		_skeletonMachineOutput.insert(_skeletonMachineOutput.end(), skeletonMachineOutput.begin(), skeletonMachineOutput.end());
		_skeletonBinding = binding;
	}

	void CPUSkinDeformer::ExecuteCPU(
		IteratorRange<const unsigned*> instanceIndices,
		unsigned outputInstanceStride,
		IteratorRange<const void*> srcVB,
		IteratorRange<const void*> deformTemporariesVB,
		IteratorRange<const void*> dstVB) const
	{
		assert(instanceIndices.size() == 1);
		auto instanceIdx = instanceIndices[0];
		assert(instanceIdx == 0);

		IteratorRange<VertexElementIterator> sourceElements[16];
		IteratorRange<VertexElementIterator> destinationElements[16];

		for (const auto&geo:_geos) {

			auto binding = _bindingHelper.CalculateRanges(
				MakeIteratorRange(sourceElements, &sourceElements[dimof(sourceElements)]),
				MakeIteratorRange(destinationElements, &destinationElements[dimof(destinationElements)]),
				geo._geoId, srcVB, deformTemporariesVB, dstVB);

			auto& inputPosElement = sourceElements[0];
			auto& outputPosElement = destinationElements[0];
			assert(inputPosElement.begin().Format() == Format::R32G32B32_FLOAT);
			assert(outputPosElement.begin().Format() == Format::R32G32B32_FLOAT);
			assert(outputPosElement.size() <= inputPosElement.size());
			
			for (const auto&section:geo._sections) {
				std::vector<Float3x4> jointTransform(section._jointMatrices.size());
				WriteJointTransforms(
					section,
					MakeIteratorRange(jointTransform),
					MakeIteratorRange(_skeletonMachineOutput));

				assert(section._preskinningDrawCalls.size() == section._drawCallWeightsPerVertex.size());
				for (unsigned dc=0; dc<section._preskinningDrawCalls.size(); ++dc) {
					auto& drawCall = section._preskinningDrawCalls[dc];
					auto weightsPerVertex = section._drawCallWeightsPerVertex[dc];
					assert((drawCall._firstVertex + drawCall._indexCount) <= outputPosElement.size());

					auto srcPosition = inputPosElement.begin() + drawCall._firstVertex;

					// drawCall._subMaterialIndex is 0, 1, 2 or 4 depending on the number of weights we have to proces
					if (weightsPerVertex == 0) {
						// in this case, we just copy
						for (auto p=outputPosElement.begin() + drawCall._firstVertex; p < (outputPosElement.begin() + drawCall._firstVertex + drawCall._indexCount); ++p, ++srcPosition)
							*p = (*srcPosition).As<Float3>();
						continue;
					}

					auto srcJointWeight = geo._jointWeights.begin() + drawCall._firstVertex * geo._influencesPerVertex;
					auto srcJointIndex = geo._jointIndices.begin() + drawCall._firstVertex * geo._influencesPerVertex;

					for (auto p=outputPosElement.begin() + drawCall._firstVertex; 
						p < (outputPosElement.begin() + drawCall._firstVertex + drawCall._indexCount); 
						++p, ++srcPosition, srcJointWeight+=geo._influencesPerVertex, srcJointIndex+=geo._influencesPerVertex) {
					
						Float3 deformedPosition { 0.f, 0.f, 0.f };
						for (unsigned b=0; b<weightsPerVertex; ++b) {
							assert(srcJointIndex[b] < jointTransform.size());
							deformedPosition += srcJointWeight[b] * TransformPoint(jointTransform[srcJointIndex[b]], (*srcPosition).As<Float3>());
						}

						*p = deformedPosition;
					}
				}
			}
		}
	}

	CPUSkinDeformer::CPUSkinDeformer(
		const RenderCore::Assets::ModelScaffold& modelScaffold,
		const std::string& modelScaffoldName)
	{
		auto largeBlocks = modelScaffold.OpenLargeBlocks();
		auto base = largeBlocks->TellP();

		auto geoCount = modelScaffold.GetGeoCount();
		for (unsigned geoIdx=0; geoIdx<geoCount; ++geoIdx) {
			auto geoMachine = modelScaffold.GetGeoMachine(geoIdx);

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

			if (!skinningData) continue;
		
			auto& skelVb = skinningData->_skeletonBinding;

			auto skelVbData = std::make_unique<uint8_t[]>(skelVb._size);
			largeBlocks->Seek(base + skelVb._offset);
			largeBlocks->Read(skelVbData.get(), skelVb._size);

			Geo constructedGeo;
			constructedGeo._geoId = geoIdx;

			constructedGeo._influencesPerVertex = 0;
			unsigned elements = 0;
			for (unsigned c=0; ; ++c) {
				auto weightsElement = Internal::FindElement(MakeIteratorRange(skelVb._ia._elements), "WEIGHTS", c);
				auto jointIndicesElement = Internal::FindElement(MakeIteratorRange(skelVb._ia._elements), "JOINTINDICES", c);
				if (!weightsElement || !jointIndicesElement)
					break;
				assert(GetComponentCount(GetComponents(weightsElement->_nativeFormat)) == GetComponentCount(GetComponents(jointIndicesElement->_nativeFormat)));
				constructedGeo._influencesPerVertex += GetComponentCount(GetComponents(weightsElement->_nativeFormat));
				++elements;
			}

			if (!elements)
				Throw(std::runtime_error("Could not create SkinDeformer because there is no position, weights and/or joint indices element in input geometry"));

			{
				auto vertexCount = skelVb._size / skelVb._ia._vertexStride;
				constructedGeo._jointWeights.resize(vertexCount * constructedGeo._influencesPerVertex);
				constructedGeo._jointIndices.resize(vertexCount * constructedGeo._influencesPerVertex);

				unsigned componentIterator=0;
				for (unsigned c=0; c<elements; ++c) {
					auto weightsElement = Internal::FindElement(MakeIteratorRange(skelVb._ia._elements), "WEIGHTS", c);
					auto jointIndicesElement = Internal::FindElement(MakeIteratorRange(skelVb._ia._elements), "JOINTINDICES", c);
					assert(weightsElement && jointIndicesElement);

					auto subWeights = AsFloat4s(Internal::AsVertexElementIteratorRange(MakeIteratorRange(skelVbData.get(), PtrAdd(skelVbData.get(), skelVb._size)), *weightsElement, skelVb._ia._vertexStride));
					auto subJoints = AsUInt4s(Internal::AsVertexElementIteratorRange(MakeIteratorRange(skelVbData.get(), PtrAdd(skelVbData.get(), skelVb._size)), *jointIndicesElement, skelVb._ia._vertexStride));
					auto subComponentCount = GetComponentCount(GetComponents(weightsElement->_nativeFormat));

					for (unsigned q=0; q<vertexCount; ++q) {
						std::memcpy(&constructedGeo._jointWeights[q*constructedGeo._influencesPerVertex+componentIterator], &subWeights[q][0], subComponentCount * sizeof(float));
						std::memcpy(&constructedGeo._jointIndices[q*constructedGeo._influencesPerVertex+componentIterator], &subJoints[q][0], subComponentCount * sizeof(float));
					}
					componentIterator += subComponentCount;
				}
			}

			constructedGeo._sections.reserve(skinningData->_preskinningSections.size());
			for (const auto&sourceSection:skinningData->_preskinningSections) {
				Section section;
				section._preskinningDrawCalls = MakeIteratorRange(sourceSection._preskinningDrawCalls);
				section._drawCallWeightsPerVertex = MakeIteratorRange(sourceSection._drawCallWeightsPerVertex);
				section._bindShapeByInverseBindMatrices = MakeIteratorRange(sourceSection._bindShapeByInverseBindMatrices);
				section._bindShapeMatrix = sourceSection._bindShapeMatrix;
				section._postSkinningBindMatrix = sourceSection._postSkinningBindMatrix;
				section._jointMatrices = { sourceSection._jointMatrices, sourceSection._jointMatrices + sourceSection._jointMatrixCount };
				constructedGeo._sections.push_back(section);
			}

			_geos.emplace_back(std::move(constructedGeo));
		}

		_jointInputInterface = CopyCmdStreamInputInterface(modelScaffold);
	}

	void* CPUSkinDeformer::QueryInterface(size_t typeId)
	{
		if (typeId == typeid(CPUSkinDeformer).hash_code())
			return this;
		else if (typeId == typeid(ISkinDeformer).hash_code())
			return (ISkinDeformer*)this;
		else if (typeId == typeid(IGeoDeformer).hash_code())
			return (IGeoDeformer*)this;
		return nullptr;
	}

	void CPUSkinDeformer::Bind(const DeformerInputBinding& binding)
	{
		_bindingHelper._inputBinding = binding;
	}

	bool CPUSkinDeformer::IsCPUDeformer() const
	{
		return true;
	}

	std::future<void> CPUSkinDeformer::GetInitializationFuture() const
	{
		return {};
	}

	CPUSkinDeformer::~CPUSkinDeformer()
	{
	}
		
	class CPUSkinDeformConfigure : public IDeformConfigure
	{
	public:
		void Configure(DeformerConstruction& deformerConstruction, InputStreamFormatter<char>&) override
		{
			// We'll create one CPUSkinDeformer per element with skinned meshes. Each deformer will animate all of the meshes
			// within that one element
			unsigned elementIdx = 0;
			for (auto ele:deformerConstruction.GetModelRendererConstruction()) {
				auto modelScaffold = ele.GetModelScaffold();
				if (!modelScaffold) { ++elementIdx; continue; }

				std::vector<std::pair<unsigned, DeformOperationInstantiation>> instantiations;
				auto geoCount = modelScaffold->GetGeoCount();
				for (unsigned c=0; c<geoCount; ++c) {
					auto machine = modelScaffold->GetGeoMachine(c);

					const Assets::SkinningDataDesc* skinningData = nullptr;
					for (auto cmd:machine) {
						if (cmd.Cmd() == (uint32_t)Assets::GeoCommand::AttachSkinningData) {
							skinningData = &cmd.As<Assets::SkinningDataDesc>();
							break;
						}
					}
					if (!skinningData) break;

					DeformOperationInstantiation deformOp;
					deformOp._generatedElements = {DeformOperationInstantiation::SemanticNameAndFormat{s_positionEleName, 0, Format::R32G32B32_FLOAT}};
					deformOp._upstreamSourceElements = {DeformOperationInstantiation::SemanticNameAndFormat{s_positionEleName, 0, Format::R32G32B32_FLOAT}};
					deformOp._suppressElements = {s_weightsEle, s_jointIndicesEle};
					instantiations.emplace_back(c, std::move(deformOp));
				}

				// create the deformer if necessary and add the instantiations we just find
				if (!instantiations.empty()) {
					auto deformer = std::make_shared<CPUSkinDeformer>(*modelScaffold, ele.GetModelScaffoldName());
					for (auto& inst:instantiations)
						deformerConstruction.Add(deformer, std::move(inst.second), elementIdx, inst.first);
				}

				++elementIdx;
			}
		}
	};

	std::shared_ptr<IDeformConfigure> CreateCPUSkinDeformerConfigure()
	{
		return std::make_shared<CPUSkinDeformConfigure>();
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	RenderCore::Assets::SkeletonBinding GPUSkinDeformer::CreateBinding(
		const RenderCore::Assets::SkeletonMachine::OutputInterface& skeletonMachineOutputInterface) const
	{
		return RenderCore::Assets::SkeletonBinding{skeletonMachineOutputInterface, _jointInputInterface};
	}

	void GPUSkinDeformer::FeedInSkeletonMachineResults(
		unsigned instanceIdx,
		IteratorRange<const Float4x4*> skeletonMachineOutput,
		const RenderCore::Assets::SkeletonBinding& binding)
	{
		if (_jointMatrices.size() < (instanceIdx+1)*_jointMatricesInstanceStride) {
			assert(!_defaultInstanceJointMatrices.empty());
			_jointMatrices.reserve((instanceIdx+1)*_jointMatricesInstanceStride);
			while (_jointMatrices.size() < (instanceIdx+1)*_jointMatricesInstanceStride)
				_jointMatrices.insert(_jointMatrices.end(), _defaultInstanceJointMatrices.begin(), _defaultInstanceJointMatrices.end());
		}

		CopySkeletonMachineResults(
			MakeIteratorRange(
				_jointMatrices.begin()+instanceIdx*_jointMatricesInstanceStride, 
				_jointMatrices.begin()+(instanceIdx+1)*_jointMatricesInstanceStride),
			skeletonMachineOutput, binding);
	}

	void GPUSkinDeformer::CopySkeletonMachineResults(
		IteratorRange<Float3x4*> dst,
		IteratorRange<const Float4x4*> skeletonMachineOutput,
		const RenderCore::Assets::SkeletonBinding& binding)
	{
		assert(dst.size() == _jointMatricesInstanceStride);
		for (unsigned sectionIdx=0; sectionIdx<_sections.size(); ++sectionIdx) {
			auto& section = _sections[sectionIdx];
			auto destination = MakeIteratorRange(
				dst.begin()+section._rangeInJointMatrices.first, 
				dst.begin()+section._rangeInJointMatrices.second);

			unsigned c=0;
			if (binding.GetModelJointCount()) {
				for (; c<std::min(section._jointMatrices.size(), destination.size()); ++c) {
					auto transMachineOutput = binding.ModelJointToMachineOutput(section._jointMatrices[c]);
					if (transMachineOutput != ~unsigned(0x0)) {
						destination[c] = Truncate(Combine(Combine(section._bindShapeByInverseBindMatrices[c], skeletonMachineOutput[transMachineOutput]), section._postSkinningBindMatrix));
					} else {
						destination[c] = Truncate(Combine(section._bindShapeMatrix, section._postSkinningBindMatrix));
					}
				}
			} else {
				for (; c<std::min(section._jointMatrices.size(), destination.size()); ++c)
					destination[c] = Truncate(Combine(section._bindShapeMatrix, section._postSkinningBindMatrix));
			}

			for (; c<destination.size(); ++c)
				destination[c] = Identity<Float3x4>();
		}
	}

	void GPUSkinDeformer::SetDefaultSkeletonMachineResults(
		IteratorRange<const Float4x4*> skeletonMachineOutput,
		const RenderCore::Assets::SkeletonBinding& binding)
	{
		// note that this won't effect any instances that have previously recieved skeleton machine results,
		// or even any instances with lower instanceIdx's than those that have previously received results
		CopySkeletonMachineResults(MakeIteratorRange(_defaultInstanceJointMatrices), skeletonMachineOutput, binding);
	}

	void GPUSkinDeformer::ExecuteGPU(
		IThreadContext& threadContext,
		IteratorRange<const unsigned*> instanceIndices,
		unsigned outputInstanceStride,
		const IResourceView& srcVB,
		const IResourceView& deformTemporariesVB,
		const IResourceView& dstVB,
		Metrics& metrics) const
	{
		assert(!instanceIndices.empty());
		struct InvocationParams
		{
			// InvocationParams
			unsigned _vertexCount, _firstVertex;
			unsigned _instanceCount, _outputInstanceStride, _deformTemporariesInstanceStride, _iaParamsIdx;

			// SkinInvocationParams
			unsigned _softInfluenceCount, _firstJointTransform;
			unsigned _skinIAParamsIdx, _jointMatricesInstanceStride;
		};

		auto& metalContext = *Metal::DeviceContext::Get(threadContext);

		std::shared_ptr<RenderCore::IResourceView> jointMatricesBuffer;
		auto jmTemporaryDataSize = instanceIndices.size()*sizeof(Float3x4)*_jointMatricesInstanceStride;
		{
			auto temporaryMapping = metalContext.MapTemporaryStorage(jmTemporaryDataSize, BindFlag::UnorderedAccess);
			for (unsigned c=0; c<instanceIndices.size(); ++c) {
				// fallback to the default instance data if FeedInSkeletonMachineResults() has never been called for this instance
				const Float3x4* jointMatrixSrc;
				if ((instanceIndices[c]+1)*_jointMatricesInstanceStride <= _jointMatrices.size())
					jointMatrixSrc = PtrAdd(_jointMatrices.data(), instanceIndices[c]*sizeof(Float3x4)*_jointMatricesInstanceStride);
				else
					jointMatrixSrc = _defaultInstanceJointMatrices.data();

				std::memcpy(
					PtrAdd(temporaryMapping.GetData().begin(), c*sizeof(Float3x4)*_jointMatricesInstanceStride), 
					jointMatrixSrc,
					sizeof(Float3x4)*_jointMatricesInstanceStride);
			}
			auto beginAndEndInResource = temporaryMapping.GetBeginAndEndInResource();
			jointMatricesBuffer = temporaryMapping.AsResourceView();
		}

		auto sharedRes = _pipelineCollection->_preparedSharedResources.TryActualize();
		if (!sharedRes) return;

		auto encoder = metalContext.BeginComputeEncoder(sharedRes->_pipelineLayout);
		Metal::CapturedStates capturedStates;
		encoder.BeginStateCapture(capturedStates);

		const IResourceView* rvs[] { _staticVertexAttachmentsView.get(), &srcVB, &dstVB, &deformTemporariesVB, jointMatricesBuffer.get(), _iaParamsView.get(), _skinIAParamsView.get() };
		UniformsStream us;
		us._resourceViews = MakeIteratorRange(rvs);
		sharedRes->_boundUniforms.ApplyLooseUniforms(metalContext, encoder, us, 0);
		++metrics._descriptorSetWrites;

		const Techniques::ComputePipelineAndLayout* currentPipelineLayout = nullptr;
		unsigned currentPipelineMarker = ~0u;

		const unsigned instanceCount = (unsigned)instanceIndices.size();
		const unsigned wavegroupWidth = 64;
		for (const auto&dispatch:_dispatches) {
			if (dispatch._pipelineMarker != currentPipelineMarker) {
				currentPipelineLayout = _pipelineCollection->_pipelines[dispatch._pipelineMarker]->TryActualize();
				currentPipelineMarker = dispatch._pipelineMarker;
			}
			if (!currentPipelineLayout) continue;
				
			InvocationParams invocationParams { 
				dispatch._vertexCount,  dispatch._firstVertex,
				instanceCount, outputInstanceStride, outputInstanceStride, dispatch._iaParamsIdx,
				dispatch._softInfluenceCount, dispatch._firstJointTransform,
				dispatch._skinIAParamsIdx, _jointMatricesInstanceStride };
			auto groupCount = (dispatch._vertexCount*instanceCount+wavegroupWidth-1)/wavegroupWidth;
			encoder.PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, MakeOpaqueIteratorRange(invocationParams));
			encoder.Dispatch(*currentPipelineLayout->_pipeline, groupCount, 1, 1);
			metrics._vertexCount += groupCount*wavegroupWidth;
		}

		metrics._dispatchCount += (unsigned)_dispatches.size();
		metrics._constantDataSize += jmTemporaryDataSize;
		metrics._inputStaticDataSize += _staticVertexAttachmentsSize;
	}

	template<typename Marker, typename Time>
		static bool MarkerTimesOut(Marker& marker, Time timeoutTime)
		{
			auto remainingTime = timeoutTime - std::chrono::steady_clock::now();
			if (remainingTime.count() <= 0) return true;
			auto t = marker.StallWhilePending(std::chrono::duration_cast<std::chrono::microseconds>(remainingTime));
			return t.value_or(::Assets::AssetState::Pending) == ::Assets::AssetState::Pending;
		}

	std::future<void> GPUSkinDeformer::GetInitializationFuture() const
	{
		std::vector<unsigned> pipelineMarkers;
		pipelineMarkers.reserve(_dispatches.size());
		for (const auto&dispatch:_dispatches) {
			auto i = std::lower_bound(pipelineMarkers.begin(), pipelineMarkers.end(), dispatch._pipelineMarker);
			if (i == pipelineMarkers.end() || *i != dispatch._pipelineMarker)
				pipelineMarkers.insert(i, dispatch._pipelineMarker);
		}

		std::promise<void> promise;
		auto result = promise.get_future();
		::Assets::PollToPromise(
			std::move(promise),
			[pipelineCollection=std::weak_ptr<Internal::DeformerPipelineCollection>(_pipelineCollection), pipelineMarkers, linearBufferCompletion=_linearBufferCompletion](auto timeout) {
				auto l = pipelineCollection.lock();
				if (!l) return ::Assets::PollStatus::Finish;
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				if (MarkerTimesOut(l->_preparedSharedResources, timeoutTime)) return ::Assets::PollStatus::Continue;
				for (auto m:pipelineMarkers)
					if (MarkerTimesOut(*l->_pipelines[m], timeoutTime)) return ::Assets::PollStatus::Continue;
				if (linearBufferCompletion.wait_until(timeoutTime) == std::future_status::timeout) return ::Assets::PollStatus::Continue;
				return ::Assets::PollStatus::Finish;
			},
			[]() {});
		return result;
	}

	BufferUploads::CommandListID GPUSkinDeformer::GetCompletionCmdList() const
	{
		assert(_linearBufferCompletion.valid());	// must have called Bind() beforehand
		return _linearBufferCompletion.get().GetCompletionCommandList();
	}

	GPUSkinDeformer::GPUSkinDeformer(
		std::shared_ptr<Internal::DeformerPipelineCollection> pipelineCollection,
		std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold,
		const std::string& modelScaffoldName)
	: _modelScaffold(std::move(modelScaffold))			// we take internal pointers so preserve lifetime
	, _pipelineCollection(std::move(pipelineCollection))
	{
		auto geoCount = _modelScaffold->GetGeoCount();

		std::vector<ModelScaffoldLoadRequest> staticDataLoadRequests;
		staticDataLoadRequests.reserve(geoCount);
		unsigned skelVBIterator = 0;

		unsigned jointMatrixBufferCount = 0;
		for (unsigned geoIdx=0; geoIdx<geoCount; ++geoIdx) {
			auto geoMachine = _modelScaffold->GetGeoMachine(geoIdx);

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

			if (!skinningData) continue;

			auto& skelVb = skinningData->_skeletonBinding;

			unsigned influencesPerVertex = 0;
			unsigned skelVBStride = skelVb._ia._vertexStride;
			unsigned weightsOffset = ~0u, indicesOffset = ~0u;
			Format weightsFormat = Format::Unknown, indicesFormat = Format::Unknown;
			unsigned parrallelElementsCount = 0;
			for (unsigned c=0; ; ++c) {
				auto weightsElement = Internal::FindElement(MakeIteratorRange(skelVb._ia._elements), "WEIGHTS", c);
				auto jointIndicesElement = Internal::FindElement(MakeIteratorRange(skelVb._ia._elements), "JOINTINDICES", c);
				if (!weightsElement || !jointIndicesElement)
					break;
				if (!parrallelElementsCount) {
					weightsOffset = weightsElement->_alignedByteOffset;
					indicesOffset = jointIndicesElement->_alignedByteOffset;
					weightsFormat = weightsElement->_nativeFormat;
					indicesFormat = jointIndicesElement->_nativeFormat;
				} else {
					// we must use the same type format for each attribute (though the quantity can differ)
					assert(GetComponentType(weightsFormat) == GetComponentType(weightsElement->_nativeFormat));
					assert(GetComponentType(indicesFormat) == GetComponentType(jointIndicesElement->_nativeFormat));
					auto weightsBitsPerComponent = BitsPerPixel(weightsFormat) / GetComponentCount(GetComponents(weightsFormat));
					auto indicesBitsPerComponent = BitsPerPixel(indicesFormat) / GetComponentCount(GetComponents(indicesFormat));
					// Ensure that the attributes are sequential
					assert(weightsElement->_alignedByteOffset == weightsOffset + influencesPerVertex*weightsBitsPerComponent/8);
					assert(jointIndicesElement->_alignedByteOffset == indicesOffset + influencesPerVertex*indicesBitsPerComponent/8);
				}
				assert(GetComponentCount(GetComponents(weightsElement->_nativeFormat)) == GetComponentCount(GetComponents(jointIndicesElement->_nativeFormat)));
				influencesPerVertex += GetComponentCount(GetComponents(weightsElement->_nativeFormat));
				++parrallelElementsCount;
			}

			if (weightsOffset == ~0u || indicesOffset == ~0u)
				Throw(std::runtime_error("Could not create SkinDeformer because there is no position, weights and/or joint indices element in input geometry"));
			if (influencesPerVertex == 1) {
				// no limitation on alignment
			} else if (influencesPerVertex == 2) {
				// must be aligned to multiple of 2 (technically we just want to prevent the 2 influences from ever straddling a dword boundary)
				if ((skelVBStride%2)!=0 || (weightsOffset%2)!=0 || (indicesOffset%2)!=0)
					Throw(std::runtime_error("Could not create SkinDeformer because input skeleton binding data is not correctly aligned"));
			} else {
				// 4 or more influences, must be aligned to multiple of 4
				if ((skelVBStride%4)!=0 || (weightsOffset%4)!=0 || (indicesOffset%4)!=0)
					Throw(std::runtime_error("Could not create SkinDeformer because input skeleton binding data is not correctly aligned"));
			}

			_sections.reserve(skinningData->_preskinningSections.size());
			for (const auto&sourceSection:skinningData->_preskinningSections) {
				Section section;
				section._geoId = geoIdx;
				section._preskinningDrawCalls = MakeIteratorRange(sourceSection._preskinningDrawCalls);
				section._drawCallWeightsPerVertex = MakeIteratorRange(sourceSection._drawCallWeightsPerVertex);
				section._bindShapeByInverseBindMatrices = MakeIteratorRange(sourceSection._bindShapeByInverseBindMatrices);
				section._bindShapeMatrix = sourceSection._bindShapeMatrix;
				section._postSkinningBindMatrix = sourceSection._postSkinningBindMatrix;
				section._jointMatrices = { sourceSection._jointMatrices, sourceSection._jointMatrices + sourceSection._jointMatrixCount };
				section._rangeInJointMatrices = { jointMatrixBufferCount, jointMatrixBufferCount + (unsigned)sourceSection._jointMatrixCount };

				section._indicesFormat = indicesFormat;
				section._weightsFormat = weightsFormat;
				section._sectionInfluencesPerVertex = influencesPerVertex;
				section._skinIAParamsIdx = (unsigned)_skinIAParams.size();

				_sections.push_back(section);
				jointMatrixBufferCount += sourceSection._jointMatrixCount;
			}

			SkinIAParams iaParams = {0};
			iaParams._weightsOffset = weightsOffset + skelVBIterator;
			iaParams._jointIndicesOffset = indicesOffset + skelVBIterator;
			iaParams._staticVertexAttachmentsStride = skelVBStride;
			_skinIAParams.push_back(iaParams);

			staticDataLoadRequests.push_back({_modelScaffold, skelVb._offset, skelVb._size});
			skelVBIterator += skelVb._size;
		}
		_jointMatricesInstanceStride = jointMatrixBufferCount;
		_defaultInstanceJointMatrices.resize(_jointMatricesInstanceStride, Identity<Float3x4>());

		assert(!staticDataLoadRequests.empty());
		_staticVertexAttachments = LoadStaticResourcePartialAsync(
			*_pipelineCollection->_pipelineCollection->GetDevice(), MakeIteratorRange(staticDataLoadRequests), skelVBIterator,
			BindFlag::UnorderedAccess,
			(StringMeld<64>() << "[skin]" << modelScaffoldName).AsStringSection()).first;
		_staticVertexAttachmentsView = _staticVertexAttachments->CreateBufferView(BindFlag::UnorderedAccess);
		_staticVertexAttachmentsSize = skelVBIterator;

		_jointInputInterface = CopyCmdStreamInputInterface(*_modelScaffold);
	}

	void GPUSkinDeformer::Bind(const DeformerInputBinding& bindings)
	{
		for (auto s=_sections.begin(); s!=_sections.end();){
			auto start = s;
			++s;
			while (s!=_sections.end() && s->_geoId == start->_geoId) ++s;

			Internal::GPUDeformEntryHelper helper{bindings, start->_geoId};
			auto selectors = helper._selectors;
			selectors.SetParameter("JOINT_INDICES_TYPE", (unsigned)GetComponentType(start->_indicesFormat));
			selectors.SetParameter("JOINT_INDICES_PRECISION", (unsigned)GetComponentPrecision(start->_indicesFormat));
			selectors.SetParameter("WEIGHTS_TYPE", (unsigned)GetComponentType(start->_weightsFormat));
			selectors.SetParameter("WEIGHTS_PRECISION", (unsigned)GetComponentPrecision(start->_weightsFormat));
			selectors.SetParameter("INFLUENCE_COUNT", (unsigned)start->_sectionInfluencesPerVertex);
			auto pipelineMarker = _pipelineCollection->GetPipeline(std::move(selectors));
			for (auto q=start; q!=s; ++q) {
				assert(q->_indicesFormat == start->_indicesFormat);
				assert(q->_weightsFormat == start->_weightsFormat);
				assert(q->_preskinningDrawCalls.size() == q->_drawCallWeightsPerVertex.size());
				for (unsigned dc=0; dc<q->_preskinningDrawCalls.size(); ++dc) {
					const auto& draw = q->_preskinningDrawCalls[dc];
					assert(draw._firstIndex == ~unsigned(0x0));		// avoid confusion; this isn't used for anything
					Dispatch dispatch;
					dispatch._iaParamsIdx = (unsigned)_iaParams.size();
					dispatch._skinIAParamsIdx = q->_skinIAParamsIdx;
					dispatch._vertexCount = draw._indexCount;
					dispatch._firstVertex = draw._firstVertex;
					dispatch._softInfluenceCount = q->_drawCallWeightsPerVertex[dc];
					dispatch._pipelineMarker = pipelineMarker;
					dispatch._firstJointTransform = q->_rangeInJointMatrices.first;
					_dispatches.push_back(dispatch);
				}
			}
			
			_iaParams.push_back(helper._iaParams);
		}

		// sort by pipeline
		std::stable_sort(
			_dispatches.begin(), _dispatches.end(),
			[](const auto& lhs, const auto& rhs) {
				return lhs._pipelineMarker < rhs._pipelineMarker;
			});

		std::vector<uint8_t> uploadBuffer;
		uploadBuffer.reserve(_iaParams.size() * sizeof(Internal::GPUDeformerIAParams) + _skinIAParams.size() * sizeof(SkinIAParams));
		uploadBuffer.insert(uploadBuffer.end(), (const uint8_t*)AsPointer(_iaParams.begin()), (const uint8_t*)AsPointer(_iaParams.end()));
		uploadBuffer.insert(uploadBuffer.end(), (const uint8_t*)AsPointer(_skinIAParams.begin()), (const uint8_t*)AsPointer(_skinIAParams.end()));

		auto utilitiesBuffer = _pipelineCollection->_pipelineCollection->GetDevice()->CreateResource(
			CreateDesc(
				BindFlag::ShaderResource | BindFlag::UnorderedAccess | BindFlag::TransferDst,
				LinearBufferDesc::Create(uploadBuffer.size()), "skin-ia-data"));

		auto& bufferUploads = Techniques::Services::GetBufferUploads();
		auto transaction = bufferUploads.Begin(utilitiesBuffer, BufferUploads::CreateBasicPacket(std::move(uploadBuffer)));
		_linearBufferCompletion = std::move(transaction._future);

		_iaParamsView = utilitiesBuffer->CreateBufferView(BindFlag::ShaderResource, 0, _iaParams.size() * sizeof(Internal::GPUDeformerIAParams));
		_skinIAParamsView = utilitiesBuffer->CreateBufferView(BindFlag::ShaderResource, _iaParams.size() * sizeof(Internal::GPUDeformerIAParams), _skinIAParams.size() * sizeof(SkinIAParams));
	}

	bool GPUSkinDeformer::IsCPUDeformer() const { return false; }

	void* GPUSkinDeformer::QueryInterface(size_t typeId)
	{
		if (typeId == typeid(GPUSkinDeformer).hash_code())
			return this;
		else if (typeId == typeid(ISkinDeformer).hash_code())
			return (ISkinDeformer*)this;
		else if (typeId == typeid(IGeoDeformer).hash_code())
			return (IGeoDeformer*)this;
		return nullptr;
	}

	GPUSkinDeformer::~GPUSkinDeformer()
	{
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static const Assets::SkinningDataDesc* FindSkinningData(IteratorRange<Assets::ScaffoldCmdIterator> machine)
	{
		for (auto cmd:machine)
			if (cmd.Cmd() == (uint32_t)Assets::GeoCommand::AttachSkinningData)
				return &cmd.As<Assets::SkinningDataDesc>();
		return nullptr;
	}

	static const Assets::RawGeometryDesc* FindRawGeometry(IteratorRange<Assets::ScaffoldCmdIterator> machine)
	{
		for (auto cmd:machine)
			if (cmd.Cmd() == (uint32_t)Assets::GeoCommand::AttachRawGeometry)
				return &cmd.As<Assets::RawGeometryDesc>();
		return nullptr;
	}

	static Format PostSkinningPositionFormat(Format inputFormat)
	{
		auto compType = GetComponentType(inputFormat);
		switch (compType) {
		default: return inputFormat;

		case FormatComponentType::UNorm:
		case FormatComponentType::SNorm:
			// We can't realistically return to a UNorm/SNorm format after
			// skinning -- because these are usually used to fit the pre-skinned
			// data exactly to the range, and skinning will just shift out of that range
			auto compCount = GetComponentCount(GetComponents(inputFormat));
			if (compCount == 1) return Format::R32_FLOAT;
			else if (compCount == 2) return Format::R32G32_FLOAT;
			else if (compCount == 3) return Format::R32G32B32_FLOAT;
			else return Format::R32G32B32A32_FLOAT;
		}
	}

	class GPUSkinDeformConfigure : public IDeformConfigure
	{
	public:
		void Configure(DeformerConstruction& deformerConstruction, InputStreamFormatter<char>&) override
		{
			// We'll create one GPUSkinDeformer per element with skinned meshes. Each deformer will animate all of the meshes
			// within that one element
			unsigned elementIdx = 0;
			for (auto ele:deformerConstruction.GetModelRendererConstruction()) {
				auto modelScaffold = ele.GetModelScaffold();
				if (!modelScaffold) { ++elementIdx; continue; }

				std::vector<std::pair<unsigned, DeformOperationInstantiation>> instantiations;
				auto geoCount = modelScaffold->GetGeoCount();
				for (unsigned c=0; c<geoCount; ++c) {
					auto* skinningData = FindSkinningData(modelScaffold->GetGeoMachine(c));
					if (!skinningData) continue;

					auto& animVB = skinningData->_animatedVertexElements;
					auto positionElement = Internal::FindElement(MakeIteratorRange(animVB._ia._elements), s_positionEleName);
					auto tangentsElement = Internal::FindElement(MakeIteratorRange(animVB._ia._elements), s_tangentEleName);
					auto normalsElement = Internal::FindElement(MakeIteratorRange(animVB._ia._elements), s_normalEleName);
					if (!positionElement)
						Throw(std::runtime_error("Missing animated position in GPU skinning input"));

					DeformOperationInstantiation deformOp;
					deformOp._upstreamSourceElements.push_back({s_positionEleName, 0});
					deformOp._generatedElements.push_back({s_positionEleName, 0, PostSkinningPositionFormat(positionElement->_nativeFormat)});
					if (normalsElement) {
						deformOp._upstreamSourceElements.push_back({s_normalEleName, 0});
						deformOp._generatedElements.push_back({s_normalEleName, 0, normalsElement->_nativeFormat});
					}
					if (tangentsElement) {
						deformOp._upstreamSourceElements.push_back({s_tangentEleName, 0});
						deformOp._generatedElements.push_back({s_tangentEleName, 0, tangentsElement->_nativeFormat});
					}
					deformOp._suppressElements = {s_weightsEle, s_jointIndicesEle};
					instantiations.emplace_back(c, std::move(deformOp));
				}

				// create the deformer if necessary and add the instantiations we just find
				if (!instantiations.empty()) {
					auto deformer = std::make_shared<GPUSkinDeformer>(_pipelineCollection, modelScaffold, ele.GetModelScaffoldName());

					// if there's a skeleton attached to the ModelRendererConstruction, then we should pass in a default set of joint matrices
					const Assets::SkeletonMachine* skeletonMachine = nullptr;
					if (auto* skeleton=deformerConstruction.GetModelRendererConstruction().GetSkeletonScaffold().get())
						skeletonMachine = &skeleton->GetSkeletonMachine();
					if (!skeletonMachine)
						skeletonMachine = modelScaffold->EmbeddedSkeleton();
					if (skeletonMachine) {
						RenderCore::Assets::SkeletonBinding defaultSkeletonBinding;
						std::vector<Float4x4> defaultSkeletonMachineOutput;
						if (skeletonMachine) {
							defaultSkeletonBinding = {skeletonMachine->GetOutputInterface(), modelScaffold->FindCommandStreamInputInterface()};
							defaultSkeletonMachineOutput.resize(skeletonMachine->GetOutputMatrixCount(), Identity<Float4x4>());
							skeletonMachine->GenerateOutputTransforms(MakeIteratorRange(defaultSkeletonMachineOutput));
						}

						deformer->SetDefaultSkeletonMachineResults(defaultSkeletonMachineOutput, defaultSkeletonBinding);
					}

					for (auto& inst:instantiations)
						deformerConstruction.Add(deformer, std::move(inst.second), elementIdx, inst.first);
				}

				++elementIdx;
			}
		}

		GPUSkinDeformConfigure(std::shared_ptr<PipelineCollection> pipelineCollection)
		{
			UniformsStreamInterface usi;
			usi.BindResourceView(0, Hash64("StaticVertexAttachments"));
			usi.BindResourceView(1, Hash64("InputAttributes"));
			usi.BindResourceView(2, Hash64("OutputAttributes"));
			usi.BindResourceView(3, Hash64("DeformTemporaryAttributes"));
			usi.BindResourceView(4, Hash64("JointTransforms"));
			usi.BindResourceView(5, Hash64("IAParams"));
			usi.BindResourceView(6, Hash64("SkinIAParams"));

			UniformsStreamInterface pushConstantsUSI;
			pushConstantsUSI.BindImmediateData(0, Hash64("InvocationParams"));

			ShaderSourceParser::InstantiationRequest instRequest { SKIN_COMPUTE_HLSL };
			uint64_t patchExpansions[] { Hash64("PerformDeform"), Hash64("GetDeformInvocationParams") };

			_pipelineCollection = std::make_shared<Internal::DeformerPipelineCollection>(
				std::move(pipelineCollection),
				SKIN_PIPELINE ":Main",
				std::move(usi), std::move(pushConstantsUSI),
				std::move(instRequest), MakeIteratorRange(patchExpansions));
			_pipelineCollection->RegisterOnFrameBarrierCallback(Techniques::Services::GetSubFrameEvents());
		}

	private:
		std::shared_ptr<RenderCore::Techniques::Internal::DeformerPipelineCollection> _pipelineCollection;
	};

	std::shared_ptr<IDeformConfigure> CreateGPUSkinDeformerConfigure(std::shared_ptr<PipelineCollection> pipelineCollection)
	{
		return std::make_shared<GPUSkinDeformConfigure>(std::move(pipelineCollection));
	}

	ISkinDeformer::~ISkinDeformer() {}

	namespace Internal
	{
		const DeformerInputBinding::GeoBinding* DeformerInputBindingHelper::CalculateRanges(
			IteratorRange<VertexElementRange*> sourceElements,
			IteratorRange<VertexElementRange*> destinationElements,
			unsigned geoIdx,
			IteratorRange<const void*> srcVB,
			IteratorRange<const void*> deformTemporariesVB,
			IteratorRange<const void*> dstVB) const
		{
			// note that we ignore the elementIdx when looking up in _inputBinding._geoBindings. This is because
			// the input bindings are specific to this deformer, and the deformer only supports a single model scaffold, anyway
			auto i = std::find_if(_inputBinding._geoBindings.begin(), _inputBinding._geoBindings.end(), [geoIdx](const auto& c) { return c.first.second == geoIdx; });
			assert(i != _inputBinding._geoBindings.end());
			if (i == _inputBinding._geoBindings.end())
				return nullptr;
			auto& binding = i->second;
			assert(binding._inputElements.size() <= sourceElements.size());
			assert(binding._outputElements.size() <= destinationElements.size());

			for (unsigned c=0; c<binding._inputElements.size(); ++c) {
				if (binding._inputElements[c]._inputSlot == Internal::VB_CPUStaticData) {
					sourceElements[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(srcVB.begin(), binding._inputElements[c]._alignedByteOffset + binding._bufferOffsets[Internal::VB_CPUStaticData]), srcVB.end()),
						binding._bufferStrides[Internal::VB_CPUStaticData], binding._inputElements[c]._nativeFormat);
				} else {
					assert(binding._inputElements[c]._inputSlot == Internal::VB_CPUDeformTemporaries);
					sourceElements[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(deformTemporariesVB.begin(), binding._inputElements[c]._alignedByteOffset + binding._bufferOffsets[Internal::VB_CPUDeformTemporaries]), deformTemporariesVB.end()),
						binding._bufferStrides[Internal::VB_CPUDeformTemporaries], binding._inputElements[c]._nativeFormat);
				}
			}

			for (unsigned c=0; c<binding._outputElements.size(); ++c) {
				if (binding._outputElements[c]._inputSlot == Internal::VB_PostDeform) {
					destinationElements[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(dstVB.begin(), binding._outputElements[c]._alignedByteOffset + binding._bufferOffsets[Internal::VB_PostDeform]), dstVB.end()),
						binding._bufferStrides[Internal::VB_PostDeform], binding._outputElements[c]._nativeFormat);
				} else {
					assert(binding._outputElements[c]._inputSlot == Internal::VB_CPUDeformTemporaries);
					destinationElements[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(deformTemporariesVB.begin(), binding._outputElements[c]._alignedByteOffset + binding._bufferOffsets[Internal::VB_CPUDeformTemporaries]), deformTemporariesVB.end()),
						binding._bufferStrides[Internal::VB_CPUDeformTemporaries], binding._outputElements[c]._nativeFormat);
				}
			}

			return &binding;
		}
	}
}}
