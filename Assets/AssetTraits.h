// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IFileSystem.h"
#include "InitializerPack.h"
#include "../OSServices/Log.h"
#include "../Utility/UTFUtils.h"
#include "../Utility/StringUtils.h"
#include <assert.h>
#include <memory>
#include <future>

#if !defined(__CLR_VER)
	#include "../Utility/Threading/CompletionThreadPool.h"		// used by AutoConstructToPromise
	#include "Marker.h"		// used by ConstructToMarker
#endif

namespace Utility { class ThreadPool; }

namespace Assets
{
	class DirectorySearchRules;
	class ArtifactChunkContainer;
	class ArtifactRequest;
    class IArtifactCollection;
	DirectorySearchRules DefaultDirectorySearchRules(StringSection<>);

	#define ENABLE_IF(...) typename std::enable_if_t<__VA_ARGS__>* = nullptr

	namespace Internal
	{
		template <typename AssetType>
			class AssetTraits_
		{
		public:
			static const bool Constructor_TextFile = std::is_constructible<AssetType, StringSection<>, DirectorySearchRules&&, DependencyValidation&&>::value;
			static const bool Constructor_ChunkFileContainer = std::is_constructible<AssetType, const ArtifactChunkContainer&>::value && !std::is_same_v<AssetType, ArtifactChunkContainer>;
			static const bool Constructor_FileSystem = std::is_constructible<AssetType, IFileInterface&, DirectorySearchRules&&, DependencyValidation&&>::value;
		};

		template<typename AssetType> static auto RemoveSmartPtr_Helper(int) -> typename AssetType::element_type;
		template<typename AssetType, typename...> static auto RemoveSmartPtr_Helper(...) -> AssetType;
		template<typename AssetType> using RemoveSmartPtrType = decltype(RemoveSmartPtr_Helper<AssetType>(0));

		template<typename Promise>
			using PromisedType = std::decay_t<decltype(std::declval<Promise>().get_future().get())>;

		template<typename Promise>
			using PromisedTypeRemPtr = RemoveSmartPtrType<PromisedType<Promise>>;

		template<typename AssetType>
			using AssetTraits = AssetTraits_<std::decay_t<RemoveSmartPtrType<AssetType>>>;

        template <typename... Params> uint64_t BuildParamHash(const Params&... initialisers);

