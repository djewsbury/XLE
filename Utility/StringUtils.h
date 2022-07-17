// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Detail/API.h"
#include "UTFUtils.h"
#include "PtrUtils.h"	// for AsPointer
#include "Optional.h"
#include <string>
#include <cstring>
#include <algorithm>
#include <assert.h>
#include <cstddef>

#if REDIRECT_CLIB_WITH_PREPROCESSOR
    #include <stdlib.h>     // (this must be pulled in, so we can replace _itoa_s, etc)
#endif

namespace Utility
{
    // naming convention
    //  size : byte unit
    //  length : array size based on each type without terminating 0
    //  count, dim : length + 1 (for terminating 0)

        ////////////   S T R I N G   T O O L S   ////////////
            // (note that counts are array length in number of chars/ucs2s/ucs4s/utf16s etc)
    
        ////////////   U T F 8   O V E R R I D E S   ////////////
    XL_UTILITY_API size_t   XlStringSize        (const utf8* str);
    XL_UTILITY_API size_t   XlGlyphCount        (const utf8* str);
    XL_UTILITY_API void     XlCopyString        (utf8* dst, size_t size, const utf8* src);
    XL_UTILITY_API void     XlCopyNString       (utf8* dst, size_t count, const utf8*src, size_t length);
	XL_UTILITY_API void     XlCatString			(utf8* dst, size_t size, const utf8* src);
    XL_UTILITY_API void     XlCatNString        (utf8* dst, size_t size, const utf8* src, size_t length);
    XL_UTILITY_API int      XlComparePrefix     (const utf8* x, const utf8* y, size_t size);
    XL_UTILITY_API int      XlComparePrefixI    (const utf8* x, const utf8* y, size_t size);
    XL_UTILITY_API int      XlCompareString     (const utf8* x, const utf8* y);
    XL_UTILITY_API int      XlCompareStringI    (const utf8* x, const utf8* y);

		////////////   U T F 1 6   O V E R R I D E S   ////////////
	XL_UTILITY_API void     XlCopyString(utf16* dst, size_t size, const utf16* src);
	XL_UTILITY_API void     XlCopyNString(utf16* dst, size_t count, const utf16*src, size_t length);
    XL_UTILITY_API size_t   XlStringSize        (const utf16* str);
    XL_UTILITY_API size_t   XlGlyphCount        (const utf16* str);
    XL_UTILITY_API void     XlCopyString        (utf16* dst, size_t size, const utf16* src);
    XL_UTILITY_API void     XlCopyNString       (utf16* dst, size_t count, const utf16*src, size_t length);

        ////////////   U C S 2   O V E R R I D E S   ////////////
    XL_UTILITY_API size_t   XlStringSize        (const ucs2* str);
    XL_UTILITY_API size_t   XlGlyphCount        (const ucs2* str);
    XL_UTILITY_API void     XlCopyString        (ucs2* dst, size_t size, const ucs2* src);
    XL_UTILITY_API void     XlCopyNString       (ucs2* dst, size_t count, const ucs2*src, size_t length);
    XL_UTILITY_API void     XlCatString         (ucs2* dst, size_t size, const ucs2* src);
    XL_UTILITY_API void     XlCatNString        (ucs2* dst, size_t size, const ucs2* src, size_t length);
    XL_UTILITY_API int      XlComparePrefix     (const ucs2* x, const ucs2* y, size_t len);
    XL_UTILITY_API int      XlComparePrefixI    (const ucs2* x, const ucs2* y, size_t len);
    XL_UTILITY_API size_t   XlCompareString     (const ucs2* x, const ucs2* y);
    XL_UTILITY_API size_t   XlCompareStringI    (const ucs2* x, const ucs2* y);

        ////////////   U C S 4   O V E R R I D E S   ////////////
    XL_UTILITY_API size_t   XlStringSize        (const ucs4* str);
    XL_UTILITY_API size_t   XlStringSizeSafe    (const ucs4* str, const ucs4* end);
    XL_UTILITY_API size_t   XlGlyphCount        (const ucs4* str);
    XL_UTILITY_API void     XlCopyString        (ucs4* dst, size_t count, const ucs4* src);
    XL_UTILITY_API void     XlCopyNString       (ucs4* dst, size_t count, const ucs4*src, size_t length);
    XL_UTILITY_API void     XlCatString         (ucs4* dst, size_t count, const ucs4* src);
    XL_UTILITY_API int      XlComparePrefix     (const ucs4* x, const ucs4* y, size_t len);
    XL_UTILITY_API int      XlComparePrefixI    (const ucs4* x, const ucs4* y, size_t len);
    XL_UTILITY_API int      XlCompareString     (const ucs4* x, const ucs4* y);
    XL_UTILITY_API int      XlCompareStringI    (const ucs4* x, const ucs4* y);

