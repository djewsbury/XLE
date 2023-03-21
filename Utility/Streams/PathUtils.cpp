// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PathUtils.h"
#include "../PtrUtils.h"
#include "../IteratorUtils.h"
#include "../MemoryUtils.h"
#include "../StringFormat.h"	// for StringSection<> streaming in ColouriseFilename
#include "../../Core/SelectConfiguration.h"
#include <utility>
#include <sstream>
#include <algorithm>
#include <assert.h>
#include <cctype>

namespace Utility
{
    namespace Legacy
    {
        template <class T>
        void concat_path(T* dst, int count, const T* a, const T* b, const T* bEnd)
        {
            // do simple concatenation first
            T tmp[MaxPath];
            if (a && a[0]) {
                XlCopyString(tmp, dimof(tmp), a);
                    // note -- there maybe be some problems with the following
                    //          lines with network paths... todo, more robust
                    //          implementation
                static const T slash[2] = { '/', '\0' };
                XlCatString(tmp, dimof(tmp), slash);
            }
            if (b) {
                XlCatNString(tmp, dimof(tmp), b, bEnd - b);
            }
            // XlNormalizePath(dst, count, tmp);        DavidJ -- this has a problem with network paths. \\NetworkServer\path\... -> /NetworkServer/path/... so skip this step (caller can always normalize afterwards)
            XlCopyString(dst, count, tmp);

            if (!dst[0]) {
                static const T dot[2] = { '.', '\0' };
                XlCopyString(dst, count, dot);
            }
        }

        void XlConcatPath(char* dst, int count, const char* a, const char* b, const char* bEnd)
        {
            concat_path<char>(dst, count, a, b, bEnd);
        }

        void XlConcatPath(ucs2* dst, int count, const ucs2* a, const ucs2* b, const ucs2* bEnd)
        {
            concat_path<ucs2>(dst, count, a, b, bEnd);
        }

        template<typename CharType>
            const CharType* XlExtension(const CharType* path)
        {
            auto len = XlStringSize(path);
            if (!len) return nullptr;
            const auto* p = &path[len-1];
            while (p >= path) {
                if (*p == '\\' || *p == '/') return nullptr;
                if (*p == '.') return p + 1;
                --p;
            }
            return nullptr;
        }

        template const utf8* XlExtension(const utf8* path);
        template const ucs2* XlExtension(const ucs2* path);
        template const ucs4* XlExtension(const ucs4* path);

        void XlChopExtension(char* path)
        {
            char* p = (char*)XlExtension(path);
            if (!p || !p[0])
                return;

            // p points to the first character of the extension, so move one character
            // backwards
            --p;
            *p = '\0';
        }

        template <class T>
        void get_dir_name(T* dst, int count, const T* path)
        {
            const T* p = path;
            const T* sep = 0;
            while (*p) {
                if ((*p == '/' || *p == '\\') && p[1]) {
                    sep = p;
                }
                ++p;
            }

            if (sep) {
                int n = (int)(sep - path)+1;
                if (n == 1) {
                    n = 2;  // for root(/) dir
                }
                if (count < n)
                    n = count;
                XlCopyNString(dst, count, path, n);
            } else {
                static const T dot[2] = { '.', '\0' };
                XlCopyString(dst, count, dot);
            }
        }

        void XlDirname(char* dst, int count, const char* path)
        {
            get_dir_name<char>(dst, count, path);
        }

        void XlDirname(ucs2* dst, int count, const ucs2* path)
        {
            get_dir_name<ucs2>(dst, count, path);
        }

        void XlBasename(char* dst, int count, const char* path)
        {
            const char* p = path;
            const char* sep = 0;
            while (*p) {
                if (*p == '/' || *p == '\\') {
                    sep = p;
                }
                ++p;
            }

            XlCopyString(dst, count, sep ? sep + 1 : path);
        }

        const char* XlBasename(const char* path)
        {
            const char* p = path;
            const char* sep = 0;
            while (*p) {
                if (*p == '/' || *p == '\\') {
                    sep = p;
                }
                ++p;
            }

            return sep ? sep + 1 : path;
        }
    }

    #define TC template<typename CharType>

