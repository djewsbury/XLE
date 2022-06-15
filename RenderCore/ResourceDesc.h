// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Format.h"
#include "../Core/Prefix.h"
#include "../Utility/StringUtils.h"
#include "../Utility/IteratorUtils.h"   // for VectorPattern
#include <iosfwd>

namespace RenderCore
{
    /// Container for BindFlag::Enum
    namespace BindFlag
    {
        /// <summary>Determines how the buffer will be bound to the pipeline</summary>
        /// Most buffers are just blocks of data on the GPU. They can be bound to the
        /// pipeline in multiple ways, for different purposes. 
        /// 
        /// This flag controls how the buffer can be used. Most buffer only have a single
        /// bind flag. But sometimes we need to combine input and output binding modes
        /// eg: 
        ///     <list>
        ///         <item> BindFlag::RenderTarget | BindFlag::ShaderResource
        ///         <item> BindFlag::DepthStencil | BindFlag::ShaderResource
        ///         <item> BindFlag::UnorderedAccess | BindFlag::VertexBuffer
        ///     </list>
        enum Enum
        {
            VertexBuffer        = 1<<0,     ///< Used as an vertex buffer (ie, IASetVertexBuffers)
            IndexBuffer         = 1<<1,     ///< Used as an index buffer (ie, IASetIndexBuffer)
            ShaderResource      = 1<<2,     ///< Used as a shader resource (ie, PSSetShaderResources)
            RenderTarget        = 1<<3,     ///< Used as a render target (ie, OMSetRenderTargets)
            DepthStencil        = 1<<4,     ///< Used as a depth buffer (ie, OMSetRenderTargets)
            UnorderedAccess     = 1<<5,     ///< Used as a unordered access texture or structured buffer (ie, CSSetUnorderedAccessViews)
            ConstantBuffer      = 1<<7,     ///< Used as a constant buffer (ie, VSSetConstantBuffers)
            StreamOutput        = 1<<8,     ///< Used as a stream-output buffer from the geomtry shader (ie, SOSetTargets)
            DrawIndirectArgs    = 1<<9,     ///< Used with DrawInstancedIndirect or DrawIndexedInstancedIndirect
            RawViews            = 1<<10,    ///< Enables use of raw shader resource views
            InputAttachment     = 1<<11,    ///< Used as an input attachment for a render pass (usually appears in combination with ShaderResource as well as some other output oriented flags)
            TransferSrc         = 1<<12,    ///< Primarily used as the source resource in a copy operation (typically for staging texture)
            TransferDst         = 1<<13,    ///< Primarily used as the destination resource in a copy operation (typically for readback textures)
            PresentationSrc     = 1<<14,    ///< Part of a swap chain that can be presented to the screen
            TexelBuffer         = 1<<15,    ///< Combine with UnorderedAccess to get an UnorderedAccessTexelBuffer or ShaderResource to get a UniformTexelBuffer
        };
        typedef unsigned BitField;
    }

    std::string BindFlagsAsString(BindFlag::BitField bindFlags);
    const char* AsString(BindFlag::Enum);

