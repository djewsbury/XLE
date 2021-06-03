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

	namespace Internal
	{
		template <typename AssetType>
			class AssetTraits
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
			static const bool Constructor_ChunkFileContainer = std::is_constructible<AssetType, const ChunkFileContainer&>::value;
			static const bool Constructor_FileSystem = std::is_constructible<AssetType, IFileInterface&, const DirectorySearchRules&, const DependencyValidation&>::value;

			static const bool HasCompileProcessType = HasCompileProcessTypeHelper::value;
			static const bool HasChunkRequests = decltype(HasChunkRequestsHelper<AssetType>(0))::value;
		};

			///////////////////////////////////////////////////////////////////////////////////////////////////

		const ConfigFileContainer<InputStreamFormatter<utf8>>& GetConfigFileContainer(StringSection<ResChar> identifier);
		const ChunkFileContainer& GetChunkFileContainer(StringSection<ResChar> identifier);
		PtrToFuturePtr<ConfigFileContainer<InputStreamFormatter<utf8>>> GetConfigFileContainerFuture(StringSection<ResChar> identifier);
		PtrToFuturePtr<ChunkFileContainer> GetChunkFileContainerFuture(StringSection<ResChar> identifier);

        template <typename... Params> uint64_t BuildParamHash(Params... initialisers);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	#define ENABLE_IF(X) typename std::enable_if<X>::type* = nullptr

	//
	//		Auto construct to:
	//			(InputStreamFormatter<utf8>&, const DirectorySearchRules&, const DependencyValidation&)
	//
	template<typename AssetType, ENABLE_IF(Internal::AssetTraits<AssetType>::Constructor_Formatter)>
		std::unique_ptr<AssetType> AutoConstructAsset(StringSection<ResChar> initializer)
	{
		// First parameter should be the section of the input file to read (or just use the root of the file if it doesn't exist)
		// See also AutoConstructToFuture<> variation of this function
		const char* p = XlFindChar(initializer, ':');
		if (p) {
			char buffer[256];
			XlCopyString(buffer, MakeStringSection(initializer.begin(), p));
			const auto& container = Internal::GetConfigFileContainer(buffer);
			TRY {
				auto fmttr = container.GetFormatter(MakeStringSection((const utf8*)(p+1), (const utf8*)initializer.end()));
				return std::make_unique<AssetType>(
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
				return std::make_unique<AssetType>(
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
		std::unique_ptr<AssetType> AutoConstructAsset(const Blob& blob, const DependencyValidation& depVal, StringSection<ResChar> requestParameters = {})
	{
		TRY {
			auto container = ConfigFileContainer<>(blob, depVal);
			auto fmttr = requestParameters.IsEmpty() ? container.GetRootFormatter() : container.GetFormatter(requestParameters.Cast<utf8>());
			return std::make_unique<AssetType>(
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
		std::unique_ptr<AssetType> AutoConstructAsset(StringSection<ResChar> initializer)
	{
		// See also AutoConstructToFuture<> variation of this function
		const auto& container = Internal::GetChunkFileContainer(initializer);
		TRY {
			return std::make_unique<AssetType>(container);
		} CATCH (const Exceptions::ConstructionError& e) {
			Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
		} CATCH_END
	}

	template<typename AssetType, typename... Params, ENABLE_IF(Internal::AssetTraits<AssetType>::Constructor_ChunkFileContainer)>
		std::unique_ptr<AssetType> AutoConstructAsset(const Blob& blob, const DependencyValidation& depVal, StringSection<ResChar> requestParameters = {})
	{
		TRY {
			return std::make_unique<AssetType>(ChunkFileContainer(blob, depVal, requestParameters));
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
		std::unique_ptr<AssetType> AutoConstructAsset(StringSection<ResChar> initializer)
	{
		// See also AutoConstructToFuture<> variation of this function
		const auto& container = Internal::GetChunkFileContainer(initializer);
		TRY {
			auto chunks = container.ResolveRequests(MakeIteratorRange(AssetType::ChunkRequests));
			return std::make_unique<AssetType>(MakeIteratorRange(chunks), container.GetDependencyValidation());
		} CATCH (const Exceptions::ConstructionError& e) {
			Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
		} CATCH_END
	}

	template<typename AssetType, typename... Params, ENABLE_IF(Internal::AssetTraits<AssetType>::HasChunkRequests)>
		std::unique_ptr<AssetType> AutoConstructAsset(const Blob& blob, const DependencyValidation& depVal, StringSection<ResChar> requestParameters = {})
	{
		TRY {
			auto chunks = ChunkFileContainer(blob, depVal, requestParameters).ResolveRequests(MakeIteratorRange(AssetType::ChunkRequests));
			return std::make_unique<AssetType>(MakeIteratorRange(chunks), depVal);
		} CATCH (const Exceptions::ConstructionError& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	}

	template<typename AssetType, typename... Params, ENABLE_IF(Internal::AssetTraits<AssetType>::HasChunkRequests)>
		std::unique_ptr<AssetType> AutoConstructAsset(const IArtifactCollection& artifactCollection)
	{
		TRY {
			auto chunks = artifactCollection.ResolveRequests(MakeIteratorRange(AssetType::ChunkRequests));
			return std::make_unique<AssetType>(MakeIteratorRange(chunks), artifactCollection.GetDependencyValidation());
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
		std::unique_ptr<AssetType> AutoConstructAsset(StringSection<ResChar> initializer)
	{
		auto depVal = GetDepValSys().Make(initializer);
		TRY { 
			auto file = MainFileSystem::OpenFileInterface(initializer, "rb");
			return std::make_unique<AssetType>(
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
		std::unique_ptr<AssetType> AutoConstructAsset(StringSection<ResChar> initializer)
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
			return std::make_unique<AssetType>(
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
	template<typename AssetType, typename... Params, typename std::enable_if<std::is_constructible<AssetType, Params...>::value>::type* = nullptr>
		static std::unique_ptr<AssetType> AutoConstructAsset(Params... initialisers)
	{
		return std::make_unique<AssetType>(std::forward<Params>(initialisers)...);
	}

	#undef ENABLE_IF

}

