// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Cubemap.hlsl"
#include "../TechniqueLibrary/Math/MathConstants.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"

Texture2D Input : register(t0, space0);
RWTexture2DArray<float4> Output : register(u1, space0);

float2 DirectionToEquirectangularCoord(float3 direction, bool hemi)
{
	if (hemi) return DirectionToHemiEquirectangularCoord_YUp(direction);
	return DirectionToEquirectangularCoord_YUp(direction);
}

void swap(inout float lhs, inout float rhs)
{
	float t = rhs;
	rhs = lhs;
	lhs = t;
}

float minX(float x, float y, float z, float w) { return min(min(min(x, y), z), w); }
float maxX(float x, float y, float z, float w) { return max(max(max(x, y), z), w); }

float CubeMapTexelSolidAngle(float2 faceCoordMin, float2 faceCoordMax)
{
        // Based on the method from here:
        //      http://www.rorydriscoll.com/2012/01/15/cubemap-texel-solid-angle/
        // We can calculate the solid angle of a single texel of the
        // cube map (which represents its weight in an angular based system)
        // On that page, Rory shows an algebraic derivation of this formula. See also
        // the comments section for a number of altnerative derivations (including
        // an interesting formula for the ratio of the area of a texel and the area on
        // the equivalent sphere surface).
    return CubeMapAreaElement(faceCoordMin.x, faceCoordMin.y) 
	 	 - CubeMapAreaElement(faceCoordMin.x, faceCoordMax.y)
		 - CubeMapAreaElement(faceCoordMax.x, faceCoordMin.y)
		 + CubeMapAreaElement(faceCoordMax.x, faceCoordMax.y);
}

float4 LoadInput(float2 xy, int2 dims)
{
	return Input.Load(uint3((int(xy.x)+dims.x)%dims.x, (int(xy.y)+dims.y)%dims.y, 0));
}

