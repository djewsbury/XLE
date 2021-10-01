// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OverlayUtils.h"
#include "Font.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Assets/AssetFutureContinuation.h"

namespace RenderOverlays { namespace DebuggingDisplay
{
    ButtonStyle s_buttonNormal      { ColorB( 51,  51,  51), ColorB(191, 123, 0) };
    ButtonStyle s_buttonMouseOver   { ColorB(120, 120, 120), ColorB(255, 255, 255) };
    ButtonStyle s_buttonPressed     { ColorB(120, 120, 120), ColorB(196, 196, 196), true };

    class UtilFontBox
    {
    public:
        std::shared_ptr<RenderOverlays::Font> _buttonFont;

        UtilFontBox(
            std::shared_ptr<RenderOverlays::Font> buttonFont)
        : _buttonFont(std::move(buttonFont))
        {}

        static void ConstructToFuture(::Assets::FuturePtr<UtilFontBox>& future)
        {
            ::Assets::WhenAll(
                RenderOverlays::MakeFont("DosisExtraBold", 20)).ThenConstructToFuture(future);
        }
    };

    void DrawButtonBasic(
        IOverlayContext& context, const Rect& rect, 
        const char label[], const ButtonStyle& formatting)
    {
        if (formatting._depressed)
            FillDepressedRoundedRectangle(context, rect, formatting._background);
        else
            FillRaisedRoundedRectangle(context, rect, formatting._background);
        auto* fonts = ConsoleRig::TryActualizeCachedBox<UtilFontBox>();
        if (!fonts) return;
        DrawText()
            .Alignment(TextAlignment::Center)
            .Color(formatting._foreground)
            .Font(*fonts->_buttonFont)
            .Draw(context, rect, label);
    }

}}

