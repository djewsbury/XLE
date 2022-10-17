// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "EditorInterfaceUtils.h"
#include "EngineDevice.h"
#include "NativeEngineDevice.h"
#include "GUILayerUtil.h"
#include "MathLayer.h"
#include "MarshalString.h"
#include "LevelEditorScene.h"
#include "ExportedNativeTypes.h"
#include "../EntityInterface/EntityInterface.h"
#include "../ToolsRig/VisualisationUtils.h"
#include "../ToolsRig/PlacementsManipulators.h"
#include "../../SceneEngine/PlacementsManager.h"
#include "../../SceneEngine/IntersectionTest.h"
#if 0
#include "../../SceneEngine/BasicLightingStateDelegate.h"
#include "../../SceneEngine/Terrain.h"
#include "../../SceneEngine/TerrainConfig.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#endif
#include "../../Utility/StringUtils.h"
#include "../../Math/Transformations.h"

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Runtime::InteropServices;

namespace GUILayer
{
    public value class HitRecord
    {
    public:
        EntityInterface::DocumentId _document;
        EntityInterface::EntityId _object;
        Vector3 _worldSpaceCollision;
        float _distance;
        uint64 _materialGuid;
        String^ _materialName;
        String^ _modelName;
        unsigned _drawCallIndex;
    };

    void ObjectSet::Add(uint64 document, uint64 id)
    {
        _nativePlacements->push_back(std::make_pair(document, id));
    }

    void ObjectSet::Clear() { _nativePlacements->clear(); }
    bool ObjectSet::IsEmpty() { return _nativePlacements->empty(); }

    void ObjectSet::DoFixup(SceneEngine::PlacementsEditor& placements)
    {
        placements.PerformGUIDFixup(
            AsPointer(_nativePlacements->begin()),
            AsPointer(_nativePlacements->end()));
    }
        
    void ObjectSet::DoFixup(PlacementsEditorWrapper^ placements)
    {
        DoFixup(placements->GetNative());
    }

    ObjectSet::ObjectSet()
    { 
        _nativePlacements.reset(new NativePlacementSet); 
    }
    ObjectSet::~ObjectSet()    { _nativePlacements.reset(); }

///////////////////////////////////////////////////////////////////////////////////////////////////

	CameraDescWrapper::CameraDescWrapper(ToolsRig::VisCameraSettings& camSettings)
    {
        _native.reset(new RenderCore::Techniques::CameraDesc(
            ToolsRig::AsCameraDesc(camSettings)));
    }

    CameraDescWrapper::~CameraDescWrapper() { _native.reset(); }

///////////////////////////////////////////////////////////////////////////////////////////////////

