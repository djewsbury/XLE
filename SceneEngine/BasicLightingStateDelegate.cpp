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
        ObjectTable<RenderCore::LightingEngine::SunSourceFrustumSettings> _sunSourceFrustumSettingsInCfgFile;
        std::vector<std::pair<uint64_t, uint64_t>> _shadowToAssociatedLight;

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
        const ParameterBox::ParameterName Radius = "Radius";
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
                        auto positionValue = light._parameters.GetParameter<Float3>(Position);
                        auto radiusValue = light._parameters.GetParameter<Float3>(Radius);
                        
                        if (positionValue || radiusValue) {
                            ScaleTranslation st;
                            if (positionValue)
                                st._translation = positionValue.value();
                            if (radiusValue)
                                st._scale = radiusValue.value();
                            positional->SetLocalToWorld(AsFloat4x4(st));
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

        for (const auto& sunSource:_sunSourceFrustumSettingsInCfgFile._objects) {
            auto op = LowerBound(_sunSourceHashToShadowOperatorId, sunSource.first);
            if (op == _sunSourceHashToShadowOperatorId.end() || op->first != sunSource.first) continue;

            auto lightAssociation = std::find_if(
                _shadowToAssociatedLight.begin(), _shadowToAssociatedLight.end(), 
                [sunSource](const auto& c) { return c.first == sunSource.first; });
            if (lightAssociation == _shadowToAssociatedLight.end()) continue;        // not tied to a specific light

            auto lightId = std::find_if(
                lightNameToId.begin(), lightNameToId.end(), 
                [lightAssociation](const auto& c) { return c.first == lightAssociation->second; });
            if (lightId == lightNameToId.end()) continue;        // couldn't find the associated light
            
            auto newShadow = RenderCore::LightingEngine::CreateSunSourceShadows(
                lightScene, op->second, lightId->second, sunSource.second);
            _shadowProjectionsInBoundScene.push_back(newShadow);
        }

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
        for (const auto& c:_sunSourceFrustumSettingsInCfgFile._objects) {
            auto shadowOperator = RenderCore::LightingEngine::CalculateShadowOperatorDesc(c.second);
            auto h = shadowOperator.Hash();
            auto i = std::find_if(result._shadowResolveOperators.begin(), result._shadowResolveOperators.end(), [h](const auto& c) { return c.Hash() == h; });
            if (i==result._shadowResolveOperators.end())
                i = result._shadowResolveOperators.insert(i, shadowOperator);
            _sunSourceHashToShadowOperatorId.emplace_back(c.first, (unsigned)std::distance(result._shadowResolveOperators.begin(), i));
        }

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
                
                std::vector<decltype(_sunSourceFrustumSettingsInCfgFile)::PendingProperty> properties; 
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
                auto hashName = _sunSourceFrustumSettingsInCfgFile.DeserializeObject(name, properties);
                if (!associatedLight.IsEmpty())
                    _shadowToAssociatedLight.emplace_back(hashName, Hash64(associatedLight));

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
