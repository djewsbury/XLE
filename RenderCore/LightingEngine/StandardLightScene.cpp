// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightScene.h"
#include "LightScene_Internal.h"
#include "../../Core/Exceptions.h"

namespace RenderCore { namespace LightingEngine
{
	void* StandardLightScene::TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode)
	{
		auto i = std::find_if(
			_lights.begin(), _lights.end(),
			[sourceId](const auto& c) { return c._id == sourceId; });
		if (i == _lights.end())
			return nullptr;

		if (interfaceTypeCode == typeid(IPositionalLightSource).hash_code()) {
			return (IPositionalLightSource*)&i->_desc;
		} else if (interfaceTypeCode == typeid(IUniformEmittance).hash_code()) {
			return (IUniformEmittance*)&i->_desc;
		}

		return nullptr;
	}

	auto StandardLightScene::CreateLightSource(LightOperatorId op) -> LightSourceId
	{
		assert(op < _lightSourceOperators.size());
		auto result = _nextLightSource++;
		_lights.push_back({result, op, LightDesc{}});
		return result;
	}

	void StandardLightScene::DestroyLightSource(LightSourceId sourceId)
	{
		auto i = std::find_if(
			_lights.begin(), _lights.end(),
			[sourceId](const auto& c) { return c._id == sourceId; });
		if (i != _lights.end()) {
			_lights.erase(i);
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


	ILightScene::~ILightScene() {}
	IPositionalLightSource::~IPositionalLightSource() {}
	IUniformEmittance::~IUniformEmittance() {}
	IShadowPreparer::~IShadowPreparer() {}
	IArbitraryShadowProjections::~IArbitraryShadowProjections() {}
	IOrthoShadowProjections::~IOrthoShadowProjections() {}
	INearShadowProjection::~INearShadowProjection() {}
	ILightBase::~ILightBase() {}
	IShadowProjectionFactory::~IShadowProjectionFactory() {}
}}
