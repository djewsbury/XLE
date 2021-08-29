// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IScene.h"
#include "../RenderCore/LightingEngine/SunSourceConfiguration.h"
#include "../SceneEngine/Tonemap.h"
#include "../Assets/DepVal.h"
#include "../Assets/AssetsCore.h"
#if 0
#include "../SceneEngine/VolumetricFog.h"
#include "../SceneEngine/Ocean.h"
#include "../SceneEngine/DeepOceanSim.h"
#endif

namespace Utility
{
    template<typename Type> class InputStreamFormatter;
}
namespace Assets { class DirectorySearchRules; }

namespace SceneEngine
{
    using SunSourceFrustumSettings = RenderCore::LightingEngine::SunSourceFrustumSettings;

    /// <summary>Describes a lighting environment</summary>
    /// This contains all of the settings and properties required
    /// for constructing a basic lighting environment.
    /// This can be used to implement the ISceneParser functions that
    /// return lighting settings (like ISceneParser::GetLightDesc() and ISceneParser::GetSceneLightingDesc())
    class EnvironmentSettings
    {
    public:
        std::vector<LightDesc> _lights;
        EnvironmentalLightingDesc _environmentalLightingDesc;
        ToneMapSettings _toneMapSettings;

        class SunSourceShadowProj
        {
        public:
            unsigned _lightIdx;
            SunSourceFrustumSettings _shadowFrustumSettings;
        };
        std::vector<SunSourceShadowProj> _sunSourceShadowProj;

#if 0
        VolumetricFogConfig::Renderer _volFogRenderer;
        OceanLightingSettings _oceanLighting;
        DeepOceanSimSettings _deepOceanSim;
#endif
        
        EnvironmentSettings();
        EnvironmentSettings(
            InputStreamFormatter<utf8>& formatter,
            const ::Assets::DirectorySearchRules&,
			const ::Assets::DependencyValidation&);
        ~EnvironmentSettings();

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

	private:
		::Assets::DependencyValidation _depVal;
    };

    /// <summary>Simple & partial implementation of the ILightingStateDelegate interface<summary>
    /// This provides implementations of the basic lighting related interfaces of
    /// ISceneParser that will hook into an EnvironmentSettings object.
    /// Derived classes should implement the accessor GetEnvSettings().
    class BasicLightingStateDelegate : public ILightingStateDelegate
    {
    public:
        void        ConfigureLightScene(const RenderCore::Techniques::ProjectionDesc& mainSceneCameraDesc, RenderCore::LightingEngine::ILightScene& lightScene) const override;
        auto        GetEnvironmentalLightingDesc() const -> EnvironmentalLightingDesc override;
        auto        GetToneMapSettings() const -> ToneMapSettings override;

        std::vector<RenderCore::LightingEngine::LightSourceOperatorDesc> GetLightResolveOperators() const override;
		std::vector<RenderCore::LightingEngine::ShadowOperatorDesc> GetShadowResolveOperators() const override;

		BasicLightingStateDelegate(
			const std::shared_ptr<EnvironmentSettings>& envSettings);
		~BasicLightingStateDelegate();

		static void ConstructToFuture(
			::Assets::FuturePtr<BasicLightingStateDelegate>& future,
			StringSection<::Assets::ResChar> envSettingFileName);

		::Assets::DependencyValidation GetDependencyValidation() const override { return _envSettings->GetDependencyValidation(); }

    protected:
        const EnvironmentSettings&  GetEnvSettings() const;
		std::shared_ptr<EnvironmentSettings>	_envSettings;
    };

    LightDesc                   DefaultDominantLight();
    EnvironmentalLightingDesc   DefaultEnvironmentalLightingDesc();
    EnvironmentSettings         DefaultEnvironmentSettings();
    SunSourceFrustumSettings    DefaultSunSourceFrustumSettings();

    EnvironmentalLightingDesc MakeEnvironmentalLightingDesc(const ParameterBox& props);
    LightDesc MakeLightDesc(const Utility::ParameterBox& props);
}

