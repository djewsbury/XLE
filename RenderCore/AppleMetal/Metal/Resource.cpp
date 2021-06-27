// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Resource.h"
#include "Format.h"
#include "TextureView.h"
#include "DeviceContext.h"
#include "../Device.h"
#include "../../IThreadContext.h"
#include "../../ResourceUtils.h"
#include "../../../OSServices/Log.h"
#include <stdexcept>
#include <typeinfo>

#include "IncludeAppleMetal.h"

namespace RenderCore { namespace Metal_AppleMetal
{
    void* Resource::QueryInterface(size_t guid)
    {
        if (guid == typeid(Resource).hash_code())
            return this;
        return nullptr;
    }

    ResourceDesc Resource::GetDesc() const
    { 
        return _desc; 
    }

    std::vector<uint8_t> Resource::ReadBackSynchronized(IThreadContext& context, SubResourceId subRes) const
    {
        auto* metalContext = (ImplAppleMetal::ThreadContext*)context.QueryInterface(typeid(ImplAppleMetal::ThreadContext).hash_code());
        if (!metalContext)
            Throw(std::runtime_error("Incorrect thread context passed to Apple Metal Resource::ReadBack implementation"));

        bool requiresDestaging = !_desc._cpuAccess;
		if (requiresDestaging && GetTexture()) {
			auto stagingCopyDesc = _desc;
			stagingCopyDesc._gpuAccess = 0;
			stagingCopyDesc._cpuAccess = CPUAccess::Read;
			stagingCopyDesc._bindFlags = BindFlag::TransferDst;
            if (_desc._type == ResourceDesc::Type::Texture) {
                stagingCopyDesc._textureDesc = CalculateMipMapDesc(_desc._textureDesc, subRes._mip);
                stagingCopyDesc._textureDesc._arrayCount = 0;
            }
			Resource destaging { GetObjectFactory(), stagingCopyDesc };

            @autoreleasepool {
                id<MTLBlitCommandEncoder> blitEncoder = [metalContext->GetCurrentCommandBuffer() blitCommandEncoder];
                [blitEncoder copyFromTexture:GetTexture()
                    sourceSlice:subRes._arrayLayer sourceLevel:subRes._mip toTexture:destaging.GetTexture()
                    destinationSlice:0 destinationLevel:0
                    sliceCount:1 levelCount:1];
                [blitEncoder endEncoding];
            }

			return destaging.ReadBackSynchronized(context, subRes);
		}

        #if PLATFORMOS_TARGET == PLATFORMOS_OSX
            // With "shared mode" textures, we can go straight to the main texture and
            // get the data directly.
            // With "managed mode", we must call synchronizeResource
            //
            if (_underlyingTexture && _underlyingTexture.get().storageMode == MTLStorageModeManaged) {
                @autoreleasepool {
                    id<MTLBlitCommandEncoder> blitEncoder = [metalContext->GetCurrentCommandBuffer() blitCommandEncoder];
                    [blitEncoder synchronizeResource:_underlyingTexture.get()];
                    [blitEncoder endEncoding];
                }
            } else if (_underlyingBuffer && _underlyingBuffer.get().storageMode == MTLStorageModeManaged) {
                @autoreleasepool {
                    id<MTLBlitCommandEncoder> blitEncoder = [metalContext->GetCurrentCommandBuffer() blitCommandEncoder];
                    [blitEncoder synchronizeResource:_underlyingBuffer.get()];
                    [blitEncoder endEncoding];
                }
            }
        #endif

        // We must synchronize with the GPU. Since the GPU is working asychronously, we must ensure
        // that all operations that might effect this resource have been completed. Since we don't
        // know exactly what operations effect the resource, we must wait for all!
        context.CommitCommands();
        metalContext->GetDevice()->Stall();

        if (_underlyingTexture) {

            #if PLATFORMOS_TARGET == PLATFORMOS_IOS
                if (_underlyingTexture.get().framebufferOnly)
                    Throw(std::runtime_error("Cannot use Resource::ReadBack on a framebuffer-only resource on IOS. You must readback through a CPU accessible copy of this texture."));
            #endif

            auto mipmapDesc = CalculateMipMapDesc(_desc._textureDesc, subRes._mip);
            auto pitches = MakeTexturePitches(mipmapDesc);
            MTLRegion region;
            if (_desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T1D) {
                region = MTLRegionMake1D(0, _desc._textureDesc._width);
            } else if (_desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T2D) {
                region = MTLRegionMake2D(
                    0, 0,
                    _desc._textureDesc._width, _desc._textureDesc._height);
            } else if (_desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T3D) {
                region = MTLRegionMake3D(
                    0, 0, 0,
                    _desc._textureDesc._width, _desc._textureDesc._height, _desc._textureDesc._depth);
            }

            std::vector<uint8_t> result(pitches._slicePitch);
            [_underlyingTexture.get() getBytes:result.data()
                                   bytesPerRow:pitches._rowPitch
                                 bytesPerImage:pitches._slicePitch
                                    fromRegion:region
                                   mipmapLevel:subRes._mip
                                         slice:subRes._arrayLayer];

            return result;

        } else if (_underlyingBuffer) {

            auto* contents = _underlyingBuffer.get().contents;
            if (!contents)
                Throw(std::runtime_error("Could not read back data from buffer object, either because it's empty or not marked for CPU read access"));

            auto length = _underlyingBuffer.get().length;
            std::vector<uint8_t> result(length);
            std::memcpy(result.data(), contents, length);

            return result;

        }

        return {};
    }

