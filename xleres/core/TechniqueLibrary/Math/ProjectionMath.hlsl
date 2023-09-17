// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(TRANSFORM_ALGORITHM_H)
#define TRANSFORM_ALGORITHM_H

#include "Misc.hlsl"
#include "../Framework/SystemUniforms.hlsl"

//
//        Depth transformations
//      --=====-===============--
//
//  In shaders, we deal with "depth" information in a variety of forms, and we often
//  need to convert between forms on the fly. This provides some utility functions to
//  do that.
//
//      "NDC depth"
//          This the depth values that get written into the depth buffer
//          It's depth value post perspective divide, after the perspective
//          transform. NDC depths should always be either between 0 and 1
//          or between -1 and 1 (depending on how NDC space is defined).
//
//          Importantly, it's not linear in world space. There is more
//          precision in NDC depth values near the camera, and little
//          precision in the distance.
//
//      "Linear 0 to 1 depth"
//          This is "linear" depth, meaning that it linear against world
//          space depth values. A difference of "d" between two depth values
//          in this space will always equal a difference of "Rd" in world
//          space, where R is a constant scalar.
//
//          However, the range is still 0 to 1. In this case, 0 is the camera,
//          not the near clip plane! And 1 is the far clip plane.
//
//          Note that in NDCz, 0 is the near clip plane. But in "linear 0 to 1"
//          depth, 0 is the camera!
//
//          This is useful if we have frustum corner vectors. If we have
//          world-space vectors from the camera position to the corners
//          on the far plane clip, we can scale those by the "linear 0 to 1"
//          depth to calculate the correct resolved position in world space.
//          (it's also convenient if we want to pre-calculate linear space
//          depth values and store them in a texture with values normalized
//          between 0 and 1)
//
//      "World space depth"
//          This is a depth values that corresponds to the world space units
//          (and is linear). That is, a different of "d" between two depths
//          values corresponds to exactly "d" world space units.
//
//          Actually, this could more accurate be called "view space depth",
//          because we're really calculating a distance in view space. However,
//          view space should always have the same scale as world space. So
//          distances in view space will have the same magnitude in world space.
//
//          For these depth values, 0 still corresponds to the near clip plane.
//
//  There are a number of conversion functions to go back and forth between
//  representations.
//
//  Let's consider perspective projections first --
//  For perspective projection, we assume a projection matrix of the form:
//          [ X   0   0   0 ]
//          [ 0   Y   0   0 ]
//          [ 0   0   Z   W ]
//          [ 0   0  -1   0 ]
//
//      Here, "float4(X, Y, Z, W)" is called the "minimal projection." This contains
//      enough information to reconstruct the projection matrix in most cases. Note
//      that there are some cases where we might want to use a more complex projection
//      matrix (perhaps to skew in X and Y). But the great majority of perspective
//      projection matrices will have this basic form.
//
//      let:
//          Vz = view space "z" component
//          NDCz = NDC "z" component
//          Pz, Pw = post projection z and w components
//          Lz = linear 0 to 1 depth
//
//      NDCz = Pz / Pw
//      Pz = Z * Vz + W
//      Pw = -Vz
//      NDCz = (Z * Vz + W) / -Vz
//           = -Z - W / Vz
//
//      -W/Vz = NDCz + Z
//      Vz = -W / (NDCz + Z)
//
//      Lz = Vz / f
//      let A = -W/f
//      Lz = A / (NDCz + Z)
//
//  Let's consider orthogonal projections:
//          [ X   0   0   U ]
//          [ 0   Y   0   V ]
//          [ 0   0   Z   W ]
//          [ 0   0   0   1 ]
//
//      (Note that U and V don't fit into the minimal projection!)
//
//      NDCz = Pz / Pw
//      Pz = Z * Vz + W
//      Pw = 1
//      NDCz = Pz
//           = Z * Vz + W
//
//      NDCz - W = Z * Vz
//      Vz = (NDCz - W) / Z
//
//  Argh, written these types of functions in so many different ways for so
//  many different engines. But this will be the last time...! You know why?
//  Because it's open-source!
//

