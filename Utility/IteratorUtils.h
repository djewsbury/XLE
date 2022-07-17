// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "PtrUtils.h"       // for AsPointer
#include <vector>
#include <algorithm>
#include <type_traits>      // (for is_constructible)

// support for missing "std::size" in earlier versions of visual studio STL
#if !((STL_ACTIVE == STL_MSVC) && (_MSC_VER > 1800))
    namespace std
    {
        template<typename ValueType, size_t N>
            /*constexpr*/ size_t size(ValueType (&c)[N]) { return N; }
    }
#endif

namespace RenderCore { class SharedPkt; }

namespace Utility
{
        //
        //  This file contains some utilities for common STL operations.
        //  Avoid adding too many utilities to this file, because it's included
        //  in many places (including through Assets.h, which propagates through
        //  a lot)
        //  
    template <typename First, typename Second>
        class CompareFirst
        {
        public:
            inline bool operator()(const std::pair<First, Second>& lhs, const std::pair<First, Second>& rhs) const   { return lhs.first < rhs.first; }
            inline bool operator()(const std::pair<First, Second>& lhs, const First& rhs) const                      { return lhs.first < rhs; }
            inline bool operator()(const First& lhs, const std::pair<First, Second>& rhs) const                      { return lhs < rhs.first; }
        };
    
    template <typename First, typename Second>
        class CompareSecond
        {
        public:
            inline bool operator()(const std::pair<First, Second>& lhs, const std::pair<First, Second>& rhs) const   { return lhs.second < rhs.second; }
            inline bool operator()(const std::pair<First, Second>& lhs, const Second& rhs) const                     { return lhs.second < rhs; }
            inline bool operator()(const Second& lhs, const std::pair<First, Second>& rhs) const                     { return lhs < rhs.second; }
        };

    struct CompareFirst2
    {
        template <typename LHS, typename RHS, typename std::enable_if_t<std::is_same_v<LHS, RHS>>* =nullptr>
            inline bool operator()(const LHS& lhs, const RHS& rhs) const    { return lhs.first < rhs.first; }
        template <typename LHS, typename RHS, typename std::enable_if_t<!std::is_same_v<LHS, RHS>>* =nullptr>
            inline bool operator()(const LHS& lhs, const RHS& rhs) const    { return lhs.first < rhs; }
    };

    struct CompareSecond2
    {
        template <typename LHS, typename RHS, typename std::enable_if_t<std::is_same_v<LHS, RHS>>* =nullptr>
            inline bool operator()(const LHS& lhs, const RHS& rhs) const    { return lhs.second < rhs.second; }
        template <typename LHS, typename RHS, typename std::enable_if_t<!std::is_same_v<LHS, RHS>>* =nullptr>
            inline bool operator()(const LHS& lhs, const RHS& rhs) const    { return lhs.second < rhs; }
    };

    template <typename First, typename Second, typename Allocator>
        typename std::vector<std::pair<First, Second>, Allocator>::iterator LowerBound(
            std::vector<std::pair<First, Second>, Allocator>&v, First compareToFirst)
        {
            return std::lower_bound(v.begin(), v.end(), compareToFirst, CompareFirst<First, Second>());
        }

    template <typename First, typename Second, typename Allocator>
        typename std::vector<std::pair<First, Second>, Allocator>::const_iterator LowerBound(
            const std::vector<std::pair<First, Second>, Allocator>&v, First compareToFirst)
        {
            return std::lower_bound(v.cbegin(), v.cend(), compareToFirst, CompareFirst<First, Second>());
        }

    template <typename First, typename Second, typename Allocator>
        static typename std::vector<std::pair<First, Second>, Allocator>::iterator UpperBound(
            std::vector<std::pair<First, Second>, Allocator>&v, First compareToFirst)
        {
            return std::upper_bound(v.begin(), v.end(), compareToFirst, CompareFirst<First, Second>());
        }

