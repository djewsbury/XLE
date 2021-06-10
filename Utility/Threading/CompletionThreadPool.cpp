// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CompletionThreadPool.h"
#include "ThreadLocalPtr.h"
#include "ThreadingUtils.h"
#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
    #include "../../OSServices/WinAPI/System_WinAPI.h"
#endif
#include "../../OSServices/Log.h"
#include "../../OSServices/RawFS.h"
#include "../../Core/Exceptions.h"
#include <functional>

namespace Utility
{
#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
    void CompletionThreadPool::EnqueueBasic(PendingTask&& task)
    {
        assert(IsGood());
        _pendingTasks.push_overflow(std::forward<PendingTask>(task));

            // set event should wake one thread -- and that thread should
            // then take over and execute the task
        OSServices::XlSetEvent(_events[0]);
    }

    CompletionThreadPool::CompletionThreadPool(unsigned threadCount)
    {
            // once event is an "auto-reset" event, which should wake a single thread
            // another event is a "manual-reset" event. This should 
        _events[0] = OSServices::XlCreateEvent(false);
        _events[1] = OSServices::XlCreateEvent(true);
        _workerQuit = false;

        for (unsigned i = 0; i<threadCount; ++i)
            _workerThreads.emplace_back(
                [this]
                {
                    SetYieldToPoolFunction([this](std::chrono::steady_clock::time_point waitUntilTime) {
                        assert(0);      // "waitUntilTime" must be respected

                        bool gotTask = false;
                        std::function<void()> task;

                        {
                            ScopedLock(this->_pendingsTaskLock);
                            std::function<void()>* t = nullptr;
                            if (_pendingTasks.try_front(t)) {
                                task = std::move(*t);
                                _pendingTasks.pop();
                                gotTask = true;
                            }
                        }

                        // Attempt a short wait if we didn't get a task
                        if (!gotTask) {
                            OSServices::XlWaitForMultipleSyncObjects(
                                2, this->_events,
                                false, 1, true);

                            {
                               ScopedLock(this->_pendingsTaskLock);
                                std::function<void()>* t = nullptr;
                                if (_pendingTasks.try_front(t)) {
                                    task = std::move(*t);
                                    _pendingTasks.pop();
                                    gotTask = true;
                                }
                            }
                        }

                        if (gotTask) {
                            TRY
                            {
                                task();
                            } CATCH(const std::exception& e) {
                                Log(Error) << "Suppressing exception in thread pool thread: " << e.what() << std::endl;
								(void)e;
                            } CATCH(...) {
                                Log(Error) << "Suppressing unknown exception in thread pool thread." << std::endl;
                            } CATCH_END
                        }
                    });

                    while (!this->_workerQuit) {
                        bool gotTask = false;
                        std::function<void()> task;

                        {
                                // note that _pendingTasks is safe for multiple pushing threads,
                                // but not safe for multiple popping threads. So we have to
                                // lock to prevent more than one thread from attempt to pop
                                // from it at the same time.
                            ScopedLock(this->_pendingsTaskLock);

                            std::function<void()>*t = nullptr;
                            if (_pendingTasks.try_front(t)) {
                                task = std::move(*t);
                                _pendingTasks.pop();
                                gotTask = true;
                            }
                        }

                        if (gotTask) {
                                // if we got this far, we can execute the task....
                            TRY
                            {
                                task();
                            } CATCH(const std::exception& e) {
                                Log(Error) << "Suppressing exception in thread pool thread: " << e.what() << std::endl;
								(void)e;
                            } CATCH(...) {
                                Log(Error) << "Suppressing unknown exception in thread pool thread." << std::endl;
                            } CATCH_END

                                // That that when using completion routines, we want to attempt to
                                // distribute the tasks evenly between threads (so that the completion
                                // routines will also be distributed evenly between threads.). To achieve
                                // this, let's not attempt to search for another task immediately... Instead
                                // when after we complete a task, let's encourage this thread to go back into
                                // a stall (unless all of our threads are saturated)
                            Threading::YieldTimeSlice();
                            continue;
                        }

                            // Wait for the event with the "alertable" flag set true
                            // note -- this is why we can't use std::condition_variable
                            //      (because threads waiting on a condition variable won't
                            //      be woken to execute completion routines)
                        OSServices::XlWaitForMultipleSyncObjects(
                            2, this->_events,
                            false, OSServices::XL_INFINITE, true);
                    }

                    SetYieldToPoolFunction(nullptr);
                }
            );
    }

    CompletionThreadPool::~CompletionThreadPool()
    {
        _workerQuit = true;
        OSServices::XlSetEvent(_events[1]);   // trigger a manual reset event should wake all threads (and keep them awake)
        for (auto&t : _workerThreads) t.join();

        OSServices::XlCloseSyncObject(_events[0]);
        OSServices::XlCloseSyncObject(_events[1]);
    }
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////

    void ThreadPool::EnqueueBasic(PendingTask&& task)
    {
        assert(IsGood());
        std::unique_lock<decltype(this->_pendingTaskLock)> autoLock(this->_pendingTaskLock);
        _pendingTasks.push(std::forward<PendingTask>(task));
        _pendingTaskVariable.notify_one();
    }