void Panel(inout float4 result, float2 tc, float2 tcMins, float2 tcMaxs, float3 panel[3], bool hemi)
{
    float3 plusX = panel[0];
    float3 plusY = panel[1];
    float3 center = panel[2];

    if (    tc.x >= tcMins.x && tc.y >= tcMins.y
        &&  tc.x <  tcMaxs.x && tc.y <  tcMaxs.y) {

		result.rgb = 0.0.xxx;
		result.a = 0.f;

#if 0

		// brute-force filtering! It's kind of silly, but it should be reasonably close to correct
		for (uint y=0; y<32; y++) {
			for (uint x=0; x<32; x++) {
				float2 face;
				face.x = 2.0f * (tc.x + x/32.0f - tcMins.x) / (tcMaxs.x - tcMins.x) - 1.0f;
        		face.y = 2.0f * (tc.y + y/32.0f - tcMins.y) / (tcMaxs.y - tcMins.y) - 1.0f;
				float3 finalDirection = center + plusX * face.x + plusY * face.y;
				float2 finalCoord = DirectionToEquirectangularCoord(finalDirection, hemi);
				result.rgb += Input.SampleLevel(EquirectangularPointSampler, finalCoord, 0).rgb;
			}
		}
		result.rgb /= (32*32);
		result.a = 1;

#elif 1

		float2 faceMin = 2.0f * float2((tc.x - tcMins.x) / (tcMaxs.x - tcMins.x), (tc.y - tcMins.y) / (tcMaxs.y - tcMins.y)) - 1.0.xx;
		float2 faceMax = 2.0f * float2((tc.x + 1 - tcMins.x) / (tcMaxs.x - tcMins.x), (tc.y + 1 - tcMins.y) / (tcMaxs.y - tcMins.y)) - 1.0.xx;
		// float2 faceMin = 2.0f * float2((tc.x - 0.5 - tcMins.x) / (tcMaxs.x - tcMins.x), (tc.y - 0.5 - tcMins.y) / (tcMaxs.y - tcMins.y)) - 1.0.xx;
		// float2 faceMax = 2.0f * float2((tc.x + 0.5 - tcMins.x) / (tcMaxs.x - tcMins.x), (tc.y + 0.5 - tcMins.y) / (tcMaxs.y - tcMins.y)) - 1.0.xx;

		int2 inputDims;
		Input.GetDimensions(inputDims.x, inputDims.y);

		// Find the min/max theta values for the projected cubemap texel in equirectangular coords
		// Fortunately the min/max values should always be at one of the corners, even in the case of the +Y,-Y faces
		float3 corners[] = {
			center + plusX * faceMin.x + plusY * faceMin.y,
			center + plusX * faceMax.x + plusY * faceMin.y,
			center + plusX * faceMin.x + plusY * faceMax.y,
			center + plusX * faceMax.x + plusY * faceMax.y
		};
		float maxTheta = CartesianToSpherical_YUp(corners[0]).y;
		float minTheta = maxTheta;
		[unroll] for (uint c=1; c<4; ++c) {
			float t = CartesianToSpherical_YUp(corners[c]).y;
			maxTheta = max(maxTheta, t);
			minTheta = min(minTheta, t);
		}
		if ((maxTheta - minTheta) > pi) {
			// wrapping near the theta=pi point
			minTheta += 2.0f * pi;
			swap(minTheta, maxTheta);
		}

		if (center.y == -1 || center.y == 1) {

			float minEquRectX = 0.5f*minTheta*reciprocalPi, maxEquRectX = 0.5f*maxTheta*reciprocalPi;
			for (float x=floor(inputDims.x * minEquRectX); x<ceil(inputDims.x * maxEquRectX); x+=1) {
				float theta = x/float(inputDims.x)*2.0f;
				float faceTheta = fmod(theta + 2.25, 0.5) - 0.25;
				theta = (theta-faceTheta)*pi;
				faceTheta *= pi;

				// alternative form:
				// float distanceToEdge = 1.0f / cos(faceTheta);
				// float inc = -atan2(1, distanceToEdge);

				// Let's try to find the min & max inclination for the cubemap texel in the equirectangular
				// projection.
				// Here we're going to be assuming that the min & max inclination will be at the corners... Which is
				// not entirely correct. The projected shape will bow out a little bit. It's pretty slight, though; this
				// might only cause us to miss a few pixels. We could just add a few pixels on the top & bottom to
				// compensate for this, since looping through additional pixels doesn't actually harm anything
				float2 axis = float2(-sin(theta), cos(theta));	// must match definition of theta in SphericalToCartesian_YUp
				float maxProjDist = dot(corners[0].xz - center.xz, axis);
				float minProjDist = maxProjDist;
				[unroll] for (uint c=1; c<4; ++c) {
					float len = dot(corners[c].xz - center.xz, axis);
					minProjDist = min(minProjDist, len);
					maxProjDist = max(maxProjDist, len);
				}
				float minInc, maxInc;
				if (center.y < 0) {
					minInc = -atan2(cos(faceTheta), maxProjDist);
					maxInc = -atan2(cos(faceTheta), minProjDist);
				} else {
					minInc = atan2(cos(faceTheta), minProjDist);
					maxInc = atan2(cos(faceTheta), maxProjDist);
				}

				float minEquRectY = 0.5f-(minInc * reciprocalPi), maxEquRectY = 0.5f-(maxInc*reciprocalPi);
				for (float y=floor(inputDims.y * minEquRectY); y<ceil(inputDims.y * maxEquRectY); y+=1) {
					float3 pixelDirection0 = EquirectangularCoordToDirection_YUp(float2((x)/float(inputDims.x),(y)/float(inputDims.y)));
					float3 pixelDirection1 = EquirectangularCoordToDirection_YUp(float2((x+1)/float(inputDims.x),(y)/float(inputDims.y)));
					float3 pixelDirection2 = EquirectangularCoordToDirection_YUp(float2((x)/float(inputDims.x),(y+1)/float(inputDims.y)));
					float3 pixelDirection3 = EquirectangularCoordToDirection_YUp(float2((x+1)/float(inputDims.x),(y+1)/float(inputDims.y)));
					pixelDirection0 /= dot(pixelDirection0, center);
					pixelDirection1 /= dot(pixelDirection1, center);
					pixelDirection2 /= dot(pixelDirection2, center);
					pixelDirection3 /= dot(pixelDirection3, center);
					float2 cube0 = float2(dot(pixelDirection0 - center, plusX), dot(pixelDirection0 - center, plusY));
					float2 cube1 = float2(dot(pixelDirection1 - center, plusX), dot(pixelDirection1 - center, plusY));
					float2 cube2 = float2(dot(pixelDirection2 - center, plusX), dot(pixelDirection2 - center, plusY));
					float2 cube3 = float2(dot(pixelDirection3 - center, plusX), dot(pixelDirection3 - center, plusY));
					float2 minCube = float2(minX(cube0.x, cube1.x, cube2.x, cube3.x), minX(cube0.y, cube1.y, cube2.y, cube3.y));
					float2 maxCube = float2(maxX(cube0.x, cube1.x, cube2.x, cube3.x), maxX(cube0.y, cube1.y, cube2.y, cube3.y));
					float minX = max(max(minCube.x, min(faceMin.x, faceMax.x)), -1);
					float minY = max(max(minCube.y, min(faceMin.y, faceMax.y)), -1);
					float maxX = min(min(maxCube.x, max(faceMin.x, faceMax.x)), 1);
					float maxY = min(min(maxCube.y, max(faceMin.y, faceMax.y)), 1);
					if (minX < maxX && minY < maxY) {
						float weight = CubeMapTexelSolidAngle(float2(minX, minY), float2(maxX, maxY));
						result += float4(weight*LoadInput(float2(x,y), inputDims).rgb, weight);
					}
					// result += float4(Input.SampleLevel(EquirectangularPointSampler, float2(x/inputDims.x, y/inputDims.y), 0).rgb, 1);
				}
			}

		} else {

			float3 lowDirection = center + plusX * faceMin.x + plusY * faceMin.y;
			float3 highDirection = center + plusX * faceMax.x + plusY * faceMax.y;

			float minEquRectX = 0.5f*minTheta*reciprocalPi, maxEquRectX = 0.5f*maxTheta*reciprocalPi;
			for (float x=floor(inputDims.x * minEquRectX); x<ceil(inputDims.x * maxEquRectX); x+=1) {
			// for (float x=0; x<inputDims.x; x+=4) {
				float faceTheta = x/float(inputDims.x)*2.0f;
				faceTheta = fmod(faceTheta + 2.25, 0.5) - 0.25;
				faceTheta *= pi;										// we need faceTheta in the (-.25*pi, .25*pi) for the following calculations
				float minInc = atan(lowDirection.y*cos(faceTheta));		// cos(faceTheta) here takes care of warping of the shape that occurs in X,-X,Z,-Z panels
				float maxInc = atan(highDirection.y*cos(faceTheta));
				float minEquRectY = 0.5f-(minInc * reciprocalPi), maxEquRectY = 0.5f-(maxInc*reciprocalPi);
				for (float y=floor(inputDims.y * minEquRectY); y<ceil(inputDims.y * maxEquRectY); y+=1) {
				// for (float y=0; y<inputDims.y; y+=4) {
					// We can project equirectangular point back onto the cubemap plane and see how much it overlaps with the 
					// cubemap pixel. The pixel has a distorted shape post projection; but if we assume that equirectangular input
					// pixels are small relative to the cubemap pixels, we may be able to ignore this
					float3 pixelDirection0 = EquirectangularCoordToDirection_YUp(float2((x)/float(inputDims.x),(y)/float(inputDims.y)));
					float3 pixelDirection1 = EquirectangularCoordToDirection_YUp(float2((x+1)/float(inputDims.x),(y+1)/float(inputDims.y)));
					// if (dot(pixelDirection0, center) < 0) continue;
					pixelDirection0 /= dot(pixelDirection0, center);
					pixelDirection1 /= dot(pixelDirection1, center);
					float2 cube0 = float2(dot(pixelDirection0 - center, plusX), dot(pixelDirection0 - center, plusY));
					float2 cube1 = float2(dot(pixelDirection1 - center, plusX), dot(pixelDirection1 - center, plusY));
					float minX = max(max(min(cube0.x, cube1.x), min(faceMin.x, faceMax.x)), -1);
					float minY = max(max(min(cube0.y, cube1.y), min(faceMin.y, faceMax.y)), -1);
					float maxX = min(min(max(cube0.x, cube1.x), max(faceMin.x, faceMax.x)), 1);
					float maxY = min(min(max(cube0.y, cube1.y), max(faceMin.y, faceMax.y)), 1);
					if (minX < maxX && minY < maxY) {
						float weight = CubeMapTexelSolidAngle(float2(minX, minY), float2(maxX, maxY));
						result += float4(weight*LoadInput(float2(x,y), inputDims).rgb, weight);
					}
					// result += float4(Input.SampleLevel(EquirectangularPointSampler, float2(x/inputDims.x, y/inputDims.y), 0).rgb, 1);
				}
			}

		}

		result /= result.a;

#endif
    }
}

