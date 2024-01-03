// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LevelEditorScene.h"
#include "MarshalString.h"
#include "GUILayerUtil.h"
#include "IOverlaySystem.h"
#include "EditorInterfaceUtils.h"
#include "ManipulatorsLayer.h"
#if defined(GUILAYER_SCENEENGINE)
#include "TerrainLayer.h"
#endif
#include "UITypesBinding.h" // for VisCameraSettings
#include "Exceptions.h"
#include "EngineDevice.h"
#include "NativeEngineDevice.h"
#include "ExportedNativeTypes.h"
#include "../EntityInterface/PlacementEntities.h"
#if defined(GUILAYER_SCENEENGINE)
#include "../EntityInterface/TerrainEntities.h"
#include "../EntityInterface/VegetationSpawnEntities.h"
#endif
#include "../EntityInterface/RetainedEntities.h"
#include "../EntityInterface/GameObjects.h"
#include "../EntityInterface/LightingEngineEntityDocument.h"
#include "../ToolsRig/PlacementsManipulators.h"     // just needed for destructors referenced in PlacementGobInterface.h
#if defined(GUILAYER_SCENEENGINE)
#include "../ToolsRig/TerrainManipulators.h"        // for TerrainManipulatorContext
#endif
#include "../ToolsRig/ObjectPlaceholders.h"
#include "../../SceneEngine/PlacementsManager.h"
#include "../../SceneEngine/IntersectionTest.h"
#include "../../SceneEngine/RigidModelScene.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#if defined(GUILAYER_SCENEENGINE)
#include "../../SceneEngine/Terrain.h"
#include "../../SceneEngine/TerrainFormat.h"
#include "../../SceneEngine/TerrainConfig.h"
#include "../../SceneEngine/VegetationSpawn.h"
#include "../../SceneEngine/VolumetricFog.h"
#include "../../SceneEngine/ShallowSurface.h"
#include "../../SceneEngine/DynamicImposters.h"
#include "../../SceneEngine/TerrainUberSurface.h"
#include "../../SceneEngine/TerrainMaterial.h"
#include "../../SceneEngine/SurfaceHeightsProvider.h"
#include "../../FixedFunctionModel/ModelCache.h"
#include "../../FixedFunctionModel/SharedStateSet.h"
#endif
#include "../../Math/MathSerialization.h"
#include "../../Utility/Streams/StreamTypes.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../OSServices/LegacyFileStreams.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Formatters/TextOutputFormatter.h"
#include "../../Utility/Conversion.h"
#include <memory>

using namespace System;

namespace GUILayer
{
    using namespace std::placeholders;

    // Many level editors work around a structure of objects and attributes.
    // That is, the scene is composed of a hierachy of objects, and each object
    // as some type, and a set of attributes. In the case of the SonyWWS editor,
    // this structure is backed by a xml based DOM.
    // 
    // It's handy because it's simple and flexible. And we can store just about
    // anything like this.
    // 
    // When working with a level editor like this, we need a some kind of dynamic
    // "scene" object. This scene should react to basic commands from the editor:
    //
    //      * Create/destroy object (with given fixed type)
    //      * Set object attribute
    //      * Set object parent
    //
    // We also want to build in a "document" concept. Normally a document should
    // represent a single target file (eg a level file or some settings file).
    // Every object should belong to a single object, and all of the objects in
    // a single document should usually be part of one large hierarchy.
    //
    // For convenience when working with the SonyWWS editor, we want to be able to
    // pre-register common strings (like type names and object property names).
    // It might be ok just to use hash values for all of these cases. It depends on
    // whether we want to validate the names when they are first registered.

///////////////////////////////////////////////////////////////////////////////////////////////////

