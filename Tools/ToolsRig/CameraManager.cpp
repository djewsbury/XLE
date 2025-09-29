// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CameraManager.h"
#include "UnitCamera.h"
#include "VisualisationUtils.h"
#include "../../PlatformRig/InputContext.h"
#include "../../RenderCore/RenderUtils.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../Math/Transformations.h"
#include "../../Math/Geometry.h"

using namespace OSServices::Literals;

namespace ToolsRig { namespace Camera
{
		//
		//      Note -- Our camera coordinate space:
		//
		//      *	Right handed
		//      *	+X to the right
		//      *	+Y up
		//      *	-Z into the screen
		//

	static std::tuple<float, float, Float3> SlewUpdateInternal(const Slew& slew, float dt, const OSServices::InputSnapshot& input)
	{
		constexpr auto shift        = "shift"_key;
		constexpr auto ctrl         = "control"_key;
		constexpr auto forward      = "w"_key;
		constexpr auto back         = "s"_key;
		constexpr auto left         = "a"_key;
		constexpr auto right        = "d"_key;
		constexpr auto up           = "page up"_key;
		constexpr auto down         = "page down"_key;
		constexpr auto turnLeft     = "left"_key;
		constexpr auto turnRight    = "right"_key;
		constexpr auto turnUp       = "up"_key;
		constexpr auto turnDown     = "down"_key;

			// change move/turn speed
		bool fastMove = input.IsHeld(shift);
		bool slowMove = input.IsHeld(ctrl);
		float moveScale = fastMove ? slew._speedScale : (slowMove ? (1.f/slew._speedScale) : 1.f);

		float moveSpeedX = slew._translationSpeed * moveScale;
		float moveSpeedY = slew._translationSpeed * moveScale;
		float moveSpeedZ = slew._translationSpeed * moveScale;
		float yawSpeed   = slew._rotationSpeed;
		float pitchSpeed = slew._rotationSpeed;

			// panning & rotation
		Float3 deltaPos(0,0,0);
		float deltaCameraYaw = 0.f, deltaCameraPitch = 0.f;

			// move forward and sideways and up and down
		deltaPos[2] -= input.IsHeld(forward);
		deltaPos[2] += input.IsHeld(back);
		deltaPos[0] -= input.IsHeld(left);
		deltaPos[0] += input.IsHeld(right);
		deltaPos[1] += input.IsHeld(up);
		deltaPos[1] -= input.IsHeld(down);
		deltaPos[0] *= dt * moveSpeedX;
		deltaPos[1] *= dt * moveSpeedY;
		deltaPos[2] *= dt * moveSpeedZ;

		auto mouseX = input._mouseDelta[0], mouseY = input._mouseDelta[1];
		const bool rightButton      = input.IsHeld_RButton();
		if (rightButton) {
			float mouseSensitivity = -0.01f * std::max(0.01f, slew._mouseSensitivity);
			mouseSensitivity    *= gPI / 180.0f;
			deltaCameraYaw      +=  mouseX * mouseSensitivity; 
			deltaCameraPitch    +=  mouseY * mouseSensitivity;
		} else {
			deltaCameraYaw      += input.IsHeld(turnLeft);
			deltaCameraYaw      -= input.IsHeld(turnRight);
			deltaCameraPitch    += input.IsHeld(turnUp);
			deltaCameraPitch    -= input.IsHeld(turnDown);
			deltaCameraYaw      *= dt * yawSpeed;
			deltaCameraPitch    *= dt * pitchSpeed;
		}

		return { deltaCameraYaw, deltaCameraPitch, deltaPos };
	}

