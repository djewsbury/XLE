// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "UnitCamera.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../Utility/MemoryUtils.h"
#include "../ConsoleRig/Console.h"
#include "../Math/Interpolation.h"

namespace PlatformRig { namespace Camera
{
    float UnitCamManager::UnitScaleFactor() const
    {
        return 100.f / _charactersScale;
    }

    UnitCamManager::UnitCamManager(float charactersScale)
    {
        _charactersScale = charactersScale;
        _maxCamDistanceScaleFactor = 1.0f;
        _raiseCameraFromWater = false;
        _dived = false;
        _frameId = 0;
    }

    void UnitCamManager::InitUnitCamera()
    {
        float dist = 10.f*UnitScaleFactor();
        _unitCam = UnitCamera();
        _unitCam.wheelDist = dist;
        _unitCam.actualWheelDist = dist;
        _unitCam.actualDist = dist;
    }

    float UnitCamManager::GetCameraMaxDistance() const
    {
        float factor = Clamp(GetCameraMaxDistanceScaleFactor(), 0.4f, 2.0f);
        return Tweakable("MaxDist", 18.0f*UnitScaleFactor())*factor;
    }

    float UnitCamManager::GetCameraMaxDistanceScaleFactor() const
    {
        if (_maxCamDistanceScaleFactor <= 0) { return 1.f; }
        return _maxCamDistanceScaleFactor;
    }

    //---------------------------------------------------------------------------------------
    //	process
    //---------------------------------------------------------------------------------------
    
    void UnitCamManager::AlignUnitToCamera(ClientUnit* unit, float yaw)
    {
        auto rotation = unit->_localToWorld;
        Combine_IntoRHS(RotationZ(yaw), rotation);
        unit->_localToWorld = rotation;

        _unitCam.unitYaw += yaw;
        _unitCam.yaw -= yaw;
    }

    static bool CanCameraDamping(const Float3& camTargetInfo, const ClientUnit* unit, const UnitCamera* unitCam, float unitScaleFactor)
    {
        bool applyDamping = true;

        // turn off damping if player is warped
        if (applyDamping && MagnitudeSquared(unitCam->targetPos - camTargetInfo) > (100.f * 100.f * unitScaleFactor * unitScaleFactor)) {
            applyDamping = false;
        }

        return applyDamping;
    }

