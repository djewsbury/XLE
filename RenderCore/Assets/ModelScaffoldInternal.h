// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ModelMachine.h"
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
	using MaterialGuid = uint64_t;

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

	class RawGeometry : public RawGeometryDesc {};

	class BoundSkinnedGeometry : public RawGeometry, public SkinningDataDesc
	{
	public:
		~BoundSkinnedGeometry();
	private:
		BoundSkinnedGeometry();
	};

	struct VertexData	// todo -- remove; deprecated
    {
        GeoInputAssembly    _ia;
        unsigned            _offset, _size;
    };

    struct IndexData	// todo -- remove; deprecated
    {
        Format		 _format;
        unsigned    _offset, _size;
    };

	inline void SerializationOperator(
		::Assets::NascentBlockSerializer& outputSerializer,
		const VertexData& vd)
	{
		SerializationOperator(outputSerializer, vd._ia);
		SerializationOperator(outputSerializer, vd._offset);
		SerializationOperator(outputSerializer, vd._size);
	}

	inline void SerializationOperator(
		::Assets::NascentBlockSerializer& outputSerializer,
		const IndexData& id)
	{
		SerializationOperator(outputSerializer, (uint32_t)id._format);
		SerializationOperator(outputSerializer, id._offset);
		SerializationOperator(outputSerializer, id._size);
	}

	class SupplementGeo
	{
	public:
		unsigned			_geoId;
		GeoInputAssembly	_vbIA;
	};

	unsigned BuildLowLevelInputAssembly(
		IteratorRange<InputElementDesc*> dst,
		IteratorRange<const VertexElement*> source,
		unsigned lowLevelSlot = 0);

	std::vector<MiniInputElementDesc> BuildLowLevelInputAssembly(
		IteratorRange<const VertexElement*> source);

	#pragma pack(pop)

}}
