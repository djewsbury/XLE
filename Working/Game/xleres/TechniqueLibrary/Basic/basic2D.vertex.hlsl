// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(BASIC2D_VSH)
#define BASIC2D_VSH

#include "../Framework/SystemUniforms.hlsl"
#include "../Framework/VSIN.hlsl"
#include "../Framework/VSOUT.hlsl"
#include "../Utility/Colour.hlsl"

#define NDC_POSITIVE 1
#define NDC_POSITIVE_RIGHT_HANDED 2
#define NDC_POSITIVE_REVERSEZ 3
#define NDC_POSITIVE_RIGHT_HANDED_REVERSEZ 4

#if VULKAN
	#define NDC NDC_POSITIVE_RIGHT_HANDED_REVERSEZ
#else
	#define NDC NDC_POSITIVE_REVERSEZ
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////

struct ViewFrustumInterpolator
{
	// We must use the "no persepective" flag on this attribute; because we can actually use this with 3d geometry
	// and as long as the vectors are corerct on the vertices, we'll still end up with the correct result
	noperspective float3 oViewFrustumVector : VIEWFRUSTUMVECTOR;
};

struct FullscreenCorner
{
	float2 coord;
	float4 position;
	float2 texCoord;
	ViewFrustumInterpolator vfi;
};

FullscreenCorner MakeFullscreenCorner(uint vertexId)
{
	FullscreenCorner result;

	result.coord = float2((float)(vertexId >> 1), (float)(vertexId & 1));
	#if (NDC == NDC_POSITIVE_RIGHT_HANDED) || (NDC == NDC_POSITIVE_RIGHT_HANDED_REVERSEZ)
		result.position = float4(2.f * result.coord.x - 1.f, 2.f * result.coord.y - 1.f, 0.f, 1.f);
	#else
		result.position = float4(2.f * result.coord.x - 1.f, -2.f * result.coord.y + 1.f, 0.f, 1.f);
	#endif
	result.vfi.oViewFrustumVector = SysUniform_GetFrustumCorners(vertexId).xyz;
	result.texCoord = result.coord;

	return result;
}

float4 PixelCoordToSVPosition(float2 pixelCoord)
{
	// This is a kind of viewport transform -- unfortunately it needs to
	// be customized for vulkan because of the different NDC space
#if (NDC == NDC_POSITIVE_RIGHT_HANDED)
	return float4(	pixelCoord.x * SysUniform_ReciprocalViewportDimensions().x *  2.f - 1.f,
					pixelCoord.y * SysUniform_ReciprocalViewportDimensions().y *  2.f - 1.f,
					0.f, 1.f);
#elif (NDC == NDC_POSITIVE_RIGHT_HANDED_REVERSEZ)
	return float4(	pixelCoord.x * SysUniform_ReciprocalViewportDimensions().x *  2.f - 1.f,
					pixelCoord.y * SysUniform_ReciprocalViewportDimensions().y *  2.f - 1.f,
					1.f, 1.f);
#elif (NDC == NDC_POSITIVE_REVERSEZ)
	return float4(	pixelCoord.x * SysUniform_ReciprocalViewportDimensions().x *  2.f - 1.f,
					pixelCoord.y * SysUniform_ReciprocalViewportDimensions().y * -2.f + 1.f,
					1.f, 1.f);
#else
	return float4(	pixelCoord.x * SysUniform_ReciprocalViewportDimensions().x *  2.f - 1.f,
					pixelCoord.y * SysUniform_ReciprocalViewportDimensions().y * -2.f + 1.f,
					0.f, 1.f);
#endif
}

float4 TransformPosition(float3 localPosition)
{
	float3 worldPosition = mul(SysUniform_GetLocalToWorld(), float4(localPosition,1)).xyz;
	return mul(SysUniform_GetWorldToClip(), float4(worldPosition,1));
}

///////////////////////////////////////////////////////////////////////////////////////////////////

