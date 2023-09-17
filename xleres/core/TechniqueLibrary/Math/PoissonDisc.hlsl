// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(POISSON_DISC_HLSL)
#define POISSON_DISC_HLSL

static const float2 gPoissonDisc32Tap[] =
{
	float2(-0.1924249f, -0.5685654f),
	float2(0.0002287195f, -0.830722f),
	float2(-0.6227817f, -0.676464f),
	float2(-0.3433303f, -0.8954138f),
	float2(-0.3087259f, 0.0593961f),
	float2(0.4013956f, 0.005351349f),
	float2(0.6675568f, 0.2226908f),
	float2(0.4703487f, 0.4219977f),
	float2(-0.865732f, -0.1704932f),
	float2(0.4836336f, -0.7363456f),
	float2(-0.8455518f, 0.429606f),
	float2(0.2486194f, 0.7276461f),
	float2(0.01841145f, 0.581219f),
	float2(0.9428069f, 0.2151681f),
	float2(-0.2937738f, 0.8432091f),
	float2(0.01657544f, 0.9762882f),
	float2(0.03878351f, -0.1410931f),
	float2(-0.3663213f, -0.348966f),
	float2(0.2333971f, -0.5178556f),
	float2(-0.6433204f, -0.3284476f),
	float2(0.1255225f, 0.3221043f),
	float2(0.4051761f, -0.299208f),
	float2(0.8829983f, -0.1718857f),
	float2(0.6724088f, -0.3562584f),
	float2(-0.826445f, 0.1214067f),
	float2(-0.386752f, 0.406546f),
	float2(-0.5869312f, -0.01993746f),
	float2(0.7842119f, 0.5549603f),
	float2(0.5801646f, 0.7416336f),
	float2(0.7366455f, -0.6388465f),
	float2(-0.6067169f, 0.6372176f),
	float2(0.2743046f, -0.9303559f)
};

#endif
