// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BasicLightingStateDelegate.h"
#include "LightSceneConfiguration.h"
#include "../RenderCore/LightingEngine/SunSourceConfiguration.h"
#include "../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../Tools/EntityInterface/EntityInterface.h"
#include "../Tools/ToolsRig/ToolsRigServices.h"
#include "../Assets/Assets.h"
#include "../Assets/AssetFutureContinuation.h"
#include "../Assets/ConfigFileContainer.h"
#include "../Math/Transformations.h"
#include "../Math/MathSerialization.h"
#include "../Utility/StringUtils.h"
#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/Streams/FormatterUtils.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Conversion.h"
#include "../Utility/Meta/AccessorSerialize.h"
#include "../Utility/Meta/ClassAccessors.h"
 
namespace SceneEngine
{
    static float PowerForHalfRadius(float halfRadius, float powerFraction)
	{
		const float attenuationScalar = 1.f;
		return (attenuationScalar*(halfRadius*halfRadius)+1.f) * (1.0f / (1.f-powerFraction));
	}

    class SwirlingPointLights
    {
    public:
        RenderCore::LightingEngine::LightSourceOperatorDesc _operator;
        std::vector<RenderCore::LightingEngine::ILightScene::LightSourceId> _lightSources;
        void UpdateLights(RenderCore::LightingEngine::ILightScene& lightScene)
        {
            static float cutoffRadius = 7.5f;
            static float swirlingRadius = 15.0f;
            float startingAngle = 0.f + _time;
            const auto tileLightCount = _lightSources.size();
            for (unsigned c=0; c<tileLightCount; ++c) {
                auto lightId = _lightSources[c];

                const float X = startingAngle + c / float(tileLightCount) * gPI * 2.f;
				const float Y = 3.7397f * startingAngle + .7234f * c / float(tileLightCount) * gPI * 2.f;
				// const float Z = 13.8267f * startingAngle + 0.27234f * c / float(tileLightCount) * gPI * 2.f;

                auto* positional = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(lightId);
                if (positional) {
                    Float4x4 temp = AsFloat4x4(RotationY(2.f * gPI * c/float(tileLightCount) + _time));
                    Combine_IntoLHS(temp, RotationX(2.f * gPI * c/float(tileLightCount)));
                    Combine_IntoLHS(temp, RotationY(2.f * gPI * c/float(tileLightCount)));
                    positional->SetLocalToWorld(AsFloat4x4(ScaleTranslation { Float3(0.1f, 0.1f, 1.0f), TransformPoint(temp, Float3(0,0,swirlingRadius)) }));
                }

                auto* emittance = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(lightId);
                if (emittance) {
                    auto power = PowerForHalfRadius(0.5f*cutoffRadius, 0.05f);
                    auto brightness = power * Float3{.65f + .35f * XlSin(Y), .65f + .35f * XlCos(Y), .65f + .35f * XlCos(X)};
                    emittance->SetBrightness(brightness);
                }

                auto* finite = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IFiniteLightSource>(lightId);
                if (finite) {
                    finite->SetCutoffBrightness(0.05f);
                }
            }
            _time += 1.0f/60.f;
        }

        void BindScene(RenderCore::LightingEngine::ILightScene& lightScene, unsigned opId)
        {
            assert(_lightSources.empty());
            const auto tileLightCount = 32u;
            for (unsigned c=0; c<tileLightCount; ++c) {
                auto lightId = lightScene.CreateLightSource(opId);
                _lightSources.push_back(lightId);
            }
        }

        void UnbindScene(RenderCore::LightingEngine::ILightScene& lightScene)
        {
            for (auto l:_lightSources)
                lightScene.DestroyLightSource(l);
            _lightSources.clear();
        }

        SwirlingPointLights()
        {
            _operator._shape = RenderCore::LightingEngine::LightSourceShape::Sphere;
            _time = 0.f;
        }
        float _time;
    };

    static SwirlingPointLights s_swirlingLights;
    static unsigned s_swirlingLightsOp = ~0u;

    class BasicLightingStateDelegate : public ILightingStateDelegate
    {
    public:
        void        PreRender(const RenderCore::Techniques::ProjectionDesc& mainSceneCameraDesc, RenderCore::LightingEngine::ILightScene& lightScene) override;
        void        PostRender(RenderCore::LightingEngine::ILightScene& lightScene) override;
        void        BindScene(RenderCore::LightingEngine::ILightScene& lightScene) override;
        void        UnbindScene(RenderCore::LightingEngine::ILightScene& lightScene) override;
        auto        BeginPrepareStep(RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::IThreadContext& threadContext) -> std::shared_ptr<RenderCore::LightingEngine::IProbeRenderingInstance> override;

        auto        GetEnvironmentalLightingDesc() -> EnvironmentalLightingDesc override;
        auto        GetToneMapSettings() -> ToneMapSettings override;

        Operators   GetOperators() override;        

		BasicLightingStateDelegate(EntityInterface::IDynamicFormatter& formatter);
		~BasicLightingStateDelegate();

		static void ConstructToFuture(
			::Assets::FuturePtr<BasicLightingStateDelegate>& future,
			StringSection<> envSettingFileName);

		::Assets::DependencyValidation GetDependencyValidation() const override;

    protected:
        LightOperatorResolveContext _operatorResolveContext;
        ObjectTable<RenderCore::LightingEngine::SunSourceFrustumSettings> _sunSourceFrustumSettings;

        struct PendingLightSource
        {
            uint64_t _operatorHash = 0;
            std::string _name;
            ParameterBox _parameters;
        };
        std::vector<PendingLightSource> _lightSourcesInCfgFile;

        std::vector<unsigned> _lightSourcesInBoundScene;
        std::vector<unsigned> _shadowProjectionsInBoundScene;

