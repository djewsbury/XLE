
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include <type_traits>

namespace Assets { namespace Internal
{

	template<typename Expected, typename... Types> static constexpr bool PackHasType = (std::is_same_v<Expected, Types> + ...) == 1;
	template<typename Expected, typename... Types> static std::conditional_t<PackHasType<Expected, Types...>, std::true_type, std::false_type> TupleHasType_Helper(const std::tuple<Types...>&);
	template<typename Expected> static std::false_type TupleHasType_Helper(...);
	template<typename Expected, typename Tuple> static constexpr bool TupleHasType = decltype(TupleHasType_Helper<Expected>(std::declval<const Tuple&>()))::value;

	template<typename Type> static auto HasGetDependencyValidation_Helper(int) -> decltype(std::declval<Type>().GetDependencyValidation(), std::true_type{});
	template<typename...> static auto HasGetDependencyValidation_Helper(...) -> std::false_type;
	template<typename Type> static constexpr bool HasGetDependencyValidation = decltype(HasGetDependencyValidation_Helper<Type>(0))::value;

	template<typename Type> static auto HasDerefGetDependencyValidation_Helper(int) -> decltype((*std::declval<Type>()).GetDependencyValidation(), std::true_type{});
	template<typename...> static auto HasDerefGetDependencyValidation_Helper(...) -> std::false_type;
	template<typename Type> static constexpr bool HasDerefGetDependencyValidation = decltype(HasDerefGetDependencyValidation_Helper<Type>(0))::value;

	template<typename Type> static constexpr bool HasStdGetDependencyValidation = TupleHasType<DependencyValidation, Type>;

	template<typename Type> decltype(std::declval<const Type&>().GetDependencyValidation()) GetDependencyValidation(const Type& asset) { return asset.GetDependencyValidation(); }
	template<typename Type> std::remove_reference_t<decltype(std::declval<const Type&>()->GetDependencyValidation())> GetDependencyValidation(const Type& asset) { return asset ? asset->GetDependencyValidation() : std::remove_reference_t<decltype(std::declval<const Type&>()->GetDependencyValidation())>{}; }
	template<typename Type> std::enable_if_t<HasStdGetDependencyValidation<Type> && !HasGetDependencyValidation<Type> && !HasDerefGetDependencyValidation<Type>, const DependencyValidation&> GetDependencyValidation(const Type& asset) { return std::get<DependencyValidation>(asset); }

	template<typename Type, typename =std::enable_if_t<!HasGetDependencyValidation<Type> && !HasDerefGetDependencyValidation<Type> && !HasStdGetDependencyValidation<Type>>>
		DependencyValidation GetDependencyValidation(const Type& asset) { return {}; }

		///////

	template<typename Type> static auto HasGetActualizationLog_Helper(int) -> decltype(std::declval<Type>().GetActualizationLog(), std::true_type{});
	template<typename...> static auto HasGetActualizationLog_Helper(...) -> std::false_type;
	template<typename Type> static constexpr bool HasGetActualizationLog = decltype(HasGetActualizationLog_Helper<Type>(0))::value;

	template<typename Type> static auto HasDerefGetActualizationLog_Helper(int) -> decltype((*std::declval<Type>()).GetActualizationLog(), std::true_type{});
	template<typename...> static auto HasDerefGetActualizationLog_Helper(...) -> std::false_type;
	template<typename Type> static constexpr bool HasDerefGetActualizationLog = decltype(HasDerefGetActualizationLog_Helper<Type>(0))::value;

	template<typename Type> static constexpr bool HasStdGetActualizationLog = TupleHasType<Blob, Type>;

	template<typename Type> decltype(std::declval<const Type&>().GetActualizationLog()) GetActualizationLog(const Type& asset) { return asset.GetActualizationLog(); }
	template<typename Type> std::remove_reference_t<decltype(std::declval<const Type&>()->GetActualizationLog())> GetActualizationLog(const Type& asset) { return asset ? asset->GetActualizationLog() : std::remove_reference_t<decltype(std::declval<const Type&>()->GetActualizationLog())>{}; }
	template<typename Type> std::enable_if_t<HasStdGetActualizationLog<Type>, const Blob&> GetActualizationLog(const Type& asset) { return std::get<Blob>(asset); }

	template<typename Type, typename =std::enable_if_t<!HasGetActualizationLog<Type> && !HasDerefGetActualizationLog<Type> && !HasStdGetActualizationLog<Type>>>
		Blob GetActualizationLog(const Type& asset) { return {}; }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename Future, typename AssetType>
		void TryGetAssetFromFuture(
			Future& future,
			AssetState& state,
			AssetType& actualized,
			Blob& actualizationLog,
			DependencyValidation& actualizedDepVal)
	{
		TRY {
			auto pendingResult = future.get();
			actualized = std::move(pendingResult);
			actualizedDepVal = Internal::GetDependencyValidation(actualized);
			actualizationLog = {};
			state = AssetState::Ready;
		} CATCH (const Exceptions::ConstructionError& e) {
			actualizedDepVal = e.GetDependencyValidation();
			actualizationLog = e.GetActualizationLog();
			state = AssetState::Invalid;
		} CATCH (const Exceptions::InvalidAsset& e) {
			actualizedDepVal = e.GetDependencyValidation();
			actualizationLog = e.GetActualizationLog();
			state = AssetState::Invalid;
		} CATCH (const Exceptions::ExceptionWithDepVal& e) {
			actualizedDepVal = e.GetDependencyValidation();
			actualizationLog = AsBlob(e);
			state = AssetState::Invalid;
		} CATCH (const std::exception& e) {
			actualizedDepVal = {};
			actualizationLog = AsBlob(e);
			state = AssetState::Invalid;
		} CATCH_END
	}

	template<typename Type>
		void SetPromiseInvalidAsset(std::promise<Type>& promise, DependencyValidation depVal, const Blob& log)
	{
		promise.set_exception(std::make_exception_ptr(Exceptions::InvalidAsset({}, depVal, log)));
	}
}}
