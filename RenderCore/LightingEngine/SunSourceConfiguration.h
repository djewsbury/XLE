// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ILightScene.h"
#include "../../Math/ProjectionMath.h"

namespace RenderCore { namespace Techniques { class ProjectionDesc; }}

namespace RenderCore { namespace LightingEngine
{
    class ShadowOperatorDesc;

    class SunSourceFrustumSettings
    {
    public:
        struct Flags 
        {
            enum Enum 
            { 
                HighPrecisionDepths = 1<<0, 
                ArbitraryCascades = 1<<1,
                RayTraced = 1<<2,
                CullFrontFaces = 1<<3       //< When set, cull front faces and leave back faces; when not set, cull back faces and leave front faces
            };
            using BitField = unsigned;
        };
        unsigned        _maxFrustumCount;
        float           _maxDistanceFromCamera;
        float           _frustumSizeFactor;
        float           _focusDistance;
        Flags::BitField _flags;
        unsigned        _textureSize;

        /*float           _slopeScaledBias;
        float           _depthBiasClamp;
        unsigned        _rasterDepthBias;

        float           _dsSlopeScaledBias;
        float           _dsDepthBiasClamp;
        unsigned        _dsRasterDepthBias;*/

        // float           _worldSpaceResolveBias;
        float           _tanBlurAngle;
        float           _minBlurSearch, _maxBlurSearch;

        SunSourceFrustumSettings();
    };

    /// <summary>Calculate a default set of shadow cascades for the sun<summary>
    /// Frequently we render the shadows from the sun using a number of "cascades."
    /// This function will generate reasonable set of cascades given the input parameters
    /// <param name="mainSceneCameraDesc">This is the projection desc used when rendering the 
    /// the main scene from this camera (it's the project desc for the shadows render). This
    /// is required for adapting the shadows projection to the main scene camera.</param>
    ILightScene::ShadowProjectionId CreateShadowCascades(
        ILightScene& lightScene,
        ILightScene::ShadowOperatorId shadowOperatorId,
        ILightScene::LightSourceId associatedLightId,
        const Techniques::ProjectionDesc& mainSceneCameraDesc,
        const SunSourceFrustumSettings& settings);

	ShadowOperatorDesc CalculateShadowOperatorDesc(const SunSourceFrustumSettings& settings);

    namespace Internal
    {
        std::vector<IOrthoShadowProjections::OrthoSubProjection> TestResolutionNormalizedOrthogonalShadowProjections(
            const Float3& negativeLightDirection,
            const RenderCore::Techniques::ProjectionDesc& mainSceneProjectionDesc,
            const SunSourceFrustumSettings& settings,
            ClipSpaceType clipSpaceType);
    }

}}
