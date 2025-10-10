// CompoundDocument:1
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/gbuffer.hlsl"
#include "../Math/TextureAlgorithm.hlsl" // (for SystemInputs)
#include "../../Forward/ForwardPlusLighting.hlsl"

#if (VULKAN!=1)
	[earlydepthstencil]
#endif
float4 frameworkEntry(
	GBufferValues sample : GBUFFERVALUES,
	float4 position : SV_Position,
	float3 worldPosition : WORLDPOSITION,
	float3 worldVertexNormal : NORMAL,
	float3 worldViewVector : WORLDVIEWVECTOR,
	SystemInputs sys) : SV_Target0
{
	GBufferValues sample = PerPixel(geo);

	#if VSOUT_HAS_NORMAL
		const bool hasNormal = true;
	#else
		const bool hasNormal = false;
	#endif
	float3 result =
		CalculateIllumination(
			sample, normalize(worldViewVector), worldPosition, worldVertexNormal,
			NDCDepthToLinear0To1(position.z),
			LightScreenDest_Create(int2(position.xy), GetSampleIndex(sys)), 
			hasNormal);

	return float4(result, sample.blendingAlpha);
}

GBufferValues SampleFallback() : GBUFFERVALUES { return GBufferValues_Default(); }

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
	=~
		<.>::SampleFallback
		Implements=SV_SystemPS

)-- */
