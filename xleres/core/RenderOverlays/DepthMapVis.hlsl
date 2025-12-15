// CompoundDocument:1

#include "Interfaces.hlsl"
#include "../TechniqueLibrary/Framework/CommonResources.hlsl"

Texture2DArray<float> CubeMap;

float4 cubeMapVis(float2 shapeRel : TEXCOORD0) : SV_Target0
{
	// 	   Z+
	// X+  Y+  X-  Y-
	// 	   Z-

	// Note that the order must agree with shader cubemap lookup
	// (see "Cube Map Face Selection" in Vulkan spec)
	float2 panelmins[6] = {
		float2(0/4.0, 1/3.0),			// X+
		float2(2/4.0, 1/3.0),			// X-
		float2(1/4.0, 1/3.0),			// Y+
		float2(3/4.0, 1/3.0),			// Y-
		float2(1/4.0, 0/3.0),			// Z+
		float2(1/4.0, 2/3.0)			// Z-
	};
	float2 panelmaxs[6] = {
		float2(1/4.0, 2/3.0),			// X+
		float2(3/4.0, 2/3.0),			// X-
		float2(2/4.0, 2/3.0),			// Y+
		float2(4/4.0, 2/3.0),			// Y-
		float2(2/4.0, 1/3.0),			// Z+
		float2(2/4.0, 3/3.0)			// Z-
	};

	for (uint c=0; c<6; ++c) {
		if (	shapeRel.x >= panelmins[c].x && shapeRel.x < panelmaxs[c].x
			&& 	shapeRel.y >= panelmins[c].y && shapeRel.y < panelmaxs[c].y) {

			float2 panelCoords = float2(
				(shapeRel.x-panelmins[c].x)/(panelmaxs[c].x-panelmins[c].x),
				(shapeRel.y-panelmins[c].y)/(panelmaxs[c].y-panelmins[c].y));

			float value = CubeMap.SampleLevel(PointClampSampler, float3(panelCoords, c), 0);
			value = 1-value;
			value = pow(value, 5);
			value = 1-value;
			return float4(value.xxx, 1);
		}
	}
	return 0;
}

/* <<Chunk:StructuredDocument:main>>--(

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Entity = cubeMapVis
RawMaterial = cubeMapVis =~
	States=~
		ForwardBlend=~
			Src=srcalpha; Dst=invsrcalpha; Op=add

ShaderPatchCollection = cubeMapVis =~
	~
		<.>::cubeMapVis
		Implements=SV_AutoPS

)--*/

