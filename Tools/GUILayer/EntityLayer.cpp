// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "EntityLayer.h"
#include "MarshalString.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/MemoryUtils.h"

namespace GUILayer
{
    using namespace EntityInterface;

    using RetainedStringAndHash = std::pair<std::string, uint64_t>;
    struct EntityLayerPimpl
    {
        std::vector<std::pair<uint32_t, RetainedStringAndHash>> _documentTypes;
        std::vector<std::pair<uint32_t, RetainedStringAndHash>> _entityTypes;
        std::vector<std::pair<uint32_t, RetainedStringAndHash>> _properties;
        std::vector<std::pair<uint32_t, RetainedStringAndHash>> _childLists;
        uint32_t _nextDocumentTypeId = 1;
        uint32_t _nextEntityTypeId = 1;
        uint32_t _nextPropertyId = 1;
        uint32_t _nextChildListId = 1;
    };

    auto EntityLayer::CreateDocument(DocumentTypeId docType) -> DocumentId
    {
        auto i = LowerBound(_pimpl->_documentTypes, docType);
        if (i != _pimpl->_documentTypes.end() && i->first == docType) {
            return _switch->CreateDocument(i->second.first, "");
        } else {
            return 0;
        }
    }
    bool EntityLayer::DeleteDocument(DocumentId doc)
        { return _switch->DeleteDocument(doc); }

    static void AsNativeHelper(
        std::vector<PropertyInitializer>& initializers,
        ImpliedTyping::TypeCat type,
        uint32_t arrayCount,
        ImpliedTyping::TypeHint typeHint,
        IteratorRange<const void*> data,
        EntityInterface::StringAndHash prop)
    {
        PropertyInitializer init;
        init._type = ImpliedTyping::TypeDesc { type, arrayCount, typeHint };
        init._data = data;
        init._prop = prop;
        initializers.push_back(init);
    }

    static std::vector<PropertyInitializer> AsNative(
        EntityLayerPimpl& pimpl,
        IEnumerable<EntityLayer::PropertyInitializer>^ initializers)
    {
        std::vector<PropertyInitializer> native;
        if (initializers) {
            for each(auto i in initializers) {

                auto lookup = LowerBound(pimpl._properties, i._prop);
                if (lookup == pimpl._properties.end() || lookup->first != i._prop)
                    continue;

                // we need a native helper function because ImpliedTyping::TypeDesc has special alignas() declaration
                AsNativeHelper(
                    native,
                    (ImpliedTyping::TypeCat)i._elementType,
                    i._arrayCount,
                    i._isString ? ImpliedTyping::TypeHint::String : ImpliedTyping::TypeHint::None,
                    MakeIteratorRange(i._srcBegin, i._srcEnd), EntityInterface::StringAndHash{ lookup->second.first, lookup->second.second });
            }
        }
        return std::move(native);
    }

    EntityId EntityLayer::AssignEntityId(DocumentId doc)
    {
        auto intrf = _switch->GetInterface(doc);
        if (intrf) {
            return intrf->AssignEntityId();
        }
        return ~0ull;
    }

    bool EntityLayer::CreateEntity(DocumentId doc, EntityTypeId objType, EntityId obj, IEnumerable<PropertyInitializer>^ initializers)
    {
        auto native = AsNative(*_pimpl.get(), initializers);
        auto intrf = _switch->GetInterface(doc);
        if (intrf) {
            auto i = LowerBound(_pimpl->_entityTypes, objType);
            if (i != _pimpl->_entityTypes.end() && i->first == objType)
                return intrf->CreateEntity({i->second.first, i->second.second}, obj, native); 
        }
        return false;
    }

    bool EntityLayer::DeleteEntity(DocumentId doc, EntityId obj)
    { 
        auto intrf = _switch->GetInterface(doc);
        if (intrf)
            return intrf->DeleteEntity(obj);
        return false;
    }

    bool EntityLayer::SetProperty(DocumentId doc, EntityId obj, IEnumerable<PropertyInitializer>^ initializers)
    { 
        auto native = AsNative(*_pimpl.get(), initializers);
        auto intrf = _switch->GetInterface(doc);
        if (intrf)
            return intrf->SetProperty(obj, native); 
        return false;
    }

