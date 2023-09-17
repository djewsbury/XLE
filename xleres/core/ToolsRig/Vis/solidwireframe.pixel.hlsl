// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

struct GStoPS
{
	float4 position				 : SV_Position;
	float3 barycentricCoords	 : BARYCENTRIC;
	#if SOLIDWIREFRAME_TEXCOORD==1
		float2 texCoord : TEXCOORD0;
	#endif
};

float edgeFactor( float3 barycentricCoords, float width )
{
    float3 d = fwidth(barycentricCoords);
    float3 a3 = smoothstep(0.0.xxx, d*width, barycentricCoords);
    return min(min(a3.x, a3.y), a3.z);
}


float edgeFactor2( float2 coords, float width )
{
    float2 d = fwidth(coords);
    float2 a3 = smoothstep(0.0.xx, d*width, coords);
    return min(a3.x, a3.y);
}

float4 main(GStoPS input) : SV_Target0
{
	float E = edgeFactor( input.barycentricCoords, 1.5f );
	return float4( E.xxx, 1.f );
}

float4 blend(GStoPS input) : SV_Target0
{
	float E = edgeFactor( input.barycentricCoords, 1.5f );
	return float4( E.xxx, 1-E );
}

float4 marker(GStoPS input) : SV_Target0
{
	float3 b = input.barycentricCoords;
	float3 d = fwidth(b);
	float3 A = b / d;
	float E = min(min(A.x, A.y), A.z);

	E = min(min(input.barycentricCoords.x, input.barycentricCoords.y), input.barycentricCoords.z);
	float a = saturate(1-E*3);
	float3 col = 8.f * (exp(a)-1).xxx * lerp(float3(0.3,0.3,1), float3(0.3,1,.5), a);

	b = abs(b - 0.5.xxx);
	bool hatchA = frac(min(min(b.x, b.y), b.z) * 30.f) > 0.33f;
	if (hatchA) {
		col += (32.f * (1.f - edgeFactor( input.barycentricCoords, 3.f ))).xxx;
	}

	return float4(col, 1.f);
}

float4 outlinepatch(GStoPS input) : SV_Target0
{
	#if SOLIDWIREFRAME_TEXCOORD==1
		float E = edgeFactor( input.barycentricCoords, 1.5f );
		float patchEdge = 1.0f - edgeFactor2(frac(input.texCoord), 5.f).xxx;
		float3 result = lerp( E.xxx, float3(0,1,0), patchEdge );
		return float4( result, 1.f );
	#else
		return 1.0.xxxx;
	#endif
}
