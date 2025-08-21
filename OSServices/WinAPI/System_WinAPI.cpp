// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "System_WinAPI.h"
#include "WinAPIWrapper.h"
#include "../RawFS.h"
#include "../TimeUtils.h"
#include "../Log.h"
#include "../../Core/Prefix.h"
#include "../../Core/Types.h"
#include "../../Utility/Threading/LockFree.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/FunctionUtils.h"
#include "IncludeWindows.h"
#include <process.h>
#include <share.h>
#include <time.h>
#include <timeapi.h>
#include <fstream>

#include <psapi.h>
#include <shellapi.h>
#include <ImageHlp.h>
#include <avrt.h>
#include <shlobj.h>
#include <objbase.h>
#include <shobjidl.h>

// #if !defined(XLE_GET_MODULE_PATH_ENABLE)
//     #define XLE_GET_MODULE_PATH_ENABLE 1
// #endif

namespace OSServices
{

//////////////////////////////////////////////////////////////////////////

uint64 GetPerformanceCounter()
{
    LARGE_INTEGER i;
    QueryPerformanceCounter(&i);
    return i.QuadPart;
}

uint64 GetPerformanceCounterFrequency()
{
    LARGE_INTEGER i;
    QueryPerformanceFrequency(&i);
    return i.QuadPart;
}

void ConfigureProcessSettings()
{
    // Windows has a built-in system for managing thread priority for multimedia applications called MMCSS
    // It's a little hidden, you could say, within the layers of the WinAPI
    //
    // But there's a set of configuration at
    //      HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile
    //
    // and we can opt-in to one of the configurations there by selecting it by name
    //
    // There's also a function, DwmEnableMMCSS, that enableds the MMCSS system as whole throughout the 
    // entire system. I'm not sure if this is enabled by default

    DWORD taskIndex = 0;
    auto avTaskHandle = ::AvSetMmThreadCharacteristicsA("Games", &taskIndex);       // (requires Vista and above)
    if (!avTaskHandle) {
        auto error = ::GetLastError();
        if (error == ERROR_INVALID_TASK_NAME) Log(Warning) << "Thread priorties not set because there is no 'Games' entry in 'HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile'" << std::endl;
        else if (error == ERROR_PRIVILEGE_NOT_HELD) Log(Warning) << "Cannot set thread priorties due to lack of privileges" << std::endl;
        else Log(Warning) << "Cannot set thread priorties due to unknown reason" << std::endl;
    }

    // see also AvRevertMmThreadCharacteristics to undo what we've done here

    // AvQuerySystemResponsiveness

    // also consider: SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
    //  this just ensures that the display is not turned off while the application is alive

    // We should attempt to set windows to the highest possible timer precision. This is a system wide setting, so
    // it will effect other applications running at the same time
    // Some background:
    // https://randomascii.wordpress.com/2020/10/04/windows-timer-resolution-the-great-rule-change/
    //
    // In theory we should check battery status and consider reducing frequency when on battery
    //
    // See also NtSetTimerResolution / NtQueryTimerResolution
    // http://undocumented.ntinternals.net/index.html?page=UserMode%2FUndocumented%20Functions%2FTime%2FNtSetTimerResolution.html
    unsigned timePeriod = 1;
    while (timePeriod < 15) {
        auto hres = timeBeginPeriod(timePeriod);        // (timeEndPeriod to clear this again)
        if (hres == TIMERR_NOCANDO) {
            ++timePeriod;
            continue;
        }
        break;
    }
}

//////////////////////////////////////////////////////////////////////////

void XlGetLocalTime(uint64 time, struct tm* local)
{
    __time64_t fileTime = time;
    _localtime64_s(local, &fileTime);
}

uint64 XlMakeFileTime(struct tm* local)
{
    return _mktime64(local);
}

double XlDiffTime(uint64 endTime, uint64 beginTime)
{
    return _difftime64(endTime, beginTime);
}

//////////////////////////////////////////////////////////////////////////
extern "C" IMAGE_DOS_HEADER __ImageBase;
ModuleId GetCurrentModuleId() 
{ 
        // We want to return a value that is unique to the current 
        // module (considering DLLs as separate modules from the main
        // executable). It's value doesn't matter, so long as it is
        // unique from other modules, and won't change over the lifetime
        // of the proces.
        //
        // When compiling under visual studio/windows, the __ImageBase
        // global points to the base of memory. Since the static global
        // is unique to each dll module, and the address it points to
        // will also be unique to each module, we can use it as a id
        // for the current module.
        // Actually, we could probably do the same thing with any
        // static global pointer... Just declare a char, and return
        // a pointer to it...?
    return (ModuleId)&__ImageBase; 
}


//////////////////////////////////////////////////////////////////////////
#if 0
bool XlIsCriticalSectionLocked(void* cs) 
{
    CRITICAL_SECTION* csPtr = reinterpret_cast<CRITICAL_SECTION*>(cs);
    return csPtr->RecursionCount > 0 && csPtr->OwningThread == (HANDLE)(size_t)GetCurrentThreadId();
}
#endif

static uint32_t FromWinWaitResult(uint32_t winResult)
{
    switch(winResult) {
    case WAIT_TIMEOUT:
        return XL_WAIT_TIMEOUT;

    case WAIT_FAILED:
        return XL_WAIT_FAILED;

    case WAIT_IO_COMPLETION:
        return XL_WAIT_IO_COMPLETION;

    default:
        if (winResult >= WAIT_OBJECT_0 && winResult < WAIT_OBJECT_0+XL_MAX_WAIT_OBJECTS) {
            return winResult - WAIT_OBJECT_0 + XL_WAIT_OBJECT_0;
        }
        if (winResult >= WAIT_ABANDONED_0 && winResult < WAIT_ABANDONED_0+XL_MAX_WAIT_OBJECTS) {
            return winResult - WAIT_ABANDONED_0 + XL_WAIT_ABANDONED_0;
        }
        return XL_WAIT_FAILED;
    }
}


#if 0
void XlGetNumCPUs(int* physical, int* logical, int* avail)
{
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);

