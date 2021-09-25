// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ClassAccessors.h"
#include "../FunctionUtils.h"
#include "../StringUtils.h"

    //  This file contains functions that are useful when implementing Legacy_GetAccessors<>.
    //  However, to use the ClassAccessors interface, just include "ClassAccessors.h"

template<typename Type> const Utility::ClassAccessors& Legacy_GetAccessors();

namespace Utility
{
    namespace Internal
    {
///////////////////////////////////////////////////////////////////////////////////////////////////

        template<
            typename GetFn,
            typename std::enable_if<FunctionTraits<GetFn>::arity == 1>::type* =nullptr>
            ClassAccessors::Property::GetterFn WrapGetFunction(GetFn&& getFn)
        {
            return [capGetFn=std::move(getFn)](const void* object, IteratorRange<void*> destination, ImpliedTyping::TypeDesc requestedType) {
                using ResultOfGetFnType = typename FunctionTraits<GetFn>::ReturnType;
                using ExpectedObjectType = std::decay_t<std::tuple_element_t<0, typename FunctionTraits<GetFn>::ArgumentTuple>>;
                ResultOfGetFnType resultOfGetFn = capGetFn(*(const ExpectedObjectType*)object);
                return ImpliedTyping::Cast(
                    destination, requestedType,
                    MakeOpaqueIteratorRange(resultOfGetFn), ImpliedTyping::TypeOf<std::decay_t<ResultOfGetFnType>>());
            };
        }

        template<
            typename GetFn,
            std::invoke_result_t<GetFn, const void*, IteratorRange<void*>, ImpliedTyping::TypeDesc>* =nullptr>
            GetFn&& WrapGetFunction(GetFn&& getFn)
        {
            return std::move(getFn);
        }

        template<typename ObjectType, typename ResultOfGetFnType>
            ClassAccessors::Property::GetterFn WrapGetFunction(ResultOfGetFnType (ObjectType::*fn)() const)
        {
            return [fn](const void* object, IteratorRange<void*> destination, ImpliedTyping::TypeDesc requestedType) {
                ResultOfGetFnType resultOfGetFn = (((const ObjectType*)object)->*fn)();
                return ImpliedTyping::Cast(
                    destination, requestedType,
                    MakeOpaqueIteratorRange(resultOfGetFn), ImpliedTyping::TypeOf<std::decay_t<ResultOfGetFnType>>());
            };
        }

        template<typename ObjectType>
            ClassAccessors::Property::GetterFn WrapGetFunction(bool (ObjectType::*fn)(IteratorRange<void*>, ImpliedTyping::TypeDesc) const)
        {
            return [fn](const void* object, IteratorRange<void*> destination, ImpliedTyping::TypeDesc requestedType) {
                return (((const ObjectType*)object)->*fn)(destination, requestedType);
            };
        }

        inline ClassAccessors::Property::GetterFn WrapGetFunction(std::nullptr_t)
        {
            return {};
        }

        template<
            typename SetFn,
            typename std::enable_if<FunctionTraits<SetFn>::arity == 2>::type* =nullptr>
            ClassAccessors::Property::SetterFn WrapSetFunction(SetFn&& setFn)
        {
            return [capSetFn=std::move(setFn)](void* object, IteratorRange<const void*> src, ImpliedTyping::TypeDesc srcType) {
                using ExpectedObjectType = std::decay_t<std::tuple_element_t<0, typename FunctionTraits<SetFn>::ArgumentTuple>>;

                if constexpr (!std::is_same_v<std::tuple_element_t<1, typename FunctionTraits<SetFn>::ArgumentTuple>, StringSection<>>) {
                    // Setter does not take a string as input
                    using MidwayObjectType = std::decay_t<std::tuple_element_t<1, typename FunctionTraits<SetFn>::ArgumentTuple>>;
                    if (srcType._typeHint == ImpliedTyping::TypeHint::String && (srcType._type == ImpliedTyping::TypeCat::UInt8 || srcType._type == ImpliedTyping::TypeCat::Int8)) {
                        auto midwayObject = ImpliedTyping::ConvertFullMatch<MidwayObjectType>(MakeStringSection((const char*)src.begin(), (const char*)src.end()));
                        if (!midwayObject.has_value()) return false;
                        capSetFn(*(ExpectedObjectType*)object, midwayObject.value());
                    } else {
                        assert(srcType._typeHint != ImpliedTyping::TypeHint::String);       // no support for wide char type strings here
                        MidwayObjectType midwayObject;
                        bool castSuccess = ImpliedTyping::Cast(
                            MakeOpaqueIteratorRange(midwayObject), ImpliedTyping::TypeOf<MidwayObjectType>(),
                            src, srcType);
                        if (!castSuccess)
                            return false;
                        capSetFn(*(ExpectedObjectType*)object, midwayObject);
                    }
                } else {
                    // Setter does take a string as input
                    if (srcType._typeHint == ImpliedTyping::TypeHint::String && (srcType._type == ImpliedTyping::TypeCat::UInt8 || srcType._type == ImpliedTyping::TypeCat::Int8)) {
                        capSetFn(*(ExpectedObjectType*)object, MakeStringSection((const char*)src.begin(), (const char*)src.end()));
                    } else {
                        assert(srcType._typeHint != ImpliedTyping::TypeHint::String);       // no support for wide char type strings here
                        auto str = ImpliedTyping::AsString(src, srcType);
                        capSetFn(*(ExpectedObjectType*)object, str);
                    }
                }
                return true;
            };
        }

