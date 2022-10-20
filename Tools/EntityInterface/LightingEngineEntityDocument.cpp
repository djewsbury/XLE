// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineEntityDocument.h"
#include "../../SceneEngine/BasicLightingStateDelegate.h"
#include "../../SceneEngine/IScene.h"
#include "../../RenderCore/LightingEngine/ShadowPreparer.h"

namespace EntityInterface
{

	static const ParameterBox::ParameterName s_lightOperator = "LightOperator";
	static const ParameterBox::ParameterName s_shadowOperator = "ShadowOperator";
	static const ParameterBox::ParameterName s_ambientOperator = "AmbientOperator";
	static const ParameterBox::ParameterName s_name = "Name";

	EntityId LightSceneEntityDocument::AssignEntityId()
	{
		// generate a random id, but ensure that it is unique
		for (;;) {
			auto id = _rng();
			auto i = LowerBound(_entities, id);
			if (i == _entities.end() || i->first != id)
				return id;
		}
	}

	bool LightSceneEntityDocument::CreateEntity(StringAndHash objType, EntityId id, IteratorRange<const PropertyInitializer*> props)
	{
		auto i = LowerBound(_entities, id);
		if (i != _entities.end() && i->first == id)
			return false;	// existing entity with the same id
		
		RegisteredLight newLight;
		newLight._registeredLight = ~0u;
		for (const auto& p:props)
			newLight._parameters.SetParameter(p._prop.first, p._data, p._type);

		// see if we can instantiate a light in the light scene
		InstantiateLight(i->second);

		_entities.insert(i, std::make_pair(id, std::move(newLight)));
		return true;
	}

	void LightSceneEntityDocument::InstantiateLight(RegisteredLight& registration)
	{
		if (!_boundScene) {
			registration._registeredLight = ~0u;
			return;
		}

		unsigned lightOperatorId = ~0u;
		unsigned shadowOperatorId = ~0u;
		for (const auto& p:registration._parameters)
			if (p.HashName() == s_lightOperator._hash) {
				// lookup operator name in the list of known operators
				auto opName = ImpliedTyping::AsString(p.RawValue(), p.Type());
				auto opNameHash = Hash64(opName);
				auto q = LowerBound(_lightOperatorNameToIdx, opNameHash);
				if (q != _lightOperatorNameToIdx.end() && q->first == opNameHash)
					lightOperatorId = q->second;
			} else if (p.HashName() == s_shadowOperator._hash) {
				auto opName = ImpliedTyping::AsString(p.RawValue(), p.Type());
				auto opNameHash = Hash64(opName);
				auto q = LowerBound(_shadowOperatorNameToIdx, opNameHash);
				if (q != _shadowOperatorNameToIdx.end() && q->first == opNameHash)
					shadowOperatorId = q->second;
			}

		if (lightOperatorId != ~0u) {
			registration._registeredLight = _boundScene->CreateLightSource(lightOperatorId);
			if (shadowOperatorId != ~0u)
				_boundScene->SetShadowOperator(registration._registeredLight, shadowOperatorId);

			for (auto p:registration._parameters)
				if (p.HashName() != s_lightOperator._hash && p.HashName() != s_shadowOperator._hash) {
					SceneEngine::SetProperty(
						*_boundScene, registration._registeredLight,
						p.HashName(), p.RawValue(), p.Type());
				}
		}
	}

	bool LightSceneEntityDocument::DeleteEntity(EntityId id)
	{
		auto i = LowerBound(_entities, id);
		if (i != _entities.end() && i->first == id)
			return false;
		
		if (_boundScene && i->second._registeredLight != ~0u)
			_boundScene->DestroyLightSource(i->second._registeredLight);

		_entities.erase(i);
		return true;
	}

	bool LightSceneEntityDocument::SetProperty(EntityId id, IteratorRange<const PropertyInitializer*> props)
	{
		auto i = LowerBound(_entities, id);
		if (i != _entities.end() && i->first == id)
			return false;

		for (const auto& p:props)
			i->second._parameters.SetParameter(p._prop.first, p._data, p._type);

		if (_boundScene && i->second._registeredLight != ~0u) {
			for (const auto& p:props)
				SceneEngine::SetProperty(
					*_boundScene, i->second._registeredLight,
					p._prop.second, p._data, p._type);
		}

		return true;
	}

