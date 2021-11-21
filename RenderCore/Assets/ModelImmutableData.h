// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ModelScaffoldInternal.h"
#include "SkeletonScaffoldInternal.h"
#include "AnimationScaffoldInternal.h"
#include <vector>
#include <utility>

namespace RenderCore { namespace Assets
{
    #pragma pack(push)
    #pragma pack(1)

////////////////////////////////////////////////////////////////////////////////////////////
    //      i m m u t a b l e   d a t a         //

    class ModelImmutableData
    {
    public:
        ModelCommandStream          _visualScene;
        
        RawGeometry*                _geos;
        size_t                      _geoCount;
        BoundSkinnedGeometry*       _boundSkinnedControllers;
        size_t                      _boundSkinnedControllerCount;

        SkeletonMachine				_embeddedSkeleton;
        Float4x4*                   _defaultTransforms;
        size_t                      _defaultTransformCount;        

        std::pair<Float3, Float3>   _boundingBox;
        unsigned                    _maxLOD;

        ModelImmutableData() = delete;
        ~ModelImmutableData();
    };

    class ModelSupplementImmutableData
    {
    public:
        SupplementGeo*  _geos;
        size_t          _geoCount;
    };

    #pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////////////////
    //      b i n d i n g s         //

    class AnimationSetBinding
    {
    public:
        unsigned GetCount() const { return (unsigned)_animDriverToMachineParameter.size(); }
        unsigned AnimDriverToMachineParameter(unsigned index) const { return _animDriverToMachineParameter[index]; }

        AnimationSetBinding(const AnimationSet::OutputInterface&            output,
                            const SkeletonMachine::InputInterface&    input);
        AnimationSetBinding();
        AnimationSetBinding(AnimationSetBinding&& moveFrom) never_throws;
        AnimationSetBinding& operator=(AnimationSetBinding&& moveFrom) never_throws;
        ~AnimationSetBinding();

    private:
        std::vector<unsigned>   _animDriverToMachineParameter;
    };

    class SkeletonBinding
    {
    public:
        unsigned GetModelJointCount() const { return (unsigned)_modelJointIndexToMachineOutput.size(); }
        unsigned ModelJointToMachineOutput(unsigned index) const { return _modelJointIndexToMachineOutput[index]; }

        SkeletonBinding(    const SkeletonMachine::OutputInterface&		output,
                            const ModelCommandStream::InputInterface&   input);
        SkeletonBinding();
        ~SkeletonBinding();

    private:
        std::vector<unsigned>   _modelJointIndexToMachineOutput;
    };
}}
