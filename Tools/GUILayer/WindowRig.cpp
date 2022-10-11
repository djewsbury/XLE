// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "WindowRig.h"
#include "ExportedNativeTypes.h"
#include "../../PlatformRig/OverlappedWindow.h"
#include "../../PlatformRig/FrameRig.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/ResourceDesc.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../Utility/PtrUtils.h"
#include "../../OSServices/WinAPI/IncludeWindows.h"

#include "../../PlatformRig/OverlaySystem.h"
#include "../../RenderOverlays/DebuggingDisplay.h"

namespace GUILayer
{

///////////////////////////////////////////////////////////////////////////////////////////////////

    class ResizePresentationChain : public PlatformRig::IWindowHandler
    {
    public:
        void    OnResize(unsigned newWidth, unsigned newHeight);

        ResizePresentationChain(
            std::shared_ptr<RenderCore::IPresentationChain> presentationChain);
    protected:
        std::weak_ptr<RenderCore::IPresentationChain> _presentationChain;
    };

    void ResizePresentationChain::OnResize(unsigned newWidth, unsigned newHeight)
    {
		auto chain = _presentationChain.lock();
        if (chain) {
                //  When we become an icon, we'll end up with zero width and height.
                //  We can't actually resize the presentation to zero. And we can't
                //  delete the presentation chain from here. So maybe just do nothing.
            if (newWidth && newHeight) {
				chain->Resize(newWidth, newHeight);
            }
        }
    }

    ResizePresentationChain::ResizePresentationChain(std::shared_ptr<RenderCore::IPresentationChain> presentationChain)
    : _presentationChain(presentationChain)
    {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    void WindowRig::AddWindowHandler(std::shared_ptr<PlatformRig::IWindowHandler> windowHandler)
    {
        _windowHandlers.push_back(std::move(windowHandler));
    }

    void WindowRig::OnResize(unsigned newWidth, unsigned newHeight)
    {
        for (auto i=_windowHandlers.begin(); i!=_windowHandlers.end(); ++i) {
            (*i)->OnResize(newWidth, newHeight);
        }
        _frameRig->UpdatePresentationChain(*_device, *_presentationChain);
    }

    PlatformRig::OverlaySystemSet& WindowRig::GetMainOverlaySystemSet()
    {
        return *_mainOverlaySystemSet;
    }

    WindowRig::WindowRig(
        std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
        std::shared_ptr<RenderCore::Techniques::FrameRenderingApparatus> frameRenderingApparatus,
        const void* platformWindowHandle)
    : _device(drawingApparatus->_device)
    {
        ::RECT clientRect;
        GetClientRect((HWND)platformWindowHandle, &clientRect);

        _presentationChain = _device->CreatePresentationChain(
            platformWindowHandle,
			RenderCore::PresentationChainDesc {
				unsigned(clientRect.right - clientRect.left), unsigned(clientRect.bottom - clientRect.top)});
        _frameRig = std::make_shared<PlatformRig::FrameRig>(*frameRenderingApparatus, drawingApparatus.get());

        _mainOverlaySystemSet = std::make_shared<PlatformRig::OverlaySystemSet>();
        _frameRig->SetMainOverlaySystem(_mainOverlaySystemSet);

        _frameRig->UpdatePresentationChain(*_device, *_presentationChain);

        /*{
            auto overlaySwitch = std::make_shared<PlatformRig::OverlaySystemSwitch>();
            overlaySwitch->AddSystem(PlatformRig::KeyId_Make("~"), PlatformRig::CreateConsoleOverlaySystem());
            _frameRig->GetMainOverlaySystem()->AddSystem(overlaySwitch);
        }*/

        AddWindowHandler(std::make_shared<ResizePresentationChain>(_presentationChain));
    }

    WindowRig::~WindowRig() {}


    IWindowRig::~IWindowRig() {}
}

