// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IBufferUploads.h"
#include "../Utility/Threading/LockFree.h"
#include <utility>
#include <thread>

namespace BufferUploads
{
        /////////////////////////////////////////////////

    class AssemblyLine;
    class ThreadContext;
    namespace PlatformInterface { class GPUEventStack; }

    class Manager : public IManager
    {
    public:
        TransactionMarker       Transaction_Begin(const ResourceDesc& desc, const std::shared_ptr<IDataPacket>& data, TransactionOptions::BitField flags) override;
        TransactionMarker       Transaction_Begin(ResourceLocator destinationResource, const std::shared_ptr<IDataPacket>& data, TransactionOptions::BitField flags) override;
        TransactionMarker       Transaction_Begin(const std::shared_ptr<IAsyncDataSource>& data, BindFlag::BitField bindFlags, TransactionOptions::BitField flags) override;
        TransactionMarker       Transaction_Begin(ResourceLocator destinationResource, const std::shared_ptr<IAsyncDataSource>& data, TransactionOptions::BitField flags) override;
        void                    Transaction_Release(TransactionID id) override;
        void                    Transaction_Validate(TransactionID id) override;

        ResourceLocator         Transaction_Immediate(
                                    RenderCore::IThreadContext& threadContext,
                                    const ResourceDesc& desc, IDataPacket& data,
                                    const PartialResource&) override;
        
        ResourceLocator         GetResource(TransactionID id);
        void                    Resource_Validate(const ResourceLocator& locator) override;
        bool                    IsComplete(CommandListID id) override;
        void                    StallUntilCompletion(RenderCore::IThreadContext& immediateContext, CommandListID id) override;

        CommandListMetrics      PopMetrics() override;
        PoolSystemMetrics       CalculatePoolMetrics() const override;

        void                    Update(RenderCore::IThreadContext&) override;
        void                    FramePriority_Barrier() override;

        EventListID             EventList_GetLatestID() override;
        void                    EventList_Get(EventListID id, Event_ResourceReposition*&begin, Event_ResourceReposition*&end) override;
        void                    EventList_Release(EventListID id) override;

        Manager(RenderCore::IDevice& renderDevice);
        ~Manager();

    private:
        std::shared_ptr<AssemblyLine> _assemblyLine;
        unsigned _foregroundStepMask, _backgroundStepMask;

        std::unique_ptr<std::thread> _backgroundThread;
        std::unique_ptr<ThreadContext> _backgroundContext;
        std::unique_ptr<ThreadContext> _foregroundContext;

        volatile bool _shutdownBackgroundThread;

        LockFreeFixedSizeQueue<unsigned, 4> _pendingFramePriority_CommandLists;

        uint32_t DoBackgroundThread();

        ThreadContext* MainContext();
        const ThreadContext* MainContext() const;
    };
}
