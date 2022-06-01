// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Ocean.hlsl"
#include "../TechniqueLibrary/Framework/gbuffer.hlsl"
#include "../TechniqueLibrary/Math/Misc.hlsl"

RWTexture2D<uint4>	OutputNormalsTexture;
RWTexture2D<uint>	FoamQuantityTexture;

Texture2D<float>	HeightsTexture			: register(t0);
Texture2D<float>	XTexture				: register(t1);
Texture2D<float>	YTexture				: register(t2);
Texture2D<uint>		FoamQuantityLastFrame	: register(t3);

cbuffer OceanGridConstants
{
	uint GridWidth, GridHeight;
	float SpectrumFade;
}

// #define USE_ENCODED_NORMALS 1

float3 BuildWorldSpaceDisplacement(uint2 gridCoords)
{
	float3 displacement;
	displacement.x =       XTexture[gridCoords%uint2(GridWidth, GridHeight)];
	displacement.y =       YTexture[gridCoords%uint2(GridWidth, GridHeight)];
	displacement.z = HeightsTexture[gridCoords%uint2(GridWidth, GridHeight)];

	float powNeg1 = 1.f;
	if (((gridCoords.x + gridCoords.y)&1)==1) powNeg1 = -1.f;
	displacement *= powNeg1;

		//
		//		DavidJ -- Note
		//			 *	ignoring StrengthConstantXYZ here! Instead, we multiply
		//				by these values in the final pixel shader
		//				Doing it this way means the math isn't really perfectly
		//				accurate! But the problem is, sometimes StrengthConstantXYZ
		//				can be close to 0, but DetailNormalsStrength can be high.
		//				In those cases, the derivatives have been scaled down too
		//				much and can't be used effectively for the detail normals.
	displacement.xy	 *= /*StrengthConstantXY * */ StrengthConstantMultiplier;
	displacement.z	 *= /*StrengthConstantZ * */ StrengthConstantMultiplier;
	return displacement;
}

float3 BuildWorldSpaceBasePosition(uint2 gridCoords)
{
	return float3(
		gridCoords.x / float(GridWidth) * PhysicalWidth,
		gridCoords.y / float(GridHeight) * PhysicalHeight,
		0.f);
}

float3 BuildWorldSpaceCoordinate(uint2 gridCoords)
{
	return BuildWorldSpaceBasePosition(gridCoords) + BuildWorldSpaceDisplacement(gridCoords);
}

[numthreads(32, 32, 1)]
	void BuildNormals(uint3 dispatchThreadId : SV_DispatchThreadID)
{

		//
		//		Build normals using just the offsets between grid
		//		elements. We could probably also do this using
		//		the derivative of the displacement FFTs... But this
		//		is simplier and more straightforward.
		//
	uint2 coords00 = dispatchThreadId.xy;
	uint2 coords01 = uint2(0,1) + coords00.xy;
	uint2 coords10 = uint2(1,0) + coords00.xy;
	uint2 coords11 = uint2(1,1) + coords00.xy;

	// coords10.x = coords10.x % GridWidth;
	// coords01.y = coords01.y % GridHeight;

	float3 pos00 = BuildWorldSpaceCoordinate(coords00);
	float3 pos01 = BuildWorldSpaceCoordinate(coords01);
	float3 pos10 = BuildWorldSpaceCoordinate(coords10);
	float3 pos11 = BuildWorldSpaceCoordinate(coords11);

	#if 0
		float3 u = pos10 - pos00;
		float3 v = pos01 - pos00;
	#else
			//	we get a slightly smoother look if we use 4
			//	points for calculating the normal (instead of 3)
		float3 u = lerp(pos10 - pos00, pos11 - pos01, 0.5f);
		float3 v = lerp(pos01 - pos00, pos11 - pos10, 0.5f);
	#endif
	float3 normal = normalize(cross(normalize(u), normalize(v)));

	#if USE_ENCODED_NORMALS==1
		normal = GBuffer_CalculateBestFitNormal(normal);
	#else
		normal.rgb = normal.rgb * .5f + .5f;
	#endif
	OutputNormalsTexture[coords00] = uint4(normal * 255.f, 0xff);
}

