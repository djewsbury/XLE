#include "../TechniqueLibrary/Framework/VSOUT.hlsl"
#include "../TechniqueLibrary/Framework/gbuffer.hlsl"
#include "MaterialParam.sh"

float3 CoordinatesToColor(float3 coords);
float4 AmendColor(VSOUT geo, float4 inputColor);

bool EarlyRejectionTest(VSOUT geo);
GBufferValues PerPixel(VSOUT geo);
float4 PerPixelCustomLighting(VSOUT geo);
    
void PerPixel_Separate(
    VSOUT geo, 
    out float3 diffuseAlbedo,
    out float3 worldSpaceNormal,

    out CommonMaterialParam material,

    out float blendingAlpha,
    out float normalMapAccuracy,
    out float cookedAmbientOcclusion,
    out float cookedLightOcclusion,

    out float3 transmission);
