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
    enum class CullTestResult { Culled, Within, Boundary };    
    enum class GeometricCoordinateSpace { LeftHanded, RightHanded };
    enum class ClipSpaceType { 
        StraddlingZero,
        Positive,
        PositiveRightHanded,
        // StraddlingZero_ReverseZ,  (not yet implemented)
        Positive_ReverseZ,
        PositiveRightHanded_ReverseZ };

    CullTestResult TestAABB(
        const Float4x4& localToProjection, 
        const Float3& mins, const Float3& maxs,
        ClipSpaceType clipSpaceType);

    CullTestResult TestAABB_Aligned(
        const Float4x4& localToProjection, 
        const Float3& mins, const Float3& maxs,
        ClipSpaceType clipSpaceType);

    inline bool CullAABB(
        const Float4x4& localToProjection, 
        const Float3& mins, const Float3& maxs,
        ClipSpaceType clipSpaceType)
    {
        return TestAABB(localToProjection, mins, maxs, clipSpaceType)
            == CullTestResult::Culled;
    }

    inline bool CullAABB_Aligned(
        const Float4x4& localToProjection,
        const Float3& mins, const Float3& maxs,
        ClipSpaceType clipSpaceType)
    {
        return TestAABB_Aligned(localToProjection, mins, maxs, clipSpaceType)
            == CullTestResult::Culled;
    }

    class AccurateFrustumTester
    {
    public:
        CullTestResult TestSphere(Float3 centerPoint, float radius);

        AccurateFrustumTester(const Float4x4& localToProjection, ClipSpaceType clipSpaceType);
        ~AccurateFrustumTester();
    private:
        Float4 _frustumPlanes[6];
        Float3 _frustumCorners[8];
        Float4x4 _localToProjection;
        ClipSpaceType _clipSpaceType;
    };

    class ArbitraryConvexVolumeTester
    {
    public:
        CullTestResult TestSphere(Float3 centerPoint, float radius);

        CullTestResult TestAABB(
            const Float3x4& aabbToLocalSpace, 
            Float3 mins, Float3 maxs);

        struct Edge { unsigned _cornerZero, _cornerOne; uint64_t _faceBitMask; }; 
        ArbitraryConvexVolumeTester(
            std::vector<Float4>&& planes,                   // A, B, C, D plane definition (eg, from PlaneFit)
            std::vector<Float3>&& corners,                  // corner points
            std::vector<Edge>&& edges,
            std::vector<unsigned>&& cornerFaceBitMasks);
        ArbitraryConvexVolumeTester() = default;
        ArbitraryConvexVolumeTester(ArbitraryConvexVolumeTester&&) = default;
        ArbitraryConvexVolumeTester& operator=(ArbitraryConvexVolumeTester&&) = default;
    private:
        std::vector<Float4> _planes;
        std::vector<Float3> _corners;
        std::vector<Edge> _edges;
        std::vector<unsigned> _cornerFaceBitMasks;
    };

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

    ArbitraryConvexVolumeTester ExtrudeFrustumOrthogonally(
        const Float4x4& localToClipSpace,
        Float3 extrusionDirectionLocal,
        float extrusionLength,
        ClipSpaceType clipSpaceType);

///////////////////////////////////////////////////////////////////////////////////////////////////
        //   B U I L D I N G   P R O J E C T I O N   M A T R I C E S
///////////////////////////////////////////////////////////////////////////////////////////////////
    
    Float4x4 PerspectiveProjection(
        float verticalFOV, float aspectRatio,
        float nearClipPlane, float farClipPlane,
        GeometricCoordinateSpace coordinateSpace,
        ClipSpaceType clipSpaceType);

    Float4x4 PerspectiveProjection(
        float l, float t, float r, float b,
        float nearClipPlane, float farClipPlane,
        ClipSpaceType clipSpaceType);

    Float4x4 OrthogonalProjection(
        float l, float t, float r, float b,
        float nearClipPlane, float farClipPlane,
        GeometricCoordinateSpace coordinateSpace,
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
        const std::pair<Float2, Float2>& viewport);

///////////////////////////////////////////////////////////////////////////////////////////////////

    std::pair<Float4x4, Float4x4> CubemapViewAndProjection(
        unsigned cubeFace,
        Float3 centerLocation, float nearClip, float farClip,
        GeometricCoordinateSpace coordinateSpace,
        ClipSpaceType clipSpaceType);

///////////////////////////////////////////////////////////////////////////////////////////////////

    std::pair<Float2, Float2> GetPlanarMinMax(const Float4x4& worldToClip, const Float4& plane, ClipSpaceType clipSpaceType);

///////////////////////////////////////////////////////////////////////////////////////////////////

}

