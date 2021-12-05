// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SkinDeformer.h"
#include "SkinDeformerInternal.h"
#include "DeformAcceleratorInternal.h"		// (required for some utility functions)
#include "CommonUtils.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/ModelScaffoldInternal.h"
#include "../Assets/ModelImmutableData.h"
#include "../BufferView.h"
#include "../IDevice.h"
#include "../ResourceDesc.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/AssetTraits.h"
#include "../../Assets/Continuation.h"
#include "../../xleres/FileList.h"
#include <assert.h>

#include "PipelineOperators.h"
#include "CommonBindings.h"
#include "../UniformsStream.h"
#include "../../Assets/Marker.h"
#include "../../Utility/ParameterBox.h"

namespace RenderCore { namespace Techniques
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	const DeformerInputBinding::GeoBinding* InputBindingHelper::CalculateRanges(
		IteratorRange<VertexElementRange*> sourceElements,
		IteratorRange<VertexElementRange*> destinationElements,
		unsigned geoId,
		IteratorRange<const void*> srcVB,
		IteratorRange<const void*> deformTemporariesVB,
		IteratorRange<const void*> dstVB) const
	{
		auto binding = std::find_if(_inputBinding._geoBindings.begin(), _inputBinding._geoBindings.end(), [geoId](const auto& c) { return c._geoId == geoId; });
		assert(binding != _inputBinding._geoBindings.end());
		if (binding == _inputBinding._geoBindings.end())
			return nullptr;
		assert(binding->_inputElements.size() <= sourceElements.size());
		assert(binding->_outputElements.size() <= destinationElements.size());

		for (unsigned c=0; c<binding->_inputElements.size(); ++c) {
			if (binding->_inputElements[c]._inputSlot == Internal::VB_CPUStaticData) {
				sourceElements[c] = MakeVertexIteratorRangeConst(
					MakeIteratorRange(PtrAdd(srcVB.begin(), binding->_inputElements[c]._alignedByteOffset), srcVB.end()),
					binding->_bufferStrides[Internal::VB_CPUStaticData], binding->_inputElements[c]._nativeFormat);
			} else {
				assert(binding->_inputElements[c]._inputSlot == Internal::VB_CPUDeformTemporaries);
				sourceElements[c] = MakeVertexIteratorRangeConst(
					MakeIteratorRange(PtrAdd(deformTemporariesVB.begin(), binding->_inputElements[c]._alignedByteOffset), deformTemporariesVB.end()),
					binding->_bufferStrides[Internal::VB_CPUDeformTemporaries], binding->_inputElements[c]._nativeFormat);
			}
		}

		for (unsigned c=0; c<binding->_outputElements.size(); ++c) {
			if (binding->_outputElements[c]._inputSlot == Internal::VB_PostDeform) {
				destinationElements[c] = MakeVertexIteratorRangeConst(
					MakeIteratorRange(PtrAdd(dstVB.begin(), binding->_outputElements[c]._alignedByteOffset), dstVB.end()),
					binding->_bufferStrides[Internal::VB_PostDeform], binding->_outputElements[c]._nativeFormat);
			} else {
				assert(binding->_outputElements[c]._inputSlot == Internal::VB_CPUDeformTemporaries);
				destinationElements[c] = MakeVertexIteratorRangeConst(
					MakeIteratorRange(PtrAdd(deformTemporariesVB.begin(), binding->_outputElements[c]._alignedByteOffset), deformTemporariesVB.end()),
					binding->_bufferStrides[Internal::VB_CPUDeformTemporaries], binding->_outputElements[c]._nativeFormat);
			}
		}

		return AsPointer(binding);
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
					destination[c] = Truncate(skeletonMachineResult[transMachineOutput] * section._bindShapeByInverseBindMatrices[c]);
				} else {
					destination[c] = Truncate(section._bindShapeByInverseBindMatrices[c]);
				}
			}
		} else {
			for (; c<std::min(section._jointMatrices.size(), destination.size()); ++c)
				destination[c] = Truncate(section._bindShapeByInverseBindMatrices[c]);
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
		unsigned instanceIdx,
		IteratorRange<const void*> srcVB,
		IteratorRange<const void*> deformTemporariesVB,
		IteratorRange<const void*> dstVB) const
	{
		assert(instanceIdx == 0);

		IDeformOperator::VertexElementRange sourceElements[16];
		IDeformOperator::VertexElementRange destinationElements[16];
		unsigned currentGeoId = ~0u;
		const DeformerInputBinding::GeoBinding* binding = nullptr;

		for (const auto&section:_sections) {

			if (section._geoId != currentGeoId) {
				binding = _bindingHelper.CalculateRanges(
					MakeIteratorRange(sourceElements, &sourceElements[dimof(sourceElements)]),
					MakeIteratorRange(destinationElements, &destinationElements[dimof(destinationElements)]),
					section._geoId, srcVB, deformTemporariesVB, dstVB);
				currentGeoId = section._geoId;
			}

			auto& inputPosElement = sourceElements[0];
			auto& outputPosElement = destinationElements[0];
			assert(inputPosElement.begin().Format() == Format::R32G32B32_FLOAT);
			assert(outputPosElement.begin().Format() == Format::R32G32B32_FLOAT);
			assert(outputPosElement.size() <= inputPosElement.size());

			std::vector<Float3x4> jointTransform(section._jointMatrices.size());
			WriteJointTransforms(
				section,
				MakeIteratorRange(jointTransform),
				MakeIteratorRange(_skeletonMachineOutput));

			for (const auto&drawCall:section._preskinningDrawCalls) {
				assert((drawCall._firstVertex + drawCall._indexCount) <= outputPosElement.size());

				auto srcPosition = inputPosElement.begin() + drawCall._firstVertex;

				// drawCall._subMaterialIndex is 0, 1, 2 or 4 depending on the number of weights we have to proces
				if (drawCall._subMaterialIndex == 0) {
					// in this case, we just copy
					for (auto p=outputPosElement.begin() + drawCall._firstVertex; p < (outputPosElement.begin() + drawCall._firstVertex + drawCall._indexCount); ++p, ++srcPosition)
						*p = (*srcPosition).As<Float3>();
					continue;
				}

				auto srcJointWeight = _jointWeights.begin() + drawCall._firstVertex * _influencesPerVertex;
				auto srcJointIndex = _jointIndices.begin() + drawCall._firstVertex * _influencesPerVertex;

				for (auto p=outputPosElement.begin() + drawCall._firstVertex; 
					p < (outputPosElement.begin() + drawCall._firstVertex + drawCall._indexCount); 
					++p, ++srcPosition, srcJointWeight+=_influencesPerVertex, srcJointIndex+=_influencesPerVertex) {
				
					Float3 deformedPosition { 0.f, 0.f, 0.f };
					for (unsigned b=0; b<drawCall._subMaterialIndex; ++b) {
						assert(srcJointIndex[b] < jointTransform.size());
						deformedPosition += srcJointWeight[b] * TransformPoint(jointTransform[srcJointIndex[b]], (*srcPosition).As<Float3>());
					}

					*p = deformedPosition;
				}
			}
		}
	}

	CPUSkinDeformer::CPUSkinDeformer(
		const RenderCore::Assets::ModelScaffold& modelScaffold,
		const std::string& modelScaffoldName)
	{
		auto& immData = modelScaffold.ImmutableData();
		for (unsigned c=0; c<immData._boundSkinnedControllerCount; ++c) {
			auto& skinnedController = immData._boundSkinnedControllers[c];
		
			auto& skelVb = skinnedController._skeletonBinding;

			auto skelVbData = std::make_unique<uint8_t[]>(skelVb._size);
			{
				auto largeBlocks = modelScaffold.OpenLargeBlocks();
				auto base = largeBlocks->TellP();

				largeBlocks->Seek(base + skelVb._offset);
				largeBlocks->Read(skelVbData.get(), skelVb._size);
			}

			_influencesPerVertex = 0;
			unsigned elements = 0;
			for (unsigned c=0; ; ++c) {
				auto weightsElement = Internal::FindElement(MakeIteratorRange(skelVb._ia._elements), "WEIGHTS", c);
				auto jointIndicesElement = Internal::FindElement(MakeIteratorRange(skelVb._ia._elements), "JOINTINDICES", c);
				if (!weightsElement || !jointIndicesElement)
					break;
				assert(GetComponentCount(GetComponents(weightsElement->_nativeFormat)) == GetComponentCount(GetComponents(jointIndicesElement->_nativeFormat)));
				_influencesPerVertex += GetComponentCount(GetComponents(weightsElement->_nativeFormat));
				++elements;
			}

			if (!elements)
				Throw(std::runtime_error("Could not create SkinDeformer because there is no position, weights and/or joint indices element in input geometry"));

			{
				auto vertexCount = skelVb._size / skelVb._ia._vertexStride;
				_jointWeights.resize(vertexCount * _influencesPerVertex);
				_jointIndices.resize(vertexCount * _influencesPerVertex);

				unsigned componentIterator=0;
				for (unsigned c=0; c<elements; ++c) {
					auto weightsElement = Internal::FindElement(MakeIteratorRange(skelVb._ia._elements), "WEIGHTS", c);
					auto jointIndicesElement = Internal::FindElement(MakeIteratorRange(skelVb._ia._elements), "JOINTINDICES", c);
					assert(weightsElement && jointIndicesElement);

					auto subWeights = AsFloat4s(Internal::AsVertexElementIteratorRange(MakeIteratorRange(skelVbData.get(), PtrAdd(skelVbData.get(), skelVb._size)), *weightsElement, skelVb._ia._vertexStride));
					auto subJoints = AsUInt4s(Internal::AsVertexElementIteratorRange(MakeIteratorRange(skelVbData.get(), PtrAdd(skelVbData.get(), skelVb._size)), *jointIndicesElement, skelVb._ia._vertexStride));
					auto subComponentCount = GetComponentCount(GetComponents(weightsElement->_nativeFormat));

					for (unsigned q=0; q<vertexCount; ++q) {
						std::memcpy(&_jointWeights[q*_influencesPerVertex+componentIterator], &subWeights[q][0], subComponentCount * sizeof(float));
						std::memcpy(&_jointIndices[q*_influencesPerVertex+componentIterator], &subJoints[q][0], subComponentCount * sizeof(float));
					}
					componentIterator += subComponentCount;
				}
			}

			_sections.reserve(skinnedController._preskinningSections.size());
			for (const auto&sourceSection:skinnedController._preskinningSections) {
				Section section;
				section._preskinningDrawCalls = MakeIteratorRange(sourceSection._preskinningDrawCalls);
				section._bindShapeByInverseBindMatrices = MakeIteratorRange(sourceSection._bindShapeByInverseBindMatrices);
				section._jointMatrices = { sourceSection._jointMatrices, sourceSection._jointMatrices + sourceSection._jointMatrixCount };
				section._geoId = immData._geoCount + c;
				_sections.push_back(section);
			}

			_jointInputInterface = modelScaffold.CommandStream().GetInputInterface();
		}
	}

	void* CPUSkinDeformer::QueryInterface(size_t typeId)
	{
		if (typeId == typeid(CPUSkinDeformer).hash_code())
			return this;
		else if (typeId == typeid(ISkinDeformer).hash_code())
			return (ISkinDeformer*)this;
		else if (typeId == typeid(IDeformOperator).hash_code())
			return (IDeformOperator*)this;
		return nullptr;
	}

	CPUSkinDeformer::~CPUSkinDeformer()
	{
	}

	class CPUSkinDeformerFactory : public IDeformOperationFactory
	{
	public:
		std::shared_ptr<IDeformOperator> Configure(
			std::vector<RenderCore::Techniques::DeformOperationInstantiation>& result,
			StringSection<> initializer,
			std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold,
			const std::string& modelScaffoldName) override
		{
			// auto sep = std::find(initializer.begin(), initializer.end(), ',');
			// assert(sep != initializer.end());

			const std::string positionEleName = "POSITION";
			auto weightsEle = Hash64("WEIGHTS");
			auto jointIndicesEle = Hash64("JOINTINDICES");
			auto& immData = modelScaffold->ImmutableData();
			for (unsigned c=0; c<immData._boundSkinnedControllerCount; ++c) {

				// skeleton & anim set:
				//			StringSection<>(initializer.begin(), sep),
				//			StringSection<>(sep+1, initializer.end()

				DeformOperationInstantiation deformOp;
				// deformOp._cpuOperator = std::make_shared<SkinDeformer>(*modelScaffold, c);
				deformOp._geoId = unsigned(immData._geoCount) + c;
				deformOp._generatedElements = {DeformOperationInstantiation::SemanticNameAndFormat{positionEleName, 0, Format::R32G32B32_FLOAT}};
				deformOp._upstreamSourceElements = {DeformOperationInstantiation::SemanticNameAndFormat{positionEleName, 0, Format::R32G32B32_FLOAT}};
				deformOp._suppressElements = {weightsEle, jointIndicesEle};
				deformOp._cpuDeformer = true;
				result.emplace_back(std::move(deformOp));
			}

			return std::make_shared<CPUSkinDeformer>(*modelScaffold, modelScaffoldName);
		}

		virtual void Bind(IDeformOperator& op, const DeformerInputBinding& binding) override
		{
			auto* deformer = checked_cast<CPUSkinDeformer*>(&op);
			deformer->_bindingHelper._inputBinding = binding;
		}
	};

	std::shared_ptr<IDeformOperationFactory> CreateCPUSkinDeformerFactory()
	{
		return std::make_shared<CPUSkinDeformerFactory>();
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
		for (unsigned sectionIdx=0; sectionIdx<_sections.size(); ++sectionIdx) {
			auto& section = _sections[sectionIdx];
			auto destination = MakeIteratorRange(_jointMatrices.begin()+section._rangeInJointMatrices.first, _jointMatrices.begin()+section._rangeInJointMatrices.second);

			unsigned c=0;
			if (binding.GetModelJointCount()) {
				for (; c<std::min(section._jointMatrices.size(), destination.size()); ++c) {
					auto transMachineOutput = binding.ModelJointToMachineOutput(section._jointMatrices[c]);
					if (transMachineOutput != ~unsigned(0x0)) {
						destination[c] = Truncate(skeletonMachineOutput[transMachineOutput] * section._bindShapeByInverseBindMatrices[c]);
					} else {
						destination[c] = Truncate(section._bindShapeByInverseBindMatrices[c]);
					}
				}
			} else {
				for (; c<std::min(section._jointMatrices.size(), destination.size()); ++c)
					destination[c] = Truncate(section._bindShapeByInverseBindMatrices[c]);
			}

			for (; c<destination.size(); ++c)
				destination[c] = Identity<Float3x4>();
		}
	}

	void GPUSkinDeformer::ExecuteGPU(
		IThreadContext& threadContext,
		unsigned instanceId,
		const IResourceView& srcVB,
		const IResourceView& deformTemporariesVB,
		const IResourceView& dstVB) const
	{
		struct InvocationParams
		{
			unsigned _vertexCount, _firstVertex, _softInfluenceCount, _firstJointTransform;
		};

		IComputeShaderOperator* currentOperator = nullptr;
		unsigned currentGeoId = ~0u;

		static const auto InvocationParamsHash = Hash64("InvocationParams");

		const unsigned wavegroupWidth = 64;
		for (const auto&section:_sections) {
			if (section._geoId != currentGeoId) {
				auto op = section._pipelineMarker->TryActualize();
				if (!op) continue;

				// note -- we could make some of the iaParams values push constants to avoid re-uploading _jointMatrices here
				const IResourceView* rvs[] { _staticVertexAttachmentsView.get(), &srcVB, &dstVB };
				UniformsStream::ImmediateData immDatas[] {
					MakeOpaqueIteratorRange(section._iaParams),
					MakeIteratorRange(_jointMatrices)
				};
				UniformsStream us;
				us._resourceViews = MakeIteratorRange(rvs);
				us._immediateData = MakeIteratorRange(immDatas);

				// We have to call EndDispatches / BeginDispatches every geo change, because this is required
				// to push though updates to the uniform buffers in "us"
				
				if (currentOperator)
					currentOperator->EndDispatches();
				(*op)->BeginDispatches(threadContext, us, {}, InvocationParamsHash);
				currentOperator = op->get();
				currentGeoId = section._geoId;
			}

			for (const auto&drawCall:section._preskinningDrawCalls) {
				assert(drawCall._firstIndex == ~unsigned(0x0));		// avoid confusion; this isn't used for anything
				InvocationParams invocationParams { drawCall._indexCount, drawCall._firstVertex, drawCall._subMaterialIndex, section._rangeInJointMatrices.first };
				currentOperator->Dispatch((drawCall._indexCount+wavegroupWidth-1)/wavegroupWidth, 1, 1, MakeOpaqueIteratorRange(invocationParams));
			}
		}

		if (currentOperator)
			currentOperator->EndDispatches();
	}

