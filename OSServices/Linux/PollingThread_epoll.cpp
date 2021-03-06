// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "System_Linux.h"
#include "../PollingThread.h"
#include "../../OSServices/Log.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/FunctionUtils.h"
#include "../../Core/Exceptions.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace OSServices
{
	template<typename Ptr1, typename Ptr2>
		bool PointersEquivalent(const Ptr1& lhs, const Ptr2& rhs) { return !lhs.owner_before(rhs) && !rhs.owner_before(lhs); }

	static struct epoll_event EPollEvent(PollingEventType::BitField types, bool oneShot = true)
	{
		struct epoll_event result{};
		result.events = EPOLLRDHUP | EPOLLHUP | EPOLLERR;

		const bool edgeTriggered = false;
		if (edgeTriggered)
			result.events |= EPOLLET;
		if (oneShot)
			result.events |= EPOLLONESHOT;

		if (types & PollingEventType::Input)
			result.events |= EPOLLIN;
		if (types & PollingEventType::Output)
			result.events |= EPOLLOUT;
		return result;
	}

	static PollingEventType::BitField AsPollingEventType(uint32_t osEventFlags)
	{
		PollingEventType::BitField result = 0;
		if (osEventFlags & EPOLLIN)
			result |= PollingEventType::Input;
		if (osEventFlags & EPOLLOUT)
			result |= PollingEventType::Output;
		return result;
	}

	class PollingThread::Pimpl
	{
	public:
		int _interruptPollEvent = -1;
		std::atomic<bool> _pendingShutdown;
		std::thread _backgroundThread;

		std::mutex _interfaceLock;

		////////////////////////////////////////////////////////

		struct PendingOnceInitiate
		{
			std::shared_ptr<IConduitProducer> _producer;
			std::promise<std::any> _promise;
		};
		std::vector<PendingOnceInitiate> _pendingOnceInitiates;

		////////////////////////////////////////////////////////

		struct ChangeEvent
		{
			std::shared_ptr<IConduitProducer> _producer;
			std::weak_ptr<IConduitConsumer> _consumer;
			std::promise<void> _onChangePromise;
		};
		std::vector<ChangeEvent> _pendingEventConnects;
		std::vector<ChangeEvent> _pendingEventDisconnects;

		////////////////////////////////////////////////////////

		Pimpl() : _pendingShutdown(false)
		{
			_interruptPollEvent = eventfd(0, EFD_NONBLOCK);
			_backgroundThread = std::thread(
				[this]() {
					TRY {
						this->ThreadFunction(); 
					} CATCH(const std::exception& e) {
						Log(Error) << "Encountered exception in background epoll thread. Terminating any asynchronous operations" << std::endl;
						Log(Error) << "Exception as follows: " << e.what() << std::endl;
					} CATCH(...) {
						Log(Error) << "Encountered exception in background epoll thread. Terminating any asynchronous operations" << std::endl;
						Log(Error) << "Unknown exception type" << std::endl;
					} CATCH_END
				});
		}

		~Pimpl()
		{
			_pendingShutdown.store(true);
			InterruptBackgroundThread();
			_backgroundThread.join();
			close(_interruptPollEvent);
		}

		void ThreadFunction()
		{
			int epollContext = epoll_create1(0);
			if (epollContext < 0)
				Throw(std::runtime_error("Failure in epoll_create1"));

			auto cleanup = AutoCleanup(
				[epollContext]() { 
					close(epollContext); 
				});
			(void)cleanup;

			{
				auto readEvent = EPollEvent(PollingEventType::Input, false);
				readEvent.data.fd = _interruptPollEvent;
				auto ret = epoll_ctl(epollContext, EPOLL_CTL_ADD, _interruptPollEvent, &readEvent);
				if (ret < 0)
					Throw(std::runtime_error("Failure when adding interrupt event to epoll queue"));
			}

			struct ActiveOnceEvent
			{
				std::shared_ptr<IConduitProducer> _producer;
				std::promise<std::any> _promise;
				IOPlatformHandle _platformHandle;
			};
			std::vector<ActiveOnceEvent> activeOnceEvents;
			struct ActiveEvent
			{
				std::shared_ptr<IConduitProducer> _producer;
				std::weak_ptr<IConduitConsumer> _consumer;
				IOPlatformHandle _platformHandle;
			};
			std::vector<ActiveEvent> activeEvents;

			struct epoll_event events[32];
			while (!_pendingShutdown.load()) {
				// add/remove all events that are pending a state change
				{
					std::vector<std::promise<void>> pendingPromisesToTrigger;
					std::vector<std::pair<std::promise<void>, std::exception_ptr>> pendingExceptionsToPropagate1;
					std::vector<std::pair<std::promise<std::any>, std::exception_ptr>> pendingExceptionsToPropagate2;
					{
						ScopedLock(_interfaceLock);
						for (auto& event:_pendingOnceInitiates) {
							auto existing = std::find_if(
								activeOnceEvents.begin(), activeOnceEvents.end(),
								[&event](const auto& ae) { return PointersEquivalent(event._producer, ae._producer); });
							if (existing != activeOnceEvents.end()) {
								pendingExceptionsToPropagate2.push_back({std::move(event._promise), std::make_exception_ptr(std::runtime_error("Attempting to connect a producer that is already connected"))});
								continue;
							}
							
							auto* conduitPlatformHandle = dynamic_cast<IConduitProducer_PlatformHandle*>(event._producer.get());
							if (!conduitPlatformHandle) {
								pendingExceptionsToPropagate2.push_back({std::move(event._promise), std::make_exception_ptr(std::runtime_error("Unknown conduit producer type"))});
								continue;
							}

							auto platformHandle = conduitPlatformHandle->GetPlatformHandle();
							if (platformHandle < 0) {
								pendingExceptionsToPropagate2.push_back({std::move(event._promise), std::make_exception_ptr(std::runtime_error("Invalid platform handle on conduit passed to RespondOnce"))});
								continue;
							}

							existing = std::find_if(
								activeOnceEvents.begin(), activeOnceEvents.end(),
								[platformHandle](const auto& ae) { return ae._platformHandle == platformHandle; });
							if (existing != activeOnceEvents.end()) {
								// We can't queue multiple poll operations on the same platform handle, because we will be using
								// the platform handle to lookup events in activeOnceEvents (this would otherwise make it ambigious)
								pendingExceptionsToPropagate2.push_back({std::move(event._promise), std::make_exception_ptr(std::runtime_error("Multiple asynchronous events queued for the same platform handle"))});
								continue;
							}

							auto evt = EPollEvent(conduitPlatformHandle->GetListenTypes());
							evt.data.fd = platformHandle;
							auto ret = epoll_ctl(epollContext, EPOLL_CTL_ADD, platformHandle, &evt);
							if (ret < 0) {
								pendingExceptionsToPropagate2.push_back({std::move(event._promise), std::make_exception_ptr(std::runtime_error("Failed to add asyncronous event to epoll queue"))});
							} else {
								ActiveOnceEvent activeEvent;
								activeEvent._platformHandle = platformHandle;
								activeEvent._promise = std::move(event._promise);
								activeEvent._producer = std::move(event._producer);
								activeOnceEvents.push_back(std::move(activeEvent));
							}
						}
						_pendingOnceInitiates.clear();

						for (auto& event:_pendingEventConnects) {
							auto existing = std::find_if(
								activeEvents.begin(), activeEvents.end(),
								[&event](const auto& ae) { return PointersEquivalent(event._producer, ae._producer); });
							if (existing != activeEvents.end()) {
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Attempting to connect a producer that is already connected"))});
								continue;
							}

							// If the conduit is already expired, the system will just end up removing the event immediately
							// so let's not even bother adding it in that case
							auto conduit = event._consumer.lock();
							if (!conduit) {
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Conduit ptr already expired before connection"))});
								continue;
							}

							auto* conduitPlatformHandle = dynamic_cast<IConduitProducer_PlatformHandle*>(event._producer.get());
							if (!conduitPlatformHandle) {
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Unknown conduit producer type"))});
								continue;
							}

							auto platformHandle = conduitPlatformHandle->GetPlatformHandle();
							if (platformHandle < 0) {
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Invalid platform handle on conduit passed to Connect"))});
								continue;
							}

							existing = std::find_if(
								activeEvents.begin(), activeEvents.end(),
								[platformHandle](const auto& ae) { return ae._platformHandle == platformHandle; });
							if (existing != activeEvents.end()) {
								// We can't queue multiple poll operations on the same platform handle, because we will be using
								// the platform handle to lookup events in activeOnceEvents (this would otherwise make it ambigious)
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Multiple asynchronous events queued for the same platform handle"))});
								continue;
							}

							auto evt = EPollEvent(conduitPlatformHandle->GetListenTypes(), false);
							evt.data.fd = platformHandle;
							auto ret = epoll_ctl(epollContext, EPOLL_CTL_ADD, platformHandle, &evt);
							if (ret < 0) {
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Failed to add asyncronous event to epoll queue"))});
							} else {
								ActiveEvent activeEvent;
								activeEvent._platformHandle = platformHandle;
								activeEvent._producer = std::move(event._producer);
								activeEvent._consumer = std::move(event._consumer);
								activeEvents.push_back(std::move(activeEvent));
								pendingPromisesToTrigger.push_back(std::move(event._onChangePromise));
							}
						}
						_pendingEventConnects.clear();

						for (auto& event:_pendingEventDisconnects) {
							auto existing = std::find_if(
								activeEvents.begin(), activeEvents.end(),
								[&event](const auto& ae) { return PointersEquivalent(event._producer, ae._producer); });
							if (existing == activeEvents.end()) {
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Attempting to disconnect an event that is not currently connected"))});
								continue;
							}

							auto ret = epoll_ctl(epollContext, EPOLL_CTL_DEL, existing->_platformHandle, nullptr);
							if (ret < 0) {
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Failed to remove asyncronous event to epoll queue"))});
							} else {
								pendingPromisesToTrigger.push_back(std::move(event._onChangePromise));
							}

							activeEvents.erase(existing);
						}
						_pendingEventDisconnects.clear();

						// if any conduits have expired, we can go ahead and quietly remove them from the 
						// epoll context. It's better to get an explicit disconnect, but this at least cleans
						// up anything hanging. Note that we're expecting the conduit to have destroyed the 
						// platform handle when it was cleaned up (in other words, that platform handle is now dangling)
						for (auto i=activeEvents.begin(); i!=activeEvents.end();) {
							if (i->_consumer.expired()) {
								auto ret = epoll_ctl(epollContext, EPOLL_CTL_DEL, i->_platformHandle, nullptr);
								if (ret < 0)
									Log(Error) << "Got error return from epoll_ctl when removing expired event" << std::endl;
								i = activeEvents.erase(i);
							} else
								++i;
						}
					}

					// We wait until we unlock _interfaceLock before we trigger the promises
					// this may change the order in which set_exception and set_value will happen
					// But it avoids complication if there are any continuation functions that happen
					// on the same thread and interact with the PollingThread class
					for (auto&p:pendingExceptionsToPropagate1)
						p.first.set_exception(std::move(p.second));
					for (auto&p:pendingExceptionsToPropagate2)
						p.first.set_exception(std::move(p.second));
					for (auto&p:pendingPromisesToTrigger)
						p.set_value();
				}
				
				errno = 0;
				const int timeoutInMilliseconds = -1;
				int eventCount = epoll_wait(epollContext, events, dimof(events), timeoutInMilliseconds);
				if (eventCount <= 0 || eventCount > dimof(events)) {
					// We will actually get here during normal shutdown. When the main
					// thread calls _backgroundThread.join(), it seems to trigger an interrupt event on the epoll system
					// automatically. In that case, errno will be EINTR. Since this happens during normal usage,
					// we can't treat this as an error.
					auto e = errno;
					if (e != EINTR) {
						// This is a low-level failure. No further operations will be processed; so let's propage
						// exception messages to everything waiting. Most importantly, promised will not be completed,
						// so we must set them into exception state
						auto msgToPropagate = "PollingThread received an error message during wait: " + std::to_string(errno);
						for (auto&e:activeOnceEvents)
							e._promise.set_exception(std::make_exception_ptr(std::runtime_error(msgToPropagate)));
						for (auto&e:activeEvents) {
							auto consumer = e._consumer.lock();
							if (consumer)
								consumer->OnException(std::make_exception_ptr(std::runtime_error(msgToPropagate)));
						}
						{
							ScopedLock(_interfaceLock);
							for (auto& e:_pendingOnceInitiates)
								e._promise.set_exception(std::make_exception_ptr(std::runtime_error(msgToPropagate)));
							for (auto& e:_pendingEventConnects)
								e._onChangePromise.set_exception(std::make_exception_ptr(std::runtime_error(msgToPropagate)));
							for (auto& e:_pendingEventDisconnects)
								e._onChangePromise.set_exception(std::make_exception_ptr(std::runtime_error(msgToPropagate)));
							_pendingOnceInitiates.clear();
							_pendingEventConnects.clear();
							_pendingEventDisconnects.clear();
						}
						Throw(std::runtime_error("Failure in epoll_wait: " + std::to_string(errno)));
					}
					break;
				}

				for (const auto& triggeredEvent:MakeIteratorRange(events, &events[eventCount])) {

					// _interruptPollEvent exists only to break us out of "epoll_wait", so don't need to do much in this case
					// We should just drain _interruptPollEvent of all data we can read. Since we're not using EFD_SEMAPHORE
					// for this event, we should just have to read one
					if (triggeredEvent.data.fd == _interruptPollEvent) {
						uint64_t eventFdCounter=0;
						auto ret = read(_interruptPollEvent, &eventFdCounter, sizeof(eventFdCounter));
						assert(ret > 0);
						continue;
					}

					auto onceEvent = std::find_if(
						activeOnceEvents.begin(), activeOnceEvents.end(),
						[&triggeredEvent](const auto& ae) { return ae._platformHandle == triggeredEvent.data.fd; });
					if (onceEvent != activeOnceEvents.end()) {
						if (triggeredEvent.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
							// this is a disconnection or error event. We should return an exception and also remove
							// the event from both the queue and our list of active events
							auto ret = epoll_ctl(epollContext, EPOLL_CTL_DEL, triggeredEvent.data.fd, nullptr);
							assert(ret == 0);
							auto promise = std::move(onceEvent->_promise);
							activeOnceEvents.erase(onceEvent);
							
							promise.set_exception(std::make_exception_ptr(std::runtime_error("")));
						} else if (triggeredEvent.events & (EPOLLIN | EPOLLOUT)) {
							// This means data is available to read, or the fd is ready for writing to
							// It's effectively a success. Still, for one-shot events, we will remove the event
							// It seems that fd will still be registered in the epollContext, even for an event
							// marked as a one-shot (it just gets set to a disabled state)
							auto ret = epoll_ctl(epollContext, EPOLL_CTL_DEL, triggeredEvent.data.fd, nullptr);
							assert(ret == 0);
							auto promise = std::move(onceEvent->_promise);
							auto producer = std::move(onceEvent->_producer);
							activeOnceEvents.erase(onceEvent);

							std::any payload;
							auto* conduitPlatformHandle = dynamic_cast<IConduitProducer_PlatformHandle*>(producer.get());
							if (conduitPlatformHandle) {
								TRY {
									payload = conduitPlatformHandle->GeneratePayload(AsPollingEventType(triggeredEvent.events));
								} CATCH(...) {
									promise.set_exception(std::current_exception());
									continue;
								} CATCH_END
							} else {
								payload = AsPollingEventType(triggeredEvent.events);
							}
							
							promise.set_value(std::move(payload));
						} else {
							Log(Error) << "Unexpected event trigger value in PollingThread" << std::endl;
						}

						continue;
					}

					auto evnt = std::find_if(
						activeEvents.begin(), activeEvents.end(),
						[&triggeredEvent](const auto& ae) { return ae._platformHandle == triggeredEvent.data.fd; });
					if (evnt != activeEvents.end()) {
						if (triggeredEvent.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
							// After any of these error cases, we always remove the event from the list of active events
							// The client must reconnect the conduit if they want to receive anything new from it
							auto ret = epoll_ctl(epollContext, EPOLL_CTL_DEL, triggeredEvent.data.fd, nullptr);
							assert(ret == 0);
							auto conduit = evnt->_consumer.lock();
							activeEvents.erase(evnt);
							if (conduit)
								conduit->OnException(std::make_exception_ptr(std::runtime_error("Received a low level hangup or error message")));
						} else if (triggeredEvent.events & (EPOLLIN | EPOLLOUT)) {
							// Ready for read/write. We don't remove the event from the epoll context in this case
							auto consumer = evnt->_consumer.lock();
							if (consumer) {

								std::any payload;
								auto* conduitPlatformHandle = dynamic_cast<IConduitProducer_PlatformHandle*>(evnt->_producer.get());
								if (conduitPlatformHandle) {
									TRY {
										payload = conduitPlatformHandle->GeneratePayload(AsPollingEventType(triggeredEvent.events));
									} CATCH(...) {
										consumer->OnException(std::current_exception());
										continue;
									} CATCH_END
								} else {
									payload = AsPollingEventType(triggeredEvent.events);
								}

								TRY {
									consumer->OnEvent(std::move(payload));
								} CATCH(const std::exception& e) {
								Log(Error) << "Suppressed exception from IConduitConsumer: " << e.what() << std::endl;
							} CATCH(...) {
								Log(Error) << "Suppressed unknown exception from IConduitConsumer" << std::endl;
							} CATCH_END
							} else {
								Log(Verbose) << "PollingThread event generated for consumer that is expired" << std::endl;
							}
						} else {
							Throw(std::runtime_error("Unexpected event trigger value"));
						}
						continue;
					}

					Log(Error) << "Got an event for a platform handle that isn't in our activeEvents list" << std::endl;
				}
			}

			// We're ending all waiting. We must set any remainding promises to exception status, because they
			// will never be completed
			auto msgToPropagate = "Event cannot complete because PollingThread is shutting down";
			for (auto&e:activeOnceEvents)
				e._promise.set_exception(std::make_exception_ptr(std::runtime_error(msgToPropagate)));
			{
				ScopedLock(_interfaceLock);
				for (auto& e:_pendingOnceInitiates)
					e._promise.set_exception(std::make_exception_ptr(std::runtime_error(msgToPropagate)));
				for (auto& e:_pendingEventConnects)
					e._onChangePromise.set_exception(std::make_exception_ptr(std::runtime_error(msgToPropagate)));
				for (auto& e:_pendingEventDisconnects)
					e._onChangePromise.set_exception(std::make_exception_ptr(std::runtime_error(msgToPropagate)));
			}
		}

		void InterruptBackgroundThread()
		{
			ssize_t ret = 0;
			do {
				uint64_t counterIncrement = 1;
				ret = write(_interruptPollEvent, &counterIncrement, sizeof(counterIncrement));
			} while (ret < 0 && errno == EAGAIN);
		}
	};

	auto PollingThread::RespondOnce(const std::shared_ptr<IConduitProducer>& producer) -> std::future<std::any>
	{
		std::future<std::any> result;
		{
			ScopedLock(_pimpl->_interfaceLock);
			Pimpl::PendingOnceInitiate pendingInit;
			pendingInit._producer = producer;
			result = pendingInit._promise.get_future();
			_pimpl->_pendingOnceInitiates.push_back(std::move(pendingInit));
		}
		_pimpl->InterruptBackgroundThread();
		return result;
	}

	std::future<void> PollingThread::Connect(
		const std::shared_ptr<IConduitProducer>& producer, 
		const std::shared_ptr<IConduitConsumer>& consumer)
	{
		std::future<void> result;
		{
			ScopedLock(_pimpl->_interfaceLock);
			Pimpl::ChangeEvent change;
			change._producer = producer;
			change._consumer = consumer;
			result = change._onChangePromise.get_future();
			_pimpl->_pendingEventConnects.push_back(std::move(change));
		}
		_pimpl->InterruptBackgroundThread();
		return result;
	}

	std::future<void> PollingThread::Disconnect(
		const std::shared_ptr<IConduitProducer>& producer)
	{
		std::future<void> result;
		{
			ScopedLock(_pimpl->_interfaceLock);
			Pimpl::ChangeEvent change;
			change._producer = producer;
			result = change._onChangePromise.get_future();
			_pimpl->_pendingEventDisconnects.push_back(std::move(change));
		}
		_pimpl->InterruptBackgroundThread();
		return result;
	}

	PollingThread::PollingThread()
	{
		_pimpl = std::make_unique<Pimpl>();
	}

	PollingThread::~PollingThread()
	{

	}

	class RealUserEvent : public IConduitProducer, public IConduitProducer_PlatformHandle
	{
	public:
		int _platformHandle;

		IOPlatformHandle GetPlatformHandle() const override { return _platformHandle; }
		PollingEventType::BitField GetListenTypes() const override { return PollingEventType::Input; }
		std::any GeneratePayload(PollingEventType::BitField triggeredEvents) override 
		{
			// Unlike Windows, eventfd will not automatically decrease the counter in an
			// event or semaphore. We need to explicitly read it to decrease it. We should
			// do this in the same thread that waits in order to ensure that we can return
			// to an unsignalled state before the next wait,
			// In effect this will replicate the same behaviour as windows -- ie there's
			// one automatic decrease for per thread wake-up
			uint64_t eventFdCounter=0;
			auto ret = read(_platformHandle, &eventFdCounter, sizeof(eventFdCounter));
			assert(ret > 0);
			return eventFdCounter; 
		}

		RealUserEvent(UserEvent::Type type)
		{
			if (type == UserEvent::Type::Semaphore) {
				_platformHandle = eventfd(0, EFD_NONBLOCK|EFD_SEMAPHORE);
			} else {
				_platformHandle = eventfd(0, EFD_NONBLOCK);
			}
		}

		~RealUserEvent()
		{
			if (_platformHandle != -1)
				close(_platformHandle);
		}

		RealUserEvent& operator=(RealUserEvent&& moveFrom) never_throws
		{
			if (_platformHandle != -1)
				close(_platformHandle);
			_platformHandle = moveFrom._platformHandle;
			moveFrom._platformHandle = -1;
			return *this;
		}
		
		RealUserEvent(RealUserEvent&& moveFrom) never_throws
		{
			_platformHandle = moveFrom._platformHandle;
			moveFrom._platformHandle = -1;
		}
	};

	void UserEvent::IncreaseCounter()
	{
		auto* that = (RealUserEvent*)this;
		uint64_t eventFdCounter = 1;
		auto ret = write(that->_platformHandle, &eventFdCounter, sizeof(eventFdCounter));
		assert(ret > 0);
	}

	std::shared_ptr<UserEvent> CreateUserEvent(UserEvent::Type type)
	{
		RealUserEvent* result = new RealUserEvent(type);
		// Little trick here -- UserEventactually a dummy class that only provides
		// it's method signatures
		return std::shared_ptr<UserEvent>((UserEvent*)result);
	}
	
}


