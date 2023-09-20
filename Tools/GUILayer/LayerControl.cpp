// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma warning(disable:4793) //  : function compiled as native :

#include "LayerControl.h"
#include "IWindowRig.h"
#include "IOverlaySystem.h"
#include "UITypesBinding.h"
#include "EngineDevice.h"
#include "NativeEngineDevice.h"
#include "GUILayerUtil.h"
#include "ExportedNativeTypes.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../ToolsRig/ModelVisualisation.h"
#include "../ToolsRig/IManipulator.h"
#include "../ToolsRig/BasicManipulators.h"
#include "../ToolsRig/VisualisationUtils.h"
#include "../../PlatformRig/FrameRig.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../PlatformRig/PlatformApparatuses.h"
#include "../../RenderOverlays/SimpleVisualization.h"

using namespace System;

namespace GUILayer 
{
    bool LayerControl::Render(const std::shared_ptr<RenderCore::IThreadContext>& threadContext, IWindowRig& windowRig)
    {
            // Rare cases can recursively start rendering
            // (for example, if we attempt to call a Windows GUI function while in the middle of
            // rendering)
            // Re-entering rendering recursively can cause some bad problems, however
            //  -- so we need to prevent it.
        if (_activePaint)
            return false;

            // Check for cases where a paint operation can be begun on one window
            // while another window is in the middle of rendering.
        static bool activePaintCheck2 = false;
        if (activePaintCheck2) return false;

        _activePaint = true;
        activePaintCheck2 = true;

        if (_pendingUpdateRenderTargets) {
            // ensure overlays have render targets configured
            auto rtu = windowRig.GetFrameRig().GetOverlayConfiguration(*windowRig.GetPresentationChain());
            _mainOverlaySystemSet->OnRenderTargetUpdate(rtu._preregAttachments, rtu._fbProps, rtu._systemAttachmentFormats);
            if (_debugOverlaysApparatus.get()) {
                auto updatedAttachments = PlatformRig::InitializeColorLDR(rtu._preregAttachments);
                _debugOverlaysApparatus->_debugScreensOverlaySystem->OnRenderTargetUpdate(updatedAttachments, rtu._fbProps, rtu._systemAttachmentFormats);
            }
            _pendingUpdateRenderTargets = false;
        }
        
        bool result = true;
        TRY
        {
            auto& frameRig = windowRig.GetFrameRig();
            auto parserContext = frameRig.StartupFrame(threadContext, windowRig.GetPresentationChain());
            TRY {
                _mainOverlaySystemSet->Render(parserContext);
                if (_debugOverlaysApparatus.get())
                    _debugOverlaysApparatus->_debugScreensOverlaySystem->Render(parserContext);
            } CATCH(const std::exception& e) {
                RenderOverlays::DrawBottomOfScreenErrorMsg(parserContext, *EngineDevice::GetInstance()->GetNative().GetOverlayApparatus(), e.what());
            } CATCH_END

            frameRig.ShutdownFrame(parserContext);

            // return false if when we have pending resources (encourage another redraw)
            result = !parserContext.HasPendingAssets();

			if (_mainOverlaySystemSet->GetOverlayState()._refreshMode == PlatformRig::IOverlaySystem::RefreshMode::RegularAnimation)
				result = false;

        } CATCH (...) {
        } CATCH_END
        activePaintCheck2 = false;
        _activePaint = false;

        return result;
    }

	void LayerControl::OnResize(IWindowRig& windowRig)
    {
		// We must reset the framebuffer in order to dump references to the presentation chain on DX (because it's going to be resized along with the window)
		EngineDevice::GetInstance()->GetNative().ResetFrameBufferPool();

        if (_mainOverlaySystemSet.get()) {
            auto rtu = windowRig.GetFrameRig().GetOverlayConfiguration(*windowRig.GetPresentationChain());
            _mainOverlaySystemSet->OnRenderTargetUpdate(rtu._preregAttachments, rtu._fbProps, rtu._systemAttachmentFormats);
        }
	}

    void LayerControl::ProcessInput(const PlatformRig::InputContext& context, const OSServices::InputSnapshot& snapshot)
    {
        if (_mainOverlaySystemSet.get())
            _mainOverlaySystemSet->ProcessInput(context, snapshot);
    }