        std::vector<std::pair<uint64_t, RenderCore::LightingEngine::ILightScene::LightOperatorId>> _lightOperatorHashToId;
        std::vector<std::pair<uint64_t, RenderCore::LightingEngine::ILightScene::ShadowOperatorId>> _shadowOperatorHashToId;
        std::vector<std::pair<uint64_t, RenderCore::LightingEngine::ILightScene::ShadowOperatorId>> _sunSourceHashToShadowOperatorId;
        std::vector<std::pair<uint64_t, RenderCore::LightingEngine::ILightScene::LightOperatorId>> _ambientOperatorHashToId;        

        ::Assets::DependencyValidation _depVal;

        void DeserializeLightSources(EntityInterface::IDynamicFormatter& formatter);
    };

    void BasicLightingStateDelegate::PreRender(
        const RenderCore::Techniques::ProjectionDesc& mainSceneCameraDesc,
        RenderCore::LightingEngine::ILightScene& lightScene)
    {
        s_swirlingLights.UpdateLights(lightScene);
    }

    void        BasicLightingStateDelegate::PostRender(RenderCore::LightingEngine::ILightScene& lightScene)
    {
    }

    void        BasicLightingStateDelegate::BindScene(RenderCore::LightingEngine::ILightScene& lightScene)
    {
        const ParameterBox::ParameterName Transform = "Transform";
        const ParameterBox::ParameterName Position = "Position";
        const ParameterBox::ParameterName Brightness = "Brightness";
        const ParameterBox::ParameterName CutoffBrightness = "CutoffBrightness";
        const ParameterBox::ParameterName CutoffRange = "CutoffRange";
        const ParameterBox::ParameterName SkyTexture = "SkyTexture";

        std::vector<std::pair<uint64_t, RenderCore::LightingEngine::ILightScene::LightSourceId>> lightNameToId;

        for (const auto&light:_lightSourcesInCfgFile) {
            if (!light._operatorHash) continue;

            auto lightOperator = LowerBound(_lightOperatorHashToId, light._operatorHash);
            if (lightOperator != _lightOperatorHashToId.end() && lightOperator->first == light._operatorHash) {

                auto newLight = lightScene.CreateLightSource(lightOperator->second);
                _lightSourcesInBoundScene.push_back(newLight);

                auto* positional = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(newLight);
                if (positional) {
                    auto transformValue = light._parameters.GetParameter<Float4x4>(Transform);
                    if (transformValue) {
                        positional->SetLocalToWorld(transformValue.value());
                    } else {
                        Float4x4 localToWorld = Identity<Float4x4>();
                        auto positionValue = light._parameters.GetParameter<Float3>(Position);
                        if (positionValue) {
                            SetTranslation(localToWorld, positionValue.value());
                            positional->SetLocalToWorld(localToWorld);
                        }
                    }                    
                }

                auto* uniformEmittance = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(newLight);
                if (uniformEmittance) {
                    auto brightness = light._parameters.GetParameter<Float3>(Brightness);
                    if (brightness)
                        uniformEmittance->SetBrightness(brightness.value());
                }

                auto* finite = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IFiniteLightSource>(newLight);
                if (finite) {
                    auto cutoffBrightness = light._parameters.GetParameter<float>(CutoffBrightness);
                    if (cutoffBrightness)
                        finite->SetCutoffBrightness(cutoffBrightness.value());
                    auto cutoffRange = light._parameters.GetParameter<float>(CutoffRange);
                    if (cutoffRange)
                        finite->SetCutoffRange(cutoffRange.value());
                }

                lightNameToId.emplace_back(Hash64(light._name), newLight);

                continue;
            }

            lightOperator = LowerBound(_ambientOperatorHashToId, light._operatorHash);
            if (lightOperator != _ambientOperatorHashToId.end() && lightOperator->first == light._operatorHash) {

                auto newLight = lightScene.CreateLightSource(lightOperator->second);
                _lightSourcesInBoundScene.push_back(newLight);

                auto* distanceIBL = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IDistantIBLSource>(newLight);
                if (distanceIBL) {
                    distanceIBL->SetEquirectangularSource(light._parameters.GetParameterAsString(SkyTexture).value());
                }
                continue;
            }
        }

        for (const auto& sunSource:_sunSourceFrustumSettings._objects) {
            auto op = LowerBound(_sunSourceHashToShadowOperatorId, sunSource.first);
            if (op == _sunSourceHashToShadowOperatorId.end() || op->first != sunSource.first) continue;

            // todo -- correct id for the associated light
            auto newShadow = RenderCore::LightingEngine::CreateSunSourceShadows(
                lightScene, op->second, 32, sunSource.second);
            _shadowProjectionsInBoundScene.push_back(newShadow);
        }

#if 0
        auto lightOperators = _operatorResolveContext._lightSourceOperators;

        RenderCore::LightingEngine::ILightScene::LightSourceId lightIndexToId[_envSettings->_lights.size()];

        unsigned lightIdx=0;
        for (const auto& light:_envSettings->_lights) {
            unsigned operatorId = 0;
            for (; operatorId != lightOperators.size(); ++operatorId)
                if (lightOperators[operatorId]._diffuseModel == light._diffuseModel && lightOperators[operatorId]._shape == light._shape)
                    break;

            assert(operatorId < lightOperators.size());
            auto lightId = lightScene.CreateLightSource(operatorId);
            lightIndexToId[lightIdx++] = lightId;

            auto* positional = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(lightId);
            if (positional) {
                ScaleRotationTranslationM srt { Expand(light._radii, 1.0f), light._orientation, light._position };
                positional->SetLocalToWorld(AsFloat4x4(srt));
            }

            auto* emittance = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(lightId);
            if (emittance) {
                emittance->SetBrightness(light._brightness);
                emittance->SetDiffuseWideningFactors({light._diffuseWideningMin, light._diffuseWideningMax});
            }

            auto* finite = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IFiniteLightSource>(lightId);
            if (finite) {
                finite->SetCutoffBrightness(light._cutoffBrightness);
            }
        }

        for (const auto& shadow:_envSettings->_sunSourceShadowProj) {
            unsigned operatorId = 0;
            auto shadowId = RenderCore::LightingEngine::CreateSunSourceShadows(
                lightScene, operatorId, lightIndexToId[shadow._lightIdx],
                shadow._shadowFrustumSettings);
            _shadowProjectionsInBoundScene.push_back(shadowId);
        }
        
        // Create the "ambient/environment light"
        auto ambientLight = lightScene.CreateLightSource((unsigned)lightOperators.size());
        auto* distantIBLSource = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IDistantIBLSource>(ambientLight);
        if (distantIBLSource) {
            if (GetEnvSettings()._environmentalLightingDesc._skyTextureType == RenderCore::LightingEngine::SkyTextureType::Equirectangular)
                distantIBLSource->SetEquirectangularSource(GetEnvSettings()._environmentalLightingDesc._skyTexture);
        }
        _lightSourcesInBoundScene.push_back(ambientLight);
#endif

        s_swirlingLights.BindScene(lightScene, s_swirlingLightsOp);
    }

