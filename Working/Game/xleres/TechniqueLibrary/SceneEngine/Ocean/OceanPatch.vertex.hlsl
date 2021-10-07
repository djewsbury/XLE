// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define VSOUT_HAS_TEXCOORD 1
#define VSOUT_HAS_WORLD_VIEW_VECTOR 1
#define DO_REFLECTION_IN_VS 1
#define VSOUT_HAS_FOG_COLOR 1

#if (MAT_DYNAMIC_REFLECTION==1) && (DO_REFLECTION_IN_VS==1)
	#define VSOUTPUT_EXTRA float4 dynamicReflectionTexCoord : DYNREFLTC; float2 specularityTC : SPECTC;
#elif MAT_USE_SHALLOW_WATER==1
	#define VSOUTPUT_EXTRA float4 shallowWaterTexCoord : SHALLOWWATER; float2 specularityTC : SPECTC;
#else
	#define VSOUTPUT_EXTRA float2 specularityTC : SPECTC;
#endif

#include "Ocean.hlsl"
#include "OceanShallow.hlsl"
#include "OceanRenderingConstants.hlsl"

#include "../../Framework/MainGeometry.hlsl"
#include "../../Framework/SystemUniforms.hlsl"
#include "../../Framework/CommonResources.hlsl"
#include "../../Math/perlinnoise.hlsl"
#include "../Lighting/resolvefog.hlsl"

#if !defined(SHALLOW_WATER_TILE_DIMENSION)
	#define SHALLOW_WATER_TILE_DIMENSION 32
#endif

Texture2D<float>		HeightsTexture			: register(t0);
Texture2D<float>		XTexture				: register(t1);
Texture2D<float>		YTexture				: register(t2);
Texture2DArray<float>	ShallowWaterHeights		: register(t3);
Texture2D<uint>			ShallowGridsLookupTable : register(t4);

cbuffer GridConstants : register(b7)
{
	row_major float4x4	LocalToClip;
	float3		LocalSpaceView2;
	float3		GridFrustumCorners[4];
	float3		GridProjectionOrigin;
	float2		GridTexCoordOrigin;
	float2		GridTexCoordOriginIntPart;
	uint		GridPatchWidth;
	uint		GridPatchHeight;
}

	////////////////////////////   G R I D   P R O J E C T I O N   ////////////////////////////

void CalculateLocalPosition(uint2 gridCoords, out float2 texCoords, out float3 outBaseLocalCoords, out float3 outDisplacement)
{
		//
		//		CPU side calculated a projection matrix, which we want to use to project
		//		our grid vertices on the ocean surface. This will mean that grid elements are
		//		evenly distributed in screen space.
		//			-- but it can cause swimming.
		//
	float3 baseLocalPosition;
	const bool useScreenSpaceGrid = UseScreenSpaceGrid != 0;
	[branch] if (useScreenSpaceGrid) {
		float2 frustumCoords = float2(gridCoords.x/float(GridPatchWidth-1), gridCoords.y/float(GridPatchHeight-1));

		float topLeftWeight		= (1.f - frustumCoords.x) * frustumCoords.y;
		float bottomLeftWeight	= (1.f - frustumCoords.x) * (1.f - frustumCoords.y);
		float topRightWeight	= frustumCoords.x * frustumCoords.y;
		float bottomRightWeight = frustumCoords.x * (1.f - frustumCoords.y);

		float3 viewFrustumVector =
			  topLeftWeight		* GridFrustumCorners[0].xyz
			+ bottomLeftWeight	* GridFrustumCorners[1].xyz
			+ topRightWeight	* GridFrustumCorners[2].xyz
			+ bottomRightWeight * GridFrustumCorners[3].xyz
			;

		viewFrustumVector.z = min(0.0, viewFrustumVector.z);
		float distanceToPlaneIntersection = (GridProjectionOrigin.z - WaterBaseHeight) / -viewFrustumVector.z;

		baseLocalPosition = GridProjectionOrigin + distanceToPlaneIntersection * viewFrustumVector;
	} else {
		baseLocalPosition = float3(
			gridCoords.x / float(GridPatchWidth) * PhysicalWidth,
			gridCoords.y / float(GridPatchHeight) * PhysicalHeight,
			WaterBaseHeight);
	}

		//	Load the displacement from the input textures
		//		if our grid is not aligned with the input texture, we have to do
		//		interpolation between samples.
		//	It would be ideal if we had a 1:1 mapping between grid elements and
		//	texels... But that's difficult to get lod working well.

	float3 displacement;
	const bool interpolateBetweenSamples = useScreenSpaceGrid;
	[branch] if (interpolateBetweenSamples) {
		texCoords = float2(
			baseLocalPosition.x / PhysicalWidth,
			baseLocalPosition.y / PhysicalHeight);
		texCoords += GridTexCoordOrigin + GridShift;

		uint2 texDim;
		XTexture.GetDimensions(texDim.x, texDim.y);

		displacement.x = OceanTextureCustomInterpolate(XTexture, texDim, texCoords);
		displacement.y = OceanTextureCustomInterpolate(YTexture, texDim, texCoords);
		displacement.z = OceanTextureCustomInterpolate(HeightsTexture, texDim, texCoords);
	} else {
		displacement.x = XTexture[gridCoords];
		displacement.y = YTexture[gridCoords];
		displacement.z = HeightsTexture[gridCoords];

			//	Have to multiply the height value by -1^(gridCoords.x+gridCoords.y)
			//		-- each height value flips in direction every grid cell
		float powNeg1 = 1.f;
		if (((gridCoords.x + gridCoords.y)&1)==1) powNeg1 = -1.f;
		displacement *= powNeg1;

		texCoords = float2(gridCoords.x/float(GridPatchWidth), gridCoords.y/float(GridPatchHeight));
	}

		//	we need to flatten the ocean in the distance. It's a bit wierd, really, but it's
		//	necessary, because the ocean might not go all the way to horizon. It will get
		//	clipped out at the far draw distance, and at that point, there are still visible
		//	waves.
	float3 viewOffsetVector = baseLocalPosition - LocalSpaceView2;
	float distanceFromCameraSq = dot(viewOffsetVector, viewOffsetVector);
	float distanceAttenuation = saturate(1.f - distanceFromCameraSq / (1000.f * 1000.f));

	displacement.xy *= distanceAttenuation * StrengthConstantXY * StrengthConstantMultiplier;
	displacement.z *= distanceAttenuation * StrengthConstantZ * StrengthConstantMultiplier;

	outBaseLocalCoords = baseLocalPosition;
	outDisplacement = displacement;
}

