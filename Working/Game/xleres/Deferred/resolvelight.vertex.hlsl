// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../TechniqueLibrary/Basic/basic2D.vertex.hlsl"
#include "../TechniqueLibrary/Math/MathConstants.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightDesc.hlsl"

cbuffer IndividualLightBuffer BIND_NUMERIC_B3
{
	LightDesc IndividualLight;
}

void main(float3 iPosition : POSITION, out float4 oPosition : SV_Position, out float2 oTexCoord : TEXCOORD0, out ViewFrustumInterpolator vfi)
{
    float3 worldSpacePosition = iPosition;

    // Depending on the light shape, transform the input position into "worldSpace"
    #if LIGHT_SHAPE == 1            // Sphere
        // Orienting towards the camera is unessential, but may help with some consistency around the edges
        float3 cameraRight = float3(CameraBasis[0].x, CameraBasis[1].x, CameraBasis[2].x);
        float3 cameraUp = float3(CameraBasis[0].y, CameraBasis[1].y, CameraBasis[2].y);
        float3 cameraForward = -float3(CameraBasis[0].z, CameraBasis[1].z, CameraBasis[2].z);
        float3 lightToView = IndividualLight.Position - WorldSpaceView;
        worldSpacePosition 
            = IndividualLight.Position 
            - IndividualLight.CutoffRange * cameraUp * iPosition.x
            + IndividualLight.CutoffRange * cameraRight * iPosition.y
            - IndividualLight.CutoffRange * cameraForward * iPosition.z
            ;
    #else
    #endif

	oPosition = mul(SysUniform_GetWorldToClip(), float4(worldSpacePosition, 1));

	// Any geometry that goes behind the near clip needs to be projected onto the near clip plane
	// Unfortunately we can't do this in the vertex shader directly. We have to clip on a
	// triangle basis... so it has to go into a geometry shader

	float2 positionProjected = oPosition.xy / oPosition.w;
	
	#if 1 // NDC == NDC_POSITIVE_RIGHT_HANDED
		oTexCoord.y = (positionProjected.y + 1.0f) / 2.0f;
	#else
		oTexCoord.y = (1.0f - positionProjected.y) / 2.0f;
	#endif
	oTexCoord.x = (positionProjected.x + 1.0f) / 2.0f;

	// We can use bilinear interpolation here to find the correct view frustum vector
	// Note that this require we use "noperspective" interpolation between vertices for this attribute, though
	float w0 = (1.0f - oTexCoord.x) * (1.0f - oTexCoord.y);
	float w1 = (1.0f - oTexCoord.x) * oTexCoord.y;
	float w2 = oTexCoord.x * (1.0f - oTexCoord.y);
	float w3 = oTexCoord.x * oTexCoord.y;

	vfi.oViewFrustumVector = 
		  w0 * SysUniform_GetFrustumCorners(0).xyz
		+ w1 * SysUniform_GetFrustumCorners(1).xyz
		+ w2 * SysUniform_GetFrustumCorners(2).xyz
		+ w3 * SysUniform_GetFrustumCorners(3).xyz
		;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct TileableLightDesc
{
	float3 Position;
	float CutoffRange;
	float LinearizedDepthMin, LinearizedDepthMax;
};
StructuredBuffer<TileableLightDesc> CombinedLightBuffer : register (t1, space1);

void PrepareMany(
	float3 iPosition : POSITION,
	uint instanceIdx : SV_InstanceID,
	out float4 oPosition : SV_Position,
	out nointerpolation uint objectIdx : OBJECT_INDEX)
{
    float3 worldSpacePosition = iPosition;

    // Depending on the light shape, transform the input position into "worldSpace"
    {// Sphere
		TileableLightDesc lightDesc = CombinedLightBuffer[instanceIdx];
        // Orienting towards the camera is unessential, but may help with some consistency around the edges
        float3 cameraRight = float3(CameraBasis[0].x, CameraBasis[1].x, CameraBasis[2].x);
        float3 cameraUp = float3(CameraBasis[0].y, CameraBasis[1].y, CameraBasis[2].y);
        float3 cameraForward = -float3(CameraBasis[0].z, CameraBasis[1].z, CameraBasis[2].z);
        float3 lightToView = lightDesc.Position - WorldSpaceView;
		float range = lightDesc.CutoffRange;
        worldSpacePosition 
            = lightDesc.Position 
            - range * cameraUp * iPosition.x
            + range * cameraRight * iPosition.y
            - range * cameraForward * iPosition.z
            ;
    }

	oPosition = mul(SysUniform_GetWorldToClip(), float4(worldSpacePosition, 1));
	objectIdx = instanceIdx;
}
