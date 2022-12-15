// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../DisplaySettings.h"
#include "WinAPIWrapper.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Core/Prefix.h"
#include "../../Core/Exceptions.h"
#include <vector>
#include <optional>
#include <stdexcept>
#include <assert.h>

namespace OSServices
{

	struct DisplayModeDesc
	{
		unsigned _width, _height;
		unsigned _refreshRate;

		friend bool operator==(const DisplayModeDesc& lhs, const DisplayModeDesc& rhs)
		{
			return (lhs._width == rhs._width) && (lhs._height == rhs._height) && (lhs._refreshRate == rhs._refreshRate);
		}
	};

	struct MonitorDesc
	{
		std::vector<DisplayModeDesc> _displayModes;
		bool _advanceColorSupported;
		std::wstring _friendlyName;
		std::wstring _deviceName;
		unsigned _adapterIdx;
	};

	struct AdapterDesc
	{
		std::wstring _friendlyName;
		std::wstring _deviceName;
	};

	static std::optional<DisplayModeDesc> AsDisplayModeDesc(DEVMODEW devMode)
	{
		const unsigned requiredFields = 
			DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
		if ((devMode.dmFields & requiredFields) != requiredFields)
			return {};

		DisplayModeDesc result;
		result._width = devMode.dmPelsWidth;
		result._height = devMode.dmPelsHeight;
		result._refreshRate = devMode.dmDisplayFrequency;
		return result;		// can't query any good information about bit depth / hdr
	}

	struct WindowsDisplay
	{
		std::wstring _deviceName;
		std::wstring _friendlyMonitorName;
		std::wstring _friendlyAdapterName;
		std::wstring _adapterDeviceName;
		std::wstring _targetDeviceName;
		std::pair<uint16_t, uint16_t> _manufacturerAndProductCodes = {0,0};
		LUID _adapterLUID = {};
		bool _advancedColorSupported = false;
	};

	static std::vector<WindowsDisplay> QueryDisplays_CCD()
	{
		std::vector<DISPLAYCONFIG_PATH_INFO> paths;
		std::vector<DISPLAYCONFIG_MODE_INFO> modes;

		const unsigned queryFlags = QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE;		// QDC_VIRTUAL_REFRESH_RATE_AWARE
		for (;;) {
			uint32_t pathArrayCount = 0, modeArrayCount = 0;
			auto hres = GetDisplayConfigBufferSizes(queryFlags, &pathArrayCount, &modeArrayCount);
			if (SUCCEEDED(hres)) {
				paths.resize(pathArrayCount);
				modes.resize(modeArrayCount);

				hres = QueryDisplayConfig(
					queryFlags,
					&pathArrayCount, paths.data(),
					&modeArrayCount, modes.data(),
					nullptr);
				if (hres == ERROR_INSUFFICIENT_BUFFER) continue;

				paths.resize(pathArrayCount);
				modes.resize(modeArrayCount);
			}

			if (!SUCCEEDED(hres))
				Throw(std::runtime_error("Failure while querying active monitors from Windows"));
			break;
		}

		std::vector<WindowsDisplay> displays;

		for (auto& path:paths) {

			WindowsDisplay sourceDisplay = {};
			sourceDisplay._adapterLUID = path.targetInfo.adapterId;

			DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
			targetName.header.adapterId = path.targetInfo.adapterId;
			targetName.header.id = path.targetInfo.id;
			targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
			targetName.header.size = sizeof(targetName);
			auto hres = DisplayConfigGetDeviceInfo(&targetName.header);
			if (SUCCEEDED(hres)) {
				sourceDisplay._friendlyMonitorName = targetName.monitorFriendlyDeviceName;
				sourceDisplay._manufacturerAndProductCodes = {targetName.edidManufactureId, targetName.edidProductCodeId};
				sourceDisplay._targetDeviceName = targetName.monitorDevicePath;
			}

			DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
			sourceName.header.adapterId = path.sourceInfo.adapterId;
			sourceName.header.id = path.sourceInfo.id;
			sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
			sourceName.header.size = sizeof(sourceName);
			hres = DisplayConfigGetDeviceInfo(&sourceName.header);
			if (SUCCEEDED(hres))
				sourceDisplay._deviceName = sourceName.viewGdiDeviceName;

			DISPLAYCONFIG_ADAPTER_NAME adapterName = {};
			adapterName.header.adapterId = path.targetInfo.adapterId;
			adapterName.header.id = path.targetInfo.id;
			adapterName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
			adapterName.header.size = sizeof(adapterName);
			hres = DisplayConfigGetDeviceInfo(&adapterName.header);
			if (SUCCEEDED(hres))
				sourceDisplay._adapterDeviceName = adapterName.adapterDevicePath;

			DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color_info = {};
			color_info.header.adapterId = path.targetInfo.adapterId;
			color_info.header.id = path.targetInfo.id;
			color_info.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
			color_info.header.size = sizeof(color_info);
			hres = DisplayConfigGetDeviceInfo(&color_info.header);
			if (SUCCEEDED(hres)) {
				sourceDisplay._advancedColorSupported = color_info.advancedColorSupported;
			} else
				sourceDisplay._advancedColorSupported = false;

			// little awkward to get the friendly name for the adapter -- IPortableDeviceManager doesn't seem to work
			// might have to use https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdigetdeviceregistrypropertya?redirectedfrom=MSDN

			displays.push_back(sourceDisplay);
		}

		return displays;
	}

