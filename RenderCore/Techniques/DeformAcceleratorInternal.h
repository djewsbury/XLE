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
		static constexpr unsigned VB_CPUTemporaryDeform = 1;
		static constexpr unsigned VB_GPUTemporaryDeform = 2;
		static constexpr unsigned VB_PostDeform = 3;

		struct SourceDataTransform
		{
			unsigned	_geoId;
			uint64_t	_sourceStream;
			Format		_targetFormat;
			unsigned	_targetOffset;
			unsigned	_targetStride;
			unsigned	_vertexCount;
		};

		struct NascentDeformForGeo
		{
			struct CPUOp
			{
				std::shared_ptr<ICPUDeformOperator> _deformOp;
				struct Attribute { Format _format = Format(0); unsigned _offset = 0; unsigned _stride = 0; unsigned _vbIdx = ~0u; };
				std::vector<Attribute> _inputElements;
				std::vector<Attribute> _outputElements;
			};

			std::vector<CPUOp> _cpuOps;
			std::vector<std::future<std::shared_ptr<IGPUDeformOperator>>> _gpuOps;

			RendererGeoDeformInterface _rendererInterf;
			std::vector<SourceDataTransform> _cpuStaticDataLoadRequests;
			std::optional<std::pair<unsigned, unsigned>> _gpuStaticDataRange;

			unsigned _vbOffsets[4] = {0,0,0,0};
			unsigned _vbSizes[4] = {0,0,0,0};
		};

		NascentDeformForGeo BuildNascentDeformForGeo(
			IteratorRange<const DeformOperationInstantiation*> globalDeformAttachments,
			InputLayout srcVBLayout,
			unsigned geoId,
			unsigned vertexCount,
			unsigned& preDeformStaticDataVBIterator,
			unsigned& deformTemporaryGPUVBIterator,
			unsigned& deformTemporaryCPUVBIterator,
			unsigned& postDeformVBIterator);

		static NascentDeformForGeo::CPUOp::Attribute AsCPUOpAttribute(const InputElementDesc& e, unsigned baseOffset, unsigned stride, unsigned inputSlot)
		{
			return {e._nativeFormat, baseOffset + e._alignedByteOffset, stride, inputSlot};
		}

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