	template<typename FirstType, typename SecondType>
		std::pair<typename std::vector<std::pair<FirstType, SecondType> >::iterator, typename std::vector<std::pair<FirstType, SecondType> >::iterator>
			EqualRange(std::vector<std::pair<FirstType, SecondType> >& vector, FirstType searchKey)
		{
			return std::equal_range(vector.begin(), vector.end(), searchKey, CompareFirst<FirstType, SecondType>());
		}

    template<typename FirstType, typename SecondType>
        std::pair<typename std::vector<std::pair<FirstType, SecondType> >::const_iterator, typename std::vector<std::pair<FirstType, SecondType> >::const_iterator>
            EqualRange(const std::vector<std::pair<FirstType, SecondType> >& vector, FirstType searchKey)
        {
            return std::equal_range(vector.cbegin(), vector.cend(), searchKey, CompareFirst<FirstType, SecondType>());
        }

    template <typename Vector, typename Pred>
        typename Vector::iterator FindIf(Vector& v, Pred&& predicate)
        {
            return std::find_if(v.begin(), v.end(), std::forward<Pred>(predicate));
        }

    template <typename Vector, typename Pred>
        typename Vector::const_iterator FindIf(const Vector& v, Pred&& predicate)
        {
            return std::find_if(v.cbegin(), v.cend(), std::forward<Pred>(predicate));
        }

    template<typename SearchI, typename CompareI>
        SearchI FindFirstNotOf(
            SearchI searchStart, SearchI searchEnd,
            CompareI compareBegin, CompareI compareEnd)
        {
            auto i = searchStart;
            while (i != searchEnd && std::find(compareBegin, compareEnd, *i) != compareEnd)
                ++i;
            return i;
        }

    template<typename SearchI, typename CompareI>
        SearchI FindLastOf(
            SearchI searchStart, SearchI searchEnd,
            CompareI compareBegin, CompareI compareEnd)
        {
            if (searchStart == searchEnd) return searchEnd;
            auto i = searchEnd-1;
            while (i >= searchStart) {
                if (std::find(compareBegin, compareEnd, *i) != compareEnd)
                    return i;
                --i;
            }
            return searchEnd;
        }

    template<typename SearchI, typename CompareI>
        SearchI FindLastOf(
            SearchI searchStart, SearchI searchEnd,
            CompareI compare)
        {
            if (searchStart == searchEnd) return searchEnd;
            auto i = searchEnd-1;
            while (i >= searchStart) {
                if (*i == compare)
                    return i;
                --i;
            }
            return searchEnd;
        }

	namespace Internal
	{
		template<typename Iterator> inline size_t IteratorDifference(Iterator first, Iterator second)		{ return std::distance(first, second); } 
		template<> inline size_t IteratorDifference(void* first, void* second)								{ return (size_t)PtrDiff(second, first); }
		template<> inline size_t IteratorDifference(const void* first, const void* second)					{ return (size_t)PtrDiff(second, first); }

        template<
            typename DstType, typename SrcType,
            typename std::enable_if<
                std::is_constructible_v<DstType, SrcType>
            >::type* =nullptr
            > DstType ImplicitIteratorCast(SrcType input) { return input; }

        template<
            typename DstType, typename SrcType,
            typename std::enable_if<
                !std::is_constructible_v<DstType, SrcType>
                && std::is_constructible_v<DstType, decltype(AsPointer(std::declval<SrcType>()))>
            >::type* =nullptr
            > DstType ImplicitIteratorCast(SrcType input) { return AsPointer(input); }

        template<typename DstType, typename SrcType>
			static auto HasImplicitIteratorCast_Helper(int) -> decltype(ImplicitIteratorCast<DstType>(std::declval<SrcType>()), std::true_type{});

		template<typename...>
			static auto HasImplicitIteratorCast_Helper(...) -> std::false_type;