    /// Container for AllocationRules::Enum
    namespace AllocationRules
    {
        /// <summary>Determines how to to allocate the resource, and rules for host access</summary>
        /// Use these flags to identicate how the host (ie, CPU-side) will use the resource.
        ///
        /// Most resources should be GPU-only, in which case there will be no host flags. However, for staging
        /// buffers, dynamic resources, and other similar resources, we need to place them into memory
        /// that is visible to the CPU. As a result, we need to specify how we're going to use the source at
        /// allocation time.
        ///
        /// Different graphics APIs have different names and flags for these rules. But at the end of the
        /// day, there are a few main usage patterns. The flags here are selected to try to match those patterns.
        enum Enum
        {
            /// Host will not read, and typically writes in a sequential pattern. Use for staging resources and most dynamic resources.
            /// ResourceMap{} is enabled, but use Mode::WriteDiscardPrevious
            HostVisibleSequentialWrite       = 1<<0,
            /// Both reading and writing are enabled. Use for readback buffers (ie, blit from a GPU resource to a HostAccessRandomAccess 
            /// to read back data from a resource)
            /// ResourceMap{} is enabled in any mode
            HostVisibleRandomAccess          = 1<<1,
            /// All the system to return a non-mappable buffer, even if a HostAccess... flag is set.
            /// Use this if you want a buffer that is both host visible & GPU local, but are prepared to handle cases where this isn't
            /// possible (ie, if it's not supported on the particular machine, or if such memory is all used up).
            /// The ResourceMap{} may or may not be enabled -- caller must handle either case
            FallbackNonHostVisible          = 1<<2,
            /// Map the resource into CPU visible memory at allocation time, and keep it mapped until destruction. This is useful
            /// for reuable staging buffers, and avoid thrashing the CPU heap by continually mapping and unmapping resources.
            PermanentlyMapped               = 1<<3,
            /// Set to disable automatic cache invalidation & flushing before and after ResourceMap{} operations. In Vulkan, by
            /// default we set the VK_MEMORY_PROPERTY_HOST_COHERENT_BIT flag. This ensures the CPU cache and the GPU cache are kept
            /// up to date implicitly. Use DisableAutoCacheCoherency to disable the Vulkan flag -- in which case, the caller must
            /// explicity flush and invalidate the cache as needed. This can be useful (particular with PermanentlyMapped buffers)
            /// when the caller wants to affect the caches for only a part of the resource.
            DisableAutoCacheCoherency       = 1<<4,
            /// Set as a hint to the allocator that this is a large resizable render target (which can be a source of fragmentation)
            ResizeableRenderTarget          = 1<<5
        };
        typedef unsigned BitField;
    }
        
        /////////////////////////////////////////////////

    class LinearBufferDesc
    {
	public:
        unsigned _sizeInBytes;
        unsigned _structureByteSize;

        static LinearBufferDesc Create(unsigned sizeInBytes, unsigned structureByteSize=0);
        uint64_t CalculateHash() const;
    };

    class TextureSamples
    {
	public:
        uint8_t _sampleCount;
        uint8_t _samplingQuality;
        static TextureSamples Create(uint8_t sampleCount=1, uint8_t samplingQuality=0)
        {
            TextureSamples result;
            result._sampleCount = sampleCount;
            result._samplingQuality = samplingQuality;
            return result;
        }
        friend bool operator==(const TextureSamples& lhs, const TextureSamples& rhs) { return lhs._sampleCount == rhs._sampleCount && lhs._samplingQuality == rhs._samplingQuality; }
    };

    class TextureDesc
    {
	public:
        uint32_t _width, _height, _depth;
        Format _format;
        enum class Dimensionality { Undefined, T1D, T2D, T3D, CubeMap };
        Dimensionality _dimensionality;
        uint8_t _mipCount;
        uint16_t _arrayCount;
        TextureSamples _samples;

        static TextureDesc Plain1D(
            uint32_t width, Format format, 
            uint8_t mipCount=1, uint16_t arrayCount=0);
        static TextureDesc Plain2D(
            uint32_t width, uint32_t height, Format format, 
            uint8_t mipCount=1, uint16_t arrayCount=0, const TextureSamples& samples = TextureSamples::Create());
        static TextureDesc Plain3D(
            uint32_t width, uint32_t height, uint32_t depth, Format format, uint8_t mipCount=1);
        static TextureDesc PlainCube(
            uint32_t width, uint32_t height, Format format, uint8_t mipCount=1, uint16_t arrayCount=0);
        static TextureDesc Empty();

        uint64_t CalculateHash() const;
    };

    /// <summary>Description of a buffer</summary>
    /// Description of a buffer, used during creation operations.
    /// Usually, BufferDesc is filled with a description of a new buffer to create,
    /// and passed to IManager::Transaction_Begin.
    class ResourceDesc
    {
    public:
            // following the D3D11 style; let's use a "type" member, with a union
        enum class Type { LinearBuffer, Texture, Unknown, Max };
        Type _type;
        BindFlag::BitField _bindFlags;
        AllocationRules::BitField _allocationRules;
        union {
            LinearBufferDesc _linearBufferDesc;
            TextureDesc _textureDesc;
        };
        char _name[48];

