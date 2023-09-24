
#define FFX_GPU 1
#define FFX_HLSL 1
#define FFX_HALF 1
#define FFX_CAS_PACKED_ONLY 1
#include "../../Foreign/FidelityFX-gpu/ffx_core.h"

#if FFX_HALF
	FfxFloat16x3 casLoadHalf(FFX_PARAMETER_IN FfxInt16x2 position);
	void casInputHalf(FFX_PARAMETER_INOUT FfxFloat16x2 red, FFX_PARAMETER_INOUT FfxFloat16x2 green, FFX_PARAMETER_INOUT FfxFloat16x2 blue);
	void casOutputHalf(FfxInt32x4 iPxPos, FFX_PARAMETER_INOUT FfxFloat16x2 red, FFX_PARAMETER_INOUT FfxFloat16x2 green, FFX_PARAMETER_INOUT FfxFloat16x2 blue);
#else
	FfxFloat32x3 casLoad(FFX_PARAMETER_IN FfxInt32x2 position);
	void casInput(FFX_PARAMETER_INOUT FfxFloat32 red, FFX_PARAMETER_INOUT FfxFloat32 green, FFX_PARAMETER_INOUT FfxFloat32 blue);
	void casOutput(FfxInt32x2 iPxPos, FFX_PARAMETER_INOUT FfxFloat32 red, FFX_PARAMETER_INOUT FfxFloat32 green, FFX_PARAMETER_INOUT FfxFloat32 blue);
#endif  // FFX_HALF

void casStoreOutput(FfxInt32x2 iPxPos, FfxFloat32x4 fColor);

#include "../../Foreign/FidelityFX-gpu/cas/ffx_cas.h"

cbuffer ControlUniforms
{
	FfxUInt32x4 CasConst0;
    FfxUInt32x4 CasConst1;
	uint2 NoiseOffset;
	float NoiseStrength;
};

RWTexture2D<float4> Output;
Texture2D<float4> Input;

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
		casOutputHalf(FfxInt32x4(gxy, gxy + FfxInt32x2(8, 0)), cR, cG, cB);
		ffxCasDepackHalf(c0, c1, cR, cG, cB);
		casStoreOutput(FfxInt32x2(gxy), FfxFloat32x4(c0));
		casStoreOutput(FfxInt32x2(gxy) + FfxInt32x2(8, 0), FfxFloat32x4(c1));
		gxy.y += 8u;

		ffxCasFilterHalf(cR, cG, cB, gxy, CasConst0, CasConst1, sharpenOnly);
		casOutputHalf(FfxInt32x4(gxy, gxy + FfxInt32x2(8, 0)), cR, cG, cB);
		ffxCasDepackHalf(c0, c1, cR, cG, cB);
		casStoreOutput(FfxInt32x2(gxy), FfxFloat32x4(c0));
		casStoreOutput(FfxInt32x2(gxy) + FfxInt32x2(8, 0), FfxFloat32x4(c1));

	#else

		// Filter.
		FfxFloat32x3 c;

		ffxCasFilter(c.r, c.g, c.b, gxy, CasConst0, CasConst1, sharpenOnly);
		casOutput(FfxInt32x2(gxy), c.r, c.g, c.b);
		casStoreOutput(FfxInt32x2(gxy), FfxFloat32x4(c, 1));
		gxy.x += 8u;

		ffxCasFilter(c.r, c.g, c.b, gxy, CasConst0, CasConst1, sharpenOnly);
		casOutput(FfxInt32x2(gxy), c.r, c.g, c.b);
		casStoreOutput(FfxInt32x2(gxy), FfxFloat32x4(c, 1));
		gxy.y += 8u;

		ffxCasFilter(c.r, c.g, c.b, gxy, CasConst0, CasConst1, sharpenOnly);
		casOutput(FfxInt32x2(gxy), c.r, c.g, c.b);
		casStoreOutput(FfxInt32x2(gxy), FfxFloat32x4(c, 1));
		gxy.x -= 8u;

		ffxCasFilter(c.r, c.g, c.b, gxy, CasConst0, CasConst1, sharpenOnly);
		casOutput(FfxInt32x2(gxy), c.r, c.g, c.b);
		casStoreOutput(FfxInt32x2(gxy), FfxFloat32x4(c, 1));

	#endif  // FFX_HALF
}

Texture2D<float> NoiseTexture;

#if FFX_HALF

	FfxFloat16x2 FilmGrain_SampleNoise(int4 pixelId)
	{
		uint2 coord = pixelId.xy ^ NoiseOffset;
		FfxFloat16 n = NoiseTexture.Load(uint3(coord & 0xff, 0));
		FfxFloat16 a =  NoiseStrength * ( n * 2 - 1 );

		coord = pixelId.zw ^ NoiseOffset;
		n = NoiseTexture.Load(uint3(coord & 0xff, 0));
		FfxFloat16 b =  NoiseStrength * ( n * 2 - 1 );

		return FfxFloat16x2(a,b);
	}

