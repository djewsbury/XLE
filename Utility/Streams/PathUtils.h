// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../UTFUtils.h"
#include "../Detail/API.h"
#include "../StringUtils.h"
#include "../IteratorUtils.h"   // for IteratorRange
#include <string>
#include <vector>
#include <utility>

namespace Utility
{
	//////////////////////////////////////////////////////////////////////////////
	/// <summary>Defines some rules for working with filenames</summary>
	/// These rules define how a filesystem or some other system handles filenames.
	/// Most importantly, it defines if the filesystem is case sensitive or not.
	///
	/// Note that GetSeparator() returns the default separator to use when building
	/// a filename string (such as in SplitPath::Rebuild). However, when parsing filenames,
	/// we support either "/" or "\" as separators (eg, Windows style), regardless of
	/// the result of GetSeparator().
    class FilenameRules
    {
    public:
        template<typename CharType>
            CharType GetSeparator() const { return (CharType)_separator; }
        bool IsCaseSensitive() const { return _isCaseSensitive; }

        constexpr FilenameRules(char separator, bool isCaseSensitive)
            : _separator(separator), _isCaseSensitive(isCaseSensitive) {}
    protected:
        char _separator;
        bool _isCaseSensitive;
    };

    constexpr FilenameRules s_defaultFilenameRules('/', true);

	//////////////////////////////////////////////////////////////////////////////
	/// <summary>Split a filename into its component parts</summary>
	/// Separates a filename into stem, path, file, extension and parameters.
    /// Handy for separating out an individual part of a filename (such as just
    /// getting the extension)
    ///
    /// Note that this class does not keep a copy of the value passed in. If the
    /// input string is freed (or changes or becomes invalid), that the FileNameSplitter 
    /// will be invalidated. Be careful when using with volatile or temporary objects!
    ///
    /// It's done this way to minimize overhead when doing simple operations (like
    /// finding the extension).
    ///
    /// Note that network paths and path with protocols (eg, "file://.../") are not supported.
    ///
    /// <seealso cref="Utility::XlExtension"/>
    /// <seealso cref="Utility::XlDirname"/>
	template<typename CharType>
        class FileNameSplitter
	{
	public:
        using Section = StringSection<CharType>;

		Section		Stem() const					{ return _stem; }
		Section		Path() const					{ return _path; }
		Section		File() const					{ return _file; }
		Section		Extension() const				{ return !_extension.IsEmpty() ? Section(_extension._start+1, _extension._end) : Section(); }
		Section		ExtensionWithPeriod() const		{ return _extension; }
        Section		Parameters() const				{ return !_parameters.IsEmpty() ? Section(_parameters._start+1, _parameters._end) : Section(); }
        Section     ParametersWithDivider() const   { return _parameters; }

		Section     StemAndPath() const			    { return Section(_stem._start, _path._end); }
		Section	    FileAndExtension() const        { return Section(_file._start, _extension._end); }
        Section     AllExceptParameters() const     { return Section(_stem._start, _parameters._start); }
		Section		StemPathAndFilename() const	{ return Section(_stem._start, _file._end); }

        Section     FullFilename() const            { return _fullFilename; }

    	FileNameSplitter(const CharType rawString[]);
        FileNameSplitter(const std::basic_string<CharType>& rawString);
        FileNameSplitter(Section rawString);
	private:
		Section     _stem;
		Section     _path;
		Section     _file;
		Section     _extension;
		Section     _parameters;
        Section     _fullFilename;
	};

    template<typename CharType> FileNameSplitter<CharType> MakeFileNameSplitter(const CharType rawString[])                     { return FileNameSplitter<CharType>(rawString); }
    template<typename CharType, typename T, typename A> FileNameSplitter<CharType> MakeFileNameSplitter(const std::basic_string<CharType, T, A>& rawString)   { return FileNameSplitter<CharType>(MakeStringSection(rawString)); }
    template<typename CharType> FileNameSplitter<CharType> MakeFileNameSplitter(StringSection<CharType> rawString)              { return FileNameSplitter<CharType>(rawString); }

	//////////////////////////////////////////////////////////////////////////////
	/// <summary>Split a path into its component directories</summary>
	/// This is a starting point when simplifying a path, converting separators,
    /// or converting between absolute and relative filename forms.
    ///
    /// Note that this class does not keep a copy of the value passed in. If the
    /// input string is freed (or changes or becomes invalid), that the SplitPath 
    /// will be invalidated. Be careful when using with volatile or temporary objects!
    ///
	template<typename CharType=char>
        class SplitPath
	{
	public:
        using Section = StringSection<CharType>;
        using String = std::basic_string<CharType>;