    EditorScene::EditorScene()
    {
#if defined(GUILAYER_SCENEENGINE)
        auto fixedFunctionModelCache = std::make_shared<FixedFunctionModel::ModelCache>();
            // note --  we need to have the terrain manager a default terrain format here... But it's too early
            //          for some settings (like the gradient flags settings!)
        auto defTerrainFormat = std::make_shared<SceneEngine::TerrainFormat>(SceneEngine::GradientFlagsSettings(true));
        _terrainManager = std::make_shared<SceneEngine::TerrainManager>(defTerrainFormat);
        _vegetationSpawnManager = std::make_shared<SceneEngine::VegetationSpawnManager>(fixedFunctionModelCache);
        _volumeFogManager = std::make_shared<SceneEngine::VolumetricFogManager>();
        _shallowSurfaceManager = std::make_shared<SceneEngine::ShallowSurfaceManager>();
        _dynamicImposters = std::make_shared<SceneEngine::DynamicImposters>(fixedFunctionModelCache->GetSharedStateSet());
        _placementsManager->GetRenderer()->SetImposters(_dynamicImposters);
#else
        // Base scene aspects
        auto& drawingApparatus = EngineDevice::GetInstance()->GetNative().GetDrawingApparatus();
        auto& primaryResources = EngineDevice::GetInstance()->GetNative().GetPrimaryResourcesApparatus();
        _flexObjects = std::make_shared<EntityInterface::RetainedEntities>();
        _placeholders = std::make_shared<ToolsRig::ObjectPlaceholders>(drawingApparatus->_drawablesPool, drawingApparatus->_pipelineAccelerators, primaryResources->_bufferUploads, _flexObjects);
        _currentTime = 0.f;

        // Placements scene aspects
		std::shared_ptr<::Assets::OperationContext> loadingContext;

		SceneEngine::IRigidModelScene::Config rigModelSceneCfg;
        rigModelSceneCfg._disableRepositionableGeometry = true;     // disable this, because this requires a constructionContext, and those have issues in the level editor -- because the preview window doesn't use them, and it's invalid to create the same descriptor set both with and without a construction context
        _rigidModelScene = SceneEngine::CreateRigidModelScene(
            drawingApparatus->_drawablesPool, drawingApparatus->_pipelineAccelerators,
            drawingApparatus->_deformAccelerators, primaryResources->_bufferUploads,
            loadingContext,
            rigModelSceneCfg);
        _placementsManager = std::make_shared<SceneEngine::PlacementsManager>(_rigidModelScene, loadingContext);
        _placementsCells = std::make_shared<SceneEngine::PlacementCellSet>();
        _placementsCellsHidden = std::make_shared<SceneEngine::PlacementCellSet>();
        _placementsEditor = _placementsManager->CreateEditor(_placementsCells);
		_placementsHidden = _placementsManager->CreateEditor(_placementsCellsHidden);
#endif
    }

	EditorScene::~EditorScene()
	{}

    template <typename Type>
        Type GetFirst(IEnumerable<Type>^ input)
        {
            if (!input) return Type();
            auto e = input->GetEnumerator();
            if (!e || !e->MoveNext()) return Type();
            return e->Current;
        }

    void EditorSceneManager::SetTypeAnnotation(
        uint typeId, String^ annotationName, 
        IEnumerable<EntityLayer::PropertyInitializer>^ initializers)
    {
        if (!_scene->_placeholders) return ;

        if (annotationName == "vis") {
            auto mappedId = _entities->HashNameForTypeId(typeId);
            std::string geoType;
            auto p = GetFirst(initializers);
            assert(p._elementType == (unsigned)ImpliedTyping::TypeCat::UInt8);
            if (p._srcBegin && p._srcBegin != p._srcEnd)
                geoType = {(const char*)p._srcBegin, (const char*)p._srcEnd};
                
            _scene->_placeholders->AddAnnotation(mappedId, geoType);
        }
    }

    const EntityInterface::RetainedEntities& EditorSceneManager::GetFlexObjects()
    {
        return *_scene->_flexObjects;
    }

#if defined(GUILAYER_SCENEENGINE)
    void EditorSceneManager::SaveTerrainLock(uint layerId, IProgress^ progress)
    {
        if (!_scene->_terrainManager) return;

        SceneEngine::GenericUberSurfaceInterface* interf = nullptr;
        if (layerId == SceneEngine::CoverageId_Heights) {
            interf = _scene->_terrainManager->GetHeightsInterface();
        } else {
            interf = _scene->_terrainManager->GetCoverageInterface(layerId);
        }
        if (!interf) return;

        auto nativeProgress = progress ? IProgress::CreateNative(progress) : nullptr;
        TRY { interf->FlushLockToDisk(nativeProgress.get()); } 
        CATCH (const std::exception& e) { Throw(Marshal(e)); }
        CATCH_END
    }