        ////////////   w c h a r _ t   O V E R R I D E S   ////////////
    XL_UTILITY_API size_t   XlStringSize        (const wchar_t* str);
    XL_UTILITY_API size_t   XlGlyphCount        (const wchar_t* str);
    XL_UTILITY_API void     XlCopyString        (wchar_t* dst, size_t size, const wchar_t* src);
    XL_UTILITY_API void     XlCopyNString       (wchar_t* dst, size_t count, const wchar_t*src, size_t length);

    XL_UTILITY_API void     XlCopyString_SafeUtf       (utf8* dst, size_t size, const utf8* src);
    XL_UTILITY_API void     XlCopyString_SafeUtfN      (utf8* dst, size_t size, const utf8* src, const uint32_t numSeq);
    XL_UTILITY_API void     XlCatSafeUtf        (utf8* dst, size_t size, const utf8* src);


    template <typename CharType>
        const CharType* XlStringEnd(const CharType nullTermStr[])
            { return &nullTermStr[XlStringSize(nullTermStr)]; }

    template <typename CharType>
        CharType* XlStringEnd(CharType nullTermStr[])
            { return &nullTermStr[XlStringSize(nullTermStr)]; }

        ////////////   S T R I N G   S E C T I O N   ////////////

    /// <summary>Pointers to the start and end of a string</summary>
    /// This object points into the interior of another object, identifying
    /// the start and end of a string.
    ///
    /// This is a 3rd common string representation:
    ///     * c-style char pointer
    ///     * stl-style std::string
    ///     * begin/end string "section"
    ///
    /// This useful for separating a part of a large string, or while serializing
    /// from an text file (when we want to identify an interior string without
    /// requiring an extra allocation).
    template<typename CharType=char>
        class StringSection
    {
    public:
        const CharType* _start;
        const CharType* _end;
        using value_type = CharType;

        size_t Length() const                           { return size_t(_end - _start); }
        bool IsEmpty() const                            { return _end <= _start; }
        std::basic_string<CharType> AsString() const    { return std::basic_string<CharType>(_start, _end); }

		template<typename OtherCharType>
			StringSection<OtherCharType> Cast() const { return StringSection<OtherCharType>((const OtherCharType*)_start, (const OtherCharType*)_end); }

        const CharType* begin() const   { return _start; }
        const CharType* end() const     { return _end; }
		size_t size() const				{ return Length(); }

        const CharType& operator[](size_t index) const { assert(index < Length()); return _start[index]; }

        StringSection(const CharType* start, const CharType* end) : _start(start), _end(end) {}
        StringSection() : _start(nullptr), _end(nullptr) {}
        StringSection(const CharType* nullTerm) : _start(nullTerm), _end(XlStringEnd(_start)) {}
        StringSection(std::nullptr_t) = delete;      // prevent construction from nullptr constant (tends to be a common error)
        
		template<typename CT, typename A>
			StringSection(const std::basic_string<CharType, CT, A>& str) : _start(AsPointer(str.cbegin())), _end(AsPointer(str.cend())) {}
    };

    template<typename Iterator>
        inline auto MakeStringSection(Iterator start, Iterator end)
            -> StringSection<typename std::decay<decltype(*AsPointer(std::declval<Iterator>()))>::type>
        {
            using CharType = typename std::decay<decltype(*AsPointer(std::declval<Iterator>()))>::type;
            return StringSection<CharType>(AsPointer(start), AsPointer(end));
        }

    template<typename CharType>
        inline StringSection<CharType> MakeStringSection(const CharType* nullTerm)
        {
            return StringSection<CharType>(nullTerm);
        }

    template<typename CharType, typename CT, typename A>
        inline StringSection<CharType> MakeStringSection(const std::basic_string<CharType, CT, A>& str)
        {
            return StringSection<CharType>(AsPointer(str.cbegin()), AsPointer(str.cend()));
        }