#if 0
	static void CopyWeightsUNorm8(void* dst, IteratorRange<VertexElementIterator> inputRange, unsigned dstStride)
	{
		// Since we're going from arbitrary inputs -> uint8, we can end up dropping a lot of precision here
		auto fmtBreakdown = BreakdownFormat(inputRange.begin()._format);
		switch (fmtBreakdown._type) {
		case VertexUtilComponentType::Float32:
			for (auto v:inputRange) {
				for (unsigned c=0; c<fmtBreakdown._componentCount; c++)
					((uint8_t*)dst)[2*c] = (uint8_t)std::clamp(((float*)v._data.begin())[c] * 255.f, 0.f, 255.f);
				dst = PtrAdd(dst, dstStride);
			}
			break;

		case VertexUtilComponentType::Float16:
			for (auto v:inputRange) {
				Float4 f32;
				GetVertDataF16(f32.data(), (const uint16_t*)v._data.begin(), fmtBreakdown._componentCount);
				for (unsigned c=0; c<fmtBreakdown._componentCount; c++)
					((uint8_t*)dst)[2*c+1] = (uint8_t)std::clamp(f32[c] * 255.f, 0.f, 255.f);
				dst = PtrAdd(dst, dstStride);
			}
			break;

		case VertexUtilComponentType::UNorm8:
			for (auto v:inputRange) {
				for (unsigned c=0; c<fmtBreakdown._componentCount; c++)
					((uint8_t*)dst)[2*c] = ((uint8_t*)v._data.begin())[c];
				dst = PtrAdd(dst, dstStride);
			}
			break;

		case VertexUtilComponentType::UNorm16:
			for (auto v:inputRange) {
				for (unsigned c=0; c<fmtBreakdown._componentCount; c++)
					((uint8_t*)dst)[2*c] = ((uint16_t*)v._data.begin())[c] >> 8;
				dst = PtrAdd(dst, dstStride);
			}
			break;

		case VertexUtilComponentType::SNorm8:
		case VertexUtilComponentType::SNorm16:
		case VertexUtilComponentType::UInt8:
		case VertexUtilComponentType::UInt16:
		case VertexUtilComponentType::UInt32:
		case VertexUtilComponentType::SInt8:
		case VertexUtilComponentType::SInt16:
		case VertexUtilComponentType::SInt32:
		default:
			Throw(std::runtime_error("Unexpected input format for skinning weights. Float32/Float16/UNorm8/UNorm16 expected"));
		}
	}

	static void CopyIndicesUInt8(void* dst, IteratorRange<VertexElementIterator> inputRange, unsigned dstStride)
	{
		auto fmtBreakdown = BreakdownFormat(inputRange.begin()._format);
		switch (fmtBreakdown._type) {
		case VertexUtilComponentType::UInt8:
			for (auto v:inputRange) {
				for (unsigned c=0; c<fmtBreakdown._componentCount; c++)
					((uint8_t*)dst)[2*c] = ((uint8_t*)v._data.begin())[c];
				dst = PtrAdd(dst, dstStride);
			}
			break;
		case VertexUtilComponentType::UInt16:
			for (auto v:inputRange) {
				for (unsigned c=0; c<fmtBreakdown._componentCount; c++)
					((uint8_t*)dst)[2*c] = (((uint16_t*)v._data.begin())[c]) & 0xff;
				dst = PtrAdd(dst, dstStride);
			}
			break;
		case VertexUtilComponentType::UInt32:
			for (auto v:inputRange) {
				for (unsigned c=0; c<fmtBreakdown._componentCount; c++)
					((uint8_t*)dst)[2*c] = (((uint32_t*)v._data.begin())[c]) & 0xff;
				dst = PtrAdd(dst, dstStride);
			}
			break;

		case VertexUtilComponentType::Float32:
		case VertexUtilComponentType::Float16:
		case VertexUtilComponentType::UNorm8:
		case VertexUtilComponentType::UNorm16:
		case VertexUtilComponentType::SNorm8:
		case VertexUtilComponentType::SNorm16:
		case VertexUtilComponentType::SInt8:
		case VertexUtilComponentType::SInt16:
		case VertexUtilComponentType::SInt32:
		default:
			Throw(std::runtime_error("Unexpected input format for skinning weights. UInt8/UInt16/UInt32 expected"));
		}
	}

	static std::vector<uint8_t> ReconfigureStaticVertexAttachmentsBuffer(
		unsigned influencesPerVertex,
		unsigned vertexCount,
		unsigned parrallelElementsCount,
		const RenderCore::Assets::VertexData& skelVb,
		IteratorRange<void*> skelVbData)
	{
		// Reform the vertex attributes based weight & joint idx buffer into the format that our compute shader
		// wants to take
		auto bufferByteSize = influencesPerVertex*2*vertexCount;
		std::vector<uint8_t> buffer;
		buffer.resize(bufferByteSize, 0u);

		unsigned componentIterator=0;
		for (unsigned c=0; c<parrallelElementsCount; ++c) {
			auto weightsElement = Internal::FindElement(MakeIteratorRange(skelVb._ia._elements), "WEIGHTS", c);
			auto jointIndicesElement = Internal::FindElement(MakeIteratorRange(skelVb._ia._elements), "JOINTINDICES", c);
			assert(weightsElement && jointIndicesElement);

			auto subWeights = Internal::AsVertexElementIteratorRange(MakeIteratorRange(skelVbData.begin(), PtrAdd(skelVbData.begin(), skelVb._size)), *weightsElement, skelVb._ia._vertexStride);
			auto subJoints = Internal::AsVertexElementIteratorRange(MakeIteratorRange(skelVbData.begin(), PtrAdd(skelVbData.begin(), skelVb._size)), *jointIndicesElement, skelVb._ia._vertexStride);
			auto subComponentCount = GetComponentCount(GetComponents(weightsElement->_nativeFormat));

			assert(subWeights.size() == vertexCount);
			assert(subJoints.size() == vertexCount);

			// copy the weights & indices into the format which we'll use for the static attributes input buffer
			// weights & indices are interleaved -- index, weight, index, weight...
			CopyIndicesUInt8(PtrAdd(buffer.data(), componentIterator*2), subJoints, influencesPerVertex*2);
			CopyWeightsUNorm8(PtrAdd(buffer.data(), componentIterator*2+1), subWeights, influencesPerVertex*2);
			componentIterator += subComponentCount;
		}

		return buffer;
	}
