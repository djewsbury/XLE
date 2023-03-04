// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InputSnapshot.h"
#include "../Utility/MemoryUtils.h"

namespace OSServices
{

    static void AddOrChangeButton(std::vector<InputSnapshot::ActiveButton>& container, const InputSnapshot::ActiveButton& newButton)
    {
        for (auto i=container.begin(); i!=container.end(); ++i) {
            if (i->_name == newButton._name) {
                *i = newButton;
                return;
            }
        }
        container.push_back(newButton);
    }

    void    InputSnapshot::Accumulate(const InputSnapshot& newEvnts, const InputSnapshot& lastFrameState)
    {
        for (unsigned c=0; c<32; ++c) {
            if (newEvnts._mouseButtonsTransition & (1<<c)) {
                _mouseButtonsDown = (_mouseButtonsDown & ~(1<<c)) | (newEvnts._mouseButtonsDown & (1<<c));
                if ((newEvnts._mouseButtonsDown & (1<<c)) != (lastFrameState._mouseButtonsDown & (1<<c))) {
                    _mouseButtonsTransition |= (1<<c);
                } else {
                    _mouseButtonsTransition &= ~(1<<c);
                }
            }
        }

        _mouseButtonsDblClk |= newEvnts._mouseButtonsDblClk;

        for (auto a=newEvnts._activeButtons.begin(); a!=newEvnts._activeButtons.end(); ++a) {
            if (a->_transition) {
                bool lastFrameKeyState = false;
                for (auto i=lastFrameState._activeButtons.begin(); i!=lastFrameState._activeButtons.end(); ++i) {
                    if (i->_name == a->_name) {
                        lastFrameKeyState = i->_state;
                        break;
                    }
                }

                AddOrChangeButton(
                    _activeButtons,
                    ActiveButton(a->_name, a->_state != lastFrameKeyState, a->_state));
            }
        }

        _mousePosition = newEvnts._mousePosition;
        _mouseDelta += newEvnts._mouseDelta;
        _wheelDelta += newEvnts._wheelDelta;
        _pressedChar = newEvnts._pressedChar;
    }

    void    InputSnapshot::Reset()
    {
            // clear "transition" flags (and remove any buttons that transitioned up)
        _mouseButtonsTransition = 0;
        _mouseButtonsDblClk = 0;
        _pressedChar = 0;
        _wheelDelta = 0;
        _mouseDelta = Coord2(0,0);
        for (auto a = _activeButtons.begin(); a!=_activeButtons.end();) {
            if (a->_state) {
                a->_transition = false;
                ++a;
            } else {
                a = _activeButtons.erase(a);
            }
        }
    }

    OSServices::KeyId           KeyId_Make(StringSection<char> name)            { return Hash32(name.begin(), name.end()); }

}