    TC FileNameSplitter<CharType>::FileNameSplitter(Section rawString)
	{
        _fullFilename = rawString;

        const CharType seps[] = { (CharType)'\\', (CharType)'/' };
        const CharType sepsAndDot[] = { (CharType)'\\', (CharType)'/', (CharType)'.' };

        const auto* pathStart = rawString._start;

		// Since we use a colon for both the parameters divider and after the drive name on Windows,
		// there can be some ambiguity. We can solve that by putting some restrictons on how we use
		// drive names
		//  - drive names can't have dots or slashes
		//  - when ':' is used as a drive name separator, it must immediately be followed by a slash
		//		ie, "c:\something" style paths are supported, but "c:something" paths are not. The
		//		second form is a idiosyncracy of dos/windows, anyway, any probably not useful in our context
		auto firstColon = std::find(rawString._start, rawString._end, ':');
		if (firstColon < std::find_first_of(rawString._start, rawString._end, sepsAndDot, ArrayEnd(sepsAndDot))
			&& (firstColon+1) < rawString.end() && (*(firstColon+1) == '\\' || *(firstColon+1) == '/')) {
    		_drive = Section(rawString._start, firstColon+1);
            pathStart = firstColon+1;
        } else {
            _drive = Section(rawString._start, rawString._start);
        }

		auto lastSlash = FindLastOf(rawString._start, rawString._end, seps, ArrayEnd(seps));
		if (lastSlash == rawString._end) {
            _path = Section(pathStart, pathStart);
        } else {
            _path = Section(pathStart, lastSlash+1);
        }

		auto paramStart = std::find(_path._end, rawString._end, ':');
        auto endOfFile = FindLastOf(_path._end, paramStart, '.');
		_file = Section(_path._end, endOfFile);
        _extension = Section(endOfFile, paramStart);
        _parameters = Section(paramStart, rawString._end);
	}

    TC FileNameSplitter<CharType>::FileNameSplitter(const CharType rawString[])
        : FileNameSplitter(Section(rawString)) {}

    TC FileNameSplitter<CharType>::FileNameSplitter(const std::basic_string<CharType>& rawString)
        : FileNameSplitter(Section(rawString)) {}


	TC unsigned SplitPath<CharType>::GetSectionCount() const { return (unsigned)_sections.size(); }
    
	TC typename SplitPath<CharType>::SectionType AsSectionType(StringSection<CharType> e)
	{
		if ((e._end - e._start) == 1 && *e._start == '.')
            return SplitPath<CharType>::SectionType::CurrentDir;
		if ((e._end - e._start) == 2 && *e._start == '.' && *(e._start + 1) == '.')
            return SplitPath<CharType>::SectionType::BackOne;

        // extended series of dots (eg, "...") are considered just a name, and not special
        // commands
		return SplitPath<CharType>::SectionType::Name;
	}

	TC auto SplitPath<CharType>::GetSectionType(unsigned index) const -> SectionType
	{
		return AsSectionType(GetSection(index));
	}

	TC auto SplitPath<CharType>::GetSection(unsigned index) const -> Section
	{
		return _sections[index];
	}

    TC auto SplitPath<CharType>::GetDrive() const -> Section { return _drive; }

	TC auto SplitPath<CharType>::Simplify() const -> SplitPath
	{
		auto result = _sections;    // (copy)

		    // remove "." and ".." elements where possible
		for (auto i = result.begin(); i != result.end();) {
			auto e = *i;
			auto type = AsSectionType(e);
			if (type == SectionType::CurrentDir) {
				i = result.erase(i);
			} else if (type == SectionType::BackOne) {
				if (i != result.begin() && AsSectionType(*(i-1)) == SectionType::Name) {
					i = result.erase(i - 1, i + 1);
				} else ++i;		// can't go back too far (this means some ".." may be left)
			} else ++i;
		}

			// If we remove all path elements (ie, because of "." or ".." elements) then we must
			// supress the "endsWithSeparator" flag. Consider -- "somepath/../". If this becomes
			// "/", then the meaning has changed
		auto endsWithSeparator = _endsWithSeparator;
		if (result.empty()) endsWithSeparator = false;

		return SplitPath(std::move(result), _beginsWithSeparator, endsWithSeparator, _drive);
	}