    void UnitCamManager::ProcessUnitCameraDamping(float dt, const Float3& info, const ClientUnit* unit)
    {
        if (!_camInertia.enabled) {
            _unitCam.targetPos = info;
            return;
        }

        if (!CanCameraDamping(info, unit, &_unitCam, UnitScaleFactor())) {
            _camInertia.Reset();
            _prevCamInertia.Reset();
            _unitCam.inertiaDist = 0.0f;
            _unitCam.targetPos = info;
            return;
        }

        // duration
        _camInertia.timer += dt;
        if (_camInertia.duration != 0.0f && _camInertia.timer > _camInertia.duration && !_camInertia.HasFlags(RESTORE)) {
            _camInertia.AddFlags(RESTORE);
            _camInertia.duration = 0.f;
            _camInertia.accelTimer = 0.f;
        }

        // move speed & camera speed
        Float3 oldTarget = _unitCam.targetPos;
        Float3 newTarget = info;
        float moveDist = Magnitude(newTarget - oldTarget) - _unitCam.inertiaDist;
        float moveSpeed = moveDist / dt;
        float cameraSpeed = _camInertia.maxSpeed ? _camInertia.maxSpeed : _prevCamInertia.maxSpeed;
        //(_unitCam.inertiaDist > _camInertia.maxDist && _prevCamInertia.maxSpeed > _camInertia.maxSpeed) ? _prevCamInertia.maxSpeed : _camInertia.maxSpeed;
        float deltaInertia = (moveSpeed - cameraSpeed) * dt;
        if (_unitCam.inertiaDist + deltaInertia < 0.0f)
            deltaInertia = -_unitCam.inertiaDist;

        if (_camInertia.HasFlags(RESTORE) && dt != 0.f) {      
            _camInertia.accelTimer += cameraSpeed * dt * dt * 0.5f;
            deltaInertia = -(cameraSpeed * dt + _camInertia.accelTimer);
        
            if (_unitCam.inertiaDist < 0.f) {
                _prevCamInertia = _camInertia;
                _unitCam.inertiaDist = 0.0f;
                _camInertia.accelTimer = 0.f;
                _camInertia.Reset();
            }
        }
        // update accel flags
        if (_camInertia.accelTime > 0.0f) {
            if (_unitCam.inertiaDist == 0.0f && deltaInertia > 0.0f) {
                _camInertia.AddFlags(ACCELERATION);
                _camInertia.accelTimer = 0.0f;
            }

            if (deltaInertia < 0.0f) {
                _camInertia.RemoveFlags(ACCELERATION);
                _camInertia.accelTimer = 0.0f;
            }

            if (_unitCam.inertiaDist > _camInertia.slowDownStartDist || deltaInertia > 0.0f) {
                _camInertia.AddFlags(CHECK_SLOW_DOWN);
                _camInertia.slowDownTimer = 0.0f;
            }
        } else {
            _camInertia.RemoveFlags(ACCELERATION);
            _camInertia.accelTimer = 0.0f;
            _camInertia.RemoveFlags(CHECK_SLOW_DOWN);
            _camInertia.slowDownTimer = 0.0f;
        }

        // acceleration & slow down
        if (deltaInertia > 0) { 
            if (_camInertia.HasFlags(ACCELERATION)) {
                if (_camInertia.accelTimer > _camInertia.accelTime) {
                    _camInertia.accelTimer = 0.0f;
                    _camInertia.RemoveFlags(ACCELERATION);
                } else {
                    _camInertia.accelTimer += dt;
                    if (_camInertia.accelTime > 0.0f)
                        deltaInertia *= _camInertia.accelTimer / _camInertia.accelTime;
                }
            }
        } else {
            float slowDownRate = 0.5f;
            if (_camInertia.HasFlags(CHECK_SLOW_DOWN) && _unitCam.inertiaDist < _camInertia.slowDownStartDist) {
                if (_unitCam.inertiaDist == 0.0f) {
                    _camInertia.RemoveFlags(CHECK_SLOW_DOWN);
                    _camInertia.slowDownTimer = 0.0f;
                } else {
                    _camInertia.slowDownTimer += dt;
                    if (_camInertia.accelTime > 0.0f)
                        deltaInertia *= 1.0f - slowDownRate * Clamp(_camInertia.slowDownTimer / _camInertia.accelTime, 0.0f, 1.0f);
                }
            }
        }

        // clamp max distance
        _unitCam.inertiaDist += deltaInertia;
        if (deltaInertia > 0) {
            if (_unitCam.inertiaDist > _camInertia.maxDist)
                _unitCam.inertiaDist = _camInertia.maxDist;
        } else {
            if (_unitCam.inertiaDist < 0.0f)
                _unitCam.inertiaDist = 0.0f;
        }

        if (_unitCam.inertiaDist > 0.0f) {
            newTarget += Normalize(oldTarget - newTarget) * _unitCam.inertiaDist;
            // hold z
            if (_camInertia.HasFlags(HOLD_Z))
                newTarget[2] = info[2];
        }

        _unitCam.targetPos = newTarget;
    }

    void UnitCamManager::ProcessUnitCamRotation(float dt, const Float3& deltaRotation, bool inWater)
    {
        float cameraMinPitch = Tweakable("camMinPitch", -88.f) * gPI/180.0f;
        float cameraMaxPitch = Tweakable("camMaxPitch",  88.f) * gPI/180.0f;
        float tiltStartPitch = Tweakable("camTiltStartPitch", 50.f) * gPI/180.0f;
        float pitch = _unitCam.pitch + _unitCam.tiltPitch;

        if (_raiseCameraFromWater) {
            float damping = std::max(0.3f, pitch * 5.0f);
            float deltaPitch = damping * dt * Tweakable("camPitchAlignSpeed", 60.f*UnitScaleFactor()) * gPI/180.0f;
            pitch = std::max(pitch - deltaPitch, 0.0f);
            _raiseCameraFromWater &= (pitch > 0.0f);
        } else {
            pitch = Clamp(pitch + deltaRotation[0], cameraMinPitch, cameraMaxPitch);
        }

        if (pitch > tiltStartPitch) {
            _unitCam.tiltPitch = pitch - tiltStartPitch;
            pitch = tiltStartPitch;
            if (inWater) {
                _unitCam.tiltPitch = 0.0f;
            }
        } else {
            _unitCam.tiltPitch = 0;
        }

        _unitCam.pitch = pitch;
        _unitCam.yaw += deltaRotation[2];

        //
        float speed = Tweakable("camRotateDampingSpeed", 0.1f);
        _unitCam.rotateDamping += fabsf(deltaRotation[0]) * speed;
        _unitCam.rotateDamping += fabsf(deltaRotation[2]) * speed;
        _unitCam.rotateDamping = std::min(_unitCam.rotateDamping, Tweakable("camRotateDampingMax", 0.1f));
        _unitCam.ratateDampingFrameID = _frameId;
    }

