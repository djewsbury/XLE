// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/Prefix.h"
#include "../Utility/MemoryUtils.h"		// for TypeHashCode
#include <memory>
#include <string>
#include <vector>
#include <assert.h>
#include <stddef.h>     // (for ptrdiff_t)

#if COMPILER_ACTIVE == COMPILER_TYPE_MSVC

	#if _MSC_VER >= 1700
		#define VARIADIC_TEMPLATE_PARAMETERS
		#define STD_LIB_HAS_MAKE_UNIQUE
		#define STD_LIB_HAS_FORWARD
	#endif

    ////////////////////////////////////////////////////////////////////////////////////////////////

        //
        //   std::make_unique implementation from C++14 proposals at:
        //      http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3588.txt
        //

	#if !defined(STD_LIB_HAS_MAKE_UNIQUE)

		#if defined(VARIADIC_TEMPLATE_PARAMETERS)

			namespace std {
				template<class T> struct _Never_true : false_type { };

				template<class T> struct _Unique_if {
					typedef unique_ptr<T> _Single;
				};

				template<class T> struct _Unique_if<T[]> {
					typedef unique_ptr<T[]> _Runtime;
				};

				template<class T, size_t N> struct _Unique_if<T[N]> {
					static_assert(_Never_true<T>::value, "make_unique forbids T[N]. Please use T[].");
				};

				template<class T, class... Args> typename _Unique_if<T>::_Single make_unique(Args&&... args) {
					return unique_ptr<T>(new T(std::forward<Args>(args)...));
				}

				template<class T> typename _Unique_if<T>::_Single make_unique_default_init() {
					return unique_ptr<T>(new T);
				}

				template<class T> typename _Unique_if<T>::_Runtime make_unique(size_t n) {
					typedef typename remove_extent<T>::type U;
					return unique_ptr<T>(new U[n]());
				}

				template<class T> typename _Unique_if<T>::_Runtime make_unique_default_init(size_t n) {
					typedef typename remove_extent<T>::type U;
					return unique_ptr<T>(new U[n]);
				}

				template<class T, class... Args> typename _Unique_if<T>::_Runtime make_unique_value_init(size_t n, Args&&... args) {
					typedef typename remove_extent<T>::type U;
					return unique_ptr<T>(new U[n]{ std::forward<Args>(args)... });
				}

				template<class T, class... Args> typename _Unique_if<T>::_Runtime make_unique_auto_size(Args&&... args) {
					typedef typename remove_extent<T>::type U;
					return unique_ptr<T>(new U[sizeof...(Args)]{ std::forward<Args>(args)... });
				}
			}

		#else

			namespace std {

					//
					//      Primitive implementation of make_unique with limited parameter support.
					//      This is required to get around lack of support for variadic template
					//      parameters most version of the VS compiler.
					//

				template <typename Result>
					std::unique_ptr<Result> make_unique() { return std::unique_ptr<Result>(new Result()); }

				template <typename Result, typename X1>
					std::unique_ptr<Result> make_unique(X1 x1) { return std::unique_ptr<Result>(new Result(std::forward<X1>(x1))); }

				template <typename Result, typename X1, typename X2>
					std::unique_ptr<Result> make_unique(X1 x1, X2 x2) { return std::unique_ptr<Result>(new Result(std::forward<X1>(x1), std::forward<X2>(x2))); }

				template <typename Result, typename X1, typename X2, typename X3>
					std::unique_ptr<Result> make_unique(X1 x1, X2 x2, X3 x3) { return std::unique_ptr<Result>(new Result(std::forward<X1>(x1), std::forward<X2>(x2), std::forward<X3>(x3))); }

				template <typename Result, typename X1, typename X2, typename X3, typename X4>
					std::unique_ptr<Result> make_unique(X1 x1, X2 x2, X3 x3, X4 x4) { return std::unique_ptr<Result>(new Result(std::forward<X1>(x1), std::forward<X2>(x2), std::forward<X3>(x3), std::forward<X4>(x4))); }

				template <typename Result, typename X1, typename X2, typename X3, typename X4, typename X5>
					std::unique_ptr<Result> make_unique(X1 x1, X2 x2, X3 x3, X4 x4, X5 x5) { return std::unique_ptr<Result>(new Result(std::forward<X1>(x1), std::forward<X2>(x2), std::forward<X3>(x3), std::forward<X4>(x4), std::forward<X5>(x5))); }

				template <typename Result, typename X1, typename X2, typename X3, typename X4, typename X5, typename X6>
					std::unique_ptr<Result> make_unique(X1 x1, X2 x2, X3 x3, X4 x4, X5 x5, X6 x6) { return std::unique_ptr<Result>(new Result(std::forward<X1>(x1), std::forward<X2>(x2), std::forward<X3>(x3), std::forward<X4>(x4), std::forward<X5>(x5), std::forward<X6>(x6))); }


				//
				//      (see also an interesting version from the comments in 
				//          http://herbsutter.com/gotw/_102/#comment-6428
				//      this uses macros from the VS2012 headers. Something similar
				//      might be possible using Loki libraries or boost libraries.)
				//
				// #define _MAKE_UNIQUE(TEMPLATE_LIST, PADDING_LIST, LIST, COMMA, X1, X2, X3, X4)  \
				//                                                                                 \
				//     template<class T COMMA LIST(_CLASS_TYPE)>>                                  \
				//         std::unique_ptr<T> make_unique(LIST(_TYPE_REFREF_ARG))                  \
				//         {                                                                       \
				//             return std::unique_ptr<T>(new T(LIST(_FORWARD_ARG)));               \
				//         }                                                                       \
				//                                                                                 \
				//     /**/
				// 
				// _VARIADIC_EXPAND_0X(_MAKE_UNIQUE, , , , )
				// #undef _MAKE_UNIQUE



				template<class T> struct _Never_true : false_type { };

				template<class T> struct _Unique_if {
					typedef unique_ptr<T> _Single;
				};

				template<class T> struct _Unique_if<T[]> {
					typedef unique_ptr<T[]> _Runtime;
				};

				template<class T, size_t N> struct _Unique_if<T[N]> {
					static_assert(_Never_true<T>::value, "make_unique forbids T[N]. Please use T[].");
				};

				template<class T> typename _Unique_if<T>::_Runtime make_unique(size_t n) 
				{
					typedef typename remove_extent<T>::type U;
					return unique_ptr<T>(new U[n]());
				}

				#if (TARGET_64BIT==1)
					template<class T> typename _Unique_if<T>::_Runtime make_unique(unsigned n)
					{
						typedef typename remove_extent<T>::type U;
						return unique_ptr<T>(new U[n]());
					}
				#endif
			}

		#endif
	#endif

	#if !defined(STD_LIB_HAS_FORWARD)

		#if _MSC_VER <= 1600
			namespace std
			{
					//
					//      From http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2009/n2951.html
					//      (recommended implementation of std::forward)
					//
				template<class T, class U/*,        this condition isn't compiling in VS2010
					class = 
						typename enable_if<
							(is_lvalue_reference<T>::value ? is_lvalue_reference<U>::value : true) &&
							is_convertible<typename remove_reference<U>::type*,typename remove_reference<T>::type*>::value
						>::type*/>
				inline T&& forward(U&& u)
				{
					return static_cast<T&&>(u);
				}
			}
		#endif

	#endif