        ////////////   S T R I N G   S E A R C H I N G   ////////////
    XL_UTILITY_API const char*  XlFindChar          (const char* s, const char ch);
    XL_UTILITY_API char*        XlFindChar          (char* s, const char ch);
	XL_UTILITY_API const char*  XlFindChar          (StringSection<char> s, char ch);
    XL_UTILITY_API const char*  XlFindAnyChar       (const char s[], const char ch[]);
    XL_UTILITY_API char*        XlFindAnyChar       (char s[], const char delims[]);
    XL_UTILITY_API const char*  XlFindNot           (const char s[], const char ch[]);
    XL_UTILITY_API char*        XlFindNot           (char s[], const char delims[]);
    XL_UTILITY_API const char*  XlFindCharReverse   (const char* s, char ch);
    XL_UTILITY_API const char*  XlFindString        (const char* s, const char* x);
    XL_UTILITY_API char*        XlFindString        (char* s, const char* x);
    XL_UTILITY_API const char*  XlFindStringI       (const char* s, const char* x);
	XL_UTILITY_API const char*  XlFindString        (StringSection<char> s, StringSection<char> x);
	XL_UTILITY_API const char*  XlFindStringI       (StringSection<char> s, StringSection<char> x);
    XL_UTILITY_API const char*  XlFindStringSafe    (const char* s, const char* x, size_t size);
    XL_UTILITY_API const char*  XlReplaceString     (char* dst, size_t size, const char* src, const char* strOld, const char* strNew);

        ////////////   T O K E N I Z E   ////////////
    XL_UTILITY_API size_t   XlTokenizeString    (char* buf, size_t count, const char* delimiters, char** tokens, size_t numMaxToken);
    XL_UTILITY_API char*    XlStrTok            (char* token, const char* delimit, char** context);
    XL_UTILITY_API bool     XlMatchWildcard     (const char* str, const char* pat, bool nocase = true);

        ////////////   C O N V E R S I O N S   ////////////
    XL_UTILITY_API int      XlExtractInt        (const char* buf, int* arr, int length);
    XL_UTILITY_API bool     XlSafeAtoi          (const char* str, int* n);
    XL_UTILITY_API bool     XlSafeAtoi64        (const char* str, int64* n);
    XL_UTILITY_API bool     XlSafeAtoui64       (const char* str, uint64_t* n);

    XL_UTILITY_API void     XlCompactString     (char* dst, size_t size, const char* src);
    XL_UTILITY_API const char*  XlLowerCase     (char* str);
    XL_UTILITY_API const char*  XlUpperCase     (char* str);

    XL_UTILITY_API const ucs4* XlFindString      (const ucs4* s, const ucs4* x);
    XL_UTILITY_API const ucs4* XlFindStringI     (const ucs4* s, const ucs4* x);
    XL_UTILITY_API const ucs4* XlFindStringSafe  (const ucs4* s, const ucs4* x, size_t len);

    XL_UTILITY_API void     XlCompactString     (ucs4* dst, size_t size, const ucs4* src);        // add - johnryou (compare two string ignoring whitespace characters)
    XL_UTILITY_API const ucs4* XlLowerCase   (ucs4* str);
    XL_UTILITY_API const ucs4* XlUpperCase   (ucs4* str);

            // url encoded -> non encoded
    XL_UTILITY_API size_t   XlDecodeUrl(char* dst, size_t count, const char* encoded);

        ////////////   U T F   H E L P E R S   ////////////
    #define UTF8_BUFSIZE(count)         ((count) * 4)       // this covers current valid unicode range.
    #define UTF16_BUFSIZE(count)        ((count) * 4)       // in bytes
    #define WCHAR_BUFSIZE(count)        (UTF16_BUFSIZE(count) / sizeof(wchar_t))

    XL_UTILITY_API bool     XlIsValidAscii(const char* str);
    XL_UTILITY_API bool     XlIsValidUtf8(const utf8* str, size_t count = -1);
    XL_UTILITY_API bool     XlHasUtf8Bom(const utf8* str);
    XL_UTILITY_API size_t   XlGetOffset(const char* str, size_t index);              // return byte offset for index's character.
    inline int              XlCountUtfSequence(uint8_t c);

        ////////////   C H A R A C T E R   C L A S S I F I C A T I O N   A N D   C O N V E R S I O N   ////////////
    XL_UTILITY_API bool     XlIsAlnum(char c);
    XL_UTILITY_API bool     XlIsEngAlpha(char c);
    XL_UTILITY_API bool     XlIsAlNumSpace(char c);

