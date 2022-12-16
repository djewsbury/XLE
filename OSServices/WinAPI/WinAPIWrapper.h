// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Core/SelectConfiguration.h"

#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS

#include "../../OSServices/WinAPI/IncludeWindows.h"

namespace OSServices { namespace Windows
{
        //
        //      Redirection to help with unicode support
        //      (ie, we can select to use the single byte or multi byte
        //      char versions of the windows functions and objects here...)
        //
    typedef WNDCLASSEXA                         WNDCLASSEX;
    static const auto Fn_RegisterClassEx =            &RegisterClassExA;
    static const auto Fn_CreateWindowEx =             &CreateWindowExA;
    static const auto Fn_UnregisterClass =            &UnregisterClassA;
    static const auto Fn_SetDllDirectory =            &SetDllDirectoryA;
    static const auto Fn_LoadLibrary =                &LoadLibraryA;
    static const auto Fn_GetProcAddress =             &GetProcAddress;
    static const auto Fn_FreeLibrary =                &::FreeLibrary;
    static const auto Fn_EnumDisplayDevices =         &EnumDisplayDevicesW;       // (we always use widechar versions for these, because some newer related APIs are widechar only)
    static const auto Fn_EnumDisplaySettingsEx =      &EnumDisplaySettingsExW;
    static const auto Fn_ChangeDisplaySettingsEx =    &ChangeDisplaySettingsExW;
}}

#endif
