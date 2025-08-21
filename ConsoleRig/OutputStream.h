// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <stdarg.h>

namespace Utility { class OutputStream; }

namespace ConsoleRig
{
    Utility::OutputStream&      GetWarningStream();

    void        xleWarning(const char format[], ...);
    void        xleWarning(const char format[], va_list args);

    #if defined(_DEBUG)
        void            xleWarningDebugOnly(const char format[], ...);
    #else
        inline void     xleWarningDebugOnly(const char format[], ...) { (void)format; }
    #endif

    void DebuggerOnlyWarning(const char format[], ...);
}