cbuffer BuildMipsConstants
{
	uint2 OutputDimensions;
};

Texture2D<uint4>	SourceNormalsTexture;

[numthreads(32, 32, 1)]
	void BuildNormalsMipmap(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 destinationCoords = dispatchThreadId.xy;
	if (destinationCoords.x < OutputDimensions.x && destinationCoords.y < OutputDimensions.y) {
		uint2 sourceCoords00 = destinationCoords*2;
		uint2 sourceCoords01 = sourceCoords00 + uint2(0,1);
		uint2 sourceCoords10 = sourceCoords00 + uint2(1,0);
		uint2 sourceCoords11 = sourceCoords00 + uint2(1,1);

		float3 sourceNormal00 = DecodeGBufferNormal(SourceNormalsTexture[sourceCoords00] / 255.f);
		float3 sourceNormal01 = DecodeGBufferNormal(SourceNormalsTexture[sourceCoords01] / 255.f);
		float3 sourceNormal10 = DecodeGBufferNormal(SourceNormalsTexture[sourceCoords10] / 255.f);
		float3 sourceNormal11 = DecodeGBufferNormal(SourceNormalsTexture[sourceCoords11] / 255.f);

		float3 averageNormal = .25f * ( sourceNormal00 + sourceNormal01 + sourceNormal10 + sourceNormal11 );
		float accuracyFactor = dot(averageNormal, averageNormal);

		averageNormal /= sqrt(accuracyFactor); // averageNormal = normalize(averageNormal);
		#if USE_ENCODED_NORMALS==1
			averageNormal = GBuffer_CalculateBestFitNormal(averageNormal);
		#else
			averageNormal.rgb = averageNormal.rgb * .5f + .5f;
		#endif
		accuracyFactor = OutputDimensions.x / float(512.f);
		OutputNormalsTexture[destinationCoords] = uint4(averageNormal * 255.f, accuracyFactor * 255.f);

		// OutputNormalsTexture[destinationCoords] = SourceNormalsTexture[sourceCoords00];
	}
}

