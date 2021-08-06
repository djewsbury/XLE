// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)


#include "../../Math/perlinnoise.hlsl"
#include "../../Math/EdgeDetection.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////

cbuffer Parameters
{
		//	common control parameters
	float2 Center;
	float Radius;
	float Adjustment;

	uint2 SurfaceMins;			// Minimum coord of the "CachedSurface", in terrain uber-surface coords
	uint2 SurfaceMaxs;			// Max coord of the "CachedSurface", in terrain uber-surface coords
	uint2 DispatchOffset;		// Terrain uber-surfacec coord when dispatchThreadId = uint3(0,0,0)
}

cbuffer GaussianParameters
{
	float4 Weights[33];		// 33 is the maximum filter size
	uint FilterSize;
	uint SmoothFlags;
}

float GetWeight(uint index)
{
	return Weights[index].x;
}

float LengthSquared(float2 input) { return dot(input, input); }

RWTexture2D<float> OutputSurface : register(u0);
Texture2D<float> InputSurface;		// blur methods need to write to a different surface

cbuffer RaiseLowerParameters
{
	float PowerValue;
}

[numthreads(16, 16, 1)]
	void RaiseLower(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 surfaceSpaceCoord = DispatchOffset + dispatchThreadId.xy;
	float rsq = LengthSquared(float2(surfaceSpaceCoord) - Center);
	if (surfaceSpaceCoord.x <= SurfaceMaxs.x && surfaceSpaceCoord.y <= SurfaceMaxs.y && rsq < (Radius*Radius)) {
		float r = sqrt(rsq);
		float A = (1.0f - r/Radius);

			// different strength values can have a really interesting result
			//		values between 1/8 -> 8 are the most interesting
		A = pow(A, PowerValue);

		OutputSurface[surfaceSpaceCoord - SurfaceMins] += Adjustment * A;
	}
}

float CalculateSmoothedHeight(uint2 surfaceSpaceCoord)
{
		//
		//		We need to blur on the surface around the given area.
		//		Performance is not a concern, so we can just implement it in a
		//		brute force manner
		//			-- we don't have to worry about separability, or anything like that
		//

	float accum = 0.f;
	int filterHalf = min(FilterSize, 33)/2;
	for (int y=-filterHalf; y<=filterHalf; ++y) {
		for (int x=-filterHalf; x<=filterHalf; ++x) {
			uint2 coords = uint2(surfaceSpaceCoord + int2(x,y));
			if (	coords.x >= SurfaceMins.x && coords.x <= SurfaceMaxs.x
				&&	coords.y >= SurfaceMins.y && coords.y <= SurfaceMaxs.y) {

				accum += GetWeight(filterHalf+x) * GetWeight(filterHalf+y) * InputSurface[coords - SurfaceMins];
			}
		}
	}
	return accum;
}

float2 CalculateDHDXY(uint2 surfaceSpaceCoord)
{
	float centerHeight = InputSurface[surfaceSpaceCoord - SurfaceMins];

	float2 dhdp = 0.0.xx;
	for (uint y=0; y<5; ++y) {
		for (uint x=0; x<5; ++x) {

			int2 coords = int2(surfaceSpaceCoord + int2(x,y) - int2(2,2));
			if (	coords.x >= int(SurfaceMins.x) && coords.x <= int(SurfaceMaxs.x)
				&&	coords.y >= int(SurfaceMins.y) && coords.y <= int(SurfaceMaxs.y)) {

				float heightDiff = InputSurface[coords - SurfaceMins] - centerHeight;
				dhdp.x += ScharrHoriz5x5[x][y] * heightDiff;
				dhdp.y += ScharrVert5x5[x][y] * heightDiff;
			}
		}
	}
	return dhdp;
}