	void Slew::Update(Float4x4& cameraToWorld, float dt, const OSServices::InputSnapshot& input) const
	{
		Float3 deltaPos;
		float deltaCameraYaw, deltaCameraPitch;
		std::tie(deltaCameraYaw, deltaCameraPitch, deltaPos) = SlewUpdateInternal(*this, dt, input);

			// apply rotation
		static cml::EulerOrder eulerOrder = cml::euler_order_zxz;
		Float3 ypr = cml::matrix_to_euler<Float4x4, Float4x4::value_type>(cameraToWorld, eulerOrder);
		ypr[2] += deltaCameraYaw;
		ypr[1] += deltaCameraPitch;
		const float safetyThreshold = 0.01f;
		ypr[1] = Clamp(ypr[1], safetyThreshold, gPI - safetyThreshold);

		Float3 camPos = TransformPoint(cameraToWorld, deltaPos);
		Float3x3 rotationPart;
		cml::matrix_rotation_euler(rotationPart, ypr[0], ypr[1], ypr[2], eulerOrder);
		cameraToWorld = Expand(rotationPart, camPos);
	}

	void Slew::Update(VisCameraSettings& camera, float dt, const OSServices::InputSnapshot& input) const
	{
		Float3 deltaPos;
		float deltaCameraYaw, deltaCameraPitch;
		std::tie(deltaCameraYaw, deltaCameraPitch, deltaPos) = SlewUpdateInternal(*this, dt, input);

		// deltaCameraYaw & pitch modify the position of the focus, relative to the position
		auto spherical = CartesianToSpherical(camera._focus - camera._position);
		spherical[0] -= deltaCameraPitch;
		spherical[1] += deltaCameraYaw;
		const float safetyThreshold = 0.01f;
		spherical[0] = Clamp(spherical[0], safetyThreshold, gPI - safetyThreshold);
		camera._focus = camera._position + SphericalToCartesian(spherical);

		auto cameraToWorld = MakeCameraToWorld(
			Normalize(camera._focus - camera._position),
			Float3(0.f, 0.f, 1.f), camera._position);

		auto trans = TransformDirectionVector(cameraToWorld, Float3(deltaPos));
		camera._position += trans;
		camera._focus += trans;
	}

	static std::tuple<float, float, Float3> OrbitUpdateInternal(const Orbit& orbit, float dt, const OSServices::InputSnapshot& input, float distanceToFocus)
	{
		constexpr auto shift        = "shift"_key;
		constexpr auto forward      = "w"_key;
		constexpr auto back         = "s"_key;
		constexpr auto left         = "a"_key;
		constexpr auto right        = "d"_key;
		constexpr auto up           = "page up"_key;
		constexpr auto down         = "page down"_key;

		bool fastMove     = input.IsHeld(shift);
		float moveScale   = fastMove ? orbit._speedScale : 1.f;
		moveScale        *= std::max(0.2f, distanceToFocus);

		float deltaRotationX = 0.f, deltaRotationY = 0.f;
		Float3 deltaPos(0,0,0);

			// move forward and sideways and up and down
		deltaPos[2] += input.IsHeld(forward);
		deltaPos[2] -= input.IsHeld(back);
		deltaPos[0] -= input.IsHeld(left);
		deltaPos[0] += input.IsHeld(right);
		deltaPos[1] += input.IsHeld(up);
		deltaPos[1] -= input.IsHeld(down);
		deltaPos[0] *= dt * moveScale;
		deltaPos[1] *= dt * moveScale;
		deltaPos[2] *= dt * moveScale;

		auto mouseX = input._mouseDelta[0], mouseY = input._mouseDelta[1];
		const bool rightButton      = input.IsHeld_RButton();
		if (rightButton) {
			float mouseSensitivity = -0.01f * std::max(0.01f, orbit._mouseSensitivity);
			mouseSensitivity    *= gPI / 180.0f;
			deltaRotationX      +=  mouseX * mouseSensitivity; 
			deltaRotationY      +=  mouseY * mouseSensitivity;
		}

		return { deltaRotationX, deltaRotationY, deltaPos };
	}