#endif

	GPUSkinDeformer::GPUSkinDeformer(
		IDevice& device,
		std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold,
		const std::string& modelScaffoldName)
	: _modelScaffold(std::move(modelScaffold))			// we take internal pointers so preserve lifetime
	{
		auto& immData = _modelScaffold->ImmutableData();

		std::vector<std::pair<unsigned, unsigned>> staticDataLoadRequests;
		staticDataLoadRequests.reserve(immData._boundSkinnedControllerCount);
		unsigned skelVBIterator = 0;

		unsigned jointMatrixBufferCount = 0;
		for (unsigned c=0; c<immData._boundSkinnedControllerCount; ++c) {
			auto& skinnedController = immData._boundSkinnedControllers[c];

			auto& skelVb = skinnedController._skeletonBinding;

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

			_sections.reserve(skinnedController._preskinningSections.size());
			for (const auto&sourceSection:skinnedController._preskinningSections) {
				Section section;
				section._geoId = c+immData._geoCount;
				section._preskinningDrawCalls = MakeIteratorRange(sourceSection._preskinningDrawCalls);
				section._bindShapeByInverseBindMatrices = MakeIteratorRange(sourceSection._bindShapeByInverseBindMatrices);
				section._jointMatrices = { sourceSection._jointMatrices, sourceSection._jointMatrices + sourceSection._jointMatrixCount };
				section._rangeInJointMatrices = { jointMatrixBufferCount, jointMatrixBufferCount + (unsigned)sourceSection._jointMatrixCount };

				section._iaParams = {0};
				section._iaParams._weightsOffset = weightsOffset + skelVBIterator;
				section._iaParams._jointIndicesOffset = indicesOffset + skelVBIterator;
				section._iaParams._staticVertexAttachmentsStride = skelVBStride;
				section._indicesFormat = indicesFormat;
				section._weightsFormat = weightsFormat;
				section._influencesPerVertex = influencesPerVertex;

				_sections.push_back(section);
				jointMatrixBufferCount += sourceSection._jointMatrixCount;
			}

			staticDataLoadRequests.push_back({ skelVb._offset, skelVb._size });
			skelVBIterator += skelVb._size;
		}
		_jointMatrices.resize(jointMatrixBufferCount, Identity<Float3x4>());
		
		_staticVertexAttachments = LoadStaticResourcePartialAsync(
			device, MakeIteratorRange(staticDataLoadRequests), skelVBIterator, _modelScaffold,
			BindFlag::UnorderedAccess,
			(StringMeld<64>() << "[skin]" << modelScaffoldName).AsStringSection()).first;
		_staticVertexAttachmentsView = _staticVertexAttachments->CreateBufferView(BindFlag::UnorderedAccess);

		_jointInputInterface = _modelScaffold->CommandStream().GetInputInterface();
	}

	void GPUSkinDeformer::Bind(SkinDeformerPipelineCollection& pipelineCollection, const DeformerInputBinding& bindings)
	{
		auto& immData = _modelScaffold->ImmutableData();

		for (auto s=_sections.begin(); s!=_sections.end(); ){
			auto start = s;
			++s;
			while (s!=_sections.end() && s->_geoId == start->_geoId) ++s;

			auto binding = std::find_if(bindings._geoBindings.begin(), bindings._geoBindings.end(), [geoId=start->_geoId](const auto& c) { return c._geoId == geoId; });
			if (binding == bindings._geoBindings.end())
				Throw(std::runtime_error("Missing deformer binding for geoId (" + std::to_string(start->_geoId) + ")"));

			unsigned inPositionsOffset = 0, inNormalsOffset = 0, inTangentsOffset = 0;
			unsigned outPositionsOffset = 0, outNormalsOffset = 0, outTangentsOffset = 0;
			ParameterBox selectors;
			for (const auto&ele:binding->_inputElements) {
				assert(ele._inputSlot == Internal::VB_GPUStaticData);
				auto semanticHash = Hash64(ele._semanticName);
				if (semanticHash == CommonSemantics::POSITION && ele._semanticIndex == 0) {
					selectors.SetParameter("IN_POSITION_FORMAT", (unsigned)ele._nativeFormat);
					inPositionsOffset = ele._alignedByteOffset + binding->_bufferOffsets[Internal::VB_GPUStaticData];
				} else if (semanticHash == CommonSemantics::NORMAL && ele._semanticIndex == 0) {
					selectors.SetParameter("IN_NORMAL_FORMAT", (unsigned)ele._nativeFormat);
					inNormalsOffset = ele._alignedByteOffset + binding->_bufferOffsets[Internal::VB_GPUStaticData];
				} else if (semanticHash == CommonSemantics::TEXTANGENT && ele._semanticIndex == 0) {
					selectors.SetParameter("IN_TEXTANGENT_FORMAT", (unsigned)ele._nativeFormat);
					inTangentsOffset = ele._alignedByteOffset + binding->_bufferOffsets[Internal::VB_GPUStaticData];
				} else {
					assert(0);
				}
			}

			for (const auto&ele:binding->_outputElements) {
				assert(ele._inputSlot == Internal::VB_PostDeform);
				auto semanticHash = Hash64(ele._semanticName);
				if (semanticHash == CommonSemantics::POSITION && ele._semanticIndex == 0) {
					selectors.SetParameter("OUT_POSITION_FORMAT", (unsigned)ele._nativeFormat);
					outPositionsOffset = ele._alignedByteOffset + binding->_bufferOffsets[Internal::VB_PostDeform];
				} else if (semanticHash == CommonSemantics::NORMAL && ele._semanticIndex == 0) {
					selectors.SetParameter("OUT_NORMAL_FORMAT", (unsigned)ele._nativeFormat);
					outNormalsOffset = ele._alignedByteOffset + binding->_bufferOffsets[Internal::VB_PostDeform];
				} else if (semanticHash == CommonSemantics::TEXTANGENT && ele._semanticIndex == 0) {
					selectors.SetParameter("OUT_TEXTANGENT_FORMAT", (unsigned)ele._nativeFormat);
					outTangentsOffset = ele._alignedByteOffset + binding->_bufferOffsets[Internal::VB_PostDeform];
				} else {
					assert(0);
				}
			}

			selectors.SetParameter("JOINT_INDICES_TYPE", (unsigned)GetComponentType(start->_indicesFormat));
			selectors.SetParameter("JOINT_INDICES_PRECISION", (unsigned)GetComponentPrecision(start->_indicesFormat));
			selectors.SetParameter("WEIGHTS_TYPE", (unsigned)GetComponentType(start->_weightsFormat));
			selectors.SetParameter("WEIGHTS_PRECISION", (unsigned)GetComponentPrecision(start->_weightsFormat));

			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Hash64("IAParams"));
			usi.BindImmediateData(1, Hash64("JointTransforms"));
			usi.BindResourceView(0, Hash64("StaticVertexAttachments"));
			usi.BindResourceView(1, Hash64("InputAttributes"));
			usi.BindResourceView(2, Hash64("OutputAttributes"));
			
			auto pipelineMarker = pipelineCollection.GetPipelineMarker(selectors, usi);

			for (auto q=start; q!=s; ++q) {
				q->_iaParams._inPositionsOffset = inPositionsOffset;
				q->_iaParams._inNormalsOffset = inNormalsOffset;
				q->_iaParams._inTangentsOffset = inTangentsOffset;
				q->_iaParams._outPositionsOffset = outPositionsOffset;
				q->_iaParams._outNormalsOffset = outNormalsOffset;
				q->_iaParams._outTangentsOffset = outTangentsOffset;
				q->_iaParams._inputStride = binding->_bufferStrides[Internal::VB_GPUStaticData];
				q->_iaParams._outputStride = binding->_bufferStrides[Internal::VB_PostDeform];
				assert(q->_indicesFormat == start->_indicesFormat);
				assert(q->_weightsFormat == start->_weightsFormat);
				q->_pipelineMarker = pipelineMarker;
			}
		}
	}

	void GPUSkinDeformer::StallForPipeline()
	{
		for (auto& section:_sections)
			section._pipelineMarker->StallWhilePending();
	}

	void* GPUSkinDeformer::QueryInterface(size_t typeId)
	{
		if (typeId == typeid(GPUSkinDeformer).hash_code())
			return this;
		else if (typeId == typeid(ISkinDeformer).hash_code())
			return (ISkinDeformer*)this;
		else if (typeId == typeid(IDeformOperator).hash_code())
			return (IDeformOperator*)this;
		return nullptr;
	}

	GPUSkinDeformer::~GPUSkinDeformer()
	{
	}

	uint64_t SkinDeformerPipelineCollection::GetPipeline(const ParameterBox& selectors, const UniformsStreamInterface& usi)
	{
		// note -- no selector filtering done here
		uint64_t hash = HashCombine(selectors.GetHash(), selectors.GetParameterNamesHash());
		hash = HashCombine(usi.GetHash(), hash);

		auto i = LowerBound(_pipelines, hash);
		if (i==_pipelines.end() || i->first != hash) {
			auto operatorMarker = CreateComputeOperator(_pipelineCollection, SKIN_COMPUTE_HLSL ":main", selectors, usi);
			_pipelines.insert(i, std::make_pair(hash, operatorMarker));
		}
		return hash;
	}
	auto SkinDeformerPipelineCollection::GetPipelineMarker(const ParameterBox& selectors, const UniformsStreamInterface& usi) -> PipelineMarkerPtr
	{
		return CreateComputeOperator(_pipelineCollection, SKIN_COMPUTE_HLSL ":main", selectors, usi);
	}
	SkinDeformerPipelineCollection::SkinDeformerPipelineCollection() {}
	SkinDeformerPipelineCollection::~SkinDeformerPipelineCollection() {}

	class GPUSkinDeformerFactory : public IDeformOperationFactory
	{
	public:
		std::shared_ptr<IDeformOperator> Configure(
			std::vector<RenderCore::Techniques::DeformOperationInstantiation>& result,
			StringSection<> initializer,
			std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold,
			const std::string& modelScaffoldName) override
		{
			const std::string positionEleName = "POSITION";
			auto weightsEle = Hash64("WEIGHTS");
			auto jointIndicesEle = Hash64("JOINTINDICES");
			auto& immData = modelScaffold->ImmutableData();
			for (unsigned c=0; c<immData._boundSkinnedControllerCount; ++c) {

				auto& animVB = immData._boundSkinnedControllers[c]._animatedVertexElements;
				auto positionElement = Internal::FindElement(MakeIteratorRange(animVB._ia._elements), "POSITION");
				auto tangentsElement = Internal::FindElement(MakeIteratorRange(animVB._ia._elements), "TEXTANGENT");
				auto normalsElement = Internal::FindElement(MakeIteratorRange(animVB._ia._elements), "NORMAL");
				if (!positionElement)
					Throw(std::runtime_error("Missing animated position in GPU skinning input"));

				DeformOperationInstantiation inst;
				inst._upstreamSourceElements.push_back({"POSITION", 0});
				inst._generatedElements.push_back({positionEleName, 0, positionElement->_nativeFormat});
				if (normalsElement) {
					inst._upstreamSourceElements.push_back({"NORMAL", 0});
					inst._generatedElements.push_back({"NORMAL", 0, normalsElement->_nativeFormat});
				}
				if (tangentsElement) {
					inst._upstreamSourceElements.push_back({"TEXTANGENT", 0});
					inst._generatedElements.push_back({"TEXTANGENT", 0, tangentsElement->_nativeFormat});
				}
				inst._suppressElements = {weightsEle, jointIndicesEle};
				inst._geoId = unsigned(immData._geoCount) + c;
				result.push_back(std::move(inst));
			}
			return std::make_shared<GPUSkinDeformer>(*_device, modelScaffold, modelScaffoldName);
		}

		virtual void Bind(
			IDeformOperator& op,
			const DeformerInputBinding& binding) override
		{
			auto* deformer = checked_cast<GPUSkinDeformer*>(&op);
			deformer->Bind(_pipelineCollection, binding);
		}

		std::shared_ptr<IDevice> _device;
		SkinDeformerPipelineCollection _pipelineCollection;
	};

	std::shared_ptr<IDeformOperationFactory> CreateGPUSkinDeformerFactory(
		std::shared_ptr<IDevice> device,
		std::shared_ptr<PipelineCollection> pipelineCollection)
	{
		auto result = std::make_shared<GPUSkinDeformerFactory>();
		result->_device = device;
		result->_pipelineCollection._pipelineCollection = pipelineCollection;
		return result;
	}

	ISkinDeformer::~ISkinDeformer() {}
}}
