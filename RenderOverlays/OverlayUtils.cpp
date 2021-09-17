// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OverlayUtils.h"
#include "Font.h"
#include "../ConsoleRig/ResourceBox.h"

namespace RenderOverlays { namespace DebuggingDisplay
{
    ButtonStyle s_buttonNormal      { ColorB( 51,  51,  51), ColorB(191, 123, 0) };
    ButtonStyle s_buttonMouseOver   { ColorB(120, 120, 120), ColorB(255, 255, 255) };
    ButtonStyle s_buttonPressed     { ColorB(120, 120, 120), ColorB(196, 196, 196), true };

    class UtilFontBox
    {
    public:
        std::shared_ptr<RenderOverlays::Font> _buttonFont;
        UtilFontBox() : _buttonFont(RenderOverlays::GetX2Font("DosisExtraBold", 20)) {}
    };

    void DrawButtonBasic(
        IOverlayContext& context, const Rect& rect, 
        const char label[], const ButtonStyle& formatting)
    {
        if (formatting._depressed)
            FillDepressedRoundedRectangle(context, rect, formatting._background);
        else
            FillRaisedRoundedRectangle(context, rect, formatting._background);
        context.DrawText(
            std::make_tuple(AsPixelCoords(rect._topLeft), AsPixelCoords(rect._bottomRight)),
			ConsoleRig::FindCachedBox<UtilFontBox>()._buttonFont, TextStyle{}, formatting._foreground, TextAlignment::Center, label);
    }

}}

