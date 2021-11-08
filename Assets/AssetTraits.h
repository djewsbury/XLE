// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"		// (for ResChar)
#include "AssetUtils.h"     // (for DirectorySearchRules)
#include "ConfigFileContainer.h"
#include "ChunkFileContainer.h"
#include "DepVal.h"
#include "IFileSystem.h"
#include "../Utility/UTFUtils.h"
#include "../Utility/StringUtils.h"
#include <assert.h>
#include <future>

namespace Utility
{
	template<typename CharType> class InputStreamFormatter;
}

namespace Assets
{
	template <typename Asset> class DivergentAsset;
	template <typename Formatter> class ConfigFileContainer;
	class DirectorySearchRules;
	class ChunkFileContainer;
	class ArtifactRequest;
    class IArtifactCollection;

	#define ENABLE_IF(X) typename std::enable_if<X>::type* = nullptr

	namespace Internal
	{
		template <typename AssetType>
			class AssetTraits_
		{
		private:
			struct HasCompileProcessTypeHelper
			{
				struct FakeBase { static const uint64_t CompileProcessType; };
				struct TestSubject : public FakeBase, public AssetType {};

				template <typename C, C> struct Check;

				// This technique is based on an implementation from StackOverflow. Here, taking the address
				// of the static member variable in TestSubject would be ambiguous, iff CompileProcessType 
				// is actually a member of AssetType (otherwise, the member in FakeBase is found)
				template <typename C> static std::false_type Test(Check<const uint64_t*, &C::CompileProcessType> *);
				template <typename> static std::true_type Test(...);

				static const bool value = decltype(Test<TestSubject>(0))::value;
			};

			template<typename T> static auto HasChunkRequestsHelper(int) -> decltype(&T::ChunkRequests[0], std::true_type{});
			template<typename...> static auto HasChunkRequestsHelper(...) -> std::false_type;

		public:
			using DivAsset = DivergentAsset<AssetType>;

			static const bool Constructor_Formatter = std::is_constructible<AssetType, InputStreamFormatter<utf8>&, const DirectorySearchRules&, const DependencyValidation&>::value;
			static const bool Constructor_TextFile = std::is_constructible<AssetType, StringSection<>&, const DirectorySearchRules&, const DependencyValidation&>::value;
			static const bool Constructor_ChunkFileContainer = std::is_constructible<AssetType, const ChunkFileContainer&>::value && !std::is_same_v<AssetType, ChunkFileContainer>;
			static const bool Constructor_FileSystem = std::is_constructible<AssetType, IFileInterface&, const DirectorySearchRules&, const DependencyValidation&>::value;

			static const bool HasCompileProcessType = HasCompileProcessTypeHelper::value;
			static const bool HasChunkRequests = decltype(HasChunkRequestsHelper<AssetType>(0))::value;
		};

		template<typename AssetType> static auto RemoveSmartPtr_Helper(int) -> typename AssetType::element_type;
		template<typename AssetType, typename...> static auto RemoveSmartPtr_Helper(...) -> AssetType;
		template<typename AssetType> using RemoveSmartPtrType = decltype(RemoveSmartPtr_Helper<AssetType>(0));

		template<typename AssetType>
			using AssetTraits = AssetTraits_<std::decay_t<RemoveSmartPtrType<AssetType>>>;

		template<typename AssetType, typename... Params>
			static auto HasConstructToPromiseOverride_Helper(int) -> decltype(
				Internal::RemoveSmartPtrType<AssetType>::ConstructToPromise(std::declval<std::promise<AssetType>&&>(), std::declval<Params>()...), 
				std::true_type{});

		template<typename...>
			static auto HasConstructToPromiseOverride_Helper(...) -> std::false_type;

		template<typename AssetType, typename... Params>
			struct HasConstructToPromiseOverride : decltype(HasConstructToPromiseOverride_Helper<AssetType, Params...>(0)) {};

			///////////////////////////////////////////////////////////////////////////////////////////////////

		const ConfigFileContainer<InputStreamFormatter<utf8>>& GetConfigFileContainer(StringSection<> identifier);
		const ChunkFileContainer& GetChunkFileContainer(StringSection<> identifier);
		PtrToFuturePtr<ConfigFileContainer<InputStreamFormatter<utf8>>> GetConfigFileContainerFuture(StringSection<> identifier);
		PtrToFuturePtr<ChunkFileContainer> GetChunkFileContainerFuture(StringSection<> identifier);

