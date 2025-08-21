// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "OSServicesPrimitives.h"
#include "../Utility/StringUtils.h"
#include "../Utility/MemoryUtils.h"
#include <assert.h>
#include <cstdint>
#include <vector>

namespace OSServices
{
	using KeyId = uint32_t;

	///////////////////////////////////////////////////////////////////////////////////
    class InputSnapshot
    {
    public:
        unsigned        _mouseButtonsTransition;
        unsigned        _mouseButtonsDown;
        unsigned        _mouseButtonsDblClk;
        Coord2          _mousePosition;
        Coord2          _mouseDelta;
        signed          _wheelDelta;
        wchar_t         _pressedChar;

        struct ActiveButton
        {
            KeyId       _name;
            bool        _transition;
            bool        _state;

            ActiveButton(KeyId name, bool transition, bool state) 
                : _name(name), _transition(transition), _state(state) {}
        };
        std::vector<ActiveButton>   _activeButtons;

        InputSnapshot() : _mouseButtonsTransition(0), _mouseButtonsDblClk(0), _mouseButtonsDown(0), _wheelDelta(0), _mousePosition(0,0), _mouseDelta(0,0), _pressedChar(0) {}
        InputSnapshot(  unsigned buttonsDown, unsigned buttonsTransition, signed wheelDelta, 
                        Coord2 mousePosition, Coord2 mouseDelta, wchar_t pressedChar=0) 
            : _mouseButtonsTransition(buttonsTransition), _mouseButtonsDown(buttonsDown)
            , _wheelDelta(wheelDelta), _mousePosition(mousePosition), _mouseDelta(mouseDelta)
            , _pressedChar(pressedChar), _mouseButtonsDblClk(0) {}

            //
            //      Each mouse button gets 2 bits, meaning 4 possible states:
            //          Press, Release, Held, Up
            //  
        bool    IsHeld_LButton() const       { return ((_mouseButtonsDown&1)==1); }
        bool    IsPress_LButton() const      { return ((_mouseButtonsDown&1)==1) && ((_mouseButtonsTransition&1)==1); }
        bool    IsRelease_LButton() const    { return ((_mouseButtonsDown&1)==0) && ((_mouseButtonsTransition&1)==1); }
        bool    IsUp_LButton() const         { return ((_mouseButtonsDown&1)==0) && ((_mouseButtonsTransition&1)==0); }
        bool    IsDblClk_LButton() const     { return !!(_mouseButtonsDblClk & 1); }

        bool    IsHeld_RButton() const       { return ((_mouseButtonsDown&(1<<1))==(1<<1)); }
        bool    IsPress_RButton() const      { return ((_mouseButtonsDown&(1<<1))==(1<<1))  && ((_mouseButtonsTransition&(1<<1))==(1<<1)); }
        bool    IsRelease_RButton() const    { return ((_mouseButtonsDown&(1<<1))==0)       && ((_mouseButtonsTransition&(1<<1))==(1<<1)); }
        bool    IsUp_RButton() const         { return ((_mouseButtonsDown&(1<<1))==0)       && ((_mouseButtonsTransition&(1<<1))==0); }
        bool    IsDblClk_RButton() const     { return !!(_mouseButtonsDblClk & (1<<1)); }

        bool    IsHeld_MButton() const       { return ((_mouseButtonsDown&(1<<2))==(1<<2)); }
        bool    IsPress_MButton() const      { return ((_mouseButtonsDown&(1<<2))==(1<<2))  && ((_mouseButtonsTransition&(1<<2))==(1<<2)); }
        bool    IsRelease_MButton() const    { return ((_mouseButtonsDown&(1<<2))==0)       && ((_mouseButtonsTransition&(1<<2))==(1<<2)); }
        bool    IsUp_MButton() const         { return ((_mouseButtonsDown&(1<<2))==0)       && ((_mouseButtonsTransition&(1<<2))==0); }
        bool    IsDblClk_MButton() const     { return !!(_mouseButtonsDblClk & (1<<2)); }

