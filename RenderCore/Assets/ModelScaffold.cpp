// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelScaffold.h"
#include "ModelScaffoldInternal.h"
#include "ModelImmutableData.h"
#include "AssetUtils.h"
#include "../../Assets/ChunkFileContainer.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/PtrUtils.h"

namespace RenderCore { namespace Assets
{
    static const unsigned ModelScaffoldVersion = 1;
    static const unsigned ModelScaffoldLargeBlocksVersion = 0;

    template <typename Type>
        void DestroyArray(const Type* begin, const Type* end)
        {
            for (auto i=begin; i!=end; ++i) { i->~Type(); }
        }

////////////////////////////////////////////////////////////////////////////////

        /// This DestroyArray stuff is too awkward! We could use a serializable vector instead
    ModelCommandStream::~ModelCommandStream()
    {
        DestroyArray(_geometryInstances,        &_geometryInstances[_geometryInstanceCount]);
        DestroyArray(_skinControllerInstances,  &_skinControllerInstances[_skinControllerInstanceCount]);
    }

    BoundSkinnedGeometry::~BoundSkinnedGeometry() {}

    ModelImmutableData::~ModelImmutableData()
    {
        DestroyArray(_geos, &_geos[_geoCount]);
        DestroyArray(_boundSkinnedControllers, &_boundSkinnedControllers[_boundSkinnedControllerCount]);
    }

    uint64_t GeoInputAssembly::BuildHash() const
    {
            //  Build a hash for this object.
            //  Note that we should be careful that we don't get an
            //  noise from characters in the left-over space in the
            //  semantic names. Do to this right, we should make sure
            //  that left over space has no effect.
        auto elementsHash = Hash64(AsPointer(_elements.cbegin()), AsPointer(_elements.cend()));
        elementsHash ^= uint64_t(_vertexStride);
        return elementsHash;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	const ModelImmutableData&   ModelScaffold::ImmutableData() const                
    {
        return *(const ModelImmutableData*)::Assets::Block_GetFirstObject(_rawMemoryBlock.get());
    }

    const ModelCommandStream&       ModelScaffold::CommandStream() const                { return ImmutableData()._visualScene; }
    const SkeletonMachine&			ModelScaffold::EmbeddedSkeleton() const             { return ImmutableData()._embeddedSkeleton; }
    std::pair<Float3, Float3>       ModelScaffold::GetStaticBoundingBox(unsigned) const { return ImmutableData()._boundingBox; }
    unsigned                        ModelScaffold::GetMaxLOD() const                    { return ImmutableData()._maxLOD; }

	std::shared_ptr<::Assets::IFileInterface>	ModelScaffold::OpenLargeBlocks() const { return _largeBlocksReopen(); }

    const ::Assets::ArtifactRequest ModelScaffold::ChunkRequests[2]
    {
        ::Assets::ArtifactRequest { "Scaffold", ChunkType_ModelScaffold, ModelScaffoldVersion, ::Assets::ArtifactRequest::DataType::BlockSerializer },
        ::Assets::ArtifactRequest { "LargeBlocks", ChunkType_ModelScaffoldLargeBlocks, ModelScaffoldLargeBlocksVersion, ::Assets::ArtifactRequest::DataType::ReopenFunction }
    };
    
    ModelScaffold::ModelScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
    {
		assert(chunks.size() == 2);
		_rawMemoryBlock = std::move(chunks[0]._buffer);
		_largeBlocksReopen = std::move(chunks[1]._reopenFunction);
		_depVal = depVal;
    }

    ModelScaffold::ModelScaffold(ModelScaffold&& moveFrom) never_throws
    : _rawMemoryBlock(std::move(moveFrom._rawMemoryBlock))
    , _largeBlocksReopen(std::move(moveFrom._largeBlocksReopen))
	, _depVal(std::move(moveFrom._depVal))
    {}

    ModelScaffold& ModelScaffold::operator=(ModelScaffold&& moveFrom) never_throws
    {
        if (_rawMemoryBlock)
		    ImmutableData().~ModelImmutableData();
        _rawMemoryBlock = std::move(moveFrom._rawMemoryBlock);
		_largeBlocksReopen = std::move(moveFrom._largeBlocksReopen);
		_depVal = std::move(moveFrom._depVal);
        return *this;
    }

    ModelScaffold::ModelScaffold() {}

    ModelScaffold::~ModelScaffold()
    {
        if (_rawMemoryBlock)
            ImmutableData().~ModelImmutableData();
    }

//////////////////////////////////////////////////////////////////////////////////////////////////

    const ModelSupplementImmutableData&   ModelSupplementScaffold::ImmutableData() const                
    {
        return *(const ModelSupplementImmutableData*)::Assets::Block_GetFirstObject(_rawMemoryBlock.get());
    }

	std::shared_ptr<::Assets::IFileInterface>	ModelSupplementScaffold::OpenLargeBlocks() const { return _largeBlocksReopen(); }

    const ::Assets::ArtifactRequest ModelSupplementScaffold::ChunkRequests[]
    {
        ::Assets::ArtifactRequest { "Scaffold", ChunkType_ModelScaffold, 0, ::Assets::ArtifactRequest::DataType::BlockSerializer },
        ::Assets::ArtifactRequest { "LargeBlocks", ChunkType_ModelScaffoldLargeBlocks, 0, ::Assets::ArtifactRequest::DataType::ReopenFunction }
    };
    
    ModelSupplementScaffold::ModelSupplementScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		assert(chunks.size() == 2);
		_rawMemoryBlock = std::move(chunks[0]._buffer);
		_largeBlocksReopen = chunks[1]._reopenFunction;
	}

    ModelSupplementScaffold::ModelSupplementScaffold(ModelSupplementScaffold&& moveFrom) never_throws
    : _rawMemoryBlock(std::move(moveFrom._rawMemoryBlock))
    , _largeBlocksReopen(std::move(moveFrom._largeBlocksReopen))
	, _depVal(std::move(moveFrom._depVal))
    {}

    ModelSupplementScaffold& ModelSupplementScaffold::operator=(ModelSupplementScaffold&& moveFrom) never_throws
    {
        if (_rawMemoryBlock)
    		ImmutableData().~ModelSupplementImmutableData();
        _rawMemoryBlock = std::move(moveFrom._rawMemoryBlock);
		_largeBlocksReopen = std::move(moveFrom._largeBlocksReopen);
		_depVal = std::move(moveFrom._depVal);
		return *this;
    }

    ModelSupplementScaffold::ModelSupplementScaffold() {}

    ModelSupplementScaffold::~ModelSupplementScaffold()
    {
        if (_rawMemoryBlock)
            ImmutableData().~ModelSupplementImmutableData();
    }

}}
