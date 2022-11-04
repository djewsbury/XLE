// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BasicLightingStateDelegate.h"
#include "LightSceneConfiguration.h"
#include "../RenderCore/LightingEngine/SunSourceConfiguration.h"
#include "../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../Formatters/IDynamicFormatter.h"
#include "../Tools/EntityInterface/EntityInterface.h"
#include "../Tools/ToolsRig/ToolsRigServices.h"
#include "../Assets/Assets.h"
#include "../Assets/Continuation.h"
#include "../Assets/ConfigFileContainer.h"
#include "../Math/Transformations.h"
#include "../Math/MathSerialization.h"
#include "../Utility/StringUtils.h"
#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/Streams/FormatterUtils.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Conversion.h"
// #include "../Utility/Meta/AccessorSerialize.h"
// #include "../Utility/Meta/ClassAccessors.h"
 
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

        void BindScene(RenderCore::LightingEngine::ILightScene& lightScene)
        {
            assert(_operatorId != ~0u);
            assert(_lightSources.empty());
            const auto tileLightCount = 32u;
            for (unsigned c=0; c<tileLightCount; ++c) {
                auto lightId = lightScene.CreateLightSource(_operatorId);
                _lightSources.push_back(lightId);
            }
        }

        void UnbindScene(RenderCore::LightingEngine::ILightScene& lightScene)
        {
            for (auto l:_lightSources)
                lightScene.DestroyLightSource(l);
            _lightSources.clear();
        }

        void        BindCfg(MergedLightingEngineCfg& cfg)
        {
            RenderCore::LightingEngine::LightSourceOperatorDesc opDesc;
            opDesc._shape = RenderCore::LightingEngine::LightSourceShape::Sphere;
            _operatorId = cfg.Register(opDesc);
        }

        SwirlingPointLights()
        {
            _time = 0.f;
        }
        float _time;
        unsigned _operatorId = ~0u;
    };

    class BasicLightingStateDelegate : public ILightingStateDelegate
    {
    public:
        void        PreRender(const RenderCore::Techniques::ProjectionDesc& mainSceneCameraDesc, RenderCore::LightingEngine::ILightScene& lightScene) override;
        void        PostRender(RenderCore::LightingEngine::ILightScene& lightScene) override;
        void        BindScene(RenderCore::LightingEngine::ILightScene& lightScene, std::shared_ptr<::Assets::OperationContext>) override;
        void        UnbindScene(RenderCore::LightingEngine::ILightScene& lightScene) override;
        auto        BeginPrepareStep(RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::IThreadContext& threadContext) -> std::shared_ptr<RenderCore::LightingEngine::IProbeRenderingInstance> override;

        void        BindCfg(MergedLightingEngineCfg& cfg) override;

		BasicLightingStateDelegate(Formatters::IDynamicFormatter& formatter);
		~BasicLightingStateDelegate();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<BasicLightingStateDelegate>>&& promise,
			StringSection<> envSettingFileName);

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

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

        std::vector<std::pair<uint64_t, RenderCore::LightingEngine::ILightScene::LightOperatorId>> _lightOperatorHashToId;
        std::vector<std::pair<uint64_t, RenderCore::LightingEngine::ILightScene::ShadowOperatorId>> _shadowOperatorHashToId;
        std::vector<std::pair<uint64_t, RenderCore::LightingEngine::ILightScene::ShadowOperatorId>> _sunSourceHashToShadowOperatorId;
        uint64_t _ambientOperator = ~0ull;

        ::Assets::DependencyValidation _depVal;

        SwirlingPointLights _swirlingLights;

        void DeserializeLightSources(Formatters::IDynamicFormatter& formatter);
    };

    void BasicLightingStateDelegate::PreRender(
        const RenderCore::Techniques::ProjectionDesc& mainSceneCameraDesc,
        RenderCore::LightingEngine::ILightScene& lightScene)
    {
        _swirlingLights.UpdateLights(lightScene);
    }

    void        BasicLightingStateDelegate::PostRender(RenderCore::LightingEngine::ILightScene& lightScene)
    {
    }

    void        BasicLightingStateDelegate::BindScene(RenderCore::LightingEngine::ILightScene& lightScene, std::shared_ptr<::Assets::OperationContext> operationContext)
    {
        const ParameterBox::ParameterName SkyTexture = "SkyTexture";

        std::vector<std::pair<uint64_t, RenderCore::LightingEngine::ILightScene::LightSourceId>> lightNameToId;

        for (const auto&light:_lightSourcesInCfgFile) {
            if (!light._operatorHash) continue;

            auto lightOperator = LowerBound(_lightOperatorHashToId, light._operatorHash);
            if (lightOperator != _lightOperatorHashToId.end() && lightOperator->first == light._operatorHash) {

                auto newLight = lightScene.CreateLightSource(lightOperator->second);
                _lightSourcesInBoundScene.push_back(newLight);
                InitializeLight(lightScene, newLight, light._parameters, Zero<Float3>());
                lightNameToId.emplace_back(Hash64(light._name), newLight);

                continue;
            }

            if (light._operatorHash == _ambientOperator) {
                auto newLight = lightScene.CreateAmbientLightSource();
                _lightSourcesInBoundScene.push_back(newLight);

                auto* distanceIBL = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IDistantIBLSource>(newLight);
                if (distanceIBL) {
                    distanceIBL->SetEquirectangularSource(operationContext, light._parameters.GetParameterAsString(SkyTexture).value());
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
            
            lightScene.SetShadowOperator(lightId->second, op->second);
            RenderCore::LightingEngine::SetupSunSourceShadows(
                lightScene, lightId->second, sunSource.second);
        }

        _swirlingLights.BindScene(lightScene);
    }

    void        BasicLightingStateDelegate::UnbindScene(RenderCore::LightingEngine::ILightScene& lightScene)
    {
        _swirlingLights.UnbindScene(lightScene);
        for (auto lightSource:_lightSourcesInBoundScene)
            lightScene.DestroyLightSource(lightSource);
        _lightSourcesInBoundScene.clear();
    }

    std::shared_ptr<RenderCore::LightingEngine::IProbeRenderingInstance> BasicLightingStateDelegate::BeginPrepareStep(RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::IThreadContext& threadContext)
    {
        return nullptr;
    }

    void BasicLightingStateDelegate::BindCfg(MergedLightingEngineCfg& cfg)
    {
        _lightOperatorHashToId.clear();
        _shadowOperatorHashToId.clear();
        _ambientOperator = ~0ull;
        _sunSourceHashToShadowOperatorId.clear();

        _lightOperatorHashToId.reserve(_operatorResolveContext._lightSourceOperators._objects.size());
        _shadowOperatorHashToId.reserve(_operatorResolveContext._lightSourceOperators._objects.size());
        for (const auto& c:_operatorResolveContext._lightSourceOperators._objects)
            _lightOperatorHashToId.emplace_back(c.first, cfg.Register(c.second));
        for (const auto& c:_operatorResolveContext._shadowOperators._objects)
            _shadowOperatorHashToId.emplace_back(c.first, cfg.Register(c.second));
        for (const auto& c:_sunSourceFrustumSettingsInCfgFile._objects) {
            auto shadowOperator = RenderCore::LightingEngine::CalculateShadowOperatorDesc(c.second);
            _sunSourceHashToShadowOperatorId.emplace_back(c.first, cfg.Register(shadowOperator));
        }

        _swirlingLights.BindCfg(cfg);

        if (!_operatorResolveContext._ambientOperators._objects.empty()) {
            cfg.SetAmbientOperator(_operatorResolveContext._ambientOperators._objects[0].second);
            _ambientOperator = _operatorResolveContext._ambientOperators._objects[0].first;
        }

        std::sort(_lightOperatorHashToId.begin(), _lightOperatorHashToId.end(), CompareFirst<uint64_t, unsigned>());
        std::sort(_shadowOperatorHashToId.begin(), _shadowOperatorHashToId.end(), CompareFirst<uint64_t, unsigned>());
        std::sort(_sunSourceHashToShadowOperatorId.begin(), _sunSourceHashToShadowOperatorId.end(), CompareFirst<uint64_t, unsigned>());
    }

    void BasicLightingStateDelegate::DeserializeLightSources(Formatters::IDynamicFormatter& formatter)
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
                        ImpliedTyping::TypeDesc type;
                        auto value = RequireRawValue(formatter, type);
                        lightProperties.SetParameter(keyname, value, type);
                    }
                }
                RequireEndElement(formatter);

                auto i = std::find_if(
                    _lightSourcesInCfgFile.begin(), _lightSourcesInCfgFile.end(),
                    [name](const auto& c) { return XlEqString(name, c._name); });
                if (!name.IsEmpty() && i != _lightSourcesInCfgFile.end()) {
                    i->_operatorHash = operatorHash ? operatorHash : i->_operatorHash;
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
                if (!associatedLight.IsEmpty() && hashName.has_value())
                    _shadowToAssociatedLight.emplace_back(hashName.value(), Hash64(associatedLight));

            } else {
                SkipValueOrElement(formatter);
            }
        }
    }

	void BasicLightingStateDelegate::ConstructToPromise(
		std::promise<std::shared_ptr<BasicLightingStateDelegate>>&& promise,
		StringSection<> envSettingFileName)
	{
        auto fmttrFuture = ToolsRig::Services::GetEntityMountingTree().BeginFormatter(envSettingFileName);
        ::Assets::WhenAll(std::move(fmttrFuture)).ThenConstructToPromise(
            std::move(promise),
            [](auto fmttr) { return std::make_shared<BasicLightingStateDelegate>(*fmttr); });
	}

	BasicLightingStateDelegate::BasicLightingStateDelegate(
		Formatters::IDynamicFormatter& formatter)
    : _depVal(formatter.GetDependencyValidation())
	{
         // we have to parse through the configuration file and discover all of the operators that it's going to need
        StringSection<> keyname;
        while (formatter.TryKeyedItem(keyname)) {
            if (XlEqString(keyname, "LightOperators")) {
                RequireBeginElement(formatter);
                _operatorResolveContext.Deserialize(formatter);
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

    ::Assets::PtrToMarkerPtr<ILightingStateDelegate> CreateBasicLightingStateDelegate(StringSection<> envSettings)
    {
        auto result = std::make_shared<::Assets::MarkerPtr<BasicLightingStateDelegate>>(envSettings.AsString());
        BasicLightingStateDelegate::ConstructToPromise(result->AdoptPromise(), envSettings);
        return std::reinterpret_pointer_cast<::Assets::MarkerPtr<ILightingStateDelegate>>(result);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
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
#endif

    RenderCore::LightingEngine::SunSourceFrustumSettings DefaultSunSourceFrustumSettings()
    {
        RenderCore::LightingEngine::SunSourceFrustumSettings result;
        result._maxFrustumCount = 3;
        result._maxDistanceFromCamera = 2000.f;
        result._focusDistance = 5.0f;
        result._flags = 0;
        result._textureSize = 2048;
        return result;
    }

#if 0
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
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////

    unsigned MergedLightingEngineCfg::Register(const RenderCore::LightingEngine::LightSourceOperatorDesc& operatorDesc)
    {
        assert(_lightHashes.size() == _lightResolveOperators.size());
        auto hash = operatorDesc.GetHash();
        auto i = std::find(_lightHashes.begin(), _lightHashes.end(), hash);
        if (i != _lightHashes.end())
            return (unsigned)std::distance(_lightHashes.begin(), i);
        _lightResolveOperators.push_back(operatorDesc);
        _lightHashes.push_back(hash);
        return unsigned(_lightHashes.size()-1);
    }

    unsigned MergedLightingEngineCfg::Register(const RenderCore::LightingEngine::ShadowOperatorDesc& operatorDesc)
    {
        assert(_shadowHashes.size() == _shadowResolveOperators.size());
        auto hash = operatorDesc.GetHash();
        auto i = std::find(_shadowHashes.begin(), _shadowHashes.end(), hash);
        if (i != _shadowHashes.end())
            return (unsigned)std::distance(_shadowHashes.begin(), i);
        _shadowResolveOperators.push_back(operatorDesc);
        _shadowHashes.push_back(hash);
        return unsigned(_shadowHashes.size()-1);
    }

    void MergedLightingEngineCfg::SetAmbientOperator(const RenderCore::LightingEngine::AmbientLightOperatorDesc& operatorDesc)
    {
        _ambientOperator = operatorDesc;
    }

    std::future<void> IScene::PrepareForView(PrepareForViewContext& prepareContext) const { return {}; }
    IScene::~IScene() {}
    ISceneOverlay::~ISceneOverlay() {}


    static const ParameterBox::ParameterName LocalToWorld = "LocalToWorld";
    static const ParameterBox::ParameterName Position = "Position";
    static const ParameterBox::ParameterName Radius = "Radius";
    static const ParameterBox::ParameterName Brightness = "Brightness";
    static const ParameterBox::ParameterName CutoffBrightness = "CutoffBrightness";
    static const ParameterBox::ParameterName CutoffRange = "CutoffRange";
    static const ParameterBox::ParameterName DiffuseWideningMin = "DiffuseWideningMin";
    static const ParameterBox::ParameterName DiffuseWideningMax = "DiffuseWideningMax";
    static const ParameterBox::ParameterName EquirectangularSource = "EquirectangularSource";

    void InitializeLight(
        RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId sourceId,
        const ParameterBox& parameters,
        const Float3& offsetLocalToWorld)
    {
        auto* positional = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(sourceId);
        if (positional) {
            auto transformValue = parameters.GetParameter<Float4x4>(LocalToWorld);
            if (transformValue) {
                Combine_IntoLHS(transformValue.value(), offsetLocalToWorld);
                positional->SetLocalToWorld(transformValue.value());
            } else {
                auto positionValue = parameters.GetParameter<Float3>(Position);
                auto radiusValue = parameters.GetParameter<Float3>(Radius);
                
                if (positionValue || radiusValue) {
                    ScaleTranslation st;
                    if (positionValue)
                        st._translation = positionValue.value();
                    if (radiusValue)
                        st._scale = radiusValue.value();
                    st._translation += offsetLocalToWorld;
                    positional->SetLocalToWorld(AsFloat4x4(st));
                }
            }
        }

        auto* uniformEmittance = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(sourceId);
        if (uniformEmittance) {
            auto brightness = parameters.GetParameter<Float3>(Brightness);
            if (brightness)
                uniformEmittance->SetBrightness(brightness.value());

            auto wideningMin = parameters.GetParameter<float>(DiffuseWideningMin);
            auto wideningMax = parameters.GetParameter<float>(DiffuseWideningMax);
            if (wideningMin && wideningMax)
                uniformEmittance->SetDiffuseWideningFactors({wideningMin.value(), wideningMax.value()});
        }

        auto* finite = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IFiniteLightSource>(sourceId);
        if (finite) {
            auto cutoffBrightness = parameters.GetParameter<float>(CutoffBrightness);
            if (cutoffBrightness)
                finite->SetCutoffBrightness(cutoffBrightness.value());
            auto cutoffRange = parameters.GetParameter<float>(CutoffRange);
            if (cutoffRange)
                finite->SetCutoffRange(cutoffRange.value());
        }

        auto* distantIBL = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IDistantIBLSource>(sourceId);
        if (distantIBL) {
            auto src = parameters.GetParameterAsString(EquirectangularSource);
            if (src)
                distantIBL->SetEquirectangularSource(nullptr, src.value());     // todo -- Assets::OperationContext
        }
    }

    template <typename Type>
        std::optional<Type> ConvertOrCast(IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
    {
        if ((type._type == ImpliedTyping::TypeCat::UInt8 || type._type == ImpliedTyping::TypeCat::Int8) && type._typeHint == ImpliedTyping::TypeHint::String)
            return ImpliedTyping::ConvertFullMatch<Type>(MakeStringSection((const char*)data.begin(), (const char*)data.end()));

        Type result;
        if (ImpliedTyping::Cast(MakeOpaqueIteratorRange(result), ImpliedTyping::TypeOf<Type>(), data, type))
            return result;
        return {};
    }

    bool SetProperty(
        RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId sourceId,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
    {
        if (propertyNameHash == LocalToWorld._hash) {
            if (auto* positional = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(sourceId)) {
                if (auto localToWorld = ConvertOrCast<Float4x4>(data, type)) {
                    positional->SetLocalToWorld(localToWorld.value());
                    return true;
                }
            }
        } else if (propertyNameHash == Position._hash) {
            if (auto* positional = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(sourceId)) {
                if (auto position = ConvertOrCast<Float3>(data, type)) {
                    Float4x4 localToWorld = positional->GetLocalToWorld();
                    SetTranslation(localToWorld, position.value());
                    positional->SetLocalToWorld(localToWorld);
                    return true;
                }
            }
        } else if (propertyNameHash == Radius._hash) {
            if (auto* positional = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(sourceId)) {
                if (auto radius = ConvertOrCast<Float3>(data, type)) {
                    ScaleRotationTranslationM srt{positional->GetLocalToWorld()};
                    srt._scale = radius.value();
                    positional->SetLocalToWorld(AsFloat4x4(srt));
                    return true;
                }
            }
        } else if (propertyNameHash == Brightness._hash) {
            if (auto* uniformEmittance = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(sourceId)) {
                if (auto brightness = ConvertOrCast<Float3>(data, type)) {
                    uniformEmittance->SetBrightness(brightness.value());
                    return true;
                }
            }
        } else if (propertyNameHash == DiffuseWideningMin._hash) {
            if (auto* uniformEmittance = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(sourceId)) {
                if (auto wideningMin = ConvertOrCast<float>(data, type)) {
                    uniformEmittance->SetDiffuseWideningFactors({wideningMin.value(), uniformEmittance->GetDiffuseWideningFactors()[1]});
                    return true;
                }
            }
        } else if (propertyNameHash == DiffuseWideningMax._hash) {
            if (auto* uniformEmittance = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(sourceId)) {
                if (auto wideningMax = ConvertOrCast<float>(data, type)) {
                    uniformEmittance->SetDiffuseWideningFactors({uniformEmittance->GetDiffuseWideningFactors()[0], wideningMax.value()});
                    return true;
                }
            }
        } else if (propertyNameHash == CutoffBrightness._hash) {
            if (auto* finite = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IFiniteLightSource>(sourceId)) {
                if (auto cutoffBrightness = ConvertOrCast<float>(data, type)) {
                    finite->SetCutoffBrightness(cutoffBrightness.value());
                    return true;
                }
            }
        } else if (propertyNameHash == CutoffRange._hash) {
            if (auto* finite = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IFiniteLightSource>(sourceId)) {
                if (auto cutoffRange = ConvertOrCast<float>(data, type)) {
                    finite->SetCutoffRange(cutoffRange.value());
                    return true;
                }
            }
        } else if (propertyNameHash == EquirectangularSource._hash) {
            if (auto* distantIBL = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IDistantIBLSource>(sourceId)) {
                auto src = ImpliedTyping::AsString(data, type);
                distantIBL->SetEquirectangularSource(nullptr, src);     // todo -- Assets::OperationContext
            }
        }

        return false;
    }

    static const ParameterBox::ParameterName Shape = "Shape";
    static const ParameterBox::ParameterName DiffuseModel = "DiffuseModel";
    static const ParameterBox::ParameterName DominantLight = "DominantLight";

    template<typename MemberType, std::optional<MemberType> StringToEnum(StringSection<>), typename ObjectType>
        static void SetViaEnumFn(ObjectType& dst, MemberType ObjectType::*ptrToMember, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
    {
        uint32_t intValue;
        if (type._typeHint != ImpliedTyping::TypeHint::String && ImpliedTyping::Cast(MakeOpaqueIteratorRange(intValue), ImpliedTyping::TypeOf<uint32_t>(), data, type)) {
            // just an int value, set directly from this int
            dst.*ptrToMember = (MemberType)intValue;
        } else {
            auto str = ImpliedTyping::AsString(data, type);
            auto o = StringToEnum(str);
            if (!o.has_value()) Throw(std::runtime_error("Unknown value for enum (" + str + ")"));
            dst.*ptrToMember = o.value();
        }
    }

    bool SetProperty(
        RenderCore::LightingEngine::LightSourceOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
    {
        using namespace RenderCore::LightingEngine;
        if (propertyNameHash == Shape._hash) {
            SetViaEnumFn<LightSourceShape, AsLightSourceShape>(desc, &LightSourceOperatorDesc::_shape, data, type);
            return true;
        } else if (propertyNameHash == DiffuseModel._hash) {
            SetViaEnumFn<RenderCore::LightingEngine::DiffuseModel, AsDiffuseModel>(desc, &LightSourceOperatorDesc::_diffuseModel, data, type);
            return true;
        } else if (propertyNameHash == DominantLight._hash) {
            if (auto value = ConvertOrCast<unsigned>(data, type)) {
                if (value.value()) {
                    desc._flags |= RenderCore::LightingEngine::LightSourceOperatorDesc::Flags::DominantLight;
                } else {
                    desc._flags &= ~RenderCore::LightingEngine::LightSourceOperatorDesc::Flags::DominantLight;
                }
                return true;
            }
        }
        return false;
    }

    static const ParameterBox::ParameterName Format = "Format";
    static const ParameterBox::ParameterName ResolveType = "ResolveType";
    static const ParameterBox::ParameterName ProjectionMode = "ProjectionMode";
    static const ParameterBox::ParameterName FilterModel = "FilterModel";
    static const ParameterBox::ParameterName CullMode = "CullMode";
    static const ParameterBox::ParameterName Dims = "Dims";
    static const ParameterBox::ParameterName SlopeScaledBias = "SlopeScaledBias";
    static const ParameterBox::ParameterName DepthBias = "DepthBias";
    static const ParameterBox::ParameterName DepthBiasClamp = "DepthBiasClamp";

    bool SetProperty(
        RenderCore::LightingEngine::ShadowOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
    {
        using namespace RenderCore::LightingEngine;
        if (propertyNameHash == Format._hash) {
            SetViaEnumFn<RenderCore::Format, RenderCore::AsFormat>(desc, &ShadowOperatorDesc::_format, data, type);
            return true;
        } else if (propertyNameHash == ResolveType._hash) {
            SetViaEnumFn<ShadowResolveType, AsShadowResolveType>(desc, &ShadowOperatorDesc::_resolveType, data, type);
            return true;
        } else if (propertyNameHash == ProjectionMode._hash) {
            SetViaEnumFn<ShadowProjectionMode, AsShadowProjectionMode>(desc, &ShadowOperatorDesc::_projectionMode, data, type);
            return true;
        } else if (propertyNameHash == FilterModel._hash) {
            SetViaEnumFn<ShadowFilterModel, AsShadowFilterModel>(desc, &ShadowOperatorDesc::_filterModel, data, type);
            return true;
        } else if (propertyNameHash == CullMode._hash) {
            SetViaEnumFn<RenderCore::CullMode, RenderCore::AsCullMode>(desc, &ShadowOperatorDesc::_cullMode, data, type);
            return true;
        } else if (propertyNameHash == Dims._hash) {
            if (auto dims = ConvertOrCast<uint32_t>(data, type)) {
                desc._width = desc._height = dims.value();
                return true;
            }
        } else if (propertyNameHash == SlopeScaledBias._hash) {
            if (auto slopeScaledBias = ConvertOrCast<float>(data, type)) {
                 desc._doubleSidedBias._slopeScaledBias = desc._singleSidedBias._slopeScaledBias = slopeScaledBias.value();
                 return true;
            }
        } else if (propertyNameHash == DepthBias._hash) {
            if (auto depthBias = ConvertOrCast<int>(data, type)) {
                desc._doubleSidedBias._depthBias = desc._singleSidedBias._depthBias = depthBias.value();
                return true;
            }
        } else if (propertyNameHash == DepthBiasClamp._hash) {
            if (auto depthBiasClamp = ConvertOrCast<float>(data, type)) {
                desc._doubleSidedBias._depthBiasClamp = desc._singleSidedBias._depthBiasClamp = depthBiasClamp.value();
                return true;
            }
        }

        return true;
    }

    bool SetProperty(
        RenderCore::LightingEngine::AmbientLightOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
    {
        return false;
    }

    static const ParameterBox::ParameterName MaxCascadeCount = "MaxCascadeCount";
    static const ParameterBox::ParameterName MaxDistanceFromCamera = "MaxDistanceFromCamera";
    static const ParameterBox::ParameterName CascadeSizeFactor = "CascadeSizeFactor";
    static const ParameterBox::ParameterName FocusDistance = "FocusDistance";
    static const ParameterBox::ParameterName ResolutionScale = "ResolutionScale";
    static const ParameterBox::ParameterName Flags = "Flags";
    static const ParameterBox::ParameterName TextureSize = "TextureSize";
    static const ParameterBox::ParameterName BlurAngleDegrees = "BlurAngleDegrees";
    static const ParameterBox::ParameterName MinBlurSearch = "MinBlurSearch";
    static const ParameterBox::ParameterName MaxBlurSearch = "MaxBlurSearch";
    static const ParameterBox::ParameterName HighPrecisionDepths = "HighPrecisionDepths";
    static const ParameterBox::ParameterName CasterDistanceExtraBias = "CasterDistanceExtraBias";
    static const ParameterBox::ParameterName WorldSpaceResolveBias = "WorldSpaceResolveBias";
    static const ParameterBox::ParameterName BaseBias = "BaseBias";
    static const ParameterBox::ParameterName EnableContactHardening = "EnableContactHardening";

    bool SetProperty(
        RenderCore::LightingEngine::SunSourceFrustumSettings& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        static const unsigned s_staticMaxSubProjections = 6;

        if (propertyNameHash == MaxCascadeCount._hash) {
            if (auto value = ConvertOrCast<uint32_t>(data, type)) {
                desc._maxFrustumCount = Clamp(value.value(), 1u, s_staticMaxSubProjections);
                return true;
            }
        } else if (propertyNameHash == MaxDistanceFromCamera._hash) {
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._maxDistanceFromCamera = value.value();
                return true;
            }
        } else if (propertyNameHash == CascadeSizeFactor._hash) {
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._frustumSizeFactor = value.value();
                return true;
            }
        } else if (propertyNameHash == FocusDistance._hash) {
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._focusDistance = value.value();
                return true;
            }
        } else if (propertyNameHash == ResolutionScale._hash) {
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._resolutionScale = value.value();
                return true;
            }
        } else if (propertyNameHash == Flags._hash) {
            if (auto value = ConvertOrCast<uint32_t>(data, type)) {
                desc._flags = value.value();
                return true;
            }
        } else if (propertyNameHash == TextureSize._hash) {
            if (auto value = ConvertOrCast<uint32_t>(data, type)) {
                desc._textureSize = 1<<(IntegerLog2(value.value()-1)+1);  // ceil to a power of two
                return true;
            }
        } else if (propertyNameHash == BlurAngleDegrees._hash) {
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._tanBlurAngle = XlTan(Deg2Rad(value.value()));
                return true;
            }
        } else if (propertyNameHash == MinBlurSearch._hash) {
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._minBlurSearch = value.value();
                return true;
            }
        } else if (propertyNameHash == MaxBlurSearch._hash) {
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._maxBlurSearch = value.value();
                return true;
            }
        } else if (propertyNameHash == HighPrecisionDepths._hash) {
            if (auto value = ConvertOrCast<uint32_t>(data, type)) {
                using Obj = RenderCore::LightingEngine::SunSourceFrustumSettings;
                if (value.value()) desc._flags |= Obj::Flags::HighPrecisionDepths; 
                else desc._flags &= ~Obj::Flags::HighPrecisionDepths; 
                return true;
            }
        } else if (propertyNameHash == CasterDistanceExtraBias._hash) {
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._casterDistanceExtraBias = value.value();
                return true;
            }
        } else if (propertyNameHash == WorldSpaceResolveBias._hash) {
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._worldSpaceResolveBias = value.value();
                return true;
            }
        } else if (propertyNameHash == SlopeScaledBias._hash) {
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._slopeScaledBias = value.value();
                return true;
            }
        } else if (propertyNameHash == BaseBias._hash) {
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._baseBias = value.value();
                return true;
            }
        } else if (propertyNameHash == EnableContactHardening._hash) {
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._enableContactHardening = value.value();
                return true;
            }
        } else if (propertyNameHash == FilterModel._hash) {
            using namespace RenderCore::LightingEngine;
            using Obj = RenderCore::LightingEngine::SunSourceFrustumSettings;
            SetViaEnumFn<ShadowFilterModel, AsShadowFilterModel>(desc, &Obj::_filterModel, data, type);
            return true;
        } else if (propertyNameHash == CullMode._hash) {
            using Obj = RenderCore::LightingEngine::SunSourceFrustumSettings;
            SetViaEnumFn<RenderCore::CullMode, RenderCore::AsCullMode>(desc, &Obj::_cullMode, data, type);
            return true;
        }

        return false;
    }

}

