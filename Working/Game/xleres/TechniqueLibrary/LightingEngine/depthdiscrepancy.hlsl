// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "xleres/TechniqueLibrary/Framework/gbuffer.hlsl"		// for gbuffer normal reading
#include "xleres/TechniqueLibrary/Math/ProjectionMath.hlsl"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct DepthDiscrepancy_Normals
{
	float2 _depthdivEstimate;
	float _centerDepth;
	uint2 _centerPixel;
	float _accuracyRange;
	Texture2D<float> _mainDepth;
};

float2 LocalWorldSpaceDerivatives(int2 px, Texture2D<float> mainDepth)
{
	int2 twiddler = int2(0,1);
	float outDepth0 = NDCDepthToWorldSpace(mainDepth[px.xy + twiddler.xx]);
	float outDepth1 = NDCDepthToWorldSpace(mainDepth[px.xy + twiddler.yx]);
	float outDepth2 = NDCDepthToWorldSpace(mainDepth[px.xy + twiddler.xy]);
	float outDepth3 = NDCDepthToWorldSpace(mainDepth[px.xy + twiddler.yy]);
	return float2(
		((outDepth1 - outDepth0) + (outDepth3 - outDepth2))/2, 
		((outDepth2 - outDepth0) + (outDepth3 - outDepth1))/2);
}

DepthDiscrepancy_Normals DepthDiscrepancy_Normals_Setup(uint2 pixelId, Texture2D<float> mainDepth, Texture2D<float3> gbufferNormal)
{
	// Try estimating the depth buffer derivatives by looking at the worldspace normals
	//
	// this is a little odd, because the worldspace normal isn't necessarily the normal of the geometry
	// it's modified by normal mapping & vertex interpolation
	//
	// However, we may be able to estimate something simplier -- such as if a sample from the depth buffer
	// is significantly too deep to be on the plane
	//
	// We loose some accuracy due to the way the normal is stored in the gbuffer
	//
	// Overall, this method may just not be accurate enough -- and it's unclear if it's preferable to other methods

	float3 worldSpaceNormal = DecodeGBufferNormal(gbufferNormal[pixelId]);

#if 1
	// The 2 cases here are basically the same, just with a different approach to the math
	float3 cameraRight = float3(SysUniform_GetCameraBasis()[0].x, SysUniform_GetCameraBasis()[1].x, SysUniform_GetCameraBasis()[2].x);
	float3 cameraUp = float3(SysUniform_GetCameraBasis()[0].y, SysUniform_GetCameraBasis()[1].y, SysUniform_GetCameraBasis()[2].y);
	float3 negCameraForward = float3(SysUniform_GetCameraBasis()[0].z, SysUniform_GetCameraBasis()[1].z, SysUniform_GetCameraBasis()[2].z);

	float 
		dotZ = dot(worldSpaceNormal, -negCameraForward), 
		dotX = dot(worldSpaceNormal, -cameraRight), 
		dotY = dot(worldSpaceNormal, -cameraUp);
	float2 viewSpaceGradient = float2(dotX, dotY) / dotZ.xx;

	// (d vz / d cx) = (d vz / d vx) * (d vx / d cx)
	// cx = (A*vx+B) / -vz
	// vx = (-cx*vz-B)/A
	// assume vz is constant, then (d vx / d cx) = -vz / A
	// when we do this, vz will later get removed from the equation (which is correct for orthogonal)

	float2 worldSpaceGradientInClipCoords = float2(
		dotZ / SysUniform_GetMinimalProjection().x,
		dotZ / SysUniform_GetMinimalProjection().y) * viewSpaceGradient;
#else
	float3x3 viewToWorld = float3x3(SysUniform_GetCameraBasis()[0].xyz, SysUniform_GetCameraBasis()[1].xyz, SysUniform_GetCameraBasis()[2].xyz);
	float3 viewSpaceNormal = mul(transpose(viewToWorld), worldSpaceNormal);
	float2 viewSpaceGradient = float2(viewSpaceNormal.x / viewSpaceNormal.z, viewSpaceNormal.y / viewSpaceNormal.z);
#endif

	float center = mainDepth[pixelId];

	MiniProjZW miniProj = AsMiniProjZW(SysUniform_GetMinimalProjection());
	const float b = 8.f/16777215.f;
	float accuracyRange = b*miniProj.W/((center+miniProj.Z)*(b+center+miniProj.Z));
	float2 depthdiv = LocalWorldSpaceDerivatives(pixelId, mainDepth);
	accuracyRange += 0.25f * max(abs(depthdiv.x), abs(depthdiv.y));		// we need to widen the bias with the slope

	DepthDiscrepancy_Normals result;
	result._depthdivEstimate = viewSpaceGradient * (float2(1.f / 1920.f, 1.f / 1080.f) * 2.0f) / float2(SysUniform_GetMinimalProjection().x, SysUniform_GetMinimalProjection().y);
	result._centerDepth = NDCDepthToWorldSpace(center);
	result._centerPixel = pixelId;
	result._accuracyRange = accuracyRange;
	result._mainDepth = mainDepth;
	return result;
}

