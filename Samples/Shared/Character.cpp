// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Character.h"
#include "SampleGlobals.h"
#include "../../RenderCore/Assets/ModelRunTime.h"
#include "../../RenderCore/Assets/RawMaterial.h"
#include "../../RenderCore/Assets/MaterialScaffold.h"
#include "../../RenderCore/Assets/AnimationScaffoldInternal.h"
#include "../../Assets/Assets.h"
#include "../../Assets/IntermediateAssets.h"
#include "../../Assets/AssetUtils.h"
#include "../../Math/Transformations.h"
#include "../../ConsoleRig/Console.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/StringFormat.h"

#if defined(ENABLE_TERRAIN)
    #include "../../SceneEngine/Terrain.h"
#endif

namespace Sample
{
    static const std::string WeaponSlash = "onehand_co_sk_weapon_slash";

    const RenderCore::Assets::AnimationImmutableData& CharacterModel::GetAnimationData() const  
    {
		auto actual = _animationSet->TryActualize();
		if (!actual) Throw(::Exceptions::BasicLabel("Pending or invalid data in CharacterModel (%s)", AnimationSetInitialiser()));
        return (*actual)->ImmutableData(); 
    }

    const RenderCore::Assets::AnimationSet& CharacterModel::GetAnimationSet() const
    {
		auto actual = _animationSet->TryActualize();
		if (!actual) Throw(::Exceptions::BasicLabel("Pending or invalid data in CharacterModel (%s)", AnimationSetInitialiser()));
        return (*actual)->ImmutableData()._animationSet; 
    }

    const RenderCore::Assets::ModelRenderer& CharacterModel::GetRenderer() const
    {
		if (!_renderer) Throw(::Exceptions::BasicLabel("Pending or invalid data in CharacterModel (%s)", SkinInitialiser()));
        return *_renderer; 
    }

    const RenderCore::Assets::SkinPrepareMachine& CharacterModel::GetPrepareMachine() const
    {
		if (!_prepareMachine) Throw(::Exceptions::BasicLabel("Pending or invalid data in CharacterModel (%s)", SkinInitialiser()));
        return *_prepareMachine; 
    }

    const RenderCore::Assets::ModelScaffold& CharacterModel::GetModelScaffold() const
    {
		auto actual = _model->TryActualize();
		if (!actual) Throw(::Exceptions::BasicLabel("Pending or invalid data in CharacterModel (%s)", SkinInitialiser()));
        return **actual;
    }

///////////////////////////////////////////////////////////////////////////////

    CharacterModel::CharacterModel(
        const CharacterInputFiles& files, 
        RenderCore::Assets::SharedStateSet& sharedStates)
    {
        #if defined(_DEBUG)
            _skinInitialiser = files._skin; _skeletonInitialiser = files._skeleton; _animationSetInitialiser = files._animationSet;
        #endif

        using namespace RenderCore::Assets;
        _model = ::Assets::MakeAsset<ModelScaffold>(files._skin.c_str());
        _skeleton = Assets::MakeAsset<SkeletonScaffold>(files._skeleton.c_str());
        _animationSet = Assets::MakeAsset<AnimationSetScaffold>(files._animationSet.c_str());
        auto matScaffold = Assets::MakeAsset<MaterialScaffold>(files._skin.c_str(), files._skin.c_str());

        auto searchRules = Assets::DefaultDirectorySearchRules(files._skin.c_str());
        const unsigned levelOfDetail = 0;

            // stall here until our resources are full loaded
        _model->StallWhilePending();
        matScaffold->StallWhilePending();
        _renderer = std::make_unique<ModelRenderer>(
            *_model->Actualize(), *matScaffold->Actualize(), ModelRenderer::Supplements(), 
            sharedStates, &searchRules, levelOfDetail);

        _skeleton->StallWhilePending();
        _animationSet->StallWhilePending();
        _prepareMachine = std::make_unique<SkinPrepareMachine>(*_model->Actualize(), *_animationSet->Actualize(), *_skeleton->Actualize());
    }

    CharacterModel::~CharacterModel()
    {}


    Character::Character(uint64 id, const CharacterModel& model)
    : _model(&model)
    , _localToWorld(Identity<Float4x4>())
    , _id(id)
    {
    }

///////////////////////////////////////////////////////////////////////////////

    NPCCharacter::NPCCharacter(uint64 id, const CharacterModel& model, std::shared_ptr<AnimationDecisionTree> animDecisionTree)
    : Character(id, model)
    {
        _walkingVelocity = LinearInterpolate(200.f, 500.f, rand() / float(RAND_MAX)) / CharactersScale;

        Float3 translation(
            LinearInterpolate(-1500.f, 1500.f, rand() / float(RAND_MAX)) / CharactersScale,
            LinearInterpolate(-1500.f, 1500.f, rand() / float(RAND_MAX)) / CharactersScale,
            0.f);
        Combine_IntoLHS(_localToWorld, translation);

        _currentVelScale = 1.f;
        _animDecisionTree = std::move(animDecisionTree);
    }

