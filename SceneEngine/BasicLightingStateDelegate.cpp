// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BasicLightingStateDelegate.h"
#include "LightSceneConfiguration.h"
#include "../RenderCore/LightingEngine/SunSourceConfiguration.h"
#include "../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../RenderCore/LightingEngine/SkyOperator.h"
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

using namespace Utility::Literals;
 
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
            const float cutoffRadius = _desc._cutoffRadius;
            const float swirlingRadius = _desc._swirlingRadius;
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
                    Combine_IntoLHS(temp, RotationX(IntegerHash32(c) / 10000.0f));
                    Combine_IntoLHS(temp, RotationY(2.f * gPI * c/float(tileLightCount)));
                    positional->SetLocalToWorld(AsFloat4x4(ScaleTranslation { Float3(0.1f, 0.1f, 1.0f), TransformPoint(temp, Float3(0,0,std::sin(IntegerHash32(-(signed)c)+_time)*swirlingRadius)) }));
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
            if (!_desc._lightCount) return;
            assert(_operatorId != ~0u);
            assert(_lightSources.empty());
            for (unsigned c=0; c<_desc._lightCount; ++c) {
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
            if (_desc._lightCount) {
                RenderCore::LightingEngine::LightSourceOperatorDesc opDesc;
                opDesc._shape = RenderCore::LightingEngine::LightSourceShape::Sphere;
                _operatorId = cfg.Register(opDesc);
            }
        }

        SwirlingPointLights(const SwirlingLightsOperatorDesc& desc = {}) : _desc(desc)
        {
            _time = 0.f;
        }
        float _time;
        unsigned _operatorId = ~0u;
        SwirlingLightsOperatorDesc _desc;
    };

    static bool SetProperty(SwirlingLightsOperatorDesc&, uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type);

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
        ParameterBox _bloomPropertiesInCfgFile;

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

    void        BasicLightingStateDelegate::BindScene(
        RenderCore::LightingEngine::ILightScene& lightScene,
        std::shared_ptr<::Assets::OperationContext> operationContext)
    {
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

                auto* distanceIBL = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::ISkyTextureProcessor>(newLight);
                if (distanceIBL)
                    distanceIBL->SetEquirectangularSource(operationContext, light._parameters.GetParameterAsString("EquirectangularSource"_h).value());
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

        if (_bloomPropertiesInCfgFile.GetCount() != 0)
            if (auto* bloom = query_interface_cast<RenderCore::LightingEngine::IBloom*>(&lightScene))
                for (auto p:_bloomPropertiesInCfgFile)
                    SetProperty(*bloom, p.HashName(), p.RawValue(), p.Type());

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

        if (!_operatorResolveContext._ambientOperators._objects.empty()) {
            if (_operatorResolveContext._ambientOperators._objects.size() != 1)
                Throw(std::runtime_error("Only one ambient operator allowed in BasicLightingStateDelegate configuration file"));

            _ambientOperator = _operatorResolveContext._ambientOperators._objects[0].first;
        }

        if (!_operatorResolveContext._toneMapAcesOperators._objects.empty()) {
            if (_operatorResolveContext._toneMapAcesOperators._objects.size() != 1)
                Throw(std::runtime_error("Only one tonemap operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._toneMapAcesOperators._objects[0].second);
        }

        if (!_operatorResolveContext._forwardLightingOperators._objects.empty()) {
            if (_operatorResolveContext._forwardLightingOperators._objects.size() != 1 || !_operatorResolveContext._deferredLightingOperators._objects.empty())
                Throw(std::runtime_error("Only one lighting technique operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._forwardLightingOperators._objects[0].second);
        }

        if (!_operatorResolveContext._deferredLightingOperators._objects.empty()) {
            if (_operatorResolveContext._deferredLightingOperators._objects.size() != 1 || !_operatorResolveContext._forwardLightingOperators._objects.empty())
                Throw(std::runtime_error("Only one lighting technique operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._deferredLightingOperators._objects[0].second);
        }

        if (!_operatorResolveContext._multiSampleOperators._objects.empty()) {
            if (_operatorResolveContext._multiSampleOperators._objects.size() != 1)
                Throw(std::runtime_error("Only one multisample operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._multiSampleOperators._objects[0].second);
        }

        if (!_operatorResolveContext._skyOperators._objects.empty()) {
            if (_operatorResolveContext._skyOperators._objects.size() != 1)
                Throw(std::runtime_error("Only one sky operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._skyOperators._objects[0].second);
        }
        
        if (!_operatorResolveContext._skyTextureProcessors._objects.empty()) {
            if (_operatorResolveContext._skyTextureProcessors._objects.size() != 1)
                Throw(std::runtime_error("Only one sky texture processor allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._skyTextureProcessors._objects[0].second);
        }

        _swirlingLights.BindCfg(cfg);

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

            } else if (XlEqString(keyname, "Bloom")) {

                RequireBeginElement(formatter);
                StringSection<> keyname;
                while (formatter.TryKeyedItem(keyname)) {
                    ImpliedTyping::TypeDesc typeDesc;
                    auto data = RequireRawValue(formatter, typeDesc);
                    _bloomPropertiesInCfgFile.SetParameter(keyname, data, typeDesc);
                }
                RequireEndElement(formatter);

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
            } else if (XlEqString(keyname, "SwirlingLights")) {
                RequireBeginElement(formatter);
                SwirlingLightsOperatorDesc opDesc;
                uint64_t keyname;
                while (formatter.TryKeyedItem(keyname)) {
                    ImpliedTyping::TypeDesc typeDesc;
                    auto value = RequireRawValue(formatter, typeDesc);
                    SetProperty(opDesc, keyname, value, typeDesc);
                }
                RequireEndElement(formatter);
                _swirlingLights = opDesc;
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

    template<typename T> void MergedLightingEngineCfg::AddToOperatorList(T& op)
    {
        if (_firstChainedOperator) {
            auto* o = _firstChainedOperator;
            while (o != &op && o->_next) o = const_cast<RenderCore::LightingEngine::ChainedOperatorDesc*>(o->_next);
            if (o != &op) {
                assert(!o->_next);
                assert(!op._next);
                o->_next = &op;
            }
        } else {
            _firstChainedOperator = &op;
        }
    }

    void MergedLightingEngineCfg::SetOperator(const RenderCore::LightingEngine::ForwardLightingTechniqueDesc& operatorDesc)
    {
        _forwardLightingOperator._desc = operatorDesc;
        AddToOperatorList(_forwardLightingOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const RenderCore::LightingEngine::DeferredLightingTechniqueDesc& operatorDesc)
    {
        _deferredLightingOperator._desc = operatorDesc;
        AddToOperatorList(_deferredLightingOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const RenderCore::LightingEngine::ToneMapAcesOperatorDesc& operatorDesc)
    {
        _toneMapAcesOperator._desc = operatorDesc;
        AddToOperatorList(_toneMapAcesOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const RenderCore::LightingEngine::MultiSampleOperatorDesc& operatorDesc)
    {
        _msaaOperator._desc = operatorDesc;
        AddToOperatorList(_msaaOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const RenderCore::LightingEngine::SkyOperatorDesc& operatorDesc)
    {
        _skyOperator._desc = operatorDesc;
        AddToOperatorList(_skyOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const RenderCore::LightingEngine::SkyTextureProcessorDesc& operatorDesc)
    {
        _skyTextureProcessor._desc = operatorDesc;
        AddToOperatorList(_skyTextureProcessor);
    }

    MergedLightingEngineCfg::MergedLightingEngineCfg() = default;
    MergedLightingEngineCfg::~MergedLightingEngineCfg() = default;

    std::future<void> IScene::PrepareForView(PrepareForViewContext& prepareContext) const { return {}; }
    IScene::~IScene() {}
    ISceneOverlay::~ISceneOverlay() {}


    constexpr auto LocalToWorld = "LocalToWorld"_h;
    constexpr auto Position = "Position"_h;
    constexpr auto Radius = "Radius"_h;
    constexpr auto Brightness = "Brightness"_h;
    constexpr auto CutoffBrightness = "CutoffBrightness"_h;
    constexpr auto CutoffRange = "CutoffRange"_h;
    constexpr auto DiffuseWideningMin = "DiffuseWideningMin"_h;
    constexpr auto DiffuseWideningMax = "DiffuseWideningMax"_h;
    constexpr auto EquirectangularSource = "EquirectangularSource"_h;

    void InitializeLight(
        RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId sourceId,
        const ParameterBox& parameters,
        const Float3& offsetLocalToWorld)
    {
        auto* positional = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(sourceId);
        if (positional) {
            auto transformValue = parameters.GetParameter<Float3x4>(LocalToWorld);
            if (transformValue) {
                Combine_IntoLHS(transformValue.value(), offsetLocalToWorld);
                positional->SetLocalToWorld(AsFloat4x4(transformValue.value()));
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

        auto* distantIBL = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::ISkyTextureProcessor>(sourceId);
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
        switch (propertyNameHash) {
        case LocalToWorld:
            if (auto* positional = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(sourceId)) {
                if (auto localToWorld = ConvertOrCast<Float3x4>(data, type)) {
                    positional->SetLocalToWorld(AsFloat4x4(localToWorld.value()));
                    return true;
                }
            }
            break;
        case Position:
            if (auto* positional = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(sourceId)) {
                if (auto position = ConvertOrCast<Float3>(data, type)) {
                    Float4x4 localToWorld = positional->GetLocalToWorld();
                    SetTranslation(localToWorld, position.value());
                    positional->SetLocalToWorld(localToWorld);
                    return true;
                }
            }
            break;
        case Radius:
            if (auto* positional = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IPositionalLightSource>(sourceId)) {
                if (auto radius = ConvertOrCast<Float3>(data, type)) {
                    ScaleRotationTranslationM srt{positional->GetLocalToWorld()};
                    srt._scale = radius.value();
                    positional->SetLocalToWorld(AsFloat4x4(srt));
                    return true;
                }
            }
            break;
        case Brightness:
            if (auto* uniformEmittance = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(sourceId)) {
                if (auto brightness = ConvertOrCast<Float3>(data, type)) {
                    uniformEmittance->SetBrightness(brightness.value());
                    return true;
                }
            }
            break;
        case DiffuseWideningMin:
            if (auto* uniformEmittance = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(sourceId)) {
                if (auto wideningMin = ConvertOrCast<float>(data, type)) {
                    uniformEmittance->SetDiffuseWideningFactors({wideningMin.value(), uniformEmittance->GetDiffuseWideningFactors()[1]});
                    return true;
                }
            }
            break;
        case DiffuseWideningMax:
            if (auto* uniformEmittance = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IUniformEmittance>(sourceId)) {
                if (auto wideningMax = ConvertOrCast<float>(data, type)) {
                    uniformEmittance->SetDiffuseWideningFactors({uniformEmittance->GetDiffuseWideningFactors()[0], wideningMax.value()});
                    return true;
                }
            }
            break;
        case CutoffBrightness:
            if (auto* finite = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IFiniteLightSource>(sourceId)) {
                if (auto cutoffBrightness = ConvertOrCast<float>(data, type)) {
                    finite->SetCutoffBrightness(cutoffBrightness.value());
                    return true;
                }
            }
            break;
        case CutoffRange:
            if (auto* finite = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::IFiniteLightSource>(sourceId)) {
                if (auto cutoffRange = ConvertOrCast<float>(data, type)) {
                    finite->SetCutoffRange(cutoffRange.value());
                    return true;
                }
            }
            break;
        case EquirectangularSource:
            if (auto* distantIBL = lightScene.TryGetLightSourceInterface<RenderCore::LightingEngine::ISkyTextureProcessor>(sourceId)) {
                auto src = ImpliedTyping::AsString(data, type);
                distantIBL->SetEquirectangularSource(nullptr, src);     // todo -- Assets::OperationContext
            }
            break;
        }

        return false;
    }

    bool SetProperty(
        RenderCore::LightingEngine::IBloom& bloom,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        switch (propertyNameHash) {
        case "BroadRadius"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                bloom.SetBroadRadius(*value);
                return true;
            }
            break;

        case "PreciseRadius"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                bloom.SetPreciseRadius(*value);
                return true;
            }
            break;

        case "Threshold"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                bloom.SetThreshold(*value);
                return true;
            }
            break;

        case "Desaturation"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                bloom.SetDesaturationFactor(*value);
                return true;
            }
            break;

        case "BroadBrightness"_h:
            if (auto value = ConvertOrCast<Float3>(data, type)) {
                bloom.SetBroadBrightness(*value);
                return true;
            }
            break;

        case "PreciseBrightness"_h:
            if (auto value = ConvertOrCast<Float3>(data, type)) {
                bloom.SetPreciseBrightness(*value);
                return true;
            }
            break;
        }
        return false;
    }

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
        switch (propertyNameHash) {
        case "Shape"_h:
            SetViaEnumFn<LightSourceShape, AsLightSourceShape>(desc, &LightSourceOperatorDesc::_shape, data, type);
            return true;
        case "DiffuseModel"_h:
            SetViaEnumFn<RenderCore::LightingEngine::DiffuseModel, AsDiffuseModel>(desc, &LightSourceOperatorDesc::_diffuseModel, data, type);
            return true;
        case "DominantLight"_h:
            if (auto value = ConvertOrCast<unsigned>(data, type)) {
                if (value.value()) {
                    desc._flags |= RenderCore::LightingEngine::LightSourceOperatorDesc::Flags::DominantLight;
                } else {
                    desc._flags &= ~RenderCore::LightingEngine::LightSourceOperatorDesc::Flags::DominantLight;
                }
                return true;
            }
            break;
        }
        return false;
    }

    bool SetProperty(
        RenderCore::LightingEngine::ShadowOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
    {
        using namespace RenderCore::LightingEngine;
        switch (propertyNameHash) {
        case "Format"_h:
            SetViaEnumFn<RenderCore::Format, RenderCore::AsFormat>(desc, &ShadowOperatorDesc::_format, data, type);
            return true;
        case "ResolveType"_h:
            SetViaEnumFn<ShadowResolveType, AsShadowResolveType>(desc, &ShadowOperatorDesc::_resolveType, data, type);
            return true;
        case "ProjectionMode"_h:
            SetViaEnumFn<ShadowProjectionMode, AsShadowProjectionMode>(desc, &ShadowOperatorDesc::_projectionMode, data, type);
            return true;
        case "FilterModel"_h:
            SetViaEnumFn<ShadowFilterModel, AsShadowFilterModel>(desc, &ShadowOperatorDesc::_filterModel, data, type);
            return true;
        case "CullMode"_h:
            SetViaEnumFn<RenderCore::CullMode, RenderCore::AsCullMode>(desc, &ShadowOperatorDesc::_cullMode, data, type);
            return true;
        case "Dims"_h:
            if (auto dims = ConvertOrCast<uint32_t>(data, type)) {
                desc._width = desc._height = dims.value();
                return true;
            }
            break;
        case "SlopeScaledBias"_h:
            if (auto slopeScaledBias = ConvertOrCast<float>(data, type)) {
                 desc._doubleSidedBias._slopeScaledBias = desc._singleSidedBias._slopeScaledBias = slopeScaledBias.value();
                 return true;
            }
            break;
        case "DepthBias"_h:
            if (auto depthBias = ConvertOrCast<int>(data, type)) {
                desc._doubleSidedBias._depthBias = desc._singleSidedBias._depthBias = depthBias.value();
                return true;
            }
            break;
        case "DepthBiasClamp"_h:
            if (auto depthBiasClamp = ConvertOrCast<float>(data, type)) {
                desc._doubleSidedBias._depthBiasClamp = desc._singleSidedBias._depthBiasClamp = depthBiasClamp.value();
                return true;
            }
            break;
        }

        return true;
    }

    bool SetProperty(
        RenderCore::LightingEngine::AmbientLightOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
    {
        return false;
    }

    bool SetProperty(
        RenderCore::LightingEngine::SunSourceFrustumSettings& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        static const unsigned s_staticMaxSubProjections = 6;

        switch (propertyNameHash) {
        case "MaxCascadeCount"_h:
            if (auto value = ConvertOrCast<uint32_t>(data, type)) {
                desc._maxFrustumCount = Clamp(value.value(), 1u, s_staticMaxSubProjections);
                return true;
            }
            break;
        case "MaxDistanceFromCamera"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._maxDistanceFromCamera = value.value();
                return true;
            }
            break;
        case "CascadeSizeFactor"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._frustumSizeFactor = value.value();
                return true;
            }
            break;
        case "FocusDistance"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._focusDistance = value.value();
                return true;
            }
            break;
        case "ResolutionScale"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._resolutionScale = value.value();
                return true;
            }
            break;
        case "Flags"_h:
            if (auto value = ConvertOrCast<uint32_t>(data, type)) {
                desc._flags = value.value();
                return true;
            }
            break;
        case "TextureSize"_h:
            if (auto value = ConvertOrCast<uint32_t>(data, type)) {
                desc._textureSize = 1<<(IntegerLog2(value.value()-1)+1);  // ceil to a power of two
                return true;
            }
            break;
        case "BlurAngleDegrees"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._tanBlurAngle = XlTan(Deg2Rad(value.value()));
                return true;
            }
            break;
        case "MinBlurSearch"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._minBlurSearch = value.value();
                return true;
            }
            break;
        case "MaxBlurSearch"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._maxBlurSearch = value.value();
                return true;
            }
            break;
        case "HighPrecisionDepths"_h:
            if (auto value = ConvertOrCast<uint32_t>(data, type)) {
                using Obj = RenderCore::LightingEngine::SunSourceFrustumSettings;
                if (value.value()) desc._flags |= Obj::Flags::HighPrecisionDepths; 
                else desc._flags &= ~Obj::Flags::HighPrecisionDepths; 
                return true;
            }
            break;
        case "CasterDistanceExtraBias"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._casterDistanceExtraBias = value.value();
                return true;
            }
            break;
        case "WorldSpaceResolveBias"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._worldSpaceResolveBias = value.value();
                return true;
            }
            break;
        case "SlopeScaledBias"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._slopeScaledBias = value.value();
                return true;
            }
            break;
        case "BaseBias"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._baseBias = value.value();
                return true;
            }
            break;
        case "EnableContactHardening"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._enableContactHardening = value.value();
                return true;
            }
            break;
        case "FilterModel"_h:
            using namespace RenderCore::LightingEngine;
            using Obj = RenderCore::LightingEngine::SunSourceFrustumSettings;
            SetViaEnumFn<ShadowFilterModel, AsShadowFilterModel>(desc, &Obj::_filterModel, data, type);
            return true;
        case "CullMode"_h:
            using Obj = RenderCore::LightingEngine::SunSourceFrustumSettings;
            SetViaEnumFn<RenderCore::CullMode, RenderCore::AsCullMode>(desc, &Obj::_cullMode, data, type);
            return true;
        }

        return false;
    }

    bool SetProperty(
        RenderCore::LightingEngine::ForwardLightingTechniqueDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        // no properties yet
        return false;
    }

    bool SetProperty(
        RenderCore::LightingEngine::DeferredLightingTechniqueDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        // no properties yet
        return false;
    }

    bool SetProperty(
        RenderCore::LightingEngine::ToneMapAcesOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        switch (propertyNameHash) {
        case "BroadBloomMaxRadius"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._broadBloomMaxRadius = *value;
                return true;
            }
            break;

        case "EnableBroadBloom"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._broadBloomMaxRadius = *value ? 128.f : 0.f;
                return true;
            }
            break;

        case "EnablePreciseBloom"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._enablePreciseBloom = *value;
                return true;
            }
            break;
        }
        return false;
    }

    bool SetProperty(
        RenderCore::LightingEngine::MultiSampleOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        switch (propertyNameHash) {
        case "SampleCount"_h:
            if (auto value = ConvertOrCast<unsigned>(data, type)) {
                desc._samples._sampleCount = *value;
                return true;
            }
            break;

        case "SamplingQuality"_h:
            if (auto value = ConvertOrCast<unsigned>(data, type)) {
                desc._samples._samplingQuality = *value;
                return true;
            }
            break;
        }
        return false;
    }

    bool SetProperty(
        RenderCore::LightingEngine::SkyOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        // no useful properties yet
        return false;
    }

    bool SetProperty(
        RenderCore::LightingEngine::SkyTextureProcessorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        switch (propertyNameHash) {
        case "CubeMapFaceDimension"_h:
            if (auto value = ConvertOrCast<unsigned>(data, type)) {
                desc._cubemapFaceDimension = *value;
                return true;
            }
            break;

        case "CubeMapFormat"_h:
            SetViaEnumFn<RenderCore::Format, RenderCore::AsFormat>(desc, &RenderCore::LightingEngine::SkyTextureProcessorDesc::_cubemapFormat, data, type);
            break;

        case "SpecularCubeMapFaceDimension"_h:
            if (auto value = ConvertOrCast<unsigned>(data, type)) {
                desc._specularCubemapFaceDimension = *value;
                return true;
            }
            break;

        case "SpecularCubeMapFormat"_h:
            SetViaEnumFn<RenderCore::Format, RenderCore::AsFormat>(desc, &RenderCore::LightingEngine::SkyTextureProcessorDesc::_specularCubemapFormat, data, type);
            break;
        }
        return false;
    }

    bool SetProperty(SwirlingLightsOperatorDesc& desc, uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        switch (propertyNameHash) {
        case "LightCount"_h:
            if (auto value = ConvertOrCast<unsigned>(data, type)) {
                desc._lightCount = *value;
                return true;
            }
            break;
        case "SwirlingRadius"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._swirlingRadius = *value;
                return true;
            }
            break;
        case "CutoffRadius"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._cutoffRadius = *value;
                return true;
            }
            break;
        }
        return false;
    }

}
