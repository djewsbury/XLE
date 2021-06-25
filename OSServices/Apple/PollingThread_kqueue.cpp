// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "System_Apple.h"
#include "../PollingThread.h"
#include "../Log.h"
#include "../../Utility/FunctionUtils.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Core/Exceptions.h"

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include <atomic>

namespace OSServices
{
	template<typename Ptr1, typename Ptr2>
		bool PointersEquivalent(const Ptr1& lhs, const Ptr2& rhs) { return !lhs.owner_before(rhs) && !rhs.owner_before(lhs); }

	class RealUserEvent : public IConduitProducer
	{
	public:
		UserEvent::Type _type;
		static unsigned _nextUserEventId;
		unsigned _userEventId = ~0u;

		// We need to use a mutex lock here 
		Threading::Mutex _counterLock;
		unsigned _counter = 0;
		int _kqueueContext = -1;

		RealUserEvent(UserEvent::Type type) :  _type(type)
		{
			_kqueueContext = -1;
			_userEventId = _nextUserEventId++;
			_counter = 0;
		}

		~RealUserEvent()
		{
			// We're expecting to have been released from the kqueueContext already
			// If we hit this assert it means that this event id is still registered with a kqueue context
			// that's not correct because nothing can respond to the event after we destroy this object 
			assert(_kqueueContext == -1);
		}

		RealUserEvent& operator=(RealUserEvent&& moveFrom) never_throws = delete;
		RealUserEvent(RealUserEvent&& moveFrom) never_throws = delete;
	};

	unsigned RealUserEvent::_nextUserEventId = 1000;

	class PollingThread::Pimpl : public std::enable_shared_from_this<PollingThread::Pimpl>
	{
	public:
		std::atomic<bool> _pendingShutdown;
		std::thread _backgroundThread;
		unsigned _interruptUserEventId = 0;
		int _kqueueContext = -1;
		unsigned _userEventQueueRefCount = 0;

		std::mutex _interfaceLock;

		////////////////////////////////////////////////////////

		struct PendingOnceInitiate
		{
			std::shared_ptr<IConduitProducer> _producer;
			std::promise<std::any> _promise;
		};
		std::vector<PendingOnceInitiate> _pendingOnceInitiates;

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
			_kqueueContext = kqueue();
			if (_kqueueContext == -1)
				Throw(std::runtime_error("kqueue failed to initialize a new event queue. Error code: " + std::to_string(errno)));

			{
				_interruptUserEventId = RealUserEvent::_nextUserEventId++;
				struct kevent eventChange {
					.ident = _interruptUserEventId,
					.filter = EVFILT_USER,
					.flags = EV_ADD | EV_RECEIPT | EV_CLEAR,
					.fflags = 0,
					.data = 0,
					.udata = 0
				};
				struct kevent receiveError;
				auto keventRes = kevent(_kqueueContext, &eventChange, 1, &receiveError, 1, nullptr);
				// Because were using EV_RECEIPT mode; we should get one response with the EV_ERROR flag
				// set. As per the kqueue docs, "When a filter is successfully added the data field will be	zero."
				assert(keventRes == 1);
				assert(receiveError.flags & EV_ERROR);
				assert(receiveError.data == 0);
			}

			_backgroundThread = std::thread(
				[this]() {
					TRY {
						this->ThreadFunction(); 
					} CATCH(const std::exception& e) {
						Log(Error) << "Encountered exception in background kqueue thread. Terminating any asynchronous operations" << std::endl;
						Log(Error) << "Exception as follows: " << e.what() << std::endl;
					} CATCH(...) {
						Log(Error) << "Encountered exception in background kqueue thread. Terminating any asynchronous operations" << std::endl;
						Log(Error) << "Unknown exception type" << std::endl;
					} CATCH_END
				});
		}

