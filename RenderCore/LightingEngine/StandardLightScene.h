// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ILightScene.h"
#include "StandardLightOperators.h"
#include "../../Math/Transformations.h"
#include "../../Math/ProjectionMath.h"

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	class ILightBase
	{
	public:
		virtual void* QueryInterface(uint64_t interfaceTypeCode) = 0;
		virtual ~ILightBase();
	};

	class StandardLightScene : public ILightScene
	{
	public:
		struct Light
		{
			LightSourceId _id;
			std::unique_ptr<ILightBase> _desc;
		};
		struct LightSet
		{
			LightOperatorId _operatorId;
			ShadowOperatorId _shadowOperatorId;
			std::vector<Light> _lights;
		};
		std::vector<LightSet> _lightSets;

		struct DynamicShadowProjection
		{
			ShadowProjectionId _id;
			ShadowOperatorId _operatorId;
			LightSourceId _lightId;
			std::unique_ptr<ILightBase> _desc;
		};
		std::vector<DynamicShadowProjection> _dynamicShadowProjections;

		LightSourceId _nextLightSource = 0;
		ShadowProjectionId _nextShadow = 0;

		virtual void* TryGetLightSourceInterface(LightSourceId, uint64_t interfaceTypeCode) override;
		virtual void DestroyLightSource(LightSourceId) override;
		virtual void* TryGetShadowProjectionInterface(ShadowProjectionId, uint64_t interfaceTypeCode) override;
		virtual void DestroyShadowProjection(ShadowProjectionId) override;
		virtual void Clear() override;
		virtual void* QueryInterface(uint64_t) override;
		StandardLightScene();
		~StandardLightScene();

	protected:
		LightSourceId AddLightSource(
			LightOperatorId operatorId,
			std::unique_ptr<ILightBase> desc);

		ShadowProjectionId AddShadowProjection(
			ShadowOperatorId shadowOperatorId,
			LightSourceId associatedLight,
			std::unique_ptr<ILightBase> desc);

		void ReserveLightSourceIds(unsigned idCount); 
		LightSet& GetLightSet(LightOperatorId, ShadowOperatorId);
	};

	class StandardLightDesc : public ILightBase, public IPositionalLightSource, public IUniformEmittance, public IFiniteLightSource
	{
	public:
		Float3x3    _orientation;
		Float3      _position;
		Float2      _radii;

		float       _cutoffRange;
		Float3      _brightness;
		float       _diffuseWideningMin;
		float       _diffuseWideningMax;

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

		virtual void SetCutoffBrightness(float cutoffBrightness) override
		{
			// distance attenuation formula:
			//		1.0f / (distanceSq+1)
			// 
			// brightness / (distanceSq+1) = cutoffBrightness
			// (distanceSq+1) / brightness = 1.0f / cutoffBrightness
			// distanceSq = brightness / cutoffBrightness - 1
			float brightness = std::max(std::max(_brightness[0], _brightness[1]), _brightness[2]);
			if (cutoffBrightness < brightness) {
				SetCutoffRange(std::sqrt(brightness / cutoffBrightness - 1.0f));
			} else {
				// The light can't actually get as bright as the cutoff brightness.. just set to a small value
				SetCutoffRange(1e-3f);
			}
		}

		virtual void SetBrightness(Float3 rgb) override { _brightness = rgb; }
		virtual Float3 GetBrightness() const override { return _brightness; }
		virtual void SetDiffuseWideningFactors(Float2 minAndMax) override
		{
			_diffuseWideningMin = minAndMax[0];
			_diffuseWideningMax = minAndMax[1];
		}
		virtual Float2 GetDiffuseWideningFactors() const override
		{
			return Float2 { _diffuseWideningMin, _diffuseWideningMax };
		}

		virtual void* QueryInterface(uint64_t interfaceTypeCode) override;

		struct Flags
		{
			enum Enum { SupportFiniteRange = 1<<0 };
			using BitField = unsigned;
		};
		Flags::BitField _flags;

		StandardLightDesc(Flags::BitField flags)
		: _flags(flags)
		{
			_position = Normalize(Float3(-.1f, 0.33f, 1.f));
			_orientation = Identity<Float3x3>();
			_cutoffRange = 10000.f;
			_radii = Float2(1.f, 1.f);
			_brightness = Float3(1.f, 1.f, 1.f);

			_diffuseWideningMin = 0.5f;
			_diffuseWideningMax = 2.5f;
		}
	};
}}}


namespace RenderCore { namespace LightingEngine
{

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