        template<typename DstType, typename SrcType>
            decltype(ImplicitIteratorCast<DstType>(std::declval<SrcType>())) StaticIteratorCast(SrcType input) { return ImplicitIteratorCast<DstType>(input); }

        template<
            typename DstType, typename SrcType,
            typename std::enable_if<
                !decltype(HasImplicitIteratorCast_Helper<DstType, SrcType>(0))::value
            >::type* =nullptr
            > DstType StaticIteratorCast(SrcType input) { return static_cast<DstType>(AsPointer(input)); }
	}

    template<typename Iterator>
        class IteratorRange : public std::pair<Iterator, Iterator>
        {
        public:
            Iterator begin() const      { return this->first; }
            Iterator end() const        { return this->second; }
            Iterator cbegin() const     { return this->first; }
            Iterator cend() const       { return this->second; }
            bool empty() const          { return this->first == this->second; }
			size_t size() const			{ return Internal::IteratorDifference(this->first, this->second); }

            using iterator = Iterator;

            // operator[] is only available on iterator range for types other than void*/const void*
            auto operator[](size_t index) const -> decltype(*std::declval<const Iterator>()) { return this->first[index]; }

            template<typename I=Iterator>
                auto data() const -> decltype(AsPointer(std::declval<const I>())) { return AsPointer(this->first); }

            template<typename OtherIteratorType>
                IteratorRange<OtherIteratorType> Cast() const { return IteratorRange<OtherIteratorType>(Internal::StaticIteratorCast<OtherIteratorType>(this->first), Internal::StaticIteratorCast<OtherIteratorType>(this->second)); }

            IteratorRange() : std::pair<Iterator, Iterator>((Iterator)nullptr, (Iterator)nullptr) {}

            template<typename OtherIteratorType, decltype(Internal::ImplicitIteratorCast<Iterator>(std::declval<OtherIteratorType>()))* = nullptr>
                IteratorRange(OtherIteratorType f, OtherIteratorType s) : std::pair<Iterator, Iterator>(Internal::ImplicitIteratorCast<Iterator>(f), Internal::ImplicitIteratorCast<Iterator>(s)) {}

            IteratorRange(std::initializer_list<std::decay_t<decltype(*std::declval<Iterator>())>> init) : std::pair<Iterator, Iterator>(std::begin(init), std::end(init)) {}

            // The following constructor & operator pair now handle conversion from different types of IteratorRanges
            // (so long as there's an automatic static_cast conversion down to the new iterator type)
            // Furthermore, they will handle anything with const begin() and end() methods
            // It's quite flexible, so be conscious of automatic conversions
            // Also the Microsoft intellisense code doesn't seem to always be able to identify the all of the cases were conversion is
            // possible! I've no idea why

            template<   typename OtherRange,
                        decltype(Internal::ImplicitIteratorCast<Iterator>(std::begin(std::declval<const OtherRange&>())))* = nullptr>
                IteratorRange(const OtherRange& copyFrom)
                    : std::pair<Iterator, Iterator>(Internal::ImplicitIteratorCast<Iterator>(std::begin(copyFrom)), Internal::ImplicitIteratorCast<Iterator>(std::end(copyFrom))) {}

            template<   typename OtherRange,
                        decltype(Internal::ImplicitIteratorCast<Iterator>(std::begin(std::declval<const OtherRange&>())))* = nullptr>
                IteratorRange& operator=(const OtherRange& copyFrom)
                {
                    std::pair<Iterator, Iterator>::operator=(std::make_pair(Internal::ImplicitIteratorCast<Iterator>(std::begin(copyFrom)), Internal::ImplicitIteratorCast<Iterator>(std::end(copyFrom))));
                    return *this;
                }
        };

    template<>
        class IteratorRange<void*> : public std::pair<void*, void*>
        {
        public:
            void* begin() const         { return this->first; }
            void* end() const           { return this->second; }
            void* cbegin() const        { return this->first; }
            void* cend() const          { return this->second; }
            bool empty() const          { return this->first == this->second; }
			size_t size() const			{ return Internal::IteratorDifference(this->first, this->second); }

