// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ModelScaffoldInternal.h"
#include "SkeletonMachine.h"
#include "AnimationScaffoldInternal.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include <vector>
#include <utility>

namespace RenderCore { namespace Assets
{
    #pragma pack(push)
    #pragma pack(1)

////////////////////////////////////////////////////////////////////////////////////////////
    //      i m m u t a b l e   d a t a         //

    struct ModelDefaultPoseData
    {
        SerializableVector<Float4x4>    _defaultTransforms;
        std::pair<Float3, Float3>       _boundingBox = {Float3(0,0,0), Float3(0,0,0)};

        friend void SerializationOperator(::Assets::BlockSerializer&, const ModelDefaultPoseData&);
    };

    struct ModelRootData
    {
        unsigned    _maxLOD = 0;
        friend void SerializationOperator(::Assets::BlockSerializer&, const ModelRootData&);
    };

    class ModelImmutableData    // todo -- deprecaated
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

}}
