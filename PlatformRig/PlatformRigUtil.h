// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Math/Vector.h"
#include "../Utility/StringUtils.h"

namespace RenderOverlays { namespace DebuggingDisplay { class DebugScreensSystem; }}
namespace RenderOverlays { class IOverlayContext; struct ImmediateLayout; }
namespace RenderCore { namespace Techniques { class TechniqueContext; }}

namespace PlatformRig
{
///////////////////////////////////////////////////////////////////////////////////////////////////

    class FrameRig;

    class ScriptInterface
    {
    public:
        void BindTechniqueContext(const std::string& name, std::shared_ptr<RenderCore::Techniques::TechniqueContext>);
        void BindFrameRig(const std::string& name, std::shared_ptr<FrameRig>);

        ScriptInterface();
        ~ScriptInterface();
    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    void TopBarHeading(RenderOverlays::IOverlayContext&, RenderOverlays::ImmediateLayout&, StringSection<>);

///////////////////////////////////////////////////////////////////////////////////////////////////

    void InitDebugDisplays(RenderOverlays::DebuggingDisplay::DebugScreensSystem& system);
    void ShowDebugScreen(StringSection<>);

    class InputContext;
    using Coord2 = Int2;
    InputContext InputContextForSubView(
        const InputContext& superViewContext,
        Coord2 subViewMins, Coord2 subViewMaxs);

}