	void Orbit::Update(Float4x4& cameraToWorld, Float3& focusPoint, float dt, const OSServices::InputSnapshot& input) const
	{
		float deltaRotationX, deltaRotationY;
		Float3 deltaPos;
		std::tie(deltaRotationX, deltaRotationY, deltaPos) = OrbitUpdateInternal(*this, dt, input, Magnitude(ExtractTranslation(cameraToWorld) - focusPoint));

		Float3 rotYAxis = Truncate(cameraToWorld * Float4(1.0f, 0.f, 0.f, 0.f));

		Combine_IntoLHS(cameraToWorld, Float3(-focusPoint));
		cameraToWorld = Combine(cameraToWorld, MakeRotationMatrix(rotYAxis, deltaRotationY));
		Combine_IntoLHS(cameraToWorld, RotationZ(deltaRotationX));
		Combine_IntoLHS(cameraToWorld, focusPoint);
		Combine_IntoLHS(cameraToWorld, Float3(deltaPos[2] * Normalize(focusPoint - ExtractTranslation(cameraToWorld))));

		auto flatCameraRight = ExtractRight_Cam(cameraToWorld);
		flatCameraRight[2] = 0.f;
		if (!Normalize_Checked(&flatCameraRight, flatCameraRight))
			flatCameraRight = Float3{0,1,0};

		Float3 flatCameraForward = ExtractForward_Cam(cameraToWorld);
		flatCameraForward[2] = 0.f;
		if (!Normalize_Checked(&flatCameraForward, flatCameraForward))
			flatCameraForward = Float3{1,0,0};        // happens when facing directly up

		Float3 cameraFocusDrift = deltaPos[0] * flatCameraRight + deltaPos[2] * flatCameraForward + Float3{0,0,deltaPos[1]};
		Combine_IntoLHS(cameraToWorld, cameraFocusDrift);
		focusPoint += cameraFocusDrift;
	}

	void Orbit::Update(VisCameraSettings& camera, float dt, const OSServices::InputSnapshot& input) const
	{
		float deltaRotationX, deltaRotationY;
		Float3 deltaPos;
		std::tie(deltaRotationX, deltaRotationY, deltaPos) = OrbitUpdateInternal(*this, dt, input, Magnitude(camera._focus - camera._position));

		auto spherical = CartesianToSpherical(camera._position - camera._focus);
		spherical[1] += deltaRotationX;
		spherical[0] += deltaRotationY;
		const float safetyThreshold = 0.01f;
		spherical[0] = Clamp(spherical[0], safetyThreshold, gPI - safetyThreshold);
		camera._position = camera._focus + SphericalToCartesian(spherical);

		Float3 flatCameraRight = Cross(camera._focus - camera._position, Float3{0,0,1});
		flatCameraRight[2] = 0.f;
		if (!Normalize_Checked(&flatCameraRight, flatCameraRight))
			flatCameraRight = Float3{0,1,0};        // happens when facing directly up

		Float3 flatCameraForward = camera._focus - camera._position;
		flatCameraForward[2] = 0.f;
		if (!Normalize_Checked(&flatCameraForward, flatCameraForward))
			flatCameraForward = Float3{1,0,0};        // happens when facing directly up

		Float3 cameraFocusDrift = deltaPos[0] * flatCameraRight + deltaPos[2] * flatCameraForward + Float3{0,0,deltaPos[1]};
		camera._position += cameraFocusDrift;
		camera._focus += cameraFocusDrift;
	}