#if 0

namespace SceneEngine
{
    ToneMapSettings::ToneMapSettings() {}
}

template<> const ClassAccessors& Legacy_GetAccessors<SceneEngine::ToneMapSettings>()
{
    static ClassAccessors dummy(0);
    return dummy;
}

#endif

#if 0

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
        AddStringToEnum<RenderCore::CullMode, RenderCore::AsString, RenderCore::AsCullMode>(props, "CullMode", &Obj::_cullMode);
		props.Add("Dims", [](const Obj& obj) { return obj._width; }, [](Obj& obj, uint32_t value) { obj._width = obj._height = value; });
		props.Add(
            "SlopeScaledBias", 
            [](const Obj& obj) { return obj._singleSidedBias._slopeScaledBias; },
            [](Obj& obj, float slopeScaledBias) { obj._doubleSidedBias._slopeScaledBias = obj._singleSidedBias._slopeScaledBias = slopeScaledBias; });
        props.Add(
            "DepthBias", 
            [](const Obj& obj) { return obj._singleSidedBias._depthBias; },
            [](Obj& obj, int depthBias) { obj._doubleSidedBias._depthBias = obj._singleSidedBias._depthBias = depthBias; });
        props.Add(
            "DepthBiasClamp", 
            [](const Obj& obj) { return obj._singleSidedBias._depthBiasClamp; },
            [](Obj& obj, float depthBiasClamp) { obj._doubleSidedBias._depthBiasClamp = obj._singleSidedBias._depthBiasClamp = depthBiasClamp; });
		init = true;
	}
	return props;
}

#endif
