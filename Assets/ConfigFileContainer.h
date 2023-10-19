// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DepVal.h"
#include "AssetsCore.h"
#include "AssetTraits.h"
#include "AssetUtils.h"		// for DirectorySearchRules
#include "../Formatters/TextFormatter.h"
#include "../Utility/StringFormat.h"
#include "../Utility/StringUtils.h"
#include <memory>
#include <vector>

namespace std { template<typename T> class shared_future; }

namespace Assets
{
    /// <summary>Container file with with one child that is initialized via TextInputFormatter</summary>
    ///
    /// Represents a file that contains a single serialized item. That item must be a type that
    /// can be deserialized with TextInputFormatter.
    ///
    /// <example>
    ///     Consider a configuration object like:
    ///     <code>\code
    ///         class Config
    ///         {
    ///         public:
    ///             Config( TextInputFormatter<>& formatter,
    ///                     const ::Assets::DirectorySearchRules&);
    ///             ~Config();
    ///         };
    ///     \endcode</code>
    ///
    ///     This might contain some configuration options, maybe some simple members or maybe
    ///     even some complex members.
    ///
    ///     Sometimes we might want to store a configuration settings like this in it's own
    ///     individual file. Other times, we might want to store it within a larger file, just
    ///     as part of heirarchy of serialized objects.
    ///
    ///     Because the object is deserialized directly from the formatter, we have the flexibility
    ///     to do that.
    ///
    ///     When we want that object to exist on it's own, in an individual file, we can use
    ///     ConfigFileContainer<Config>. With a ConfigFileContainer, it can be considered a
    ///     fully functional asset, with a dependency validation, relative path rules and
    ///     reporting correctly to the InvalidAssetManager.
    /// </example>
    template<typename Formatter = Formatters::TextInputFormatter<>>
        class ConfigFileContainer
    {
    public:
		Formatter GetRootFormatter() const;
		Formatter GetFormatter(StringSection<typename Formatter::value_type>) const;

		static std::unique_ptr<ConfigFileContainer> CreateNew(StringSection<> initialiser);

        ConfigFileContainer(StringSection<> initializer);
		ConfigFileContainer(const Blob& blob, const DependencyValidation& depVal, StringSection<> = {});
        ~ConfigFileContainer();

        const DependencyValidation& GetDependencyValidation() const   { return _validationCallback; }
    protected:
		Blob _fileData; 
		DependencyValidation _validationCallback;
    };

    template<typename CharType>
        class TextChunk
    {
    public:
        StringSection<CharType> _type, _name, _content;

        TextChunk(StringSection<CharType> type, StringSection<CharType> name, StringSection<CharType> content)
            : _type(type), _name(name), _content(content) {}
    };

    template<typename CharType>
        std::vector<TextChunk<CharType>> ReadCompoundTextDocument(StringSection<CharType> doc);

    void CleanupConfigFileGlobals();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class DirectorySearchRules;
	DirectorySearchRules DefaultDirectorySearchRules(StringSection<ResChar>);

#if !defined(__CLR_VER)

    namespace Internal
    {
		const ConfigFileContainer<Formatters::TextInputFormatter<>>& GetConfigFileContainer(StringSection<> identifier);
		std::shared_future<std::shared_ptr<ConfigFileContainer<Formatters::TextInputFormatter<>>>> GetConfigFileContainerFuture(StringSection<> identifier);

		template<typename AssetType>
            static const bool HasConstructor_Formatter = std::is_constructible<RemoveSmartPtrType<AssetType>, Formatters::TextInputFormatter<>&, const DirectorySearchRules&, const DependencyValidation&>::value;

		template<typename AssetType>
            static const bool HasConstructor_SimpleFormatter = std::is_constructible<RemoveSmartPtrType<AssetType>, Formatters::TextInputFormatter<>&>::value;
    }

    #define ENABLE_IF(...) typename std::enable_if_t<__VA_ARGS__>* = nullptr

