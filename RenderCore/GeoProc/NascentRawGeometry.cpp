// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NascentRawGeometry.h"
#include "GeometryAlgorithm.h"
#include "../Format.h"
#include "../Assets/AssetUtils.h"
#include "../Assets/ModelMachine.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Math/MathSerialization.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StreamUtils.h"
#include "../../Utility/Streams/SerializationUtils.h"

namespace RenderCore { namespace Assets { namespace GeoProc
{
	void NascentRawGeometry::SerializeWithResourceBlock(
		::Assets::BlockSerializer& serializer, 
		LargeResourceBlockConstructor& largeResourcesBlock) const
	{
			//  We're going to write the index and vertex buffer data to the "large resources block"
			//  class members and scaffold structure get written to the serialiser, but the very large stuff
			//  should end up in a separate pool

		auto vbOffset = largeResourcesBlock.AddBlock(_vertices);
		auto vbSize = _vertices.size();

		auto ibOffset = largeResourcesBlock.AddBlock(_indices);
		auto ibSize = _indices.size();

		serializer << (uint32_t)Assets::GeoCommand::AttachRawGeometry;
		auto recall = serializer.CreateRecall(sizeof(unsigned));

		SerializationOperator(
			serializer, 
			RenderCore::Assets::VertexData 
				{ _mainDrawInputAssembly, unsigned(vbOffset), unsigned(vbSize) });

		SerializationOperator(
			serializer, 
			RenderCore::Assets::IndexData 
				{ _indexFormat, unsigned(ibOffset), unsigned(ibSize) });
		
		SerializationOperator(serializer, _mainDrawCalls);
		SerializationOperator(serializer, _geoSpaceToNodeSpace);

		SerializationOperator(serializer, _finalVertexIndexToOriginalIndex);

		serializer.PushSizeValueAtRecall(recall);
	}

	void NascentRawGeometry::SerializeTopologicalWithResourceBlock(
		::Assets::BlockSerializer& serializer, 
		LargeResourceBlockConstructor& largeResourcesBlock) const
	{
		// write out a version of RawGeometryDesc that is setup for topological operations
		// ie, index buffer has adjacency information

		auto vbOffset = largeResourcesBlock.AddBlock(_vertices);
		auto vbSize = _vertices.size();

		std::vector<uint8_t> adjacencyIndexBuffer;
		auto adjacencyIndexFormat = Format::R32_UINT;

		if (_indexFormat == Format::R32_UINT) {
			adjacencyIndexBuffer.resize(_indices.size()*2);
			TriListToTriListWithAdjacency(
				MakeIteratorRange((unsigned*)AsPointer(adjacencyIndexBuffer.begin()), (unsigned*)AsPointer(adjacencyIndexBuffer.end())),
				MakeIteratorRange((const unsigned*)AsPointer(_indices.begin()), (const unsigned*)AsPointer(_indices.end())));
		} else if (_indexFormat == Format::R16_UINT) {
			std::vector<unsigned> largeIndices { (const uint16_t*)AsPointer(_indices.begin()), (const uint16_t*)AsPointer(_indices.end()) };
			adjacencyIndexBuffer.resize(largeIndices.size()*2*sizeof(unsigned));
			TriListToTriListWithAdjacency(
				MakeIteratorRange((unsigned*)AsPointer(adjacencyIndexBuffer.begin()), (unsigned*)AsPointer(adjacencyIndexBuffer.end())),
				MakeIteratorRange(largeIndices));
		} else
			Throw(std::runtime_error("Unsupported index format in SerializeTopologicalWithResourceBlock"));

		auto ibSize = adjacencyIndexBuffer.size();
		auto ibOffset = largeResourcesBlock.AddBlock(std::move(adjacencyIndexBuffer));

		serializer << (uint32_t)Assets::GeoCommand::AttachRawGeometry;
		auto recall = serializer.CreateRecall(sizeof(unsigned));

		SerializationOperator(
			serializer, 
			RenderCore::Assets::VertexData 
				{ _mainDrawInputAssembly, unsigned(vbOffset), unsigned(vbSize) });

		SerializationOperator(
			serializer, 
			RenderCore::Assets::IndexData 
				{ adjacencyIndexFormat, unsigned(ibOffset), unsigned(ibSize) });
		
		auto adjustedDrawCalls = _mainDrawCalls;
		for (auto& a:adjustedDrawCalls) {
			assert(a._topology == Topology::TriangleList);
			a._topology = Topology::TriangleListWithAdjacency;
			// _firstIndex, _indexCount doubled by addition of adjacency
			a._firstIndex *= 2;
			a._indexCount *= 2;
		}
		SerializationOperator(serializer, adjustedDrawCalls);
		SerializationOperator(serializer, _geoSpaceToNodeSpace);

		std::vector<uint32_t> dummyMapping;
		SerializationOperator(serializer, dummyMapping);

		serializer.PushSizeValueAtRecall(recall);
	}

	std::ostream& SerializationOperator(std::ostream& stream, const NascentRawGeometry& geo)
	{
		stream << "Vertex bytes: " << ByteCount(geo._vertices.size()) << std::endl;
		stream << "Index bytes: " << ByteCount(geo._indices.size()) << std::endl;
		stream << "IA: " << geo._mainDrawInputAssembly << std::endl;
		stream << "Index fmt: " << AsString(geo._indexFormat) << std::endl;
		unsigned c=0;
		for(const auto& dc:geo._mainDrawCalls) {
			stream << "Draw [" << c++ << "] " << dc << std::endl;
		}
		stream << std::endl;
		stream << "Geo Space To Node Space: " << geo._geoSpaceToNodeSpace << std::endl;

		return stream;
	}

	size_t LargeResourceBlockConstructor::AddBlock(IteratorRange<const void*> data)
	{
		// Check for duplicate pointers... We could do a hash here as well to check for
		// duplicate block data -- but that might be overkill
		size_t iterator = 0;
		for (auto&e:_elements) {
			if (data.begin() == e.begin() && data.end() == e.end())
				return iterator;
			iterator += e.size();
		}
		_elements.emplace_back(data);
		return iterator;
	}

	size_t LargeResourceBlockConstructor::AddBlock(std::vector<uint8_t>&& block)
	{
		auto result = AddBlock(MakeIteratorRange(block));
		_retainedBlocks.emplace_back(std::move(block));			// just hold onto this so the pointers in _elements don't go out of date
		return result;
	}

	size_t LargeResourceBlockConstructor::CalculateSize() const
	{
		size_t iterator = 0;
		for (auto&e:_elements) iterator += e.size();
		return iterator;
	}

}}}


