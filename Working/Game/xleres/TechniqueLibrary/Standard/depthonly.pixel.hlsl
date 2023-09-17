#include "../Framework/VSOUT.hlsl"
#include "depth-plus-util.hlsl"
#include "utility-shader.hlsl"
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

[earlydepthstencil]
float4 utility(VSOUT geo) : SV_Target0
{
	GBufferValues sample = PerPixel(geo);
	return UtilityShaderValue(geo, sample);
}

float4 utilityWithEarlyRejection(VSOUT geo) : SV_Target0
{
    if (EarlyRejectionTest(geo))
        discard;
	GBufferValues sample = PerPixel(geo);
	return UtilityShaderValue(geo, sample);
}