    void        BasicLightingStateDelegate::UnbindScene(RenderCore::LightingEngine::ILightScene& lightScene)
    {
        s_swirlingLights.UnbindScene(lightScene);
        for (auto shadowId:_shadowProjectionsInBoundScene)
            lightScene.DestroyShadowProjection(shadowId);
        for (auto lightSource:_lightSourcesInBoundScene)
            lightScene.DestroyLightSource(lightSource);
    }

    std::shared_ptr<RenderCore::LightingEngine::IProbeRenderingInstance> BasicLightingStateDelegate::BeginPrepareStep(RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::IThreadContext& threadContext)
    {
        return nullptr;
    }

    static void DeserializeLightOperators(EntityInterface::IDynamicFormatter& formatter, LightOperatorResolveContext& operatorResolveContext)
    {
        StringSection<> name;
        while (formatter.TryKeyedItem(name)) {
            if (XlEqString(name, "LightSource")) {
                RequireBeginElement(formatter);
                operatorResolveContext.DeserializeLightSourceOperator(formatter);
                RequireEndElement(formatter);
            } else if (XlEqString(name, "Shadow")) {
                RequireBeginElement(formatter);
                operatorResolveContext.DeserializeShadowOperator(formatter);
                RequireEndElement(formatter);
            } else if (XlEqString(name, "Ambient")) {
                RequireBeginElement(formatter);
                operatorResolveContext.DeserializeAmbientOperator(formatter);
                RequireEndElement(formatter);
            } else {
                formatter.SkipValueOrElement();
            }
        }
	}

    auto BasicLightingStateDelegate::GetOperators() -> Operators
    {
        _lightOperatorHashToId.clear();
        _shadowOperatorHashToId.clear();
        _ambientOperatorHashToId.clear();
        _sunSourceHashToShadowOperatorId.clear();

        Operators result;
        result._lightResolveOperators.reserve(_operatorResolveContext._lightSourceOperators._objects.size());
        result._shadowResolveOperators.reserve(_operatorResolveContext._shadowOperators._objects.size());
        _lightOperatorHashToId.reserve(_operatorResolveContext._lightSourceOperators._objects.size());
        _shadowOperatorHashToId.reserve(_operatorResolveContext._lightSourceOperators._objects.size());
        for (const auto& c:_operatorResolveContext._lightSourceOperators._objects) {
            auto h = c.second.Hash();
            auto i = std::find_if(result._lightResolveOperators.begin(), result._lightResolveOperators.end(), [h](const auto& c) { return c.Hash() == h; });
            if (i==result._lightResolveOperators.end())
                i = result._lightResolveOperators.insert(i, c.second);
            _lightOperatorHashToId.emplace_back(c.first, (unsigned)std::distance(result._lightResolveOperators.begin(), i));
        }
        for (const auto& c:_operatorResolveContext._shadowOperators._objects) {
            auto h = c.second.Hash();
            auto i = std::find_if(result._shadowResolveOperators.begin(), result._shadowResolveOperators.end(), [h](const auto& c) { return c.Hash() == h; });
            if (i==result._shadowResolveOperators.end())
                i = result._shadowResolveOperators.insert(i, c.second);
            _shadowOperatorHashToId.emplace_back(c.first, (unsigned)std::distance(result._shadowResolveOperators.begin(), i));
        }
        for (const auto& c:_sunSourceFrustumSettings._objects) {
            auto shadowOperator = RenderCore::LightingEngine::CalculateShadowOperatorDesc(c.second);
            auto h = shadowOperator.Hash();
            auto i = std::find_if(result._shadowResolveOperators.begin(), result._shadowResolveOperators.end(), [h](const auto& c) { return c.Hash() == h; });
            if (i==result._shadowResolveOperators.end())
                i = result._shadowResolveOperators.insert(i, shadowOperator);
            _sunSourceHashToShadowOperatorId.emplace_back(c.first, (unsigned)std::distance(result._shadowResolveOperators.begin(), i));
        }

        /*std::vector<RenderCore::LightingEngine::LightSourceOperatorDesc> result;
        for (const auto& light:_envSettings->_lights) {
            RenderCore::LightingEngine::LightSourceOperatorDesc opDesc { light._shape, light._diffuseModel };
            if (light._isDominantLight)
                opDesc._flags |= RenderCore::LightingEngine::LightSourceOperatorDesc::Flags::DominantLight;
            auto h = opDesc.Hash();
            auto i = std::find_if(result.begin(), result.end(), [h](const auto& c) { return c.Hash() == h; });
            if (i == result.end())
                result.push_back(opDesc);
        }*/

        {
            auto h = s_swirlingLights._operator.Hash();
            auto i = std::find_if(result._lightResolveOperators.begin(), result._lightResolveOperators.end(), [h](const auto& c) { return c.Hash() == h; });
            s_swirlingLightsOp = (unsigned)std::distance(result._lightResolveOperators.begin(), i);
            if (i == result._lightResolveOperators.end())
                result._lightResolveOperators.push_back(s_swirlingLights._operator);
        }

        if (!_operatorResolveContext._ambientOperators._objects.empty())
            _ambientOperatorHashToId.emplace_back(_operatorResolveContext._ambientOperators._objects[0].first, (unsigned)result._lightResolveOperators.size());

        std::sort(_lightOperatorHashToId.begin(), _lightOperatorHashToId.end(), CompareFirst<uint64_t, unsigned>());
        std::sort(_shadowOperatorHashToId.begin(), _shadowOperatorHashToId.end(), CompareFirst<uint64_t, unsigned>());
        std::sort(_sunSourceHashToShadowOperatorId.begin(), _sunSourceHashToShadowOperatorId.end(), CompareFirst<uint64_t, unsigned>());

        return result;
    }

#if 0
    std::vector<RenderCore::LightingEngine::ShadowOperatorDesc> BasicLightingStateDelegate::GetShadowResolveOperators()
    {
        std::vector<RenderCore::LightingEngine::ShadowOperatorDesc> result;
        std::vector<uint64_t> resultHashes;
        for (const auto& shadow:_envSettings->_sunSourceShadowProj) {
            RenderCore::LightingEngine::ShadowOperatorDesc opDesc;
            opDesc = RenderCore::LightingEngine::CalculateShadowOperatorDesc(shadow._shadowFrustumSettings);
            auto h = opDesc.Hash();
            if (std::find(resultHashes.begin(), resultHashes.end(), h) == resultHashes.end()) {
                result.push_back(opDesc);
                resultHashes.push_back(h);
            }
        }
        return result;
    }
#endif