[numthreads(32, 32, 1)]
	void BuildDerivatives(uint3 dispatchThreadId : SV_DispatchThreadID)
{
		//
		//		Rather than writing normals to a texture, let's write out
		//		the derivatives. This should work better with mipmaps and
		//		linear interpolation (it should also be naturally more accurate
		//		at low bit depths).
		//

	uint2 coords00 = dispatchThreadId.xy;
	uint2 coords01 = uint2(0,1) + coords00.xy;
	uint2 coords10 = uint2(1,0) + coords00.xy;
	uint2 coords11 = uint2(1,1) + coords00.xy;

	float3 disp00 = BuildWorldSpaceDisplacement(coords00);
	float3 disp01 = BuildWorldSpaceDisplacement(coords01);
	float3 disp10 = BuildWorldSpaceDisplacement(coords10);
	float3 disp11 = BuildWorldSpaceDisplacement(coords11);

	float3 pos00 = BuildWorldSpaceBasePosition(coords00) + disp00;
	float3 pos01 = BuildWorldSpaceBasePosition(coords01) + disp01;
	float3 pos10 = BuildWorldSpaceBasePosition(coords10) + disp10;
	float3 pos11 = BuildWorldSpaceBasePosition(coords11) + disp11;

	float dhdx0 = (pos10.z - pos00.z) / float(pos10.x - pos00.x);
	float dhdx1 = (pos11.z - pos01.z) / float(pos11.x - pos01.x);
	float dhdy0 = (pos01.z - pos00.z) / float(pos01.y - pos00.y);
	float dhdy1 = (pos11.z - pos10.z) / float(pos11.y - pos10.y);

	float2 result = float2(	lerp(dhdx0, dhdx1, 0.5f),
							lerp(dhdy0, dhdy1, 0.5f));
	const float normalizingScale = .5f;
	result = 0.5f + 0.5f * normalizingScale * result;
	OutputNormalsTexture[coords00] = uint4(saturate(result) * 255.f, 0, 0);

		//
		//		Also calculate the foam quantity based on the
		//		velocity of the water. Foam should spawn around areas
		//		where the water is moving fastest...
		//
		//		Following Tessendorf's paper, we can look at the eigen
		//		values and eigenvectors of a small 2x2 matrix of
		//		the 3d displacements.
		//
		//		todo -- each thread should calculate a small grid of
		//				normals/foam quantities (instead of a single
		//				pixel). This will minimize the overhead in
		//				sampling adjacent displacement amounts.
		//
#if (DO_FOAM_SIM==1)
	{
			//	DavidJ -- note --	have to multiply by StrengthConstantXY here,
			//						because it's been removed from BuildWorldSpaceDisplacement().
		float2 positionSpacing = float2(PhysicalWidth, PhysicalHeight) / float2(GridWidth, GridHeight);
		float ddxdx = StrengthConstantXY * lerp((disp10.x - disp00.x), (disp11.x - disp01.x), 0.5f) / positionSpacing.x;
		float ddydy = StrengthConstantXY * lerp((disp01.y - disp00.y), (disp11.y - disp10.y), 0.5f) / positionSpacing.y;
		float ddxdy = StrengthConstantXY * lerp((disp01.x - disp00.x), (disp11.x - disp10.x), 0.5f) / positionSpacing.y;
		float ddydx = StrengthConstantXY * lerp((disp10.y - disp00.y), (disp11.y - disp01.y), 0.5f) / positionSpacing.x;

		float Jxx = 1.f + ddxdx;
		float Jyy = 1.f + ddydy;
		float Jyx = ddydx;
		float Jxy = ddxdy;

		float A = 0.5f * (Jxx + Jyy);
		float B = 0.5f * sqrt(Sq(Jxx - Jyy) + 4.f * Jxy * Jxy);

			//	Based on the sign of J-, we need to either add to
			//	the quantity of foam, or remove it. Foam should slowly
			//	build up in areas that are compressed, or disapate
			//	in areas that are more relaxed
		// if (B > 0.25f*A) {
		float ratio = B/A;
		const float jacobian = Jxx * Jyy - Jxy * Jyx;
		uint result;
		if (ratio > FoamThreshold) {
		// if ((A - B) < 0.f) {
		// if (jacobian < 0.f) {
				//	when B is smaller than A, J- is less than zero -- then trigger this
				//	case
			uint scale = uint(min((ratio - FoamThreshold) * FoamIncreaseSpeed, FoamIncreaseClamp));
			result = min(FoamQuantityLastFrame[coords00] + scale, 255u);
		} else {
			result = uint(max(int(FoamQuantityLastFrame[coords00]) - FoamDecrease, 0));
		}
		FoamQuantityTexture[coords00] = result;
	}
#endif
}

Texture2D<uint2>	SourceDerivativesTexture;

[numthreads(32, 32, 1)]
	void BuildDerivativesMipmap(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 destinationCoords = dispatchThreadId.xy;
	if (destinationCoords.x < OutputDimensions.x && destinationCoords.y < OutputDimensions.y) {
		uint2 sourceCoords00 = destinationCoords*2;
		uint2 sourceCoords01 = sourceCoords00 + uint2(0,1);
		uint2 sourceCoords10 = sourceCoords00 + uint2(1,0);
		uint2 sourceCoords11 = sourceCoords00 + uint2(1,1);

		uint2 source00 = SourceDerivativesTexture[sourceCoords00%uint2(OutputDimensions*2)];
		uint2 source01 = SourceDerivativesTexture[sourceCoords01%uint2(OutputDimensions*2)];
		uint2 source10 = SourceDerivativesTexture[sourceCoords10%uint2(OutputDimensions*2)];
		uint2 source11 = SourceDerivativesTexture[sourceCoords11%uint2(OutputDimensions*2)];

		uint2 result = (source00 + source01 + source10 + source11) / 4;
		OutputNormalsTexture[destinationCoords] = uint4(result, 0, 0);
	}
}