		template<typename T> struct IsSharedPtr : std::false_type {};
		template<typename T> struct IsSharedPtr<std::shared_ptr<T>> : std::true_type {};
		template<typename T> struct IsUniquePtr : std::false_type {};
		template<typename T> struct IsUniquePtr<std::unique_ptr<T>> : std::true_type {};

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
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	//
	//		Auto construct to:
	//			(IFileInterface&, DirectorySearchRules&&, DependencyValidation&&)
	//
	template<typename AssetType, ENABLE_IF(Internal::AssetTraits<AssetType>::Constructor_FileSystem)>
		AssetType AutoConstructAsset(StringSection<> initializer)
	{
		auto depVal = GetDepValSys().Make(initializer);
		TRY { 
			auto file = MainFileSystem::OpenFileInterface(initializer, "rb");
			return Internal::InvokeAssetConstructor<AssetType>(
				*file,
				DefaultDirectorySearchRules(initializer),
				::Assets::DependencyValidation(depVal));
		} CATCH (const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, std::move(depVal)));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, std::move(depVal)));
		} CATCH_END
	}

	//
	//		Auto construct to:
	//			(StringSection<>, DirectorySearchRules&&, DependencyValidation&&)
	//
	template<typename AssetType, ENABLE_IF(Internal::AssetTraits<AssetType>::Constructor_TextFile)>
		AssetType AutoConstructAsset(StringSection<> initializer)
	{
		auto depVal = GetDepValSys().Make(initializer);
		TRY { 
			auto file = MainFileSystem::OpenFileInterface(initializer, "rb");
			file->Seek(0, OSServices::FileSeekAnchor::End);
			auto size = file->TellP();
			auto block = std::make_unique<char[]>(size);
			file->Seek(0);
			auto readCount = file->Read(block.get(), size);
			assert(readCount == 1); (void)readCount;
			return Internal::InvokeAssetConstructor<AssetType>(
				MakeStringSection(block.get(), PtrAdd(block.get(), size)),
				DefaultDirectorySearchRules(initializer),
				::Assets::DependencyValidation(depVal));
		} CATCH (const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, std::move(depVal)));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, std::move(depVal)));
		} CATCH_END
	}

	//
	//		Auto construct entry point
	//
	template<typename AssetType, typename... Params, ENABLE_IF(std::is_constructible<Internal::RemoveSmartPtrType<AssetType>, Params&&...>::value)>
		static AssetType AutoConstructAsset(Params&&... initialisers)
	{
		return Internal::InvokeAssetConstructor<AssetType>(std::forward<Params>(initialisers)...);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	/// <summary>Helper class for defeating C++ unqualified name lookup rules</summary>
	///
	/// When an unqualified function is referenced from a template function/method, normally the 
	/// function declaration must be visible in the compilation context of the first pass of that 
	/// template function/method.
	///
	/// In other words, even if there is an implementation or override for the function visible to
	/// the instantiation context, it can't be used unless it was also visible to the first pass.
	///
	/// In practices, this requires the DeferredConstruction header to be included after all possible
	/// overrides of AutoConstructToPromiseOverride (and other similar functions)
	///
	/// However, the above rule is broken for the case of argument dependant lookup (ADL). In that case, all
	/// possibilities in the instantiation context are considered.
	/// In the C++ standard, this is 14.6.4.2
	///
	/// We want AutoConstructToPromiseOverride to take on this ADL behaviour, to enable greater flexibility
	/// and avoid awkward restrictions about include order.
	///
	/// To do this, we replace std::promise<> with an near-identical class in Assets, and this causes
	/// ADL to kick in and find the AutoConstructToPromiseOverride implementations in Assets. This works
	/// even if all of the arguments in AutoConstructToPromiseOverride are templated -- ie, it's just the
	/// fact that we're passing in an object from the Assets namespace that causes all of the overrides
	/// in Assets to be found with the preferred behaviour.
	///
	template<typename T>
		class WrappedPromise : public std::promise<T>
	{
	public:
		WrappedPromise(std::promise<T>&& p) : std::promise<T>(std::move(p)) {}
		WrappedPromise() {}
		using std::promise<T>::operator=;
	};

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

		template<typename AssetOrPtrType, typename... Params>
			static auto HasConstructToPromiseFreeOverride_Helper(int) -> decltype(
				AutoConstructToPromiseOverride(std::declval<WrappedPromise<AssetOrPtrType>&&>(), std::declval<Params>()...),
				std::true_type{});

		template<typename...>
			static auto HasConstructToPromiseFreeOverride_Helper(...) -> std::false_type;

		template<typename AssetOrPtrType, typename... Params>
			struct HasConstructToPromiseFreeOverride : decltype(HasConstructToPromiseFreeOverride_Helper<AssetOrPtrType, Params&&...>(0)) {};		// outside of AssetTraits because the ptr type is important

		ThreadPool& GetLongTaskThreadPool();
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

#if !defined(__CLR_VER)
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
		} else if constexpr (Internal::HasConstructToPromiseFreeOverride<Internal::PromisedType<Promise>, Params&&...>::value) {
			TRY {
				AutoConstructToPromiseOverride(WrappedPromise<Internal::PromisedType<Promise>>{std::move(promise)}, std::forward<Params>(initialisers)...);
			} CATCH(const std::exception& e) {
				Log(Error) << "Suppressing exception thrown from AutoConstructToPromiseOverride override. Overrides should not throw exceptions, and instead store them in the promise. Details follow:" << std::endl;
				Log(Error) << e.what() << std::endl;
			} CATCH(...) {
				Log(Error) << "Suppressing unknown exception thrown from AutoConstructToPromiseOverride override. Overrides should not throw exceptions, and instead store them in the promise." << std::endl;
			} CATCH_END
		} else {
			Internal::GetLongTaskThreadPool().Enqueue(
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
#endif

	#undef ENABLE_IF
}