    void UnitCamManager::ProcessUnitCamWheelDistance(float dt, float deltaDistance)
    {
        float targetMoveRate = Clamp(_unitCam.wheelDist * 0.3f, 1.0f*UnitScaleFactor(), 20.0f*UnitScaleFactor());
        _unitCam.wheelDist = Clamp((_unitCam.wheelDist + deltaDistance * targetMoveRate), Tweakable("camMinDist", 0.f*UnitScaleFactor()), GetCameraMaxDistance());
        if (_unitCam.wheelDist < Tweakable("camCloseUpFadeOutDist", 1.f*UnitScaleFactor())) {
            _unitCam.wheelDist = (deltaDistance > 0.0f) ? Tweakable("camCloseUpFadeOutDist", 1.f*UnitScaleFactor()) : 0.0f;
        }

        float currentDelta = _unitCam.wheelDist - _unitCam.actualWheelDist;
        if (currentDelta != 0.0f) {
            float vel = currentDelta / dt;
            float maxVel = Tweakable("camZoomCatupUpBaseVel", 5.f*UnitScaleFactor()) + pow(abs(currentDelta), Tweakable("camZoomCatchUpVelPower", 1.8f));
            float finalVel = 0;
            if (currentDelta >= 0) {
                finalVel = std::min(vel, maxVel);
            } else {
                finalVel = std::max(vel, -maxVel);
            }

            if (finalVel != 0.0f) {
                float diff = dt * finalVel;
                _unitCam.actualWheelDist += diff;
                // _unitCam.zOffsetSpline.InterpolateFloat(_unitCam.actualWheelDist, _unitCam.zOffset);
                _unitCam.zOffset = 0.05f * _unitCam.actualWheelDist;

                if (_unitCam.actualBlockedDist > 0.0f) {
                    _unitCam.actualBlockedDist += diff;
                    _unitCam.actualBlockedDist = std::max(_unitCam.actualBlockedDist, 0.0f);
                }
            }
        }
    }

    struct SStanceInfo {
        float heightCollider;
        float heightPivot;
    };

    static SStanceInfo GetStanceInfo(const ClientUnit& unit)
    {
        SStanceInfo result;
        result.heightCollider = result.heightPivot = 0.f;
        return result;
    }

    static Float3 NormalizeWithZeroCheck(const Float3& input)
    {
        float magSq = MagnitudeSquared(input);
        if (magSq < 1e-10f) {
            return Float3(0.f, 0.f, 0.f);
        }
        return input * XlRSqrt(magSq);
    }

