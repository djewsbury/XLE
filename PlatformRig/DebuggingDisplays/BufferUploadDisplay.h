// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderCore/ResourceDesc.h"
#include "../../RenderCore/BufferUploads/IBufferUploads.h"
#include "../../RenderCore/BufferUploads/Metrics.h"
#include "../../RenderCore/BufferUploads/BatchedResources.h"
#include "../../Utility/Threading/Mutex.h"
#include <deque>

namespace PlatformRig { namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    class BufferUploadDisplay : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        BufferUploadDisplay(RenderCore::BufferUploads::IManager* manager);
        ~BufferUploadDisplay();
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input);

    protected:
        std::deque<RenderCore::BufferUploads::CommandListMetrics> _recentHistory;
        RenderCore::BufferUploads::IManager* _manager;

        struct GPUMetrics
        {
            float _slidingAverageCostMS;
            unsigned _slidingAverageBytesPerSecond;
            GPUMetrics();
        };

        struct FrameRecord
        {
            unsigned _frameId;
            float _gpuCost;
            GPUMetrics _gpuMetrics;
            unsigned _commandListStart, _commandListEnd;
            FrameRecord();
        };
        std::deque<FrameRecord> _frames;

        std::vector<unsigned>   _gpuEventsBuffer;
        Threading::Mutex        _gpuEventsBufferLock;

        typedef uint64_t GPUTime;
        GPUTime         _mostRecentGPUFrequency, _lastUploadBeginTime;
        float           _mostRecentGPUCost;
        unsigned        _mostRecentGPUFrameId;
        unsigned        _lockedFrameId;

        struct GraphSlot {
            std::optional<float>    _minHistory, _maxHistory;
        };
        std::vector<GraphSlot> _graphSlots;

        unsigned        _accumulatedCreateCount[(unsigned)RenderCore::BufferUploads::UploadDataType::Max];
        unsigned        _accumulatedCreateBytes[(unsigned)RenderCore::BufferUploads::UploadDataType::Max];
        unsigned        _accumulatedUploadCount[(unsigned)RenderCore::BufferUploads::UploadDataType::Max];
        unsigned        _accumulatedUploadBytes[(unsigned)RenderCore::BufferUploads::UploadDataType::Max];

        double          _reciprocalTimerFrequency;
        unsigned        _graphsMode;

        GPUMetrics      CalculateGPUMetrics();
        void            AddCommandListToFrame(unsigned frameId, unsigned commandListIndex);
        void            AddGPUToCostToFrame(unsigned frameId, float gpuCost);

        void            ProcessGPUEvents(const void* eventsBufferStart, const void* eventsBufferEnd);
        void            ProcessGPUEvents_MT(const void* eventsBufferStart, const void* eventsBufferEnd);
        static void     GPUEventListener(const void* eventsBufferStart, const void* eventsBufferEnd);
        static BufferUploadDisplay* s_gpuListenerDisplay;

        void    DrawMenuBar(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState);
        void    DrawDisplay(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState);
        void    DrawStatistics(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState, const RenderCore::BufferUploads::CommandListMetrics& mostRecentResults);
        void    DrawRecentRetirements(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState);
        size_t  FillValuesBuffer(unsigned graphType, unsigned uploadType, float valuesBuffer[], size_t valuesMaxCount);
        void    DrawDoubleGraph(
            IOverlayContext& context, Interactables& interactables, InterfaceState& interfaceState,
            const Rect& rect, unsigned topGraphSlotIdx, unsigned bottomGraphSlotIdx,
            StringSection<> topGraphName, unsigned topGraphType, unsigned topUploadType,
            StringSection<> bottomGraphName, unsigned bottomGraphType, unsigned bottomUploadType);
    };

    class BatchingDisplay : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        BatchingDisplay(std::shared_ptr<RenderCore::BufferUploads::IBatchedResources> batchedResources);
        ~BatchingDisplay();
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input);

    protected:
        RenderCore::BufferUploads::BatchingSystemMetrics _lastFrameMetrics;
        class WarmSpan
        {
        public:
            uint64_t _heapGuid;
            unsigned _begin, _end;
            unsigned _frameStart;
        };
        std::vector<WarmSpan> _warmSpans;

        float CalculateWarmth(uint64_t heapGuid, unsigned begin, unsigned end, bool allocatedMode);
        bool FindSpan(uint64_t heapGuid, unsigned begin, unsigned end, bool allocatedMode);
        std::shared_ptr<RenderCore::BufferUploads::IBatchedResources> _batchedResources;

        float _runningAveAllocs = 0;
        float _runningAveRepositions = 0;
    };
}}



