// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "WindowRig.h"
#include "ExportedNativeTypes.h"
#include "../../PlatformRig/OverlappedWindow.h"
#include "../../PlatformRig/FrameRig.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../PlatformRig/MainInputHandler.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/ResourceDesc.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../Utility/PtrUtils.h"
#include "../../OSServices/WinAPI/IncludeWindows.h"

namespace GUILayer
{

///////////////////////////////////////////////////////////////////////////////////////////////////

    void WindowRig::OnResize(unsigned newWidth, unsigned newHeight)
    {
        _frameRig->GetTechniqueContext()._frameBufferPool->Reset();
        _frameRig->ReleaseDoubleBufferAttachments();
        _frameRig->GetTechniqueContext()._attachmentPool->ResetActualized();

        auto desc = _presentationChain->GetDesc();
		desc._width = newWidth;
		desc._height = newHeight;
		_presentationChain->ChangeConfiguration(*_device->GetImmediateContext(), desc);
        _frameRig->UpdatePresentationChain(*_presentationChain);
    }

    void WindowRig::OnInputEvent(const PlatformRig::InputSnapshot& snapshot)
    {
        _mainInputHandler->OnInputEvent(MakeInputContext(), snapshot);
    }

    PlatformRig::InputContext WindowRig::MakeInputContext()
    {
        RECT clientRect;
		GetClientRect((HWND)_platformWindowHandle, &clientRect);
		return { PlatformRig::Coord2{clientRect.left, clientRect.top}, PlatformRig::Coord2{clientRect.right, clientRect.bottom} };
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
    , _platformWindowHandle(platformWindowHandle)
    {
        ::RECT clientRect;
        GetClientRect((HWND)platformWindowHandle, &clientRect);

        RenderCore::PresentationChainDesc presChainCfg {
			unsigned(clientRect.right - clientRect.left), unsigned(clientRect.bottom - clientRect.top)};
        presChainCfg._bindFlags = RenderCore::BindFlag::UnorderedAccess | RenderCore::BindFlag::RenderTarget;
        _presentationChain = _device->CreatePresentationChain(platformWindowHandle, presChainCfg);
        _frameRig = std::make_shared<PlatformRig::FrameRig>(*frameRenderingApparatus, drawingApparatus.get());

        _mainOverlaySystemSet = std::make_shared<PlatformRig::OverlaySystemSet>();
        _frameRig->SetMainOverlaySystem(_mainOverlaySystemSet);

        _frameRig->UpdatePresentationChain(*_presentationChain);

        _mainInputHandler = std::make_unique<PlatformRig::MainInputHandler>();
        _mainInputHandler->AddListener(_mainOverlaySystemSet->GetInputListener());

        /*{
            auto overlaySwitch = std::make_shared<PlatformRig::OverlaySystemSwitch>();
            overlaySwitch->AddSystem("~"_key, PlatformRig::CreateConsoleOverlaySystem());
            _frameRig->GetMainOverlaySystem()->AddSystem(overlaySwitch);
        }*/
    }

    WindowRig::~WindowRig() {}


    IWindowRig::~IWindowRig() {}
}

