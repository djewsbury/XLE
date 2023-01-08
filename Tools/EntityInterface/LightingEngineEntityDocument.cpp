// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineEntityDocument.h"
#include "../../SceneEngine/BasicLightingStateDelegate.h"
#include "../../SceneEngine/IScene.h"
#include "../../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../../RenderCore/LightingEngine/SunSourceConfiguration.h"

using namespace Utility::Literals;

namespace EntityInterface
{

	constexpr auto s_lightOperator = "LightOperator"_h;
	constexpr auto s_shadowOperator = "ShadowOperator"_h;
	constexpr auto s_skyTextureProcessor = "SkyTextureProcessor"_h;
	constexpr auto s_envSettings = "EnvSettings"_h;
	constexpr auto s_directionalLight = "DirectionalLight"_h;
	constexpr auto s_areaLight = "AreaLight"_h;
	constexpr auto s_distantIBL = "DistantIBL"_h;
	constexpr auto s_name = "Name"_h;
	constexpr auto s_packedColor = "PackedColor"_h;
	constexpr auto s_brightnessScalar = "BrightnessScalar"_h;
	constexpr auto s_sunSourceShadowSettings = "SunSourceShadowSettings"_h;
	constexpr auto s_light = "Light"_h;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct MultiEnvironmentSettingsDocument::RegisteredLight
	{
		enum class Type { Positional, DistantIBL };
		Type _type = Type::Positional;
		ParameterBox _parameters;
		LightSourceId _instantiatedLight = ~0u;
		EnvSettingsId _container = ~0ull;
		std::string _explicitLightOperator, _explicitShadowOperator;
		RenderCore::LightingEngine::LightSourceOperatorDesc _impliedLightingOperator;
		std::string _name;
	};

	struct MultiEnvironmentSettingsDocument::RegisteredShadow
	{
		ParameterBox _parameters;
		std::string _attachedLightName;
		EnvSettingsId _container = ~0ull;
		RenderCore::LightingEngine::SunSourceFrustumSettings _settings;
	};

	struct MultiEnvironmentSettingsDocument::BoundScene
	{
		RenderCore::LightingEngine::ILightScene* _boundScene = nullptr;		// raw pointer, lifetime must be guaranteed by client
		std::vector<std::pair<uint64_t, unsigned>> _lightOperatorNameToIdx;
		std::vector<std::pair<uint64_t, unsigned>> _shadowOperatorNameToIdx;
		std::vector<uint64_t> _lightOperatorHashes;
		std::vector<uint64_t> _shadowOperatorHashes;
	};

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
	struct MultiEnvironmentSettingsDocument::SkyTextureProcessorAndName
	{
		std::string _name;
		EnvSettingsId _container = ~0ull;
		RenderCore::LightingEngine::SkyTextureProcessorDesc _opDesc;
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
			cfg._mergedCfg.SetOperator(l.second._opDesc);
		}

		// register "implicit" light operators
		for (const auto& l:_lights)
			if (l.second._container == envSettings && l.second._explicitLightOperator.empty())
				cfg._mergedCfg.Register(l.second._impliedLightingOperator);

