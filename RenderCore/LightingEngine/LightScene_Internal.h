// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "LightScene.h"
#include "StandardLightOperators.h"
#include "../../Math/Transformations.h"
#include "../../Math/ProjectionMath.h"

namespace RenderCore { namespace LightingEngine
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
	
	class IShadowProjectionFactory
	{
	public:
		virtual std::unique_ptr<ILightBase> CreateShadowProjection(ILightScene::ShadowOperatorId) = 0;
		virtual ~IShadowProjectionFactory();
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
			std::unique_ptr<ILightBase> _desc;
		};
		std::vector<ShadowProjection> _shadowProjections;

        std::vector<LightSourceOperatorDesc> _lightSourceOperators;
		std::shared_ptr<IShadowProjectionFactory> _shadowProjectionFactory;

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


}}

