// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RunLoop_WinAPI.h"
#include "InputTranslator.h"
#include "../OverlappedWindow.h"
#include "../InputSnapshot.h"
#include "../DisplaySettings.h"
#include "../Log.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/UTFUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/FunctionUtils.h"
#include "../../Core/Exceptions.h"
#include <queue>
#include <windowsx.h>

#include "WinAPIWrapper.h"

namespace OSServices
{
    void OnDisplaySettingsChange(unsigned, unsigned);

	static std::shared_ptr<IOSRunLoop> s_osRunLoop;

	IOSRunLoop* GetOSRunLoop()
	{
		return s_osRunLoop.get();
	}

	void SetOSRunLoop(const std::shared_ptr<IOSRunLoop>& runLoop)
	{
		s_osRunLoop = runLoop;
	}

    class CurrentModule
    {
    public:
        CurrentModule();
        ~CurrentModule();

        uint64      HashId();
        ::HMODULE   Handle();
        ::HINSTANCE HInstance();

        static CurrentModule& GetInstance();

    protected:
        uint64 _moduleHash;
    };

    inline uint64       CurrentModule::HashId()       { return _moduleHash; }
    inline ::HMODULE    CurrentModule::Handle()       { return ::GetModuleHandle(0); }
    inline ::HINSTANCE  CurrentModule::HInstance()    { return (::HINSTANCE)(::GetModuleHandle(0)); }

    CurrentModule::CurrentModule()
    {
        wchar_t buffer[MaxPath];
        auto filenameLength = ::GetModuleFileNameW(Handle(), (LPWSTR)buffer, dimof(buffer));
        _moduleHash = Utility::Hash64(buffer, &buffer[filenameLength]);
    }

    CurrentModule::~CurrentModule() {}
    
    CurrentModule& CurrentModule::GetInstance()
    {
        static CurrentModule result;
        return result;
    }

    class Window::Pimpl
    {
    public:
        HWND        _hwnd;

        bool        _activated;
        std::shared_ptr<InputTranslator> _inputTranslator;

		std::shared_ptr<OSRunLoop_BasicTimer> _runLoop;
        Signal<SystemMessageVariant&&> _onMessageImmediate;
        std::deque<SystemMessageVariant> _systemMessages;

        std::shared_ptr<DisplaySettingsManager> _displaySettingsManager;
        DisplaySettingsManager::MonitorId _capturedMonitorId = ~0u;

        bool _shown = false;