    public ref class EditorInterfaceUtils
    {
    public:
        static void* CalculateWorldToProjection(
            CameraDescWrapper^ camera, float viewportAspect)
        {
            auto proj = RenderCore::Techniques::Projection(*camera->_native, viewportAspect);
            auto temp = std::make_unique<Float4x4>(Combine(
                InvertOrthonormalTransform(camera->_native->_cameraToWorld), proj));
            return temp.release();
        }

        static System::Collections::Generic::ICollection<HitRecord>^
            RayIntersection(
                CameraDescWrapper^ camera, unsigned viewportWidth, unsigned viewportHeight,
                IntersectionTestSceneWrapper^ testScene,
                Vector3 start, Vector3 end, unsigned filter)
        {
            TRY
            {
				SceneEngine::IntersectionTestContext testContext {
					*camera->_native, UInt2{0,0}, UInt2{viewportWidth,viewportHeight},
					EngineDevice::GetInstance()->GetNative().GetDrawingApparatus() };
                auto firstResult = testScene->_scene->FirstRayIntersection(
                    testContext, std::make_pair(AsFloat3(start), AsFloat3(end)), filter);

                if (firstResult._type != 0) {
                    auto record = gcnew HitRecord;
                    record->_document = firstResult._objectGuid.first;
                    record->_object = firstResult._objectGuid.second;
                    record->_worldSpaceCollision = AsVector3(firstResult._worldSpaceCollision);
                    record->_distance = firstResult._distance;
                    record->_materialGuid = firstResult._materialGuid;
                    record->_materialName = clix::marshalString<clix::E_UTF8>(firstResult._materialName);
                    record->_modelName = clix::marshalString<clix::E_UTF8>(firstResult._modelName);
                    record->_drawCallIndex = firstResult._drawCallIndex;

                        // hack -- for placement objects, we must strip off the top 32 bits
                        //          from the object id.
                    if (firstResult._type == SceneEngine::IntersectionTestResult::Type::Placement) {
                        record->_object &= 0x00000000ffffffffull;
                    }

                    auto result = gcnew System::Collections::Generic::List<HitRecord>();
                    result->Add(*record);
                    return result;
                }
            }
            CATCH(const ::Assets::Exceptions::RetrievalError&) {}
            CATCH_END

            return nullptr;
        }

        static System::Collections::Generic::ICollection<HitRecord>^
            FrustumIntersection(
				CameraDescWrapper^ camera,
                IntersectionTestSceneWrapper^ testScene,
                const float matrix[],
                unsigned filter)
        {
            TRY
            {
                Float4x4 worldToProjection = Transpose(AsFloat4x4(matrix));

                SceneEngine::IntersectionTestContext testContext {
					*camera->_native, UInt2{0,0}, UInt2{0,0},
					EngineDevice::GetInstance()->GetNative().GetDrawingApparatus()};
				std::vector<SceneEngine::IntersectionTestResult> nativeResults;
                testScene->_scene->FrustumIntersection(
                    nativeResults,
                    testContext, worldToProjection, filter);

                if (!nativeResults.empty()) {

                    auto result = gcnew System::Collections::Generic::List<HitRecord>();
                    for (const auto& i: nativeResults) {
                        auto record = gcnew HitRecord;
                        record->_document = i._objectGuid.first;
                        record->_object = i._objectGuid.second;
                        record->_distance = i._distance;
                        record->_worldSpaceCollision = AsVector3(i._worldSpaceCollision);
                        record->_drawCallIndex = (unsigned)-1;

                            // hack -- for placement objects, we must strip off the top 32 bits
                            //          from the object id.
                        if (i._type == SceneEngine::IntersectionTestResult::Type::Placement) {
                            record->_object &= 0x00000000ffffffffull;
                        }
                    
                        result->Add(*record);
                    }

                    return result;
                }
            }
            CATCH(const ::Assets::Exceptions::RetrievalError&) {}
            CATCH_END

            return nullptr;
        }

        static unsigned AsTypeId(System::Type^ type)
        {
            if (type == bool::typeid)                   { return (unsigned)ImpliedTyping::TypeCat::Bool; }
            else if (type == System::Byte::typeid)      { return (unsigned)ImpliedTyping::TypeCat::UInt8; }
            else if (type == System::SByte::typeid)     { return (unsigned)ImpliedTyping::TypeCat::Int8; }
            else if (type == System::UInt16::typeid)    { return (unsigned)ImpliedTyping::TypeCat::UInt16; }
            else if (type == System::Int16::typeid)     { return (unsigned)ImpliedTyping::TypeCat::Int16; }
            else if (type == System::UInt32::typeid)    { return (unsigned)ImpliedTyping::TypeCat::UInt32; }
            else if (type == System::Int32::typeid)     { return (unsigned)ImpliedTyping::TypeCat::Int32; }
            else if (type == System::Single::typeid)    { return (unsigned)ImpliedTyping::TypeCat::Float; }

            // else if (type == System::UInt64::typeid)    { return (unsigned)ImpliedTyping::TypeCat::UInt64; }
            // else if (type == System::Int64::typeid)     { return (unsigned)ImpliedTyping::TypeCat::Int64; }
            // else if (type == System::Double::typeid)    { return (unsigned)ImpliedTyping::TypeCat::Double; }
            else if (type == System::Char::typeid)      { return (unsigned)ImpliedTyping::TypeCat::UInt16; }

            return (unsigned)ImpliedTyping::TypeCat::Void;
        }

        ref class ScatterPlaceOperation
        {
        public:
            List<Tuple<uint64, uint64>^>^ _toBeDeleted;
            List<Vector3>^ _creationPositions;
        };

        static ScatterPlaceOperation^ CalculateScatterOperation(
            PlacementsEditorWrapper^ placements,
            IntersectionTestSceneWrapper^ scene,
            IEnumerable<System::String^>^ modelName, 
            Vector3 centre, float radius, float density)
        {
            std::vector<SceneEngine::PlacementGUID> toBeDeleted;
            std::vector<Float3> spawnPositions;

            std::vector<std::string> convertedNames;
            std::vector<const char*> names;
            for each(System::String^ n in modelName) {
                auto str = clix::marshalString<clix::E_UTF8>(n);
                convertedNames.push_back(str);
                names.push_back(convertedNames[convertedNames.size()-1].c_str());
            }

            ToolsRig::CalculateScatterOperation(
                toBeDeleted, spawnPositions,
                placements->GetNative(), scene->GetNative(), 
                AsPointer(names.cbegin()), (unsigned)names.size(),
                AsFloat3(centre), radius, density);

            auto result = gcnew ScatterPlaceOperation();
            result->_toBeDeleted = gcnew List<Tuple<uint64, uint64>^>();
            result->_creationPositions = gcnew List<Vector3>();
            for (const auto& d:toBeDeleted) result->_toBeDeleted->Add(Tuple::Create(d.first, d.second & 0xffffffffull));
            for (const auto& p:spawnPositions) result->_creationPositions->Add(Vector3(p[0], p[1], p[2]));

            return result;
        }
        
        static Tuple<Vector3, Vector3>^ CalculatePlacementCellBoundary(
            EditorSceneManager^ sceneMan,
            EntityInterface::DocumentId doc)
        {
            auto boundary = sceneMan->GetScene()._placementsEditor->StallAndCalculateCellBoundary(doc);
            return Tuple::Create(AsVector3(boundary.first), AsVector3(boundary.second));
        }

#if defined(GUILAYER_SCENEENGINE)
        static bool GetTerrainHeight(
            [Out] float% height,
            IntersectionTestSceneWrapper^ testScene,
            float worldX, float worldY)
        {
            auto& terrain = *testScene->GetNative().GetTerrain();
            height = SceneEngine::GetTerrainHeight(
                *terrain.GetFormat().get(), terrain.GetConfig(), terrain.GetCoords(),
                Float2(worldX, worldY));
            return true;
        }

        static bool GetTerrainHeightAndNormal(
            [Out] float% height, [Out] Vector3% normal,
            IntersectionTestSceneWrapper^ testScene,
            float worldX, float worldY)
        {
            auto& terrain = *testScene->GetNative().GetTerrain();
            Float3 nativeNormal;
            float nativeHeight;
            bool queryResult = SceneEngine::GetTerrainHeightAndNormal(
                nativeHeight, nativeNormal,
                *terrain.GetFormat().get(), terrain.GetConfig(), terrain.GetCoords(),
                Float2(worldX, worldY));
            height = nativeHeight;
            normal = AsVector3(nativeNormal);
            return queryResult;
        }

        static bool GetTerrainUnderCursor(
            [Out] Vector3% intersection,
			CameraDescWrapper^ camera,
			TechniqueContextWrapper^ techniqueContext,
			unsigned viewportWidth, unsigned viewportHeight,
            IntersectionTestSceneWrapper^ testScene,
            unsigned ptx, unsigned pty)
        {
			SceneEngine::IntersectionTestContext testContext {
				*camera->_native, UInt2{0,0}, UInt2{viewportWidth,viewportHeight},
				techniqueContext->_techniqueContext.GetNativePtr()
			};
            auto test = testScene->_scene->UnderCursor(
                testContext, UInt2(ptx, pty),
                SceneEngine::IntersectionTestResult::Type::Terrain);
            if (test._type == SceneEngine::IntersectionTestResult::Type::Terrain) {
                intersection = AsVector3(test._worldSpaceCollision);
                return true;
            }
            return false;
        }

        static void GenerateBlankUberSurface(
            String^ uberSurfaceDirectory, 
            unsigned newCellCountX, unsigned newCellCountY,
            unsigned nodeDimensions, unsigned cellTreeDepth,
            IProgress^ progress)
        {
            auto nativeProgress = progress ? IProgress::CreateNative(progress) : nullptr;
            ToolsRig::GenerateBlankUberSurface(
                clix::marshalString<clix::E_UTF8>(uberSurfaceDirectory).c_str(),
                newCellCountX, newCellCountY,
                nodeDimensions, cellTreeDepth, nativeProgress.get());
        }

        static unsigned DefaultResolutionForLayer(SceneEngine::TerrainCoverageId layerId)
        {
            switch (layerId) {
            case SceneEngine::CoverageId_AngleBasedShadows: return 1;
            case SceneEngine::CoverageId_AmbientOcclusion: return 1;
            default: return 4;
            }
        }

        static unsigned DefaultOverlapForLayer(SceneEngine::TerrainCoverageId layerId)
        {
            switch (layerId) {
            case SceneEngine::CoverageId_AngleBasedShadows: return 1;
            case SceneEngine::CoverageId_AmbientOcclusion: return 1;
            default: return 1;
            }
        }

        static unsigned DefaultFormatCatForLayer(SceneEngine::TerrainCoverageId layerId)
        {
            switch (layerId) {
            case SceneEngine::CoverageId_AngleBasedShadows: return (unsigned)ImpliedTyping::TypeCat::UInt16;
            case SceneEngine::CoverageId_AmbientOcclusion:  return (unsigned)ImpliedTyping::TypeCat::UInt8;
            default: return (unsigned)ImpliedTyping::TypeCat::UInt8;
            }
        }

        static unsigned DefaultFormatCountForLayer(SceneEngine::TerrainCoverageId layerId)
        {
            switch (layerId) {
            case SceneEngine::CoverageId_AngleBasedShadows: return 2;
            case SceneEngine::CoverageId_AmbientOcclusion:  return 1;
            default: return 1;
            }
        }

        static TerrainConfig::CoverageLayerDesc^ DefaultCoverageLayer(
            TerrainConfig^ cfg, String^ baseDirectory, unsigned coverageLayerId) 
        {
            auto result = gcnew TerrainConfig::CoverageLayerDesc(
                baseDirectory, coverageLayerId);
            
            const auto layerRes = DefaultResolutionForLayer(coverageLayerId);
            const auto overlap = DefaultOverlapForLayer(coverageLayerId);
            result->NodeDims = AsVectorUInt2(layerRes * AsUInt2(cfg->NodeDims));
            result->Overlap = overlap;
            result->FormatCat = DefaultFormatCatForLayer(coverageLayerId);
            result->FormatArrayCount = DefaultFormatCountForLayer(coverageLayerId);

            return result;
        }

        static void GenerateShadowsSurface(
            TerrainConfig^ cfg, String^ uberSurfaceDir,
            IProgress^ progress)
        {
            auto nativeProgress = progress ? IProgress::CreateNative(progress) : nullptr;
            ToolsRig::GenerateShadowsSurface(
                cfg->GetNative(), clix::marshalString<clix::E_UTF8>(uberSurfaceDir).c_str(),
                true, nativeProgress.get());
        }

        static void GenerateAmbientOcclusionSurface(
            TerrainConfig^ cfg, String^ uberSurfaceDir,
            IProgress^ progress)
        {
            auto nativeProgress = progress ? IProgress::CreateNative(progress) : nullptr;
            ToolsRig::GenerateAmbientOcclusionSurface(
                cfg->GetNative(), clix::marshalString<clix::E_UTF8>(uberSurfaceDir).c_str(),
                true, nativeProgress.get());
        }

        static float GetSunPathAngle(EditorSceneManager^ sceneMan)
        {
            auto terr = sceneMan->GetScene()._terrainManager;
            if (!terr) return 0.f;

            return terr->GetConfig().SunPathAngle();
        }

        static void FlushTerrainToDisk(EditorSceneManager^ sceneMan, IProgress^ progress)
        {
            auto terr = sceneMan->GetScene()._terrainManager;
            if (!terr) return;

            auto nativeProgress = progress ? IProgress::CreateNative(progress) : nullptr;
            terr->FlushToDisk(nativeProgress.get());
        }

        static void SetShortCircuitSettings(EditorSceneManager^ sceneMan, bool gradFlagEnable, float gradFlagSpacing, float gradFlagThreshold0, float gradFlagThreshold1, float gradFlagThreshold2)
        {
            auto terr = sceneMan->GetScene()._terrainManager;
            if (!terr) return;
            terr->SetShortCircuitSettings(SceneEngine::GradientFlagsSettings(gradFlagEnable, gradFlagSpacing, gradFlagThreshold0, gradFlagThreshold1, gradFlagThreshold2));
        }

        static void GenerateCellFiles(
            TerrainConfig^ cfg, String^ uberSurfaceDir, 
            bool overwriteExisting, 
            float slopeThreshold0, float slopeThreshold1, float slopeThreshold2,
            IProgress^ progress)
        {
            auto nativeProgress = progress ? IProgress::CreateNative(progress) : nullptr;
            SceneEngine::GradientFlagsSettings gradFlagSettings(
                cfg->EncodedGradientFlags, cfg->ElementSpacing, 
                slopeThreshold0, slopeThreshold1, slopeThreshold2);
            ToolsRig::GenerateCellFiles(
                cfg->GetNative(), 
                clix::marshalString<clix::E_UTF8>(uberSurfaceDir).c_str(), 
                overwriteExisting, gradFlagSettings, nativeProgress.get());
        }

        static void GenerateCellFiles(
            TerrainConfig^ cfg, String^ uberSurfaceDir, 
            bool overwriteExisting, 
            unsigned coverageId,
            IProgress^ progress)
        {
            auto nativeProgress = progress ? IProgress::CreateNative(progress) : nullptr;
            ToolsRig::GenerateCellFiles(
                cfg->GetNative(), 
                clix::marshalString<clix::E_UTF8>(uberSurfaceDir).c_str(), 
                overwriteExisting, coverageId, nativeProgress.get());
        }

        static void GenerateMissingUberSurfaceFiles(
            TerrainConfig^ cfg, String^ uberSurfaceDir, 
            IProgress^ progress)
        {
            auto nativeProgress = progress ? IProgress::CreateNative(progress) : nullptr;
            ToolsRig::GenerateMissingUberSurfaceFiles(
                cfg->GetNative(), 
                clix::marshalString<clix::E_UTF8>(uberSurfaceDir).c_str(), 
                nativeProgress.get());
        }

        static void ExecuteTerrainExport(
            String^ dstFile, 
            TerrainConfig^ cfg, 
            String^ uberSurfaceDir,
            unsigned coverageId,
            IProgress^ progress)
        {
            auto nativeProgress = progress ? IProgress::CreateNative(progress) : nullptr;
            ToolsRig::ExecuteTerrainExport(
                clix::marshalString<clix::E_UTF8>(dstFile).c_str(), 
                cfg->GetNative(), 
                clix::marshalString<clix::E_UTF8>(uberSurfaceDir).c_str(), 
                coverageId, nativeProgress.get());
        }
#endif
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    IEnumerable<String^>^ EnvironmentSettingsSet::Names::get()
    {
        auto result = gcnew List<String^>();
        return result;
    }

    void EnvironmentSettingsSet::AddDefault()
    {
    }

    EnvironmentSettingsSet::EnvironmentSettingsSet(EditorSceneManager^ scene) {}
    EnvironmentSettingsSet::~EnvironmentSettingsSet() {}
}