    XL_UTILITY_API bool     XlIsDigit(char c);
    XL_UTILITY_API bool     XlIsDigit(utf8 c);
    XL_UTILITY_API bool     XlIsDigit(ucs4 c);

    XL_UTILITY_API bool     XlIsHex(char c);
    XL_UTILITY_API bool     XlIsLower(char c);
    XL_UTILITY_API bool     XlIsPrint(char c);
    XL_UTILITY_API bool     XlIsSpace(char c);
    XL_UTILITY_API bool     XlIsUpper(char c);

    XL_UTILITY_API int      XlFromHex(char c);
    XL_UTILITY_API char     XlToHex(int n);

    XL_UTILITY_API char     XlToLower(char c);
    XL_UTILITY_API char     XlToUpper(char c);
    XL_UTILITY_API wchar_t  XlToLower(wchar_t c);
    XL_UTILITY_API wchar_t  XlToUpper(wchar_t c);

    XL_UTILITY_API utf16    XlToLower(utf16 c);
    XL_UTILITY_API utf16    XlToUpper(utf16 c);
    
    XL_UTILITY_API ucs2     XlToLower(ucs2 c);
    XL_UTILITY_API ucs2     XlToUpper(ucs2 c);

    XL_UTILITY_API bool     XlIsAlpha(ucs4 c);
    XL_UTILITY_API bool     XlIsEngAlpha(ucs4 c);
    XL_UTILITY_API bool     XlIsSpace(ucs4 c);
    XL_UTILITY_API bool     XlIsUpper(ucs4 c);
    XL_UTILITY_API bool     XlIsLower(ucs4 c);

    XL_UTILITY_API ucs4     XlToLower(ucs4 c);
    XL_UTILITY_API ucs4     XlToUpper(ucs4 c);

    XL_UTILITY_API bool     XlIsUpper(ucs4 c);
    XL_UTILITY_API bool     XlIsLower(ucs4 c);

    // <string <==> numeric> conversion
    XL_UTILITY_API bool     XlAtoBool(const char* str, const char** end_ptr = 0);
    XL_UTILITY_API int32    XlAtoI32 (const char* str, const char** end_ptr = 0, int radix = 10);
    XL_UTILITY_API int64    XlAtoI64 (const char* str, const char** end_ptr = 0, int radix = 10);
    XL_UTILITY_API uint32_t   XlAtoUI32(const char* str, const char** end_ptr = 0, int radix = 10);
    XL_UTILITY_API uint64_t   XlAtoUI64(const char* str, const char** end_ptr = 0, int radix = 10);
    XL_UTILITY_API f32      XlAtoF32 (const char* str, const char** end_ptr = 0);
    XL_UTILITY_API f64      XlAtoF64 (const char* str, const char** end_ptr = 0);

    XL_UTILITY_API char*    XlI32toA(int32 value, char* buffer, size_t dim, int radix);
    XL_UTILITY_API char*    XlI64toA(int64 value, char* buffer, size_t dim, int radix);
    XL_UTILITY_API char*    XlUI32toA(uint32_t value, char* buffer, size_t dim, int radix);
    XL_UTILITY_API char*    XlUI64toA(uint64_t value, char* buffer, size_t dim, int radix);

	    // ms secure version compatible
    XL_UTILITY_API int      XlI32toA_s(int32 value, char* buffer, size_t dim, int radix);
    XL_UTILITY_API int      XlI64toA_s(int64 value, char* buffer, size_t dim, int radix);
    XL_UTILITY_API int      XlUI32toA_s(uint32_t value, char* buffer, size_t dim, int radix);
    XL_UTILITY_API int      XlUI64toA_s(uint64_t value, char* buffer, size_t dim, int radix);

	    // non-secure version
    XL_UTILITY_API char*    XlI32toA_ns(int32 value, char* buffer, int radix);
    XL_UTILITY_API char*    XlI64toA_ns(int64 value, char* buffer, int radix);
    XL_UTILITY_API char*    XlUI32toA_ns(uint32_t value, char* buffer, int radix);
    XL_UTILITY_API char*    XlUI64toA_ns(uint64_t value, char* buffer, int radix);

    XL_UTILITY_API bool     XlToHexStr(const char* x, size_t xlen, char* y, size_t ylen);
    XL_UTILITY_API bool     XlHexStrToBin(const char* x, char* y);

