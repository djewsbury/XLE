// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/IteratorUtils.h"
#include <memory>
#include <string>

namespace OSServices
{

	class DisplaySettingsManager
	{
	public:
		using MonitorId = unsigned;
		using AdapterId = unsigned;

		enum class ToggleableState { Unsupported, Supported, LeaveUnchanged, Disable=Unsupported, Enable=Supported };

		struct ModeDesc
		{
			unsigned _width, _height;
			unsigned _refreshRate;
			ToggleableState _hdr;
		};

		struct MonitorDesc
		{
			std::string _friendlyName;
			AdapterId _adapter = 0;
			uint64_t _locallyUniqueId = 0;
		};

		struct AdapterDesc
		{
			std::string _friendlyName;
			uint64_t _locallyUniqueId = 0;
		};

		IteratorRange<const ModeDesc*> GetModes(MonitorId);
		IteratorRange<const MonitorDesc*> GetMonitors();
		IteratorRange<const AdapterDesc*> GetAdapters();

		/// DesktopGeometry is used when associating windows in a windowing system with a specific monitor
		/// The behaviour will be specific to the windowing system. For example, Windows has one large 2D
		/// field, and a part of that field is assigned to each monitor.
		struct DesktopGeometry
		{
			int _x, _y;
			int _width, _height;
		};
		DesktopGeometry GetDesktopGeometryForMonitor(MonitorId);
		bool IsValidMonitor(MonitorId monitorId);

		bool TryChangeMode(MonitorId, const ModeDesc&);
		void ReleaseMode(MonitorId);

		ModeDesc GetCurrentMode(MonitorId);

		DisplaySettingsManager();
		~DisplaySettingsManager();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;

		friend void OnDisplaySettingsChange(unsigned, unsigned);
	};

}