		enum class SectionType { CurrentDir, BackOne, Name };

		unsigned    GetSectionCount() const;
		SectionType GetSectionType(unsigned index) const;
		Section     GetSection(unsigned index) const;
        Section     GetStem() const;
		SplitPath   Simplify() const;
        bool        BeginsWithSeparator() const     { return _beginsWithSeparator; }
		bool        EndsWithSeparator() const       { return _endsWithSeparator; }
        bool&       BeginsWithSeparator()           { return _beginsWithSeparator; }
		bool&       EndsWithSeparator()             { return _endsWithSeparator; }

        String      Rebuild(const FilenameRules& rules = s_defaultFilenameRules) const; 
        void        Rebuild(CharType dest[], size_t destCount, const FilenameRules& rules = s_defaultFilenameRules) const;

        IteratorRange<const Section*> GetSections() const { return MakeIteratorRange(_sections); }

        template <int Count>
            void Rebuild(CharType (&dest)[Count], const FilenameRules& rules = s_defaultFilenameRules) const 
            {
                Rebuild(dest, Count, rules);
            }

		explicit SplitPath(const String& path);
		explicit SplitPath(const CharType path[]);
        explicit SplitPath(Section path);
		explicit SplitPath(std::vector<Section>&& sections);
        SplitPath();
        
        SplitPath(SplitPath&& moveFrom) never_throws;
        SplitPath& operator=(SplitPath&& moveFrom) never_throws;
        ~SplitPath();
	private:
		std::vector<Section>	_sections;          // vector here means we need heap allocations (but it avoid imposing confusing limitations)
		bool                    _beginsWithSeparator;
        bool                    _endsWithSeparator;
        Section                 _stem;

        SplitPath(
            std::vector<Section>&& sections, 
            bool beginsWithSeparator, bool endsWithSeparator, 
            Section stem);
	};

	template<typename CharType> SplitPath<CharType> MakeSplitPath(const CharType rawString[]) { return SplitPath<CharType>(rawString); }
	template<typename CharType, typename T, typename A> SplitPath<CharType> MakeSplitPath(const std::basic_string<CharType, T, A>& rawString) { return SplitPath<CharType>(MakeStringSection(rawString)); }
	template<typename CharType> SplitPath<CharType> MakeSplitPath(StringSection<CharType> rawString) { return SplitPath<CharType>(rawString); }

    template<typename CharType>
    	std::basic_string<CharType>	MakeRelativePath(
            const SplitPath<CharType>& basePath, 
            const SplitPath<CharType>& destinationObject,
            const FilenameRules& rules = s_defaultFilenameRules);

    utf8 ConvertPathChar(utf8 input, const FilenameRules& rules = s_defaultFilenameRules);
    ucs2 ConvertPathChar(ucs2 input, const FilenameRules& rules = s_defaultFilenameRules);

	static const uint64_t s_FNV_init64 = 0xcbf29ce484222325ULL;
    template<typename CharType>
		uint64_t HashFilename(StringSection<CharType> filename, const FilenameRules& rules = s_defaultFilenameRules, uint64_t seed = s_FNV_init64);

	template<typename CharType>
		uint64_t HashFilenameAndPath(StringSection<CharType> filename, const FilenameRules& rules = s_defaultFilenameRules, uint64_t seed = s_FNV_init64);

    std::string ColouriseFilename(StringSection<> filename);

    namespace Legacy
    {
        XL_UTILITY_API void XlConcatPath(char* dst, int count, const char* a, const char* b, const char* bEnd);
        XL_UTILITY_API void XlConcatPath(ucs2* dst, int count, const ucs2* a, const ucs2* b, const ucs2* bEnd);

        XL_UTILITY_API template<typename CharType> const CharType* XlExtension(const CharType* path);
        XL_UTILITY_API void XlChopExtension(char* path);
        XL_UTILITY_API void XlDirname(char* dst, int count, const char* path);
        XL_UTILITY_API void XlDirname(ucs2* dst, int count, const ucs2* path);
        XL_UTILITY_API void XlBasename(char* dst, int count, const char* path);
        XL_UTILITY_API const char* XlBasename(const char* path);
    }
}
