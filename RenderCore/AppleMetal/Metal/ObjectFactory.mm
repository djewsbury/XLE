//
//  ObjectFactory.mm
//  RenderCore_AppleMetal
//
//  Created by Ken Domke on 2/13/18.
//

#include "ObjectFactory.h"
#include <assert.h>

#include "IncludeAppleMetal.h"

namespace RenderCore { namespace Metal_AppleMetal
{
    OCPtr<AplMtlTexture> ObjectFactory::CreateTexture(MTLTextureDescriptor* textureDesc)
    {
        /* KenD -- this is actually creating Metal textures.
         * Other methods create Resources from MTLTextures (keeping a reference to a MTLTexture),
         * or create Resources from IResourcePtrs (keeping a reference to a MTLTexture).
         */
        assert([_mtlDevice conformsToProtocol:@protocol(MTLDevice)]);
        id<MTLDevice> device = (id<MTLDevice>)_mtlDevice;

        assert(textureDesc.width != 0);
        return moveptr([device newTextureWithDescriptor:textureDesc]);
    }

    OCPtr<AplMtlBuffer> ObjectFactory::CreateBuffer(const void* bytes, unsigned length)
    {
        /* KenD -- ideally we could use a private storage mode if the buffer is only meant to be read by the GPU...
         * Documentation for `newBufferWithBytes`:
         * "MTLBuffer objects created with this method are CPU-accessible and can be specified with a
         * MTLResourceStorageModeShared or MTLResourceStorageModeManaged storage mode, but not a
         * MTLResourceStorageModePrivate storage mode."
         */
        assert([_mtlDevice conformsToProtocol:@protocol(MTLDevice)]);
        id<MTLDevice> device = (id<MTLDevice>)_mtlDevice;

        if (bytes) {
            return moveptr([device newBufferWithBytes:bytes
                length:length
                options:MTLResourceStorageModeShared]);
        } else {
            return moveptr([device newBufferWithLength:length
                options:MTLResourceStorageModeShared]);
        }
    }

    OCPtr<AplMtlSamplerState> ObjectFactory::CreateSamplerState(MTLSamplerDescriptor* samplerDesc)
    {
        assert([_mtlDevice conformsToProtocol:@protocol(MTLDevice)]);
        id<MTLDevice> device = (id<MTLDevice>)_mtlDevice;
        if (!(_featureSet & FeatureSet::Flags::SamplerComparisonFn)) {
            assert(samplerDesc.compareFunction == MTLCompareFunctionNever);
        }

        return moveptr([device newSamplerStateWithDescriptor:samplerDesc]);
    }

    OCPtr<AplMtlDepthStencilState> ObjectFactory::CreateDepthStencilState(MTLDepthStencilDescriptor* dss)
    {
        assert([_mtlDevice conformsToProtocol:@protocol(MTLDevice)]);
        id<MTLDevice> device = (id<MTLDevice>)_mtlDevice;

        return moveptr([device newDepthStencilStateWithDescriptor:dss]);
    }