		for (const auto& s:_sunSourceShadowSettings)
			if (s.second._container == envSettings)
				cfg._mergedCfg.Register(RenderCore::LightingEngine::CalculateShadowOperatorDesc(s.second._settings));
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
		if (objType.second == s_lightOperator) {
			auto i = LowerBound(_lightOperators, id);
			assert(i == _lightOperators.end() || i->first != id);
			i = _lightOperators.insert(i, std::make_pair(id, LightSourceOperatorAndName{}));
			for (const auto& p:props) {
				if (p._prop.second == s_name) {
					i->second._name = ImpliedTyping::AsString(p._data, p._type);
				} else
					SceneEngine::SetProperty(i->second._opDesc, p._prop.second, p._data, p._type);
			}
			return true;
		} else if (objType.second == s_shadowOperator) {
			auto i = LowerBound(_shadowOperators, id);
			assert(i == _shadowOperators.end() || i->first != id);
			i = _shadowOperators.insert(i, std::make_pair(id, ShadowOperatorAndName{}));
			for (const auto& p:props) {
				if (p._prop.second == s_name) {
					i->second._name = ImpliedTyping::AsString(p._data, p._type);
				} else
					SceneEngine::SetProperty(i->second._opDesc, p._prop.second, p._data, p._type);
			}
			return true;
		} else if (objType.second == s_skyTextureProcessor) {
			auto i = LowerBound(_ambientOperators, id);
			assert(i == _ambientOperators.end() || i->first != id);
			i = _ambientOperators.insert(i, std::make_pair(id, SkyTextureProcessorAndName{}));
			for (const auto& p:props)
				SceneEngine::SetProperty(i->second._opDesc, p._prop.second, p._data, p._type);
			return true;
		} else if (objType.second == s_envSettings) {
			auto i = LowerBound(_envSettingContainers, id);
			assert(i == _envSettingContainers.end() || i->first != id);
			i = _envSettingContainers.insert(i, std::make_pair(id, EnvSettingContainer{}));
			for (const auto& p:props)
				if (p._prop.second == s_name)
					i->second._name = ImpliedTyping::AsString(p._data, p._type);
			return true;
		} else if (objType.second == s_directionalLight || objType.second == s_areaLight || objType.second == s_distantIBL) {
			auto i = LowerBound(_lights, id);
			assert(i == _lights.end() || i->first != id);
			RegisteredLight newLight;
			newLight._instantiatedLight = ~0u;
			newLight._type = (objType.second == s_distantIBL) ? RegisteredLight::Type::DistantIBL : RegisteredLight::Type::Positional;
			_lights.insert(i, std::make_pair(id, std::move(newLight)));
			SetProperty(id, props);
			return true;
		} else if (objType.second == s_sunSourceShadowSettings) {
			auto i = LowerBound(_sunSourceShadowSettings, id);
			assert(i == _sunSourceShadowSettings.end() || i->first != id);
			RegisteredShadow newShadow;
			_sunSourceShadowSettings.insert(i, std::make_pair(id, std::move(newShadow)));
			SetProperty(id, props);
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

		auto i6 = LowerBound(_sunSourceShadowSettings, id);
		if (i6 != _sunSourceShadowSettings.end() && i6->first == id) {
			auto attachedLightName = i6->second._attachedLightName;
			auto container = i6->second._container;
			_sunSourceShadowSettings.erase(i6);

			if (container != ~0u)
				for (auto& r:_lights)
					if (r.second._name == attachedLightName && r.second._container == container && r.second._instantiatedLight != ~0u) {
						DeinstantiateLight(r.second);
						InstantiateLight(r.second);
					}
			return true;
		}

		return false;
	}

