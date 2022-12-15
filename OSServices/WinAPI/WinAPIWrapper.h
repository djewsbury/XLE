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
    static auto Fn_RegisterClassEx =            &RegisterClassExA;
    static auto Fn_CreateWindowEx =             &CreateWindowExA;
    static auto Fn_UnregisterClass =            &UnregisterClassA;
    static auto Fn_SetDllDirectory =            &SetDllDirectoryA;
    static auto Fn_LoadLibrary =                &LoadLibraryA;
    static auto Fn_GetProcAddress =             &GetProcAddress;
    static auto Fn_FreeLibrary =                &::FreeLibrary;
    static auto Fn_EnumDisplayDevices =         &EnumDisplayDevicesW;       // (we always use widechar versions for these, because some newer related APIs are widechar only)
    static auto Fn_EnumDisplaySettingsEx =      &EnumDisplaySettingsExW;
    static auto Fn_ChangeDisplaySettingsEx =    &ChangeDisplaySettingsExW;
}}

#endif
