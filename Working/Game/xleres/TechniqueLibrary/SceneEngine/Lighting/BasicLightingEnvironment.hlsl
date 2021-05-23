// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(BASIC_LIGHTING_ENVIRONMENT_H)
#define BASIC_LIGHTING_ENVIRONMENT_H

#include "../../LightingEngine/LightDesc.hlsl"
#include "../../Framework/Binding.hlsl"

#if !defined(BASIC_LIGHT_COUNT)
    #define BASIC_LIGHT_COUNT 1
#endif

// In pure forward shading models, we resolve all of the lights
// in one go (as opposed to other models, where a single light is
// resolved at a time). This cbuffer holds the settings for all
// of the lights in the scene.
cbuffer BasicLightingEnvironment BIND_SEQ_B3
{
    AmbientDesc BasicAmbient;
    RangeFogDesc BasicRangeFog;
    VolumeFogDesc BasicVolumeFog;
    LightDesc BasicLight[BASIC_LIGHT_COUNT];
}

#endif
