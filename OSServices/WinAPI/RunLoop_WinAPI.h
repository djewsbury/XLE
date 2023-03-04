// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../OverlappedWindow.h"
#include <vector>
#include <chrono>

#include "IncludeWindows.h"

namespace OSServices
{
	class OSRunLoop_BasicTimer : public IOSRunLoop
	{
	public:
		EventId ScheduleTimeoutEvent(std::chrono::steady_clock::time_point timePoint, TimeoutCallback&& callback);
		void RemoveEvent(EventId evnts);

		bool OnOSTrigger(UINT_PTR osTimer);

		OSRunLoop_BasicTimer(HWND attachedWindow);
		~OSRunLoop_BasicTimer();
	private:
		struct TimeoutEvent
		{
			unsigned _eventId;
			TimeoutCallback _callback;			
		};
		std::vector<std::pair<std::chrono::steady_clock::time_point, TimeoutEvent>> _timeoutEvents;

		HWND _attachedWindow;
		UINT_PTR _osTimer;
		unsigned _nextEventId;

		void ResetUnderlyingTimer(std::chrono::steady_clock::time_point currentTimepoint);
	};
}
