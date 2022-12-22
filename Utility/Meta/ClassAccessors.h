// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../FunctionUtils.h"
#include "../ImpliedTyping.h"
#include "../Optional.h"
#include "../Utility/MemoryUtils.h"
#include <string>
#include <functional>
#include <vector>

namespace Utility
{
    namespace ImpliedTyping { class TypeDesc; }
    namespace Internal { class ClassAccessorsHelper; }

    /// <summary>Get and set properties associated with a native C++ class</summary>
    ///
    /// This class is designed to facilite interaction between a native class and data.
    /// For a class type, we can define get and set operations, with a given string name. The
    /// system can use these properties for serialisation tasks (as well as binding to data
    /// in various ways.
    ///
    /// It basically functions as a very simple meta data system. There are far more complicated
    /// meta data solutions; but this class captures some very specific core functionality that
    /// they provide -- just the ability to ability to interact with arbitrary "properties" (sometimes
    /// called fields or members or a variety of different names) and get and set them with type
    /// casting as needed.
    ///
    /// You can build a lot with even just that basic core -- so this provides a way to do that without
    /// extra overhead of a complete metadata system.
    ///
    /// Classes with accessors can be used with the AccessorSerialize and AccessorDeserialize functions.
    /// These can load and save these types automatically.
    class ClassAccessors
    {
    public:

        using PropertyNameHash = uint64_t;
        class PropertyName
        {
        public:
            PropertyNameHash _hash;

            PropertyName(StringSection<> name);
            PropertyName(PropertyNameHash hash);
            PropertyName(const char name[]);
        };

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////

        class Property
        {
        public:
            using SetterFn      = std::function<bool(void*, IteratorRange<const void*>, ImpliedTyping::TypeDesc)>;
            using GetterFn      = std::function<bool(const void*, IteratorRange<void*>, ImpliedTyping::TypeDesc)>;
            using GetAsStringFn = std::function<std::string(const void*, bool)>;

            std::string                 _name;
            SetterFn                    _setter;
            GetterFn                    _getter;
            GetAsStringFn               _getAsString;

            std::optional<ImpliedTyping::TypeDesc>     _naturalType;
        };

        IteratorRange<const std::pair<PropertyNameHash, Property>*> GetProperties() const { return MakeIteratorRange(_properties); }

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
                ////   P R O P E R T I E S   I N T E R F A C E   ////

        template<typename ResultType, typename ObjectType>
            std::optional<ResultType> Get(const ObjectType& srcObject, PropertyName id) const;
        template<typename ValueType, typename ObjectType>
            bool Set(ObjectType& dstObject, PropertyName id, const ValueType& valueSrc) const;

        bool Get(
            IteratorRange<void*> dst, ImpliedTyping::TypeDesc dstType,
            const void* srcObject, PropertyName id) const;
        bool Set(
            void* dstObject, PropertyName id,
            IteratorRange<const void*> src, ImpliedTyping::TypeDesc srcType) const;

        template<typename ObjectType>
            std::optional<std::string> GetAsString(const ObjectType& srcObject, PropertyName id, bool strongTyping = false) const;
        template<typename ObjectType>
            bool SetFromString(ObjectType& srcObject, PropertyName id, StringSection<> src) const;
        
        std::optional<std::string> GetAsString(const void* srcObject, PropertyName id, bool strongTyping = false) const;
        bool SetFromString(
            void* dstObject, PropertyName id,
            StringSection<> src) const;

        std::optional<ImpliedTyping::TypeDesc> GetNaturalType(PropertyName id) const;
            
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
                ////   I N I T I A L I Z A T I O N   I N T E R F A C E   ////

        template<typename GetFn, typename SetFn>
            void Add(
                StringSection<> name, 
                GetFn&& getter, SetFn&& setter, 
                const std::optional<ImpliedTyping::TypeDesc>& naturalType = {});

        template<typename ObjectType, typename MemberType>
            void Add(
                StringSection<> name, 
                MemberType ObjectType::*ptrToMember);

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////

        ClassAccessors(size_t associatedType);
        ~ClassAccessors();

    protected:
        size_t _associatedType;
        std::vector<std::pair<PropertyNameHash, Property>> _properties;

        Property& PropertyForId(PropertyNameHash id);

        friend Internal::ClassAccessorsHelper;
    };

    namespace Legacy
    {
        class ClassAccessorsWithChildLists : public ClassAccessors
        {
        public:
            class ChildList
            {
            public:
                using CreateFn      = std::function<void*(void*)>;
                using GetCountFn    = std::function<size_t(const void*)>;
                using GetByIndexFn  = std::function<const void*(const void*,size_t)>;
                using GetByKeyFn    = std::function<const void*(const void*,uint64_t)>;

                std::string                 _name;
                const ClassAccessors*       _childProps;
                CreateFn                    _createFn;
                GetCountFn                  _getCount;
                GetByIndexFn                _getByIndex;
                GetByKeyFn                  _getByKeyFn;
            };

            size_t GetChildListCount() const { return _childLists.size(); }
            const ChildList& GetChildListByIndex(size_t index) const { return _childLists[index].second; }

            std::pair<void*, const ClassAccessors*> TryCreateChild(void* dst, uint64_t childListId) const;

            template<typename Type>
                std::pair<void*, const ClassAccessors*> TryCreateChild(Type& dst, uint64_t childListId) const;

            template<typename ChildType, typename CreateFn, typename GetCountFn, typename GetByIndexFn, typename GetByKeyFn>
                void AddChildList(const char name[], CreateFn&&, GetCountFn&&, GetByIndexFn&&, GetByKeyFn&&);

            ClassAccessorsWithChildLists(size_t associatedType);
            ~ClassAccessorsWithChildLists();

        protected:
            std::vector<std::pair<uint64_t, ChildList>> _childLists;
        };

        template<typename Type>
            std::pair<void*, const ClassAccessors*> ClassAccessorsWithChildLists::TryCreateChild(
                Type& dst, uint64_t childListId) const
            {
                assert(TypeHashCode<Type> == _associatedType);
                return TryCreateChild(&dst, childListId);
            }
    }

    inline ClassAccessors::PropertyName::PropertyName(PropertyNameHash hash)
    {
        _hash = hash;
    }

    template<typename ResultType, typename ObjectType>
        std::optional<ResultType> ClassAccessors::Get(const ObjectType& srcObject, PropertyName id) const
    {
        assert(TypeHashCode<ObjectType> == _associatedType);
        ResultType res;
        if (Get(MakeOpaqueIteratorRange(res), ImpliedTyping::TypeOf<std::decay_t<ResultType>>(), &srcObject, id))
            return res;
        return {};
    }

    template<typename ValueType, typename ObjectType>
        bool ClassAccessors::Set(ObjectType& dstObject, PropertyName id, const ValueType& valueSrc) const
    {
        assert(TypeHashCode<ObjectType> == _associatedType);
        return Set(&dstObject, id, MakeOpaqueIteratorRange(valueSrc), ImpliedTyping::TypeOf<std::decay_t<ValueType>>());
    }

    template<typename ObjectType>
        std::optional<std::string> ClassAccessors::GetAsString(const ObjectType& srcObject, PropertyName id, bool strongTyping) const
    {
        assert(TypeHashCode<ObjectType> == _associatedType);
        return GetAsString((const void*)&srcObject, id, strongTyping);
    }

    template<typename ObjectType>
        bool ClassAccessors::SetFromString(ObjectType& srcObject, PropertyName id, StringSection<> src) const
    {
        assert(TypeHashCode<ObjectType> == _associatedType);
        return SetFromString((void*)&srcObject, id, src);
    }
}

using namespace Utility;
