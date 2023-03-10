// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"		// (for ResChar)
#include "IFileSystem.h"
#include "../Utility/UTFUtils.h"
#include "../Utility/StringUtils.h"
#include <assert.h>

namespace Formatters
{
	template<typename CharType> class TextInputFormatter;
}
namespace std { template<typename T> class shared_future; }

namespace Assets
{
	class DirectorySearchRules;
	class ChunkFileContainer;
	class ArtifactRequest;
    class IArtifactCollection;
	DirectorySearchRules DefaultDirectorySearchRules(StringSection<ResChar>);

	#define ENABLE_IF(...) typename std::enable_if_t<__VA_ARGS__>* = nullptr

	namespace Internal
	{
		template <typename AssetType>
			class AssetTraits_
		{
		public:
			static const bool Constructor_TextFile = std::is_constructible<AssetType, StringSection<>, DirectorySearchRules&&, DependencyValidation&&>::value;
			static const bool Constructor_ChunkFileContainer = std::is_constructible<AssetType, const ChunkFileContainer&>::value && !std::is_same_v<AssetType, ChunkFileContainer>;
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


			///////////////////////////////////////////////////////////////////////////////////////////////////

		const ChunkFileContainer& GetChunkFileContainer(StringSection<> identifier);
		std::shared_future<std::shared_ptr<ChunkFileContainer>> GetChunkFileContainerFuture(StringSection<> identifier);

        template <typename... Params> uint64_t BuildParamHash(const Params&... initialisers);

		template<typename T> struct IsSharedPtr : std::false_type {};
		template<typename T> struct IsSharedPtr<std::shared_ptr<T>> : std::true_type {};
		template<typename T> struct IsUniquePtr : std::false_type {};
		template<typename T> struct IsUniquePtr<std::unique_ptr<T>> : std::true_type {};

		template <typename Type, typename... Params, ENABLE_IF(IsSharedPtr<std::decay_t<Type>>::value)>
			Type InvokeAssetConstructor(Params&&... params)
		{
			return std::make_shared<typename Type::element_type>(std::forward<Params>(params)...);
		}

		template <typename Type, typename... Params, ENABLE_IF(IsUniquePtr<std::decay_t<Type>>::value)>
			Type InvokeAssetConstructor(Params&&... params)
		{
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
	//			(const ChunkFileContainer&)
	//
	template<typename AssetType, typename... Params, ENABLE_IF(Internal::AssetTraits<AssetType>::Constructor_ChunkFileContainer)>
		AssetType AutoConstructAsset(StringSection<> initializer)
	{
		// See also AutoConstructToPromise<> variation of this function
		const auto& container = Internal::GetChunkFileContainer(initializer);
		TRY {
			return Internal::InvokeAssetConstructor<AssetType>(container);
		} CATCH (const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
		} CATCH_END
	}

	template<typename AssetType, typename... Params, ENABLE_IF(Internal::AssetTraits<AssetType>::Constructor_ChunkFileContainer)>
		AssetType AutoConstructAsset(const Blob& blob, const DependencyValidation& depVal, StringSection<> requestParameters = {})
	{
		TRY {
			return Internal::InvokeAssetConstructor<AssetType>(ChunkFileContainer(blob, depVal, requestParameters));
		} CATCH (const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	}

	//
	//		Auto construct to:
	//			(IFileInterface&, const DirectorySearchRules&, const DependencyValidation&)
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
				depVal);
		} CATCH (const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	}

	//
	//		Auto construct to:
	//			(StringSection<utf8>&, const DirectorySearchRules&, const DependencyValidation&)
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
				depVal);
		} CATCH (const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	}

	//
	//		Auto construct entry point
	//
	template<typename AssetType, typename... Params, typename std::enable_if<std::is_constructible<Internal::RemoveSmartPtrType<AssetType>, Params&&...>::value>::type* = nullptr>
		static AssetType AutoConstructAsset(Params&&... initialisers)
	{
		return Internal::InvokeAssetConstructor<AssetType>(std::forward<Params>(initialisers)...);
	}

	#undef ENABLE_IF

}

