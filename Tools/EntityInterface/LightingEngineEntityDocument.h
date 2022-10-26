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

		void PrepareCfg(EnvSettingsId envSettings, MergedLightingCfgHelper& cfg);
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
			enum class Type { Positional, DistantIBL };
			Type _type = Type::Positional;
			ParameterBox _parameters;
			LightSourceId _instantiatedLight = ~0u;
			EnvSettingsId _container = ~0ull;
			std::string _explicitLightOperator, _explicitShadowOperator;
			RenderCore::LightingEngine::LightSourceOperatorDesc _impliedLightingOperator;
		};
		std::vector<std::pair<EntityId, RegisteredLight>> _lights;
		std::vector<std::pair<EntityId, RegisteredLight>> _distantIBL;

		struct BoundScene
		{
			std::shared_ptr<RenderCore::LightingEngine::ILightScene> _boundScene;
			std::vector<std::pair<uint64_t, unsigned>> _lightOperatorNameToIdx;
			std::vector<std::pair<uint64_t, unsigned>> _shadowOperatorNameToIdx;
			std::vector<uint64_t> _lightOperatorHashes;
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
		bool InstantiateLight(RegisteredLight&);
		void DeinstantiateLight(RegisteredLight&);
	};

}