    void EditorSceneManager::AbandonTerrainLock(uint layerId)
    {
        if (!_scene->_terrainManager) return;

        SceneEngine::GenericUberSurfaceInterface* interf = nullptr;
        if (layerId == SceneEngine::CoverageId_Heights) {
            interf = _scene->_terrainManager->GetHeightsInterface();
        } else {
            interf = _scene->_terrainManager->GetCoverageInterface(layerId);
        }
        if (!interf) return;

        TRY { interf->AbandonLock(); }
        CATCH (const std::exception& e) { Throw(Marshal(e)); }
        CATCH_END
    }

    static bool HasLock(const SceneEngine::GenericUberSurfaceInterface& interf)
    {
        auto lock = interf.GetLock();
        return (lock.first[0] < lock.second[0]) && (lock.first[1] < lock.second[1]);
    }

    bool EditorSceneManager::HasTerrainLock(uint layerId)
    {
        if (!_scene->_terrainManager) return false;

        SceneEngine::GenericUberSurfaceInterface* interf = nullptr;
        if (layerId == SceneEngine::CoverageId_Heights) {
            interf = _scene->_terrainManager->GetHeightsInterface();
        } else {
            interf = _scene->_terrainManager->GetCoverageInterface(layerId);
        }
        return interf && HasLock(*interf);
    }

    IManipulatorSet^ EditorSceneManager::CreateTerrainManipulators(TerrainManipulatorContext^ context) 
    { 
        return gcnew TerrainManipulators(_scene->_terrainManager, context->GetNative());
    }
#endif

    IManipulatorSet^ EditorSceneManager::CreatePlacementManipulators(
        IPlacementManipulatorSettingsLayer^ context)
    {
        if (_scene->_placementsEditor) {
            return gcnew PlacementManipulators(context->GetNative(), _scene->_placementsEditor, _scene->_placementsManager->GetRenderer());
        } else {
            return nullptr;
        }
    }

    PlacementsEditorWrapper^ EditorSceneManager::GetPlacementsEditor()
	{
		return gcnew PlacementsEditorWrapper(_scene->_placementsEditor);
    }

    PlacementsRendererWrapper^ EditorSceneManager::GetPlacementsRenderer()
    {
        return gcnew PlacementsRendererWrapper(_scene->_placementsManager->GetRenderer());
    }

#if defined(GUILAYER_SCENEENGINE)
	IntersectionTestSceneWrapper^ EditorSceneManager::GetIntersectionScene()
	{
		return gcnew IntersectionTestSceneWrapper(
            _scene->_terrainManager,
            _scene->_placementsCells,
            _scene->_placementsEditor,
            {_scene->_placeholders->CreateIntersectionTester()} );
    }
#else
    IntersectionTestSceneWrapper^ EditorSceneManager::GetIntersectionScene()
	{
        auto scene = SceneEngine::CreateIntersectionTestScene(
            nullptr,
            _scene->_placementsEditor,
            {_scene->_placeholders->CreateIntersectionTester()});
        return gcnew IntersectionTestSceneWrapper(std::move(scene));
    }
#endif

    EntityLayer^ EditorSceneManager::GetEntityInterface()
    {
        return _entities;
    }

    ::EntityInterface::MultiEnvironmentSettingsDocument& EditorSceneManager::GetEnvSettingsDocument()
    {
        assert(_multiEnvSettingsDocument.get());
        return *_multiEnvSettingsDocument.get();
    }

    void EditorSceneManager::IncrementTime(float increment)
    {
        _scene->IncrementTime(increment);
    }

    EditorScene& EditorSceneManager::GetScene()
    {
        return *_scene.get();
    }

    static void PrepareDirectoryForFile(std::string& destinationFile)
    {
        OSServices::CreateDirectoryRecursive(MakeFileNameSplitter(destinationFile).StemAndPath());
    }