	TC auto SplitPath<CharType>::Rebuild(const FilenameRules& rules) const -> String
	{
		std::basic_stringstream<CharType> stream;

        if (!_drive.IsEmpty()) {
            auto*s = _drive._start;
            while (s!=_drive._end) {
                auto chr = ConvertPathChar(*s++, rules);
                stream.write(&chr, 1);
            }
        }

        auto sep = rules.GetSeparator<CharType>();
        if (_beginsWithSeparator)
			stream.write(&sep, 1);

        if (!_sections.empty()) {
		    for (auto e=_sections.cbegin();;) {
			    auto*s = e->_start;
                while (s!=e->_end) {
                    auto chr = ConvertPathChar(*s++, rules);
                    stream.write(&chr, 1);
                }

                ++e;
                if (e==_sections.cend()) break;
                stream.write(&sep, 1);
		    }
        }

		if (_endsWithSeparator)
			stream.write(&sep, 1);
		return stream.str();
	}

    TC void SplitPath<CharType>::Rebuild(
        CharType dest[], size_t destCount, 
        const FilenameRules& rules) const
    {
        auto* i = dest;
        auto* iend = &dest[destCount];

        if (!_drive.IsEmpty()) {
            auto*s = _drive._start;
            while (s!=_drive._end && i!=iend) {
                if (s >= dest && s < iend) assert(s>=i);   // check for reading&writing from the same place
                *i++ = ConvertPathChar(*s++, rules);
            }
        }

        if (_beginsWithSeparator && i < iend)
			*i++ = rules.GetSeparator<CharType>();

        if (!_sections.empty()) {
            for (auto e=_sections.cbegin();;) {
                auto*s = e->_start;
                while (s!=e->_end && i!=iend) {
                    if (s >= dest && s < iend) assert(s>=i);   // check for reading&writing from the same place
                    *i++ = ConvertPathChar(*s++, rules);
                }

                ++e;
                if (e==_sections.cend() || i == iend) break;
                *i++ = rules.GetSeparator<CharType>();
		    }
        }

        if (_endsWithSeparator && i < iend)
			*i++ = rules.GetSeparator<CharType>();

        // force null terminator, even if it causes a truncate
        *std::min(i, iend-1) = '\0';
    }
    
    TC SplitPath<CharType>::SplitPath(Section rawString)
    {
        _endsWithSeparator = false;
        _beginsWithSeparator = false;

        const CharType seps[] = { (CharType)'\\', (CharType)'/' };
        const CharType sepsAndDot[] = { (CharType)'\\', (CharType)'/', (CharType)'.' };
		const auto* i = rawString._start;
        const auto* iend = rawString._end;

            // (not supporting "d:file.txt" type filenames currently! (ambiguity with parameters)
        auto firstSep = std::find_first_of(i, iend, sepsAndDot, ArrayEnd(sepsAndDot));
        auto firstColon = std::find(i, iend, ':');
        if (firstColon != iend && firstSep != iend && firstColon < firstSep) {
            _drive = Section(i, firstColon+1);
            i = firstColon+1;
        } else {
            _drive = Section(i, i);
        }

        auto* leadingSeps = FindFirstNotOf(i, iend, seps, ArrayEnd(seps));
        if (leadingSeps != i) {
            _beginsWithSeparator = true;
            i = leadingSeps;
        }

		if (i != iend) {
            for (;;) {
                auto* start = i;
                i = std::find_first_of(i, iend, seps, ArrayEnd(seps));
            
                _sections.push_back(Section(start, i));
                if (i == iend) break;

                i = FindFirstNotOf(i, iend, seps, ArrayEnd(seps));
                if (i == iend) { _endsWithSeparator = true; break; }
		    }
        }
    }

	TC SplitPath<CharType>::SplitPath(const String& input)
        : SplitPath(Section(input))
	{
	}

	TC SplitPath<CharType>::SplitPath(const CharType input[])
		: SplitPath(Section(input))
	{}

    TC SplitPath<CharType>::SplitPath() { _beginsWithSeparator = false; _endsWithSeparator = false; }

