// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShadowDynamicSample.hlsl"

#define SHADOW_PROBE_BIQUADRATIC 1

#if SHADOW_PROBE
	struct StaticShadowProbeDesc
	{
		MiniProjZW _miniProjZW;
	};

	#if defined(LIGHT_RESOLVE_SHADER)
		TextureCubeArray<float> StaticShadowProbeDatabase : register(t10, space1);
		StructuredBuffer<StaticShadowProbeDesc> StaticShadowProbeProperties : register(t11, space1);
	#else
		TextureCubeArray<float> StaticShadowProbeDatabase : register(t11, space2);
		StructuredBuffer<StaticShadowProbeDesc> StaticShadowProbeProperties : register(t12, space2);
	#endif

	float SampleStaticShadowDatabase(uint databaseEntry, float3 offset, LightScreenDest screenDest)
	{
		return ResolveShadows_CubeMapArray(StaticShadowProbeDatabase, databaseEntry, StaticShadowProbeProperties[databaseEntry]._miniProjZW, offset);
	}

#endif