	//
	//		Auto construct to:
	//			(Formatters::TextInputFormatter<>&, const DirectorySearchRules&, const DependencyValidation&)
	//
	template<typename AssetType, ENABLE_IF(Internal::HasConstructor_Formatter<AssetType>)>
		AssetType AutoConstructAsset(StringSection<char> initializer)
	{
		// First parameter should be the section of the input file to read (or just use the root of the file if it doesn't exist)
		// See also AutoConstructToPromise<> variation of this function
		const char* p = XlFindChar(initializer, ':');
		if (p) {
			char buffer[256];
			XlCopyString(buffer, MakeStringSection(initializer.begin(), p));
			const auto& container = Internal::GetConfigFileContainer(buffer);
			TRY {
				auto fmttr = container.GetFormatter(MakeStringSection(p+1, initializer.end()));
				return Internal::InvokeAssetConstructor<AssetType>(
					fmttr,
					DefaultDirectorySearchRules(buffer),
					container.GetDependencyValidation());
			} CATCH (const Exceptions::ExceptionWithDepVal& e) {
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
			} CATCH (const Exceptions::ExceptionWithDepVal& e) {
				Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
			} CATCH (const std::exception& e) {
				Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
			} CATCH_END
		}
	}

	//
	//		Auto construct to:
	//			(Formatters::TextInputFormatter<>&)
	//
	template<typename AssetType, ENABLE_IF(Internal::HasConstructor_SimpleFormatter<AssetType>)>
		AssetType AutoConstructAsset(StringSection<char> initializer)
	{
		// First parameter should be the section of the input file to read (or just use the root of the file if it doesn't exist)
		// See also AutoConstructToPromise<> variation of this function
		const char* p = XlFindChar(initializer, ':');
		if (p) {
			char buffer[256];
			XlCopyString(buffer, MakeStringSection(initializer.begin(), p));
			const auto& container = Internal::GetConfigFileContainer(buffer);
			TRY {
				auto fmttr = container.GetFormatter(MakeStringSection(p+1, initializer.end()));
				return Internal::InvokeAssetConstructor<AssetType>(fmttr);
			} CATCH (const Exceptions::ExceptionWithDepVal& e) {
				Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
			} CATCH (const std::exception& e) {
				Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
			} CATCH_END
		} else {
			const auto& container = Internal::GetConfigFileContainer(initializer);
			TRY { 
				auto fmttr = container.GetRootFormatter();
				return Internal::InvokeAssetConstructor<AssetType>(fmttr);
			} CATCH (const Exceptions::ExceptionWithDepVal& e) {
				Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
			} CATCH (const std::exception& e) {
				Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
			} CATCH_END
		}
	}

	template<typename AssetType, ENABLE_IF(Internal::HasConstructor_Formatter<AssetType>)>
		AssetType AutoConstructAsset(const Blob& blob, const DependencyValidation& depVal, StringSection<> requestParameters = {})
	{
		TRY {
			auto container = ConfigFileContainer<>(blob, depVal);
			auto fmttr = requestParameters.IsEmpty() ? container.GetRootFormatter() : container.GetFormatter(requestParameters);
			return Internal::InvokeAssetConstructor<AssetType>(
				fmttr,
				DirectorySearchRules{},
				container.GetDependencyValidation());
		} CATCH(const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH(const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	}

	template<
		typename Promise,
		ENABLE_IF(	Internal::AssetTraits<Internal::PromisedType<Promise>>::Constructor_Formatter
					&& !std::is_same_v<std::decay_t<Internal::PromisedTypeRemPtr<Promise>>, ConfigFileContainer<>>)>
		void AutoConstructToPromiseOverride(Promise&& promise, StringSection<> initializer)
	{
		const char* p = XlFindChar(initializer, ':');
		if (p) {
			std::string containerName = MakeStringSection(initializer.begin(), p).AsString();
			std::string sectionName = MakeStringSection((const utf8*)(p+1), (const utf8*)initializer.end()).AsString();
			auto containerFuture = Internal::GetConfigFileContainerFuture(MakeStringSection(containerName));
			WhenAll(containerFuture).ThenConstructToPromise(
				std::move(promise),
				[containerName, sectionName](const std::shared_ptr<ConfigFileContainer<>>& container) {
					auto fmttr = container->GetFormatter(sectionName);
					return Internal::InvokeAssetConstructor<Internal::PromisedType<Promise>>(
						fmttr, 
						DefaultDirectorySearchRules(containerName),
						container->GetDependencyValidation());
				});
		} else {
			std::string containerName = initializer.AsString();
			auto containerFuture = Internal::GetConfigFileContainerFuture(MakeStringSection(containerName));
			WhenAll(containerFuture).ThenConstructToPromise(
				std::move(promise),
				[containerName](const std::shared_ptr<ConfigFileContainer<>>& container) {
					auto fmttr = container->GetRootFormatter();
					return Internal::InvokeAssetConstructor<Internal::PromisedType<Promise>>(
						fmttr, 
						DefaultDirectorySearchRules(containerName),
						container->GetDependencyValidation());
				});
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
		} CATCH(const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH(const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	}

	#undef ENABLE_IF

#endif

}

