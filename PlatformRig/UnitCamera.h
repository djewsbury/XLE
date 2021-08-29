// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Core/Types.h"
#include "../Math/Transformations.h"

namespace PlatformRig { class InputSnapshot; }

namespace PlatformRig { namespace Camera
{

    static const float Fov = 40.f;

    enum Flags {    // [blue_120425] inertia flags
        ACCELERATION    = 1 << 0,
        CHECK_SLOW_DOWN = 1 << 1,
        HOLD_Z          = 1 << 2,
        DEFAULT         = 1 << 3,
        RESTORE         = 1 << 4,
    };

    struct UnitCamera {
        // ZOffsetInterpolator zOffsetSpline;

        float wheelDist;        // player-controlled distance (maybe by scrolling mouse-wheel)
        float actualWheelDist;

        float inertiaDist;

        float hitDist;
        float blockedDist;      // 0 > if there is a wall between target and goal position (= actualWheelDist + inertiaDist)
        float actualBlockedDist;

        float actualDist;       // real distance

        //float inertiaRot;

        Float3 pivotPos;
        float zOffset;
        Float3 oldTargetPos;      // target position of previous frame
        Float3 targetPos;

        Float3 camPosition;
        Float3x4 camRotation;

        float unitYaw;
        float yaw;
        float pitch;
        float tiltPitch;

        float rotateDamping;
        uint32 ratateDampingFrameID;

        float fov;

        UnitCamera() : actualDist(0), inertiaDist(0), pivotPos(0.f, 0.f, 0.f), zOffset(0),
            targetPos(0.f, 0.f, 0.f), camPosition(0.f, 0.f, 0.f), camRotation(Identity<Float3x4>()), unitYaw(0), yaw(0), pitch(0), tiltPitch(0),
            actualWheelDist(0), wheelDist(0), blockedDist(0), actualBlockedDist(0), rotateDamping(0), ratateDampingFrameID(0), fov(Fov) {}

        void CalcRotation();
    };

    struct CameraInertia {
        float timer;
        float maxSpeed;
        float maxDist;
        float duration;

        float accelTimer;
        float accelTime;

        float slowDownTimer;
        float slowDownStartDist;
        uint32 flags;

        bool controllerDependent;
        float controllerSpeedRate;

        bool enabled;

        void AddFlags(uint32 f) { flags |= f; }
        void RemoveFlags(uint32 f) { flags &= ~f; }
        void SetFlags(uint32 f) { flags = f; }
        bool HasFlags(uint32 f) const { return ((flags & f) == f); }

        void Reset();

        CameraInertia()
            : timer(0),
            maxSpeed(0),
            maxDist(0),
            duration(0),
            accelTimer(0),
            accelTime(0),
            slowDownTimer(0),
            slowDownStartDist(0),
            flags(0),
            controllerDependent(false),
            controllerSpeedRate(0),
            enabled(true) {}
    };

    class ClientUnit
    {
    public:
        Float3x4 _localToWorld;
    };

    class UnitCamManager
    {
    public:
        class OutputCamera
        {
        public:
            Float3x4    _cameraToWorld;
            float       _fov;
        };
        void InitUnitCamera();
        void AlignUnitToCamera(ClientUnit* unit, float yaw);
        OutputCamera UpdateUnitCamera(float dt, ClientUnit* myUnit, const InputSnapshot& snapShot);

        float GetCameraMaxDistance() const;
        float GetCameraMaxDistanceScaleFactor() const;
        const UnitCamera& GetUnitCamera() const { return _unitCam; }

        UnitCamManager(float charactersScale);
    private:
        void ProcessUnitCameraDamping(float dt, const Float3& info, const ClientUnit* unit);
        void ProcessUnitCamRotation(float dt, const Float3& deltaRotation, bool inWater);
        void ProcessUnitCamWheelDistance(float dt, float zoomDelta);
        void ProcessUnitCamActualDist(ClientUnit* unit, float dt);

        UnitCamera      _unitCam;
        CameraInertia   _camInertia;
        CameraInertia   _prevCamInertia;

        float       _maxCamDistanceScaleFactor;
        bool        _raiseCameraFromWater;
        bool        _dived;
        unsigned    _frameId;

        float       _charactersScale;

        float UnitScaleFactor() const;
    };

}}

