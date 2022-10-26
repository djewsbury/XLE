// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma warning(disable:4793) // 'Exceptions::BasicLabel::BasicLabel' : function compiled as native :

#include "EnvironmentSettings.h"
#include "RetainedEntities.h"
#include "../../RenderCore/LightingEngine/StandardLightOperators.h"
#if defined(GUILAYER_SCENEENGINE)
#include "../../SceneEngine/Ocean.h"
#include "../../SceneEngine/DeepOceanSim.h"
#include "../../SceneEngine/VolumetricFog.h"
#include "../../SceneEngine/ShallowSurface.h"
#endif
#include "../../RenderCore/LightingEngine/SunSourceConfiguration.h"
#include "../../SceneEngine/BasicLightingStateDelegate.h"
#include "../../Math/Transformations.h"
#include "../../Math/MathSerialization.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/OutputStreamFormatter.h"
#include "../../Utility/Meta/AccessorSerialize.h"
#include <memory>

#if defined(GUILAYER_SCENEENGINE)
namespace SceneEngine 
{
    extern DeepOceanSimSettings GlobalOceanSettings; 
    extern OceanLightingSettings GlobalOceanLightingSettings;
}
#endif

namespace EntityInterface
{
    #define ParamName(x) static auto x = ParameterBox::MakeParameterNameHash(#x);

///////////////////////////////////////////////////////////////////////////////////////////////////

    static void ReadTransform(SceneEngine::LightDesc& light, const ParameterBox& props)
    {
        static const auto transformHash = ParameterBox::MakeParameterNameHash("Transform");
        auto transform = props.GetParameter(transformHash, Identity<Float4x4>());

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
        static const auto* EnvSettings = "EnvSettings";
        static const auto* AmbientSettings = "AmbientSettings";
        static const auto* DirectionalLight = "DirectionalLight";
        static const auto* AreaLight = "AreaLight";
        static const auto* ToneMapSettings = "ToneMapSettings";
        static const auto* ShadowFrustumSettings = "ShadowFrustumSettings";

        static const auto* OceanLightingSettings = "OceanLightingSettings";
        static const auto* OceanSettings = "OceanSettings";
        static const auto* FogVolumeRenderer = "FogVolumeRenderer";
    }
    
    namespace Attribute
    {
        static const auto Flags = ParameterBox::MakeParameterNameHash("Flags");
        static const auto Name = ParameterBox::MakeParameterNameHash("Name");
        static const auto AttachedLight = ParameterBox::MakeParameterNameHash("Light");
    }

    SceneEngine::EnvironmentSettings
        BuildEnvironmentSettings(
            const RetainedEntities& flexGobInterface,
            const RetainedEntity& obj)
    {
        using namespace SceneEngine;

        const auto typeAmbient = flexGobInterface.GetTypeId(EntityTypeName::AmbientSettings);
        const auto typeDirectionalLight = flexGobInterface.GetTypeId(EntityTypeName::DirectionalLight);
        const auto typeAreaLight = flexGobInterface.GetTypeId(EntityTypeName::AreaLight);
        const auto typeToneMapSettings = flexGobInterface.GetTypeId(EntityTypeName::ToneMapSettings);
        const auto typeShadowFrustumSettings = flexGobInterface.GetTypeId(EntityTypeName::ShadowFrustumSettings);

        EnvironmentSettings result;
        result._environmentalLightingDesc = SceneEngine::DefaultEnvironmentalLightingDesc();
        for (const auto& cid : obj._children) {
            const auto* child = flexGobInterface.GetEntity(obj._doc, cid.second);
            if (!child) continue;

            if (child->_type == typeAmbient) {
                result._environmentalLightingDesc = MakeEnvironmentalLightingDesc(child->_properties);
            }

            if (child->_type == typeDirectionalLight || child->_type == typeAreaLight) {

                const auto& props = child->_properties;
                auto light = SceneEngine::MakeLightDesc(props);
                if (child->_type == typeDirectionalLight)
                    light._shape = RenderCore::LightingEngine::LightSourceShape::Directional;
                ReadTransform(light, props);
                
                if (props.GetParameter(Attribute::Flags, 0u) & (1<<0)) {

                        // look for frustum settings that match the "name" parameter
                    auto frustumSettings = SceneEngine::DefaultSunSourceFrustumSettings();
                    auto fsRef = props.GetParameterAsString(Attribute::Name);
                    if (fsRef.has_value()) {
                        for (const auto& cid2 : obj._children) {
                            const auto* fsSetObj = flexGobInterface.GetEntity(obj._doc, cid2.second);
                            if (!fsSetObj || fsSetObj->_type != typeShadowFrustumSettings) continue;
                            
                            auto attachedLight = fsSetObj->_properties.GetParameterAsString(Attribute::AttachedLight);
                            if (!attachedLight.has_value() || XlCompareStringI(attachedLight.value().c_str(), fsRef.value().c_str())!=0) continue;

                            frustumSettings = CreateFromParameters<RenderCore::LightingEngine::SunSourceFrustumSettings>(fsSetObj->_properties);
                            break;
                        }
                    }

                    result._sunSourceShadowProj.push_back(
                        EnvironmentSettings::SunSourceShadowProj { unsigned(result._lights.size()), frustumSettings });
                }

                result._lights.push_back(light);

            }

            if (child->_type == typeToneMapSettings) {
                result._toneMapSettings = CreateFromParameters<ToneMapSettings>(child->_properties);
            }
        }

        return std::move(result);
    }