    std::shared_ptr<IResourceView> Resource::CreateTextureView(BindFlag::Enum usage, const TextureViewDesc& window)
    {
        if (!_underlyingTexture)
            Throw(std::runtime_error("Attempting to a create a texture view for a resource that is not a texture"));
        return std::make_shared<ResourceView>(GetObjectFactory(), shared_from_this(), usage, window);
    }

    std::shared_ptr<IResourceView> Resource::CreateBufferView(BindFlag::Enum usage, unsigned rangeOffset, unsigned rangeSize)
    {
        if (!_underlyingBuffer)
            Throw(std::runtime_error("Attempting to a create a buffer view for a resource that is not a buffer"));
        return std::make_shared<ResourceView>(GetObjectFactory(), shared_from_this(), rangeOffset, rangeSize);
    }

    uint64_t Resource::GetGUID() const
    {
        return _guid;
    }

    static uint64_t s_nextResourceGUID = 1;

    static std::function<SubResourceInitData(SubResourceId)> AsResInitializer(const SubResourceInitData& initData)
	{
		if (initData._data.size()) {
			return [&initData](SubResourceId sr) { return (sr._mip==0&&sr._arrayLayer==0) ? initData : SubResourceInitData{}; };
		 } else {
			 return {};
		 }
	}

    Resource::Resource(
        ObjectFactory& factory, const Desc& desc,
        const SubResourceInitData& initData)
    : Resource(factory, desc, AsResInitializer(initData))
    {}