    void ThreadPool::RunBlocks(bool finishWhenEmpty)
    {
        ++_runningWorkerCount;

        SetYieldToPoolFunction([this](std::chrono::steady_clock::time_point waitUntilTime) {
            std::function<void()> task;
            {
                // slightly awkwardly, we can't actually use std::timed_mutex and try_lock_until
                // here, because std::timed_mutex can't be used with std::condition_variable
                std::unique_lock<decltype(this->_pendingTaskLock)> autoLock(this->_pendingTaskLock);
                if (this->_workerQuit) 
                    return;

                if (_pendingTasks.empty()) {
                    this->_pendingTaskVariable.wait_until(autoLock, waitUntilTime);
                    if (this->_workerQuit) return;
                    if (_pendingTasks.empty())
                        return;
                }

                task = std::move(_pendingTasks.front());
                _pendingTasks.pop();
            }

            TRY
            {
                task();
            } CATCH(const std::exception& e) {
                Log(Error) << "Suppressing exception in thread pool thread: " << e.what() << std::endl;
                (void)e;
            } CATCH(...) {
                Log(Error) << "Suppressing unknown exception in thread pool thread." << std::endl;
            } CATCH_END
        });

        for (;;) {
            std::function<void()> task;

            {
                    // note that _pendingTasks is safe for multiple pushing threads,
                    // but not safe for multiple popping threads. So we have to
                    // lock to prevent more than one thread from attempt to pop
                    // from it at the same time.
                std::unique_lock<decltype(_pendingTaskLock)> autoLock(_pendingTaskLock);
                if (_workerQuit) break;

                if (_pendingTasks.empty()) {
                    --_runningWorkerCount;
                    if (finishWhenEmpty) {
                        SetYieldToPoolFunction(nullptr);
                        return;
                    }
                    _pendingTaskVariable.wait(autoLock);
                    if (_workerQuit) break;
                    ++_runningWorkerCount;
                    if (_pendingTasks.empty())
                        continue;
                }

                task = std::move(_pendingTasks.front());
                _pendingTasks.pop();
            }

            TRY
            {
                task();
            } CATCH(const std::exception& e) {
                Log(Error) << "Suppressing exception in thread pool thread: " << e.what() << std::endl;
                (void)e;
            } CATCH(...) {
                Log(Error) << "Suppressing unknown exception in thread pool thread." << std::endl;
            } CATCH_END
        }

        SetYieldToPoolFunction(nullptr);
    }

    ThreadPool::ThreadPool(unsigned threadCount)
    {
        _workerQuit = false;

        for (unsigned i = 0; i<threadCount; ++i)
            _workerThreads.emplace_back([this] { this->RunBlocks(false); });
    }

    bool ThreadPool::StallAndDrainQueue(std::optional<std::chrono::steady_clock::duration> stallDuration)
    {
        if (stallDuration.has_value()) {
            auto timeoutPt = std::chrono::steady_clock::now() + stallDuration.value();
            while (_runningWorkerCount) {
                Threading::YieldTimeSlice();
                RunBlocks(true);
                if (std::chrono::steady_clock::now() >= timeoutPt)
                    return false;
            }
            return true;
        } else {
            while (_runningWorkerCount) {
                Threading::YieldTimeSlice();
                RunBlocks(true);
            }
            return true;
        }
    }

    ThreadPool::~ThreadPool()
    {
        _workerQuit = true;
        _pendingTaskVariable.notify_all();
        for (auto&t : _workerThreads) t.join();
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

#if !FEATURE_THREAD_LOCAL_KEYWORD
    static thread_local_ptr<std::function<void(std::chrono::steady_clock::time_point)>> s_threadPoolYieldFunction;

    void YieldToPoolUntilInternal(std::chrono::steady_clock::time_point timeoutTime)
    {
        auto* yieldFn = s_threadPoolYieldFunction.get();
        if (yieldFn) {
            (*yieldFn)(timeoutTime);
        } else {
            std::this_thread::sleep_until(timeoutTime);
        }
    }

    void SetYieldToPoolFunction(const std::function<void(std::chrono::steady_clock::time_point)>& yieldToPoolFunction)
    {
        s_threadPoolYieldFunction.allocate(yieldToPoolFunction);
    }
#else
    static thread_local std::function<void(std::chrono::steady_clock::time_point)> s_threadPoolYieldFunction;

    void YieldToPoolUntilInternal(std::chrono::steady_clock::time_point timeoutTime)
    {
        if (s_threadPoolYieldFunction) {
            s_threadPoolYieldFunction(timeoutTime);
        } else {
            std::this_thread::sleep_until(timeoutTime);
        }
    }

    void SetYieldToPoolFunction(const std::function<void(std::chrono::steady_clock::time_point)>& yieldToPoolFunction)
    {
        s_threadPoolYieldFunction = yieldToPoolFunction;
    }

#endif

}

