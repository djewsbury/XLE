// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "LightScene.h"
#include "StandardLightOperators.h"
#include "ShadowPreparer.h"
#include "../../Math/Transformations.h"
#include "../../Math/ProjectionMath.h"

namespace RenderCore { namespace LightingEngine { namespace Internal
{
    class LightDesc : public IPositionalLightSource, public IUniformEmittance
	{
	public:
		Float3x3    _orientation;
		Float3      _position;
		Float2      _radii;

		float       _cutoffRange;
		Float3      _diffuseColor;
		Float3      _specularColor;
		float       _diffuseWideningMin;
		float       _diffuseWideningMax;

		unsigned    _diffuseModel;				// shift to light operator
		unsigned    _shadowResolveModel;		// shift to shadowing operator

 		virtual void SetLocalToWorld(const Float4x4& localToWorld) override
		{
			ScaleRotationTranslationM srt(localToWorld);
			_orientation = srt._rotation;
			_position = srt._translation;
			_radii = Truncate(srt._scale); 
		}

		virtual Float4x4 GetLocalToWorld() const override
		{
			ScaleRotationTranslationM srt { Expand(_radii, 1.f), _orientation, _position };
			return AsFloat4x4(srt);
		}

		virtual void SetCutoffRange(float cutoff) override { _cutoffRange = cutoff; }
		virtual float GetCutoffRange() const override { return _cutoffRange; }

		virtual void SetBrightness(Float3 rgb) override { _diffuseColor = rgb; }
		virtual Float3 GetBrightness() const override { return _diffuseColor; }
		virtual void SetDiffuseWideningFactors(Float2 minAndMax) override
		{
			_diffuseWideningMin = minAndMax[0];
			_diffuseWideningMax = minAndMax[1];
		}
		virtual Float2 GetDiffuseWideningFactors() const override
		{
			return Float2 { _diffuseWideningMin, _diffuseWideningMax };
		}

        LightDesc()
        {
            _position = Normalize(Float3(-.1f, 0.33f, 1.f));
            _orientation = Identity<Float3x3>();
            _cutoffRange = 10000.f;
            _radii = Float2(1.f, 1.f);
            _diffuseColor = Float3(1.f, 1.f, 1.f);
            _specularColor = Float3(1.f, 1.f, 1.f);

            _diffuseWideningMin = 0.5f;
            _diffuseWideningMax = 2.5f;
            _diffuseModel = 1;

            _shadowResolveModel = 0;
        }
	};

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
			Float3      _projMins;
			Float3      _projMaxs;
		};

		ShadowProjectionMode	_mode = ShadowProjectionMode::Arbitrary;
		unsigned                _normalProjCount = 0;
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

	using LightId = unsigned;

	/// <summary>Defines the projected shadows for a single light<summary>
	class ShadowProjectionDesc : public IShadowPreparer, public IArbitraryShadowProjections, public IOrthoShadowProjections, public INearShadowProjection
	{
	public:
		using Projections = MultiProjection<MaxShadowTexturesPerLight>;
		Projections     _projections;
		Float4x4        _worldToClip = Identity<Float4x4>();   ///< Intended for use in CPU-side culling. Objects culled by this transform will be culled from all projections

		float           _worldSpaceResolveBias = 0.f;
		float           _tanBlurAngle = 0.00436f;
		float           _minBlurSearch = 0.5f, _maxBlurSearch = 25.f;

		virtual void SetDesc(const Desc& newDesc) override
		{
			_worldSpaceResolveBias = newDesc._worldSpaceResolveBias;
			_tanBlurAngle = newDesc._tanBlurAngle;
			_minBlurSearch = newDesc._minBlurSearch;
			_maxBlurSearch = newDesc._maxBlurSearch;
		}
		virtual Desc GetDesc() const override
		{
			return Desc { _worldSpaceResolveBias, _tanBlurAngle, _minBlurSearch, _maxBlurSearch };
		}

