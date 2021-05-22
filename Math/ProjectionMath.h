// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Vector.h"
#include "Matrix.h"
#include "../Utility/IteratorUtils.h"

namespace XLEMath
{
    namespace AABBIntersection {
        enum Enum { Culled, Within, Boundary };
    }
    
    namespace GeometricCoordinateSpace      { enum Enum { LeftHanded,       RightHanded };  }
    enum class ClipSpaceType { StraddlingZero,   Positive,   PositiveRightHanded };

    AABBIntersection::Enum TestAABB(
        const Float4x4& localToProjection, 
        const Float3& mins, const Float3& maxs,
        ClipSpaceType clipSpaceType);

    AABBIntersection::Enum TestAABB_Aligned(
        const Float4x4& localToProjection, 
        const Float3& mins, const Float3& maxs,
        ClipSpaceType clipSpaceType);

    inline bool CullAABB(
        const Float4x4& localToProjection, 
        const Float3& mins, const Float3& maxs,
        ClipSpaceType clipSpaceType)
    {
        return TestAABB(localToProjection, mins, maxs, clipSpaceType)
            == AABBIntersection::Culled;
    }

    inline bool CullAABB_Aligned(
        const Float4x4& localToProjection,
        const Float3& mins, const Float3& maxs,
        ClipSpaceType clipSpaceType)
    {
        return TestAABB_Aligned(localToProjection, mins, maxs, clipSpaceType)
            == AABBIntersection::Culled;
    }

    Float4 ExtractMinimalProjection(const Float4x4& projectionMatrix);
    bool IsOrthogonalProjection(const Float4x4& projectionMatrix);
    
    /**
     * Tests whether any triangle in geometry is at least partially visible given the projectionMatrix
     * geometry: A pair of lists. The first is a list of indexes for the triangles.
     *           There should an index for each vertex in each triangle, collated.
     *           The second list is a list of vertexes, one for each index in the first list.
     * projectionMatrix: The projection to clip space
     * clipSpaceType: The type of clip space the projection matrix is in
     *
     * returns: True iff any triangle is at least partially visible
     **/
    bool TestTriangleList(const std::pair<IteratorRange<const unsigned *>, IteratorRange<const Float3*>> &geometry,
                          const Float4x4 &projectionMatrix,
                          ClipSpaceType clipSpaceType);

///////////////////////////////////////////////////////////////////////////////////////////////////
        //   B U I L D I N G   P R O J E C T I O N   M A T R I C E S
///////////////////////////////////////////////////////////////////////////////////////////////////
    
    Float4x4 PerspectiveProjection(
        float verticalFOV, float aspectRatio,
        float nearClipPlane, float farClipPlane,
        GeometricCoordinateSpace::Enum coordinateSpace,
        ClipSpaceType clipSpaceType);

    Float4x4 PerspectiveProjection(
        float l, float t, float r, float b,
        float nearClipPlane, float farClipPlane,
        ClipSpaceType clipSpaceType);

    Float4x4 OrthogonalProjection(
        float l, float t, float r, float b,
        float nearClipPlane, float farClipPlane,
        GeometricCoordinateSpace::Enum coordinateSpace,
        ClipSpaceType clipSpaceType);

    Float4x4 OrthogonalProjection(
        float l, float t, float r, float b,
        float nearClipPlane, float farClipPlane,
        ClipSpaceType clipSpaceType);

    void CalculateAbsFrustumCorners(
        Float3 frustumCorners[8],
        const Float4x4& worldToProjection,
        ClipSpaceType clipSpaceType);

///////////////////////////////////////////////////////////////////////////////////////////////////

    std::pair<float, float> CalculateNearAndFarPlane(const Float4& minimalProjection, ClipSpaceType clipSpaceType);
    std::pair<float, float> CalculateNearAndFarPlane_Ortho(const Float4& minimalProjection, ClipSpaceType clipSpaceType);
    std::pair<float, float> CalculateFov(const Float4& minimalProjection, ClipSpaceType clipSpaceType);
    Float2 CalculateDepthProjRatio_Ortho(const Float4& minimalProjection, ClipSpaceType clipSpaceType);

    std::pair<Float3, Float3> BuildRayUnderCursor(
        Int2 mousePosition, 
        Float3 absFrustumCorners[], 
        const Float3& cameraPosition,
        float nearClip, float farClip,
        const std::pair<Float2, Float2>& viewport);

///////////////////////////////////////////////////////////////////////////////////////////////////

    std::pair<Float2, Float2> GetPlanarMinMax(const Float4x4& worldToClip, const Float4& plane, ClipSpaceType clipSpaceType);

///////////////////////////////////////////////////////////////////////////////////////////////////

}

