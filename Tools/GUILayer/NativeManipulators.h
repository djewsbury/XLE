// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using namespace System::Collections::Generic;

namespace PlatformRig { class InputSnapshot; }
namespace ToolsRig { class IManipulator; }

namespace GUILayer
{
    ref class CameraDescWrapper;
    ref class EditorSceneManager;
    ref class TechniqueContextWrapper;
    ref class EngineDevice;
    ref class SimpleRenderingContext;
    ref class IManipulatorSet;

    public ref class ActiveManipulatorContext
    {
    public:
        ToolsRig::IManipulator* GetNativeManipulator();

        property IManipulatorSet^ ManipulatorSet		{ IManipulatorSet^ get(); void set(IManipulatorSet^); }
        property System::String^ ActiveManipulator      { System::String^ get(); void set(System::String^); }

        void RaisePropertyChange();

        event System::EventHandler^ OnActiveManipulatorChange;
        event System::EventHandler^ OnManipulatorSetChange;

        void SetPaintCoverageMaterial(int index);
        int GetPaintCoverageMaterial();

        ActiveManipulatorContext();
        ~ActiveManipulatorContext();
    private:
        IManipulatorSet^ _manipulatorSet;
        System::String^ _activeManipulator;
    };

    public interface class IViewContext
    {
        property System::Drawing::Size ViewportSize { System::Drawing::Size get(); }
        property CameraDescWrapper^ Camera { CameraDescWrapper^ get(); }
        property EditorSceneManager^ SceneManager { EditorSceneManager^ get(); }
        property EngineDevice^ EngineDevice { GUILayer::EngineDevice^ get(); }
    };

    /// <summary>Provides a bridge between the SCE level editor types and native manipulators<summary>
    /// Accesses native IManipulator methods internally, but takes Sce level editor types as
    /// method parameters.
    public ref class NativeManipulatorLayer
    {
    public:
        bool MouseMove(IViewContext^ vc, System::Drawing::Point scrPt);
        void Render(SimpleRenderingContext^ context);

        bool OnHover(IViewContext^ vc, System::Drawing::Point scrPt);
        bool OnBeginDrag(IViewContext^ vc, System::Drawing::Point scrPt);
        bool OnDragging(IViewContext^ vc, System::Drawing::Point scrPt);
        bool OnEndDrag(IViewContext^ vc, System::Drawing::Point scrPt);
        void OnMouseWheel(IViewContext^ vc, System::Drawing::Point scrPt, int delta);

        NativeManipulatorLayer(ActiveManipulatorContext^ manipContext);
        ~NativeManipulatorLayer();

    private:
        bool SendInputEvent(IViewContext^ vc, const PlatformRig::InputSnapshot& evnt);
        ActiveManipulatorContext^ _manipContext;
    };
}