	TC SplitPath<CharType>::SplitPath(std::vector<Section>&& sections, 
        bool beginsWithSeparator, bool endsWithSeparator, 
        Section drive)
	: _sections(std::forward<std::vector<Section>>(sections))
	, _beginsWithSeparator(beginsWithSeparator)
    , _endsWithSeparator(endsWithSeparator)
    , _drive(drive)
	{}

	TC SplitPath<CharType>::SplitPath(std::vector<Section>&& sections)
	: _sections(std::move(sections))
	, _beginsWithSeparator(false)
    , _endsWithSeparator(false)
	{
	}

    TC SplitPath<CharType>::~SplitPath() {}

    TC SplitPath<CharType>::SplitPath(SplitPath&& moveFrom) never_throws
    : _sections(std::move(moveFrom._sections))
    , _beginsWithSeparator(moveFrom._beginsWithSeparator)
    , _endsWithSeparator(moveFrom._endsWithSeparator)
    , _drive(moveFrom._drive)
    {
    }

    TC auto SplitPath<CharType>::operator=(SplitPath&& moveFrom) never_throws -> SplitPath&
    {
        _sections = std::move(moveFrom._sections);
        _beginsWithSeparator = moveFrom._beginsWithSeparator;
        _endsWithSeparator = moveFrom._endsWithSeparator;
        _drive = moveFrom._drive;
        return *this;
    }

	

	TC static bool PathElementMatch(StringSection<CharType> lhs, StringSection<CharType> rhs, bool isCaseSensitive)
	{
        if (isCaseSensitive) return XlEqString(lhs, rhs);
        else return XlEqStringI(lhs, rhs);
	}

	TC std::basic_string<CharType> MakeRelativePath(
        const SplitPath<CharType>& iBasePath, 
        const SplitPath<CharType>& iDestinationObject,
        const FilenameRules& rules)
	{

        ////////////////////////////////////////////////////////////////////////////////
		    // Find initial parts of "basePath" that match "destinationObject"
            // Note that if there is a drive specified for "destinationObject",
            // then it must match the drive in "basePath". If there is a drive
            // in destinationObject, but no drive in basePath (or a different drive), 
            // then we can't make a relative path.
            // Alternatively, if there is a drive in basePath, and no drive in destinationObject,
            // then we'll assume that destinationObject is on the same drive
        auto destinationObject = iDestinationObject.Simplify();
        
        if (!iDestinationObject.GetDrive().IsEmpty()) {
            if (!XlEqStringI(iBasePath.GetDrive(), iDestinationObject.GetDrive()))
                return destinationObject.Rebuild(rules);
        }

		// Don't support paths that don't begin with a separator (like "C:folder\somefile")
		// unless both paths are like that
		if (iDestinationObject.BeginsWithSeparator() != iBasePath.BeginsWithSeparator()) {
			return destinationObject.Rebuild(rules);
		}

		auto basePath = iBasePath.Simplify();

        using SectionType = typename SplitPath<CharType>::SectionType;

		unsigned basePrefix = 0, destinationPrefix = 0;
		unsigned baseCount = basePath.GetSectionCount();
		unsigned destinationCount = destinationObject.GetSectionCount();
		for (; basePrefix < baseCount && destinationPrefix < destinationCount;) {
			if (basePath.GetSectionType(basePrefix) == SectionType::CurrentDir) {
				++basePrefix;
				continue;
			}
			if (destinationObject.GetSectionType(destinationPrefix) == SectionType::CurrentDir) {
				++destinationPrefix;
				continue;
			}

			if (	basePath.GetSectionType(basePrefix) == SectionType::BackOne
				||	destinationObject.GetSectionType(destinationPrefix) == SectionType::BackOne) {
				break;
			}

			if (PathElementMatch(basePath.GetSection(basePrefix), destinationObject.GetSection(destinationPrefix), rules.IsCaseSensitive())) {
				++basePrefix;
				++destinationPrefix;
				continue;
			}

			break;		// paths have diverged
		}

		if (basePrefix == 0 && !basePath.BeginsWithSeparator()) {
			// if there's no agreement (and no drive specified), we just have to assume that the destination path is absolute
			return destinationObject.Rebuild(rules);
		}

		// any remaining paths in "basePath" need to be translated into "../"
		signed backPaths = 0;
		auto baseIterator = basePrefix;
		for (; baseIterator < baseCount; ++baseIterator) {
			auto type = basePath.GetSectionType(baseIterator);
			if (type == SectionType::CurrentDir) {
			} else if (type == SectionType::BackOne) {
				--backPaths;
			} else {
				++backPaths;
			}
		}

        CharType dots[] = { (CharType)'.', (CharType)'.' };
        auto sep = rules.GetSeparator<CharType>();

		    // now we can just append the rest of "destinationObject"
		    //		... if backPaths is 0, we should prepend "./"
		    //		and backPaths shouldn't be less than zero if we properly simplified
		    //		the paths, and the base path is valid
            //  (note that we never need to write the drive out here)
		std::basic_stringstream<CharType> meld;
		bool pendingSlash = false;
		if (backPaths < 0) {
			UNREACHABLE();
		} else if (!backPaths) {
			// meld << "./";
		} else {
			meld.write(dots, 2);
			for (signed c = 1; c < backPaths; ++c) {
				meld.write(&sep, 1);
                meld.write(dots, 2);
            }
			pendingSlash = true;
		}

		auto destinationIterator = destinationPrefix;
		for (; destinationIterator < destinationCount; ++destinationIterator) {
			auto element = destinationObject.GetSection(destinationIterator);
			if (pendingSlash)
				meld.write(&sep, 1);
            if (rules.IsCaseSensitive()) {
			    meld.write(element._start, element._end - element._start);
            } else {
                    // convert to lower case for non-case sensitive file systems
                for (auto* i=element._start; i!=element._end; ++i) {
                    auto c = XlToLower(*i);
                    meld.write(&c, 1);
                }
            }
			pendingSlash = true;
		}

		if (destinationObject.EndsWithSeparator()) {
			if (!pendingSlash)
                meld.write(dots, 1);    // (creates "./" for empty paths)
            meld.write(&sep, 1);
		}

		return meld.str();
	}