#endif

namespace Utility
{
            /////////////////////////////////////////////////////////////////////////////

    template <typename Type>
        constexpr Type * PtrAdd( Type * input, ptrdiff_t offset )  { return (Type*)( size_t(input) + offset ); }

    template <typename TypeA>
        constexpr ptrdiff_t PtrDiff(TypeA * lhs, TypeA * rhs) { return ptrdiff_t(lhs) - ptrdiff_t(rhs); }

    template <typename TypeA>
        constexpr ptrdiff_t PtrDiff(TypeA * lhs, size_t rhs) { return ptrdiff_t(lhs) - ptrdiff_t(rhs); }

    template <typename Type>
        constexpr Type * AsPointer( Type * i )                     { return i; }

    #if STL_ACTIVE == STL_MSVC

        template <typename VecType>
			constexpr typename VecType::value_type * AsPointer(const std::_Vector_iterator<VecType> & i)
                { return i._Unwrapped(); }

        template <typename VecType>
			constexpr const typename VecType::value_type * AsPointer(const std::_Vector_const_iterator<VecType> & i)
                { return i._Unwrapped(); }

		#if _MSC_VER >= 1700
			template <typename StringType>
				constexpr typename StringType::value_type * AsPointer(const std::_String_iterator<StringType>& i)
				{
					return i._Unwrapped();
				}

			template <typename StringType>
				constexpr const typename StringType::value_type * AsPointer(const std::_String_const_iterator<StringType>& i)
				{
					return i._Unwrapped();
				}