struct MiniProjZW
{
    float Z;
    float W;
};

MiniProjZW AsMiniProjZW(float4 minimalProjection)
{
    MiniProjZW result;
    result.Z = minimalProjection.z;
    result.W = minimalProjection.w;
    return result;
}

MiniProjZW AsMiniProjZW(float2 minimalProjectionZW)
{
    MiniProjZW result;
    result.Z = minimalProjectionZW.x;
    result.W = minimalProjectionZW.y;
    return result;
}

MiniProjZW GlobalMiniProjZW()
{
    return AsMiniProjZW(SysUniform_GetMinimalProjection());
}

///////////////////////////////////////////////////////////////////////////////
    //      P E R S P E C T I V E       //
///////////////////////////////////////////////////////////////////////////////

    float NDCDepthToWorldSpace_Perspective(float NDCz, MiniProjZW miniProj)
    {
            // Note we negate the equation here because view space depths
            // are actually negative (ie, -Z is into the screen in view space).
            // For convenience, we want to return a positive value.
	    return miniProj.W / (NDCz + miniProj.Z);
    }

    float WorldSpaceDepthToNDC_Perspective(float worldSpaceDepth, MiniProjZW miniProj)
    {
            // see note above about negating the equation
	    return miniProj.W / worldSpaceDepth - miniProj.Z;
    }

    float NDCDepthToLinear0To1_Perspective(float NDCz, MiniProjZW miniProj, float farClip)
    {
            // note -- could be optimised by pre-calculating "A" (see above)
	    return NDCDepthToWorldSpace_Perspective(NDCz, miniProj) / farClip;
    }

    float Linear0To1DepthToNDC_Perspective(float worldSpaceDepth, MiniProjZW miniProj, float farClip)
    {
            // note -- could be optimised by pre-calculating "A" (see above)
	    return WorldSpaceDepthToNDC_Perspective(worldSpaceDepth * farClip, miniProj);
    }

///////////////////////////////////////////////////////////////////////////////
    //      O R T H O G O N A L     //
///////////////////////////////////////////////////////////////////////////////

    float NDCDepthToWorldSpace_Ortho(float NDCz, MiniProjZW miniProj)
    {
            // see note above about negating the equation
        return (miniProj.W - NDCz) / miniProj.Z;
    }

    float WorldSpaceDepthToNDC_Ortho(float worldSpaceDepth, MiniProjZW miniProj)
    {
            // see note above about negating the equation
	    return -(miniProj.Z * worldSpaceDepth + miniProj.W);
    }

    float NDCDepthDifferenceToWorldSpace_Ortho(float ndcDepthDifference, MiniProjZW miniProj)
    {
        return -ndcDepthDifference / miniProj.Z;
    }

    float WorldSpaceDepthDifferenceToNDC_Ortho(float worldSpaceDepth, MiniProjZW miniProj)
    {
        return -worldSpaceDepth * miniProj.Z;
    }

    float NDCDepthToLinear0To1_Ortho(float NDCz, MiniProjZW miniProj, float farClip)
    {
            // note -- could be optimised by pre-calculating "A" (see above)
	    return NDCDepthToWorldSpace_Ortho(NDCz, miniProj) / farClip;
    }

    float Linear0To1DepthToNDC_Ortho(float worldSpaceDepth, MiniProjZW miniProj, float farClip)
    {
            // note -- could be optimised by pre-calculating "A" (see above)
	    return WorldSpaceDepthToNDC_Ortho(worldSpaceDepth * farClip, miniProj);
    }

///////////////////////////////////////////////////////////////////////////////
    //      D E F A U L T S     //
