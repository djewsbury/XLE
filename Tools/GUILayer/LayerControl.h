// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EngineControl.h"
#include <memory>

namespace PlatformRig { class OverlaySystemSet; }

namespace GUILayer 
{
    ref class IOverlaySystem;
    ref class VisCameraSettings;

    public ref class LayerControl : public EngineControl
    {
    public:
        void AddDefaultCameraHandler(VisCameraSettings^);
        void AddSystem(IOverlaySystem^ overlay);
        PlatformRig::OverlaySystemSet& GetMainOverlaySystemSet();
        void UpdateRenderTargets();

        LayerControl(System::Windows::Forms::Control^ control);
        ~LayerControl();

    protected:
        bool _activePaint;
        bool _pendingUpdateRenderTargets;
        clix::auto_ptr<PlatformRig::OverlaySystemSet> _mainOverlaySystemSet;

        virtual bool Render(const std::shared_ptr<RenderCore::IThreadContext>&, IWindowRig&) override;
		virtual void OnResize(IWindowRig&) override;
        virtual void ProcessInput(const PlatformRig::InputContext&, const OSServices::InputSnapshot&) override;
    };
}
