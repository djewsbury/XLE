// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

cbuffer Parameters : register(b0, space1)
{
	uint2					TileDimensions;
};
Texture2D<float>		DebuggingTextureMin : register(t1, space1);
Texture2D<float>		DebuggingTextureMax : register(t2, space1);

void main(	uint vertexIndex		: SV_VertexID,
			out float3 minCoords	: MINCOORDS, 
			out float3 maxCoords	: MAXCOORDS)
{
		//	Output 1 vertex per beam. The geometry shader
		//	will expand to full geometry

	uint primitiveIndex = vertexIndex;
	int2 tileCoords = int2(primitiveIndex%TileDimensions.x, primitiveIndex/TileDimensions.x);
	minCoords = float3(
		(tileCoords.x / float(TileDimensions.x)),
		(tileCoords.y / float(TileDimensions.y)),
		DebuggingTextureMin.Load(int3(tileCoords*int2(16,16),0)));

	maxCoords = float3(
		((tileCoords.x+1) / float(TileDimensions.x)),
		((tileCoords.y+1) / float(TileDimensions.y)),
		DebuggingTextureMax.Load(int3(tileCoords*int2(16,16),0)));
}
