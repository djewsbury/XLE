// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "System_WinAPI.h"
#include "../PollingThread.h"
#include "../Log.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Core/Exceptions.h"
#include "IncludeWindows.h"

namespace OSServices
{
	template<typename Ptr1, typename Ptr2>
		bool PointersEquivalent(const Ptr1& lhs, const Ptr2& rhs) { return !lhs.owner_before(rhs) && !rhs.owner_before(lhs); }

	class PollingThread::Pimpl : public std::enable_shared_from_this<PollingThread::Pimpl>
	{
	public:
		XlHandle _interruptPollEvent;
		std::atomic<bool> _pendingShutdown;
		std::thread _backgroundThread;
		std::thread::id _constructionThread;

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

		struct ActiveOnceEvent
		{
			std::shared_ptr<IConduitProducer> _producer;
			XlHandle _platformHandle;
			std::promise<std::any> _promise;
		};
		std::vector<ActiveOnceEvent> _activeOnceEvents;
		
		struct SpecialOverlapped;
		struct ActiveEvent
		{
			// Note that after we begin waiting, we keep a strong pointer to the producer
			// This is important because BeginOperation can sometimes pass memory buffers to
			// async windows calls. For example, when calling ReadDirectoryChangesW, we
			// pass a pointer to a buffer that must remain valid until we cancel IO for that OVERLAPPED
			// the lifecycle for that buffer should be maintained by the IConduitProducer -- and so
			// therefore, we must keep a strong pointer to it for as long as the event is
			// active
			// The consumer can still be a weak pointer, though -- any events are cancelled
			// if the consumer is released by the client
			uint64_t _id = ~0ull;
			std::shared_ptr<IConduitProducer> _producer;
			std::weak_ptr<IConduitConsumer> _consumer;
			std::unique_ptr<SpecialOverlapped> _overlapped;
			std::optional<std::promise<void>> _cancelCompletionPromise;
		};
		std::vector<ActiveEvent> _activeEvents;

		////////////////////////////////////////////////////////

		struct SpecialOverlapped : public OVERLAPPED 
		{ 
			std::weak_ptr<Pimpl> _manager;
		};

		static void CALLBACK CompletionRoutineFunction(
			DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered,
			LPOVERLAPPED lpOverlapped)
		{
			auto manager = static_cast<SpecialOverlapped*>(lpOverlapped)->_manager.lock();
			if (!manager)
				return;

			auto i = std::find_if(
				manager->_activeEvents.begin(), manager->_activeEvents.end(),
				[lpOverlapped](const auto& e) { return e._overlapped.get() == lpOverlapped; });
			if (i == manager->_activeEvents.end()) {
				return;
			}

			if (i->_cancelCompletionPromise.has_value()) {
				// Most of the time, dwErrorCode should be ERROR_OPERATION_ABORTED here
				// However, it might be possible that we got a normal "complete" at around the same
				// time that we were calling CancelIoEx. Will we still consider those cases as "cancels"
				// though, and ignore whatever data we got back 
				i->_cancelCompletionPromise.value().set_value();
				manager->_activeEvents.erase(i);
				return;
			}
			assert(dwErrorCode != ERROR_OPERATION_ABORTED);

			// If the consumer is lapsed, we don't even call IConduitProducer_CompletionRoutine::OnTrigger,
			// and we don't restart the wait
			auto consumer = i->_consumer.lock();
			if (!consumer)
				return;
			
			auto conduitCompletion = dynamic_cast<IConduitProducer_CompletionRoutine*>(i->_producer.get());
			if (!conduitCompletion) {
				consumer->OnException(std::make_exception_ptr(std::runtime_error("Cannot react to event because conduit producer type is unknown")));
				return;
			}

			std::any res;
			TRY {
				// Note that if we get an exception in GeneratePayload, we will call IConduitConsumer::OnException,
				// and we will not restart waiting for this conduit
				res = conduitCompletion->GeneratePayload(dwNumberOfBytesTransfered);

				TRY {
					consumer->OnEvent(std::move(res));
				} CATCH (const std::exception& e) {
					// We must suppress any exceptions raised by IConduitConsumer::OnEvent. Passing it to
					// IConduitConsumer::OnException makes no sense, and there's nowhere else to send it
					// After this kind of exception, we will still restart waiting on the event, and it
					// can trigger again
					Log(Error) << "Suppressing exception in IConduitConsumer::OnEvent: " << e.what() << std::endl;
				} CATCH (...) {
					Log(Error) << "Suppressing unknown exception in IConduitConsumer::OnEvent" << std::endl;
				} CATCH_END

				// restart operation (allocate a new OVERLAPPED object to help distinguish it)
				auto oldOverlapped = std::move(i->_overlapped);
				i->_overlapped = std::make_unique<SpecialOverlapped>();
				std::memset((OVERLAPPED*)i->_overlapped.get(), 0, sizeof(OVERLAPPED));
				i->_overlapped->_manager = oldOverlapped->_manager;
				conduitCompletion->BeginOperation(i->_overlapped.get(), CompletionRoutineFunction);
			} CATCH (...) {
				// Pass on the exception to the consumer, then erase the active event from the list entirely
				// Once we hit an exception, it's considered dead, and we don't want it in our _activeEvents
				// list
				consumer->OnException(std::current_exception());
				assert(!i->_cancelCompletionPromise.has_value());
				manager->_activeEvents.erase(i);
			} CATCH_END
		}

