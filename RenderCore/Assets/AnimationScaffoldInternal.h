// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AnimationSet.h"

namespace RenderCore { namespace Assets
{
    #pragma pack(push)
    #pragma pack(1)
    
    class AnimationImmutableData
    {
    public:
        AnimationSet							_animationSet;

        AnimationImmutableData();
        ~AnimationImmutableData();
    };

    #pragma pack(pop)

}}

