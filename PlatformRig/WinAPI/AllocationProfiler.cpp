// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../AllocationProfiler.h"
#include "../../Utility/PtrUtils.h"
#include <assert.h>

#if CLIBRARIES_ACTIVE == CLIBRARIES_MSVC && defined(_DEBUG)
    #define ALLOCATION_PROFILER_ENABLE
#endif

namespace PlatformRig
{
    AccumulatedAllocations* AccumulatedAllocations::_instance = nullptr;

    #if defined(ALLOCATION_PROFILER_ENABLE)

        class AccumulatedAllocations::Pimpl
        {
        public:
            _CRT_ALLOC_HOOK _oldHook;
            CurrentHeapMetrics _currentHeapMetrics;
            mutable unsigned _currentHeapMetricsCountDown = 0;
            Pimpl() : _oldHook(nullptr) {}
        };

        static int VSAllocationHook( 
            int allocType, void *userData, size_t size, int blockType,
            long requestNumber, const unsigned char *filename, int lineNumber);

        AccumulatedAllocations::AccumulatedAllocations()
        {
            assert(_instance == nullptr);
            
            _pimpl = std::make_unique<Pimpl>();
            _pimpl->_oldHook = _CrtSetAllocHook(&VSAllocationHook);
            _instance = this;
        }

        AccumulatedAllocations::~AccumulatedAllocations()
        {
            assert(_instance == this);
            auto beforeReset = _CrtSetAllocHook(_pimpl->_oldHook);
            assert(beforeReset == &VSAllocationHook);
            _instance = nullptr;
        }

        static int VSAllocationHook(
            int allocType, void *userData, size_t size, int blockType,
            long requestNumber, const unsigned char *filename, int lineNumber)
        {
                // note -- we can't call functions that potentially allocate
                //          (including CRT functions) because we'll end up
                //          with a recursive loop.
            auto *instance = AccumulatedAllocations::GetInstance();
            assert(instance);

            switch (allocType) {
            case _HOOK_ALLOC:
                ++instance->_accumulating._allocationCount;
                instance->_accumulating._allocationsSize += size;
                break;
            case _HOOK_FREE:
                ++instance->_accumulating._freeCount;
                instance->_accumulating._freesSize += size;
                break;
            case _HOOK_REALLOC:
                ++instance->_accumulating._reallocCount;
                instance->_accumulating._reallocsSize += size;
                break;
            }
    
            if (instance->_pimpl->_oldHook)
                return (*instance->_pimpl->_oldHook)(allocType, userData, size, blockType, requestNumber, filename, lineNumber);

            return 1;
        }

        auto AccumulatedAllocations::GetCurrentHeapMetrics() -> CurrentHeapMetrics
        {
            if (!_instance->_pimpl->_currentHeapMetricsCountDown) {
                _CrtMemState memState;
                _CrtMemCheckpoint(&memState);
                _instance->_pimpl->_currentHeapMetrics = {};
                _instance->_pimpl->_currentHeapMetrics._usage = memState.lSizes[_NORMAL_BLOCK];
                _instance->_pimpl->_currentHeapMetrics._blockCount = memState.lCounts[_NORMAL_BLOCK];
                _instance->_pimpl->_currentHeapMetricsCountDown = 128;
            } else {
                --_instance->_pimpl->_currentHeapMetricsCountDown;
            }
            return _instance->_pimpl->_currentHeapMetrics;
        }

    #else

        class AccumulatedAllocations::Pimpl {};
        AccumulatedAllocations::AccumulatedAllocations() {}
        AccumulatedAllocations::~AccumulatedAllocations() {}
        auto AccumulatedAllocations::GetCurrentHeapMetrics() -> CurrentHeapMetrics
        {
            CurrentHeapMetrics result;
            result._usage = result._blockCount = 0;
            return result;
        }

    #endif
}

