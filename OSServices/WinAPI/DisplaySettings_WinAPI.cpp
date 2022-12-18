// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../DisplaySettings.h"
#include "WinAPIWrapper.h"
#include "System_WinAPI.h"
#include "../Log.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/Conversion.h"
#include "../../Core/Prefix.h"
#include "../../Core/Exceptions.h"
#include <vector>
#include <optional>
#include <stdexcept>
#include <assert.h>

namespace OSServices
{

	static bool operator==(const DisplaySettingsManager::ModeDesc& lhs, const DisplaySettingsManager::ModeDesc& rhs)
	{
		return (lhs._width == rhs._width) && (lhs._height == rhs._height) && (lhs._refreshRate == rhs._refreshRate);
	}

	struct InternalMonitorDesc
	{
		std::wstring _deviceName;
		size_t _modesStart = 0;
		size_t _modesEnd = 0;
		unsigned _targetInfoId = 0;
		bool _hdrSupported = false;
	};

	struct InternalAdapterDesc
	{
		std::wstring _deviceName;
		LUID _luid;
	};

	class DisplaySettingsManager::Pimpl
	{
	public:
		std::vector<MonitorDesc> _monitors;
		std::vector<InternalMonitorDesc> _monitorsInternal;
		std::vector<AdapterDesc> _adapters;
		std::vector<InternalAdapterDesc> _adaptersInternal;
		std::vector<ModeDesc> _modes;
		bool _initialized = false;
		std::thread::id _attachedThreadId;

		std::vector<std::pair<uint64_t, ModeDesc>> _savedOriginalModes;
		std::optional<std::pair<uint64_t, ModeDesc>> _lastDisplayChange;
		bool _performingDisplayChangeCurrently = false;

		void QueryFromOS();
		void ClearCache();
		std::optional<ModeDesc> QueryCurrentSettingsFromOS(unsigned monitorIdx);
	};

	static std::optional<DisplaySettingsManager::ModeDesc> AsDisplayModeDesc(DEVMODEW devMode, DisplaySettingsManager::ToggleableState hdrState)
	{
		const unsigned requiredFields = 
			DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
		if ((devMode.dmFields & requiredFields) != requiredFields)
			return {};

		DisplaySettingsManager::ModeDesc result;
		result._width = devMode.dmPelsWidth;
		result._height = devMode.dmPelsHeight;
		result._refreshRate = devMode.dmDisplayFrequency;
		result._hdr = hdrState;
		return result;		// can't query any good information about bit depth / hdr
	}

	struct WindowsDisplay
	{
		std::wstring _deviceName;
		std::string _friendlyMonitorName;
		std::string _friendlyAdapterName;
		std::wstring _adapterDeviceName;
		std::wstring _targetDeviceName;
		std::pair<uint16_t, uint16_t> _manufacturerAndProductCodes = {0,0};
		LUID _adapterLUID = {};
		unsigned _targetInfoId = 0, _sourceInfoId = 0;
		bool _advancedColorSupported = false;
		unsigned _bitsPerColorChannel;
	};