    void UnitCamManager::ProcessUnitCamActualDist(ClientUnit* unit, float dt)
    {
        float cameraVolume = 0.1f;
        float hitDist = 0.0f;
        Float3 capsuleCenter(0.f, 0.f, 0.f), dir(0.f, 0.f, 0.f);

        Float3 offset(0.f, 0.f, 0.f);

        const SStanceInfo info = GetStanceInfo(*unit);
        capsuleCenter = ExtractTranslation(unit->_localToWorld) + Float3(0, 0, info.heightCollider - info.heightPivot);
        dir = _unitCam.pivotPos-capsuleCenter;
        capsuleCenter += offset;

        if (constant_expression<true>::result()) { // !contacts || hitDist <= 0.0f) {
            Float3 forward = ExtractForward(_unitCam.camRotation);
            Float3 goal = -forward * _unitCam.actualWheelDist +
                (_unitCam.targetPos - _unitCam.pivotPos) + Float3(0,0,_unitCam.zOffset);
            float goalDist = Magnitude(goal);
            dir = goal;

            if (constant_expression<true>::result()) { // !contacts || hitDist <= 0.0f) {
                hitDist = 10000.0f*UnitScaleFactor();
            }

                // here, hitDist can be limited by a ray test against physical features (ie, for camera collisions)
           
            if (_dived && dir[2] > 0) {
                float waterLevel = 0.f; // (should be water level relative to the character position)
                waterLevel -= cameraVolume;

                float surfaceDist = waterLevel / Normalize(dir)[2];

                if (surfaceDist < 0.0f) {
                    surfaceDist = 0;
                }
                if (hitDist > surfaceDist) {
                    hitDist = surfaceDist;
                }
            }

            const float blockDistTest = Tweakable("camBlockDistTest", -1.f);
            if (blockDistTest > 0) {
                hitDist = blockDistTest;
            }

            _unitCam.hitDist = std::min(hitDist, goalDist);

            bool totalEclipse = false;
            if (hitDist < goalDist) { // blocked
                totalEclipse = true;
            }

            if (_unitCam.ratateDampingFrameID != _frameId) {
                _unitCam.rotateDamping = 0.0f;
            } else {
                _unitCam.hitDist *= (1.0f - _unitCam.rotateDamping);
            }

            _unitCam.blockedDist = goalDist - _unitCam.hitDist;

            float dist = 0;
            float diff = _unitCam.actualBlockedDist - _unitCam.blockedDist;

            if (fabsf(diff) < 0.0001f) {
                dist = diff;
            } else if (diff > 0) { //fadeout
                float fadeoutSpeed = std::max(0.01f, Tweakable("camBlockFadeoutSpeed", 3.f));
                float vel = pow(diff, Tweakable("camBlockFadeoutScale", 1.f)) * fadeoutSpeed;
                dist = std::min(diff, vel * dt);            
            } else if (diff < 0) { // closeup
                if (totalEclipse) {
                    _unitCam.actualBlockedDist = _unitCam.blockedDist;
                } else {
                    float closeupSpeed = std::max(0.01f, Tweakable("camBlockCloseupSpeed", 5.f));                
                    float vel = -pow(-diff, Tweakable("camBlockCloseupScale", 1.6f) * closeupSpeed);
                    dist = std::max(diff, vel * dt);                         
                }
            }
        
            _unitCam.actualBlockedDist -= dist;        
            _unitCam.actualDist = goalDist - _unitCam.actualBlockedDist;
            _unitCam.camPosition = _unitCam.pivotPos + _unitCam.actualDist * NormalizeWithZeroCheck(dir);
        } else {
            _unitCam.actualDist = 0.0f;
            _unitCam.hitDist = _unitCam.blockedDist = _unitCam.actualBlockedDist = _unitCam.actualWheelDist;
            _unitCam.camPosition = capsuleCenter + hitDist * NormalizeWithZeroCheck(dir);
        }
    }

    static float GetCameraYawInput(float dt, RenderOverlays::Coord2 mouseMovement)
    {
        const float limit = std::min(dt, 0.033f) * 10.0f * 2.f * gPI;
        return Clamp(-0.002f * float(mouseMovement[0]), -limit, limit);
    }

    static float GetCameraPitchInput(float dt, RenderOverlays::Coord2 mouseMovement)
    {
        const float limit = std::min(dt, 0.033f) * 10.0f * 2.f * gPI;
        return Clamp(-0.002f * float(mouseMovement[1]), -limit, limit);
    }

    static RenderOverlays::Coord2 prevMousePosition(0, 0);

    static Float3 GetCameraTargetPos(const ClientUnit& unit, float unitScaleFactor)
    {
        return ExtractTranslation(unit._localToWorld) + Float3(0.f, 0.f, 125.f / 100.f * unitScaleFactor);
    }

    static Float3 ObjectTransformToYawPitchRoll(const Float3x4& objectToWorld)
    {
            //
            //      Convert an "object transform" to ypr
            //      This is a normal object to world style transform used
            //      by objects (but not cameras)
            //
        static cml::EulerOrder eulerOrder = cml::euler_order_yxz;       
        Float3 ypr = cml::matrix_to_euler<Float3x4, Float3x4::value_type>(objectToWorld, eulerOrder);
        return Float3(ypr[2], ypr[0], ypr[1]);
    }

