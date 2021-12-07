// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "SimpleModelDeform.h"
#include "../Assets/ModelScaffoldInternal.h"
#include "../Format.h"
#include "../Types.h"
#include <memory>
#include <vector>
#include <future>
#include <optional>

namespace RenderCore { namespace Techniques 
{
	namespace Internal
	{
		static constexpr unsigned VB_CPUStaticData = 0;
		static constexpr unsigned VB_GPUStaticData = 1;
		static constexpr unsigned VB_CPUDeformTemporaries = 2;
		static constexpr unsigned VB_GPUDeformTemporaries = 3;
		static constexpr unsigned VB_PostDeform = 4;
		static constexpr unsigned VB_Count = 5;

		class DeformerInputBindingHelper
		{
		public:
			DeformerInputBinding _inputBinding;

			using VertexElementRange = IteratorRange<RenderCore::VertexElementIterator>;
			const DeformerInputBinding::GeoBinding* CalculateRanges(
				IteratorRange<VertexElementRange*> sourceElements,
				IteratorRange<VertexElementRange*> destinationElements,
				unsigned geoId,
				IteratorRange<const void*> srcVB,
				IteratorRange<const void*> deformTemporariesVB,
				IteratorRange<const void*> dstVB) const;
		};

		struct SourceDataTransform
		{
			unsigned	_geoId;
			uint64_t	_sourceStream;
			Format		_targetFormat;
			unsigned	_targetOffset;
			unsigned	_targetStride;
			unsigned	_vertexCount;
		};

		struct WorkingDeformer
		{
			IteratorRange<const DeformOperationInstantiation*> _instantiations;
			DeformerInputBinding _inputBinding;
		};

		struct DeformBufferIterators
		{
			unsigned _bufferIterators[VB_Count] = {0,0,0,0,0};
			std::vector<SourceDataTransform> _cpuStaticDataLoadRequests;
			std::vector<std::pair<unsigned, unsigned>> _gpuStaticDataLoadRequests;
		};

		DeformerToRendererBinding CreateDeformBindings(
			IteratorRange<WorkingDeformer*> workingDeformers,
			DeformBufferIterators& bufferIterators,
			bool isCPUDeformer,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
			const std::string& modelScaffoldName);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		static const RenderCore::Assets::VertexElement* FindElement(IteratorRange<const RenderCore::Assets::VertexElement*> ele, uint64_t semanticHash)
		{
			return std::find_if(
				ele.begin(), ele.end(),
				[semanticHash](const RenderCore::Assets::VertexElement& ele) {
					return (Hash64(ele._semanticName) + ele._semanticIndex) == semanticHash;
				});
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

		static IteratorRange<VertexElementIterator> AsVertexElementIteratorRange(
			IteratorRange<void*> vbData,
			const RenderCore::Assets::VertexElement& ele,
			unsigned vertexStride)
		{
			unsigned vCount = vbData.size() / vertexStride;
			auto beginPtr = PtrAdd(vbData.begin(), ele._alignedByteOffset);		
			auto endPtr = PtrAdd(vbData.begin(), ele._alignedByteOffset + vertexStride*vCount);
			VertexElementIterator begin {
				MakeIteratorRange(beginPtr, endPtr),
				vertexStride, ele._nativeFormat };
			VertexElementIterator end {
				MakeIteratorRange(endPtr, endPtr),
				vertexStride, ele._nativeFormat };
			return { begin, end };
		}

		static IteratorRange<VertexElementIterator> AsVertexElementIteratorRange(
			IteratorRange<void*> vbData,
			Format format,
			unsigned byteOffset,
			unsigned vertexStride)
		{
			unsigned vCount = vbData.size() / vertexStride;
			auto beginPtr = PtrAdd(vbData.begin(), byteOffset);
			auto endPtr = PtrAdd(vbData.begin(), byteOffset + vertexStride*vCount);
			VertexElementIterator begin { MakeIteratorRange(beginPtr, endPtr), vertexStride, format };
			VertexElementIterator end { MakeIteratorRange(endPtr, endPtr), vertexStride, format };
			return { begin, end };
		}
	}
}}

