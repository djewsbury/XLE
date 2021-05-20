// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightUniforms.h"
#include "StandardLightScene.h"
#include "../../Math/Transformations.h"

namespace RenderCore { namespace LightingEngine { namespace Internal
{
    CB_Ambient MakeAmbientUniforms(const EnvironmentalLightingDesc& desc)
    {
        return CB_Ambient 
            { 
                desc._ambientLight, desc._skyReflectionScale, desc._skyReflectionBlurriness, {0,0,0},
            };
    }

    CB_RangeFog MakeRangeFogUniforms(const EnvironmentalLightingDesc& desc)
    {
        if (desc._doRangeFog)
            return CB_RangeFog { desc._rangeFogInscatter, desc._rangeFogThickness };
        return CB_RangeFog { Float3(0.f, 0.f, 0.f), 0 };
    }

    CB_Light MakeLightUniforms(const StandardLightDesc& light)
    {
        return CB_Light 
            {
                light._position, light._cutoffRange, 
                light._diffuseColor, light._radii[0],
                light._specularColor, light._radii[1],
                ExtractRight(light._orientation), light._diffuseWideningMin, 
                ExtractForward(light._orientation), light._diffuseWideningMax, 
                ExtractUp(light._orientation), 0
            };
    }

    CB_Light MakeBlankLightDesc()
    {
        return CB_Light
            {   Float3(0.f, 0.f, 0.f), 0.f, 
                Float3(0.f, 0.f, 0.f), 0.f,
                Float3(0.f, 0.f, 0.f), 0.f,
                Float3(0.f, 0.f, 0.f), 0.f,
                Float3(0.f, 0.f, 0.f), 0.f,
                Float3(0.f, 0.f, 0.f), 0 };
    }

    CB_VolumeFog MakeBlankVolumeFogDesc()
    {
        return CB_VolumeFog
            {   0.f, 0.f, 0.f, 0,
                Float3(0.f, 0.f, 0.f), 0, 
                Float3(0.f, 0.f, 0.f), 0 };
    }

    CB_BasicEnvironment MakeBasicEnvironmentUniforms(const EnvironmentalLightingDesc& env)
    {
        CB_BasicEnvironment result;
        result._ambient = MakeAmbientUniforms(env);
        result._rangeFog = MakeRangeFogUniforms(env);
        result._volumeFog = MakeBlankVolumeFogDesc();
        return result;
    }
}}}
