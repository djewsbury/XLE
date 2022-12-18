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
#include <variant>

namespace Utility { template<typename... Args> class Signal; }
namespace OSServices { class DisplaySettingsManager; }

namespace PlatformRig
{
    struct SystemDisplayChange {};
    struct WindowResize { signed _newWidth = 0, _newHeight = 0; };
    class InputSnapshot;
    class InputContext;

    using SystemMessageVariant = std::variant<InputSnapshot, SystemDisplayChange, WindowResize>;

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
    ///         Window* window = ...;
    ///
    ///             // create a new presentation attached to the underlying
    ///             // window handle
    ///         auto presentationChain = device->CreatePresentationChain(
    ///             window->GetUnderlyingHandle());
    ///
    ///             // render a frame so we have some content when we first
    ///             // show the window
    ///         auto& threadContext = *device->GetImmediateContext();
    ///         auto renderTarget = threadContext.BeginFrame(*presentationChain);
    ///         (render a frame, clear presentation target, etc)
    ///         threadContext.Present(*presentationChain);
    ///         window->Show();
    ///
    ///         for (;;) { // start rendering / presentation loop
    ///     \endcode</code>
    /// </example>
    ///  
    class Window
    {
    public:
        const void* GetUnderlyingHandle() const;
        std::pair<Int2, Int2> GetRect() const;
        void SetTitle(const char titleText[]);
        void Resize(unsigned width, unsigned height);
        void Show(bool newState = true);

        Utility::Signal<SystemMessageVariant&&>& OnMessage();
        InputContext MakeInputContext();

        enum class PumpResult { Continue, Background, Terminate };
        static PumpResult DoMsgPump();

        void CaptureMonitor(
            std::shared_ptr<OSServices::DisplaySettingsManager>,
            unsigned monitorId);     // OSServices::DisplaySettingsManager::MonitorId
        void ReleaseMonitor();

        unsigned GetDPI() const;    // return DPI using OS heustrics (ie, on Windows 96 means 100% scaling)

        Window();
        ~Window();

        Window(Window&&) = default;
        Window& operator=(Window&&) = default;

        class Pimpl;
    protected:
        std::unique_ptr<Pimpl> _pimpl;
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
}