[numthreads(16, 16, 1)]
	void Smooth(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 surfaceSpaceCoord = DispatchOffset + dispatchThreadId.xy;
	float rsq = LengthSquared(float2(surfaceSpaceCoord) - Center);
	if (surfaceSpaceCoord.x <= SurfaceMaxs.x && surfaceSpaceCoord.y <= SurfaceMaxs.y && rsq < (Radius*Radius)) {
		float smoothed = CalculateSmoothedHeight(surfaceSpaceCoord);

			//
			//		Effect of the blur fades off linearly with
			//		distance from the center...
			//
		float strength = Adjustment * saturate(1.0f - sqrt(rsq)/Radius);
		float oldHeight = InputSurface[surfaceSpaceCoord - SurfaceMins];

		bool ok = false;
		if (oldHeight < smoothed) {
			ok = (SmoothFlags&1)!=0;
		} else {
			ok = (SmoothFlags&2)!=0;
		}

		// We can try to bias the smoothing to apply more smoothly to (mostly)
		// flat areas... we want to be able to create smooth surfaces sometimes,
		// without change the shape of the major terrain features around it.
		// float2 dhdxy = CalculateDHDXY(surfaceSpaceCoord);
		// strength *= lerp(0.05f, 1.f, 1.0f - saturate(1.f * abs(dhdxy.x)));
		// strength *= lerp(0.05f, 1.f, 1.0f - saturate(1.f * abs(dhdxy.y)));

		if (ok) {
			OutputSurface[surfaceSpaceCoord - SurfaceMins] = lerp(oldHeight, smoothed, strength);
		}
	}
}

[numthreads(16, 16, 1)]
	void FineTune(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 surfaceSpaceCoord = DispatchOffset + dispatchThreadId.xy;
	float rsq = LengthSquared(float2(surfaceSpaceCoord) - Center);
	if (surfaceSpaceCoord.x <= SurfaceMaxs.x && surfaceSpaceCoord.y <= SurfaceMaxs.y && rsq < (Radius*Radius)) {

		float A = 1.0f - sqrt(rsq)/Radius;
		A = pow(A, 1.f/2.f);
		float strength = A;
		float oldHeight = OutputSurface[surfaceSpaceCoord - SurfaceMins];
		OutputSurface[surfaceSpaceCoord - SurfaceMins] = lerp(oldHeight, Adjustment, A);

	}
}

[numthreads(16, 16, 1)]
	void AddNoise(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 surfaceSpaceCoord = DispatchOffset + dispatchThreadId.xy;
	float rsq = LengthSquared(float2(surfaceSpaceCoord) - Center);
	if (surfaceSpaceCoord.x <= SurfaceMaxs.x && surfaceSpaceCoord.y <= SurfaceMaxs.y && rsq < (Radius*Radius)) {

		float noisyHeight = fbmNoise2D(
		 	float2(surfaceSpaceCoord.xy), 50.0f, 0.5f, 2.1042, 10);

		float A = 1.0f - sqrt(rsq)/Radius;
		A = pow(A, 1.f/8.f);
		float strength = Adjustment * A;
		OutputSurface[surfaceSpaceCoord - SurfaceMins] += strength * noisyHeight;

	}
}

cbuffer FillWithNoiseParameters
{
	float2 RectMins, RectMaxs;
	float BaseHeight;		// 250.f
	float NoiseHeight;		// 500.f
	float Roughness;		// 250.f
	float FractalDetail;	// 0.5f
}

[numthreads(16, 16, 1)]
	void FillWithNoise(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 surfaceSpaceCoord = DispatchOffset + dispatchThreadId.xy;
	if (surfaceSpaceCoord.x <= SurfaceMaxs.x && surfaceSpaceCoord.y <= SurfaceMaxs.y
		&& surfaceSpaceCoord.x >= uint(RectMins.x) && surfaceSpaceCoord.y >= uint(RectMins.y)
		&& surfaceSpaceCoord.x <= uint(RectMaxs.x) && surfaceSpaceCoord.y <= uint(RectMaxs.y)) {

		float noisyHeight = fbmNoise2D(
		 	float2(surfaceSpaceCoord.xy), Roughness, FractalDetail, 2.1042, 10);
		OutputSurface[surfaceSpaceCoord - SurfaceMins] = BaseHeight + NoiseHeight * noisyHeight;

	}
}

cbuffer CopyHeightParameters
{
	float2 SourceCoordinate;
	float CopyHeightPowerValue;
	uint CopyHeightFlags;
}

