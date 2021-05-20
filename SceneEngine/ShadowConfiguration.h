// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

namespace RenderCore { namespace LightingEngine { class ShadowProjectionDesc; class LightDesc; class ShadowOperatorDesc; }}
namespace RenderCore { namespace Techniques { class ProjectionDesc; }}

namespace SceneEngine
{
    class DefaultShadowFrustumSettings
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
            typedef unsigned BitField;
        };
        unsigned        _frustumCount;
        float           _maxDistanceFromCamera;
        float           _frustumSizeFactor;
        float           _focusDistance;
        Flags::BitField _flags;
        unsigned        _textureSize;

        float           _slopeScaledBias;
        float           _depthBiasClamp;
        unsigned        _rasterDepthBias;

        float           _dsSlopeScaledBias;
        float           _dsDepthBiasClamp;
        unsigned        _dsRasterDepthBias;

        float           _worldSpaceResolveBias;
        float           _tanBlurAngle;
        float           _minBlurSearch, _maxBlurSearch;

        DefaultShadowFrustumSettings();
    };

    /// <summary>Calculate a default set of shadow cascades for the sun<summary>
    /// Frequently we render the shadows from the sun using a number of "cascades."
    /// This function will generate reasonable set of cascades given the input parameters
    /// <param name="mainSceneCameraDesc">This is the projection desc used when rendering the 
    /// the main scene from this camera (it's the project desc for the shadows render). This
    /// is required for adapting the shadows projection to the main scene camera.</param>
    RenderCore::LightingEngine::ShadowProjectionDesc CalculateDefaultShadowCascades(
        const RenderCore::LightingEngine::LightDesc& lightDesc,
        unsigned lightId,
        const RenderCore::Techniques::ProjectionDesc& mainSceneCameraDesc,
        const DefaultShadowFrustumSettings& settings);

	RenderCore::LightingEngine::ShadowOperatorDesc
		CalculateShadowGeneratorDesc(
			const DefaultShadowFrustumSettings& settings);

}
