// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Format.h"
#include "../StateDesc.h"
#include "../Types.h"
#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringUtils.h"

namespace RenderCore { class MiniInputElementDesc; }

namespace RenderCore { namespace Assets 
{
    typedef uint64_t MaterialGuid;

    #pragma pack(push)
    #pragma pack(1)

///////////////////////////////////////////////////////////////////////////////////////////////////
    //      g e o m e t r y         //

    class ModelCommandStream
    {
    public:
            //  "Geo calls" & "draw calls". Geo calls have 
            //  a vertex buffer and index buffer, and contain
            //  draw calls within them.
        class GeoCall
        {
        public:
            unsigned        _geoId;
            unsigned        _transformMarker;
            MaterialGuid*   _materialGuids;
            size_t          _materialCount;
            unsigned        _levelOfDetail;
        };

        class InputInterface
        {
        public:
            uint64_t*	_jointNames;
            size_t      _jointCount;
        };

            /////   -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-   /////
        const GeoCall&  GetGeoCall(size_t index) const;
        size_t          GetGeoCallCount() const;

        const GeoCall&  GetSkinCall(size_t index) const;
        size_t          GetSkinCallCount() const;

        auto            GetInputInterface() const -> const InputInterface& { return _inputInterface; }

        ~ModelCommandStream();
    private:
        GeoCall*        _geometryInstances;
        size_t          _geometryInstanceCount;
        GeoCall*        _skinControllerInstances;
        size_t          _skinControllerInstanceCount;
        InputInterface  _inputInterface;

        ModelCommandStream(const ModelCommandStream&) = delete;
        ModelCommandStream& operator=(const ModelCommandStream&) = delete;
    };

    inline auto         ModelCommandStream::GetGeoCall(size_t index) const -> const GeoCall&    { return _geometryInstances[index]; }
    inline size_t       ModelCommandStream::GetGeoCallCount() const                             { return _geometryInstanceCount; }
    inline auto         ModelCommandStream::GetSkinCall(size_t index) const -> const GeoCall&   { return _skinControllerInstances[index]; }
    inline size_t       ModelCommandStream::GetSkinCallCount() const                            { return _skinControllerInstanceCount; }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class DrawCallDesc
    {
    public:
        unsigned    _firstIndex = 0, _indexCount = 0;
        unsigned    _firstVertex = 0;
        unsigned    _subMaterialIndex = 0;
        Topology	_topology = (Topology)4; // Topology::TriangleList;
    };

    class VertexElement
    {
    public:
        char            _semanticName[16];  // limited max size for semantic name (only alternative is to use a hash value)
        unsigned        _semanticIndex;
        Format			_nativeFormat;
        unsigned        _alignedByteOffset;

        VertexElement();
		VertexElement(const char name[], unsigned semanticIndex, Format nativeFormat, unsigned offset);
        VertexElement(const VertexElement&) never_throws;
        VertexElement& operator=(const VertexElement&) never_throws;
    };

    class GeoInputAssembly
    {
    public:
        SerializableVector<VertexElement>   _elements;
        unsigned                            _vertexStride = 0;

        uint64_t BuildHash() const;
    };

    class VertexData
    {
    public:
        GeoInputAssembly    _ia;
        unsigned            _offset, _size;
    };

    class IndexData
    {
    public:
        Format		 _format;
        unsigned    _offset, _size;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    class RawGeometry
    {
    public:
        VertexData  _vb;
        IndexData   _ib;
        SerializableVector<DrawCallDesc>	_drawCalls;
		Float4x4							_geoSpaceToNodeSpace;					// transformation from the coordinate space of the geometry itself to whatever node it's attached to. Useful for some deformation operations, where a post-performance transform is required
		SerializableVector<unsigned>		_finalVertexIndexToOriginalIndex;		// originalIndex = _finalVertexIndexToOriginalIndex[finalIndex]
    };

    class BoundSkinnedGeometry : public RawGeometry
    {
    public:

            //  The "RawGeometry" base class contains the 
            //  unanimated vertex elements (and draw calls for
            //  rendering the object as a whole)
        VertexData      _animatedVertexElements;
        VertexData      _skeletonBinding;

		struct Section
		{
			SerializableVector<Float4x4>		_bindShapeByInverseBindMatrices;
			SerializableVector<DrawCallDesc>	_preskinningDrawCalls;
			uint16_t*							_jointMatrices;
			size_t								_jointMatrixCount;
			Float4x4							_bindShapeMatrix;			// (the bind shape matrix is already combined into the _bindShapeByInverseBindMatrices fields. This is included mostly just for debugging)
            Float4x4                            _postSkinningBindMatrix;
		};
		SerializableVector<Section>			_preskinningSections;

        std::pair<Float3, Float3>			_localBoundingBox;

        ~BoundSkinnedGeometry();
    private:
        BoundSkinnedGeometry();
    };

    class SupplementGeo
    {
    public:
        unsigned    _geoId;
        VertexData  _vb;
    };

	unsigned BuildLowLevelInputAssembly(
        IteratorRange<InputElementDesc*> dst,
        IteratorRange<const VertexElement*> source,
        unsigned lowLevelSlot = 0);

	std::vector<MiniInputElementDesc> BuildLowLevelInputAssembly(
		IteratorRange<const VertexElement*> source);

///////////////////////////////////////////////////////////////////////////////////////////////////

    #pragma pack(pop)

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
		::Assets::NascentBlockSerializer& outputSerializer,
		const VertexElement& ia)
	{
		outputSerializer.SerializeRaw(ia);
	}
    
    inline void SerializationOperator(
		::Assets::NascentBlockSerializer& outputSerializer,
		const GeoInputAssembly& ia)
	{
		SerializationOperator(outputSerializer, ia._elements);
		SerializationOperator(outputSerializer, ia._vertexStride);
	}

	inline void SerializationOperator(
		::Assets::NascentBlockSerializer& outputSerializer,
		const IndexData& indexData)
	{
		SerializationOperator(outputSerializer, (unsigned&)indexData._format);
		SerializationOperator(outputSerializer, indexData._offset);
		SerializationOperator(outputSerializer, indexData._size);
	}

	inline void SerializationOperator(
		::Assets::NascentBlockSerializer& outputSerializer,
		const VertexData& vertexData)
	{
		SerializationOperator(outputSerializer, vertexData._ia);
		SerializationOperator(outputSerializer, vertexData._offset);
		SerializationOperator(outputSerializer, vertexData._size);
	}

	inline void SerializationOperator(
		::Assets::NascentBlockSerializer& outputSerializer,
		const DrawCallDesc& drawCall)
	{
		outputSerializer.SerializeValue(drawCall._firstIndex);
		outputSerializer.SerializeValue(drawCall._indexCount);
		outputSerializer.SerializeValue(drawCall._firstVertex);
		outputSerializer.SerializeValue(drawCall._subMaterialIndex);
		outputSerializer.SerializeValue((unsigned&)drawCall._topology);
	}

}}
