// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/SelectConfiguration.h"
#include "Detail/API.h"
#include <string>
#include <assert.h>
#include <stdlib.h>
#include <memory>
#include <cstring>

namespace Utility
{
        ////////////   C O P Y  &  S E T   ////////////

    void     XlClearMemory          (void* p, size_t size);
    void     XlSetMemory            (void* p, int c, size_t size);

    void     XlCopyMemory           (void* dest, const void* src, size_t size);
    void     XlCopyMemoryAlign16    (void* dest, const void* src, size_t size);
    void     XlMoveMemory           (void* dest, const void* src, size_t size);

    int      XlCompareMemory        (const void* a, const void* b, size_t size);

    template <typename Type> 
        void XlZeroMemory(Type& destination)
            { XlClearMemory(&destination, sizeof(Type)); }

        ////////////   A L I G N E D   A L L O C A T E   ////////////

    #if CLIBRARIES_ACTIVE == CLIBRARIES_MSVC || defined(__MINGW32__)

        #define XlMemAlign          _aligned_malloc
        #define XlMemAlignFree      _aligned_free

    #else

        inline void* XlMemAlign(size_t size, size_t alignment)
        {
            void* result = nullptr;
            int errorNumber = posix_memalign(&result, alignment, size);
            assert(!errorNumber); (void)errorNumber;
            return result;
            // return memalign(size, alignment);
        }
        
        inline void XlMemAlignFree(void* data)
        {
            free(data);
        }

    #endif

    struct PODAlignedDeletor { void operator()(void* p); };

    template<typename Type>
        struct AlignedDeletor : public PODAlignedDeletor 
    {
    public:
        void operator()(Type* p)
        {
            if (p) { p->~Type(); }
            PODAlignedDeletor::operator()(p);
        }
    };

    template<typename Type>
        using AlignedUniquePtr = std::unique_ptr<Type, AlignedDeletor<Type>>;

        ////////////   H A S H I N G   ////////////

    static const uint64_t DefaultSeed64 = 0xE49B0E3F5C27F17Eull;
    XL_UTILITY_API uint64_t Hash64(const void* begin, const void* end, uint64_t seed = DefaultSeed64);

    static const uint64_t DefaultSeed32 = 0xB0F57EE3;
    XL_UTILITY_API uint32_t Hash32(const void* begin, const void* end, uint32_t seed = DefaultSeed32);
    XL_UTILITY_API uint32_t Hash32(const std::string& str, uint32_t seed = DefaultSeed32);

    XL_UTILITY_API uint64_t Hash64(const char str[], uint64_t seed = DefaultSeed64);
    XL_UTILITY_API uint64_t Hash64(const std::string& str, uint64_t seed = DefaultSeed64);

	template <typename CharType> class StringSection;
	XL_UTILITY_API uint64_t Hash64(StringSection<char> str, uint64_t seed = DefaultSeed64);

    template <typename Type> class IteratorRange;
	XL_UTILITY_API uint64_t Hash64(IteratorRange<const void*> data, uint64_t seed = DefaultSeed64);

	inline uint64_t HashCombine(uint64_t high, uint64_t low)
	{
		// This code based on "FarmHash"... which was in-turn
		// inspired by Murmur Hash. See:
		// https://code.google.com/p/farmhash/source/browse/trunk/src/farmhash.h
		// We want to combine two 64 bit hash values to create a new hash value
		// We could just return an xor of the two values. But this might not
		// be perfectly reliable (for example, the xor of 2 equals values is zero,
		// which could result in loss of information sometimes)
		const auto kMul = 0x9ddfea08eb382d69ull;
		auto a = (low ^ high) * kMul;
		a ^= (a >> 47);
		auto b = (high ^ a) * kMul;
		b ^= (b >> 47);
		b *= kMul;
		return b;
	}

    uint32_t IntegerHash32(uint32_t key);
    uint64_t IntegerHash64(uint64_t key);

        ////////////   C O M P I L E  -  T I M E  -  H A S H I N G   ////////////

