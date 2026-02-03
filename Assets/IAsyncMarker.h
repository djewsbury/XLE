// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "../Core/Prefix.h"     // (for DEBUG_ONLY)
#include <chrono>

namespace Assets
{
	/// <summary>Records the status of asynchronous operation, very much like a std::promise<AssetState></summary>
	class IAsyncMarker
	{
	public:
		virtual AssetState		            GetAssetState() const = 0;
		virtual std::optional<AssetState>   StallWhilePending(std::chrono::microseconds timeout = std::chrono::microseconds(0)) const = 0;
		virtual Blob				    	GetActualizationLog() const = 0;
		virtual ~IAsyncMarker();
	};

    class GenericFuture : public IAsyncMarker
    {
    public:
        AssetState		GetAssetState() const { return _state; }
        std::optional<AssetState>   StallWhilePending(std::chrono::microseconds timeout = std::chrono::microseconds(0)) const;
        const char*     GetDebugLabel() const;  // GetDebugLabel only provided in debug builds, and only intended for debugging
		Blob			GetActualizationLog() const;

        GenericFuture(AssetState state = AssetState::Pending);
        ~GenericFuture();

		GenericFuture(GenericFuture&&) = delete;
		GenericFuture& operator=(GenericFuture&&) = delete;
		GenericFuture(const GenericFuture&) = delete;
		GenericFuture& operator=(const GenericFuture&) = delete;

		void	SetState(AssetState newState);
		void	SetDebugLabel(StringSection<char> initializer);

	private:
		AssetState _state;
		DEBUG_ONLY(ResChar _initializer[MaxPath];)
    };

	inline bool TimedWait(const IAsyncMarker& future, std::chrono::microseconds timeout)
	{
		auto stallResult = future.StallWhilePending(timeout);
		return stallResult.value_or(AssetState::Pending) != AssetState::Pending;
	}

	inline bool TimedWaitUntil(const IAsyncMarker& future, std::chrono::steady_clock::time_point timeoutTime)
	{
		auto stallResult = future.StallWhilePending(std::chrono::microseconds(500));	// no StallUntil(), so just have to pick a timeout time
		return stallResult.value_or(AssetState::Pending) != AssetState::Pending;
	}

}
