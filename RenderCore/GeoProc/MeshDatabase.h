// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Format.h"
#include "../VertexUtil.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Core/Types.h"
#include <utility>
#include <memory>
#include <vector>
#include <string>

namespace RenderCore { class InputElementDesc; }

namespace RenderCore { namespace Assets { namespace GeoProc
{
    namespace ProcessingFlags 
    {
        enum Enum { TexCoordFlip = 1<<0, TangentHandinessFlip = 1<<1, BitangentFlip = 1<<2, Renormalize = 1<<3 };
        typedef unsigned BitField;
    }

    namespace FormatHint
    {
        enum Enum { IsColor = 1<<0 };
        typedef unsigned BitField;
    }

    class IVertexSourceData;
    class NativeVBLayout;

    /// <summary>A representation of a mesh used during geometry processing</summary>
    /// MeshDatabase provides some utilities and structure that can be used in
    /// geometry processing operations (such as redundant vertex removal and mesh
    /// optimisation)
    /// A lot of flexibility is allowed for how the source mesh data is stored -- whi
    class MeshDatabase
    {
    public:
        unsigned    HasElement(const char name[]) const;
        unsigned    FindElement(const char name[], unsigned semanticIndex = 0) const;
        void        RemoveStream(unsigned elementIndex);

        template<typename OutputType>
            OutputType GetUnifiedElement(size_t vertexIndex, unsigned elementIndex) const;
        size_t GetUnifiedVertexCount() const { return _unifiedVertexCount; }

        auto    BuildNativeVertexBuffer(const NativeVBLayout& outputLayout) const   -> std::vector<uint8_t>;
        auto    BuildUnifiedVertexIndexToPositionIndex() const                      -> std::unique_ptr<uint32[]>;

        unsigned    AddStream(
            std::shared_ptr<IVertexSourceData> dataSource,
            std::vector<unsigned>&& vertexMap,
            const char semantic[], unsigned semanticIndex);
        unsigned    InsertStream(
            unsigned insertionPosition,
            std::shared_ptr<IVertexSourceData> dataSource,
            std::vector<unsigned>&& vertexMap,
            const char semantic[], unsigned semanticIndex);

        class Stream;
        IteratorRange<const Stream*> GetStreams() const     { return MakeIteratorRange(_streams); }
		IteratorRange<Stream*> GetStreams()					{ return MakeIteratorRange(_streams); }

        MeshDatabase();
        MeshDatabase(MeshDatabase&& moveFrom) never_throws;
        MeshDatabase& operator=(MeshDatabase&& moveFrom) never_throws;
        ~MeshDatabase();

        class Stream
        {
        public:
            const std::vector<unsigned>& GetVertexMap() const   { return _vertexMap; }
            const std::string& GetSemanticName() const          { return _semanticName; }
            const unsigned GetSemanticIndex() const             { return _semanticIndex; }
			const std::shared_ptr<IVertexSourceData>& GetSourceData() const { return _sourceData; }

			void SetSemantic(const std::string& newName, unsigned semanticIndex) { _semanticName = newName; _semanticIndex = semanticIndex; }

            unsigned UnifiedToStream(unsigned input) const
            {
                if (!_vertexMap.empty()) return _vertexMap[input];
                return input;
            }

            Stream();
            Stream(
                std::shared_ptr<IVertexSourceData> sourceData, std::vector<unsigned> vertexMap, 
                const std::string& semanticName, unsigned semanticIndex);
            Stream(Stream&& moveFrom) never_throws;
            Stream& operator=(Stream&& moveFrom) never_throws;
            ~Stream();
        private:
            std::shared_ptr<IVertexSourceData>  _sourceData;
            std::vector<unsigned>   _vertexMap;
            std::string             _semanticName;
            unsigned                _semanticIndex;
        };

    private:
        size_t _unifiedVertexCount;
        std::vector<Stream> _streams;

        void WriteStream(
            const Stream& stream, const void* dst, 
            Format dstFormat, size_t dstStride, size_t dstSize) const;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    class IVertexSourceData
    {
    public:
        virtual IteratorRange<const void*> GetData() const = 0;
        virtual Format GetFormat() const = 0;

        virtual size_t GetStride() const = 0;
        virtual size_t GetCount() const = 0;
        virtual ProcessingFlags::BitField GetProcessingFlags() const = 0;
        virtual FormatHint::BitField GetFormatHint() const = 0;

        virtual ~IVertexSourceData();
    };

