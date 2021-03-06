// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ResourceDesc.h"       // actually only needed for TexturePitches
#include "Types_Forward.h"
#include "../Utility/MemoryUtils.h"
#include <memory>
#include <iostream>

namespace RenderCore
{
    class ResourceDesc;
    class TextureDesc;
    class TexturePitches;
    class SubResourceInitData;
    class Box2D;
	class IResource;
    class IResourceView;

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      C O P Y I N G       //
///////////////////////////////////////////////////////////////////////////////////////////////////

    unsigned CopyMipLevel(
        void* destination, size_t destinationDataSize, TexturePitches dstPitches,
        const TextureDesc& dstDesc,
        const SubResourceInitData& srcData);

    unsigned CopyMipLevel(
        void* destination, size_t destinationDataSize, TexturePitches dstPitches,
        const TextureDesc& dstDesc,
        const Box2D& dst2D,
        const SubResourceInitData& srcData);

    TextureDesc CalculateMipMapDesc(const TextureDesc& topMostMipDesc, unsigned mipMapIndex);

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      R E S O U R C E   S I Z E S       //
///////////////////////////////////////////////////////////////////////////////////////////////////

    unsigned ByteCount(
        unsigned nWidth, unsigned nHeight, unsigned nDepth, 
        unsigned mipCount, Format format);
    unsigned ByteCount(const TextureDesc& tDesc);
    unsigned ByteCount(const ResourceDesc& desc);

    class SubResourceOffset { public: size_t _offset; size_t _size; TexturePitches _pitches; };
    SubResourceOffset GetSubResourceOffset(
        const TextureDesc& tDesc, unsigned mipIndex, unsigned arrayLayer);

    TexturePitches MakeTexturePitches(const TextureDesc& desc);

    unsigned CalculatePrimitiveCount(Topology topology, unsigned vertexCount, unsigned drawCallCount = 1);

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      V I E W    P O O L       //
///////////////////////////////////////////////////////////////////////////////////////////////////

	class ViewPool
	{
	public:
		IResourceView* GetTextureView(const std::shared_ptr<IResource>& resource, BindFlag::Enum usage, const TextureViewDesc& viewDesc);
		void Erase(IResource& res);
        void Reset();

        struct Metrics
        {
            unsigned _viewCount;
        };
        Metrics GetMetrics() const;

	private:
		struct Entry { std::shared_ptr<IResource> _resource; std::shared_ptr<IResourceView> _view; };
		std::vector<std::pair<uint64, Entry>> _views;
	};

    std::ostream& SerializationOperator(std::ostream& strm, const ResourceDesc&);
}
