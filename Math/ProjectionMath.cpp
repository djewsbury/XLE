// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

// warnings from CML
#pragma warning(disable:4267)       //  warning C4267: 'initializing' : conversion from 'size_t' to 'int', possible loss of data

#include "ProjectionMath.h"
#include "Geometry.h"
#include "Transformations.h"
#include "../Core/Prefix.h"
#include "../Core/SelectConfiguration.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/ArithmeticUtils.h"
#include <assert.h>
#include <cfloat>
#include <limits>

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__) 
    #include <immintrin.h>
    #define HAS_SSE_INSTRUCTIONS
#endif

namespace XLEMath
{
    static Float4x4 InvertWorldToProjection(const Float4x4& input, bool useAccurateInverse)
    {
        if (useAccurateInverse) {
            return cml::inverse(input);
        } else {
            return Inverse(input);
        }
    }

    void CalculateAbsFrustumCorners(
        Float3 frustumCorners[8], const Float4x4& worldToProjection,
        ClipSpaceType clipSpaceType)
    {
            //  So long as we can invert the world to projection matrix accurately, we can 
            //  extract the frustum corners easily. We just need to pass the coordinates
            //  of the corners of clip space through the inverse matrix.
            //
            //  If the matrix inversion is not accurate enough, we can do this by going back to
            //  the source components that built the worldToProjection matrix.
            //  We can easily get the projection top/left/right/bottom from the raw projection matrix
            //  and we can also get the near and far clip from that. The world to view matrix can be
            //  inverted accurately with InvertOrthonormalTransform (and normally we should have the
            //  world to view matrix calculated at higher points in the pipeline). 
            //  So by using those source components, we can calculate the corners without and extra
            //  matrix inversion operations.
        static bool useAccurateInverse = true;      // the normal cml invert is pretty accurate. But sometimes it seems we get a better result with this
        auto projectionToWorld = InvertWorldToProjection(worldToProjection, useAccurateInverse);
        float yAtTop = 1.f;
        float yAtBottom = -1.f;
        if (clipSpaceType == ClipSpaceType::PositiveRightHanded || clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ) {
            yAtTop = -1.f;
            yAtBottom = 1.f;
        }
        float zAtNear = 0.f, zAtFar = 1.0f;
        if (clipSpaceType == ClipSpaceType::StraddlingZero) {
            zAtNear = -1.f;
        } else if (clipSpaceType == ClipSpaceType::Positive_ReverseZ || clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ) {
            zAtNear = 1.f;
            zAtFar = 0.f;
        }

        Float4 v0 = projectionToWorld * Float4(-1.f, yAtTop,    zAtNear, 1.f);
        Float4 v1 = projectionToWorld * Float4(-1.f, yAtBottom, zAtNear, 1.f);
        Float4 v2 = projectionToWorld * Float4( 1.f, yAtTop,    zAtNear, 1.f);
        Float4 v3 = projectionToWorld * Float4( 1.f, yAtBottom, zAtNear, 1.f);

        Float4 v4 = projectionToWorld * Float4(-1.f, yAtTop,    zAtFar, 1.f);
        Float4 v5 = projectionToWorld * Float4(-1.f, yAtBottom, zAtFar, 1.f);
        Float4 v6 = projectionToWorld * Float4( 1.f, yAtTop,    zAtFar, 1.f);
        Float4 v7 = projectionToWorld * Float4( 1.f, yAtBottom, zAtFar, 1.f);

        frustumCorners[0] = Truncate(v0) / v0[3];
        frustumCorners[1] = Truncate(v1) / v1[3];
        frustumCorners[2] = Truncate(v2) / v2[3];
        frustumCorners[3] = Truncate(v3) / v3[3];

        frustumCorners[4] = Truncate(v4) / v4[3];
        frustumCorners[5] = Truncate(v5) / v5[3];
        frustumCorners[6] = Truncate(v6) / v6[3];
        frustumCorners[7] = Truncate(v7) / v7[3];
    }
    
#if defined(HAS_SSE_INSTRUCTIONS)

    static inline void TestAABB_SSE_TransCorner(
        __m128 corner0, __m128 corner1, 
        __m128& A0, __m128& A1, __m128& A2,
        float* dst)
    {
        // still need many registers --
        // A0, A1, A2
        // x0, y0, z2
        // x1, y1, z1
        // abuv, cz11
        auto x0 = _mm_dp_ps(A0, corner0, (0xF<<4)|(1<<0));      // L: ~12, T: 0 (varies for different processors)
        auto y0 = _mm_dp_ps(A1, corner0, (0xF<<4)|(1<<1));      // L: ~12, T: 0 (varies for different processors)
        auto z0 = _mm_dp_ps(A2, corner0, (0xF<<4)|(1<<2));      // L: ~12, T: 0 (varies for different processors)

        auto x1 = _mm_dp_ps(A0, corner1, (0xF<<4)|(1<<0));      // L: ~12, T: 0 (varies for different processors)
        auto y1 = _mm_dp_ps(A1, corner1, (0xF<<4)|(1<<1));      // L: ~12, T: 0 (varies for different processors)
        auto z1 = _mm_dp_ps(A2, corner1, (0xF<<4)|(1<<2));      // L: ~12, T: 0 (varies for different processors)

        auto clipSpaceXYZ0 = _mm_add_ps(x0, y0);                // L: 3, T: 1
        auto clipSpaceXYZ1 = _mm_add_ps(x1, y1);                // L: 3, T: 1
        clipSpaceXYZ0 = _mm_add_ps(z0, clipSpaceXYZ0);          // L: 3, T: 1
        clipSpaceXYZ1 = _mm_add_ps(z1, clipSpaceXYZ1);          // L: 3, T: 1
        _mm_store_ps(dst, clipSpaceXYZ0);
        _mm_store_ps(dst + 4, clipSpaceXYZ1);
    }

    __declspec(align(16)) static const unsigned g_zeroZWComponentsInit[] = { 0xffffffff, 0xffffffff, 0, 0 };
    static const auto g_zeroZWComponents = _mm_load_ps((const float*)g_zeroZWComponentsInit);
    static const auto g_signMask = _mm_set1_ps(-0.f);       // -0.f = 1 << 31
        
    static inline void TestAABB_SSE_CalcFlags(
        const float* clipSpaceXYZMem, const float* clipSpaceWMem,
        __m128& andUpper, __m128& andLower,
        __m128& orUpperLower)
    {
        assert((size_t(clipSpaceXYZMem)&0xF) == 0);
        assert((size_t(clipSpaceWMem)&0xF) == 0);

        auto xyz = _mm_load_ps(clipSpaceXYZMem);
        auto w = _mm_load_ps(clipSpaceWMem);

        auto cmp0 = _mm_cmpgt_ps(xyz, w);               // L: 3, T: -

        auto negW = _mm_xor_ps(w, g_signMask);          // L: 1, T: ~0.33 (this will flip the sign of w)
        negW = _mm_and_ps(negW, g_zeroZWComponents);    // L: 1, T: 1

        auto cmp1 = _mm_cmplt_ps(xyz, negW);            // L: 3, T: -

            // apply bitwise "and" and "or" as required...
        andUpper = _mm_and_ps(andUpper, cmp0);          // L: 1, T: ~1
        andLower = _mm_and_ps(andLower, cmp1);          // L: 1, T: ~1

        orUpperLower = _mm_or_ps(orUpperLower, cmp0);   // L: 1, T: .33
        orUpperLower = _mm_or_ps(orUpperLower, cmp1);   // L: 1, T: .33
    }

