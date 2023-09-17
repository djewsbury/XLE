// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(NODES_MATERIAL_PARAM_H)
#define NODES_MATERIAL_PARAM_H

struct CommonMaterialParam
{
    float   roughness;
    float   specular;
    float   metal;
};

CommonMaterialParam CommonMaterialParam_Default()
{
    CommonMaterialParam result;
    result.roughness = 0.33f;
    result.specular = 0.07f;
    result.metal = 0.f;
    return result;
}

CommonMaterialParam CommonMaterialParam_Make(float roughness, float specular, float metal)
{
    CommonMaterialParam result = CommonMaterialParam_Default();
    result.roughness = roughness;
    result.specular = specular;
    result.metal = metal;
    return result;
}

CommonMaterialParam ScaleByRange(
    CommonMaterialParam input,
    float2 rRange, float2 sRange, float2 mRange)
{
    CommonMaterialParam result;
    result.roughness = lerp(rRange.x, rRange.y, input.roughness);
    result.specular = lerp(sRange.x, sRange.y, input.specular);
    result.metal = lerp(mRange.x, mRange.y, input.metal);
    return result;
}

CommonMaterialParam Scale(CommonMaterialParam input, float factor)
{
    CommonMaterialParam result = input;
    result.roughness *= factor;
    result.specular *= factor;
    result.metal *= factor;
    return result;
}

CommonMaterialParam Add(CommonMaterialParam lhs, CommonMaterialParam rhs)
{
    CommonMaterialParam result;
    result.roughness = lhs.roughness + rhs.roughness;
    result.specular = lhs.specular + rhs.specular;
    result.metal = lhs.metal + rhs.metal;
    return result;
}

#endif
