// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(LIGHT_DESC_H)
#define LIGHT_DESC_H

struct AmbientDesc
{
	float3	Colour;
	float 	SkyReflectionScale;
	float	SkyReflectionBlurriness;
	float	Dummy0, Dummy1, Dummy2;
};

struct RangeFogDesc
{
	float3 	Inscatter;
	float	MonochromeOpticalThickness;
};

struct VolumeFogDesc
{
	float	OpticalThickness;
	float	DensityScale;
	float	HeightStart;
	float	HeightEnd;
	bool	EnableFlag;
	float3	SunInscatter;
	float3	AmbientInscatter;
	float	LargeParticleBalance;
	float	MieIsotropicCoefficient;
};

struct LightDesc
{
    float3	Position; 		float	CutoffRange;
	float3	Brightness; 	float	SourceRadiusX;
	float3	OrientationX; 	float	SourceRadiusY;
	float3	OrientationY; 	uint	Shape;
	float3	OrientationZ; 	float 	CosConeAngle;
	uint StaticDatabaseLightId; uint DynamicCubeDatabaseLightId; uint2 	Dummy2;
};

#define LIGHT_SHAPE_DIRECTIONAL		0
#define LIGHT_SHAPE_SPHERE			1			// (LIGHT_SHAPE_SPHERE|2) must equal LIGHT_SHAPE_CONE
#define LIGHT_SHAPE_TUBE			2
#define LIGHT_SHAPE_CONE			3
#define LIGHT_SHAPE_RECTANGLE		4

///////////////////////////////////////////////////////////////////////////////////////////////////
	//   structures used by resolvers...

struct LightScreenDest
{
    int2 pixelCoords;
    uint sampleIndex;
};

struct LightSampleExtra
{
    float screenSpaceOcclusion;
};

LightScreenDest LightScreenDest_Create(int2 pixelCoords, uint sampleIndex)
{
	LightScreenDest result;
	result.pixelCoords = pixelCoords;
	result.sampleIndex = sampleIndex;
	return result;
}

#endif
