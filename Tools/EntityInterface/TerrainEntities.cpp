// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TerrainEntities.h"
#include "RetainedEntities.h"
#include "../../SceneEngine/Terrain.h"
#include "../../SceneEngine/TerrainConfig.h"
#include "../../SceneEngine/TerrainMaterial.h"
#include "../../SceneEngine/SceneEngineUtils.h"
#include "../../SceneEngine/TerrainFormat.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Meta/AccessorSerialize.h"

namespace EntityInterface
{
    static const EntityTypeId ObjectType_Terrain = 1;
    static const PropertyId Property_UberSurfaceDir = 201;
    static const PropertyId Property_Offset = 202;

///////////////////////////////////////////////////////////////////////////////////////////////////

	DocumentId TerrainEntities::CreateDocument(DocumentTypeId docType, const char initializer[]) { return 0; }
	bool TerrainEntities::DeleteDocument(DocumentId doc, DocumentTypeId docType) { return false; }

	EntityId TerrainEntities::AssignObjectId(DocumentId doc, EntityTypeId type) const
	{
		if (type == ObjectType_Terrain) {
		    return 1;
        }
        static EntityId incrementalId = 100;
        return incrementalId++;
	}

	bool TerrainEntities::CreateObject(
		const Identifier& id,
		const PropertyInitializer initializers[], size_t initializerCount)
	{
		if (id.ObjectType() == ObjectType_Terrain) {
            _terrainManager->SetWorldSpaceOrigin(Float3(0.f, 0.f, 0.f));
            for (size_t c=0; c<initializerCount; ++c)
                SetTerrainProperty(initializers[c]);
		    return true;
        }
        return false;
	}

	bool TerrainEntities::DeleteObject(
		const Identifier& id)
	{
		if (id.ObjectType() == ObjectType_Terrain) {
            _uberSurfaceDir = ::Assets::rstring();
    		return true;
        }
        return false;
	}

	bool TerrainEntities::SetProperty(
		const Identifier& id,
		const PropertyInitializer initializers[], size_t initializerCount)
	{
		if (id.ObjectType() == ObjectType_Terrain) {
            for (size_t c=0; c<initializerCount; ++c)
                SetTerrainProperty(initializers[c]);
            return true;
        }

		return false;
	}

    bool TerrainEntities::SetTerrainProperty(const PropertyInitializer& prop)
    {
        if (prop._prop == Property_UberSurfaceDir) {

            _uberSurfaceDir = {(const char*)prop._src.begin(), (const char*)prop._src.end()};

            if (!_uberSurfaceDir.empty())
                _terrainManager->LoadUberSurface(_uberSurfaceDir);
            return true;

        } else if (prop._prop == Property_Offset) {
			assert(prop._src.size() >= sizeof(Float3));
            _terrainManager->SetWorldSpaceOrigin(*(const Float3*)prop._src.begin());
            return true;
        }

        return false;
    }

	bool TerrainEntities::GetProperty(
		const Identifier& id, PropertyId prop,
		void* dest, unsigned* destSize) const
	{
		assert(0);		
		return false;
	}

    bool TerrainEntities::SetParent(const Identifier& child, const Identifier& parent, ChildListId childList, int insertionPosition)
    {
        return false;
    }

	EntityTypeId TerrainEntities::GetTypeId(const char name[]) const
	{
		if (!XlCompareString(name, "Terrain")) return ObjectType_Terrain;
		return 0;
	}

	DocumentTypeId TerrainEntities::GetDocumentTypeId(const char name[]) const
	{
		return 0;
	}

	PropertyId TerrainEntities::GetPropertyId(EntityTypeId type, const char name[]) const
	{
        if (type == ObjectType_Terrain) {
            if (!XlCompareString(name, "UberSurfaceDir"))   return Property_UberSurfaceDir;
            if (!XlCompareString(name, "Offset"))           return Property_Offset;
        }

		return 0;
	}

	ChildListId TerrainEntities::GetChildListId(EntityTypeId type, const char name[]) const
	{
		return 0;
	}

	void TerrainEntities::PrintDocument(std::ostream& stream, DocumentId doc, unsigned indent) const
	{
		assert(0);
		stream << "Terrain Entities (printing not supported)" << std::endl;
	}

