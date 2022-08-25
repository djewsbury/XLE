// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StandardLightScene.h"
#include "ILightScene.h"
#include "../../Core/Exceptions.h"

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	void* StandardPositionalLight::QueryInterface(uint64_t interfaceTypeCode)
	{
		if (interfaceTypeCode == typeid(IPositionalLightSource).hash_code()) {
			return (IPositionalLightSource*)this;
		} else if (interfaceTypeCode == typeid(IUniformEmittance).hash_code()) {
			return (IUniformEmittance*)this;
		} else if (interfaceTypeCode == typeid(IFiniteLightSource).hash_code()) {
			if (_flags & Flags::SupportFiniteRange)
				return (IFiniteLightSource*)this;
		} else if (interfaceTypeCode == typeid(StandardPositionalLight).hash_code()) {
			return this;
		} else if (interfaceTypeCode == typeid(IAttachedShadowProbe).hash_code()) {
			return (IAttachedShadowProbe*)this;
		}
		return nullptr;
	}

	static const unsigned s_dominantLightSetIdx = ~0u;

	void* StandardLightScene::TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode)
	{
		auto i = LowerBound(_lookupTable, sourceId);
		if (i == _lookupTable.end() || i->first != sourceId) return nullptr;		// not found

		if (i->second._lightSet == s_dominantLightSetIdx) {
			assert(_dominantLightId == sourceId);
			assert(_dominantLightSet._lights.size() <= 1);
			assert(i->second._lightIndex == 0);
			if (!_dominantLightSet._lights.empty())
				return _dominantLightSet._lights[0]->QueryInterface(interfaceTypeCode);
			return nullptr;
		}

		auto& set = _tileableLightSets[i->second._lightSet];
		assert(set._lights[i->second._lightIndex]);

		// test components first
		for (auto& comp:set._components)
			if (void* interf = comp->QueryInterface(interfaceTypeCode, i->second._lightIndex))
				return interf;

		// fallback to the ILightBase
		return set._lights[i->second._lightIndex]->QueryInterface(interfaceTypeCode);
	}

	auto StandardLightScene::AddLightSource(LightOperatorId operatorId, std::unique_ptr<ILightBase> desc) -> LightSourceId
	{
		auto result = _nextLightSource++;
		LightSet* lightSet;
		unsigned lightSetIdx;
		if (operatorId == _dominantLightSet._operatorId) {
			assert(_dominantLightSet._lights.empty() || (_dominantLightSet._lights.size() == 1 && !_dominantLightSet._lights[0]));
			lightSet = &_dominantLightSet;
			lightSetIdx = s_dominantLightSetIdx;
			assert(_dominantLightId == ~0u);
			_dominantLightId = result;
		} else {
			lightSetIdx = GetLightSet(operatorId, ~0u);
			lightSet = &_tileableLightSets[lightSetIdx];
		}

		unsigned newLightIdx = lightSet->_allocatedLights.Allocate();
		if (lightSet->_lights.size() <= newLightIdx)
			lightSet->_lights.resize(newLightIdx+1);
		lightSet->_lights[newLightIdx] = std::move(desc);
		AddToLookupTable(result, {lightSetIdx, newLightIdx});

		// call components to register this light
		for (auto& comp:lightSet->_components)
			comp->RegisterLight(newLightIdx, *lightSet->_lights[newLightIdx]);

		return result;
	}

	void StandardLightScene::DestroyLightSource(LightSourceId sourceId)
	{
		auto i = LowerBound(_lookupTable, sourceId);
		if (i == _lookupTable.end() || i->first != sourceId) {
			assert(0);
			return;		// not found
		}

		auto* set = (i->second._lightSet == s_dominantLightSetIdx) ? &_dominantLightSet : &_tileableLightSets[i->second._lightSet];

		auto lightDesc = std::move(set->_lights[i->second._lightIndex]);
		for (const auto& comp:set->_components)
			comp->DeregisterLight(i->second._lightIndex);

		// Also destroy a shadow projection associated with this light, if it exists
		if (set->_shadowOperatorId != ~0u) {
			auto i = std::find_if(
				_dynamicShadowProjections.begin(), _dynamicShadowProjections.end(),
				[sourceId](const auto& c) { return c._lightId == sourceId; });
			if (i != _dynamicShadowProjections.end())
				_dynamicShadowProjections.erase(i);
		}

		if (i->second._lightSet == s_dominantLightSetIdx)
			_dominantLightId = ~0u;

		_lookupTable.erase(i);
	}

	void* StandardLightScene::TryGetShadowProjectionInterface(ShadowProjectionId preparerId, uint64_t interfaceTypeCode)
	{
		auto i = std::find_if(
			_dynamicShadowProjections.begin(), _dynamicShadowProjections.end(),
			[preparerId](const auto& c) { return c._id == preparerId; });
		if (i != _dynamicShadowProjections.end())
			return i->_desc->QueryInterface(interfaceTypeCode);

		if (_dominantShadowProjection._id == preparerId)
			return _dominantShadowProjection._desc->QueryInterface(interfaceTypeCode);

		return nullptr;
	}

	void StandardLightScene::ChangeLightSet(
		std::vector<std::pair<LightSourceId, LightSetAndIndex>>::iterator i,
		unsigned dstSetIdx)
	{
		auto* dstSet = &_tileableLightSets[dstSetIdx];
		auto* srcSet = &_tileableLightSets[i->second._lightSet];	// lookup again

		auto l = std::move(srcSet->_lights[i->second._lightIndex]);
		srcSet->_allocatedLights.Deallocate(i->second._lightIndex);
		for (const auto& comp:srcSet->_components)
			comp->DeregisterLight(i->second._lightIndex);

		auto newLightIdx = dstSet->_allocatedLights.Allocate();
		if (dstSet->_lights.size() <= newLightIdx)
			dstSet->_lights.resize(newLightIdx+1);
		dstSet->_lights[newLightIdx] = std::move(l);
		for (const auto& comp:srcSet->_components)
			comp->RegisterLight(newLightIdx, *dstSet->_lights[newLightIdx]);

		i->second = {dstSetIdx, newLightIdx};
	}

	void StandardLightScene::AddToLookupTable(LightSourceId lightId, LightSetAndIndex setAndIndex)
	{
		auto i = LowerBound(_lookupTable, lightId);
		assert(i == _lookupTable.end() || i->first != lightId);
		if (i == _lookupTable.end() || i->first != lightId) {
			_lookupTable.insert(i, std::make_pair(lightId, setAndIndex));
		} else {
			i->second = setAndIndex;
		}
	}
	
	auto StandardLightScene::AddShadowProjection(
		ShadowOperatorId shadowOperatorId,
		LightSourceId associatedLight,
		std::unique_ptr<ILightBase> desc) -> ShadowProjectionId
	{
		auto i = LowerBound(_lookupTable, associatedLight);
		if (i == _lookupTable.end() || i->first != associatedLight) {
			assert(0);
			return ~0u;
		}

		auto result = _nextShadow++;

		// assuming just the one dominant light here
		if (i->second._lightSet == s_dominantLightSetIdx) {
			assert(_dominantLightId == associatedLight);
			assert(_dominantLightSet._lights.size() <= 1);
			assert(_dominantShadowProjection._id == ~0u);
			assert(_dominantLightSet._shadowOperatorId == shadowOperatorId);		// the shadow operator is preset, and must agree with the shadow projection begin applied
			_dominantShadowProjection = {result, shadowOperatorId, associatedLight, std::move(desc)};
			return result;
		}

		_dynamicShadowProjections.push_back({result, shadowOperatorId, associatedLight, std::move(desc)});

		auto* srcSet = &_tileableLightSets[i->second._lightSet];
		auto dstSetIdx = GetLightSet(srcSet->_operatorId, shadowOperatorId);
		assert(dstSetIdx != i->second._lightSet);
		if (dstSetIdx != i->second._lightSet)
			ChangeLightSet(i, dstSetIdx);
		
		return result;
	}

	void StandardLightScene::ChangeLightsShadowOperator(IteratorRange<const LightSourceId*> lights, ShadowOperatorId shadowOperatorId)
	{
		// we could potentially do some optimizations here by sorting "lights" or by handling lights on a set by set basis
		for (auto lightId:lights) {
			auto i = LowerBound(_lookupTable, lightId);
			if (i == _lookupTable.end() || i->first != lightId) {
				assert(0);
				continue;
			}

			assert(i->second._lightSet != s_dominantLightSetIdx);
			auto* srcSet = &_tileableLightSets[i->second._lightSet];
			auto dstSetIdx = GetLightSet(srcSet->_operatorId, shadowOperatorId);
			if (i->second._lightSet == dstSetIdx) continue;	// no actual change

			ChangeLightSet(i, dstSetIdx);
		}
	}

	void StandardLightScene::DestroyShadowProjection(ShadowProjectionId preparerId)
	{
		auto i = std::find_if(
			_dynamicShadowProjections.begin(), _dynamicShadowProjections.end(),
			[preparerId](const auto& c) { return c._id == preparerId; });
		if (i != _dynamicShadowProjections.end()) {
			auto associatedLight = i->_lightId;
			_dynamicShadowProjections.erase(i);
			
			auto lightLookup = LowerBound(_lookupTable, associatedLight);
			assert(lightLookup != _lookupTable.end() && lightLookup->first == associatedLight);

			auto* srcSet = &_tileableLightSets[lightLookup->second._lightSet];
			auto dstSetIdx = GetLightSet(srcSet->_operatorId, ~0u);
			if (lightLookup->second._lightSet != dstSetIdx)
				ChangeLightSet(lightLookup, dstSetIdx);
			return;
		}

		if (_dominantShadowProjection._id == preparerId) {
			_dominantShadowProjection = {};
			return;
		}
			
		Throw(std::runtime_error("Invalid shadow preparer id: " + std::to_string(preparerId)));
	}

	auto StandardLightScene::GetLightSet(LightOperatorId lightOperator, ShadowOperatorId shadowOperator) -> unsigned
	{
		for (auto s=_tileableLightSets.begin(); s!=_tileableLightSets.end(); ++s)
			if (s->_operatorId == lightOperator && s->_shadowOperatorId == shadowOperator)
				return std::distance(_tileableLightSets.begin(), s);
		_tileableLightSets.push_back(LightSet{lightOperator, shadowOperator});
		return _tileableLightSets.size()-1;
	}

	void StandardLightScene::Clear()
	{
		for (auto& set:_tileableLightSets)
			set._lights.clear();
		_dynamicShadowProjections.clear();
		_dominantShadowProjection = {};
		_dominantLightSet._lights.clear();
		_dominantLightId = ~0u;
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
	ILightComponent::~ILightComponent() {}
}}}

