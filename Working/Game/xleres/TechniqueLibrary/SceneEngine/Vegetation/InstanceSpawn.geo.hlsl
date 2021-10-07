// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define GEO_HAS_TEXCOORD 1

#include "../../Framework/SystemUniforms.hlsl"
#include "../../Framework/MainGeometry.hlsl"
#include "../../Math/perlinnoise.hlsl"
#include "../../Math/MathConstants.hlsl"
#include "../../Math/ProjectionMath.hlsl"

#include "../Terrain/Terrain.hlsl"
#include "../../Math/perlinnoise.hlsl"
#include "xleres/Objects/Terrain/TerrainGenerator.hlsl"

struct WorkingTriangle
{
	float3 pts[3];
};

struct GSOutput
{
	float4 position				: INSTANCEPOS;		// world space position & rotation value
	#if (TERRAIN_NORMAL==1)
		float3 instanceParam	: INSTANCEPARAM;	// terrain dhdxy & instance type & shadowing
	#else
		uint instanceParam		: INSTANCEPARAM;	// instance type & shadowing
	#endif
};

cbuffer InstanceSpawn : register(b5)
{
	row_major float4x4 WorldToCullFrustum;
	float GridSpacing;
	float BaseDrawDistanceSq;
	float JitterAmount;

	float4 MaterialParams[8];
	float4 SuppressionNoiseParams[8];
}

float GetSuppressionThreshold(uint matIndex) 	{ return MaterialParams[matIndex].x; }

static const uint MaxOutputVertices = 112;

void Swap(inout float3 A, inout float3 B) 		{ float3 x = A; A = B; B = x; }

void WriteInstance(
	float3 instancePosition, float2 tc, float2 dhdxy,
	inout uint outputVertices, inout PointStream<GSOutput> outputStream)
{
	if (outputVertices>=MaxOutputVertices)
		return;

	float3 camOffset = instancePosition - SysUniform_GetWorldSpaceView();
	float distSq = dot(camOffset, camOffset);
	if (distSq > BaseDrawDistanceSq)
		return;

	uint materialIndex = 0;
	#if defined(COVERAGE_1001)	// 1001 is the decoration coverage layer
		uint2 decorTexCoord = lerp(CoverageCoordMins[COVERAGE_1001].xy, CoverageCoordMaxs[COVERAGE_1001].xy, tc.xy);
		materialIndex = MakeCoverageTileSet(COVERAGE_1001).Load(uint4(decorTexCoord, CoverageOrigin[COVERAGE_1001].z, 0)).r;
	#endif

		// core parameters for the noise field
	const uint octaves = 3;
	float4 noiseParam = SuppressionNoiseParams[materialIndex];
	float noiseValue2 = fbmNoise2D(instancePosition.xy, noiseParam.x, noiseParam.y, noiseParam.z, octaves);

		// suppress points according to the noise pattern
	if (noiseValue2 < GetSuppressionThreshold(materialIndex)) return;

///////////////////////////////////////////////////////////////////////////////////////////////////

	const float hgrid = 9.632f, gain = .85f, lacunarity = 2.0192f;
	float noiseValue = fbmNoise2D(instancePosition.xy, hgrid, gain, lacunarity, octaves);

	const float noiseScaleForType = 16.f;
	uint instanceType = (materialIndex << 12) | uint(4096.0f * frac(noiseScaleForType * abs(noiseValue)));

		// Currently both x and y jitter are generated from the same noise value!
		// It maybe not ok... perhaps a second noise look-up is required
	const float noiseScaleX =  4.5f;
	const float noiseScaleY = -3.3f;

	float3 shift;
	shift.x = JitterAmount * frac(noiseValue  * noiseScaleX);
	shift.y = JitterAmount * frac(noiseValue2 * noiseScaleY);
	shift.z = dot(shift.xy, dhdxy);	// (try to follow the surface of the triangle)
	instancePosition += shift;

///////////////////////////////////////////////////////////////////////////////////////////////////

	float rotationValue = 2.f * 3.14159f * frac(noiseValue * 18.43f);

	GSOutput output;
	output.position = float4(instancePosition, rotationValue);

		//	using the texture coordinate value, we can look up
		//	the shadowing in the terrain shadowing map
		//	This produces a single shadow sample per object. It's not perfectly
		//	accurate for large objects (particularlly tall plants, etc). But it should
		//	work well for grass and small things. And it should be much more efficient
		//	than doing extra shadowing work per-vertex or per-pixel.
		// todo -- we could also lookup ambient occlusion!
	#if defined(COVERAGE_2)
		uint2 shadowTexCoord = lerp(CoverageCoordMins[COVERAGE_2].xy, CoverageCoordMaxs[COVERAGE_2].xy, tc.xy);
		float2 shadowSample = MakeCoverageTileSet(COVERAGE_2).Load(uint4(shadowTexCoord, CoverageOrigin[COVERAGE_2].z, 0)).rg;
		float shadowing =
			  saturate(ShadowSoftness * (SunAngle + shadowSample.r))
			* saturate(ShadowSoftness * (shadowSample.g - SunAngle));
	#else
		float shadowing = 1.f;
	#endif

	uint basicInstanceParam = (instanceType) | ((uint(float(0xffff) * shadowing)) << 16);
	#if (TERRAIN_NORMAL==1)
		output.instanceParam = float3(dhdxy, asfloat(basicInstanceParam));
	#else
		output.instanceParam = basicInstanceParam;
	#endif

	outputStream.Append(output);
	++outputVertices;
}

