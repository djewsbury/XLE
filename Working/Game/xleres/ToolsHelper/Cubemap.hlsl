// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

// See DirectX documentation:
// https://msdn.microsoft.com/en-us/library/windows/desktop/bb204881(v=vs.85).aspx
// OpenGL/OpenGLES use the same standard
static const float3 CubeMapFaces[6][3] =
{
        // +X, -X
    float3(0,0,-1), float3(0,-1,0), float3(1,0,0),
    float3(0,0,1), float3(0,-1,0), float3(-1,0,0),

        // +Y, -Y
    float3(1,0,0), float3(0,0,1), float3(0,1,0),
    float3(1,0,0), float3(0,0,-1), float3(0,-1,0),

        // +Z, -Z
    float3(1,0,0), float3(0,-1,0), float3(0,0,1),
    float3(-1,0,0), float3(0,-1,0), float3(0,0,-1)
};

float3 CalculateCubeMapDirection(uint faceIndex, float2 texCoord)
{
    float3 plusU  = CubeMapFaces[faceIndex][0];
    float3 plusV  = CubeMapFaces[faceIndex][1];
    float3 center = CubeMapFaces[faceIndex][2];
    return normalize(
          center
        + plusU * (2.f * texCoord.x - 1.f)
        + plusV * (2.f * texCoord.y - 1.f));
}

// This is the standard vertical cross layout for cubemaps
// For Y up:
//		    +Y              0
//		 +Z +X -Z         4 1 5
//		    -Y              2
//		    -X              3
//
// For Z up:
//		    +Z
//		 -Y +X +Y
//		    -Z
//		    -X
//
// CubeMapGen expects:
//			+Y
//		 -X +Z +X
//			-Y
//			-Z

static const float3 VerticalCrossPanels_ZUp[6][3] =
{
	{ float3(0,1,0), float3(1,0,0), float3(0,0,1) },
    { float3(0,1,0), float3(0,0,-1), float3(1,0,0) },
	{ float3(0,1,0), float3(-1,0,0), float3(0,0,-1) },
    { float3(0,1,0), float3(0,0,1), float3(-1,0,0) },

	{ float3(1,0,0), float3(0,0,-1), float3(0,-1,0) },
	{ float3(-1,0,0), float3(0,0,-1), float3(0,1,0) }
};

static const float3 VerticalCrossPanels_CubeMapGen[6][3] =
{
	{ float3(1,0,0), float3(0,0,1), float3(0,1,0) },
	{ float3(1,0,0), float3(0,-1,0), float3(0,0,1) },
	{ float3(1,0,0), float3(0,0,-1), float3(0,-1,0) },
	{ float3(1,0,0), float3(0,1,0), float3(0,0,-1) },

	{ float3(0,0,1), float3(0,-1,0), float3(-1,0,0) },
	{ float3(0,0,-1), float3(0,-1,0), float3(1,0,0) }
};

float CubeMapAreaElement(float x, float y)
{
    return atan2(x * y, sqrt(x * x + y * y + 1.f));
}

float CubeMapTexelSolidAngle(uint2 tc, uint2 dims)
{
        // Based on the method from here:
        //      http://www.rorydriscoll.com/2012/01/15/cubemap-texel-solid-angle/
        // We can calculate the solid angle of a single texel of the
        // cube map (which represents its weight in an angular based system)
        // On that page, Rory shows an algebraic derivation of this formula. See also
        // the comments section for a number of altnerative derivations (including
        // an interesting formula for the ratio of the area of a texel and the area on
        // the equivalent sphere surface).

    float2 reciprocalDims = 1.0f / float2(dims);

        // scale up to [-1, 1] range (inclusive), offset by 0.5 to point to texel center.
    float U = (2.0f * reciprocalDims.x * (float(tc.x) + 0.5f)) - 1.0f;
    float V = (2.0f * reciprocalDims.y * (float(tc.y) + 0.5f)) - 1.0f;

        // U and V are the -1..1 texture coordinate on the current face.
        // Get projected area for this texel
    float x0 = U - reciprocalDims.x;
    float y0 = V - reciprocalDims.y;
    float x1 = U + reciprocalDims.x;
    float y1 = V + reciprocalDims.y;
    return CubeMapAreaElement(x0, y0) - CubeMapAreaElement(x0, y1) - CubeMapAreaElement(x1, y0) + CubeMapAreaElement(x1, y1);
}
