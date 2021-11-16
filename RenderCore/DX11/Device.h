// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../IDevice.h"
#include "IDeviceDX11.h"
#include "Metal/DX11.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../Utility/IntrusivePtr.h"

namespace RenderCore { namespace Metal_DX11 { class DeviceContext; class ObjectFactory; } }

namespace RenderCore { namespace ImplDX11
{
////////////////////////////////////////////////////////////////////////////////

    class Device;

    class PresentationChain : public IPresentationChain
    {
    public:
        void                Resize(unsigned newWidth, unsigned newHeight) override;
        IDXGI::SwapChain*   GetUnderlying() const { return _underlying.get(); }

        void                AttachToContext(Metal_DX11::DeviceContext& context, Metal_DX11::ObjectFactory& factory);

        const std::shared_ptr<PresentationChainDesc>& GetDesc() const override;
		IResourcePtr GetPresentationResource();

        PresentationChain(intrusive_ptr<IDXGI::SwapChain> underlying, const void* attachedWindow);
        ~PresentationChain();
    private:
        intrusive_ptr<IDXGI::SwapChain>         _underlying;
        const void*                             _attachedWindow;
        intrusive_ptr<ID3D::Texture2D>          _defaultDepthTarget;
        std::shared_ptr<PresentationChainDesc>  _desc;

		intrusive_ptr<ID3D::Texture2D> _lastBackBuffer;
		std::shared_ptr<RenderCore::IResource> _lastBackBufferResource;
    };

////////////////////////////////////////////////////////////////////////////////

    class ThreadContext : public IThreadContext
    {
    public:
        IResourcePtr BeginFrame(IPresentationChain& presentationChain) override;
        void    Present(IPresentationChain& presentationChain) override;
		void	CommitCommands(CommitCommandsFlags::BitField=0) override;

        bool                        IsImmediate() const override;
        ThreadContextStateDesc      GetStateDesc() const override;
        std::shared_ptr<IDevice>    GetDevice() const;
        void                        IncrFrameId();
		void						InvalidateCachedState() const override;

		IAnnotator&					GetAnnotator() override;

        ThreadContext(intrusive_ptr<ID3D::DeviceContext> devContext, std::shared_ptr<Device> device);
        ~ThreadContext();
    protected:
        std::shared_ptr<Metal_DX11::DeviceContext> _underlying;
        std::weak_ptr<Device>   _device;  // (must be weak, because Device holds a shared_ptr to the immediate context)
        unsigned                _frameId;
		std::unique_ptr<IAnnotator> _annotator;
    };

    class ThreadContextDX11 : public ThreadContext, public IThreadContextDX11
    {
    public:
        virtual void*       QueryInterface(size_t guid);
        std::shared_ptr<Metal_DX11::DeviceContext>&  GetUnderlying();
        ID3D::Device* ThreadContextDX11::GetUnderlyingDevice();

        ThreadContextDX11(intrusive_ptr<ID3D::DeviceContext> devContext, std::shared_ptr<Device> device);
        ~ThreadContextDX11();
    };

////////////////////////////////////////////////////////////////////////////////

    class Device : public IDevice, public std::enable_shared_from_this<Device>
    {
    public:
        std::unique_ptr<IPresentationChain>     CreatePresentationChain(const void* platformValue, const PresentationChainDesc& desc) /*override*/;

        DeviceDesc     GetDesc();

        std::shared_ptr<IThreadContext>         GetImmediateContext();
        std::unique_ptr<IThreadContext>         CreateDeferredContext();

		IResourcePtr CreateResource(
			const ResourceDesc& desc,
			const std::function<SubResourceInitData(SubResourceId)>&);

		FormatCapability		QueryFormatCapability(Format format, BindFlag::BitField bindingType);

        ID3D::Device*           GetUnderlyingDevice() { return _underlying.get(); }

		void					Stall();

		std::shared_ptr<ILowLevelCompiler>		CreateShaderCompiler();

        Device();
        ~Device();

    protected:
        intrusive_ptr<ID3D::Device>         _underlying;
        intrusive_ptr<ID3D::DeviceContext>  _immediateContext;
        D3D_FEATURE_LEVEL                   _featureLevel;

        std::shared_ptr<ThreadContextDX11>  _immediateThreadContext;

        intrusive_ptr<IDXGI::Factory>       GetDXGIFactory();
        ConsoleRig::AttachablePtr<Metal_DX11::ObjectFactory> _mainFactory;
    };

    class DeviceDX11 : public Device, public IDeviceDX11
    {
    public:
        virtual void*           QueryInterface(size_t guid);
        ID3D::Device*           GetUnderlyingDevice();
        ID3D::DeviceContext*    GetImmediateDeviceContext();
        
        DeviceDX11();
        ~DeviceDX11();
    };

////////////////////////////////////////////////////////////////////////////////
}}