    UnitCamManager::OutputCamera UnitCamManager::UpdateUnitCamera(float dt, ClientUnit* unit, const InputSnapshot& snapShot)
    {
        ++_frameId;

        //////////////////////////////////////////////////////////////////////////
        // calculate camera's rotation
        bool leftButton = snapShot.IsHeld_LButton();
        bool rightButton = snapShot.IsHeld_RButton();

        Float3 deltaRot(0.f, 0.f, 0.f);
        if (rightButton || leftButton) {
            auto relativeMouse = snapShot._mousePosition;
            relativeMouse[0] -= prevMousePosition[0];
            relativeMouse[1] -= prevMousePosition[1];
            deltaRot[2] = GetCameraYawInput(dt, relativeMouse);
            deltaRot[0] = GetCameraPitchInput(dt, relativeMouse);
        }
        prevMousePosition = snapShot._mousePosition;

        const bool keepCameraRotation = false;

        Float3 ypr = ObjectTransformToYawPitchRoll(unit->_localToWorld);
        float unitYaw = ypr[0];
        if (constant_expression<keepCameraRotation>::result()) {
            _unitCam.yaw += _unitCam.unitYaw - unitYaw;
        }

        _unitCam.unitYaw = unitYaw;
        // if (!rightButton && !leftButton) {
        //     _unitCam.yaw *= 0.98f;
        // }

        // camera yaw is applied to unit rotation
        const bool isUnitAlignedToCamera = rightButton;
        if (isUnitAlignedToCamera) {
            deltaRot[2] = 0;
            //_unitCam.yaw = 0;
        }

        bool dived = _dived;
        float waterLevel = 0.f; // -actor->GetActorStats().relativeWaterLevel;
        _dived |= (waterLevel > Tweakable("camDiveStartDepth", 2.f*UnitScaleFactor()));
        _dived &= (waterLevel > Tweakable("camDiveEndDepth", 1.f*UnitScaleFactor()));
        _raiseCameraFromWater |= (dived && !_dived) && (_unitCam.pitch >0);

        if (deltaRot[0] || deltaRot[2] || _raiseCameraFromWater) {
            bool inWater = false; // actor->GetActorStats().inWaterTimer > 0.0f ? true : false;
            ProcessUnitCamRotation(dt, deltaRot, inWater);
        }

        _unitCam.CalcRotation();

        //////////////////////////////////////////////////////////////////////////
        // calculate camera's distance
        static float wheelSensitivity = -0.005f;
        ProcessUnitCamWheelDistance(dt, snapShot._wheelDelta*wheelSensitivity);

        Float3 camTargetInfo = GetCameraTargetPos(*unit, UnitScaleFactor());

        _unitCam.pivotPos = camTargetInfo;

        ProcessUnitCameraDamping(dt, camTargetInfo, unit);
        ProcessUnitCamActualDist(unit, dt);

        Float3 systemCameraPos = _unitCam.camPosition;
        auto systemCameraRotation = _unitCam.camRotation;
        Combine_IntoRHS(RotationX(_unitCam.tiltPitch), systemCameraRotation);

        float fov = Deg2Rad(Fov);
        _unitCam.fov = fov;

        OutputCamera result;
        result._cameraToWorld = systemCameraRotation;
        result._cameraToWorld(0,3) = systemCameraPos[0];
        result._cameraToWorld(1,3) = systemCameraPos[1];
        result._cameraToWorld(2,3) = systemCameraPos[2];
        result._fov = fov;
        return result;
    }

    void CameraInertia::Reset()
    {
        timer = 0.0f;
        duration = 0.0f;
        slowDownTimer = 0.0f;
        SetFlags(DEFAULT);
        enabled = false;
    }

    void UnitCamera::CalcRotation() 
    { 
        camRotation = Identity<Float3x4>();
        Combine_IntoLHS(camRotation,    RotationX(pitch));
        Combine_IntoLHS(camRotation,    RotationZ(unitYaw+yaw));
    }
}}