    static bool GetPropertyHelper(
        EntityInterface::IMutableEntityDocument& doc,
        EntityId entity,
        EntityInterface::StringAndHash prop,
        IteratorRange<void*> dest,
        unsigned* destSize)
    {
        assert(destSize);
        assert(!dest.empty());
        auto nativeResult = doc.GetProperty(entity, prop, dest);
        if (!nativeResult) return false;
        *destSize = nativeResult.value().GetSize();
        return true;
    }

    bool EntityLayer::GetProperty(DocumentId doc, EntityId obj, PropertyId prop, void* dest, unsigned* destSize)
    { 
        assert(destSize);
        assert(dest);
        auto intrf = _switch->GetInterface(doc);
        if (intrf) {
            auto i = LowerBound(_pimpl->_properties, prop);
            if (i != _pimpl->_properties.end() && i->first == prop) {
                // we need a helper function to isolate ImpliedTyping::TypeDesc from managed code
                return GetPropertyHelper(
                    *intrf, obj,
                    EntityInterface::StringAndHash{i->second.first, i->second.second},
                    MakeIteratorRange(dest, PtrAdd(dest, *destSize)),
                    destSize);
            }
        }
        return false;
    }

    bool EntityLayer::SetObjectParent(
        DocumentId doc, 
        EntityId childId,
        EntityId parentId,
		ChildListId childList,
		int insertionPosition)
    {
        auto intrf = _switch->GetInterface(doc);
        if (intrf) {
            auto i = LowerBound(_pimpl->_childLists, childList);
            if (i != _pimpl->_childLists.end() && i->first == childList)
                return intrf->SetParent(childId, parentId, {i->second.first, i->second.second}, insertionPosition);
        }
        return false;
    }

    auto EntityLayer::GetDocumentTypeId(System::String^ name) -> DocumentTypeId
    {
        auto nativeName = clix::marshalString<clix::E_UTF8>(name);
        auto hash = Hash64(nativeName);
        for (auto& i:_pimpl->_documentTypes)
            if (i.second.second == hash)
                return i.first;

        auto result = _pimpl->_nextDocumentTypeId++;
        _pimpl->_documentTypes.emplace_back(result, RetainedStringAndHash{nativeName, hash});
        return result;
    }

    auto EntityLayer::GetTypeId(System::String^ name) -> EntityTypeId
    {
        auto nativeName = clix::marshalString<clix::E_UTF8>(name);
        auto hash = Hash64(nativeName);
        for (auto& i:_pimpl->_entityTypes)
            if (i.second.second == hash)
                return i.first;

        auto result = _pimpl->_nextEntityTypeId++;
        _pimpl->_entityTypes.emplace_back(result, RetainedStringAndHash{nativeName, hash});
        return result;
    }

    auto EntityLayer::GetPropertyId(EntityTypeId type, System::String^ name) -> PropertyId
    {
        auto nativeName = clix::marshalString<clix::E_UTF8>(name);
        auto hash = Hash64(nativeName);
        for (auto& i:_pimpl->_properties)
            if (i.second.second == hash)
                return i.first;

        auto result = _pimpl->_nextPropertyId++;
        _pimpl->_properties.emplace_back(result, RetainedStringAndHash{nativeName, hash});
        return result;
    }

    auto EntityLayer::GetChildListId(EntityTypeId type, System::String^ name) -> ChildListId
    {
        auto nativeName = clix::marshalString<clix::E_UTF8>(name);
        auto hash = Hash64(nativeName);
        for (auto& i:_pimpl->_childLists)
            if (i.second.second == hash)
                return i.first;

        auto result = _pimpl->_nextChildListId++;
        _pimpl->_childLists.emplace_back(result, RetainedStringAndHash{nativeName, hash});
        return result;
    }

    uint64_t EntityLayer::HashNameForTypeId(EntityTypeId type)
    {
        for (auto& i:_pimpl->_entityTypes)
            if (i.first == type)
                return i.second.second;
        return ~0ull;
    }

    EntityInterface::Switch& EntityLayer::GetSwitch()
    {
        return *_switch.get();
    }

    EntityLayer::EntityLayer(std::shared_ptr<Switch> swtch)
    : _switch(std::move(swtch))
    {
        _pimpl = std::make_shared<EntityLayerPimpl>();
    }

    EntityLayer::~EntityLayer() 
    {
        _pimpl.reset();
    }
}
