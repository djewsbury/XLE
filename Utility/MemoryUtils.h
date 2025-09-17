// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/SelectConfiguration.h"
#include "../Core/Prefix.h"
#include "../Core/Aliases.h"
#include "Detail/API.h"
#include "ctti/type_id.hpp"
#include <string>
#include <assert.h>
#include <stdlib.h>
#include <memory>
#include <cstring>
#include <string_view>

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

        ////////////   T Y P E   C O D E S   ////////////

    template<typename Type>
        constexpr ctti::detail::hash_t TypeHashCode = ctti::unnamed_type_id<Type>().hash();     // unnamed_type_id<> has std::decay within it

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

	constexpr uint64_t HashCombine(uint64_t high, uint64_t low)
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

    /// <summary>Returns some statistics related to use of Hash64 & Hash32</summary>
    /// In first & second in the result:
    ///   1. the number of runtime hashes calculated since startup
    ///   2. the sum total bytes in runtime hashes since startup
    /// These values may not be calculated in all builds. When not available, the result
    /// will be {0,0}
    XL_UTILITY_API std::pair<size_t, size_t> GetRuntimeHashStats();

        ////////////   C O M P I L E  -  T I M E  -  H A S H I N G   ////////////

    ///
    ///     @{
    ///
    ///      Generate a string hash value at compile time through one of the following entry points:
    ///
    ///      when using namespace Utility::Literals:
    ///          operator"" _h           (implicitly 64 bit hash)
    ///          operator"" _h32
    ///          operator"" _h64
    ///
    ///      free functions:
    ///          Utility::ConstHash64(const char*, size_t len, uint64_t seed=DefaultSeed64)
    ///          Utility::ConstHash64(std::string_view, uint64_t seed=DefaultSeed64)
    ///
    ///          Utility::ConstHash32(const char*, size_t len, uint64_t seed=DefaultSeed32)
    ///          Utility::ConstHash32(std::string_view, uint64_t seed=DefaultSeed32)
    ///
    ///      Note that except for the note below, on C++17 these will follow the constexpr behaviour, which 
    ///      means they are only evaluated at compile time in certain situations (see the language documentation)
    ///      On C++20, consteval will be used.
    ///
    ///      However on Clang/GCC there is an optimization which will mean that the literal suffix will always
    ///      be evaluated at compile time, even pre C++20. This may make the literal suffix version preferable.
    ///
    ///      Use of the literal suffix is expected, however the free functions are provided
    ///      where operator"" is inconvenient, or a specific seed value is required.
    ///

    namespace Internal
    {

        constexpr uint64_t ConstHash64_1(const char * key, size_t len, const uint64_t seed = DefaultSeed64)
        {
            // The code in this function derived from Foreign/Hash/MurmurHash2.cpp
            // (and made to work with constexpr in C++17)
            // See licence in that file:
            // MurmurHash2 was written by Austin Appleby, and is placed in the public
            // domain. The author hereby disclaims copyright to this source code.

            const uint64_t m = 0xc6a4a7935bd1e995ull;
            const int r = 47;

            uint64_t h = seed ^ (len * m);

            while (len >= 8) {
                uint64_t k = 
                    uint64_t((uint8_t)*(key+0))
                    | uint64_t((uint8_t)*(key+1)) << 8ull
                    | uint64_t((uint8_t)*(key+2)) << 16ull
                    | uint64_t((uint8_t)*(key+3)) << 24ull
                    | uint64_t((uint8_t)*(key+4)) << 32ull
                    | uint64_t((uint8_t)*(key+5)) << 40ull
                    | uint64_t((uint8_t)*(key+6)) << 48ull
                    | uint64_t((uint8_t)*(key+7)) << 56ull
                    ;

                key += 8;
                len -= 8;

                k *= m; 
                k ^= k >> r; 
                k *= m; 

                h ^= k;
                h *= m; 
            }

            switch(len & 7)
            {
            case 7: h ^= uint64_t((uint8_t)key[6]) << 48;
            case 6: h ^= uint64_t((uint8_t)key[5]) << 40;
            case 5: h ^= uint64_t((uint8_t)key[4]) << 32;
            case 4: h ^= uint64_t((uint8_t)key[3]) << 24;
            case 3: h ^= uint64_t((uint8_t)key[2]) << 16;
            case 2: h ^= uint64_t((uint8_t)key[1]) << 8;
            case 1: h ^= uint64_t((uint8_t)key[0]);
                h *= m;
            };

            h ^= h >> r;
            h *= m;
            h ^= h >> r;

            return h;
        }

        constexpr uint32_t rotl32_constexpr_helper ( uint32_t x, int8_t r ) { return (x << r) | (x >> (32 - r)); }
        constexpr uint32_t fmix_constexpr_helper ( uint32_t h ) { h ^= h >> 16; h *= 0x85ebca6b; h ^= h >> 13; h *= 0xc2b2ae35; h ^= h >> 16; return h; }

        constexpr uint32_t ConstHash32_1(const char * key, const size_t len, const uint32_t seed = DefaultSeed32)
        {
            // The code in this function derived from Foreign/Hash/MurmurHash3.cpp
            // (and made to work with constexpr in C++17)
            // See licence in that file:
            // MurmurHash3 was written by Austin Appleby, and is placed in the public
            // domain. The author hereby disclaims copyright to this source code.

            const auto nblocks = len / 4;

            uint32_t h1 = seed;

            uint32_t c1 = 0xcc9e2d51;
            uint32_t c2 = 0x1b873593;

            //----------
            // body

            for(unsigned i = 0; i<nblocks; i++)
            {
                uint32_t k1 = 
                    uint32_t((uint8_t)key[i*4+0])
                    | uint32_t((uint8_t)key[i*4+1]) << 8u
                    | uint32_t((uint8_t)key[i*4+2]) << 16u
                    | uint32_t((uint8_t)key[i*4+3]) << 24u
                    ;

                k1 *= c1;
                k1 = Internal::rotl32_constexpr_helper(k1,15);
                k1 *= c2;

                h1 ^= k1;
                h1 = Internal::rotl32_constexpr_helper(h1,13); 
                h1 = h1*5+0xe6546b64;
            }

            //----------
            // tail

            const auto * tail = key + nblocks*4;

            uint32_t k1 = 0;

            switch(len & 3)
            {
            case 3: k1 ^= uint32_t(uint8_t(tail[2])) << 16u;
            case 2: k1 ^= uint32_t(uint8_t(tail[1])) << 8u;
            case 1: k1 ^= uint32_t(uint8_t(tail[0]));
                    k1 *= c1; k1 = Internal::rotl32_constexpr_helper(k1,15); k1 *= c2; h1 ^= k1;
            };

            //----------
            // finalization

            h1 ^= len;

            h1 = Internal::fmix_constexpr_helper(h1);

            return h1;
        }

        template<uint64_t Seed, char... Chars>
            constexpr uint64_t ConstHash64_2_Tail()
        {
            // The code in this function derived from Foreign/Hash/MurmurHash2.cpp
            // (and made to work with constexpr in C++17)
            // See licence in that file:
            // MurmurHash2 was written by Austin Appleby, and is placed in the public
            // domain. The author hereby disclaims copyright to this source code.

            constexpr uint64_t m = 0xc6a4a7935bd1e995ull;
            constexpr int r = 47;
            constexpr auto len = sizeof...(Chars);
            std::tuple<decltype(Chars)...> tup{Chars...};
            auto h = Seed;
            if constexpr (len >= 7) h ^= uint64_t((uint8_t)std::get<6>(tup)) << 48ull;
            if constexpr (len >= 6) h ^= uint64_t((uint8_t)std::get<5>(tup)) << 40ull;
            if constexpr (len >= 5) h ^= uint64_t((uint8_t)std::get<4>(tup)) << 32ull;
            if constexpr (len >= 4) h ^= uint64_t((uint8_t)std::get<3>(tup)) << 24ull;
            if constexpr (len >= 3) h ^= uint64_t((uint8_t)std::get<2>(tup)) << 16ull;
            if constexpr (len >= 2) h ^= uint64_t((uint8_t)std::get<1>(tup)) <<  8ull;
            if constexpr (len >= 1) { h ^= uint64_t((uint8_t)std::get<0>(tup)); h *= m; }

            h ^= h >> r;
            h *= m;
            h ^= h >> r;
            return h;
        }

        constexpr uint64_t ConstHash64_2_Block_Helper(uint64_t k, uint64_t h)
        {
            // The code in this function derived from Foreign/Hash/MurmurHash2.cpp
            // (and made to work with constexpr in C++17)
            // See licence in that file:
            // MurmurHash2 was written by Austin Appleby, and is placed in the public
            // domain. The author hereby disclaims copyright to this source code.

            constexpr uint64_t m = 0xc6a4a7935bd1e995ull;
            constexpr int r = 47;
            k *= m;
            k ^= k >> r;
            k *= m;

            h ^= k;
            h *= m; 
            return h;
        }

        template<uint64_t Seed, char A, char B, char C, char D, char E, char F, char G, char H, char... Remainder>
            constexpr uint64_t ConstHash64_2_Block()
        {
            constexpr uint64_t k = 
                uint64_t((uint8_t)A)
                | uint64_t((uint8_t)B) << 8ull
                | uint64_t((uint8_t)C) << 16ull
                | uint64_t((uint8_t)D) << 24ull
                | uint64_t((uint8_t)E) << 32ull
                | uint64_t((uint8_t)F) << 40ull
                | uint64_t((uint8_t)G) << 48ull
                | uint64_t((uint8_t)H) << 56ull
                ;

            constexpr auto NewSeed = ConstHash64_2_Block_Helper(k, Seed);
            if constexpr (sizeof...(Remainder) >= 8) {
                return ConstHash64_2_Block<NewSeed, Remainder...>();
            } else {
                return ConstHash64_2_Tail<NewSeed, Remainder...>();
            }
        }

        template<uint32_t Seed, uint32_t OriginalLen, char... Chars>
            constexpr uint32_t ConstHash32_2_Tail()
        {
            // The code in this function derived from Foreign/Hash/MurmurHash3.cpp
            // (and made to work with constexpr in C++17)
            // See licence in that file:
            // MurmurHash3 was written by Austin Appleby, and is placed in the public
            // domain. The author hereby disclaims copyright to this source code.

            constexpr uint32_t c1 = 0xcc9e2d51;
            constexpr uint32_t c2 = 0x1b873593;

            constexpr auto len = sizeof...(Chars);
            std::tuple<decltype(Chars)...> tup{Chars...};
            uint32_t k1 = 0;
            uint32_t h1 = Seed;
            if constexpr (len >= 3) k1 ^= uint32_t((uint8_t)std::get<2>(tup)) << 16u;
            if constexpr (len >= 2) k1 ^= uint32_t((uint8_t)std::get<1>(tup)) <<  8u;
            if constexpr (len >= 1) { k1 ^= uint32_t((uint8_t)std::get<0>(tup)); k1 *= c1; k1 = rotl32_constexpr_helper(k1,15); k1 *= c2; h1 ^= k1; }

            h1 ^= OriginalLen;
            h1 = fmix_constexpr_helper(h1);
            return h1;
        }

        constexpr uint64_t ConstHash32_2_Block_Helper(uint32_t k1, uint32_t h1)
        {
            // The code in this function derived from Foreign/Hash/MurmurHash3.cpp
            // (and made to work with constexpr in C++17)
            // See licence in that file:
            // MurmurHash3 was written by Austin Appleby, and is placed in the public
            // domain. The author hereby disclaims copyright to this source code.

            constexpr uint32_t c1 = 0xcc9e2d51;
            constexpr uint32_t c2 = 0x1b873593;

            k1 *= c1;
            k1 = rotl32_constexpr_helper(k1,15);
            k1 *= c2;

            h1 ^= k1;
            h1 = rotl32_constexpr_helper(h1,13);
            h1 = h1*5+0xe6546b64;
            return h1;
        }

        template<uint32_t Seed, uint32_t OriginalLen, char A, char B, char C, char D, char... Remainder>
            constexpr uint32_t ConstHash32_2_Block()
        {
            constexpr uint32_t k1 = 
                uint32_t((uint8_t)A)
                | uint32_t((uint8_t)B) << 8u
                | uint32_t((uint8_t)C) << 16u
                | uint32_t((uint8_t)D) << 24u
                ;

            constexpr auto NewSeed = ConstHash32_2_Block_Helper(k1, Seed);
            if constexpr (sizeof...(Remainder) >= 4) {
                return ConstHash32_2_Block<NewSeed, OriginalLen, Remainder...>();
            } else {
                return ConstHash32_2_Tail<NewSeed, OriginalLen, Remainder...>();
            }
        }

        template<uint64_t Seed, char... C>
            constexpr uint64_t ConstHash64_2()
        {
            constexpr auto len = sizeof...(C);
            constexpr uint64_t m = 0xc6a4a7935bd1e995ull;
            constexpr uint64_t h = Seed ^ (len * m);
            if constexpr (sizeof...(C) >= 8) {
                return Internal::ConstHash64_2_Block<h, C...>();
            } else {
                return Internal::ConstHash64_2_Tail<h, C...>();
            }
        }

        template<uint32_t Seed, char... C>
            constexpr uint32_t ConstHash32_2()
        {
            constexpr auto len = (uint32_t)sizeof...(C);
            if constexpr (sizeof...(C) >= 4) {
                return Internal::ConstHash32_2_Block<Seed, len, C...>();
            } else {
                return Internal::ConstHash32_2_Tail<Seed, len, C...>();
            }
        }
    }

    XLE_CONSTEVAL_OR_CONSTEXPR uint64_t ConstHash64(const char* str, const size_t len, uint64_t seed=DefaultSeed64) never_throws { return Internal::ConstHash64_1(str, len, seed); }
    XLE_CONSTEVAL_OR_CONSTEXPR uint32_t ConstHash32(const char* str, const size_t len, uint32_t seed=DefaultSeed32) never_throws { return Internal::ConstHash32_1(str, len, seed); }

    XLE_CONSTEVAL_OR_CONSTEXPR uint64_t ConstHash64(std::string_view v, uint64_t seed=DefaultSeed64) { return Internal::ConstHash64_1(v.data(), v.size(), seed); }
    XLE_CONSTEVAL_OR_CONSTEXPR uint32_t ConstHash32(std::string_view v, uint32_t seed=DefaultSeed32) { return Internal::ConstHash32_1(v.data(), v.size(), seed); }
    
    template<int N> XLE_CONSTEVAL_OR_CONSTEXPR uint64_t ConstHash64(char (&key)[N], uint64_t seed=DefaultSeed64) { static_assert(N != 0); return Internal::ConstHash64_1(key, N-1, seed); }
    template<int N> XLE_CONSTEVAL_OR_CONSTEXPR uint32_t ConstHash32(char (&key)[N], uint32_t seed=DefaultSeed32) { static_assert(N != 0); return Internal::ConstHash32_1(key, N-1, seed); }

    namespace Literals
    {
        #if (COMPILER_ACTIVE == COMPILER_TYPE_GCC || COMPILER_ACTIVE == COMPILER_TYPE_CLANG) && (__cplusplus < 202002L)

            // These variants uss an GNU/clang extension "string literal operator templates"
            // It's not supported in MSVC
            // but it has an advantage in that it works more like consteval than constexpr -- it can only be evaluated
            // at compile time, regardless of the context of how it's used
            // In C++20, it shouldn't be needed because "consteval" is a simplier alternative
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wgnu-string-literal-operator-template"
                template <typename T, T... chars>
                    XLE_CONSTEVAL_OR_CONSTEXPR uint64_t operator""_h() never_throws
                { 
                    return Internal::ConstHash64_2<DefaultSeed64, chars...>();
                }

                template <typename T, T... chars>
                    XLE_CONSTEVAL_OR_CONSTEXPR uint64_t operator""_h64() never_throws
                {
                    return Internal::ConstHash64_2<DefaultSeed64, chars...>();
                }

                template <typename T, T... chars>
                    XLE_CONSTEVAL_OR_CONSTEXPR uint32_t operator""_h32() never_throws
                {
                    return Internal::ConstHash32_2<DefaultSeed32, chars...>();
                }
            #pragma GCC diagnostic pop

        #else

            XLE_CONSTEVAL_OR_CONSTEXPR uint64_t operator""_h(const char* str, const size_t len) never_throws { return Internal::ConstHash64_1(str, len); }
            XLE_CONSTEVAL_OR_CONSTEXPR uint32_t operator""_h32(const char* str, const size_t len) never_throws { return Internal::ConstHash32_1(str, len); }
            XLE_CONSTEVAL_OR_CONSTEXPR uint64_t operator""_h64(const char* str, const size_t len) never_throws { return Internal::ConstHash64_1(str, len); }

        #endif

        XLE_CONSTEVAL_OR_CONSTEXPR uint64_t operator""_h_compatible(const char* str, const size_t len) never_throws { return Internal::ConstHash64_1(str, len); }
        XLE_CONSTEVAL_OR_CONSTEXPR uint32_t operator""_h32_compatible(const char* str, const size_t len) never_throws { return Internal::ConstHash32_1(str, len); }
        XLE_CONSTEVAL_OR_CONSTEXPR uint64_t operator""_h64_compatible(const char* str, const size_t len) never_throws { return Internal::ConstHash64_1(str, len); }
    }

    ///
    ///     @}
    ///

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
        struct ConstHash64Legacy
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

    uint64_t ConstHash64LegacyFromString(const char* begin, const char* end);

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