    Resource::Resource(
        ObjectFactory& factory, const Desc& desc,
        const IDevice::ResourceInitializer& initializer)
    : _desc(desc)
    , _guid(s_nextResourceGUID++)
    {
        /* Overview: This is the base constructor for the Resource.
         * The ObjectFactory uses the MTLDevice to create the actual MTLTexture or MTLBuffer.
         */

        if (desc._type == ResourceDesc::Type::Texture) {
            MTLTextureDescriptor* textureDesc = [[MTLTextureDescriptor alloc] init];

            /* Not supporting arrays of 1D or 2D textures at this point */
            if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T1D) {
                assert(desc._textureDesc._height == 1);
                assert(desc._textureDesc._arrayCount <= 1);
                if (desc._textureDesc._arrayCount > 1) {
                    textureDesc.textureType = MTLTextureType1DArray;
                } else {
                    textureDesc.textureType = MTLTextureType1D;
                }
            } else if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T2D) {
                assert(desc._textureDesc._arrayCount <= 1);
                if (desc._textureDesc._arrayCount > 1) {
                    assert(desc._textureDesc._samples._sampleCount <= 1); // MTLTextureType2DMultisampleArray is not supported in IOS
                    textureDesc.textureType = MTLTextureType2DArray;
                } else {
                    if (desc._textureDesc._samples._sampleCount > 1) {
                        textureDesc.textureType = MTLTextureType2DMultisample;
                    } else
                        textureDesc.textureType = MTLTextureType2D;
                }
            } else if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T3D) {
                assert(desc._textureDesc._arrayCount <= 1);
                textureDesc.textureType = MTLTextureType3D;
            } else if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::CubeMap) {
                assert(desc._textureDesc._arrayCount == 6);
                textureDesc.textureType = MTLTextureTypeCube;
            }

            textureDesc.pixelFormat = AsMTLPixelFormat(desc._textureDesc._format);
            if (textureDesc.pixelFormat == MTLPixelFormatInvalid) {
                // Some formats, like three-byte formats, cannot be handled
                [textureDesc release];
                Throw(::Exceptions::BasicLabel("Cannot create texture resource because format is not supported by Apple Metal: (%s)", AsString(desc._textureDesc._format)));
            }
            assert(textureDesc.pixelFormat != MTLPixelFormatInvalid);

            textureDesc.width = desc._textureDesc._width;
            textureDesc.height = desc._textureDesc._height;
            textureDesc.depth = desc._textureDesc._depth;

            textureDesc.mipmapLevelCount = desc._textureDesc._mipCount;
            textureDesc.sampleCount = desc._textureDesc._samples._sampleCount;
            // In Metal, arrayLength is only set for arrays.  For non-arrays, arrayLength must be 1.
            // That is, the RenderCore arrayCount is not the same as the Metal arrayLength.
            if (textureDesc.textureType != MTLTextureTypeCube) {
                textureDesc.arrayLength = (desc._textureDesc._arrayCount > 1) ? desc._textureDesc._arrayCount : 1;
            } else {
                textureDesc.arrayLength = 1;
            }

            // KenD -- leaving unset for now
            // textureDesc.resourceOptions / compare to allocationRules in ResourceDesc
            // textureDesc.cpuCacheMode / Metal documentation suggests this is only worth considering changing if there are known performance issues

            /* KenD -- Metal TODO -- when populating a texture with data by using ReplaceRegion,
             * we cannot have a private storage mode.  Instead of using ReplaceRegion to populate
             * a texture that does not need CPU, prefer to populate it using a blit command encoder
             * and making the storage private.
             * This is also suggested by frame capture.
             * Currently, if CPU access is required, leaving storage mode as default.
             */
            if (desc._cpuAccess == 0 && !initializer) {
                textureDesc.storageMode = MTLStorageModePrivate;
            }

            textureDesc.usage = MTLTextureUsageUnknown;
            if (desc._bindFlags & BindFlag::ShaderResource) {
                textureDesc.usage |= MTLTextureUsageShaderRead;
            } else if (desc._bindFlags & BindFlag::UnorderedAccess) {
                if (desc._gpuAccess & GPUAccess::Read)
                    textureDesc.usage |= MTLTextureUsageShaderRead;
                if (desc._gpuAccess & GPUAccess::Write)
                    textureDesc.usage |= MTLTextureUsageShaderWrite;
            }
            if (desc._bindFlags & BindFlag::RenderTarget ||
                desc._bindFlags & BindFlag::DepthStencil) {
                textureDesc.usage |= MTLTextureUsageRenderTarget;
            }

            assert(textureDesc.width != 0);
            _underlyingTexture = factory.CreateTexture(textureDesc);
#if DEBUG
            if (desc._name[0]) {
                [_underlyingTexture.get() setLabel:[NSString stringWithCString:desc._name encoding:NSUTF8StringEncoding]];
            }
