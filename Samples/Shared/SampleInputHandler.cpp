// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SampleInputHandler.h"
#include "Character.h"
#include "../../SceneEngine/IntersectionTest.h"
#include "../../Tools/ToolsRig/IManipulator.h"
#include "../../Math/Transformations.h"
#include "../../Utility/PtrUtils.h"

namespace Sample
{
    bool SampleInputHandler::OnInputEvent(
		const RenderOverlays::DebuggingDisplay::InputContext& context,
        const OSServices::InputSnapshot& evnt)
    {
        using namespace RenderOverlays::DebuggingDisplay;
            //  on left button release; if we ctrl-clicked the terrain
            //  then let's moved the player character to that location
        static KeyId ctrl = KeyId_Make("control");
        // const KeyId shift = KeyId_Make("shift");

        if (evnt.IsRelease_LButton() && evnt.IsHeld(ctrl)) {
            SceneEngine::IntersectionTestScene intersectionTestScene(_terrain);
            auto intersection = intersectionTestScene.UnderCursor(
                *_intersectionTestContext.get(), evnt._mousePosition);
            if (intersection._type == SceneEngine::IntersectionTestResult::Type::Terrain) {
                _playerCharacter->SetLocalToWorld(AsFloat4x4(intersection._worldSpaceCollision));
                return true;
            }
        }

        // if (evnt.IsHeld(shift) && evnt.IsHeld_MButton()) {
        //     auto deltaMouse = evnt._mouseDelta;
        //     SceneEngine::SunDirectionAngle = Clamp(
        //         SceneEngine::SunDirectionAngle + deltaMouse[0] * 1.0f * gPI / 180.f, 
        //         -.5f * gPI, .5f * gPI);
        // }

        return false;
    }

    SampleInputHandler::SampleInputHandler(
        std::shared_ptr<PlatformRig::Camera::ICameraAttach> playerCharacter,
        std::shared_ptr<SceneEngine::TerrainManager> terrain,
        std::shared_ptr<SceneEngine::IntersectionTestContext> intersectionTestContext)
    {
        _playerCharacter = std::move(playerCharacter);
        _terrain = std::move(terrain);
        _intersectionTestContext = std::move(intersectionTestContext);
    }

    SampleInputHandler::~SampleInputHandler() {}
}