    ref class TextPendingExport : public EditorSceneManager::PendingExport
    {
    public:
        EditorSceneManager::ExportResult PerformExport(System::Uri^ destFile) override
        {
            EditorSceneManager::ExportResult result;
			result._success = false;
            TRY
            {
                auto nativeDestFile = clix::marshalString<clix::E_UTF8>(destFile->LocalPath);
                PrepareDirectoryForFile(nativeDestFile);

                auto output = OSServices::Legacy::OpenFileOutput(nativeDestFile.c_str(), "wb");
                // pin_ptr<const wchar_t> p = ::PtrToStringChars(_preview);
                // output->Write(p, _preview->Length * sizeof(wchar_t));
                    // we need to use clix::marshalString in order to convert to UTF8 characters
                auto nativeString = clix::marshalString<clix::E_UTF8>(_preview);
                output->Write(
                    AsPointer(nativeString.cbegin()), 
                    int(nativeString.size() * sizeof(decltype(nativeString)::value_type)));
                
                result._success = true;
                result._messages = "Success";
                
            } CATCH(const std::exception& e) {
                result._messages = "Error while writing to file: " + destFile + " : " + clix::marshalString<clix::E_UTF8>(e.what());
            } CATCH(...) {
                result._messages = "Unknown error occurred while writing to file " + destFile;
            } CATCH_END
            return result;
        }
    };

	enum class StreamWriterResult { Text, Binary, MetricsText, None };

	static auto __clrcall AsPendingExportType(StreamWriterResult input) -> EditorSceneManager::PendingExport::Type
	{
		switch (input) {
		case StreamWriterResult::Text: return EditorSceneManager::PendingExport::Type::Text;
		case StreamWriterResult::Binary: return EditorSceneManager::PendingExport::Type::Binary;
		case StreamWriterResult::MetricsText: return EditorSceneManager::PendingExport::Type::MetricsText;
		default:
		case StreamWriterResult::None: return EditorSceneManager::PendingExport::Type::None;
		}	
	}

    template<typename Type>
        static EditorSceneManager::PendingExport^ ExportViaStream(
            System::String^ typeName,
            Type streamWriter)
    {
        auto result = gcnew TextPendingExport();
        result->_success = false;
        
        TRY
        {
            MemoryOutputStream<utf8> stream;
            result->_previewType = AsPendingExportType(streamWriter(stream));
            result->_preview = clix::marshalString<clix::E_UTF8>(stream.GetBuffer().str());
            result->_success = true;
            result->_messages = "Success";
        } CATCH(const std::exception& e) {
            result->_messages = "Error while exporting environment settings: " + clix::marshalString<clix::E_UTF8>(e.what());
        } CATCH(...) {
            result->_messages = "Unknown error occurred while exporting environment settings";
        } CATCH_END
        return result;
    }

    static auto WriteGameObjects(
        OutputStream& stream, uint64_t docId, 
        EntityInterface::RetainedEntities* flexObjects) -> StreamWriterResult
    {
        Formatters::TextOutputFormatter formatter(stream);
#if defined(GUILAYER_SCENEENGINE)
        EntityInterface::ExportGameObjects(formatter, *flexObjects);
#endif
        return StreamWriterResult::Text;
    }

    auto EditorSceneManager::ExportGameObjects(
        EntityInterface::DocumentId docId) -> PendingExport^
    {
        return ExportViaStream(
            "game objects",
            std::bind(WriteGameObjects, _1, docId, _scene->_flexObjects.get()));
    }

    static auto WriteEnvSettings(OutputStream& stream, uint64_t docId, EntityInterface::RetainedEntities* flexObjects) 
        -> StreamWriterResult
    {
        Formatters::TextOutputFormatter formatter(stream);
#if defined(GUILAYER_SCENEENGINE)
        EntityInterface::ExportEnvSettings(formatter, *flexObjects, docId);
#endif
        return StreamWriterResult::Text;
    }

