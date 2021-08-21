// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IWindowRig.h"
#include "EngineForward.h"
#include <memory>
#include <vector>

namespace RenderCore { namespace Techniques { class SubFrameEvents; }}

namespace GUILayer
{
    class WindowRig : public IWindowRig
    {
    public:
        PlatformRig::FrameRig& GetFrameRig() { return *_frameRig; }
        PlatformRig::OverlaySystemSet& GetMainOverlaySystemSet();
        std::shared_ptr<RenderCore::IPresentationChain>& GetPresentationChain() { return _presentationChain; }

        void AddWindowHandler(std::shared_ptr<PlatformRig::IWindowHandler> windowHandler);
        void OnResize(unsigned newWidth, unsigned newHeight);

        WindowRig(RenderCore::IDevice& device, const void* platformWindowHandle);
        ~WindowRig();
    protected:
        std::shared_ptr<PlatformRig::FrameRig> _frameRig;
        std::shared_ptr<RenderCore::IPresentationChain> _presentationChain;
        std::vector<std::shared_ptr<PlatformRig::IWindowHandler>> _windowHandlers;
        std::shared_ptr<PlatformRig::OverlaySystemSet> _mainOverlaySystemSet;
    };
}