            auto data() const -> void* { return this->first; }

            template<typename OtherIteratorType>
                IteratorRange<OtherIteratorType> Cast() const { return IteratorRange<OtherIteratorType>(Internal::StaticIteratorCast<OtherIteratorType>(this->first), Internal::StaticIteratorCast<OtherIteratorType>(this->second)); }

            IteratorRange() : std::pair<void*, void*>(nullptr, nullptr) {}

            template<typename OtherIteratorType, decltype(Internal::ImplicitIteratorCast<void*>(std::declval<OtherIteratorType>()))* = nullptr>
                IteratorRange(OtherIteratorType f, OtherIteratorType s) : std::pair<void*, void*>(Internal::ImplicitIteratorCast<void*>(f), Internal::ImplicitIteratorCast<void*>(s)) {}

            template<   typename OtherRange,
                        decltype(Internal::ImplicitIteratorCast<void*>(std::begin(std::declval<const OtherRange&>())))* = nullptr>
                IteratorRange(const OtherRange& copyFrom)
                    : std::pair<void*, void*>(Internal::ImplicitIteratorCast<void*>(std::begin(copyFrom)), Internal::ImplicitIteratorCast<void*>(std::end(copyFrom))) {}

            template<   typename OtherRange,
                        decltype(Internal::ImplicitIteratorCast<void*>(std::begin(std::declval<const OtherRange&>())))* = nullptr>
                IteratorRange& operator=(const OtherRange& copyFrom)
                {
                    std::pair<void*, void*>::operator=(std::make_pair(Internal::ImplicitIteratorCast<void*>(std::begin(copyFrom)), Internal::ImplicitIteratorCast<void*>(std::end(copyFrom))));
                    return *this;
                }
        };

    template<>
        class IteratorRange<const void*> : public std::pair<const void*, const void*>
        {
        public:
            const void* begin() const       { return this->first; }
            const void* end() const         { return this->second; }
            const void* cbegin() const      { return this->first; }
            const void* cend() const        { return this->second; }
            bool empty() const              { return this->first == this->second; }
			size_t size() const             { return Internal::IteratorDifference(this->first, this->second); }

            auto data() const -> const void* { return this->first; }

            template<typename OtherIteratorType>
                IteratorRange<OtherIteratorType> Cast() const { return IteratorRange<OtherIteratorType>(Internal::StaticIteratorCast<OtherIteratorType>(this->first), Internal::StaticIteratorCast<OtherIteratorType>(this->second)); }

            IteratorRange() : std::pair<const void*, const void*>(nullptr, nullptr) {}

            template<typename OtherIteratorType, decltype(Internal::ImplicitIteratorCast<const void*>(std::declval<OtherIteratorType>()))* = nullptr>
                IteratorRange(OtherIteratorType f, OtherIteratorType s) : std::pair<const void*, const void*>(Internal::ImplicitIteratorCast<const void*>(f), Internal::ImplicitIteratorCast<const void*>(s)) {}

            template<   typename OtherRange,
                        decltype(Internal::ImplicitIteratorCast<const void*>(std::begin(std::declval<const OtherRange&>())))* = nullptr>
                IteratorRange(const OtherRange& copyFrom)
                    : std::pair<const void*, const void*>(Internal::ImplicitIteratorCast<const void*>(std::begin(copyFrom)), Internal::ImplicitIteratorCast<const void*>(std::end(copyFrom))) {}

            template<   typename OtherRange,
                        decltype(Internal::ImplicitIteratorCast<const void*>(std::begin(std::declval<const OtherRange&>())))* = nullptr>
                IteratorRange& operator=(const OtherRange& copyFrom)
                {
                    std::pair<const void*, const void*>::operator=(std::make_pair(Internal::ImplicitIteratorCast<const void*>(std::begin(copyFrom)), Internal::ImplicitIteratorCast<const void*>(std::end(copyFrom))));
                    return *this;
                }
        };

