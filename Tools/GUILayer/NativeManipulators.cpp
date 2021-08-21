// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma warning(disable:4564)

#include "NativeManipulators.h"
#include "ManipulatorUtils.h"
#include "EditorInterfaceUtils.h"
#include "SimpleRenderingContext.h"
#include "GUILayerUtil.h"
#include "LevelEditorScene.h"       // for getting the intersection scene from the IViewContext
#include "MarshalString.h"
#include "EngineDevice.h"
#include "NativeEngineDevice.h"
#include "../ToolsRig/IManipulator.h"
#include "../PlatformRig/InputListener.h"
#include "../SceneEngine/IntersectionTest.h"
#include "../../RenderCore/IDevice.h"
#include "../../SceneEngine/BasicLightingStateDelegate.h"
#include <msclr/auto_handle.h>

using namespace System;
using namespace System::Drawing;

extern "C" __declspec(dllimport) short __stdcall GetKeyState(int nVirtKey);

namespace GUILayer
{
    static void SetupModifierKeys(PlatformRig::InputSnapshot& evnt)
    {
        typedef PlatformRig::InputSnapshot::ActiveButton ActiveButton;
        static auto shift = PlatformRig::KeyId_Make("shift");
        static auto control = PlatformRig::KeyId_Make("control");
        static auto alt = PlatformRig::KeyId_Make("alt");

        if (GetKeyState(0x10) < 0) evnt._activeButtons.push_back(ActiveButton(shift, false, true));
        if (GetKeyState(0x11) < 0) evnt._activeButtons.push_back(ActiveButton(control, false, true));
        if (GetKeyState(0x12) < 0) evnt._activeButtons.push_back(ActiveButton(alt, false, true));
    }

    bool NativeManipulatorLayer::MouseMove(IViewContext^ vc, Point scrPt)
    {
        using namespace PlatformRig;
		InputSnapshot evnt(0, 0, 0, Coord2(scrPt.X, scrPt.Y), Coord2(0, 0));
			// "return true" has two effects --
			//		1. sets the cursor to a moving cursor
			//		2. turns mouse down into "drag-begin" event
		return SendInputEvent(vc, evnt);
    }

    void NativeManipulatorLayer::Render(SimpleRenderingContext^ context)
    {
        auto underlying = _manipContext->GetNativeManipulator();
        if (!underlying) return;
        underlying->Render(context->GetThreadContext(), context->GetParsingContext(), context->GetPipelineAccelerators());
    }

    bool NativeManipulatorLayer::OnBeginDrag(IViewContext^ vc, Point scrPt) 
    { 
        using namespace PlatformRig;
        auto btnState = 1<<0;
		InputSnapshot evnt(
			btnState, btnState, 0,
			Coord2(scrPt.X, scrPt.Y), Coord2(0, 0));
        SetupModifierKeys(evnt);
		return SendInputEvent(vc, evnt);
    }

    bool NativeManipulatorLayer::OnDragging(IViewContext^ vc, Point scrPt) 
	{
			//  We need to create a fake "mouse over" event and pass it through to
			//  the currently selected manipulator. We might also need to set the state
			//  for buttons and keys pressed down.
            //  For the first "OnDragging" operation after a "OnBeginDrag", we should
            //  emulate a mouse down event.
		using namespace PlatformRig;
        auto btnState = 1<<0;
		InputSnapshot evnt(
			btnState, 0, 0,
			Coord2(scrPt.X, scrPt.Y), Coord2(0, 0));
        SetupModifierKeys(evnt);
        return SendInputEvent(vc, evnt);
	}

    bool NativeManipulatorLayer::OnEndDrag(IViewContext^ vc, Point scrPt) 
	{
            // Emulate a "mouse up" operation 
        using namespace PlatformRig;
        auto btnState = 1<<0;
		InputSnapshot evnt(
			0, btnState, 0,
			Coord2(scrPt.X, scrPt.Y), Coord2(0, 0));
        SetupModifierKeys(evnt);
        return SendInputEvent(vc, evnt);
	}

    bool NativeManipulatorLayer::OnHover(IViewContext^ vc, Point scrPt)
    {
        using namespace PlatformRig;
		InputSnapshot evnt(0, 0, 0, Coord2(scrPt.X, scrPt.Y), Coord2(0, 0));
        SetupModifierKeys(evnt);
        return SendInputEvent(vc, evnt);
    }

    void NativeManipulatorLayer::OnMouseWheel(IViewContext^ vc, Point scrPt, int delta)
    {
        using namespace PlatformRig;
		InputSnapshot evnt(
			0, 0, delta,
			Coord2(scrPt.X, scrPt.Y), Coord2(0, 0));
        SetupModifierKeys(evnt);
        SendInputEvent(vc, evnt);
    }

