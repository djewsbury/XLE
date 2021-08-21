// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EngineControl.h"
#include <memory>

namespace RenderCore { namespace Techniques { class  TechniqueContext; class AttachmentPool; class FrameBufferPool; }}

namespace GUILayer 
{
    class LayerControlPimpl;

    ref class ModelVisSettings;
    ref class IOverlaySystem;
    ref class VisCameraSettings;
    ref class VisMouseOver;
    ref class VisResources;
    ref class TechniqueContextWrapper;

    public ref class LayerControl : public EngineControl
    {
    public:
        void AddDefaultCameraHandler(VisCameraSettings^);
        void AddSystem(IOverlaySystem^ overlay);

        LayerControl(System::Windows::Forms::Control^ control);
        ~LayerControl();

    protected:
        bool _activePaint;

        virtual bool Render(const std::shared_ptr<RenderCore::IThreadContext>&, IWindowRig&) override;
		virtual void OnResize() override;
    };
}
