// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma warning(disable:4793) //  : function compiled as native :

#include "EngineControl.h"
#include "EngineDevice.h"
#include "NativeEngineDevice.h"
#include "WindowRig.h"
#include "DelayedDeleteQueue.h"
#include "ExportedNativeTypes.h"
#include "../../PlatformRig/FrameRig.h"
#include "../../PlatformRig/OverlappedWindow.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../PlatformRig/InputTranslator.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/IDevice.h"
#include "../../Utility/PtrUtils.h"

using namespace System::Windows::Forms;

namespace GUILayer 
{
	static msclr::auto_gcroot<System::Collections::Generic::List<System::WeakReference^>^> s_regularAnimationControls
		= gcnew System::Collections::Generic::List<System::WeakReference^>();

	bool EngineControl::HasRegularAnimationControls()
	{
		for (int c=0; c<s_regularAnimationControls->Count;) {
			if (!s_regularAnimationControls.get()[c]->IsAlive) {
				s_regularAnimationControls->RemoveAt(c);
			} else {
				++c;
			}
		}
		
		for each(auto r in s_regularAnimationControls.get()) {
			auto target = (EngineControl^)r->Target;
			if (target && target->IsVisible())
				return true;
		}
		return false;
	}

	void EngineControl::TickRegularAnimation()
	{
		array<System::WeakReference^>^ renderables = gcnew array<System::WeakReference^>(s_regularAnimationControls->Count);
		s_regularAnimationControls->CopyTo(renderables);
		for each(auto r in renderables) {
			auto target = (EngineControl^)r->Target;
			if (target)
				target->Render();
		}
	}

	static void AddRegularAnimation(EngineControl^ ctrl)
	{
		for (int c=0; c<s_regularAnimationControls->Count;++c)
			if (!s_regularAnimationControls.get()[c]->Target == (System::Object^)ctrl)
				return;
		s_regularAnimationControls->Add(gcnew System::WeakReference(ctrl));
	}

