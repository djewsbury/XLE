// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "InputListener.h"
#include <vector>
#include <memory>

namespace RenderOverlays { namespace DebuggingDisplay { class DebugScreensSystem; }}

namespace PlatformRig
{
    class MainInputHandler : public IInputListener
    {
    public:
        bool    OnInputEvent(const InputContext& context, const InputSnapshot& evnt);
        void    AddListener(std::shared_ptr<IInputListener> listener);
        void    RemoveListened(IInputListener&);

        MainInputHandler();
        ~MainInputHandler();
    private:
        std::vector<std::shared_ptr<IInputListener>> _listeners;
    };

    class DebugScreensInputHandler : public IInputListener
    {
    public:
        bool    OnInputEvent(const InputContext& context, const InputSnapshot& evnt);

        DebugScreensInputHandler(std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem> debugScreens);
        ~DebugScreensInputHandler();
    private:
        std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem>       _debugScreens;
    };

}