	void TerrainEntities::OnTerrainReload()
    {
        if (!_uberSurfaceDir.empty())
            _terrainManager->LoadUberSurface(_uberSurfaceDir.c_str());
    }

	TerrainEntities::TerrainEntities(
        std::shared_ptr<SceneEngine::TerrainManager> terrainManager) 
    : _terrainManager(std::move(terrainManager))
    {
    }

	TerrainEntities::~TerrainEntities() {}

    ///////////////////////////////////////////////////////////////////////////////////////////////////

    static void UpdateTerrainBaseTexture(
        const RetainedEntities& sys,
        const RetainedEntity& obj,
        SceneEngine::TerrainManager& terrainMan)
    {
        using namespace SceneEngine;

        auto asset = CreateFromParameters<TerrainMaterialConfig>(obj._properties);

            // rebuild all of the material binding information as well
        // {
        //     asset._strataMaterials.clear();
        //     auto matType = sys.GetTypeId((const utf8*)"TerrainStrataMaterial");
        //     for (auto c=obj._children.cbegin(); c!=obj._children.end(); ++c) {
        //         auto* mat = sys.GetEntity(obj._doc, *c);
        //         if (!mat || mat->_type != matType) continue;
        // 
        //         auto nativeMat = CreateFromParameters<TerrainMaterialConfig::StrataMaterial>(mat->_properties);
        //         for (auto c=mat->_children.begin(); c!=mat->_children.end(); ++c) {
        //             auto* strataObj = sys.GetEntity(mat->_doc, *c);
        //             if (!strataObj) continue;
        // 
        //             nativeMat._strata.emplace_back(
        //                 CreateFromParameters<TerrainMaterialConfig::StrataMaterial::Strata>(strataObj->_properties));
        //         }
        // 
        //         asset._strataMaterials.emplace_back(std::move(nativeMat));
        //     }
        // }

        {
            asset._gradFlagMaterials.clear();
            auto matType = sys.GetTypeId("TerrainGradFlagMaterial");
            for (auto c=obj._children.cbegin(); c!=obj._children.end(); ++c) {
                auto* mat = sys.GetEntity(obj._doc, c->second);
                if (!mat || mat->_type != matType) continue;

                asset._gradFlagMaterials.emplace_back(
                    CreateFromParameters<TerrainMaterialConfig::GradFlagMaterial>(mat->_properties));
            }
        }

        {
            asset._procTextures.clear();
            auto matType = sys.GetTypeId("TerrainProcTexture");
            for (auto c=obj._children.cbegin(); c!=obj._children.end(); ++c) {
                auto* mat = sys.GetEntity(obj._doc, c->second);
                if (!mat || mat->_type != matType) continue;

                asset._procTextures.emplace_back(
                    CreateFromParameters<TerrainMaterialConfig::ProcTextureSetting>(mat->_properties));
            }
        }

        terrainMan.LoadMaterial(asset);
    }

    static void ClearTerrainBaseTexture(SceneEngine::TerrainManager& terrainMan)
    {
        terrainMan.LoadMaterial(SceneEngine::TerrainMaterialConfig());
    }

    void RegisterTerrainFlexObjects(
        RetainedEntities& flexSys, 
        std::shared_ptr<SceneEngine::TerrainManager> terrainMan)
    {
        std::weak_ptr<SceneEngine::TerrainManager> weakPtrTerrainMan = terrainMan;
        flexSys.RegisterCallback(
            flexSys.GetTypeId("TerrainBaseTexture"),
            [weakPtrTerrainMan](const RetainedEntities& flexSys, const Identifier& obj, RetainedEntities::ChangeType changeType)
            {
                auto l = weakPtrTerrainMan.lock();
                if (!l) return;

                if (changeType != RetainedEntities::ChangeType::Delete) {
                    auto* object = flexSys.GetEntity(obj);
                    if (object)
                        UpdateTerrainBaseTexture(flexSys, *object, *l);
                } else {
                    ClearTerrainBaseTexture(*l);
                }
            });
    }

    void ReloadTerrainFlexObjects(RetainedEntities& flexSys, SceneEngine::TerrainManager& terrainMan)
    {
        auto objs = flexSys.FindEntitiesOfType(flexSys.GetTypeId("TerrainBaseTexture"));
        for (auto i=objs.cbegin(); i!=objs.cend(); ++i)
            UpdateTerrainBaseTexture(flexSys, **i, terrainMan);
    }
}
