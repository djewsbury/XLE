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
	static const ParameterBox::ParameterName s_distantIBL = "DistantIBL";
	static const ParameterBox::ParameterName s_name = "Name";
	static const ParameterBox::ParameterName s_packedColor = "PackedColor";
	static const ParameterBox::ParameterName s_brightnessScalar = "BrightnessScalar";

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

	void MultiEnvironmentSettingsDocument::PrepareCfg(EnvSettingsId envSettings, MergedLightingCfgHelper& cfg)
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

		// register "implicit" light operators
		for (const auto& l:_lights)
			if (l.second._explicitLightOperator.empty())
				cfg._mergedCfg.Register(l.second._impliedLightingOperator);
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
		} else if (objType.second == s_directionalLight._hash || objType.second == s_areaLight._hash || objType.second == s_distantIBL._hash) {
			auto i = LowerBound(_lights, id);
			assert(i == _lights.end() || i->first != id);
			RegisteredLight newLight;
			newLight._instantiatedLight = ~0u;
			for (const auto& p:props) {
				if (p._prop.second == s_lightOperator._hash) {
					newLight._explicitLightOperator = ImpliedTyping::AsString(p._data, p._type);
				} else if (p._prop.second == s_shadowOperator._hash) {
					newLight._explicitShadowOperator = ImpliedTyping::AsString(p._data, p._type);
				} else
					newLight._parameters.SetParameter(p._prop.first, p._data, p._type);
			}
			if (objType.second == s_distantIBL._hash)
				newLight._type = RegisteredLight::Type::DistantIBL;
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

	static bool SetSpecialProperty(
		RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId sourceId,
		uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type,
		const ParameterBox& pbox)
	{
		if (propertyNameHash == s_packedColor._hash) {
			uint32_t packedColor;
			if (ImpliedTyping::Cast(MakeOpaqueIteratorRange(packedColor), ImpliedTyping::TypeOf<uint32_t>(), data, type)) {
				if (auto brightnessScalar = pbox.GetParameter<float>(s_brightnessScalar)) {
					Float3 brightness = Float3 { (packedColor>>16) & 0xff, (packedColor>>8) & 0xff, packedColor & 0xff } / 255.f * brightnessScalar.value();
					auto* emittance = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(sourceId);
					if (emittance)
						emittance->SetBrightness(brightness);
				}
			}
			return true;
		} else if (propertyNameHash == s_brightnessScalar._hash) {
			float brightnessScalar;
			if (ImpliedTyping::Cast(MakeOpaqueIteratorRange(brightnessScalar), ImpliedTyping::TypeOf<float>(), data, type)) {
				if (auto packedColor = pbox.GetParameter<uint32_t>(s_packedColor)) {
					Float3 brightness = Float3 { (*packedColor>>16) & 0xff, (*packedColor>>8) & 0xff, *packedColor & 0xff } / 255.f * brightnessScalar;
					auto* emittance = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(sourceId);
					if (emittance)
						emittance->SetBrightness(brightness);
				}
			}
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
			bool changedOperator = false;
			for (const auto& p:props) {
				if (p._prop.second == s_lightOperator._hash) {
					auto newOperator = ImpliedTyping::AsString(p._data, p._type);
					if (newOperator != i5->second._explicitLightOperator) {
						i5->second._explicitLightOperator = newOperator;
						changedOperator = true;
					}
				} else if (p._prop.second == s_shadowOperator._hash) {
					auto newOperator = ImpliedTyping::AsString(p._data, p._type);
					if (newOperator != i5->second._explicitLightOperator) {
						i5->second._explicitShadowOperator = newOperator;
						changedOperator = true;
					}
				} else if (SceneEngine::SetProperty(i5->second._impliedLightingOperator, p._prop.second, p._data, p._type)) {
					// DiffuseModel
					// ShadowResolveModel
					// Shape
					// DominantLight
					// etc
					i5->second._parameters.SetParameter(p._prop.first, p._data, p._type);
					changedOperator = true;
				} else
					i5->second._parameters.SetParameter(p._prop.first, p._data, p._type);
			}

			if (i5->second._instantiatedLight != ~0u) {
				auto boundScene = LowerBound(_boundScenes, i5->second._container);
				if (boundScene != _boundScenes.end() && boundScene->first == i5->second._container) {
					if (changedOperator) {
						// destroy and recreate the light, because the operator changed
						DeinstantiateLight(i5->second);
						bool successful = InstantiateLight(i5->second);
						// If reinstantiation is not successful, it's because the new light operator is implicit and
						// hasn't already been registered in the scene. We increase change id to signal clients that
						// the technique must be rebuilt
						if (!successful) IncreaseChangeId(i5->second._container);
					} else {
						for (const auto& p:props) {
							if (!SetSpecialProperty(
								*boundScene->second._boundScene, i5->second._instantiatedLight,
								p._prop.second, p._data, p._type, i5->second._parameters)) {
								SceneEngine::SetProperty(
									*boundScene->second._boundScene, i5->second._instantiatedLight,
									p._prop.second, p._data, p._type);
							}
						}
					}
				}
			}
			return true;
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

				// If there's a bound scene, attempt to instantiate. If we get a failed instantiate
				// here, it means that the new light requires an operator that isn't registered -- we'll
				// have to rebuild the technique
				auto boundScene = LowerBound(_boundScenes, parent);
				if (boundScene != _boundScenes.end() && boundScene->first == parent) {
					bool successful = InstantiateLight(i5->second);
					if (!successful)
						IncreaseChangeId(i5->second._container);
				}
			}
			return true;
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
		if (i == _boundScenes.end() || i->first != envSettings)
			 i = _boundScenes.insert(i, std::make_pair(envSettings, BoundScene{}));

		i->second._boundScene = std::move(lightScene);
		i->second._lightOperatorNameToIdx = mergedCfgHelper._lightOperatorNameToIdx;
		i->second._shadowOperatorNameToIdx = mergedCfgHelper._shadowOperatorNameToIdx;
		i->second._lightOperatorHashes.clear();
		i->second._lightOperatorHashes.reserve(mergedCfgHelper._mergedCfg.GetLightOperators().size());
		for (const auto& o:mergedCfgHelper._mergedCfg.GetLightOperators())
			i->second._lightOperatorHashes.push_back(o.GetHash());

		for (auto& light:_lights)
			if (light.second._container == envSettings)
				InstantiateLight(light.second);
	}

	void MultiEnvironmentSettingsDocument::UnbindScene(RenderCore::LightingEngine::ILightScene& scene)
	{
		for (auto bs=_boundScenes.begin(); bs!=_boundScenes.end(); ++bs) {
			if (bs->second._boundScene.get() != &scene) continue;

			for (auto& l:_lights)
				if (l.second._container == bs->first)
					DeinstantiateLight(l.second);
			_boundScenes.erase(bs);
			return;
		}
	}

	auto MultiEnvironmentSettingsDocument::FindEnvSettingsId(StringSection<> name) -> EnvSettingsId
	{
		for (const auto& l:_envSettingContainers)
			if (XlEqString(name, l.second._name))
				return l.first;
		return ~0ull;		// couldn't find it
	}

	bool MultiEnvironmentSettingsDocument::InstantiateLight(RegisteredLight& registration)
	{
		assert(registration._instantiatedLight == ~0u);

		if (registration._container == ~0ull) {
			registration._instantiatedLight = ~0u;
			return false;
		}

		auto boundScene = LowerBound(_boundScenes, registration._container);
		if (boundScene == _boundScenes.end() || boundScene->first != registration._container) {
			registration._instantiatedLight = ~0u;
			return false;
		}

		unsigned lightOperatorId = ~0u;
		if (!registration._explicitLightOperator.empty()) {
			// lookup operator name in the list of known operators
			auto opNameHash = Hash64(registration._explicitLightOperator);
			auto q = LowerBound(boundScene->second._lightOperatorNameToIdx, opNameHash);
			if (q != boundScene->second._lightOperatorNameToIdx.end() && q->first == opNameHash)
				lightOperatorId = q->second;
		} else {
			auto hash = registration._impliedLightingOperator.GetHash();
			auto q = std::find(boundScene->second._lightOperatorHashes.begin(), boundScene->second._lightOperatorHashes.end(), hash);
			if (q != boundScene->second._lightOperatorHashes.end())
				lightOperatorId = (unsigned)std::distance(boundScene->second._lightOperatorHashes.begin(), q);
		}

		unsigned shadowOperatorId = ~0u;
		if (!registration._explicitShadowOperator.empty()) {
			auto opNameHash = Hash64(registration._explicitShadowOperator);
			auto q = LowerBound(boundScene->second._shadowOperatorNameToIdx, opNameHash);
			if (q != boundScene->second._shadowOperatorNameToIdx.end() && q->first == opNameHash)
			shadowOperatorId = q->second;
		}

		if (lightOperatorId == ~0u) {
			registration._instantiatedLight = ~0u;
			return false;
		}

		if (registration._type == RegisteredLight::Type::Positional) {
			registration._instantiatedLight = boundScene->second._boundScene->CreateLightSource(lightOperatorId);
			if (shadowOperatorId != ~0u)
				boundScene->second._boundScene->SetShadowOperator(registration._instantiatedLight, shadowOperatorId);
		} else if (registration._type == RegisteredLight::Type::DistantIBL) {
			registration._instantiatedLight = boundScene->second._boundScene->CreateAmbientLightSource();
		} else {
			assert(0);
			return false;
		}

		for (auto p:registration._parameters)
			if (p.HashName() != s_lightOperator._hash && p.HashName() != s_shadowOperator._hash)
				if (!SetSpecialProperty(
					*boundScene->second._boundScene, registration._instantiatedLight,
					p.HashName(), p.RawValue(), p.Type(), registration._parameters)) {

					SceneEngine::SetProperty(
						*boundScene->second._boundScene, registration._instantiatedLight,
						p.HashName(), p.RawValue(), p.Type());
				}

		return true;
	}

	void MultiEnvironmentSettingsDocument::DeinstantiateLight(RegisteredLight& registration)
	{
		if (registration._instantiatedLight == ~0u) return;

		if (registration._container == ~0ull) {
			registration._instantiatedLight = ~0u;
			return;
		}

		auto boundScene = LowerBound(_boundScenes, registration._container);
		if (boundScene == _boundScenes.end() || boundScene->first != registration._container) {
			registration._instantiatedLight = ~0u;
			return;
		}

		boundScene->second._boundScene->DestroyLightSource(registration._instantiatedLight);
		registration._instantiatedLight = ~0u;
	}


}