    template<typename OutputType>
        OutputType GetVertex(const IVertexSourceData& sourceData, size_t index);

    class NativeVBLayout
    {
    public:
        std::vector<InputElementDesc> _elements;
        unsigned _vertexStride = 0;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    std::shared_ptr<IVertexSourceData>
        CreateRawDataSource(
            const void* dataBegin, const void* dataEnd, 
            size_t count, size_t stride,
			Format srcFormat);

    std::shared_ptr<IVertexSourceData>
        CreateRawDataSource(
            const void* dataBegin, const void* dataEnd, 
			Format srcFormat);

    std::shared_ptr<IVertexSourceData>
        CreateRawDataSource(
            std::vector<uint8>&& data, 
            size_t count, size_t stride,
			Format srcFormat);

    IteratorRange<VertexElementIterator> MakeVertexIteratorRange(const IVertexSourceData& srcData);

    /// <summary>Remove duplicates from a stream</summary>
    /// Searches for duplicate elements in a stream, and combines them into
    /// one. Will generate a new vertex mapping in which the duplicates have been
    /// combined to share the same element.
    ///
    /// Many algorithms work on the set of unique vertex positions (as opposed to
    /// unique unified vertices). This method is useful to find the unique vertex
    /// positions.
    ///
    /// The algorithm finds all pairs of vertices within the given threshold. Sometimes
    /// this results in chains of vertices (eg, A is close to B and B is close to C).
    /// All chains will be combined into a single vertex (even if A is not close to C).
    /// The resulting vertex will be the one that is closest to the average of all 
    /// of the vertices in the chain.
    std::shared_ptr<IVertexSourceData>
        RemoveDuplicates(
            std::vector<unsigned>& outputMapping,
            const IVertexSourceData& sourceStream,
            IteratorRange<const unsigned*> originalMapping,
            float threshold);

    std::shared_ptr<IVertexSourceData>
        RemoveBitwiseIdenticals(
            std::vector<unsigned>& outputMapping,
            const IVertexSourceData& sourceStream,
            IteratorRange<const unsigned*> originalMapping = {});

    // Similar to RemoveBitwiseIdenticals, however this time don't modify
    // the underlying vertex buffer. We will just produce a mapping with duplicate
    // entries for every case where there are bitwise indentical entries
    // Some vertex layouts use the w component in position for some purpose 
    // other than position -- to avoid problems in these cases, consider setting
    // the "ignoreWComponent" flag
    std::vector<unsigned> MapToBitwiseIdenticals(
        const IVertexSourceData& sourceStream,
            IteratorRange<const unsigned*> originalMapping = {},
            bool ignoreWComponent = false);

	MeshDatabase RemoveDuplicates(
		std::vector<unsigned>& outputMapping,
		const MeshDatabase& input);

    struct NativeVBSettings
    {
        bool    _use16BitFloats = true;
    };

    NativeVBLayout BuildDefaultLayout(MeshDatabase& mesh, const NativeVBSettings& settings = {});

    /// <summary>Creates a triangle winding order for a convex polygon with the given number of points<summary>
    /// If we have a convex polygon, this can be used to convert it into a list of triangles.
    /// However, note that if the polygon is concave, then some bad triangles will be created.
    size_t CreateTriangleWindingFromPolygon(
        unsigned buffer[], size_t bufferCount, size_t polygonVertexCount);

    /// <summary>Copy vertex data with format conversion</summary>
    /// This is typically used for copying vertex data between similar formats
    /// (for example, 32 bit floats to 16 bit floats)
    void CopyVertexData(
        const void* dst, Format dstFmt, size_t dstStride, size_t dstDataSize,
        const void* src, Format srcFmt, size_t srcStride, size_t srcDataSize,
        unsigned count, 
		IteratorRange<const unsigned*> mapping = {},
        ProcessingFlags::BitField processingFlags = 0);

	void Copy(IteratorRange<VertexElementIterator> destination, IteratorRange<VertexElementIterator> source, unsigned vertexCount);

	/// <summary>Compress a sparse index buffer to a contiguous list of indices and return a remapping</summary>
	/// The returned remapping is of the format remapping[oldIndex]=newIndex
	std::vector<unsigned> CompressIndexBuffer(IteratorRange<unsigned*> indexBufferInAndOut);

}}}

