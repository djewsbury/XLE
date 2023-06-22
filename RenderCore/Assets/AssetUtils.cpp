// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AssetUtils.h"
#include "ModelMachine.h"
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
			stream << e._semanticName << "[" << e._semanticIndex << "] " << AsString(e._format);
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
			VertexElement ele;
			XlZeroMemory(ele);     // make sure unused space is 0
			XlCopyNString(ele._semanticName, AsPointer(i->_semanticName.begin()), i->_semanticName.size());
			ele._semanticName[dimof(ele._semanticName)-1] = '\0';
			ele._semanticIndex = i->_semanticIndex;
			ele._format = i->_nativeFormat;
			ele._alignedByteOffset = i->_alignedByteOffset;
			result._elements.push_back(ele);
		}
		return result;
	}

	unsigned BuildLowLevelInputAssembly(
		IteratorRange<InputElementDesc*> dst,
		IteratorRange<const VertexElement*> source,
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
					sourceElement._format, lowLevelSlot, sourceElement._alignedByteOffset);
			}
		}
		return vertexElementCount;
	}

	std::vector<MiniInputElementDesc> BuildLowLevelInputAssembly(IteratorRange<const VertexElement*> source)
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
				MiniInputElementDesc{Hash64(sourceElement._semanticName) + sourceElement._semanticIndex, sourceElement._format});
		}
		return result;
	}

	Assets::VertexElement FindPositionElement(IteratorRange<const VertexElement*> elements)
	{
		for (unsigned c=0; c<elements.size(); ++c)
			if (elements[c]._semanticIndex == 0 && !XlCompareStringI(elements[c]._semanticName, "POSITION"))
				return elements[c];
		return Assets::VertexElement();
	}

	uint64_t GeoInputAssembly::BuildHash() const
	{
			//  Build a hash for this object.
			//  Note that we should be careful that we don't get an
			//  noise from characters in the left-over space in the
			//  semantic names. Do to this right, we should make sure
			//  that left over space has no effect.
		auto elementsHash = Hash64(AsPointer(_elements.cbegin()), AsPointer(_elements.cend()));
		elementsHash ^= uint64_t(_vertexStride);
		return elementsHash;
	}

	template<typename IndexType>
		void FlipIndexBufferWinding(IteratorRange<IndexType*> indices, Topology topology)
	{
		if (topology != Topology::TriangleList)
			Throw(std::runtime_error("Only triangle list topology type supported in FlipIndexBufferWinding"));

		assert((indices.size() % 3) == 0);
		for (auto i=indices.begin(); (i+3)<=indices.end(); i+=3)
			std::swap(i[1], i[2]);
	}

	template void FlipIndexBufferWinding(IteratorRange<uint8_t*>, Topology);
	template void FlipIndexBufferWinding(IteratorRange<uint16_t*>, Topology);
	template void FlipIndexBufferWinding(IteratorRange<uint32_t*>, Topology);
}}

