// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AssetUtils.h"
#include "ModelScaffoldInternal.h"
#include "../Types.h"
#include "../Format.h"
#include "../StateDesc.h"

namespace RenderCore { namespace Assets
{
	std::ostream& SerializationOperator(std::ostream& stream, const GeoInputAssembly& ia)
	{
		stream << "Stride: " << ia._vertexStride << ": ";
		for (size_t c=0; c<ia._elements.size(); c++) {
			if (c != 0) stream << ", ";
			const auto& e = ia._elements[c];
			stream << e._semanticName << "[" << e._semanticIndex << "] " << AsString(e._nativeFormat);
		}
		return stream;
	}

	std::ostream& SerializationOperator(std::ostream& stream, const DrawCallDesc& dc)
	{
		stream << "{ [" << AsString(dc._topology) << "] idxCount: " << dc._indexCount;
		if (dc._firstIndex)
			stream << ", firstIdx: " << dc._firstIndex;
		stream << ", topology: " << AsString(dc._topology);
		stream << " }";
		return stream;
	}

	GeoInputAssembly CreateGeoInputAssembly(   
		const std::vector<InputElementDesc>& vertexInputLayout,
		unsigned vertexStride)
	{ 
		GeoInputAssembly result;
		result._vertexStride = vertexStride;
		result._elements.reserve(vertexInputLayout.size());
		for (auto i=vertexInputLayout.begin(); i!=vertexInputLayout.end(); ++i) {
			RenderCore::Assets::VertexElement ele;
			XlZeroMemory(ele);     // make sure unused space is 0
			XlCopyNString(ele._semanticName, AsPointer(i->_semanticName.begin()), i->_semanticName.size());
			ele._semanticName[dimof(ele._semanticName)-1] = '\0';
			ele._semanticIndex = i->_semanticIndex;
			ele._nativeFormat = i->_nativeFormat;
			ele._alignedByteOffset = i->_alignedByteOffset;
			result._elements.push_back(ele);
		}
		return std::move(result);
	}

	unsigned BuildLowLevelInputAssembly(
		IteratorRange<InputElementDesc*> dst,
		IteratorRange<const RenderCore::Assets::VertexElement*> source,
		unsigned lowLevelSlot)
	{
		unsigned vertexElementCount = 0;
		for (unsigned i=0; i<source.size(); ++i) {
			auto& sourceElement = source[i];
			assert((vertexElementCount+1) <= dst.size());
			if ((vertexElementCount+1) <= dst.size()) {
					// in some cases we need multiple "slots". When we have multiple slots, the vertex data 
					//  should be one after another in the vb (that is, not interleaved)
				dst[vertexElementCount++] = InputElementDesc(
					sourceElement._semanticName, sourceElement._semanticIndex,
					sourceElement._nativeFormat, lowLevelSlot, sourceElement._alignedByteOffset);
			}
		}
		return vertexElementCount;
	}

	std::vector<MiniInputElementDesc> BuildLowLevelInputAssembly(IteratorRange<const RenderCore::Assets::VertexElement*> source)
	{
		std::vector<MiniInputElementDesc> result;
		result.reserve(source.size());
		for (unsigned i=0; i<source.size(); ++i) {
			auto& sourceElement = source[i];
			#if defined(_DEBUG)
				auto expectedOffset = CalculateVertexStride(MakeIteratorRange(result), false);
				assert(expectedOffset == sourceElement._alignedByteOffset);
			#endif
			result.push_back(
				MiniInputElementDesc{Hash64(sourceElement._semanticName) + sourceElement._semanticIndex, sourceElement._nativeFormat});
		}
		return result;
	}
}}

