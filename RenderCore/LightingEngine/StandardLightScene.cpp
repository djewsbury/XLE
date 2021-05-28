// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StandardLightScene.h"
#include "LightScene.h"
#include "../../Core/Exceptions.h"

namespace RenderCore { namespace LightingEngine
{
	void* StandardLightDesc::QueryInterface(uint64_t interfaceTypeCode)
	{
		if (interfaceTypeCode == typeid(IPositionalLightSource).hash_code()) {
			return (IPositionalLightSource*)this;
		} else if (interfaceTypeCode == typeid(IUniformEmittance).hash_code()) {
			return (IUniformEmittance*)this;
		} else if (interfaceTypeCode == typeid(StandardLightDesc).hash_code()) {
			return this;
		}
		return nullptr;
	}

	void* StandardLightScene::TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode)
	{
		auto i = std::find_if(
			_lights.begin(), _lights.end(),
			[sourceId](const auto& c) { return c._id == sourceId; });
		if (i == _lights.end())
			return nullptr;
		return i->_desc->QueryInterface(interfaceTypeCode);
	}

	auto StandardLightScene::CreateLightSource(LightOperatorId op) -> LightSourceId
	{
		auto result = _nextLightSource++;
		auto desc = _lightSourceFactory->CreateLightSource(op);	
		_lights.push_back({result, op, std::move(desc)});
		return result;
	}

	void StandardLightScene::DestroyLightSource(LightSourceId sourceId)
	{
		auto i = std::find_if(
			_lights.begin(), _lights.end(),
			[sourceId](const auto& c) { return c._id == sourceId; });
		if (i != _lights.end()) {
			_lights.erase(i);

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

	auto StandardLightScene::CreateShadowProjection(ShadowOperatorId op, LightSourceId associatedLight) -> ShadowProjectionId
	{
		assert(_shadowProjectionFactory);
		auto result = _nextShadow++;
		auto desc = _shadowProjectionFactory->CreateShadowProjection(op);	
		_shadowProjections.push_back({result, op, associatedLight, std::move(desc)});
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
		_lights.clear();
		_shadowProjections.clear();
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

	uint64_t LightSourceOperatorDesc::Hash(uint64_t seed) const
	{
		uint64_t h = 
			(uint64_t(_shape) & 0xff) << 8ull | (uint64_t(_diffuseModel) & 0xff);
		return HashCombine(h, seed);
	}


	ILightScene::~ILightScene() {}
	IPositionalLightSource::~IPositionalLightSource() {}
	IUniformEmittance::~IUniformEmittance() {}
	IShadowPreparer::~IShadowPreparer() {}
	IArbitraryShadowProjections::~IArbitraryShadowProjections() {}
	IOrthoShadowProjections::~IOrthoShadowProjections() {}
	INearShadowProjection::~INearShadowProjection() {}
	ILightBase::~ILightBase() {}
	ILightSourceFactory::~ILightSourceFactory() {}
	IShadowProjectionFactory::~IShadowProjectionFactory() {}
}}
