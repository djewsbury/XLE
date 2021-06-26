// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include <Metal/Metal.h>

#include "QueryPool.h"
#include "DeviceContext.h"
#include "../Device.h"

#pragma GCC diagnostic ignored "-Wunguarded-availability-new"

namespace RenderCore { namespace Metal_AppleMetal
{
    auto TimeStampQueryPool::SetTimeStampQuery(DeviceContext& context) -> QueryId 
    { 
        return QueryId_Invalid; 
    }

    auto TimeStampQueryPool::BeginFrame(DeviceContext& context) -> FrameId
    { 
        return FrameId_Invalid;
    }

    void TimeStampQueryPool::EndFrame(DeviceContext& context, FrameId frame)
    {}

    auto TimeStampQueryPool::GetFrameResults(DeviceContext& context, FrameId id) -> FrameResults
    {
        return FrameResults { false, false, nullptr, nullptr, 0ull };
    }

    TimeStampQueryPool::TimeStampQueryPool(ObjectFactory& factory) {}
    TimeStampQueryPool::~TimeStampQueryPool() {}

////////////////////////////////////////////////////////////////////////////////////////////////////

    auto SyncEventSet::SetEvent() -> SyncEvent {
        auto iDevice = _context->GetDevice();
        assert(iDevice);
        auto *deviceInterface = ((RenderCore::ImplAppleMetal::Device*)iDevice->QueryInterface(typeid(RenderCore::ImplAppleMetal::Device).hash_code()));
        assert(deviceInterface);
        auto device = deviceInterface->GetUnderlying();
        assert(device);
        auto deviceContext = DeviceContext::Get(*_context);
        assert(deviceContext);
        auto buffer = deviceContext->RetrieveCommandBuffer();
        assert(buffer);

        auto result = ++_nextEvent;
        std::shared_ptr<SyncEvent> lastCompletedEvent = _lastCompletedEvent;

        if (@available(iOS 12, macOS 10.14, *)) {
            IdPtr event(moveptr([device newSharedEvent]));
            assert(event);
            [event.get() notifyListener:_listener.get() atValue:1 block:^(id<MTLSharedEvent> sharedEvent, uint64_t value) {
                if (result > *lastCompletedEvent)
                    *lastCompletedEvent = result;
            }];
            assert(0);
            /*deviceContext->OnEndEncoding([deviceContext, event]{
                if (@available(iOS 12, macOS 10.14, *)) {
                    [deviceContext->RetrieveCommandBuffer() encodeSignalEvent:event.get() value:1];
                } else {
                    assert(false);
                }
            });*/
        } else {
            [buffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull) {
                if (result > *lastCompletedEvent)
                    *lastCompletedEvent = result;
            }];
        }
        return result;
    }

    auto SyncEventSet::NextEventToSet() -> SyncEvent {
        return _nextEvent + 1;
    }

    auto SyncEventSet::LastCompletedEvent() -> SyncEvent {
        return *_lastCompletedEvent;
    }

    bool SyncEventSet::IsSupported() {
        return true;
    }

    SyncEventSet::SyncEventSet(IThreadContext *context) : _context(context), _nextEvent(0), _lastCompletedEvent(new SyncEvent) {
        if (@available(iOS 12, macOS 10.14, *)) {
            _listener = [[MTLSharedEventListener alloc] init];
        }
    }

    SyncEventSet::~SyncEventSet() {}

////////////////////////////////////////////////////////////////////////////////////////////////////

    #if defined(GPUANNOTATIONS_ENABLE)

        void GPUAnnotation::Begin(DeviceContext& context, const char annotationName[])
        {
            context.PushDebugGroup(annotationName);
        }

        void GPUAnnotation::End(DeviceContext& context)
        {
            context.PopDebugGroup();
        }

        GPUAnnotation::GPUAnnotation(DeviceContext& context, const char annotationName[])
        : _context(&context)
        {
            Begin(*_context, annotationName);
        }

        GPUAnnotation::~GPUAnnotation()
        {
            if (_context)
                End(*_context);
        }

    #endif

    void    Annotator::Frame_Begin(unsigned frameID) {}
    void    Annotator::Frame_End() {}

    void    Annotator::Event(const char name[], EventTypes::BitField types)
    {
        assert(0);
        /*if (types & EventTypes::Flags::MarkerBegin) {
            GPUAnnotation::Begin(*DeviceContext::Get(context), name);
        } else if (types & EventTypes::Flags::MarkerEnd) {
            GPUAnnotation::End(*DeviceContext::Get(context));
        }*/
    }

    unsigned    Annotator::AddEventListener(const EventListener& callback) { return 0; }
    void        Annotator::RemoveEventListener(unsigned listenerId) {}

    bool		Annotator::IsCaptureToolAttached() { return false; }
    void		Annotator::BeginFrameCapture() {}
    void		Annotator::EndFrameCapture() {}

    Annotator::Annotator() {}
    Annotator::~Annotator() {}


}}