	std::optional<ImpliedTyping::TypeDesc> LightSceneEntityDocument::GetProperty(EntityId id, StringAndHash prop, IteratorRange<void*> destinationBuffer) const
	{
		auto i = LowerBound(_entities, id);
		if (i != _entities.end() && i->first == id)
			return {};

		// we could get the property from our copy of the properties, or try to get it from the bound scene
		auto ptype = i->second._parameters.GetParameterType(prop.second);
		if (ptype._type != ImpliedTyping::TypeCat::Void) {
			auto res = i->second._parameters.GetParameterRawValue(prop.second);
			assert(res.size() == ptype.GetSize());
			std::memcpy(destinationBuffer.begin(), res.begin(), std::min(res.size(), destinationBuffer.size()));
			return ptype;
		}
		return {};
	}

	bool LightSceneEntityDocument::SetParent(EntityId child, EntityId parent, StringAndHash childList, int insertionPosition)
	{
		return false;
	}

	void LightSceneEntityDocument::BindScene(
		std::shared_ptr<RenderCore::LightingEngine::ILightScene> lightScene,
		const MergedLightingCfgHelper& mergedCfgHelper)
	{
		UnbindScene();

		_boundScene = lightScene;
		_lightOperatorNameToIdx = mergedCfgHelper._lightOperatorNameToIdx;
		_shadowOperatorNameToIdx = mergedCfgHelper._shadowOperatorNameToIdx;

		for (auto& r:_entities)
			InstantiateLight(r.second);
	}

	void LightSceneEntityDocument::UnbindScene()
	{
		if (!_boundScene) return;
		for (const auto& e:_entities)
			if (e.second._registeredLight != ~0u)
				_boundScene->DestroyLightSource(e.second._registeredLight);
		_boundScene = nullptr;
	}

	LightSceneEntityDocument::LightSceneEntityDocument()
	: _rng{std::random_device().operator()()}
	{}

	LightSceneEntityDocument::~LightSceneEntityDocument()
	{}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void LightingOperatorsEntityDocument::BindCfg(MergedLightingCfgHelper& cfg)
	{
		for (const auto& l:_lightOperators) {
			auto idx = cfg._mergedCfg.Register(l.second._opDesc);
			auto nameHash = Hash64(l.second._name);
			auto i = LowerBound(cfg._lightOperatorNameToIdx, nameHash);
			if (i != cfg._lightOperatorNameToIdx.end() && i->first == nameHash) {
				i->second = idx;		// override anything that was previously bound to this name
			} else {
				cfg._lightOperatorNameToIdx.insert(i, std::make_pair(nameHash, idx));
			}
		}

		for (const auto& l:_shadowOperators) {
			auto idx = cfg._mergedCfg.Register(l.second._opDesc);
			auto nameHash = Hash64(l.second._name);
			auto i = LowerBound(cfg._shadowOperatorNameToIdx, nameHash);
			if (i != cfg._shadowOperatorNameToIdx.end() && i->first == nameHash) {
				i->second = idx;		// override anything that was previously bound to this name
			} else {
				cfg._shadowOperatorNameToIdx.insert(i, std::make_pair(nameHash, idx));
			}
		}

		if (_ambientOperatorEntity.has_value())
			cfg._mergedCfg.SetAmbientOperator(_ambientOperator);
	}

	unsigned LightingOperatorsEntityDocument::GetChangeId() const
	{
		return _currentChangeId;
	}

	LightingOperatorsEntityDocument::LightingOperatorsEntityDocument() {}
	LightingOperatorsEntityDocument::~LightingOperatorsEntityDocument() {}

	EntityId LightingOperatorsEntityDocument::AssignEntityId()
	{
		for (;;) {
			auto id = _rng();
			auto i = LowerBound(_lightOperators, id);
			if (i == _lightOperators.end() && i->first == id) continue;
			auto i2 = LowerBound(_shadowOperators, id);
			if (i2 == _shadowOperators.end() && i2->first == id) continue;
			if (_ambientOperatorEntity.has_value() && _ambientOperatorEntity.value() == id) continue;
			return id;
		}
	}