[numthreads(16, 16, 1)]
	void CopyHeight(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 surfaceSpaceCoord = DispatchOffset + dispatchThreadId.xy;
	float rsq = LengthSquared(float2(surfaceSpaceCoord) - Center);
	if (surfaceSpaceCoord.x <= SurfaceMaxs.x && surfaceSpaceCoord.y <= SurfaceMaxs.y && rsq < (Radius*Radius)) {

		float sourceHeight = OutputSurface[SourceCoordinate - SurfaceMins];

		float oldHeight = OutputSurface[surfaceSpaceCoord - SurfaceMins];

			// flags tell us if it's ok to raise up or down
		bool ok = false;
		if (oldHeight < sourceHeight) {
			ok = (CopyHeightFlags&1)!=0;
		} else {
			ok = (CopyHeightFlags&2)!=0;
		}

		if (ok) {
			float A = 1.0f - sqrt(rsq)/Radius;
			A = pow(A, CopyHeightPowerValue);
			float strength = (Adjustment / 100.f) * A;
			OutputSurface[surfaceSpaceCoord - SurfaceMins] = lerp(oldHeight, sourceHeight, strength);
		}

	}
}

cbuffer RotateParameters
{
	float2 RotationAxis;
	float RotationAngle;
}

void swap(inout float A, inout float B)
{
	float t = A;
	A = B;
	B = t;
}

float3x3 RotationMatrix(float3 axis, float angle)
{
	float sine, cosine;
	sincos(angle, sine, cosine);

	float omc = 1.f - cosine;

    float xomc = axis.x * omc;
    float yomc = axis.y * omc;
    float zomc = axis.z * omc;

    float xxomc = axis.x * xomc;
    float yyomc = axis.y * yomc;
    float zzomc = axis.z * zomc;
    float xyomc = axis.x * yomc;
    float yzomc = axis.y * zomc;
    float zxomc = axis.z * xomc;

    float xs = axis.x * sine;
    float ys = axis.y * sine;
    float zs = axis.z * sine;

	return float3x3(
		float3(xxomc + cosine, xyomc + zs, zxomc - ys),
		float3(xyomc - zs, yyomc + cosine, yzomc + xs),
		float3(zxomc + ys, yzomc - xs, zzomc + cosine));
}