    NativeManipulatorLayer::NativeManipulatorLayer(ActiveManipulatorContext^ manipContext) 
    : _manipContext(manipContext)
    {
    }

    NativeManipulatorLayer::~NativeManipulatorLayer()
    {
    }

    bool NativeManipulatorLayer::SendInputEvent(
        IViewContext^ vc, 
		const PlatformRig::InputSnapshot& evnt)
	{
		auto underlying = _manipContext->GetNativeManipulator();
        if (!underlying) return false;

		msclr::auto_handle<CameraDescWrapper> cam = vc->Camera;
		SceneEngine::IntersectionTestContext intersectionContext {
			*cam->_native,
			UInt2{0,0}, UInt2{(unsigned)vc->ViewportSize.Width, (unsigned)vc->ViewportSize.Height},
            vc->EngineDevice->GetNative().GetDrawingApparatus() };

        // Only way to get the intersection scene is via the SceneManager
        // But what if we don't want to use a SceneManager. Is there a better
        // way to get the intersection scene?
		msclr::auto_handle<IntersectionTestSceneWrapper> hitTestScene = vc->SceneManager->GetIntersectionScene();

        TRY
        {
		    underlying->OnInputEvent(evnt, intersectionContext, &hitTestScene->GetNative());
        } 
        // We need to translate the exceptions that can be raised by native manipulators into something that
        // the .net editors can use here. C# code can't extract any of the C++ details from the except class,
        // so this is the only place to do it.
        // We're going to translate them into messages that can be reported to the user here.
        CATCH (const std::exception& e)
        {
            throw gcnew System::Exception(clix::marshalString<clix::E_UTF8>(e.what()));
        }
        CATCH_END

		return true;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    IManipulatorSet^ ActiveManipulatorContext::ManipulatorSet::get() { return _manipulatorSet; }
    void ActiveManipulatorContext::ManipulatorSet::set(IManipulatorSet^ value)
    {
        if (value != _manipulatorSet) {
            delete _manipulatorSet;
            _manipulatorSet = value;
            OnManipulatorSetChange(this, EventArgs::Empty);
        }
    }

    String^ ActiveManipulatorContext::ActiveManipulator::get() { return _activeManipulator; }
    void ActiveManipulatorContext::ActiveManipulator::set(String^ value)
    {
        if (value != _activeManipulator) {
            auto oldNativeManip = GetNativeManipulator();
            _activeManipulator = value;
            auto newNativeManip = GetNativeManipulator();

            if (oldNativeManip != newNativeManip) {
                if (oldNativeManip) oldNativeManip->SetActivationState(false);
                if (newNativeManip) newNativeManip->SetActivationState(true);
            }
            OnActiveManipulatorChange(this, EventArgs::Empty);
        }
    }

    void ActiveManipulatorContext::RaisePropertyChange()
    {
        OnActiveManipulatorChange(this, EventArgs::Empty);
    }

    void ActiveManipulatorContext::SetPaintCoverageMaterial(int index)
    {
        if (ManipulatorSet != nullptr) {
            auto props = ManipulatorSet->GetProperties("Paint Coverage");
            if (props != nullptr) {
                auto item = props->PropertyDescriptors["PaintValue"];
                if (item != nullptr) {
                    auto obj = System::Linq::Enumerable::FirstOrDefault(props->Items);
                    if (obj != nullptr) {
                        item->SetValue(obj, gcnew System::Int32(index));
                        RaisePropertyChange();
                    }
                }
            }
        }
    }

    int ActiveManipulatorContext::GetPaintCoverageMaterial()
    {
        if (ManipulatorSet != nullptr) {
            auto props = ManipulatorSet->GetProperties("Paint Coverage");
            if (props != nullptr) {
                auto item = props->PropertyDescriptors["PaintValue"];
                if (item != nullptr) {
                    auto obj = System::Linq::Enumerable::FirstOrDefault(props->Items);
                    if (obj != nullptr) {
                        return (System::Int32)item->GetValue(obj);
                    }
                }
            }
        }
        return 0;
    }

    ActiveManipulatorContext::ActiveManipulatorContext()
    {
        _manipulatorSet = nullptr;
        _activeManipulator = "";
    }

    ActiveManipulatorContext::~ActiveManipulatorContext()
    {
        delete _manipulatorSet;
    }

    ToolsRig::IManipulator* ActiveManipulatorContext::GetNativeManipulator()
    {
        auto set = ManipulatorSet;
        auto active = ActiveManipulator;
		if (!set || !active) return nullptr;
		return set->GetManipulator(active).get();
    }

}