		~Pimpl()
		{
			_pendingShutdown.store(true);
			InterruptBackgroundThread();
			_backgroundThread.join();
			// _userEventQueueRefCount is the number of user events that have a reference on our _kqueueContext
			// it has to be the zero here, or we've got a leaked user event somewhere
			assert(_userEventQueueRefCount == 0);
			close(_kqueueContext);
		}

		Pimpl(const Pimpl&) = delete;
		Pimpl&operator=(const Pimpl&) = delete;

		void TryReleaseKQueue(IConduitProducer& producer)
		{
			// Don't throw exceptions from here, because we will use this while
			// processing other exceptions and shutting down the system
			auto* platformHandleProducer = dynamic_cast<RealUserEvent*>(&producer);
			if (platformHandleProducer) {
				ScopedLock(platformHandleProducer->_counterLock);
				if (platformHandleProducer->_kqueueContext != _kqueueContext) {
					Log(Error) << "Found RealUserEvent that is not attached to the expected kqueue" << std::endl;
				} else {
					platformHandleProducer->_kqueueContext = -1;
					--_userEventQueueRefCount;
				}
			}
		}

		void TryThreadRelease(IConduitProducer& producer)
		{
			auto* platformHandleProducer = dynamic_cast<RealUserEvent*>(&producer);
			if (platformHandleProducer) {
				ScopedLock(platformHandleProducer->_counterLock);
				if (platformHandleProducer->_kqueueContext != _kqueueContext) {
					Log(Error) << "Found RealUserEvent that is not attached to the expected kqueue" << std::endl;
					return;
				}
				if (platformHandleProducer->_type == UserEvent::Type::Binary) {
					platformHandleProducer->_counter = 0;
				} else {
					assert(platformHandleProducer->_type == UserEvent::Type::Semaphore);
					--platformHandleProducer->_counter;
				}
				platformHandleProducer->_kqueueContext = -1;
				--_userEventQueueRefCount;
			}
		}