	void OrthogonalFlatCam::Update(VisCameraSettings& camera, const OSServices::InputSnapshot& input, const Float2& projSpaceMouseOver) const
	{
		assert(camera._projection == VisCameraSettings::Projection::Orthogonal);

		unsigned mainMouseButton = (_mode == Manipulator::Mode::Max_MiddleButton) ? 2 : 1;
		if (input.IsHeld_MouseButton(mainMouseButton)) {

			constexpr auto alt = "alt"_key;
			constexpr auto shift = "shift"_key;
			enum ModifierMode
			{
				Translate, Orbit
			};
			ModifierMode modifierMode = Orbit;

			if (_mode == Manipulator::Mode::Max_MiddleButton) {
				modifierMode = input.IsHeld(alt) ? Orbit : Translate;
			} else if (_mode == Manipulator::Mode::Blender_RightButton) {
				modifierMode = input.IsHeld(shift) ? Translate : Orbit;
			} else if (_mode == Manipulator::Mode::OnlyTranslation) {
				modifierMode = Translate;
			}

			if (input._mouseDelta[0] || input._mouseDelta[1]) {
				if (modifierMode == Translate) {
					Float3 cameraForward = camera._focus - camera._position;
					if (!Normalize_Checked(&cameraForward, cameraForward))
						cameraForward = Float3{0,-1,0};

					Float3 cameraRight = Cross(cameraForward, Float3{0,0,1});
					if (!Normalize_Checked(&cameraRight, cameraRight))
						cameraRight = Float3{1,0,0};

					Float3 cameraUp = Cross(cameraRight, cameraForward);
					if (!Normalize_Checked(&cameraUp, cameraUp))
						cameraUp = Float3{0,0,1};

					float size = std::abs(camera._top - camera._bottom);
					auto translation
						=  cameraRight * input._mouseDelta[0] * size * 0.1f * _translationSpeed
						+  cameraUp * input._mouseDelta[1] * size * 0.1f * _translationSpeed;
					camera._position += translation;
					camera._focus += translation;
				}
			}

		}

		if (input._wheelDelta) {
			// zoom in/out so that the projSpaceMouseOver stays in the same place in proj space
			float scale = std::exp(-input._wheelDelta / (4.f * 180.f));
			camera._left = LinearInterpolate(projSpaceMouseOver[0], camera._left, scale);
			camera._right = LinearInterpolate(projSpaceMouseOver[0], camera._right, scale);
			camera._top = LinearInterpolate(projSpaceMouseOver[1], camera._top, scale);
			camera._bottom = LinearInterpolate(projSpaceMouseOver[1], camera._bottom, scale);
		}
	}

	void UnitCam::Update(VisCameraSettings& camera, const Float3x4& playerCharacterLocalToWorld, float dt, const OSServices::InputSnapshot& input) const
	{
		Camera::ClientUnit clientUnit;
		clientUnit._localToWorld = playerCharacterLocalToWorld;

		auto camResult = _unitCamera->UpdateUnitCamera(dt, &clientUnit, input);
		assert(ExtractTranslation(camResult._cameraToWorld)[0] == ExtractTranslation(camResult._cameraToWorld)[0]);
		assert(ExtractTranslation(camResult._cameraToWorld)[1] == ExtractTranslation(camResult._cameraToWorld)[1]);
		assert(ExtractTranslation(camResult._cameraToWorld)[2] == ExtractTranslation(camResult._cameraToWorld)[2]);
		auto cameraToWorld = AsFloat4x4(camResult._cameraToWorld);

			//  Convert from object-to-world transform into
			//  camera-to-world transform
		std::swap(cameraToWorld(0,1), cameraToWorld(0,2));
		std::swap(cameraToWorld(1,1), cameraToWorld(1,2));
		std::swap(cameraToWorld(2,1), cameraToWorld(2,2));
		cameraToWorld(0,2) = -cameraToWorld(0,2);
		cameraToWorld(1,2) = -cameraToWorld(1,2);
		cameraToWorld(2,2) = -cameraToWorld(2,2);

		camera._position = ExtractTranslation(cameraToWorld);
		auto forward = ExtractForward_Cam(cameraToWorld);
		auto pcPosition = ExtractTranslation(playerCharacterLocalToWorld), pcUp = ExtractUp(playerCharacterLocalToWorld);
		float muA, muB;
		// find a position near to the character central axis to be considered the "focus"
		if (ShortestSegmentBetweenLines(muA, muB, {camera._position, camera._position+forward}, {pcPosition, pcPosition+pcUp})) {
			muA = std::max(muA, 0.01f);
			camera._focus = camera._position + muA * forward;
		} else {
			camera._focus = camera._position + forward;
		}

		camera._verticalFieldOfView = camResult._fov;
	}

