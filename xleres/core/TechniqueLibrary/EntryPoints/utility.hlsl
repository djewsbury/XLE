// CompoundDocument:1
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/gbuffer.hlsl"

#define UTILITY_SHADER_TYPE_FLAT_COLOR 0
#define UTILITY_SHADER_TYPE_DIFFUSE_ALBEDO 1
#define UTILITY_SHADER_TYPE_WORLD_SPACE_POSITION 2
#define UTILITY_SHADER_TYPE_WORLD_SPACE_NORMAL 3
#define UTILITY_SHADER_TYPE_ROUGHNESS 4
#define UTILITY_SHADER_TYPE_METAL 5
#define UTILITY_SHADER_TYPE_SPECULAR 6
#define UTILITY_SHADER_TYPE_COOKEDAO 7

float4 frameworkEntry(
	GBufferValues sample : GBUFFERVALUES,
	float3 worldPosition : WORLDPOSITION,
	float4 color : COLOR0) : SV_Target0
{
	#if !defined(UTILITY_SHADER)

		return float4(1, 0, 0, 1);

	#elif UTILITY_SHADER == UTILITY_SHADER_TYPE_FLAT_COLOR

		return color;

	#elif UTILITY_SHADER == UTILITY_SHADER_TYPE_DIFFUSE_ALBEDO

		return float4(sample.diffuseAlbedo, sample.blendingAlpha);

	#elif UTILITY_SHADER == UTILITY_SHADER_TYPE_WORLD_SPACE_POSITION

		return float4(worldPosition, 1);

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

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

/* <<Chunk:StructuredDocument:main>>--(

Entity=main
TechniqueDelegateConfig=main=~
	PipelineLayout=xleres/Config/main.pipeline:GraphicsMain
	Preconfiguration=xleres/Config/Preconfiguration.hlsl
RawMaterial=main=~
ShaderPatchCollection=main=~
ManualSelectorFiltering=main=~
ShaderPatchCollection=main=~
	sys=~
		<.>::frameworkEntry
		Implements=SV_SystemPS

)-- */
