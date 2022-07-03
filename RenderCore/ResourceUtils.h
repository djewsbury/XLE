// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ResourceDesc.h"       // actually only needed for TexturePitches
#include "Format.h"
#include "StateDesc.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/Threading/Mutex.h"
#include <memory>
#include <iostream>

namespace RenderCore
{
    class ResourceDesc;
    class TextureDesc;
    class TexturePitches;
    class SubResourceInitData;
	class IResource;
    class IResourceView;

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      C O P Y I N G       //
///////////////////////////////////////////////////////////////////////////////////////////////////

    class CopyPartial_Dest
    {
    public:
        IResource*          _resource;
        SubResourceId       _subResource;
        VectorPattern<unsigned, 3>      _leftTopFront;
        bool                _leftTopFrontIsLinearBufferOffset = false;

        CopyPartial_Dest(
            IResource& destination,
            SubResourceId subRes = {},
            VectorPattern<unsigned, 3> leftTopFront = {0,0,0})
        : _resource(&destination), _subResource(subRes), _leftTopFront(leftTopFront) {}

        CopyPartial_Dest(
            IResource& destination,
            unsigned bufferStart)
        : _resource(&destination), _subResource(), _leftTopFront(bufferStart, 0, 0), _leftTopFrontIsLinearBufferOffset(true) {}
    };

    class CopyPartial_Src
    {
    public:
        IResource*                      _resource;
        std::pair<unsigned, unsigned>   _linearBufferRange = { 0, 0 };

        // Subresource range
        SubResourceId                   _subResource;
        unsigned                        _mipLevelCount = 1;
        unsigned                        _arrayLayerCount = 1;

        // partial Subresource area
        VectorPattern<unsigned, 3>      _leftTopFront;
        VectorPattern<unsigned, 3>      _rightBottomBack;
        TexturePitches                  _partialSubresourcePitches;     // only used we're transferring a partial subresource

        enum Flags { EnableSubresourceRange = 1<<0, EnablePartialSubresourceArea = 1<<1, EnableLinearBufferRange = 1<<2 };
        unsigned _flags = 0;

        CopyPartial_Src(IResource& source) : _resource(&source), _flags(0) {}
        CopyPartial_Src(IResource& source, unsigned bufferStart, unsigned bufferEnd = ~0u)
        : _resource(&source), _linearBufferRange(bufferStart, bufferEnd), _flags(Flags::EnableLinearBufferRange) {}

        CopyPartial_Src& SubresourceRange(SubResourceId firstSubresource, unsigned mipLevelCount, unsigned arrayLayerCount)
        {
            _subResource = firstSubresource;
            _mipLevelCount = mipLevelCount;
            _arrayLayerCount = arrayLayerCount;
            _flags |= Flags::EnableSubresourceRange;
            return *this;
        }

        CopyPartial_Src& SingleSubresource(SubResourceId subresource)
        {
            _subResource = subresource;
            _mipLevelCount = 1;
            _arrayLayerCount = 1;
            _flags |= Flags::EnableSubresourceRange;
            return *this;
        }

        CopyPartial_Src& PartialSubresource(
            VectorPattern<unsigned, 3> leftTopFront,
            VectorPattern<unsigned, 3> rightBottomBack,
            TexturePitches pitches)
        {
            _leftTopFront = leftTopFront;
            _rightBottomBack = rightBottomBack;
            _partialSubresourcePitches = pitches;
            _flags |= Flags::EnablePartialSubresourceArea;
            return *this;
        }
    };

    class Box2D
    {
    public:
        signed _left = 0, _top = 0;
		signed _right = 0, _bottom = 0;

        friend bool operator==(const Box2D& lhs, const Box2D& rhs);
    };

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
		const std::shared_ptr<IResourceView>& GetTextureView(const std::shared_ptr<IResource>& resource, BindFlag::Enum usage, const TextureViewDesc& viewDesc);
		void Erase(IResource& res);
        void Reset();

        struct Metrics
        {
            unsigned _viewCount;
        };
        Metrics GetMetrics() const;

	private:
		struct Entry { std::shared_ptr<IResource> _resource; std::shared_ptr<IResourceView> _view; };
		std::vector<std::pair<uint64_t, Entry>> _views;
	};

    class ISampler;
    class IDevice;

    class SamplerPool
    {
    public:
		std::shared_ptr<ISampler> GetSampler(const SamplerDesc&);
        SamplerPool(IDevice& device);
    private:
        Threading::Mutex _lock;
        std::vector<std::pair<uint64_t, std::shared_ptr<ISampler>>> _samplers;
        IDevice* _device = nullptr;
    };

    std::ostream& SerializationOperator(std::ostream& strm, const ResourceDesc&);
}
