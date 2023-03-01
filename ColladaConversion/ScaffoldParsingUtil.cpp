// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ScaffoldParsingUtil.h"
#include "../Utility/ArithmeticUtils.h"
#include "../Math/XLEMath.h"
#include <iostream>

namespace ColladaConversion
{
    bool BeginsWith(const Formatters::XmlInputFormatter<utf8>::InteriorSection& section, const utf8 match[])
    {
        auto matchLen = XlStringSize(match);
        if ((section._end - section._start) < ptrdiff_t(matchLen)) return false;
        return Is(Formatters::XmlInputFormatter<utf8>::InteriorSection(section._start, section._start + matchLen), match);
    }

    bool EndsWith(const Formatters::XmlInputFormatter<utf8>::InteriorSection& section, const utf8 match[])
    {
        auto matchLen = XlStringSize(match);
        if ((section._end - section._start) < ptrdiff_t(matchLen)) return false;
        return Is(Formatters::XmlInputFormatter<utf8>::InteriorSection(section._end - matchLen, section._end), match);
    }
}

namespace Formatters
{
    std::ostream& SerializationOperator(std::ostream& os, const StreamLocation& loc)
    {
        return os << "Line: " << loc._lineIndex << ", Char: " << loc._charIndex;
    }
}
