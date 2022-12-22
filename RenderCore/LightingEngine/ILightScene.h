// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/MemoryUtils.h"
#include <cstdint>

namespace Assets { class OperationContext; }

namespace RenderCore { namespace LightingEngine
{
	class ILightScene
	{
	public:
		using LightSourceId = unsigned;
		using LightOperatorId = unsigned;
		virtual void* TryGetLightSourceInterface(LightSourceId, uint64_t interfaceTypeCode) = 0; 
		virtual LightSourceId CreateLightSource(LightOperatorId op) = 0;
		virtual LightSourceId CreateAmbientLightSource() = 0;
		virtual void DestroyLightSource(LightSourceId) = 0;

		using ShadowOperatorId = unsigned;
		virtual void SetShadowOperator(LightSourceId, ShadowOperatorId) = 0;

		virtual void Clear() = 0;

		virtual void* QueryInterface(uint64_t) = 0;
		virtual ~ILightScene();

		template<typename Type>
			Type* TryGetLightSourceInterface(LightSourceId sourceId)
			{
				constexpr auto interfaceCode = TypeHashCode<Type>;
				return (Type*)TryGetLightSourceInterface(sourceId, interfaceCode);
			}
	};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class IPositionalLightSource
	{
	public:
		virtual void SetLocalToWorld(const Float4x4&) = 0;
		virtual Float4x4 GetLocalToWorld() const = 0;
		virtual ~IPositionalLightSource();
	};

	class IUniformEmittance
	{
	public:
		virtual void SetBrightness(Float3 rgb) = 0;
		virtual Float3 GetBrightness() const = 0;
		virtual void SetDiffuseWideningFactors(Float2) = 0;
		virtual Float2 GetDiffuseWideningFactors() const = 0;
		virtual ~IUniformEmittance();
	};

	class IFiniteLightSource
	{
	public:
		virtual void SetCutoffBrightness(float brightness) = 0;
		virtual void SetCutoffRange(float cutoff) = 0;
		virtual float GetCutoffRange() const = 0;
		virtual ~IFiniteLightSource();
	};

	class IDistantIBLSource
	{
	public:
		virtual void SetEquirectangularSource(std::shared_ptr<::Assets::OperationContext>, StringSection<> input) = 0;
		virtual ~IDistantIBLSource();
	};

	class ISSAmbientOcclusion
	{
	public:
		virtual ~ISSAmbientOcclusion();
	};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class IDepthTextureResolve
	{
	public:
		struct Desc
		{
			float       _worldSpaceResolveBias = 0.f;
			float       _tanBlurAngle = 0.f;
			float       _minBlurSearch = 0.f, _maxBlurSearch = 0.f;
			float		_casterDistanceExtraBias = 0.f;
		};
		virtual void SetDesc(const Desc&) = 0;
		virtual Desc GetDesc() const = 0;
		virtual ~IDepthTextureResolve();
	};
	
	class IArbitraryShadowProjections
	{
	public:
		virtual void SetArbitrarySubProjections(
			IteratorRange<const Float4x4*> worldToCamera,
			IteratorRange<const Float4x4*> cameraToProjection) = 0;
		virtual ~IArbitraryShadowProjections();
	};

	class IOrthoShadowProjections
	{
	public:
		struct OrthoSubProjection
		{
			Float3      _leftTopFront = Zero<Float3>();
			Float3      _rightBottomBack = Zero<Float3>();
		};
		virtual void SetWorldToOrthoView(const Float4x4& worldToCamera) = 0;
		virtual void SetOrthoSubProjections(IteratorRange<const OrthoSubProjection*>) = 0;
		virtual Float4x4 GetWorldToOrthoView() const = 0;
		virtual std::vector<OrthoSubProjection> GetOrthoSubProjections() const = 0;
		virtual ~IOrthoShadowProjections();
	};

	class INearShadowProjection
	{
	public:
		virtual void SetProjection(const Float4x4&) = 0;
		virtual ~INearShadowProjection();
	};
}}
