// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CompletionThreadPool.h"
#include "ThreadLocalPtr.h"
#include "ThreadingUtils.h"
#include "../../OSServices/Log.h"
#include "../../OSServices/RawFS.h"
#include "../../Core/Exceptions.h"
#include <functional>

namespace Utility
{
    namespace Internal
    {
        class IYieldToPoolHelper
        {
        public:
            virtual void SetYieldToPoolInterface(Internal::IYieldToPool*) = 0;
            virtual Internal::IYieldToPool* GetYieldToPoolInterface() = 0;
            virtual ~IYieldToPoolHelper() = default;
        };

        class YieldToPoolHelper : public IYieldToPoolHelper
        {
        public:
            virtual void SetYieldToPoolInterface(Internal::IYieldToPool*) override;
            virtual Internal::IYieldToPool* GetYieldToPoolInterface() override;
        };

        IYieldToPool* GetYieldToPoolInterface();
        void SetYieldToPoolInterface(IYieldToPool* newValue);
    }

    class ThreadPool::YieldToPoolInterface : public Internal::IYieldToPool
    {
    public:
        virtual std::future_status YieldWith(std::function<std::future_status()>&& yieldingFunction) override;
        YieldToPoolInterface(ThreadPool&);
    private:
        ThreadPool* _pool;
    };

////////////////////////////////////////////////////////////////////////////////////////////////////

    ThreadPool::Page::Page()
    : _storage(PageSize)
    , _heap(PageSize)
    {}

    void ThreadPool::RunBlocks()
    {
        ++_workersOwningABlockCount;
        ++_workersTotalCount;
        ++_workersNonFrozenCount;

        YieldToPoolInterface yieldToPool{*this};
        Internal::SetYieldToPoolInterface(&yieldToPool);

        for (;;) {
            StoredFunction task;
            void* fnObjectPtr;

            {
                std::unique_lock<decltype(_pendingTaskLock)> autoLock(_pendingTaskLock);
                if (_workerQuit) {
                    --_workersOwningABlockCount;
                    --_workersNonFrozenCount;
                    break;
                }

                DrainPendingReleaseAlreadyLocked();
                if (_pendingTasks.empty()) {
                    --_workersOwningABlockCount;

                    _pendingTaskVariable.wait(autoLock);
                    if (_workerQuit) {
                        --_workersNonFrozenCount;
                        break;
                    }

                    // If we have too many workers at this point, we should shutdown this thread
                    // This occurs when recovering from a freezing and unfreezing a thread
                    // Note the double-check here, and notify_one() to wake up another thread
                    if (_workersNonFrozenCount.load() > _requestedWorkerCount) {
                        auto prevValue = _workersNonFrozenCount.fetch_add(-1);
                        if (prevValue > _requestedWorkerCount) {
                            _pendingTaskVariable.notify_one();
                            break;
                        } else {
                            ++_workersNonFrozenCount;
                        }
                    }

                    ++_workersOwningABlockCount;
                    if (_pendingTasks.empty())
                        continue;
                }

                task = std::move(_pendingTasks.front());
                _pendingTasks.pop();
                fnObjectPtr = PtrAdd(AsPointer(_pages[task._pageIdx]._storage.begin()), task._offset);
            }

            TRY
            {
                task._caller(fnObjectPtr);
                task._destructor(fnObjectPtr);
            } CATCH(const std::exception& e) {
                Log(Error) << "Suppressing exception in thread pool thread: " << e.what() << std::endl;
                (void)e;
            } CATCH(...) {
                Log(Error) << "Suppressing unknown exception in thread pool thread." << std::endl;
            } CATCH_END

            AddPendingRelease(task);
        }

        Internal::SetYieldToPoolInterface(nullptr);
        --_workersTotalCount;
    }

