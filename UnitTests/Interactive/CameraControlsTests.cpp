// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InteractiveTestHelper.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../PlatformRig/InputContext.h"
#include "../../RenderCore/Techniques/RenderPassUtils.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderOverlays/OverlayContext.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/OverlayApparatus.h"
#include "../../RenderOverlays/SimpleVisualization.h"
#include "../../RenderOverlays/DrawText.h"
#include "../../Tools/ToolsRig/CameraManager.h"
#include "../../Tools/ToolsRig/UnitCamera.h"
#include "../../Tools/ToolsRig/VisualisationUtils.h"
#include "../../Math/Transformations.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace PlatformRig::Literals;

namespace UnitTests
{
	TEST_CASE( "CameraControlsTest", "[math]" )
	{
		using namespace RenderCore;

		auto testHelper = CreateInteractiveTestHelper(IInteractiveTestHelper::EnabledComponents::RenderCoreTechniques);

		class CameraControlsTestOverlay : public IInteractiveTestOverlay
		{
		public:
			virtual void Render(
				RenderCore::Techniques::ParsingContext& parserContext,
				IInteractiveTestHelper& testHelper) override
			{
				RenderOverlays::DrawGrid(*testHelper.GetOverlayApparatus()->_immediateDrawables, parserContext);

				auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(
					parserContext.GetThreadContext(), *testHelper.GetOverlayApparatus());

				std::tuple<Float3, Float3> object { Float3{-.5f, -.5f, 0.f}, Float3{.5f, .5f, 1.8f} };
				RenderOverlays::DebuggingDisplay::DrawBoundingBox(
					*overlayContext, object,
					Identity<Float3x4>(),
					RenderOverlays::ColorB::White);

				const char* modetxt = "<<unknown>>";
				switch (_mode) {
				case 0: modetxt = "Slew to VisCamSettings"; break;
				case 1: modetxt = "Slew to Float4x4"; break;
				case 2: modetxt = "Orbit to VisCamSettings"; break;
				case 3: modetxt = "Orbit to Float4x4"; break;
				case 4: modetxt = "UnitCam to VisCamSettings"; break;
				case 5: modetxt = "UnitCam to Float4x4"; break;
				case 6: modetxt = "Manipulator to VisCamSettings"; break;
				case 7: modetxt = "Manipulator to Float4x4"; break;
				case 8: modetxt = "Character to VisCamSettings"; break;
				case 9: modetxt = "Character to Float4x4"; break;
				}

				RenderOverlays::DrawText{}.FormatAndDraw(*overlayContext, RenderOverlays::Rect{0,0,512,512}, "Mode: %s", modetxt);

				auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext, LoadStore::Clear);
				RenderOverlays::ExecuteDraws(
					parserContext, rpi,
					*testHelper.GetOverlayApparatus());
			}

			ToolsRig::Camera::Slew _slew;
			ToolsRig::Camera::Orbit _orbit;
			ToolsRig::Camera::UnitCam _unitCam;
			ToolsRig::Camera::Manipulator _manipulator;
			ToolsRig::Camera::CharacterCam _character;
			unsigned _mode = 0;

			ToolsRig::VisCameraSettings _visCamSettings;

			RenderCore::Techniques::CameraDesc _outputCamera;
			virtual bool OnInputEvent(
				const PlatformRig::InputContext& context,
				const OSServices::InputSnapshot& evnt,
				IInteractiveTestHelper& testHelper) override
			{
				constexpr auto tab = "tab"_key;
				if (evnt.IsPress(tab)) {
					_mode = (_mode+1)%10;
				} else if (evnt.IsPress("r"_key)) {
					_visCamSettings._position = Float3{0.0f, 5.0f, 3.5f};
					_visCamSettings._focus = Float3{0.f, 0.f, 1.8f/2.f};
					_outputCamera = ToolsRig::AsCameraDesc(_visCamSettings);
				}

				const float dt = 1.0f / 60.f;
				if (_mode == 0) {
					_slew.Update(_visCamSettings, dt, evnt);
				} else if (_mode == 1) {
					auto camDesc = ToolsRig::AsCameraDesc(_visCamSettings);
					_slew.Update(camDesc._cameraToWorld, dt, evnt);
					float originalFocusDistance = Magnitude(_visCamSettings._focus - _visCamSettings._position);
					_visCamSettings = ToolsRig::AsVisCameraSettings(camDesc);
					_visCamSettings._focus = _visCamSettings._position + Normalize(_visCamSettings._focus - _visCamSettings._position) * originalFocusDistance;
				} else if (_mode == 2) {
					_orbit.Update(_visCamSettings, dt, evnt);
				} else if (_mode == 3) {
					auto camDesc = ToolsRig::AsCameraDesc(_visCamSettings);
					_orbit.Update(camDesc._cameraToWorld, _visCamSettings._focus, dt, evnt);
					auto originalFocus = _visCamSettings._focus;
					_visCamSettings = ToolsRig::AsVisCameraSettings(camDesc);
					_visCamSettings._focus = _visCamSettings._focus;
				} else if (_mode == 4) {
					_unitCam.Update(_visCamSettings, Identity<Float3x4>(), dt, evnt);
				} else if (_mode == 5) {
					auto camDesc = ToolsRig::AsCameraDesc(_visCamSettings);
					_unitCam.Update(camDesc._cameraToWorld, Identity<Float3x4>(), dt, evnt);
					float originalFocusDistance = Magnitude(_visCamSettings._focus - _visCamSettings._position);
					_visCamSettings = ToolsRig::AsVisCameraSettings(camDesc);
					_visCamSettings._focus = _visCamSettings._position + Normalize(_visCamSettings._focus - _visCamSettings._position) * originalFocusDistance;
				} else if (_mode == 6) {
					_manipulator.Update(_visCamSettings, dt, evnt);
				} else if (_mode == 7) {
					auto camDesc = ToolsRig::AsCameraDesc(_visCamSettings);
					float originalFocusDistance = Magnitude(_visCamSettings._focus - _visCamSettings._position);
					_manipulator.Update(camDesc._cameraToWorld, camDesc._verticalFieldOfView, originalFocusDistance, dt, evnt);
					_visCamSettings = ToolsRig::AsVisCameraSettings(camDesc);
					_visCamSettings._focus = _visCamSettings._position + Normalize(_visCamSettings._focus - _visCamSettings._position) * originalFocusDistance;
				} else if (_mode == 8) {
					_character.Update(_visCamSettings, dt, evnt);
				} else if (_mode == 9) {
					auto camDesc = ToolsRig::AsCameraDesc(_visCamSettings);
					float originalFocusDistance = Magnitude(_visCamSettings._focus - _visCamSettings._position);
					_character.Update(camDesc._cameraToWorld, camDesc._verticalFieldOfView, originalFocusDistance, dt, evnt);
					_visCamSettings = ToolsRig::AsVisCameraSettings(camDesc);
					_visCamSettings._focus = _visCamSettings._position + Normalize(_visCamSettings._focus - _visCamSettings._position) * originalFocusDistance;
				}
				
				_outputCamera = ToolsRig::AsCameraDesc(_visCamSettings);
				return true;
			}

			CameraControlsTestOverlay()
			{
				_visCamSettings._position = Float3{0.0f, 5.0f, 3.5f};
				_visCamSettings._focus = Float3{0.f, 0.f, 1.8f/2.f};
				_outputCamera = ToolsRig::AsCameraDesc(_visCamSettings);

				_unitCam._unitCamera = std::make_unique<ToolsRig::Camera::UnitCamManager>(1.f);
				_unitCam._unitCamera->InitUnitCamera();
			}
		};

		auto tester = std::make_shared<CameraControlsTestOverlay>();
		testHelper->Run(tester->_outputCamera, tester);
	}
}
