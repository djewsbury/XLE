// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/ModelMachine.h"
#include "../Format.h"
#include "../../Math/Matrix.h"
#include <vector>

namespace Assets { class BlockSerializer; }

namespace RenderCore { namespace Assets { namespace GeoProc
{
	class LargeResourceBlockConstructor
	{
	public:
		using Element = IteratorRange<const void*>;
		std::vector<Element> _elements;
		size_t AddBlock(IteratorRange<const void*>);		// given block must outlive this instance
		size_t AddBlock(std::vector<uint8_t>&&);
		size_t CalculateSize() const; 
	protected:
		std::vector<std::vector<uint8_t>> _retainedBlocks;
	};

	class NascentRawGeometry
	{
	public:
		std::vector<uint8_t>		_vertices;
		std::vector<uint8_t>		_indices;

		GeoInputAssembly            _mainDrawInputAssembly;
		Format                      _indexFormat = Format(0);
		std::vector<DrawCallDesc>   _mainDrawCalls;

		Float4x4 _geoSpaceToNodeSpace = Identity<Float4x4>();

			//  Only required during processing
		size_t						_finalVertexCount;
		std::vector<uint32_t>		_finalVertexIndexToOriginalIndex;

		std::vector<uint8_t>		_adjacencyIndexBuffer;

		void SerializeWithResourceBlock(
			::Assets::BlockSerializer& outputSerializer, 
			LargeResourceBlockConstructor& largeResourcesBlock) const;

		void SerializeTopologicalWithResourceBlock(
			::Assets::BlockSerializer& outputSerializer, 
			LargeResourceBlockConstructor& largeResourcesBlock) const;

		friend std::ostream& SerializationOperator(std::ostream&, const NascentRawGeometry&);
	};

}}}

