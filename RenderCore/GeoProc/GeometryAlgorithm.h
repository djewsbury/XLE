// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Format.h"
#include "../StateDesc.h"
#include "../../Math/Matrix.h"
#include "../../Utility/IteratorUtils.h"

namespace RenderCore { namespace Assets { namespace GeoProc { class MeshDatabase; }}}
namespace RenderCore { namespace Assets { struct VertexElement; }}
namespace RenderCore { class InputElementDesc; }

namespace RenderCore { namespace Assets { namespace GeoProc
{
    namespace GenerateTangentFrameFlags
    {
        enum { Normals=1<<0, Tangents=1<<1, Bitangents=1<<2 };
        using BitField = unsigned;
    }

    void GenerateTangentFrame(
        MeshDatabase& mesh,
        unsigned semanticIndex,
        GenerateTangentFrameFlags::BitField creationFlags,
        IteratorRange<const unsigned*> flatTriList,            // unified vertex index
        float equivalenceThreshold);

    void Transform(
        RenderCore::Assets::GeoProc::MeshDatabase& mesh, 
        const Float4x4& transform);

    void RemoveRedundantBitangents(
        RenderCore::Assets::GeoProc::MeshDatabase& mesh);

    void CopyVertexElements(
        IteratorRange<void*> destinationBuffer,            size_t destinationVertexStride,
        IteratorRange<const void*> sourceBuffer,           size_t sourceVertexStride,
        IteratorRange<const Assets::VertexElement*> destinationLayout,
        IteratorRange<const Assets::VertexElement*> sourceLayout,
        IteratorRange<const uint32_t*> reordering);

    unsigned CalculateVertexSize(IteratorRange<const Assets::VertexElement*> layout);
    unsigned CalculateVertexSize(IteratorRange<const InputElementDesc*> layout);

    /// <summary>Generated a tri-list-with-adjacency index buffer</summary>
    /// Given in input tri-list index buffer, calculate the adjacency information for each edge, and build
    /// a tri-list-with-adjacency index buffer. In the output, there are 6 vertices per triangle -- the 
    /// 3 extra vertices can be combined with the 3 main triangle edges to find adjacency triangles. See the
    /// vulkan spec documentation for the particulars of the output
    void TriListToTriListWithAdjacency(
        IteratorRange<unsigned*> outputTriListWithAdjacency,
        IteratorRange<const unsigned*> inputTriListIndexBuffer);

    struct DrawCallForGeoAlgorithm
    {
        IteratorRange<const void*> _indices;
        Format _ibFormat;
        Topology _topology = Topology::TriangleList;
    };

    std::vector<unsigned> BuildAdjacencyIndexBufferForUniquePositions(
        RenderCore::Assets::GeoProc::MeshDatabase& mesh,
        IteratorRange<const DrawCallForGeoAlgorithm*> drawCalls);

    std::vector<unsigned> BuildAdjacencyIndexBufferForUnifiedIndices(
        IteratorRange<const DrawCallForGeoAlgorithm*> drawCalls);

    std::vector<unsigned> BuildFlatTriList(
        IteratorRange<const DrawCallForGeoAlgorithm*> drawCalls);

    std::vector<uint8_t> ConvertIndexBufferFormat(
        std::vector<unsigned>&& src,
        Format ibFormat);

}}}
