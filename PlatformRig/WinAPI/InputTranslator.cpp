// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InputTranslator.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../Utility/PtrUtils.h"

#include "../../OSServices/WinAPI/IncludeWindows.h"

namespace PlatformRig
{
    class InputTranslator::Pimpl
    {
    public:
        typedef InputSnapshot::ActiveButton ActiveButton;
        
        Int2 _currentMousePosition;
        std::vector<ActiveButton> _passiveButtonState;
        std::vector<std::weak_ptr<IInputListener>> _listeners;
        HWND _hwnd;
    };

    InputTranslator::InputTranslator(const void* hwnd)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_currentMousePosition = Int2(0,0);
        _pimpl->_hwnd = (HWND)hwnd;
    }

    InputTranslator::~InputTranslator()
    {
    }

    static InputContext MakeInputContext(HWND hwnd)
	{
		RECT clientRect;
		GetClientRect(hwnd, &clientRect);
		return { Coord2{clientRect.left, clientRect.top}, Coord2{clientRect.right, clientRect.bottom} };
	}

    void    InputTranslator::OnMouseMove(signed newX, signed newY)
    {
        using namespace RenderOverlays::DebuggingDisplay;
        auto oldPos = _pimpl->_currentMousePosition;
        _pimpl->_currentMousePosition = Int2(newX, newY);

        InputSnapshot snapShot(
            GetMouseButtonState(), 0, 0, 
            _pimpl->_currentMousePosition, _pimpl->_currentMousePosition - oldPos);
        snapShot._activeButtons = _pimpl->_passiveButtonState;
        Publish(snapShot);
    }

    void    InputTranslator::OnMouseButtonChange (signed newX, signed newY, unsigned index,    bool newState)
    {
        assert(index < 32);
        using namespace RenderOverlays::DebuggingDisplay;
		auto oldPos = _pimpl->_currentMousePosition;
        _pimpl->_currentMousePosition = Int2(newX, newY);

        InputSnapshot snapShot(GetMouseButtonState(), 1<<index, 0, _pimpl->_currentMousePosition, _pimpl->_currentMousePosition - oldPos);
        snapShot._activeButtons = _pimpl->_passiveButtonState;
        Publish(snapShot);
    }

    void    InputTranslator::OnMouseButtonDblClk (signed newX, signed newY, unsigned index)
    {
        assert(index < 32);
        using namespace RenderOverlays::DebuggingDisplay;
		auto oldPos = _pimpl->_currentMousePosition;
        _pimpl->_currentMousePosition = Int2(newX, newY);

        InputSnapshot snapShot(GetMouseButtonState(), 0, 0, _pimpl->_currentMousePosition, _pimpl->_currentMousePosition - oldPos);
        snapShot._mouseButtonsDblClk |= (1<<index);
        Publish(snapShot);
    }

    void    InputTranslator::OnKeyChange         (unsigned keyCode,  bool newState)
    {
        using namespace RenderOverlays::DebuggingDisplay;
        InputSnapshot snapShot(GetMouseButtonState(), 0, 0, _pimpl->_currentMousePosition, Int2(0,0));
        snapShot._activeButtons.reserve(_pimpl->_passiveButtonState.size() + 1);

        KeyId keyCodeToName = KeyId_Make(AsKeyName(keyCode));
        bool found = false;
        for (auto i = _pimpl->_passiveButtonState.begin(); i!=_pimpl->_passiveButtonState.end();) {
            if (i->_name == keyCodeToName) {

                assert(!found);
                i->_state = newState;

                InputSnapshot::ActiveButton newButton = *i;
                newButton._transition = true;
                snapShot._activeButtons.push_back(newButton);

                if (newState) {
                    ++i;
                } else {
                    i = _pimpl->_passiveButtonState.erase(i);
                }

            } else {
                snapShot._activeButtons.push_back(*i);
                ++i;
            }
        }

        if (!found) {
            snapShot._activeButtons.push_back(
                InputSnapshot::ActiveButton(keyCodeToName, true, newState));
            if (newState) {
                _pimpl->_passiveButtonState.push_back(
                    InputSnapshot::ActiveButton(keyCodeToName, false, newState));
            }
        }

        Publish(snapShot);
    }

    void            InputTranslator::OnChar(ucs2 chr)
    {
        using namespace RenderOverlays::DebuggingDisplay;
        InputSnapshot snapShot(GetMouseButtonState(), 0, 0, _pimpl->_currentMousePosition, Int2(0,0), chr);
        snapShot._activeButtons = _pimpl->_passiveButtonState;
        Publish(snapShot);
    }

    void            InputTranslator::OnMouseWheel(signed wheelDelta)
    {
        using namespace RenderOverlays::DebuggingDisplay;
        InputSnapshot snapShot(GetMouseButtonState(), 0, wheelDelta, _pimpl->_currentMousePosition, Int2(0,0));
        snapShot._activeButtons = _pimpl->_passiveButtonState;
        Publish(snapShot);
    }

    void    InputTranslator::OnFocusChange()
    {
            // we have to reset the "_passiveButtonState" set when we gain or loose focus.
            // this is because we will miss key up messages when not focussed...
            // We could also generate input messages focus releasing those buttons
            // (otherwise clients will get key down, but not key up)
        using namespace RenderOverlays::DebuggingDisplay;
        _pimpl->_passiveButtonState.clear();

		// send a snapshot that emulates releasing all held mouse buttons
		auto emulateClickUp = GetMouseButtonState();
		InputSnapshot snapShot(0, emulateClickUp, 0, _pimpl->_currentMousePosition, Int2(0,0));
		Publish(snapShot);
    }

    const char*     InputTranslator::AsKeyName(unsigned keyCode)
    {
        switch (keyCode) {
        case VK_RETURN:     return "enter";
        case VK_SHIFT:      return "shift";
        case VK_CONTROL:    return "control";
        case VK_MENU:       return "alt";
        case VK_PAUSE:      return "pause";
        case VK_CAPITAL:    return "capslock";
        case VK_ESCAPE:     return "escape";
        case VK_SPACE:      return "space";
        case VK_PRIOR:      return "page up";
        case VK_NEXT:       return "page down";
        case VK_END:        return "end";
        case VK_HOME:       return "home";
        case VK_LEFT:       return "left";
        case VK_UP:         return "up";
        case VK_RIGHT:      return "right";
        case VK_DOWN:       return "down";
        case VK_SNAPSHOT:   return "prtsc";
        case VK_INSERT:     return "insert";
        case VK_DELETE:     return "delete";
        case VK_BACK:       return "backspace";
        case VK_TAB:        return "tab";
        case 0x30:          return "0";
        case 0x31:          return "1";
        case 0x32:          return "2";
        case 0x33:          return "3";
        case 0x34:          return "4";
        case 0x35:          return "5";
        case 0x36:          return "6";
        case 0x37:          return "7";
        case 0x38:          return "8";
        case 0x39:          return "9";
        case 0x41:          return "a";
        case 0x42:          return "b";
        case 0x43:          return "c";
        case 0x44:          return "d";
        case 0x45:          return "e";
        case 0x46:          return "f";
        case 0x47:          return "g";
        case 0x48:          return "h";
        case 0x49:          return "i";
        case 0x4A:          return "j";
        case 0x4B:          return "k";
        case 0x4C:          return "l";
        case 0x4D:          return "m";
        case 0x4E:          return "n";
        case 0x4F:          return "o";
        case 0x50:          return "p";
        case 0x51:          return "q";
        case 0x52:          return "r";
        case 0x53:          return "s";
        case 0x54:          return "t";
        case 0x55:          return "u";
        case 0x56:          return "v";
        case 0x57:          return "w";
        case 0x58:          return "x";
        case 0x59:          return "y";
        case 0x5A:          return "z";
        case VK_NUMPAD0:    return "num0";
        case VK_NUMPAD1:    return "num1";
        case VK_NUMPAD2:    return "num2";
        case VK_NUMPAD3:    return "num3";
        case VK_NUMPAD4:    return "num4";
        case VK_NUMPAD5:    return "num5";
        case VK_NUMPAD6:    return "num6";
        case VK_NUMPAD7:    return "num7";
        case VK_NUMPAD8:    return "num8";
        case VK_NUMPAD9:    return "num9";
        case VK_MULTIPLY:   return "num*";
        case VK_ADD:        return "num+";
        case VK_SUBTRACT:   return "num-";
        case VK_DECIMAL:    return "num.";
        case VK_DIVIDE:     return "num/";
        case VK_F1:         return "f1";
        case VK_F2:         return "f2";
        case VK_F3:         return "f3";
        case VK_F4:         return "f4";
        case VK_F5:         return "f5";
        case VK_F6:         return "f6";
        case VK_F7:         return "f7";
        case VK_F8:         return "f8";
        case VK_F9:         return "f9";
        case VK_F10:        return "f10";
        case VK_F11:        return "f11";
        case VK_F12:        return "f12";
        case VK_F13:        return "f13";
        case VK_F14:        return "f14";
        case VK_F15:        return "f15";
        case VK_F16:        return "f16";
        case VK_F17:        return "f17";
        case VK_F18:        return "f18";
        case VK_F19:        return "f19";
        case VK_F20:        return "f20";
        case VK_F21:        return "f21";
        case VK_F22:        return "f22";
        case VK_F23:        return "f23";
        case VK_F24:        return "f24";
        case VK_OEM_3:      return "~";
        case VK_OEM_PLUS:   return "+";
        case VK_OEM_COMMA:  return ",";
        case VK_OEM_MINUS:  return "-";
        case VK_OEM_PERIOD: return ".";
        }
        return "<<unknown>>";
    }

    unsigned    InputTranslator::GetMouseButtonState() const
    {
        return  ((GetKeyState(VK_MBUTTON) < 0) << 2)
            |   ((GetKeyState(VK_RBUTTON) < 0) << 1)
            |   ((GetKeyState(VK_LBUTTON) < 0) << 0)
            ;
    }

    void        InputTranslator::Publish(const InputSnapshot& snapShot)
    {
        auto context = MakeInputContext(_pimpl->_hwnd);
        for (auto i=_pimpl->_listeners.begin(); i!=_pimpl->_listeners.end();) {
            auto l = i->lock();
            if (l) {
                if (l->OnInputEvent(context, snapShot)) {
                    break;
                }
                ++i;
            } else {
                i = _pimpl->_listeners.erase(i);
            }
        }
    }

    void            InputTranslator::AddListener(std::weak_ptr<IInputListener> listener)
    {
        if (std::find_if(_pimpl->_listeners.begin(), _pimpl->_listeners.end(), 
            [=](const std::weak_ptr<IInputListener>& test) { return Equivalent(listener, test); }) == _pimpl->_listeners.end()) {
            _pimpl->_listeners.push_back(listener);
        }
    }

    Int2    InputTranslator::GetMousePosition()
    {
        return _pimpl->_currentMousePosition;
    }

}

