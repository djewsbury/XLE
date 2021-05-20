// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ShadowPreparer.h"
#include "LightScene_Internal.h"
#include "../../Math/Matrix.h"
#include "../../Math/Vector.h"
#include <memory>

namespace RenderCore { namespace Techniques { class IShaderResourceDelegate; }}

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	template<int MaxProjections> class MultiProjection;

	class CB_ArbitraryShadowProjection
	{
	public:
		uint32_t    _projectionCount; 
		uint32_t    _dummy[3];
		Float4      _minimalProj[MaxShadowTexturesPerLight];
		Float4x4    _worldToProj[MaxShadowTexturesPerLight];
	};

	class CB_OrthoShadowProjection
	{
	public:
		Float3x4    _worldToProj;
		Float4      _minimalProjection;
		uint32_t    _projectionCount;
		uint32_t    _dummy[3];
		Float4      _cascadeScale[MaxShadowTexturesPerLight];
		Float4      _cascadeTrans[MaxShadowTexturesPerLight];
		Float3x4    _nearCascade;       // special projection for the area closest to the camera
		Float4      _nearMinimalProjection;
	};

	class CB_ShadowResolveParameters
	{
	public:
		float       _worldSpaceBias;
		float       _tanBlurAngle;
		float       _minBlurSearch, _maxBlurSearch;
		float       _shadowTextureSize;
		unsigned    _dummy[3];
		CB_ShadowResolveParameters();
	};

	struct CB_ScreenToShadowProjection
	{
		Float4x4    _cameraToShadow[6];
		Float4x4    _orthoCameraToShadow;
		Float2      _xyScale;
		Float2      _xyTrans;
		Float4x4    _orthoNearCameraToShadow;
	};

	CB_ScreenToShadowProjection BuildScreenToShadowProjection(
        unsigned frustumCount,
        const CB_ArbitraryShadowProjection& arbitraryCB,
        const CB_OrthoShadowProjection& orthoCB,
        const Float4x4& cameraToWorld,
        const Float4x4& cameraToProjection);

	/// <summary>Contains the result of a shadow prepare operation</summary>
	/// Typically shadows are prepared as one of the first steps of while rendering
	/// a frame. (though, I guess, the prepare step could happen at any time).
	/// We need to retain the shader constants and render target outputs
	/// from that preparation, to use later while resolving the lighting of the main
	/// scene.
	class PreparedShadowFrustum
	{
	public:
		unsigned    _frustumCount;
		bool        _enableNearCascade;

		ShadowProjectionMode			_mode;
		CB_ArbitraryShadowProjection    _arbitraryCBSource;
		CB_OrthoShadowProjection        _orthoCBSource;

		void InitialiseConstants(
			const MultiProjection<MaxShadowTexturesPerLight>&);

		PreparedShadowFrustum();
		PreparedShadowFrustum(PreparedShadowFrustum&& moveFrom) never_throws;
		PreparedShadowFrustum& operator=(PreparedShadowFrustum&& moveFrom) never_throws;
	};

	/// <summary>Prepared "Depth Map" shadow frustum</summary>
	class PreparedDMShadowFrustum : public PreparedShadowFrustum
	{
	public:
		CB_ShadowResolveParameters  _resolveParameters;

		bool IsReady() const;
	};

	// PreparedDMShadowFrustum SetupPreparedDMShadowFrustum(const ShadowProjectionDesc& frustum);

}}}