	static bool SetSpecialProperty(
		RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId sourceId,
		uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type,
		const ParameterBox& pbox)
	{
		if (propertyNameHash == s_packedColor) {
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
		} else if (propertyNameHash == s_brightnessScalar) {
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
				if (p._prop.second == s_name) {
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
				if (p._prop.second == s_name) {
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
				if (p._prop.second == s_name) {
					i4->second._name = ImpliedTyping::AsString(p._data, p._type);
					result = true;
				}
			return result;
		}

		auto i5 = LowerBound(_lights, id);
		if (i5 != _lights.end() && i5->first == id) {
			bool changedOperatorOrName = false;
			for (const auto& p:props) {
				if (p._prop.second == s_lightOperator) {
					auto newOperator = ImpliedTyping::AsString(p._data, p._type);
					if (newOperator != i5->second._explicitLightOperator) {
						i5->second._explicitLightOperator = newOperator;
						changedOperatorOrName = true;
					}
				} else if (p._prop.second == s_shadowOperator) {
					auto newOperator = ImpliedTyping::AsString(p._data, p._type);
					if (newOperator != i5->second._explicitLightOperator) {
						i5->second._explicitShadowOperator = newOperator;
						changedOperatorOrName = true;
					}
				} else if (p._prop.second == s_name) {
					i5->second._name = ImpliedTyping::AsString(p._data, p._type);
					changedOperatorOrName = true;
				} else if (SceneEngine::SetProperty(i5->second._impliedLightingOperator, p._prop.second, p._data, p._type)) {
					// DiffuseModel
					// ShadowResolveModel
					// Shape
					// DominantLight
					// etc
					i5->second._parameters.SetParameter(p._prop.first, p._data, p._type);
					changedOperatorOrName = true;
				} else
					i5->second._parameters.SetParameter(p._prop.first, p._data, p._type);
			}

			// instantiation
			auto boundScene = LowerBound(_boundScenes, i5->second._container);
			if (boundScene != _boundScenes.end() && boundScene->first == i5->second._container) {
				if (changedOperatorOrName || (i5->second._instantiatedLight == ~0u)) {
					// destroy and recreate the light, because the operator changed (or name changed, which could change shadow configuration)
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

			return true;
		}

		auto i6 = LowerBound(_sunSourceShadowSettings, id);
		if (i6 != _sunSourceShadowSettings.end() && i6->first == id) {
			auto originalAttachedLightName = i6->second._attachedLightName;
			auto originalOperatorHash = RenderCore::LightingEngine::CalculateShadowOperatorDesc(i6->second._settings).GetHash();
			bool successfulPropertyChange = false;
			for (const auto& p:props) {
				if (p._prop.second == s_light) {
					i6->second._attachedLightName = ImpliedTyping::AsString(p._data, p._type);
				} else {
					successfulPropertyChange |= SceneEngine::SetProperty(i6->second._settings, p._prop.second, p._data, p._type);
					i6->second._parameters.SetParameter(p._prop.first, p._data, p._type);
				}
			}
			
			bool changedOperator = false;
			if (successfulPropertyChange)
				changedOperator = RenderCore::LightingEngine::CalculateShadowOperatorDesc(i6->second._settings).GetHash() != originalOperatorHash;

			// update instantiations
			auto boundScene = LowerBound(_boundScenes, i6->second._container);
			if (boundScene != _boundScenes.end() && boundScene->first == i6->second._container) {
				
				// if the attached name changed, remove the shadow operator from it's previously assignment
				bool attachedNameChange = originalAttachedLightName != i6->second._attachedLightName;
				if (attachedNameChange) {
					for (const auto& r:_lights)
						if (r.second._container == i6->second._container && r.second._name == originalAttachedLightName && r.second._instantiatedLight != ~0u)
							boundScene->second._boundScene->SetShadowOperator(r.second._instantiatedLight, ~0u);
				}

				// push updates to shadow 
				for (auto& r:_lights)
					if (r.second._container == i6->second._container && r.second._name == i6->second._attachedLightName && r.second._instantiatedLight != ~0u) {

						if (attachedNameChange || changedOperator) {
							// after an operator change, just go ahead and reinstantiate the light entirely (to reuse code)
							DeinstantiateLight(r.second);
							bool successful = InstantiateLight(r.second);
							if (!successful) IncreaseChangeId(i6->second._container);
						} else {
							RenderCore::LightingEngine::SetupSunSourceShadows(
								*boundScene->second._boundScene,
								r.second._instantiatedLight,
								i6->second._settings);
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
			return {};
		}

		auto i6 = LowerBound(_sunSourceShadowSettings, id);
		if (i6 != _sunSourceShadowSettings.end() && i6->first == id) {
			auto ptype = i6->second._parameters.GetParameterType(prop.second);
			if (ptype._type != ImpliedTyping::TypeCat::Void) {
				auto res = i6->second._parameters.GetParameterRawValue(prop.second);
				assert(res.size() == ptype.GetSize());
				std::memcpy(destinationBuffer.begin(), res.begin(), std::min(res.size(), destinationBuffer.size()));
				return ptype;
			}
			return {};
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

		auto i6 = LowerBound(_sunSourceShadowSettings, child);
		if (i6 != _sunSourceShadowSettings.end() && i6->first == child) {
			if (i6->second._container != parent) {
				i6->second._container = parent;

				// reinstantiate any lights in the old container that may have lost their shadow, or lights in the new
				// container that may have gained a shadow
				for (auto& r:_lights)
					if (r.second._name == i6->second._attachedLightName && r.second._instantiatedLight != ~0u) {
						DeinstantiateLight(r.second);
						InstantiateLight(r.second);
					}
			}
			return true;
		}

		return false;
	}

	void MultiEnvironmentSettingsDocument::BindScene(
		EnvSettingsId envSettings,
		RenderCore::LightingEngine::ILightScene& lightScene,
		const MergedLightingCfgHelper& mergedCfgHelper)
	{
		for (auto& l:_lights)
			if (l.second._container == envSettings)
				DeinstantiateLight(l.second);

		auto i = LowerBound(_boundScenes, envSettings);
		if (i == _boundScenes.end() || i->first != envSettings)
			 i = _boundScenes.insert(i, std::make_pair(envSettings, BoundScene{}));

		i->second._boundScene = &lightScene;
		i->second._lightOperatorNameToIdx = mergedCfgHelper._lightOperatorNameToIdx;
		i->second._shadowOperatorNameToIdx = mergedCfgHelper._shadowOperatorNameToIdx;
		i->second._lightOperatorHashes.clear();
		i->second._lightOperatorHashes.reserve(mergedCfgHelper._mergedCfg.GetLightOperators().size());
		for (const auto& o:mergedCfgHelper._mergedCfg.GetLightOperators())
			i->second._lightOperatorHashes.push_back(o.GetHash());
		i->second._shadowOperatorHashes.clear();
		i->second._shadowOperatorHashes.reserve(mergedCfgHelper._mergedCfg.GetShadowOperators().size());
		for (const auto& o:mergedCfgHelper._mergedCfg.GetShadowOperators())
			i->second._shadowOperatorHashes.push_back(o.GetHash());

		for (auto& light:_lights)
			if (light.second._container == envSettings)
				InstantiateLight(light.second);
	}

	void MultiEnvironmentSettingsDocument::UnbindScene(RenderCore::LightingEngine::ILightScene& scene)
	{
		for (auto bs=_boundScenes.begin(); bs!=_boundScenes.end(); ++bs) {
			if (bs->second._boundScene != &scene) continue;

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

		if (lightOperatorId == ~0u) {
			registration._instantiatedLight = ~0u;
			return false;
		}

		if (registration._type == RegisteredLight::Type::Positional) {
			registration._instantiatedLight = boundScene->second._boundScene->CreateLightSource(lightOperatorId);
		} else if (registration._type == RegisteredLight::Type::DistantIBL) {
			registration._instantiatedLight = boundScene->second._boundScene->CreateAmbientLightSource();
		} else {
			assert(0);
			return false;
		}

		for (auto p:registration._parameters)
			if (p.HashName() != s_lightOperator && p.HashName() != s_shadowOperator)
				if (!SetSpecialProperty(
					*boundScene->second._boundScene, registration._instantiatedLight,
					p.HashName(), p.RawValue(), p.Type(), registration._parameters)) {

					SceneEngine::SetProperty(
						*boundScene->second._boundScene, registration._instantiatedLight,
						p.HashName(), p.RawValue(), p.Type());
				}
		
		// Attach shadows to this light, if any have been configured
		if (!registration._explicitShadowOperator.empty()) {
			auto opNameHash = Hash64(registration._explicitShadowOperator);
			auto q = LowerBound(boundScene->second._shadowOperatorNameToIdx, opNameHash);
			if (q != boundScene->second._shadowOperatorNameToIdx.end() && q->first == opNameHash) {
				boundScene->second._boundScene->SetShadowOperator(registration._instantiatedLight, q->second);
			} else
				return false;	// missing shadow operator
		} else if (!registration._name.empty()) {
			auto i = std::find_if(_sunSourceShadowSettings.begin(), _sunSourceShadowSettings.end(),
				[name=registration._name, container=registration._container](const auto& q) { return q.second._container == container && q.second._attachedLightName == name; });
			if (i != _sunSourceShadowSettings.end()) {
				auto shadowOpHash = RenderCore::LightingEngine::CalculateShadowOperatorDesc(i->second._settings).GetHash();
				auto q = std::find(boundScene->second._shadowOperatorHashes.begin(), boundScene->second._shadowOperatorHashes.end(), shadowOpHash);
				if (q != boundScene->second._shadowOperatorHashes.end() && *q == shadowOpHash) {
					boundScene->second._boundScene->SetShadowOperator(
						registration._instantiatedLight,
						(unsigned)std::distance(boundScene->second._shadowOperatorHashes.begin(), q));
					RenderCore::LightingEngine::SetupSunSourceShadows(
						*boundScene->second._boundScene, 
						registration._instantiatedLight,
						i->second._settings);
				} else
					return false; // missing shadow operator
			}
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