		void ThreadFunction()
		{
			assert(_kqueueContext != -1);
			struct ActiveOnceEvent
			{
				std::shared_ptr<IConduitProducer> _producer;
				std::promise<std::any> _promise;
			};
			struct ActiveEvent
			{
				std::shared_ptr<IConduitProducer> _producer;
				std::weak_ptr<IConduitConsumer> _consumer;
			};
			std::vector<ActiveEvent> activeEvents;
			std::vector<ActiveOnceEvent> activeOnceEvents;
			activeEvents.reserve(64);
			activeOnceEvents.reserve(64);

			while (!_pendingShutdown.load()) {
				// add/remove all events that are pending a state change
				{
					std::vector<std::promise<void>> pendingPromisesToTrigger;
					std::vector<std::pair<std::promise<void>, std::exception_ptr>> pendingExceptionsToPropagate1;
					std::vector<std::pair<std::promise<std::any>, std::exception_ptr>> pendingExceptionsToPropagate2;
					std::vector<PendingOnceInitiate> immediateActivates;
					{
						ScopedLock(_interfaceLock);
						for (auto& event:_pendingOnceInitiates) {
							auto existing = std::find_if(
								activeOnceEvents.begin(), activeOnceEvents.end(),
								[&event](const auto& ae) { return PointersEquivalent(event._producer, ae._producer); });
							if (existing != activeOnceEvents.end()) {
								// We can't queue multiple poll operations on the same platform handle, because we will be using
								// the platform handle to lookup events in activeOnceEvents (this would otherwise make it ambigious)
								pendingExceptionsToPropagate2.emplace_back(std::move(event._promise), std::make_exception_ptr(std::runtime_error("Multiple asynchronous events queued for the same platform handle")));
								continue;
							}

							auto* platformHandleProducer = dynamic_cast<RealUserEvent*>(event._producer.get());
							if (!platformHandleProducer) {
								pendingExceptionsToPropagate2.emplace_back(std::move(event._promise), std::make_exception_ptr(std::runtime_error("Expecting user event based conduit to be used with RespondOnce call")));
								continue;
							}

							ScopedLock(platformHandleProducer->_counterLock);
							if (platformHandleProducer->_kqueueContext != -1) {
								// We caqn't use the same user event in multiple polling threads at the same time in
								// this implementation. In other implementations (epoll, Win32, etc) that might not be
								// an issue -- but it's just the way teh classes are configured here
								// going back to prevKqueueContext would not be completely safe. Let's just go back to -1
								// instead
								pendingExceptionsToPropagate2.emplace_back(std::move(event._promise), std::make_exception_ptr(std::runtime_error("Attempting to connect a user event with this polling thread, when it is already connected to another polling thread")));
								continue;
							}

							// If the counter already has a value, we consider it already primed and trigger immediately
							// We modify the counter value immediately and activate the promise after we've released our locks
							if (platformHandleProducer->_counter > 0) {
								if (platformHandleProducer->_type == UserEvent::Type::Binary) {
									platformHandleProducer->_counter = 0;
								} else {
									assert(platformHandleProducer->_type == UserEvent::Type::Semaphore);
									--platformHandleProducer->_counter;
								}
								immediateActivates.push_back(std::move(event));
								continue;
							}

							platformHandleProducer->_kqueueContext = _kqueueContext;
							++_userEventQueueRefCount;

							// EVFILT_USER is added in OSX 10.6
							struct kevent eventChange {
								.ident = platformHandleProducer->_userEventId,
								.filter = EVFILT_USER,			// use EVFILT_VNODE for file system watching
								.flags = EV_ADD | EV_RECEIPT | EV_CLEAR | EV_ONESHOT,
								.fflags = 0,
								.data = 0,
								.udata = event._producer.get()	// note that for user events, this is actually ignored. What matters is the udata ptr when we trigger the event, not when we register the listener here
							};
							struct kevent receiveError;
							auto keventRes = kevent(_kqueueContext, &eventChange, 1, &receiveError, 1, nullptr);
							assert(keventRes == 1);
							assert(receiveError.flags & EV_ERROR);
							assert(receiveError.data == 0);			// see above; expecting one return with EV_ERROR & data field set to 0

							ActiveOnceEvent activeEvent;
							activeEvent._producer = event._producer;
							activeEvent._promise = std::move(event._promise);
							activeOnceEvents.push_back(std::move(activeEvent));
						}
						_pendingOnceInitiates.clear();

						for (auto& event:_pendingEventConnects) {
							auto existing = std::find_if(
								activeEvents.begin(), activeEvents.end(),
								[&event](const auto& ae) { return PointersEquivalent(event._producer, ae._producer); });
							if (existing != activeEvents.end()) {
								// We can't queue multiple poll operations on the same platform handle, because we will be using
								// the platform handle to lookup events in activeOnceEvents (this would otherwise make it ambigious)
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Multiple asynchronous events queued for the same conduit"))});
								continue;
							}

							auto* keventProducer = dynamic_cast<Internal::KEvent*>(event._producer.get());
							if (keventProducer) {
								struct kevent eventChange {
									.ident = keventProducer->_ident,
									.filter = keventProducer->_filter,
									.flags = EV_ADD | EV_RECEIPT | EV_CLEAR | EV_ENABLE,
									.fflags = keventProducer->_fflags,
									.data = 0,
									.udata = event._producer.get()
								};
								struct kevent receiveError;
								auto keventRes = kevent(_kqueueContext, &eventChange, 1, &receiveError, 1, nullptr);
								assert(keventRes == 1);
								assert(receiveError.flags & EV_ERROR);
								assert(receiveError.data == 0);
							} else {
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Unknown conduit producer type"))});
								continue;
							}
							
							ActiveEvent activeEvent;
							activeEvent._producer = std::move(event._producer);
							activeEvent._consumer = std::move(event._consumer);
							activeEvents.push_back(std::move(activeEvent));
							pendingPromisesToTrigger.push_back(std::move(event._onChangePromise));
						}
						_pendingEventConnects.clear();

						for (auto& event:_pendingEventDisconnects) {
							auto existing = std::find_if(
								activeEvents.begin(), activeEvents.end(),
								[&event](const auto& ae) { return PointersEquivalent(event._producer, ae._producer); });
							if (existing == activeEvents.end()) {
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Attempting to disconnect a conduit that is not currently connected"))});
								continue;
							}

							auto* keventProducer = dynamic_cast<Internal::KEvent*>(existing->_producer.get());
							if (keventProducer) {
								struct kevent eventChange {
									.ident = keventProducer->_ident,
									.filter = keventProducer->_filter,
									.flags = EV_DELETE | EV_RECEIPT,
									.fflags = keventProducer->_fflags,
									.data = 0,
									.udata = 0
								};
								struct kevent receiveError;
								auto keventRes = kevent(_kqueueContext, &eventChange, 1, &receiveError, 1, nullptr);
								assert(keventRes == 1);
								assert(receiveError.flags & EV_ERROR);
								assert(receiveError.data == 0);
							} else {
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Unknown conduit producer type"))});
								continue;
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
								auto* keventProducer = dynamic_cast<Internal::KEvent*>(i->_producer.get());
								if (keventProducer) {
									struct kevent eventChange {
										.ident = keventProducer->_ident,
										.filter = keventProducer->_filter,
										.flags = EV_DELETE | EV_RECEIPT,
										.fflags = keventProducer->_fflags,
										.data = 0,
										.udata = 0
									};
									struct kevent receiveError;
									auto keventRes = kevent(_kqueueContext, &eventChange, 1, &receiveError, 1, nullptr);
									assert(keventRes == 1);
									assert(receiveError.flags & EV_ERROR);
									assert(receiveError.data == 0);
								} else {
									Log(Error) << "Unknown producer type when removing event due to consumer begin expired" << std::endl;
								}
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

					for (auto&i:immediateActivates)
						i._promise.set_value(PollingEventType::Input);	// we've modified the counter value already
				}

				// call kevent to wait for any events to happen
				struct kevent triggeredEvents[64];
				auto completedEventCount = kevent(_kqueueContext, nullptr, 0, triggeredEvents, dimof(triggeredEvents), nullptr);
				if (completedEventCount == -1) {
					// This is a low-level failure. No further operations will be processed; so let's propage
					// exception messages to everything waiting. Most importantly, promised will not be completed,
					// so we must set them into exception state
					auto msgToPropagate = "PollingThread received an error during wait. Errno: " + std::to_string(errno);
					for (auto&e:activeOnceEvents) {
						TryReleaseKQueue(*e._producer);
						e._promise.set_exception(std::make_exception_ptr(std::runtime_error(msgToPropagate)));
					}
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
					Throw(std::runtime_error(msgToPropagate));
					break;
				}

				assert(completedEventCount < dimof(triggeredEvents));
				for (const auto& evnt : MakeIteratorRange(triggeredEvents, &triggeredEvents[std::min(completedEventCount, (int)dimof(triggeredEvents))])) {
					auto isError = evnt.flags & EV_ERROR;

					if (evnt.filter == EVFILT_USER && evnt.ident == _interruptUserEventId) {
						if (isError)
							Log(Error) << "Recieved error while waiting on queue thread interrupt event" << std::endl;
						continue;
					}

					// We use the "udata" field to lookup the activeEvent or activeOnceEvent

					auto a = std::find_if(
						activeOnceEvents.begin(), activeOnceEvents.end(),
						[&evnt](const auto &c) { return c._producer.get() == evnt.udata; });
					if (a != activeOnceEvents.end()) {
						TryThreadRelease(*a->_producer);

						if (isError) {
							a->_promise.set_exception(std::make_exception_ptr(std::runtime_error("Event failed in low level kqueue service")));
						} else {
							a->_promise.set_value(PollingEventType::Input);
						}
						// The event is marked as one shot in it's registration; it gets removed from kqueue automatically
						assert(evnt.flags & EV_ONESHOT);
						activeOnceEvents.erase(a);
						continue;
					}

					auto a2 = std::find_if(
						activeEvents.begin(), activeEvents.end(),
						[&evnt](const auto &c) { return c._producer.get() == evnt.udata; });
					if (a2 != activeEvents.end()) {
						TryThreadRelease(*a2->_producer);

						auto consumer = a2->_consumer.lock();
						if (consumer) {
							Internal::KEventTriggerPayload triggerPayload { evnt.flags, evnt.fflags };
							std::any payload;

							if (!isError) {
								TRY {
									auto* kevent = dynamic_cast<Internal::KEvent*>(a2->_producer.get());
									if (kevent) {
										payload = kevent->GeneratePayload(triggerPayload);
									} else {
										payload = triggerPayload;
									}
								} CATCH(...) {
									consumer->OnException(std::current_exception());
									continue;
								} CATCH_END
							}

							TRY {
								if (!isError) {
									consumer->OnEvent(std::move(payload));
								} else {
									consumer->OnException(nullptr);
								}
							} CATCH(const std::exception& e) {
								Log(Error) << "Suppressed exception from IConduitConsumer: " << e.what() << std::endl;
							} CATCH(...) {
								Log(Error) << "Suppressed unknown exception from IConduitConsumer" << std::endl;
							} CATCH_END
						} else {
							Log(Verbose) << "PollingThread event generated for consumer that is expired" << std::endl;
						}
						continue;
					}

					Log(Error) << "Received an event for an event that could not be found in our active event list" << std::endl;
				}
			}

			// We're ending all waiting. We must set any remainding promises to exception status, because they
			// will never be completed
			auto msgToPropagate = "Event cannot complete because PollingThread is shutting down";
			for (auto&e:activeOnceEvents) {
				TryReleaseKQueue(*e._producer);
				e._promise.set_exception(std::make_exception_ptr(std::runtime_error(msgToPropagate)));
			}
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
			struct kevent eventChange {
				.ident = _interruptUserEventId,
				.filter = EVFILT_USER,
				.flags = 0,
				.fflags = NOTE_TRIGGER,
				.data = 0,
				.udata = 0
			};
			auto keventRes = kevent(_kqueueContext, &eventChange, 1, nullptr, 0, nullptr);
			assert(keventRes == 0);
		}
	};

	auto PollingThread::RespondOnce(const std::shared_ptr<IConduitProducer>& producer) -> std::future<std::any>
	{
		assert(producer);
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
		assert(producer && consumer);
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
		assert(producer);
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
		_pimpl = std::make_shared<Pimpl>();
	}

	PollingThread::~PollingThread()
	{
	}

//////////////////////////////////////////////////////////////////////////////////////////////////

	void UserEvent::IncreaseCounter()
	{
		auto* that = (RealUserEvent*)this;
		ScopedLock(that->_counterLock);
		++that->_counter;
		if (that->_kqueueContext != -1) {
			struct kevent eventChange {
				.ident = that->_userEventId,
				.filter = EVFILT_USER,
				.flags = 0,
				.fflags = NOTE_TRIGGER,
				.data = 0,
				.udata = (IConduitProducer*)this		// since we use the producer* to lookup the item, we have to set it here. Note that it matters here, not where we register the event
			};
			auto keventRes = kevent(that->_kqueueContext, &eventChange, 1, nullptr, 0, nullptr);
			assert(keventRes == 0);
		}
	}

	std::shared_ptr<UserEvent> CreateUserEvent(UserEvent::Type type)
	{
		// kqueue based user events are a little different, because we need to have a pointer
		// to the kqueue context itself in order to trigger the event. This is because the 
		// user events aren't really an object itself, they are just an id in the kqueue
		// -- as opposed to eventfd() or windows events
		RealUserEvent* result = new RealUserEvent(type);
		// Little trick here -- UserEventactually a dummy class that only provides
		// it's method signatures
		return std::shared_ptr<UserEvent>((UserEvent*)result);
	}

	
}

