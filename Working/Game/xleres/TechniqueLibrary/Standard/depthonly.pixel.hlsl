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
DepthMotionNormalEncoded depthMotionNormal(VSOUT geo)
{
	GBufferValues sample = PerPixel(geo);

	float3 prevPos;
	#if (VSOUT_HAS_PREV_POSITION==1)
		prevPos = geo.prevPosition.xyz / geo.prevPosition.w;
		prevPos.x = prevPos.x * 0.5 + 0.5;
		prevPos.y = prevPos.y * 0.5 + 0.5;
		prevPos.xy = SysUniform_GetViewportMinXY() + prevPos * SysUniform_GetViewportWidthHeight();
		prevPos.xyz -= geo.position.xyz;
		prevPos.xy = clamp(round(prevPos.xy), -127, 127);
	#else
		prevPos = 0.0.xxx;
	#endif
	return EncodeDepthMotionNormal(sample, int2(prevPos.xy));
}

DepthMotionNormalEncoded depthMotionNormalWithEarlyRejection(VSOUT geo)
{
    if (EarlyRejectionTest(geo))
        discard;

	GBufferValues sample = PerPixel(geo);

	float3 prevPos;
	#if (VSOUT_HAS_PREV_POSITION==1)
		prevPos = geo.prevPosition.xyz / geo.prevPosition.w;
		prevPos.x = prevPos.x * 0.5 + 0.5;
		prevPos.y = prevPos.y * 0.5 + 0.5;
		prevPos.xy = SysUniform_GetViewportMinXY() + prevPos * SysUniform_GetViewportWidthHeight();
		prevPos.xyz -= geo.position.xyz;
		prevPos.xy = clamp(round(prevPos.xy), -127, 127);
	#else
		prevPos = 0.0.xxx;
	#endif
	return EncodeDepthMotionNormal(sample, int2(prevPos.xy));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

[earlydepthstencil]
DepthMotionEncoded depthMotion(VSOUT geo)
{
	float3 prevPos;
	#if (VSOUT_HAS_PREV_POSITION==1)
		prevPos = geo.prevPosition.xyz / geo.prevPosition.w;
		prevPos.x = prevPos.x * 0.5 + 0.5;
		prevPos.y = prevPos.y * 0.5 + 0.5;
		prevPos.xy = SysUniform_GetViewportMinXY() + prevPos * SysUniform_GetViewportWidthHeight();
		prevPos.xyz -= geo.position.xyz;
		prevPos.xy = clamp(round(prevPos.xy), -127, 127);
	#else
		prevPos = 0.0.xxx;
	#endif
	return EncodeDepthMotion(int2(prevPos.xy));
}

DepthMotionEncoded depthMotionWithEarlyRejection(VSOUT geo)
{
    if (EarlyRejectionTest(geo))
        discard;

	GBufferValues sample = PerPixel(geo);

	float3 prevPos;
	#if (VSOUT_HAS_PREV_POSITION==1)
		prevPos = geo.prevPosition.xyz / geo.prevPosition.w;
		prevPos.x = prevPos.x * 0.5 + 0.5;
		prevPos.y = prevPos.y * 0.5 + 0.5;
		prevPos.xy = SysUniform_GetViewportMinXY() + prevPos * SysUniform_GetViewportWidthHeight();
		prevPos.xyz -= geo.position.xyz;
		prevPos.xy = clamp(round(prevPos.xy), -127, 127);
	#else
		prevPos = 0.0.xxx;
	#endif
	return EncodeDepthMotion(int2(prevPos.xy));
}