    auto EditorSceneManager::ExportEnv(EntityInterface::DocumentId docId) -> PendingExport^
    {
        return ExportViaStream(
            "environment settings",
            std::bind(WriteEnvSettings, _1, docId, _scene->_flexObjects.get()));
    }

    static auto WritePlacementsCfg(
        OutputStream& stream, 
        IEnumerable<EditorSceneManager::PlacementCellRef>^ cells) -> StreamWriterResult
    {
        Formatters::TextOutputFormatter formatter(stream);
        for each(auto c in cells) {
            auto ele = formatter.BeginKeyedElement("CellRef");
            formatter.WriteKeyedValue("Offset", ImpliedTyping::AsString(AsFloat3(c.Offset)));
            formatter.WriteKeyedValue("Mins", ImpliedTyping::AsString(AsFloat3(c.Mins)));
            formatter.WriteKeyedValue("Maxs", ImpliedTyping::AsString(AsFloat3(c.Maxs)));
            formatter.WriteKeyedValue("NativeFile", clix::marshalString<clix::E_UTF8>(c.NativeFile));
            formatter.EndElement(ele);
        }
        return StreamWriterResult::Text;
    }

    auto EditorSceneManager::ExportPlacementsCfg(IEnumerable<PlacementCellRef>^ cells) -> PendingExport^
    {
        return ExportViaStream(
            "placements config",
            std::bind(WritePlacementsCfg, _1, msclr::gcroot<IEnumerable<PlacementCellRef>^>(cells)));
    }

    ref class PlacementsPendingExport : public EditorSceneManager::PendingExport
    {
    public:
        EditorSceneManager::ExportResult PerformExport(System::Uri^ destFile) override
        {
            EditorSceneManager::ExportResult result;
			result._success = false;
            TRY
            {
                auto nativeDestFile = clix::marshalString<clix::E_UTF8>(destFile->LocalPath);
                PrepareDirectoryForFile(nativeDestFile);

                _placements->WriteCell(_doc, nativeDestFile.c_str());

                    // write metrics file as well
                {
                    auto metrics = _placements->GetMetricsString(_doc);
                    auto output = OSServices::Legacy::OpenFileOutput((nativeDestFile + ".metrics").c_str(), "wb");
                    output->Write(MakeStringSection(Conversion::Convert<std::basic_string<utf8>>(metrics)));
                }

                result._success = true;
                result._messages = "Success";
                
            } CATCH(const std::exception& e) {
                result._messages = "Error while writing to file: " + destFile + " : " + clix::marshalString<clix::E_UTF8>(e.what());
            } CATCH(...) {
                result._messages = "Unknown error occurred while writing to file " + destFile;
            } CATCH_END
            return result;
        }

        PlacementsPendingExport(
            EntityInterface::DocumentId doc,
            std::shared_ptr<SceneEngine::PlacementsEditor> placements) : _doc(doc), _placements(placements) {}
    private:
        EntityInterface::DocumentId _doc;
        clix::shared_ptr<SceneEngine::PlacementsEditor> _placements;
    };

    auto EditorSceneManager::ExportPlacements(EntityInterface::DocumentId placementsDoc) -> PendingExport^
    {
			// (note -- hidden placements will not be exported)
        auto result = gcnew PlacementsPendingExport(placementsDoc, _scene->_placementsEditor);
        result->_success = false;

        TRY
        {
            result->_preview = clix::marshalString<clix::E_UTF8>(
                _scene->_placementsEditor->GetMetricsString(placementsDoc));
            result->_success = true;
            result->_messages = "Success";
            result->_previewType = PendingExport::Type::MetricsText;
        } CATCH(const std::exception& e) {
            result->_messages = "Error while exporting placements: " + clix::marshalString<clix::E_UTF8>(e.what());
        } CATCH(...) {
            result->_messages = "Unknown error occurred while exporting placements";
        } CATCH_END

        return result;
    }

#if defined(GUILAYER_SCENEENGINE)
    static auto WriteTerrainCfg(OutputStream& stream, SceneEngine::TerrainConfig& cfg) 
        -> StreamWriterResult
    {
        TextOutputFormatter formatter(stream);
        cfg.Write(formatter);
        return StreamWriterResult::Text;
    }

