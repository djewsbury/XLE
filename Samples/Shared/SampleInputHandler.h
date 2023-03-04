// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include <memory>

namespace SceneEngine { class IntersectionTestContext; class TerrainManager; }
namespace PlatformRig { namespace Camera { class ICameraAttach; }}

namespace Sample
{
    class Character;
    class SampleInputHandler : public PlatformRig::IInputListener
    {
    public:
        bool OnInputEvent(
			const PlatformRig::InputContext& context,
			const OSServices::InputSnapshot& evnt);

        SampleInputHandler(
            std::shared_ptr<PlatformRig::Camera::ICameraAttach> playerCharacter, 
            std::shared_ptr<SceneEngine::TerrainManager> terrain,
            std::shared_ptr<SceneEngine::IntersectionTestContext> intersectionTestContext);
        ~SampleInputHandler();
    protected:
        std::shared_ptr<PlatformRig::Camera::ICameraAttach> _playerCharacter;
        std::shared_ptr<SceneEngine::TerrainManager> _terrain;
        std::shared_ptr<SceneEngine::IntersectionTestContext> _intersectionTestContext;
    };
}



