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
    class PlacementEntities : public IMutableEntityDocument, public ITranslateHighlightableId
    {
    public:
        EntityId AssignEntityId() override;
        bool CreateEntity(StringAndHash id, EntityId, IteratorRange<const PropertyInitializer*>) override;
        bool DeleteEntity(EntityId id) override;
        bool SetProperty(EntityId id, IteratorRange<const PropertyInitializer*>) override;
        std::optional<ImpliedTyping::TypeDesc> GetProperty(EntityId id, StringAndHash prop, IteratorRange<void*> destinationBuffer) const override;
        bool SetParent(EntityId child, EntityId parent, StringAndHash childList, int insertionPosition) override;

        std::pair<uint64_t, uint64_t> QueryHighlightableId(EntityId) override;

		void PrintDocument(std::ostream& stream, DocumentId doc, unsigned indent) const;

        PlacementEntities(
            std::shared_ptr<SceneEngine::PlacementsManager> manager,
            std::shared_ptr<SceneEngine::PlacementsEditor> editor,
			std::shared_ptr<SceneEngine::PlacementsEditor> hiddenObjects,
            StringSection<> initializer,
            uint64_t documentId);
        ~PlacementEntities();

    protected:
        std::shared_ptr<SceneEngine::PlacementsManager> _manager;
        std::shared_ptr<SceneEngine::PlacementsEditor> _editor;
		std::shared_ptr<SceneEngine::PlacementsEditor> _hiddenObjects;

        uint64_t _cellId = ~0ull;
    };

    std::shared_ptr<Switch::IDocumentType> CreatePlacementEntitiesSwitch(
        std::shared_ptr<SceneEngine::PlacementsManager> manager,
        std::shared_ptr<SceneEngine::PlacementsEditor> editor,
        std::shared_ptr<SceneEngine::PlacementsEditor> hiddenObjects);

    class RetainedEntities;
    void RegisterDynamicImpostersFlexObjects(
        RetainedEntities& flexSys, 
        std::shared_ptr<SceneEngine::DynamicImposters> imposters);
}

