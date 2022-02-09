// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SkeletonMachine.h"
#include "AnimationScaffoldInternal.h"
#include "ModelScaffold.h"
// #include "ModelImmutableData.h"
// #include "RawAnimationCurve.h"
#include "AssetUtils.h"
// #include "../../Assets/ChunkFileContainer.h"
// #include "../../Assets/DeferredConstruction.h"
// #include "../../Math/Quaternion.h"
// #include "../../OSServices/Log.h"

namespace RenderCore { namespace Assets
{
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    AnimationImmutableData::AnimationImmutableData() {}
    AnimationImmutableData::~AnimationImmutableData() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    const SkeletonMachine&   SkeletonScaffold::GetSkeletonMachine() const                
    {
        return *(const SkeletonMachine*)::Assets::Block_GetFirstObject(_rawMemoryBlock.get());
    }

    const ::Assets::ArtifactRequest SkeletonScaffold::ChunkRequests[]
    {
        ::Assets::ArtifactRequest { "Scaffold", ChunkType_Skeleton, 0, ::Assets::ArtifactRequest::DataType::BlockSerializer },
    };
    
    SkeletonScaffold::SkeletonScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
    {
		assert(chunks.size() == 1);
		_rawMemoryBlock = std::move(chunks[0]._buffer);
    }

    SkeletonScaffold::SkeletonScaffold(SkeletonScaffold&& moveFrom) never_throws
    : _rawMemoryBlock(std::move(moveFrom._rawMemoryBlock))
	, _depVal(std::move(moveFrom._depVal))
    {}

    SkeletonScaffold& SkeletonScaffold::operator=(SkeletonScaffold&& moveFrom) never_throws
    {
		if (_rawMemoryBlock)
            GetSkeletonMachine().~SkeletonMachine();
        _rawMemoryBlock = std::move(moveFrom._rawMemoryBlock);
		_depVal = std::move(moveFrom._depVal);
        return *this;
    }

    SkeletonScaffold::SkeletonScaffold() {}

    SkeletonScaffold::~SkeletonScaffold()
    {
        if (_rawMemoryBlock)
		    GetSkeletonMachine().~SkeletonMachine();
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    const AnimationImmutableData&   AnimationSetScaffold::ImmutableData() const                
    {
        return *(const AnimationImmutableData*)::Assets::Block_GetFirstObject(_rawMemoryBlock.get());
    }

    const ::Assets::ArtifactRequest AnimationSetScaffold::ChunkRequests[]
    {
        ::Assets::ArtifactRequest { "Scaffold", ChunkType_AnimationSet, 0, ::Assets::ArtifactRequest::DataType::BlockSerializer },
    };
    
    AnimationSetScaffold::AnimationSetScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
    {
		assert(chunks.size() == 1);
		_rawMemoryBlock = std::move(chunks[0]._buffer);
    }

    AnimationSetScaffold::AnimationSetScaffold(AnimationSetScaffold&& moveFrom) never_throws
    : _rawMemoryBlock(std::move(moveFrom._rawMemoryBlock))
	, _depVal(std::move(moveFrom._depVal))
    {}

    AnimationSetScaffold& AnimationSetScaffold::operator=(AnimationSetScaffold&& moveFrom) never_throws
    {
		if (_rawMemoryBlock)
            ImmutableData().~AnimationImmutableData();
        _rawMemoryBlock = std::move(moveFrom._rawMemoryBlock);
		_depVal = std::move(moveFrom._depVal);
        return *this;
    }

    AnimationSetScaffold::AnimationSetScaffold() {}

    AnimationSetScaffold::~AnimationSetScaffold()
    {
        if (_rawMemoryBlock)
            ImmutableData().~AnimationImmutableData();
    }

}}