    /// <summary>Generate a hash value at compile time</summary>
    /// Generate a simple hash value at compile time, from a set of 4-character 
    /// chars.
    ///
    /// The hash algorithm is very simple, and unique. It will produce very different
    /// hash values compared to the Hash64 function.
    /// There may be some value to attempting to make it match the "Hash64" function. 
    /// However, that would require a lot more work... The current implementation is
    /// more or less the simpliest possible implementation.
    ///
    /// Usage:
    /// <code>\code
    ///     static const uint64_t HashValue = ConstHash64<'Skel', 'eton'>::Value
    /// \endcode</code>
    ///
    /// Note that a better implementation would be possible with with a compiler that
    /// supports constexpr, but this was written before that was common. Still, it
    /// provides.
    ///
    /// Note that with modern clang, we need to use the "constexpr" keyword, anyway
    /// to prevent linker errors with missing copies of the "Value" static member. 
    template<unsigned S0, unsigned S1 = 0, unsigned S2 = 0, unsigned S3 = 0>
        struct ConstHash64
    {
    public:
        template<unsigned NewValue, uint64_t CumulativeHash>
            struct Calc
            {
                    // Here is the hash algorithm --
                    //  Since we're dealing 32 bit values, rather than chars, the algorithm
                    //  must be slightly difference. Here's something I've just invented.
                    //  It might be OK, but no real testing has gone into it.
                    //  Note that since we're building a 64 bit hash value, any strings with
                    //  8 or fewer characters can be stored in their entirety, anyway
                static constexpr const uint64_t Value = (NewValue == 0) ? CumulativeHash : (((CumulativeHash << 21ull) | (CumulativeHash >> 43ull)) ^ uint64_t(NewValue));
            };

        static constexpr const uint64_t Seed = 0xE49B0E3F5C27F17Eull;
        static constexpr const uint64_t Value = Calc<S3, Calc<S2, Calc<S1, Calc<S0, Seed>::Value>::Value>::Value>::Value;
    };

    uint64_t ConstHash64FromString(const char* begin, const char* end);

        ////////////   I N L I N E   I M P L E M E N T A T I O N S   ////////////

    inline void XlClearMemory(void* p, size_t size)                     { memset(p, 0, size); }
    inline void XlSetMemory(void* p, int c, size_t size)                { memset(p, c, size); }
    inline int  XlCompareMemory(const void* a, const void* b, size_t size) { return memcmp(a, b, size); }
    inline void XlCopyMemory(void* dest, const void* src, size_t size)  { memcpy(dest, src, size); }
    inline void XlMoveMemory(void* dest, const void* src, size_t size)  { memmove(dest, src, size); }

    inline void XlCopyMemoryAlign16(void* dest, const void* src, size_t size)
    {
            //  Alignment promise on input & output
            //  (often used for textures, etc)
            //  Use 128bit instructions to improve copy speed...
        assert((size_t(dest) & 0xf)==0x0);
        assert((size_t(src) & 0xf)==0x0);
        memcpy(dest, src, size);
    }

    template<typename Type> std::unique_ptr<uint8_t[]> DuplicateMemory(const Type& input)
    {
        auto result = std::make_unique<uint8_t[]>(sizeof(Type));
        XlCopyMemory(result.get(), &input, sizeof(Type));
        return result;
    }

        ////////////   D E F A U L T   I N I T I A L I Z A T I O N   A L L O C A T O R   ////////////

    // See https://stackoverflow.com/questions/21028299/is-this-behavior-of-vectorresizesize-type-n-under-c11-and-boost-container/21028912#21028912
    // Allocator adaptor that interposes construct() calls to
    // convert value initialization into default initialization.
    template <typename T, typename A=std::allocator<T>>
    class default_init_allocator : public A {
        typedef std::allocator_traits<A> a_t;
    public:
        template <typename U> struct rebind {
            using other =
            default_init_allocator<
            U, typename a_t::template rebind_alloc<U>
            >;
        };
        using A::A;

        template <typename U>
        void construct(U* ptr)
        noexcept(std::is_nothrow_default_constructible<U>::value) {
            ::new(static_cast<void*>(ptr)) U;
        }
        template <typename U, typename...Args>
        void construct(U* ptr, Args&&... args) {
            a_t::construct(static_cast<A&>(*this),
                           ptr, std::forward<Args>(args)...);
        }
    };

}

using namespace Utility;
