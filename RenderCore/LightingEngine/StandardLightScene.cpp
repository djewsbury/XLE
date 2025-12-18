// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StandardLightScene.h"
#include "StandardLightOperators.h"
#include "ILightScene.h"
#include "../../Assets/DepVal.h"

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	struct StandardLightScene::LightSet
	{
		LightOperatorId _operatorId = ~0u;
		PageHeap<StandardPositionalLight> _baseData;
		std::vector<std::shared_ptr<ILightSceneComponent>> _boundComponents;
		StandardPositionLightFlags::BitField _flags = 0;

		void* QueryInterface(unsigned lightIdx, uint64_t interfaceTypeCode)
		{
			switch (interfaceTypeCode) {
			case TypeHashCode<IPositionalLightSource>: return (IPositionalLightSource*)&_baseData.GetObject(lightIdx);
			case TypeHashCode<IUniformEmittance>: return (IUniformEmittance*)&_baseData.GetObject(lightIdx);
			case TypeHashCode<IFiniteLightSource>:
				if (_flags & StandardPositionLightFlags::SupportFiniteRange)
					return (IFiniteLightSource*)&_baseData.GetObject(lightIdx);
				break;
			}
			return nullptr;
		}

		LightSet(LightOperatorId operatorId) : _operatorId(operatorId) {}
	};

	auto StandardLightScene::TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode) -> void*
	{
		auto i = LowerBound(_lookupTable, sourceId);
		if (i == _lookupTable.end() || i->first != sourceId) return nullptr;		// not found

		auto& set = *_lightSets[i->second._lightSet];
		assert(set._baseData._allocationFlags.IsAllocated(i->second._lightIndex));

		// test components first
		for (auto& comp:set._boundComponents)
			if (void* interf = comp->QueryInterface(i->second._lightSet, i->second._lightIndex, interfaceTypeCode))
				return interf;

		// fallback to the ILightBase
		return set.QueryInterface(i->second._lightIndex, interfaceTypeCode);
	}

	auto StandardLightScene::CreateLightSource(LightOperatorId operatorId) -> LightSourceId
	{
		auto result = _nextLightSource++;
		auto lightSetIdx = GetLightSet(operatorId);
		auto& lightSet = *_lightSets[lightSetIdx];

		auto newLight = lightSet._baseData.Allocate();
		auto newLightIdx = newLight.GetIndex();
		AddToLookupTable(result, {lightSetIdx, newLightIdx});

		// call components to register this light
		for (auto& comp:lightSet._boundComponents)
			comp->RegisterLight(lightSetIdx, newLightIdx);

		return result;
	}

	void StandardLightScene::DestroyLightSource(LightSourceId sourceId)
	{
		auto i = LowerBound(_lookupTable, sourceId);
		if (i == _lookupTable.end() || i->first != sourceId) {
			assert(0);
			return;		// not found
		}

		auto& set = *_lightSets[i->second._lightSet];

		for (const auto& comp:set._boundComponents)
			comp->DeregisterLight(i->second._lightSet, i->second._lightIndex);
		set._baseData.Deallocate(i->second._lightIndex);

		_lookupTable.erase(i);
	}

	void StandardLightScene::ChangeLightSet(
		std::vector<std::pair<LightSourceId, LightSetAndIndex>>::iterator i,
		unsigned dstSetIdx)
	{
		auto& dstSet = *_lightSets[dstSetIdx];
		auto& srcSet = *_lightSets[i->second._lightSet];
		assert(dstSetIdx != i->second._lightSet);

		auto l = std::move(srcSet._baseData.GetObject(i->second._lightIndex));
		for (const auto& comp:srcSet._boundComponents)
			comp->DeregisterLight(i->second._lightSet, i->second._lightIndex);
		srcSet._baseData.Deallocate(i->second._lightIndex);

		auto newLight = dstSet._baseData.Allocate();
		*newLight = std::move(l);
		auto newLightIdx = newLight.GetIndex();
		for (const auto& comp:dstSet._boundComponents)
			comp->RegisterLight(dstSetIdx, newLightIdx);

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

	void StandardLightScene::ChangeOperatorId(IteratorRange<const LightSourceId*> lights, LightOperatorId shadowOperatorId)
	{
		// we could potentially do some optimizations here by sorting "lights" or by handling lights on a set by set basis
		for (auto lightId:lights) {
			auto i = LowerBound(_lookupTable, lightId);
			if (i == _lookupTable.end() || i->first != lightId) {
				assert(0);
				continue;
			}

			auto& srcSet = *_lightSets[i->second._lightSet];
			auto dstSetIdx = GetLightSet(srcSet._operatorId);
			if (i->second._lightSet == dstSetIdx) continue;	// no actual change

			ChangeLightSet(i, dstSetIdx);
		}
	}

	auto StandardLightScene::GetLightSet(LightOperatorId lightOperator) -> unsigned
	{
		for (auto& s:_lightSets)
			if (s->_operatorId == lightOperator)
				return (unsigned)(&s-_lightSets.data());
		_lightSets.emplace_back(std::make_unique<LightSet>(lightOperator));
		auto newSetIdx =  (unsigned)_lightSets.size()-1;
		auto& newSet = *_lightSets.back();
		newSet._boundComponents.reserve(_components.size());
		for (auto& comp:_components) {

			auto qi = [set=&newSet, setIdx=newSetIdx, cCount=newSet._boundComponents.size()](ILightScene::LightSourceId lightIdx, uint64_t code) -> void* {
				for (unsigned c=0; c<cCount; ++c)
					if (auto res = set->_boundComponents[cCount-1-c]->QueryInterface(setIdx, lightIdx, code))
						return res;
				return set->QueryInterface(lightIdx, code);
			};

			if (comp->BindToSet(newSet._operatorId, newSetIdx, std::move(qi)))
				newSet._boundComponents.push_back(comp);
		}

		for (auto& a:_associatedFlags)
			if (a.first == lightOperator)
				newSet._flags |= a.second;
		return newSetIdx;
	}

	void StandardLightScene::Clear()
	{
		// we have to clear components, because we don't actually remove all lights from the components
		for (auto& set:_lightSets) {
			set->_baseData = {};
			set->_boundComponents.clear();
		}
		_components.clear();
	}

	void StandardLightScene::ReserveLightSourceIds(unsigned idCount)
	{
		_nextLightSource += idCount;
	}

	void* StandardLightScene::QueryInterface(uint64_t typeCode)
	{
		switch (typeCode) {
		case TypeHashCode<StandardLightScene>:
			return this;
		default:
			return nullptr;
		}		
	}

	void StandardLightScene::RegisterComponent(std::shared_ptr<ILightSceneComponent> comp)
	{
		_components.push_back(std::move(comp));
		auto& newComp = _components.back();

		for (unsigned setIdx=0; setIdx<_lightSets.size(); ++setIdx) {
			auto& set = _lightSets[setIdx];

			auto qi = [set=set.get(), setIdx, cCount=set->_boundComponents.size()](ILightScene::LightSourceId lightIdx, uint64_t code) -> void* {
				for (unsigned c=0; c<cCount; ++c)
					if (auto res = set->_boundComponents[cCount-1-c]->QueryInterface(setIdx, lightIdx, code))
						return res;
				return set->QueryInterface(lightIdx, code);
			};

			if (newComp->BindToSet(set->_operatorId, setIdx, std::move(qi))) {
				set->_boundComponents.push_back(newComp);
				for (auto i=set->_baseData.begin(); i!=set->_baseData.end(); ++i)
					newComp->RegisterLight(setIdx, i.GetIndex());
			}
		}
	}

	void StandardLightScene::DeregisterComponent(ILightSceneComponent& comp)
	{
		for (auto& set:_lightSets)
			for (auto i=set->_boundComponents.begin(); i!=set->_boundComponents.end(); ++i) {
				if (i->get() == &comp) set->_boundComponents.erase(i);
				break;
			}

		for (auto i=_components.begin(); i!=_components.end(); ++i) {
			_components.erase(i);
			break;
		}
	}

	void StandardLightScene::AssociateFlag(LightOperatorId operatorId, StandardPositionLightFlags::BitField flag)
	{
		auto i = _associatedFlags.begin();
		for (;i!=_associatedFlags.end(); ++i) if (i->first == operatorId) break;
		if (i != _associatedFlags.end()) i->second |= flag;
		else _associatedFlags.emplace_back(operatorId, flag);

		for (auto& set:_lightSets)
			if (set->_operatorId == operatorId)
				set->_flags |= flag;
	}

	::Assets::DependencyValidation StandardLightScene::GetDependencyValidation() const { return {}; }

	StandardLightScene::StandardLightScene()
	{}
	StandardLightScene::~StandardLightScene()
	{}

	ILightBase::~ILightBase() = default;
	ILightSceneComponent::~ILightSceneComponent() = default;

}}}

namespace RenderCore { namespace LightingEngine
{
	uint64_t PositionalLightOperatorDesc::GetHash(uint64_t seed) const
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
	IDepthTextureResolve::~IDepthTextureResolve() {}
	IArbitraryShadowProjections::~IArbitraryShadowProjections() {}
	IOrthoShadowProjections::~IOrthoShadowProjections() {}
	INearShadowProjection::~INearShadowProjection() {}
}}