#endif
            [textureDesc release];

            unsigned faceCount = 6; // cube map
            if (desc._textureDesc._dimensionality != TextureDesc::Dimensionality::CubeMap) {
                faceCount = 1;
            }

            unsigned bytesPerTexel = BitsPerPixel(desc._textureDesc._format) / 8u;
            /* Metal does not support three-byte formats, so the texture content loader should have
             * expanded a three-byte format into a four-byte format.
             * If not, we skip out early without trying to populate the texture.
             */
            if (bytesPerTexel == 3) {
                assert(0);
                return;
            }

            /* KenD -- note that in Metal, for a cubemap, there are six slices, but arrayCount is still 1.
             * The order of the faces is pretty typical.
             Slice Index    Slice Orientation
             0              +X
             1              -X
             2              +Y
             3              -Y
             4              +Z
             5              -Z
             */

            /* The only BlockCompression type expected to be used with Metal would be PVRTC */
            auto hasPVRTCPixelFormat = GetCompressionType(desc._textureDesc._format) == FormatCompressionType::BlockCompression;

            for (unsigned f=0; f < faceCount; ++f) {
                if (initializer) {
                    for (unsigned m=0; m < desc._textureDesc._mipCount; ++m) {
                        auto mipWidth  = std::max(desc._textureDesc._width >> m, 1u);
                        auto mipHeight = std::max(desc._textureDesc._height >> m, 1u);
                        assert(desc._textureDesc._depth <= 1);              // DavidJ -- 3D textures are not supported by this Metal API, so we must ensure that this is a 2d texture
                        auto subRes = initializer({m, f});
                        auto bytesPerRow = subRes._pitches._rowPitch;
                        auto bytesPerImage = subRes._pitches._slicePitch;   // Since 3d textures are not supported, the "slice pitch" is equal to the image pitch
                        if (hasPVRTCPixelFormat) {
                            /* From Apple documentation on replaceRegion...:
                             *    This method is supported if you are copying to an entire texture with a PVRTC pixel format; in
                             *    which case, bytesPerRow and bytesPerImage must both be set to 0. This method is not
                             *    supported for copying to a subregion of a texture that has a PVRTC pixel format.
                             */
                            bytesPerRow = 0;
                            bytesPerImage = 0;
                        } else {
                            // When the input pitches are zero, it means the texture data is densely packed,
                            // and we should just derive the pitches from the dimensions
                            if (bytesPerRow == 0)
                                bytesPerRow = mipWidth * BitsPerPixel(desc._textureDesc._format) / 8u;
                            if (bytesPerImage == 0)
                                bytesPerImage = mipHeight * bytesPerRow;
                        }
                        [_underlyingTexture replaceRegion:MTLRegionMake2D(0, 0, mipWidth, mipHeight)
                                              mipmapLevel:m
                                                    slice:f
                                                withBytes:subRes._data.begin()
                                              bytesPerRow:bytesPerRow
                                            bytesPerImage:bytesPerImage];
                    }
                } else {
                    // KenD -- in the case where we don't have initialization data, leave the texture as is
                }
            }
            //Log(Verbose) << "Created texture resource and might have populated it" << std::endl;
        } else if (desc._type == ResourceDesc::Type::LinearBuffer) {
            if (desc._cpuAccess == 0 && desc._gpuAccess == GPUAccess::Read) {
                // KenD -- the case of creating read-only GPU buffers is supported (used for constant/vertex/index buffers)
                assert(initializer);
                assert(desc._bindFlags & BindFlag::ConstantBuffer || desc._bindFlags & BindFlag::VertexBuffer || desc._bindFlags & BindFlag::IndexBuffer);
                _underlyingBuffer = factory.CreateBuffer(initializer({0,0})._data.begin(), desc._linearBufferDesc._sizeInBytes);
            } else {
                // KenD -- Metal TODO -- support creating linear buffers with different access modes; also consider different binding types
                // Dynamic geo buffer has cpu access write | write dynamic; gpu access read.
                const void* bytes = nullptr;
                if (initializer) {
                    bytes = initializer({0,0})._data.begin();
                }
                _underlyingBuffer = factory.CreateBuffer(bytes, desc._linearBufferDesc._sizeInBytes);
            }
        } else {
            assert(0);
        }
    }

    Resource::Resource(const id<MTLTexture>& texture, const ResourceDesc& desc)
    : _underlyingTexture(texture)
    , _desc(desc)
    , _guid(s_nextResourceGUID++)
    {
        // KenD -- this wraps a MTL resource in an IResource, such as with the drawable for the current framebuffer

        if (![texture conformsToProtocol:@protocol(MTLTexture)]) {
            Throw(::Exceptions::BasicLabel("Creating non-texture as texture resource"));
        }
        //Log(Verbose) << "Created resource from a texture (wrapping a MTLTexture in a Resource; this is done for the current framebuffer)" << std::endl;
    }

    Resource::Resource(const id<MTLTexture>& texture, const ResourceDesc& desc, uint64_t guidOverride)
    : _underlyingTexture(texture)
    , _desc(desc)
    , _guid(guidOverride)
    {
        if (![texture conformsToProtocol:@protocol(MTLTexture)]) {
            Throw(::Exceptions::BasicLabel("Creating non-texture as texture resource"));
        }
    }

    Resource::Resource() : _guid(s_nextResourceGUID++) {}
    Resource::~Resource() {}

    uint64_t Resource::ReserveGUID()
    {
        return s_nextResourceGUID++;
    }

    namespace Internal
    {
        ResourceDesc ExtractDesc(const IResource& input)
        {
            auto* res = (Resource*)const_cast<IResource&>(input).QueryInterface(typeid(Resource).hash_code());
            if (res)
                return res->GetDesc();
            return ResourceDesc{};
        }

        ResourceDesc ExtractRenderBufferDesc(const id<MTLTexture>& texture)
        {
            return CreateDesc(BindFlag::RenderTarget, 0, GPUAccess::Write, TextureDesc::Plain2D((uint32)texture.width, (uint32)texture.height, AsRenderCoreFormat(texture.pixelFormat)), "");
        }

        inline RawMTLHandle GetBufferRawMTLHandle(const IResource& resource)
        {
            return (RawMTLHandle)static_cast<const Resource&>(resource).GetBuffer();
        }
    }

    void BlitPass::Write(
        const CopyPartial_Dest& dst,
        const RenderCore::SubResourceInitData& srcData,
        RenderCore::Format srcDataFormat,
        VectorPattern<unsigned, 3> srcDataDimensions)
    {
        assert(0);
#if 0
        auto* metalContentRes = (RenderCore::Metal_AppleMetal::Resource*)dst._resource->QueryInterface(typeid(RenderCore::Metal_AppleMetal::Resource).hash_code());
        assert(metalContentRes);

        auto srcPixelCount = srcDataDimensions[0] * srcDataDimensions[1] * srcDataDimensions[2];
        if (!srcPixelCount)
            Throw(std::runtime_error("No source pixels in WriteTexels operation. The depth of the srcDataDimensions field might need to be at least 1."));

        auto transferSrc = _devContext->GetDevice()->CreateResource(
            RenderCore::CreateDesc(
                RenderCore::BindFlag::TransferSrc,
                0, RenderCore::GPUAccess::Read,
                RenderCore::TextureDesc::Plain3D(srcDataDimensions[0], srcDataDimensions[1], srcDataDimensions[2], srcDataFormat),
                "BlitPassInstanceSrc"),
            [srcData](RenderCore::SubResourceId) { return srcData; });

        auto* metalSrcRes = (RenderCore::Metal_AppleMetal::Resource*)transferSrc->QueryInterface(typeid(RenderCore::Metal_AppleMetal::Resource).hash_code());
        assert(metalSrcRes);

        if (!_openedEncoder) {
            _devContext->CreateBlitCommandEncoder();
            _openedEncoder = true;
        }

        id<MTLBlitCommandEncoder> encoder = _devContext->GetBlitCommandEncoder();
        [encoder copyFromTexture:metalSrcRes->GetTexture()
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0,0,0)
                      sourceSize:MTLSizeMake(srcDataDimensions[0], srcDataDimensions[1], srcDataDimensions[2])
                       toTexture:metalContentRes->GetTexture()
                destinationSlice:dst._subResource._arrayLayer
                destinationLevel:dst._subResource._mip
               destinationOrigin:MTLOriginMake(dst._leftTopFront[0], dst._leftTopFront[1], dst._leftTopFront[2])];

        id<MTLCommandBuffer> commandBuffer = _devContext->RetrieveCommandBuffer();
        [commandBuffer addCompletedHandler:^(id){ (void)transferSrc; }];
