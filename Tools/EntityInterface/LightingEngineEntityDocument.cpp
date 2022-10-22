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
	static const ParameterBox::ParameterName s_envSettings = "EnvSettings";
	static const ParameterBox::ParameterName s_directionalLight = "DirectionalLight";
	static const ParameterBox::ParameterName s_areaLight = "AreaLight";
	static const ParameterBox::ParameterName s_name = "Name";

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct MultiEnvironmentSettingsDocument::LightSourceOperatorAndName
	{
		std::string _name;
		EnvSettingsId _container = ~0ull;
		RenderCore::LightingEngine::LightSourceOperatorDesc _opDesc;
	};
	struct MultiEnvironmentSettingsDocument::ShadowOperatorAndName
	{
		std::string _name;
		EnvSettingsId _container = ~0ull;
		RenderCore::LightingEngine::ShadowOperatorDesc _opDesc;
	};
	struct MultiEnvironmentSettingsDocument::AmbientOperatorAndName
	{
		std::string _name;
		EnvSettingsId _container = ~0ull;
		RenderCore::LightingEngine::AmbientLightOperatorDesc _opDesc;
	};
	struct MultiEnvironmentSettingsDocument::EnvSettingContainer
	{
		std::string _name;
		unsigned _changeId = 0;
	};

	void MultiEnvironmentSettingsDocument::BindCfg(EnvSettingsId envSettings, MergedLightingCfgHelper& cfg)
	{
		for (const auto& l:_lightOperators) {
			if (l.second._container != envSettings) continue;
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
			if (l.second._container != envSettings) continue;
			auto idx = cfg._mergedCfg.Register(l.second._opDesc);
			auto nameHash = Hash64(l.second._name);
			auto i = LowerBound(cfg._shadowOperatorNameToIdx, nameHash);
			if (i != cfg._shadowOperatorNameToIdx.end() && i->first == nameHash) {
				i->second = idx;		// override anything that was previously bound to this name
			} else {
				cfg._shadowOperatorNameToIdx.insert(i, std::make_pair(nameHash, idx));
			}
		}

		for (const auto& l:_ambientOperators) {
			if (l.second._container != envSettings) continue;
			cfg._mergedCfg.SetAmbientOperator(l.second._opDesc);
		}
	}

	unsigned MultiEnvironmentSettingsDocument::GetChangeId(EnvSettingsId envSettings) const
	{
		auto i = LowerBound(_envSettingContainers, envSettings);
		if (i != _envSettingContainers.end() && i->first == envSettings)
			return i->second._changeId;
		return 0u;
	}

	MultiEnvironmentSettingsDocument::MultiEnvironmentSettingsDocument() {}
	MultiEnvironmentSettingsDocument::~MultiEnvironmentSettingsDocument() {}

	EntityId MultiEnvironmentSettingsDocument::AssignEntityId()
	{
		for (;;) {
			auto id = _rng();
			auto i = LowerBound(_lightOperators, id);
			if (i == _lightOperators.end() && i->first == id) continue;
			auto i2 = LowerBound(_shadowOperators, id);
			if (i2 == _shadowOperators.end() && i2->first == id) continue;
			auto i3 = LowerBound(_ambientOperators, id);
			if (i3 == _ambientOperators.end() && i3->first == id) continue;
			auto i4 = LowerBound(_envSettingContainers, id);
			if (i4 == _envSettingContainers.end() && i4->first == id) continue;
			return id;
		}
	}

	bool MultiEnvironmentSettingsDocument::CreateEntity(StringAndHash objType, EntityId id, IteratorRange<const PropertyInitializer*> props)
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
			return true;
		} else if (objType.second == s_ambientOperator._hash) {
			auto i = LowerBound(_ambientOperators, id);
			assert(i == _ambientOperators.end() || i->first != id);
			i = _ambientOperators.insert(i, std::make_pair(id, AmbientOperatorAndName{}));
			for (const auto& p:props)
				SceneEngine::SetProperty(i->second._opDesc, p._prop.second, p._data, p._type);
			return true;
		} else if (objType.second == s_envSettings._hash) {
			auto i = LowerBound(_envSettingContainers, id);
			assert(i == _envSettingContainers.end() || i->first != id);
			i = _envSettingContainers.insert(i, std::make_pair(id, EnvSettingContainer{}));
			for (const auto& p:props)
				if (p._prop.second == s_name._hash)
					i->second._name = ImpliedTyping::AsString(p._data, p._type);
			return true;
		} else if (objType.second == s_directionalLight._hash || objType.second == s_areaLight._hash) {
			auto i = LowerBound(_lights, id);
			assert(i == _lights.end() || i->first != id);
			RegisteredLight newLight;
			newLight._registeredLight = ~0u;
			for (const auto& p:props)
				newLight._parameters.SetParameter(p._prop.first, p._data, p._type);
			_lights.insert(i, std::make_pair(id, std::move(newLight)));
			return true;
		} else
			return false;
	}

	bool MultiEnvironmentSettingsDocument::DeleteEntity(EntityId id)
	{
		auto i = LowerBound(_lightOperators, id);
		if (i != _lightOperators.end() && i->first == id) {
			IncreaseChangeId(i->second._container);
			_lightOperators.erase(i);
			return true;
		}

		auto i2 = LowerBound(_shadowOperators, id);
		if (i2 != _shadowOperators.end() && i2->first == id) {
			IncreaseChangeId(i2->second._container);
			_shadowOperators.erase(i2);
			return true;
		}

		auto i3 = LowerBound(_ambientOperators, id);
		if (i3 != _ambientOperators.end() && i3->first == id) {
			IncreaseChangeId(i3->second._container);
			_ambientOperators.erase(i3);
			return true;
		}

		auto i4 = LowerBound(_envSettingContainers, id);
		if (i4 != _envSettingContainers.end() && i4->first == id) {
			_envSettingContainers.erase(i4);

			// unbind any objects from this env settings
			for (auto& l:_lightOperators) if (l.second._container == id) l.second._container = ~0ull;
			for (auto& l:_shadowOperators) if (l.second._container == id) l.second._container = ~0ull;
			for (auto& l:_ambientOperators) if (l.second._container == id) l.second._container = ~0ull;
			return true;
		}

		auto i5 = LowerBound(_lights, id);
		if (i5 != _lights.end() && i5->first == id) {
			DeinstantiateLight(i5->second);
			_lights.erase(i5);
			return true;
		}

		return false;
	}

	bool MultiEnvironmentSettingsDocument::SetProperty(EntityId id, IteratorRange<const PropertyInitializer*> props)
	{
		auto i = LowerBound(_lightOperators, id);
		if (i != _lightOperators.end() && i->first == id) {
			bool result = false;
			for (const auto& p:props) {
				if (p._prop.second == s_name._hash) {
					i->second._name = ImpliedTyping::AsString(p._data, p._type);
					result = true;
				} else
					result |= SceneEngine::SetProperty(i->second._opDesc, p._prop.second, p._data, p._type);
			}
			if (result) IncreaseChangeId(i->second._container);
			return result;
		}

		auto i2 = LowerBound(_shadowOperators, id);
		if (i2 != _shadowOperators.end() && i2->first == id) {
			bool result = false;
			for (const auto& p:props) {
				if (p._prop.second == s_name._hash) {
					i2->second._name = ImpliedTyping::AsString(p._data, p._type);
					result = true;
				} else
					result |= SceneEngine::SetProperty(i2->second._opDesc, p._prop.second, p._data, p._type);
			}
			if (result) IncreaseChangeId(i2->second._container);
			return result;
		}

		auto i3 = LowerBound(_ambientOperators, id);
		if (i3 != _ambientOperators.end() && i3->first == id) {
			bool result = false;
			for (const auto& p:props)
				result |= SceneEngine::SetProperty(i3->second._opDesc, p._prop.second, p._data, p._type);
			if (result) IncreaseChangeId(i3->second._container);
			return result;
		}

		auto i4 = LowerBound(_envSettingContainers, id);
		if (i4 != _envSettingContainers.end() && i4->first == id) {
			bool result = false;
			for (const auto& p:props)
				if (p._prop.second == s_name._hash) {
					i4->second._name = ImpliedTyping::AsString(p._data, p._type);
					result = true;
				}
			return result;
		}

		auto i5 = LowerBound(_lights, id);
		if (i5 != _lights.end() && i5->first == id) {
			for (const auto& p:props)
				i5->second._parameters.SetParameter(p._prop.first, p._data, p._type);

			if (i5->second._registeredLight != ~0u) {
				auto boundScene = LowerBound(_boundScenes, i5->second._container);
				if (boundScene != _boundScenes.end() && boundScene->first == i5->second._container) {
					for (const auto& p:props)
						SceneEngine::SetProperty(
							*boundScene->second._boundScene, i5->second._registeredLight,
							p._prop.second, p._data, p._type);
				}
			}
		}

		return false;
	}

	void MultiEnvironmentSettingsDocument::IncreaseChangeId(EnvSettingsId envSettings)
	{
		if (envSettings == ~0ull) return;
		auto i = LowerBound(_envSettingContainers, envSettings);
		if (i != _envSettingContainers.end() && i->first == envSettings)
			++i->second._changeId;
	}

	std::optional<ImpliedTyping::TypeDesc> MultiEnvironmentSettingsDocument::GetProperty(EntityId id, StringAndHash prop, IteratorRange<void*> destinationBuffer) const
	{
		auto i5 = LowerBound(_lights, id);
		if (i5 != _lights.end() && i5->first == id) {
			// we could get the property from our copy of the properties, or try to get it from the bound scene
			auto ptype = i5->second._parameters.GetParameterType(prop.second);
			if (ptype._type != ImpliedTyping::TypeCat::Void) {
				auto res = i5->second._parameters.GetParameterRawValue(prop.second);
				assert(res.size() == ptype.GetSize());
				std::memcpy(destinationBuffer.begin(), res.begin(), std::min(res.size(), destinationBuffer.size()));
				return ptype;
			}
		}

		return {};
	}

	bool MultiEnvironmentSettingsDocument::SetParent(EntityId child, EntityId parent, StringAndHash childList, int insertionPosition)
	{
		auto i4 = LowerBound(_envSettingContainers, parent);
		if (i4 == _envSettingContainers.end() || i4->first != parent)
			return false;

		auto i = LowerBound(_lightOperators, child);
		if (i != _lightOperators.end() && i->first == child) {
			if (i->second._container != parent) {
				IncreaseChangeId(i->second._container);
				i->second._container = parent;
				IncreaseChangeId(i->second._container);
			}
			return true;
		}

		auto i2 = LowerBound(_shadowOperators, child);
		if (i2 != _shadowOperators.end() && i2->first == child) {
			if (i2->second._container != parent) {
				IncreaseChangeId(i2->second._container);
				i2->second._container = parent;
				IncreaseChangeId(i2->second._container);
			}
			return true;
		}

		auto i3 = LowerBound(_ambientOperators, child);
		if (i3 != _ambientOperators.end() && i3->first == child) {
			if (i3->second._container != parent) {
				IncreaseChangeId(i3->second._container);
				i3->second._container = parent;
				IncreaseChangeId(i3->second._container);
			}
			return true;
		}

		auto i5 = LowerBound(_lights, child);
		if (i5 != _lights.end() && i5->first == child) {
			if (i5->second._container != parent) {
				DeinstantiateLight(i5->second);
				i5->second._container = parent;
				InstantiateLight(i5->second);
			}
		}

		return false;
	}

	void MultiEnvironmentSettingsDocument::BindScene(
		EnvSettingsId envSettings,
		std::shared_ptr<RenderCore::LightingEngine::ILightScene> lightScene,
		const MergedLightingCfgHelper& mergedCfgHelper)
	{
		for (auto& l:_lights)
			if (l.second._container == envSettings)
				DeinstantiateLight(l.second);

		auto i = LowerBound(_boundScenes, envSettings);
		if (i != _boundScenes.end() && i->first == envSettings) {
			i->second._boundScene = std::move(lightScene);
			i->second._lightOperatorNameToIdx = mergedCfgHelper._lightOperatorNameToIdx;
			i->second._shadowOperatorNameToIdx = mergedCfgHelper._shadowOperatorNameToIdx;
		} else {
			BoundScene boundScene;
			boundScene._boundScene = std::move(lightScene);
			boundScene._lightOperatorNameToIdx = mergedCfgHelper._lightOperatorNameToIdx;
			boundScene._shadowOperatorNameToIdx = mergedCfgHelper._shadowOperatorNameToIdx;
			_boundScenes.insert(i, std::make_pair(envSettings, std::move(boundScene)));
		}

		for (auto& l:_lights)
			if (l.second._container == envSettings)
				InstantiateLight(l.second);
	}

	void MultiEnvironmentSettingsDocument::UnbindScene(RenderCore::LightingEngine::ILightScene& scene)
	{
		for (auto bs=_boundScenes.begin(); bs!=_boundScenes.end(); ++bs) {
			if (bs->second._boundScene.get() != &scene) continue;

			for (auto& l:_lights)
				if (l.second._container == bs->first)
					DeinstantiateLight(l.second);
			_boundScenes.erase(bs);
		}
	}

	void MultiEnvironmentSettingsDocument::InstantiateLight(RegisteredLight& registration)
	{
		if (registration._container == ~0ull) {
			registration._registeredLight = ~0u;
			return;
		}

		auto boundScene = LowerBound(_boundScenes, registration._container);
		if (boundScene == _boundScenes.end() || boundScene->first != registration._container) {
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
				auto q = LowerBound(boundScene->second._lightOperatorNameToIdx, opNameHash);
				if (q != boundScene->second._lightOperatorNameToIdx.end() && q->first == opNameHash)
					lightOperatorId = q->second;
			} else if (p.HashName() == s_shadowOperator._hash) {
				auto opName = ImpliedTyping::AsString(p.RawValue(), p.Type());
				auto opNameHash = Hash64(opName);
				auto q = LowerBound(boundScene->second._shadowOperatorNameToIdx, opNameHash);
				if (q != boundScene->second._shadowOperatorNameToIdx.end() && q->first == opNameHash)
					shadowOperatorId = q->second;
			}

		if (lightOperatorId == ~0u) {
			registration._registeredLight = ~0u;
			return;
		}

		registration._registeredLight = boundScene->second._boundScene->CreateLightSource(lightOperatorId);
		if (shadowOperatorId != ~0u)
			boundScene->second._boundScene->SetShadowOperator(registration._registeredLight, shadowOperatorId);

		for (auto p:registration._parameters)
			if (p.HashName() != s_lightOperator._hash && p.HashName() != s_shadowOperator._hash) {
				SceneEngine::SetProperty(
					*boundScene->second._boundScene, registration._registeredLight,
					p.HashName(), p.RawValue(), p.Type());
			}
	}

	void MultiEnvironmentSettingsDocument::DeinstantiateLight(RegisteredLight& registration)
	{
		if (registration._registeredLight == ~0u) return;

		if (registration._container == ~0ull) {
			registration._registeredLight = ~0u;
			return;
		}

		auto boundScene = LowerBound(_boundScenes, registration._container);
		if (boundScene == _boundScenes.end() || boundScene->first != registration._container) {
			registration._registeredLight = ~0u;
			return;
		}

		boundScene->second._boundScene->DestroyLightSource(registration._registeredLight);
		registration._registeredLight = ~0u;
	}


}

