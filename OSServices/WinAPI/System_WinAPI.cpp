// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "System_WinAPI.h"
#include "../RawFS.h"
#include "../TimeUtils.h"
#include "../../Core/Prefix.h"
#include "../../Core/Types.h"
#include "../../Utility/Threading/LockFree.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/StringFormat.h"
#include "IncludeWindows.h"
#include <process.h>
#include <share.h>
#include <time.h>

#include <psapi.h>
#include <shellapi.h>
#include <ImageHlp.h>

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
bool XlIsCriticalSectionLocked(void* cs) 
{
    CRITICAL_SECTION* csPtr = reinterpret_cast<CRITICAL_SECTION*>(cs);
    return csPtr->RecursionCount > 0 && csPtr->OwningThread == (HANDLE)(size_t)GetCurrentThreadId();
}

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

bool XlResetEvent(XlHandle h)
{
    return ResetEvent(h) != FALSE;
}

bool XlSetEvent(XlHandle h)
{
    return SetEvent(h) != FALSE;
}

bool XlPulseEvent(XlHandle h)
{
    return PulseEvent(h) != FALSE;
}

uint32_t XlWaitForMultipleSyncObjects(uint32_t waitCount, XlHandle waitObjects[], bool waitAll, uint32_t waitTime, bool alterable)
{
    static_assert(sizeof(XlHandle) == sizeof(HANDLE));
    return FromWinWaitResult(WaitForMultipleObjectsEx(waitCount, waitObjects, waitAll ? TRUE : FALSE, waitTime, alterable));
}

void GetProcessPath(utf8 dst[], size_t bufferCount)    { GetModuleFileNameA(NULL, (char*)dst, (DWORD)bufferCount); }
void ChDir(const utf8 path[])                          { SetCurrentDirectoryA((const char*)path); }
void DeleteFile(const utf8 path[]) { auto result = ::DeleteFileA((char*)path); (void)result; }

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
    }

    std::string result = (const char*)lpMsgBuf;
    LocalFree(lpMsgBuf);
    return result;
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