		bool    IsHeld_MouseButton(unsigned btnIdx) const       { return ((_mouseButtonsDown&(1u<<btnIdx))==(1u<<btnIdx)); }
        bool    IsPress_MouseButton(unsigned btnIdx) const      { return ((_mouseButtonsDown&(1u<<btnIdx))==(1u<<btnIdx))	&& ((_mouseButtonsTransition&(1u<<btnIdx))==(1u<<btnIdx)); }
        bool    IsRelease_MouseButton(unsigned btnIdx) const    { return ((_mouseButtonsDown&(1u<<btnIdx))==0u)				&& ((_mouseButtonsTransition&(1u<<btnIdx))==(1u<<btnIdx)); }
        bool    IsUp_MouseButton(unsigned btnIdx) const         { return ((_mouseButtonsDown&(1u<<btnIdx))==0u)				&& ((_mouseButtonsTransition&(1u<<btnIdx))==0u); }
        bool    IsDblClk_MouseButton(unsigned btnIdx) const     { return !!(_mouseButtonsDblClk & (1u<<btnIdx)); }

        template<typename Iterator> static bool IsHeld      (KeyId key, Iterator begin, Iterator end);
        template<typename Iterator> static bool IsPress     (KeyId key, Iterator begin, Iterator end);
        template<typename Iterator> static bool IsRelease   (KeyId key, Iterator begin, Iterator end);
        template<typename Iterator> static bool IsUp        (KeyId key, Iterator begin, Iterator end);

        bool    IsHeld(KeyId key) const       { return IsHeld(key, _activeButtons.begin(), _activeButtons.end()); }
        bool    IsPress(KeyId key) const      { return IsPress(key, _activeButtons.begin(), _activeButtons.end()); }
        bool    IsRelease(KeyId key) const    { return IsRelease(key, _activeButtons.begin(), _activeButtons.end()); }
        bool    IsUp(KeyId key) const         { return IsUp(key, _activeButtons.begin(), _activeButtons.end()); }

        void    Accumulate(const InputSnapshot& newEvnts, const InputSnapshot& lastFrameState);
        void    Reset();
    };

	KeyId KeyId_Make(StringSection<char>);

    ///////////////////////////////////////////////////////////////////////////////////
   
    namespace Literals
    {
        #if (COMPILER_ACTIVE == COMPILER_TYPE_GCC || COMPILER_ACTIVE == COMPILER_TYPE_CLANG) && (__cplusplus < 202002L)

            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wgnu-string-literal-operator-template"
                template <typename T, T... chars>
                    XLE_CONSTEVAL_OR_CONSTEXPR uint32_t operator""_key() never_throws { return ::Utility::Internal::ConstHash32_2<DefaultSeed32, chars...>(); }
            #pragma GCC diagnostic pop

        #else

            XLE_CONSTEVAL_OR_CONSTEXPR uint32_t operator""_key(const char* str, const size_t len) never_throws { return ::Utility::Internal::ConstHash32_1(str, len); }

        #endif
    }

	///////////////////////////////////////////////////////////////////////////////////
	template<typename Iterator> bool InputSnapshot::IsHeld(KeyId key, Iterator begin, Iterator end)
    {
        for (auto i=begin; i!=end; ++i)
            if (i->_name==key) return i->_state;
        return false;
    }

    template<typename Iterator> bool InputSnapshot::IsPress(KeyId key, Iterator begin, Iterator end)
    {
        for (auto i=begin; i!=end; ++i)
            if (i->_name==key) return i->_state && i->_transition;
        return false;
    }

    template<typename Iterator> bool InputSnapshot::IsRelease(KeyId key, Iterator begin, Iterator end)
    {
        for (auto i=begin; i!=end; ++i)
            if (i->_name==key) return !i->_state && i->_transition;
        return false;
    }

    template<typename Iterator> bool InputSnapshot::IsUp(KeyId key, Iterator begin, Iterator end)
    {
        for (auto i=begin; i!=end; ++i)
            if (i->_name==key) return !i->_state && !i->_transition;
        return true;
    }
}
