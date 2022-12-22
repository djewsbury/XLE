// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IDevice_Forward.h"
#include "ResourceDesc.h"       // (required just for SubResourceId)
#include "../Utility/IteratorUtils.h"
#include <memory>
#include <functional>

#if OUTPUT_DLL
    #define render_dll_export       dll_export
#else
    #define render_dll_export
#endif

namespace RenderCore
{
////////////////////////////////////////////////////////////////////////////////

    enum class Format;
    enum class PipelineType;
    namespace BindFlag { typedef unsigned BitField; }
    enum class FormatCapability
    {
        NotSupported,
        Supported
    };
    class IThreadContext;

    /// <summary>Device description</summary>
    /// The build number is in a format such as:
    /// <code>\code
    ///     vX.Y.Z-[commits]-[commit marker]-[configuration]
    /// \endcode</code>
    /// Here, X, Y, Z are major, minor and patch version.
    /// <list>
    ///     <item> [commits] is the number of extra commits past the version tag in git.</item>
    ///     <item> [commit marker] is the short name of the latest commit to git.</item>
    ///     <item> [configuration] is the build configuration</item>
    /// </list>
    /// The build date format is determined by the OS and locale at compilation time.
    class DeviceDesc
    {
    public:
        const char* _underlyingAPI;
        const char* _buildVersion;
        const char* _buildDate;
    };

    ///
    /// <summary>Represents a set of back buffers for rendering to a window</summary>
    ///
    /// For most platforms we require 1 or more back buffers, and some output
    /// window to render on. This is what the presentation chain is for.
    ///
    /// Normally there is only one RenderCore::Device, but sometimes we need multiple
    /// PresentationChains (for example, if we want to render to multiple windows), in
    /// an editor.
    ///
    /// PresentationChain closely matches IDXGISwapChain behaviour in windows.
    ///
    /// Call RenderCore::IDevice::CreatePresentationChain to create a new chain.
    ///
    /// <seealso cref="RenderCore::IDevice::CreatePresentationChain"/>
    ///
    class IPresentationChain
    {
    public:
        /// <summary>Resizes or changes the presentation chain</summary>
        /// Normally this is called after the output window changes size. If the presentation
        // chain size doesn't match the output window's size, the behaviour is undefined (though
        /// on windows, the output is stretched to the window size).
        ///
        /// Use the default arguments to automatically adjust to the same size as
        /// the window.
        ///
        /// Should not be called between BeginFrame/Present
        virtual void ChangeConfiguration(
            IThreadContext& immediateThreadContext,
            const PresentationChainDesc& desc) = 0;

        /// <summary>Returns a context object that will track the size of the viewport</summary>
        virtual PresentationChainDesc GetDesc() const = 0;
        virtual std::shared_ptr<IDevice> GetDevice() const = 0;
        virtual ~IPresentationChain();
    };

	class ILowLevelCompiler;
    class SamplerDesc;
    class DescriptorSetSignature;
    class DescriptorSetInitializer;

    ///
    /// <summary>Represents a hardware device capable of rendering</summary>
    ///
    /// IDevice represents a single hardware device that can render. Usually
    /// it is the first rendering object created. Most rendering objects are
    /// associated with a single device (because the device defines the format
    /// and memory location of the object). So a device must be created and
    /// selected before anything else is created.
    ///
    /// To create a device, use the IAPIInstance interface (see DeviceInitialization.h)
    ///
    /// Normally there is only a single device. Multiple devices are only
    /// required in very special case situations (for example, if a PC has 2 graphics
    /// cards, and you want to render using both cards).
    ///
    /// Normally clients should create a device first, and then create a presentation
    /// chain once an output window has been created.
    ///
    /// You can use "QueryInterface" to get extended interfaces for the device. Some
    /// platforms might expose special case behaviour. To get access, use QueryInterface
    /// to check if the device supports the behaviour you want.
    ///
    class IDevice
    {
    public:
        /// <summary>Initialised a window for rendering</summary>
        /// To render to a window, we first need to create a presentation chain. This
        /// creates the buffers necessary to render to that window.
        /// <param name="platformWindowHandle">A platform specific value representing a window. On windows,
        /// this is would be a HWND value</param>
        /// <param name="desc">The description struct that specifies the width, height, color format and msaa
        /// sample count of the back buffer.
        /// Normally, width and height are the same size as the window client
        /// area. If a different size is used, the behaviour will depend on the specific window environment
        /// and underlying graphics api.</param>
        virtual std::unique_ptr<IPresentationChain>     CreatePresentationChain(
            const void* platformWindowHandle,
            const PresentationChainDesc& desc = {}) = 0;

