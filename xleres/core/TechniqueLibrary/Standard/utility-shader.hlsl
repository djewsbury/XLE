
#define UTILITY_SHADER_TYPE_FLAT_COLOR 0
#define UTILITY_SHADER_TYPE_DIFFUSE_ALBEDO 1
#define UTILITY_SHADER_TYPE_WORLD_SPACE_POSITION 2
#define UTILITY_SHADER_TYPE_WORLD_SPACE_NORMAL 3
#define UTILITY_SHADER_TYPE_ROUGHNESS 4
#define UTILITY_SHADER_TYPE_METAL 5
#define UTILITY_SHADER_TYPE_SPECULAR 6
#define UTILITY_SHADER_TYPE_COOKEDAO 7

float4 UtilityShaderValue(VSOUT geo, GBufferValues sample)
{
    #if !defined(UTILITY_SHADER)

        return float4(1, 0, 0, 1);

    #elif UTILITY_SHADER == UTILITY_SHADER_TYPE_FLAT_COLOR

        return VSOUT_GetColor0(geo);

    #elif UTILITY_SHADER == UTILITY_SHADER_TYPE_DIFFUSE_ALBEDO

        return float4(sample.diffuseAlbedo, sample.blendingAlpha);

    #elif UTILITY_SHADER == UTILITY_SHADER_TYPE_WORLD_SPACE_POSITION

        return float4(VSOUT_GetWorldPosition(geo), 1);

    #elif UTILITY_SHADER == UTILITY_SHADER_TYPE_WORLD_SPACE_NORMAL

        return float4(0.5 * sample.worldSpaceNormal + 0.5, 1);

    #elif UTILITY_SHADER == UTILITY_SHADER_TYPE_ROUGHNESS

        return float4(sample.material.roughness.xxx, 1);

    #elif UTILITY_SHADER == UTILITY_SHADER_TYPE_METAL

        return float4(sample.material.metal.xxx, 1);

    #elif UTILITY_SHADER == UTILITY_SHADER_TYPE_SPECULAR

        return float4(sample.material.specular.xxx, 1);

    #elif UTILITY_SHADER == UTILITY_SHADER_TYPE_COOKEDAO

        return float4(sample.cookedAmbientOcclusion.xxx, 1);

    #else

        return 1;

    #endif
}