		////////////////////////////////////////////////////////

		Pimpl() : _pendingShutdown(false)
		{
			_constructionThread = std::this_thread::get_id();
			_interruptPollEvent = XlCreateEvent(false);
			_backgroundThread = std::thread(
				[this]() {
					TRY {
						this->ThreadFunction(); 
					} CATCH(const std::exception& e) {
						Log(Error) << "Encountered exception in background WaitForMultipleObjects thread. Terminating any asynchronous operations" << std::endl;
						Log(Error) << "Exception as follows: " << e.what() << std::endl;
					} CATCH(...) {
						Log(Error) << "Encountered exception in background WaitForMultipleObjects thread. Terminating any asynchronous operations" << std::endl;
						Log(Error) << "Unknown exception type" << std::endl;
					} CATCH_END
				});
		}

		~Pimpl()
		{
			// Better to destruct this object in the same thread we created it. Ideally we don't want to destroy
			// it from within CompletionRoutineFunction (which can happen due to the ref counting) because that
			// would create a complex web of interleaved Win32 calls
			assert(std::this_thread::get_id() != _backgroundThread.get_id());		// problem, because _backgroundThread.join() must fail
			assert(std::this_thread::get_id() == _constructionThread);
			_pendingShutdown.store(true);
			InterruptBackgroundThread();
			_backgroundThread.join();
			XlCloseSyncObject(_interruptPollEvent);
		}

		void ThreadFunction()
		{
			std::vector<XlHandle> handlesToWaitOn;

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
								_activeOnceEvents.begin(), _activeOnceEvents.end(),
								[&event](const auto& ae) { return PointersEquivalent(event._producer, ae._producer); });
							if (existing != _activeOnceEvents.end()) {
								// We can't queue multiple poll operations on the same platform handle, because we will be using
								// the platform handle to lookup events in _activeOnceEvents (this would otherwise make it ambigious)
								pendingExceptionsToPropagate2.push_back({std::move(event._promise), std::make_exception_ptr(std::runtime_error("Multiple asynchronous events queued for the same platform handle"))});
								continue;
							}

							auto* platformHandleProducer = dynamic_cast<IConduitProducer_PlatformHandle*>(event._producer.get());
							if (!platformHandleProducer) {
								pendingExceptionsToPropagate2.push_back({std::move(event._promise), std::make_exception_ptr(std::runtime_error("Expecting platform handle based conduit to be used with RespondOnce call"))});
								continue;
							}

							ActiveOnceEvent activeEvent;
							activeEvent._producer = event._producer;
							activeEvent._platformHandle = platformHandleProducer->GetPlatformHandle();
							activeEvent._promise = std::move(event._promise);
							_activeOnceEvents.push_back(std::move(activeEvent));
						}
						_pendingOnceInitiates.clear();

