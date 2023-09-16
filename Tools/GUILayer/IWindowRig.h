// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace PlatformRig { class FrameRig; }
namespace RenderCore { class IPresentationChain; }

namespace GUILayer
{
    class IWindowRig
    {
    public:
        virtual PlatformRig::FrameRig& GetFrameRig() = 0;
        virtual std::shared_ptr<RenderCore::IPresentationChain>& GetPresentationChain() = 0;
        virtual ~IWindowRig();
    };
}

