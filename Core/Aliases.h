// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>
#include <tuple>
#include <vector>
#include <type_traits>

template<class T> using sp = std::shared_ptr<T>;
template<class T1, class T2> using p = std::pair<T1, T2>;
template<class T> using v = std::vector<T>;
template<class T1, class T2> using vp = std::vector<std::pair<T1, T2>>;
template<class... T> using t = std::tuple<T...>;

template <std::size_t I, class... Types> constexpr typename std::tuple_element<I, std::tuple<Types...>>::type& g( std::tuple<Types...>& t ) noexcept { return std::get<I, Types...>(t); }
template <std::size_t I, class... Types> constexpr typename std::tuple_element<I, std::tuple<Types...>>::type&& g( std::tuple<Types...>&& t ) noexcept { return std::get<I, Types...>(std::move(t)); }
template <std::size_t I, class... Types> constexpr const typename std::tuple_element<I, std::tuple<Types...>>::type& g( const std::tuple<Types...>& t ) noexcept { return std::get<I, Types...>(t); }
template <std::size_t I, class... Types> constexpr const typename std::tuple_element<I, std::tuple<Types...>>::type&& g( const std::tuple<Types...>&& t ) noexcept { return std::get<I, Types...>(std::move(t)); }
template <class T, class... Types> constexpr T& get( std::tuple<Types...>& t ) noexcept { return std::get<T, Types...>(t); }
template <class T, class... Types> constexpr T&& get( std::tuple<Types...>&& t ) noexcept { return std::get<T, Types...>(std::move(t)); }
template <class T, class... Types> constexpr const T& get( const std::tuple<Types...>& t ) noexcept { return std::get<T, Types...>(t); }
template <class T, class... Types> constexpr const T&& get( const std::tuple<Types...>&& t ) noexcept { return std::get<T, Types...>(std::move(t)); }

#define b2e(x) (x).begin(), (x).end()
#define Lambda_EqMember(mem, x) [x](const auto& q) { return q.mem == x; }

#if __cplusplus >= 202002L
	#define XLE_CONSTEVAL_OR_CONSTEXPR consteval
#else
	#define XLE_CONSTEVAL_OR_CONSTEXPR constexpr
#endif

#define HASH_LIKE_ENUM(X)																																												\
	XLE_CONSTEVAL_OR_CONSTEXPR X As##X(const char* str, const size_t len, uint64_t seed=DefaultSeed64) never_throws { return (X)Utility::Internal::ConstHash64_1(str, len, seed); }						\
	template<int N> XLE_CONSTEVAL_OR_CONSTEXPR X As##X(char (&key)[N], uint64_t seed=DefaultSeed64) { static_assert(N != 0); return (X)Utility::Internal::ConstHash64_1(key, N-1, seed); }				\
	XLE_CONSTEVAL_OR_CONSTEXPR X As##X(std::string_view v, uint64_t seed=DefaultSeed64) { return (X)Utility::Internal::ConstHash64_1(v.data(), v.size(), seed); }										\
	inline bool operator<(X lhs, X rhs) { return uint64_t(lhs) < uint64_t(rhs); }																														\
	/**/

#if !__cpp_lib_remove_cvref
	namespace std {
		template<class T>
			struct remove_cvref { using type = std::remove_cv_t<std::remove_reference_t<T>>; };
		template<class T>
			using remove_cvref_t = typename remove_cvref<T>::type;
	}
#endif

