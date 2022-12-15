// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

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

		struct ModeDesc
		{
			unsigned _width, _height;
			unsigned _refreshRate;
		};

		struct MonitorDesc
		{
			bool _hdrSupported = false;
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

		enum class ToggleableState { Disable, Enable, LeaveUnchanged };
		
		bool TryChangeMode(MonitorId, const ModeDesc&, ToggleableState hdrState = ToggleableState::LeaveUnchanged);
		void ReleaseMode(MonitorId);

		DisplaySettingsManager();
		~DisplaySettingsManager();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;

		friend void OnDisplaySettingsChange(unsigned, unsigned);
	};

}