    template<typename Container>
        IteratorRange<decltype(std::begin(std::declval<Container&>()))> MakeIteratorRange(Container& c)
        {
            return IteratorRange<decltype(std::begin(std::declval<Container&>()))>(std::begin(c), std::end(c));
        }
   
    template<typename Iterator>
        IteratorRange<Iterator> MakeIteratorRange(Iterator begin, Iterator end)
        {
            return IteratorRange<Iterator>(begin, end);
        }

    template<typename ArrayElement, int Count>
        IteratorRange<ArrayElement*> MakeIteratorRange(ArrayElement (&c)[Count])
        {
            return IteratorRange<ArrayElement*>(&c[0], &c[Count]);
        }

	template<typename Type>
		IteratorRange<void*> MakeOpaqueIteratorRange(Type& object)
		{
            static_assert(!std::is_same_v<std::decay_t<Type>, RenderCore::SharedPkt>);
			return MakeIteratorRange(&object, PtrAdd(&object, sizeof(Type)));
		}
		
	template<typename Type>
		IteratorRange<const void*> MakeOpaqueIteratorRange(const Type& object)
		{
            static_assert(!std::is_same_v<std::decay_t<Type>, RenderCore::SharedPkt>);
			return MakeIteratorRange(&object, PtrAdd(&object, sizeof(Type)));
		}

#pragma warning(push)
#pragma warning(disable:4789)       // buffer '' of size 12 bytes will be overrun; 4 bytes will be written starting at offset 12

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"     // array index 3 is past the end of the array (which contains 2 elements)

    /// We can initialize from anything that looks like a collection of unsigned values
    /// This is a simple way to get casting from XLEMath::UInt2 (etc) types without
    /// having to include XLEMath headers from here.
    /// Plus, it will also work with any other types that expose a stl collection type
    /// interface.
    template<typename Type, unsigned Count> 
        class VectorPattern
    {
    public:
        Type _values[Count];

        VectorPattern(Type x=0, Type y=0, Type z=0, Type w=0)
        { 
            if (constant_expression<(Count > 0u)>::result()) _values[0] = x;
            if (constant_expression<(Count > 1u)>::result()) _values[1] = y;
            if (constant_expression<(Count > 2u)>::result()) _values[2] = z;
            if (constant_expression<(Count > 3u)>::result()) _values[3] = w;
        }

        template<int InitCount>
            VectorPattern(Type (&values)[InitCount])
        {
            for (unsigned c=0; c<Count; ++c) {
                if (c < InitCount) _values[c] = values[c];
                else _values[c] = (Type)0;
            }
        }

		Type operator[](unsigned index) const { return _values[index]; }
        Type& operator[](unsigned index) { return _values[index]; }

        template<
			typename Source,
			std::enable_if<std::is_assignable<Type, Source>::value>* = nullptr>
            VectorPattern(const Source& src)
            {
                auto size = std::size(src);
                unsigned c=0;
                for (; c<std::min(unsigned(size), Count); ++c) _values[c] = src[c];
                for (; c<Count; ++c) _values[c] = 0;
            }
    };
#pragma GCC diagnostic pop
#pragma warning(pop)

	template <typename Iterator>
        Iterator LowerBound2(IteratorRange<Iterator> v, decltype(std::declval<Iterator>()->first) compareToFirst)
        {
            return std::lower_bound(v.begin(), v.end(), compareToFirst, 
				CompareFirst<decltype(std::declval<Iterator>()->first), decltype(std::declval<Iterator>()->second)>());
        }
}

namespace std
{
    template<typename Iterator> Iterator begin(const IteratorRange<Iterator>& range) { return range.begin(); }
    template<typename Iterator> Iterator end(const IteratorRange<Iterator>& range) { return range.end(); }
}

using namespace Utility;
