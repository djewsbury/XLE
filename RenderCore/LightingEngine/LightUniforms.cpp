// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightUniforms.h"
#include "StandardLightScene.h"
#include "StandardLightOperators.h"
#include "../../Math/Transformations.h"

namespace RenderCore { namespace LightingEngine { namespace Internal
{
#if 0
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
#endif

    unsigned AsUniformShapeCode(LightSourceShape shape)
    {
        return unsigned(shape);
    }

    CB_Light MakeLightUniforms(const StandardPositionalLight& light, unsigned shapeCode)
    {
        return CB_Light 
            {
                (shapeCode == 0) ? light._unitLengthPosition : light._position, light._cutoffRange,
                light._brightness, light._radii[0],
                ExtractRight(light._orientation), light._radii[1],
                ExtractForward(light._orientation), shapeCode, 
                ExtractUp(light._orientation), 0
            };
    }

    CB_Light MakeBlankLightDesc()
    {
        return CB_Light
            {   Float3(0.f, 0.f, 0.f), 0.f, 
                Float3(0.f, 0.f, 0.f), 0.f,
                Float3(0.f, 0.f, 0.f), 0.f,
                Float3(0.f, 0.f, 0.f), 0,
                Float3(0.f, 0.f, 0.f), 0 };
    }

    CB_VolumeFog MakeBlankVolumeFogDesc()
    {
        return CB_VolumeFog
            {   0.f, 0.f, 0.f, 0,
                Float3(0.f, 0.f, 0.f), 0, 
                Float3(0.f, 0.f, 0.f), 0 };
    }
}}}
