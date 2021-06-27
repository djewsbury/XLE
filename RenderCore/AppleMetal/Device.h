// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IDeviceAppleMetal.h"
#include "../IDevice.h"
#include "../IThreadContext.h"
#include "../IAnnotator.h"
#include "Metal/FeatureSet.h"
#include "../../Utility/OCUtils.h"
#include <memory>

@class CAMetalLayer;
@protocol MTLDrawable;
@protocol MTLCommandQueue;
@protocol MTLCommandBuffer;
@protocol MTLDevice;

namespace RenderCore { namespace Metal_AppleMetal
{
    class ObjectFactory;
    class DeviceContext;
}}

namespace RenderCore { namespace ImplAppleMetal
{

////////////////////////////////////////////////////////////////////////////////

    class PresentationChain : public IPresentationChain
    {
    public:
        void                Resize(unsigned newWidth, unsigned newHeight) /*override*/;
        const std::shared_ptr<PresentationChainDesc>& GetDesc() const;

        CAMetalLayer* GetUnderlyingLayer() const { return _layer; }

        PresentationChain(id<MTLDevice> device, const void* platformValue, const PresentationChainDesc &desc);
        ~PresentationChain();

        std::vector<std::pair<id, uint64_t>> _drawableTextureGUIDMapping;

    private:
        OCPtr<CAMetalLayer> _layer;
        std::shared_ptr<PresentationChainDesc> _desc;
    };

////////////////////////////////////////////////////////////////////////////////

    class Device;

    class ThreadContext : public IThreadContext, public IThreadContextAppleMetal
    {
    public:
        IResourcePtr BeginFrame(IPresentationChain& presentationChain);
        void        Present(IPresentationChain& presentationChain) /*override*/;
        void        CommitCommands(CommitCommandsFlags::BitField=0) /*override*/;

        // METAL_TODO: These could become private, as they should only be called
        // by BeginFrame/Present/CommitCommands and startup and shutdown, but
        // probably better to just inline them and eliminate them.
    private:
        void        BeginHeadlessFrame();
        void        EndHeadlessFrame();
    public:

        void*                       QueryInterface(size_t guid);
        bool                        IsImmediate() const;
        ThreadContextStateDesc      GetStateDesc() const;
        std::shared_ptr<IDevice>    GetDevice() const;
        void                        IncrFrameId();
        void                        InvalidateCachedState() const;

        IAnnotator&                 GetAnnotator();

        const std::shared_ptr<Metal_AppleMetal::DeviceContext>&  GetDeviceContext();

        id<MTLCommandBuffer> GetCurrentCommandBuffer() { return (id<MTLCommandBuffer>)_commandBuffer.get(); }

        ThreadContext(
            id<MTLCommandQueue> immediateCommandQueue,
            const std::shared_ptr<Device>& device);
        ThreadContext(
            id<MTLCommandBuffer> commandBuffer,
            const std::shared_ptr<Device>& device);
        ~ThreadContext();

    private:
        IdPtr _immediateCommandQueue;      // (id<MTLCommandQueue>)
        std::weak_ptr<Device> _device;  // (must be weak, because Device holds a shared_ptr to the immediate context)

        IdPtr _activeFrameDrawable;        // (id<MTLDrawable>)
        // TODO: Should this be managed implicitly by the DeviceContext?
        IdPtr _commandBuffer;              // (id<MTLCommandBuffer>)

        std::shared_ptr<IAnnotator> _annotator;

        std::shared_ptr<Metal_AppleMetal::DeviceContext> _devContext;
    };

////////////////////////////////////////////////////////////////////////////////

    class Device : public IDevice, public std::enable_shared_from_this<Device>
    {
    public:
        std::unique_ptr<IPresentationChain> CreatePresentationChain(
            const void* platformValue, const PresentationChainDesc &desc) override;
        void* QueryInterface(size_t guid) override;

        std::shared_ptr<IThreadContext> GetImmediateContext() override;
        std::unique_ptr<IThreadContext> CreateDeferredContext() override;

        using ResourceInitializer = std::function<SubResourceInitData(SubResourceId)>;
        IResourcePtr CreateResource(const ResourceDesc& desc, const ResourceInitializer& init) override;
        DeviceDesc GetDesc() override;
        FormatCapability QueryFormatCapability(Format format, BindFlag::BitField bindingType) override;

        std::shared_ptr<IDescriptorSet> CreateDescriptorSet(const DescriptorSetInitializer& desc) override;
        std::shared_ptr<ISampler> CreateSampler(const SamplerDesc& desc) override;
        std::shared_ptr<ICompiledPipelineLayout> CreatePipelineLayout(const PipelineLayoutInitializer& desc) override;

        std::shared_ptr<ILowLevelCompiler> CreateShaderCompiler() override;

        void Stall() override;

        Metal_AppleMetal::FeatureSet::BitField GetFeatureSet();

        id<MTLDevice> GetUnderlying() const { return (id<MTLDevice>)_underlying.get(); }

        Device();
        explicit Device(id<MTLDevice> underlying);
        ~Device();

    protected:
        IdPtr _underlying;                 // id<MTLDevice>
        std::shared_ptr<Metal_AppleMetal::ObjectFactory> _objectFactory;
        IdPtr _immediateCommandQueue;      // id<MTLCommandQueue>
        std::shared_ptr<ThreadContext> _immediateContext;
    };

////////////////////////////////////////////////////////////////////////////////
}}

namespace RenderCore
{
    // NOTE: Make sure you call SetAppleMetalAPIValidationEnabled somewhere
    // during setup if you plan to use this flag in your code.
    //
    // There are a few cases where the most efficient way to pass a buffer
    // will fail Metal API validation. You can wrap such calls in
    //     if (!appleMetalAPIValidationEnabled) { ... }
    // This value is meaningless if Apple Metal is not being used. (Xcode
    // still sets up to enable validation, but nothing gets validated.)
    //
    //  * -1 for you forgot to call SetAppleMetalAPIValidationEnabled
    //  * 0 for disabled
    //  * 1 for enabled
    //  * 2 for extended
    extern int appleMetalAPIValidationEnabled;

    void SetAppleMetalAPIValidationEnabled();

    // NOTE: This function overrides whatever is set in your Xcode scheme
    // when running in Xcode, or your shell/user defaults/system defaults
    // otherwise. It must be called before MTLCreateSystemDefaultDevice
    // (normally called by cocos3d's CreateXLEDevice, which calls
    // RenderCore's CreateDevice), or it will have no effect.
    //
    // The values are the same as above.
    void ForceAppleMetalAPIValidation(int level);
}

