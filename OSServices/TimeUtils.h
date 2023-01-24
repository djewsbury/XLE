// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <chrono>

namespace OSServices
{
	uint64_t    GetPerformanceCounter();
	uint64_t    GetPerformanceCounterFrequency();

	template<typename Rep, typename Period>
		uint64_t AsMilliseconds(const std::chrono::duration<Rep, Period>& input)
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(input).count();	
	}

	template<typename Rep, typename Period>
		uint64_t AsMicroseconds(const std::chrono::duration<Rep, Period>& input)
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(input).count();	
	}

	void ConfigureProcessSettings();
	void ConfigureDPIAwareness();
}
