// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "InputContext.h"
#include <vector>
#include <memory>

namespace PlatformRig
{
    class MainInputHandler : public IInputListener
    {
    public:
        ProcessInputResult    OnInputEvent(const InputContext& context, const OSServices::InputSnapshot& evnt) override;
        void    AddListener(std::shared_ptr<IInputListener> listener);
        void    RemoveListened(IInputListener&);

        MainInputHandler();
        ~MainInputHandler();
    private:
        std::vector<std::shared_ptr<IInputListener>> _listeners;
    };
}

