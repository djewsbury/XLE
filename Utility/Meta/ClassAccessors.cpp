// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ClassAccessors.h"
#include "../ParameterBox.h"        // for ParameterBox::MakeParameterNameHash

namespace Utility
{
    bool ClassAccessors::Set(
        void* dst, PropertyName id,
        IteratorRange<const void*> src, ImpliedTyping::TypeDesc srcType) const
    {
		assert(src.size() == srcType.GetSize());
        auto i = LowerBound(_properties, id._hash);
        if (i!=_properties.end() && i->first == id._hash)
            if (i->second._setter)
                return i->second._setter(dst, src, srcType);
        return false;
    }

    bool ClassAccessors::Get(
        IteratorRange<void*> dst, ImpliedTyping::TypeDesc dstType,
        const void* src, PropertyName id) const
    {
        auto i = LowerBound(_properties, id._hash);
        if (i!=_properties.end() && i->first == id._hash)
            if (i->second._getter)
                return i->second._getter(src, dst, dstType);

        return false;
    }

    std::optional<std::string> ClassAccessors::GetAsString(const void* srcObject, PropertyName id, bool strongTyping) const
    {
        auto i = LowerBound(_properties, id._hash);
        if (i!=_properties.end() && i->first == id._hash)
            if (i->second._getAsString)
                return i->second._getAsString(srcObject, strongTyping);
        return {};
    }

    bool ClassAccessors::SetFromString(
        void* dstObject, PropertyName id,
        StringSection<> src) const
    {
        return Set(
            dstObject, id,
            MakeIteratorRange(src.begin(), src.end()), ImpliedTyping::TypeDesc{ImpliedTyping::TypeOf<const char*>()._type, (uint16_t)src.size(), ImpliedTyping::TypeHint::String});
    }

    std::optional<ImpliedTyping::TypeDesc> ClassAccessors::GetNaturalType(PropertyName id) const
    {
        auto i = LowerBound(_properties, id._hash);
        if (i!=_properties.end() && i->first == id._hash)
            return i->second._naturalType;
		return {};
	}

    auto ClassAccessors::PropertyForId(uint64_t id) -> Property&
    {
        auto i = LowerBound(_properties, id);
        if (i==_properties.end() || i->first != id)
            i=_properties.insert(i, std::make_pair(id, Property()));
        return i->second;
    }

    ClassAccessors::ClassAccessors(size_t associatedType) 
        : _associatedType(associatedType) {}
    ClassAccessors::~ClassAccessors() {}


    namespace Legacy
    {
        std::pair<void*, const ClassAccessors*> ClassAccessorsWithChildLists::TryCreateChild(
            void* dst, uint64_t childListId) const
        {
            auto i = LowerBound(_childLists, childListId);
            if (i!=_childLists.end() && i->first == childListId) {
                void* created = i->second._createFn(dst);
                return std::make_pair(created, i->second._childProps);
            }
            return std::make_pair(nullptr, nullptr);
        }
    }

    ClassAccessors::PropertyName::PropertyName(StringSection<> name)
    {
        _hash = ParameterBox::MakeParameterNameHash(name);
    }

    ClassAccessors::PropertyName::PropertyName(const char name[])
    {
        _hash = ParameterBox::MakeParameterNameHash(name);
    }
}

