// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(DEFAULT_MATERIAL_H)
#define DEFAULT_MATERIAL_H

#include "../TechniqueLibrary/Framework/Binding.hlsl"

    //  This cbuffer contains basic constants are used frequently enough that
    //  we can add support for them in most shaders.
cbuffer BasicMaterialConstants BIND_MAT_B0
{
	float3  MaterialDiffuse;
    float   Opacity;
    float3  MaterialSpecular;
    float   AlphaThreshold;

	float   RoughnessMin;
	float   RoughnessMax;
	float   SpecularMin;
	float   SpecularMax;
	float   MetalMin;
	float   MetalMax;
}

#endif