#else

	float FilmGrain_SampleNoise(int2 pixelId)
	{
		uint2 coord = pixelId ^ NoiseOffset;		// xor here seems to give us fewer patterns than plus
		float n = NoiseTexture.Load(uint3(coord & 0xff, 0));
		return NoiseStrength * ( n * 2 - 1 );
	}

#endif

void FilmGrain(FfxUInt32x3 LocalThreadId, FfxUInt32x3 WorkGroupId)
{
	FfxUInt32x2 gxy = ffxRemapForQuad(LocalThreadId.x) + FfxUInt32x2(WorkGroupId.x << 4u, WorkGroupId.y << 4u);

	#if FFX_HALF

		half4 m = half4(0.2126, 0.7152, 0.0722, 0);

		FfxFloat16x2 n = FilmGrain_SampleNoise(int4(gxy.xy, gxy.xy + int2(8,0)));
		Output[gxy.xy] = Input[gxy.xy] + m * n.x;
		Output[gxy.xy + int2(8,0)] = Input[gxy.xy + int2(8,0)] + m * n.y;

		gxy.y += 8;
		n = FilmGrain_SampleNoise(int4(gxy.xy, gxy.xy + int2(8,0)));
		Output[gxy.xy] = Input[gxy.xy] + m * n.x;
		Output[gxy.xy + int2(8,0)] = Input[gxy.xy + int2(8,0)] + m * n.y;

	#else

		float4 m = float4(0.2126, 0.7152, 0.0722, 0);
		Output[gxy.xy] = Input[gxy.xy] + m * FilmGrain_SampleNoise(gxy.xy);
		Output[gxy.xy + uint2(8,0)] = Input[gxy.xy + uint2(8,0)] + m * FilmGrain_SampleNoise(gxy.xy + uint2(8,0));
		Output[gxy.xy + uint2(0,8)] = Input[gxy.xy + uint2(0,8)] + m * FilmGrain_SampleNoise(gxy.xy + uint2(0,8));
		Output[gxy.xy + uint2(8,8)] = Input[gxy.xy + uint2(8,8)] + m * FilmGrain_SampleNoise(gxy.xy + uint2(8,8));

	#endif

}

[numthreads(64, 1, 1)]
	void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID, uint3 dispatchThreadId : SV_DispatchThreadID)
{
	#if SHARPEN

		Sharpen(groupThreadId, groupId);

	#elif FILM_GRAIN

		FilmGrain(groupThreadId, groupId);

	#else

		#error No post-processing suboperator enabled

	#endif
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

	void casOutputHalf(FfxInt32x4 iPxPos, FFX_PARAMETER_INOUT FfxFloat16x2 red, FFX_PARAMETER_INOUT FfxFloat16x2 green, FFX_PARAMETER_INOUT FfxFloat16x2 blue)
	{
		red   = ffxSrgbFromLinearHalf(red);
		green = ffxSrgbFromLinearHalf(green);
		blue  = ffxSrgbFromLinearHalf(blue);

		#if FILM_GRAIN
			// film grain in non-linear srgb seems to work better (simply because it offsets the fact that the noise is just clearer in the dark areas)
			FfxFloat16x2 filmGrain = FilmGrain_SampleNoise(iPxPos);
			// film grain looks like a tiny bit nicer if we use the SRGB primaries brightnesses. Perhaps because it shifts it away from looking so gray
			red += FfxFloat16(0.2126) * filmGrain;
			green += FfxFloat16(0.7152) * filmGrain;
			blue += FfxFloat16(0.0722) * filmGrain;
		#endif
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

	void casOutput(FfxInt32x2 iPxPos, FFX_PARAMETER_INOUT FfxFloat32 red, FFX_PARAMETER_INOUT FfxFloat32 green, FFX_PARAMETER_INOUT FfxFloat32 blue)
	{
		red   = ffxSrgbToLinear(red);
		green = ffxSrgbToLinear(green);
		blue  = ffxSrgbToLinear(blue);
		#if FILM_GRAIN
			// film grain in non-linear srgb seems to work better (simply because it offsets the fact that the noise is just clearer in the dark areas)
			float filmGrain = FilmGrain_SampleNoise(iPxPos);
			// film grain looks like a tiny bit nicer if we use the SRGB primaries brightnesses. Perhaps because it shifts it away from looking so gray
			red += 0.2126f * filmGrain;
			green += 0.7152f * filmGrain;
			blue += 0.0722f * filmGrain;
		#endif
	}

#endif  // FFX_HALF

void casStoreOutput(FfxInt32x2 iPxPos, FfxFloat32x4 fColor)
{
	Output[iPxPos] = fColor;
}