        //  Selectively convert case to lower case on filesystems where it doesn't matter.
    utf8 ConvertPathChar(utf8 input, const FilenameRules& rules) { if (rules.IsCaseSensitive()) return input; return XlToLower(input); }
	utf16 ConvertPathChar(utf16 input, const FilenameRules& rules) { if (rules.IsCaseSensitive()) return input; return XlToLower(input); }
    ucs2 ConvertPathChar(ucs2 input, const FilenameRules& rules) { if (rules.IsCaseSensitive()) return input; return XlToLower(input); }

	static uint64_t FNVHash64(const void* start, const void* end, uint64_t hval)
	{
		auto* b = (unsigned char*)start;
		auto* e = (unsigned char*)end;

		while (b<e) {
			hval ^= (uint64_t)*b++;
			hval += 
				(hval << 1) + (hval << 4) + (hval << 5) +
				(hval << 7) + (hval << 8) + (hval << 40);
		}
		return hval;
	}

	static uint64_t FNVHash64(uint16_t chr, uint64_t hval)
	{
		// FNV always works with 8 bit values entering the machine
		//	-- so we have to split this value up into 2, and do it twice.
		// However, since we're using this for UTF16 filenames, the upper
		// 8 bits will very frequently be zero. Maybe it would be best to
		// actually skip the second hash part when that is the case?
		hval ^= (uint64_t)(chr & 0xff);
		hval +=
			(hval << 1) + (hval << 4) + (hval << 5) +
			(hval << 7) + (hval << 8) + (hval << 40);

		hval ^= (uint64_t)(chr >> 8);
		hval +=
			(hval << 1) + (hval << 4) + (hval << 5) +
			(hval << 7) + (hval << 8) + (hval << 40);
		return hval;
	}

	template<>
		uint64_t HashFilename(StringSection<utf16> filename, const FilenameRules& rules, uint64_t seed)
	{
		// Note -- see also an interesting hashing for filenames in the linux source /fs/ folder.
		//		it does a simple 32 bit hash, but does 4 characters at a time.
		//
		// We're going to apply "tolower" to the filename, as if were ucs2. Given
		// the format of utf16, this seems like it should be ok...?
		// However, just using basic "tolower" behaviour here... it's not clear
		// if it matches the way the underlying OS ignore case exactly. It should be
		// ok for ASCII chars; but some cases may not be accounted for correctly.
		//
		// To make this function simple and efficient, we'll use the FNV-1a algorithm.
		// See details here: http://isthe.com/chongo/tech/comp/fnv/
		//
		// This method is just well suited to our needs in this case
		if (rules.IsCaseSensitive())
			return FNVHash64(filename.begin(), filename.end(), seed);

		uint64_t hval = seed;
		for (auto i:filename)
			hval = FNVHash64((utf16)std::tolower(i), hval);		// note -- potentially non-ideal tolower usage here...?
		return hval;
	}

