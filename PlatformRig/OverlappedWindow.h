// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Math/Vector.h"
#include <memory>       // for unique_ptr
#include <functional>
#include <chrono>

namespace PlatformRig
{
    class InputTranslator;

    class IWindowHandler
    {
    public:
        virtual void    OnResize(unsigned newWidth, unsigned newHeight) = 0;
        virtual ~IWindowHandler() {}
    };

	class IOSRunLoop
	{
	public:
		using TimeoutCallback = std::function<void()>;
		using EventId = unsigned;

		virtual EventId ScheduleTimeoutEvent(
			std::chrono::time_point<std::chrono::steady_clock> timePoint,		// in milliseconds, matching values returned from Millisecond_Now(),
			TimeoutCallback&& callback) = 0;
		virtual void RemoveEvent(EventId evnts) = 0;

		virtual ~IOSRunLoop() {}
	};

	IOSRunLoop* GetOSRunLoop();
	void SetOSRunLoop(const std::shared_ptr<IOSRunLoop>& runLoop);

    /// <summary>An independent window in OS presentation scheme</summary>
    /// Creates and manages an independent OS window.
    /// The result depends on the particular OS. But on an OS like Microsoft
    /// Windows, we should expect a new top-level window to appear. A normal
    /// game will have just one window like this, and will attach a rendering
    /// surface to the window.
    ///
    /// <example>
    ///     To associate a presentation chain with the window, follow this example:
    ///      <code>\code
    ///         RenderCore::IDevice* device = ...;
    ///         OverlappedWindow* window = ...;
    ///         auto rect = window->GetRect();
    ///             // create a new presentation attached to the underlying
    ///             // window handle
    ///         auto presentationChain = device->CreatePresentationChain(
    ///             window->GetUnderlyingHandle(), 
    ///             rect.second[0] - rect.first[0], rect.second[1] - rect.first[1]);
    ///         for (;;;) { // start rendering / presentation loop
    ///     \endcode</code>
    /// </example>
    ///  
    class OverlappedWindow
    {
    public:
        const void* GetUnderlyingHandle() const;
        std::pair<Int2, Int2> GetRect() const;
        void SetTitle(const char titleText[]);
        void Resize(unsigned width, unsigned height);

        InputTranslator& GetInputTranslator();
        void AddWindowHandler(std::shared_ptr<IWindowHandler> handler);

        enum class PumpResult { Continue, Background, Terminate };
        static PumpResult DoMsgPump();

        OverlappedWindow();
        ~OverlappedWindow();

        OverlappedWindow(OverlappedWindow&&) = default;
        OverlappedWindow& operator=(OverlappedWindow&&) = default;

        class Pimpl;
    protected:
        std::unique_ptr<Pimpl> _pimpl;
    };

}

