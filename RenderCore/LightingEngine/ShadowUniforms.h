// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ShadowPreparer.h"
#include "../Techniques/TechniqueUtils.h"
#include "../../Math/Matrix.h"
#include "../../Math/Vector.h"
#include "../../Math/Transformations.h"
#include "../../Math/ProjectionMath.h"
#include <memory>

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	template<int MaxProjections> class MultiProjection;
	constexpr unsigned MaxShadowTexturesPerLight = 6;

	class CB_ShadowResolveParameters
	{
	public:
		float       _worldSpaceResolveBias;
		float       _tanBlurAngle;
		float       _minBlurSearchNorm, _maxBlurSearchNorm;
		float       _shadowTextureSize;
		float 		_casterDistanceExtraBias;
		unsigned    _dummy[2];
		CB_ShadowResolveParameters();
	};

	SharedPkt BuildScreenToShadowProjection(
		ShadowProjectionMode mode,
		unsigned normalProjCount,
		const SharedPkt& mainUniforms,
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

		ShadowProjectionMode	_mode;
		SharedPkt				_cbSource;

		Float4x4 _multiViewWorldToClip[MaxShadowTexturesPerLight+1];

		// maxBlurRadiusNorm is used to leave room for the blur during cascade transitions
		// it should be max blur in pix / texture size in pix
		void InitialiseConstants(
			const MultiProjection<MaxShadowTexturesPerLight>&,
			unsigned operatorMaxFrustumCount,
			float maxBlurRadiusNorm);

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

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	/// <summary>Represents a set of shared projections</summary>
	/// This class is intended to be used with cascaded shadows (and similiar
	/// cascaded effects). Multiple cascades require multiple projections, and
	/// this class represents a small bundle of cascades.
	///
	/// Sometimes we want to put restrictions on the cascades in order to 
	/// reduce shader calculations. For example, a collection of orthogonal
	/// cascades can be defined by a set of axially aligned volumes in a shared
	/// orthogonal projection space.
	///
	template<int MaxProjections> class MultiProjection
	{
	public:
		class FullSubProjection
		{
		public:
			Float4x4    _worldToProjTransform;
		};

		class OrthoSubProjection
		{
		public:
			Float3      _leftTopFront;
			Float3      _rightBottomBack;
		};

		ShadowProjectionMode	_mode = ShadowProjectionMode::Arbitrary;
		unsigned                _normalProjCount = 0;
		unsigned				_operatorNormalProjCount = 0;
		bool                    _useNearProj = false;

		unsigned Count() const { return _normalProjCount + (_useNearProj?1:0); }

			/// @{
		///////////////////////////////////////////////////////////////////////
			/// When in "Full" mode, each sub projection gets a full view and 
			/// projection matrix. This means that every sub projection can 
			/// have a completely independently defined projection.
		FullSubProjection   _fullProj[MaxProjections];
		///////////////////////////////////////////////////////////////////////
			/// @}

			/// @{
		///////////////////////////////////////////////////////////////////////
			/// When a "OrthoSub" mode, the sub projections have some restrictions
			/// There is a single "definition transform" that defines a basic
			/// projection that all sub projects inherit. The sub projects then
			/// define and axially aligned area of XYZ space inside of the 
			/// definition transform. When used with an orthogonal transform, this
			/// allows each sub projection to wrap a volume of space. But all sub
			/// projections must match the rotation and skew of other projections.
		OrthoSubProjection  _orthoSub[MaxProjections];
		Float4x4            _definitionViewMatrix;
		///////////////////////////////////////////////////////////////////////
			/// @}

			/// @{
			/// In both modes, we often need to store the "minimal projection"
			/// This is the 4 most important elements of the projection matrix.
			/// In typical projection matrices, the remaining parts can be implied
			/// which means that these 4 elements is enough to do reverse projection
			/// work in the shader.
			/// In the case of shadows, mostly we need to convert depth values from
			/// projection space into view space (and since view space typically 
			/// has the same scale as world space, we can assume that view space 
			/// depth values are in natural world space units).
		Float4      _minimalProjection[MaxProjections];
			/// @}

		Float4x4    _specialNearProjection;
		Float4      _specialNearMinimalProjection;
	};

	void CalculateProjections(
		IteratorRange<Techniques::ProjectionDesc*> dst,
		const MultiProjection<MaxShadowTexturesPerLight>& projections);
}}}
