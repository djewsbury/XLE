// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Core/SelectConfiguration.h"

#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS

#include "../../OSServices/WinAPI/IncludeWindows.h"
#include <shellscalingapi.h>        // for SetProcessDpiAwareness, GetDpiForMonitor

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

    enum class EmulateableVersion
    {
        WindowsPreVista,
        WindowsVista,
        Windows8_1, Windows10_16, Windows10_17,
        Latest
    };

    struct ExtensionFunctions
    {
        decltype(&EnableNonClientDpiScaling) Fn_EnableNonClientDpiScaling;              // Windows 10, version 1607
        decltype(&GetWindowDpiAwarenessContext) Fn_GetWindowDpiAwarenessContext;        // Windows 10, version 1607
        decltype(&SetProcessDpiAwarenessContext) Fn_SetProcessDpiAwarenessContext;      // Windows 10, version 1703
        decltype(&SetProcessDpiAwareness) Fn_SetProcessDpiAwareness;                    // Windows 8.1
        decltype(&SetProcessDPIAware) Fn_SetProcessDPIAware;                            // Windows Vista
        decltype(&GetDpiForWindow) Fn_GetDpiForWindow;                                  // Windows 10, version 1607
        decltype(&GetDpiForMonitor) Fn_GetDpiForMonitor;                                // Windows 8.1

        std::vector<HMODULE> _attachedModules;
        EmulateableVersion _emulating;

        ExtensionFunctions() = default;
        ~ExtensionFunctions();
        ExtensionFunctions(ExtensionFunctions&&) = delete;
        ExtensionFunctions& operator=(ExtensionFunctions&&) = delete;
    };
    ExtensionFunctions& GetExtensionFunctions();

    void EnumlateWindowsVersion(EmulateableVersion);
}}

#endif
