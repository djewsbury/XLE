// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/Threading/ThreadingUtils.h"
#include <memory>
#include <cstdint>
#include <future>
#include <any>

namespace OSServices
{
	struct PollingEventType
	{
		enum Flags
		{
			Input = 1<<0,
			Output = 1<<1
		};
		using BitField = unsigned;
	};

	class IConduitProducer
	{
	public:
		virtual ~IConduitProducer() = default;
	};

	class IConduitConsumer
	{
	public:
		virtual void OnEvent(std::any&& payload) = 0;
		virtual void OnException(const std::exception_ptr& exception) = 0;
		virtual ~IConduitConsumer() = default;
	};

	/** <summary>Abstraction of OS specific event polling behaviour</summary>
	 * 
	 * All OSs provide some means to efficiently wait for, and react to, events raised
	 * by "waitable objects". Typically these waitable objects are related to features
	 * provided by drivers -- such as asynchrous file reads, network socket, etc.
	 * 
	 * Even though all OSs provide roughly the same concepts, the actual API and behaviour
	 * can be quite different. For example, on Windows we have WaitForMultipleObjects and
	 * family. On Linux we have epoll/select/pool. On OSX we have kqueues So we need to use 
	 * some kind of abstraction layer in order to limit the amount of platform specific code.
	 * 
	 * Furthermore, we want to avoid working with the low level concepts (ie, file handle, 
	 * socket handle, event handle, etc) as much as possible and instead prefer robust
	 * patterns. In this case, futures are a great pattern -- they can be used in 3 different
	 * ways, all of which are useful
	 * 
	 * 	- checking the status intermittantly (eg, future.wait_for(0))
	 *  - stalling a thread until the future has a result (eg, future.get()/future.wait())
	 *  - triggering some follow up (eg, when(future, ...))
	 * 
	 * Plus, they represent what we're trying to do in a RAII-friendly way.
	 * 
	 * Internally, the PollingThread will have one or more threads. But we don't want to do 
	 * too much client logic on those threads. It might be better to just shift the client
	 * logic off to a general thread pool somewhere. That may just help avoid any OS
	 * specific complications (given that everything in this implementation is intended 
	 * to be very OS specific)
	 */
	class PollingThread
	{
	public:
		// Simple event type -- just tell me when something happens. One-off, with
		// no explicit cancel mechanism
		std::future<std::any> RespondOnce(
			const std::shared_ptr<IConduitProducer>& producer);

		std::future<void> Connect(
			const std::shared_ptr<IConduitProducer>& producer, 
			const std::shared_ptr<IConduitConsumer>& consumer);

		std::future<void> Disconnect(
			const std::shared_ptr<IConduitProducer>& producer);

		PollingThread();
		~PollingThread();
	private:
		class Pimpl;
		std::shared_ptr<Pimpl> _pimpl;
	};

	class UserEvent : public IConduitProducer
	{
	public:
		enum class Type { Binary, Semaphore };

		void IncreaseCounter();

		UserEvent& operator=(const UserEvent&) = delete;
		UserEvent(const UserEvent&) = delete;
		UserEvent() never_throws = delete;
	};

	std::shared_ptr<UserEvent> CreateUserEvent(UserEvent::Type = UserEvent::Type::Binary);
}

