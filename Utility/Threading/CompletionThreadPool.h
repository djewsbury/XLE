// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Mutex.h"
#include "LockFree.h"
#include "../HeapUtils.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include <vector>
#include <thread>
#include <functional>
#include <queue>
#include <optional>
#include <future>
#include <chrono>

namespace Utility
{
    namespace Internal
    {
        class IYieldToPool
        {
        public:
            virtual std::future_status YieldWith(std::function<std::future_status()>&& yieldingFunction) = 0;
            virtual ~IYieldToPool() = default;
        };
        IYieldToPool* GetYieldToPoolInterface();
        class IYieldToPoolHelper;
    }

    /** <summary>Temporarily yield execution of this thread to whatever pool manages it</summary>
     * 
     * Operations running on a thread pool thread should normally not use busy loops or
     * long locks waiting for mutexes. When a thread pool operation is stalled for some
     * synchronization primitive, the entire worker thread becomes stalled. Since there
     * are a finite number of worker threads, this can result in a deadlock were all
     * worker threads are stalled waiting on some pool operation that can never execute.
     * 
     * Rather than stalling or yielding worker thread time, we should instead attempt to 
     * find some other thread pool block to take it's place. The thread pool will do this
     * by putting the current thread into a frozen state, and spin up another thread to
     * take it's place. This attempts to maintain a fixed number of non-stalled threads in 
     * the thread pool at all times. Assuming no cyclic dependencies, this can solve cases
     * where one thread pool block is waiting on the result of another thread pool block.
     * 
     * When run on some other thread, it will just yield back to the OS.
    */
    template<typename Rep, typename Period>
        void YieldToPoolFor(std::chrono::duration<Rep, Period> duration);
    void YieldToPool(std::condition_variable& cv, std::unique_lock<std::mutex>& lock);
    template<typename Rep, typename Period>
        std::cv_status YieldToPoolFor(std::condition_variable& cv, std::unique_lock<std::mutex>& lock, std::chrono::duration<Rep, Period> duration);
    template<typename Clock, typename Duration>
        std::cv_status YieldToPoolUntil(std::condition_variable& cv, std::unique_lock<std::mutex>& lock, std::chrono::time_point<Clock, Duration> timepoint);
    template<typename FutureType>
        void YieldToPool(std::future<FutureType>& future);
    template<typename FutureType, typename Rep, typename Period>
        std::future_status YieldToPoolFor(std::future<FutureType>& future, std::chrono::duration<Rep, Period> duration);
    template<typename FutureType, typename Clock, typename Duration>
        std::future_status YieldToPoolUntil(std::future<FutureType>& future, std::chrono::time_point<Clock, Duration> timepoint);
    template<typename FutureType>
        void YieldToPool(std::shared_future<FutureType>& future);
    template<typename FutureType, typename Rep, typename Period>
        std::future_status YieldToPoolFor(std::shared_future<FutureType>& future, std::chrono::duration<Rep, Period> duration);
    template<typename FutureType, typename Clock, typename Duration>
        std::future_status YieldToPoolUntil(std::shared_future<FutureType>& future, std::chrono::time_point<Clock, Duration> timepoint);

    class ThreadPool
    {
    public:
        template<class Fn, class... Args>
            void Enqueue(Fn&& fn, Args... args);
        void EnqueueBasic(std::function<void()>&& fn);

        bool IsGood() const { return !_workerThreads.empty(); }
        bool StallAndDrainQueue(std::optional<std::chrono::steady_clock::duration> stallDuration = {});

        ThreadPool(unsigned threadCount);
        ~ThreadPool();

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;
    private:
        std::vector<std::thread> _workerThreads;

        Threading::Conditional _pendingTaskVariable;
        Threading::Mutex _pendingTaskLock;

        struct StoredFunction
        {
            unsigned _pageIdx;
            size_t  _offset;
            size_t  _size;
            void (*_destructor)(void*);
            void (*_moveConstructor)(void*, void*);
            void (*_caller)(void*);
			// OSServices::ModuleId _moduleId;
        };

