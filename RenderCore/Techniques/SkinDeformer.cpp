// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SkinDeformer.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/ModelScaffoldInternal.h"
#include "../Assets/ModelImmutableData.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/AssetTraits.h"
#include <assert.h>

namespace RenderCore { namespace Techniques
{
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

	static IteratorRange<VertexElementIterator> AsVertexElementIteratorRange(
		IteratorRange<void*> vbData,
		const RenderCore::Assets::VertexElement& ele,
		unsigned vertexStride)
	{
		VertexElementIterator begin {
			MakeIteratorRange(PtrAdd(vbData.begin(), ele._alignedByteOffset), AsPointer(vbData.end())),
			vertexStride, ele._nativeFormat };
		VertexElementIterator end {
			MakeIteratorRange(AsPointer(vbData.end()), AsPointer(vbData.end())),
			vertexStride, ele._nativeFormat };
		return { begin, end };
	}

	static const RenderCore::Assets::VertexElement* FindElement(
		IteratorRange<const RenderCore::Assets::VertexElement*> ele,
		StringSection<> semantic, unsigned semanticIndex = 0)
	{
		auto i = std::find_if(
			ele.begin(), ele.end(),
			[semantic, semanticIndex](const RenderCore::Assets::VertexElement& ele) {
				return XlEqString(semantic, ele._semanticName) && ele._semanticIndex == semanticIndex;
			});
		if (i==ele.end())
			return nullptr;
		return i;
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
			auto weightsElement = FindElement(MakeIteratorRange(skelVb._ia._elements), "WEIGHTS", c);
			auto jointIndicesElement = FindElement(MakeIteratorRange(skelVb._ia._elements), "JOINTINDICES", c);
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
				auto weightsElement = FindElement(MakeIteratorRange(skelVb._ia._elements), "WEIGHTS", c);
				auto jointIndicesElement = FindElement(MakeIteratorRange(skelVb._ia._elements), "JOINTINDICES", c);
				assert(weightsElement && jointIndicesElement);

				auto subWeights = AsFloat4s(AsVertexElementIteratorRange(MakeIteratorRange(skelVbData.get(), PtrAdd(skelVbData.get(), skelVb._size)), *weightsElement, skelVb._ia._vertexStride));
				auto subJoints = AsUInt4s(AsVertexElementIteratorRange(MakeIteratorRange(skelVbData.get(), PtrAdd(skelVbData.get(), skelVb._size)), *jointIndicesElement, skelVb._ia._vertexStride));
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
					{DeformOperationInstantiation::NameAndFormat{positionEleName, 0, Format::R32G32B32_FLOAT}},
					{DeformOperationInstantiation::NameAndFormat{positionEleName, 0, Format::R32G32B32_FLOAT}},
					{weightsEle, jointIndicesEle}
				});
		}

		return result;
	}

}}
