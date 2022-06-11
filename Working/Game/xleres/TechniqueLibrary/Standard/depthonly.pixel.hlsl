#include "../Framework/VSOUT.hlsl"
#include "depth-plus-util.hlsl"
#include "../../Nodes/Templates.pixel.sh"

[earlydepthstencil]
void frameworkEntryDepthOnly() {}

void frameworkEntryDepthOnlyWithEarlyRejection(VSOUT geo)
{
    if (EarlyRejectionTest(geo))
        discard;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

[earlydepthstencil]
DepthPlusEncoded depthPlus(VSOUT geo)
{
	GBufferValues sample = PerPixel(geo);
	return ResolveDepthPlus(geo, sample);
}

DepthPlusEncoded depthPlusWithEarlyRejection(VSOUT geo)
{
    if (EarlyRejectionTest(geo))
        discard;

	GBufferValues sample = PerPixel(geo);
	return ResolveDepthPlus(geo, sample);
}

float4 flatColorWithEarlyRejection(VSOUT geo) : SV_Target0
{
    if (EarlyRejectionTest(geo))
        discard;
	return VSOUT_GetColor0(geo);
}

float4 copyDiffuseAlbedo(VSOUT geo) : SV_Target0
{
	GBufferValues sample = PerPixel(geo);
	return float4(sample.diffuseAlbedo, sample.blendingAlpha);
}

float4 copyDiffuseAlbedoWithEarlyRejection(VSOUT geo) : SV_Target0
{
    if (EarlyRejectionTest(geo))
        discard;
	GBufferValues sample = PerPixel(geo);
	return float4(sample.diffuseAlbedo, sample.blendingAlpha);
}

