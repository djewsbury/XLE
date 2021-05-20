// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"
#include "../../Utility/IteratorUtils.h"
#include <cstdint>

#pragma once

namespace RenderCore { namespace LightingEngine
{
	class ILightScene
	{
	public:
		using LightSourceId = unsigned;
		using LightOperatorId = unsigned;
		virtual void* TryGetLightSourceInterface(LightSourceId, uint64_t interfaceTypeCode) = 0; 
		virtual LightSourceId CreateLightSource(LightOperatorId op) = 0;
		virtual void DestroyLightSource(LightSourceId) = 0;

		using ShadowProjectionId = unsigned;
		using ShadowOperatorId = unsigned;
		virtual void* TryGetShadowProjectionInterface(ShadowProjectionId, uint64_t interfaceTypeCode) = 0; 
		virtual ShadowProjectionId CreateShadowProjection(ShadowOperatorId op, LightSourceId associatedLight) = 0;
		virtual void DestroyShadowProjection(ShadowProjectionId) = 0;

		virtual void* QueryInterface(uint64_t) = 0;
		virtual ~ILightScene();

		template<typename Type>
			Type* TryGetLightSourceInterface(LightSourceId sourceId)
			{
				return (Type*)TryGetLightSourceInterface(sourceId, typeid(Type).hash_code());
			}

		template<typename Type>
			Type* TryGetShadowProjectionInterface(ShadowProjectionId shadowId)
			{
				return (Type*)TryGetShadowProjectionInterface(shadowId, typeid(Type).hash_code());
			}
	};

	class ILightBase
	{
	public:
		virtual void* QueryInterface(uint64_t interfaceTypeCode) = 0;
		virtual ~ILightBase();
	};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class IPositionalLightSource
	{
	public:
		virtual void SetLocalToWorld(const Float4x4&) = 0;
		virtual Float4x4 GetLocalToWorld() const = 0;
		virtual void SetCutoffRange(float) = 0;
		virtual float GetCutoffRange() const = 0;
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class IShadowPreparer
	{
	public:
		struct Desc
		{
			float       _worldSpaceResolveBias = 0.f;
			float       _tanBlurAngle = 0.f;
			float       _minBlurSearch = 0.f, _maxBlurSearch = 0.f;
		};
		virtual void SetDesc(const Desc&) = 0;
		virtual Desc GetDesc() const = 0;
		virtual ~IShadowPreparer();
	};
	
	class IArbitraryShadowProjections
	{
	public:
		virtual void SetProjections(
			IteratorRange<const Float4x4*> worldToCamera,
			IteratorRange<const Float4x4*> cameraToProjection) = 0;
		virtual ~IArbitraryShadowProjections();
	};

	class IOrthoShadowProjections
	{
	public:
		struct OrthoSubProjection
		{
			Float3      _projMins;
			Float3      _projMaxs;
		};
		virtual void SetWorldToDefiningProjection(const Float4x4& worldToCamera) = 0;
		virtual void SetSubProjections(IteratorRange<const OrthoSubProjection*>) = 0;
		virtual ~IOrthoShadowProjections();
	};

	class INearShadowProjection
	{
	public:
		virtual void SetProjection(const Float4x4&) = 0;
		virtual ~INearShadowProjection();
	};

}}