        ////////////   H E L P E R S   ////////////
    template <int Count, typename CharType>
        void XlCopyString(CharType (&destination)[Count], const CharType source[])
        {
            XlCopyString(destination, Count, source);
        }

    template <int Count, typename CharType>
        void XlCopyNString(CharType (&destination)[Count], const CharType source[], size_t length)
        {
            XlCopyNString(destination, Count, source, length);
        }

	template <int Count, typename CharType>
        void XlCopyString(CharType (&destination)[Count], const StringSection<CharType>& source)
        {
            XlCopyNString(destination, Count, source._start, source.Length());
        }

	template <typename CharType>
		void XlCopyString(CharType destination[], size_t destinationCount, const StringSection<CharType>& source)
		{
			XlCopyNString(destination, destinationCount, source._start, source.Length());
		}

    template <int Count, typename CharType>
        void XlCopyString(CharType (&destination)[Count], const std::basic_string<CharType>& source)
        {
            XlCopyNString(destination, Count, AsPointer(source.cbegin()), source.size());
        }

    template <typename CharType>
        void XlCatString(CharType destination[], size_t size, const StringSection<CharType>& source)
        {
            XlCatNString(destination, size, source.begin(), source.Length());
        }

    template <int Count, typename CharType>
        void XlCatString(CharType (&destination)[Count], const CharType source[])
        {
            XlCatString(destination, Count, source);
        }

    template <int Count, typename CharType>
        void XlCatString(CharType (&destination)[Count], const StringSection<CharType>& source)
        {
            XlCatNString(destination, Count, source.begin(), source.Length());
        }

    template <int Count, typename CharType>
        void XlCatString(CharType (&destination)[Count], const std::basic_string<CharType>& source)
        {
            XlCatNString(destination, Count, AsPointer(source.cbegin()), source.size());
        }

    template<typename T>
        bool XlEqString(const T* a, const T* b)
        {
            return XlCompareString(a, b) == 0;
        }

    template<typename T>
        bool XlEqStringI(const T* a, const T* b)
        {
            return XlCompareStringI(a, b) == 0;
        }

    template<typename T>
        bool XlEqStringI(const std::basic_string<T>& a, const std::basic_string<T>& b)
        {
            auto sz = a.size();
            if (b.size() != sz) return false;
            for (size_t i = 0; i < sz; ++i)
                if (XlToLower(a[i]) != XlToLower(b[i]))
                    return false;
            return true;
        }

    template<typename T>
        bool XlEqStringI(const std::basic_string<T>& a, const T* b)
        {
            auto sz = a.size();
            size_t i = 0;
            for (; i < sz; ++i)
                if (!b[i] || XlToLower(a[i]) != XlToLower(b[i]))
                    return false;
            return b[i] == 0;
        }

    template<typename T>
        bool XlEqString(const std::basic_string<T>& a, const T* b)
        {
            auto sz = a.size();
            size_t i = 0;
            for (; i < sz; ++i)
                if (!b[i] || a[i] != b[i])
                    return false;
            return b[i] == 0;
        }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename T>
        bool XlEqStringI(const StringSection<T>& a, const StringSection<T>& b)
        {
            auto sz = a.Length();
            if (b.Length() != sz) return false;
            for (size_t i = 0; i < sz; ++i)
                if (XlToLower(a._start[i]) != XlToLower(b._start[i]))
                    return false;
            return true;
        }

    template<typename T>
        bool XlEqString(const StringSection<T>& a, const StringSection<T>& b)
        {
            auto sz = a.Length();
            if (b.Length() != sz) return false;
            for (size_t i = 0; i < sz; ++i)
                if (a._start[i] != b._start[i])
                    return false;
            return true;
        }

    template<typename T>
        bool XlEqStringI(const StringSection<T>& a, const T* b)
        {
            auto sz = a.Length();
            size_t i = 0;
            for (; i < sz; ++i)
                if (!b[i] || XlToLower(a._start[i]) != XlToLower(b[i]))
                    return false;
            return b[i] == 0;
        }

    template<typename T>
        bool XlEqString(const StringSection<T>& a, const T* b)
        {
            auto sz = a.Length();
            size_t i = 0;
            for (; i < sz; ++i)
                if (!b[i] || a._start[i] != b[i])
                    return false;
            return b[i] == 0;
        }

