// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SkinDeformer.h"
#include "DeformAcceleratorInternal.h"		// (required for some utility functions)
#include "../Assets/ModelScaffold.h"
#include "../Assets/ModelScaffoldInternal.h"
#include "../Assets/ModelImmutableData.h"
#include "../BufferView.h"
#include "../IDevice.h"
#include "../ResourceDesc.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/AssetTraits.h"
#include <assert.h>

#include "PipelineOperators.h"
#include "CommonBindings.h"
#include "../UniformsStream.h"
#include "../../Assets/Marker.h"
#include "../../Utility/ParameterBox.h"

namespace RenderCore { namespace Techniques
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void SkinDeformer::WriteJointTransforms(
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

	RenderCore::Assets::SkeletonBinding SkinDeformer::CreateBinding(
		const RenderCore::Assets::SkeletonMachine::OutputInterface& skeletonMachineOutputInterface) const
	{
		return RenderCore::Assets::SkeletonBinding{skeletonMachineOutputInterface, _jointInputInterface};
	}

	void SkinDeformer::FeedInSkeletonMachineResults(
		unsigned instanceIdx,
		IteratorRange<const Float4x4*> skeletonMachineOutput,
		const RenderCore::Assets::SkeletonBinding& binding)
	{
		_skeletonMachineOutput.clear();
		_skeletonMachineOutput.insert(_skeletonMachineOutput.end(), skeletonMachineOutput.begin(), skeletonMachineOutput.end());
		_skeletonBinding = binding;
	}

	void SkinDeformer::Execute(
		unsigned instanceId,
		IteratorRange<const VertexElementRange*> sourceElements,
		IteratorRange<const VertexElementRange*> destinationElements) const
	{
		assert(destinationElements.size() == 1);

		auto& inputPosElement = sourceElements[0];
		auto& outputPosElement = destinationElements[0];
		assert(inputPosElement.begin().Format() == Format::R32G32B32_FLOAT);
		assert(outputPosElement.begin().Format() == Format::R32G32B32_FLOAT);
		assert(outputPosElement.size() <= inputPosElement.size());

		for (const auto&section:_sections) {
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

	SkinDeformer::SkinDeformer(
		const RenderCore::Assets::ModelScaffold& modelScaffold,
		unsigned geoId)
	{
		auto& immData = modelScaffold.ImmutableData();
		assert(geoId < immData._boundSkinnedControllerCount);
		auto& skinnedController = immData._boundSkinnedControllers[geoId];
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
			_sections.push_back(section);
		}

		_jointInputInterface = modelScaffold.CommandStream().GetInputInterface();
	}

	void* SkinDeformer::QueryInterface(size_t typeId)
	{
		if (typeId == typeid(SkinDeformer).hash_code())
			return this;
		else if (typeId == typeid(IDeformOperation).hash_code())
			return (IDeformOperation*)this;
		return nullptr;
	}

	SkinDeformer::~SkinDeformer()
	{
	}

	std::vector<RenderCore::Techniques::DeformOperationInstantiation> SkinDeformer::InstantiationFunction(
		StringSection<> initializer,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold)
	{
		// auto sep = std::find(initializer.begin(), initializer.end(), ',');
		// assert(sep != initializer.end());

		const std::string positionEleName = "POSITION";
		auto weightsEle = Hash64("WEIGHTS");
		auto jointIndicesEle = Hash64("JOINTINDICES");
		std::vector<DeformOperationInstantiation> result;
		auto& immData = modelScaffold->ImmutableData();
		for (unsigned c=0; c<immData._boundSkinnedControllerCount; ++c) {

			// skeleton & anim set:
			//			StringSection<>(initializer.begin(), sep),
			//			StringSection<>(sep+1, initializer.end()

			result.emplace_back(
				DeformOperationInstantiation {
					std::make_shared<SkinDeformer>(*modelScaffold, c),
					unsigned(immData._geoCount) + c,
					{DeformOperationInstantiation::SemanticNameAndFormat{positionEleName, 0, Format::R32G32B32_FLOAT}},
					{DeformOperationInstantiation::SemanticName{positionEleName, 0}},
					{weightsEle, jointIndicesEle}
				});
		}

		return result;
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

	static const unsigned s_wavegroupWidth = 64;

	void GPUSkinDeformer::ExecuteGPU(
		IThreadContext& threadContext,
		unsigned instanceId,
		const IResourceView& sourceElements,
		const IResourceView& destinationElements) const
	{
		_operator->StallWhilePending();
		auto op = _operator->Actualize();

		struct InvocationParams
		{
			unsigned _vertexCount, _firstVertex, _softInfluenceCount, _firstJointTransform;
		};

		const IResourceView* rvs[] { _staticVertexAttachmentsView.get(), &sourceElements, &destinationElements };
		UniformsStream::ImmediateData immDatas[] {
			MakeOpaqueIteratorRange(_iaParams),
			MakeIteratorRange(_jointMatrices)
		};
		UniformsStream us;
		us._resourceViews = MakeIteratorRange(rvs);
		us._immediateData = MakeIteratorRange(immDatas);

		op->BeginDispatches(threadContext, us, {}, Hash64("InvocationParams"));

		for (const auto&section:_sections) {
			for (const auto&drawCall:section._preskinningDrawCalls) {
				assert(drawCall._firstIndex == ~unsigned(0x0));		// avoid confusion; this isn't used for anything
				InvocationParams invocationParams { drawCall._indexCount, drawCall._firstVertex, drawCall._subMaterialIndex, section._rangeInJointMatrices.first };
				op->Dispatch((drawCall._indexCount+s_wavegroupWidth-1)/s_wavegroupWidth, 1, 1, MakeOpaqueIteratorRange(invocationParams));
			}
		}

		op->EndDispatches();
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
		std::shared_ptr<RenderCore::Techniques::PipelineCollection>& pipelinePool,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		unsigned geoId)
	: _modelScaffold(modelScaffold)			// we take internal pointers so preserve lifetime
	{
		auto& immData = modelScaffold->ImmutableData();
		assert(geoId < immData._boundSkinnedControllerCount);
		auto& skinnedController = immData._boundSkinnedControllers[geoId];
		auto& skelVb = skinnedController._skeletonBinding;

		auto skelVbData = std::make_unique<uint8_t[]>(skelVb._size);
		{
			auto largeBlocks = modelScaffold->OpenLargeBlocks();
			auto base = largeBlocks->TellP();

			largeBlocks->Seek(base + skelVb._offset);
			largeBlocks->Read(skelVbData.get(), skelVb._size);
		}

		_influencesPerVertex = 0;
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
				assert(weightsElement->_alignedByteOffset == weightsOffset + _influencesPerVertex*weightsBitsPerComponent/8);
				assert(jointIndicesElement->_alignedByteOffset == indicesOffset + _influencesPerVertex*indicesBitsPerComponent/8);
			}
			assert(GetComponentCount(GetComponents(weightsElement->_nativeFormat)) == GetComponentCount(GetComponents(jointIndicesElement->_nativeFormat)));
			_influencesPerVertex += GetComponentCount(GetComponents(weightsElement->_nativeFormat));
			++parrallelElementsCount;
		}

		if (weightsOffset == ~0u || indicesOffset == ~0u)
			Throw(std::runtime_error("Could not create SkinDeformer because there is no position, weights and/or joint indices element in input geometry"));
		if ((skelVBStride%4)!=0 || (weightsOffset%4)!=0 || (indicesOffset%4)!=0)
			Throw(std::runtime_error("Could not create SkinDeformer because input skeleton binding data is not correctly aligned"));

		unsigned vertexCount = skelVb._size / skelVb._ia._vertexStride;
		assert(vertexCount > 0);
		assert((_influencesPerVertex%1) == 0);		// must be a multiple of 2
	
		/*{
			auto buffer = ReconfigureStaticVertexAttachmentsBuffer(
				_influencesPerVertex, vertexCount, parrallelElementsCount, 
				skelVb, MakeIteratorRange(skelVbData.get(), PtrAdd(skelVbData.get(), skelVb._size)));
			_staticVertexAttachments = device.CreateResource(
				CreateDesc(
					BindFlag::UnorderedAccess, 0, GPUAccess::Read,
					LinearBufferDesc::Create(buffer.size()),
					"SkinDeformer-binding"),
				SubResourceInitData{MakeIteratorRange(buffer)});
			_staticVertexAttachmentsView = _staticVertexAttachments->CreateBufferView(BindFlag::UnorderedAccess);
		}
		*/

		_staticVertexAttachments = device.CreateResource(
			CreateDesc(
				BindFlag::UnorderedAccess, 0, GPUAccess::Read,
				LinearBufferDesc::Create(skelVb._size),
				"SkinDeformer-binding"),
			SubResourceInitData{MakeIteratorRange(skelVbData.get(), PtrAdd(skelVbData.get(), skelVb._size))});
		_staticVertexAttachmentsView = _staticVertexAttachments->CreateBufferView(BindFlag::UnorderedAccess);

		unsigned jointMatrixBufferCount = 0;
		_sections.reserve(skinnedController._preskinningSections.size());
		for (const auto&sourceSection:skinnedController._preskinningSections) {
			Section section;
			section._preskinningDrawCalls = MakeIteratorRange(sourceSection._preskinningDrawCalls);
			section._bindShapeByInverseBindMatrices = MakeIteratorRange(sourceSection._bindShapeByInverseBindMatrices);
			section._jointMatrices = { sourceSection._jointMatrices, sourceSection._jointMatrices + sourceSection._jointMatrixCount };
			section._rangeInJointMatrices = { jointMatrixBufferCount, jointMatrixBufferCount + (unsigned)sourceSection._jointMatrixCount };
			_sections.push_back(section);
			jointMatrixBufferCount += sourceSection._jointMatrixCount;
		}
		_jointMatrices.resize(jointMatrixBufferCount, Identity<Float3x4>());

		_jointInputInterface = modelScaffold->CommandStream().GetInputInterface();

		auto& animIA = skinnedController._animatedVertexElements._ia;
		auto& postAnimIA = skinnedController._vb._ia;

		_iaParams = {0};

		ParameterBox selectors;
		for (const auto&ele:animIA._elements) {
			auto semanticHash = Hash64(ele._semanticName);
			if (semanticHash == CommonSemantics::POSITION && ele._semanticIndex == 0) {
				selectors.SetParameter("POSITION_FORMAT", (unsigned)ele._nativeFormat);
				_iaParams._positionsOffset = ele._alignedByteOffset;
			} else if (semanticHash == CommonSemantics::NORMAL && ele._semanticIndex == 0) {
				selectors.SetParameter("NORMAL_FORMAT", (unsigned)ele._nativeFormat);
				_iaParams._normalsOffset = ele._alignedByteOffset;
			} else if (semanticHash == CommonSemantics::TEXTANGENT && ele._semanticIndex == 0) {
				selectors.SetParameter("TEXTANGENT_FORMAT", (unsigned)ele._nativeFormat);
				_iaParams._tangentsOffset = ele._alignedByteOffset;
			} else
				Throw(std::runtime_error(""));
		}
		_iaParams._inputStride = _iaParams._outputStride = animIA._vertexStride;

		_iaParams._weightsOffset = weightsOffset;
		_iaParams._jointIndicesOffset = indicesOffset;
		_iaParams._staticVertexAttachmentsStride = skelVBStride;

		selectors.SetParameter("INFLUENCE_COUNT", _influencesPerVertex);
		selectors.SetParameter("JOINT_INDICES_TYPE", (unsigned)GetComponentType(indicesFormat));
		selectors.SetParameter("JOINT_INDICES_PRECISION", (unsigned)GetComponentPrecision(indicesFormat));
		selectors.SetParameter("WEIGHTS_TYPE", (unsigned)GetComponentType(weightsFormat));
		selectors.SetParameter("WEIGHTS_PRECISION", (unsigned)GetComponentPrecision(weightsFormat));

		UniformsStreamInterface usi;
		usi.BindImmediateData(0, Hash64("IAParams"));
		usi.BindImmediateData(1, Hash64("JointTransforms"));
		usi.BindResourceView(0, Hash64("StaticVertexAttachments"));
		usi.BindResourceView(1, Hash64("InputAttributes"));
		usi.BindResourceView(2, Hash64("OutputAttributes"));
		
		_operator = CreateComputeOperator(pipelinePool, "xleres/Deform/skin.compute.hlsl:main", selectors, usi);
	}

	void* GPUSkinDeformer::QueryInterface(size_t typeId)
	{
		if (typeId == typeid(GPUSkinDeformer).hash_code())
			return this;
		else if (typeId == typeid(IDeformOperation).hash_code())
			return (IDeformOperation*)this;
		return nullptr;
	}

	GPUSkinDeformer::~GPUSkinDeformer()
	{
	}

	std::vector<RenderCore::Techniques::DeformOperationInstantiation> GPUSkinDeformer::InstantiationFunction(
		StringSection<> initializer,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold)
	{
#if 0
		// auto sep = std::find(initializer.begin(), initializer.end(), ',');
		// assert(sep != initializer.end());
		// skeleton & anim set:
		//	StringSection<>(initializer.begin(), sep),
		//	StringSection<>(sep+1, initializer.end()

		const std::string positionEleName = "POSITION";
		auto weightsEle = Hash64("WEIGHTS");
		auto jointIndicesEle = Hash64("JOINTINDICES");
		std::vector<DeformOperationInstantiation> result;
		auto& immData = modelScaffold->ImmutableData();
		for (unsigned c=0; c<immData._boundSkinnedControllerCount; ++c) {
			result.emplace_back(
				DeformOperationInstantiation {
					std::make_shared<GPUSkinDeformer>(*modelScaffold, c),
					unsigned(immData._geoCount) + c,
					{DeformOperationInstantiation::NameAndFormat{positionEleName, 0, Format::R32G32B32_FLOAT}},
					{DeformOperationInstantiation::NameAndFormat{positionEleName, 0, Format::R32G32B32_FLOAT}},
					{weightsEle, jointIndicesEle}
				});
		}

		return result;
#endif
		return {};
	}


	void IDeformOperation::Execute(
		unsigned instanceIdx,
		IteratorRange<const VertexElementRange*> sourceElements,
		IteratorRange<const VertexElementRange*> destinationElements) const {}
	void IDeformOperation::ExecuteGPU(
		IThreadContext& threadContext,
		unsigned instanceIdx,
		const IResourceView& sourceElements,
		const IResourceView& destinationElements) const {}

}}
