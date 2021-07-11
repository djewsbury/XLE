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