	template<>
		uint64_t HashFilename(StringSection<utf8> filename, const FilenameRules& rules, uint64_t seed)
	{
		// Implemented so we get the same hash for utf8 and utf16 versions
		// of the same string
		uint64_t hval = seed;
		if (rules.IsCaseSensitive()) {

			for (auto i = filename.begin(); i<filename.end();) {
				auto chr = (utf16)utf8_nextchar(i, filename.end());
				hval = FNVHash64(chr, hval);
			}

		} else {

			for (auto i = filename.begin(); i<filename.end();) {
				auto chr = (utf16)std::tolower(utf8_nextchar(i, filename.end()));
				hval = FNVHash64(chr, hval);
			}

		}
		return hval;
	}

	static inline bool IsSeparator(utf16 chr)	{ return chr == '/' || chr == '\\'; }
	static inline bool IsSeparator(utf8 chr)	{ return chr == '/' || chr == '\\'; }

	template<typename CharType> uint16_t NextChar(CharType const*& iterator, CharType const*);

	template<> uint16_t NextChar(utf8 const*& iterator, utf8 const* end) { return (uint16_t)utf8_nextchar(iterator, end); }
	template<> uint16_t NextChar(utf16 const*& iterator, utf16 const* end) { return (uint16_t)*iterator++; }

	template<bool CaseSensitive, typename CharType>
		uint64_t HashFilenameAndPath_Internal(StringSection<CharType> filename, const FilenameRules& rules, uint64_t seed)
	{
		if (filename.IsEmpty())
			return FNVHash64('\0', seed);	// note that we can't ignore the seed in the return value, because that might hold some importance to the caller

		// This is a special version of HashFilenameAndPath where we assume there may be path
		// separators in the filename. Whenever we find any sequence of either type of path
		// separator, we hash a single '/'

		uint64_t hval = seed; 

		uint64_t backpathQueue[64];
		unsigned backpathQueueCount = 0;

		// Special case some logic for paths that begin with a '/' -- this allows us
		// to handle an exception for strings that are only separators. That is the only
		// cases where we don't ignore a separator at the end of a string
		auto i = filename.begin();
		if (i != filename.end() && IsSeparator(*i)) {
			while (i < filename.end() && IsSeparator(*i)) ++i;	// skip over additionals
			hval = FNVHash64('/', hval);
		}

		backpathQueue[backpathQueueCount++] = hval;
		bool pendingSlash = false;

		for (; i!=filename.end();) {
			if (*i == '.') {
				if ((i+1) == filename.end()) {
					// ending in a segment that is just '.'. We ignore that final dot, it holds
					// no meaning
					return hval;
				} else if (IsSeparator(*(i+1))) {
					// This is "./". There are no cases where this has an impact, we should ignore it
					i += 2;
					while (i < filename.end() && IsSeparator(*i)) ++i;	// skip over additionals
					continue;
				} else if (*(i+1) == '.') {
					if (pendingSlash) {
						hval = FNVHash64('/', hval);
						pendingSlash = false;
					}

					if ((i+2) == filename.end()) {
						// ending in a back path
						if (backpathQueueCount < 2) {
							// entering into negative territory at the end (see below for how this is handled)
							hval = FNVHash64('.', backpathQueue[0]);
							hval = FNVHash64('.', hval);
							hval = FNVHash64('/', hval);
							return hval;
						}
						return backpathQueue[backpathQueueCount-2];
					} else if (IsSeparator(*(i+2))) {
						if (backpathQueueCount < 2) {
							// we're entering into negative territory. Nothing that we've read
							// so far actually matters (other than if we started with a separator)
							// We can handle this case recursively
							i += 2;
							while (i != filename.end() && IsSeparator(*i)) ++i;	// skip over additionals
							hval = FNVHash64('.', backpathQueue[0]);
							hval = FNVHash64('.', hval);
							hval = FNVHash64('/', hval);
							return HashFilenameAndPath(MakeStringSection(i, filename.end()), rules, hval);
						}
						hval = backpathQueue[backpathQueueCount-2];
						backpathQueueCount-=1;
						i += 2;
						while (i != filename.end() && IsSeparator(*i)) ++i;	// skip over additionals
						if (i != filename.end())	// note that we ignore slashes at the end of paths
							hval = FNVHash64('/', hval);
						continue;
					}
				}
			}

			if (pendingSlash) {
				hval = FNVHash64('/', hval);
				pendingSlash = false;
			}

			while (i != filename.end() && !IsSeparator(*i)) {
				auto nextChar = NextChar<CharType>(i, filename.end());
				if constexpr (!CaseSensitive)
					nextChar = std::tolower(nextChar);
				hval = FNVHash64(nextChar, hval);
			}

			if (i != filename.end()) {
				while (i != filename.end() && IsSeparator(*i)) ++i;	// skip over additionals
				assert(backpathQueueCount < dimof(backpathQueue));
				backpathQueue[backpathQueueCount++] = hval;
				pendingSlash = true;
			}
		}

		return hval;
	}

