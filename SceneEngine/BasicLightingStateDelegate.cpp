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
#include "../RenderCore/LightingEngine/ShadowProbes.h"
#include "../RenderCore/Techniques/TechniqueDelegates.h"        // for Techniques::UtilityDelegateType
#include "../Formatters/IDynamicFormatter.h"
#include "../Tools/ToolsRig/ToolsRigServices.h"
#include "../Assets/Assets.h"
#include "../Assets/Continuation.h"
#include "../Assets/ConfigFileContainer.h"
#include "../Math/Transformations.h"
#include "../Math/MathSerialization.h"
#include "../Utility/StringUtils.h"
#include "../Formatters/TextFormatter.h"
#include "../Formatters/FormatterUtils.h"

using namespace Utility::Literals;
 
namespace SceneEngine
{
    using namespace RenderCore;

    static float PowerForHalfRadius(float halfRadius, float powerFraction)
	{
		const float attenuationScalar = 1.f;
		return (attenuationScalar*(halfRadius*halfRadius)+1.f) * (1.0f / (1.f-powerFraction));
	}

    class SwirlingPointLights
    {
    public:
        std::vector<LightingEngine::ILightScene::LightSourceId> _lightSources;
        void UpdateLights(LightingEngine::ILightScene& lightScene)
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

                auto* positional = lightScene.TryGetLightSourceInterface<LightingEngine::IPositionalLightSource>(lightId);
                if (positional) {
                    Float4x4 temp = AsFloat4x4(RotationY(2.f * gPI * c/float(tileLightCount) + _time));
                    Combine_IntoLHS(temp, RotationX(IntegerHash32(c) / 10000.0f));
                    Combine_IntoLHS(temp, RotationY(2.f * gPI * c/float(tileLightCount)));
                    positional->SetLocalToWorld(AsFloat4x4(ScaleTranslation { Float3(0.1f, 0.1f, 1.0f), TransformPoint(temp, Float3(0,0,std::sin(IntegerHash32(-(signed)c)+_time)*swirlingRadius)) }));
                }

                auto* emittance = lightScene.TryGetLightSourceInterface<LightingEngine::IUniformEmittance>(lightId);
                if (emittance) {
                    auto power = PowerForHalfRadius(0.5f*cutoffRadius, 0.05f);
                    auto brightness = power * Float3{.65f + .35f * XlSin(Y), .65f + .35f * XlCos(Y), .65f + .35f * XlCos(X)};
                    emittance->SetBrightness(brightness);
                }

