// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OverlayUtils.h"
#include "Font.h"

namespace RenderOverlays { namespace DebuggingDisplay
{
    ButtonStyle s_buttonNormal      ( ColorB(127, 192, 127,  64), ColorB(164, 192, 164, 255) );
    ButtonStyle s_buttonMouseOver   ( ColorB(127, 192, 127,  64), ColorB(255, 255, 255, 160) );
    ButtonStyle s_buttonPressed     ( ColorB(127, 192, 127,  64), ColorB(255, 255, 255,  96) );

    void DrawButtonBasic(
        IOverlayContext* context, const Rect& rect, 
        const char label[], const ButtonStyle& formatting)
    {
        FillRectangle(context, rect, formatting._background);
        OutlineRectangle(context, rect, formatting._foreground);
        context->DrawText(
            std::make_tuple(Float3(float(rect._topLeft[0]), float(rect._topLeft[1]), 0.f), Float3(float(rect._bottomRight[0]), float(rect._bottomRight[1]), 0.f)),
			GetDefaultFont(), TextStyle{}, formatting._foreground, TextAlignment::Center, label);
    }

}}

