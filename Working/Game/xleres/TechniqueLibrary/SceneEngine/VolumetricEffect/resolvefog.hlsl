// Copyright 2016 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../LightingEngine/LightDesc.hlsl"
#include "../../Framework/CommonResources.hlsl"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CalculateTransmissionAndInscatter(
    VolumeFogDesc desc,
    float3 rayStart, float3 rayEnd, out float transmissionValue, out float inscatter)
{
	float3 diff = rayStart - rayEnd;
	float diffLen = length(diff);

    float a = saturate((desc.HeightEnd   - rayEnd.z) / (rayStart.z - rayEnd.z));
    float b = saturate((desc.HeightStart - rayEnd.z) / (rayStart.z - rayEnd.z));
    float ha = (lerp(rayEnd.z, rayStart.z, a) - desc.HeightStart) / (desc.HeightEnd - desc.HeightStart);
    float hb = (lerp(rayEnd.z, rayStart.z, b) - desc.HeightStart) / (desc.HeightEnd - desc.HeightStart);

        // We need to calculate an integral of density against distance for the ray
        // as it passes through the area where the falls off!
        // Fortunately, it's easy... The fog falls off linearly with height. So the
        // integral is just average of the density at the start and at the end (if the
        // camera is outside, that average should just be half the desnity value).
        // Distance within the partial area is easy, as well... We just have to handle the
        // situation where the camera is within the fog.
    float aveDensity = (ha + hb) * 0.5f;
    float partialDistance = abs(b - a) * diffLen;

	[branch] if (rayEnd.z < rayStart.z) {

			// Ray is pointing down. Camera might be outside of the volume, and we're
			// looking into it.
		float maxDensityDistance = 0.f;
		if (rayEnd.z < desc.HeightEnd)
			maxDensityDistance = diffLen * min(1.f, (desc.HeightEnd - rayEnd.z) / (rayStart.z - rayEnd.z));

		transmissionValue = exp(-desc.OpticalThickness * (aveDensity * partialDistance + maxDensityDistance));

			// Calculate the inscattered light. This is a little more difficult than the
			// transmission coefficient!
			// Light scatters in proportional to distance (and proportional to density in
			// the partial section).
			// However occlusion should also apply to the inscattered light (ie, the transmission
			// coefficient should take effect). The inscattered light can turn out to be a lot of
			// light, so this is can important step. But the math is a bit more complex.
		// inscatter = CalculateInscatter(maxDensityDistance, desc.OpticalThickness);
		inscatter = maxDensityDistance * exp(-desc.OpticalThickness * .5f * maxDensityDistance);
        float outscatter = exp(-desc.OpticalThickness * aveDensity * partialDistance);
		inscatter *= outscatter;

			// rough estimate for inscatter in the partial area...
			// this is a cheap hack, but it works surprisingly well!
		// inscatter += CalculatePartialInscatter(partialDistance, aveDensity, desc.OpticalThickness);
		inscatter += aveDensity * partialDistance * sqrt(outscatter); // exp(-desc.OpticalThickness * aveDensity * .5f * partialDistance);

	} else {

			// Ray is pointing up. Camera might be inside of the volume, and we're
			// looking out of it.
		float maxDensityDistance = 0.f;
		if (rayStart.z < desc.HeightEnd)
			maxDensityDistance = diffLen * min(1.f, (desc.HeightEnd - rayStart.z) / (rayEnd.z - rayStart.z));

		transmissionValue = exp(-desc.OpticalThickness * (aveDensity * partialDistance + maxDensityDistance));

		//inscatter = CalculateInscatter(maxDensityDistance, desc.OpticalThickness);
		//inscatter += exp(-desc.OpticalThickness * maxDensityDistance)
		//	* CalculatePartialInscatter(partialDistance, aveDensity, desc.OpticalThickness);

		inscatter = aveDensity * partialDistance * exp(-desc.OpticalThickness * aveDensity * .5f * partialDistance);
        float outscatter = exp(-desc.OpticalThickness * maxDensityDistance);
		inscatter *= outscatter;
		inscatter += maxDensityDistance * sqrt(outscatter); // exp(-desc.OpticalThickness * .5f * maxDensityDistance);

	}
}

float3 GetInscatterColor(VolumeFogDesc desc, float cosTheta)
{
    cosTheta = max(0, cosTheta);
    cosTheta *= cosTheta;
    cosTheta *= cosTheta;
    return desc.AmbientInscatter + cosTheta * desc.SunInscatter;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
Texture2D<float> VolFogLookupTable : register(t23);

cbuffer VolFogLookupTableConstants : register(b13)
{
	float LookupTableMaxValue;
	float LookupTableMaxDistance;
}

float CalculatePartialInscatter(float distance, float densScale, float opticalThickness)
{
	const bool usePrecalculatedDensity = true;
	if (usePrecalculatedDensity) {
		return LookupTableMaxValue * VolFogLookupTable.SampleLevel(
			ClampingSampler, float2(distance / LookupTableMaxDistance, densScale), 0);
	} else {
		float result = 0.f;
		const uint stepCount = 256;
		const float stepDistance = distance / float(stepCount);
		float t = 1.f; // exp(-Density * stepDistance);
		for (uint c=0; c<stepCount; ++c) {
			result += t * stepDistance * densScale * opticalThickness;
			t *= exp(-opticalThickness * densScale * stepDistance);
		}
		return result;
	}
}

float CalculateInscatter(float distance, float opticalThickness)
{
	return CalculatePartialInscatter(distance, 1.f, opticalThickness);
}
#endif