	static std::vector<WindowsDisplay> QueryDisplays_OldAPI()
	{
		// Note that this older querying method isn't multi-process safe, because another process
		// could change the array of results while we're querying it (or even just a monitor turning on or off)
		std::vector<WindowsDisplay> result;

		unsigned c=0;
		unsigned primaryDeviceIdx = 0;
		for (;;) {
			DISPLAY_DEVICEW adapterInfo;
			XlZeroMemory(adapterInfo);
			adapterInfo.cb = sizeof(DISPLAY_DEVICEW);
			auto hres = Windows::Fn_EnumDisplayDevices(nullptr, c++, &adapterInfo, 0);
			if (!hres) break;		// zero means we hit the end
			if (!SUCCEEDED(hres)) continue;

			if (!(adapterInfo.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;

			if (adapterInfo.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
				primaryDeviceIdx = (unsigned)result.size();

			DISPLAY_DEVICEW monitorInfo;
			XlZeroMemory(monitorInfo);
			monitorInfo.cb = sizeof(DISPLAY_DEVICEW);
			hres = Windows::Fn_EnumDisplayDevices(adapterInfo.DeviceName, 0, &monitorInfo, 0);
			if (!SUCCEEDED(hres)) continue;

			WindowsDisplay display;
			display._deviceName = adapterInfo.DeviceName;
			display._advancedColorSupported = false;		// can never query this via this path
			display._friendlyMonitorName = monitorInfo.DeviceString;
			display._friendlyAdapterName = adapterInfo.DeviceString;
			// can't get _adapterDeviceName & _targetDeviceName that is compatible with the CCD path
			// manifacturer & luid codes also missing
			
			result.push_back(display);
		}

		return result;
	}

	void DisplaySettingsManager::LogDisplaySettings()
	{
		auto displayQuery = QueryDisplays_CCD();
		if (displayQuery.empty())
			displayQuery = QueryDisplays_OldAPI();

		std::vector<std::wstring> adapters;
		std::vector<MonitorDesc> monitors;

		for (const auto& dev:displayQuery) {

			auto i = std::find(adapters.begin(), adapters.end(), dev._adapterDeviceName);
			if (i == adapters.end()) {
				adapters.push_back(dev._adapterDeviceName);
				i = adapters.end()-1;
			}

			MonitorDesc monitorDesc;
			monitorDesc._adapterIdx = (unsigned)std::distance(adapters.begin(), i);
			monitorDesc._deviceName = dev._deviceName;
			monitorDesc._friendlyName = dev._friendlyMonitorName;

			unsigned c=0;
			for (;;) {
				DEVMODEW devMode;
				XlZeroMemory(devMode);
				devMode.dmSize = sizeof(DEVMODEW);
				auto hres = Windows::Fn_EnumDisplaySettingsEx(
					dev._deviceName.c_str(),
					c++, &devMode, 0);
				if (!hres) break;

				auto dispMode = AsDisplayModeDesc(devMode);
				if (dispMode) {
					auto existing = std::find(monitorDesc._displayModes.begin(), monitorDesc._displayModes.end(), *dispMode);
					if (existing == monitorDesc._displayModes.end())
						monitorDesc._displayModes.push_back(*dispMode);
				}
			}

			monitors.emplace_back(std::move(monitorDesc));
		}

		int q=0;
		(void)q;
	}

	DisplaySettingsManager::DisplaySettingsManager() {}
	DisplaySettingsManager::~DisplaySettingsManager() {}
}