        uint64_t CalculateHash() const;

		ResourceDesc();
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    class TextureViewDesc
    {
    public:
        struct SubResourceRange { unsigned _min; unsigned _count; };
        static const unsigned Unlimited = ~0x0u;
        static const SubResourceRange All;

		struct Flags
        {
			enum Bits 
            { 
                AttachedCounter = 1<<0, AppendBuffer = 1<<1, 
                ForceArray = 1<<2, ForceSingleSample = 1<<3, 
                JustDepth = 1<<4, JustStencil = 1<<5 
            };
			using BitField = unsigned;
		};

        enum Aspect { UndefinedAspect, ColorLinear, ColorSRGB, DepthStencil, Depth, Stencil };
        
        struct FormatFilter
        {
            Aspect      _aspect;
            Format      _explicitFormat;

            FormatFilter(Aspect aspect = UndefinedAspect)
                : _aspect(aspect), _explicitFormat(Format(0)) {}
            FormatFilter(Format explicitFormat) : _aspect(UndefinedAspect), _explicitFormat(explicitFormat) {}
        };

        FormatFilter                _format = FormatFilter {};
        SubResourceRange            _mipRange = All;
        SubResourceRange            _arrayLayerRange = All;
        TextureDesc::Dimensionality _dimensionality = TextureDesc::Dimensionality::Undefined;
		Flags::BitField				_flags = 0;

        uint64_t GetHash() const;
    };

    Format ResolveFormat(Format baseFormat, TextureViewDesc::FormatFilter filter, BindFlag::Enum usage);

///////////////////////////////////////////////////////////////////////////////////////////////////

    class SubResourceId 
    { 
    public:
        unsigned _mip = 0;
        unsigned _arrayLayer = 0;
        
        friend std::ostream& operator<<(std::ostream& str, SubResourceId subr);
        friend bool operator==(SubResourceId lhs, SubResourceId rhs) { return lhs._mip == rhs._mip && lhs._arrayLayer == rhs._arrayLayer; }
    };

    class PresentationChainDesc
    {
    public:
        unsigned _width = 0u, _height = 0u;
        Format _format = Format(0);
        TextureSamples _samples = TextureSamples::Create();
        BindFlag::BitField _bindFlags = BindFlag::RenderTarget;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    inline ResourceDesc CreateDesc(
        BindFlag::BitField bindFlags,
        AllocationRules::BitField allocationRules,
        const TextureDesc& textureDesc,
        StringSection<char> name)
    {
		ResourceDesc desc;
        desc._type = ResourceDesc::Type::Texture;
        desc._bindFlags = bindFlags;
        desc._allocationRules = allocationRules;
        desc._textureDesc = textureDesc;
        XlCopyString(desc._name, dimof(desc._name), name);
        return desc;
    }

    inline ResourceDesc CreateDesc(
        BindFlag::BitField bindFlags,
        AllocationRules::BitField allocationRules,
        const LinearBufferDesc& linearBufferDesc,
        StringSection<char> name)
    {
		ResourceDesc desc;
        desc._type = ResourceDesc::Type::LinearBuffer;
        desc._bindFlags = bindFlags;
        desc._allocationRules = allocationRules;
        desc._linearBufferDesc = linearBufferDesc;
        XlCopyString(desc._name, dimof(desc._name), name);
        return desc;
    }

    inline ResourceDesc CreateDesc(
        BindFlag::BitField bindFlags,
        const TextureDesc& textureDesc,
        StringSection<char> name)
    {
		ResourceDesc desc;
        desc._type = ResourceDesc::Type::Texture;
        desc._bindFlags = bindFlags;
        desc._allocationRules = 0;
        desc._textureDesc = textureDesc;
        XlCopyString(desc._name, dimof(desc._name), name);
        return desc;
    }