bool DepthDiscrepancy_IsContinuous(DepthDiscrepancy_Normals calculator, int2 pixelSpaceOffset)
{
	float wsEstimate = calculator._centerDepth + dot(calculator._depthdivEstimate.xy, pixelSpaceOffset);
	float wsTest = NDCDepthToWorldSpace(calculator._mainDepth[calculator._centerPixel + pixelSpaceOffset]);
	return wsTest > (wsEstimate-calculator._accuracyRange);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct DepthDiscrepancy_FloatTexture
{
	float2 _depthdivEstimate;
	float _centerDepth;
	uint2 _centerPixel;
	Texture2D<float> _mainDepth;
};

DepthDiscrepancy_FloatTexture DepthDiscrepancy_FloatTexture_Setup(uint2 pixelId, Texture2D<float> mainDepth)
{
	int4 twiddler = int4(0,1,0,1);		// sx, lx, sy, ly
	if (pixelId.y & 1) { twiddler.x = -1; twiddler.y = 0; }		// this adds a lot of noise; but may actually be moderately more balanced?
	if (pixelId.x & 1) { twiddler.z = -1; twiddler.w = 0; }

	float outDepth0 = mainDepth[pixelId.xy + twiddler.xz];
	float outDepth1 = mainDepth[pixelId.xy + twiddler.yz];
	float outDepth2 = mainDepth[pixelId.xy + twiddler.xw];
	float outDepth3 = mainDepth[pixelId.xy + twiddler.yw];

	DepthDiscrepancy_FloatTexture result;
	result._depthdivEstimate = float2(
		((outDepth1 - outDepth0) + (outDepth3 - outDepth2))/2, 
		((outDepth2 - outDepth0) + (outDepth3 - outDepth1))/2);

	// depthdiv = float2(outDepth1 - outDepth0, outDepth2 - outDepth0);

	result._centerDepth = mainDepth[pixelId];
	result._centerPixel = pixelId;
	result._mainDepth = mainDepth;
	return result;
}

bool DepthDiscrepancy_IsContinuous(DepthDiscrepancy_FloatTexture calculator, int2 pixelSpaceOffset)
{
	float test = calculator._mainDepth[calculator._centerPixel + pixelSpaceOffset];
	float estimate = calculator._centerDepth + dot(calculator._depthdivEstimate.xy, pixelSpaceOffset);
	// return abs(estimate-test) <= 8.f/65535.f;		// 16 bit
	return abs(estimate-test) <= 8.f/16777215.f;		// 24 bit
}

int DepthDiscrepancy_Test(DepthDiscrepancy_FloatTexture calculator, int2 pixelSpaceOffset)
{
	float test = calculator._mainDepth[calculator._centerPixel + pixelSpaceOffset];
	float estimate = calculator._centerDepth + dot(calculator._depthdivEstimate.xy, pixelSpaceOffset);
	// const float comparison = 8.f/65535.f;		// 16 bit
	const float comparison = 8.f/16777215.f;		// 24 bit
	float A = test-estimate;
	if (A > comparison) return 1;
	if (A < -comparison) return -1;
	return 0;
}

int DepthDiscrepancy_TestRatio(DepthDiscrepancy_FloatTexture calculator, int2 pixelSpaceOffset, float deadzone)
{
	float test = calculator._mainDepth[calculator._centerPixel + pixelSpaceOffset];
	float estimate = calculator._centerDepth + dot(calculator._depthdivEstimate.xy, pixelSpaceOffset);
	float A = test/estimate;
	if (A > (1+deadzone)) return 1;
	if (A < (1-deadzone)) return -1;
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct DepthDiscrepancy_IntTexture
{
	int2 _depthdivEstimate;
	int _centerDepth;
	uint2 _centerPixel;
	Texture2D<int> _mainDepth;
};

DepthDiscrepancy_IntTexture DepthDiscrepancy_IntTexture_Setup(uint2 pixelId, Texture2D<int> mainDepth)
{
	int4 twiddler = int4(0,1,0,1);		// sx, lx, sy, ly
	if (pixelId.y & 1) { twiddler.x = -1; twiddler.y = 0; }		// this adds a lot of noise; but may actually be moderately more balanced?
	if (pixelId.x & 1) { twiddler.z = -1; twiddler.w = 0; }

	int outDepth0 = mainDepth[pixelId.xy + twiddler.xz];
	int outDepth1 = mainDepth[pixelId.xy + twiddler.yz];
	int outDepth2 = mainDepth[pixelId.xy + twiddler.xw];
	int outDepth3 = mainDepth[pixelId.xy + twiddler.yw];

	DepthDiscrepancy_IntTexture result;
	result._depthdivEstimate = int2(
		((outDepth1 - outDepth0) + (outDepth3 - outDepth2))/2, 
		((outDepth2 - outDepth0) + (outDepth3 - outDepth1))/2);

	// depthdiv = float2(outDepth1 - outDepth0, outDepth2 - outDepth0);

	result._centerDepth = mainDepth[pixelId];
	result._centerPixel = pixelId;
	result._mainDepth = mainDepth;
	return result;
}

bool DepthDiscrepancy_IsContinuous(DepthDiscrepancy_IntTexture calculator, int2 pixelSpaceOffset)
{
	int test = calculator._mainDepth[calculator._centerPixel + pixelSpaceOffset];
	int estimate = calculator._centerDepth + dot(calculator._depthdivEstimate.xy, pixelSpaceOffset);
	// We have to compensate for clamping to integer values in 2 places
	// 	1. when the depth value is written to the depth buffer
	// 	2. after we create the estimate
	// looks like we might have to use twice the kernel radius as an error factor
	return abs(estimate-test) <= 8;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct DepthDiscrepancy_WorldSpace		// note -- this structure must actually be different from DepthDiscrepancy_Normals, otherwise the compiler considers it ambiguous!
{
	float2 _depthdivEstimate;
	float _centerDepth;
	float _accuracyRange;
	uint2 _centerPixel;
	Texture2D<float> _mainDepth;
};

DepthDiscrepancy_WorldSpace DepthDiscrepancy_WorldSpace_Setup(uint2 pixelId, Texture2D<float> mainDepth)
{
	// This is a similar result, just done in worldspace space, still based on NDC values
	// we get from the depth buffer

	float outDepth0 = NDCDepthToWorldSpace(mainDepth[pixelId.xy + int2(0,0)]);
	float outDepth1 = NDCDepthToWorldSpace(mainDepth[pixelId.xy + int2(1,0)]);
	float outDepth2 = NDCDepthToWorldSpace(mainDepth[pixelId.xy + int2(0,1)]);
	float outDepth3 = NDCDepthToWorldSpace(mainDepth[pixelId.xy + int2(1,1)]);

	DepthDiscrepancy_WorldSpace result;
	result._depthdivEstimate = float2(
		((outDepth1 - outDepth0) + (outDepth3 - outDepth2))/2, 
		((outDepth2 - outDepth0) + (outDepth3 - outDepth1))/2);

	float A = mainDepth[pixelId];
	float center = NDCDepthToWorldSpace(A);

	// range = W/(x+Z) - W/(x+b+Z)
	// 		 = bW/((x+Z)(b+x+Z)		... not massively improved

	MiniProjZW miniProj = AsMiniProjZW(SysUniform_GetMinimalProjection());
	const float b = 8.f/16777215.f;
	// float accuracyRange = abs(miniProj.W / ((A + 8.f/16777215.f) + miniProj.Z) - center);
	float accuracyRange = b*miniProj.W/((A+miniProj.Z)*(b+A+miniProj.Z));
	// we need to widen the bias with the slope... This begins to work less well with bigger distances between the near and far clip
	accuracyRange += 0.25f * max(abs(result._depthdivEstimate.x), abs(result._depthdivEstimate.y));

	result._centerDepth = center;
	result._centerPixel = pixelId;
	result._accuracyRange = accuracyRange;
	result._mainDepth = mainDepth;
	return result;
}

bool DepthDiscrepancy_IsContinuous(DepthDiscrepancy_WorldSpace calculator, int2 pixelSpaceOffset)
{
	float test = NDCDepthToWorldSpace(calculator._mainDepth[calculator._centerPixel + pixelSpaceOffset]);
	float estimate = calculator._centerDepth + dot(calculator._depthdivEstimate.xy, pixelSpaceOffset);
	return abs(estimate-test) <= calculator._accuracyRange;
}