float4 VerticalCubeMapCross(float2 texCoord, bool hemi)
{
	float4 result = 0.0.xxxx;
	Panel(
		result,
		texCoord,
		float2(1.0f/3.0f, 0.0f), float2(2.0f/3.0f, 1.0f/4.0f),
		VerticalCrossPanels_CubeMapGen[0], hemi);

	Panel(
		result,
		texCoord,
		float2(1.0f/3.0f, 1.0f/4.0f), float2(2.0f/3.0f, 2.0f/4.0f),
		VerticalCrossPanels_CubeMapGen[1], hemi);

	Panel(
		result,
		texCoord,
		float2(1.0f/3.0f, 2.0f/4.0f), float2(2.0f/3.0f, 3.0f/4.0f),
		VerticalCrossPanels_CubeMapGen[2], hemi);

	Panel(
		result,
		texCoord,
		float2(1.0f/3.0f, 3.0f/4.0f), float2(2.0f/3.0f, 1.0f),
		VerticalCrossPanels_CubeMapGen[3], hemi);

	Panel(
		result,
		texCoord,
		float2(0.0f, 1.0f/4.0f), float2(1.0f/3.0f, 2.0f/4.0f),
		VerticalCrossPanels_CubeMapGen[4], hemi);

	Panel(
		result,
		texCoord,
		float2(2.0f/3.0f, 1.0f/4.0f), float2(1.0f, 2.0f/4.0f),
		VerticalCrossPanels_CubeMapGen[5], hemi);
	return result;
}

float4 WriteVerticalCubeMapCross(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return VerticalCubeMapCross(texCoord, false);
}

float4 WriteVerticalHemiCubeMapCorss(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return VerticalCubeMapCross(texCoord, true);
}

[numthreads(8, 8, 6)]
	void EquRectToCube(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 textureDims; uint arrayLayerCount;
	Output.GetDimensions(textureDims.x, textureDims.y, arrayLayerCount);
	if (dispatchThreadId.x < textureDims.x && dispatchThreadId.y < textureDims.y) {
		float4 color;
		Panel(
			color,
			dispatchThreadId.xy, 0.0.xx, textureDims.xy,
			CubeMapFaces[dispatchThreadId.z],
			false);
		Output[dispatchThreadId.xyz] = color;
	}
}
