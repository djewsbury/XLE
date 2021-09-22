// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "EntityLayer.h"
#include "MarshalString.h"
#include "../../Utility/PtrUtils.h"

namespace GUILayer
{
    using namespace EntityInterface;

    DocumentId EntityLayer::CreateDocument(DocumentTypeId docType) 
        { return _switch->CreateDocument(docType, ""); }
    bool EntityLayer::DeleteDocument(DocumentId doc, DocumentTypeId docType)
        { return _switch->DeleteDocument(doc, docType); }

    EntityId EntityLayer::AssignObjectId(DocumentId doc, EntityTypeId type)
        { return _switch->AssignObjectId(doc, type); }

    static std::vector<PropertyInitializer> AsNative(
        IEnumerable<EntityLayer::PropertyInitializer>^ initializers)
    {
        std::vector<PropertyInitializer> native;
        if (initializers) {
            for each(auto i in initializers) {
                PropertyInitializer n;
                n._prop = i._prop;
				n._src = { i._srcBegin, i._srcEnd };
                n._elementType = i._elementType;
                n._arrayCount = i._arrayCount;
                n._isString = i._isString;
                native.push_back(n);
            }
        }
        return std::move(native);
    }

    bool EntityLayer::CreateObject(DocumentId doc, EntityId obj, EntityTypeId objType, IEnumerable<PropertyInitializer>^ initializers)
    {
        auto native = AsNative(initializers);
        Identifier indentifier;
        auto intrf = _switch->GetInterface(
            indentifier, Identifier(doc, obj, objType));
        if (intrf)
            return intrf->CreateObject(indentifier, AsPointer(native.cbegin()), native.size()); 
        return false;
    }

    bool EntityLayer::DeleteObject(DocumentId doc, EntityId obj, EntityTypeId objType)
    { 
        Identifier indentifier;
        auto intrf = _switch->GetInterface(
            indentifier, Identifier(doc, obj, objType));
        if (intrf)
            return intrf->DeleteObject(indentifier); 
        return false;
    }

    bool EntityLayer::SetProperty(DocumentId doc, EntityId obj, EntityTypeId objType, IEnumerable<PropertyInitializer>^ initializers)
    { 
        auto native = AsNative(initializers);
        Identifier indentifier;
        auto intrf = _switch->GetInterface(
            indentifier, Identifier(doc, obj, objType));
        if (intrf)
            return intrf->SetProperty(indentifier, AsPointer(native.cbegin()), native.size()); 
        return false;
    }

    bool EntityLayer::GetProperty(DocumentId doc, EntityId obj, EntityTypeId objType, PropertyId prop, void* dest, unsigned* destSize)
    { 
        Identifier indentifier;
        auto intrf = _switch->GetInterface(
            indentifier, Identifier(doc, obj, objType));
        if (intrf)
            return intrf->GetProperty(indentifier, prop, dest, destSize); 
        return false;
    }

    DocumentTypeId EntityLayer::GetDocumentTypeId(System::String^ name)                  { return _switch->GetDocumentTypeId(clix::marshalString<clix::E_UTF8>(name).c_str()); }
    EntityTypeId EntityLayer::GetTypeId(System::String^ name)                            { return _switch->GetTypeId(clix::marshalString<clix::E_UTF8>(name).c_str()); }
    PropertyId EntityLayer::GetPropertyId(EntityTypeId type, System::String^ name)       { return _switch->GetPropertyId(type, clix::marshalString<clix::E_UTF8>(name).c_str()); }
    ChildListId EntityLayer::GetChildListId(EntityTypeId type, System::String^ name)     { return _switch->GetChildListId(type, clix::marshalString<clix::E_UTF8>(name).c_str()); }

    bool EntityLayer::SetObjectParent(
        DocumentId doc, 
        EntityId childId, EntityTypeId childTypeId, 
        EntityId parentId, EntityTypeId parentTypeId, 
		ChildListId childList,
		int insertionPosition)
    {
        Identifier child, parent;
        auto intrfChild = _switch->GetInterface(
            child, Identifier(doc, childId, childTypeId));
        auto intrfParent = _switch->GetInterface(
            parent, Identifier(doc, parentId, parentTypeId));

        if (intrfChild && intrfChild == intrfParent)
            return intrfChild->SetParent(child, parent, childList, insertionPosition);
        return false;
    }

    EntityInterface::Switch& EntityLayer::GetSwitch()
    {
        return *_switch.get();
    }

    EntityLayer::EntityLayer(std::shared_ptr<Switch> swtch)
    : _switch(std::move(swtch))
    {}

    EntityLayer::~EntityLayer() 
    {}
}