        template <typename... Params> uint64_t BuildParamHash(Params... initialisers);

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
	//			(InputStreamFormatter<utf8>&, const DirectorySearchRules&, const DependencyValidation&)
	//
	template<typename AssetType, ENABLE_IF(Internal::AssetTraits<AssetType>::Constructor_Formatter)>
		AssetType AutoConstructAsset(StringSection<> initializer)
	{
		// First parameter should be the section of the input file to read (or just use the root of the file if it doesn't exist)
		// See also AutoConstructToPromise<> variation of this function
		const char* p = XlFindChar(initializer, ':');
		if (p) {
			char buffer[256];
			XlCopyString(buffer, MakeStringSection(initializer.begin(), p));
			const auto& container = Internal::GetConfigFileContainer(buffer);
			TRY {
				auto fmttr = container.GetFormatter(MakeStringSection((const utf8*)(p+1), (const utf8*)initializer.end()));
				return Internal::InvokeAssetConstructor<AssetType>(
					fmttr, 
					DefaultDirectorySearchRules(buffer),
					container.GetDependencyValidation());
			} CATCH (const Exceptions::ConstructionError& e) {
				Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
			} CATCH (const std::exception& e) {
				Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
			} CATCH_END
		} else {
			const auto& container = Internal::GetConfigFileContainer(initializer);
			TRY { 
				auto fmttr = container.GetRootFormatter();
				return Internal::InvokeAssetConstructor<AssetType>(
					fmttr,
					DefaultDirectorySearchRules(initializer),
					container.GetDependencyValidation());
			} CATCH (const Exceptions::ConstructionError& e) {
				Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
			} CATCH (const std::exception& e) {
				Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
			} CATCH_END
		}
	}

	template<typename AssetType, ENABLE_IF(Internal::AssetTraits<AssetType>::Constructor_Formatter)>
		AssetType AutoConstructAsset(const Blob& blob, const DependencyValidation& depVal, StringSection<> requestParameters = {})
	{
		TRY {
			auto container = ConfigFileContainer<>(blob, depVal);
			auto fmttr = requestParameters.IsEmpty() ? container.GetRootFormatter() : container.GetFormatter(requestParameters.Cast<utf8>());
			return Internal::InvokeAssetConstructor<AssetType>(
				fmttr,
				DirectorySearchRules{},
				container.GetDependencyValidation());
		} CATCH(const Exceptions::ConstructionError& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH(const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	}
	
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
		} CATCH (const Exceptions::ConstructionError& e) {
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
		} CATCH (const Exceptions::ConstructionError& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	}

	//
	//		Auto construct to:
	//			(IteratorRange<ArtifactRequestResult>, const DependencyValidation&)
	//
	template<typename AssetType, typename... Params, ENABLE_IF(Internal::AssetTraits<AssetType>::HasChunkRequests)>
		AssetType AutoConstructAsset(StringSection<> initializer)
	{
		// See also AutoConstructToPromise<> variation of this function
		const auto& container = Internal::GetChunkFileContainer(initializer);
		TRY {
			auto chunks = container.ResolveRequests(MakeIteratorRange(Internal::RemoveSmartPtrType<AssetType>::ChunkRequests));
			return Internal::InvokeAssetConstructor<AssetType>(MakeIteratorRange(chunks), container.GetDependencyValidation());
		} CATCH (const Exceptions::ConstructionError& e) {
			Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
		} CATCH_END
	}

	template<typename AssetType, typename... Params, ENABLE_IF(Internal::AssetTraits<AssetType>::HasChunkRequests)>
		AssetType AutoConstructAsset(const Blob& blob, const DependencyValidation& depVal, StringSection<> requestParameters = {})
	{
		TRY {
			auto chunks = ChunkFileContainer(blob, depVal, requestParameters).ResolveRequests(MakeIteratorRange(Internal::RemoveSmartPtrType<AssetType>::ChunkRequests));
			return Internal::InvokeAssetConstructor<AssetType>(MakeIteratorRange(chunks), depVal);
		} CATCH (const Exceptions::ConstructionError& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	}

	template<typename AssetType, typename... Params, ENABLE_IF(Internal::AssetTraits<AssetType>::HasChunkRequests)>
		AssetType AutoConstructAsset(const IArtifactCollection& artifactCollection)
	{
		TRY {
			auto chunks = artifactCollection.ResolveRequests(MakeIteratorRange(Internal::RemoveSmartPtrType<AssetType>::ChunkRequests));
			return Internal::InvokeAssetConstructor<AssetType>(MakeIteratorRange(chunks), artifactCollection.GetDependencyValidation());
		} CATCH (const Exceptions::ConstructionError& e) {
			Throw(Exceptions::ConstructionError(e, artifactCollection.GetDependencyValidation()));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, artifactCollection.GetDependencyValidation()));
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
		} CATCH (const Exceptions::ConstructionError& e) {
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
		} CATCH (const Exceptions::ConstructionError& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	}

	//
	//		Auto construct entry point
	//
	template<typename AssetType, typename... Params, typename std::enable_if<std::is_constructible<Internal::RemoveSmartPtrType<AssetType>, Params...>::value>::type* = nullptr>
		static AssetType AutoConstructAsset(Params... initialisers)
	{
		return Internal::InvokeAssetConstructor<AssetType>(std::forward<Params>(initialisers)...);
	}

	#undef ENABLE_IF

}

