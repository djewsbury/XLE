// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EntityInterface.h"
#include "../../SceneEngine/IScene.h"
#include "../../RenderCore/LightingEngine/ILightScene.h"
#include <vector>
#include <memory>
#include <random>

namespace EntityInterface
{

	struct MergedLightingCfgHelper
	{
		SceneEngine::MergedLightingEngineCfg _mergedCfg;

		std::vector<std::pair<uint64_t, unsigned>> _lightOperatorNameToIdx;
		std::vector<std::pair<uint64_t, unsigned>> _shadowOperatorNameToIdx;
	};

	class LightingOperatorsEntityDocument : public IMutableEntityDocument
	{
	public:
		void BindCfg(MergedLightingCfgHelper& cfg);
		unsigned GetChangeId() const;

		LightingOperatorsEntityDocument();
		~LightingOperatorsEntityDocument();

		EntityId AssignEntityId() override;
		bool CreateEntity(StringAndHash objType, EntityId id, IteratorRange<const PropertyInitializer*>) override;
		bool DeleteEntity(EntityId id) override;
		bool SetProperty(EntityId id, IteratorRange<const PropertyInitializer*>) override;
		std::optional<ImpliedTyping::TypeDesc> GetProperty(EntityId id, StringAndHash prop, IteratorRange<void*> destinationBuffer) const override;
		bool SetParent(EntityId child, EntityId parent, StringAndHash childList, int insertionPosition) override;
	protected:
		struct LightSourceOperatorAndName;
		struct ShadowOperatorAndName;
		std::vector<std::pair<EntityId, LightSourceOperatorAndName>> _lightOperators;
		std::vector<std::pair<EntityId, ShadowOperatorAndName>> _shadowOperators;
		std::optional<EntityId> _ambientOperatorEntity;
		RenderCore::LightingEngine::AmbientLightOperatorDesc _ambientOperator;

		unsigned _currentChangeId = ~0u;
		std::mt19937_64 _rng;
	};

	class LightSceneEntityDocument : public IMutableEntityDocument
	{
	public:

		void BindScene(
			std::shared_ptr<RenderCore::LightingEngine::ILightScene> lightScene,
			const MergedLightingCfgHelper& mergedCfgHelper);
		void UnbindScene();

		LightSceneEntityDocument();
		~LightSceneEntityDocument();

		EntityId AssignEntityId() override;
		bool CreateEntity(StringAndHash objType, EntityId id, IteratorRange<const PropertyInitializer*>) override;
		bool DeleteEntity(EntityId id) override;
		bool SetProperty(EntityId id, IteratorRange<const PropertyInitializer*>) override;
		std::optional<ImpliedTyping::TypeDesc> GetProperty(EntityId id, StringAndHash prop, IteratorRange<void*> destinationBuffer) const override;
		bool SetParent(EntityId child, EntityId parent, StringAndHash childList, int insertionPosition) override;

	private:
		std::mt19937_64 _rng;
		using LightSourceId = RenderCore::LightingEngine::ILightScene::LightSourceId;
		struct RegisteredLight
		{
			ParameterBox _parameters;
			LightSourceId _registeredLight = ~0u;
		};
		std::vector<std::pair<EntityId, RegisteredLight>> _entities;

		std::shared_ptr<RenderCore::LightingEngine::ILightScene> _boundScene;
		std::vector<std::pair<uint64_t, unsigned>> _lightOperatorNameToIdx;
		std::vector<std::pair<uint64_t, unsigned>> _shadowOperatorNameToIdx;

		void InstantiateLight(RegisteredLight&);
	};

	class MultiEnvironmentSettingsDocument : public IMutableEntityDocument
	{
	public:
		using EnvSettingsId = EntityId;
		EnvSettingsId FindEnvSettingsId(StringSection<> name);

		void BindScene(
			EnvSettingsId envSettings,
			std::shared_ptr<RenderCore::LightingEngine::ILightScene> lightScene,
			const MergedLightingCfgHelper& mergedCfgHelper);
		void UnbindScene(RenderCore::LightingEngine::ILightScene&);

		void BindCfg(EnvSettingsId envSettings, MergedLightingCfgHelper& cfg);
		unsigned GetChangeId(EnvSettingsId envSettings) const;

		MultiEnvironmentSettingsDocument();
		~MultiEnvironmentSettingsDocument();

		EntityId AssignEntityId() override;
		bool CreateEntity(StringAndHash objType, EntityId id, IteratorRange<const PropertyInitializer*>) override;
		bool DeleteEntity(EntityId id) override;
		bool SetProperty(EntityId id, IteratorRange<const PropertyInitializer*>) override;
		std::optional<ImpliedTyping::TypeDesc> GetProperty(EntityId id, StringAndHash prop, IteratorRange<void*> destinationBuffer) const override;
		bool SetParent(EntityId child, EntityId parent, StringAndHash childList, int insertionPosition) override;
	private:
		using LightSourceId = RenderCore::LightingEngine::ILightScene::LightSourceId;
		struct RegisteredLight
		{
			ParameterBox _parameters;
			LightSourceId _registeredLight = ~0u;
			EnvSettingsId _container = ~0ull;
		};
		std::vector<std::pair<EntityId, RegisteredLight>> _lights;

		struct BoundScene
		{
			std::shared_ptr<RenderCore::LightingEngine::ILightScene> _boundScene;
			std::vector<std::pair<uint64_t, unsigned>> _lightOperatorNameToIdx;
			std::vector<std::pair<uint64_t, unsigned>> _shadowOperatorNameToIdx;
		};
		std::vector<std::pair<EnvSettingsId, BoundScene>> _boundScenes;

		struct LightSourceOperatorAndName;
		struct ShadowOperatorAndName;
		struct AmbientOperatorAndName;
		std::vector<std::pair<EntityId, LightSourceOperatorAndName>> _lightOperators;
		std::vector<std::pair<EntityId, ShadowOperatorAndName>> _shadowOperators;
		std::vector<std::pair<EntityId, AmbientOperatorAndName>> _ambientOperators;

		struct EnvSettingContainer;
		std::vector<std::pair<EnvSettingsId, EnvSettingContainer>> _envSettingContainers;
		std::mt19937_64 _rng;

		void IncreaseChangeId(EnvSettingsId);
		void InstantiateLight(RegisteredLight&);
		void DeinstantiateLight(RegisteredLight&);
	};

}