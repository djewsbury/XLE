// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightScene.h"
#include "LightScene_Internal.h"
#include "../../Core/Exceptions.h"

namespace RenderCore { namespace LightingEngine { namespace Internal
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

		if (interfaceTypeCode == typeid(IShadowPreparer).hash_code()) {
			return &i->_desc;
		} else if (interfaceTypeCode == typeid(IArbitraryShadowProjections).hash_code()) {
			if (i->_desc._projections._mode == ShadowProjectionMode::Arbitrary || i->_desc._projections._mode == ShadowProjectionMode::ArbitraryCubeMap)
				return (IArbitraryShadowProjections*)&i->_desc;
		} else if (interfaceTypeCode == typeid(IOrthoShadowProjections).hash_code()) {
			if (i->_desc._projections._mode == ShadowProjectionMode::Ortho)
				return (IOrthoShadowProjections*)&i->_desc;
		} else if (interfaceTypeCode == typeid(INearShadowProjection).hash_code()) {
			if (i->_desc._projections._useNearProj)
				return (INearShadowProjection*)&i->_desc;
		}

		return nullptr;
	}

	auto StandardLightScene::CreateShadowProjection(ShadowOperatorId op, LightSourceId associatedLight) -> ShadowProjectionId
	{
		assert(op < _shadowOperators.size());
		auto result = _nextShadow++;
		ShadowProjectionDesc desc{};
		desc._projections._mode = _shadowOperators[op]._projectionMode;
		desc._projections._useNearProj = _shadowOperators[op]._enableNearCascade;
		desc._projections._normalProjCount = _shadowOperators[op]._normalProjCount;
		_shadowProjections.push_back({result, op, associatedLight, desc});
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

}}}

namespace RenderCore { namespace LightingEngine
{
	ILightScene::~ILightScene() {}
	IPositionalLightSource::~IPositionalLightSource() {}
	IUniformEmittance::~IUniformEmittance() {}
	IShadowPreparer::~IShadowPreparer() {}
	IArbitraryShadowProjections::~IArbitraryShadowProjections() {}
	IOrthoShadowProjections::~IOrthoShadowProjections() {}
	INearShadowProjection::~INearShadowProjection() {}
}}