void RasterizeBetweenEdges(	float3 e00, float3 e01, float3 e10, float3 e11,
							float2 tc00, float2 tc01, float2 tc10, float2 tc11,
							float2 dhdxy,
							inout uint outputVertices, float spacing,
							inout PointStream<GSOutput> outputStream)
{
		// If either edge is horizontal, we will get an finite
		// loop here. So we need to prevent this case
	[branch] if ((e01.y == e00.y) || (e11.y == e10.y)) return;

	float y = ceil(e00.y/spacing)*spacing;
	for (;y<e01.y; y+=spacing) {
		float e0a = (y - e00.y) / (e01.y - e00.y);
		float e2a = (y - e10.y) / (e11.y - e10.y);

		float spanx0 = lerp(e00.x, e01.x, e0a);
		float spanx1 = lerp(e10.x, e11.x, e2a);
		float spanz0 = lerp(e00.z, e01.z, e0a);
		float spanz1 = lerp(e10.z, e11.z, e2a);

		float2 spantc0 = lerp(tc00, tc01, e0a);
		float2 spantc1 = lerp(tc10, tc11, e2a);

		float spanMinX = min(spanx0, spanx1);
		float spanMaxX = max(spanx0, spanx1);

		if (spanx1 == spanx0) continue;		// causes a divide by zero, which could mean bad outputs

		float x = ceil(spanMinX/spacing)*spacing;
		for (;x<spanMaxX; x+=spacing) {

			float ax = (x - spanx0) / (spanx1 - spanx0);
			float  z = lerp(spanz0, spanz1, ax);

			float2 tc = lerp(spantc0, spantc1, ax);

			WriteInstance(
				float3(x, y, z), tc, dhdxy,
				outputVertices, outputStream);

		}
	}
}

float EstimatePointCount(WorkingTriangle tri, float spacing)
{
	float minY = tri.pts[0].y;
	float maxY = tri.pts[2].y;
	float minX = min(min(tri.pts[0].x, tri.pts[1].x), tri.pts[2].x);
	float maxX = max(max(tri.pts[0].x, tri.pts[1].x), tri.pts[2].x);
	float ceilMinX = ceil(minX/spacing)*spacing;
	float ceilMinY = ceil(minY/spacing)*spacing;

		//	Calculation is not exact... But it's quick.
	return ceil((maxX - ceilMinX)/spacing) * ceil((maxY - ceilMinY)/spacing) / 2;
}

