// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Framework/SystemUniforms.hlsl"
#include "../../Framework/VSIN.hlsl"

#if SPAWNED_INSTANCE==1
    struct InstanceDef
    {
        float4 posAndShadowing;

        #if GEO_INSTANCE_ALIGN_UP==1
            row_major float3x3 rotationMatrix;
        #else
            float2 sinCosTheta;
        #endif
    };

    StructuredBuffer<InstanceDef> InstanceOffsets : register(t15);

    float3 InstanceWorldPosition(VSIN input, out float3 worldNormal, out float3 objectCentreWorld)
    {
        float3 localPosition = VSIN_GetLocalPosition(input);

            // Ideally the object we're instantiating should have an
            // identity local-to-world. But if it doesn't we need to transform
            // the position here.
        localPosition = mul(SysUniform_GetLocalToWorld(), float4(localPosition,1)).xyz;

        objectCentreWorld = InstanceOffsets[input.instanceId].posAndShadowing.xyz;
        float3 localNormal = VSIN_GetLocalNormal(input);

        #if GEO_INSTANCE_ALIGN_UP==1
            float3x3 rotMat = InstanceOffsets[input.instanceId].rotationMatrix;
            float scale = .75f + .25f * rotMat[0].x;
            worldNormal = mul(rotMat, localNormal.xyz);
            return objectCentreWorld + scale * mul(rotMat, localPosition.xyz);
        #else
            float2 sc = InstanceOffsets[input.instanceId].sinCosTheta;
            float2x2 rotMat = float2x2(float2(sc.y, -sc.x), float2(sc.x, sc.y));
            float scale = .75f + .25f * sc.x;       // add cheap scale component (directly related to rotation)

            worldNormal = float3(mul(rotMat, localNormal.xy), localNormal.z);
            return float3(
                objectCentreWorld.xy + scale * mul(rotMat, localPosition.xy),
                objectCentreWorld.z +  scale * localPosition.z);
        #endif
    }

    float GetInstanceShadowing(VSIN input)
    {
        return InstanceOffsets[input.instanceId].posAndShadowing.w;
    }

#endif