        std::queue<StoredFunction> _pendingTasks;
        LockFreeFixedSizeQueue<StoredFunction, 256> _pendingRelease;

        static constexpr unsigned PageSize = 32*1024;
        struct Page
        {
            std::vector<uint8_t> _storage;
            SimpleSpanningHeap _heap;
            Page();
        };
        std::vector<Page> _pages;
        
        volatile bool _workerQuit;
        std::atomic<signed> _workersOwningABlockCount;
        std::atomic<signed> _workersFrozenCount;
        std::atomic<signed> _workersNonFrozenCount;
        std::atomic<signed> _workersTotalCount;
        unsigned _requestedWorkerCount;

        ConsoleRig::AttachablePtr<Internal::IYieldToPoolHelper> _yieldToPoolHelper;

        void RunBlocks();
        void RunBlocksDrainThread();
        void DrainPendingReleaseAlreadyLocked();
        void AddPendingRelease(StoredFunction fn);

        class YieldToPoolInterface;
    };

    template<class Fn, class... Args>
        void ThreadPool::Enqueue(Fn&& fn, Args... args)
    {
        assert(IsGood());

        std::unique_lock<decltype(this->_pendingTaskLock)> autoLock(this->_pendingTaskLock);

        using BoundFn = decltype(std::bind(std::move(fn), std::forward<Args>(args)...));
        StoredFunction storedFunction;
        if constexpr(sizeof...(Args)==0) {
            static_assert(sizeof(Fn) <= PageSize);
            storedFunction._size = sizeof(Fn);
            storedFunction._destructor = &Internal::Destructor<Fn>;
            storedFunction._moveConstructor = &Internal::MoveConstructor<Fn>;
            storedFunction._caller = &Internal::CallOpaqueFunction<Fn>;
        } else {
            static_assert(sizeof(BoundFn) <= PageSize);
            storedFunction._size = sizeof(BoundFn);
            storedFunction._destructor = &Internal::Destructor<BoundFn>;
            storedFunction._moveConstructor = &Internal::MoveConstructor<BoundFn>;
            storedFunction._caller = &Internal::CallOpaqueFunction<BoundFn>;
        }
    
        bool foundAllocation = false;
        for (unsigned p=0; p<_pages.size(); ++p) {
            auto attemptedAllocation = _pages[p]._heap.Allocate(storedFunction._size);
            if (attemptedAllocation != ~0u) {
                storedFunction._pageIdx = p;
                storedFunction._offset = attemptedAllocation;
                foundAllocation = true;
            }
        }

        if (!foundAllocation) {
            _pages.push_back({});
            auto p = _pages.end()-1;
            auto allocation = p->_heap.Allocate(storedFunction._size);
            assert(allocation != ~0u);
            storedFunction._pageIdx = (unsigned)_pages.size()-1;
            storedFunction._offset = allocation;
        }

        #pragma push_macro("new")
        #undef new
        if constexpr(sizeof...(Args)==0) {
            new((void*)PtrAdd(AsPointer(_pages[storedFunction._pageIdx]._storage.begin()), storedFunction._offset)) Fn(std::move(fn));
        } else {
            new((void*)PtrAdd(AsPointer(_pages[storedFunction._pageIdx]._storage.begin()), storedFunction._offset)) BoundFn(std::bind(std::move(fn), std::forward<Args>(args)...));
        }
        #pragma pop_macro("new")

        _pendingTasks.push(storedFunction);
        _pendingTaskVariable.notify_one();
    }

    template<typename Rep, typename Period>
        void YieldToPoolFor(std::chrono::duration<Rep, Period> duration)
    {
        auto* interface = Internal::GetYieldToPoolInterface();
        if (interface) {
            interface->YieldWith([duration]() { std::this_thread::sleep_for(duration); return std::future_status::ready; });
        } else {
            std::this_thread::sleep_for(duration);
        }
    }