        template<
            typename SetFn,
            std::invoke_result_t<SetFn, void*, IteratorRange<const void*>, ImpliedTyping::TypeDesc>* =nullptr>
            SetFn&& WrapSetFunction(SetFn&& setFn)
        {
            return std::move(setFn);
        }

        template<typename ObjectType, typename NewValueType>
            ClassAccessors::Property::SetterFn WrapSetFunction(void (ObjectType::*fn)(NewValueType))
        {
            return [fn](void* object, IteratorRange<const void*> src, ImpliedTyping::TypeDesc srcType) {
                std::decay_t<NewValueType> midwayObject;
                bool castSuccess = ImpliedTyping::Cast(
                    MakeOpaqueIteratorRange(midwayObject), ImpliedTyping::TypeOf<std::decay_t<NewValueType>>(),
                    src, srcType);
                if (!castSuccess)
                    return false;
                (((ObjectType*)object)->*fn)(midwayObject);
                return true;
            };
        }

        template<typename ObjectType>
            ClassAccessors::Property::SetterFn WrapSetFunction(bool (ObjectType::*fn)(IteratorRange<const void*>, ImpliedTyping::TypeDesc))
        {
            return [fn](void* object, IteratorRange<const void*> src, ImpliedTyping::TypeDesc srcType) {
                return (((ObjectType*)object)->*fn)(src, srcType);
            };
        }

        inline ClassAccessors::Property::SetterFn WrapSetFunction(std::nullptr_t)
        {
            return {};
        }

        template<
            typename GetFn,
            typename std::enable_if<FunctionTraits<GetFn>::arity == 1>::type* =nullptr>
            ClassAccessors::Property::GetAsStringFn WrapGetAsStringFunction(GetFn&& getFn)
        {
            return [getFn](const void* object, bool strongTyping) {
                using ResultOfGetFnType = typename FunctionTraits<GetFn>::ReturnType;
                using ExpectedObjectType = std::decay_t<std::tuple_element_t<0, typename FunctionTraits<GetFn>::ArgumentTuple>>;
                ResultOfGetFnType resultOfGetFn = getFn(*(const ExpectedObjectType*)object);
                return ImpliedTyping::AsString(
                    MakeOpaqueIteratorRange(resultOfGetFn), ImpliedTyping::TypeOf<std::decay_t<ResultOfGetFnType>>(), strongTyping);
            };
        }

        template<
            typename GetFn,
            std::invoke_result_t<GetFn, const void*, IteratorRange<void*>, ImpliedTyping::TypeDesc>* =nullptr>
            ClassAccessors::Property::GetAsStringFn WrapGetAsStringFunction(GetFn&& getFn)
        {
            // todo -- if a "naturalType" is passed to the Add() function, we could potentially use that here
            // Throw(std::runtime_error("Could not extract string value from this property because the natural type is not known"))
            return nullptr;
        }

