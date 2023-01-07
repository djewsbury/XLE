// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderCore/IDevice_Forward.h"
#include <memory>
#include <functional>

namespace RenderCore { class IThreadContext; }
namespace PlatformRig { class FrameRig; class IWindowHandler; class OverlaySystemSet; }

namespace GUILayer
{
    class IWindowRig
    {
    public:
        virtual PlatformRig::FrameRig& GetFrameRig() = 0;
        virtual PlatformRig::OverlaySystemSet& GetMainOverlaySystemSet() = 0;
        virtual std::shared_ptr<RenderCore::IPresentationChain>& GetPresentationChain() = 0;
        virtual ~IWindowRig();
    };
}