    EnvSettingsVector BuildEnvironmentSettings(const RetainedEntities& flexGobInterface)
    {
        EnvSettingsVector result;

        const auto typeSettings = flexGobInterface.GetTypeId(EntityTypeName::EnvSettings);
        auto allSettings = flexGobInterface.FindEntitiesOfType(typeSettings);
        for (const auto& s : allSettings)
            result.push_back(
                std::make_pair(
                    s->_properties.GetParameterAsString(Attribute::Name).value(),
                    BuildEnvironmentSettings(flexGobInterface, *s)));

        return std::move(result);
    }

    template<typename CharType>
        void SerializeBody(
            OutputStreamFormatter& formatter,
            const RetainedEntity& obj,
            const RetainedEntities& entities)
    {
        obj._properties.SerializeWithCharType<CharType>(formatter);

        for (auto c=obj._children.cbegin(); c!=obj._children.cend(); ++c) {
            const auto* child = entities.GetEntity(obj._doc, c->second);
            if (child)
                SerializationOperator<CharType>(formatter, *child, entities);
        }
    }

    template<typename CharType>
        void SerializationOperator(
            OutputStreamFormatter& formatter,
            const RetainedEntity& obj,
            const RetainedEntities& entities)
    {
        auto name = entities.GetTypeName(obj._type);
        auto eleId = formatter.BeginKeyedElement(name);
        if (!obj._children.empty()) formatter.NewLine();    // properties can continue on the same line, but only if we don't have children
        SerializeBody<CharType>(formatter, obj, entities);
        formatter.EndElement(eleId);
    }

