// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RunLoop_WinAPI.h"
#include "InputTranslator.h"
#include "../OverlappedWindow.h"
#include "../InputListener.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/UTFUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/Conversion.h"
#include "../../OSServices/WinAPI/WinAPIWrapper.h"
#include "../../Core/Exceptions.h"
#include <windowsx.h>

namespace PlatformRig
{
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

    class OverlappedWindow::Pimpl
    {
    public:
        HWND        _hwnd;

        bool        _activated;
        std::shared_ptr<InputTranslator> _inputTranslator;

        std::vector<std::shared_ptr<IWindowHandler>> _windowHandlers;

		std::shared_ptr<OSRunLoop_BasicTimer> _runLoop;

        Pimpl() : _hwnd(HWND(INVALID_HANDLE_VALUE)), _activated(false) {}
    };

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        switch (msg) {
        case WM_CLOSE:
            PostQuitMessage(0);
            break;

        case WM_ERASEBKGND:
            return 0;       // (suppress these)

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
                auto pimpl = (OverlappedWindow::Pimpl*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
                if (!pimpl || pimpl->_hwnd != hwnd) break;

                auto* inputTrans = pimpl->_inputTranslator.get();
                if (!pimpl->_activated) { inputTrans = nullptr; }

                switch (msg) {
                case WM_ACTIVATE:
                    pimpl->_activated = wparam != WA_INACTIVE;
					if (inputTrans) inputTrans->OnFocusChange();
                    break;

                case WM_MOUSEMOVE:
                    if (pimpl->_activated && inputTrans)
                        inputTrans->OnMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                    break;

                case WM_LBUTTONDOWN:    if (inputTrans) { inputTrans->OnMouseButtonChange(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0, true); }    break;
                case WM_RBUTTONDOWN:    if (inputTrans) { inputTrans->OnMouseButtonChange(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 1, true); }    break;
                case WM_MBUTTONDOWN:    if (inputTrans) { inputTrans->OnMouseButtonChange(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 2, true); }    break;

                case WM_LBUTTONUP:      if (inputTrans) { inputTrans->OnMouseButtonChange(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0, false); }   break;
                case WM_RBUTTONUP:      if (inputTrans) { inputTrans->OnMouseButtonChange(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 1, false); }   break;
                case WM_MBUTTONUP:      if (inputTrans) { inputTrans->OnMouseButtonChange(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 2, false); }   break;

                case WM_LBUTTONDBLCLK:  if (inputTrans) { inputTrans->OnMouseButtonDblClk(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0); }   break;
                case WM_RBUTTONDBLCLK:  if (inputTrans) { inputTrans->OnMouseButtonDblClk(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 1); }   break;
                case WM_MBUTTONDBLCLK:  if (inputTrans) { inputTrans->OnMouseButtonDblClk(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 2); }   break;

                case WM_MOUSEWHEEL:     if (inputTrans) { inputTrans->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wparam)); }    break;

                case WM_SYSKEYDOWN:
                case WM_SYSKEYUP:
                case WM_KEYDOWN:
                case WM_KEYUP:
                    if (inputTrans) { inputTrans->OnKeyChange((unsigned)wparam, (msg==WM_KEYDOWN) || (msg==WM_SYSKEYDOWN)); }
                    if (msg==WM_SYSKEYUP || msg==WM_SYSKEYDOWN) return true;        // (suppress default windows behaviour for these system keys)
                    break;

                case WM_CHAR:
                    if (inputTrans) { inputTrans->OnChar((wchar_t)wparam); }
                    break;

                case WM_SIZE:
                    for (auto i=pimpl->_windowHandlers.begin(); i!=pimpl->_windowHandlers.end(); ++i) {
                        signed x = ((int)(short)LOWORD(lparam)), y = ((int)(short)HIWORD(lparam));
                        (*i)->OnResize(x, y);
                    }
                    return msg == WM_SIZING;
                }
            }
            break;

		case WM_TIMER:
			{
				auto pimpl = (OverlappedWindow::Pimpl*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
				pimpl->_runLoop->OnOSTrigger(wparam);
			}
			break;
        }

        return DefWindowProc(hwnd, msg, wparam, lparam);
    }

    void OverlappedWindow::AddWindowHandler(std::shared_ptr<IWindowHandler> handler)
    {
        _pimpl->_windowHandlers.push_back(handler);
    }

    OverlappedWindow::OverlappedWindow() 
    {
        _pimpl = std::make_unique<Pimpl>();

            //
            //      ---<>--- Register window class ---<>---
            //

        OSServices::Windows::WNDCLASSEX wc;
        XlZeroMemory(wc);
        XlSetMemory(&wc, 0, sizeof(wc));

        auto windowClassName = Conversion::Convert<std::string>(CurrentModule::GetInstance().HashId());

        wc.cbSize           = sizeof(wc);
        wc.style            = CS_OWNDC | CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc      = (::WNDPROC)&WndProc;
        wc.cbClsExtra       = 0;
        wc.cbWndExtra       = 0;
        wc.hInstance        = CurrentModule::GetInstance().Handle();
        wc.hIcon            = ::LoadIcon(nullptr, IDI_INFORMATION);
        wc.hCursor          = ::LoadCursor(nullptr, IDC_ARROW); 
        wc.hbrBackground    = (HBRUSH)nullptr;
        wc.lpszMenuName     = 0;
        wc.lpszClassName    = windowClassName.c_str();
        wc.hIconSm          = NULL;

            //       (Ignore class registration failure errors)
        (*OSServices::Windows::Fn_RegisterClassEx)(&wc);

            //
            //      ---<>--- Create the window itself ---<>---
            //
        DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
        DWORD windowStyleEx = 0;
        _pimpl->_hwnd = (*OSServices::Windows::Fn_CreateWindowEx)(
            windowStyleEx, windowClassName.c_str(), 
            NULL, windowStyle, 
            32, 32, 1280, 720, 
            NULL, NULL, CurrentModule::GetInstance().HInstance(), NULL);

        if (!_pimpl->_hwnd || _pimpl->_hwnd == INVALID_HANDLE_VALUE) {
            Throw(::Exceptions::BasicLabel( "Failure during windows construction" ));        // (note that a window class can be leaked by this.. But, who cares?)
        }

        SetWindowLongPtr(_pimpl->_hwnd, GWLP_USERDATA, (LONG_PTR)_pimpl.get());
        ShowWindow(_pimpl->_hwnd, SW_SHOWNORMAL);

            //  Create input translator -- used to translate between windows messages
            //  and the cross platform input-handling interface
        _pimpl->_inputTranslator = std::make_shared<InputTranslator>(_pimpl->_hwnd);

		_pimpl->_runLoop = std::make_shared<OSRunLoop_BasicTimer>(_pimpl->_hwnd);
		SetOSRunLoop(_pimpl->_runLoop);
    }

    OverlappedWindow::~OverlappedWindow()
    {
		SetOSRunLoop(nullptr);
		_pimpl->_inputTranslator.reset();
        ::DestroyWindow(_pimpl->_hwnd);
        auto windowClassName = Conversion::Convert<std::string>(CurrentModule::GetInstance().HashId());
        (*OSServices::Windows::Fn_UnregisterClass)(windowClassName.c_str(), CurrentModule::GetInstance().Handle());
    }

    const void* OverlappedWindow::GetUnderlyingHandle() const
    {
        return _pimpl->_hwnd;
    }

    std::pair<Int2, Int2> OverlappedWindow::GetRect() const
    {
        RECT clientRect;
        GetClientRect(_pimpl->_hwnd, &clientRect);
        return std::make_pair(Int2(clientRect.left, clientRect.top), Int2(clientRect.right, clientRect.bottom));
    }

    void OverlappedWindow::Resize(unsigned width, unsigned height)
    {
        RECT adjusted { 0, 0, (LONG)width, (LONG)height };
        AdjustWindowRectEx(&adjusted, GetWindowStyle(_pimpl->_hwnd), FALSE, GetWindowExStyle(_pimpl->_hwnd));
        SetWindowPos(
            _pimpl->_hwnd, (HWND)INVALID_HANDLE_VALUE,
            0, 0, adjusted.right - adjusted.left, adjusted.bottom - adjusted.top,
            SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
    }

    void OverlappedWindow::SetTitle(const char titleText[])
    {
        SetWindowTextA(_pimpl->_hwnd, titleText);
    }
    
    void OverlappedWindow::AddListener(std::weak_ptr<IInputListener> listener)
    {
        _pimpl->_inputTranslator->AddListener(listener);
    }

    auto OverlappedWindow::DoMsgPump() -> PumpResult
    {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return PumpResult::Terminate;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        DWORD foreWindowProcess;
        GetWindowThreadProcessId(GetForegroundWindow(), &foreWindowProcess);
        
        return (GetCurrentProcessId() != foreWindowProcess)
            ? PumpResult::Background
            : PumpResult::Continue;
    }

}