			template <typename Traits>
				constexpr const typename Traits::char_type * AsPointer(const std::_String_view_iterator<Traits>& i)
				{
					return i._Unwrapped();
				}
		#else
			template <typename Type, typename Traits, typename Alloc>
				Type * AsPointer(const std::_String_iterator<Type, Traits, Alloc>& i)
				{
					return (Type*)i._Ptr;
				}

			template <typename Type, typename Traits, typename Alloc>
				const Type * AsPointer(const std::_String_const_iterator<Type, Traits, Alloc>& i)
				{
					return (Type*)i._Ptr;
				}
		#endif

    #else

        template <typename Iterator>
            decltype(&(*std::declval<Iterator>())) AsPointer(Iterator i) { return &(*i); }
    
    #endif

    /// <summary>Finds the end of a fixed length array</summary>
    /// This just finds the end of an array. It's similar to using dimof
    /// as in the following example:
    /// <code>\code{.cpp}
    ///     char fixedLengthArray[256];
    ///     auto* end1 = &fixedLengthArray[dimof(fixedLengthArray)];
    ///     auto* end2 = ArrayEnd(fixedLengthArray);
    ///     assert(end1==end2);
    /// \endcode</code>
    template<typename Type, int Count>
        constexpr Type* ArrayEnd(Type (&input)[Count]) { return &input[Count]; }

    /// <summary>Equivalent to static_cast, but with extra debugging in Debug builds</summary>
    /// In debug builds, if rtti is enabled, performs a dynamic_cast. Otherwise, performs a
    /// static_cast.
    /// This is useful when doing an up-cast with static_cast, because it provides some
    /// extra validation.
    /// But note that is rtti is disabled in debug, dynamic_cast can't be performed. So the
    /// system will just drop back to static_cast.
    template<typename DestinationType, typename SourceType>
        DestinationType checked_cast(SourceType source)
        {
                //
                //  Note -- if RTTI is disabled in Debug, then it's never checked
                //
            #if defined(_DEBUG) && FEATURE_RTTI
                DestinationType dynamicResult = dynamic_cast<DestinationType>(source);
				DestinationType staticResult = static_cast<DestinationType>(source);
                assert(dynamicResult == staticResult);
                return staticResult;
            #else
                return static_cast<DestinationType>(source);
            #endif
        }

	template<typename DestinationType, typename SourceType>
        decltype(std::static_pointer_cast<DestinationType>(std::declval<const SourceType&>())) checked_pointer_cast(const SourceType& source)
        {
                //
                //  Note -- if RTTI is disabled in Debug, then it's never checked
                //
            #if defined(_DEBUG) && FEATURE_RTTI
                auto dynamicResult = std::dynamic_pointer_cast<DestinationType>(source);
				auto staticResult = std::static_pointer_cast<DestinationType>(source);
                assert(dynamicResult == staticResult);
                return staticResult;
            #else
                return std::static_pointer_cast<DestinationType>(source);
            #endif
        }

	template<typename RequestedInterface, typename SrcType>
        RequestedInterface query_interface_cast(SrcType* input)
    {
		assert(input);
		constexpr auto interfaceCode = TypeHashCode<std::decay_t<std::remove_pointer_t<RequestedInterface>>>;
        return (RequestedInterface)input->QueryInterface(interfaceCode);
    }

    template<typename T>
        static bool Equivalent(const std::weak_ptr<T>& lhs, const std::weak_ptr<T>& rhs)
        {
                //  "owner_before" should compare the control block of these pointers in
                //  most cases. We want to check to see if both pointers have the same
                //  control block (and then consider them equivalent)
            return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
        }

    template<typename T>
        static bool Equivalent(const std::shared_ptr<T>& lhs, const std::weak_ptr<T>& rhs)
        {
            return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
        }

    template<typename T>
        static bool Equivalent(const std::weak_ptr<T>& lhs, const std::shared_ptr<T>& rhs)
        {
            return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
        }

            ////////////////////////////////////////////////////////

    /// <summary>Returns a default value for a given type</summary>
    /// Returns a reasonable default for the templated type. The meaning of "default"
    /// can change from type to type. Generally this is used by other template functions,
    /// where any assignment is required, but no value is forthcoming (for example, if
    /// deserialization failed, or an expected value is somehow missing).
    /// This is intended to be similar to the "default" keyword in C#.
    template<typename Type> Type Default() { return Type(); }
    template<typename Type, typename std::enable_if<std::is_pointer<Type>::value>::type* = nullptr>
        Type* Default() { return nullptr; }

}

using namespace Utility;
