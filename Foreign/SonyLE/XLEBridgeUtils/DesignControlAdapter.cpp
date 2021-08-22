// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma warning(disable:4564)   // method 'Copy' of class 'Sce::Atf::Dom::DomNode' defines unsupported default parameter 'originalToCopyMap'

#include "XLELayerUtils.h"
#include "../../Tools/ToolsRig/VisualisationUtils.h"
#include "../../Tools/GUILayer/NativeEngineDevice.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/IDevice.h"
#include <msclr\auto_handle.h>
#include <memory>

using namespace System;
using namespace System::Windows::Forms;
using namespace System::Drawing;
using namespace System::Collections::Generic;
using namespace System::Runtime::InteropServices;
using namespace Sce::Atf;
using namespace Sce::Atf::Adaptation;
using namespace Sce::Atf::Applications;
using namespace Sce::Atf::Dom;
using namespace Sce::Atf::VectorMath;

namespace GUILayer { class RenderTargetWrapper; }

namespace XLEBridgeUtils
{
///////////////////////////////////////////////////////////////////////////////////////////////////

    public interface class IManipulatorExtra
    {
    public:
        virtual bool ClearBeforeDraw() = 0;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    public ref class DesignControlAdapter : public GUILayer::IViewContext
    {
    public:
        DesignControlAdapter(
            Control^ attachedControl, 
            Sce::Atf::Rendering::Camera^ camera,
            GUILayer::EditorSceneManager^ sceneManager,
            GUILayer::ObjectSet^ selection,
            GUILayer::RetainedRenderResources^ savedRes)
        {
            _layerControl = gcnew GUILayer::LayerControl(attachedControl);
            _cameraSettings = gcnew GUILayer::VisCameraSettings();
            _renderSettings = gcnew GUILayer::EditorSceneRenderSettings();
            _renderSettings->_selection = selection;
            _sceneManager = sceneManager;
            _mainOverlay = sceneManager->CreateOverlaySystem(_cameraSettings, _renderSettings);
            _layerControl->AddSystem(_mainOverlay);
            _manipulatorOverlay = gcnew GUILayer::SimpleRenderingContextOverlaySystem();
            _manipulatorOverlay->RetainedResources = savedRes;
            _manipulatorOverlay->CameraSettings = _cameraSettings;
            _layerControl->AddSystem(_manipulatorOverlay);
            SceCamera = camera;
            ViewportSize = attachedControl->Size;
            attachedControl->Resize += gcnew System::EventHandler(this, &DesignControlAdapter::OnResize);
        }

        ~DesignControlAdapter() 
        { 
            delete _layerControl; _layerControl = nullptr; 
            delete _cameraSettings; _cameraSettings = nullptr; 
            delete _renderSettings; _renderSettings = nullptr; 
            delete _manipulatorOverlay; _manipulatorOverlay = nullptr; 
            delete _mainOverlay; _mainOverlay = nullptr;
        }

        void Render()
        {
                //  "_cameraSettings" should match the camera set in 
                //  the view control
            auto camera = _camera;
            auto& dstCam = *_cameraSettings->GetUnderlyingRaw();
            dstCam._position = AsFloat3(camera->WorldEye);
            dstCam._focus = AsFloat3(camera->WorldLookAtPoint);
            dstCam._nearClip = camera->NearZ;
            dstCam._farClip = camera->FarZ;

            if (camera->ProjectionType == Sce::Atf::Rendering::ProjectionType::Orthographic) {
                dstCam._projection = ToolsRig::VisCameraSettings::Projection::Orthogonal;
                dstCam._left = camera->Frustum->Left;
                dstCam._right = camera->Frustum->Right;
                dstCam._top = camera->Frustum->Top;
                dstCam._bottom = camera->Frustum->Bottom;
            } else {
                dstCam._projection = ToolsRig::VisCameraSettings::Projection::Perspective;
                dstCam._verticalFieldOfView = camera->YFov * 180.f / gPI;
            }
            _layerControl->Render();

                // testing for agreement between SonyWWS camera code and XLE camera code
            // float viewportAspect = Width / float(Height);
            // auto camDesc = PlatformRig::AsCameraDesc(*_cameraSettings->GetUnderlyingRaw());
            // auto proj = RenderCore::Techniques::PerspectiveProjection(
            //     camDesc, viewportAspect);
            // auto viewMat = InvertOrthonormalTransform(camDesc._cameraToWorld);
            // auto compViewMat = Camera->ViewMatrix;
            // auto compProjMat = Camera->ProjectionMatrix;
            // (void)compViewMat; (void)compProjMat; (void)proj; (void)viewMat;
        }

        void AddRenderCallback(GUILayer::RenderCallback^ callback)
        {
            _manipulatorOverlay->OnRender += callback;
        }

		void AddNonRenderPassCallback(GUILayer::RenderCallback^ callback)
        {
            _manipulatorOverlay->OnRenderPostProcess += callback;
        }

        property GUILayer::EditorSceneManager^ SceneManager {
            virtual GUILayer::EditorSceneManager^ get() { return _sceneManager; }
        }

        property GUILayer::EngineDevice^ EngineDevice { 
            virtual GUILayer::EngineDevice^ get() { return GUILayer::EngineDevice::GetInstance(); }
        }

        property GUILayer::EditorSceneRenderSettings^ RenderSettings {
            virtual GUILayer::EditorSceneRenderSettings^ get() { return _renderSettings; }
        }

        property GUILayer::CameraDescWrapper^ Camera {
            virtual GUILayer::CameraDescWrapper^ get() { return Utils::AsCameraDesc(_camera); }
        }

        property Sce::Atf::Rendering::Camera^ SceCamera
        {
            Sce::Atf::Rendering::Camera^ get() { return _camera; }
            void set(Sce::Atf::Rendering::Camera^ camera)
            {
                _camera = camera;
                    //  use the camera "AxisSystem" to convert from the native SonyWWS
                    //  camera coordinates into the coordinates we need for XLE.
                    //      SonyWWS native has +Y as up
                    //      But XLE uses +Z up
                    //  note -- handiness untested. XLE defaults to right handed coordinates in
                    //          view space.
                    //
                    //  When using AxisSystem like this, we must use the "World..." properties 
                    //  in the Camera object. eg, "WorldEye" gives us the native XLE coordinates,
                    //  "Eye" gives us the native SonyWWS coordinates.
                _camera->AxisSystem = gcnew Matrix4F(
                    1.f, 0.f, 0.f, 0.f, 
                    0.f, 0.f, -1.f, 0.f, 
                    0.f, 1.f, 0.f, 0.f, 
                    0.f, 0.f, 0.f, 1.f);
            }
        }

        virtual property Size ViewportSize;

    protected:
        void OnResize(System::Object^ sender, System::EventArgs^ e)
        {
            Control^ ctrl = dynamic_cast<Control^>(sender);
            if (ctrl != nullptr) {
                auto sz = ctrl->ClientSize;
                ViewportSize = sz;
                if (sz.Width > 0 && sz.Height > 0)
                    _camera->Aspect = (float)sz.Width / (float)sz.Height;
            }
        }

        GUILayer::LayerControl^ _layerControl;
        GUILayer::VisCameraSettings^ _cameraSettings;
        GUILayer::EditorSceneManager^ _sceneManager;
        GUILayer::EditorSceneRenderSettings^ _renderSettings;
        GUILayer::IOverlaySystem^ _mainOverlay;

    private:
        GUILayer::SimpleRenderingContextOverlaySystem^ _manipulatorOverlay;
        Sce::Atf::Rendering::Camera^ _camera;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////
    //      P I C K I N G                                                                   //
///////////////////////////////////////////////////////////////////////////////////////////////////

    public ref class Picking
    {
    public:
        [Flags]
        enum struct Flags : unsigned
        {
            Terrain = 1 << 0, Objects = 1 << 1, Helpers = 1 << 6, 
            AllWorldObjects = Terrain | Objects,
            IgnoreSelection = 1 << 10 
        };

        [StructLayout(LayoutKind::Sequential)]
        value struct HitRecord
        {
            uint64 documentId;
            uint64 instanceId;    // instance id of the game object.
            uint index;          // index of sub-object (if any )
            float distance;      // distance from camera ( for sorting )
            Vec3F hitPt;       // hit point in world space.
            Vec3F normal;      // world normal at hit-point.       
            Vec3F nearestVertex; //  nearest vertex in world space.
            bool hasNormal;
            bool hasNearestVert;
            short pad;

            uint32 drawCallIndex;
            uint64 materialGuid;
            String^ materialName;
            String^ modelName;
        };

        static array<HitRecord>^ FrustumPick(
            GUILayer::EngineDevice^ device,
            GUILayer::EditorSceneManager^ sceneManager,
            Matrix4F^ pickingFrustum, 
            GUILayer::CameraDescWrapper^ camera,
            System::Drawing::Size viewportSize,
            Flags flags)
        {
            System::Diagnostics::Debug::Assert(unsigned(flags & Flags::IgnoreSelection) == 0);

            ICollection<GUILayer::HitRecord>^ results = nullptr;
            {
                msclr::auto_handle<GUILayer::IntersectionTestSceneWrapper> scene(
                    sceneManager->GetIntersectionScene());

                cli::pin_ptr<float> ptr = &pickingFrustum->M11;
                results = GUILayer::EditorInterfaceUtils::FrustumIntersection(
					camera,
                    scene.get(), ptr, (uint)flags);
            }

            if (results == nullptr) { return nullptr; }
            return AsHitRecordArray(results);
        }

        static array<HitRecord>^ AsHitRecordArray(ICollection<GUILayer::HitRecord>^ results)
        {
            auto hitRecords = gcnew array<HitRecord>(results->Count);
            uint index = 0;
            for each(auto r in results)
            {
                hitRecords[index].documentId = r._document;
                hitRecords[index].instanceId = r._object;
                hitRecords[index].index = 0;
                hitRecords[index].distance = r._distance;
                hitRecords[index].hitPt = Utils::AsVec3F(r._worldSpaceCollision);
                hitRecords[index].normal = Vec3F(0.0f, 0.0f, 0.0f);
                hitRecords[index].nearestVertex = Vec3F(0.0f, 0.0f, 0.0f);
                hitRecords[index].hasNormal = hitRecords[index].hasNearestVert = false;
                hitRecords[index].drawCallIndex = r._drawCallIndex;
                hitRecords[index].materialGuid = r._materialGuid;
                hitRecords[index].materialName = r._materialName;
                hitRecords[index].modelName = r._modelName;
                index++;
            }

            return hitRecords;
        }

        static array<HitRecord>^ RayPick(
            GUILayer::EngineDevice^ device,
            GUILayer::EditorSceneManager^ sceneManager,
            Ray3F ray, GUILayer::CameraDescWrapper^ camera, 
            System::Drawing::Size viewportSize, Flags flags)
        {
            System::Diagnostics::Debug::Assert(unsigned(flags & Flags::IgnoreSelection) == 0);

            ICollection<GUILayer::HitRecord>^ results = nullptr; 
            auto endPt = ray.Origin + camera->_native->_farClip * ray.Direction;

            {
                msclr::auto_handle<GUILayer::IntersectionTestSceneWrapper> scene(
                    sceneManager->GetIntersectionScene());

                results = GUILayer::EditorInterfaceUtils::RayIntersection(
					camera, viewportSize.Width, viewportSize.Height,
                    scene.get(),
                    Utils::AsVector3(ray.Origin),
                    Utils::AsVector3(endPt), (uint)flags);
            }

            if (results == nullptr) { return nullptr; }
            return AsHitRecordArray(results);
        }

        static array<HitRecord>^ RayPick(
            GUILayer::IViewContext^ vc,
            Ray3F ray, Flags flags)
        {
            auto sceneMan = vc->SceneManager;
            if (!sceneMan) return nullptr;
            
            return RayPick(
                vc->EngineDevice,
                sceneMan,
                ray, vc->Camera, vc->ViewportSize, flags);
        }

        static array<HitRecord>^ FrustumPick(
            GUILayer::IViewContext^ vc,
            Matrix4F^ pickingFrustum, Flags flags)
        {
            auto sceneMan = vc->SceneManager;
            if (!sceneMan) return nullptr;
            
            return FrustumPick(
                vc->EngineDevice,
                sceneMan,
                pickingFrustum, vc->Camera, vc->ViewportSize, flags);
        }
    };
}

