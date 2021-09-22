// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ShadowPreparer.h"
#include "StandardLightScene.h"			// for ILightBase
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
		float       _worldSpaceBias;
		float       _tanBlurAngle;
		float       _minBlurSearchNorm, _maxBlurSearchNorm;
		float       _shadowTextureSize;
		float 		_casterLookupExtraBias;
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

		void InitialiseConstants(
			const MultiProjection<MaxShadowTexturesPerLight>&,
			unsigned operatorMaxFrustumCount);

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

	class IShadowProjectionDriver
	{
	public:
		virtual void UpdateProjections(
			const Techniques::ParsingContext&,
			IPositionalLightSource& lightSource,
			IOrthoShadowProjections& destination) = 0;
		virtual ~IShadowProjectionDriver() = default;
	};

	class IAttachDriver
	{
	public:
		virtual void AttachDriver(std::shared_ptr<Internal::ILightBase> driver) = 0;
		virtual ~IAttachDriver() = default;
	};

	/// <summary>Defines the projected shadows for a single light<summary>
	class StandardShadowProjection : public Internal::ILightBase, public IDepthTextureResolve, public IArbitraryShadowProjections, public IOrthoShadowProjections, public INearShadowProjection, public IAttachDriver
	{
	public:
		using Projections = MultiProjection<MaxShadowTexturesPerLight>;
		Projections     _projections;
		Float4x4        _worldToClip = Identity<Float4x4>();   ///< Intended for use in CPU-side culling. Objects culled by this transform will be culled from all projections

		float           _worldSpaceResolveBias = 0.f;
		float           _tanBlurAngle = 0.00436f;
		float           _minBlurSearchPixels = 0.5f, _maxBlurSearchPixels = 25.f;
		float			_casterLookupExtraBias = 0.001f;

		std::shared_ptr<ICompiledShadowPreparer> _preparer;
		std::shared_ptr<Internal::ILightBase> _driver;

		virtual void SetDesc(const Desc& newDesc) override
		{
			_worldSpaceResolveBias = newDesc._worldSpaceResolveBias;
			_tanBlurAngle = newDesc._tanBlurAngle;
			_minBlurSearchPixels = newDesc._minBlurSearch;
			_maxBlurSearchPixels = newDesc._maxBlurSearch;
			_casterLookupExtraBias = newDesc._casterLookupExtraBias;
		}
		virtual Desc GetDesc() const override
		{
			return Desc { _worldSpaceResolveBias, _tanBlurAngle, _minBlurSearchPixels, _maxBlurSearchPixels, _casterLookupExtraBias };
		}

		virtual void SetArbitrarySubProjections(
			IteratorRange<const Float4x4*> worldToCamera,
			IteratorRange<const Float4x4*> cameraToProjection) override
		{
			assert(_projections._mode == ShadowProjectionMode::Arbitrary || _projections._mode == ShadowProjectionMode::ArbitraryCubeMap);
			assert(worldToCamera.size() <= Internal::MaxShadowTexturesPerLight);
			assert(!worldToCamera.empty());
			assert(worldToCamera.size() == cameraToProjection.size());
			auto projCount = std::min((size_t)Internal::MaxShadowTexturesPerLight, worldToCamera.size());
            assert(projCount <= _projections._operatorNormalProjCount);     // a mis-match here means it does not agree with the operator
			for (unsigned c=0; c<projCount; ++c) {
				_projections._fullProj[c]._worldToProjTransform = Combine(worldToCamera[c], cameraToProjection[c]);
				_projections._minimalProjection[c] = ExtractMinimalProjection(cameraToProjection[c]);
			}
			_projections._normalProjCount = projCount;
		}

		virtual void SetWorldToOrthoView(const Float4x4& worldToCamera) override
		{
			assert(_projections._mode == ShadowProjectionMode::Ortho);
			assert(IsOrthonormal(Truncate3x3(worldToCamera)));
			_projections._definitionViewMatrix = worldToCamera;
		}

		virtual void SetOrthoSubProjections(IteratorRange<const OrthoSubProjection*> projections) override
		{
			assert(_projections._mode == ShadowProjectionMode::Ortho);
			assert(projections.size() < Internal::MaxShadowTexturesPerLight);
			assert(!projections.empty());
			auto projCount = std::min((size_t)Internal::MaxShadowTexturesPerLight, projections.size());
			assert(projCount <= _projections._operatorNormalProjCount);     // a mis-match here means it does not agree with the operator
            for (unsigned c=0; c<projCount; ++c) {
				_projections._orthoSub[c]._leftTopFront = projections[c]._leftTopFront;
				_projections._orthoSub[c]._rightBottomBack = projections[c]._rightBottomBack;

				auto projTransform = OrthogonalProjection(
					projections[c]._leftTopFront[0], projections[c]._leftTopFront[1], 
					projections[c]._rightBottomBack[0], projections[c]._rightBottomBack[1], 
					projections[c]._leftTopFront[2], projections[c]._rightBottomBack[2],
					GeometricCoordinateSpace::RightHanded, Techniques::GetDefaultClipSpaceType());
				_projections._fullProj[c]._worldToProjTransform = Combine(_projections._definitionViewMatrix, projTransform);
				_projections._minimalProjection[c] = ExtractMinimalProjection(projTransform);
			}
			_projections._normalProjCount = projCount;
		}

		virtual Float4x4 GetWorldToOrthoView() const override
		{
			assert(_projections._mode == ShadowProjectionMode::Ortho);
			return _projections._definitionViewMatrix;
		}

		virtual std::vector<OrthoSubProjection> GetOrthoSubProjections() const override
		{
			assert(_projections._mode == ShadowProjectionMode::Ortho);
			std::vector<OrthoSubProjection> result;
			result.reserve(_projections._normalProjCount);
			for (unsigned c=0; c<_projections._normalProjCount; ++c)
				result.push_back(OrthoSubProjection{_projections._orthoSub[c]._leftTopFront, _projections._orthoSub[c]._rightBottomBack});
			return result;
		}

		virtual void SetProjection(const Float4x4& nearWorldToProjection) override
		{
			assert(_projections._useNearProj);
			_projections._specialNearProjection = nearWorldToProjection;
			_projections._specialNearMinimalProjection = ExtractMinimalProjection(nearWorldToProjection);
		}

		virtual void AttachDriver(std::shared_ptr<Internal::ILightBase> driver) override
		{
			_driver = std::move(driver);
		}

		virtual void* QueryInterface(uint64_t interfaceTypeCode) override
		{
			if (interfaceTypeCode == typeid(IDepthTextureResolve).hash_code()) {
				return (IDepthTextureResolve*)this;
			} else if (interfaceTypeCode == typeid(IArbitraryShadowProjections).hash_code()) {
				if (_projections._mode == ShadowProjectionMode::Arbitrary || _projections._mode == ShadowProjectionMode::ArbitraryCubeMap)
					return (IArbitraryShadowProjections*)this;
			} else if (interfaceTypeCode == typeid(IOrthoShadowProjections).hash_code()) {
				if (_projections._mode == ShadowProjectionMode::Ortho)
					return (IOrthoShadowProjections*)this;
			} else if (interfaceTypeCode == typeid(INearShadowProjection).hash_code()) {
				if (_projections._useNearProj)
					return (INearShadowProjection*)this;
			} else if (interfaceTypeCode == typeid(IAttachDriver).hash_code()) {
				return (IAttachDriver*)this;
			} else if (interfaceTypeCode == typeid(StandardShadowProjection).hash_code()) {
				return this;
			} else if (_driver) {
				return _driver->QueryInterface(interfaceTypeCode);
			}
			return nullptr;
		}
	};
}}}
