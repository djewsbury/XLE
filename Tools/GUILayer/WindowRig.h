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

namespace RenderCore { namespace Techniques { class DrawingApparatus; class FrameRenderingApparatus; } }
namespace PlatformRig { class InputSnapshot; class InputContext; class MainInputHandler; }

namespace GUILayer
{
    class WindowRig : public IWindowRig
    {
    public:
        PlatformRig::FrameRig& GetFrameRig() { return *_frameRig; }
        PlatformRig::OverlaySystemSet& GetMainOverlaySystemSet();
        std::shared_ptr<RenderCore::IPresentationChain>& GetPresentationChain() { return _presentationChain; }

        void OnResize(unsigned newWidth, unsigned newHeight);
        void OnInputEvent(const PlatformRig::InputSnapshot&);

        WindowRig(
            std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
            std::shared_ptr<RenderCore::Techniques::FrameRenderingApparatus> frameRenderingApparatus,
            const void* platformWindowHandle);
        ~WindowRig();
    protected:
        std::shared_ptr<PlatformRig::FrameRig> _frameRig;
        std::shared_ptr<RenderCore::IPresentationChain> _presentationChain;
        std::shared_ptr<PlatformRig::OverlaySystemSet> _mainOverlaySystemSet;
        std::shared_ptr<RenderCore::IDevice> _device;
        std::unique_ptr<PlatformRig::MainInputHandler> _mainInputHandler;

        PlatformRig::InputContext MakeInputContext();
        const void* _platformWindowHandle;
    };
}

