// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StandardLightScene.h"
#include "ILightScene.h"
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
		for (auto&set:_lightSets) {
			auto i = std::find_if(
				set._lights.begin(), set._lights.end(),
				[sourceId](const auto& c) { return c._id == sourceId; });
			if (i != set._lights.end())
				return i->_desc->QueryInterface(interfaceTypeCode);
		}
		return nullptr;
	}

	auto StandardLightScene::AddLightSource(LightOperatorId operatorId, std::unique_ptr<ILightBase> desc) -> LightSourceId
	{
		auto result = _nextLightSource++;
		GetLightSet(operatorId, ~0u)._lights.push_back({result, std::move(desc)});
		return result;
	}

	void StandardLightScene::DestroyLightSource(LightSourceId sourceId)
	{
		for (auto&set:_lightSets) {
			auto i = std::find_if(
				set._lights.begin(), set._lights.end(),
				[sourceId](const auto& c) { return c._id == sourceId; });
			if (i != set._lights.end()) {
				set._lights.erase(i);

				// Also destroy a shadow projection associated with this light, if it exists
				if (set._shadowOperatorId != ~0u) {
					auto i = std::find_if(
						_dynamicShadowProjections.begin(), _dynamicShadowProjections.end(),
						[sourceId](const auto& c) { return c._lightId == sourceId; });
					if (i != _dynamicShadowProjections.end())
						_dynamicShadowProjections.erase(i);
				}
				return;
			}				
		}

		Throw(std::runtime_error("Invalid light source id: " + std::to_string(sourceId)));
	}

	void* StandardLightScene::TryGetShadowProjectionInterface(ShadowProjectionId preparerId, uint64_t interfaceTypeCode)
	{
		auto i = std::find_if(
			_dynamicShadowProjections.begin(), _dynamicShadowProjections.end(),
			[preparerId](const auto& c) { return c._id == preparerId; });
		if (i == _dynamicShadowProjections.end())
			return nullptr;
		return i->_desc->QueryInterface(interfaceTypeCode);
	}
	
	auto StandardLightScene::AddShadowProjection(
		ShadowOperatorId shadowOperatorId,
		LightSourceId associatedLight,
		std::unique_ptr<ILightBase> desc) -> ShadowProjectionId
	{
		auto result = _nextShadow++;
		_dynamicShadowProjections.push_back({result, shadowOperatorId, associatedLight, std::move(desc)});

		for (auto&set:_lightSets) {
			if (set._shadowOperatorId != ~0u) {
				#if defined(_DEBUG)
					auto i = std::find_if(
						set._lights.begin(), set._lights.end(),
						[associatedLight](const auto& c) { return c._id == associatedLight; });
					assert(i == set._lights.end());		// if you hit this it means we're associating a shadow projection with a light that already has a shadow projection
				#endif
				continue;
			}
			auto i = std::find_if(
				set._lights.begin(), set._lights.end(),
				[associatedLight](const auto& c) { return c._id == associatedLight; });
			if (i != set._lights.end()) {
				auto l = std::move(*i);
				set._lights.erase(i);
				GetLightSet(set._operatorId, shadowOperatorId)._lights.push_back(std::move(l));
				break;
			}
		}

		return result;
	}

	void StandardLightScene::DestroyShadowProjection(ShadowProjectionId preparerId)
	{
		auto i = std::find_if(
			_dynamicShadowProjections.begin(), _dynamicShadowProjections.end(),
			[preparerId](const auto& c) { return c._id == preparerId; });
		if (i != _dynamicShadowProjections.end()) {
			auto associatedLight = i->_lightId;

			for (auto&set:_lightSets) {
				if (set._shadowOperatorId != i->_operatorId) continue;
				auto i = std::find_if(
					set._lights.begin(), set._lights.end(),
					[associatedLight](const auto& c) { return c._id == associatedLight; });
				if (i != set._lights.end()) {
					auto l = std::move(*i);
					set._lights.erase(i);
					GetLightSet(set._operatorId, ~0u)._lights.push_back(std::move(l));
				}
			}

			_dynamicShadowProjections.erase(i);
		} else
			Throw(std::runtime_error("Invalid shadow preparer id: " + std::to_string(preparerId)));
	}

	auto StandardLightScene::GetLightSet(LightOperatorId lightOperator, ShadowOperatorId shadowOperator) -> LightSet&
	{
		for (auto&s:_lightSets)
			if (s._operatorId == lightOperator && s._shadowOperatorId == shadowOperator)
				return s;
		_lightSets.push_back(LightSet{lightOperator, shadowOperator});
		return *(_lightSets.end()-1);
	}

	void StandardLightScene::Clear()
	{
		for (auto& set:_lightSets)
			set._lights.clear();
		_dynamicShadowProjections.clear();
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
	IDepthTextureResolve::~IDepthTextureResolve() {}
	IArbitraryShadowProjections::~IArbitraryShadowProjections() {}
	IOrthoShadowProjections::~IOrthoShadowProjections() {}
	INearShadowProjection::~INearShadowProjection() {}
}}
