// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Mutex.h"
#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
    #include "LockFree.h"
    #include "../../OSServices/WinAPI/System_WinAPI.h"
#endif
#include <vector>
#include <thread>
#include <functional>
#include <queue>
#include <optional>

namespace Utility
{
    /** <summary>Temporarily yield execution of this thread to whatever pool manages it</summary>
     * 
     * Operations running on a thread pool thread should normally not use busy loops or
     * long locks waiting for mutexes. When a thread pool operation is stalled for some
     * synchronization primitive, the entire worker thread becomes stalled. Since there
     * are a finite number of worker threads, this can result in a deadlock were all
     * worker threads are stalled waiting on some pool operation that can never execute.
     * 
     * Rather than stalling or yielding worker thread time, we should instead attempt to 
     * find some other operation that can take over this worker thread temporarily.
     * 
     * When run on a thread pool worker thread, YieldToPool does exactly that. It does not
     * stall, but it will attempt pop another operation from the pending queue. It will
     * return execution back to the caller after this operation has completed, so that the
     * original operation can resume from where it left off.
     * 
     * When run on some other thread, it will just yield back to the OS.
    */
    void YieldToPoolUntilInternal(std::chrono::steady_clock::time_point timeoutTime);
    template<typename Rep, typename Period>
        void YieldToPoolFor(const std::chrono::duration<Rep, Period>& timeout)
    {
        return YieldToPoolUntilInternal(std::chrono::steady_clock::now() + timeout);
    }
    void SetYieldToPoolFunction(const std::function<void(std::chrono::steady_clock::time_point)>& yieldToPoolFunction);

#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
    class CompletionThreadPool
    {
    public:
        template<class Fn, class... Args>
            void Enqueue(Fn&& fn, Args... args);

		void EnqueueBasic(std::function<void()>&& task);

        bool IsGood() const { return !_workerThreads.empty(); }

        CompletionThreadPool(unsigned threadCount);
        ~CompletionThreadPool();

        CompletionThreadPool(const CompletionThreadPool&) = delete;
        CompletionThreadPool& operator=(const CompletionThreadPool&) = delete;
        CompletionThreadPool(CompletionThreadPool&&) = delete;
        CompletionThreadPool& operator=(CompletionThreadPool&&) = delete;
    private:
        std::vector<std::thread> _workerThreads;
        
        Threading::Mutex _pendingsTaskLock;
        typedef std::function<void()> PendingTask;
        LockFreeFixedSizeQueue<PendingTask, 256> _pendingTasks;

        OSServices::XlHandle _events[2];
        volatile bool _workerQuit;
    };

    template<class Fn, class... Args>
        void CompletionThreadPool::Enqueue(Fn&& fn, Args... args)
        {
			// note -- we seem to get a forced invocation of the copy constructor
			// for std::function<void> here, for an input lambda (even if that lamdba
			// takes no parameters and returns void). It seems like there is no way to
			// move from a lamdba of any kind into a std::function<void()> (presumably
			// because of the fake/unnamed type given with lamdbas by the compile)
			EnqueueBasic(std::bind(std::move(fn), std::forward<Args>(args)...));
        }
#endif

    class ThreadPool
    {
    public:
        template<class Fn, class... Args>
            void Enqueue(Fn&& fn, Args... args);

		void EnqueueBasic(std::function<void()>&& task);

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
        using PendingTask = std::function<void()>;
        std::queue<PendingTask> _pendingTasks;

        volatile bool _workerQuit;
        std::atomic<signed> _runningWorkerCount;

        void RunBlocks(bool finishWhenEmpty);
    };

    template<class Fn, class... Args>
        void ThreadPool::Enqueue(Fn&& fn, Args... args)
        {
			EnqueueBasic(std::bind(std::move(fn), std::forward<Args>(args)...));
        }
}

using namespace Utility;
