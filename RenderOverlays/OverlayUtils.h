// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once
#include "DebuggingDisplay.h"

namespace RenderOverlays { namespace DebuggingDisplay
{
    class ButtonStyle
    {
    public:
        ColorB  _background;
        ColorB  _foreground;

        ButtonStyle(ColorB background, ColorB foreground) : _background(background), _foreground(foreground) {}
    };

    extern ButtonStyle s_buttonNormal;
    extern ButtonStyle s_buttonMouseOver;
    extern ButtonStyle s_buttonPressed;

    template<typename T> inline const T& FormatButton(
        InterfaceState& interfaceState, InteractableId id, 
        const T& normalState, const T& mouseOverState, const T& pressedState)
    {
        if (interfaceState.HasMouseOver(id))
            return interfaceState.IsMouseButtonHeld(0)?pressedState:mouseOverState;
        return normalState;
    }

    inline const ButtonStyle& FormatButton(
        InterfaceState& interfaceState, InteractableId id) 
    {
        return FormatButton(interfaceState, id, s_buttonNormal, s_buttonMouseOver, s_buttonPressed);
    }

    void DrawButtonBasic(IOverlayContext& context, const Rect& rect, const char label[], const ButtonStyle& formatting);

}}

