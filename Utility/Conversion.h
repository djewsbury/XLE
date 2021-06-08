// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/Types.h"
#include "UTFUtils.h"
#include "StringUtils.h"
#include <string>

namespace Conversion
{
    template<typename Output> Output Convert(uint64_t);
    template<typename Output> Output Convert(const char[]);
    template<typename Type> const Type& Convert(const Type& input) { return input; }

    template<typename Output> Output Convert(const std::basic_string<utf8>& input);
	template<typename Output> Output Convert(const std::basic_string<utf16>& input);
    template<typename Output> Output Convert(const std::basic_string<ucs2>& input);
    template<typename Output> Output Convert(const std::basic_string<ucs4>& input);
    template<typename Output> Output Convert(const std::basic_string<char>& input);
    template<typename Output> Output Convert(const std::basic_string<wchar_t>& input);

	template<typename Output> Output Convert(StringSection<utf8> input);
	template<typename Output> Output Convert(StringSection<utf16> input);
	template<typename Output> Output Convert(StringSection<ucs2> input);
	template<typename Output> Output Convert(StringSection<ucs4> input);
	template<typename Output> Output Convert(StringSection<char> input);
    template<typename Output> Output Convert(StringSection<wchar_t> input);

    template<typename OutputElement, typename InputElement>
        ptrdiff_t Convert(
            OutputElement output[], size_t outputDim,
            const InputElement* begin, const InputElement* end);

    template<typename OutputElement, typename InputElement>
        ptrdiff_t Convert(
            OutputElement output[], size_t outputDim,
            const std::basic_string<InputElement>& str);

    template<typename OutputElement, typename InputElement>
        ptrdiff_t ConvertNullTerminated(
            OutputElement output[], size_t outputDim,
            const InputElement* begin);

    template<typename OutputElement, typename InputElement>
        ptrdiff_t ConvertNullTerminated(
            OutputElement output[], size_t outputDim,
            const InputElement* begin)
        {
            if (outputDim <= 1) return false;
            auto inputLen = XlStringSize(begin);
            return Convert(output, outputDim-1, begin, begin+inputLen);
        }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<> inline std::basic_string<utf8> Convert(const std::basic_string<utf8>& input) { return input; }
    template<> inline std::basic_string<utf16> Convert(const std::basic_string<utf16>& input) { return input; }
    template<> inline std::basic_string<ucs2> Convert(const std::basic_string<ucs2>& input) { return input; }
    template<> inline std::basic_string<ucs4> Convert(const std::basic_string<ucs4>& input) { return input; }
    template<> inline std::basic_string<wchar_t> Convert(const std::basic_string<wchar_t>& input) { return input; }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename OutputElement, typename InputElement>
        ptrdiff_t Convert(
            OutputElement output[], size_t outputDim,
            const std::basic_string<InputElement>& str)
    {
        return Convert(output, outputDim, AsPointer(str.cbegin()), AsPointer(str.cend()));
    }

    template<typename Element>
        ptrdiff_t Convert(
            Element output[], size_t outputDim,
            const Element* begin, const Element* end)
    {
        XlCopyNString(output, outputDim, begin, end-begin);
        return end - begin;
    }

}