        template<typename ObjectType, typename ResultOfGetFnType>
            ClassAccessors::Property::GetAsStringFn WrapGetAsStringFunction(ResultOfGetFnType (ObjectType::*fn)() const)
        {
            return [fn](const void* object, bool strongTyping) {
                ResultOfGetFnType resultOfGetFn = (((const ObjectType*)object)->*fn)();
                return ImpliedTyping::AsString(
                    MakeOpaqueIteratorRange(resultOfGetFn), ImpliedTyping::TypeOf<std::decay_t<ResultOfGetFnType>>(), strongTyping);
            };
        }

        template<typename ObjectType>
            ClassAccessors::Property::GetAsStringFn WrapGetAsStringFunction(bool (ObjectType::*fn)(IteratorRange<void*>, ImpliedTyping::TypeDesc) const)
        {
            // todo -- if a "naturalType" is passed to the Add() function, we could potentially use that here
            // Throw(std::runtime_error("Could not extract string value from this property because the natural type is not known"))
            return nullptr;
        }

        inline ClassAccessors::Property::GetAsStringFn WrapGetAsStringFunction(std::nullptr_t)
        {
            return {};
        }
    }

    template<typename GetFn, typename SetFn>
        void ClassAccessors::Add(
            StringSection<> name,
            GetFn&& getter, SetFn&& setter,
            const std::optional<ImpliedTyping::TypeDesc>& naturalType)
        {
            auto id = PropertyName(name)._hash;
            auto& p = PropertyForId(id);
            p._name = name.AsString();
            p._naturalType = naturalType;
            p._getAsString = Internal::WrapGetAsStringFunction(getter);     // may invoke a copy of "getter" here
            p._getter = Internal::WrapGetFunction(std::move(getter));
            p._setter = Internal::WrapSetFunction(std::move(setter));
        }

    template<typename ObjectType, typename MemberType>
        void ClassAccessors::Add(
            StringSection<> name, 
            MemberType ObjectType::*ptrToMember)
        {
            auto id = PropertyName(name)._hash;
            auto& p = PropertyForId(id);
            p._name = name.AsString();
            p._naturalType = ImpliedTyping::TypeOf<std::decay_t<MemberType>>();
            p._getter = [ptrToMember](const void* object, IteratorRange<void*> destination, ImpliedTyping::TypeDesc requestedType) {
                const auto& member = ((const ObjectType*)object)->*ptrToMember;
                return ImpliedTyping::Cast(
                    destination, requestedType,
                    MakeOpaqueIteratorRange(member), ImpliedTyping::TypeOf<std::decay_t<MemberType>>());
            };
            p._setter = [ptrToMember](void* object, IteratorRange<const void*> src, ImpliedTyping::TypeDesc srcType) {
                auto& member = ((ObjectType*)object)->*ptrToMember;
                return ImpliedTyping::Cast(
                    MakeOpaqueIteratorRange(member), ImpliedTyping::TypeOf<std::decay_t<MemberType>>(),
                    src, srcType);
            };
            p._getAsString = [ptrToMember](const void* object, bool strongTyping) {
                auto& member = ((ObjectType*)object)->*ptrToMember;
                return ImpliedTyping::AsString(
                    MakeOpaqueIteratorRange(member), ImpliedTyping::TypeOf<std::decay_t<MemberType>>(), strongTyping);
            };
        }

    namespace Legacy
    {
        template<typename ChildType, typename CreateFn, typename GetCountFn, typename GetByIndexFn, typename GetByKeyFn>
            void ClassAccessorsWithChildLists::AddChildList(
                const char name[],
                CreateFn&& createFn, GetCountFn&& getCountFn, GetByIndexFn&& getByIndexFn, GetByKeyFn&& getByKeyFn)
            {
                auto id = PropertyName(name)._hash;
                auto c = MakeFunction(std::move(createFn));
                auto gc = MakeFunction(std::move(getCountFn));
                auto gi = MakeFunction(std::move(getByIndexFn));
                auto gk = MakeFunction(std::move(getByKeyFn));

                ChildList child;
                child._name = name;
                child._childProps = &Legacy_GetAccessors<ChildType>();
                child._createFn = std::move(c);
                child._getCount = std::move(gc);
                child._getByIndex = std::move(gi);
                child._getByKeyFn = std::move(gk);
                auto i = LowerBound(_childLists, id);
                _childLists.insert(i, std::make_pair(id, std::move(child)));
            }
    }
}

