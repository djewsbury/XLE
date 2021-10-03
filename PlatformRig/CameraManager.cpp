// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CameraManager.h"
#include "UnitCamera.h"
#include "InputListener.h"
#include "../RenderCore/RenderUtils.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../Math/Transformations.h"

namespace PlatformRig { namespace Camera
{
    void UpdateCamera_Slew(RenderCore::Techniques::CameraDesc& camera, float dt, const InputSnapshot& input)
    {
            // (Derived from Archeage/x2standalone camera mode)
        const float cl_sensitivity = 20.f;
        const float fr_fspeed_scale = 100.f * 1.f;
        const float fr_fturn_scale = 2.f;
        const float fr_speed_scale = 100.f * 1.f/3.f;
        const float fr_turn_scale = 1.f;
        const float fr_xspeed = 40.f;
        const float fr_yspeed = 40.f;
        const float fr_zspeed = 40.f;
        const float fr_xturn = 60.f;
        const float fr_yturn = 60.f;
        

            //
            //      Our "camera" coordinate space:
            //          (different from Crytek camera space)
            //      
            //      *	Right handed
            //      *	+X to the right
            //      *	+Y up
            //      *	-Z into the screen
            //

        static const KeyId shift        = KeyId_Make("shift");
        static const KeyId ctrl         = KeyId_Make("control");
        static const KeyId forward      = KeyId_Make("w");
        static const KeyId back         = KeyId_Make("s");
        static const KeyId left         = KeyId_Make("a");
        static const KeyId right        = KeyId_Make("d");
        static const KeyId up           = KeyId_Make("home");
        static const KeyId down         = KeyId_Make("end");
        static const KeyId turnLeft     = KeyId_Make("left");
        static const KeyId turnRight    = KeyId_Make("right");
        static const KeyId turnUp       = KeyId_Make("up");
        static const KeyId turnDown     = KeyId_Make("down");

            // change move/turn speed
        bool fastMove = input.IsHeld(shift);
        bool slowMove = input.IsHeld(ctrl);
        float moveScale = fastMove ? fr_fspeed_scale : (slowMove? (fr_speed_scale/100) : fr_speed_scale);
        float turnScale = fastMove ? fr_fturn_scale : fr_turn_scale;

        float moveSpeedX = fr_xspeed * moveScale;
        float moveSpeedY = fr_yspeed * moveScale;
        float moveSpeedZ = fr_zspeed * moveScale;
        float yawSpeed   = fr_xturn  * turnScale;
        float pitchSpeed = fr_yturn  * turnScale;

            // panning & rotation
        Float3 deltaPos(0,0,0);
        float deltaCameraYaw = 0.f, deltaCameraPitch = 0.f;

            // move forward and sideways and up and down
        deltaPos[2] -= input.IsHeld(forward)    * 1.0f;
        deltaPos[2] += input.IsHeld(back)       * 1.0f;
        deltaPos[0] -= input.IsHeld(left)       * 1.0f;
        deltaPos[0] += input.IsHeld(right)      * 1.0f;
        deltaPos[1] += input.IsHeld(up)         * 1.0f;
        deltaPos[1] -= input.IsHeld(down)       * 1.0f;

        auto mouseX = input._mouseDelta[0], mouseY = input._mouseDelta[1];
        const bool rightButton      = input.IsHeld_RButton();
        if (rightButton) {
            float mouseSensitivity = -0.01f * std::max(0.01f, cl_sensitivity);
            mouseSensitivity    *= gPI / 180.0f;
            deltaCameraYaw      +=  mouseX * mouseSensitivity; 
            deltaCameraPitch    +=  mouseY * mouseSensitivity;
        } else {
            deltaCameraYaw      += input.IsHeld(left)     * yawSpeed   / 180.f;
            deltaCameraYaw      -= input.IsHeld(right)    * yawSpeed   / 180.f;
            deltaCameraPitch    += input.IsHeld(up)       * pitchSpeed / 180.f;
            deltaCameraPitch    -= input.IsHeld(down)     * pitchSpeed / 180.f;
            deltaCameraYaw      *= dt;
            deltaCameraPitch    *= dt;
        }

        deltaPos[0] *= moveSpeedX;
        deltaPos[1] *= moveSpeedY;
        deltaPos[2] *= moveSpeedZ;

            // apply rotation
        static cml::EulerOrder eulerOrder = cml::euler_order_zxz;      
        Float3 ypr = cml::matrix_to_euler<Float4x4, Float4x4::value_type>(camera._cameraToWorld, eulerOrder);
        ypr[2] += deltaCameraYaw;
        ypr[1] += deltaCameraPitch;
        ypr[1] = Clamp(ypr[1], 0.1f, 3.1f);

        Float3 camPos = Truncate(camera._cameraToWorld * Expand(Float3(dt * deltaPos), Float3::value_type(1)));
        Float3x3 rotationPart;
        cml::matrix_rotation_euler(rotationPart, ypr[0], ypr[1], ypr[2], eulerOrder);
        camera._cameraToWorld = Expand(rotationPart, camPos);
    }