                auto* finite = lightScene.TryGetLightSourceInterface<LightingEngine::IFiniteLightSource>(lightId);
                if (finite) {
                    finite->SetCutoffBrightness(0.05f);
                }
            }
            _time += 1.0f/60.f;
        }

        void BindScene(LightingEngine::ILightScene& lightScene)
        {
            if (!_desc._lightCount) return;
            assert(_operatorId != ~0u);
            assert(_lightSources.empty());
            for (unsigned c=0; c<_desc._lightCount; ++c) {
                auto lightId = lightScene.CreateLightSource(_operatorId);
                _lightSources.push_back(lightId);
            }
        }

        void UnbindScene(LightingEngine::ILightScene& lightScene)
        {
            for (auto l:_lightSources)
                lightScene.DestroyLightSource(l);
            _lightSources.clear();
        }

        void        BindCfg(MergedLightingEngineCfg& cfg)
        {
            if (_desc._lightCount) {
                LightingEngine::PositionalLightOperatorDesc opDesc;
                opDesc._shape = LightingEngine::LightSourceShape::Sphere;
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
        void        PreRender(const Techniques::ProjectionDesc& mainSceneCameraDesc, LightingEngine::ILightScene& lightScene) override;
        void        PostRender(LightingEngine::ILightScene& lightScene) override;
        void        BindScene(LightingEngine::ILightScene& lightScene, std::shared_ptr<::Assets::OperationContext>) override;
        void        UnbindScene(LightingEngine::ILightScene& lightScene) override;
        auto        BeginPrepareStep(LightingEngine::ILightScene& lightScene, IThreadContext& threadContext) -> std::shared_ptr<LightingEngine::IProbeRenderingInstance> override;

        void        BindCfg(MergedLightingEngineCfg& cfg) override;

		BasicLightingStateDelegate(Formatters::IDynamicInputFormatter& formatter);
		~BasicLightingStateDelegate();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<BasicLightingStateDelegate>>&& promise,
			StringSection<> envSettingFileName);

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

    protected:
        LightingEngineOperatorSet _operatorResolveContext;
        ObjectTable<LightingEngine::SunSourceFrustumSettings> _sunSourceFrustumSettingsInCfgFile;
        std::vector<std::pair<uint64_t, uint64_t>> _shadowToAssociatedLight;

        struct PendingLightSource
        {
            uint64_t _operatorHash = 0;
            std::string _name;
            ParameterBox _parameters;
        };
        std::vector<PendingLightSource> _lightSourcesInCfgFile;
        ParameterBox _bloomPropertiesInCfgFile;
        ParameterBox _exposurePropertiesInCfgFile;

        std::vector<unsigned> _lightSourcesInBoundScene;

        std::vector<std::pair<uint64_t, LightingEngine::ILightScene::LightOperatorId>> _lightOperatorHashToId;
        uint64_t _ambientOperator = ~0ull;

        ::Assets::DependencyValidation _depVal;

        SwirlingPointLights _swirlingLights;

        void DeserializeLightSources(Formatters::IDynamicInputFormatter& formatter);
    };

    void BasicLightingStateDelegate::PreRender(
        const Techniques::ProjectionDesc& mainSceneCameraDesc,
        LightingEngine::ILightScene& lightScene)
    {
        _swirlingLights.UpdateLights(lightScene);
    }

    void        BasicLightingStateDelegate::PostRender(LightingEngine::ILightScene& lightScene)
    {
    }

    void        BasicLightingStateDelegate::BindScene(
        LightingEngine::ILightScene& lightScene,
        std::shared_ptr<::Assets::OperationContext> operationContext)
    {
        assert(_lightSourcesInBoundScene.empty());      // if you hit this, it means we're already bound to a scene (either the same one or another)

        std::vector<std::pair<uint64_t, LightingEngine::ILightScene::LightSourceId>> lightNameToId;

        for (const auto&light:_lightSourcesInCfgFile) {
            if (!light._operatorHash) continue;

            auto lightOperator = LowerBound(_lightOperatorHashToId, light._operatorHash);
            if (lightOperator != _lightOperatorHashToId.end() && lightOperator->first == light._operatorHash) {

                auto newLight = lightScene.CreateLightSource(lightOperator->second);
                _lightSourcesInBoundScene.push_back(newLight);
                InitializeLight(lightScene, newLight, light._parameters, Zero<Float3>());
                lightNameToId.emplace_back(Hash64(light._name), newLight);

                if (light._operatorHash == _ambientOperator) {
                    auto* distanceIBL = lightScene.TryGetLightSourceInterface<LightingEngine::ISkyTextureProcessor>(newLight);
                    if (distanceIBL)
                        distanceIBL->SetEquirectangularSource(operationContext, light._parameters.GetParameterAsString("EquirectangularSource"_h).value());
                }

                continue;
            }
        }

        for (const auto& sunSource:_sunSourceFrustumSettingsInCfgFile._objects) {
            auto lightAssociation = std::find_if(
                _shadowToAssociatedLight.begin(), _shadowToAssociatedLight.end(), 
                [sunSource](const auto& c) { return c.first == sunSource.first; });
            if (lightAssociation == _shadowToAssociatedLight.end()) continue;        // not tied to a specific light

            auto lightId = std::find_if(
                lightNameToId.begin(), lightNameToId.end(), 
                [lightAssociation](const auto& c) { return c.first == lightAssociation->second; });
            if (lightId == lightNameToId.end()) continue;        // couldn't find the associated light
            
            LightingEngine::SetupSunSourceShadows(
                lightScene, lightId->second, sunSource.second);
        }

        if (_bloomPropertiesInCfgFile.GetCount() != 0)
            if (auto* bloom = query_interface_cast<LightingEngine::IBloom*>(&lightScene))
                for (auto p:_bloomPropertiesInCfgFile)
                    SetProperty(*bloom, p.HashName(), p.RawValue(), p.Type());

        if (_exposurePropertiesInCfgFile.GetCount() != 0)
            if (auto* exposure = query_interface_cast<LightingEngine::IExposure*>(&lightScene))
                for (auto p:_exposurePropertiesInCfgFile)
                    SetProperty(*exposure, p.HashName(), p.RawValue(), p.Type());

        _swirlingLights.BindScene(lightScene);
    }

    void        BasicLightingStateDelegate::UnbindScene(LightingEngine::ILightScene& lightScene)
    {
        _swirlingLights.UnbindScene(lightScene);
        for (auto lightSource:_lightSourcesInBoundScene)
            lightScene.DestroyLightSource(lightSource);
        _lightSourcesInBoundScene.clear();
    }

    std::shared_ptr<LightingEngine::IProbeRenderingInstance> BasicLightingStateDelegate::BeginPrepareStep(LightingEngine::ILightScene& lightScene, IThreadContext& threadContext)
    {
        return nullptr;
    }

    void BasicLightingStateDelegate::BindCfg(MergedLightingEngineCfg& cfg)
    {
        _lightOperatorHashToId.clear();
        _ambientOperator = ~0ull;

        _lightOperatorHashToId.reserve(_operatorResolveContext._lightSourceOperators._objects.size());
        for (const auto& c:_operatorResolveContext._lightSourceOperators._objects) {

            auto shadow = std::find_if(b2e(_operatorResolveContext._shadowOperators._objects), [n=c.first](const auto& q) { return q.first == n; });
            if (shadow != _operatorResolveContext._shadowOperators._objects.end()) {
                _lightOperatorHashToId.emplace_back(c.first, cfg.Register(c.second, shadow->second));
                continue;
            }

            auto associatedShadow = std::find_if(b2e(_shadowToAssociatedLight), [n=c.first](const auto& q) { return q.second == n; });
            if (associatedShadow != _shadowToAssociatedLight.end()) {
                auto shadow2 = std::find_if(b2e(_sunSourceFrustumSettingsInCfgFile._objects), [n=associatedShadow->first](const auto& q) { return q.first == n; });
                if (shadow2 != _sunSourceFrustumSettingsInCfgFile._objects.end()) {
                    auto shadowOperator = LightingEngine::CalculateShadowOperatorDesc(shadow2->second);
                    _lightOperatorHashToId.emplace_back(c.first, cfg.Register(c.second, shadowOperator));
                    continue;
                }
            }

            _lightOperatorHashToId.emplace_back(c.first, cfg.Register(c.second));
        }

        if (!_operatorResolveContext._ambientOperators._objects.empty()) {
            if (_operatorResolveContext._ambientOperators._objects.size() != 1)
                Throw(std::runtime_error("Only one ambient operator allowed in BasicLightingStateDelegate configuration file"));

            _ambientOperator = _operatorResolveContext._ambientOperators._objects[0].first;
            _lightOperatorHashToId.emplace_back(_ambientOperator, cfg.Register(_operatorResolveContext._ambientOperators._objects[0].second));
        }

        for (const auto& decalOperator:_operatorResolveContext._decalOperators._objects) {
            _lightOperatorHashToId.emplace_back(decalOperator.first, cfg.Register(decalOperator.second));
        }

        if (!_operatorResolveContext._toneMapAcesOperators._objects.empty()) {
            if (_operatorResolveContext._toneMapAcesOperators._objects.size() != 1)
                Throw(std::runtime_error("Only one tonemap operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._toneMapAcesOperators._objects[0].second);
        }

        if (!_operatorResolveContext._taaOperator._objects.empty()) {
            if (_operatorResolveContext._taaOperator._objects.size() != 1)
                Throw(std::runtime_error("Only one TAA operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._taaOperator._objects[0].second);
        }

        if (!_operatorResolveContext._sharpenOperator._objects.empty()) {
            if (_operatorResolveContext._sharpenOperator._objects.size() != 1)
                Throw(std::runtime_error("Only one sharpen operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._sharpenOperator._objects[0].second);
        }

        if (!_operatorResolveContext._filmGrainOperator._objects.empty()) {
            if (_operatorResolveContext._filmGrainOperator._objects.size() != 1)
                Throw(std::runtime_error("Only one film grain operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._filmGrainOperator._objects[0].second);
        }

        if (!_operatorResolveContext._forwardLightingOperators._objects.empty()) {
            if (_operatorResolveContext._forwardLightingOperators._objects.size() != 1 || !_operatorResolveContext._deferredLightingOperators._objects.empty() || !_operatorResolveContext._utilityLightingOperator._objects.empty())
                Throw(std::runtime_error("Only one lighting technique operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._forwardLightingOperators._objects[0].second);
        }

        if (!_operatorResolveContext._deferredLightingOperators._objects.empty()) {
            if (_operatorResolveContext._deferredLightingOperators._objects.size() != 1 || !_operatorResolveContext._forwardLightingOperators._objects.empty() || !_operatorResolveContext._utilityLightingOperator._objects.empty())
                Throw(std::runtime_error("Only one lighting technique operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._deferredLightingOperators._objects[0].second);
        }

        if (!_operatorResolveContext._utilityLightingOperator._objects.empty()) {
            if (_operatorResolveContext._utilityLightingOperator._objects.size() != 1 || !_operatorResolveContext._forwardLightingOperators._objects.empty() || !_operatorResolveContext._deferredLightingOperators._objects.empty())
                Throw(std::runtime_error("Only one lighting technique operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._utilityLightingOperator._objects[0].second);
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

        if (!_operatorResolveContext._ssr._objects.empty()) {
            if (_operatorResolveContext._ssr._objects.size() != 1)
                Throw(std::runtime_error("Only one screen space reflections operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._ssr._objects[0].second);
        }

        if (!_operatorResolveContext._ssao._objects.empty()) {
            if (_operatorResolveContext._ssao._objects.size() != 1)
                Throw(std::runtime_error("Only one screen space ambient occlusion operator allowed in BasicLightingStateDelegate configuration file"));

            cfg.SetOperator(_operatorResolveContext._ssao._objects[0].second);
        }

        _swirlingLights.BindCfg(cfg);

        std::sort(_lightOperatorHashToId.begin(), _lightOperatorHashToId.end(), CompareFirst<uint64_t, unsigned>());
    }

    void BasicLightingStateDelegate::DeserializeLightSources(Formatters::IDynamicInputFormatter& formatter)
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

                LightingEngine::SunSourceFrustumSettings sunSourceShadows;
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

            } else if (XlEqString(keyname, "Exposure")) {

                RequireBeginElement(formatter);
                StringSection<> keyname;
                while (formatter.TryKeyedItem(keyname)) {
                    ImpliedTyping::TypeDesc typeDesc;
                    auto data = RequireRawValue(formatter, typeDesc);
                    _exposurePropertiesInCfgFile.SetParameter(keyname, data, typeDesc);
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
        auto fmttrFuture = ToolsRig::BeginMountedFormatter(envSettingFileName);
        ::Assets::WhenAll(std::move(fmttrFuture)).ThenConstructToPromise(
            std::move(promise),
            [](auto fmttr) { return std::make_shared<BasicLightingStateDelegate>(*fmttr); });
	}

	BasicLightingStateDelegate::BasicLightingStateDelegate(
		Formatters::IDynamicInputFormatter& formatter)
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

	class LightingStateDelegateSplitter : public ILightingStateDelegate
	{
	public:
		void PreRender(
			const Techniques::ProjectionDesc& mainSceneCameraDesc, 
			LightingEngine::ILightScene& lightScene) override
		{
			_children[0]->PreRender(mainSceneCameraDesc, lightScene);
			_children[1]->PreRender(mainSceneCameraDesc, lightScene);
		}
		
		void PostRender(LightingEngine::ILightScene& lightScene) override
		{
			_children[0]->PostRender(lightScene);
			_children[1]->PostRender(lightScene);
		}

		void BindScene(LightingEngine::ILightScene& lightScene, std::shared_ptr<::Assets::OperationContext> opContext) override
		{
			_children[0]->BindScene(lightScene, opContext);
			_children[1]->BindScene(lightScene, opContext);
		}

		void UnbindScene(LightingEngine::ILightScene& lightScene) override
		{
			_children[0]->UnbindScene(lightScene);
			_children[1]->UnbindScene(lightScene);
		}

		void BindCfg(MergedLightingEngineCfg& cfg) override
		{
			_children[0]->BindCfg(cfg);
			_children[1]->BindCfg(cfg);
		}

		class SplitProbeRenderingInstance : public LightingEngine::IProbeRenderingInstance
		{
		public:
			LightingEngine::SequencePlayback::Step GetNextStep() override
			{
				auto res = _children[_iterationProgress]->GetNextStep();
				if (res._type == LightingEngine::StepType::None && _iterationProgress == 0) {
					++_iterationProgress;
					res = _children[_iterationProgress]->GetNextStep();
				}
				return res;
			}

			BufferUploads::CommandListID GetRequiredBufferUploadsCommandList() override
			{
				return std::max(_children[0]->GetRequiredBufferUploadsCommandList(), _children[1]->GetRequiredBufferUploadsCommandList());
			}

			SplitProbeRenderingInstance(std::shared_ptr<LightingEngine::IProbeRenderingInstance> zero, std::shared_ptr<LightingEngine::IProbeRenderingInstance> one)
			{
				_children[0] = std::move(zero);
				_children[1] = std::move(one);
			}

			std::shared_ptr<LightingEngine::IProbeRenderingInstance> _children[2];
			unsigned _iterationProgress = 0;
		};

		std::shared_ptr<LightingEngine::IProbeRenderingInstance> BeginPrepareStep(
			LightingEngine::ILightScene& lightScene, IThreadContext& threadContext) override
		{
			auto zero = _children[0]->BeginPrepareStep(lightScene, threadContext);
			auto one = _children[1]->BeginPrepareStep(lightScene, threadContext);
			if (!one) return zero;
			if (!zero) return one;
			return std::make_shared<SplitProbeRenderingInstance>(std::move(zero), std::move(one));
		}

		LightingStateDelegateSplitter(std::shared_ptr<ILightingStateDelegate> zero, std::shared_ptr<ILightingStateDelegate> one)
		{
			_children[0] = std::move(zero);
			_children[1] = std::move(one);
		}

		std::shared_ptr<ILightingStateDelegate> _children[2];
	};

	std::future<std::shared_ptr<ILightingStateDelegate>> SplitLightingStateDelegate(
		std::shared_future<std::shared_ptr<ILightingStateDelegate>> zero,
		std::shared_future<std::shared_ptr<ILightingStateDelegate>> one)
	{
		assert(zero.valid() && one.valid());
		std::promise<std::shared_ptr<ILightingStateDelegate>> p;
		auto result = p.get_future();
		::Assets::WhenAll(std::move(zero), std::move(one)).ThenConstructToPromise(
			std::move(p),
			[](auto&& zero, auto&& one) -> std::shared_ptr<ILightingStateDelegate> {
				return std::make_shared<LightingStateDelegateSplitter>(std::move(zero), std::move(one));
			});
		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class UtilityLightingStateDelegate : public ILightingStateDelegate
    {
    public:
        void        PreRender(
            const Techniques::ProjectionDesc& mainSceneCameraDesc, 
            LightingEngine::ILightScene& lightScene)
        {}
        void        PostRender(LightingEngine::ILightScene& lightScene)
        {}

        void        BindScene(LightingEngine::ILightScene& lightScene, std::shared_ptr<::Assets::OperationContext>)
        {}
        void        UnbindScene(LightingEngine::ILightScene& lightScene)
        {}

        void        BindCfg(MergedLightingEngineCfg& cfg)
        {
            cfg.SetOperator(_techDesc);
        }

        std::shared_ptr<LightingEngine::IProbeRenderingInstance> BeginPrepareStep(
            LightingEngine::ILightScene& lightScene, IThreadContext& threadContext)
        {
            return nullptr;
        }

        UtilityLightingStateDelegate(Techniques::UtilityDelegateType utilType)
        {
            _techDesc._type = utilType;
        }

    private:
        LightingEngine::UtilityLightingTechniqueDesc _techDesc;
    };

    std::shared_ptr<ILightingStateDelegate> CreateUtilityLightingStateDelegate(Techniques::UtilityDelegateType utilType)
    {
        return std::make_shared<UtilityLightingStateDelegate>(utilType);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    LightingEngine::SunSourceFrustumSettings DefaultSunSourceFrustumSettings()
    {
        LightingEngine::SunSourceFrustumSettings result;
        result._maxFrustumCount = 3;
        result._maxDistanceFromCamera = 2000.f;
        result._focusDistance = 5.0f;
        result._flags = 0;
        result._textureSize = 2048;
        return result;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    MergedLightingEngineCfg::LightOperatorId MergedLightingEngineCfg::Register(const LightingEngine::PositionalLightOperatorDesc& pos, const LightingEngine::ShadowOperatorDesc& shadow)
    {
        auto hash = pos.GetHash(shadow.GetHash());
        auto i = std::find(b2e(_lightOperatorHashes), hash);
        if (i != _lightOperatorHashes.end())
            return (unsigned)std::distance(_lightOperatorHashes.begin(), i);

        auto result = unsigned(_lightOperatorHashes.size());
        _lightOperatorHashes.push_back(hash);

        if (_reservedLightOperatorCount < dimof(_reservedLightOperators)) {
            _reservedLightOperators[_reservedLightOperatorCount]._desc = {result, pos};
            AddToOperatorList(_reservedLightOperators[_reservedLightOperatorCount]);
            ++_reservedLightOperatorCount;
        } else {
            SetOperator<LightingEngine::LightOperatorAssignment<LightingEngine::PositionalLightOperatorDesc>>({result, pos});
        }

        if (_reservedShadowOperatorCount < dimof(_reservedShadowOperators)) {
            _reservedShadowOperators[_reservedShadowOperatorCount]._desc = {result, shadow};
            AddToOperatorList(_reservedShadowOperators[_reservedShadowOperatorCount]);
            ++_reservedShadowOperatorCount;
        } else {
            SetOperator<LightingEngine::LightOperatorAssignment<LightingEngine::ShadowOperatorDesc>>({result, shadow});
        }

        return result;
    }

    unsigned MergedLightingEngineCfg::Register(const LightingEngine::PositionalLightOperatorDesc& pos)
    {
        auto hash = pos.GetHash();
        auto i = std::find(b2e(_lightOperatorHashes), hash);
        if (i != _lightOperatorHashes.end())
            return (unsigned)std::distance(_lightOperatorHashes.begin(), i);

        auto result = unsigned(_lightOperatorHashes.size());
        _lightOperatorHashes.push_back(hash);

        if (_reservedLightOperatorCount < dimof(_reservedLightOperators)) {
            _reservedLightOperators[_reservedLightOperatorCount]._desc = {result, pos};
            AddToOperatorList(_reservedLightOperators[_reservedLightOperatorCount]);
            ++_reservedLightOperatorCount;
        } else {
            SetOperator<LightingEngine::LightOperatorAssignment<LightingEngine::PositionalLightOperatorDesc>>({result, pos});
        }

        return result;
    }

    auto MergedLightingEngineCfg::Register(const LightingEngine::AmbientLightOperatorDesc& ambient) -> LightOperatorId
    {
        auto result = unsigned(_lightOperatorHashes.size());
        _lightOperatorHashes.push_back(~0ull);
        SetOperator<LightingEngine::LightOperatorAssignment<LightingEngine::AmbientLightOperatorDesc>>({result, ambient});
        return result;
    }

    auto MergedLightingEngineCfg::Register(const LightingEngine::DecalLightOperatorDesc& decal) -> LightOperatorId
    {
        auto result = unsigned(_lightOperatorHashes.size());
        _lightOperatorHashes.push_back(~0ull);
        SetOperator<LightingEngine::LightOperatorAssignment<LightingEngine::DecalLightOperatorDesc>>({result, decal});
        return result;
    }

    void MergedLightingEngineCfg::AddToOperatorList(LightingEngine::ChainedOperatorDesc& op)
    {
        if (_firstChainedOperator) {
            auto* o = _firstChainedOperator;
            while (o != &op && o->_next) o = const_cast<LightingEngine::ChainedOperatorDesc*>(o->_next);
            if (o != &op) {
                assert(!o->_next);
                assert(!op._next);
                o->_next = &op;
            }
        } else {
            _firstChainedOperator = &op;
        }
    }

    void MergedLightingEngineCfg::SetOperator(const LightingEngine::ForwardLightingTechniqueDesc& operatorDesc)
    {
        _forwardLightingOperator._desc = operatorDesc;
        AddToOperatorList(_forwardLightingOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const LightingEngine::DeferredLightingTechniqueDesc& operatorDesc)
    {
        _deferredLightingOperator._desc = operatorDesc;
        AddToOperatorList(_deferredLightingOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const LightingEngine::UtilityLightingTechniqueDesc& operatorDesc)
    {
        _utilityLightingOperator._desc = operatorDesc;
        AddToOperatorList(_utilityLightingOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const LightingEngine::ToneMapAcesOperatorDesc& operatorDesc)
    {
        _toneMapAcesOperator._desc = operatorDesc;
        AddToOperatorList(_toneMapAcesOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const LightingEngine::MultiSampleOperatorDesc& operatorDesc)
    {
        _msaaOperator._desc = operatorDesc;
        AddToOperatorList(_msaaOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const LightingEngine::TAAOperatorDesc& operatorDesc)
    {
        _taaOperator._desc = operatorDesc;
        AddToOperatorList(_taaOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const LightingEngine::SharpenOperatorDesc& operatorDesc)
    {
        _sharpenOperator._desc = operatorDesc;
        AddToOperatorList(_sharpenOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const LightingEngine::FilmGrainDesc& operatorDesc)
    {
        _filmGrainOperator._desc = operatorDesc;
        AddToOperatorList(_filmGrainOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const LightingEngine::SkyOperatorDesc& operatorDesc)
    {
        _skyOperator._desc = operatorDesc;
        AddToOperatorList(_skyOperator);
    }

    void MergedLightingEngineCfg::SetOperator(const LightingEngine::SkyTextureProcessorDesc& operatorDesc)
    {
        _skyTextureProcessor._desc = operatorDesc;
        AddToOperatorList(_skyTextureProcessor);
    }

    void MergedLightingEngineCfg::SetOperator(const LightingEngine::ScreenSpaceReflectionsOperatorDesc& operatorDesc)
    {
        _ssr._desc = operatorDesc;
        AddToOperatorList(_ssr);
    }

    void MergedLightingEngineCfg::SetOperator(const LightingEngine::AmbientOcclusionOperatorDesc& operatorDesc)
    {
        _ssao._desc = operatorDesc;
        AddToOperatorList(_ssao);
    }

    MergedLightingEngineCfg::MergedLightingEngineCfg() = default;
    MergedLightingEngineCfg::~MergedLightingEngineCfg() = default;

    std::future<void> IScene::PrepareForView(PrepareForViewContext& prepareContext) const { return {}; }
    IScene::~IScene() {}
    ISceneOverlay::~ISceneOverlay() {}


    constexpr auto LocalToWorld = "LocalToWorld"_h;
    constexpr auto Position = "Position"_h;
    constexpr auto Forward = "Forward"_h;
    constexpr auto Radius = "Radius"_h;
    constexpr auto Brightness = "Brightness"_h;
    constexpr auto CutoffBrightness = "CutoffBrightness"_h;
    constexpr auto CutoffRange = "CutoffRange"_h;
    constexpr auto DiffuseWideningMin = "DiffuseWideningMin"_h;
    constexpr auto DiffuseWideningMax = "DiffuseWideningMax"_h;
    constexpr auto ConeAngle = "ConeAngle"_h;
    constexpr auto EquirectangularSource = "EquirectangularSource"_h;

    void InitializeLight(
        LightingEngine::ILightScene& lightScene, LightingEngine::ILightScene::LightSourceId sourceId,
        const ParameterBox& parameters,
        const Float3& offsetLocalToWorld)
    {
        auto* positional = lightScene.TryGetLightSourceInterface<LightingEngine::IPositionalLightSource>(sourceId);
        if (positional) {
            auto transformValue = parameters.GetParameter<Float3x4>(LocalToWorld);
            if (transformValue) {
                Combine_IntoLHS(transformValue.value(), offsetLocalToWorld);
                positional->SetLocalToWorld(AsFloat4x4(transformValue.value()));
            } else {
                auto positionValue = parameters.GetParameter<Float3>(Position);
                auto forwardValue = parameters.GetParameter<Float3>(Forward);
                auto radiusValue = parameters.GetParameter<Float3>(Radius);
                
                if (positionValue || radiusValue || forwardValue) {
                    ScaleRotationTranslationM srt{Float3{1,1,1}, Identity<Float3x3>(), Float3{0,0,0}};
                    if (positionValue)
                        srt._translation = *positionValue;
                    if (forwardValue)
                        srt._rotation = Truncate3x3(MakeObjectToWorld(Normalize(*forwardValue), Float3{0,0,1}, Float3{0,0,0}));
                    if (radiusValue)
                        srt._scale = *radiusValue;
                    srt._translation += offsetLocalToWorld;
                    positional->SetLocalToWorld(AsFloat4x4(srt));
                }
            }
        }

        auto* uniformEmittance = lightScene.TryGetLightSourceInterface<LightingEngine::IUniformEmittance>(sourceId);
        if (uniformEmittance) {
            if (auto brightness = parameters.GetParameter<Float3>(Brightness))
                uniformEmittance->SetBrightness(*brightness);

            auto wideningMin = parameters.GetParameter<float>(DiffuseWideningMin);
            auto wideningMax = parameters.GetParameter<float>(DiffuseWideningMax);
            if (wideningMin && wideningMax)
                uniformEmittance->SetDiffuseWideningFactors({wideningMin.value(), wideningMax.value()});
        }

        auto* finite = lightScene.TryGetLightSourceInterface<LightingEngine::IFiniteLightSource>(sourceId);
        if (finite) {
            if (auto cutoffBrightness = parameters.GetParameter<float>(CutoffBrightness))
                finite->SetCutoffBrightness(*cutoffBrightness);
            if (auto cutoffRange = parameters.GetParameter<float>(CutoffRange))
                finite->SetCutoffRange(*cutoffRange);
        }

        auto* cone = lightScene.TryGetLightSourceInterface<LightingEngine::IConeSource>(sourceId);
        if (cone) {
             if (auto coneAngle = parameters.GetParameter<float>(ConeAngle))
                cone->SetConeAngle(*coneAngle);
        }

        auto* distantIBL = lightScene.TryGetLightSourceInterface<LightingEngine::ISkyTextureProcessor>(sourceId);
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
        LightingEngine::ILightScene& lightScene, LightingEngine::ILightScene::LightSourceId sourceId,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
    {
        switch (propertyNameHash) {
        case LocalToWorld:
            if (auto* positional = lightScene.TryGetLightSourceInterface<LightingEngine::IPositionalLightSource>(sourceId)) {
                if (auto localToWorld = ConvertOrCast<Float3x4>(data, type)) {
                    positional->SetLocalToWorld(AsFloat4x4(localToWorld.value()));
                    return true;
                }
            }
            break;
        case Position:
            if (auto* positional = lightScene.TryGetLightSourceInterface<LightingEngine::IPositionalLightSource>(sourceId)) {
                if (auto position = ConvertOrCast<Float3>(data, type)) {
                    Float4x4 localToWorld = positional->GetLocalToWorld();
                    SetTranslation(localToWorld, position.value());
                    positional->SetLocalToWorld(localToWorld);
                    return true;
                }
            }
            break;
        case Forward:
            if (auto* positional = lightScene.TryGetLightSourceInterface<LightingEngine::IPositionalLightSource>(sourceId)) {
                if (auto forward = ConvertOrCast<Float3>(data, type)) {
                    ScaleRotationTranslationM srt{positional->GetLocalToWorld()};
                    srt._rotation = Truncate3x3(MakeObjectToWorld(Normalize(*forward), Float3{0,0,1}, Float3{0,0,0}));
                    positional->SetLocalToWorld(AsFloat4x4(srt));
                    return true;
                }
            }
            break;
        case Radius:
            if (auto* positional = lightScene.TryGetLightSourceInterface<LightingEngine::IPositionalLightSource>(sourceId)) {
                if (auto radius = ConvertOrCast<Float3>(data, type)) {
                    ScaleRotationTranslationM srt{positional->GetLocalToWorld()};
                    srt._scale = radius.value();
                    positional->SetLocalToWorld(AsFloat4x4(srt));
                    return true;
                }
            }
            break;
        case Brightness:
            if (auto* uniformEmittance = lightScene.TryGetLightSourceInterface<LightingEngine::IUniformEmittance>(sourceId)) {
                if (auto brightness = ConvertOrCast<Float3>(data, type)) {
                    uniformEmittance->SetBrightness(brightness.value());
                    return true;
                }
            }
            break;
        case DiffuseWideningMin:
            if (auto* uniformEmittance = lightScene.TryGetLightSourceInterface<LightingEngine::IUniformEmittance>(sourceId)) {
                if (auto wideningMin = ConvertOrCast<float>(data, type)) {
                    uniformEmittance->SetDiffuseWideningFactors({wideningMin.value(), uniformEmittance->GetDiffuseWideningFactors()[1]});
                    return true;
                }
            }
            break;
        case DiffuseWideningMax:
            if (auto* uniformEmittance = lightScene.TryGetLightSourceInterface<LightingEngine::IUniformEmittance>(sourceId)) {
                if (auto wideningMax = ConvertOrCast<float>(data, type)) {
                    uniformEmittance->SetDiffuseWideningFactors({uniformEmittance->GetDiffuseWideningFactors()[0], wideningMax.value()});
                    return true;
                }
            }
            break;
        case CutoffBrightness:
            if (auto* finite = lightScene.TryGetLightSourceInterface<LightingEngine::IFiniteLightSource>(sourceId)) {
                if (auto cutoffBrightness = ConvertOrCast<float>(data, type)) {
                    finite->SetCutoffBrightness(cutoffBrightness.value());
                    return true;
                }
            }
            break;
        case CutoffRange:
            if (auto* finite = lightScene.TryGetLightSourceInterface<LightingEngine::IFiniteLightSource>(sourceId)) {
                if (auto cutoffRange = ConvertOrCast<float>(data, type)) {
                    finite->SetCutoffRange(cutoffRange.value());
                    return true;
                }
            }
            break;
         case ConeAngle:
            if (auto* cone = lightScene.TryGetLightSourceInterface<LightingEngine::IConeSource>(sourceId)) {
                if (auto angle = ConvertOrCast<float>(data, type)) {
                    cone->SetConeAngle(angle.value());
                    return true;
                }
            }
            break;
        case EquirectangularSource:
            if (auto* distantIBL = lightScene.TryGetLightSourceInterface<LightingEngine::ISkyTextureProcessor>(sourceId)) {
                auto src = ImpliedTyping::AsString(data, type);
                distantIBL->SetEquirectangularSource(nullptr, src);     // todo -- Assets::OperationContext
            }
            break;
        }

        return false;
    }

    bool SetProperty(
        LightingEngine::IBloom& bloom,
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

    bool SetProperty(
        LightingEngine::IExposure& exposure,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        switch (propertyNameHash) {
        case "Exposure"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                exposure.SetExposure(*value);
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
        LightingEngine::PositionalLightOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
    {
        using namespace LightingEngine;
        switch (propertyNameHash) {
        case "Shape"_h:
            SetViaEnumFn<LightSourceShape, AsLightSourceShape>(desc, &PositionalLightOperatorDesc::_shape, data, type);
            return true;
        case "DiffuseModel"_h:
            SetViaEnumFn<LightingEngine::DiffuseModel, AsDiffuseModel>(desc, &PositionalLightOperatorDesc::_diffuseModel, data, type);
            return true;
        case "DominantLight"_h:
            if (auto value = ConvertOrCast<unsigned>(data, type)) {
                if (value.value()) {
                    desc._flags |= LightingEngine::PositionalLightOperatorDesc::Flags::DominantLight;
                } else {
                    desc._flags &= ~LightingEngine::PositionalLightOperatorDesc::Flags::DominantLight;
                }
                return true;
            }
            break;
        }
        return false;
    }

    bool SetProperty(
        LightingEngine::ShadowOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
    {
        using namespace LightingEngine;
        switch (propertyNameHash) {
        case "Format"_h:
            SetViaEnumFn<Format, AsFormat>(desc, &ShadowOperatorDesc::_format, data, type);
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
            SetViaEnumFn<CullMode, AsCullMode>(desc, &ShadowOperatorDesc::_cullMode, data, type);
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
        case "NormalProjectionCount"_h:
            if (auto normalProjectionCount = ConvertOrCast<unsigned>(data, type)) {
                desc._normalProjCount = normalProjectionCount.value();
                return true;
            }
            break;
        }

        return true;
    }

    bool SetProperty(
        LightingEngine::AmbientLightOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
    {
        return false;
    }

    bool SetProperty(
        LightingEngine::DecalLightOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
    {
        return false;
    }

    bool SetProperty(
        LightingEngine::SunSourceFrustumSettings& desc,
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
                using Obj = LightingEngine::SunSourceFrustumSettings;
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
            using namespace LightingEngine;
            using Obj = LightingEngine::SunSourceFrustumSettings;
            SetViaEnumFn<ShadowFilterModel, AsShadowFilterModel>(desc, &Obj::_filterModel, data, type);
            return true;
        case "CullMode"_h:
            using Obj = LightingEngine::SunSourceFrustumSettings;
            SetViaEnumFn<CullMode, AsCullMode>(desc, &Obj::_cullMode, data, type);
            return true;
        }

        return false;
    }

    bool SetProperty(
        LightingEngine::ForwardLightingTechniqueDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        // no properties yet
        return false;
    }

    bool SetProperty(
        LightingEngine::DeferredLightingTechniqueDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        // no properties yet
        return false;
    }

    bool SetProperty(
        LightingEngine::UtilityLightingTechniqueDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        switch (propertyNameHash) {
        case "Type"_h:
            SetViaEnumFn<Techniques::UtilityDelegateType, Techniques::AsUtilityDelegateType>(desc, &LightingEngine::UtilityLightingTechniqueDesc::_type, data, type);
            return true;
        }
        return false;
    }

    bool SetProperty(
        LightingEngine::ToneMapAcesOperatorDesc& desc,
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
        LightingEngine::MultiSampleOperatorDesc& desc,
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
        LightingEngine::TAAOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        switch (propertyNameHash) {
        case "TimeConstant"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._timeConstant = *value;
                return true;
            }
            break;

        case "FindOptimalMotionVector"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._findOptimalMotionVector = *value;
                return true;
            }
            break;

        case "CatmullRomSampling"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._catmullRomSampling = *value;
                return true;
            }
            break;

        case "SharpenHistory"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._sharpenHistory = *value;
                return true;
            }
            break;
        }
        return false;
    }

    bool SetProperty(
        LightingEngine::SharpenOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        switch (propertyNameHash) {
        case "Amount"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._amount = *value;
                return true;
            }
            break;
        }
        return false;
    }

    bool SetProperty(
        LightingEngine::FilmGrainDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        switch (propertyNameHash) {
        case "Strength"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._strength = *value;
                return true;
            }
            break;
        }
        return false;
    }

    bool SetProperty(
        LightingEngine::SkyOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        // no useful properties yet
        return false;
    }

    std::optional<LightingEngine::SkyTextureProcessorDesc::CoordinateSystem> AsCoordinateSystem(StringSection<> name)
	{
		if (XlEqString(name, "YUp")) return LightingEngine::SkyTextureProcessorDesc::CoordinateSystem::YUp;
		if (XlEqString(name, "ZUp")) return LightingEngine::SkyTextureProcessorDesc::CoordinateSystem::ZUp;
		return {};
	}

    bool SetProperty(
        LightingEngine::SkyTextureProcessorDesc& desc,
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
            SetViaEnumFn<Format, AsFormat>(desc, &LightingEngine::SkyTextureProcessorDesc::_cubemapFormat, data, type);
            break;

        case "SpecularCubeMapFaceDimension"_h:
            if (auto value = ConvertOrCast<unsigned>(data, type)) {
                desc._specularCubemapFaceDimension = *value;
                return true;
            }
            break;

        case "SpecularCubeMapFormat"_h:
            SetViaEnumFn<Format, AsFormat>(desc, &LightingEngine::SkyTextureProcessorDesc::_specularCubemapFormat, data, type);
            break;

        case "ProgressiveCompilation"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._progressiveCompilation = *value;
                return true;
            }
            break;

        case "UseProgressiveSpecularAsBackground"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._useProgressiveSpecularAsBackground = *value;
                return true;
            }
            break;

        case "BlurBackground"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._blurBackground = *value;
                return true;
            }
            break;

        case "CoordinateSystem"_h:
            SetViaEnumFn<LightingEngine::SkyTextureProcessorDesc::CoordinateSystem, AsCoordinateSystem>(desc, &LightingEngine::SkyTextureProcessorDesc::_coordinateSystem, data, type);
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

    bool SetProperty(
        LightingEngine::ScreenSpaceReflectionsOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        switch (propertyNameHash) {
        case "EnableFinalBlur"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._enableFinalBlur = *value;
                return true;
            }
            break;
        case "SplitConfidence"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._splitConfidence = *value;
                return true;
            }
            break;
        }
        return false;
    }

    bool SetProperty(
        LightingEngine::AmbientOcclusionOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type)
    {
        switch (propertyNameHash) {
        case "SearchSteps"_h:
            if (auto value = ConvertOrCast<unsigned>(data, type)) {
                desc._searchSteps = *value;
                return true;
            }
            break;
        case "MaxWorldSpaceDistance"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._maxWorldSpaceDistance = *value;
                return true;
            }
            break;
        case "SampleBothDirections"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._sampleBothDirections = *value;
                return true;
            }
            break;
        case "LateTemporalFiltering"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._lateTemporalFiltering = *value;
                return true;
            }
            break;
        case "EnableFiltering"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._enableFiltering = *value;
                return true;
            }
            break;
        case "EnableHierarchicalStepping"_h:
            if (auto value = ConvertOrCast<bool>(data, type)) {
                desc._enableHierarchicalStepping = *value;
                return true;
            }
            break;
        case "ThicknessHeuristicFactor"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._thicknessHeuristicFactor = *value;
                return true;
            }
            break;
        case "FilteringStrength"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._filteringStrength = *value;
                return true;
            }
            break;
        case "VariationTolerance"_h:
            if (auto value = ConvertOrCast<float>(data, type)) {
                desc._variationTolerance = *value;
                return true;
            }
            break;
        }
        return false;
    }

}
