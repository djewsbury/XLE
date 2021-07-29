// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StandardLightScene.h"
#include "LightScene.h"
#include "../../Core/Exceptions.h"

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	void* StandardLightDesc::QueryInterface(uint64_t interfaceTypeCode)
	{
		if (interfaceTypeCode == typeid(IPositionalLightSource).hash_code()) {
			return (IPositionalLightSource*)this;
		} else if (interfaceTypeCode == typeid(IUniformEmittance).hash_code()) {
			return (IUniformEmittance*)this;
		} else if (interfaceTypeCode == typeid(IFiniteLightSource).hash_code()) {
			if (_flags & Flags::SupportFiniteRange)
				return (IFiniteLightSource*)this;
		} else if (interfaceTypeCode == typeid(StandardLightDesc).hash_code()) {
			return this;
		}
		return nullptr;
	}

	void* StandardLightScene::TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode)
	{
		auto i = std::find_if(
			_tileableLights.begin(), _tileableLights.end(),
			[sourceId](const auto& c) { return c._id == sourceId; });
		if (i == _tileableLights.end())
			return nullptr;
		return i->_desc->QueryInterface(interfaceTypeCode);
	}

	auto StandardLightScene::AddLightSource(LightOperatorId operatorId, std::unique_ptr<ILightBase> desc) -> LightSourceId
	{
		auto result = _nextLightSource++;
		_tileableLights.push_back({result, operatorId, std::move(desc)});
		return result;
	}

	void StandardLightScene::DestroyLightSource(LightSourceId sourceId)
	{
		auto i = std::find_if(
			_tileableLights.begin(), _tileableLights.end(),
			[sourceId](const auto& c) { return c._id == sourceId; });
		if (i != _tileableLights.end()) {
			_tileableLights.erase(i);

			// Also destroy a shadow projection associated with this light, if it exists
			auto i = std::find_if(
				_shadowProjections.begin(), _shadowProjections.end(),
				[sourceId](const auto& c) { return c._lightId == sourceId; });
			if (i != _shadowProjections.end())
				_shadowProjections.erase(i);
		} else
			Throw(std::runtime_error("Invalid light source id: " + std::to_string(sourceId)));
	}

	void* StandardLightScene::TryGetShadowProjectionInterface(ShadowProjectionId preparerId, uint64_t interfaceTypeCode)
	{
		auto i = std::find_if(
			_shadowProjections.begin(), _shadowProjections.end(),
			[preparerId](const auto& c) { return c._id == preparerId; });
		if (i == _shadowProjections.end())
			return nullptr;
		return i->_desc->QueryInterface(interfaceTypeCode);
	}
	
	auto StandardLightScene::AddShadowProjection(
		LightOperatorId operatorId,
		LightSourceId associatedLight,
		std::unique_ptr<ILightBase> desc) -> ShadowProjectionId
	{
		auto result = _nextShadow++;
		_shadowProjections.push_back({result, operatorId, associatedLight, std::move(desc)});
		return result;
	}

	void StandardLightScene::DestroyShadowProjection(ShadowProjectionId preparerId)
	{
		auto i = std::find_if(
			_shadowProjections.begin(), _shadowProjections.end(),
			[preparerId](const auto& c) { return c._id == preparerId; });
		if (i != _shadowProjections.end()) {
			_shadowProjections.erase(i);
		} else
			Throw(std::runtime_error("Invalid shadow preparer id: " + std::to_string(preparerId)));
	}

	void StandardLightScene::Clear()
	{
		_tileableLights.clear();
		_shadowProjections.clear();
	}

	void StandardLightScene::ReserveLightSourceIds(unsigned idCount)
	{
		_nextLightSource += idCount;
	}

	void* StandardLightScene::QueryInterface(uint64_t typeCode)
	{
		if (typeCode == typeid(StandardLightScene).hash_code())
			return this;
		return nullptr;
	}

	StandardLightScene::StandardLightScene()
	{}
	StandardLightScene::~StandardLightScene()
	{}

	ILightBase::~ILightBase() {}
}}}

namespace RenderCore { namespace LightingEngine
{
	uint64_t LightSourceOperatorDesc::Hash(uint64_t seed) const
	{
		uint64_t h = 
			(uint64_t(_shape) & 0xff) << 8ull | (uint64_t(_diffuseModel) & 0xff) | (uint64_t(_flags) << 16ull);
		return HashCombine(h, seed);
	}

	ILightScene::~ILightScene() {}
	IPositionalLightSource::~IPositionalLightSource() {}
	IUniformEmittance::~IUniformEmittance() {}
	IFiniteLightSource::~IFiniteLightSource() {}
	IDistantIBLSource::~IDistantIBLSource() {}
	ISSAmbientOcclusion::~ISSAmbientOcclusion() {}
	IShadowPreparer::~IShadowPreparer() {}
	IArbitraryShadowProjections::~IArbitraryShadowProjections() {}
	IOrthoShadowProjections::~IOrthoShadowProjections() {}
	INearShadowProjection::~INearShadowProjection() {}
}}