        /// <summary>Looks for compatibility with another interface</summary>
        /// Some implementations of IDevice might provide extension interfaces.
        ///
        /// Note that reference counting behaviour is not the same as DirectX/COM QueryInterface.
        /// RenderCore objects don't have reference counting built it. So we can't increase
        /// the reference count on return. So don't delete or deref the returned object.
        /// As a result, be careful that another thread doesn't delete the object as you're using
        /// it.
        ///
        /// <example>
        /// Example:
        ///     <code>\code
        ///     if (auto* dx11Device = query_interface_cast<RenderCore::IDeviceDX11*>(device))) {
        ///         ...
        ///     }
        ///     \endcode</code>
        /// </example>
        ///
        /// <param name="guid">Unique identifier of the type in question, using the built-in C++ type hash code.</param>
        /// <returns>Returns nullptr if the interface isn't supported</returns>
        /// <seealso cref="RenderCore::IDeviceDX11"/>
        /// <seealso cref="RenderCore::IDeviceOpenGLES"/>
        /// <seealso cref="RenderCore::IDeviceVulkan"/>
        virtual void*       QueryInterface(size_t guid) = 0;

        virtual std::shared_ptr<IThreadContext>     GetImmediateContext() = 0;
        virtual std::unique_ptr<IThreadContext>     CreateDeferredContext() = 0;

        using ResourceInitializer = std::function<SubResourceInitData(SubResourceId)>;
        virtual IResourcePtr        CreateResource(const ResourceDesc& desc, StringSection<> name, const ResourceInitializer& init = ResourceInitializer()) = 0;
        IResourcePtr                CreateResource(const ResourceDesc& desc, StringSection<> name, const SubResourceInitData& initData);
        virtual FormatCapability    QueryFormatCapability(Format format, BindFlag::BitField bindingType) = 0;

        virtual std::shared_ptr<IDescriptorSet> CreateDescriptorSet(PipelineType pipelineType, const DescriptorSetSignature& signature, StringSection<> name) = 0;
        virtual std::shared_ptr<ISampler>       CreateSampler(const SamplerDesc& desc) = 0;

        virtual std::shared_ptr<ICompiledPipelineLayout> CreatePipelineLayout(const PipelineLayoutInitializer& desc, StringSection<> name) = 0;

        // Block until the GPU has caught up to (at least) the end of the previous frame
        virtual void                Stall() = 0;
        virtual void                PrepareForDestruction() = 0;

		virtual std::shared_ptr<ILowLevelCompiler>		CreateShaderCompiler() = 0;

        /// <summary>Returns description & version information for this device</summary>
        /// Queries build number and build date information.
        virtual DeviceDesc              GetDesc() = 0;
        virtual uint64_t                GetGUID() const = 0;
        virtual const DeviceFeatures&   GetDeviceFeatures() const = 0;
		virtual const DeviceLimits&		GetDeviceLimits() const = 0;
        virtual ~IDevice();
    };

    class ThreadContextStateDesc
    {
    public:
        VectorPattern<unsigned, 2> _viewportDimensions;
        unsigned _frameId;
    };

    namespace CommitCommandsFlags
    {
        enum Flags { WaitForCompletion = 1<<0 };
        using BitField = unsigned;
    }

    class IAnnotator;

    ///
    /// <summary>Represents the context state of a particular thread while rendering</summary>
    ///
    /// Each thread context is associated with a single CPU thread. As a result, the methods
    /// themselves are not-thread-safe -- because they are only called from a single thread.
    /// We need to store the context state on a thread level, because each thread can be working
    /// with a different state, and each thread wants to assume that other threads won't interfere
    /// with its own state.
    ///
    /// This object is critical for hiding the metal layer from platform-independent libraries.
    /// Only clients that actually want to do low level rendering operations require access to 
    /// Metal::DeviceContext to perform rendering operations.
    /// Many clients don't need that low-level visibility, and don't want to be exposed to graphics
    /// api specific code. For those cases, IThreadContext fully encapsulates any platform or
    /// graphics api specific behaviour.
    ///
    class IThreadContext
    {
    public:
		/// <summary>Begins rendering of a new frame</summary>
		/// Starts rendering of a new frame. The frame is ended with a call to RenderCore::IThreadContext::Present();
		/// You must pass a presentationChain. This defines how the frame will be presented to the user.
		/// Note that rendering to offscreen surfaces can happen outside of the BeginFrame/Present boundaries.
		/// <seealso cref="RenderCore::IThreadContext::Present"/>
		virtual IResourcePtr     BeginFrame(IPresentationChain& presentationChain) = 0;