    void NPCCharacter::Retarget()
    {
        _targetPosition = Float3(
            LinearInterpolate(-1500.f, 1500.f, rand() / float(RAND_MAX)),
            LinearInterpolate(-1500.f, 1500.f, rand() / float(RAND_MAX)),
            0.f);
        float A = rand() / float(RAND_MAX);
        A *= A; A *= A;
        _retargetCountdown = LinearInterpolate(20.f, 2.f, A);
    }

    void NPCCharacter::Update(float deltaTime)
    {
        /*const float animationStartTime =    Tweakable("AnimStartTime",  0.f); //30.f);
        const float animationEndTime   =    Tweakable("AnimEndTime",    2.f); // 40.f);
        _currentTime    
            = animationStartTime 
            + XlFMod(   std::max(0.f, _currentTime-animationStartTime+_animationSpeed*deltaTime), 
                        animationEndTime-animationStartTime);*/

        _retargetCountdown -= deltaTime;
        if (_retargetCountdown < 0.f) {
            Float3 targetOffset(
                LinearInterpolate(-500.f, 500.f, rand() / float(RAND_MAX)) / CharactersScale,
                LinearInterpolate(-500.f, 500.f, rand() / float(RAND_MAX)) / CharactersScale,
                0.f);
            _targetPosition      = ExtractTranslation(_localToWorld) + targetOffset;
            _retargetCountdown   = LinearInterpolate(3.f, 10.f, rand() / float(RAND_MAX));
        }

        Float3 targetOffset      = _targetPosition - ExtractTranslation(_localToWorld);
        Float3 currentForward    = ExtractForward(_localToWorld);

        float desiredAngle       = XlATan2(targetOffset[1], targetOffset[0]);
        float currentAngle       = XlATan2(currentForward[1], currentForward[0]);
        float angleDifference    = XlAbs(desiredAngle - currentAngle);

        float flipDirection = 1.f;
        if (angleDifference > gPI) {
            angleDifference = (2.f * gPI) - angleDifference;
            flipDirection = -1.f;
        }

        float velScale = 1.f;

        const float stopDistance = Tweakable("StopDistance", 100.f) / CharactersScale;
        if (MagnitudeSquared(targetOffset) < stopDistance * stopDistance) {
            velScale = 0.f;
        } else {
            if (angleDifference > gHalfPI) {
                velScale *= 1.f - Clamp((angleDifference - gHalfPI) / gHalfPI, 0.f, 0.75f);
            }
        }
        _currentVelScale = LinearInterpolate(_currentVelScale, velScale, 0.05f);

        const float maxAdjust    = (1.f + (1.f-_currentVelScale)) * Deg2Rad(Tweakable("NPCRotation", 50.f)*deltaTime);
        float rotation           = Clamp(flipDirection * (desiredAngle-currentAngle), -maxAdjust, maxAdjust);
        Combine_IntoRHS(RotationZ(rotation), _localToWorld);

        Float3 localMovement(0.f, _walkingVelocity * _currentVelScale * deltaTime, 0.f);
    
        if (_animDecisionTree) {
            _animState = _animDecisionTree->Update(deltaTime, _animState, localMovement, rotation);
        }
        Combine_IntoRHS(localMovement, _localToWorld);
    }

///////////////////////////////////////////////////////////////////////////////

    static float CalculateYaw(const Float4x4& localToWorld)
    {
        const cml::EulerOrder eulerOrder = cml::euler_order_yxz;
        Float3 ypr = cml::matrix_to_euler<Float4x4, Float4x4::value_type>(localToWorld, eulerOrder);
        return ypr[2];
    }

    PlayerCharacter::PlayerCharacter(uint64 id, const CharacterModel& model, std::shared_ptr<AnimationDecisionTree> animDecisionTree) 
        : Character(id, model) 
    {
        #if defined(ENABLE_XLNET)
            auto& stateWorld = Network::StatePropagation::StateWorld::GetInstance();
            auto stateBundle = stateWorld.CreateAuthoritativePacket(
                sizeof(StateBundleContents), Hash64("PlayerCharacter"));

            // Combine_IntoRHS(Float3(2048.f, 2048.f, 100.f), _localToWorld);
            Combine_IntoRHS(Float3(512.f, 512.f, 250.f), _localToWorld);

            StateBundleContents contents;
            contents._translation   = ExtractTranslation(_localToWorld);
            contents._yaw           = CalculateYaw(_localToWorld);
            stateBundle->UpdateData(&contents, sizeof(contents));
            stateBundle->UpdateVisibility(
                Network::StatePropagation::VisibilityObject(
                    std::make_pair(Float3(-1.f, -1.f, -1.f), Float3( 1.f,  1.f,  1.f)), 0));

            _stateBundle = std::move(stateBundle);
        #endif
        _animDecisionTree = std::move(animDecisionTree);
    }