	void UnitCam::Update(Float4x4& cameraToWorld, const Float3x4& playerCharacterLocalToWorld, float dt, const OSServices::InputSnapshot& input) const
	{
		Camera::ClientUnit clientUnit;
		clientUnit._localToWorld = playerCharacterLocalToWorld;

		auto camResult = _unitCamera->UpdateUnitCamera(dt, &clientUnit, input);
		assert(ExtractTranslation(camResult._cameraToWorld)[0] == ExtractTranslation(camResult._cameraToWorld)[0]);
		assert(ExtractTranslation(camResult._cameraToWorld)[1] == ExtractTranslation(camResult._cameraToWorld)[1]);
		assert(ExtractTranslation(camResult._cameraToWorld)[2] == ExtractTranslation(camResult._cameraToWorld)[2]);
		cameraToWorld = AsFloat4x4(camResult._cameraToWorld);

			//  Convert from object-to-world transform into
			//  camera-to-world transform
		std::swap(cameraToWorld(0,1), cameraToWorld(0,2));
		std::swap(cameraToWorld(1,1), cameraToWorld(1,2));
		std::swap(cameraToWorld(2,1), cameraToWorld(2,2));
		cameraToWorld(0,2) = -cameraToWorld(0,2);
		cameraToWorld(1,2) = -cameraToWorld(1,2);
		cameraToWorld(2,2) = -cameraToWorld(2,2);
	}

	void UnitCam::Initialize(float charactersScale)
	{
		_unitCamera = CreateUnitCamManager(charactersScale);
	}

	UnitCam::UnitCam() = default;
	UnitCam::~UnitCam() = default;
	UnitCam::UnitCam(UnitCam&&) = default;
	UnitCam& UnitCam::operator=(UnitCam&&) = default;

	void Manipulator::Update(VisCameraSettings& camera, float dt, const OSServices::InputSnapshot& input) const
	{
		unsigned mainMouseButton = (_mode == Mode::Max_MiddleButton) ? 2 : 1;
		if (input.IsHeld_MouseButton(mainMouseButton)) {
			constexpr auto alt = "alt"_key;
			constexpr auto shift = "shift"_key;
			enum ModifierMode
			{
				Translate, Orbit
			};
			ModifierMode modifierMode = Orbit;

			if (_mode == Mode::Max_MiddleButton) {
				modifierMode = input.IsHeld(alt) ? Orbit : Translate;
			} else if (_mode == Mode::Blender_RightButton) {
				modifierMode = input.IsHeld(shift) ? Translate : Orbit;
			} else if (_mode == Mode::OnlyTranslation) {
				modifierMode = Translate;
			}
				
			if (input._mouseDelta[0] || input._mouseDelta[1]) {
				if (modifierMode == Translate) {

					float distanceToFocus = Magnitude(camera._focus - camera._position);
					float speedScale = distanceToFocus * XlTan(0.5f * camera._verticalFieldOfView);

						//  Translate the camera, but don't change forward direction
						//  Speed should be related to the distance to the focus point -- so that
						//  it works ok for both small models and large models.
					Float3 cameraRight = Cross(camera._focus - camera._position, Float3{0,0,1});
					if (!Normalize_Checked(&cameraRight, cameraRight))
						cameraRight = Float3{0,1,0};        // happens when facing directly up

					Float3 cameraUp = Cross(cameraRight, camera._focus - camera._position);
					if (!Normalize_Checked(&cameraUp, cameraUp))
						cameraUp = Float3{0,0,1};

					Float3 translation
						=   (speedScale * _translateSpeed *  input._mouseDelta[1]) * cameraUp
						+   (speedScale * _translateSpeed * -input._mouseDelta[0]) * cameraRight;

					camera._position += translation;
					camera._focus += translation;

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

					Float3 orbitCenter = camera._focus;
					auto spherical = CartesianToSpherical(orbitCenter - camera._position);
					spherical[0] += input._mouseDelta[1] * _orbitRotationSpeed;
					spherical[0] = Clamp(spherical[0], gPI * 0.02f, gPI * 0.98f);
					spherical[1] -= input._mouseDelta[0] * _orbitRotationSpeed;
					camera._position = orbitCenter - SphericalToCartesian(spherical);
					camera._focus = orbitCenter;

				}
			}
		}

		if (input._wheelDelta) {
			float distanceToFocus = Magnitude(camera._focus -camera._position);

			float speedScale = distanceToFocus * XlTan(0.5f * camera._verticalFieldOfView);
			auto movement = std::min(input._wheelDelta * speedScale * _wheelTranslateSpeed, distanceToFocus - 0.1f);

			Float3 translation = movement * Normalize(camera._focus - camera._position);
			camera._position += translation;
		}
	}

