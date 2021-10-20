#include "../Framework/VSOUT.hlsl"
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

	float3 prevPos;
	#if VSOUT_HAS_PREV_POSITION
		prevPos = geo.prevPosition.xyz / geo.prevPosition.w;
		prevPos.x = prevPos.x * 0.5 + 0.5;
		prevPos.y = prevPos.y * 0.5 + 0.5;
		prevPos.xy = SysUniform_GetViewportMinXY() + prevPos.xy * SysUniform_GetViewportWidthHeight();
		prevPos.xyz -= geo.position.xyz;
		prevPos.xy = clamp(round(prevPos.xy), -127, 127);
	#else
		prevPos = 0.0.xxx;
	#endif
	return EncodeDepthPlus(sample, int2(prevPos.xy));
}

DepthPlusEncoded depthPlusWithEarlyRejection(VSOUT geo)
{
    if (EarlyRejectionTest(geo))
        discard;

	GBufferValues sample = PerPixel(geo);

	float3 prevPos;
	#if VSOUT_HAS_PREV_POSITION
		prevPos = geo.prevPosition.xyz / geo.prevPosition.w;
		prevPos.x = prevPos.x * 0.5 + 0.5;
		prevPos.y = prevPos.y * 0.5 + 0.5;
		prevPos.xy = SysUniform_GetViewportMinXY() + prevPos * SysUniform_GetViewportWidthHeight();
		prevPos.xyz -= geo.position.xyz;
		prevPos.xy = clamp(round(prevPos.xy), -127, 127);
	#else
		prevPos = 0.0.xxx;
	#endif
	return EncodeDepthPlus(sample, int2(prevPos.xy));
}

float4 flatColorEarlyRejection(VSOUT geo) : SV_Target0
{
    if (EarlyRejectionTest(geo))
        discard;
	return VSOUT_GetColor0(geo);
}