    auto EditorSceneManager::ExportTerrain(TerrainConfig^ cfg) -> PendingExport^
    {
        return ExportViaStream("terrain", std::bind(WriteTerrainCfg, _1, cfg->GetNative()));
    }

    static auto WriteTerrainCachedData(OutputStream& stream, SceneEngine::TerrainManager* terrainMan) 
        -> StreamWriterResult
    {
        SceneEngine::WriteTerrainCachedData(stream, terrainMan->GetConfig(), *terrainMan->GetFormat());
        return StreamWriterResult::Text;
    }

    auto EditorSceneManager::ExportTerrainCachedData() -> PendingExport^
    {
        return ExportViaStream(
            "terrain cached data",
            std::bind(WriteTerrainCachedData, _1, _scene->_terrainManager.get()));
    }

    static auto WriteTerrainMaterialData(OutputStream& stream, SceneEngine::TerrainManager* terrainMan) 
        -> StreamWriterResult
    {
        SceneEngine::WriteTerrainMaterialData(stream, terrainMan->GetMaterialConfig());
        return StreamWriterResult::Text;
    }

    auto EditorSceneManager::ExportTerrainMaterialData() -> PendingExport^
    {
        return ExportViaStream(
            "terrain material data",
            std::bind(WriteTerrainMaterialData, _1, _scene->_terrainManager.get()));
    }

    static auto WriteVegetationSpawnConfig(OutputStream& stream, SceneEngine::VegetationSpawnManager* man) 
        -> StreamWriterResult
    {
        Utility::TextOutputFormatter formatter(stream);
        man->GetConfig().Write(formatter);
        return StreamWriterResult::Text;
    }

    auto EditorSceneManager::ExportVegetationSpawn(EntityInterface::DocumentId docId) -> PendingExport^
    {
        return ExportViaStream(
            "vegetation spawn",
            std::bind(WriteVegetationSpawnConfig, _1, _scene->_vegetationSpawnManager.get()));
    }

    void EditorSceneManager::UnloadTerrain()
    {
        // Check for active "locks" before we unload. If we find any, throw an exception back
        bool hasLock = false;
        auto* heightsIntrf = _scene->_terrainManager->GetHeightsInterface();
        hasLock |= (heightsIntrf && HasLock(*heightsIntrf));
        auto cfg = _scene->_terrainManager->GetConfig();
        for (unsigned l=0; l<cfg.GetCoverageLayerCount(); ++l) {
            auto * intrf = _scene->_terrainManager->GetCoverageInterface(cfg.GetCoverageLayer(l)._id);
            hasLock |= (intrf && HasLock(*intrf));
        }
        if (hasLock)
            throw gcnew System::Exception("Cannot unload the terrain because there are active terrain locks. Save or abandon the terrain lock before performing this operation.");

		_scene->_terrainManager->Reset();
    }

    void EditorSceneManager::ReloadTerrain(TerrainConfig^ cfg)
    {
        auto nativeCfg = cfg->GetNative();
        _scene->_terrainManager->Load(nativeCfg, Int2(0,0), nativeCfg._cellCount, true);
        _terrainInterface->OnTerrainReload();
        EntityInterface::ReloadTerrainFlexObjects(*_scene->_flexObjects, *_scene->_terrainManager);
    }