    void ThreadPool::RunBlocksDrainThread()
    {
        // This is used when draining the pool using StallAndDrainQueue()
        // we avoid some of the thread counting behaviour in RunBlocks, because we don't
        // actually want this thread to be counted as a thread pool thread
        for (;;) {
            StoredFunction task;
            void* fnObjectPtr;

            {
                std::unique_lock<decltype(_pendingTaskLock)> autoLock(_pendingTaskLock);

                DrainPendingReleaseAlreadyLocked();
                if (_pendingTasks.empty())
                    return;

                task = std::move(_pendingTasks.front());
                _pendingTasks.pop();
                fnObjectPtr = PtrAdd(AsPointer(_pages[task._pageIdx]._storage.begin()), task._offset);
            }

            TRY
            {
                task._caller(fnObjectPtr);
                task._destructor(fnObjectPtr);
            } CATCH(const std::exception& e) {
                Log(Error) << "Suppressing exception in thread pool thread: " << e.what() << std::endl;
                (void)e;
            } CATCH(...) {
                Log(Error) << "Suppressing unknown exception in thread pool thread." << std::endl;
            } CATCH_END

            AddPendingRelease(task);
        }
    }

    void ThreadPool::EnqueueBasic(std::function<void()>&& fn)
    {
        assert(IsGood());

        std::unique_lock<decltype(this->_pendingTaskLock)> autoLock(this->_pendingTaskLock);
        static_assert(sizeof(std::function<void()>) <= PageSize);
        auto size = sizeof(std::function<void()>);

        bool foundAllocation = false;
        StoredFunction storedFunction;
        for (unsigned p=0; p<_pages.size(); ++p) {
            auto attemptedAllocation = _pages[p]._heap.Allocate(size);
            if (attemptedAllocation != ~0u) {
                storedFunction = {
                    p, attemptedAllocation, size,
                    &Internal::Destructor<std::function<void()>>,
                    &Internal::MoveConstructor<std::function<void()>>,
                    &Internal::CallOpaqueFunction<std::function<void()>>
                };
                foundAllocation = true;
            }
        }

        if (!foundAllocation) {
            _pages.push_back({});
            auto p = _pages.end()-1;
            auto allocation = p->_heap.Allocate(size);
            assert(allocation != ~0u);
            storedFunction = {
                (unsigned)_pages.size()-1, allocation, size,
                &Internal::Destructor<std::function<void()>>,
                &Internal::MoveConstructor<std::function<void()>>,
                &Internal::CallOpaqueFunction<std::function<void()>>
            };
        }

        new((void*)PtrAdd(AsPointer(_pages[storedFunction._pageIdx]._storage.begin()), storedFunction._offset)) std::function<void()>(std::move(fn));

        _pendingTasks.push(storedFunction);
        _pendingTaskVariable.notify_one();
    }

    void ThreadPool::DrainPendingReleaseAlreadyLocked()
    {
        StoredFunction* fn;
        while (_pendingRelease.try_front(fn)) {
            _pages[fn->_pageIdx]._heap.Deallocate(fn->_offset, fn->_size);
            _pendingRelease.pop();
        }
    }

    void ThreadPool::AddPendingRelease(StoredFunction fn)
    {
        if (!_pendingRelease.push(fn)) {
            std::unique_lock<decltype(_pendingTaskLock)> autoLock(_pendingTaskLock);
            DrainPendingReleaseAlreadyLocked();
            auto secondAttempt = _pendingRelease.push(fn);
            (void)secondAttempt;
            assert(secondAttempt);
        }
    }

    ThreadPool::ThreadPool(unsigned threadCount)
    : _requestedWorkerCount(threadCount)
    {
        _workerQuit = false;
        _workersOwningABlockCount.store(0);
        _workersFrozenCount.store(0);
        _workersNonFrozenCount.store(0);
        _workersTotalCount.store(0);
        if (!_yieldToPoolHelper)
            _yieldToPoolHelper = std::make_shared<Internal::YieldToPoolHelper>();
        for (unsigned i = 0; i<threadCount; ++i)
            _workerThreads.emplace_back([this] { this->RunBlocks(); });
    }

