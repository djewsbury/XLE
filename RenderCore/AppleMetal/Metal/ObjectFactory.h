//
//  ObjectFactory.h
//  RenderCore_AppleMetal
//
//  Created by Ken Domke on 2/13/18.
//

#pragma once

#include "FeatureSet.h"
#include "../../../Utility/OCUtils.h"
#include "../../../Utility/Threading/Mutex.h"
#include <unordered_map>
#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSObject.h>

@protocol MTLDevice;
@protocol MTLTexture;
@protocol MTLBuffer;
@protocol MTLSamplerState;
@protocol MTLRenderPipelineState;
@protocol MTLDepthStencilState;
@class MTLTextureDescriptor;
@class MTLSamplerDescriptor;
@class MTLRenderPipelineDescriptor;
@class MTLRenderPipelineReflection;
@class MTLDepthStencilDescriptor;

/* KenD -- could switch all of these typedefs from NSObject with a protocol to simply `id`
 * However, cannot have `OCPtr<id<MTLTexture>>` because OCPtr will not work; ObjType will be `id<MTLTexture> *`, which is
 * off because `id` is already a pointer.  `OCPtr<NSObject<MTLTexture>>` is fine because ObjType will be `NSObject<MTLTexture> *`, which is fine.
 * `IdPtr` is also fine.
 */
using AplMtlTexture = NSObject<MTLTexture>;
using AplMtlBuffer = NSObject<MTLBuffer>;
using AplMtlSamplerState = NSObject<MTLSamplerState>;
using AplMtlDepthStencilState = NSObject<MTLDepthStencilState>;
using AplMtlDevice = NSObject<MTLDevice>;
using AplMtlRenderPipelineState = NSObject<MTLRenderPipelineState>;

namespace RenderCore { class IDevice; }

namespace RenderCore { namespace Metal_AppleMetal
{
    /* KenD -- void* instead for RawMTLHandle? */
    using RawMTLHandle = uint64_t;
    static const RawMTLHandle RawMTLHandle_Invalid = 0;

    class ObjectFactory
    {
    public:
        OCPtr<AplMtlTexture> CreateTexture(MTLTextureDescriptor* textureDesc); // <MTLTexture>
        OCPtr<AplMtlBuffer> CreateBuffer(const void* bytes, unsigned length); // <MTLBuffer>
        OCPtr<AplMtlSamplerState> CreateSamplerState(MTLSamplerDescriptor* samplerDesc); // <MTLSamplerState>
        OCPtr<AplMtlDepthStencilState> CreateDepthStencilState(MTLDepthStencilDescriptor* dss); // <MTLDepthStencilState>

        struct RenderPipelineState
        {
            OCPtr<AplMtlRenderPipelineState> _renderPipelineState;
            OCPtr<NSError> _error;
            OCPtr<MTLRenderPipelineReflection> _reflection;
        };
        RenderPipelineState CreateRenderPipelineState(
            MTLRenderPipelineDescriptor* desc,
            bool makeReflection = false);

        AplMtlTexture* StandIn2DTexture()     { return _standIn2DTexture; }
        AplMtlTexture* StandIn2DDepthTexture() { return _standIn2DDepthTexture; }
        AplMtlTexture* StandInCubeTexture()   { return _standInCubeTexture; }
        AplMtlSamplerState* StandInSamplerState() { return _standInSamplerState; }
        AplMtlTexture* GetStandInTexture(unsigned type, bool isDepth);

        Threading::Mutex _compiledShadersLock;
        std::unordered_map<uint64_t, IdPtr> _compiledShaders;

        FeatureSet::BitField GetFeatureSet() const { return _featureSet; }

        ObjectFactory(id<MTLDevice> mtlDevice);
        ObjectFactory() = delete;
        ~ObjectFactory();

        ObjectFactory& operator=(const ObjectFactory&) = delete;
        ObjectFactory(const ObjectFactory&) = delete;
    private:
        OCPtr<AplMtlDevice> _mtlDevice; // <MTLDevice>
        FeatureSet::BitField _featureSet;

        OCPtr<AplMtlTexture> _standIn2DTexture;
        OCPtr<AplMtlTexture> _standIn2DDepthTexture;
        OCPtr<AplMtlTexture> _standInCubeTexture;
        OCPtr<AplMtlSamplerState> _standInSamplerState;
    };

    class DeviceContext;

    ObjectFactory& GetObjectFactory(IDevice& device);
    ObjectFactory& GetObjectFactory(DeviceContext&);
    ObjectFactory& GetObjectFactory();

    void CheckGLError(const char context[]);
}}