    auto ObjectFactory::CreateRenderPipelineState(MTLRenderPipelineDescriptor* desc, bool makeReflection) -> RenderPipelineState
    {
        NSError* error = NULL;
        MTLAutoreleasedRenderPipelineReflection reflection = nullptr;
        id<MTLRenderPipelineState> pipelineState = nullptr;

        if (makeReflection) {
            MTLPipelineOption options = MTLPipelineOptionArgumentInfo;
            pipelineState = [_mtlDevice.get() newRenderPipelineStateWithDescriptor:desc options:options reflection:&reflection error:&error];
        } else {
            pipelineState = [_mtlDevice.get() newRenderPipelineStateWithDescriptor:desc error:&error];
        }

        assert(reflection);

        return RenderPipelineState { moveptr(pipelineState), error, reflection };
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    static OCPtr<AplMtlTexture> CreateStandIn2DTexture(ObjectFactory& factory, bool isDepth)
    {
        OCPtr<MTLTextureDescriptor> textureDesc = moveptr([[MTLTextureDescriptor alloc] init]);
        textureDesc.get().textureType = MTLTextureType2D;
        textureDesc.get().width = 4;
        textureDesc.get().height = 4;
        textureDesc.get().usage = MTLTextureUsageShaderRead;
        
        if (isDepth) {
            textureDesc.get().pixelFormat = MTLPixelFormatDepth32Float;
#if PLATFORMOS_TARGET == PLATFORMOS_OSX
            /* METAL_TODO: depth textures on macOS must have a private storage mode; to populate that texture, we would need to blit to it. */
            textureDesc.get().storageMode = MTLStorageModePrivate;
#endif
        } else {
            textureDesc.get().pixelFormat = MTLPixelFormatRGBA8Unorm;
        }

        unsigned data[4*4];
        memset(data, 0xff, sizeof(data));

        auto tex = factory.CreateTexture(textureDesc);
        if (textureDesc.get().storageMode == MTLStorageModeShared) {
            [tex.get() replaceRegion:MTLRegionMake2D(0, 0, 4, 4)
                         mipmapLevel:0
                               slice:0
                           withBytes:data
                         bytesPerRow:4*4
                       bytesPerImage:4*4*4];
        }
        return tex;
    }

    static OCPtr<AplMtlTexture> CreateStandInCubeTexture(ObjectFactory& factory)
    {
        OCPtr<MTLTextureDescriptor> textureDesc = moveptr([[MTLTextureDescriptor alloc] init]);
        textureDesc.get().textureType = MTLTextureTypeCube;
        textureDesc.get().pixelFormat = MTLPixelFormatRGBA8Unorm;
        textureDesc.get().width = 4;
        textureDesc.get().height = 4;
        textureDesc.get().usage = MTLTextureUsageShaderRead;

        unsigned data[4*4];
        memset(data, 0xff, sizeof(data));

        auto tex = factory.CreateTexture(textureDesc);
        for (unsigned f=0; f<6; ++f) {
            [tex.get() replaceRegion:MTLRegionMake2D(0, 0, 4, 4)
                         mipmapLevel:0
                               slice:f
                           withBytes:data
                         bytesPerRow:4*4
                       bytesPerImage:4*4*4];
        }

        return tex;
    }

    static OCPtr<AplMtlSamplerState> CreateStandInSamplerState(ObjectFactory& factory)
    {
        OCPtr<MTLSamplerDescriptor> desc = moveptr([[MTLSamplerDescriptor alloc] init]);
        desc.get().rAddressMode = MTLSamplerAddressModeRepeat;
        desc.get().sAddressMode = MTLSamplerAddressModeRepeat;
        desc.get().tAddressMode = MTLSamplerAddressModeRepeat;
        desc.get().minFilter = MTLSamplerMinMagFilterLinear;
        desc.get().magFilter = MTLSamplerMinMagFilterLinear;
        desc.get().mipFilter = MTLSamplerMipFilterLinear;
        desc.get().compareFunction = MTLCompareFunctionNever;

        auto samplerState = factory.CreateSamplerState(desc);
        return samplerState;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    static ObjectFactory* s_objectFactory_instance = nullptr;

    ObjectFactory::ObjectFactory(id<MTLDevice> mtlDevice)
    : _mtlDevice(mtlDevice)
    {
        assert(s_objectFactory_instance == nullptr);
        s_objectFactory_instance = this;

        _standIn2DTexture = CreateStandIn2DTexture(*this, false);
        _standIn2DDepthTexture = CreateStandIn2DTexture(*this, true);
        _standInCubeTexture = CreateStandInCubeTexture(*this);
        _standInSamplerState = CreateStandInSamplerState(*this);

        _featureSet = 0;

#if TARGET_OS_IPHONE
        _featureSet |= [mtlDevice supportsFeatureSet:MTLFeatureSet_iOS_GPUFamily3_v1] ? FeatureSet::Flags::SamplerComparisonFn : 0;
#elif TARGET_OS_OSX
        _featureSet |= [mtlDevice supportsFeatureSet:MTLFeatureSet_OSX_GPUFamily1_v1] ? FeatureSet::Flags::SamplerComparisonFn : 0;
#endif
    }
    ObjectFactory::~ObjectFactory()
    {
        _standIn2DTexture = OCPtr<AplMtlTexture>();
        _standIn2DDepthTexture = OCPtr<AplMtlTexture>();
        _standInCubeTexture = OCPtr<AplMtlTexture>();
        _standInSamplerState = OCPtr<AplMtlSamplerState>();

        {
            ScopedLock(_compiledShadersLock);
            decltype(_compiledShaders)().swap(_compiledShaders);
        }

        assert(s_objectFactory_instance == this);
        s_objectFactory_instance = nullptr;
    }
    
    AplMtlTexture* ObjectFactory::GetStandInTexture(unsigned typeInt, bool isDepth) {
        MTLTextureType type = (MTLTextureType)typeInt;
        assert(type == MTLTextureType2D || (type == MTLTextureTypeCube && !isDepth));
        if (type == MTLTextureTypeCube) {
            return _standInCubeTexture;
        } else {
            return isDepth ? _standIn2DDepthTexture : _standIn2DTexture;
        }
    }

    ObjectFactory& GetObjectFactory(IDevice& device) { assert(s_objectFactory_instance); return *s_objectFactory_instance; }
    ObjectFactory& GetObjectFactory(DeviceContext&) { assert(s_objectFactory_instance); return *s_objectFactory_instance; }
    ObjectFactory& GetObjectFactory() { assert(s_objectFactory_instance); return *s_objectFactory_instance; }

    void CheckGLError(const char context[]) {}
}}