    static CullTestResult TestAABB_SSE(
        const float localToProjection[], 
        const Float3& mins, const Float3& maxs)
    {
        // Perform projection into culling space...

        // We can perform the matrix * vector multiply in three ways:
        //      1. using "SSE4.1" dot product instruction "_mm_dp_ps"
        //      2. using SSE3 vector multiply and horizontal add instructions
        //      2. using "FMA" vector multiply and fused vector add
        //
        // FMA is not supported on Intel chips earlier than Haswell. That's a
        // bit frustrating.
        //
        // The dot production instruction has low throughput but very
        // high latency. That means we need to interleave a number of 
        // transforms in order to get the best performance. Actually, compiler
        // generated optimization should be better for doing that. But 
        // I'm currently using a compiler that doesn't seem to generate that
        // instruction (so, doing it by hand).
        //
        // We can separate the test for each point into 2 parts;
        //      1. the matrix * vector multiply
        //      2. comparing the result against the edges of the frustum
        //
        // The 1st part has a high latency. But the latency values for the
        // second part are much smaller. The second part is much more compact
        // and easier to optimise. It makes sense to do 2 points in parallel,
        // to cover the latency of the 1st part with the calculations from the
        // 2nd part.
        //
        // However, we have a bit of problem with register counts! We need a lot
        // of registers. Visual Studio 2010 is only using 8 xmm registers, which is
        // not really enough. We need 16 registers to do this well. But it seems that
        // we can only have 16 register in x64 mode.
        //
        //       0-3  : matrix
        //       4xyz : clipSpaceXYZ
        //       5xyz : clipSpaceWWW (then -clipSpaceWWW)
        //       6    : utility
        //       7xyz : andUpper
        //       8xyz : andLower
        //       9xyz : orUpperAndLower
        //      10    : abuv
        //      11    : cw11
        //
        // If we want to do multiple corners at the same time, it's just going to
        // increase the registers we need.
        //
        //  What this means is the idea situation depends on the hardware we're
        //  targeting!
        //      1. x86 Haswell+ --> fused-multiply-add
        //      2. x64 --> dot product with 16 xmm registers
        //      3. otherwise, 2-step, transform corners and then clip test
        //
        // One solution is to do the transformation first, and write the result to
        // memory. But this makes it more difficult to cover the latency in the
        // dot product.

        // We can use SSE "shuffle" to load the vectors for each corner.
        //
        //  abc = mins[0,1,2]
        //  uvw = maxs[0,1,2]
        //
        //  r0 = abuv
        //  r1 = cw,1,1
        //
        //  abc, abw
        //  ubc, ubw
        //  avc, avw
        //  uvc, uvw
        //
        //  abc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 1, 0));
        //  ubc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 1, 2));
        //  avc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 3, 0));
        //  uvc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 3, 2));
        //
        //  abw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 1, 0));
        //  ubw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 1, 2));
        //  avw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 3, 0));
        //  uvw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 3, 2));
        //
        // Now we need to do the matrix multiply & perspective divide
        // then we have to compare the results against 0 and 1 and 
        // do some binary comparisons.

        assert((size_t(localToProjection) & 0xf) == 0);
        auto abuv = _mm_set_ps(maxs[1], maxs[0], mins[1], mins[0]);   // (note; using WZYX order)
        auto cw11 = _mm_set_ps(1.f, 1.f, maxs[2], mins[2]);

        __m128 corners[8];
        corners[0] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 1, 0));
        corners[1] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 1, 2));
        corners[2] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 3, 0));
        corners[3] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 3, 2));
        corners[4] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 1, 0));
        corners[5] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 1, 2));
        corners[6] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 3, 0));
        corners[7] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 3, 2));

        auto A0 = _mm_load_ps(localToProjection +  0);
        auto A1 = _mm_load_ps(localToProjection +  4);
        auto A2 = _mm_load_ps(localToProjection +  8);
        auto A3 = _mm_load_ps(localToProjection + 12);

        __declspec(align(16)) Float4 cornerClipSpaceXYZ[8];
        __declspec(align(16)) Float4 cornerClipW[8];

            //  We want to interleave projection calculations for multiple vectors in 8 registers
            //  We have very few registers. So we need to separate the calculation of the "W" part
            //  This will mean having to duplicate the "shuffle" instruction. But this is a very
            //  cheap instruction.

        TestAABB_SSE_TransCorner(
            corners[0], corners[1],
            A0, A1, A2, &cornerClipSpaceXYZ[0][0]);
        TestAABB_SSE_TransCorner(
            corners[2], corners[3],
            A0, A1, A2, &cornerClipSpaceXYZ[2][0]);
        TestAABB_SSE_TransCorner(
            corners[4], corners[5],
            A0, A1, A2, &cornerClipSpaceXYZ[4][0]);
        TestAABB_SSE_TransCorner(
            corners[6], corners[7],
            A0, A1, A2, &cornerClipSpaceXYZ[6][0]);

            //  Now do the "W" parts.. Do 4 at a time to try to cover latency
        {
            auto abc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 1, 0));
            auto w0 = _mm_dp_ps(A3, abc1, (0xF<<4)|( 0xF));
            auto ubc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 1, 2));
            auto w1 = _mm_dp_ps(A3, ubc1, (0xF<<4)|( 0xF));
            auto avc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 3, 0));
            auto w2 = _mm_dp_ps(A3, avc1, (0xF<<4)|( 0xF));
            auto uvc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 3, 2));
            auto w3 = _mm_dp_ps(A3, uvc1, (0xF<<4)|( 0xF));

            _mm_store_ps(&cornerClipW[0][0], w0);
            _mm_store_ps(&cornerClipW[1][0], w1);
            _mm_store_ps(&cornerClipW[2][0], w2);
            _mm_store_ps(&cornerClipW[3][0], w3);

            auto abw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 1, 0));
            auto w4 = _mm_dp_ps(A3, abw1, (0xF<<4)|( 0xF));
            auto ubw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 1, 2));
            auto w5 = _mm_dp_ps(A3, ubw1, (0xF<<4)|( 0xF));
            auto avw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 3, 0));
            auto w6 = _mm_dp_ps(A3, avw1, (0xF<<4)|( 0xF));
            auto uvw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 3, 2));
            auto w7 = _mm_dp_ps(A3, uvw1, (0xF<<4)|( 0xF));

            _mm_store_ps(&cornerClipW[4][0], w4);
            _mm_store_ps(&cornerClipW[5][0], w5);
            _mm_store_ps(&cornerClipW[6][0], w6);
            _mm_store_ps(&cornerClipW[7][0], w7);
        }

            // Now compare with screen edges and calculate the bit masks

        __declspec(align(16)) unsigned andInitializer[] = { 0xffffffff, 0xffffffff, 0xffffffff, 0 };
        assert((size_t(andInitializer) & 0xf) == 0);

        auto andUpper = _mm_load_ps((const float*)andInitializer);
        auto andLower = _mm_load_ps((const float*)andInitializer);
        auto orUpperLower = _mm_setzero_ps();

        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[0][0], &cornerClipW[0][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[1][0], &cornerClipW[1][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[2][0], &cornerClipW[2][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[3][0], &cornerClipW[3][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[4][0], &cornerClipW[4][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[5][0], &cornerClipW[5][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[6][0], &cornerClipW[6][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[7][0], &cornerClipW[7][0], andUpper, andLower, orUpperLower);

            // Get the final result...

        andUpper        = _mm_hadd_ps(andLower,     andUpper);
        orUpperLower    = _mm_hadd_ps(orUpperLower, orUpperLower);
        andUpper        = _mm_hadd_ps(andUpper,     andUpper);
        orUpperLower    = _mm_hadd_ps(orUpperLower, orUpperLower);
        andUpper        = _mm_hadd_ps(andUpper,     andUpper);

        __declspec(align(16)) unsigned andResult[4];
        __declspec(align(16)) unsigned orUpperLowerResult[4];
        assert((size_t(andResult) & 0xf) == 0);
        assert((size_t(orUpperLowerResult) & 0xf) == 0);

        _mm_store_ps((float*)orUpperLowerResult, orUpperLower);
        _mm_store_ps((float*)andResult, andUpper);

        if (andResult[0])           { return CullTestResult::Culled; }
        if (orUpperLowerResult[0])  { return CullTestResult::Boundary; }
        return CullTestResult::Within;
    }
    
#endif

    static inline Float4 XYZProj(const Float4x4& localToProjection, const Float3 input)
    {
        return localToProjection * Expand(input, 1.f);
    }

    static CullTestResult TestAABB_Basic(const Float4x4& localToProjection, const Float3& mins, const Float3& maxs, ClipSpaceType clipSpaceType)
    {
            //  for the box to be culled, all points must be outside of the same bounding box
            //  plane... We can do this in clip space (assuming we can do a fast position transform on
            //  the CPU). We can also do this in world space by finding the planes of the frustum, and
            //  comparing each corner point to each plane.
            //
            // This method is quite fast and conveninent, but isn't actually 100% correct. There are some cases where 
            // the bounding box is straddling a plane, but all points that are on the inside of that plane are 
            // still outside of the frustum. Ie, it's just the box is just diagonally off an edge or corner of the frustum.
            // This is a lot more likely with large bounding boxes -- in those cases we should do a more accurate (and
            // more expensive) test
        Float3 corners[8] = 
        {
            Float3(mins[0], mins[1], mins[2]),
            Float3(maxs[0], mins[1], mins[2]),
            Float3(mins[0], maxs[1], mins[2]),
            Float3(maxs[0], maxs[1], mins[2]),

            Float3(mins[0], mins[1], maxs[2]),
            Float3(maxs[0], mins[1], maxs[2]),
            Float3(mins[0], maxs[1], maxs[2]),
            Float3(maxs[0], maxs[1], maxs[2])
        };

        Float4 projectedCorners[8];
        projectedCorners[0] = XYZProj(localToProjection, corners[0]);
        projectedCorners[1] = XYZProj(localToProjection, corners[1]);
        projectedCorners[2] = XYZProj(localToProjection, corners[2]);
        projectedCorners[3] = XYZProj(localToProjection, corners[3]);
        projectedCorners[4] = XYZProj(localToProjection, corners[4]);
        projectedCorners[5] = XYZProj(localToProjection, corners[5]);
        projectedCorners[6] = XYZProj(localToProjection, corners[6]);
        projectedCorners[7] = XYZProj(localToProjection, corners[7]);

        bool leftAnd = true, rightAnd = true, topAnd = true, bottomAnd = true, nearAnd = true, farAnd = true;
        bool leftOr = false, rightOr = false, topOr = false, bottomOr = false, nearOr = false, farOr = false;
        for (unsigned c=0; c<8; ++c) {
            leftAnd     &= (projectedCorners[c][0] < -projectedCorners[c][3]);
            rightAnd    &= (projectedCorners[c][0] >  projectedCorners[c][3]);
            topAnd      &= (projectedCorners[c][1] < -projectedCorners[c][3]);
            bottomAnd   &= (projectedCorners[c][1] >  projectedCorners[c][3]);
            farAnd      &= (projectedCorners[c][2] >  projectedCorners[c][3]);

            leftOr      |= (projectedCorners[c][0] < -projectedCorners[c][3]);
            rightOr     |= (projectedCorners[c][0] >  projectedCorners[c][3]);
            topOr       |= (projectedCorners[c][1] < -projectedCorners[c][3]);
            bottomOr    |= (projectedCorners[c][1] >  projectedCorners[c][3]);
            farOr       |= (projectedCorners[c][2] >  projectedCorners[c][3]);
        }
        
        if (    clipSpaceType == ClipSpaceType::Positive || clipSpaceType == ClipSpaceType::PositiveRightHanded
            ||  clipSpaceType == ClipSpaceType::Positive_ReverseZ || clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ) {
            for (unsigned c=0; c<8; ++c) {
                nearOr      |= (projectedCorners[c][2] < 0.f);
                nearAnd     &= (projectedCorners[c][2] < 0.f);
            }
        } else if (clipSpaceType == ClipSpaceType::StraddlingZero) {
            for (unsigned c=0; c<8; ++c) {
                nearOr      |= (projectedCorners[c][2] < -projectedCorners[c][3]);
                nearAnd     &= (projectedCorners[c][2] < -projectedCorners[c][3]);
            }
        } else {
            assert(0);  // unsupported clip space type
        }
        
        if (leftAnd | rightAnd | topAnd | bottomAnd | nearAnd | farAnd) {
            return CullTestResult::Culled;
        }
        if (leftOr | rightOr | topOr | bottomOr | nearOr | farOr) {
            return CullTestResult::Boundary;
        }
        return CullTestResult::Within;
    }

    CullTestResult TestAABB(
        const Float4x4& localToProjection, 
        const Float3& mins, const Float3& maxs,
        ClipSpaceType clipSpaceType)
    {
        return TestAABB_Basic(localToProjection, mins, maxs, clipSpaceType);
    }

    CullTestResult TestAABB_Aligned(
        const Float4x4& localToProjection, 
        const Float3& mins, const Float3& maxs,
        ClipSpaceType clipSpaceType)
    {
#if defined(HAS_SSE_INSTRUCTIONS)
        assert(clipSpaceType == ClipSpaceType::Positive || clipSpaceType == ClipSpaceType::PositiveRightHanded
            || clipSpaceType == ClipSpaceType::Positive_ReverseZ || clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ);
        return TestAABB_SSE(AsFloatArray(localToProjection), mins, maxs);
#else
        return TestAABB(localToProjection, mins, maxs, clipSpaceType);
#endif
    }

    constexpr unsigned ToFaceBitField(unsigned faceOne, unsigned faceTwo) { return (1<<faceOne) | (1<<faceTwo); }
    constexpr unsigned ToFaceBitField(unsigned faceOne, unsigned faceTwo, unsigned faceThree) { return (1<<faceOne) | (1<<faceTwo) | (1<<faceThree); }

    CullTestResult AccurateFrustumTester::TestSphere(Float3 centerPoint, float radius)
    {
        // This actually tests an axially aligned bounding box that just contains the sphere against the 
        // frustum. It's quick, but not completely accurate. But many cases can accurately be found to be
        // completely within, or completely without, by using this method
        auto quickTest = TestAABB(
            _localToProjection, 
            centerPoint - Float3{radius, radius, radius},
            centerPoint + Float3{radius, radius, radius},
            _clipSpaceType);
        if (quickTest != CullTestResult::Boundary) {
            return quickTest;
        }

        unsigned straddlingFlags = 0;
        Float3 intersectionCenters[6];
        for (unsigned f=0; f<6; ++f) {
            auto distance = SignedDistance(centerPoint, _frustumPlanes[f]);
            if (__builtin_expect(distance >= radius, false)) {
                return CullTestResult::Culled;        // this should be rare given the quick test above
            }
            straddlingFlags |= (distance > -radius) << f;
            intersectionCenters[f] = centerPoint - distance * Truncate(_frustumPlanes[f]);
        }
        if (!straddlingFlags) {
            return CullTestResult::Within;
        }

        // Check each corner -- 
        // This is cheap to do, and if it's inside, then we know we've got a intersection
        const unsigned faceBitFieldForCorner[] {
            ToFaceBitField(1, 2, 4),
            ToFaceBitField(1, 3, 4),
            ToFaceBitField(0, 2, 4),
            ToFaceBitField(0, 3, 4),

            ToFaceBitField(1, 2, 5),
            ToFaceBitField(1, 3, 5),
            ToFaceBitField(0, 2, 5),
            ToFaceBitField(0, 3, 5)
        };

        const float radiusSq = radius*radius;

        for (unsigned cIdx=0; cIdx<dimof(faceBitFieldForCorner); cIdx++) {
            auto faceBitMask = faceBitFieldForCorner[cIdx];
            if (__builtin_expect((straddlingFlags & faceBitMask) != faceBitMask, true)) continue;
            // the sphere is straddling all 3 edges of this corner. Check if it's
            // inside of the sphere
            if (__builtin_expect(MagnitudeSquared(_frustumCorners[cIdx] - centerPoint) < radiusSq, true)) {
                return CullTestResult::Boundary;
            }
        }

        // Check the non-aligned faces for any intersection centers we got. If it's inside
        // all, then the sphere does intersect the frustum
        // All faces have a "pair" (ie, front and back, left and right, top and bottom). The
        // non-aligned faces are just the ones other than a given face and it's pair
        struct NonAlignedFaces { unsigned _face0, _face1, _face2, _face3; };
        const NonAlignedFaces nonAlignedFaces[] {
            { 2, 4, 3, 5 },
            { 2, 4, 3, 5 },

            { 0, 5, 1, 4 },
            { 0, 5, 1, 4 },

            { 0, 3, 1, 2 },
            { 0, 3, 1, 2 }        
        };
        for (unsigned f=0; f<6; ++f) {
            if (__builtin_expect(!(straddlingFlags & (1<<f)), true)) continue;
            auto intersectionCenter = intersectionCenters[f];
            auto naFaces = nonAlignedFaces[f];
            unsigned withinCount = 0;
            withinCount += SignedDistance(intersectionCenter, _frustumPlanes[naFaces._face0]) < 0.f;
            withinCount += SignedDistance(intersectionCenter, _frustumPlanes[naFaces._face1]) < 0.f;
            withinCount += SignedDistance(intersectionCenter, _frustumPlanes[naFaces._face2]) < 0.f;
            withinCount += SignedDistance(intersectionCenter, _frustumPlanes[naFaces._face3]) < 0.f;
            if (withinCount == 4) {
                return CullTestResult::Boundary;
            }
        }

        struct Edge
        {
            unsigned _cornerZero, _cornerOne;
            unsigned _faceBitField;
        };
        const Edge faceBitFieldForEdge[] {
            // ringing around front
            Edge { 0, 1,      ToFaceBitField(4, 1) },
            Edge { 1, 3,      ToFaceBitField(4, 3) },
            Edge { 3, 2,      ToFaceBitField(4, 0) },
            Edge { 2, 0,      ToFaceBitField(4, 2) },

            // ringing around back
            Edge { 4, 6,      ToFaceBitField(5, 2) },
            Edge { 6, 7,      ToFaceBitField(5, 0) },
            Edge { 7, 5,      ToFaceBitField(5, 3) },
            Edge { 5, 4,      ToFaceBitField(5, 1) },

            // joining front to back
            Edge { 0, 4,      ToFaceBitField(2, 1) },
            Edge { 1, 5,      ToFaceBitField(1, 3) },
            Edge { 3, 7,      ToFaceBitField(3, 0) },
            Edge { 2, 6,      ToFaceBitField(0, 2) }
        };

        for (auto e:faceBitFieldForEdge) {
            if (__builtin_expect((straddlingFlags & e._faceBitField) != e._faceBitField, true)) continue;
            // the sphere is straddling both planes of this edge. Check the edge to see
            // if it intersects the sphere
            if (RayVsSphere(_frustumCorners[e._cornerZero] - centerPoint, _frustumCorners[e._cornerOne] - centerPoint, radiusSq)) {
                return CullTestResult::Boundary;
            }
        }

        // The sphere is on 2 sides of at least one plane... However, for all of those planes:
        //      . the point on the plane closest to the sphere center is outside of the frustum
        //      . the sphere does not intersect with any edges
        //      . the sphere does not contain any corners
        // Therefore, we'll conclude that this sphere is outside of the frustum
        return CullTestResult::Culled;
    }

    AccurateFrustumTester::AccurateFrustumTester(const Float4x4& localToProjection, ClipSpaceType clipSpaceType)
    : _localToProjection(localToProjection)
    , _clipSpaceType(clipSpaceType)
    {
        // Decompose the frustum into a set of planes. We'll do this such that the normal
        // points outwards.
        // planes 5-6 are the near & far
        CalculateAbsFrustumCorners(_frustumCorners, localToProjection, clipSpaceType);

        // There are a bunch of potential ways we can fit these planes
        // 1. use 3 point input version of PlaneFit and frustum corners calculated from worldToProjection
        // 2. use 4 point input version of PlaneFit (should give us a nicer fit)
        // 3. calculate from the parameters used to construct the projection matrix (ie, verticalFOV, etc)
        // Method 3 might actually be most efficient & also most numerically stable... but at the same time, 
        // building from an arbitrary input transformation seems convenient
        _frustumPlanes[0] = PlaneFit(_frustumCorners[2], _frustumCorners[3], _frustumCorners[7]);      // +X (note, these are based on an identity view matrix)
        _frustumPlanes[1] = PlaneFit(_frustumCorners[1], _frustumCorners[0], _frustumCorners[4]);      // -X
        _frustumPlanes[2] = PlaneFit(_frustumCorners[0], _frustumCorners[2], _frustumCorners[6]);      // +Y
        _frustumPlanes[3] = PlaneFit(_frustumCorners[3], _frustumCorners[1], _frustumCorners[5]);      // -Y
        _frustumPlanes[4] = PlaneFit(_frustumCorners[0], _frustumCorners[1], _frustumCorners[3]);      // +Z
        _frustumPlanes[5] = PlaneFit(_frustumCorners[6], _frustumCorners[7], _frustumCorners[5]);      // -Z
    }

    AccurateFrustumTester::~AccurateFrustumTester()
    {

    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    CullTestResult ArbitraryConvexVolumeTester::TestSphere(Float3 centerPoint, float radius)
    {
        uint64_t straddlingFlags = 0;
        auto planeCount = (uint64_t)_planes.size();
        Float3 intersectionCenters[planeCount];
        for (uint64_t f=0; f<planeCount; ++f) {
            auto distance = SignedDistance(centerPoint, _planes[f]);
            if (__builtin_expect(distance >= radius, false)) {
                return CullTestResult::Culled;
            }
            straddlingFlags |= uint64_t(distance > -radius) << f;
            intersectionCenters[f] = centerPoint - distance * Truncate(_planes[f]);
        }
        if (!straddlingFlags) {
            return CullTestResult::Within;
        }

        // Check each corner -- 
        // This is cheap to do, and if it's inside, then we know we've got a intersection
        const float radiusSq = radius*radius;
        for (unsigned cIdx=0; cIdx<_cornerFaceBitMasks.size(); cIdx++) {
            auto faceBitMask = _cornerFaceBitMasks[cIdx];
            if (__builtin_expect((straddlingFlags & faceBitMask) != faceBitMask, true)) continue;
            // the sphere is straddling all 3 edges of this corner. Check if it's
            // inside of the sphere
            if (__builtin_expect(MagnitudeSquared(_corners[cIdx] - centerPoint) < radiusSq, true)) {
                return CullTestResult::Boundary;
            }
        }

        // Check the faces for any intersection centers we got. If it's inside
        // all, then the sphere does intersect the frustum
        for (unsigned f=0; f<planeCount; ++f) {
            if (__builtin_expect(!(straddlingFlags & (1ull<<f)), true)) continue;
            auto intersectionCenter = intersectionCenters[f];
            unsigned qf=0;
            for (; qf<planeCount; ++qf)
                if (qf != f && SignedDistance(intersectionCenter, _planes[qf]) > 0.f) break;
            if (qf == planeCount)
                return CullTestResult::Boundary;
        }

        for (auto e:_edges) {
            if (__builtin_expect((straddlingFlags & e._faceBitMask) != e._faceBitMask, true)) continue;
            // the sphere is straddling both planes of this edge. Check the edge to see
            // if it intersects the sphere
            if (RayVsSphere(_corners[e._cornerZero] - centerPoint, _corners[e._cornerOne] - centerPoint, radiusSq)) {
                return CullTestResult::Boundary;
            }
        }

        // The sphere is on 2 sides of at least one plane... However, for all of those planes:
        //      . the point on the plane closest to the sphere center is outside of the frustum
        //      . the sphere does not intersect with any edges
        //      . the sphere does not contain any corners
        // Therefore, we'll conclude that this sphere is outside of the frustum
        return CullTestResult::Culled;
    }

    CullTestResult ArbitraryConvexVolumeTester::TestAABB(
        const Float3x4& aabbToLocalSpace, 
        Float3 mins, Float3 maxs)
    {
        assert(mins[0] <= maxs[0] && mins[1] <= maxs[1] && mins[2] <= maxs[2]);

        // Is it better to do calculations in AABB space, or in local space?
        // we can effectively do volume vs box or box vs volume...
        // it might depend on the complexity of the volume -- probably we should
        // assume it usually has more corners/planes than a box, though
        // But then again, the box will usually be smaller, and we're far more likely to
        // get a full rejection if we compare the box vs all of the volume planes first...

        Float3 boxCornersLocalSpace[] {
            aabbToLocalSpace * Float4{mins[0], mins[1], mins[2], 1.0f},
            aabbToLocalSpace * Float4{maxs[0], mins[1], mins[2], 1.0f},
            aabbToLocalSpace * Float4{mins[0], maxs[1], mins[2], 1.0f},
            aabbToLocalSpace * Float4{maxs[0], maxs[1], mins[2], 1.0f},
                
            aabbToLocalSpace * Float4{mins[0], mins[1], maxs[2], 1.0f},
            aabbToLocalSpace * Float4{maxs[0], mins[1], maxs[2], 1.0f},
            aabbToLocalSpace * Float4{mins[0], maxs[1], maxs[2], 1.0f},
            aabbToLocalSpace * Float4{maxs[0], maxs[1], maxs[2], 1.0f}
        };

        uint64_t straddlingFlags = 0;
        auto planeCount = (unsigned)_planes.size();
        Float3 intersectionCenters[planeCount];
        for (uint64_t f=0; f<planeCount; ++f) {

            unsigned outsideCount = 0;
            for (unsigned c=0; c<dimof(boxCornersLocalSpace); ++c)
                outsideCount += SignedDistance(boxCornersLocalSpace[c], _planes[f]) > 0.f;
            
            if (outsideCount == dimof(boxCornersLocalSpace))
                return CullTestResult::Culled;

            straddlingFlags |= uint64_t(outsideCount != 0) << f;
        }
        if (!straddlingFlags) {
            return CullTestResult::Within;
        }

        for (unsigned cIdx=0; cIdx<_cornerFaceBitMasks.size(); cIdx++) {
            auto faceBitMask = _cornerFaceBitMasks[cIdx];
            if (__builtin_expect((straddlingFlags & faceBitMask) != faceBitMask, true)) continue;

            Float3 aabbSpaceCorner = TransformPointByOrthonormalInverse(aabbToLocalSpace, _corners[cIdx]);
            bool inside = 
                  (aabbSpaceCorner[0] >= mins[0]) & (aabbSpaceCorner[0] <= maxs[0])
                & (aabbSpaceCorner[1] >= mins[1]) & (aabbSpaceCorner[1] <= maxs[1])
                & (aabbSpaceCorner[2] >= mins[2]) & (aabbSpaceCorner[2] <= maxs[2])
                ;
            if (__builtin_expect(inside, true)) {
                return CullTestResult::Boundary;
            }
        }

        // For each "straddling" face of this volume, check every edge of the aabb
        // and find the intersection points. If the intersection point is inside all other
        // volume planes, we know there is a real intersection
        // this part is where it starts to get pretty calculation heavy!
        const UInt2 aabbEdges[] = {
            { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
            { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 0 },
            { 0, 4 }, { 1, 5 }, { 3, 7 }, { 2, 6 }
        };

        for (uint64_t f=0; f<planeCount; ++f) {
            if (__builtin_expect(!(straddlingFlags & (1ull<<f)), true)) continue;

            // The bounding volume is on both sides of this plane -- but is the intersection point point actually within the finite face area?
            uint64_t surroundingFaceMask = 0;
            for (auto e:_edges)
                if (e._faceBitMask & (1ull<<f)) surroundingFaceMask |= e._faceBitMask;      // note -- this probably could be precalculated for each face
            surroundingFaceMask &= ~(1ull<<f);
            assert(surroundingFaceMask);
            for (auto aabbEdge:aabbEdges) {
                float A = SignedDistance(boxCornersLocalSpace[aabbEdge[0]], _planes[f]);
                float B = SignedDistance(boxCornersLocalSpace[aabbEdge[1]], _planes[f]);
                if ((A > 0.f) == (B > 0.f)) continue;
                Float3 intr = LinearInterpolate(boxCornersLocalSpace[aabbEdge[0]], boxCornersLocalSpace[aabbEdge[1]], -A / (B-A));
                // we're only checking the faces that share an edge here
                uint64_t qf=surroundingFaceMask;
                for (;;) {
                    if (!qf) return CullTestResult::Boundary;
                    auto bit = xl_ctz8(qf);
                    if (SignedDistance(intr, _planes[bit]) > 0.f) break;
                    qf ^= 1<<bit;
                }
            }
        }

        for (auto e:_edges) {
            if (__builtin_expect((straddlingFlags & e._faceBitMask) != e._faceBitMask, true)) continue;
            // the sphere is straddling both planes of this edge. Check the edge to see
            // if it intersects the sphere
            Float3 aabbSpaceStart = TransformPointByOrthonormalInverse(aabbToLocalSpace, _corners[e._cornerZero]);
            Float3 aabbSpaceEnd = TransformPointByOrthonormalInverse(aabbToLocalSpace, _corners[e._cornerOne]);
            if (RayVsAABB({aabbSpaceStart, aabbSpaceEnd}, mins, maxs)) {
                return CullTestResult::Boundary;
            }
        }

        return CullTestResult::Culled;
    }

    ArbitraryConvexVolumeTester::ArbitraryConvexVolumeTester(
        std::vector<Float4>&& planes,
        std::vector<Float3>&& corners,
        std::vector<Edge>&& edges,
        std::vector<unsigned>&& cornerFaceBitMasks)
    : _planes(std::move(planes))
    , _corners(std::move(corners))
    , _edges(std::move(edges))
    , _cornerFaceBitMasks(std::move(cornerFaceBitMasks))
    {
        assert(_corners.size() == _cornerFaceBitMasks.size());
        assert(_planes.size() <= 64);    // using uint64_t bit masks, so only up to 64 faces supported
    }

    static unsigned MapIdx(std::vector<unsigned>& mapping, unsigned vertexIndex)
    {
        for (unsigned c=0; c<mapping.size(); ++c)
            if (mapping[c] == vertexIndex) return c;
        mapping.push_back(vertexIndex);   
        return mapping.size()-1;
    }

    ArbitraryConvexVolumeTester ExtrudeFrustumOrthogonally(
        const Float4x4& localToClipSpaceInit,
        Float3 extrusionDirectionLocal,
        float extrusionLength,
        ClipSpaceType clipSpaceType)
    {
        // Create a convex hull that represents the given projection frustum, but extruded
        // orthogonally in the given direction.
        // The hull will begin on the faces of the frustum that are "against" the direction
        // (or just facing the other way) and will end in a plane orthogonal to the extrusion
        // direction and the given length away from the origin point of localToClipSpace (ie,
        // typically the eye position)
        // We could also make it open-ended, I guess, which would avoid the need for an extra
        // clippping plane to test against

        // todo - consider separating projection matrix from view matrix on input, because that
        // would allow us to remove the eye position from the math, which would potentially be
        // more stable in a lot of cases
        // Alternatively, the client may be able to handle coordinates spaces even better

        auto localToClipSpace = localToClipSpaceInit;
        assert(Equivalent(Magnitude(extrusionDirectionLocal), 1.f, 1e-3f));     // expecting normalized input

        // order of the frustum corners:
        //  x=-1, yAtTop
        //  x=-1, yAtBottom
        //  x= 1, yAtTop
        //  x= 1, yAtBottom
        // and then corners for far plane in the same order
        Float3 frustumCorners[8];
        CalculateAbsFrustumCorners(frustumCorners, localToClipSpace, clipSpaceType);
        struct Face { unsigned v0, v1, v2, v3; };
        Face frustumFaces[] {       // vertices should be in CCW winding for facing away from the frustum
            Face { 0, 1, 3, 2 },    // [0] front
            Face { 4, 6, 7, 5 },    // [1] back
            Face { 1, 0, 4, 5 },    // [2] x=-1
            Face { 2, 3, 7, 6 },    // [3] x= 1
            Face { 0, 2, 6, 4 },    // [4] top
            Face { 3, 1, 5, 7 }     // [5] bottom
        };
        struct Edge { unsigned f0, f1, v0, v1; };
        Edge frustumEdges[] {
            { 0, 4, 2, 0 },        // front & top
            { 0, 2, 0, 1 },        // front & x=-1
            { 0, 5, 1, 3 },        // front & bottom
            { 0, 3, 3, 2 },        // front & x=1

            { 1, 4, 4, 6 },        // back & top
            { 1, 3, 6, 7 },        // back & x=1
            { 1, 5, 7, 5 },        // back & bottom
            { 1, 2, 5, 4 },        // back & x=1

            { 2, 4, 0, 4 },        // x=-1 & top
            { 2, 5, 5, 1 },        // x=-1 & bottom

            { 3, 4, 6, 2 },        // x=1 & top
            { 3, 5, 3, 7 }         // x=1 & bottom
        };

        Float3 frustumCenter = frustumCorners[0];
        for (unsigned c=1; c<8; ++c) frustumCenter += frustumCorners[c];
        frustumCenter /= 8.f;

        // 
        bool faceDirections[6];
        Float4 facePlanes[6];
        {
            unsigned fIdx = 0;
            for (const auto& f:frustumFaces) {
                Float3 pts[4];
                pts[0] = frustumCorners[f.v0];
                pts[1] = frustumCorners[f.v1];
                pts[2] = frustumCorners[f.v2];
                pts[3] = frustumCorners[f.v3];
                // facePlanes[fIdx] = PlaneFit(pts, 4);
                facePlanes[fIdx] = PlaneFit(pts[0], pts[1], pts[2]);
                assert(SignedDistance(frustumCenter, facePlanes[fIdx]) < 0.f);
                faceDirections[fIdx++] = Dot(Truncate(facePlanes[fIdx]), extrusionDirectionLocal) > 0;
            }
        }

        std::vector<Float4> finalHullPlanes;
        std::vector<unsigned> cornerMapping;        // cornerMapping[newIndex] == oldIndex
        std::vector<unsigned> faceRevMapping;       // faceRevMapping[oldIndex] == newIndex
        faceRevMapping.resize(6, ~0u);
        std::vector<unsigned> finalHullCornerFaceBitMask;
        std::vector<ArbitraryConvexVolumeTester::Edge> finalHullEdges;

        for (unsigned fIdx=0; fIdx<6; ++fIdx)
            if (!faceDirections[fIdx]) {
                // facing away from the extrusion direction go directly into the final hull
                finalHullPlanes.push_back(facePlanes[fIdx]);
                unsigned newFaceIdx = unsigned(finalHullPlanes.size()-1);
                faceRevMapping[fIdx] = newFaceIdx;
            }

        Float4 farExtrusionPlane = Expand(extrusionDirectionLocal, -extrusionLength);

        struct PendingEdge { unsigned _oldV0, _oldV1, _newFace; };
        std::vector<PendingEdge> pendingEdges;
        pendingEdges.reserve(12);

        for (unsigned eIdx=0; eIdx<12; ++eIdx) {
            // edges along the equator get extruded and become planes
            // correct ordering of the edge vertices is a little complicated, but we can take
            // the easy approach and just ensure that the frustum center is on the right side
            if (faceDirections[frustumEdges[eIdx].f0] != faceDirections[frustumEdges[eIdx].f1]) {
                Float3 pts[4];
                pts[0] = frustumCorners[frustumEdges[eIdx].v0];
                pts[1] = frustumCorners[frustumEdges[eIdx].v1];
                pts[2] = frustumCorners[frustumEdges[eIdx].v0] + extrusionDirectionLocal;
                pts[3] = frustumCorners[frustumEdges[eIdx].v1] + extrusionDirectionLocal;
                // Float4 newPlane = PlaneFit(pts, 4);
                Float4 newPlane = PlaneFit(pts[0], pts[1], pts[2]);
                if (SignedDistance(frustumCenter, newPlane) > 0.f)
                    newPlane = -newPlane;
                finalHullPlanes.push_back(newPlane);
                pendingEdges.push_back({frustumEdges[eIdx].v0, frustumEdges[eIdx].v1, unsigned(finalHullPlanes.size()-1)});
            }
            if (faceDirections[frustumEdges[eIdx].f0] & faceDirections[frustumEdges[eIdx].f1])
                continue;
            // unless both faces are facing along the extrusion direction, we need to add the edge
            // to the final hull. The vertices should already be added an mapped (since at least one
            // face is facing against)
            unsigned newFaceIndex = finalHullPlanes.size() - 1;
            auto mappedEdge = frustumEdges[eIdx];
            mappedEdge.v0 = MapIdx(cornerMapping, mappedEdge.v0);
            mappedEdge.v1 = MapIdx(cornerMapping, mappedEdge.v1);
            mappedEdge.f0 = faceRevMapping[mappedEdge.f0];
            mappedEdge.f1 = faceRevMapping[mappedEdge.f1];
            mappedEdge.f0 = (mappedEdge.f0 != ~0u) ? mappedEdge.f0 : newFaceIndex;
            mappedEdge.f1 = (mappedEdge.f1 != ~0u) ? mappedEdge.f1 : newFaceIndex;
            assert(mappedEdge.f0 != mappedEdge.f1);
            finalHullEdges.push_back({mappedEdge.v0, mappedEdge.v1, (1u<<mappedEdge.f0)|(1u<<mappedEdge.f1)});

            if (finalHullCornerFaceBitMask.size() < cornerMapping.size())
                finalHullCornerFaceBitMask.resize(cornerMapping.size(), 0);
            finalHullCornerFaceBitMask[mappedEdge.v0] |= (1u<<mappedEdge.f0)|(1u<<mappedEdge.f1);
            finalHullCornerFaceBitMask[mappedEdge.v1] |= (1u<<mappedEdge.f0)|(1u<<mappedEdge.f1);
        }

        std::vector<Float3> finalHullCorners;
        finalHullCorners.reserve(cornerMapping.size());
        for (auto idx:cornerMapping)
            finalHullCorners.push_back(frustumCorners[idx]);

        // we should have a ring wrapping around the shape from where the new edges are created
        assert(!pendingEdges.empty());
        std::vector<PendingEdge> pendingEdgeRing;
        pendingEdgeRing.reserve(pendingEdges.size());
        pendingEdgeRing.push_back(*(pendingEdges.end()-1));
        pendingEdges.erase(pendingEdges.end()-1);
        while (!pendingEdges.empty()) {
            auto i = std::find_if(pendingEdges.begin(), pendingEdges.end(), [search=(pendingEdgeRing.end()-1)->_oldV1](const auto& e) { return e._oldV0 == search; });
            if (i != pendingEdges.end()) {
                pendingEdgeRing.push_back(*i);
                pendingEdges.erase(i);
                continue;
            }

            i = std::find_if(pendingEdges.begin(), pendingEdges.end(), [search=(pendingEdgeRing.end()-1)->_oldV1](const auto& e) { return e._oldV1 == search; });
            assert(i != pendingEdges.end());        // ring missing a link
            auto swapped = *i;
            std::swap(swapped._oldV0, swapped._oldV1);
            pendingEdgeRing.push_back(swapped);
            pendingEdges.erase(i);
        }

        // Create all of the vertices and edges, etc, related to the new corners on the farExtrusionPlane
        // It would be nice if we could get away with some of this... Normally this plane is fairly far 
        // away, and maybe perfect accuracy isn't required?
        const unsigned extrusionLimitPlane = finalHullPlanes.size();
        for (unsigned c=0; c<pendingEdgeRing.size(); ++c) {
            auto f0 = pendingEdgeRing[(c+pendingEdgeRing.size()-1)%pendingEdgeRing.size()]._newFace;
            auto f1 = pendingEdgeRing[c]._newFace;

            ArbitraryConvexVolumeTester::Edge newEdge;
            newEdge._cornerZero = MapIdx(cornerMapping, pendingEdgeRing[c]._oldV0);
            newEdge._cornerOne = (unsigned)finalHullCorners.size();     // about to add
            newEdge._faceBitMask = (1u<<f0)|(1u<<f1);
            finalHullEdges.push_back(newEdge);

            Float3 A = frustumCorners[pendingEdgeRing[c]._oldV0], B = frustumCorners[pendingEdgeRing[c]._oldV0] + extrusionDirectionLocal;
            float a = RayVsPlane(A, B, farExtrusionPlane);
            finalHullCorners.push_back(LinearInterpolate(A, B, a));
            finalHullCornerFaceBitMask.push_back((1u<<f0)|(1u<<f1)|(1u<<extrusionLimitPlane));
        }

        finalHullPlanes.push_back(farExtrusionPlane);

        // construct the inputs for the ArbitraryConvexVolumeTester
        /*
        If we could remove and later reapply the eye position, it would look like this
        for (auto& corner:finalHullCorners) corner += eyePosition;
        for (auto& pl:finalHullPlanes) pl[3] -= Dot(eyePosition, Truncate(pl));
        */

        return ArbitraryConvexVolumeTester{
            std::move(finalHullPlanes),
            std::move(finalHullCorners),
            std::move(finalHullEdges),
            std::move(finalHullCornerFaceBitMask)};
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    Float4 ExtractMinimalProjection(const Float4x4& projectionMatrix)
    {
        return Float4(projectionMatrix(0,0), projectionMatrix(1,1), projectionMatrix(2,2), projectionMatrix(2,3));
    }

    bool IsOrthogonalProjection(const Float4x4& projectionMatrix)
    {
        // In an orthogonal projection matrix, the 'w' component should
        // be constant for all inputs.
        // Let's compare the bottom row to 0.f to check this
        return      projectionMatrix(3,0) == 0.f
                &&  projectionMatrix(3,1) == 0.f
                &&  projectionMatrix(3,2) == 0.f;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    Float4x4 PerspectiveProjection(
        float verticalFOV, float aspectRatio,
        float nearClipPlane, float farClipPlane,
        GeometricCoordinateSpace coordinateSpace,
        ClipSpaceType clipSpaceType )
    {

            //
            //      Generate a perspective projection matrix with the given
            //      parameters.
            //
            //      Note that we have a few things to consider:
            //
            //          Depth range for homogeneous clip space:
            //              OpenGL defines valid clip space depths as -w<z<w
            //              But in DirectX, we need to use 0<z<w
            //              (in other words, OpenGL straddles 0, while DirectX doesn't)
            //          It's a bit odd, but we're kind of stuck with it.
            //
            //      We're assuming the "camera forward" direction as -Z in camera
            //      space. This is the Collada standard... I'm sure how common that
            //      is.
            //
            //      After transformation, +Z will be away from the viewer.
            //      (ie, increasing Z values mean greater depth)
            //
            //      The caller can choose a left handed or right handed coordinate system
            //      (this will just flip the image horizontally).
            //  
            //      We always use "verticalFOV" and an aspect ratio to define the
            //      viewing angles. This tends to make the most sense to the viewer when
            //      they are (for example) resizing a window. In that case, normally
            //      verticalFOV should stay static, while the aspect ratio will change
            //      (ie horizontal viewing angle will adapt to the dimensions of the window)
            //
            //      BTW, verticalFOV should be in radians, and is the half angle
            //      (ie, it's the angle between the centre ray and the edge of the screen, not
            //      from one edge of the screen to the other)
            //
            //      This code doesn't support skewed or off centre projections for multi-screen
            //      output.
            //      See this link for a generalised transform: 
            //              http://csc.lsu.edu/~kooima/pdfs/gen-perspective.pdf
            //

        const float n = nearClipPlane;
        const float h = n * XlTan(.5f * verticalFOV);
        const float w = h * aspectRatio;
        float l, r;
        const float t = h, b = -h;

        if (coordinateSpace == GeometricCoordinateSpace::LeftHanded) {
            l = w; r = -w;
        } else {
            l = -w; r = w;
        }

        return PerspectiveProjection(l, t, r, b, nearClipPlane, farClipPlane, clipSpaceType);
    }

    Float4x4 PerspectiveProjection(
        float l, float t, float r, float b,
        float nearClipPlane, float farClipPlane,
        ClipSpaceType clipSpaceType)
    {
        float n = nearClipPlane;
        float f = farClipPlane;
        assert(n > 0.f);

            // Note --  there's a slight awkward thing here... l, t, r and b
            //          are defined to mean values between -nearClipPlane and +nearClipPlane
            //          it might seem more logical to define them on the range between -1 and 1...?

        Float4x4 result = Identity<Float4x4>();
        result(0,0) =  (2.f * n) / (r-l);
        result(0,2) =  (r+l) / (r-l);

        result(1,1) =  (2.f * n) / (t-b);
        result(1,2) =  (t+b) / (t-b);

        if (clipSpaceType == ClipSpaceType::Positive || clipSpaceType == ClipSpaceType::PositiveRightHanded) {
                //  This is the D3D view of clip space
                //      0<z/w<1
            result(2,2) =    -(f) / (f-n);            // (note z direction flip here as well as below)
            result(2,3) =  -(f*n) / (f-n);
        } else if (clipSpaceType == ClipSpaceType::Positive_ReverseZ || clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ) {
                // as above, but swap Z/W direction for better depth buffer precision in mid and far distance
            std::swap(n, f);
            result(2,2) =    -(f) / (f-n);
            result(2,3) =  -(f*n) / (f-n);
        } else {
            assert(clipSpaceType == ClipSpaceType::StraddlingZero);
                //  This is the OpenGL view of clip space
                //      -1<z/w<1
            result(2,2) =       -(f+n) / (f-n);
            result(2,3) =   -(2.f*f*n) / (f-n);
        }

        result(3,2) =   -1.f;    // (-1 required to flip space around from -Z camera forward to (z/w) increasing with distance)
        result(3,3) =   0.f;

            //
            //      Both OpenGL & DirectX expect a left-handed coordinate system post-projection
            //          +X is right
            //          +Y is up (ie, coordinates are bottom-up)
            //          +Z is into the screen (increasing values are increasing depth, negative depth values are behind the camera)
            //
            //      But Vulkan uses a right handed coordinate system. In this system, +Y points towards
            //      the bottom of the screen.
            //

        if (clipSpaceType == ClipSpaceType::PositiveRightHanded || clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ)
            result(1,1) = -result(1,1);

        return result;
    }
    
    Float4x4 OrthogonalProjection(
        float l, float t, float r, float b,
        float nearClipPlane, float farClipPlane,
        GeometricCoordinateSpace coordinateSpace,
        ClipSpaceType clipSpaceType)
    {
        float n = nearClipPlane;
        float f = farClipPlane;

        if (clipSpaceType == ClipSpaceType::PositiveRightHanded || clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ)
            std::swap(t, b);

        Float4x4 result = Identity<Float4x4>();
        result(0,0) =  2.f / (r-l);
        result(0,3) =  -(r+l) / (r-l);

        result(1,1) =  2.f / (t-b);
        result(1,3) =  -(t+b) / (t-b);

        if (clipSpaceType == ClipSpaceType::Positive || clipSpaceType == ClipSpaceType::PositiveRightHanded) {
                //  This is the D3D view of clip space
                //      0<z/w<1
            result(2,2) =  -1.f / (f-n);            // (note z direction flip here)
            result(2,3) =    -n / (f-n);
        } else if (clipSpaceType == ClipSpaceType::Positive_ReverseZ || clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ) {
                // as above, but swap Z/W direction for better depth buffer precision in mid and far distance
            std::swap(n, f);
            result(2,2) = -1.f / (f-n);
            result(2,3) = -n / (f-n);
        } else {
            assert(clipSpaceType == ClipSpaceType::StraddlingZero);
                //  This is the OpenGL view of clip space
                //      -1<z/w<1
            result(2,2) =     -2.f / (f-n);
            result(2,3) =   -(f+n) / (f-n);
        }

        return result;
    }

    Float4x4 OrthogonalProjection(
        float l, float t, float r, float b,
        float nearClipPlane, float farClipPlane,
        ClipSpaceType clipSpaceType)
    {
        return OrthogonalProjection(
            l, t, r, b, nearClipPlane, farClipPlane,
            GeometricCoordinateSpace::RightHanded, clipSpaceType);
    }

    std::pair<float, float> CalculateNearAndFarPlane(
        const Float4& minimalProjection, ClipSpaceType clipSpaceType)
    {
            // Given a "minimal projection", figure out the near and far plane
            // that was used to create this projection matrix (assuming it was a 
            // perspective projection created with the function 
            // "PerspectiveProjection"
            //
            // Note that the "minimal projection" can be got from a projection
            // matrix using the "ExtractMinimalProjection" function.
            //
            // We just need to do some algebra to reverse the calculations we
            // used to build the perspective transform matrix.
            //
            // For ClipSpaceType::Positive:
            //      miniProj[2] = A = -f / (f-n)
            //      miniProj[3] = B = -(f*n) / (f-n)
            //      C = B / A = n
            //      A * (f-n) = -f
            //      Af - An = -f
            //      Af + f = An
            //      (A + 1) * f = An
            //      f = An / (A+1)
            //        = B / (A+1)
            //
            // For ClipSpaceType::StraddlingZero
            //      miniProj[2] = A = -(f+n) / (f-n)
            //      miniProj[3] = B = -(2fn) / (f-n)
            //      n = B / (A - 1)
            //      f = B / (A + 1)
        const float A = minimalProjection[2];
        const float B = minimalProjection[3];
        if (clipSpaceType == ClipSpaceType::Positive || clipSpaceType == ClipSpaceType::PositiveRightHanded) {
            // This is a slightly more accurate way to calculate the
            // same value as B / (A+1) when A is very near -1.
            return std::make_pair(B / A, 1.f / (A/B + 1.f/B));
        } else if (clipSpaceType == ClipSpaceType::Positive_ReverseZ || clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ) {
            return std::make_pair(1.f / (A/B + 1.f/B), B / A);
        } else {
            return std::make_pair(B / (A - 1.f), B / (A + 1.f));
        }
    }

    std::pair<float, float> CalculateNearAndFarPlane_Ortho(
        const Float4& minimalProjection, ClipSpaceType clipSpaceType)
    {
        // clipSpaceType == ClipSpaceType::Positive || clipSpaceType == ClipSpaceType::PositiveRightHanded
        //  miniProj[2] = A = -1 / (f-n)
        //  miniProj[3] = B = -n / (f-n)
        //  C = B / A = n
        //  A * (f - n) = -1
        //  Af - An + 1 = 0
        //  f = (An - 1) / A
        //
        // clipSpaceType == ClipSpaceType::StraddlingZero
        //  A = -2 / (f-n)
        //  B = -(f+n) / (f-n)
        //  n = (B + 1) / A
        //  f = (B - 1) / A
        const float A = minimalProjection[2];
        const float B = minimalProjection[3];
        if (clipSpaceType == ClipSpaceType::Positive || clipSpaceType == ClipSpaceType::PositiveRightHanded) {
            return std::make_pair(B / A, (B - 1) / A);
        } else if (clipSpaceType == ClipSpaceType::Positive_ReverseZ || clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ) {
            return std::make_pair((B - 1) / A, B / A);
        } else {
            return std::make_pair((B + 1) / A, (B - 1) / A);
        }
    }

    std::pair<float, float> CalculateFov(
        const Float4& minimalProjection, ClipSpaceType clipSpaceType)
    {
        // calculate the vertical field of view and aspect ration from the given
        // standard projection matrix;
        float n, f;
        std::tie(n, f) = CalculateNearAndFarPlane(minimalProjection, clipSpaceType);

        // M(1,1) =  (2.f * n) / (t-b);
        float tmb = (2.f * n) / minimalProjection[1];
        // tmb = 2 * h
        // h = n * XlTan(.5f * verticalFOV);
        // XlTan(.5f * verticalFOV) = h/n
        // verticalFOV = 2.f * XlATan(h/n);
        float verticalFOV = 2.f * XlATan2(tmb/2.f, n);
        float aspect = minimalProjection[1] / minimalProjection[0];
        return std::make_pair(verticalFOV, aspect);
    }

    Float2 CalculateDepthProjRatio_Ortho(
        const Float4& minimalProjection, ClipSpaceType clipSpaceType)
    {
        auto nearAndFar = CalculateNearAndFarPlane_Ortho(minimalProjection, clipSpaceType);
        return Float2(    1.f / (nearAndFar.second - nearAndFar.first),
            -nearAndFar.first / (nearAndFar.second - nearAndFar.first));
    }

    std::pair<Float4x4, Float4x4> CubemapViewAndProjection(
        unsigned cubeFace,
        Float3 centerLocation, float nearClip, float farClip,
        GeometricCoordinateSpace coordinateSpace,
        ClipSpaceType clipSpaceType)
    {
		// Using DirectX conventions for face order here:
		//		+X, -X
		//		+Y, -Y
		//		+Z, -Z
		const Float3 faceForward[] {
			Float3{1.f, 0.f, 0.f},
			Float3{-1.f, 0.f, 0.f},
			Float3{0.f, 1.f, 0.f},
			Float3{0.f, -1.f, 0.f},
			Float3{0.f, 0.f, 1.f},
			Float3{0.f, 0.f, -1.f}
		};
		const Float3 faceUp[] = {
			Float3{0.f, 1.f, 0.f},
			Float3{0.f, 1.f, 0.f},
			Float3{0.f, 0.f, -1.f},
			Float3{0.f, 0.f, 1.f},
			Float3{0.f, 1.f, 0.f},
			Float3{0.f, 1.f, 0.f}
		};
        auto camToWorld = MakeCameraToWorld(faceForward[cubeFace], faceUp[cubeFace], centerLocation);
        // See note in BuildCubemapProjectionDesc(), we usually need the geometric coordinates to be left
        // handed here to get the right result if we want to lookup cubemaps from the shader using world space coordinates
        assert(coordinateSpace == GeometricCoordinateSpace::LeftHanded);
        return {
            InvertOrthonormalTransform(camToWorld),
            PerspectiveProjection(gPI/2.0f, 1.0f, nearClip, farClip, coordinateSpace, clipSpaceType)};
    }

    std::pair<Float3, Float3> BuildRayUnderCursor(
        Int2 mousePosition, 
        Float3 absFrustumCorners[], 
        const std::pair<Float2, Float2>& viewport)
    {
        float u = (float(mousePosition[0]) - viewport.first[0]) / (viewport.second[0] - viewport.first[0]);
        float v = (float(mousePosition[1]) - viewport.first[1]) / (viewport.second[1] - viewport.first[1]);
        float w0 = (1.0f - u) * (1.0f - v);
        float w1 = (1.0f - u) * v;
        float w2 = u * (1.0f - v);
        float w3 = u * v;
        return std::make_pair(
              w0 * absFrustumCorners[0] + w1 * absFrustumCorners[1] + w2 * absFrustumCorners[2] + w3 * absFrustumCorners[3],
              w0 * absFrustumCorners[4] + w1 * absFrustumCorners[5] + w2 * absFrustumCorners[6] + w3 * absFrustumCorners[7]);
    }

    std::pair<Float2, Float2> GetPlanarMinMax(const Float4x4& worldToClip, const Float4& plane, ClipSpaceType clipSpaceType)
    {
        Float3 cameraAbsFrustumCorners[8];
        CalculateAbsFrustumCorners(cameraAbsFrustumCorners, worldToClip, clipSpaceType);

        const std::pair<unsigned, unsigned> edges[] =
        {
            std::make_pair(0, 1), std::make_pair(1, 3), std::make_pair(3, 2), std::make_pair(2, 0),
            std::make_pair(4, 5), std::make_pair(5, 7), std::make_pair(7, 6), std::make_pair(6, 4),
            std::make_pair(0, 4), std::make_pair(1, 5), std::make_pair(2, 6), std::make_pair(3, 7)
        };

        Float2 minIntersection(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), maxIntersection(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
        float intersectionPts[dimof(edges)];
        for (unsigned c=0; c<dimof(edges); ++c) {
            intersectionPts[c] = RayVsPlane(cameraAbsFrustumCorners[edges[c].first], cameraAbsFrustumCorners[edges[c].second], plane);
            if (intersectionPts[c] >= 0.f && intersectionPts[c] <= 1.f) {
                auto intr = LinearInterpolate(cameraAbsFrustumCorners[edges[c].first], cameraAbsFrustumCorners[edges[c].second], intersectionPts[c]);
                minIntersection[0] = std::min(minIntersection[0], intr[0]);
                minIntersection[1] = std::min(minIntersection[1], intr[2]);
                maxIntersection[0] = std::max(maxIntersection[0], intr[0]);
                maxIntersection[1] = std::max(maxIntersection[1], intr[2]);
            }
        }

        return std::make_pair(minIntersection, maxIntersection);
    }
    
    static bool IntersectsWhenProjects(const IteratorRange<const Float3*> obj1, const IteratorRange<const Float3*> obj2, const Float3 &axis) {
        if (MagnitudeSquared(axis) < 0.00001f) {
            return true;
        }
        
        float min1 = std::numeric_limits<float>::max();
        float max1 = -std::numeric_limits<float>::max();
        for (const Float3 &p : obj1) {
            float dist = Dot(p, axis);
            min1 = std::min(min1, dist);
            max1 = std::max(max1, dist);
        }
        
        float min2 = std::numeric_limits<float>::max();
        float max2 = -std::numeric_limits<float>::max();
        for (const Float3 &p : obj2) {
            float dist = Dot(p, axis);
            min2 = std::min(min2, dist);
            max2 = std::max(max2, dist);
        }
        
        return (min1 < max2 && min1 > min2) ||
        (max1 < max2 && max1 > min2) ||
        (min2 < max1 && min2 > min1) ||
        (max2 < max1 && max2 > min1);
    }
    
    // This check is based on the Separating Axis Theorem
    static bool SeparatingAxisTheoremCheck(const std::pair<IteratorRange<const unsigned *>, IteratorRange<const Float3*>> &geometry,
                                           const Float4x4 &projectionMatrix,
                                           ClipSpaceType clipSpaceType) {
        // Convert frustum to world space
        Float3 frustumCorners[8];
        CalculateAbsFrustumCorners(frustumCorners, projectionMatrix, clipSpaceType);
        
        Int3 faceTriangles[6] = {
            {0, 1, 2},
            {4, 6, 5},
            {0, 4, 1},
            {2, 3, 6},
            {1, 5, 3},
            {0, 2, 4}
        };
        
        Int2 frustumEdgeIndexes[12] = {
            {0, 1},
            {0, 2},
            {0, 4},
            {1, 3},
            {1, 5},
            {2, 3},
            {2, 6},
            {3, 7},
            {4, 5},
            {4, 6},
            {5, 7},
            {6, 7}
        };
        
        Float3 frustumNormals[6];
        for (unsigned planeIdx = 0; planeIdx < 6; ++planeIdx) {
            Float3 faceTriangle[3] = {frustumCorners[faceTriangles[planeIdx][0]], frustumCorners[faceTriangles[planeIdx][1]], frustumCorners[faceTriangles[planeIdx][2]]};
            frustumNormals[planeIdx] = Normalize(Cross(faceTriangle[1] - faceTriangle[0], faceTriangle[2] - faceTriangle[0]));
        }
        
        Float3 frustumEdges[12];
        for (unsigned edgeIdx = 0; edgeIdx < 12; ++edgeIdx) {
            frustumEdges[edgeIdx] = frustumCorners[frustumEdgeIndexes[edgeIdx][1]] - frustumCorners[frustumEdgeIndexes[edgeIdx][0]];
        }
        
        auto &indexes = geometry.first;
        auto &vertexes = geometry.second;
        for (unsigned triangleIdx = 0; triangleIdx < indexes.size(); triangleIdx += 3) {
            bool intersects = true;
            Float3 triangle[3];
            for (unsigned vertexIdx = 0; vertexIdx < 3; ++vertexIdx) {
                triangle[vertexIdx] = vertexes[indexes[triangleIdx + vertexIdx]];
            }
            
            if (MagnitudeSquared(triangle[0] - triangle[1]) < 0.00001f ||
                MagnitudeSquared(triangle[0] - triangle[2]) < 0.00001f ||
                MagnitudeSquared(triangle[1] - triangle[2]) < 0.00001f) {
                continue;
            }
            
            for (Float3 axis : frustumNormals) {
                if (!IntersectsWhenProjects(MakeIteratorRange(frustumCorners), MakeIteratorRange(triangle), axis)) {
                    intersects = false;
                    break;
                }
            }
            
            if (!intersects) {
                continue;
            }
            
            Float3 triangleNormal = Normalize(Cross(triangle[1] - triangle[0], triangle[2] - triangle[0]));
            if (!IntersectsWhenProjects(MakeIteratorRange(frustumCorners), MakeIteratorRange(triangle), triangleNormal)) {
                intersects = false;
                continue;
            }
            
            
            for (unsigned triEdgeIdx = 0; triEdgeIdx < 3; ++triEdgeIdx) {
                unsigned endIdx = (triEdgeIdx + 1) % 3;
                Float3 edge = triangle[endIdx] - triangle[triEdgeIdx];
                for (unsigned frustumEdgeIdx = 0; frustumEdgeIdx < 12; ++frustumEdgeIdx) {
                    Float3 axis = Normalize(Cross(frustumEdges[frustumEdgeIdx], edge));
                    if (!IntersectsWhenProjects(MakeIteratorRange(frustumCorners), MakeIteratorRange(triangle), axis)) {
                        intersects = false;
                        break;
                    }
                }
                if (!intersects) {
                    break;
                }
            }
            
            if (!intersects) {
                continue;
            }
            
            return true;
        }
        return false;
    }
    
    bool TestTriangleList(const std::pair<IteratorRange<const unsigned *>, IteratorRange<const Float3*>> &geometry,
                          const Float4x4 &projectionMatrix,
                          ClipSpaceType clipSpaceType) {
        assert(clipSpaceType == ClipSpaceType::Positive || clipSpaceType == ClipSpaceType::StraddlingZero);
        bool allAbove = true;
        bool allBelow = true;
        bool allLeft = true;
        bool allRight = true;
        bool allNear = true;
        bool allFar = true;
        
        unsigned minIdx = std::numeric_limits<unsigned>::max();
        unsigned maxIdx = 0;
        
        for (unsigned idx : geometry.first) {
            minIdx = idx < minIdx ? idx : minIdx;
            maxIdx = idx > maxIdx ? idx : maxIdx;
        }
        
        for (unsigned idx = minIdx; idx <= maxIdx; ++idx) {
            const Float3 &vertex = geometry.second[idx];
            Float4 projected = XYZProj(projectionMatrix, vertex);
            bool left = projected[0] < -projected[3];
            bool right = projected[0] > projected[3];
            bool below = projected[1] < -projected[3];
            bool above = projected[1] > projected[3];
            bool far = projected[2] > projected[3];
            
            bool near = true;
            if (clipSpaceType == ClipSpaceType::Positive) {
                near = projected[2] < 0.f;
            } else if (clipSpaceType == ClipSpaceType::StraddlingZero) {
                near = projected[2] < -projected[3];
            }
            
            if (!left && !right &&
                !above && !below &&
                !near && !far) {
                return true;
            }
            
            allAbove &= above;
            allBelow &= below;
            allLeft &= left;
            allRight &= right;
            allNear &= near;
            allFar &= far;
        }
        
        if (allAbove || allBelow ||
            allLeft || allRight ||
            allNear || allFar) {
            return false;
        }
        
        return SeparatingAxisTheoremCheck(geometry, projectionMatrix, clipSpaceType);
    }
}