    bool ThreadPool::StallAndDrainQueue(std::optional<std::chrono::steady_clock::duration> stallDuration)
    {
        if (stallDuration.has_value()) {
            auto timeoutPt = std::chrono::steady_clock::now() + stallDuration.value();
            RunBlocksDrainThread();
            if (std::chrono::steady_clock::now() >= timeoutPt)
                return _workersOwningABlockCount == 0;
            while (_workersOwningABlockCount) {
                Threading::YieldTimeSlice();
                RunBlocksDrainThread();
                if (std::chrono::steady_clock::now() >= timeoutPt)
                    return false;
            }
            std::unique_lock<decltype(_pendingTaskLock)> autoLock(_pendingTaskLock);
            DrainPendingReleaseAlreadyLocked();
            return true;
        } else {
            RunBlocksDrainThread();
            while (_workersOwningABlockCount) {
                Threading::YieldTimeSlice();
                RunBlocksDrainThread();
            }
            std::unique_lock<decltype(_pendingTaskLock)> autoLock(_pendingTaskLock);
            DrainPendingReleaseAlreadyLocked();
            return true;
        }
    }

    ThreadPool::~ThreadPool()
    {
        _workerQuit = true;
        _pendingTaskVariable.notify_all();
        for (auto&t : _workerThreads) t.join();
    }

    std::future_status ThreadPool::YieldToPoolInterface::YieldWith(std::function<std::future_status()>&& yieldingFunction)
    {
        // set this thread into frozen state and spin up a replacement thread
        ++_pool->_workersFrozenCount;
        auto prevWorkersNonFrozenCount = _pool->_workersNonFrozenCount.fetch_add(-1);
        if ((prevWorkersNonFrozenCount-1) < _pool->_requestedWorkerCount) {
            ScopedLock(_pool->_pendingTaskLock);
            _pool->_workerThreads.emplace_back([pool=_pool] { pool->RunBlocks(); });
        }
        auto resultStatus = yieldingFunction();
        // unfreeze this thread; which should encourage the new thread we spun up to shut itself down
        --_pool->_workersFrozenCount;
        ++_pool->_workersNonFrozenCount;
        return resultStatus;
    }

    ThreadPool::YieldToPoolInterface::YieldToPoolInterface(ThreadPool& pool) : _pool(&pool) {}

////////////////////////////////////////////////////////////////////////////////////////////////////

    namespace Internal
    {
        
    #if !FEATURE_THREAD_LOCAL_KEYWORD
        static thread_local_ptr<Internal::IYieldToPool> s_threadPoolYield;
        void YieldToPoolHelper::SetYieldToPoolInterface(Internal::IYieldToPool* newValue) { s_threadPoolYield = newValue; }
        Internal::IYieldToPool* YieldToPoolHelper::GetYieldToPoolInterface() { return s_threadPoolYield.get(); }
    #else
        static thread_local Internal::IYieldToPool* s_threadPoolYield;
        void YieldToPoolHelper::SetYieldToPoolInterface(Internal::IYieldToPool* newValue) { s_threadPoolYield = newValue; }
        Internal::IYieldToPool* YieldToPoolHelper::GetYieldToPoolInterface() { return s_threadPoolYield; }
    #endif

        IYieldToPoolHelper* GetYieldToPoolHelper()
        {
            // Use attachable ptrs to guarantee cross-module support
            static ConsoleRig::WeakAttachablePtr<Internal::IYieldToPoolHelper> result;
            return result.lock().get();
        }

        IYieldToPool* GetYieldToPoolInterface()
        {            
            auto* helper = GetYieldToPoolHelper();
            return helper ? helper->GetYieldToPoolInterface() : nullptr;
        }

        void SetYieldToPoolInterface(IYieldToPool* newValue)
        {            
            auto* helper = GetYieldToPoolHelper();
            if (helper) helper->SetYieldToPoolInterface(newValue);
        }
    }
}

