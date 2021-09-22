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
    public ref class EntityLayer
    {
    public:
                //// //// ////   G O B   I N T E R F A C E   //// //// ////
        using DocumentTypeId = EntityInterface::DocumentTypeId;
        using EntityTypeId = EntityInterface::EntityTypeId;
        using DocumentId = EntityInterface::DocumentId;
        using EntityId = EntityInterface::EntityId;
        using EntityTypeId = EntityInterface::EntityTypeId;
        using PropertyId = EntityInterface::PropertyId;
        using ChildListId = EntityInterface::ChildListId;

        DocumentId CreateDocument(DocumentTypeId docType);
        bool DeleteDocument(DocumentId doc, DocumentTypeId docType);

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

        EntityId AssignObjectId(DocumentId doc, EntityTypeId type);
        bool CreateObject(DocumentId doc, EntityId obj, EntityTypeId objType, IEnumerable<PropertyInitializer>^ initializers);
        bool DeleteObject(DocumentId doc, EntityId obj, EntityTypeId objType);
        bool SetProperty(DocumentId doc, EntityId obj, EntityTypeId objType, IEnumerable<PropertyInitializer>^ initializers);
        bool GetProperty(DocumentId doc, EntityId obj, EntityTypeId objType, PropertyId prop, void* dest, unsigned* destSize);

        bool SetObjectParent(DocumentId doc, 
            EntityId childId, EntityTypeId childTypeId, 
            EntityId parentId, EntityTypeId parentTypeId,
			ChildListId childList,
			int insertionPosition);

        EntityTypeId GetTypeId(System::String^ name);
        DocumentTypeId GetDocumentTypeId(System::String^ name);
        PropertyId GetPropertyId(EntityTypeId type, System::String^ name);
        ChildListId GetChildListId(EntityTypeId type, System::String^ name);

        EntityInterface::Switch& GetSwitch();

        EntityLayer(std::shared_ptr<EntityInterface::Switch> swtch);
        ~EntityLayer();
    protected:
        clix::shared_ptr<EntityInterface::Switch> _switch;
    };

}