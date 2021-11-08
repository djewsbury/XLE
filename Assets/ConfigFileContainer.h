// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DepVal.h"
#include "AssetsCore.h"		// (for ResChar)
#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/StringFormat.h"
#include <memory>
#include <vector>

namespace Utility
{
    template<typename> class InputStreamFormatter;
}

namespace Assets
{
    /// <summary>Container file with with one child that is initialized via InputStreamFormatter</summary>
    ///
    /// Represents a file that contains a single serialized item. That item must be a type that
    /// can be deserialized with InputStreamFormatter.
    ///
    /// <example>
    ///     Consider a configuration object like:
    ///     <code>\code
    ///         class Config
    ///         {
    ///         public:
    ///             Config( InputStreamFormatter<utf8>& formatter,
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
    template<typename Formatter = InputStreamFormatter<utf8>>
        class ConfigFileContainer
    {
    public:
		Formatter GetRootFormatter() const;
		Formatter GetFormatter(StringSection<typename Formatter::value_type>) const;

		static std::unique_ptr<ConfigFileContainer> CreateNew(StringSection<ResChar> initialiser);

        ConfigFileContainer(StringSection<ResChar> initializer);
		ConfigFileContainer(const Blob& blob, const DependencyValidation& depVal, StringSection<ResChar> = {});
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
}