    template<typename T>
        bool XlEqStringI(const StringSection<T>& a, const std::basic_string<T>& b)
        {
            return XlEqStringI(a, MakeStringSection(b));
        }

    template<typename T>
        bool XlEqString(const StringSection<T>& a, const std::basic_string<T>& b)
        {
            return XlEqString(a, MakeStringSection(b));
        }

    template<typename T>
        int XlCompareString(const StringSection<T>& a, const T* b)
        {
            auto alen = a.Length();
            auto blen = XlStringSize(b);
            auto cmp = XlComparePrefix(a.begin(), b, std::min(alen, blen));
            if (cmp != 0) return cmp;

                // Initial prefix is identical.
                // If the length of both strings are identical, then they
                // are identical. Otherwise we should emulate the result we 
                // would get from expression "int(*x) - int(*y)" on two 
                // null terminated strings
            if (alen == blen) return 0;
            if (alen < blen) return -int(b[alen]);
            return a[blen];
        }

    template<typename T>
        int XlCompareString(const T* a, const StringSection<T>& b)
        {
            auto alen = XlStringSize(a);
            auto blen = b.Length();
            auto cmp = XlComparePrefix(a, b.begin(), std::min(alen, blen));
            if (cmp != 0) return cmp;

                // Initial prefix is identical.
                // If the length of both strings are identical, then they
                // are identical. Otherwise we should emulate the result we 
                // would get from expression "int(*x) - int(*y)" on two 
                // null terminated strings
            if (alen == blen) return 0;
            if (alen < blen) return -int(b[alen]);
            return a[blen];
        }

    template<typename T>
        int XlCompareString(const StringSection<T>& a, const StringSection<T>& b)
        {
            auto alen = a.Length(), blen = b.Length();
            auto cmp = XlComparePrefix(a.begin(), b.begin(), std::min(alen, blen));
            if (cmp != 0) return cmp;

                // Initial prefix is identical.
                // If the length of both strings are identical, then they
                // are identical. Otherwise we should emulate the result we 
                // would get from expression "int(*x) - int(*y)" on two 
                // null terminated strings
            if (alen == blen) return 0;
            if (alen < blen) return -int(b[alen]);
            return a[blen];
        }

    template<typename T>
        int XlCompareStringI(const StringSection<T>& a, const T* b)
        {
            auto alen = a.Length();
            auto blen = XlStringSize(b);
            auto cmp = XlComparePrefixI(a.begin(), b, std::min(alen, blen));
            if (cmp != 0) return cmp;

                // Initial prefix is identical.
                // If the length of both strings are identical, then they
                // are identical. Otherwise we should emulate the result we 
                // would get from expression "int(*x) - int(*y)" on two 
                // null terminated strings
            if (alen == blen) return 0;
            if (alen < blen) return -int(XlToLower(b[alen]));
            return XlToLower(a[blen]);
        }

    template<typename T>
        int XlCompareStringI(const T* a, const StringSection<T>& b)
        {
            auto alen = XlStringSize(a);
            auto blen = b.Length();
            auto cmp = XlComparePrefixI(a, b.begin(), std::min(alen, blen));
            if (cmp != 0) return cmp;

                // Initial prefix is identical.
                // If the length of both strings are identical, then they
                // are identical. Otherwise we should emulate the result we 
                // would get from expression "int(*x) - int(*y)" on two 
                // null terminated strings
            if (alen == blen) return 0;
            if (alen < blen) return -int(XlToLower(b[alen]));
            return XlToLower(a[blen]);
        }

    template<typename T>
        int XlCompareStringI(const StringSection<T>& a, const StringSection<T>& b)
        {
            auto alen = a.Length(), blen = b.Length();
            auto cmp = XlComparePrefixI(a.begin(), b.begin(), std::min(alen, blen));
            if (cmp != 0) return cmp;

                // Initial prefix is identical.
                // If the length of both strings are identical, then they
                // are identical. Otherwise we should emulate the result we 
                // would get from expression "int(*x) - int(*y)" on two 
                // null terminated strings
            if (alen == blen) return 0;
            if (alen < blen) return -int(XlToLower(b[alen]));
            return XlToLower(a[blen]);
        }

    template<typename T>
        int XlCompareString(const std::basic_string<T>& a, const std::basic_string<T>& b)
        {
            return XlCompareString(MakeStringSection(a), MakeStringSection(b));
        }