	static void RemoveRegularAnimation(EngineControl^ ctrl)
	{
		for (int c=0; c<s_regularAnimationControls->Count;) {
			if (!s_regularAnimationControls.get()[c]->IsAlive 
				|| s_regularAnimationControls.get()[c]->Target == (System::Object^)ctrl) {
				s_regularAnimationControls->RemoveAt(c);
			} else {
				++c;
			}
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    void EngineControl::OnPaint(Control^ ctrl, PaintEventArgs^ pe)
    {
        // Note -- we're suppressing base class paint events to
        // try to avoid flicker. See:
        //    https://msdn.microsoft.com/en-us/library/1e430ef4(v=vs.85).aspx
        // __super::OnPaint(pe);

        bool res = Render();
		if (!res) {
			AddRegularAnimation(this);
		} else {
			RemoveRegularAnimation(this);
		}
    }

    bool EngineControl::Render()
    {
        auto engineDevice = EngineDevice::GetInstance();
        auto* renderDevice = engineDevice->GetNative().GetRenderDevice().get();
        auto immediateContext = renderDevice->GetImmediateContext();
        bool result = Render(immediateContext, *_pimpl->_windowRig.get());

            // perform our delayed deletes now (in the main thread)
        DelayedDeleteQueue::FlushQueue();
        return result;
    }

	void EngineControl::OnResize()
    {        
	}

	PlatformRig::InputContext EngineControl::MakeInputContext(System::Windows::Forms::Control^ control)
	{
		System::Drawing::Rectangle rect = control->ClientRectangle;
		return PlatformRig::InputContext {
			{ rect.Left, rect.Top },
			{ rect.Right, rect.Bottom }
		};
	}

    void EngineControl::Evnt_Resize(Object^ sender, System::EventArgs^ e)
    {
        auto ctrl = dynamic_cast<Control^>(sender);
        if (!ctrl) return;
		OnResize();
        _pimpl->_windowRig->OnResize(ctrl->Size.Width, ctrl->Size.Height);
    }

    void EngineControl::Evnt_KeyDown(Object^ sender, KeyEventArgs^ e)
    {
        if (_pimpl->_inputTranslator) { 
            auto ctrl = dynamic_cast<Control^>(sender);
            if (!ctrl) return;

            _pimpl->_inputTranslator->OnKeyChange(MakeInputContext(ctrl), e->KeyValue, true); 
            e->Handled = true;
            ctrl->Invalidate();
        }
    }

    void EngineControl::Evnt_KeyUp(Object^ sender, KeyEventArgs^ e)
    {
        if (_pimpl->_inputTranslator) { 
            auto ctrl = dynamic_cast<Control^>(sender);
            if (!ctrl) return;

            _pimpl->_inputTranslator->OnKeyChange(MakeInputContext(ctrl), e->KeyValue, false); 
            e->Handled = true;
            ctrl->Invalidate();
        }
    }

    void EngineControl::Evnt_KeyPress(Object^ sender, KeyPressEventArgs^ e)
    {
        if (_pimpl->_inputTranslator) {
            auto ctrl = dynamic_cast<Control^>(sender);
            if (!ctrl) return;
            
            _pimpl->_inputTranslator->OnChar(MakeInputContext(ctrl), e->KeyChar);
            e->Handled = true;
            ctrl->Invalidate();
        }
    }

    void EngineControl::Evnt_MouseMove(Object^ sender, MouseEventArgs^ e)
    {
            // (todo -- only when activated?)
        if (_pimpl->_inputTranslator) {
            auto ctrl = dynamic_cast<Control^>(sender);
            if (!ctrl) return;
            
            _pimpl->_inputTranslator->OnMouseMove(MakeInputContext(ctrl), e->Location.X, e->Location.Y);
            ctrl->Invalidate();
        }
    }

    static unsigned AsIndex(MouseButtons button)
    {
        switch (button) {
        case MouseButtons::Left: return 0;
        case MouseButtons::Right: return 1;
        case MouseButtons::Middle: return 2;
        default: return 3;
        }
    }
    
    void EngineControl::Evnt_MouseDown(Object^ sender, MouseEventArgs^ e)
    {
        if (_pimpl->_inputTranslator) {
            auto ctrl = dynamic_cast<Control^>(sender);
            if (!ctrl) return;

            _pimpl->_inputTranslator->OnMouseButtonChange(MakeInputContext(ctrl), e->Location.X, e->Location.Y, AsIndex(e->Button), true);
            ctrl->Invalidate();
        }
    }

    void EngineControl::Evnt_MouseUp(Object^ sender, MouseEventArgs^ e)
    {
        if (_pimpl->_inputTranslator) {
            auto ctrl = dynamic_cast<Control^>(sender);
            if (!ctrl) return;
            
            _pimpl->_inputTranslator->OnMouseButtonChange(MakeInputContext(ctrl), e->Location.X, e->Location.Y, AsIndex(e->Button), false);
            ctrl->Invalidate();
        }
    }

    void EngineControl::Evnt_MouseWheel(Object^ sender, MouseEventArgs^ e)
    {
        if (_pimpl->_inputTranslator) {
            auto ctrl = dynamic_cast<Control^>(sender);
            if (!ctrl) return;
            
            _pimpl->_inputTranslator->OnMouseWheel(MakeInputContext(ctrl), e->Delta);
            ctrl->Invalidate();
        }
    }

    void EngineControl::Evnt_DoubleClick(Object^ sender, MouseEventArgs^ e)
    {
        if (_pimpl->_inputTranslator) {
            auto ctrl = dynamic_cast<Control^>(sender);
            if (!ctrl) return;
            
            _pimpl->_inputTranslator->OnMouseButtonDblClk(MakeInputContext(ctrl), e->Location.X, e->Location.Y, AsIndex(e->Button));
            ctrl->Invalidate();
        }
    }

    void EngineControl::Evnt_FocusChange(Object ^sender, System::EventArgs ^e)
    {
        // when we've lost or gained the focus, we need to reset the input translator 
        //  (because we might miss key up/down message when not focused)
        if (_pimpl.get() && _pimpl->_inputTranslator.get()) {       // (this can sometimes be called after the dispose, which ends up with an invalid _pimpl)
			auto ctrl = dynamic_cast<Control^>(sender);
            _pimpl->_inputTranslator->OnFocusChange(MakeInputContext(ctrl));
        }
    }


    IWindowRig& EngineControl::GetWindowRig()
    {
        assert(_pimpl->_windowRig);
        return *_pimpl->_windowRig;
    }

    bool EngineControl::IsInputKey(Keys keyData)
    {
            // return true for any keys we want to handle as a normal (non-system)
            // key event
        switch (keyData)
        {
        case Keys::Left:
        case Keys::Right:
        case Keys::Up:
        case Keys::Down:
        case Keys::Tab:
            return true;

        default:
            return false;
        }
    }
    
    void EngineControl::OnEngineShutdown()
    {
        // Drop the "EngineControlPimpl", because this contains references to native stuff
        _pimpl.reset();
    }

	bool EngineControl::IsVisible()
	{
		Control^ ctrl = (Control^)_attachedControl->Target;
		if (ctrl)
			return ctrl->Visible;
		return false;
	}

	static std::unique_ptr<WindowRig> CreateWindowRig(EngineDevice^ engineDevice, const void* nativeWindowHandle)
    {
        return std::make_unique<WindowRig>(
            *engineDevice->GetNative().GetRenderDevice(),
            nativeWindowHandle);
    }

    EngineControl::EngineControl(Control^ control)
    {
        _pimpl.reset(new EngineControlPimpl);
        auto engineDevice = EngineDevice::GetInstance();
        _pimpl->_windowRig = CreateWindowRig(engineDevice, control->Handle.ToPointer());
        _pimpl->_inputTranslator = std::make_unique<PlatformRig::InputTranslator>();
        _pimpl->_inputTranslator->AddListener(_pimpl->_windowRig->GetFrameRig().GetMainOverlaySystem()->GetInputListener());

        control->KeyDown    += gcnew System::Windows::Forms::KeyEventHandler(this, &EngineControl::Evnt_KeyDown);
        control->KeyUp      += gcnew System::Windows::Forms::KeyEventHandler(this, &EngineControl::Evnt_KeyUp);
        control->KeyPress   += gcnew System::Windows::Forms::KeyPressEventHandler(this, &EngineControl::Evnt_KeyPress);
        control->MouseMove  += gcnew System::Windows::Forms::MouseEventHandler(this, &EngineControl::Evnt_MouseMove);
        control->MouseDown  += gcnew System::Windows::Forms::MouseEventHandler(this, &EngineControl::Evnt_MouseDown);
        control->MouseUp    += gcnew System::Windows::Forms::MouseEventHandler(this, &EngineControl::Evnt_MouseUp);
        control->MouseWheel += gcnew System::Windows::Forms::MouseEventHandler(this, &EngineControl::Evnt_MouseWheel);
        control->MouseDoubleClick += gcnew System::Windows::Forms::MouseEventHandler(this, &EngineControl::Evnt_DoubleClick);
        control->GotFocus   += gcnew System::EventHandler(this, &GUILayer::EngineControl::Evnt_FocusChange);
        control->LostFocus  += gcnew System::EventHandler(this, &GUILayer::EngineControl::Evnt_FocusChange);
        control->Resize     += gcnew System::EventHandler(this, &GUILayer::EngineControl::Evnt_Resize);

		_attachedControl = gcnew System::WeakReference(control);

        // We can't guarantee when the destructor or finalizer will be called. But we need to make sure
        // that the native objects are released before the device is destroyed. The only way to do that
        // is to install a callback in the engine device itself, 
        engineDevice->AddOnShutdown(this);
    }

    EngineControl::~EngineControl()
    {
        _pimpl.reset();
    }

    EngineControl::!EngineControl()
    {
        if (_pimpl.get()) {
			System::Diagnostics::Debug::Assert(false, "Non deterministic delete of EngineControl");
		}
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    EngineControlPimpl::~EngineControlPimpl() {}

}

