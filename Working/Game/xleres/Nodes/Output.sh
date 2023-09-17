// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(NODES_OUTPUT_H)
#define NODES_OUTPUT_H

#include "../Objects/MaterialParam.hlsl"
#include "../TechniqueLibrary/Framework/gbuffer.hlsl"

GBufferValues Output_PerPixel(
  float3 diffuseAlbedo,
  float3 worldSpaceNormal,

  CommonMaterialParam material,

  float blendingAlpha,
  float normalMapAccuracy,
  float cookedAmbientOcclusion,
  float cookedLightOcclusion,

  float3 transmission)
{
  GBufferValues values = GBufferValues_Default();
  values.diffuseAlbedo = diffuseAlbedo;
  values.worldSpaceNormal = worldSpaceNormal;
  values.material.roughness = material.roughness;
  values.material.specular = material.specular;
  values.material.metal = material.metal;
  values.blendingAlpha = blendingAlpha;
  values.normalMapAccuracy = normalMapAccuracy;
  values.cookedAmbientOcclusion = cookedAmbientOcclusion;
  values.cookedLightOcclusion = cookedLightOcclusion;
  values.transmission = transmission;
  return values;
}

float4 Output_ParamTex(CommonMaterialParam param)
{
    return float4(param.roughness, param.specular, param.metal, 1.f);
}

#endif
