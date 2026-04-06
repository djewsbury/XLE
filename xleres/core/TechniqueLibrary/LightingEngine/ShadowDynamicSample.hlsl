// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if DYNAMIC_SHADOW_PROBE

	struct DynamicShadowDesc
	{
		MiniProjZW _miniProjZW;
	};

	TextureCubeArray<float> DynamicShadowDatabase : register(t7, space2);
	StructuredBuffer<DynamicShadowDesc> DynamicShadowProperties : register(t8, space2);

	TextureCubeArray<float> DynamicCubeShadowDatabase : register(t9, space2);
	StructuredBuffer<DynamicShadowDesc> DynamicCubeShadowProperties : register(t10, space2);

	float SampleDynamicCubeShadowDatabase(uint databaseEntry, float3 offset, LightScreenDest screenDest)
	{
		return ResolveShadows_CubeMapArray(DynamicCubeShadowDatabase, databaseEntry/6, DynamicCubeShadowProperties[databaseEntry]._miniProjZW, offset);
	}

#endif

