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
		const LargeResourceBlocks& blocks) const
	{
			//  We're going to write the index and vertex buffer data to the "large resources block"
			//  class members and scaffold structure get written to the serialiser, but the very large stuff
			//  should end up in a separate pool

		assert(blocks._vb._size == _vertices.size());
		assert(blocks._ib._size == _indices.size());

		serializer << (uint32_t)Assets::GeoCommand::AttachRawGeometry;
		auto recall = serializer.CreateRecall(sizeof(unsigned));

		SerializationOperator(
			serializer, 
			RenderCore::Assets::VertexData
				{ _mainDrawInputAssembly, unsigned(blocks._vb._offset), unsigned(blocks._vb._size) });

		SerializationOperator(
			serializer, 
			RenderCore::Assets::IndexData
				{ _indexFormat, unsigned(blocks._ib._offset), unsigned(blocks._ib._size) });
		
		SerializationOperator(serializer, _mainDrawCalls);
		SerializationOperator(serializer, _geoSpaceToNodeSpace);

		SerializationOperator(serializer, _finalVertexIndexToOriginalIndex);

		serializer.PushSizeValueAtRecall(recall);
	}

	void NascentRawGeometry::SerializeTopologicalWithResourceBlock(
		::Assets::BlockSerializer& serializer, 
		const LargeResourceBlocks& blocks) const
	{
		// write out a version of RawGeometryDesc that is setup for topological operations
		// ie, index buffer has adjacency information

		assert(blocks._vb._size == _vertices.size());
		assert(blocks._topologicalIb._size == _adjacencyIndices.size());

		serializer << (uint32_t)Assets::GeoCommand::AttachRawGeometry;
		auto recall = serializer.CreateRecall(sizeof(unsigned));

		SerializationOperator(
			serializer, 
			RenderCore::Assets::VertexData
				{ _mainDrawInputAssembly, unsigned(blocks._vb._offset), unsigned(blocks._vb._size) });

		SerializationOperator(
			serializer, 
			RenderCore::Assets::IndexData
				{ _indexFormat, unsigned(blocks._ib._offset), unsigned(blocks._ib._size) });
		
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
		stream << "            VB bytes: " << ByteCount(geo._vertices.size()) << " (" << geo._vertices.size() / std::max(1u, geo._mainDrawInputAssembly._vertexStride) << "*" << geo._mainDrawInputAssembly._vertexStride << ")" << std::endl;
		stream << "            IB bytes: " << ByteCount(geo._indices.size()) << " (" << (geo._indices.size()*8/BitsPerPixel(geo._indexFormat)) << "*" << BitsPerPixel(geo._indexFormat)/8 << ")" << std::endl;
		stream << "Topological IB bytes: " << ByteCount(geo._adjacencyIndices.size()) << " (" << (geo._adjacencyIndices.size()*8/BitsPerPixel(geo._indexFormat)) << "*" << BitsPerPixel(geo._indexFormat)/8 << ")" << std::endl;
		stream << "IA: " << geo._mainDrawInputAssembly << std::endl;
		stream << "Index fmt: " << AsString(geo._indexFormat) << std::endl;
		unsigned c=0;
		for(const auto& dc:geo._mainDrawCalls)
			stream << "Draw [" << c++ << "] " << dc << std::endl;
		stream << "Geo Space To Node Space: "; CompactTransformDescription(stream, geo._geoSpaceToNodeSpace); stream << std::endl;

		return stream;
	}

	auto LargeResourceBlockConstructor::AddBlock(IteratorRange<const void*> data) -> BlockAddress
	{
		if (data.empty()) return {};
		// Check for duplicate pointers... We could do a hash here as well to check for
		// duplicate block data -- but that might be overkill
		size_t iterator = 0;
		for (auto&e:_elements) {
			if (data.begin() == e.begin() && data.end() == e.end())
				return { iterator, data.size() };
			iterator += e.size();
		}
		_elements.emplace_back(data);
		return { iterator, data.size() };
	}

	auto LargeResourceBlockConstructor::AddBlock(std::vector<uint8_t>&& block) -> BlockAddress
	{
		if (block.empty()) return {};
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