    void ExportEnvSettings(
        OutputStreamFormatter& formatter,
        const RetainedEntities& flexGobInterface,
        DocumentId docId)
    {
            // Save out the environment settings in the given document
            // in native format.
            // Our environment settings are stored in the flex object interface,
            // so we first need to convert them into native format, and then 
            // we'll write those settings to a basic text file.
            // There are two ways to handle this:
            //      1) convert from the flex gob objects into SceneEngine::EnvironmentSettings
            //          and then serialise from there
            //      2) just get the parameter boxes in the flex gob objects, and write
            //          them out
            // The second way is a lot easier. And it's much more straight-forward to
            // maintain.

        const auto typeSettings = flexGobInterface.GetTypeId(EntityTypeName::EnvSettings);
        auto allSettings = flexGobInterface.FindEntitiesOfType(typeSettings);

        bool foundAtLeastOne = false;
        for (const auto& s : allSettings)
            if (s->_doc == docId) {
                {
                    auto name = s->_properties.GetParameterAsString(Attribute::Name);
                    auto eleId = formatter.BeginKeyedElement(name.value());
                    formatter.NewLine();
                    SerializeBody<utf8>(formatter, *s, flexGobInterface);
                    formatter.EndElement(eleId);
                }

                foundAtLeastOne = true;
            }
        
        if (!foundAtLeastOne)
            Throw(::Exceptions::BasicLabel("No environment settings found"));

        formatter.Flush();
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(GUILAYER_SCENEENGINE)
    static SceneEngine::DeepOceanSimSettings BuildOceanSettings(
        const RetainedEntities& sys, const RetainedEntity& obj)
    {
        return SceneEngine::DeepOceanSimSettings(obj._properties);
    }

    static SceneEngine::OceanLightingSettings BuildOceanLightingSettings(
        const RetainedEntities& sys, const RetainedEntity& obj)
    {
        return SceneEngine::OceanLightingSettings(obj._properties);
    }
#endif

    void EnvEntitiesManager::RegisterEnvironmentFlexObjects()
    {
#if defined(GUILAYER_SCENEENGINE)
        _flexSys->RegisterCallback(
            _flexSys->GetTypeId("OceanSettings"),
            [](const RetainedEntities& flexSys, const Identifier& obj, RetainedEntities::ChangeType changeType)
            {
                if (changeType != RetainedEntities::ChangeType::Delete) {
                    auto* object = flexSys.GetEntity(obj);
                    if (object)
                        SceneEngine::GlobalOceanSettings = BuildOceanSettings(flexSys, *object);
                } else {
                    SceneEngine::GlobalOceanSettings._enable = false;
                }
            });

        _flexSys->RegisterCallback(
            _flexSys->GetTypeId("OceanLightingSettings"),
            [](const RetainedEntities& flexSys, const Identifier& obj, RetainedEntities::ChangeType changeType)
            {
                if (changeType != RetainedEntities::ChangeType::Delete) {
                    auto* object = flexSys.GetEntity(obj);
                    if (object)
                        SceneEngine::GlobalOceanLightingSettings = BuildOceanLightingSettings(flexSys, *object);
                } else {
                    SceneEngine::GlobalOceanLightingSettings = SceneEngine::OceanLightingSettings();
                }
            });
#endif
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(GUILAYER_SCENEENGINE)
    static void UpdateVolumetricFog(
        const RetainedEntities& sys, SceneEngine::VolumetricFogManager& mgr)
    {
        using namespace SceneEngine;
        VolumetricFogConfig cfg;

        const auto volumeType = sys.GetTypeId("FogVolume");
        auto volumes = sys.FindEntitiesOfType(volumeType);
        for (auto v:volumes) {
            const auto& props = v->_properties;
            cfg._volumes.push_back(VolumetricFogConfig::FogVolume(props));
        }

        const auto rendererConfigType = sys.GetTypeId("FogVolumeRenderer");
        auto renderers = sys.FindEntitiesOfType(rendererConfigType);
        if (!renderers.empty())
            cfg._renderer = VolumetricFogConfig::Renderer(renderers[0]->_properties);
        
        mgr.Load(cfg);
    }
#endif

    void EnvEntitiesManager::RegisterVolumetricFogFlexObjects(
        std::shared_ptr<SceneEngine::VolumetricFogManager> manager)
    {
#if defined(GUILAYER_SCENEENGINE)
        std::weak_ptr<SceneEngine::VolumetricFogManager> weakPtrToManager = manager;
        const char* types[] = { "FogVolume", "FogVolumeRenderer" };
        for (unsigned c=0; c<dimof(types); ++c) {
            _flexSys->RegisterCallback(
                _flexSys->GetTypeId(types[c]),
                [weakPtrToManager](const RetainedEntities& flexSys, const Identifier&, RetainedEntities::ChangeType)
                {
                    auto mgr = weakPtrToManager.lock();
                    if (!mgr) return;
                    UpdateVolumetricFog(flexSys, *mgr);
                });
        }
#endif
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    static Float4x4 GetTransform(const RetainedEntity& obj)
    {
        ParamName(Transform);
        ParamName(Translation);

        auto xform = obj._properties.GetParameter<Float4x4>(Transform);
        if (xform.has_value()) return xform.value();

        auto transl = obj._properties.GetParameter<Float3>(Translation);
        if (transl.has_value()) {
            return AsFloat4x4(transl.value());
        }
        return Identity<Float4x4>();
    }

    std::vector<Float2> ExtractVertices(const RetainedEntities& sys, const RetainedEntity& obj)
    {
            // Find the vertices within a tri mesh marker type entity,
            // and return as a triangle list of 2d positions.
        ParamName(IndexList);

        auto indexListType = obj._properties.GetParameterType(IndexList);
        if (indexListType._type == ImpliedTyping::TypeCat::Void || indexListType._arrayCount < 3)
            return std::vector<Float2>();

        auto ibData = std::make_unique<unsigned[]>(indexListType._arrayCount);
        bool success = obj._properties.GetParameter(
            IndexList, ibData.get(), 
            ImpliedTyping::TypeDesc{ImpliedTyping::TypeCat::UInt32, indexListType._arrayCount});
        if (!success) std::vector<Float2>();

        const auto& chld = obj._children;
        if (!chld.size()) std::vector<Float2>();

        auto vbData = std::make_unique<Float3[]>(chld.size());
        for (size_t c=0; c<chld.size(); ++c) {
            const auto* e = sys.GetEntity(obj._doc, chld[c].second);
            if (e) {
                vbData[c] = ExtractTranslation(GetTransform(*e));
            } else {
                vbData[c] = Zero<Float3>();
            }
        }

        std::vector<Float2> result;
        result.reserve(indexListType._arrayCount);
        for (unsigned c=0; c<indexListType._arrayCount; ++c) {
            auto i = ibData[c];
            result.push_back(Truncate(vbData[i]));
        }

        return std::move(result);
    }

#if defined(GUILAYER_SCENEENGINE)
    static void UpdateShallowSurface(
        const RetainedEntities& sys,
        SceneEngine::ShallowSurfaceManager& mgr)
    {
        using namespace SceneEngine;

        ParamName(Marker);
        ParamName(name);

        mgr.Clear();

        const auto surfaceType = sys.GetTypeId("ShallowSurface");
        const auto markerType = sys.GetTypeId("TriMeshMarker");

            // Create new surface objects for all of the "ShallowSurface" objects
        auto surfaces = sys.FindEntitiesOfType(surfaceType);
        auto markers = sys.FindEntitiesOfType(markerType);
        for (auto s:surfaces) {
            const auto& props = s->_properties;
            auto markerName = props.GetParameterAsString(Marker);
            if (!markerName.has_value()) continue;

                // Look for the marker with the matching name
            const RetainedEntity* marker = nullptr;
            for (auto m:markers) {
                auto testName = m->_properties.GetParameterAsString(name);
                if (testName.has_value() && XlEqStringI(testName.value(), markerName.value())) {
                    marker = m;
                    break;
                }
            }

            if (marker) {
                    // We have to be careful here -- because we get update
                    // callbacks when the markers change, the marker might
                    // only be partially constructed. Sometimes the vertex
                    // positions aren't properly set -- which causes us to
                    // generate bad triangles.
                auto verts = ExtractVertices(sys, *marker);
                if (verts.empty()) continue;

                auto cfg = CreateFromParameters<ShallowSurface::Config>(s->_properties);
                auto lcfg = CreateFromParameters<ShallowSurface::LightingConfig>(s->_properties);
                mgr.Add(
                    std::make_shared<ShallowSurface>(
                        AsPointer(verts.cbegin()), sizeof(Float2), 
                        verts.size(), cfg, lcfg));
            }
        }
    }
#endif

    void EnvEntitiesManager::RegisterShallowSurfaceFlexObjects(
        std::shared_ptr<SceneEngine::ShallowSurfaceManager> manager)
    {
        _shallowWaterManager = manager;

        std::weak_ptr<EnvEntitiesManager> weakPtrToThis = shared_from_this();
        const char* types[] = { "ShallowSurface", "TriMeshMarker" };
        for (unsigned c=0; c<dimof(types); ++c) {
            _flexSys->RegisterCallback(
                _flexSys->GetTypeId(types[c]),
                [weakPtrToThis](const RetainedEntities&, const Identifier&, RetainedEntities::ChangeType)
                {
                    auto mgr = weakPtrToThis.lock();
                    if (!mgr) return;
                    mgr->_pendingShallowSurfaceUpdate = true;
                });
        }
    }

    void EnvEntitiesManager::FlushUpdates()
    {
#if defined(GUILAYER_SCENEENGINE)
        if (_pendingShallowSurfaceUpdate) {
            auto mgr = _shallowWaterManager.lock();
            if (mgr) {
                UpdateShallowSurface(*_flexSys, *mgr);
            }
            _pendingShallowSurfaceUpdate = false;
        }
#endif
    }

    EnvEntitiesManager::EnvEntitiesManager(std::shared_ptr<RetainedEntities> sys)
    : _flexSys(std::move(sys))
    , _pendingShallowSurfaceUpdate(false)
    {}

    EnvEntitiesManager::~EnvEntitiesManager() {}


}