						for (auto& event:_pendingEventConnects) {
							auto existing = std::find_if(
								_activeEvents.begin(), _activeEvents.end(),
								[&event](const auto& ae) { return PointersEquivalent(event._producer, ae._producer); });
							if (existing != _activeEvents.end()) {
								// We can't queue multiple poll operations on the same platform handle, because we will be using
								// the platform handle to lookup events in _activeOnceEvents (this would otherwise make it ambigious)
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Multiple asynchronous events queued for the same conduit"))});
								continue;
							}

							ActiveEvent activeEvent;
							activeEvent._producer = std::move(event._producer);
							activeEvent._consumer = std::move(event._consumer);
							
							auto* completionRoutine = dynamic_cast<IConduitProducer_CompletionRoutine*>(activeEvent._producer.get());
							if (completionRoutine) {
								activeEvent._overlapped = std::make_unique<SpecialOverlapped>();
								std::memset((OVERLAPPED*)activeEvent._overlapped.get(), 0, sizeof(OVERLAPPED));
								activeEvent._overlapped->_manager = weak_from_this();
								TRY {
									completionRoutine->BeginOperation(activeEvent._overlapped.get(), CompletionRoutineFunction);
								} CATCH (...) {
									pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::current_exception()});
									continue;
								} CATCH_END
							}
							
							_activeEvents.push_back(std::move(activeEvent));
							pendingPromisesToTrigger.push_back(std::move(event._onChangePromise));
						}
						_pendingEventConnects.clear();

						for (auto& event:_pendingEventDisconnects) {
							auto existing = std::find_if(
								_activeEvents.begin(), _activeEvents.end(),
								[&event](const auto& ae) { return PointersEquivalent(event._producer, ae._producer); });
							if (existing == _activeEvents.end()) {
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Attempting to disconnect a conduit that is not currently connected"))});
								continue;
							}

							// If we've already begun a cancel operation for this overlapped object, we just report and error and bail out from here
							if (existing->_cancelCompletionPromise.has_value()) {
								pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::make_exception_ptr(std::runtime_error("Attempting to disconnect from an event that already has a pending disconnect"))});
								continue;
							}

							auto* completionRoutine = dynamic_cast<IConduitProducer_CompletionRoutine*>(event._producer.get());
							if (completionRoutine) {
								TRY {
									// CancelIO doesn't process immediately on windows. We need to save the promise and we'll
									// ultimately trigger it from the completion routine
									assert(existing->_overlapped);
									auto cancelType = completionRoutine->CancelOperation(existing->_overlapped.get());
									if (cancelType == IConduitProducer_CompletionRoutine::CancelOperationType::CancelIoWasCalled) {
										existing->_cancelCompletionPromise = std::move(event._onChangePromise); 
									} else {
										pendingPromisesToTrigger.push_back(std::move(event._onChangePromise));
										_activeEvents.erase(existing);
									}
								} CATCH (...) {
									pendingExceptionsToPropagate1.push_back({std::move(event._onChangePromise), std::current_exception()});
									_activeEvents.erase(existing);
								} CATCH_END
							} else {
								pendingPromisesToTrigger.push_back(std::move(event._onChangePromise));
								_activeEvents.erase(existing);
							}
						}
						_pendingEventDisconnects.clear();

						// if any conduits have expired, we can go ahead and quietly remove them from the 
						// epoll context. It's better to get an explicit disconnect, but this at least cleans
						// up anything hanging. Note that we're expecting the conduit to have destroyed the 
						// platform handle when it was cleaned up (in other words, that platform handle is now dangling)
						for (auto i=_activeEvents.begin(); i!=_activeEvents.end();) {
							if (i->_consumer.expired()) {
								auto* completionRoutine = dynamic_cast<IConduitProducer_CompletionRoutine*>(i->_producer.get());
								if (completionRoutine) {
									TRY {
										completionRoutine->CancelOperation(i->_overlapped.get());
									} CATCH (const std::exception& e) {
										Log(Error) << "Suppressed exception while cancelling expired conduit: " << e.what() << std::endl;
									} CATCH (...) {
										Log(Error) << "Suppressed unknown exception while cancelling expired conduit" << std::endl;
									} CATCH_END
								}
								i = _activeEvents.erase(i);
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
				
				const int timeoutInMilliseconds = XL_INFINITE;
				
				handlesToWaitOn.clear();
				handlesToWaitOn.reserve(_activeOnceEvents.size() + 1);
				for (const auto&e:_activeOnceEvents) handlesToWaitOn.push_back(e._platformHandle);
				handlesToWaitOn.push_back(_interruptPollEvent);
				
				assert(handlesToWaitOn.size() < XL_MAX_WAIT_OBJECTS);
				auto res = XlWaitForMultipleSyncObjects(
					(uint32_t)handlesToWaitOn.size(), handlesToWaitOn.data(),
					false, timeoutInMilliseconds, true);

				if (res == XL_WAIT_FAILED) {
					// This is a low-level failure. No further operations will be processed; so let's propage
					// exception messages to everything waiting. Most importantly, promised will not be completed,
					// so we must set them into exception state
					auto errorAsString = SystemErrorCodeAsString(GetLastError());
					auto msgToPropagate = "PollingThread received an error message during wait: " + errorAsString;
					for (auto&e:_activeOnceEvents)
						e._promise.set_exception(std::make_exception_ptr(std::runtime_error(msgToPropagate)));
					for (auto&e:_activeEvents) {
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

				if (res >= XL_WAIT_OBJECT_0 && res < (XL_WAIT_OBJECT_0+handlesToWaitOn.size())) {
					auto triggeredHandle = handlesToWaitOn[res-XL_WAIT_OBJECT_0];

					if (triggeredHandle == _interruptPollEvent) {
						continue;
					}

					auto onceEvent = std::find_if(
						_activeOnceEvents.begin(), _activeOnceEvents.end(),
						[triggeredHandle](const auto& ae) { return ae._platformHandle == triggeredHandle; });
					if (onceEvent != _activeOnceEvents.end()) {
						auto promise = std::move(onceEvent->_promise);
						_activeOnceEvents.erase(onceEvent);
						// Windows disguish a "read" interrupt from a "write" interruption
						// so we'll just have to assume it's for read
						promise.set_value(PollingEventType::Input);
						continue;
					}

					Log(Error) << "Got an event for a platform handle that isn't in our _activeEvents list" << std::endl;
				}

				// XL_WAIT_IO_COMPLETION is normal; this just happens when a completion routine was called during
				// the wait
				if (res != XL_WAIT_IO_COMPLETION) {
					Log(Error) << "Unexpected return code from XlWaitForMultipleSyncObjects: " << res << std::endl;
				}
			}

			// We're ending all waiting. We must set any remainding promises to exception status, because they
			// will never be completed
			auto msgToPropagate = "Event cannot complete because PollingThread is shutting down";
			for (auto&e:_activeOnceEvents)
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
			XlSetEvent(_interruptPollEvent);
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

	class RealPollableEvent : public IConduitProducer_PlatformHandle
	{
	public:
		XlHandle _platformHandle = INVALID_HANDLE_VALUE;
		UserEvent::Type _type;

		virtual XlHandle GetPlatformHandle() const
		{
			return _platformHandle;
		}

		RealPollableEvent(UserEvent::Type type) : _type(type)
		{
			if (type == UserEvent::Type::Semaphore) {
				_platformHandle = CreateSemaphoreA(nullptr, 0, LONG_MAX, nullptr);
			} else {
				assert(_type == UserEvent::Type::Binary);
				const bool manualReset = false;
				_platformHandle = CreateEventA(nullptr, manualReset, FALSE, nullptr);
			}
		}

		~RealPollableEvent()
		{
			if (_platformHandle != INVALID_HANDLE_VALUE)
				XlCloseSyncObject(_platformHandle);
		}

		RealPollableEvent& operator=(RealPollableEvent&& moveFrom) never_throws
		{
			if (_platformHandle != INVALID_HANDLE_VALUE)
				XlCloseSyncObject(_platformHandle);
			_platformHandle = moveFrom._platformHandle;
			_type = moveFrom._type;
			moveFrom._platformHandle = INVALID_HANDLE_VALUE;
			return *this;
		}

		RealPollableEvent(RealPollableEvent&& moveFrom) never_throws
		{
			_platformHandle = moveFrom._platformHandle;
			_type = moveFrom._type;
			moveFrom._platformHandle = INVALID_HANDLE_VALUE;
		}
	};

	void UserEvent::IncreaseCounter()
	{
		auto* that = (RealPollableEvent*)this;
		assert(that->_platformHandle != INVALID_HANDLE_VALUE);
		if (that->_type == UserEvent::Type::Binary) {
			SetEvent(that->_platformHandle);
		} else if (that->_type == UserEvent::Type::Semaphore) {
			// "ReleaseSemaphore" increments the count in a semaphore
			// It is only decremented when a waiting thread is activated
			ReleaseSemaphore(that->_platformHandle, 1, nullptr);
		} else {
			UNREACHABLE();
		}
	}

	std::shared_ptr<UserEvent> CreateUserEvent(UserEvent::Type type)
	{
		RealPollableEvent* result = new RealPollableEvent(type);
		// Little trick here -- UserEvent is actually a dummy class that only provides
		// it's method signatures
		return std::shared_ptr<UserEvent>((UserEvent*)result);
	}

	
}