    if (physical) {
        //shouldbe_implemented();
        *physical = (int)sys_info.dwNumberOfProcessors / 2; 
    }

    if (logical) {
        *logical = (int)sys_info.dwNumberOfProcessors;
    }
    if (avail) {
        *avail = (int)sys_info.dwNumberOfProcessors;   
    }

}
#endif

uint32_t XlGetCurrentProcessId()
{
    return GetCurrentProcessId();
}

#if defined(GetCurrentDirectory)
    #error GetCurrentDirectory is macro'ed, which will cause a linker error here
#endif
bool GetCurrentDirectory(uint32_t nBufferLength, char lpBuffer[])
{
	return GetCurrentDirectoryA((DWORD)nBufferLength, lpBuffer) != FALSE;
}

bool XlCloseSyncObject(XlHandle h)
{
    BOOL closeResult = CloseHandle(h);
    return closeResult != FALSE;
}

uint32_t XlWaitForSyncObject(XlHandle h, uint32_t waitTime)
{
    return FromWinWaitResult(WaitForSingleObject(h, waitTime));
}

bool XlReleaseMutex(XlHandle h)
{
    return ReleaseMutex(h) != FALSE;
}

XlHandle XlCreateSemaphore(int maxCount)
{
    HANDLE h = CreateSemaphoreA(NULL, 0, maxCount, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    return h;
}

bool XlReleaseSemaphore(XlHandle h, int releaseCount, int* previousCount)
{
    return ReleaseSemaphore(h, releaseCount, (LPLONG)previousCount) != FALSE;
}

XlHandle XlCreateEvent(bool manualReset)
{
    HANDLE h = CreateEventA(NULL, manualReset, FALSE, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    return h;
}

#if 0
bool XlResetEvent(XlHandle h)
{
    return ResetEvent(h) != FALSE;
}
#endif

bool XlSetEvent(XlHandle h)
{
    return SetEvent(h) != FALSE;
}

#if 0
bool XlPulseEvent(XlHandle h)
{
    return PulseEvent(h) != FALSE;
}
#endif

uint32_t XlWaitForMultipleSyncObjects(uint32_t waitCount, XlHandle waitObjects[], bool waitAll, uint32_t waitTime, bool alterable)
{
    static_assert(sizeof(XlHandle) == sizeof(HANDLE));
    return FromWinWaitResult(WaitForMultipleObjectsEx(waitCount, waitObjects, waitAll ? TRUE : FALSE, waitTime, alterable));
}

void ChDir(const utf8 path[])                          { SetCurrentDirectoryA((const char*)path); }
void DeleteFile(const utf8 path[]) { auto result = ::DeleteFileA((char*)path); (void)result; }

#if XLE_GET_MODULE_PATH_ENABLE

void GetProcessPath(utf8 dst[], size_t bufferCount)    { GetModuleFileNameA(NULL, (char*)dst, (DWORD)bufferCount); }

void GetModulePath(utf8 dst[], size_t bufferCount, const utf8 moduleFilename[])
{
    if (!bufferCount) return;
    auto moduleHandle = GetModuleHandleA(moduleFilename);       // GetModuleHandleA does not increase ref count
    if (moduleHandle == INVALID_HANDLE_VALUE) {
        dst[0] = '\0';
        return;
    }

    GetModuleFileNameA(moduleHandle, (char*)dst, (DWORD)bufferCount);
}

#else

void GetProcessPath(utf8 dst[], size_t bufferCount) { dst[0] = '\0'; }
void GetModulePath(utf8 dst[], size_t bufferCount, const utf8 moduleFilename[]) { dst[0] = '\0'; }

#endif

#if XLE_GET_MODULE_FILE_TIME_ENABLE

FileTime GetModuleFileTime()
{
    char path[MaxPath];
    GetModuleFileNameA(NULL, path, MaxPath);

    LOADED_IMAGE loadedImage;
    XlZeroMemory(loadedImage);
    bool succeeded = MapAndLoad(path, nullptr, &loadedImage, FALSE, TRUE);
    if (!succeeded)
        return 0;

    // we only get the low 32 bits of the timestamp from this -- 
    FileTime result = loadedImage.FileHeader->FileHeader.TimeDateStamp;

    succeeded = UnMapAndLoad(&loadedImage);
    assert(succeeded);

    return result;
}

FileTime GetModuleFileTime(const utf8 moduleFilename[])
{
    char path[MaxPath];
    GetModulePath(path, dimof(path), moduleFilename);

    LOADED_IMAGE loadedImage;
    XlZeroMemory(loadedImage);
    bool succeeded = MapAndLoad(path, nullptr, &loadedImage, FALSE, TRUE);
    if (!succeeded)
        return 0;

    // we only get the low 32 bits of the timestamp from this -- 
    FileTime result = loadedImage.FileHeader->FileHeader.TimeDateStamp;

    succeeded = UnMapAndLoad(&loadedImage);
    assert(succeeded);

    return result;
}

#else

FileTime GetModuleFileTime() { return 0; }
FileTime GetModuleFileTime(const utf8 moduleFilename[]) { return 0; }

#endif

std::string GetAppDataPath()
{
    // Requires Vista or later
    PWSTR wpath;
    auto hres = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &wpath);
    if (!SUCCEEDED(hres)) {
        CoTaskMemFree(wpath);
        return {};
    }
    std::wstring temp = wpath;
    CoTaskMemFree(wpath);
    return Conversion::Convert<std::string>(temp);
}

std::optional<std::string> ModalSelectFolderDialog(StringSection<> initialFolder)
{
    // Windows Vista API for common dialogs
    // See https://learn.microsoft.com/en-us/windows/win32/shell/common-file-dialog

    IFileDialog *pfd = nullptr;
    auto hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (!SUCCEEDED(hr)) return {};
    auto cleanup0 = AutoCleanup([pfd]() { pfd->Release(); });

    DWORD dwFlags = 0;
    hr = pfd->GetOptions(&dwFlags);
    if (!SUCCEEDED(hr)) return {};
    hr = pfd->SetOptions(dwFlags | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
    if (!SUCCEEDED(hr)) return {};

    if (!initialFolder.IsEmpty()) {
        std::wstring winitial = Conversion::Convert<std::wstring>(initialFolder);
        IShellItem *psi = nullptr;
        hr = SHCreateItemFromParsingName(winitial.c_str(), nullptr, IID_PPV_ARGS(&psi));
        if (SUCCEEDED(hr)) {
            hr = pfd->SetFolder(psi);
            assert(SUCCEEDED(hr));
        }
        if (psi) psi->Release();
    }

    hr = pfd->Show(NULL);
    if (!SUCCEEDED(hr)) return {};

    IShellItem *psiResult;
    hr = pfd->GetResult(&psiResult);
    if (!SUCCEEDED(hr)) return {};

    PWSTR pszFilePath = nullptr;
    hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
    if (!SUCCEEDED(hr)) { CoTaskMemFree(pszFilePath); return {}; }

    std::wstring wresult = pszFilePath;
    CoTaskMemFree(pszFilePath);
    psiResult->Release();

    return Conversion::Convert<std::string>(wresult);
}

void MessageUser(StringSection<> text, StringSection<> title)
{
    ::MessageBoxA(nullptr, text.AsString().c_str(), title.AsString().c_str(), MB_OK);
}

bool CopyToSystemClipboard(StringSection<> text)
{
#if 0
    // See https://learn.microsoft.com/en-us/windows/win32/dataxchg/using-the-clipboard
    if (!OpenClipboard(nullptr))
        return false; 

    auto hglbCopy = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(TCHAR)); 
    if (!hglbCopy) { 
        CloseClipboard(); 
        return FALSE; 
    }
    auto lptstrCopy = (TCHAR*)GlobalLock(hglbCopy); 
    static_assert(sizeof(char) == sizeof(decltype(text)::value_type));
    std::memcpy(lptstrCopy, text.begin(), text.size() * sizeof(TCHAR)); 
    lptstrCopy[text.size()] = 0;
    GlobalUnlock(hglbCopy); 

    if (!EmptyClipboard()) {
        CloseClipboard();
        GlobalFree(hglbCopy);
        return false;
    }

    SetClipboardData(CF_TEXT, hglbCopy);

    CloseClipboard();
    return true;
#else
    return false;
#endif
}

bool OpenExternalBrowser(StringSection<> link)
{
#if 0
    if (link.IsEmpty()) return false;

    // refuse anything that looks like it might be file -- we're begin quite conservative here, and will 
    // reject some things that might actually be valid URLs
    if (link[0] == '/' || link[0] == '\\') return false;
    auto splitPath = MakeFileNameSplitter(link);
    if (!splitPath.Stem().IsEmpty() && !XlEqString(splitPath.Stem(), "http:") && !XlEqString(splitPath.Stem(), "https:")) return false;
    auto* startServer = XlFindNot({splitPath.Stem().end(), link.end()}, "/\\");
    if (!startServer) return false;
    auto* firstSep = XlFindAnyChar({startServer, link.end()}, "\\/");
    if (firstSep && XlFindChar(MakeStringSection(firstSep, link.end()), '.')) return false;       // reject dots after the first separator -- again, valid for urls, but we'll reject conservatively
    if (XlFindStringI(link, ".exe") || XlFindStringI(link, ".bat") || XlFindStringI(link, ".cmd")) return false;
    auto dotCom = XlFindStringI(link, ".com");
    if (dotCom && (dotCom+4) == link.end() && splitPath.Stem().IsEmpty()) return false;       // must allow .com in the server name, but try to avoid it if it's not explicitly a url

    std::string finalLink;
    // stem validated as either http or https already
    if (!splitPath.Stem().IsEmpty()) finalLink = link.AsString();
    else finalLink = Concatenate("https://", link);

    // We must have called CoInitializeEx() in this thread. Just in case, we'll do it now.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);

    HINSTANCE res = ShellExecuteA(nullptr, "open", finalLink.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return ((size_t)res) > 32;        // as per msvc docs, only numbers greater than 32 are success
#endif
    return false;
}

bool OpenAppDataFolder(StringSection<> subFolder)
{
#if 0
    if (subFolder.IsEmpty()) return false;
    if (std::find(subFolder.begin(), subFolder.end(), '.') != subFolder.end()) return false;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);

    auto finalLink = Concatenate(GetAppDataPath(), "\\", subFolder);
    HINSTANCE res = ShellExecuteA(nullptr, "open", finalLink.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return ((size_t)res) > 32;        // as per msvc docs, only numbers greater than 32 are success
#endif
    return false;
}

void SetThreadName(StringSection<> text)
{
#if 0
    SetThreadDescription(GetCurrentThread(), Conversion::Convert<std::wstring>(text).c_str());
#endif
}

// void GetProcessPath(ucs2 dst[], size_t bufferCount)    { GetModuleFileNameW(NULL, (wchar_t*)dst, (DWORD)bufferCount); }
// void ChDir(const ucs2 path[])                          { SetCurrentDirectoryW((const wchar_t*)path); }
// void DeleteFile(const ucs2 path[]) { auto result = ::DeleteFileW((wchar_t*)path); (void)result; }

void MoveFile(const utf8 destination[], const utf8 source[])
{
    MoveFileA((const char*)source, (const char*)destination);
}

#if defined(GetCommandLine)
    #error GetCommandLine is macro'ed, which will cause a linker error here
#endif
const char* GetCommandLine()
{
    return GetCommandLineA();
}

std::string SystemErrorCodeAsString(int errorCode)
{
    LPVOID lpMsgBuf;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL);

        // FORMAT_MESSAGE_FROM_SYSTEM will typically give us a new line
        // at the end of the string. We can strip it off here (assuming we have write
        // access to the buffer). We're going to get rid of any terminating '.' as well.
        // Note -- assuming 8 bit base character width here (ie, ASCII, UTF8)
    if (lpMsgBuf) {
        auto *end = XlStringEnd((char*)lpMsgBuf);
        while ((end - 1) > lpMsgBuf && (*(end - 1) == '\n' || *(end - 1) == '\r' || *(end-1) == '.')) {
            --end;
            *end = '\0';
        }

        std::string result = (const char*)lpMsgBuf;
        LocalFree(lpMsgBuf);
        return result;
    } else
        return std::to_string(errorCode);
}

namespace Windows
{
    ExtensionFunctions& GetExtensionFunctions_Internal(EmulateableVersion emulateableVersion)
    {
        // todo -- use AttachablePtrs
        static ExtensionFunctions extFns;
        static bool init = false;
        if (!init) {
            if (HMODULE userModule = Fn_LoadLibrary("user32.dll")) {
                extFns._attachedModules.push_back(userModule);
                if (emulateableVersion >= EmulateableVersion::Windows10_16)
                    extFns.Fn_EnableNonClientDpiScaling = (decltype(extFns.Fn_EnableNonClientDpiScaling))Fn_GetProcAddress(userModule, "EnableNonClientDpiScaling");
                if (emulateableVersion >= EmulateableVersion::Windows10_16)
                    extFns.Fn_GetWindowDpiAwarenessContext = (decltype(extFns.Fn_GetWindowDpiAwarenessContext))Fn_GetProcAddress(userModule, "GetWindowDpiAwarenessContext");
                if (emulateableVersion >= EmulateableVersion::Windows10_17)
                    extFns.Fn_SetProcessDpiAwarenessContext = (decltype(extFns.Fn_SetProcessDpiAwarenessContext))Fn_GetProcAddress(userModule, "SetProcessDpiAwarenessContext");
                if (emulateableVersion >= EmulateableVersion::WindowsVista)
                    extFns.Fn_SetProcessDPIAware = (decltype(extFns.Fn_SetProcessDPIAware))Fn_GetProcAddress(userModule, "SetProcessDPIAware");
                if (emulateableVersion >= EmulateableVersion::WindowsVista)
                    extFns.Fn_GetDpiForWindow = (decltype(extFns.Fn_GetDpiForWindow))Fn_GetProcAddress(userModule, "GetDpiForWindow");
            }

            if (HMODULE shcore = Fn_LoadLibrary("shcore.dll")) {
                extFns._attachedModules.push_back(shcore);
                if (emulateableVersion >= EmulateableVersion::Windows8_1)
                    extFns.Fn_SetProcessDpiAwareness = (decltype(extFns.Fn_SetProcessDpiAwareness))Fn_GetProcAddress(shcore, "SetProcessDpiAwareness");
                if (emulateableVersion >= EmulateableVersion::Windows8_1)
                    extFns.Fn_GetDpiForMonitor = (decltype(extFns.Fn_GetDpiForMonitor))Fn_GetProcAddress(shcore, "GetDpiForMonitor");
            }

            extFns._emulating = emulateableVersion;
            init = true;
        }
        // If you hit the following assert, you're probably calling EnumlateWindowsVersion too late
        assert(emulateableVersion == EmulateableVersion::Latest || extFns._emulating == emulateableVersion);
        return extFns;
    }

    ExtensionFunctions& GetExtensionFunctions()
    {
        return GetExtensionFunctions_Internal(EmulateableVersion::Latest);
    }

    ExtensionFunctions::~ExtensionFunctions()
    {
        for (auto l:_attachedModules)
            Fn_FreeLibrary(l);
    }

    void EnumlateWindowsVersion(EmulateableVersion version)
    {
        GetExtensionFunctions_Internal(version);
    }
}

void ConfigureDPIAwareness()
{
    auto& extFns = Windows::GetExtensionFunctions();

    // Almost all applications will want to defeat the Windows built-in DPI behaviour
    // We do this by telling Windows that we will handle the DPI behaviour ourselves.
    // This causes windows to give us actual pixel values for common functions (GetClientRect(), etc)
    // instead of scaled DPI values
    // Client applications can then selectively handle the DPI behaviour within the gfx api context
    //
    // However note that Windows DPI behaviour has made several changes between Vista and Windows 10,
    // and as a result behaviour might be slightly different on each platform
    if (extFns.Fn_SetProcessDpiAwarenessContext)
        extFns.Fn_SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    else if (extFns.Fn_SetProcessDpiAwareness)
        extFns.Fn_SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    else if (extFns.Fn_SetProcessDPIAware)
        extFns.Fn_SetProcessDPIAware();
}

std::fstream CreateAppDataFile(StringSection<> appName, StringSection<> fileName, std::ios_base::openmode openMode)
{
#if 1
    std::string santizedAppName = appName.AsString();
    const char badCharacters[] { '#', '%', '&', '{', '}', '\\', '<', '>', '*', '?', '/', ' ', '$', '!', '\'', '\"', ':', '@', '+', '`', '|', '=' };
    for (auto& c:santizedAppName)
        if (std::find(badCharacters, ArrayEnd(badCharacters), c) != ArrayEnd(badCharacters))
            c = '-';

    auto appDataPath = GetAppDataPath();
    if (appDataPath.empty())
        Throw(std::runtime_error("Could not get AppData path in CreateAppDataFile"));

    std::string fullDirectory = Concatenate(appDataPath, "\\", santizedAppName);
    CreateDirectoryA(fullDirectory.c_str(), nullptr); // try to create this, if it doesn't exist (don't do this recursively, we just want to create the app folder if we can)

    std::string fullfname = Concatenate(fullDirectory, "\\", fileName);
    auto split = MakeFileNameSplitter(fullfname);
    if (openMode & std::ios::out) {
        auto bkupFile = Concatenate(split.StemPathAndFilename(), ".0", split.ExtensionWithPeriod());
        DeleteFileA(bkupFile.c_str());
        if (!(openMode & std::ios::in)) {
            MoveFileA(fullfname.c_str(), bkupFile.c_str());
        } else {
            CopyFileA(fullfname.c_str(), bkupFile.c_str(), FALSE);
        }
    }
    return std::fstream { fullfname.c_str(), openMode };
#else
    return std::fstream { fileName.AsString().c_str(), openMode };
#endif
}

#if 0

void XlStartSelfProcess(const char* commandLine, int delaySec, bool terminateSelf)
{
    if (delaySec < 0) {
        delaySec = 0;
    }

    if (!commandLine) {
        commandLine = GetCommandLineA();

        // skip first argument
        while (*commandLine && !XlIsSpace(*commandLine)) {
            ++commandLine;
        }

        // trim blank
        while (*commandLine && XlIsSpace(*commandLine)) {
            ++commandLine;
        }
    }

    char pathBuf[1024];
    GetTempPathA(dimof(pathBuf), pathBuf);

    char tempFileName[256];
    XlFormatString(tempFileName, dimof(tempFileName), "__resetart_%u", XlGetCurrentProcessId());
    
    char tempFilePathName[1024];
    XlMakePath(tempFilePathName, NULL, pathBuf, tempFileName, "bat");

    XLHFILE file = XlOpenFile(tempFilePathName, "w");
    if (IS_VALID_XLHFILE(file)) {
        char buf[2048];
        XlFormatString(buf, dimof(buf), "sleep %d\r\n", delaySec);
        XlWriteFile(file, buf, (uint32_t)XlStringSize(buf));

        char processImage[1024];
        GetModuleFileNameExA(XlGetCurrentProcess(), NULL, processImage, dimof(processImage));
        XlFormatString(buf, dimof(buf), "start %s %s\r\n", processImage, commandLine);
        XlWriteFile(file, buf, (uint32_t)XlStringSize(buf));

        XlFormatString(buf, dimof(buf), "sleep 10\r\n", delaySec);
        XlWriteFile(file, buf, (uint32_t)XlStringSize(buf));

        XlCloseFile(file);
        ShellExecute(NULL, NULL, tempFilePathName, NULL, NULL, SW_SHOW);

        if (terminateSelf) {
            TerminateProcess(XlGetCurrentProcess(), 0);
        }
    }
}

// win32 condition-variable work-around

void (WINAPI *InitializeConditionVariable_fn)(PCONDITION_VARIABLE) = NULL;
BOOL (WINAPI *SleepConditionVariableCS_fn)(PCONDITION_VARIABLE, PCRITICAL_SECTION, DWORD) = NULL;
void (WINAPI *WakeAllConditionVariable_fn)(PCONDITION_VARIABLE) = NULL;
void (WINAPI *WakeConditionVariable_fn)(PCONDITION_VARIABLE) = NULL;
void (WINAPI *DestroyConditionVariable_fn)(PCONDITION_VARIABLE) = NULL;

static void WINAPI dummy(PCONDITION_VARIABLE)
{
	// do nothing!
}

static void WINAPI intl_win32_cond_init(PCONDITION_VARIABLE p)
{
	intl_win32_cond* cond = reinterpret_cast<intl_win32_cond*>(p);

	if (InitializeCriticalSectionAndSpinCount(&cond->lock, 400) == 0) {
		return;
	}
	if ((cond->event = CreateEvent(NULL,TRUE,FALSE,NULL)) == NULL) {
		DeleteCriticalSection(&cond->lock);
		return;
	}

	cond->n_waiting = cond->n_to_wake = cond->generation = 0;
}


static void WINAPI intl_win32_cond_destroy(PCONDITION_VARIABLE p)
{
	intl_win32_cond* cond = reinterpret_cast<intl_win32_cond*>(p);
	DeleteCriticalSection(&cond->lock);
	CloseHandle(cond->event);
}

static BOOL WINAPI intl_win32_cond_wait(PCONDITION_VARIABLE p, PCRITICAL_SECTION m, DWORD ms)
{
	intl_win32_cond *cond = reinterpret_cast<intl_win32_cond*>(p);

	int generation_at_start;
	int waiting = 1;
	BOOL result = FALSE;

	DWORD start, end;
	DWORD org = ms;

	EnterCriticalSection(&cond->lock);
	++cond->n_waiting;
	generation_at_start = cond->generation;
	LeaveCriticalSection(&cond->lock);

	LeaveCriticalSection(m);

	start = GetTickCount();
	do {
		DWORD res;
		res = WaitForSingleObject(cond->event, ms);
		EnterCriticalSection(&cond->lock);
		if (cond->n_to_wake &&
		    cond->generation != generation_at_start) {
			--cond->n_to_wake;
			--cond->n_waiting;
			result = 2;
			waiting = 0;
			goto out;
		} else if (res != WAIT_OBJECT_0) {
			result = (res==WAIT_TIMEOUT) ? TRUE : FALSE;
			--cond->n_waiting;
			waiting = 0;
			goto out;
		} else if (ms != INFINITE) {
			end = GetTickCount();
			if (start + org <= end) {
				result = 1; /* Timeout */
				--cond->n_waiting;
				waiting = 0;
				goto out;
			} else {
				ms = start + org - end;
			}
		}
		/* If we make it here, we are still waiting. */
		if (cond->n_to_wake == 0) {
			/* There is nobody else who should wake up; reset
			 * the event. */
			ResetEvent(cond->event);
		}
	out:
		LeaveCriticalSection(&cond->lock);
	} while (waiting);

	EnterCriticalSection(m);

	EnterCriticalSection(&cond->lock);
	if (!cond->n_waiting)
		ResetEvent(cond->event);
	LeaveCriticalSection(&cond->lock);

	return result;
}


static void WINAPI intl_win32_cond_wake(PCONDITION_VARIABLE p) 
{
	intl_win32_cond* cond = reinterpret_cast<intl_win32_cond*>(p);
	EnterCriticalSection(&cond->lock);

	++cond->n_to_wake;
	cond->generation++;
	SetEvent(cond->event);
	LeaveCriticalSection(&cond->lock);
}

static void WINAPI intl_win32_cond_wake_all(PCONDITION_VARIABLE p) 
{
	intl_win32_cond* cond = reinterpret_cast<intl_win32_cond*>(p);
	EnterCriticalSection(&cond->lock);

	cond->n_to_wake = cond->n_waiting;

	cond->generation++;
	SetEvent(cond->event);
	LeaveCriticalSection(&cond->lock);
}

static struct win32_condvar_init
{

	win32_condvar_init() 
	{

		HMODULE h = GetModuleHandle(TEXT("kernel32.dll"));
		if (h == NULL) {
			return;
		}

	#define LOAD(name)				\
		(*((FARPROC*)&name##_fn)) = GetProcAddress(h, #name)

		LOAD(InitializeConditionVariable);
		LOAD(SleepConditionVariableCS);
		LOAD(WakeAllConditionVariable);
		LOAD(WakeConditionVariable);

		bool res =  InitializeConditionVariable_fn && 
					SleepConditionVariableCS_fn &&
					WakeConditionVariable_fn &&
					WakeAllConditionVariable_fn; 

		if (!res) {
			InitializeConditionVariable_fn = intl_win32_cond_init;
			DestroyConditionVariable_fn = intl_win32_cond_destroy;
			SleepConditionVariableCS_fn = intl_win32_cond_wait;
			WakeConditionVariable_fn = intl_win32_cond_wake;
			WakeAllConditionVariable_fn =intl_win32_cond_wake_all;
		} else {
			DestroyConditionVariable_fn = dummy;
		}
	}
} prepare_condvar;

#endif

}

