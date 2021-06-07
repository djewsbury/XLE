// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BasicManipulators.h"
#include "IManipulator.h"
#include "ModelVisualisation.h"
#include "VisualisationUtils.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../SceneEngine/IntersectionTest.h"
#include "../../Math/Transformations.h"
#include "../../Math/Geometry.h"
#include <memory>

namespace ToolsRig
{
    class CameraMovementManipulator : public IManipulator
    {
    public:
        bool OnInputEvent(
            const PlatformRig::InputSnapshot& evnt, 
            const SceneEngine::IntersectionTestContext& hitTestContext,
            const SceneEngine::IIntersectionScene* hitTestScene);
        void Render(
            RenderCore::IThreadContext& context, 
            RenderCore::Techniques::ParsingContext& parserContext,
            RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators);

        const char* GetName() const;
        std::string GetStatusText() const;

        IteratorRange<const FloatParameter*>  GetFloatParameters() const { return {}; }
        IteratorRange<const BoolParameter*>   GetBoolParameters() const { return {}; }
		IteratorRange<const IntParameter*>   GetIntParameters() const { return {}; }
		void SetActivationState(bool newState) {}

        CameraMovementManipulator(
            const std::shared_ptr<VisCameraSettings>& visCameraSettings,
			CameraManipulatorMode mode);
        ~CameraMovementManipulator();

    protected:
        std::shared_ptr<VisCameraSettings> _visCameraSettings;
        float _translateSpeed, _orbitRotationSpeed, _wheelTranslateSpeed;
		CameraManipulatorMode _mode;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    bool CameraMovementManipulator::OnInputEvent(
        const PlatformRig::InputSnapshot& evnt, 
        const SceneEngine::IntersectionTestContext& hitTestContext,
        const SceneEngine::IIntersectionScene* hitTestScene)
    {
            //  This is a simple camera manipulator
            //  It should operate when the middle mouse button is down.
            //  We'd do max controls to start with:
            //      * no modifiers: translate along X, Y axis
            //      * alt: orbit around focus point
            //      * mouse wheel: translate in and out
            //  
            //  Note that blender controls are very similar...
            //      * no modifiers: orbit around focus point
            //      * shift: translate along X, Y axis
            //
            //  We could add some configuration options to make it a bit
            //  more generic.
            //
            //  It's hard to use the keyboard to move the camera around 
            //  because it tends to conflict with other key-binds.
            //  However, we could make a rule that keyboard input is 
            //  directed to the camera when the middle mouse button is down.
        if (!_visCameraSettings) { return false; }

		bool gotSomething = false;
        static auto ctrl = PlatformRig::KeyId_Make("control");
        if (evnt.IsHeld(ctrl) && evnt.IsPress_LButton() && hitTestScene) {
            auto worldSpaceRay = SceneEngine::IntersectionTestContext::CalculateWorldSpaceRay(
				AsCameraDesc(*_visCameraSettings), evnt._mousePosition, hitTestContext._viewportMins, hitTestContext._viewportMaxs);

            auto intr = hitTestScene->FirstRayIntersection(hitTestContext, worldSpaceRay);
            if (intr._type != 0)
                _visCameraSettings->_focus = intr._worldSpaceCollision;
			gotSomething = true;
        }

        auto cameraToWorld = MakeCameraToWorld(
			Normalize(_visCameraSettings->_focus -_visCameraSettings->_position),
			Float3(0.f, 0.f, 1.f), _visCameraSettings->_position);
		auto up = ExtractUp_Cam(cameraToWorld);
		auto right = ExtractRight_Cam(cameraToWorld);
		auto forward = ExtractForward_Cam(cameraToWorld);

		unsigned mainMouseButton = (_mode == CameraManipulatorMode::Max_MiddleButton) ? 2 : 1;
		if (evnt.IsHeld_MouseButton(mainMouseButton)) {
			static auto alt = PlatformRig::KeyId_Make("alt");
			static auto shift = PlatformRig::KeyId_Make("shift");
			enum ModifierMode
			{
				Translate, Orbit
			};
			ModifierMode modifierMode = Orbit;

			if (_mode == CameraManipulatorMode::Max_MiddleButton) {
				modifierMode = evnt.IsHeld(alt) ? Orbit : Translate;
			} else if (_mode == CameraManipulatorMode::Blender_RightButton) {
				modifierMode = evnt.IsHeld(shift) ? Translate : Orbit;
			}
                
			if (evnt._mouseDelta[0] || evnt._mouseDelta[1]) {
				if (modifierMode == Translate) {

					float distanceToFocus = Magnitude(_visCameraSettings->_focus -_visCameraSettings->_position);
					float speedScale = distanceToFocus * XlTan(0.5f * Deg2Rad(_visCameraSettings->_verticalFieldOfView));

						//  Translate the camera, but don't change forward direction
						//  Speed should be related to the distance to the focus point -- so that
						//  it works ok for both small models and large models.
					Float3 translation
						=   (speedScale * _translateSpeed *  evnt._mouseDelta[1]) * up
						+   (speedScale * _translateSpeed * -evnt._mouseDelta[0]) * right;

					_visCameraSettings->_position += translation;
					_visCameraSettings->_focus += translation;

				} else if (modifierMode == Orbit) {

						//  We're going to orbit around the "focus" point marked in the
						//  camera settings. Let's assume it's a reasonable point to orbit
						//  about.
						//
						//  We could also attempt to recalculate an orbit point based
						//  on a collision test against the scene.
						//
						//  Let's do the rotation using Spherical coordinates. This allows us
						//  to clamp the maximum pitch.
						//

					// Float4 plane = Expand(Float3(_visCameraSettings->_focus - _visCameraSettings->_position), 0.f);
					// float t = RayVsPlane(_visCameraSettings->_position, _visCameraSettings->_focus, plane);

					Float3 orbitCenter = _visCameraSettings->_focus; // _visCameraSettings->_position + t * (_visCameraSettings->_focus - _visCameraSettings->_position);
					auto spherical = CartesianToSpherical(orbitCenter - _visCameraSettings->_position);
					spherical[0] += evnt._mouseDelta[1] * _orbitRotationSpeed;
					spherical[0] = Clamp(spherical[0], gPI * 0.02f, gPI * 0.98f);
					spherical[1] -= evnt._mouseDelta[0] * _orbitRotationSpeed;
					_visCameraSettings->_position = orbitCenter - SphericalToCartesian(spherical);
					_visCameraSettings->_focus = orbitCenter;

				}
			}
			gotSomething = true;
		}

        if (evnt._wheelDelta) {
            float distanceToFocus = Magnitude(_visCameraSettings->_focus -_visCameraSettings->_position);

            float speedScale = distanceToFocus * XlTan(0.5f * Deg2Rad(_visCameraSettings->_verticalFieldOfView));
            auto movement = std::min(evnt._wheelDelta * speedScale * _wheelTranslateSpeed, distanceToFocus - 0.1f);

            Float3 translation = movement * forward;
            _visCameraSettings->_position += translation;
			gotSomething = true;
        }

        return gotSomething;
    }

