// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "AssetTraits.h"
#include "../Utility/IteratorUtils.h"
#include <functional>
#include <memory>

namespace Assets
{
    class IFileInterface;
	using ArtifactReopenFunction = std::function<std::shared_ptr<IFileInterface>()>;

    class ArtifactRequest
    {
    public:
		const char*		_name;		// for debugging purposes, to make it easier to track requests
        uint64_t 		_chunkTypeCode;
        unsigned        _expectedVersion;
        
        enum class DataType
        {
            ReopenFunction, 
			Raw, BlockSerializer,
			SharedBlob,
			Filename
        };
        DataType        _dataType;
    };

    class ArtifactRequestResult
    {
    public:
        std::unique_ptr<uint8[], PODAlignedDeletor> _buffer;
        size_t                                      _bufferSize = 0;
		Blob										_sharedBlob;
		ArtifactReopenFunction						_reopenFunction;
		std::string 								_artifactFilename;
    };

    /// <summary>Utility for building asset objects that load from chunk files (sometimes asychronously)</summary>
    /// Some simple assets simply want to load some raw data from a chunk in a file, or
    /// perhaps from a few chunks in the same file. This is a base class to take away some
    /// of the leg-work involved in implementing that class.
    class ArtifactChunkContainer
    {
    public:
        const std::string& Filename() const						{ return _filename; }
		const DependencyValidation& GetDependencyValidation() const	{ return _validationCallback; }

		std::vector<ArtifactRequestResult> ResolveRequests(IteratorRange<const ArtifactRequest*> requests) const;
        std::vector<ArtifactRequestResult> ResolveRequests(IFileInterface& file, IteratorRange<const ArtifactRequest*> requests) const;

		std::shared_ptr<IFileInterface> OpenFile() const;

        ArtifactChunkContainer(std::shared_ptr<IFileSystem> fs, std::string assetTypeName, DependencyValidation depVal);
		ArtifactChunkContainer(const Blob& blob, const DependencyValidation& depVal, StringSection<>);
		ArtifactChunkContainer(std::shared_ptr<IFileSystem> fs, StringSection<> assetTypeName);      // note -- avoid using, because this will query the depVal for the given file
		ArtifactChunkContainer();
        ~ArtifactChunkContainer();

		ArtifactChunkContainer(const ArtifactChunkContainer&) = default;
		ArtifactChunkContainer& operator=(const ArtifactChunkContainer&) = default;
		ArtifactChunkContainer(ArtifactChunkContainer&&) never_throws = default;
		ArtifactChunkContainer& operator=(ArtifactChunkContainer&&) never_throws = default;
    private:
        rstring			_filename;
		std::shared_ptr<IFileSystem> _fs;
		Blob			_blob;
		DependencyValidation		_validationCallback;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal 
	{
		template <typename AssetType>
			class AssetTraits2_
		{
		private:
			template<typename T> static auto HasCompileProcessTypeHelper(int) -> std::is_integral<decltype(GetCompileProcessType(std::declval<T*>()))>;
			template<typename...> static auto HasCompileProcessTypeHelper(...) -> std::false_type;

			template<typename T> static auto HasChunkRequestsHelper(int) -> decltype(&T::ChunkRequests[0], std::true_type{});
			template<typename...> static auto HasChunkRequestsHelper(...) -> std::false_type;

		public:
			static const bool Constructor_Blob = std::is_constructible<AssetType, ::Assets::Blob&&, DependencyValidation&&, StringSection<>>::value;
			static const bool Constructor_ArtifactRequestResult = std::is_constructible<AssetType, IteratorRange<ArtifactRequestResult*>, DependencyValidation&&>::value;

			static const bool HasCompileProcessType = decltype(HasCompileProcessTypeHelper<AssetType>(0))::value;
			static const bool HasChunkRequests = decltype(HasChunkRequestsHelper<AssetType>(0))::value;
		};

		template<typename AssetType>
			using AssetTraits2 = AssetTraits2_<std::decay_t<RemoveSmartPtrType<AssetType>>>;

		// Note -- here's a useful pattern that can turn any expression in a SFINAE condition
		// Taken from stack overflow -- https://stackoverflow.com/questions/257288/is-it-possible-to-write-a-template-to-check-for-a-functions-existence
		// If the expression in the first decltype() is invalid, we will trigger SFINAE and fall back to std::false_type
		template<typename PromiseType>
			static auto HasConstructToPromiseFromBlob_Helper(int) -> decltype(PromisedTypeRemPtr<PromiseType>::ConstructToPromise(std::declval<PromiseType&&>(), std::declval<Blob&&>(), std::declval<DependencyValidation&&>(), std::declval<StringSection<>>()), std::true_type{});

		template<typename...>
			static auto HasConstructToPromiseFromBlob_Helper(...) -> std::false_type;

		template<typename PromiseType>
			struct HasConstructToPromiseFromBlob_ : decltype(HasConstructToPromiseFromBlob_Helper<PromiseType>(0)) {};

		template<typename PromiseType>
			using HasConstructToPromiseFromBlob = HasConstructToPromiseFromBlob_<PromiseType>;

        const ArtifactChunkContainer& GetChunkFileContainer(StringSection<> identifier);
		std::shared_future<std::shared_ptr<ArtifactChunkContainer>> GetChunkFileContainerFuture(StringSection<> identifier);
	}

    #define ENABLE_IF(...) typename std::enable_if_t<__VA_ARGS__>* = nullptr

	//
	//		Auto construct to:
	//			(const ArtifactChunkContainer&)
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
			return Internal::InvokeAssetConstructor<AssetType>(ArtifactChunkContainer(blob, depVal, requestParameters));
		} CATCH (const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	}

	template<
		typename Promise,
		ENABLE_IF(	Internal::AssetTraits2<Internal::PromisedTypeRemPtr<Promise>>::HasChunkRequests 
				&&  !Internal::AssetTraits2<Internal::PromisedTypeRemPtr<Promise>>::HasCompileProcessType)>
		void AutoConstructToPromiseOverride(Promise&& promise, StringSection<> initializer)
	{
		auto containerFuture = Internal::GetChunkFileContainerFuture(initializer);
		WhenAll(containerFuture).ThenConstructToPromise(
			std::move(promise),
			[](const std::shared_ptr<ArtifactChunkContainer>& container) {
				auto chunks = container->ResolveRequests(MakeIteratorRange(Internal::PromisedTypeRemPtr<Promise>::ChunkRequests));
				return Internal::InvokeAssetConstructor<Internal::PromisedType<Promise>>(MakeIteratorRange(chunks), container->GetDependencyValidation());
			});
	}

    #undef ENABLE_IF
}

