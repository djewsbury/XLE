// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../EntityInterface/EntityInterface.h"
#include "CLIXAutoPtr.h"

using namespace System::Collections::Generic;
namespace EntityInterface { class Switch; }

namespace GUILayer
{
    struct EntityLayerPimpl;

    public ref class EntityLayer
    {
    public:
                //// //// ////   G O B   I N T E R F A C E   //// //// ////
        using DocumentId = EntityInterface::Switch::DocumentId;
        using EntityId = EntityInterface::EntityId;
        using DocumentTypeId = uint32_t;
        using EntityTypeId = uint32_t;
        using PropertyId = uint32_t;
        using ChildListId = uint32_t;

        DocumentId CreateDocument(DocumentTypeId docType);
        bool DeleteDocument(DocumentId doc);

        value struct PropertyInitializer
        {
            PropertyId _prop;
            const void* _srcBegin, *_srcEnd;
            unsigned _elementType;
            unsigned _arrayCount;
            bool _isString;

            PropertyInitializer(PropertyId prop, const void* srcBegin, const void* srcEnd, unsigned elementType, unsigned arrayCount, bool isString)
                : _prop(prop), _srcBegin(srcBegin), _srcEnd(srcEnd), _elementType(elementType), _arrayCount(arrayCount), _isString(isString) {}
        };

        EntityId AssignEntityId(DocumentId doc);
        bool CreateEntity(DocumentId doc, EntityTypeId objType, EntityId obj, IEnumerable<PropertyInitializer>^ initializers);
        bool DeleteEntity(DocumentId doc, EntityId obj);
        bool SetProperty(DocumentId doc, EntityId obj, IEnumerable<PropertyInitializer>^ initializers);
        bool GetProperty(DocumentId doc, EntityId obj, PropertyId prop, void* dest, unsigned* destSize);

        bool SetObjectParent(
            DocumentId doc, 
            EntityId childId, EntityId parentId,
			ChildListId childList, int insertionPosition);

        EntityTypeId GetTypeId(System::String^ name);
        DocumentTypeId GetDocumentTypeId(System::String^ name);
        PropertyId GetPropertyId(EntityTypeId type, System::String^ name);
        ChildListId GetChildListId(EntityTypeId type, System::String^ name);

        uint64_t HashNameForTypeId(EntityTypeId);
        System::Tuple<uint64_t, uint64_t>^ QueryNativeHighlightableId(DocumentId doc, EntityId obj);

        EntityInterface::Switch& GetSwitch();

        EntityLayer(std::shared_ptr<EntityInterface::Switch> swtch);
        ~EntityLayer();
    protected:
        clix::shared_ptr<EntityInterface::Switch> _switch;
        clix::shared_ptr<EntityLayerPimpl> _pimpl;
    };

}