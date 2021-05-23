// CompoundDocument:1

<<Chunk:FunctionLinkingGraph:main>>--(

main = DeclareInput(
    float4 position : SV_Position,
    float2 texCoord : TEXCOORD0,
    float3 viewFrustumVector : VIEWFRUSTUMVECTOR
    {{#passSampleIndex}}, uint sampleIndex : SV_SampleIndex{{/passSampleIndex}})

// Link in our main module
// We specify a filter for defines here. This is important because some defines
// are intended for this file (eg, for Mustache symbols) while other defines
// need to be passed down to this module
// It's good to be strict about this list, because the fewer defines get passed
// done to the modules, the fewer different versions of that module we have
libLightShape = Module(lib_lightshape.hlsl, GBUFFER_TYPE;MSAA_SAMPLERS;MSAA_SAMPLES;DIFFUSE_METHOD)
libShadow = Module(lib_shadow.hlsl, MSAA_SAMPLERS;MSAA_SAMPLES)
libHelper = Module(lib_helper.hlsl, MSAA_SAMPLERS;MSAA_SAMPLES;HAS_SCREENSPACE_AO)

// The basic structure is simple:
// 1) Calculate some inputs to the resolve operations
// 2) Perform each resolve step
// 3) Generate the output value by combining the resolve outputs
//
// Steps 1 and 3 are fixes, but step 2 varies depending on the options
// selected for the light (ie, this is where the dynamic linking occurs)
setup = libHelper.Setup(position, viewFrustumVector)
worldPosition = Alias(setup.2)
worldSpaceDepth = Alias(setup.3)
screenSpaceOcclusion = Alias(setup.4)
{{#passSampleIndex}}PassValue(sampleIndex, setup.5){{/passSampleIndex}}

light = libLightShape.DoResolve_{{shape}}(position, viewFrustumVector, worldPosition, screenSpaceOcclusion {{#passSampleIndex}}, sampleIndex{{/passSampleIndex}})
cascade = libShadow.DoResolve_{{cascade}}(position, texCoord, worldSpaceDepth)
shadow = libShadow.DoResolve_{{shadows}}(cascade.3, cascade.4, cascade.5, position {{#passSampleIndex}}, sampleIndex{{/passSampleIndex}})

finalize = libHelper.FinalizeResolve(light.result, shadow.result)
output = DeclareOutput(float4 outColour : SV_Target0)
outColour = finalize.result

)--
