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

    ILightScene::ShadowProjectionId CreateSunSourceShadows(
        ILightScene& lightScene,
        ILightScene::ShadowOperatorId shadowOperatorId,
        ILightScene::LightSourceId associatedLightId,
        const SunSourceFrustumSettings& settings);

    /// <summary>Calculate a default set of shadow cascades for the sun<summary>
    /// Frequently we render the shadows from the sun using a number of "cascades."
    /// This function will generate reasonable set of cascades given the input parameters
    /// <param name="mainSceneCameraDesc">This is the projection desc used when rendering the 
    /// the main scene from this camera (it's the project desc for the shadows render). This
    /// is required for adapting the shadows projection to the main scene camera.</param>
    void ConfigureShadowProjectionImmediately(
        ILightScene& lightScene,
        ILightScene::ShadowProjectionId shadowProjectionId,
        ILightScene::LightSourceId associatedLightId,
        const SunSourceFrustumSettings& settings,
        const Techniques::ProjectionDesc& mainSceneProjectionDesc);

	ShadowOperatorDesc CalculateShadowOperatorDesc(const SunSourceFrustumSettings& settings);

    class ISunSourceShadows
	{
	public:
		virtual SunSourceFrustumSettings GetSettings() const = 0;
        virtual void SetSettings(const SunSourceFrustumSettings&) = 0;
        virtual void FixMainSceneCamera(const Techniques::ProjectionDesc&) = 0;
        virtual void UnfixMainSceneCamera() = 0;
		virtual ~ISunSourceShadows() = default;
	};

    namespace Internal
    {
        std::pair<std::vector<IOrthoShadowProjections::OrthoSubProjection>, Float4x4> TestResolutionNormalizedOrthogonalShadowProjections(
            const Float3& negativeLightDirection,
            const Techniques::ProjectionDesc& mainSceneProjectionDesc,
            const SunSourceFrustumSettings& settings,
            ClipSpaceType clipSpaceType);
    }

}}