    void UpdateCamera_Orbit(RenderCore::Techniques::CameraDesc& camera, float dt, Float3& focusPoint, const InputSnapshot& input)
    {
        const float cl_sensitivity   = 20.f;
        const float fr_fspeed_scale  = 1.f;
        const float fr_speed_scale   = 1.f/3.f;

        static const KeyId shift        = KeyId_Make("shift");
        static const KeyId forward      = KeyId_Make("w");
        static const KeyId back         = KeyId_Make("s");
        static const KeyId left         = KeyId_Make("a");
        static const KeyId right        = KeyId_Make("d");
        static const KeyId up           = KeyId_Make("home");
        static const KeyId down         = KeyId_Make("end");
        static const KeyId turnLeft     = KeyId_Make("left");
        static const KeyId turnRight    = KeyId_Make("right");
        static const KeyId turnUp       = KeyId_Make("up");
        static const KeyId turnDown     = KeyId_Make("down");

        bool fastMove     = input.IsHeld(shift);
        float moveScale   = fastMove ? fr_fspeed_scale : fr_speed_scale;
        moveScale        *= std::max(0.2f, Magnitude(ExtractTranslation(camera._cameraToWorld) - focusPoint));

        float deltaRotationX = 0.f, deltaRotationY = 0.f;
        Float3 deltaPos(0,0,0);

            // move forward and sideways and up and down
        deltaPos[2] += input.IsHeld(forward)    * 1.0f;
        deltaPos[2] -= input.IsHeld(back)       * 1.0f;
        deltaPos[0] -= input.IsHeld(left)       * 1.0f;
        deltaPos[0] += input.IsHeld(right)      * 1.0f;
        deltaPos[1] += input.IsHeld(up)         * 1.0f;
        deltaPos[1] -= input.IsHeld(down)       * 1.0f;

        auto mouseX = input._mouseDelta[0], mouseY = input._mouseDelta[1];
        const bool rightButton      = input.IsHeld_RButton();
        if (rightButton) {
            float mouseSensitivity = -0.01f * std::max(0.01f, cl_sensitivity);
            mouseSensitivity    *= gPI / 180.0f;
            deltaRotationX      +=  mouseX * mouseSensitivity; 
            deltaRotationY      +=  mouseY * mouseSensitivity;
        }

        deltaPos[0] *= moveScale;
        deltaPos[1] *= moveScale;
        deltaPos[2] *= moveScale;

        Float3 rotYAxis = Truncate(camera._cameraToWorld * Float4(1.0f, 0.f, 0.f, 0.f));

        Float4x4 cameraToWorld = camera._cameraToWorld;
        Combine_IntoLHS(cameraToWorld, Float3(-focusPoint));
        cameraToWorld = Combine(cameraToWorld, MakeRotationMatrix(rotYAxis, deltaRotationY));
        Combine_IntoLHS(cameraToWorld, RotationZ(deltaRotationX));
        Combine_IntoLHS(cameraToWorld, focusPoint);
        Combine_IntoLHS(cameraToWorld, Float3(deltaPos[2] * Normalize(focusPoint - ExtractTranslation(camera._cameraToWorld))));

        Float3 cameraFocusDrift = 
            Float3(0.f, 0.f, deltaPos[1])
            + deltaPos[0] * Truncate(camera._cameraToWorld * Float4(1.0f, 0.f, 0.f, 0.f));
        Combine_IntoLHS(cameraToWorld, cameraFocusDrift);
        focusPoint += cameraFocusDrift;

        camera._cameraToWorld = cameraToWorld;
    }

