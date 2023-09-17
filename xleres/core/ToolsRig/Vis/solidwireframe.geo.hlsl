// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)


struct VStoGS
{
	float4 position : SV_Position;
	#if SOLIDWIREFRAME_TEXCOORD==1
		float2 texCoord : TEXCOORD0;
		float2 dhdxy : DHDXY;	// hack for terrain
	#endif
	#if SOLIDWIREFRAME_WORLDPOSITION==1
		float3 worldPosition : WORLDPOSITION;
	#endif
};

struct GStoPS
{
	float4 position				 : SV_Position;
	float3 barycentricCoords	 : BARYCENTRIC;
	#if SOLIDWIREFRAME_TEXCOORD==1
		float2 texCoord : TEXCOORD0;
		float2 dhdxy : DHDXY;	// hack for terrain
	#endif
	#if SOLIDWIREFRAME_WORLDPOSITION==1
		float3 worldPosition : WORLDPOSITION;
	#endif
};

[maxvertexcount(3)]
	void main( triangle VStoGS input[3], inout TriangleStream<GStoPS> OutStream )
{
	GStoPS vert;
	vert.position			 = input[0].position;
	vert.barycentricCoords	 = float3( 1.f, 0.f, 0.f );
	#if SOLIDWIREFRAME_TEXCOORD==1
		vert.texCoord = input[0].texCoord;
		vert.dhdxy = input[0].dhdxy;
	#endif
	#if SOLIDWIREFRAME_WORLDPOSITION==1
		vert.worldPosition = input[0].worldPosition;
	#endif
	OutStream.Append( vert );

	vert.position			 = input[1].position;
	vert.barycentricCoords	 = float3( 0.f, 1.f, 0.f );
	#if SOLIDWIREFRAME_TEXCOORD==1
		vert.texCoord = input[1].texCoord;
		vert.dhdxy = input[1].dhdxy;
	#endif
	#if SOLIDWIREFRAME_WORLDPOSITION==1
		vert.worldPosition = input[1].worldPosition;
	#endif
	OutStream.Append( vert );

	vert.position			 = input[2].position;
	vert.barycentricCoords	 = float3( 0.f, 0.f, 1.f );
	#if SOLIDWIREFRAME_TEXCOORD==1
		vert.texCoord = input[2].texCoord;
		vert.dhdxy = input[2].dhdxy;
	#endif
	#if SOLIDWIREFRAME_WORLDPOSITION==1
		vert.worldPosition = input[2].worldPosition;
	#endif
	OutStream.Append( vert );
	OutStream.RestartStrip();
}




