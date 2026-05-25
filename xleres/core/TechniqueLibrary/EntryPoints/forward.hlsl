// CompoundDocument:1
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/gbuffer.hlsl"
#include "../Math/TextureAlgorithm.hlsl" // (for SystemInputs)
#include "../../Forward/ForwardPlusLighting.hlsl"

float4 frameworkEntry(
	GBufferValues sample : GBUFFERVALUES,
	float4 position : SV_Position,
	float3 worldPosition : WORLDPOSITION,
	float3 worldVertexNormal : NORMAL,
	// float3 worldViewVector : WORLDVIEWVECTOR,
	SystemInputs sys : SYSTEMINPUTS) : SV_Target0
{
	const bool hasNormal = true;		// todo -- can't detect when there is no normal
	float3 result =
		CalculateIllumination(
			sample, normalize(SysUniform_GetWorldSpaceView()-worldPosition), worldPosition, worldVertexNormal,
			NDCDepthToLinear0To1(position.z),
			LightScreenDest_Create(int2(position.xy), GetSampleIndex(sys)), 
			hasNormal);
	return float4(result, sample.blendingAlpha);
}

GBufferValues SampleFallback() : GBUFFERVALUES { return GBufferValues_Default(); }

SystemInputs InitializeSystemInputs(uint sampleIndex : SV_SampleIndex) : SYSTEMINPUTS
{
	SystemInputs result;
	#if MSAA_SAMPLES > 1
        result.sampleIndex = sampleIndex;
    #endif
	return result;
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

/* <<Chunk:StructuredDocument:main>>--(

Entity=main
TechniqueDelegateConfig=main=~
	PipelineLayout=xleres/Forward/forward.pipeline:GraphicsForwardPlus
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
	=~
		<.>::InitializeSystemInputs
		Implements=SV_SystemPS

)-- */