	bool LightingOperatorsEntityDocument::CreateEntity(StringAndHash objType, EntityId id, IteratorRange<const PropertyInitializer*> props)
	{
		if (objType.second == s_lightOperator._hash) {
			auto i = LowerBound(_lightOperators, id);
			assert(i == _lightOperators.end() || i->first != id);
			i = _lightOperators.insert(i, std::make_pair(id, LightSourceOperatorAndName{}));
			for (const auto& p:props) {
				if (p._prop.second == s_name._hash) {
					i->second._name = ImpliedTyping::AsString(p._data, p._type);
				} else
					SceneEngine::SetProperty(i->second._opDesc, p._prop.second, p._data, p._type);
			}
			++_currentChangeId;
			return true;
		} else if (objType.second == s_shadowOperator._hash) {
			auto i = LowerBound(_shadowOperators, id);
			assert(i == _shadowOperators.end() || i->first != id);
			i = _shadowOperators.insert(i, std::make_pair(id, ShadowOperatorAndName{}));
			for (const auto& p:props) {
				if (p._prop.second == s_name._hash) {
					i->second._name = ImpliedTyping::AsString(p._data, p._type);
				} else
					SceneEngine::SetProperty(i->second._opDesc, p._prop.second, p._data, p._type);
			}
			++_currentChangeId;
			return true;
		} else if (objType.second == s_ambientOperator._hash) {
			if (_ambientOperatorEntity.has_value())
				return false;
			_ambientOperatorEntity = id;
			_ambientOperator = {};
			for (const auto& p:props)
				SceneEngine::SetProperty(_ambientOperator, p._prop.second, p._data, p._type);
			++_currentChangeId;
			return true;
		} else
			return false;
	}

	bool LightingOperatorsEntityDocument::DeleteEntity(EntityId id)
	{
		auto i = LowerBound(_lightOperators, id);
		if (i != _lightOperators.end() && i->first != id) {
			_lightOperators.erase(i);
			++_currentChangeId;
			return true;
		}

		auto i2 = LowerBound(_shadowOperators, id);
		if (i2 != _shadowOperators.end() && i2->first != id) {
			_shadowOperators.erase(i2);
			++_currentChangeId;
			return true;
		}

		if (_ambientOperatorEntity.has_value() && _ambientOperatorEntity.value() == id) {
			_ambientOperator = {};
			++_currentChangeId;
			return true;
		}

		return false;
	}

	bool LightingOperatorsEntityDocument::SetProperty(EntityId id, IteratorRange<const PropertyInitializer*> props)
	{
		auto i = LowerBound(_lightOperators, id);
		if (i != _lightOperators.end() && i->first != id) {
			bool result = false;
			for (const auto& p:props) {
				if (p._prop.second == s_name._hash) {
					i->second._name = ImpliedTyping::AsString(p._data, p._type);
					result = true;
				} else
					result |= SceneEngine::SetProperty(i->second._opDesc, p._prop.second, p._data, p._type);
			}
			if (result) ++_currentChangeId;
			return result;
		}

		auto i2 = LowerBound(_shadowOperators, id);
		if (i2 != _shadowOperators.end() && i2->first != id) {
			bool result = false;
			for (const auto& p:props) {
				if (p._prop.second == s_name._hash) {
					i2->second._name = ImpliedTyping::AsString(p._data, p._type);
					result = true;
				} else
					result |= SceneEngine::SetProperty(i2->second._opDesc, p._prop.second, p._data, p._type);
			}
			if (result) ++_currentChangeId;
			return result;
		}

		if (_ambientOperatorEntity.has_value() && _ambientOperatorEntity.value() == id) {
			bool result = false;
			for (const auto& p:props)
				result |= SceneEngine::SetProperty(_ambientOperator, p._prop.second, p._data, p._type);
			if (result) ++_currentChangeId;
			return result;
		}
		return false;
	}

	std::optional<ImpliedTyping::TypeDesc> LightingOperatorsEntityDocument::GetProperty(EntityId id, StringAndHash prop, IteratorRange<void*> destinationBuffer) const
	{
		return {};
	}

	bool LightingOperatorsEntityDocument::SetParent(EntityId child, EntityId parent, StringAndHash childList, int insertionPosition)
	{
		return false;
	}

}