    auto BasicLightingStateDelegate::GetEnvironmentalLightingDesc() -> RenderCore::LightingEngine::EnvironmentalLightingDesc
    {
        return {};
    }

    ToneMapSettings BasicLightingStateDelegate::GetToneMapSettings()
    {
        return {};
    }

    ::Assets::DependencyValidation BasicLightingStateDelegate::GetDependencyValidation() const
    {
        return _depVal;
    }

    void BasicLightingStateDelegate::DeserializeLightSources(EntityInterface::IDynamicFormatter& formatter)
    {
        StringSection<> keyname;
        while (formatter.TryKeyedItem(keyname)) {
            if (XlEqString(keyname, "Light")) {

                RequireBeginElement(formatter);

                ParameterBox lightProperties;
                StringSection<> name;
                uint64_t operatorHash = 0;

                StringSection<> keyname;
                while (formatter.TryKeyedItem(keyname)) {
                    if (XlEqString(keyname, "Name")) name = RequireStringValue(formatter);
                    else if (XlEqString(keyname, "Operator")) operatorHash = Hash64(RequireStringValue(formatter));
                    else {
                        IteratorRange<const void*> value;
                        ImpliedTyping::TypeDesc type;
                        if (!formatter.TryRawValue(value, type))
                            Throw(FormatException("Expecting value", formatter.GetLocation()));
                        lightProperties.SetParameter(keyname, value, type);
                    }
                }
                RequireEndElement(formatter);

                auto i = std::find_if(
                    _lightSourcesInCfgFile.begin(), _lightSourcesInCfgFile.end(),
                    [name](const auto& c) { return XlEqString(name, c._name); });
                if (!name.IsEmpty() && i != _lightSourcesInCfgFile.end()) {
                    i->_operatorHash = operatorHash ?: i->_operatorHash;
                    i->_parameters.MergeIn(lightProperties);
                } else {
                    _lightSourcesInCfgFile.push_back(PendingLightSource{operatorHash, name.AsString(), std::move(lightProperties)});
                }
            } else if (XlEqString(keyname, "SunSourceShadow")) {
                RequireBeginElement(formatter);

                RenderCore::LightingEngine::SunSourceFrustumSettings sunSourceShadows;
                StringSection<> name, associatedLight;
                
                std::vector<decltype(_sunSourceFrustumSettings)::PendingProperty> properties; 
                while (formatter.TryKeyedItem(keyname)) {
                    if (XlEqString(keyname, "Name")) name = RequireStringValue(formatter);
                    else if (XlEqString(keyname, "Light")) associatedLight = RequireStringValue(formatter);
                    else {
                        ImpliedTyping::TypeDesc typeDesc;
                        auto data = RequireRawValue(formatter, typeDesc);
                        properties.push_back({keyname, data, typeDesc});
                    }
                }

                RequireEndElement(formatter);
                _sunSourceFrustumSettings.DeserializeObject(name, properties);

            } else {
                SkipValueOrElement(formatter);
            }
        }
    }

	void BasicLightingStateDelegate::ConstructToFuture(
		::Assets::FuturePtr<BasicLightingStateDelegate>& future,
		StringSection<> envSettingFileName)
	{
        auto fmttrFuture = ToolsRig::Services::GetEntityMountingTree().BeginFormatter(envSettingFileName);
        ::Assets::WhenAll(fmttrFuture).ThenConstructToFuture(
            future,
            [](auto fmttr) { return std::make_shared<BasicLightingStateDelegate>(*fmttr); });
	}

