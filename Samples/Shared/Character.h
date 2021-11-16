// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AnimationDecisionTree.h"
#include "../../PlatformRig/CameraManager.h"
#include "../../Assets/AssetsCore.h"

namespace FixedFunctionModel
{
    class SharedStateSet;
    class AnimationImmutableData;
    class AnimationSet;
    class ModelRenderer;
    class SkinPrepareMachine;
}

namespace RenderCore { namespace Assets
{
    class ModelScaffold;
    class SkeletonScaffold;
    class AnimationSetScaffold;
}}

namespace Sample
{
    namespace Network { typedef uint64 StateBundleId; typedef uint64 NetworkTime; }

    class AnimationDecisionTree;

    class CharacterInputFiles
    {
    public:
        std::string     _skin;
        std::string     _animationSet;
        std::string     _skeleton;

        uint64 MakeHash() const;
    };

    class CharacterModel
    {
    public:
        CharacterModel(
            const CharacterInputFiles& files,
            RenderCore::Assets::SharedStateSet& sharedStates);
        ~CharacterModel();

        const RenderCore::Assets::AnimationImmutableData& GetAnimationData() const;
        const RenderCore::Assets::AnimationSet& GetAnimationSet() const;
        const RenderCore::Assets::ModelRenderer& GetRenderer() const;
        const RenderCore::Assets::SkinPrepareMachine& GetPrepareMachine() const;

        const RenderCore::Assets::ModelScaffold& GetModelScaffold() const;

    protected:
        ::Assets::PtrToMarkerPtr<RenderCore::Assets::ModelScaffold> _model;
        ::Assets::PtrToMarkerPtr<RenderCore::Assets::SkeletonScaffold> _skeleton;
        ::Assets::PtrToMarkerPtr<RenderCore::Assets::AnimationSetScaffold> _animationSet;

        std::unique_ptr<RenderCore::Assets::ModelRenderer> _renderer;
        std::unique_ptr<RenderCore::Assets::SkinPrepareMachine> _prepareMachine;

        #if defined(_DEBUG)
            Assets::rstring _skinInitialiser;
            Assets::rstring _skeletonInitialiser;
            Assets::rstring _animationSetInitialiser;
            const ::Assets::ResChar* SkinInitialiser() const { return _skinInitialiser.c_str(); }
            const ::Assets::ResChar* SkeletonInitialiser() const { return _skeletonInitialiser.c_str(); }
            const ::Assets::ResChar* AnimationSetInitialiser() const { return _animationSetInitialiser.c_str(); }
        #else
            const ::Assets::ResChar* SkinInitialiser() const { return ""; }
            const ::Assets::ResChar* SkeletonInitialiser() const { return ""; }
            const ::Assets::ResChar* AnimationSetInitialiser() const { return ""; }
        #endif
    };

    class Character
    {
    public:
        const CharacterModel*   _model;
        AnimationState          _animState;
        Float4x4                _localToWorld;
        uint64                  _id;

        const Float4x4&     GetLocalToWorld() const { return _localToWorld; }
        void                SetLocalToWorld(const Float4x4& newTransform) { _localToWorld = newTransform; }

        Character(uint64 id, const CharacterModel& model);
    };

    class NPCCharacter : public Character
    {
    public:
        float   _walkingVelocity;
        Float3  _targetPosition;
        float   _retargetCountdown;
        float   _currentVelScale;
        std::shared_ptr<AnimationDecisionTree> _animDecisionTree;

        NPCCharacter(uint64 id, const CharacterModel& model, std::shared_ptr<AnimationDecisionTree> animDecisionTree);
        void Update(float deltaTime);
        void Retarget();
    };

    class PlayerCharacter : public Character
    {
    public:
        void    Update(float deltaTime);
        void    Accumulate(const PlatformRig::InputSnapshot& evnt);
        PlayerCharacter(uint64 id, const CharacterModel& model, std::shared_ptr<AnimationDecisionTree> animDecisionTree);
    protected:
        std::shared_ptr<AnimationDecisionTree>  _animDecisionTree;
        PlatformRig::InputSnapshot              _accumulatedState;
        PlatformRig::InputSnapshot              _prevAccumulatedState;

        struct StateBundleContents
        {
            Float3 _translation; float _yaw;
        };
        #if defined(ENABLE_XLNET)
            std::shared_ptr<Network::StatePropagation::StateBundle>      _stateBundle;
        #endif
    };
    
    template<typename Type> class Spline
    {
    public:
        struct DataPoint
        {
            Type                    _value;
            Network::NetworkTime    _time;
        };
        DataPoint   _points[4];

        Spline();
        Type        Calculate(Network::NetworkTime time);
    };

    template<typename Type>
        Spline<Type>::Spline()
        {
            for (unsigned c=0; c<dimof(_points); ++c) {
                _points[c]._time = 0;
            }
        }

    template<typename Type>
        static Type HermiteInterpolation(float s, const Type& p1, const Type& t1, const Type& p2, const Type& t2)
    {
        float s2 = s * s;
        float s3 = s2 * s;
        float h1 = 2.f * s3 - 3 * s2 + 1.0f;
        float h2 = -2.f * s3 + 3 * s2;
        float h3 = s3 - 2.f * s2 + s;
        float h4 = s3 - s2;

        return h1 * p1 + h2 * p2 + h3 * t1 + h4 * t2;
    }

    template<typename Type>
        Type        Spline<Type>::Calculate(Network::NetworkTime time)
        {
            float timeScale = 1.0f / (_points[3]._time - _points[1]._time);
            Type tan0 = (_points[1]._value - _points[0]._value) * ((_points[3]._time - _points[1]._time) / float(_points[1]._time - _points[0]._time));
            Type tan1 = (_points[3]._value - _points[2]._value) * ((_points[3]._time - _points[1]._time) / float(_points[3]._time - _points[2]._time));
            return HermiteInterpolation(
                (time - _points[1]._time) * timeScale,
                _points[1]._value, tan0,
                _points[3]._value, tan1);
        }

    class NetworkCharacter : public Character
    {
    public:
        void    Update(float deltaTime);
        void    HandleEvent(const char eventString[]);
        bool    IsAttached(Network::StateBundleId bundleId);
        NetworkCharacter(
            uint64 id,
            const CharacterModel& model
            #if defined(ENABLE_XLNET)
                , std::shared_ptr<Network::StatePropagation::StateBundle>& stateBundle
            #endif
            , std::shared_ptr<AnimationDecisionTree> animDecisionTree
            );

    protected:
        struct StateBundleContents { Float3 _translation; float _yaw; };
        #if defined(ENABLE_XLNET)
            std::shared_ptr<Network::StatePropagation::StateBundle>      _stateBundle;
        #endif

        Spline<Float3>          _translationSpline;
        Spline<float>           _yawSpline;
        StateBundleContents     _currentValues;
        bool                    _fullOnDataPoints;
        std::string             _queuedEvent;
        std::shared_ptr<AnimationDecisionTree> _animDecisionTree;
    };

    inline bool operator<(const Character& lhs, const Character& rhs) 
    { 
        if (lhs._model < rhs._model) return true;
        if (lhs._model > rhs._model) return false;
        if (lhs._animState._animation < rhs._animState._animation) return true;
        if (lhs._animState._animation > rhs._animState._animation) return false;
        return lhs._animState._time < rhs._animState._time; 
    }

}