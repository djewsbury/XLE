// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ScaffoldCmdStream.h"
#include "../StateDesc.h"
#include "../Format.h"
#include "../../Math/Matrix.h"
#include "../../Math/Vector.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Utility/Streams/SerializationUtils.h"

namespace RenderCore { class InputElementDesc; class MiniInputElementDesc; }

namespace RenderCore { namespace Assets
{
	enum class GeoCommand : uint32_t
	{
		AttachRawGeometry = s_scaffoldCmdBegin_ModelMachine + 0x100,
		AttachSkinningData
	};

	enum class ModelCommand : uint32_t
	{
		BeginSubModel = s_scaffoldCmdBegin_ModelMachine,
		EndSubModel,
		SetLevelOfDetail,
		InputInterface,			// ModelInputInterfaceDesc

		// ModelCommandStream style callouts
		SetTransformMarker,
		SetMaterialAssignments,
		GeoCall
	};

	struct SubModelDesc
	{
		unsigned _levelOfDetail = 0;
	};

	struct ModelInputInterfaceDesc
	{
		SerializableVector<uint64_t> _jointNames;
	};

	struct GeoCallDesc
	{
		unsigned _geoId;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////

	#pragma pack(push)
	#pragma pack(1)

	struct DrawCallDesc
	{
		unsigned	_firstIndex = 0, _indexCount = 0;
		unsigned	_firstVertex = 0;
		Topology	_topology = Topology::TriangleList;
	};

	struct VertexElement
	{
		char			_semanticName[16];  // limited max size for semantic name (only alternative is to use a hash value)
		unsigned		_semanticIndex;
		Format			_nativeFormat;
		unsigned		_alignedByteOffset;

		VertexElement();
		VertexElement(const char name[], unsigned semanticIndex, Format nativeFormat, unsigned offset);
		VertexElement(const VertexElement&) never_throws;
		VertexElement& operator=(const VertexElement&) never_throws;
	};

	struct GeoInputAssembly
	{
		SerializableVector<VertexElement>	_elements;
		unsigned							_vertexStride = 0;

		uint64_t BuildHash() const;
	};

	struct VertexData
	{
		GeoInputAssembly    _ia;
		unsigned            _offset, _size;
	};

	struct IndexData
	{
		Format		 _format;
		unsigned    _offset, _size;
	};

	struct RawGeometryDesc
	{
		VertexData							_vb;
		IndexData							_ib;
		SerializableVector<DrawCallDesc>	_drawCalls;
		Float4x4							_geoSpaceToNodeSpace;					// transformation from the coordinate space of the geometry itself to whatever node it's attached to. Useful for some deformation operations, where a post-performance transform is required
		SerializableVector<unsigned>		_finalVertexIndexToOriginalIndex;		// originalIndex = _finalVertexIndexToOriginalIndex[finalIndex]
	};

	struct ModelDefaultPoseData
	{
		SerializableVector<Float4x4>    _defaultTransforms;
		std::pair<Float3, Float3>       _boundingBox = {Float3(0,0,0), Float3(0,0,0)};

		friend void SerializationOperator(::Assets::BlockSerializer&, const ModelDefaultPoseData&);
	};

	struct ModelRootData
	{
		unsigned    _maxLOD = 0;
		friend void SerializationOperator(::Assets::BlockSerializer&, const ModelRootData&);
	};

	struct SkinningDataDesc
	{
			//  The "RawGeometryDesc" base class contains the 
			//  unanimated vertex elements (and draw calls for
			//  rendering the object as a whole)
		VertexData		_animatedVertexElements;
		VertexData		_skeletonBinding;

		struct Section
		{
			SerializableVector<Float4x4>		_bindShapeByInverseBindMatrices;
			SerializableVector<DrawCallDesc>	_preskinningDrawCalls;
			SerializableVector<unsigned>		_drawCallWeightsPerVertex;
			uint16_t*							_jointMatrices;
			size_t								_jointMatrixCount;
			Float4x4							_bindShapeMatrix;			// (the bind shape matrix is already combined into the _bindShapeByInverseBindMatrices fields. This is included mostly just for debugging)
			Float4x4							_postSkinningBindMatrix;
		};
		SerializableVector<Section>			_preskinningSections;

		std::pair<Float3, Float3>			_localBoundingBox;
	};

	#pragma pack(pop)

	unsigned BuildLowLevelInputAssembly(
		IteratorRange<InputElementDesc*> dst,
		IteratorRange<const VertexElement*> source,
		unsigned lowLevelSlot = 0);

	std::vector<MiniInputElementDesc> BuildLowLevelInputAssembly(
		IteratorRange<const VertexElement*> source);

///////////////////////////////////////////////////////////////////////////////////////////////////

	inline VertexElement::VertexElement()
	{
		_nativeFormat = Format(0); _alignedByteOffset = 0; _semanticIndex = 0;
		XlZeroMemory(_semanticName);
	}

	inline VertexElement::VertexElement(const char name[], unsigned semanticIndex, Format nativeFormat, unsigned offset)
	{
		XlZeroMemory(_semanticName);
		XlCopyString(_semanticName, name);
		_semanticIndex = semanticIndex;
		_nativeFormat = nativeFormat;
		_alignedByteOffset = offset;
	}

	inline VertexElement::VertexElement(const VertexElement& ele) never_throws
	{
		_nativeFormat = ele._nativeFormat; _alignedByteOffset = ele._alignedByteOffset; _semanticIndex = ele._semanticIndex;
		XlCopyMemory(_semanticName, ele._semanticName, sizeof(_semanticName));
	}

	inline VertexElement& VertexElement::operator=(const VertexElement& ele) never_throws
	{
		_nativeFormat = ele._nativeFormat; _alignedByteOffset = ele._alignedByteOffset; _semanticIndex = ele._semanticIndex;
		XlCopyMemory(_semanticName, ele._semanticName, sizeof(_semanticName));
		return *this;
	}

	inline void SerializationOperator(
		::Assets::BlockSerializer& outputSerializer,
		const VertexElement& ia)
	{
		outputSerializer.SerializeRaw(ia);
	}
	
	inline void SerializationOperator(
		::Assets::BlockSerializer& outputSerializer,
		const GeoInputAssembly& ia)
	{
		SerializationOperator(outputSerializer, ia._elements);
		SerializationOperator(outputSerializer, ia._vertexStride);
	}

	inline void SerializationOperator(
		::Assets::BlockSerializer& outputSerializer,
		const VertexData& vd)
	{
		SerializationOperator(outputSerializer, vd._ia);
		SerializationOperator(outputSerializer, vd._offset);
		SerializationOperator(outputSerializer, vd._size);
	}

	inline void SerializationOperator(
		::Assets::BlockSerializer& outputSerializer,
		const IndexData& id)
	{
		SerializationOperator(outputSerializer, (uint32_t)id._format);
		SerializationOperator(outputSerializer, id._offset);
		SerializationOperator(outputSerializer, id._size);
	}

	inline void SerializationOperator(
		::Assets::BlockSerializer& outputSerializer,
		const DrawCallDesc& drawCall)
	{
		SerializationOperator(outputSerializer, drawCall._firstIndex);
		SerializationOperator(outputSerializer, drawCall._indexCount);
		SerializationOperator(outputSerializer, drawCall._firstVertex);
		SerializationOperator(outputSerializer, (unsigned)drawCall._topology);
	}
}}