[numthreads(16, 16, 1)]
	void Rotate(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 surfaceSpaceCoord = DispatchOffset + dispatchThreadId.xy;
	if (surfaceSpaceCoord.x >= SurfaceMins.x && surfaceSpaceCoord.y >= SurfaceMins.y
		&& surfaceSpaceCoord.x <= SurfaceMaxs.x && surfaceSpaceCoord.y <= SurfaceMaxs.y) {

			//	imagine that we've taken a column of land, and rotated it
			//	We can find the new height by walking along a vector perpendicular
			//		to the rotation axis, and checking the height values of the
			//		rotated elements as we go.

		float rotationOriginHeight = InputSurface[Center - SurfaceMins];
		float3 rotationOrigin = float3(Center, rotationOriginHeight);

		float2 rotationPerpen = float2(-RotationAxis.y, RotationAxis.x);	// (2d cross product)
			// assuming RotationAxis is normalized (and so will be rotationPerpen)
		float2 offsetFromCenter = surfaceSpaceCoord - Center;
		float2 flattenedOffset = offsetFromCenter - RotationAxis * dot(offsetFromCenter, RotationAxis);

		float2 walkingVector = rotationPerpen; // * sign(dot(flattenedOffset, rotationPerpen));

		float3x3 rotationMatrix = RotationMatrix(float3(RotationAxis, 0.f), RotationAngle);

			//	walk along "walkingVector", rotating the position every time, and looking for points that intersect
			//	with this cell in post rotated space.
			// we actually need to do a bresenham's line style iteration here...

		float newHeight = InputSurface[surfaceSpaceCoord - SurfaceMins] - 50.f;
		float3 samplingPoint = float3(surfaceSpaceCoord, InputSurface[surfaceSpaceCoord - SurfaceMins]);
		bool gotIntersection = false;

		{
				//	do we need to walk backwards, as well as forwards? Walking
				//	a little bit backwards might help with edge cases.
			int2 s = surfaceSpaceCoord - Radius * walkingVector;
			int2 e = surfaceSpaceCoord + Radius * walkingVector;

			int w = e.x - s.x;
			int h = e.y - s.y;
			// int dx1, dy1;
			// if (w<0) { dx1 = -1; } else if (w>0) { dx1 = 1; }
			// if (h<0) { dy1 = -1; } else if (h>0) { dy1 = 1; }
			// if (w<0) { dx2 = -1; } else if (w>0) { dx2 = 1; }
			int dx1 = sign(w);
			int dy1 = sign(h);
			int dx2 = dx1;
			int dy2 = 0;

			int longest = abs(w);
			int shortest = abs(h);
			if (!(longest>shortest)) {
					// these are the "y" dominant octants
				swap(longest, shortest);
				// if (h<0) { dy2 = -1; } else if (h>0) { dy2 = 1; }
				dy2 = sign(h);
				dx2 = 0;
			}

			float3 startPoint = 0.0.xxxx;
			bool startPointValid = false;

			int numerator = longest >> 1;
			for (int i=0; i<=longest; i++) {
				numerator += shortest;
				if (!(numerator<longest)) {
					numerator -= longest;
					s.x += dx1;
					s.y += dy1;
				} else {
					s.x += dx2;
					s.y += dy2;
				}

				float3 currentPoint;
				currentPoint.xy = float2(s.x, s.y);
				currentPoint.z = 0.f;

					//	sometimes our sample point can end up outside of the valid
					//	cached area. In these cases, we have to avoid attempting to sample
					//	this point
				bool currentPointValid =
						currentPoint.x >= SurfaceMins.x && currentPoint.y >= SurfaceMins.y
					&&	currentPoint.x <= SurfaceMaxs.x && currentPoint.y <= SurfaceMaxs.y;

				if (currentPointValid) {
					currentPoint.z = InputSurface[currentPoint.xy - SurfaceMins];

					if (startPointValid) {
							//	after rotation, does the vector between startPoint and currentPoint
							//	pass through the cell we're testing? If so, record it's height.
						float3 rotatedCurrent = currentPoint;
						if (LengthSquared(rotatedCurrent.xy - rotationOrigin.xy) < (Radius*Radius)) {
							rotatedCurrent = mul(rotationMatrix, currentPoint - rotationOrigin) + rotationOrigin;
						}

						float3 rotatedStart = startPoint;
						if (LengthSquared(rotatedStart.xy - rotationOrigin.xy) < (Radius*Radius)) {
							rotatedStart = mul(rotationMatrix, startPoint - rotationOrigin) + rotationOrigin;
						}

						float a0 = 0.00001f + dot((rotatedStart   - samplingPoint).xy, rotationPerpen);
						float a1 = 0.00001f + dot((rotatedCurrent - samplingPoint).xy, rotationPerpen);
						if ((a0 < 0.f) != (a1 < 0.f) && abs(a0 - a1) > 0.f) {
							float alpha = a0 / (a0 - a1);
							float interpolatedHeight = lerp(rotatedStart.z, rotatedCurrent.z, alpha);
							newHeight = max(newHeight, interpolatedHeight);
							gotIntersection = true;
						}
					}
				}

				startPoint = currentPoint;
				startPointValid = currentPointValid;
			}
		}

		if (gotIntersection && !isinf(newHeight) && !isnan(newHeight)) {
			OutputSurface[surfaceSpaceCoord - SurfaceMins] = clamp(newHeight, 0, 1000.f);
		}
	}
}

	//
	// Manipulator ideas:
	////////////////////////////////
	//		* stamp & copy (copy height pattern from one place to another)
	//		* anti-smooth
	//		* blocky noise
	//
	//		Only draw terrain grid around cursor
	//

///////////////////////////////////////////////////////////////////////////////////////////////////

#include "../TechniqueLibrary/RenderOverlays/dd/DebuggingShapes.hlsl"
#include "../TechniqueLibrary/RenderOverlays/dd/DebuggingPanels.hlsl"

Texture2D<float>		GPUCacheDebugging		: register(t5);

float4 GpuCacheDebugging(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	float2 outputDimensions = position.xy / texCoord.xy;
	float aspectRatio = outputDimensions.y / outputDimensions.x;

	float4 result = float4(0.0.xxx, 0.f);
	RenderTile(float2( 0,  0), float2(.5, .5), texCoord, GPUCacheDebugging, result);
	result.xyz /= 1000.f;

	return result;
}