///////////////////////////////////////////////////////////////////////////////

    float NDCDepthToWorldSpace(float NDCz)
    {
        if (SysUniform_IsOrthogonalProjection()) {
            return NDCDepthToWorldSpace_Ortho(NDCz, GlobalMiniProjZW());
        } else {
	        return NDCDepthToWorldSpace_Perspective(NDCz, GlobalMiniProjZW());
        }
    }

    float WorldSpaceDepthToNDC(float worldSpaceDepth)
    {
        if (SysUniform_IsOrthogonalProjection()) {
            return WorldSpaceDepthToNDC_Ortho(worldSpaceDepth, GlobalMiniProjZW());
        } else {
	        return WorldSpaceDepthToNDC_Perspective(worldSpaceDepth, GlobalMiniProjZW());
        }
    }

    float NDCDepthToLinear0To1(float NDCz)
    {
        if (SysUniform_IsOrthogonalProjection()) {
            return NDCDepthToLinear0To1_Ortho(NDCz, GlobalMiniProjZW(), SysUniform_GetFarClip());
        } else {
            return NDCDepthToLinear0To1_Perspective(NDCz, GlobalMiniProjZW(), SysUniform_GetFarClip());
        }
    }

    float Linear0To1DepthToNDC(float worldSpaceDepth)
    {
        if (SysUniform_IsOrthogonalProjection()) {
            return Linear0To1DepthToNDC_Ortho(worldSpaceDepth, GlobalMiniProjZW(), SysUniform_GetFarClip());
        } else {
            return Linear0To1DepthToNDC_Perspective(worldSpaceDepth, GlobalMiniProjZW(), SysUniform_GetFarClip());
        }
    }

///////////////////////////////////////////////////////////////////////////////

float3 WorldPositionFromLinear0To1Depth_Perspective(
    float3 viewFrustumVector, float linear0To1Depth,
    float3 viewPosition)
{
    return viewPosition + viewFrustumVector * linear0To1Depth;
}

float3 WorldPositionFromLinear0To1Depth(float3 viewFrustumVector, float linear0To1Depth)
{
    if (!SysUniform_IsOrthogonalProjection()) {
        return SysUniform_GetWorldSpaceView() + viewFrustumVector * linear0To1Depth;
    } else {
        float4x4 cameraBasis = SysUniform_GetCameraBasis();
        float3 negCameraForward = float3(cameraBasis[0].z, cameraBasis[1].z, cameraBasis[2].z);
        return viewFrustumVector + SysUniform_GetWorldSpaceView() + negCameraForward * (SysUniform_GetFarClip() * (1-linear0To1Depth));
    }
}

///////////////////////////////////////////////////////////////////////////////

bool PtInFrustum(float4 pt)
{
	float xyMax = max(abs(pt.x), abs(pt.y));
	return max(xyMax, max(pt.z, pt.w-pt.z)) <= pt.w;
}

bool PtInFrustumXY(float4 pt) 	{ return max(abs(pt.x), abs(pt.y)) <= pt.w; }
bool PtInFrustumZ(float4 pt) 	{ return max(pt.z, pt.w-pt.z) <= pt.w; }

bool InsideFrustum(float4 clipSpacePosition) { return PtInFrustum(clipSpacePosition); }

bool TriInFrustum(float4 pt0, float4 pt1, float4 pt2)
{
	float3 xs = float3(pt0.x, pt1.x, pt2.x);
	float3 ys = float3(pt0.y, pt1.y, pt2.y);
	float3 zs = float3(pt0.z, pt1.z, pt2.z);
	float3 ws = abs(float3(pt0.w, pt1.w, pt2.w));

	int l  = CountTrue(xs < -ws);
	int r  = CountTrue(xs >  ws);
	int t  = CountTrue(ys < -ws);
	int b  = CountTrue(ys >  ws);
	int f  = CountTrue(zs < 0.f);
	int bk = CountTrue(zs >  ws);

	return max(max(max(max(max(l, r), t), b), f), bk) < 3;
}

float BackfaceSign(float4 A, float4 B, float4 C)
{
	float2 a = A.xy / A.w;
	float2 b = B.xy / B.w;
	float2 c = C.xy / C.w;
	float2 edge0 = float2(b.x - a.x, b.y - a.y);
	float2 edge1 = float2(c.x - b.x, c.y - b.y);
	return (edge0.x*edge1.y) - (edge0.y*edge1.x);
}

#endif
