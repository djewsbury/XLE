// CompoundDocument:1
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/VSOUT.hlsl"
#include "../Standard/depth-plus-util.hlsl"

#if (VULKAN!=1)
	[earlydepthstencil]
#endif
void frameworkEntry()
{
	// note -- early rejection isn't supported
}

void DefaultPrevPosition(
	out float4 prevPosition : PREVPOSITION,
	float3 worldPosition : WORLDPOSITION)
{
	prevPosition = mul(SysUniform_GetPrevWorldToClip(), float4(worldPosition,1));
}

DepthPlusEncoded MakeDepthPlusEncoded(
	float4 position : SV_Position,
	float4 prevPosition : PREVPOSITION,
	GBufferValues sample : GBUFFERVALUES) : DEPTHPLUSENCODED
{
	float3 prevPos;
	float historyAccumulationWeight = 1;
	prevPos = prevPosition.xyz / prevPosition.w;
	prevPos.xy = SysUniform_GetViewportCenter() + prevPos.xy * SysUniform_GetViewportHalfWidthHeight();
	prevPos.xyz -= position.xyz;
	prevPos.xy = clamp(round(prevPos.xy), -127, 127);

	#if DEPTH_PLUS_HISTORY_ACCUMULATION
		int2 depthPrevDims;
		DepthPrev.GetDimensions(depthPrevDims.x, depthPrevDims.y);
		historyAccumulationWeight = CalculatePixelHistoryConfidence(
			position.xy, prevPos.xy,
			sample.worldSpaceNormal, sample.material.roughness, prevPosition.z / prevPosition.w,
			depthPrevDims);
	#endif
	return EncodeDepthPlus(sample, int2(prevPos.xy), historyAccumulationWeight);
}

// Some of the extended modes not supported due to awkwardness in making some of the patches conditional on selector
#if DEPTH_PLUS_NORMAL
	#error DEPTH_PLUS_NORMAL Not supported
#endif

#if DEPTH_PLUS_HISTORY_ACCUMULATION
	#error DEPTH_PLUS_HISTORY_ACCUMULATION Not supported
#endif

#if (VULKAN!=1)
	[earlydepthstencil]
#endif
void frameworkEntry_DepthPlus(
	out int2 motionBuffer : SV_Target0,
	DepthPlusEncoded encoded : DEPTHPLUSENCODED)
{
	// note -- early rejection isn't supported
	#if DEPTH_PLUS_MOTION
		motionBuffer = encoded.motionBuffer;
	#else
		motionBuffer = 0;
	#endif
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
	=~
		<.>::frameworkEntry
		Implements=SV_SystemPS

Entity=depthPlus
TechniqueDelegateConfig=depthPlus=~
	PipelineLayout=xleres/Config/main.pipeline:GraphicsMain
	Preconfiguration=xleres/Config/Preconfiguration.hlsl
RawMaterial=depthPlus=~
ShaderPatchCollection=depthPlus=~
ManualSelectorFiltering=depthPlus=~
ShaderPatchCollection=depthPlus=~
	=~
		<.>::DefaultPrevPosition
		Implements=SV_SystemPS
	=~
		<.>::MakeDepthPlusEncoded
		Implements=SV_SystemPS
	=~
		<.>::frameworkEntry_DepthPlus
		Implements=SV_SystemPS
	=~
		<.>::SampleFallback
		Implements=SV_SystemPS

)-- */
