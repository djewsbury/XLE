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
#include "Continuation.h"
#include "../Formatters/TextFormatter.h"
#include "../Utility/StringFormat.h"
#include "../Utility/StringUtils.h"
#include "../Utility/Streams/SerializationUtils.h"		// (included to ensure that AssetMixinTraits::HasDeserializationOperatorFromFormatter resolves correctly)
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class DirectorySearchRules;
	DirectorySearchRules DefaultDirectorySearchRules(StringSection<ResChar>);

#if !defined(__CLR_VER)

    namespace Internal
    {
		const ConfigFileContainer<Formatters::TextInputFormatter<>>& GetConfigFileContainer(StringSection<> identifier);
		std::shared_future<std::shared_ptr<ConfigFileContainer<Formatters::TextInputFormatter<>>>> GetConfigFileContainerFuture(StringSection<> identifier);

		template<typename AssetType>
            static constexpr bool HasConstructor_Formatter = std::is_constructible<RemoveSmartPtrType<AssetType>, Formatters::TextInputFormatter<>&, const DirectorySearchRules&, const DependencyValidation&>::value;

		template<typename AssetType>
            static constexpr bool HasConstructor_SimpleFormatter = std::is_constructible<RemoveSmartPtrType<AssetType>, Formatters::TextInputFormatter<>&>::value;

		#define TEST_SUBST_MEMBER(Name, ...)																		\
			template<typename T> static constexpr auto Name##_(int) -> decltype(__VA_ARGS__, std::true_type{});		\
			template<typename...> static constexpr auto Name##_(...) -> std::false_type;							\
			static constexpr bool Name = decltype(Name##_<Type>(0))::value;											\
			/**/

		template<typename Type>
			struct AssetMixinTraits_
		{
			TEST_SUBST_MEMBER(HasDeserializeKey, std::declval<T&>().TryDeserializeKey(
				std::declval< Formatters::TextInputFormatter<char>& >(),
				std::declval< StringSection<> >()
				));

			TEST_SUBST_MEMBER(HasMergeInWithFilenameResolve, std::declval<T&>().MergeInWithFilenameResolve(
				std::declval< const T& >(),
				std::declval< const ::Assets::DirectorySearchRules& >()
				));

			TEST_SUBST_MEMBER(HasDeserializationOperatorFromFormatter, std::declval< Formatters::TextInputFormatter<char>& >() >> std::declval<T&>());
		};

		template<typename AssetType>
			using AssetMixinTraits = AssetMixinTraits_<std::decay_t<RemoveSmartPtrType<AssetType>>>;

		#undef TEST_SUBST_MEMBER

		inline std::vector<std::string> DeserializeInheritList(Formatters::TextInputFormatter<utf8>& formatter)
		{
			std::vector<std::string> result; StringSection<> value;
			if (!formatter.TryBeginElement())
				Throw(Formatters::FormatException("Malformed inherit list", formatter.GetLocation()));
			while (formatter.TryStringValue(value)) result.push_back(value.AsString());
			if (!formatter.TryEndElement())
				Throw(Formatters::FormatException("Malformed inherit list", formatter.GetLocation()));
			return result;
		}
		void SkipValueOrElement(Formatters::TextInputFormatter<utf8>&);

		template<typename AssetType>
			AssetType ConstructFromFormatterSyncHelper(
				const ConfigFileContainer<Formatters::TextInputFormatter<>>& container, StringSection<> internalSection,
				DirectorySearchRules&& searchRules, const DependencyValidation& depVal)
		{
			TRY {

				auto fmttr = internalSection.IsEmpty() ? container.GetRootFormatter() : container.GetFormatter(internalSection);

				if constexpr (
					Internal::AssetTraits<AssetType>::IsContextImbue
					&& Internal::HasConstructor_SimpleFormatter<typename Internal::AssetTraits<AssetType>::ContextImbueInternalType>) {

					using InternalAssetType = typename Internal::AssetTraits<AssetType>::ContextImbueInternalType;

					if constexpr (Internal::AssetMixinTraits<InternalAssetType>::HasDeserializeKey) {

						InternalAssetType asset = ::Assets::Internal::InvokeAssetConstructor<InternalAssetType>();
						InheritList inheritList;
						StringSection<> keyname;
						while (fmttr.TryKeyedItem(keyname))
							if (XlEqString(keyname, "Inherit")) {
								inheritList = Internal::DeserializeInheritList(fmttr);
							} else if (!Internal::MaybeDeref(asset).TryDeserializeKey(fmttr, keyname))
								Internal::SkipValueOrElement(fmttr);

						return { std::move(asset), std::move(searchRules), depVal, std::move(inheritList) };

					} else {

						return { Internal::InvokeAssetConstructor<InternalAssetType>(fmttr), std::move(searchRules), depVal, InheritList{} };

					}

				} else if constexpr (Internal::HasConstructor_Formatter<AssetType>) {

					return Internal::InvokeAssetConstructor<AssetType>( fmttr, std::move(searchRules), depVal);

				} else if constexpr (Internal::HasConstructor_SimpleFormatter<AssetType>) {

					return Internal::InvokeAssetConstructor<AssetType>(fmttr);

				} else if constexpr (Internal::AssetMixinTraits<AssetType>::HasDeserializationOperatorFromFormatter) {

					AssetType result;
					fmttr >> result;
					return result;

				} else {

					UNREACHABLE()

				}

			} CATCH (const Exceptions::ExceptionWithDepVal& e) {
				Throw(Exceptions::ConstructionError(e, depVal));
			} CATCH (const std::exception& e) {
				Throw(Exceptions::ConstructionError(e, depVal));
			} CATCH_END
		}

		template <typename AssetType>
			static constexpr bool ValidForConstructFromFormatterSyncHelper
				=  Internal::HasConstructor_Formatter<AssetType>
				|| Internal::HasConstructor_SimpleFormatter<AssetType>
				|| (Internal::AssetTraits<AssetType>::IsContextImbue && Internal::HasConstructor_SimpleFormatter<typename Internal::AssetTraits<AssetType>::ContextImbueInternalType>)
				|| Internal::AssetMixinTraits<AssetType>::HasDeserializationOperatorFromFormatter;
	}

	#define ENABLE_IF(...) typename std::enable_if_t<__VA_ARGS__>* = nullptr

	//
	//		Auto construct to:
	//			(Formatters::TextInputFormatter<>&, const DirectorySearchRules&, const DependencyValidation&)
	//			(Formatters::TextInputFormatter<>&)
	//			(Formatters::TextInputFormatter<>&) (with imbued context)
	//
	template<
		typename AssetType,
		ENABLE_IF(
			Internal::ValidForConstructFromFormatterSyncHelper<AssetType>
		)>
		AssetType AutoConstructAsset(StringSection<char> initializer)
	{
		// First parameter should be the section of the input file to read (or just use the root of the file if it doesn't exist)
		// See also AutoConstructToPromise<> variation of this function
		StringSection<> containerName, internalSection;
		if (const char* p = XlFindChar(initializer, ':')) {
			containerName = MakeStringSection(initializer.begin(), p);
			internalSection = MakeStringSection(p+1, initializer.end());
		} else
			containerName = initializer;
		
		const auto& container = Internal::GetConfigFileContainer(containerName);
		return Internal::ConstructFromFormatterSyncHelper<AssetType>(container, internalSection, DefaultDirectorySearchRules(containerName), container.GetDependencyValidation());
	}

	template<
		typename AssetType,
		ENABLE_IF(
			Internal::ValidForConstructFromFormatterSyncHelper<AssetType>
		)>
		AssetType AutoConstructAsset(const Blob& blob, DirectorySearchRules&& searchRules, DependencyValidation&& depVal, StringSection<> requestParameters = {})
	{
		TRY {
			auto container = ConfigFileContainer<>(blob, depVal);
			auto fmttr = requestParameters.IsEmpty() ? container.GetRootFormatter() : container.GetFormatter(requestParameters);
			return Internal::ConstructFromFormatterSyncHelper<AssetType>(container, requestParameters, std::move(searchRules), container.GetDependencyValidation());
		} CATCH(const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH(const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	}

	template<
		typename Promise,
		ENABLE_IF(
			Internal::ValidForConstructFromFormatterSyncHelper<Internal::PromisedType<Promise>>
		)>
		void AutoConstructToPromiseOverride_0(Promise&& promise, StringSection<> initializer)
	{
		// Note that this free function has to have a lower priority, or it just catches everything
		// In particular, it can hide the mechanism for invoking compiles

		std::string containerName, internalSection;
		if (const char* p = XlFindChar(initializer, ':')) {
			containerName = MakeStringSection(initializer.begin(), p).AsString();
			internalSection = MakeStringSection(p+1, initializer.end()).AsString();
		} else
			containerName = initializer.AsString();

		WhenAll(Internal::GetConfigFileContainerFuture(containerName)).ThenConstructToPromise(
			std::move(promise),
			[containerName, internalSection](const auto& container) {
				return Internal::ConstructFromFormatterSyncHelper<Internal::PromisedType<Promise>>(*container, internalSection, DefaultDirectorySearchRules(containerName), container->GetDependencyValidation());
			});
	}

	#undef ENABLE_IF

#endif

}

