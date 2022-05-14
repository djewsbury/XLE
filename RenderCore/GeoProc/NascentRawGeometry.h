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

		void SerializeWithResourceBlock(
			::Assets::BlockSerializer& outputSerializer, 
			std::vector<uint8>& largeResourcesBlock) const;

		friend std::ostream& SerializationOperator(std::ostream&, const NascentRawGeometry&);
	};

}}}

