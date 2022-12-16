// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DisplaySettingsDisplay.h"
#include "../OverlappedWindow.h"
#include "../../OSServices/DisplaySettings.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../Assets/Marker.h"
#include "../../Utility/StringFormat.h"

namespace PlatformRig { namespace Overlays
{
	using namespace RenderOverlays;
	using namespace RenderOverlays::DebuggingDisplay;

	static void DrawHeading(IOverlayContext& context, Layout& layout, RenderOverlays::Font& font, StringSection<> msg)
	{
		const unsigned headerLineHeight = 30;
		const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };
		auto allocation = layout.AllocateFullWidth(headerLineHeight);
		FillRectangle(context, allocation, titleBkground);
		allocation._topLeft[0] += 8;
		DrawText()
			.Font(font)
			.Color({ 191, 123, 0 })
			.Alignment(RenderOverlays::TextAlignment::Left)
			.Flags(RenderOverlays::DrawTextFlags::Shadow)
			.Draw(context, allocation, msg);
	}

	class DisplaySettingsDisplay : public IWidget
	{
	public:
		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
		{
			const unsigned lineHeight = 20;

			auto* headingFont = _headingFont->TryActualize();
			if (!headingFont) return;

			DrawHeading(context, layout, **headingFont, "Active Monitor");

			char buffer[256];
			auto monitors = _dispSettings->GetMonitors();
			std::pair<OSServices::DisplaySettingsManager::ModeDesc, bool> currentMode;
			if (_activeMonitorId < monitors.size()) {
				DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), "Name: " + monitors[_activeMonitorId]._friendlyName);
				DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), StringMeldInPlace(buffer) << "AdapterId: " << monitors[_activeMonitorId]._adapter << " (" << _dispSettings->GetAdapters()[monitors[_activeMonitorId]._adapter]._friendlyName << ")");
				DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), StringMeldInPlace(buffer) << "LocallyUniqueId: 0x" << std::hex << monitors[_activeMonitorId]._locallyUniqueId);
				DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), StringMeldInPlace(buffer) << "HDR: " << (monitors[_activeMonitorId]._hdrSupported ? "supported" : "unsupported"));
				auto geo = _dispSettings->GetDesktopGeometryForMonitor(_activeMonitorId);
				DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), StringMeldInPlace(buffer) << "Geometry X: " << geo._x << ", Y: " << geo._y << " Width: " << geo._width << ", Geometry Height: " << geo._height);
				currentMode = _dispSettings->GetCurrentMode(_activeMonitorId);
				DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), StringMeldInPlace(buffer) << "Current Mode: " << currentMode.first._width << "x" << currentMode.first._height << " (" << currentMode.first._refreshRate << "Hz)");
				DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), StringMeldInPlace(buffer) << "Current Mode HDR: " << (currentMode.second ? "Yes" : "No"));
			}

			layout.AllocateFullWidth(lineHeight);

			switch (_menuMode) {
			case MenuMode::MainMenu:
				DrawHeading(context, layout, **headingFont, "Main menu");
				DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), "1. Change active monitor");
				if (_window)
					DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), (_capturedMonitor == _activeMonitorId) ? "2. Release Monitor" : "2. Capture Monitor");
				DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), "3. Change mode");
				if (std::find(_monitorsReleasableMode.begin(), _monitorsReleasableMode.end(), _activeMonitorId) != _monitorsReleasableMode.end())
					DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), "4. Release mode");
				if (_activeMonitorId < monitors.size() && monitors[_activeMonitorId]._hdrSupported) {
					if (currentMode.second) {
						DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), "5. Disable HDR");
					} else
						DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), "5. Enable HDR");
				}
				break;

			case MenuMode::SelectMonitor:
				DrawHeading(context, layout, **headingFont, "Select Monitor");
				for (unsigned c=0; c<monitors.size(); ++c)
					DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), StringMeldInPlace(buffer) << char('1' + c) <<  ". " << monitors[c]._friendlyName);
				break;

			case MenuMode::ChangeMode:
				DrawHeading(context, layout, **headingFont, "New Mode");
				if (_activeMonitorId < monitors.size()) {
					auto modes = _dispSettings->GetModes(_activeMonitorId);
					if (_modeSelectorOffset != 0)
						DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), u8"\u2191\u2191\u2191 Up arrow");

					if (_modeSelectorOffset < modes.size()) {
						auto m = modes.begin() + _modeSelectorOffset;
						for (unsigned c=0; c<9; ++c) {
							if (m == modes.end()) break;
							DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), StringMeldInPlace(buffer) << char('1' + c) <<  ". " << m->_width << "x" << m->_height << " " << m->_refreshRate << "Hz");
							++m;
						}
						if (m != modes.end())
							DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), u8"\u2193\u2193\u2193 Down arrow");
					}

					DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), "Backspace to exit menu");
				}
				break;
			}
		}

		ProcessInputResult ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input)
		{
			switch (_menuMode) {
			case MenuMode::MainMenu:
				if (input._pressedChar == '1') {
					 _menuMode = MenuMode::SelectMonitor;
				} else if (input._pressedChar == '2') {
					if (_capturedMonitor == _activeMonitorId) {
						_window->ReleaseMonitor();
						_capturedMonitor = ~0u;
					} else {
						_window->CaptureMonitor(_dispSettings, _activeMonitorId);
						_capturedMonitor = _activeMonitorId;
					}
				} else if (input._pressedChar == '3') {
					_menuMode = MenuMode::ChangeMode;
					_modeSelectorOffset = 0;
				} else if (input._pressedChar == '4') {
					auto i = std::find(_monitorsReleasableMode.begin(), _monitorsReleasableMode.end(), _activeMonitorId);
					if (i!=_monitorsReleasableMode.end()) {
						_monitorsReleasableMode.erase(i);
						_dispSettings->ReleaseMode(_activeMonitorId);
					}
				} else if (input._pressedChar == '5') {
					auto monitors = _dispSettings->GetMonitors();
					if (_activeMonitorId < monitors.size() && monitors[_activeMonitorId]._hdrSupported) {
						auto currentMode = _dispSettings->GetCurrentMode(_activeMonitorId);
						if (currentMode.second) {
							_hdrState = OSServices::DisplaySettingsManager::DisplaySettingsManager::ToggleableState::Disable;
						} else {
							_hdrState = OSServices::DisplaySettingsManager::DisplaySettingsManager::ToggleableState::Enable;
						}
						_dispSettings->TryChangeMode(_activeMonitorId, currentMode.first, _hdrState);
					}
				}
				break;

			case MenuMode::SelectMonitor:
				{
					auto monitorsCount = _dispSettings->GetMonitors().size();
					if (input._pressedChar >= '1' && input._pressedChar < ('1'+monitorsCount)) {
						_activeMonitorId = input._pressedChar - '1';
						_menuMode = MenuMode::MainMenu;
					}
				}
				break;
		
			case MenuMode::ChangeMode:
				{
					if (input.IsPress(KeyId_Make("backspace"))) {
						_menuMode = MenuMode::MainMenu;
					} else if (input.IsPress(KeyId_Make("up"))) {
						if (_modeSelectorOffset != 0) --_modeSelectorOffset;
					} else if (input.IsPress(KeyId_Make("down"))) {
						++_modeSelectorOffset;
					} else if (input._pressedChar >= '1' && input._pressedChar <= '9') {
						auto idx = (input._pressedChar - '1') + _modeSelectorOffset;
						auto modes = _dispSettings->GetModes(_activeMonitorId);
						if (idx < modes.size()) {
							bool success = _dispSettings->TryChangeMode(_activeMonitorId, modes[idx], _hdrState);
							if (success) {
								auto i = std::find(_monitorsReleasableMode.begin(), _monitorsReleasableMode.end(), _activeMonitorId);
								if (i == _monitorsReleasableMode.end())
									_monitorsReleasableMode.push_back(_activeMonitorId);
							}
						}
					}
				}
				break;
			}

			return ProcessInputResult::Passthrough;
		}

		DisplaySettingsDisplay(std::shared_ptr<OSServices::DisplaySettingsManager> dispSettings, std::shared_ptr<Window> window)
		: _dispSettings(std::move(dispSettings))
		, _window(std::move(window))
		{
			_headingFont = RenderOverlays::MakeFont("DosisExtraBold", 20);
		}

	protected:
		std::shared_ptr<OSServices::DisplaySettingsManager> _dispSettings;
		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _headingFont;
		OSServices::DisplaySettingsManager::MonitorId _activeMonitorId = 0;
		std::shared_ptr<Window> _window;

		enum MenuMode { MainMenu, SelectMonitor, ChangeMode };
		MenuMode _menuMode = MenuMode::MainMenu;
		OSServices::DisplaySettingsManager::MonitorId _capturedMonitor = ~0u;
		std::vector<OSServices::DisplaySettingsManager::MonitorId> _monitorsReleasableMode;

		unsigned _modeSelectorOffset = 0;
		OSServices::DisplaySettingsManager::DisplaySettingsManager::ToggleableState _hdrState = OSServices::DisplaySettingsManager::DisplaySettingsManager::ToggleableState::LeaveUnchanged;
	};

	std::shared_ptr<IWidget> CreateDisplaySettingsDisplay(std::shared_ptr<OSServices::DisplaySettingsManager> dispSettings, std::shared_ptr<Window> window)
	{
		return std::make_shared<DisplaySettingsDisplay>(std::move(dispSettings), std::move(window));
	}

}}