float CalcShallowWaterHeight(float2 worldCoords, out float3 shallowWaterTexCoord, out float shallowWaterWeight)
{
		//	get the height from the shallow water sim
	shallowWaterTexCoord = 0.0.xxx;
	shallowWaterWeight = 0.f;
	float result = WaterBaseHeight;

#if (USE_LOOKUP_TABLE==1)
	int2 tileCoord = int2(floor(worldCoords / ShallowGridPhysicalDimension));
	uint arrayIndex = CalculateShallowWaterArrayIndex(ShallowGridsLookupTable, tileCoord);
	[branch] if (arrayIndex < 128) {

		shallowWaterTexCoord = float3(
			worldCoords.xy / float(ShallowGridPhysicalDimension), float(arrayIndex));

		uint3 coords = uint3(uint2((shallowWaterTexCoord.xy - float2(tileCoord)) * SHALLOW_WATER_TILE_DIMENSION), arrayIndex);

			// todo -- incorrect interpolation from from tile to the next... (maybe clamping as a rough
			//			approximation is enough?)
		result = ShallowWaterHeights.SampleLevel(ClampingSampler, float3(shallowWaterTexCoord.xy - float2(tileCoord), arrayIndex), 0);
		shallowWaterWeight = 1.f;		// weight could fade off towards the edges (particularly since we've already calculated the neighbouring tiles)

	}
#endif

	return result;
}

	////////////////////////////   M A I N   ////////////////////////////

VSOUT main(uint vertexId : SV_VertexId)
{
	VSOUT output;
	uint2 gridCoords = uint2(vertexId%GridPatchWidth, vertexId/GridPatchWidth);

	float3 baseLocalPosition, displacement;
	CalculateLocalPosition(gridCoords, output.texCoord, baseLocalPosition, displacement);

	#if MAT_USE_SHALLOW_WATER==1
		float shallowWaterWeight;
		float3 shallowWaterTexCoord;
		float shallowWaterHeight = CalcShallowWaterHeight(
			baseWorldPosition.xy, shallowWaterTexCoord, shallowWaterWeight);
		displacement = lerp(displacement, float3(0.f, 0.f, shallowWaterHeight - WaterBaseHeight), shallowWaterWeight);
	#endif

	float3 finalLocalPosition = baseLocalPosition + displacement;

	output.position = mul(LocalToClip, float4(finalLocalPosition,1));

	#if VSOUT_HAS_WORLD_VIEW_VECTOR
		output.worldViewVector = LocalSpaceView2.xyz - finalLocalPosition.xyz;
	#endif

	#if MAT_USE_SHALLOW_WATER==1
		output.shallowWaterTexCoord = float4(shallowWaterTexCoord, shallowWaterWeight);
	#endif

	#if (MAT_DYNAMIC_REFLECTION==1) && (DO_REFLECTION_IN_VS==1)
		output.dynamicReflectionTexCoord = mul(
			LocalToReflection, float4(finalLocalPosition, 1.f));
	#endif

	float2 worldSpaceTC = output.texCoord + GridTexCoordOriginIntPart;
	float specularityOffsetX = PerlinNoise3D(float3(15.f * worldSpaceTC,			0.47f * SysUniform_GetGlobalTime()));
	float specularityOffsetY = PerlinNoise3D(float3(15.f * (1.0f - worldSpaceTC),	0.53f * SysUniform_GetGlobalTime()));
	output.specularityTC	 =	worldSpaceTC * SpecularityFrequency
								+	0.15f * float2(specularityOffsetX, specularityOffsetY);

	#if VSOUT_HAS_FOG_COLOR == 1
		// (this only works correctly because the Z values in local space are the same as world space)
		output.fogColor = ResolveOutputFogColor(finalLocalPosition, LocalSpaceView2);
	#endif

	return output;
}

VSOUT ShallowWater(uint vertexId : SV_VertexId)
{
	const int TileDimension = SHALLOW_WATER_TILE_DIMENSION;
	uint2 gridCoords	 = uint2(vertexId%TileDimension, vertexId/TileDimension);
	float waterHeight	 = ShallowWaterHeights.Load(uint4(gridCoords, ArrayIndex, 0));

	float3 worldPosition = float3(
		WorldSpaceOffset.x + (SimulatingIndex.x + gridCoords.x / float(TileDimension)) * ShallowGridPhysicalDimension,
		WorldSpaceOffset.y + (SimulatingIndex.y + gridCoords.y / float(TileDimension)) * ShallowGridPhysicalDimension,
		waterHeight);
	VSOUT output;
	output.position = mul(SysUniform_GetWorldToClip(), float4(worldPosition,1));
	return output;
}
