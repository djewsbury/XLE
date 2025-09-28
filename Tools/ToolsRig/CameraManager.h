// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Math/Matrix.h"

namespace RenderCore { namespace Techniques { class CameraDesc; } }
namespace PlatformRig { enum class ProcessInputResult; }

namespace OSServices { class InputSnapshot; }
namespace ToolsRig { class VisCameraSettings; }
namespace ToolsRig { namespace Camera
{
	class UnitCamManager;
	using ProcessInputResult = PlatformRig::ProcessInputResult;

	struct Slew
	{
		void Update(VisCameraSettings& camera, float dt, const OSServices::InputSnapshot& input) const;
		void Update(Float4x4& cameraToWorld, float dt, const OSServices::InputSnapshot& input) const;

		float _mouseSensitivity = 20.f;
		float _speedScale = 20.f;
		float _translationSpeed = 10.f;
		float _rotationSpeed = gPI * .5f;
	};

	struct Orbit
	{
		void Update(VisCameraSettings& camera, float dt, const OSServices::InputSnapshot& input) const;
		void Update(Float4x4& cameraToWorld, Float3& focusPoint, float dt, const OSServices::InputSnapshot& input) const;

		float _mouseSensitivity = 20.f;
		float _speedScale = 20.f;
	};

	struct UnitCam
	{
		void Update(VisCameraSettings& camera, const Float3x4& playerCharacterLocalToWorld, float dt, const OSServices::InputSnapshot& input) const;
		void Update(Float4x4& cameraToWorld, const Float3x4& playerCharacterLocalToWorld, float dt, const OSServices::InputSnapshot& input) const;

		std::unique_ptr<UnitCamManager> _unitCamera;
		void Initialize(float charactersScale);

		UnitCam();
		~UnitCam();
		UnitCam(UnitCam&&);
		UnitCam& operator=(UnitCam&&);
	};

	struct Manipulator
	{
		void Update(VisCameraSettings& camera, float dt, const OSServices::InputSnapshot& input) const;
		void Update(Float4x4& cameraToWorld, float& fov, float distanceToFocus, float dt, const OSServices::InputSnapshot& input) const;

		enum class Mode { Max_MiddleButton, Blender_RightButton, OnlyTranslation };
		Mode _mode = Mode::Blender_RightButton;

		float _translateSpeed = 1.f / 512.f;
		float _orbitRotationSpeed = (1.f / 768.f) * gPI;
		float _wheelTranslateSpeed = 1.f / 512.f;
		float _wheelOrthoWindowSpeed = 1.f / 512.f;
	};

	struct CharacterCam
	{
		void Update(VisCameraSettings& camera, float dt, const OSServices::InputSnapshot& input) const;
		void Update(Float4x4& cameraToWorld, float& fov, float distanceToFocus, float dt, const OSServices::InputSnapshot& input) const;

		float _rotationSpeed = -gPI / 1000.0f;
		float _translationSpeed = 0.01f;
	};

	struct OrthogonalFlatCam
	{
		void Update(VisCameraSettings& camera, const OSServices::InputSnapshot& input, const Float2& projSpaceMouseOver) const;

		float _translationSpeed = 0.01f;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class CameraInputHandler
	{
	public:
		void Update(float dt, const OSServices::InputSnapshot& input);
		void Update(const Float3x4& playerCharacterLocalToWorld, float dt, const OSServices::InputSnapshot& input);

		std::shared_ptr<VisCameraSettings> GetCamera() { return _camera; }
		
		CameraInputHandler(std::shared_ptr<VisCameraSettings> camera, float characterScale);
		~CameraInputHandler();
	protected:
		std::shared_ptr<VisCameraSettings> _camera;
		UnitCam _unitCam;
		Orbit _orbit;
		Slew _slew;
		unsigned _mode = 0;
	};

	std::unique_ptr<UnitCamManager> CreateUnitCamManager(float charactersScale);

}}