	BasicLightingStateDelegate::BasicLightingStateDelegate(
		EntityInterface::IDynamicFormatter& formatter)
    : _depVal(formatter.GetDependencyValidation())
	{
         // we have to parse through the configuration file and discover all of the operators that it's going to need
        StringSection<> keyname;
        while (formatter.TryKeyedItem(keyname)) {
            if (XlEqString(keyname, "LightOperators")) {
                RequireBeginElement(formatter);
                DeserializeLightOperators(formatter, _operatorResolveContext);
                RequireEndElement(formatter);
            } else if (XlEqString(keyname, "LightScene")) {
                RequireBeginElement(formatter);
                DeserializeLightSources(formatter);
                RequireEndElement(formatter);
            } else
                formatter.SkipValueOrElement();
        }
    }

	BasicLightingStateDelegate::~BasicLightingStateDelegate() {}

    ::Assets::PtrToFuturePtr<ILightingStateDelegate> CreateBasicLightingStateDelegate(StringSection<> envSettings)
    {
        auto result = std::make_shared<::Assets::FuturePtr<BasicLightingStateDelegate>>(envSettings.AsString());
        BasicLightingStateDelegate::ConstructToFuture(*result, envSettings);
        return std::reinterpret_pointer_cast<::Assets::FuturePtr<ILightingStateDelegate>>(result);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    LightDesc::LightDesc()
    {
        _orientation = Identity<Float3x3>();
		_position = Float3{0, 1, 0};
		_radii = Float2{0, 0};

        _cutoffBrightness = 0.01f;
        _brightness = Float3{1,1,1};
		_diffuseWideningMin = 1.0f;
		_diffuseWideningMax = 1.0f;

        _shape = RenderCore::LightingEngine::LightSourceShape::Directional;
        _diffuseModel = RenderCore::LightingEngine::DiffuseModel::Disney;
        _isDominantLight = false;
    }

    LightDesc DefaultDominantLight()
    {
        LightDesc light;
        light._shape = RenderCore::LightingEngine::LightSourceShape::Directional;
        light._position = Normalize(Float3(-0.15046243f, 0.97377890f, 0.17063323f));
        light._cutoffBrightness = 0.01f;
        light._brightness = Float3(3.2803922f, 2.2372551f, 1.9627452f);
        light._diffuseWideningMax = .9f;
        light._diffuseWideningMin = 0.2f;
        return light;
    }

    EnvironmentalLightingDesc DefaultEnvironmentalLightingDesc()
    {
        EnvironmentalLightingDesc result;
        result._ambientLight = Float3(0.f, 0.f, 0.f);
        result._skyTexture = "xleres/defaultresources/sky/samplesky2.dds";
        result._diffuseIBL = "xleres/defaultresources/sky/samplesky2_diffuse.dds";
        result._specularIBL = "xleres/defaultresources/sky/samplesky2_specular.dds";
        result._skyTextureType = RenderCore::LightingEngine::SkyTextureType::Cube;
        result._skyReflectionScale = 1.f;
        result._doAtmosphereBlur = false;
        return result;
    }

#if 0
    EnvironmentSettings DefaultEnvironmentSettings()
    {
        EnvironmentSettings result;
        result._environmentalLightingDesc = DefaultEnvironmentalLightingDesc();

        auto defLight = DefaultDominantLight();
        result._lights.push_back(defLight);

        auto frustumSettings = DefaultSunSourceFrustumSettings();
        result._sunSourceShadowProj.push_back(EnvironmentSettings::SunSourceShadowProj { 0, frustumSettings });

        if (constant_expression<false>::result()) {
            LightDesc secondaryLight;
            secondaryLight._shape = RenderCore::LightingEngine::LightSourceShape::Directional;
            secondaryLight._position = Normalize(Float3(0.71622938f, 0.48972201f, -0.49717990f));
            secondaryLight._cutoffBrightness = 0.01f;
            secondaryLight._brightness = Float3(3.2803922f, 2.2372551f, 1.9627452f);
            secondaryLight._diffuseWideningMax = 2.f;
            secondaryLight._diffuseWideningMin = 0.5f;
            result._lights.push_back(secondaryLight);

            LightDesc tertiaryLight;
            tertiaryLight._shape = RenderCore::LightingEngine::LightSourceShape::Directional;
            tertiaryLight._position = Normalize(Float3(-0.75507462f, -0.62672323f, 0.19256261f));
            tertiaryLight._cutoffBrightness = 0.01f;
            tertiaryLight._brightness = Float3(0.13725491f, 0.18666667f, 0.18745099f);
            tertiaryLight._diffuseWideningMax = 2.f;
            tertiaryLight._diffuseWideningMin = 0.5f;
            result._lights.push_back(tertiaryLight);
        }

        return std::move(result);
    }
#endif

    SunSourceFrustumSettings DefaultSunSourceFrustumSettings()
    {
        SunSourceFrustumSettings result;
        result._maxFrustumCount = 3;
        result._maxDistanceFromCamera = 2000.f;
        result._focusDistance = 5.0f;
        result._flags = 0;
        result._textureSize = 2048;
        return result;
    }

    static ParameterBox::ParameterNameHash ParamHash(const char name[])
    {
        return ParameterBox::MakeParameterNameHash(name);
    }

    static Float3 AsFloat3Color(unsigned packedColor)
    {
        return Float3(
            (float)((packedColor >> 16) & 0xff) / 255.f,
            (float)((packedColor >>  8) & 0xff) / 255.f,
            (float)(packedColor & 0xff) / 255.f);
    }

    RenderCore::LightingEngine::EnvironmentalLightingDesc MakeEnvironmentalLightingDesc(const ParameterBox& props)
    {
        static const auto ambientHash = ParamHash("AmbientLight");
        static const auto ambientBrightnessHash = ParamHash("AmbientBrightness");

        static const auto skyTextureHash = ParamHash("SkyTexture");
        static const auto skyTextureTypeHash = ParamHash("SkyTextureType");
        static const auto skyReflectionScaleHash = ParamHash("SkyReflectionScale");
        static const auto skyReflectionBlurriness = ParamHash("SkyReflectionBlurriness");
        static const auto skyBrightness = ParamHash("SkyBrightness");
        static const auto diffuseIBLHash = ParamHash("DiffuseIBL");
        static const auto specularIBLHash = ParamHash("SpecularIBL");

        static const auto rangeFogInscatterHash = ParamHash("RangeFogInscatter");
        static const auto rangeFogInscatterReciprocalScaleHash = ParamHash("RangeFogInscatterReciprocalScale");
        static const auto rangeFogInscatterScaleHash = ParamHash("RangeFogInscatterScale");
        static const auto rangeFogThicknessReciprocalScaleHash = ParamHash("RangeFogThicknessReciprocalScale");

        static const auto atmosBlurStdDevHash = ParamHash("AtmosBlurStdDev");
        static const auto atmosBlurStartHash = ParamHash("AtmosBlurStart");
        static const auto atmosBlurEndHash = ParamHash("AtmosBlurEnd");

        static const auto flagsHash = ParamHash("Flags");

            ////////////////////////////////////////////////////////////

        RenderCore::LightingEngine::EnvironmentalLightingDesc result;
        result._ambientLight = props.GetParameter(ambientBrightnessHash, 1.f) * AsFloat3Color(props.GetParameter(ambientHash, ~0x0u));
        result._skyReflectionScale = props.GetParameter(skyReflectionScaleHash, result._skyReflectionScale);
        result._skyReflectionBlurriness = props.GetParameter(skyReflectionBlurriness, result._skyReflectionBlurriness);
        result._skyBrightness = props.GetParameter(skyBrightness, result._skyBrightness);
        result._skyTextureType = (RenderCore::LightingEngine::SkyTextureType)props.GetParameter(skyTextureTypeHash, unsigned(result._skyTextureType));

        float inscatterScaleScale = 1.f / std::max(1e-5f, props.GetParameter(rangeFogInscatterReciprocalScaleHash, 1.f));
        inscatterScaleScale = props.GetParameter(rangeFogInscatterScaleHash, inscatterScaleScale);
        result._rangeFogInscatter = inscatterScaleScale * AsFloat3Color(props.GetParameter(rangeFogInscatterHash, 0));
        result._rangeFogThickness = 1.f / std::max(1e-5f, props.GetParameter(rangeFogThicknessReciprocalScaleHash, 0.f));

        result._atmosBlurStdDev = props.GetParameter(atmosBlurStdDevHash, result._atmosBlurStdDev);
        result._atmosBlurStart = props.GetParameter(atmosBlurStartHash, result._atmosBlurStart);
        result._atmosBlurEnd = std::max(result._atmosBlurStart, props.GetParameter(atmosBlurEndHash, result._atmosBlurEnd));

        auto flags = props.GetParameter<unsigned>(flagsHash);
        if (flags.has_value()) {
            result._doAtmosphereBlur = !!(flags.value() & (1<<0));
            result._doRangeFog = !!(flags.value() & (1<<1));
        }

        auto skyTexture = props.GetParameterAsString(skyTextureHash);
        if (skyTexture.has_value())
            result._skyTexture = skyTexture.value();
        auto diffuseIBL = props.GetParameterAsString(diffuseIBLHash);
        if (diffuseIBL.has_value())
            result._diffuseIBL = diffuseIBL.value();
        auto specularIBL = props.GetParameterAsString(specularIBLHash);
        if (specularIBL.has_value())
            result._specularIBL = specularIBL.value();

		// If we don't have a diffuse IBL texture, or specular IBL texture, then attempt to build
		// the filename from the sky texture
		if ((result._diffuseIBL.empty() || result._specularIBL.empty()) && !result._skyTexture.empty()) {
			auto splitter = MakeFileNameSplitter(result._skyTexture);

			if (result._diffuseIBL.empty())
				result._diffuseIBL = Concatenate(MakeStringSection(splitter.DriveAndPath().begin(), splitter.File().end()), "_diffuse", splitter.ExtensionWithPeriod());

			if (result._specularIBL.empty())
				result._specularIBL = Concatenate(MakeStringSection(splitter.DriveAndPath().begin(), splitter.File().end()), "_specular", splitter.ExtensionWithPeriod());
		}

        return result;
    }

    LightDesc MakeLightDesc(const Utility::ParameterBox& props)
    {
        static const auto colorHash = ParameterBox::MakeParameterNameHash("Color");
        static const auto brightnessHash = ParameterBox::MakeParameterNameHash("Brightness");
        static const auto diffuseModel = ParameterBox::MakeParameterNameHash("DiffuseModel");
        static const auto diffuseWideningMin = ParameterBox::MakeParameterNameHash("DiffuseWideningMin");
        static const auto diffuseWideningMax = ParameterBox::MakeParameterNameHash("DiffuseWideningMax");
        static const auto cutoffBrightness = ParameterBox::MakeParameterNameHash("CutoffBrightness");
        static const auto shape = ParameterBox::MakeParameterNameHash("Shape");

        LightDesc result;
        result._brightness = props.GetParameter(brightnessHash, 1.f) * AsFloat3Color(props.GetParameter(colorHash, ~0x0u));

        result._diffuseWideningMin = props.GetParameter(diffuseWideningMin, result._diffuseWideningMin);
        result._diffuseWideningMax = props.GetParameter(diffuseWideningMax, result._diffuseWideningMax);
        result._cutoffBrightness = props.GetParameter(cutoffBrightness, result._cutoffBrightness);

        result._shape = (RenderCore::LightingEngine::LightSourceShape)props.GetParameter(shape, unsigned(result._shape));

        result._diffuseModel = (RenderCore::LightingEngine::DiffuseModel)props.GetParameter(diffuseModel, 1);
        // auto shadowModel = props.GetParameter(shadowResolveModel, 0);
        return result;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
    static void ReadTransform(LightDesc& light, const ParameterBox& props)
    {
        static const auto transformHash = ParameterBox::MakeParameterNameHash("Transform");
        auto transform = Transpose(props.GetParameter(transformHash, Identity<Float4x4>()));

        ScaleRotationTranslationM decomposed(transform);
        light._position = decomposed._translation;
        light._orientation = decomposed._rotation;
        light._radii = Float2(decomposed._scale[0], decomposed._scale[1]);

            // For directional lights we need to normalize the position (it will be treated as a direction)
        if (light._shape == RenderCore::LightingEngine::LightSourceShape::Directional)
            light._position = (MagnitudeSquared(light._position) > 1e-5f) ? Normalize(light._position) : Float3(0.f, 0.f, 0.f);
    }
    
    namespace EntityTypeName
    {
        static const auto* EnvSettings = (const utf8*)"EnvSettings";
        static const auto* AmbientSettings = (const utf8*)"AmbientSettings";
        static const auto* DirectionalLight = (const utf8*)"DirectionalLight";
        static const auto* AreaLight = (const utf8*)"AreaLight";
        static const auto* ToneMapSettings = (const utf8*)"ToneMapSettings";
        static const auto* ShadowFrustumSettings = (const utf8*)"ShadowFrustumSettings";

        static const auto* OceanLightingSettings = (const utf8*)"OceanLightingSettings";
        static const auto* OceanSettings = (const utf8*)"OceanSettings";
        static const auto* FogVolumeRenderer = (const utf8*)"FogVolumeRenderer";
    }
    
    namespace Attribute
    {
        static const auto AttachedLight = ParameterBox::MakeParameterNameHash("Light");
        static const auto Name = ParameterBox::MakeParameterNameHash("Name");
        static const auto Flags = ParameterBox::MakeParameterNameHash("Flags");
    }

    EnvironmentSettings::EnvironmentSettings(
        InputStreamFormatter<utf8>& formatter, 
        const ::Assets::DirectorySearchRules&,
		const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
    {
        using namespace SceneEngine;

        _environmentalLightingDesc = DefaultEnvironmentalLightingDesc();

        std::vector<std::pair<uint64, SunSourceFrustumSettings>> shadowSettings;
        std::vector<uint64> lightNames;
        std::vector<std::pair<uint64, uint64>> lightFrustumLink;    // lightid to shadow settings map

        utf8 buffer[256];

        bool exit = false;
        StringSection<> name;
        while (formatter.TryKeyedItem(name)) {
            switch(formatter.PeekNext()) {
            case FormatterBlob::BeginElement:
                {
                    RequireBeginElement(formatter);

                    if (XlEqString(name, EntityTypeName::AmbientSettings)) {
                        _environmentalLightingDesc = MakeEnvironmentalLightingDesc(ParameterBox(formatter));
                    } else if (XlEqString(name, EntityTypeName::ToneMapSettings)) {
                        AccessorDeserialize(formatter, _toneMapSettings);
                    } else if (XlEqString(name, EntityTypeName::DirectionalLight) || XlEqString(name, EntityTypeName::AreaLight)) {

                        ParameterBox params(formatter);
                        uint64 hashName = 0ull;
                        auto paramValue = params.GetParameterAsString(Attribute::Name);
                        if (paramValue.has_value())
                            hashName = Hash64(paramValue.value());

                        auto lightDesc = MakeLightDesc(params);
                        if (XlEqString(name, EntityTypeName::DirectionalLight))
                            lightDesc._shape = RenderCore::LightingEngine::LightSourceShape::Directional;
                        ReadTransform(lightDesc, params);

                        _lights.push_back(lightDesc);

                        if (params.GetParameter(Attribute::Flags, 0u) & (1<<0)) {
                            lightNames.push_back(hashName);
                        } else {
                            lightNames.push_back(0);    // dummy if shadows are disabled
                        }
                        
                    } else if (XlEqString(name, EntityTypeName::ShadowFrustumSettings)) {

                        ParameterBox params(formatter);
                        uint64 hashName = 0ull;
                        auto paramValue = params.GetParameterAsString(Attribute::Name);
                        if (paramValue.has_value())
                            hashName = Hash64(paramValue.value());

                        shadowSettings.push_back(
                            std::make_pair(hashName, CreateFromParameters<SunSourceFrustumSettings>(params)));

                        uint64 frustumLink = 0;
                        paramValue = params.GetParameterAsString(Attribute::AttachedLight);
                        if (paramValue.has_value())
                            frustumLink = Hash64(paramValue.value());
                        lightFrustumLink.push_back(std::make_pair(frustumLink, hashName));

#if 0
                    } else if (XlEqString(name, EntityTypeName::OceanLightingSettings)) {
                        _oceanLighting = OceanLightingSettings(ParameterBox(formatter));
                    } else if (XlEqString(name, EntityTypeName::OceanSettings)) {
                        _deepOceanSim = DeepOceanSimSettings(ParameterBox(formatter));
                    } else if (XlEqString(name, EntityTypeName::FogVolumeRenderer)) {
                        _volFogRenderer = VolumetricFogConfig::Renderer(formatter);
#endif
                    } else
                        SkipElement(formatter);
                    
                    RequireEndElement(formatter);
                    break;
                }

            case FormatterBlob::Value:
                RequireStringValue(formatter);
                break;

            default:
                Throw(FormatException("Expected value or element", formatter.GetLocation()));
            }
        }

            // bind shadow settings (mapping via the light name parameter)
        for (unsigned c=0; c<lightFrustumLink.size(); ++c) {
            auto f = std::find_if(shadowSettings.cbegin(), shadowSettings.cend(), 
                [&lightFrustumLink, c](const std::pair<uint64, SunSourceFrustumSettings>&i) { return i.first == lightFrustumLink[c].second; });

            auto l = std::find(lightNames.cbegin(), lightNames.cend(), lightFrustumLink[c].first);

            if (f != shadowSettings.end() && l != lightNames.end()) {
                auto lightIndex = std::distance(lightNames.cbegin(), l);
                assert(lightIndex < ptrdiff_t(_lights.size()));

                _sunSourceShadowProj.push_back(
                    EnvironmentSettings::SunSourceShadowProj { unsigned(lightIndex), f->second });
                _lights[lightIndex]._isDominantLight = true;
            }
        }
    }

    EnvironmentSettings::EnvironmentSettings() {}
    EnvironmentSettings::~EnvironmentSettings() {}
#endif

    /*std::vector<std::pair<std::string, SceneEngine::EnvironmentSettings>> 
        DeserializeEnvSettings(InputStreamFormatter<utf8>& formatter)
    {
        std::vector<std::pair<std::string, SceneEngine::EnvironmentSettings>> result;
        for (;;) {
            switch(formatter.PeekNext()) {
            case InputStreamFormatter<utf8>::Blob::BeginElement:
                {
                    InputStreamFormatter<utf8>::InteriorSection name;
                    if (!formatter.TryBeginElement(name)) break;
                    auto settings = DeserializeSingleSettings(formatter);
                    if (!formatter.TryEndElement()) break;

                    result.emplace_back(std::move(settings));
                    break;
                }

            default:
                return std::move(result);
            }
        }
    }*/
}

#if 1

namespace SceneEngine
{
    ToneMapSettings::ToneMapSettings() {}
}

#endif

template<> const ClassAccessors& Legacy_GetAccessors<SceneEngine::ToneMapSettings>()
{
    static ClassAccessors dummy(0);
    return dummy;
}

template<typename MemberType, const char* EnumToString(MemberType), std::optional<MemberType> StringToEnum(StringSection<>), typename ObjectType>
	void AddStringToEnum(ClassAccessors& accessors, StringSection<> propName, MemberType ObjectType::*ptrToMember)
{
	accessors.Add(
		propName,
		[ptrToMember](const ObjectType& obj) {
			return EnumToString(obj.*ptrToMember);
		},
		[ptrToMember](ObjectType& obj, StringSection<> newValue) {
			auto conversion = StringToEnum(newValue);
			if (!conversion.has_value())
				Throw(std::runtime_error(std::string{"The enum value "} + newValue.AsString() + " was not understood"));
			obj.*ptrToMember = conversion.value();
		});
}

template<> const ClassAccessors& Legacy_GetAccessors<RenderCore::LightingEngine::LightSourceOperatorDesc>()
{
	using Obj = RenderCore::LightingEngine::LightSourceOperatorDesc;
	static ClassAccessors props(typeid(Obj).hash_code());
	static bool init = false;
	if (!init) {
		AddStringToEnum<RenderCore::LightingEngine::LightSourceShape, RenderCore::LightingEngine::AsString, RenderCore::LightingEngine::AsLightSourceShape>(props, "Shape", &Obj::_shape);
		AddStringToEnum<RenderCore::LightingEngine::DiffuseModel, RenderCore::LightingEngine::AsString, RenderCore::LightingEngine::AsDiffuseModel>(props, "DiffuseModel", &Obj::_diffuseModel);
        props.Add(
            "DominantLight",
            [](const Obj& obj) { return !!(obj._flags & RenderCore::LightingEngine::LightSourceOperatorDesc::Flags::DominantLight); },
            [](Obj& obj, uint32_t value) { 
                if (value) {
                    obj._flags |= RenderCore::LightingEngine::LightSourceOperatorDesc::Flags::DominantLight;
                } else {
                    obj._flags &= ~RenderCore::LightingEngine::LightSourceOperatorDesc::Flags::DominantLight;
                }
            });
		init = true;
	}
	return props;
}

template<> const ClassAccessors& Legacy_GetAccessors<RenderCore::LightingEngine::AmbientLightOperatorDesc>()
{
	using Obj = RenderCore::LightingEngine::AmbientLightOperatorDesc;
	static ClassAccessors props(typeid(Obj).hash_code());
	static bool init = false;
	if (!init) {
		init = true;
	}
	return props;
}

template<> const ClassAccessors& Legacy_GetAccessors<RenderCore::LightingEngine::ShadowOperatorDesc>()
{
	using Obj = RenderCore::LightingEngine::ShadowOperatorDesc;
	static ClassAccessors props(typeid(Obj).hash_code());
	static bool init = false;
	if (!init) {
		AddStringToEnum<RenderCore::Format, RenderCore::AsString, RenderCore::AsFormat>(props, "Format", &Obj::_format);
		AddStringToEnum<RenderCore::LightingEngine::ShadowResolveType, RenderCore::LightingEngine::AsString, RenderCore::LightingEngine::AsShadowResolveType>(props, "ResolveType", &Obj::_resolveType);
		AddStringToEnum<RenderCore::LightingEngine::ShadowProjectionMode, RenderCore::LightingEngine::AsString, RenderCore::LightingEngine::AsShadowProjectionMode>(props, "ProjectionMode", &Obj::_projectionMode);
		AddStringToEnum<RenderCore::LightingEngine::ShadowFilterModel, RenderCore::LightingEngine::AsString, RenderCore::LightingEngine::AsShadowFilterModel>(props, "FilterModel", &Obj::_filterModel);
		props.Add("Dims", [](const Obj& obj) { return obj._width; }, [](Obj& obj, uint32_t value) { obj._width = obj._height = value; });
		props.Add("SlopeScaledBias", &Obj::_slopeScaledBias);
		init = true;
	}
	return props;
}
