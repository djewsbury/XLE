// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <ostream>
#include <sstream>

namespace Utility
{
    using OutputStream = std::ostream;
    template<typename CharType = char>
        using MemoryOutputStream = std::basic_stringstream<CharType>;
}

using namespace Utility;