		/// <summary>Finishes a frame, and presents it to the user</summary>
		/// Present() is used to finish a frame, and present it to the user. 
		/// 
		/// The system will often stall in Present(). This is the most likely place
		/// we need to synchronise with the hardware. So, if the CPU is running fast
		/// and the GPU can't keep up, we'll get a stall in Present().
		/// Normally, this is a good thing, because it means we're GPU bound.
		///
		/// Back buffers get flipped when we Present(). So any new rendering after Present
		/// will go to the next frame.
		///
		/// <example>
		///   Normally, present is used like this:
		///
		///     <code>\code
		///     RenderCore::IDevice* device = ...;
		///     RenderCore::IPresentationChain* presentationChain = ...;
		///     threadContext->BeginFrame(*presentationChain);
		///         ClearBackBufferAndDepthBuffer(device);   // (helps synchronisation in multi-GPU setups)
		///         DoRendering(device);
		///     threadContext->Present(*presentationChain);
		///     \endcode</code>
		///
		///   But in theory we can call Present at any time.
		/// </example>
		virtual void			Present(IPresentationChain& presentationChain) = 0;

        /// <summary>Finishes some non-presentation GPU work</summary>
        /// When you want to use the GPU for non-presentation work, like rendering to
        /// an offscreen surface, you don't want to call BeginFrame and Present, but
        /// you do still need a way to tell Metal, and the GPU, when you're done.
        ///
        /// To do this, call CommitCommands().
        ///
        /// Do not call this method if you're between a BeginFrame and Present. A
        /// presentation frame must be ended with a Present.
        ///
        /// You never need to call both Present and this method; Present already
        /// takes care of committing work and starting the next frame.
        virtual void            CommitCommands(CommitCommandsFlags::BitField=0) = 0;

        virtual IAnnotator&		GetAnnotator() = 0;

        virtual void*           QueryInterface(size_t guid) = 0;
        virtual auto			GetDevice() const -> std::shared_ptr<IDevice> = 0;

        virtual bool            IsImmediate() const = 0;
		virtual void			InvalidateCachedState() const = 0;
        virtual ThreadContextStateDesc  GetStateDesc() const = 0;

        virtual ~IThreadContext();
    };

    class IResourceView
    {
    public:
        virtual std::shared_ptr<IResource> GetResource() const = 0;
        virtual ~IResourceView();
    };
    
    class IResource
    {
    public:
		virtual ResourceDesc	        GetDesc() const = 0;
        virtual void*			        QueryInterface(size_t guid) = 0;
        virtual uint64_t                GetGUID() const = 0;
        virtual std::vector<uint8_t>    ReadBackSynchronized(IThreadContext& context, SubResourceId subRes = {}) const = 0;
        virtual std::shared_ptr<IResourceView>  CreateTextureView(BindFlag::Enum usage = BindFlag::ShaderResource, const TextureViewDesc& window = TextureViewDesc{}) = 0;
        virtual std::shared_ptr<IResourceView>  CreateBufferView(BindFlag::Enum usage = BindFlag::ConstantBuffer, unsigned rangeOffset = 0, unsigned rangeSize = 0) = 0;
        virtual ~IResource();
    };

    class ISampler
    {
    public:
        virtual SamplerDesc GetDesc() const = 0;
        virtual ~ISampler();
    };

    class ICompiledPipelineLayout
    {
    public:
        virtual uint64_t GetGUID() const = 0;
        virtual PipelineLayoutInitializer GetInitializer() const = 0;
        virtual ~ICompiledPipelineLayout();
    };

    class IDescriptorSet
	{
	public:
        virtual void Write(const DescriptorSetInitializer& newDescriptors) = 0;
		virtual ~IDescriptorSet() = default;
	};

    using Resource = IResource;     // old naming compatibility

////////////////////////////////////////////////////////////////////////////////
}
