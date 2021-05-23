// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "resolvefog.hlsl"
#include "VolumetricFog.hlsl"
#include "../Lighting/BasicLightingEnvironment.hlsl"
#include "../../Math/ProjectionMath.hlsl"
#include "../../Math/TextureAlgorithm.hlsl"
#include "../../Framework/CommonResources.hlsl"

float3 GetDirectionToSun() { return BasicLight[0].Position; }

Texture2D_MaybeMS<float>	DepthTexture	 	: register(t4);

#if (MONOCHROME_INSCATTER==1)
	Texture3D<float>		InscatterTexture	: register(t7);
#else
	Texture3D<float4>		InscatterTexture	: register(t7);
#endif
Texture3D<float>			TransmissionTexture	: register(t8);

float SampleTransmission(float2 coords, float slice) 	{ return TransmissionTexture.SampleLevel(ClampingSampler, float3(coords, slice), 0.f); }
float SampleInscatter(float2 coords, float slice) 		{ return InscatterTexture.SampleLevel(ClampingSampler, float3(coords, slice), 0.f); }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VolumeFogDesc MakeFogDesc()
{
	VolumeFogDesc result;
	result.OpticalThickness = OpticalThickness;
	result.HeightStart = HeightStart;
	result.HeightEnd = HeightEnd;
	result.EnableFlag = 0;
	result.SunInscatter = SunInscatter;
	result.AmbientInscatter = AmbientInscatter;
	return result;
}

float4 DoResolveFog(
	float4 position,
	float2 texCoord,
	float3 viewFrustumVector,
	SystemInputs sys,
	bool useVolumetricGrid)
{
	int2 pixelCoords		= position.xy;
	float depth				= LoadFloat1(DepthTexture, pixelCoords.xy, GetSampleIndex(sys));
	float worldSpaceDepth	= NDCDepthToWorldSpace(depth);
	float3 worldPosition 	= CalculateWorldPosition(viewFrustumVector, NDCDepthToLinear0To1(depth), SysUniform_GetWorldSpaceView());

	float transmission = 1.f;
	float inscatter = 0.f;
	VolumeFogDesc desc = MakeFogDesc();

	if (useVolumetricGrid) {

		float slice	= worldSpaceDepth / WorldSpaceGridDepth;
		if (slice > 1.f) {
			float3 gridEnd = CalculateWorldPosition(viewFrustumVector, WorldSpaceGridDepth / SysUniform_GetFarClip(), SysUniform_GetWorldSpaceView());
			CalculateTransmissionAndInscatter(desc, gridEnd, worldPosition, transmission, inscatter);

			#if (MONOCHROME_INSCATTER!=1)
				inscatter *= .1f * SunInscatter;
			#endif
		}

		slice = DepthBiasInvEq(saturate(slice));

		float gridTransmission = SampleTransmission(texCoord, slice);
		float gridInscatter = SampleInscatter(texCoord, slice);

		inscatter = inscatter * gridTransmission + gridInscatter;
		transmission *= gridTransmission;

	} else {

		CalculateTransmissionAndInscatter(desc, SysUniform_GetWorldSpaceView(), worldPosition, transmission, inscatter);
		#if (MONOCHROME_INSCATTER!=1)
			inscatter *= .1f * SunInscatter;
		#endif

	}

	#if (MONOCHROME_INSCATTER==1)

			// In theory, this might not be as accurate... Because we're
			// assuming the inscattering coefficient is the same all along
			// the ray. For long rays (in particular) this shouldn't really
			// be true. But the visual result is very close. So it seems
			// like calculate the scattering coefficient at every point along
			// the ray is redundant.
			// Note that we don't really need to do this calculation at a per
			// pixel level. We could calculate it as the same resolution as the
			// simulation grid (perhaps writing into a 2d texture from the same
			// compute shader). But we need the value for distance scattering, so
			// it can't just be integrated into the InscatterTexture

		float3 directionToSun = GetDirectionToSun();
		float3 directionToSample = viewFrustumVector;
		float directionToSampleRLength = rsqrt(dot(directionToSample, directionToSample));
		float cosTheta = dot(directionToSun, directionToSample) * directionToSampleRLength;
		return float4(inscatter * GetInscatterColor(desc, cosTheta), transmission);

	#else
		return float4(inscatter, transmission);
	#endif
}

float4 ResolveFog(
	float4 position : SV_Position,
	float2 texCoord : TEXCOORD0,
	float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
	SystemInputs sys) : SV_Target0
{
	return DoResolveFog(position, texCoord, viewFrustumVector, sys, true);
}

float4 ResolveFogNoGrid(
	float4 position : SV_Position,
	float2 texCoord : TEXCOORD0,
	float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
	SystemInputs sys) : SV_Target0
{
	return DoResolveFog(position, texCoord, viewFrustumVector, sys, false);
}
