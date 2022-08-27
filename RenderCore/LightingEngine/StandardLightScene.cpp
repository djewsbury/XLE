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
			assert(i->second._lightIndex == 0);
			if (!_dominantLightSet._baseData.empty())
				return _dominantLightSet._baseData.GetObject(0).QueryInterface(interfaceTypeCode);
			return nullptr;
		}

		auto& set = _tileableLightSets[i->second._lightSet];
		assert(set._baseData._allocationFlags.IsAllocated(i->second._lightIndex));

		// test components first
		for (auto& comp:set._boundComponents)
			if (void* interf = comp->QueryInterface(i->second._lightSet, i->second._lightIndex, interfaceTypeCode))
				return interf;

		// fallback to the ILightBase
		return set._baseData.GetObject(i->second._lightIndex).QueryInterface(interfaceTypeCode);
	}

	auto StandardLightScene::AddLightSource(LightOperatorId operatorId) -> LightSourceId
	{
		auto result = _nextLightSource++;
		LightSet* lightSet;
		unsigned lightSetIdx;
		if (operatorId == _dominantLightSet._operatorId) {
			assert(_dominantLightSet._baseData.empty());
			lightSet = &_dominantLightSet;
			lightSetIdx = s_dominantLightSetIdx;
			assert(_dominantLightId == ~0u);
			_dominantLightId = result;
		} else {
			lightSetIdx = GetLightSet(operatorId, ~0u);
			lightSet = &_tileableLightSets[lightSetIdx];
		}

		auto newLight = lightSet->_baseData.Allocate();
		auto newLightIdx = newLight.GetIndex();
		AddToLookupTable(result, {lightSetIdx, newLightIdx});

		// call components to register this light
		for (auto& comp:lightSet->_boundComponents)
			comp->RegisterLight(lightSetIdx, newLightIdx, *newLight);

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

		for (const auto& comp:set->_boundComponents)
			comp->DeregisterLight(i->second._lightSet, i->second._lightIndex);
		set->_baseData.Deallocate(i->second._lightIndex);

#if 0
		// Also destroy a shadow projection associated with this light, if it exists
		if (set->_shadowOperatorId != ~0u) {
			auto i = std::find_if(
				_dynamicShadowProjections.begin(), _dynamicShadowProjections.end(),
				[sourceId](const auto& c) { return c._lightId == sourceId; });
			if (i != _dynamicShadowProjections.end())
				_dynamicShadowProjections.erase(i);
		}
#endif

		if (i->second._lightSet == s_dominantLightSetIdx)
			_dominantLightId = ~0u;

		_lookupTable.erase(i);
	}

	void* StandardLightScene::TryGetShadowProjectionInterface(ShadowProjectionId preparerId, uint64_t interfaceTypeCode)
	{
		return TryGetLightSourceInterface(preparerId, interfaceTypeCode);
		/*
		// test components first
		for (auto& comp:set._boundComponents)
			if (void* interf = comp->QueryInterface(i->second._setIndex, i->second._lightIndex, interfaceTypeCode))
				return interf;

		auto i = std::find_if(
			_dynamicShadowProjections.begin(), _dynamicShadowProjections.end(),
			[preparerId](const auto& c) { return c._id == preparerId; });
		if (i != _dynamicShadowProjections.end())
			return i->_desc->QueryInterface(interfaceTypeCode);

		if (_dominantShadowProjection._id == preparerId)
			return _dominantShadowProjection._desc->QueryInterface(interfaceTypeCode);

		return nullptr;
		*/
	}

	void StandardLightScene::ChangeLightSet(
		std::vector<std::pair<LightSourceId, LightSetAndIndex>>::iterator i,
		unsigned dstSetIdx)
	{
		auto* dstSet = &_tileableLightSets[dstSetIdx];
		auto* srcSet = &_tileableLightSets[i->second._lightSet];
		assert(dstSetIdx != i->second._lightSet);

		auto l = std::move(srcSet->_baseData.GetObject(i->second._lightIndex));
		for (const auto& comp:srcSet->_boundComponents)
			comp->DeregisterLight(i->second._lightSet, i->second._lightIndex);
		srcSet->_baseData.Deallocate(i->second._lightIndex);

		auto newLight = dstSet->_baseData.Allocate();
		*newLight = std::move(l);
		auto newLightIdx = newLight.GetIndex();
		for (const auto& comp:dstSet->_boundComponents)
			comp->RegisterLight(dstSetIdx, newLightIdx, *newLight);

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
	
#if 0
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
#endif

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

#if 0
	void StandardLightScene::DestroyShadowProjection(ShadowProjectionId preparerId)
	{
		assert(0);
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
#endif

	auto StandardLightScene::GetLightSet(LightOperatorId lightOperator, ShadowOperatorId shadowOperator) -> unsigned
	{
		for (auto s=_tileableLightSets.begin(); s!=_tileableLightSets.end(); ++s)
			if (s->_operatorId == lightOperator && s->_shadowOperatorId == shadowOperator)
				return std::distance(_tileableLightSets.begin(), s);
		_tileableLightSets.push_back(LightSet{lightOperator, shadowOperator});
		auto newSetIdx =  (unsigned)_tileableLightSets.size()-1;
		auto& newSet = _tileableLightSets.back();
		newSet._boundComponents.reserve(_components.size());
		for (auto& comp:_components)
			if (comp->BindToSet(newSet._operatorId, newSet._shadowOperatorId, newSetIdx))
				newSet._boundComponents.push_back(comp);
		return newSetIdx;
	}

	void StandardLightScene::Clear()
	{
		// we have to clear components, because we don't actually remove all lights from the components
		for (auto& set:_tileableLightSets) {
			set._baseData = {};
			set._boundComponents.clear();
		}
		_dominantLightSet._baseData = {};
		_dominantLightSet._boundComponents.clear();
		_dominantLightId = ~0u;
		_components.clear();
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

	void StandardLightScene::RegisterComponent(std::shared_ptr<ILightSceneComponent> comp)
	{
		_components.push_back(std::move(comp));
		auto& newComp = _components.back();

		for (unsigned setIdx=0; setIdx<_tileableLightSets.size(); ++setIdx) {
			auto& set = _tileableLightSets[setIdx];
			if (newComp->BindToSet(set._operatorId, set._shadowOperatorId, setIdx)) {
				set._boundComponents.push_back(newComp);
				for (auto i=set._baseData.begin(); i!=set._baseData.end(); ++i)
					newComp->RegisterLight(setIdx, i.GetIndex(), *i);
			}
		}
	}

	void StandardLightScene::DeregisterComponent(ILightSceneComponent& comp)
	{
		for (auto& set:_tileableLightSets)
			for (auto i=set._boundComponents.begin(); i!=set._boundComponents.end(); ++i) {
				if (i->get() == &comp) set._boundComponents.erase(i);
				break;
			}

		for (auto i=_components.begin(); i!=_components.end(); ++i) {
			_components.erase(i);
			break;
		}
	}

	StandardLightScene::StandardLightScene()
	{}
	StandardLightScene::~StandardLightScene()
	{}

	ILightBase::~ILightBase() {}
	ILightSceneComponent::~ILightSceneComponent() {}
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
}}