	template<>
		uint64_t HashFilenameAndPath(StringSection<utf16> filename, const FilenameRules& rules, uint64_t seed)
	{
		if (rules.IsCaseSensitive()) {
			return HashFilenameAndPath_Internal<true>(filename, rules, seed);
		} else {
			return HashFilenameAndPath_Internal<false>(filename, rules, seed);
		}
	}

	template<>
		uint64_t HashFilenameAndPath(StringSection<utf8> filename, const FilenameRules& rules, uint64_t seed)
	{
		if (rules.IsCaseSensitive()) {
			return HashFilenameAndPath_Internal<true>(filename, rules, seed);
		} else {
			return HashFilenameAndPath_Internal<false>(filename, rules, seed);
		}
	}

	std::string ColouriseFilename(StringSection<> filename)
	{
		auto split = MakeFileNameSplitter(filename);
		std::stringstream str;
		if (!split.DriveAndPath().IsEmpty()) {
			const bool gradualBrightnessChange = true;
			if (!gradualBrightnessChange) {
				str << "{color:9f9f9f}" << split.DriveAndPath();
			} else {
				auto splitPath = MakeSplitPath(split.DriveAndPath());
				if (splitPath.BeginsWithSeparator()) str << "/";
				for (unsigned c=0; c<splitPath.GetSectionCount(); ++c) {
					float alpha = c/float(splitPath.GetSectionCount());
					auto brightness = unsigned(0x5f * (1-alpha) + 0xcf * alpha) & 0xff;
					if (c != 0) str << "/";
					str << "{color:" << std::hex << brightness << brightness << brightness << std::dec << "}" << splitPath.GetSection(c);
				}
				if (splitPath.EndsWithSeparator()) str << "/";
			}
		}
		if (!split.File().IsEmpty())
			str << "{color:7f8fdf}" << split.File();
		if (!split.ExtensionWithPeriod().IsEmpty())
			str << "{color:df8f7f}" << split.ExtensionWithPeriod();
		if (!split.ParametersWithDivider().IsEmpty())
			str << "{color:7fdf8f}" << split.ParametersWithDivider();
		return str.str();
	}

    FilenameRules s_defaultFilenameRules('/', true);

    template class FileNameSplitter<utf8>;
	template class FileNameSplitter<utf16>;
    template class FileNameSplitter<ucs2>;
    template class SplitPath<utf8>;
	template class SplitPath<utf16>;
    template class SplitPath<ucs2>;
    template std::basic_string<utf8> MakeRelativePath(const SplitPath<utf8>&, const SplitPath<utf8>&, const FilenameRules&);
	template std::basic_string<utf16> MakeRelativePath(const SplitPath<utf16>&, const SplitPath<utf16>&, const FilenameRules&);
    template std::basic_string<ucs2> MakeRelativePath(const SplitPath<ucs2>&, const SplitPath<ucs2>&, const FilenameRules&);

}