		virtual void SetProjections(
			IteratorRange<const Float4x4*> worldToCamera,
			IteratorRange<const Float4x4*> cameraToProjection) override
		{
			assert(_projections._mode == ShadowProjectionMode::Arbitrary || _projections._mode == ShadowProjectionMode::ArbitraryCubeMap);
			assert(worldToCamera.size() <= MaxShadowTexturesPerLight);
			assert(!worldToCamera.empty());
			assert(worldToCamera.size() == cameraToProjection.size());
			auto projCount = std::min((size_t)MaxShadowTexturesPerLight, worldToCamera.size());
            assert(projCount == _projections._normalProjCount);     // a mis-match here means it does not agree with the operator
			for (unsigned c=0; c<projCount; ++c) {
				_projections._fullProj[c]._worldToProjTransform = Combine(worldToCamera[c], cameraToProjection[c]);
				_projections._minimalProjection[c] = ExtractMinimalProjection(cameraToProjection[c]);
			}
		}

		virtual void SetWorldToDefiningProjection(const Float4x4& worldToCamera) override
		{
			assert(_projections._mode == ShadowProjectionMode::Ortho);
			_projections._definitionViewMatrix = worldToCamera;
		}

		virtual void SetSubProjections(IteratorRange<const OrthoSubProjection*> projections) override
		{
			assert(_projections._mode == ShadowProjectionMode::Ortho);
			assert(projections.size() < MaxShadowTexturesPerLight);
			assert(!projections.empty());
			auto projCount = std::min((size_t)MaxShadowTexturesPerLight, projections.size());
			assert(projCount == _projections._normalProjCount);     // a mis-match here means it does not agree with the operator
            for (unsigned c=0; c<projCount; ++c) {
				_projections._orthoSub[c]._projMins = projections[c]._projMins;
				_projections._orthoSub[c]._projMaxs = projections[c]._projMaxs;
			}
		}

		virtual void SetProjection(const Float4x4& nearWorldToProjection) override
		{
			assert(_projections._useNearProj);
			_projections._specialNearProjection = nearWorldToProjection;
			_projections._specialNearMinimalProjection = ExtractMinimalProjection(nearWorldToProjection);
		}
	};

	class StandardLightScene : public ILightScene
	{
	public:
		struct Light
		{
			LightSourceId _id;
			LightOperatorId _operatorId;
			LightDesc _desc;
		};
		std::vector<Light> _lights;
		struct ShadowProjection
		{
			ShadowProjectionId _id;
			ShadowOperatorId _operatorId;
			LightSourceId _lightId;
			ShadowProjectionDesc _desc;
		};
		std::vector<ShadowProjection> _shadowProjections;

        std::vector<LightSourceOperatorDesc> _lightSourceOperators;
        std::vector<ShadowOperatorDesc> _shadowOperators;

		LightSourceId _nextLightSource = 0;
		ShadowProjectionId _nextShadow = 0;

		virtual void* TryGetLightSourceInterface(LightSourceId, uint64_t interfaceTypeCode) override;
		virtual LightSourceId CreateLightSource(LightOperatorId op) override;
		virtual void DestroyLightSource(LightSourceId) override;
		virtual void* TryGetShadowProjectionInterface(ShadowProjectionId, uint64_t interfaceTypeCode) override;
		virtual ShadowProjectionId CreateShadowProjection(ShadowOperatorId op, LightSourceId associatedLight) override;
		virtual void DestroyShadowProjection(ShadowProjectionId) override;
		virtual void* QueryInterface(uint64_t) override;
		StandardLightScene();
		~StandardLightScene();
	};


	////////////  temp ----->
	enum class SkyTextureType { HemiCube, Cube, Equirectangular, HemiEquirectangular };
	
	class EnvironmentalLightingDesc
	{
	public:
		std::string   _skyTexture;   ///< use "<texturename>_*" when using a half cube style sky texture. The system will fill in "_*" with appropriate characters	
		SkyTextureType _skyTextureType;

		std::string   _diffuseIBL;   ///< Diffuse IBL map. Sometimes called irradiance map or ambient map
		std::string   _specularIBL;  ///< Prefiltered specular IBL map.

		Float3	_ambientLight = Float3(0.f, 0.f, 0.f);

		float   _skyBrightness = 1.f;
		float   _skyReflectionScale = 1.0f;
		float   _skyReflectionBlurriness = 2.f;

		bool    _doRangeFog = false;
		Float3  _rangeFogInscatter = Float3(0.f, 0.f, 0.f);
		float   _rangeFogThickness = 10000.f;     // optical thickness for range based fog

		bool    _doAtmosphereBlur = false;
		float   _atmosBlurStdDev = 1.3f;
		float   _atmosBlurStart = 1000.f;
		float   _atmosBlurEnd = 1500.f;
	};


}}}

