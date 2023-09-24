
#define FFX_GPU 1
#define FFX_HLSL 1
#include "../../Foreign/FidelityFX-gpu/ffx_core.h"

#if FFX_HALF
	FfxFloat16x3 casLoadHalf(FFX_PARAMETER_IN FfxInt16x2 position);
	void casInputHalf(FFX_PARAMETER_INOUT FfxFloat16x2 red, FFX_PARAMETER_INOUT FfxFloat16x2 green, FFX_PARAMETER_INOUT FfxFloat16x2 blue);
	void casOutputHalf(FFX_PARAMETER_INOUT FfxFloat16x2 red, FFX_PARAMETER_INOUT FfxFloat16x2 green, FFX_PARAMETER_INOUT FfxFloat16x2 blue);
#else
	FfxFloat32x3 casLoad(FFX_PARAMETER_IN FfxInt32x2 position);
	void casInput(FFX_PARAMETER_INOUT FfxFloat32 red, FFX_PARAMETER_INOUT FfxFloat32 green, FFX_PARAMETER_INOUT FfxFloat32 blue);
	void casOutput(FFX_PARAMETER_INOUT FfxFloat32 red, FFX_PARAMETER_INOUT FfxFloat32 green, FFX_PARAMETER_INOUT FfxFloat32 blue);
#endif  // FFX_HALF

void casStoreOutput(FfxInt32x2 iPxPos, FfxFloat32x4 fColor);

#include "../../Foreign/FidelityFX-gpu/cas/ffx_cas.h"

cbuffer ControlUniforms
{
	FfxUInt32x4 CasConst0;
    FfxUInt32x4 CasConst1;
};

void Sharpen(FfxUInt32x3 LocalThreadId, FfxUInt32x3 WorkGroupId)
{
    // Do remapping of local xy in workgroup for a more PS-like swizzle pattern.
    FfxUInt32x2 gxy = ffxRemapForQuad(LocalThreadId.x) + FfxUInt32x2(WorkGroupId.x << 4u, WorkGroupId.y << 4u);
    const bool sharpenOnly = true;

	#if FFX_HALF

		// Filter.
		FfxFloat16x4 c0, c1;
		FfxFloat16x2 cR, cG, cB;

		ffxCasFilterHalf(cR, cG, cB, gxy, CasConst0, CasConst1, sharpenOnly);
		casOutputHalf(cR, cG, cB);
		ffxCasDepackHalf(c0, c1, cR, cG, cB);
		casStoreOutput(FfxInt32x2(gxy), FfxFloat32x4(c0));
		casStoreOutput(FfxInt32x2(gxy) + FfxInt32x2(8, 0), FfxFloat32x4(c1));
		gxy.y += 8u;

		ffxCasFilterHalf(cR, cG, cB, gxy, CasConst0, CasConst1, sharpenOnly);
		casOutputHalf(cR, cG, cB);
		ffxCasDepackHalf(c0, c1, cR, cG, cB);
		casStoreOutput(FfxInt32x2(gxy), FfxFloat32x4(c0));
		casStoreOutput(FfxInt32x2(gxy) + FfxInt32x2(8, 0), FfxFloat32x4(c1));

	#else

		// Filter.
		FfxFloat32x3 c;

		ffxCasFilter(c.r, c.g, c.b, gxy, CasConst0, CasConst1, sharpenOnly);
		casOutput(c.r, c.g, c.b);
		casStoreOutput(FfxInt32x2(gxy), FfxFloat32x4(c, 1));
		gxy.x += 8u;

		ffxCasFilter(c.r, c.g, c.b, gxy, CasConst0, CasConst1, sharpenOnly);
		casOutput(c.r, c.g, c.b);
		casStoreOutput(FfxInt32x2(gxy), FfxFloat32x4(c, 1));
		gxy.y += 8u;

		ffxCasFilter(c.r, c.g, c.b, gxy, CasConst0, CasConst1, sharpenOnly);
		casOutput(c.r, c.g, c.b);
		casStoreOutput(FfxInt32x2(gxy), FfxFloat32x4(c, 1));
		gxy.x -= 8u;

		ffxCasFilter(c.r, c.g, c.b, gxy, CasConst0, CasConst1, sharpenOnly);
		casOutput(c.r, c.g, c.b);
		casStoreOutput(FfxInt32x2(gxy), FfxFloat32x4(c, 1));

	#endif  // FFX_HALF
}

RWTexture2D<float4> Output;
Texture2D<float4> Input;

[numthreads(64, 1, 1)]
	void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID, uint3 dispatchThreadId : SV_DispatchThreadID)
{
	Sharpen(groupThreadId, groupId);
	// Output[dispatchThreadId.xy] = Input[dispatchThreadId.xy];
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
		//		interface			//
////////////////////////////////////////////////////////////////////////////////////////////////////////

#if FFX_HALF

	FfxFloat16x3 casLoadHalf(FFX_PARAMETER_IN FfxInt16x2 position) { return FfxFloat16x3(Input.Load(FfxInt32x3(position, 0)).rgb); }

	// Transform input from the load into a linear color space between 0 and 1.
	void casInputHalf(FFX_PARAMETER_INOUT FfxFloat16x2 red, FFX_PARAMETER_INOUT FfxFloat16x2 green, FFX_PARAMETER_INOUT FfxFloat16x2 blue)
	{
		red   = ffxLinearFromSrgbHalf(red);
		green = ffxLinearFromSrgbHalf(green);
		blue  = ffxLinearFromSrgbHalf(blue);
	}

	void casOutputHalf(FFX_PARAMETER_INOUT FfxFloat16x2 red, FFX_PARAMETER_INOUT FfxFloat16x2 green, FFX_PARAMETER_INOUT FfxFloat16x2 blue)
	{
		red   = ffxSrgbFromLinearHalf(red);
		green = ffxSrgbFromLinearHalf(green);
		blue  = ffxSrgbFromLinearHalf(blue);
	}

#else

	FfxFloat32x3 casLoad(FFX_PARAMETER_IN FfxInt32x2 position) { return Input.Load(FfxInt32x3(position, 0)).rgb; }

	// Transform input from the load into a linear color space between 0 and 1.
	void casInput(FFX_PARAMETER_INOUT FfxFloat32 red, FFX_PARAMETER_INOUT FfxFloat32 green, FFX_PARAMETER_INOUT FfxFloat32 blue)
	{
		red   = ffxLinearFromSrgb(red);
		green = ffxLinearFromSrgb(green);
		blue  = ffxLinearFromSrgb(blue);
	}

	void casOutput(FFX_PARAMETER_INOUT FfxFloat32 red, FFX_PARAMETER_INOUT FfxFloat32 green, FFX_PARAMETER_INOUT FfxFloat32 blue)
	{
		red   = ffxSrgbToLinear(red);
		green = ffxSrgbToLinear(green);
		blue  = ffxSrgbToLinear(blue);
	}

#endif  // FFX_HALF

void casStoreOutput(FfxInt32x2 iPxPos, FfxFloat32x4 fColor)
{
    Output[iPxPos] = fColor;
}

