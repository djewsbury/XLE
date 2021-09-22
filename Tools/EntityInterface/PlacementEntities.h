// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EntityInterface.h"

namespace SceneEngine
{
    class PlacementsManager;
    class PlacementsEditor;
    class DynamicImposters;
}

namespace EntityInterface
{
    class PlacementEntities : public IEntityInterface
    {
    public:
        DocumentId CreateDocument(DocumentTypeId docType, const char initializer[]);
        bool DeleteDocument(DocumentId doc, DocumentTypeId docType);

        EntityId AssignObjectId(DocumentId doc, EntityTypeId type) const;
        bool CreateObject(const Identifier& id, const PropertyInitializer initializers[], size_t initializerCount);
        bool DeleteObject(const Identifier& id);
        bool SetProperty(const Identifier& id, const PropertyInitializer initializers[], size_t initializerCount);
        bool GetProperty(const Identifier& id, PropertyId prop, void* dest, unsigned* destSize) const;
        bool SetParent(const Identifier& child, const Identifier& parent, ChildListId childList, int insertionPosition);

        EntityTypeId GetTypeId(const char name[]) const;
        DocumentTypeId GetDocumentTypeId(const char name[]) const;
        PropertyId GetPropertyId(EntityTypeId type, const char name[]) const;
        ChildListId GetChildListId(EntityTypeId type, const char name[]) const;

		void PrintDocument(std::ostream& stream, DocumentId doc, unsigned indent) const;

        PlacementEntities(
            std::shared_ptr<SceneEngine::PlacementsManager> manager,
            std::shared_ptr<SceneEngine::PlacementsEditor> editor,
			std::shared_ptr<SceneEngine::PlacementsEditor> hiddenObjects);
        ~PlacementEntities();

    protected:
        std::shared_ptr<SceneEngine::PlacementsManager> _manager;
        std::shared_ptr<SceneEngine::PlacementsEditor> _editor;
		std::shared_ptr<SceneEngine::PlacementsEditor> _hiddenObjects;

        unsigned _cellCounter;
    };

    class RetainedEntities;
    void RegisterDynamicImpostersFlexObjects(
        RetainedEntities& flexSys, 
        std::shared_ptr<SceneEngine::DynamicImposters> imposters);
}