    void CameraInputHandler::Update(float dt, const InputSnapshot& accumulatedInputState, const Float3x4& playerCharacterLocalToWorld)
    {
        static const KeyId shift = KeyId_Make("shift");
        static const KeyId tab = KeyId_Make("tab");

        static unsigned mode = 0;
        if (accumulatedInputState.IsPress(tab)) {
            mode = (mode+1)%2;
        }

        if (mode==0) {
            Camera::ClientUnit clientUnit;
            clientUnit._localToWorld = playerCharacterLocalToWorld;
            /*if (accumulatedInputState.IsPress_RButton()) {
                _unitCamera->AlignUnitToCamera(&clientUnit, _unitCamera->GetUnitCamera().yaw);
                _playerCharacter->SetLocalToWorld(clientUnit._localToWorld);     // push this back into the character transform
            }*/

            if (!accumulatedInputState.IsHeld(shift)) {

                auto camResult = _unitCamera->UpdateUnitCamera(dt, &clientUnit, accumulatedInputState);
                assert(ExtractTranslation(camResult._cameraToWorld)[0] == ExtractTranslation(camResult._cameraToWorld)[0]);
                assert(ExtractTranslation(camResult._cameraToWorld)[1] == ExtractTranslation(camResult._cameraToWorld)[1]);
                assert(ExtractTranslation(camResult._cameraToWorld)[2] == ExtractTranslation(camResult._cameraToWorld)[2]);
                _camera._cameraToWorld = AsFloat4x4(camResult._cameraToWorld);
                _camera._verticalFieldOfView = camResult._fov;

                    //  Convert from object-to-world transform into
                    //  camera-to-world transform
                std::swap(_camera._cameraToWorld(0,1), _camera._cameraToWorld(0,2));
                std::swap(_camera._cameraToWorld(1,1), _camera._cameraToWorld(1,2));
                std::swap(_camera._cameraToWorld(2,1), _camera._cameraToWorld(2,2));
                _camera._cameraToWorld(0,2) = -_camera._cameraToWorld(0,2);
                _camera._cameraToWorld(1,2) = -_camera._cameraToWorld(1,2);
                _camera._cameraToWorld(2,2) = -_camera._cameraToWorld(2,2);

            }
        } else if (mode==1) {
            UpdateCamera_Slew(_camera, dt, accumulatedInputState);
        } else if (mode==2) {
            auto orbitFocus = _orbitFocus;
            orbitFocus = TransformPoint(playerCharacterLocalToWorld, orbitFocus);
            UpdateCamera_Orbit(_camera, dt, orbitFocus, accumulatedInputState);
        }
    }

    CameraInputHandler::CameraInputHandler(
        const RenderCore::Techniques::CameraDesc& camera, 
        float charactersScale) 
    : _camera(camera) 
    , _orbitFocus(0.f, 0.f, 0.f)
    {
        _unitCamera = std::make_unique<UnitCamManager>(charactersScale);
        _unitCamera->InitUnitCamera();
    }

    CameraInputHandler::~CameraInputHandler() {}


}}

