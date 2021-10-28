// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "SelectConfiguration.h"

#if COMPILER_ACTIVE == COMPILER_TYPE_MSVC

    #define never_throws    noexcept
    #define force_inline    __forceinline
    #define no_inline       __declspec(noinline)
    #define dll_export      __declspec(dllexport)
    #define dll_import      __declspec(dllimport)
	#define attribute_packed
	#define expect_evaluation(X, Y)	X

	#if _MSC_VER <= 1600 || !defined(__cplusplus)
		#define thread_local    __declspec(thread)
	#endif

#elif (COMPILER_ACTIVE == COMPILER_TYPE_GCC) || (COMPILER_ACTIVE == COMPILER_TYPE_CLANG)

    #define never_throws		noexcept
    #define force_inline		inline __attribute__(( always_inline ))
	#define no_inline           __attribute__(( noinline ))
    #define attribute_packed	__attribute__((packed))
	#define expect_evaluation(X, Y)	__builtin_expect(X, Y)

    // _MSC_VER is defined with clang when microsoft extensions are enabled
    #if defined(_MSC_VER)
        #define dll_export      __declspec(dllexport)
        #define dll_import      __declspec(dllimport)
    #else
        #define dll_export      __attribute__((visibility("default")))
    #endif

    #if !defined(__cplusplus) && !defined(thread_local)
		#define thread_local    _Thread_local
	#endif

#endif

#pragma warning(disable:4481)   //  warning C4481: nonstandard extension used: override specifier 'override'
#pragma warning(disable:4068)   //  unknown pragma
#pragma clang diagnostic ignored "-Wmultichar" // multi character constant warning when using ConstHash64 object

#if !defined(dimof)
    #define dimof(x) (sizeof(x)/sizeof(*x))
#endif

    //
    //      See similar values in stdlib.h & in the windows headers
    //      Let's just use the same maximums for all platforms!
    //      There's also PATH_MAX (in limits.h) but this can be
    //      defined to a higher value for some platforms. That may
    //      not be exactly what we want -- ideally path sizes should
    //      be similar on all platforms.
    //
static const unsigned MaxPath         = 260;    /* max. length of full pathname */
static const unsigned MaxDrive        =   3;    /* max. length of drive component */
static const unsigned MaxDir          = 256;    /* max. length of path component */
static const unsigned MaxFilename     = 256;    /* max. length of file name component */
static const unsigned MaxExtension    = 256;    /* max. length of extension component */

// #ifndef c_assert
//     #define c_assert(expr)  typedef char __assertarray__[(expr) ? 1 : -1]; 
// #endif

#if COMPILER_ACTIVE == COMPILER_TYPE_GCC

        // useful!
    // #pragma GCC poison printf sprintf fprintf

#endif

#if CLIBRARIES_ACTIVE == CLIBRARIES_MSVC && defined(_DEBUG)

    #include <crtdbg.h>

    ////////////////////////////////////////////////////////////////////////////////////////////////
        //
        //      DavidJ --   After including some of the standard headers, we
        //                  can redefined new to add some debugging information
        //                  in the call.
        //
        //                  This is incompatible with certain uses of the "new"
        //                  keyword. For example, when defining an operator new
        //                  override, or when using the placement new. In these
        //                  cases, we just temporarily undefine new using the
        //                  following pattern:
        //
        //                  #undef new
        //                      ....
        //                  #if defined(DEBUG_NEW)
        //                      #define new DEBUG_NEW
        //                  #endif
        //
        //                  In C++11, we shouldn't be using new directly as often,
        //                  so maybe there's a better way to do this kind of tracking?
        //
    #if defined(_CRTDBG_MAP_ALLOC)
        #define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
        #define new DEBUG_NEW
    #endif

#endif

#if defined(_DEBUG)
    #define DEBUG_ONLY(...)       __VA_ARGS__
#else
    #define DEBUG_ONLY(...)       
#endif

#if defined(__cplusplus)

template<typename Type>
    inline void DeleteAndClear(Type*& ptr)
    {
        delete ptr;
        ptr = nullptr;
    }

template<typename Type>
    inline void ReleaseAndClear(Type*& ptr)
    {
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
    }

template<typename Type>
    inline void DeleteArrayAndClear(Type*& ptr)
    {
        delete ptr;
        ptr = nullptr;
    }

#if !defined(foreach)
    #define foreach(iteratorName, ContainerType, container)                                         \
        for (auto iteratorName=container.begin(); iteratorName!=container.end(); ++iteratorName)    \
        /**/
#endif

#if !defined(foreach_const)
    #define foreach_const(iteratorName, ContainerType, container)                                   \
        for (auto iteratorName=container.cbegin(); iteratorName!=container.cend(); ++iteratorName)  \
        /**/
#endif

    /// <summary>Wraps a compile condition for a if() statement<summary>
    /// Visual Studio produces "Conditional Expression is Constant" warnings
    /// when a static boolean value is used in a condition. This is a simple
    /// way to explicitly specify that the condition is a static/compile time
    /// condition and avoid any warnings.
    /// eg:
    ///     <example>
    ///         <code>\code
    ///             if (constant_expression<sizeof(void*) == 4>::result()) {
    ///                 ...
    ///             }
    ///         \endcode</code>
    ///     </example>

template<bool B> struct constant_expression    { static bool result() { return true; } };
template<> struct constant_expression<false>   { static bool result() { return false; } };

#define T1(A) template<typename A>
#define T2(A, B) template<typename A, typename B>
#define T3(A, B, C) template<typename A, typename B, typename C>
#define T4(A, B, C, D) template<typename A, typename B, typename C, typename D>

#if defined(ERROR_ON_DEPRECATED)
	#define XLE_DEPRECATED_ATTRIBUTE		[[deprecated]]
#else
	#define XLE_DEPRECATED_ATTRIBUTE
#endif

#endif
