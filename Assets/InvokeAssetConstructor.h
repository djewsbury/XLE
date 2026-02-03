// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <type_traits>
#include <tuple>
#include <memory>

namespace Assets { namespace Internal
{
	#define ENABLE_IF(...) typename std::enable_if_t<__VA_ARGS__>* = nullptr

	template<typename T> struct IsSharedPtr : std::false_type {};
	template<typename T> struct IsSharedPtr<std::shared_ptr<T>> : std::true_type {};
	template<typename T> struct IsUniquePtr : std::false_type {};
	template<typename T> struct IsUniquePtr<std::unique_ptr<T>> : std::true_type {};

	template<typename AssetType> static auto RemoveSmartPtr_Helper(int) -> typename AssetType::element_type;
	template<typename AssetType, typename...> static auto RemoveSmartPtr_Helper(...) -> AssetType;
	template<typename AssetType> using RemoveSmartPtrType = decltype(RemoveSmartPtr_Helper<AssetType>(0));

	template<typename Promise>
		using PromisedType = std::decay_t<decltype(std::declval<Promise>().get_future().get())>;

	template<typename Promise>
		using PromisedTypeRemPtr = RemoveSmartPtrType<PromisedType<Promise>>;

	template <typename Type, typename... Params, ENABLE_IF(IsSharedPtr<std::decay_t<Type>>::value)>
		Type InvokeAssetConstructor(Params&&... params)
	{
		using T = std::tuple<Params...>;
		if constexpr (std::is_same_v<T, std::tuple<>>) {
			return std::make_shared<typename Type::element_type>();
		} else if constexpr (std::is_constructible_v<Type, Params...> && std::tuple_size_v<T> == 1 && !std::is_integral_v<std::tuple_element_t<0, T>>) {
			return Type { std::forward<Params>(params)... };		// constructing a smart ptr from another smart ptr
		} else
			return std::make_shared<typename Type::element_type>(std::forward<Params>(params)...);
	}

	template <typename Type, typename... Params, ENABLE_IF(IsUniquePtr<std::decay_t<Type>>::value)>
		Type InvokeAssetConstructor(Params&&... params)
	{
		using T = std::tuple<Params...>;
		if constexpr (std::is_same_v<T, std::tuple<>>) {
			return std::make_unique<typename Type::element_type>();
		} else if constexpr (std::is_constructible_v<Type, Params...> && std::tuple_size_v<T> == 1 && !std::is_integral_v<std::tuple_element_t<0, T>>) {
			return Type { std::forward<Params>(params)... };		// constructing a smart ptr from another smart ptr
		} else
			return std::make_unique<typename Type::element_type>(std::forward<Params>(params)...);
	}

	template <typename Type, typename... Params, ENABLE_IF(!IsSharedPtr<std::decay_t<Type>>::value && !IsUniquePtr<std::decay_t<Type>>::value)>
		Type InvokeAssetConstructor(Params&&... params)
	{
		return Type { std::forward<Params>(params)... };
	}

	#undef ENABLE_IF
}}