VSOUT frameworkEntry(VSIN vsin)
{
	VSOUT output;
	#if GEO_HAS_PIXELPOSITION
		output.position = PixelCoordToSVPosition(vsin.pixelposition.xy);
	#else
		output.position = TransformPosition(vsin.position);
	#endif

	// Note that we're kind of forced to do srgb -> linear conversion here, because we'll loose precision
	// assuming 8 bit color inputs	
	#if VSOUT_HAS_COLOR>=1
		output.color = float4(SRGBToLinear_Formal(VSIN_GetColor0(vsin).rgb), VSIN_GetColor0(vsin).a);
	#endif

	#if VSOUT_HAS_COLOR1>=1
		output.color1 = float4(SRGBToLinear_Formal(VSIN_GetColor1(vsin).rgb), VSIN_GetColor1(vsin).a);
	#endif

	#if VSOUT_HAS_TEXCOORD>=1
		output.texCoord = VSIN_GetTexCoord0(vsin);
	#endif

	#if VSOUT_HAS_TEXCOORD1>=1
		output.texCoord1 = VSIN_GetTexCoord1(vsin);
	#endif
	
	#if VSOUT_HAS_TANGENT_FRAME==1
		output.tangent = VSIN_GetLocalTangent(vsin);
		output.bitangent = VSIN_GetLocalBitangent(vsin);
	#endif

	#if (VSOUT_HAS_NORMAL==1)
		output.normal = VSIN_GetLocalNormal(vsin);
	#endif

	#if VSOUT_HAS_LOCAL_TANGENT_FRAME==1
		output.localTangent = VSIN_GetLocalTangent(vsin);
		output.localBitangent = VSIN_GetLocalBitangent(vsin);
	#endif

	#if (VSOUT_HAS_LOCAL_NORMAL==1)
		output.localNormal = VSIN_GetLocalNormal(vsin);
	#endif

	return output;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void fullscreen(uint vertexId : SV_VertexID, out float4 oPosition : SV_Position, out float2 oTexCoord : TEXCOORD0)
{
	FullscreenCorner corner = MakeFullscreenCorner(vertexId);
	oTexCoord = corner.texCoord;
	oPosition = corner.position;
}

void fullscreen_viewfrustumvector(uint vertexId : SV_VertexID, out float4 oPosition : SV_Position, out float2 oTexCoord : TEXCOORD0, out ViewFrustumInterpolator vfi)
{
	FullscreenCorner corner = MakeFullscreenCorner(vertexId);
	oTexCoord = corner.texCoord;
	vfi = corner.vfi;
	oPosition = corner.position;
}

void fullscreen_viewfrustumvector_deep(uint vertexId : SV_VertexID, out float4 oPosition : SV_Position, out float2 oTexCoord : TEXCOORD0, out ViewFrustumInterpolator vfi)
{
	FullscreenCorner corner = MakeFullscreenCorner(vertexId);
	oTexCoord = corner.texCoord;
	vfi = corner.vfi;
	oPosition = float4(corner.position.xy, 1.f, 1.f);
}

void fullscreen_flip(uint vertexId : SV_VertexID, out float4 oPosition : SV_Position, out float2 oTexCoord : TEXCOORD0)
{
	vertexId ^= 1;		// xor bit 1 to flip Y coord
	FullscreenCorner corner = MakeFullscreenCorner(vertexId);
	oTexCoord = corner.texCoord;
	oPosition = corner.position;
}

void fullscreen_flip_viewfrustumvector(uint vertexId : SV_VertexID, out float4 oPosition : SV_Position, out float2 oTexCoord : TEXCOORD0, out ViewFrustumInterpolator vfi)
{
	vertexId ^= 1;		// xor bit 1 to flip Y coord
	FullscreenCorner corner = MakeFullscreenCorner(vertexId);
	oTexCoord = corner.texCoord;
	vfi = corner.vfi;
	oPosition = corner.position;
}

cbuffer ScreenSpaceOutput
{
	float2 OutputMin, OutputMax;
	float2 InputMin, InputMax;
	float2 OutputDimensions;
}

void screenspacerect(
	uint vertexId : SV_VertexID,
	out float4 oPosition : SV_Position,
	out float2 oTexCoord0 : TEXCOORD0)
{
	FullscreenCorner corner = MakeFullscreenCorner(vertexId);
	oTexCoord0 = lerp(InputMin, InputMax, corner.texCoord);
	float2 coord = lerp(OutputMin, OutputMax, corner.coord);
	oPosition = float4(
		 2.f * coord.x / OutputDimensions.x - 1.f,
		-2.f * coord.y / OutputDimensions.y + 1.f,
		 0.f, 1.f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void P_viewfrustumvector(float3 iPosition : POSITION, out float4 oPosition : SV_Position, out float2 oTexCoord : TEXCOORD0, out ViewFrustumInterpolator vfi)
{
	oPosition = TransformPosition(iPosition);
	
	#if (NDC == NDC_POSITIVE_RIGHT_HANDED) || (NDC == NDC_POSITIVE_RIGHT_HANDED_REVERSEZ)
		oTexCoord.y = (oPosition.y + 1.0f) / 2.0f;
	#else
		oTexCoord.y = (1.0f - oPosition.y) / 2.0f;
	#endif
	oTexCoord.x = (oPosition.x + 1.0f) / 2.0f;

	// We can use bilinear interpolation here to find the correct view frustum vector
	// Note that this require we use "noperspective" interpolation between vertices for this attribute, though
	float w0 = (1.0f - oTexCoord.x) * (1.0f - oTexCoord.y);
	float w1 = (1.0f - oTexCoord.x) * oTexCoord.y;
	float w2 = oTexCoord.x * (1.0f - oTexCoord.y);
	float w3 = oTexCoord.x * oTexCoord.y;

	vfi.oViewFrustumVector = 
		  w0 * SysUniform_GetFrustumCorners(0).xyz
		+ w1 * SysUniform_GetFrustumCorners(1).xyz
		+ w2 * SysUniform_GetFrustumCorners(2).xyz
		+ w3 * SysUniform_GetFrustumCorners(3).xyz
		;
}

//////////////

struct PSInput_Basic
{
	float4 _position : SV_Position;
	float4 _color	 : COLOR0;
	float2 _texCoord : TEXCOORD0;
};

float4 P2C(		float2 iPosition : PIXELPOSITION0,
				float4 iColor	 : COLOR0,
				out float4 oColor : COLOR0 ) : SV_POSITION
{
	oColor	 = iColor;
	return PixelCoordToSVPosition(iPosition);
}

float4 P2CR(	float2 iPosition  : PIXELPOSITION0,
				float4 iColor	  : COLOR0,
				float iRadius	  : RADIUS,
				out float4 oColor : COLOR0,
				out float oRadius : RADIUS ) : SV_POSITION
{
	oColor	 = iColor;
	oRadius  = iRadius;
	return PixelCoordToSVPosition(iPosition);
}

PSInput_Basic P2CT(	float2 iPosition : PIXELPOSITION0,
				float4 iColor	 : COLOR0,
				float2 iTexCoord : TEXCOORD0 )
{
	PSInput_Basic output;
	output._position = PixelCoordToSVPosition(iPosition);
	output._color	 = iColor;
	output._texCoord = iTexCoord;
	return output;
}

void P2T(	float2 iPosition : PIXELPOSITION0,
			float2 iTexCoord : TEXCOORD0,
			out float4 oPosition	: SV_Position,
			out float2 oTexCoord0	: TEXCOORD0 )
{
	oPosition = PixelCoordToSVPosition(iPosition);
	oTexCoord0 = iTexCoord;
}

void P2CTT(	float2 iPosition	: PIXELPOSITION0,
			float4 iColor		: COLOR0,
			float2 iTexCoord0	: TEXCOORD0,
			float2 iTexCoord1	: TEXCOORD1,

			out float4 oPosition	: SV_Position,
			out float4 oColor		: COLOR0,
			out float2 oTexCoord0	: TEXCOORD0,
			out float2 oTexCoord1	: TEXCOORD1,
			nointerpolation out float2 oOutputDimensions : OUTPUTDIMENSIONS )
{
	oPosition	= PixelCoordToSVPosition(iPosition);
	oColor		= iColor;
	oTexCoord0	= iTexCoord0;
	oTexCoord1	= iTexCoord1;
	oOutputDimensions = 1.0f / SysUniform_ReciprocalViewportDimensions().xy;
}

void P2CCTT(float2 iPosition	: PIXELPOSITION0,
			float4 iColor0		: COLOR0,
			float4 iColor1		: COLOR1,
			float2 iTexCoord0	: TEXCOORD0,
			float2 iTexCoord1	: TEXCOORD1,

			out float4 oPosition	: SV_Position,
			out float4 oColor0		: COLOR0,
			out float2 oTexCoord0	: TEXCOORD0,
			out float4 oColor1		: COLOR1,		// note the flip in ordering here -- makes it easier when using a PCT pixel shader
			out float2 oTexCoord1	: TEXCOORD1,
			nointerpolation out float2 oOutputDimensions : OUTPUTDIMENSIONS )
{
	oPosition	= PixelCoordToSVPosition(iPosition);
	oColor0		= iColor0;
	oColor1		= iColor1;
	oTexCoord0	= iTexCoord0;
	oTexCoord1	= iTexCoord1;
	oOutputDimensions = 1.0f / SysUniform_ReciprocalViewportDimensions().xy;
}

#endif