    void    PlayerCharacter::Accumulate(const PlatformRig::InputSnapshot& evnt)
    {
        _accumulatedState.Accumulate(evnt, _prevAccumulatedState);
    }

    void    PlayerCharacter::Update(float deltaTime)
    {
        // This is only intended to be a very basic implementation.
        // Practical applications will require more sophisticated patterns.
        using namespace RenderOverlays::DebuggingDisplay;
        static const KeyId forward      = KeyId_Make("w");
        static const KeyId back         = KeyId_Make("s");
        static const KeyId left         = KeyId_Make("a");
        static const KeyId right        = KeyId_Make("d");
        static const KeyId turnLeft     = KeyId_Make("left");
        static const KeyId turnRight    = KeyId_Make("right");
        static const KeyId attack       = KeyId_Make("space");

        const float movementSpeed = 350.f / CharactersScale;
        const float rotationSpeed = 1.f * gPI;

        Float3 localMovement(0.f, 0.f, 0.f);
        float localRotation(0.f);

        if (_accumulatedState.IsHeld_RButton()) {
            if (_accumulatedState.IsPress_RButton()) {
            } else {
                Coord deltaX = _accumulatedState._mouseDelta[0];
                localRotation = float(deltaX) * -0.0025f;
            }
        }
    
        if (    _accumulatedState.IsHeld(forward) 
            || (_accumulatedState.IsHeld_LButton() && _accumulatedState.IsHeld_RButton())) {
            localMovement[1] += deltaTime * movementSpeed;
        } else if (_accumulatedState.IsHeld(back)) {
            localMovement[1] -= deltaTime * movementSpeed;
        }

        if (_accumulatedState.IsHeld(right)) {
            localMovement[0] += deltaTime * movementSpeed;
        } else if (_accumulatedState.IsHeld(left)) {
            localMovement[0] -= deltaTime * movementSpeed;
        }

        if (_accumulatedState.IsHeld(turnLeft)) {
            localRotation += deltaTime * rotationSpeed;
        } else if (_accumulatedState.IsHeld(turnRight)) {
            localRotation -= deltaTime * rotationSpeed;
        }

        if (_animDecisionTree) {
            if (_accumulatedState.IsPress(attack)) {
                _animState = _animDecisionTree->PlayAnimation(Hash64(WeaponSlash), _animState, _model->GetAnimationSet());
                #if defined(ENABLE_XLNET)
                    _stateBundle->Event("attack");
                #endif
            } else {
                _animState = _animDecisionTree->Update(deltaTime, _animState, localMovement, localRotation);
            }
        }

        Combine_IntoRHS(localMovement, _localToWorld);

        if (localRotation != 0.f) {
            Combine_IntoRHS(RotationZ(localRotation), _localToWorld);
        }

            // clamp to terrain...
        #if defined(ENABLE_TERRAIN)
            if (MainTerrainFormat) {
                auto pos = ExtractTranslation(_localToWorld);
                pos[2] = SceneEngine::GetTerrainHeight(*MainTerrainFormat.get(), MainTerrainConfig, MainTerrainCoords, Truncate(pos));
                SetTranslation(_localToWorld, pos);
            }
        #endif

        _prevAccumulatedState = _accumulatedState;
        _accumulatedState.Reset();

        {
            static signed countdown = 10;
            if (countdown==0) {
                StateBundleContents contents;
                contents._translation   = ExtractTranslation(_localToWorld);
                contents._yaw           = CalculateYaw(_localToWorld);
                #if defined(ENABLE_XLNET)
                    _stateBundle->UpdateData(&contents, sizeof(contents));
                #endif
                countdown = 10-1;
            } else {
                --countdown;
            }
        }

        // static float animationTime = 0.f;
        // animationTime = XlFMod(animationTime + 0.1f * deltaTime, 1.f);
        // _animState._animation = Hash64(AnimationNames::RunForward);
        // _animState._time = animationTime;
    }

///////////////////////////////////////////////////////////////////////////////

    #if !defined(ENABLE_XLNET)
        namespace Network { typedef uint64 NetworkTime; typedef uint64 StateBundleId; }
    #endif