bool CullTriangle(WorkingTriangle tri)
{
		//	culling an imaginary triangle along the top of the grass
		//	objects provides a better result
	float4 p0 = mul(WorldToCullFrustum, float4(tri.pts[0] + float3(0,0,1),1));
	float4 p1 = mul(WorldToCullFrustum, float4(tri.pts[1] + float3(0,0,1),1));
	float4 p2 = mul(WorldToCullFrustum, float4(tri.pts[2] + float3(0,0,1),1));

	const bool accurateClip = true;
	if (!accurateClip) {
			// not exactly correct for large triangles -- each point could be outside, but part of the triangles could still be inside
		bool3 inFrustum = bool3(PtInFrustum(p0), PtInFrustum(p1), PtInFrustum(p2));
		return dot(inFrustum, bool3(true,true,true))!=0;
	} else {
		return TriInFrustum(p0, p1, p2);
	}
}

float2 CalculateTrangleDHDXY(float3 c0, float3 c1, float3 c2)
{
	float3 e1 = c1 - c0;
	float3 e2 = c2 - c0;
	float dhdx = (e1.z - (e1.y * e2.z) / e2.y) / (e1.x - (e2.x * e1.y) / e2.y);
	float dhdy = (e2.z / e2.y) - dhdx * e2.x;
	return float2(dhdx, dhdy);
}

float MinDistanceSq(float3 pt0, float3 pt1, float3 pt2)
{
	float3 e0 = pt0 - SysUniform_GetWorldSpaceView();
	float3 e1 = pt1 - SysUniform_GetWorldSpaceView();
	float3 e2 = pt2 - SysUniform_GetWorldSpaceView();
	return min(min(dot(e0, e0), dot(e1, e1)), dot(e2, e2));
}

[maxvertexcount(112)]
	void main(triangle VSOUT input[3], inout PointStream<GSOutput> outputStream)
{

		//
		//		Find regular points on the input geometry
		//		and output instances of new geometry.
		//
		//		This is a rasterisation operation...
		//			... we could also use a compute shader for this. Read one vertex buffer
		//			and write another.
		//

	WorkingTriangle tri;
	#if VSOUT_HAS_WORLD_POSITION
		tri.pts[0] = input[0].worldPosition.xyz;
		tri.pts[1] = input[1].worldPosition.xyz;
		tri.pts[2] = input[2].worldPosition.xyz;
	#else
		tri.pts[0] = input[0].position.xyz;
		tri.pts[1] = input[1].position.xyz;
		tri.pts[2] = input[2].position.xyz;
	#endif

	float2 tc0 = 0.0.xx, tc1 = 0.0.xx, tc2 = 0.0.xx;
	#if VSOUT_HAS_TEXCOORD
		tc0 = input[0].texCoord.xy;
		tc1 = input[1].texCoord.xy;
		tc2 = input[2].texCoord.xy;
	#endif

		//	Cull offscreen and distant triangles...
	bool inFrustum = CullTriangle(tri);
	[branch] if (inFrustum) {

			//		Sort min y to max y

		if (tri.pts[0].y > tri.pts[1].y) {
			Swap(tri.pts[0], tri.pts[1]);
		}
		if (tri.pts[1].y > tri.pts[2].y) {
			Swap(tri.pts[1], tri.pts[2]);
			if (tri.pts[0].y > tri.pts[1].y) {
				Swap(tri.pts[0], tri.pts[1]);
			}
		}

			//		Sometimes the triangle can be too big.. It would result
			//		in too many output vertices. In these cases, try to
			//		distribute the available geometry evenly over the polygon
			//		Every step reduces the instance count to a quarter

		float gridSpacing	= GridSpacing;
		uint ptCount		= EstimatePointCount(tri, gridSpacing);
		uint maxPtCount		= MaxOutputVertices/4;

		float distanceToCameraSq = MinDistanceSq(tri.pts[0], tri.pts[1], tri.pts[2]);
		maxPtCount /= (distanceToCameraSq * .0015f);	// fall off a bit with distance
		while (ptCount > maxPtCount) {
			gridSpacing *= 2.f;
			ptCount /= 4.f;
		}

			//		Edges always point down, and go from top to
			//		bottom

		float2 dhdxy = CalculateTrangleDHDXY(tri.pts[0], tri.pts[1], tri.pts[2]);

		uint outputVertices = 0;
		RasterizeBetweenEdges(tri.pts[0], tri.pts[1], tri.pts[0], tri.pts[2], tc0, tc1, tc0, tc2, dhdxy, outputVertices, gridSpacing, outputStream);
		RasterizeBetweenEdges(tri.pts[1], tri.pts[2], tri.pts[0], tri.pts[2], tc1, tc2, tc0, tc2, dhdxy, outputVertices, gridSpacing, outputStream);

	}
}