    EditorSceneManager::EditorSceneManager()
    {
        _scene = std::make_shared<EditorScene>();

        using namespace EntityInterface;
        auto placementsEditor = std::make_shared<PlacementEntities>(_scene->_placementsManager, _scene->_placementsEditor, _scene->_placementsHidden);
        auto terrainEditor = std::make_shared<TerrainEntities>(_scene->_terrainManager);
        auto flexGobInterface = std::make_shared<RetainedEntitiesAdapter>(_scene->_flexObjects);

        auto swtch = std::make_shared<Switch>();
        swtch->RegisterInterface(placementsEditor);
        swtch->RegisterInterface(terrainEditor);
        swtch->RegisterInterface(flexGobInterface);
        _entities = gcnew EntityLayer(std::move(swtch));

        _flexGobInterface = flexGobInterface;
        _terrainInterface = terrainEditor;
        RegisterTerrainFlexObjects(*_scene->_flexObjects, _scene->_terrainManager);
        RegisterVegetationSpawnFlexObjects(*_scene->_flexObjects, _scene->_vegetationSpawnManager);

        RegisterDynamicImpostersFlexObjects(*_scene->_flexObjects, _scene->_dynamicImposters);

        auto envEntitiesManager = std::make_shared<::EntityInterface::EnvEntitiesManager>(_scene->_flexObjects);
        envEntitiesManager->RegisterVolumetricFogFlexObjects(_scene->_volumeFogManager);
        envEntitiesManager->RegisterEnvironmentFlexObjects();
        envEntitiesManager->RegisterShallowSurfaceFlexObjects(_scene->_shallowSurfaceManager);
        _envEntitiesManager = envEntitiesManager;

        _scene->_prepareSteps.push_back(
            std::bind(&::EntityInterface::EnvEntitiesManager::FlushUpdates, _envEntitiesManager.get()));
    }
#else
    EditorSceneManager::EditorSceneManager()
    {
        _scene = std::make_shared<EditorScene>();

        using namespace EntityInterface;

        auto swtch = std::make_shared<Switch>();

        auto placementsEditor = CreatePlacementEntitiesSwitch(_scene->_placementsManager, _scene->_placementsEditor, _scene->_placementsHidden);
        swtch->RegisterDocumentType("PlacementsDocument", placementsEditor);

        // catch entities related to environment settings in a specific document
        _scene->_envSettingsDocument = std::make_shared<MultiEnvironmentSettingsDocument>();
        _envSettingsDocumentId = swtch->CreateDocument(_scene->_envSettingsDocument);
        swtch->RegisterDefaultDocument(EntityInterface::MakeStringAndHash("LightOperator"), _envSettingsDocumentId);
        swtch->RegisterDefaultDocument(EntityInterface::MakeStringAndHash("ShadowOperator"), _envSettingsDocumentId);
        swtch->RegisterDefaultDocument(EntityInterface::MakeStringAndHash("AmbientOperator"), _envSettingsDocumentId);
        swtch->RegisterDefaultDocument(EntityInterface::MakeStringAndHash("EnvSettings"), _envSettingsDocumentId);
        swtch->RegisterDefaultDocument(EntityInterface::MakeStringAndHash("DirectionalLight"), _envSettingsDocumentId);
        swtch->RegisterDefaultDocument(EntityInterface::MakeStringAndHash("AreaLight"), _envSettingsDocumentId);
        swtch->RegisterDefaultDocument(EntityInterface::MakeStringAndHash("DistantIBL"), _envSettingsDocumentId);
        swtch->RegisterDefaultDocument(EntityInterface::MakeStringAndHash("SunSourceShadowSettings"), _envSettingsDocumentId);

        // catch-all document for everything not caught above
        auto flexGobInterface = std::make_shared<RetainedEntitiesAdapter>(_scene->_flexObjects);
        _flexGlobDocumentId = swtch->CreateDocument(flexGobInterface);
        swtch->RegisterDefaultDocument(_flexGlobDocumentId);

        _entities = gcnew EntityLayer(std::move(swtch));

        _flexGobInterface = flexGobInterface;
    }
#endif

    EditorSceneManager::~EditorSceneManager()
    {
        _scene.reset();
        delete _entities;
    }

    EditorSceneManager::!EditorSceneManager() {}

    namespace Internal
    {
        IOverlaySystem^ CreateOverlaySystem(
            const std::shared_ptr<EditorScene>& scene, 
            const std::shared_ptr<ToolsRig::VisCameraSettings>& camera, 
            EditorSceneRenderSettings^ renderSettings);
    }

    IOverlaySystem^ EditorSceneManager::CreateOverlaySystem(VisCameraSettings^ camera, EditorSceneRenderSettings^ renderSettings)
    {
		// we have to get the PipelineAcceleratorPool from somewhere
        return Internal::CreateOverlaySystem(_scene.GetNativePtr(), camera->GetUnderlying(), renderSettings);
    }
}