	void Manipulator::Update(Float4x4& cameraToWorld, float& fov, float distanceToFocus, float dt, const OSServices::InputSnapshot& input) const
	{
		unsigned mainMouseButton = (_mode == Mode::Max_MiddleButton) ? 2 : 1;
		if (input.IsHeld_MouseButton(mainMouseButton)) {
			constexpr auto alt = "alt"_key;
			constexpr auto shift = "shift"_key;
			enum ModifierMode
			{
				Translate, Orbit
			};
			ModifierMode modifierMode = Orbit;

			if (_mode == Mode::Max_MiddleButton) {
				modifierMode = input.IsHeld(alt) ? Orbit : Translate;
			} else if (_mode == Mode::Blender_RightButton) {
				modifierMode = input.IsHeld(shift) ? Translate : Orbit;
			} else if (_mode == Mode::OnlyTranslation) {
				modifierMode = Translate;
			}
				
			if (input._mouseDelta[0] || input._mouseDelta[1]) {
				if (modifierMode == Translate) {

					float speedScale = distanceToFocus * XlTan(0.5f * fov);

					Float3 cameraRight = ExtractRight_Cam(cameraToWorld);
					Float3 cameraUp = ExtractUp_Cam(cameraToWorld);
					Float3 translation
						=   (speedScale * _translateSpeed *  input._mouseDelta[1]) * cameraUp
						+   (speedScale * _translateSpeed * -input._mouseDelta[0]) * cameraRight;

					SetTranslation(cameraToWorld, ExtractTranslation(cameraToWorld) + translation);

				} else if (modifierMode == Orbit) {

					Float3 orbitCenter = ExtractTranslation(cameraToWorld) + distanceToFocus * ExtractForward_Cam(cameraToWorld);
					auto spherical = CartesianToSpherical(orbitCenter - ExtractTranslation(cameraToWorld));
					spherical[0] += input._mouseDelta[1] * _orbitRotationSpeed;
					spherical[0] = Clamp(spherical[0], gPI * 0.02f, gPI * 0.98f);
					spherical[1] -= input._mouseDelta[0] * _orbitRotationSpeed;
					auto forward = SphericalToCartesian(spherical);
					cameraToWorld = MakeCameraToWorld(forward, ExtractUp_Cam(cameraToWorld), orbitCenter-forward);

				}
			}
		}

		if (input._wheelDelta) {
			float speedScale = distanceToFocus * XlTan(0.5f * fov);
			auto movement = std::min(input._wheelDelta * speedScale * _wheelTranslateSpeed, distanceToFocus - 0.1f);

			Float3 translation = movement * ExtractForward_Cam(cameraToWorld);
			SetTranslation(cameraToWorld, ExtractTranslation(cameraToWorld) + translation);
		}
	}