    void    NetworkCharacter::Update(float deltaTime)
    {
    #if defined(ENABLE_XLNET)
        auto data = _stateBundle->LockData();
        const StateBundleContents& lastState = *(const StateBundleContents*)data.Get();

            //// /////////////////////// //// ////                         ////
            ////                         //// //// /////////////////////// ////

        Network::NetworkTime currentTime = Network::LocalMeshNode::GetInstance().GetNetworkTime();
        Network::NetworkTime bundleTime = data.GetTime();
        if (bundleTime != _translationSpline._points[3]._time) {
            bool stillFillingIn = false;
            for (unsigned c=0; c<4; ++c) {
                if (!_translationSpline._points[c]._time) {
                    if (!c || _translationSpline._points[c-1]._time != bundleTime) {
                        _translationSpline._points[c]._value = lastState._translation;
                        _translationSpline._points[c]._time = bundleTime;
                        _yawSpline._points[c]._value = lastState._yaw;
                        _yawSpline._points[c]._time = bundleTime;
                    }
                    stillFillingIn = true;
                }
            }
            if (!stillFillingIn) {
                for (unsigned c=0; c<3; ++c) {
                    _translationSpline._points[c] = _translationSpline._points[c+1];
                    _yawSpline._points[c] = _yawSpline._points[c+1];
                }
                _translationSpline._points[3]._value = lastState._translation;
                _translationSpline._points[3]._time = bundleTime;
                _yawSpline._points[3]._value = lastState._yaw;
                _yawSpline._points[3]._time = bundleTime;
                _fullOnDataPoints = true;
            }
        }
    
            //// /////////////////////// //// ////                         ////
            ////                         //// //// /////////////////////// ////

                //
                //      -=- Let do some basic dead reckoning -=-
                //
                //              Note that the yaw dead reckoning 
                //              isn't working well... We have the handle the way 
                //              angles wrap around better.
                //

        Float3 translation;
        float yaw;
        if (_fullOnDataPoints) {
            translation = _translationSpline.Calculate(currentTime);
            yaw = _yawSpline.Calculate(currentTime);
        } else {
            translation = lastState._translation;
            yaw = lastState._yaw;
        }

        _currentValues._translation  = LinearInterpolate(_currentValues._translation, translation, 0.1f);
        _currentValues._yaw          = LinearInterpolate(_currentValues._yaw, yaw, 0.1f);

        Float4x4 bundleLocalToWorld = Identity<Float4x4>();
        Combine_IntoLHS(bundleLocalToWorld, RotationZ(_currentValues._yaw));
        Combine_IntoLHS(bundleLocalToWorld, _currentValues._translation);

        Float3 localMovement = TransformPoint(InvertOrthonormalTransform(_localToWorld), ExtractTranslation(bundleLocalToWorld));

        if (_animDecisionTree) {
            if (_queuedEvent.empty()) {
                _animState           = _animDecisionTree->Update(deltaTime, _animState, localMovement, 0.f);
            } else {
                _animState           = _animDecisionTree->PlayAnimation(Hash64(WeaponSlash), _animState, _model->GetAnimationSet());
                _queuedEvent = std::string();
            }
        }
    
        _localToWorld = bundleLocalToWorld;
    #endif
    }

    void    NetworkCharacter::HandleEvent(const char eventString[])
    {
        _queuedEvent = eventString;
    }

    bool    NetworkCharacter::IsAttached(Network::StateBundleId bundleId)
    {
        #if defined(ENABLE_XLNET)
            return _stateBundle->GetId() == bundleId;
        #else
            return false;
        #endif
    }

    NetworkCharacter::NetworkCharacter( 
        uint64 id,
        const CharacterModel& model,
        #if defined(ENABLE_XLNET)
            std::shared_ptr<Network::StatePropagation::StateBundle>& stateBundle,
        #endif
        std::shared_ptr<AnimationDecisionTree> animDecisionTree)
    : Character(id, model)
    #if defined(ENABLE_XLNET)
    ,       _stateBundle(stateBundle)
    #endif
    ,       _fullOnDataPoints(false)
    {
        #if defined(ENABLE_XLNET)
            auto data = stateBundle->LockData();
            _currentValues = *(const StateBundleContents*)data.Get();
        #endif

        _animDecisionTree = std::move(animDecisionTree);
    }

    uint64 CharacterInputFiles::MakeHash() const
    {
        return HashCombine(Hash64(_skin), HashCombine(Hash64(_animationSet), Hash64(_skeleton)));
    }
}


#include "../../Utility/Meta/ClassAccessors.h"
#include "../../Utility/Meta/ClassAccessorsImpl.h"

template<> const ClassAccessors& Legacy_GetAccessors<Sample::CharacterInputFiles>()
{
    using Obj = Sample::CharacterInputFiles;
    static ClassAccessors props(typeid(Obj).hash_code());
    static bool init = false;
    if (!init) {
        props.Add("Skin",            DefaultGet(Obj, _skin),          DefaultSet(Obj, _skin));
        props.Add("AnimationSet",    DefaultGet(Obj, _animationSet),  DefaultSet(Obj, _animationSet));
        props.Add("Skeleton",        DefaultGet(Obj, _skeleton),      DefaultSet(Obj, _skeleton));
        init = true;
    }
    return props;
}