    void CameraMovementManipulator::Render(
        RenderCore::IThreadContext&, 
        RenderCore::Techniques::ParsingContext&,
        RenderCore::Techniques::IPipelineAcceleratorPool&)
    {
        // we could draw some movement widgets here
    }

    const char* CameraMovementManipulator::GetName() const { return "Camera Movement"; }
    std::string CameraMovementManipulator::GetStatusText() const { return std::string(); }

    CameraMovementManipulator::CameraMovementManipulator(
        const std::shared_ptr<VisCameraSettings>& visCameraSettings,
		CameraManipulatorMode mode)
    : _visCameraSettings(visCameraSettings)
	, _mode(mode)
    {
        _translateSpeed = (1.f / 512.f);
        _orbitRotationSpeed = (1.f / 768.f) * gPI;
        _wheelTranslateSpeed = _translateSpeed;
    }

    CameraMovementManipulator::~CameraMovementManipulator()
    {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    std::shared_ptr<IManipulator> CreateCameraManipulator(
        const std::shared_ptr<VisCameraSettings>& visCameraSettings,
		CameraManipulatorMode mode)
    {
        return std::make_shared<CameraMovementManipulator>(visCameraSettings, mode);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    bool    ManipulatorStack::OnInputEvent(
		const PlatformRig::InputContext& context,
		const PlatformRig::InputSnapshot& evnt)
    {
		SceneEngine::IntersectionTestContext intersectionContext {
			AsCameraDesc(*_camera),
			context._viewMins, context._viewMaxs,
			_drawingApparatus };

        if (!_activeManipulators.empty()) {
            bool r = _activeManipulators[_activeManipulators.size()-1]->OnInputEvent(
                evnt, intersectionContext, _intersectionScene.get());
            if (!r)
                _activeManipulators.erase(_activeManipulators.begin() + (_activeManipulators.size()-1));
        } else {
			auto i = LowerBound(_registeredManipulators, CameraManipulator);
			if (i!=_registeredManipulators.end() && i->first == CameraManipulator)
				i->second->OnInputEvent(evnt, intersectionContext, _intersectionScene.get());
		}

        return false;
    }

    void    ManipulatorStack::Register(uint64_t id, std::shared_ptr<ToolsRig::IManipulator> manipulator)
    {
        auto i = LowerBound(_registeredManipulators, id);
        if (i!=_registeredManipulators.end() && i->first == id) {
            i->second = manipulator;
        } else {
            _registeredManipulators.insert(i, std::make_pair(id, std::move(manipulator)));
        }
    }

	void	ManipulatorStack::Set(const std::shared_ptr<SceneEngine::IIntersectionScene>& intersectionScene)
	{
		_intersectionScene = intersectionScene;
	}

    ManipulatorStack::ManipulatorStack(
        const std::shared_ptr<VisCameraSettings>& camera,
		const std::shared_ptr<RenderCore::Techniques::DrawingApparatus>& drawingApparatus)
    : _camera(camera)
	, _drawingApparatus(drawingApparatus)
    {}
    ManipulatorStack::~ManipulatorStack()
    {}
    
}

