// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../TechniqueLibrary/Basic/basic2D.vertex.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightDesc.hlsl"

cbuffer LightBuffer BIND_SEQ_B1
{
	LightDesc Light;
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
        float3 lightToView = Light.Position - WorldSpaceView;
        worldSpacePosition 
            = Light.Position 
            - Light.CutoffRange * cameraUp * iPosition.x
            + Light.CutoffRange * cameraRight * iPosition.y
            - Light.CutoffRange * cameraForward * iPosition.z
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
