// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "InitializerPack.h"
#include "Marker.h"
#include "AssetTraits.h"
#include "../ConsoleRig/GlobalServices.h"		// for GetLongTaskThreadPool
#include "../OSServices/Log.h"
#include "../Utility/Threading/CompletionThreadPool.h"
#include "../Utility/StringUtils.h"		// required for implied StringSection<> constructor
#include <memory>

// todo -- ConfigFileContainer.h is required early, because the overrides that can be called by
// ApplyAutoConstructToPromise_impl must come before that function.
//
// In the C++ standard, this is 14.6.4.2 -- only lookups using ADl can use the context from template
// instantiation.
//
// However it would be preferable to break that dependency, because it would given us more flexibility 
// for expanding the range of AutoConstructAsset() overrides
// hm... it's difficult though because ADL isn't exactly what we want here
#include "ConfigFileContainer.h"
// #include "IArtifact.h"

namespace Assets
{
	#define ENABLE_IF(...) typename std::enable_if_t<__VA_ARGS__>* = nullptr

	namespace Internal
	{
		template<typename AssetOrPtrType, typename... Params>
			static auto HasConstructToPromiseClassOverride_Helper(int) -> decltype(
				Internal::RemoveSmartPtrType<AssetOrPtrType>::ConstructToPromise(std::declval<std::promise<AssetOrPtrType>&&>(), std::declval<Params>()...),
				std::true_type{});

		template<typename...>
			static auto HasConstructToPromiseClassOverride_Helper(...) -> std::false_type;

		template<typename AssetOrPtrType, typename... Params>
			struct HasConstructToPromiseClassOverride : decltype(HasConstructToPromiseClassOverride_Helper<AssetOrPtrType, Params&&...>(0)) {};		// outside of AssetTraits because the ptr type is important

		template<typename PromiseType, typename... Params>
			static auto HasConstructToPromiseFreeOverride_Helper(int) -> decltype(
				AutoConstructToPromiseOverride(std::declval<PromiseType&&>(), std::declval<Params>()...),
				std::true_type{});

		template<typename...>
			static auto HasConstructToPromiseFreeOverride_Helper(...) -> std::false_type;

		template<typename PromiseType, typename... Params>
			struct HasConstructToPromiseFreeOverride : decltype(HasConstructToPromiseFreeOverride_Helper<PromiseType, Params&&...>(0)) {};		// outside of AssetTraits because the ptr type is important
	}

	template<typename Promise, typename... Params>
		void AutoConstructToPromiseSynchronously(Promise&& promise, Params&&... initialisers)
	{
		if constexpr (Internal::HasConstructToPromiseClassOverride<Internal::PromisedType<Promise>, Params&&...>::value) {
			TRY {
				Internal::PromisedTypeRemPtr<Promise>::ConstructToPromise(std::move(promise), std::forward<Params>(initialisers)...);
			} CATCH(const std::exception& e) {
				Log(Error) << "Suppressing exception thrown from ConstructToPromise override. Overrides should not throw exceptions, and instead store them in the promise. Details follow:" << std::endl;
				Log(Error) << e.what() << std::endl;
			} CATCH(...) {
				Log(Error) << "Suppressing unknown exception thrown from ConstructToPromise override. Overrides should not throw exceptions, and instead store them in the promise." << std::endl;
			} CATCH_END
		} else {
			TRY {
				promise.set_value(AutoConstructAsset<Internal::PromisedType<Promise>>(std::forward<Params>(initialisers)...));
			} CATCH (...) {
				promise.set_exception(std::current_exception());
			} CATCH_END
		}
	}

	namespace Internal
	{
		template<typename Promise, typename Tuple, std::size_t ... I>
			auto ApplyAutoConstructToPromise_impl(Promise&& promise, Tuple&& t, std::index_sequence<I...>)
		{
			return AutoConstructToPromiseSynchronously(std::move(promise), std::get<I>(std::forward<Tuple>(t))...);
		}

		template<typename Ty, typename Tuple>
			auto ApplyAutoConstructToPromise(Ty&& promise, Tuple&& t)
		{
			using Indices = std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>;
			return ApplyAutoConstructToPromise_impl(std::move(promise), std::move(t), Indices{});
		}
	}

	template<typename Promise, typename... Params>
		void AutoConstructToPromise(Promise&& promise, Params&&... initialisers)
	{
		// note very similar "AutoConstructToPromiseSynchronously"
		if constexpr (Internal::HasConstructToPromiseClassOverride<Internal::PromisedType<Promise>, Params&&...>::value) {
			TRY {
				Internal::PromisedTypeRemPtr<Promise>::ConstructToPromise(std::move(promise), std::forward<Params>(initialisers)...);
			} CATCH(const std::exception& e) {
				Log(Error) << "Suppressing exception thrown from ConstructToPromise override. Overrides should not throw exceptions, and instead store them in the promise. Details follow:" << std::endl;
				Log(Error) << e.what() << std::endl;
			} CATCH(...) {
				Log(Error) << "Suppressing unknown exception thrown from ConstructToPromise override. Overrides should not throw exceptions, and instead store them in the promise." << std::endl;
			} CATCH_END
		} else if constexpr (Internal::HasConstructToPromiseFreeOverride<Promise, Params&&...>::value) {
			TRY {
				AutoConstructToPromiseOverride(std::move(promise), std::forward<Params>(initialisers)...);
			} CATCH(const std::exception& e) {
				Log(Error) << "Suppressing exception thrown from AutoConstructToPromiseOverride override. Overrides should not throw exceptions, and instead store them in the promise. Details follow:" << std::endl;
				Log(Error) << e.what() << std::endl;
			} CATCH(...) {
				Log(Error) << "Suppressing unknown exception thrown from AutoConstructToPromiseOverride override. Overrides should not throw exceptions, and instead store them in the promise." << std::endl;
			} CATCH_END
		} else {
			ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
				[initializersTuple=std::tuple{MakeStoreableInAny(initialisers)...}, promise=std::move(promise)]() mutable {
					TRY {
						Internal::ApplyAutoConstructToPromise(std::move(promise), std::move(initializersTuple));
					} CATCH(...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
		}
	}

	template<typename AssetType, typename... Params>
		std::shared_ptr<Marker<AssetType>> ConstructToMarker(Params&&... initialisers)
	{
		auto future = std::make_shared<Marker<AssetType>>(Internal::AsString(initialisers...));
		AutoConstructToPromise(future->AdoptPromise(), std::forward<Params>(initialisers)...);
		return future;
	}

	template<typename AssetType, typename... Params>
		std::shared_ptr<MarkerPtr<AssetType>> ConstructToMarkerPtr(Params&&... initialisers)
	{
		auto future = std::make_shared<MarkerPtr<AssetType>>(Internal::AsString(initialisers...));
		AutoConstructToPromise(future->AdoptPromise(), std::forward<Params>(initialisers)...);
		return future;
	}

	template<typename AssetType, typename... Params>
		std::future<AssetType> ConstructToFuture(Params&&... initialisers)
	{
		std::promise<AssetType> promise;
		auto future = promise.get_future();
		AutoConstructToPromise(std::move(promise), std::forward<Params>(initialisers)...);
		return future;
	}

	template<typename AssetType, typename... Params>
		std::future<std::shared_ptr<AssetType>> ConstructToFuturePtr(Params&&... initialisers)
	{
		std::promise<std::shared_ptr<AssetType>> promise;
		auto future = promise.get_future();
		AutoConstructToPromise(std::move(promise), std::forward<Params>(initialisers)...);
		return future;
	}

	#undef ENABLE_IF
}