    template<typename T>
        int XlCompareStringI(const std::basic_string<T>& a, const std::basic_string<T>& b)
        {
            return XlCompareStringI(MakeStringSection(a), MakeStringSection(b));
        }

    template<typename T>
        bool XlBeginsWith(const StringSection<T>& a, const StringSection<T>& b)
        {
            return 
                a.Length() >= b.Length()
                && XlEqString(StringSection<T>(a.begin(), a.begin() + b.Length()), b);
        }

    template<typename T>
        bool XlBeginsWithI(const StringSection<T>& a, const StringSection<T>& b)
        {
            return 
                a.Length() >= b.Length()
                && XlEqStringI(StringSection<T>(a.begin(), a.begin() + b.Length()), b);
        }

    template<typename T>
        bool XlBeginsWith(const StringSection<T>& a, const T* b)
        {
            const auto *ai = a.begin(), *bi = b;
            for (;;) {
                if (!*bi) return true;
                if (ai == a.end()) return false;
                if (*ai != *bi) return false;
                ++ai; ++bi;
            }
        }

    template<typename T>
        bool XlBeginsWithI(const StringSection<T>& a, const T* b)
        {
            const auto *ai = a.begin(), *bi = b;
            for (;;) {
                if (!*bi) return true;
                if (ai == a.end()) return false;
                if (XlToLower(*ai) != XlToLower(*bi)) return false;
                ++ai; ++bi;
            }
        }

	template<typename T>
        bool XlEndsWith(const StringSection<T>& a, const StringSection<T>& b)
        {
            return 
                a.Length() >= b.Length()
                && XlEqString(StringSection<T>(a.end() - b.Length(), a.end()), b);
        }

    template<typename T>
        bool XlEndsWithI(const StringSection<T>& a, const StringSection<T>& b)
        {
            return 
                a.Length() >= b.Length()
                && XlEqStringI(StringSection<T>(a.end() - b.Length(), a.end()), b);
        }

    std::string Concatenate(StringSection<> zero, StringSection<> one);
    std::string Concatenate(StringSection<> zero, StringSection<> one, StringSection<> two);
    std::string Concatenate(StringSection<> zero, StringSection<> one, StringSection<> two, StringSection<> three);
    std::string Concatenate(StringSection<> zero, StringSection<> one, StringSection<> two, StringSection<> three, StringSection<> four);
    std::string Concatenate(StringSection<> zero, StringSection<> one, StringSection<> two, StringSection<> three, StringSection<> four, StringSection<> five);
    std::string Concatenate(StringSection<> zero, StringSection<> one, StringSection<> two, StringSection<> three, StringSection<> four, StringSection<> five, StringSection<> six);

///////////////////////////////////////////////////////////////////////////////////////////////////

    #if REDIRECT_CLIB_WITH_PREPROCESSOR

        #undef itoa
        #undef ltoa
        #undef ultoa
        #undef _ui64toa
        #undef _i64toa
        #undef _itoa_s
        #undef _i64toa_s
        #undef _ui64toa_s
        #undef _atoi64

        #define  itoa(v, b, r)    XlI32toA_ns((int32)v, b, r)
        #define  ltoa(v, b, r)    XlI32toA_ns((int32)v, b, r)
        #define ultoa(v, b, r)    XlUI32toA_ns((uint32_t)v, b, r)
        #define _ui64toa(v, b, r) XlUI64toA_ns(v, b, r)
        #define _i64toa(v, b, r)  XlI64toA_ns(v, b, r)
        #define _itoa_s           XlI32toA_s
        #define _i64toa_s         XlI64toA_s
        #define _ui64toa_s        XlUI64toA_s
        #define _atoi64			  XlAtoI64
        #define _strtoui64		  XlAtoUI64	

        #undef _strlwr
        #undef _strupr

        #define _strlwr XlLowerCase
        #define _strupr XlUpperCase

    #endif

    inline int XlCountUtfSequence(uint8_t c)
    {
        if (c < 0x80) {
            return 1;
        } else if (c < 0xc2) {
            // second, third, fourth byte or overlong encoding
            return 0;
        } else if (c < 0xe0) {
            return 2;
        } else if (c < 0xf0) {
            return 3;
        } else if (c < 0xf5) {
            return 4;
        } else {
            // illegal
            return 0;
        }
    }

}

using namespace Utility;