	static void SpecialCharacterCamFn(const CharacterCam& props, Float3& spherical, Float3& focus, float& zoomFactor, const OSServices::InputSnapshot& input)
	{
		zoomFactor += input._wheelDelta / (16.f * 180.f);
		zoomFactor = std::max(0.0f, zoomFactor);

		constexpr auto shift = "shift"_key;
		if (input.IsHeld_RButton()) {
			spherical[1] += input._mouseDelta[0] * props._rotationSpeed;
			if (input.IsHeld(shift)) {
				focus[2] += input._mouseDelta[1] * props._translationSpeed;
			} else {
				spherical[0] += input._mouseDelta[1] * props._rotationSpeed;
				spherical[0] = Clamp(spherical[0], 0.01f, gPI - 0.01f);
			}
		} else if (input.IsHeld_LButton()) {
			spherical[1] += input._mouseDelta[0] * props._rotationSpeed;
			focus[2] += input._mouseDelta[1] * props._translationSpeed;
		}
	}

	void CharacterCam::Update(VisCameraSettings& camera, float dt, const OSServices::InputSnapshot& input) const
	{
		const float fovMin = 2.f * gPI / 180.f;
		const float fovMax = 80.f * gPI / 180.f;

		auto spherical = CartesianToSpherical(camera._position - camera._focus);
		assert(camera._projection == VisCameraSettings::Projection::Perspective);
		float a = Clamp((camera._verticalFieldOfView - fovMax) / (fovMin - fovMax), 0.f, 1.f);
		float zoomFactor = (std::exp(a) - 1.f) / (gE - 1.f);

		SpecialCharacterCamFn(*this, spherical, camera._focus, zoomFactor, input);

		camera._position = camera._focus + SphericalToCartesian(spherical);

		float f = Clamp(std::log(zoomFactor * gE - zoomFactor + 1.0f), 0.f, 1.0f);
		camera._verticalFieldOfView = LinearInterpolate(fovMax, fovMin, f);
	}

	void CharacterCam::Update(Float4x4& cameraToWorld, float& fov, float distanceToFocus, float dt, const OSServices::InputSnapshot& input) const
	{
		const float fovMin = 2.f * gPI / 180.f;
		const float fovMax = 80.f * gPI / 180.f;

		auto offsetToFocus = distanceToFocus * ExtractForward_Cam(cameraToWorld);
		auto spherical = CartesianToSpherical(-offsetToFocus);
		Float3 focus = ExtractTranslation(cameraToWorld) + offsetToFocus;
		float a = Clamp((fov - fovMax) / (fovMin - fovMax), 0.f, 1.f);
		float zoomFactor = (std::exp(a) - 1.f) / (gE - 1.f);

		SpecialCharacterCamFn(*this, spherical, focus, zoomFactor, input);

		auto position = focus + SphericalToCartesian(spherical);
		cameraToWorld = MakeCameraToWorld(focus-position, ExtractUp_Cam(cameraToWorld), position);

		float f = Clamp(std::log(zoomFactor * gE - zoomFactor + 1.0f), 0.f, 1.0f);
		fov = LinearInterpolate(fovMax, fovMin, f);
	}

	void CameraInputHandler::Update(const Float3x4& playerCharacterLocalToWorld, float dt, const OSServices::InputSnapshot& input)
	{
		constexpr auto tab = "tab"_key;
		constexpr auto shift = "shift"_key;

		if (input.IsPress(tab))
			_mode = (_mode+1)%2;

		if (_mode==0) {
			if (!input.IsHeld(shift))
				_unitCam.Update(*_camera, playerCharacterLocalToWorld, dt, input);
		} else if (_mode==1)
			_slew.Update(*_camera, dt, input);
	}

	CameraInputHandler::CameraInputHandler(std::shared_ptr<VisCameraSettings> camera, float charactersScale)
	: _camera(std::move(camera))
	{
		_unitCam._unitCamera = std::make_unique<UnitCamManager>(charactersScale);
		_unitCam._unitCamera->InitUnitCamera();
	}

	CameraInputHandler::~CameraInputHandler() {}

	std::unique_ptr<UnitCamManager> CreateUnitCamManager(float charactersScale)
	{
		auto result = std::make_unique<UnitCamManager>(charactersScale);
		result->InitUnitCamera();
		return result;
	}


}}

