// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../OSServices/OverlappedWindow.h"
#include "../RenderCore/IDevice_Forward.h"
#include "../Utility/StringUtils.h"
#include "../Utility/FunctionUtils.h"

namespace RenderOverlays { namespace DebuggingDisplay { class DebugScreensSystem; }}
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

    void InitDebugDisplays(RenderOverlays::DebuggingDisplay::DebugScreensSystem& system);
    void ShowDebugScreen(StringSection<>);

}