	static std::vector<WindowsDisplay> QueryDisplays_CCD()
	{
		// These interfaces require Windows 7
		std::vector<DISPLAYCONFIG_PATH_INFO> paths;
		std::vector<DISPLAYCONFIG_MODE_INFO> modes;

		const unsigned queryFlags = QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE;		// QDC_VIRTUAL_REFRESH_RATE_AWARE
		for (;;) {
			uint32_t pathArrayCount = 0, modeArrayCount = 0;
			auto hres = ::GetDisplayConfigBufferSizes(queryFlags, &pathArrayCount, &modeArrayCount);
			if (SUCCEEDED(hres)) {
				paths.resize(pathArrayCount);
				modes.resize(modeArrayCount);

				hres = ::QueryDisplayConfig(
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
			sourceDisplay._sourceInfoId = path.sourceInfo.id;
			sourceDisplay._targetInfoId = path.targetInfo.id;
			
			/*
			if (path.sourceInfo.sourceModeInfoIdx < modes.size()) {
				DisplaySettingsManager::DesktopGeometry desktopGeometry;
				desktopGeometry._x = modes[path.sourceInfo.sourceModeInfoIdx].sourceMode.position.x;
				desktopGeometry._y = modes[path.sourceInfo.sourceModeInfoIdx].sourceMode.position.y;
				desktopGeometry._width = modes[path.sourceInfo.sourceModeInfoIdx].sourceMode.width;
				desktopGeometry._height = modes[path.sourceInfo.sourceModeInfoIdx].sourceMode.height;
			}
			*/

			DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
			targetName.header.adapterId = path.targetInfo.adapterId;
			targetName.header.id = path.targetInfo.id;
			targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
			targetName.header.size = sizeof(targetName);
			auto hres = DisplayConfigGetDeviceInfo(&targetName.header);
			if (SUCCEEDED(hres)) {
				sourceDisplay._friendlyMonitorName = Conversion::Convert<std::string>(MakeStringSection(targetName.monitorFriendlyDeviceName));
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
			sourceDisplay._bitsPerColorChannel = color_info.bitsPerColorChannel;
			// see also color_info.wideColorEnforced

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
		for (;;) {
			DISPLAY_DEVICEW adapterInfo;
			XlZeroMemory(adapterInfo);
			adapterInfo.cb = sizeof(DISPLAY_DEVICEW);
			auto hres = Windows::Fn_EnumDisplayDevices(nullptr, c++, &adapterInfo, 0);
			if (!hres) break;		// zero means we hit the end
			if (!SUCCEEDED(hres)) continue;

			if (!(adapterInfo.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;

			// adapterInfo.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE signifies the primary device

			DISPLAY_DEVICEW monitorInfo;
			XlZeroMemory(monitorInfo);
			monitorInfo.cb = sizeof(DISPLAY_DEVICEW);
			hres = Windows::Fn_EnumDisplayDevices(adapterInfo.DeviceName, 0, &monitorInfo, 0);
			if (!SUCCEEDED(hres)) continue;

			WindowsDisplay display;
			display._deviceName = adapterInfo.DeviceName;
			display._advancedColorSupported = false;		// can never query this via this path
			display._friendlyMonitorName = Conversion::Convert<std::string>(MakeStringSection(monitorInfo.DeviceString));
			display._friendlyAdapterName = Conversion::Convert<std::string>(MakeStringSection(adapterInfo.DeviceString));
			// can't get _adapterDeviceName & _targetDeviceName that is compatible with the CCD path
			// manifacturer & luid codes also missing

			result.push_back(display);
		}

		return result;
	}

	static DisplaySettingsManager::DesktopGeometry GetDesktopGeometryForMonitorDevice(const wchar_t* deviceName)
	{
		DisplaySettingsManager::DesktopGeometry result;
		DEVMODEW devMode;
		XlZeroMemory(devMode);
		devMode.dmSize = sizeof(DEVMODEW);
		auto hres = Windows::Fn_EnumDisplaySettingsEx(deviceName, ENUM_CURRENT_SETTINGS, &devMode, 0);
		if (SUCCEEDED(hres)) {
			if (devMode.dmFields & DM_POSITION) {
				result._x = devMode.dmPosition.x;
				result._y = devMode.dmPosition.y;
			}
			if ((devMode.dmFields & (DM_PELSWIDTH|DM_PELSHEIGHT)) == (DM_PELSWIDTH|DM_PELSHEIGHT)) {
				result._width = devMode.dmPelsWidth;
				result._height = devMode.dmPelsHeight;
			}
		}
		return result;
	}

	void DisplaySettingsManager::Pimpl::ClearCache()
	{
		assert(std::this_thread::get_id() == _attachedThreadId);
		_monitors.clear();
		_monitorsInternal.clear();
		_adapters.clear();
		_adaptersInternal.clear();
		_modes.clear();
		_lastDisplayChange = {};
		_initialized = false;
	}

	void DisplaySettingsManager::Pimpl::QueryFromOS()
	{
		assert(std::this_thread::get_id() == _attachedThreadId);
		ClearCache();
		_initialized = true;

		auto displayQuery = QueryDisplays_CCD();
		if (displayQuery.empty())
			displayQuery = QueryDisplays_OldAPI();

		for (const auto& dev:displayQuery) {
			auto i = std::find_if(_adaptersInternal.begin(), _adaptersInternal.end(), [n=dev._adapterLUID](const auto& q) { return q._luid.HighPart == n.HighPart && q._luid.LowPart == n.LowPart; });
			if (i == _adaptersInternal.end()) {
				AdapterDesc adapterDesc;
				adapterDesc._friendlyName = dev._friendlyAdapterName;
				adapterDesc._locallyUniqueId = uint64_t(dev._adapterLUID.HighPart) << 32ull | uint64_t(dev._adapterLUID.LowPart);
				_adapters.push_back(adapterDesc);
				_adaptersInternal.push_back(InternalAdapterDesc{dev._adapterDeviceName, dev._adapterLUID});
				i = _adaptersInternal.end()-1;
			}

			InternalMonitorDesc internalMonitorDesc;
			internalMonitorDesc._deviceName = dev._deviceName;
			internalMonitorDesc._modesStart = _modes.size();
			internalMonitorDesc._targetInfoId = dev._targetInfoId;

			unsigned c=0;
			for (;;) {
				DEVMODEW devMode;
				XlZeroMemory(devMode);
				devMode.dmSize = sizeof(DEVMODEW);
				auto hres = Windows::Fn_EnumDisplaySettingsEx(dev._deviceName.c_str(), c++, &devMode, 0);
				if (!hres) break;

				auto dispMode = AsDisplayModeDesc(devMode, internalMonitorDesc._hdrSupported ? ToggleableState::Supported : ToggleableState::Unsupported);
				if (dispMode) {
					auto existing = std::find(_modes.begin() + internalMonitorDesc._modesStart, _modes.end(), *dispMode);
					if (existing == _modes.end())
						_modes.push_back(*dispMode);
				}
			}

			internalMonitorDesc._modesEnd = _modes.size();
			internalMonitorDesc._hdrSupported = dev._advancedColorSupported;
			// Reverse because windows tends to list the modes from lowest resolution to highest resolution
			std::reverse(_modes.begin()+internalMonitorDesc._modesStart, _modes.begin()+internalMonitorDesc._modesEnd);
			_monitorsInternal.emplace_back(std::move(internalMonitorDesc));

			MonitorDesc monitorDesc;
			monitorDesc._friendlyName = dev._friendlyMonitorName;
			monitorDesc._adapter = (unsigned)std::distance(_adaptersInternal.begin(), i);
			monitorDesc._locallyUniqueId = Hash64(dev._deviceName);
			_monitors.push_back(monitorDesc);
		}
	}

	auto DisplaySettingsManager::Pimpl::QueryCurrentSettingsFromOS(MonitorId monitorId) -> std::optional<DisplaySettingsManager::ModeDesc>
	{
		assert(std::this_thread::get_id() == _attachedThreadId);
		assert(monitorId < _monitorsInternal.size());

		DEVMODEW devMode;
		XlZeroMemory(devMode);
		devMode.dmSize = sizeof(DEVMODEW);
		auto hres = Windows::Fn_EnumDisplaySettingsEx(_monitorsInternal[monitorId]._deviceName.c_str(), ENUM_CURRENT_SETTINGS, &devMode, 0);
		if (!hres) return {};

		ToggleableState hdrState = ToggleableState::Unsupported;

		if (_monitorsInternal[monitorId]._hdrSupported) {
			auto& adapter = _adaptersInternal[_monitors[monitorId]._adapter];
			DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO getAdvancedColor = {};
			getAdvancedColor.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
			getAdvancedColor.header.size = sizeof(getAdvancedColor);
			getAdvancedColor.header.adapterId = adapter._luid;
			getAdvancedColor.header.id = _monitorsInternal[monitorId]._targetInfoId;
			hres = ::DisplayConfigGetDeviceInfo(&getAdvancedColor.header);
			if (SUCCEEDED(hres))
				hdrState = getAdvancedColor.advancedColorEnabled ? ToggleableState::Supported : ToggleableState::Unsupported;
		}

		return AsDisplayModeDesc(devMode, hdrState);
	}

	bool DisplaySettingsManager::TryChangeMode(MonitorId monitor, const ModeDesc& requestedMode)
	{
		assert(std::this_thread::get_id() == _pimpl->_attachedThreadId);
		assert(!_pimpl->_performingDisplayChangeCurrently);

		// Change 1st display to some other format
		if (!_pimpl->_initialized)
			_pimpl->QueryFromOS();

		assert(_pimpl->_monitorsInternal.size() == _pimpl->_monitors.size());
		assert(_pimpl->_adaptersInternal.size() == _pimpl->_adapters.size());

		if (monitor >= _pimpl->_monitors.size())
			return false;

		if (requestedMode._hdr == ToggleableState::Enable && !_pimpl->_monitorsInternal[monitor]._hdrSupported)
			return false;

		auto initialMode = _pimpl->QueryCurrentSettingsFromOS(monitor);
		_pimpl->_lastDisplayChange = { _pimpl->_monitors[monitor]._locallyUniqueId, requestedMode };
		_pimpl->_performingDisplayChangeCurrently = true;

		// It's not clear if there's any particular advantage to attempting to use DCC for this (which is a lot more complicated.
		// particularly given that we have to switch the resolution, and then switch the HDR configuration in a separate step
		// in either approach

		DEVMODEW displayMode = {};
		displayMode.dmSize = sizeof(DEVMODEW);
		displayMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
		displayMode.dmPelsWidth = requestedMode._width;
		displayMode.dmPelsHeight = requestedMode._height;
		displayMode.dmDisplayFrequency = requestedMode._refreshRate;
		displayMode.dmBitsPerPel = 32;		// windows 8 & above requires this to be 32
		auto hres = Windows::Fn_ChangeDisplaySettingsEx(
			_pimpl->_monitorsInternal[monitor]._deviceName.c_str(),
			&displayMode,
			nullptr,
			CDS_FULLSCREEN,		// CDS_TEST just tests to see if it's going to work
			nullptr);

		if (!SUCCEEDED(hres)) {
			Log(Warning) << "ChangeDisplaySettingsEx failed with error code: " << SystemErrorCodeAsString(hres) << std::endl;
			_pimpl->_performingDisplayChangeCurrently = false;
			return false;
		}

		auto& adapter = _pimpl->_adaptersInternal[_pimpl->_monitors[monitor]._adapter];

		if (initialMode) {
			// If this is the first time we've changed this monitor, save the original mode -- so we can release the monitor back to the original state
			assert(_pimpl->_monitors[monitor]._locallyUniqueId != 0);
			auto i = LowerBound(_pimpl->_savedOriginalModes, _pimpl->_monitors[monitor]._locallyUniqueId);
			if (i == _pimpl->_savedOriginalModes.end() || i->first != _pimpl->_monitors[monitor]._locallyUniqueId)
				_pimpl->_savedOriginalModes.emplace_back(_pimpl->_monitors[monitor]._locallyUniqueId, *initialMode);
		}

		// attempt to enable "advanced color" modes
		if (requestedMode._hdr != ToggleableState::LeaveUnchanged) {
			DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setAdvancedColor = {};
			setAdvancedColor.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
			setAdvancedColor.header.size = sizeof(setAdvancedColor);
			setAdvancedColor.header.adapterId = adapter._luid;
			setAdvancedColor.header.id = _pimpl->_monitorsInternal[monitor]._targetInfoId;
			setAdvancedColor.enableAdvancedColor = (requestedMode._hdr == ToggleableState::Enable) ? 1 : 0;
			hres = ::DisplayConfigSetDeviceInfo(&setAdvancedColor.header);

			if (!SUCCEEDED(hres)) {
				Log(Warning) << "DisplayConfigSetDeviceInfo failed with error code: " << SystemErrorCodeAsString(hres) << std::endl;
				_pimpl->_performingDisplayChangeCurrently = false;
				return false;
			}
		}

		_pimpl->_performingDisplayChangeCurrently = false;
		return true;
	}

	void DisplaySettingsManager::ReleaseMode(MonitorId monitor)
	{
		assert(std::this_thread::get_id() == _pimpl->_attachedThreadId);

		// if we changed the video mode of the given monitor; release it and restore back to the previous mode
		if (!_pimpl->_initialized)
			_pimpl->QueryFromOS();

		if (monitor >= _pimpl->_monitors.size())
			return;

		auto i = LowerBound(_pimpl->_savedOriginalModes, _pimpl->_monitors[monitor]._locallyUniqueId);
		if (i != _pimpl->_savedOriginalModes.end() && i->first == _pimpl->_monitors[monitor]._locallyUniqueId) {
			auto& savedMode = i->second;
			auto& adapter = _pimpl->_adaptersInternal[_pimpl->_monitors[monitor]._adapter];

			// restore hdr mode first
			if (_pimpl->_monitorsInternal[monitor]._hdrSupported && savedMode._hdr != ToggleableState::LeaveUnchanged) {
				DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setAdvancedColor = {};
				setAdvancedColor.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
				setAdvancedColor.header.size = sizeof(setAdvancedColor);
				setAdvancedColor.header.adapterId = adapter._luid;
				setAdvancedColor.header.id = _pimpl->_monitorsInternal[monitor]._targetInfoId;
				setAdvancedColor.enableAdvancedColor = savedMode._hdr == ToggleableState::Enable;
				auto hres = ::DisplayConfigSetDeviceInfo(&setAdvancedColor.header);

				if (!SUCCEEDED(hres))
					Log(Warning) << "DisplayConfigSetDeviceInfo failed with error code: " << SystemErrorCodeAsString(hres) << std::endl;
			}

			// restore settings back to how they were previously
			DEVMODEW displayMode = {};
			displayMode.dmSize = sizeof(DEVMODEW);
			displayMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
			displayMode.dmPelsWidth = savedMode._width;
			displayMode.dmPelsHeight = savedMode._height;
			displayMode.dmDisplayFrequency = savedMode._refreshRate;
			displayMode.dmBitsPerPel = 32;		// windows 8 & above requires this to be 32
			auto hres = Windows::Fn_ChangeDisplaySettingsEx(
				_pimpl->_monitorsInternal[monitor]._deviceName.c_str(),
				&displayMode,
				nullptr,
				CDS_FULLSCREEN,		// CDS_TEST just tests to see if it's going to work
				nullptr);

			if (!SUCCEEDED(hres))
				Log(Warning) << "ChangeDisplaySettingsEx failed with error code: " << SystemErrorCodeAsString(hres) << std::endl;

			_pimpl->_savedOriginalModes.erase(i);
		}
	}

	auto DisplaySettingsManager::GetDesktopGeometryForMonitor(MonitorId monitorId) -> DesktopGeometry
	{
		if (!_pimpl->_initialized)
			_pimpl->QueryFromOS();

		assert(monitorId < _pimpl->_monitors.size());
		if (monitorId >= _pimpl->_monitors.size())
			return {};

		// We can cache this result, because it's probably not going to change more frequently than anything else we cache... But here
		// we're just querying it on demand
		return GetDesktopGeometryForMonitorDevice(_pimpl->_monitorsInternal[monitorId]._deviceName.c_str());
	}

	auto DisplaySettingsManager::GetCurrentMode(MonitorId monitorId) -> ModeDesc
	{
		if (!_pimpl->_initialized)
			_pimpl->QueryFromOS();

		assert(monitorId < _pimpl->_monitors.size());
		if (monitorId >= _pimpl->_monitors.size())
			return ModeDesc{0,0,0,ToggleableState::LeaveUnchanged};

		DEVMODEW devMode;
		XlZeroMemory(devMode);
		devMode.dmSize = sizeof(DEVMODEW);
		auto hres = Windows::Fn_EnumDisplaySettingsEx(_pimpl->_monitorsInternal[monitorId]._deviceName.c_str(), ENUM_CURRENT_SETTINGS, &devMode, 0);
		if (!SUCCEEDED(hres))
			return ModeDesc{0,0,0,ToggleableState::LeaveUnchanged};

		bool hdrEnabled = false;
		if (_pimpl->_monitorsInternal[monitorId]._hdrSupported) {
			DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO getAdvancedColor = {};
			getAdvancedColor.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
			getAdvancedColor.header.size = sizeof(getAdvancedColor);
			getAdvancedColor.header.adapterId = _pimpl->_adaptersInternal[_pimpl->_monitors[monitorId]._adapter]._luid;
			getAdvancedColor.header.id = _pimpl->_monitorsInternal[monitorId]._targetInfoId;
			hres = ::DisplayConfigGetDeviceInfo(&getAdvancedColor.header);
			if (SUCCEEDED(hres))
				hdrEnabled = getAdvancedColor.advancedColorEnabled;
		}

		return AsDisplayModeDesc(devMode, hdrEnabled?ToggleableState::Enable:ToggleableState::Disable).value_or(ModeDesc{0,0,0,ToggleableState::LeaveUnchanged});
	}

	auto DisplaySettingsManager::GetModes(MonitorId monitorId) -> IteratorRange<const ModeDesc*> 
	{
		if (!_pimpl->_initialized)
			_pimpl->QueryFromOS();

		assert(monitorId < _pimpl->_monitorsInternal.size());
		if (monitorId >= _pimpl->_monitorsInternal.size())
			return {};

		return MakeIteratorRange(
			_pimpl->_modes.begin() + _pimpl->_monitorsInternal[monitorId]._modesStart,
			_pimpl->_modes.begin() + _pimpl->_monitorsInternal[monitorId]._modesEnd);
	}

	auto DisplaySettingsManager::GetMonitors() -> IteratorRange<const MonitorDesc*>
	{
		if (!_pimpl->_initialized)
			_pimpl->QueryFromOS();
		return _pimpl->_monitors;
	}

	auto DisplaySettingsManager::GetAdapters() -> IteratorRange<const AdapterDesc*>
	{
		if (!_pimpl->_initialized)
			_pimpl->QueryFromOS();
		return _pimpl->_adapters;
	}

	static DisplaySettingsManager* s_dispSettingsManager = nullptr;

	void OnDisplaySettingsChange(unsigned width, unsigned height)
	{
		if (!s_dispSettingsManager) return;

		assert(std::this_thread::get_id() == s_dispSettingsManager->_pimpl->_attachedThreadId);

		// if the change wasn't one that we initiated ourselves, we need release all of our cached info (it could be a new monitor attaching,
		// or anything along those lines)
		if (s_dispSettingsManager->_pimpl->_performingDisplayChangeCurrently) return;

		bool isOurChange = false;
		if (s_dispSettingsManager->_pimpl->_lastDisplayChange.has_value()) {
			if (width == s_dispSettingsManager->_pimpl->_lastDisplayChange->second._width
				&& height == s_dispSettingsManager->_pimpl->_lastDisplayChange->second._height) {

				unsigned monitorIdx=0;
				for (; monitorIdx<s_dispSettingsManager->_pimpl->_monitors.size(); ++monitorIdx)
					if (s_dispSettingsManager->_pimpl->_monitors[monitorIdx]._locallyUniqueId == s_dispSettingsManager->_pimpl->_lastDisplayChange->first)
						break;

				if (monitorIdx < s_dispSettingsManager->_pimpl->_monitors.size())
					if (auto currentSettings = s_dispSettingsManager->_pimpl->QueryCurrentSettingsFromOS(monitorIdx))
						isOurChange = *currentSettings == s_dispSettingsManager->_pimpl->_lastDisplayChange->second;
			}
		}

		if (!isOurChange)
			s_dispSettingsManager->_pimpl->ClearCache();
	}

	DisplaySettingsManager::DisplaySettingsManager()
	{
		assert(!s_dispSettingsManager);
		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_attachedThreadId = std::this_thread::get_id();
		s_dispSettingsManager = this;
	}

	DisplaySettingsManager::~DisplaySettingsManager()
	{
		// restore all modes that have changed before we exit
		// (Windows will restore the resolution but not HDR configuration if we don't do this)
		for (unsigned monitorId=0; monitorId<_pimpl->_monitors.size(); ++monitorId)
			ReleaseMode(monitorId);
		s_dispSettingsManager = nullptr;
	}
}
