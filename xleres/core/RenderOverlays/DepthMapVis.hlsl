// CompoundDocument:1

#include "Interfaces.hlsl"
#include "../TechniqueLibrary/Framework/CommonResources.hlsl"

Texture2DArray<float> CubeMap;

float4 cubeMapVis(DebuggingShapesCoords coords : DebuggingShapesCoords) : SV_Target0
{
	// 	   Z+
	// X+  Y+  X-  Y-
	// 	   Z-

	// Note that the order must agree with shader cubemap lookup
	// (see "Cube Map Face Selection" in Vulkan spec)
	float2 panelmins[6] {
		float2(0/4., 1/3.),			// X+
		float2(2/4., 1/3.),			// X-
		float2(1/4., 1/3.),			// Y+
		float2(3/4., 1/3.),			// Y-
		float2(1/4., 0/3.),			// Z+
		float2(1/4., 2/3.)			// Z-
	};
	float2 panelmaxs[6] {
		float2(1/4., 2/3.),			// X+
		float2(3/4., 2/3.),			// X-
		float2(2/4., 2/3.),			// Y+
		float2(4/4., 2/3.),			// Y-
		float2(2/4., 1/3.),			// Z+
		float2(2/4., 3/3.)			// Z-
	};

	for (uint c=0; c<6; ++c) {
		if (	coords.x >= panelmins[c].x && coords.x < panelmaxs[c].x
			&& 	coords.y >= panelmins[c].y && coords.y < panelmaxs[c].y) {

			float2 panelCoords = float2(
				(coords.x-panelmins[c].x)/(panelmaxs[c].x-panelmins[c].x),
				(coords.y-panelmins[c].y)/(panelmaxs[c].y-panelmins[c].y));

			float value = CubeMap.SampleLevel(PointClampSampler, float3(panelCoords, c), 0);
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

ShaderPatchCollection = node =~
	~
		<.>::cubeMapVis
		Implements=SV_AutoPS

)--*/