#endif
    }

    void BlitPass::Copy(
        const CopyPartial_Dest& dst,
        const CopyPartial_Src& src)
    {
        assert(0);
#if 0
        auto* dstMetalRes = (RenderCore::Metal_AppleMetal::Resource*)dst._resource->QueryInterface(typeid(RenderCore::Metal_AppleMetal::Resource).hash_code());
        assert(dstMetalRes);

        auto* srcMetalRes = (RenderCore::Metal_AppleMetal::Resource*)src._resource->QueryInterface(typeid(RenderCore::Metal_AppleMetal::Resource).hash_code());
        assert(srcMetalRes);

        if (!_openedEncoder) {
            _devContext->CreateBlitCommandEncoder();
            _openedEncoder = true;
        }
        id<MTLBlitCommandEncoder> encoder = _devContext->GetBlitCommandEncoder();

        if (dstMetalRes->GetTexture() && srcMetalRes->GetTexture()) {

            // texture-to-texture copy
            [encoder copyFromTexture:srcMetalRes->GetTexture()
                         sourceSlice:src._subResource._arrayLayer
                         sourceLevel:src._subResource._mip
                        sourceOrigin:MTLOriginMake(src._leftTopFront[0], src._leftTopFront[1], src._leftTopFront[2])
                          sourceSize:MTLSizeMake(src._rightBottomBack[0]-src._leftTopFront[0], src._rightBottomBack[1]-src._leftTopFront[1], src._rightBottomBack[2]-src._leftTopFront[2])
                           toTexture:dstMetalRes->GetTexture()
                    destinationSlice:dst._subResource._arrayLayer
                    destinationLevel:dst._subResource._mip
                   destinationOrigin:MTLOriginMake(dst._leftTopFront[0], dst._leftTopFront[1], dst._leftTopFront[2])];

        } else if (dstMetalRes->GetTexture() && srcMetalRes->GetBuffer()) {

            // buffer-to-texture copy
            auto srcDesc = srcMetalRes->GetDesc();
            if (srcDesc._type != RenderCore::ResourceDesc::Type::Texture)
                Throw(std::runtime_error("Source resource does not have a texture desc in BlitPassInstance::Copy operation. Both input and output must have a texture type desc"));

            auto mipOffset = RenderCore::GetSubResourceOffset(srcDesc._textureDesc, src._subResource._mip, 0);
            auto startOffset =
                    src._leftTopFront[0] * RenderCore::BitsPerPixel(srcDesc._textureDesc._format) / 8
                +   src._leftTopFront[1] * mipOffset._pitches._rowPitch
                +   src._leftTopFront[2] * mipOffset._pitches._slicePitch
                ;

            [encoder copyFromBuffer:srcMetalRes->GetBuffer()
                       sourceOffset:mipOffset._offset + startOffset
                  sourceBytesPerRow:mipOffset._pitches._rowPitch
                sourceBytesPerImage:mipOffset._pitches._slicePitch
                         sourceSize:MTLSizeMake(src._rightBottomBack[0]-src._leftTopFront[0], src._rightBottomBack[1]-src._leftTopFront[1], src._rightBottomBack[2]-src._leftTopFront[2])
                          toTexture:dstMetalRes->GetTexture()
                   destinationSlice:dst._subResource._arrayLayer
                   destinationLevel:dst._subResource._mip
                  destinationOrigin:MTLOriginMake(dst._leftTopFront[0], dst._leftTopFront[1], dst._leftTopFront[2])];

        } else if (dstMetalRes->GetBuffer() && srcMetalRes->GetTexture()) {

            // texture-to-buffer copy
            auto dstDesc = dstMetalRes->GetDesc();
            if (dstDesc._type != RenderCore::ResourceDesc::Type::Texture)
                Throw(std::runtime_error("Source resource does not have a texture desc in BlitPassInstance::Copy operation. Both input and output must have a texture type desc"));

            auto mipOffset = RenderCore::GetSubResourceOffset(dstDesc._textureDesc, dst._subResource._mip, 0);
            auto startOffset =
                    dst._leftTopFront[0] * RenderCore::BitsPerPixel(dstDesc._textureDesc._format) / 8
                +   dst._leftTopFront[1] * mipOffset._pitches._rowPitch
                +   dst._leftTopFront[2] * mipOffset._pitches._slicePitch
                ;

            [encoder copyFromTexture:srcMetalRes->GetTexture()
                         sourceSlice:src._subResource._arrayLayer
                         sourceLevel:src._subResource._mip
                        sourceOrigin:MTLOriginMake(src._leftTopFront[0], src._leftTopFront[1], src._leftTopFront[2])
                          sourceSize:MTLSizeMake(src._rightBottomBack[0]-src._leftTopFront[0], src._rightBottomBack[1]-src._leftTopFront[1], src._rightBottomBack[2]-src._leftTopFront[2])
                            toBuffer:dstMetalRes->GetBuffer()
                   destinationOffset:mipOffset._offset + startOffset
              destinationBytesPerRow:mipOffset._pitches._rowPitch
            destinationBytesPerImage:mipOffset._pitches._slicePitch];

        } else {

            Throw(std::runtime_error("Expecting either destination or source or both to be a true texture type in BlitPassInstance::Copy operation. Both input resources where either buffers or invalid"));

        }
#endif
    }

    BlitPass::BlitPass(IThreadContext& threadContext)
    {
        _devContext = RenderCore::Metal_AppleMetal::DeviceContext::Get(threadContext).get();
        if (!_devContext)
            Throw(std::runtime_error("Unexpected thread context type passed to BltPassInstance constructor (expecting Apple Metal thread context)"));
        if (_devContext->IsInRenderPass())
            Throw(::Exceptions::BasicLabel("BlitPassInstance begun while inside of a render pass. This can only be called outside of render passes."));
        _openedEncoder = false;
    }

    BlitPass::~BlitPass()
    {
        assert(0);
        #if 0
        if (_openedEncoder) {
            _devContext->EndEncoding();
            _devContext->DestroyBlitCommandEncoder();
        }
        #endif
    }

}}