struct GSOutputWire
{

		//		Note -- need as few output components as possible...
		//				fewer output components mean more output
		//				vertices can fit in the geometry shader output
		//				buffer

	float4 position : SV_Position;
	float3 color	: COLOR0;
	float2 texCoord : TEXCOORD0;
};

void RasterizeLineBetweenEdges(	float3 e00, float3 e01, float3 e10, float3 e11,
								inout uint outputVertices,
								float spacing,
								inout LineStream<GSOutputWire> outputStream)
{
	float y = ceil(e00.y/spacing)*spacing;
	for (;y<e01.y; y+=spacing) {
		float e0a = (y - e00.y) / (e01.y - e00.y);
		float e2a = (y - e10.y) / (e11.y - e10.y);

		float spanx0 = lerp(e00.x, e01.x, e0a);
		float spanx1 = lerp(e10.x, e11.x, e2a);
		float spanz0 = lerp(e00.z, e01.z, e0a);
		float spanz1 = lerp(e10.z, e11.z, e2a);

		GSOutputWire output;
		output.color = 1.0.xxx;
		output.texCoord = 0.0.xx;

		if ((outputVertices+2)<=70) {
			float3 start	= float3(spanx0, y, spanz0);
			float3 end		= float3(spanx1, y, spanz1);

			output.position = mul(SysUniform_GetWorldToClip(), float4(start,1));
			outputStream.Append(output);

			output.position = mul(SysUniform_GetWorldToClip(), float4(end,1));
			outputStream.Append(output);
			outputStream.RestartStrip();
			outputVertices += 2;
		}
	}
}

[maxvertexcount(113)]
	void wireframe(triangle VSOUT input[3], inout LineStream<GSOutputWire> outputStream)
{
	GSOutputWire output;
	output.color = 1.0.xxx;
	output.texCoord = 0.0.xx;

	output.position = mul(SysUniform_GetWorldToClip(), float4(input[0].position.xyz,1));
	outputStream.Append(output);

	output.position = mul(SysUniform_GetWorldToClip(), float4(input[1].position.xyz,1));
	outputStream.Append(output);

	output.position = mul(SysUniform_GetWorldToClip(), float4(input[2].position.xyz,1));
	outputStream.Append(output);
	outputStream.RestartStrip();

	WorkingTriangle tri;
	tri.pts[0] = input[0].position.xyz;
	tri.pts[1] = input[1].position.xyz;
	tri.pts[2] = input[2].position.xyz;

		//	Sort min y to max y
	if (tri.pts[0].y > tri.pts[1].y) {
		Swap(tri.pts[0], tri.pts[1]);
	}
	if (tri.pts[1].y > tri.pts[2].y) {
		Swap(tri.pts[1], tri.pts[2]);
		if (tri.pts[0].y > tri.pts[1].y) {
			Swap(tri.pts[0], tri.pts[1]);
		}
	}

	float gridSpacing = GridSpacing;
	uint ptCount = EstimatePointCount(tri, gridSpacing);
	uint maxPtCount = MaxOutputVertices/4;
	while (ptCount > maxPtCount) {
		gridSpacing *= 2.f;
		ptCount /= 2.f;
	}

	uint outputVertices = 3;
	RasterizeLineBetweenEdges(tri.pts[0], tri.pts[1], tri.pts[0], tri.pts[2], outputVertices, gridSpacing, outputStream);
	RasterizeLineBetweenEdges(tri.pts[1], tri.pts[2], tri.pts[0], tri.pts[2], outputVertices, gridSpacing, outputStream);
}