    inline void YieldToPool(std::condition_variable& cv, std::unique_lock<std::mutex>& lock)
    {
        auto* interface = Internal::GetYieldToPoolInterface();
        if (interface) {
            interface->YieldWith(
                [&cv, &lock]() {
                    cv.wait(lock);
                    return std::future_status::ready;
                });
        } else {
            cv.wait(lock);
        }
    }

    template<typename Rep, typename Period>
        inline std::cv_status YieldToPoolFor(std::condition_variable& cv, std::unique_lock<std::mutex>& lock, std::chrono::duration<Rep, Period> duration)
    {
        auto* interface = Internal::GetYieldToPoolInterface();
        if (interface) {
            auto res = interface->YieldWith(
                [&cv, &lock, duration]() {
                    auto result = cv.wait_for(lock, duration);
                    return result == std::cv_status::no_timeout ? std::future_status::ready : std::future_status::timeout;
                });
            return res == std::future_status::ready ? std::cv_status::no_timeout : std::cv_status::timeout;
        } else {
            return cv.wait_for(lock, duration);
        }
    }

    template<typename Clock, typename Duration>
        inline std::cv_status YieldToPoolUntil(std::condition_variable& cv, std::unique_lock<std::mutex>& lock, std::chrono::time_point<Clock, Duration> timepoint)
    {
        auto* interface = Internal::GetYieldToPoolInterface();
        if (interface) {
            auto res = interface->YieldWith(
                [&cv, &lock, timepoint]() {
                    auto result = cv.wait_until(lock, timepoint);
                    return result == std::cv_status::no_timeout ? std::future_status::ready : std::future_status::timeout;
                });
            return res == std::future_status::ready ? std::cv_status::no_timeout : std::cv_status::timeout;
        } else {
            return cv.wait_until(lock, timepoint);
        }
    }

    template<typename FutureType>
        inline void YieldToPool(std::future<FutureType>& future)
    {
        auto* interface = Internal::GetYieldToPoolInterface();
        if (interface) {
            interface->YieldWith([&]() { future.wait(); return std::future_status::ready; });
        } else {
            future.wait();
        }
    }

    template<typename FutureType, typename Rep, typename Period>
        inline std::future_status YieldToPoolFor(std::future<FutureType>& future, std::chrono::duration<Rep, Period> duration)
    {
        auto* interface = Internal::GetYieldToPoolInterface();
        if (interface) {
            return interface->YieldWith([&future, duration]() { return future.wait_for(duration); });
        } else {
            return future.wait_for(duration);
        }
    }

    template<typename FutureType, typename Clock, typename Duration>
        inline std::future_status YieldToPoolUntil(std::future<FutureType>& future, std::chrono::time_point<Clock, Duration> timepoint)
    {
        auto* interface = Internal::GetYieldToPoolInterface();
        if (interface) {
            return interface->YieldWith([&future, timepoint]() { return future.wait_until(timepoint); });
        } else {
            return future.wait_until(timepoint);
        }
    }

    template<typename FutureType>
        inline void YieldToPool(std::shared_future<FutureType>& future)
    {
        auto* interface = Internal::GetYieldToPoolInterface();
        if (interface) {
            interface->YieldWith([&]() { future.wait(); return std::future_status::ready; });
        } else {
            future.wait();
        }
    }

    template<typename FutureType, typename Rep, typename Period>
        inline std::future_status YieldToPoolFor(std::shared_future<FutureType>& future, std::chrono::duration<Rep, Period> duration)
    {
        auto* interface = Internal::GetYieldToPoolInterface();
        if (interface) {
            return interface->YieldWith([&future, duration]() { return future.wait_for(duration); });
        } else {
            return future.wait_for(duration);
        }
    }

    template<typename FutureType, typename Clock, typename Duration>
        inline std::future_status YieldToPoolUntil(std::shared_future<FutureType>& future, std::chrono::time_point<Clock, Duration> timepoint)
    {
        auto* interface = Internal::GetYieldToPoolInterface();
        if (interface) {
            return interface->YieldWith([&future, timepoint]() { return future.wait_until(timepoint); });
        } else {
            return future.wait_until(timepoint);
        }
    }
}

using namespace Utility;