    void LayerControl::UpdateRenderTargets()
    {
        _pendingUpdateRenderTargets = true;
    }

    void LayerControl::EnableFrameRigOverlay(bool newState, std::shared_ptr<::Assets::OperationContext> opContext)
    {
        if (!newState) {
            if (_frameRigDisplay.get()) {
                _debugOverlaysApparatus.reset();
                _frameRigDisplay.reset();
                _pendingUpdateRenderTargets = true;
            }
            return;
        }

        if (_frameRigDisplay.get()) {
            _frameRigDisplay->SetLoadingContext(opContext);
            return;
        }

        _debugOverlaysApparatus = std::make_shared<PlatformRig::DebugOverlaysApparatus>(EngineDevice::GetInstance()->GetNative().GetOverlayApparatus());
        _frameRigDisplay = GetWindowRig().GetFrameRig().CreateDisplay(_debugOverlaysApparatus->_debugSystem, opContext);
        _frameRigDisplay->SetStyle(PlatformRig::IFrameRigDisplay::Style::NonInteractive);
        _frameRigDisplay->EnableMainStates(true);
        PlatformRig::SetSystemDisplay(*_debugOverlaysApparatus->_debugSystem.get(), _frameRigDisplay.GetNativePtr());
        _pendingUpdateRenderTargets = true;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////
    
    namespace Internal
    {
        class OverlaySystemAdapter : public PlatformRig::IOverlaySystem
        {
        public:
            virtual PlatformRig::ProcessInputResult ProcessInput(
			    const PlatformRig::InputContext& context,
			    const OSServices::InputSnapshot& evnt)
            {
                return PlatformRig::IOverlaySystem::ProcessInput(context, evnt);
            }

            void Render(
                RenderCore::Techniques::ParsingContext& parserContext) override
            {
				_managedOverlay->Render(parserContext);
            }

            void SetActivationState(bool newState)
            {
            }

            void OnRenderTargetUpdate(
                IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
                const RenderCore::FrameBufferProperties& fbProps,
                IteratorRange<const RenderCore::Format*> systemAttachmentFormats) override
            {
                _managedOverlay->OnRenderTargetUpdate(preregAttachments, fbProps, systemAttachmentFormats);
            }

            OverlaySystemAdapter(::GUILayer::IOverlaySystem^ managedOverlay) : _managedOverlay(managedOverlay) {}
            ~OverlaySystemAdapter() {}
        protected:
            msclr::auto_gcroot<::GUILayer::IOverlaySystem^> _managedOverlay;
        };
    }

	IOverlaySystem::~IOverlaySystem() {}

    void LayerControl::AddSystem(IOverlaySystem^ overlay)
    {
        _mainOverlaySystemSet->AddSystem(std::shared_ptr<Internal::OverlaySystemAdapter>(
            new Internal::OverlaySystemAdapter(overlay)));
    }

    PlatformRig::OverlaySystemSet& LayerControl::GetMainOverlaySystemSet()
    {
        return *_mainOverlaySystemSet.get();
    }

    void LayerControl::AddDefaultCameraHandler(VisCameraSettings^ settings)
    {
            // create an input listener that feeds into a stack of manipulators
        auto manipulators = std::make_shared<ToolsRig::ManipulatorStack>(settings->GetUnderlying(), EngineDevice::GetInstance()->GetNative().GetDrawingApparatus());
        manipulators->Register(
            ToolsRig::ManipulatorStack::CameraManipulator,
            ToolsRig::CreateCameraManipulator(settings->GetUnderlying()));

        auto& overlaySet = *_mainOverlaySystemSet.get();
        overlaySet.AddSystem(ToolsRig::MakeLayerForInput(manipulators));
    }

    LayerControl::LayerControl(System::Windows::Forms::Control^ control)
        : EngineControl(control)
    {
        _activePaint = false;
        _pendingUpdateRenderTargets = true;
        _mainOverlaySystemSet.reset(new PlatformRig::OverlaySystemSet());
    }

    LayerControl::~LayerControl() 
    {
        _mainOverlaySystemSet.reset();
    }
}

