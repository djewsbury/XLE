// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Conversion.h"
#include "StringUtils.h"
#include "PtrUtils.h"
#include "FastParseValue.h"
#include "../Core/SelectConfiguration.h"
#include "../Core/Exceptions.h"
#include "cutf/cutf.h"
#include <algorithm>
#include <stdexcept>

namespace Conversion
{

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<> std::basic_string<char> Convert(uint64_t input)
    {
        return std::to_string(input);
    }

    template<> std::basic_string<wchar_t> Convert(uint64_t input)
    {
        return std::to_wstring(input);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    // todo -- these could be implemented more effectively, particularly with C++17's std::from_string
    //          (or just by custom coding the appropriate
    template<> float Convert(StringSection<> input)
    {
        float result;
        auto* parseEnd = FastParseValue(input, result);
        if (parseEnd != input.end())
            Throw(std::runtime_error("Bad value converting (" + input.AsString() + ") to float"));
        return result;
    }

	template<> double Convert(StringSection<> input)
    {
        double result;
        auto* parseEnd = FastParseValue(input, result);
        if (parseEnd != input.end())
            Throw(std::runtime_error("Bad value converting (" + input.AsString() + ") to double"));
        return result;
    }

    template<> uint32_t Convert(StringSection<> input)
    {
        uint32_t result;
        auto* parseEnd = FastParseValue(input, result);
        if (parseEnd != input.end())
            Throw(std::runtime_error("Bad value converting (" + input.AsString() + ") to uint32_t"));
        return result;
    }

    template<> int32_t Convert(StringSection<> input)
    {
        int32_t result;
        auto* parseEnd = FastParseValue(input, result);
        if (parseEnd != input.end())
            Throw(std::runtime_error("Bad value converting (" + input.AsString() + ") to int32_t"));
        return result;
    }

    template<> int64_t Convert(StringSection<> input)
    {
        int64_t result;
        auto* parseEnd = FastParseValue(input, result);
        if (parseEnd != input.end())
            Throw(std::runtime_error("Bad value converting (" + input.AsString() + ") to int64_t"));
        return result;
    }

    template<> uint64_t Convert(StringSection<> input)
    {
        uint64_t result;
        auto* parseEnd = FastParseValue(input, result);
        if (parseEnd != input.end())
            Throw(std::runtime_error("Bad value converting (" + input.AsString() + ") to uint64_t"));
        return result;
    }

    template<> bool Convert(StringSection<> input)
    {
        if (    !XlCompareStringI(input, "true")
            ||  !XlCompareStringI(input, "yes")
            ||  !XlCompareStringI(input, "t")
            ||  !XlCompareStringI(input, "y")) {
            return true;
        } else if (    !XlCompareStringI(input, "false")
            ||  !XlCompareStringI(input, "no")
            ||  !XlCompareStringI(input, "f")
            ||  !XlCompareStringI(input, "n")) {
            return true;
        }
        auto asInt = Conversion::Convert<int>(input);
        return !!asInt;
    }

    template<> float Convert(const std::basic_string<utf8>& input)          { return Convert<float>(MakeStringSection(input)); }
	template<> double Convert(const std::basic_string<utf8>& input)         { return Convert<double>(MakeStringSection(input)); }
    template<> uint32_t Convert(const std::basic_string<utf8>& input)       { return Convert<uint32_t>(MakeStringSection(input)); }
    template<> int32_t Convert(const std::basic_string<utf8>& input)        { return Convert<int32_t>(MakeStringSection(input)); }
    template<> int64_t Convert(const std::basic_string<utf8>& input)        { return Convert<int64_t>(MakeStringSection(input)); }
    template<> uint64_t Convert(const std::basic_string<utf8>& input)       { return Convert<uint64_t>(MakeStringSection(input)); }
    template<> bool Convert(const std::basic_string<utf8>& input)           { return Convert<bool>(MakeStringSection(input)); }
    
    template<> float Convert(const char input[])        { return Convert<float>(MakeStringSection(input)); }
	template<> double Convert(const char input[])       { return Convert<double>(MakeStringSection(input)); }
    template<> uint32_t Convert(const char input[])     { return Convert<uint32_t>(MakeStringSection(input)); }
    template<> int32_t Convert(const char input[])      { return Convert<int32_t>(MakeStringSection(input)); }
    template<> int64_t Convert(const char input[])      { return Convert<int64_t>(MakeStringSection(input)); }
    template<> uint64_t Convert(const char input[])     { return Convert<uint64_t>(MakeStringSection(input)); }
    template<> bool Convert(const char input[])         { return Convert<bool>(MakeStringSection(input)); }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<> std::basic_string<ucs2> Convert(const std::basic_string<utf8>& input)
    {
        std::basic_string<ucs2> result;
        result.resize(input.size());
        utf8_2_ucs2(
            AsPointer(input.begin()), input.size(),
            AsPointer(result.begin()), result.size());
        return result;
    }

    template<> std::basic_string<ucs4> Convert(const std::basic_string<utf8>& input)
    {
        std::basic_string<ucs4> result;
        result.resize(input.size());
        utf8_2_ucs4(
            AsPointer(input.begin()), input.size(),
            AsPointer(result.begin()), result.size());
        return result;
    }

    template<> std::basic_string<utf16> Convert(const std::basic_string<utf8>& input)
    {
        // Note -- despite the name utf8_2_ucs2 actually does generate
        //      utf16 characters for non-BMP "Basic Multilingual Plane" characters
		std::basic_string<utf16> result;
		result.resize(input.size());
		utf8_2_ucs2(
			AsPointer(input.begin()), input.size(),
			(ucs2*)AsPointer(result.begin()), result.size());
		return result;
    }
    
    template<> std::basic_string<utf8> Convert(const std::basic_string<ucs2>& input)
    {
        std::basic_string<utf8> result;
        result.resize(input.size());      // todo -- ucs2_2_utf8 might need to expand the result
        ucs2_2_utf8(
            AsPointer(input.begin()), input.size(),
            AsPointer(result.begin()), result.size());
        return result;
    }

    template<> std::basic_string<ucs4> Convert(const std::basic_string<ucs2>& input)
    {
        std::basic_string<ucs4> result;
        result.resize(input.size());
        ucs2_2_ucs4(
            AsPointer(input.begin()), input.size(),
            AsPointer(result.begin()), result.size());
        return result;
    }

    template<> std::basic_string<utf8> Convert(const std::basic_string<ucs4>& input)
    {
        std::basic_string<utf8> result;
        result.resize(input.size());
        ucs4_2_utf8(
            AsPointer(input.begin()), input.size(),
            AsPointer(result.begin()), result.size());
        return result;
    }

    template<> std::basic_string<ucs2> Convert(const std::basic_string<ucs4>& input)
    {
        std::basic_string<ucs2> result;
        result.resize(input.size());
        ucs4_2_ucs2(
            AsPointer(input.begin()), input.size(),
            AsPointer(result.begin()), result.size());
        return result;
    }

    template<> std::basic_string<wchar_t> Convert(const std::basic_string<ucs4>& input)
    {
        assert(sizeof(wchar_t) == sizeof(ucs2));        // todo -- making assumptions about the size of wchar_t here
        std::basic_string<wchar_t> result;
        result.resize(input.size());
        ucs4_2_ucs2(
            AsPointer(input.begin()), input.size(),
            (ucs2*)AsPointer(result.begin()), result.size());
        return result;
    }

    template<> std::basic_string<wchar_t> Convert(const std::basic_string<ucs2>& input)
    {
        assert(sizeof(wchar_t) == sizeof(ucs2));        // todo -- making assumptions about the size of wchar_t here
        return reinterpret_cast<const std::basic_string<wchar_t>&>(input);
    }

    template<> std::basic_string<utf8> Convert(const std::basic_string<utf16>& input)
    {
        assert(0);      // not implemented
        return {};
    }

    template<> std::basic_string<utf8> Convert(const std::basic_string<wchar_t>& input)
    {
        return widetoutf8(input);
    }

    template<> std::basic_string<wchar_t> Convert(const std::basic_string<utf8>& input)
    {
        return utf8towide(input);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	template<> std::basic_string<ucs2> Convert(StringSection<utf8> input)
	{
		std::basic_string<ucs2> result;
		result.resize(input.Length());
		utf8_2_ucs2(
			AsPointer(input.begin()), input.Length(),
			AsPointer(result.begin()), result.size());
		return result;
	}

	template<> std::basic_string<ucs4> Convert(StringSection<utf8> input)
	{
		std::basic_string<ucs4> result;
		result.resize(input.Length());
		utf8_2_ucs4(
			AsPointer(input.begin()), input.Length(),
			AsPointer(result.begin()), result.size());
		return result;
	}

    template<> std::basic_string<utf16> Convert(StringSection<utf8> input)
	{
        // Note -- despite the name utf8_2_ucs2 actually does generate
        //      utf16 characters for non-BMP "Basic Multilingual Plane" characters
		std::basic_string<utf16> result;
		result.resize(input.Length());
		utf8_2_ucs2(
			AsPointer(input.begin()), input.Length(),
			(ucs2*)AsPointer(result.begin()), result.size());
		return result;
	}

	template<> std::basic_string<utf8> Convert(StringSection<ucs2> input)
	{
		std::basic_string<utf8> result;
		result.resize(input.Length());      // todo -- ucs2_2_utf8 might need to expand the result
		ucs2_2_utf8(
			AsPointer(input.begin()), input.Length(),
			AsPointer(result.begin()), result.size());
		return result;
	}

	template<> std::basic_string<ucs4> Convert(StringSection<ucs2> input)
	{
		std::basic_string<ucs4> result;
		result.resize(input.Length());
		ucs2_2_ucs4(
			AsPointer(input.begin()), input.Length(),
			AsPointer(result.begin()), result.size());
		return result;
	}

    template<> std::basic_string<utf8> Convert(StringSection<utf16> input)
	{
        // Unlike utf8_2_ucs2, ucs2_2_utf8 does not support utf16 surrogate pairs
		assert(0);      // not implemented
        return {};
	}

	template<> std::basic_string<utf8> Convert(StringSection<ucs4> input)
	{
		std::basic_string<utf8> result;
		result.resize(input.Length());
		ucs4_2_utf8(
			AsPointer(input.begin()), input.Length(),
			AsPointer(result.begin()), result.size());
		return result;
	}

	template<> std::basic_string<ucs2> Convert(StringSection<ucs4> input)
	{
		std::basic_string<ucs2> result;
		result.resize(input.Length());
		ucs4_2_ucs2(
			AsPointer(input.begin()), input.Length(),
			AsPointer(result.begin()), result.size());
		return result;
	}

	template<> std::basic_string<wchar_t> Convert(StringSection<ucs4> input)
	{
		assert(sizeof(wchar_t) == sizeof(ucs2));        // todo -- making assumptions about the size of wchar_t here
        std::basic_string<wchar_t> result;
		result.resize(input.Length());
		ucs4_2_ucs2(
			AsPointer(input.begin()), input.Length(),
			(ucs2*)AsPointer(result.begin()), result.size());
		return result;
	}

    template<> std::basic_string<utf8> Convert(StringSection<wchar_t> input)
    {
        return widetoutf8(input.AsString());
    }

    template<> std::basic_string<wchar_t> Convert(StringSection<utf8> input)
    {
        return utf8towide(input.AsString());
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<> ptrdiff_t Convert(
        ucs2 output[], size_t outputDim,
        const utf8* begin, const utf8* end)
    {
        return utf8_2_ucs2(begin, end-begin, output, outputDim);
    }

    template<> ptrdiff_t Convert(
        ucs4 output[], size_t outputDim,
        const utf8* begin, const utf8* end)
    {
        return utf8_2_ucs4(begin, end-begin, output, outputDim);
    }

    template<> ptrdiff_t Convert(
        wchar_t output[], size_t outputDim,
        const utf8* begin, const utf8* end)
    {
        assert(sizeof(wchar_t) == sizeof(ucs2));        // todo -- making assumptions about the size of wchar_t here
        return utf8_2_ucs2(begin, end-begin, (ucs2*)output, outputDim);
    }

    template<> ptrdiff_t Convert(
        utf16 output[], size_t outputDim,
        const utf8* begin, const utf8* end)
    {
        // not implemented
        // In theory we can use the standard library for this -- 
        //  https://en.cppreference.com/w/cpp/locale/codecvt/out
        // but that's deprecated, in favor of potentially safer 
        // std::string based solutions.
        assert(0);      
        return 0;
    }

    template<> ptrdiff_t Convert(
        utf8 output[], size_t outputDim,
        const utf16* begin, const utf16* end)
    {
        // not implemented (see comment above)
        assert(0);      
        return 0;
    }

        //////////      //////////      //////////

    template<> ptrdiff_t Convert(
        utf8 output[], size_t outputDim,
        const ucs2* begin, const ucs2* end)
    {
        return ucs2_2_utf8(begin, end-begin, output, outputDim);
    }

    template<> ptrdiff_t Convert(
        ucs4 output[], size_t outputDim,
        const ucs2* begin, const ucs2* end)
    {
        return ucs2_2_ucs4(begin, end-begin, output, outputDim);
    }

    template<> ptrdiff_t Convert(
        wchar_t output[], size_t outputDim,
        const ucs2* begin, const ucs2* end)
    {
        assert(sizeof(wchar_t) == sizeof(ucs2));        // todo -- making assumptions about the size of wchar_t here
        XlCopyNString((ucs2*)output, outputDim, begin, end-begin);
        return true;
    }

        //////////      //////////      //////////

    template<> ptrdiff_t Convert(
        utf8 output[], size_t outputDim,
        const ucs4* begin, const ucs4* end)
    {
        return ucs4_2_utf8(begin, end-begin, output, outputDim);
    }

    template<> ptrdiff_t Convert(
        ucs2 output[], size_t outputDim,
        const ucs4* begin, const ucs4* end)
    {
        return ucs4_2_ucs2(begin, end-begin, output, outputDim);
    }

    template<> ptrdiff_t Convert(
        wchar_t output[], size_t outputDim,
        const ucs4* begin, const ucs4* end)
    {
        assert(sizeof(wchar_t) == sizeof(ucs2));        // todo -- making assumptions about the size of wchar_t here
        return ucs4_2_ucs2(begin, end-begin, (ucs2*)output, outputDim);
    }


            //////////      //////////      //////////

    template<> ptrdiff_t Convert(
        utf8 output[], size_t outputDim,
        const wchar_t* begin, const wchar_t* end)
    {
        assert(sizeof(wchar_t) == sizeof(ucs2));        // todo -- making assumptions about the size of wchar_t here
        return Convert(output, outputDim, (const ucs2*)begin, (const ucs2*)end);
    }

    template<> ptrdiff_t Convert(
        ucs2 output[], size_t outputDim,
        const wchar_t* begin, const wchar_t* end)
    {
        assert(sizeof(wchar_t) == sizeof(ucs2));        // todo -- making assumptions about the size of wchar_t here
        return Convert(output, outputDim, (const ucs2*)begin, (const ucs2*)end);
    }

    template<> ptrdiff_t Convert(
        ucs4 output[], size_t outputDim,
        const wchar_t* begin, const wchar_t* end)
    {
        assert(sizeof(wchar_t) == sizeof(ucs2));        // todo -- making assumptions about the size of wchar_t here
        return Convert(output, outputDim, (const ucs2*)begin, (const ucs2*)end);
    }

}