namespace RenderCore { namespace LightingEngine
{
	uint64_t LightSourceOperatorDesc::GetHash(uint64_t seed) const
	{
		uint64_t h = 
			(uint64_t(_shape) & 0xff) << 8ull | (uint64_t(_diffuseModel) & 0xff) | (uint64_t(_flags) << 16ull);
		return HashCombine(h, seed);
	}

	std::optional<LightSourceShape> AsLightSourceShape(StringSection<> input)
	{
		if (XlEqString(input, "Directional")) return LightSourceShape::Directional;
		if (XlEqString(input, "Sphere")) return LightSourceShape::Sphere;
		if (XlEqString(input, "Tube")) return LightSourceShape::Tube;
		if (XlEqString(input, "Rectangle")) return LightSourceShape::Rectangle;
		if (XlEqString(input, "Disc")) return LightSourceShape::Disc;
		return {};
	}
	const char* AsString(LightSourceShape shape)
	{
		switch (shape) {
		case LightSourceShape::Directional: return "Directional";
		case LightSourceShape::Sphere: return "Sphere";
		case LightSourceShape::Tube: return "Tube";
		case LightSourceShape::Rectangle: return "Rectangle";
		case LightSourceShape::Disc: return "Disc";
		default:
			return nullptr;
		}
	}
	std::optional<DiffuseModel> AsDiffuseModel(StringSection<> input)
	{
		if (XlEqString(input, "Lambert")) return DiffuseModel::Lambert;
		if (XlEqString(input, "Disney")) return DiffuseModel::Disney;
		return {};
	}
	const char* AsString(DiffuseModel diffuseModel)
	{
		switch (diffuseModel) {
		case DiffuseModel::Lambert: return "Lambert";
		case DiffuseModel::Disney: return "Disney";
		default:
			return nullptr;
		}
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
	IAttachedShadowProbe::~IAttachedShadowProbe() {}
}}