        Pimpl() : _hwnd(HWND(INVALID_HANDLE_VALUE)), _activated(false) {}
    };

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        switch (msg) {
        case WM_CLOSE:
            ::DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        case WM_ERASEBKGND:
            return 0;       // (suppress these)

        case WM_DISPLAYCHANGE:
            {
                OnDisplaySettingsChange(unsigned(lparam & 0xffff), unsigned(lparam >> 16u));
                auto pimpl = (Window::Pimpl*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
                if (pimpl && pimpl->_hwnd == hwnd) {
                    pimpl->_onMessageImmediate.Invoke(SystemDisplayChange{});
                    pimpl->_systemMessages.emplace_back(SystemDisplayChange{});

                    // If we are capturing a monitor, we should realign the window with the new desktop geometry
                    // However, our "captured" monitor may have begun invalidated -- in which case we need to release that capture
                    if (pimpl->_displaySettingsManager && pimpl->_capturedMonitorId != ~0u) {
                        if (pimpl->_displaySettingsManager->IsValidMonitor(pimpl->_capturedMonitorId)) {
                            auto geometry = pimpl->_displaySettingsManager->GetDesktopGeometryForMonitor(pimpl->_capturedMonitorId);
                            BOOL hres2 = ::SetWindowPos(
                                pimpl->_hwnd,
                                HWND_TOPMOST,
                                geometry._x, geometry._y, geometry._width, geometry._height,
                                SWP_FRAMECHANGED | SWP_NOREDRAW | SWP_NOCOPYBITS | (pimpl->_shown ? SWP_SHOWWINDOW : 0));
                            assert(hres2);
                        } else {
                            pimpl->_capturedMonitorId = ~0u;
                        }
                    }
                }
            }
            break;

        case WM_DPICHANGED:
            {
                // DPI changed. Windows provides a suggested new rectangle; we should switch so long as we're not capturing a monitor
                auto* suggestedNewSize = (RECT*)lparam;
                assert(suggestedNewSize);
                auto pimpl = (Window::Pimpl*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
                if (pimpl && pimpl->_hwnd == hwnd) {
                    if (pimpl->_capturedMonitorId == ~0u) {
                        BOOL hres2 = ::SetWindowPos(
                            pimpl->_hwnd,
                            nullptr,
                            suggestedNewSize->left, suggestedNewSize->top, suggestedNewSize->right-suggestedNewSize->left, suggestedNewSize->bottom-suggestedNewSize->top,
                            SWP_NOREDRAW | SWP_NOCOPYBITS | SWP_NOZORDER | SWP_NOACTIVATE | (pimpl->_shown ? SWP_SHOWWINDOW : 0));
                        assert(hres2);
                    }
                }
            }
            return 0;

        case WM_NCCREATE:
            {
                auto& extFn = Windows::GetExtensionFunctions();
                if (extFn.Fn_EnableNonClientDpiScaling)
                    extFn.Fn_EnableNonClientDpiScaling(hwnd);        // requires windows 10
            }
            break;

        case WM_ACTIVATE:
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_CHAR:
        case WM_SIZE:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            {
                auto pimpl = (Window::Pimpl*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
                if (!pimpl || pimpl->_hwnd != hwnd) break;

                auto* inputTrans = pimpl->_inputTranslator.get();
                if (!pimpl->_activated) { inputTrans = nullptr; }

                std::optional<InputSnapshot> generatedSnapshot;
                bool suppressDefaultHandler = false;

                switch (msg) {
                case WM_ACTIVATE:
                    pimpl->_activated = wparam != WA_INACTIVE;
					if (pimpl->_inputTranslator) pimpl->_inputTranslator->OnFocusChange(pimpl->_activated);

                    // In our "capture monitor" logic, if we're not activated, we shouldn't show the window
                    // at all
                    // We could also do this logic in WM_ACTIVATEAPP; however this way will ensure we get a minimize
                    // if a popup from this app interrupts us
                    if (pimpl->_capturedMonitorId != ~0u && pimpl->_displaySettingsManager) {
                        if (wparam == WA_INACTIVE) {
                            // become inactive -- minimize
                            ShowWindow(pimpl->_hwnd, SW_SHOWMINNOACTIVE);
                        } else {
                            ShowWindow(pimpl->_hwnd, SW_RESTORE);
                        }
                    }
                    break;

                case WM_MOUSEMOVE:
                    if (pimpl->_activated && inputTrans)
                        generatedSnapshot = inputTrans->OnMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                    break;

                case WM_LBUTTONDOWN:    if (inputTrans) { generatedSnapshot = inputTrans->OnMouseButtonChange(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0, true); }    break;
                case WM_RBUTTONDOWN:    if (inputTrans) { generatedSnapshot = inputTrans->OnMouseButtonChange(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 1, true); }    break;
                case WM_MBUTTONDOWN:    if (inputTrans) { generatedSnapshot = inputTrans->OnMouseButtonChange(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 2, true); }    break;

                case WM_LBUTTONUP:      if (inputTrans) { generatedSnapshot = inputTrans->OnMouseButtonChange(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0, false); }   break;
                case WM_RBUTTONUP:      if (inputTrans) { generatedSnapshot = inputTrans->OnMouseButtonChange(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 1, false); }   break;
                case WM_MBUTTONUP:      if (inputTrans) { generatedSnapshot = inputTrans->OnMouseButtonChange(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 2, false); }   break;

                case WM_LBUTTONDBLCLK:  if (inputTrans) { generatedSnapshot = inputTrans->OnMouseButtonDblClk(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0); }   break;
                case WM_RBUTTONDBLCLK:  if (inputTrans) { generatedSnapshot = inputTrans->OnMouseButtonDblClk(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 1); }   break;
                case WM_MBUTTONDBLCLK:  if (inputTrans) { generatedSnapshot = inputTrans->OnMouseButtonDblClk(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 2); }   break;

                case WM_MOUSEWHEEL:     if (inputTrans) { generatedSnapshot = inputTrans->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wparam)); }    break;

                case WM_SYSKEYDOWN:
                case WM_SYSKEYUP:
                case WM_KEYDOWN:
                case WM_KEYUP:
                    if (inputTrans) { generatedSnapshot = inputTrans->OnKeyChange((unsigned)wparam, (msg==WM_KEYDOWN) || (msg==WM_SYSKEYDOWN)); }
                    suppressDefaultHandler =  (msg==WM_SYSKEYUP || msg==WM_SYSKEYDOWN);        // (suppress default windows behaviour for these system keys)
                    break;

                case WM_CHAR:
                    if (inputTrans) { generatedSnapshot = inputTrans->OnChar((wchar_t)wparam); }
                    break;

                case WM_SIZE:
                    {
                        // we could also use WM_WINDOWPOSCHANGED, but that adds some extra complcation
                        // it's harder to tell when the app is minimized
                        // & we just get more spam with that message

                        // first, remove any other WindowResize messages that have been queued
                        auto i = std::remove_if(
                            pimpl->_systemMessages.begin(), pimpl->_systemMessages.end(), 
                            [](auto& v) { return std::holds_alternative<WindowResize>(v); });
                        pimpl->_systemMessages.erase(i, pimpl->_systemMessages.end());

                        signed newWidth = ((int)(short)LOWORD(lparam)), newHeight = ((int)(short)HIWORD(lparam));
                        pimpl->_systemMessages.emplace_back(WindowResize{newWidth, newHeight});
                    }
                    break;
                }

                if (generatedSnapshot)
                    pimpl->_systemMessages.emplace_back(*generatedSnapshot);

                if (suppressDefaultHandler)
                    return true;
            }
            break;

		case WM_TIMER:
			{
				auto pimpl = (Window::Pimpl*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
				pimpl->_runLoop->OnOSTrigger(wparam);
			}
			break;
        }

        return DefWindowProc(hwnd, msg, wparam, lparam);
    }

    void Window::Show(bool newState)
    {
        _pimpl->_shown = newState;
        ::ShowWindow(_pimpl->_hwnd, newState ? SW_SHOWNORMAL : SW_HIDE);
    }

    void Window::Close()
    {
        // We can either post a WM_CLOSE (emulating click the window X button)
        // or call ::DestroyWindow(_pimpl->_hwnd) directly.
        ::PostMessageA(_pimpl->_hwnd, WM_CLOSE, 0, 0);
    }

    static constexpr LONG s_styleOverlapped = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
    static constexpr LONG s_styleExOverlapped = 0;

    static constexpr LONG s_styleFullscreen = WS_POPUP;
    static constexpr LONG s_styleExFullscreen = WS_EX_TOPMOST;

    void Window::CaptureMonitor(
        std::shared_ptr<DisplaySettingsManager> displaySettings,
        unsigned monitorId)
    {
        assert(displaySettings);
        assert(displaySettings->IsValidMonitor(monitorId));
        assert(!_pimpl->_displaySettingsManager && _pimpl->_capturedMonitorId == ~0u);      // attempting to capture multiple times
        _pimpl->_displaySettingsManager = std::move(displaySettings);
        _pimpl->_capturedMonitorId = monitorId;

        ::SetLastError(0);
        auto hres = ::SetWindowLongPtrA(_pimpl->_hwnd, GWL_STYLE, s_styleFullscreen);
        assert(hres != 0 || GetLastError() == 0);
        hres = ::SetWindowLongPtrA(_pimpl->_hwnd, GWL_EXSTYLE, s_styleExFullscreen);
        assert(hres != 0 || GetLastError() == 0);

        // Note that we have to call ShowWindow if we're expecting the window to be visible; otherwise we
        // end up in some partially visible state
        auto geometry = _pimpl->_displaySettingsManager->GetDesktopGeometryForMonitor(_pimpl->_capturedMonitorId);
        BOOL hres2 = ::SetWindowPos(
            _pimpl->_hwnd,
            HWND_TOPMOST,
            geometry._x, geometry._y, geometry._width, geometry._height,
            SWP_FRAMECHANGED | SWP_NOREDRAW | SWP_NOCOPYBITS | (_pimpl->_shown ? SWP_SHOWWINDOW : 0));
        assert(hres2);
    }

    void Window::ReleaseMonitor()
    {
        _pimpl->_displaySettingsManager = nullptr;
        _pimpl->_capturedMonitorId = ~0u;

        ::SetLastError(0);
        auto hres = ::SetWindowLongPtrA(_pimpl->_hwnd, GWL_EXSTYLE, s_styleExOverlapped);
        assert(hres != 0 || GetLastError() == 0);
        hres = ::SetWindowLongPtrA(_pimpl->_hwnd, GWL_STYLE, s_styleOverlapped);
        assert(hres != 0 || GetLastError() == 0);

        // Note that we have to include SWP_SHOWWINDOW if we're expecting the window to be visible; otherwise we
        // end up in some partially visible state
        BOOL hres2 = ::SetWindowPos(
            _pimpl->_hwnd,
            HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOREDRAW | SWP_NOCOPYBITS | (_pimpl->_shown ? SWP_SHOWWINDOW : 0));
        assert(hres2);
    }

    unsigned Window::GetDPI() const
    {
        auto& extFn = Windows::GetExtensionFunctions();
        if (extFn.Fn_GetDpiForWindow)           // Windows 10
            return extFn.Fn_GetDpiForWindow(_pimpl->_hwnd);

        if (extFn.Fn_GetDpiForMonitor) {        // Windows 8.1
            UINT dpiX=0, dpiY=0;
            auto monitor = MonitorFromWindow(_pimpl->_hwnd, MONITOR_DEFAULTTONEAREST);
            auto hres = extFn.Fn_GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
            if (SUCCEEDED(hres) && dpiX)
                return dpiX;
        }

        // may not get good results on Vista

        return 96;      // normal DPI in Windows contexts
    }

    Window::Window() 
    {
        _pimpl = std::make_unique<Pimpl>();

            //
            //      ---<>--- Register window class ---<>---
            //

        Windows::WNDCLASSEX wc;
        XlZeroMemory(wc);
        XlSetMemory(&wc, 0, sizeof(wc));

        auto windowClassName = Conversion::Convert<std::string>(CurrentModule::GetInstance().HashId());

        wc.cbSize           = sizeof(wc);
        wc.style            = CS_OWNDC | CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc      = (::WNDPROC)&WndProc;
        wc.cbClsExtra       = 0;
        wc.cbWndExtra       = 0;
        wc.hInstance        = CurrentModule::GetInstance().Handle();
        wc.hIcon            = ::LoadIcon(CurrentModule::GetInstance().HInstance(), "IDI_ICON1");
        wc.hCursor          = ::LoadCursor(nullptr, IDC_ARROW); 
        wc.hbrBackground    = (HBRUSH)nullptr;
        wc.lpszMenuName     = 0;
        wc.lpszClassName    = windowClassName.c_str();
        wc.hIconSm          = NULL;

            //       (Ignore class registration failure errors)
        (*Windows::Fn_RegisterClassEx)(&wc);

            //
            //      ---<>--- Create the window itself ---<>---
            //
        _pimpl->_hwnd = (*Windows::Fn_CreateWindowEx)(
            s_styleExOverlapped, windowClassName.c_str(), 
            NULL, s_styleOverlapped, 
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            NULL, NULL, CurrentModule::GetInstance().HInstance(), NULL);

        if (!_pimpl->_hwnd || _pimpl->_hwnd == INVALID_HANDLE_VALUE)
            Throw(::Exceptions::BasicLabel( "Failure during windows construction" ));        // (note that a window class can be leaked by this.. But, who cares?)

        SetWindowLongPtr(_pimpl->_hwnd, GWLP_USERDATA, (LONG_PTR)_pimpl.get());

            //  Create input translator -- used to translate between windows messages
            //  and the cross platform input-handling interface
        _pimpl->_inputTranslator = std::make_shared<InputTranslator>(_pimpl->_hwnd);

		_pimpl->_runLoop = std::make_shared<OSRunLoop_BasicTimer>(_pimpl->_hwnd);
		SetOSRunLoop(_pimpl->_runLoop);

        auto& extFn = Windows::GetExtensionFunctions();
        if (extFn.Fn_GetWindowDpiAwarenessContext && extFn.Fn_GetWindowDpiAwarenessContext(_pimpl->_hwnd) == DPI_AWARENESS_CONTEXT_UNAWARE) {
            Log(Warning) << "Window is begin created in non-DPI aware mode. This is non-ideal and will lead to wierdness on some versions of Windows and some configurations" << std::endl;
            Log(Warning) << "In this mode, Windows will scale windows based on OS DPI settings for the output monitor" << std::endl;
            Log(Warning) << "Also in this mode, some graphics APIs, such as Vulkan, intentionally do not compensate for this, and as a result the" << std::endl;
            Log(Warning) << "density of pixels in the presentation target is not the same as actual video mode (ie, in 200% scaling mode, we will have one quarter of the number of pixels we're expecting)." << std::endl;
            Log(Warning) << "Most clients will want to enable DPI-aware mode (and possibly compensate for DPI within the graphics API context)" << std::endl;
            Log(Warning) << "To do that, either use the manifest file, or call ConfigureDPIAwareness()" << std::endl;
        }
    }

    Window::~Window()
    {
		SetOSRunLoop(nullptr);
		_pimpl->_inputTranslator.reset();
        ::DestroyWindow(_pimpl->_hwnd);
        auto windowClassName = Conversion::Convert<std::string>(CurrentModule::GetInstance().HashId());
        (*Windows::Fn_UnregisterClass)(windowClassName.c_str(), CurrentModule::GetInstance().Handle());
    }

    const void* Window::GetUnderlyingHandle() const
    {
        return _pimpl->_hwnd;
    }

    std::pair<Coord2, Coord2> Window::GetRect() const
    {
        RECT clientRect;
        GetClientRect(_pimpl->_hwnd, &clientRect);
        return std::make_pair(Coord2(clientRect.left, clientRect.top), Coord2(clientRect.right, clientRect.bottom));
    }

    void Window::Resize(unsigned width, unsigned height)
    {
        RECT adjusted { 0, 0, (LONG)width, (LONG)height };
        AdjustWindowRectEx(&adjusted, GetWindowStyle(_pimpl->_hwnd), FALSE, GetWindowExStyle(_pimpl->_hwnd));
        SetWindowPos(
            _pimpl->_hwnd, (HWND)INVALID_HANDLE_VALUE,
            0, 0, adjusted.right - adjusted.left, adjusted.bottom - adjusted.top,
            SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
    }

    void Window::SetTitle(const char titleText[])
    {
        SetWindowTextA(_pimpl->_hwnd, titleText);
    }

    void Window::CaptureAndHideCursor(bool newState)
    {
        _pimpl->_inputTranslator->CaptureAndHideCursor(newState);
    }

    Utility::Signal<SystemMessageVariant&&>& Window::OnMessageImmediate()
    {
        return _pimpl->_onMessageImmediate;
    }

    auto Window::SingleWindowMessagePump(Window& window) -> SystemMessageVariant
    {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                window._pimpl->_systemMessages.emplace_back(ShutdownRequest{});
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!window._pimpl->_systemMessages.empty()) {
            auto result = std::move(window._pimpl->_systemMessages.front());
            window._pimpl->_systemMessages.pop_front();
            return result;
        }

        bool foreground = window._pimpl->_activated;
        if (!foreground) {
            // Protection for cases where a popup in our process has stolen our activation
            DWORD foreWindowProcess;
            GetWindowThreadProcessId(GetForegroundWindow(), &foreWindowProcess);
            foreground = GetCurrentProcessId() == foreWindowProcess;
        }

        return Idle{foreground ? IdleState::Foreground : IdleState::Background};
    }

}