    inline ResourceDesc CreateDesc(
        BindFlag::BitField bindFlags,
        const LinearBufferDesc& linearBufferDesc,
        StringSection<char> name)
    {
		ResourceDesc desc;
        desc._type = ResourceDesc::Type::LinearBuffer;
        desc._bindFlags = bindFlags;
        desc._allocationRules = 0;
        desc._linearBufferDesc = linearBufferDesc;
        XlCopyString(desc._name, dimof(desc._name), name);
        return desc;
    }

	inline TextureDesc TextureDesc::Plain1D(
		uint32_t width, Format format,
		uint8_t mipCount, uint16_t arrayCount)
	{
		TextureDesc result;
		result._width = width;
		result._height = 1;
		result._depth = 1;
		result._format = format;
		result._dimensionality = Dimensionality::T1D;
		result._mipCount = mipCount;
		result._arrayCount = arrayCount;
		result._samples = TextureSamples::Create();
		return result;
	}

	inline TextureDesc TextureDesc::Plain2D(
		uint32_t width, uint32_t height, Format format,
		uint8_t mipCount, uint16_t arrayCount,
		const TextureSamples& samples)
	{
		TextureDesc result;
		result._width = width;
		result._height = height;
		result._depth = 1;
		result._format = format;
		result._dimensionality = Dimensionality::T2D;
		result._mipCount = mipCount;
		result._arrayCount = arrayCount;
		result._samples = samples;
		return result;
	}

	inline TextureDesc TextureDesc::Plain3D(
		uint32_t width, uint32_t height, uint32_t depth,
		Format format, uint8_t mipCount)
	{
		TextureDesc result;
		result._width = width;
		result._height = height;
		result._depth = depth;
		result._format = format;
		result._dimensionality = Dimensionality::T3D;
		result._mipCount = mipCount;
		result._arrayCount = 0;
		result._samples = TextureSamples::Create();
		return result;
	}

    inline TextureDesc TextureDesc::PlainCube(
        uint32_t width, uint32_t height, Format format, uint8_t mipCount,
        uint16_t arrayCount)
    {
        TextureDesc result;
		result._width = width;
		result._height = height;
		result._depth = 1;
		result._format = format;
		result._dimensionality = Dimensionality::CubeMap;
		result._mipCount = mipCount;
		result._arrayCount = arrayCount;
		result._samples = TextureSamples::Create();
		return result;
    }

	inline TextureDesc TextureDesc::Empty()
	{
		TextureDesc result;
		result._width = 0;
		result._height = 0;
		result._depth = 0;
		result._format = (Format)0;
		result._dimensionality = Dimensionality::T1D;
		result._mipCount = 0;
		result._arrayCount = 0;
		result._samples = TextureSamples::Create();
		return result;
	}

	inline LinearBufferDesc LinearBufferDesc::Create(unsigned sizeInBytes, unsigned structureByteSize)
	{
		return LinearBufferDesc{ sizeInBytes, structureByteSize };
	}

    /// <summary>Distance (in bytes) between adjacent rows, depth slices or array layers in a texture</summary>
    /// Note that for compressed textures, the "row pitch" is always the distance between adjacent rows of
    /// compressed blocks. Most compression formats use blocks of 4x4 pixels. So the row pitch is actually
    /// the distance between one row of 4x4 blocks and the next row of 4x4 blocks.
    /// Another way to think of this is to imagine that each 4x4 block is 1 pixel in a texture that is 1/16th
    /// of the size. This may make the pitch values more clear.
    class TexturePitches
    {
    public:
        unsigned _rowPitch = 0u;
        unsigned _slicePitch = 0u;
        unsigned _arrayPitch = 0u;
    };

	class SubResourceInitData
	{
	public:
		IteratorRange<const void*>  _data;
		TexturePitches              _pitches = {};

		SubResourceInitData() {}
		SubResourceInitData(IteratorRange<const void*> data) : _data(data) {}
		SubResourceInitData(IteratorRange<const void*> data, TexturePitches pitches) : _data(data), _pitches(pitches) {}
	};

    unsigned ActualArrayLayerCount(const TextureDesc& desc);
}
