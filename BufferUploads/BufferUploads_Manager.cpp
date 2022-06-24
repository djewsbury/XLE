// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IBufferUploads.h"
#include "ResourceUploadHelper.h"
#include "Metrics.h"
#include "ThreadContext.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/ResourceUtils.h"
#include "../RenderCore/ResourceDesc.h"
#include "../RenderCore/Metal/Resource.h"
#include "../OSServices/Log.h"
#include "../OSServices/TimeUtils.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../ConsoleRig/AttachablePtr.h"
#include "../Utility/Threading/ThreadingUtils.h"
#include "../Utility/Threading/LockFree.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/BitUtils.h"
#include "../Utility/HeapUtils.h"
#include <assert.h>
#include <utility>
#include <algorithm>
#include <chrono>
#include <functional>
#include "thousandeyes/futures/then.h"

#pragma warning(disable:4127)       // conditional expression is constant
#pragma warning(disable:4018)       // signed/unsigned mismatch

using namespace std::chrono_literals;

namespace BufferUploads
{
    using Box2D = RenderCore::Box2D;
	namespace BindFlag = RenderCore::BindFlag;
	namespace AllocationRules = RenderCore::AllocationRules;

                    /////////////////////////////////////////////////
                ///////////////////   M A N A G E R   ///////////////////
                    /////////////////////////////////////////////////

    static UploadDataType AsUploadDataType(const ResourceDesc& desc) 
    {
        switch (desc._type) {
        case ResourceDesc::Type::LinearBuffer:     return (desc._bindFlags&BindFlag::VertexBuffer)?(UploadDataType::Vertex):(UploadDataType::Index);
        default:
        case ResourceDesc::Type::Texture:          return UploadDataType::Texture;
        }
    }
    
        ///////////////////////////////////////////////////////////////////////////////////////////////////

    class SimpleWakeupEvent
    {
    public:
        std::mutex _l;
        std::condition_variable _cv;
        volatile unsigned _semaphoreCount = 0;

        void Increment()
        {
            std::unique_lock<std::mutex> ul(_l);
            ++_semaphoreCount;
            _cv.notify_one();
        }
        void Wait()
        {
            std::unique_lock<std::mutex> ul(_l);
            if (!_semaphoreCount)
                _cv.wait(ul);
            _semaphoreCount = 0;
        }
    };
    
    class AssemblyLine : public std::enable_shared_from_this<AssemblyLine>
    {
    public:
        enum 
        {
            Step_PrepareStaging         = (1<<0),
            Step_TransferStagingToFinal = (1<<1),
            Step_CreateFromDataPacket   = (1<<2),
            Step_BatchingUpload         = (1<<3),
            Step_BatchedDefrag          = (1<<4)
        };
        
        TransactionMarker       Transaction_Begin(const ResourceDesc& desc, const std::shared_ptr<IDataPacket>& data, TransactionOptions::BitField flags);
        TransactionMarker       Transaction_Begin(ResourceLocator destinationResource, const std::shared_ptr<IDataPacket>& data, TransactionOptions::BitField flags);
        TransactionMarker       Transaction_Begin(const std::shared_ptr<IAsyncDataSource>& data, BindFlag::BitField bindFlags, TransactionOptions::BitField flags);
        TransactionMarker       Transaction_Begin(ResourceLocator destinationResource, const std::shared_ptr<IAsyncDataSource>& data, TransactionOptions::BitField flags);
        void                    Transaction_AddRef(TransactionID id);
        void                    Transaction_Release(TransactionID id);

        ResourceLocator         Transaction_Immediate(
                                    RenderCore::IThreadContext& threadContext,
                                    const ResourceDesc& desc, IDataPacket& data);

        void                Process(unsigned stepMask, ThreadContext& context, LockFreeFixedSizeQueue<unsigned, 4>& pendingFramePriorityCommandLists);
        ResourceLocator     GetResource(TransactionID id);

        void                Resource_Release(ResourceLocator& locator);
        void                Resource_AddRef(const ResourceLocator& locator);
        void                Resource_AddRef_IndexBuffer(const ResourceLocator& locator);

        AssemblyLineMetrics CalculateMetrics(ThreadContext& context);
        PoolSystemMetrics   CalculatePoolMetrics() const;
        void                Wait(unsigned stepMask, ThreadContext& context);
        void                TriggerWakeupEvent();

        unsigned            FlipWritingQueueSet();
        
        IManager::EventListID TickResourceSource(unsigned stepMask, ThreadContext& context, bool isLoading);

        AssemblyLine(RenderCore::IDevice& device);
        ~AssemblyLine();

    protected:
        struct Transaction
        {
            uint32_t _idTopPart;
            std::atomic<unsigned> _referenceCount;
            ResourceLocator _finalResource;
            ResourceDesc _desc;
            TimeMarker _requestTime;
            std::promise<ResourceLocator> _promise;
            std::future<void> _waitingFuture;

            std::atomic<bool> _statusLock;
            TransactionOptions::BitField _creationOptions;
            unsigned _heapIndex;

            Transaction(unsigned idTopPart, unsigned heapIndex);
            Transaction();
            Transaction(Transaction&& moveFrom) never_throws;
            Transaction& operator=(Transaction&& moveFrom) never_throws;
            Transaction& operator=(const Transaction& cloneFrom) = delete;
        };

        std::deque<Transaction>     _transactions;
        SimpleSpanningHeap          _transactionsHeap;

        Threading::Mutex        _transactionsLock;
        Threading::Mutex        _transactionsRepositionLock;
        std::atomic<unsigned>   _allocatedTransactionCount;
        IManager::EventListID   _transactions_resolvedEventID, _transactions_postPublishResolvedEventID;

        RenderCore::IDevice*    _device;

        Transaction*            GetTransaction(TransactionID id);
        TransactionID           AllocateTransaction(TransactionOptions::BitField flags);
        void                    ApplyRepositionEvent(ThreadContext& context, unsigned id);

        std::atomic<unsigned>   _currentQueuedBytes[(unsigned)UploadDataType::Max];
        unsigned                _nextTransactionIdTopPart;
        unsigned                _peakPrepareStaging, _peakTransferStagingToFinal, _peakCreateFromDataPacket;
        int64_t                 _waitTime;

        struct PrepareStagingStep
        {
            TransactionID _id = ~TransactionID(0);
            ResourceDesc _desc;
            std::shared_ptr<IAsyncDataSource> _packet;
            BindFlag::BitField _bindFlags = 0;
        };

        struct TransferStagingToFinalStep
        {
            TransactionID _id = ~TransactionID(0);
            ResourceDesc _finalResourceDesc;
            PlatformInterface::StagingPage::Allocation _stagingResource;
        };

        struct CreateFromDataPacketStep
        {
            TransactionID _id = ~TransactionID(0);
            ResourceDesc _creationDesc;
            std::shared_ptr<IDataPacket> _initialisationData;
        };

        class QueueSet
        {
        public:
            LockFreeFixedSizeQueue<PrepareStagingStep, 256> _prepareStagingSteps;
            LockFreeFixedSizeQueue<TransferStagingToFinalStep, 256> _transferStagingToFinalSteps;
            LockFreeFixedSizeQueue<CreateFromDataPacketStep, 256> _createFromDataPacketSteps;
        };

        QueueSet _queueSet_Main;
        QueueSet _queueSet_FramePriority[4];
        unsigned _framePriority_WritingQueueSet;

        LockFreeFixedSizeQueue<std::function<void(AssemblyLine&, ThreadContext&)>, 256> _queuedFunctions;
        SimpleWakeupEvent _wakeupEvent;

        class BatchPreparation
        {
        public:
            std::vector<CreateFromDataPacketStep> _batchedSteps;
            unsigned _batchedAllocationSize = 0;
        };
        BatchPreparation _batchPreparation_Main;

        class CommandListBudget
        {
        public:
            unsigned _limit_BytesUploaded, _limit_Operations;
            CommandListBudget(bool isLoading);
        };

        void    ResolveBatchOperation(BatchPreparation& batchOperation, ThreadContext& context, unsigned stepMask);
        void    SystemReleaseTransaction(Transaction* transaction, ThreadContext& context, bool abort = false);
        void    ClientReleaseTransaction(Transaction* transaction);

        bool    Process(const CreateFromDataPacketStep& resourceCreateStep, ThreadContext& context, const CommandListBudget& budgetUnderConstruction);
        bool    Process(const PrepareStagingStep& prepareStagingStep, ThreadContext& context, const CommandListBudget& budgetUnderConstruction);
        bool    Process(TransferStagingToFinalStep& transferStagingToFinalStep, ThreadContext& context, const CommandListBudget& budgetUnderConstruction);

        bool    ProcessQueueSet(QueueSet& queueSet, unsigned stepMask, ThreadContext& context, const CommandListBudget& budgetUnderConstruction);
        bool    DrainPriorityQueueSet(QueueSet& queueSet, unsigned stepMask, ThreadContext& context);

        void            CopyIntoBatchedBuffer(IteratorRange<void*> destination, IteratorRange<const CreateFromDataPacketStep*> steps, IteratorRange<unsigned*> offsetList, CommandListMetrics& metricsUnderConstruction);
        static bool     SortSize_LargestToSmallest(const CreateFromDataPacketStep& lhs, const CreateFromDataPacketStep& rhs);
        static bool     SortSize_SmallestToLargest(const CreateFromDataPacketStep& lhs, const CreateFromDataPacketStep& rhs);

        auto    GetQueueSet(TransactionOptions::BitField transactionOptions) -> QueueSet &;
        void    PushStep(QueueSet&, Transaction& transaction, PrepareStagingStep&& step);
        void    PushStep(QueueSet&, Transaction& transaction, TransferStagingToFinalStep&& step);
        void    PushStep(QueueSet&, Transaction& transaction, CreateFromDataPacketStep&& step);

        void    CompleteWaitForDescFuture(TransactionID transactionID, std::future<ResourceDesc> descFuture, const std::shared_ptr<IAsyncDataSource>& data, BindFlag::BitField);
        void    CompleteWaitForDataFuture(TransactionID transactionID, std::future<void> prepareFuture, PlatformInterface::StagingPage::Allocation&& stagingAllocation, const ResourceDesc& finalResourceDesc);
    };

    static void ValidatePacketSize(const ResourceDesc& desc, IDataPacket& data)
    {
        #if defined(_DEBUG)
                    //
                    //      Validate the size of information in the initialisation packet.
                    //
            if (desc._type == ResourceDesc::Type::Texture) {
                for (unsigned m=0; m<desc._textureDesc._mipCount; ++m) {
                    const size_t dataSize = data.GetData(SubResourceId{m, 0}).size();
                    if (dataSize) {
                        auto expectedSubRes = GetSubResourceOffset(desc._textureDesc, m, 0);
                        assert(dataSize == expectedSubRes._size);
                    }
                }
            }
        #endif
    }

    TransactionMarker AssemblyLine::Transaction_Begin(
        const ResourceDesc& desc, const std::shared_ptr<IDataPacket>& data, TransactionOptions::BitField flags)
    {
        assert(desc._name[0]);
        
        TransactionID transactionID = AllocateTransaction(flags);
        Transaction* transaction = GetTransaction(transactionID);
        assert(transaction);
        transaction->_desc = desc;
        if (data) ValidatePacketSize(desc, *data);

            //
            //      Have to increase _currentQueuedBytes before we push in the create step... Otherwise the create 
            //      step can actually happen first, causing _currentQueuedBytes to actually go negative! it actually
            //      happens frequently enough to create blips in the graph.
            //  
        _currentQueuedBytes[(unsigned)AsUploadDataType(desc)] += RenderCore::ByteCount(desc);

        PushStep(
            GetQueueSet(flags),
            *transaction,
            CreateFromDataPacketStep { transactionID, desc, data });

        TransactionMarker result { transaction->_promise.get_future(), transactionID, *this };
        --transaction->_referenceCount;     // todo -- can't stay like this
        return result;
    }

    TransactionMarker AssemblyLine::Transaction_Begin(ResourceLocator destinationResource, const std::shared_ptr<IDataPacket>& data, TransactionOptions::BitField flags)
    {
        auto rangeInDest = destinationResource.GetRangeInContainingResource();
        if (rangeInDest.first != ~size_t(0))
            Throw(std::runtime_error("Attempting to begin IDataPacket upload on partial/internal resource. Only full resources are supported for this variation."));
        
        TransactionID transactionID = AllocateTransaction(flags);
        Transaction* transaction = GetTransaction(transactionID);
        assert(transaction);
        auto desc = destinationResource.GetContainingResource()->GetDesc();
        transaction->_desc = desc;
        if (data) ValidatePacketSize(desc, *data);
        _currentQueuedBytes[(unsigned)AsUploadDataType(desc)] += RenderCore::ByteCount(desc);

        PushStep(
            GetQueueSet(flags),
            *transaction,
            CreateFromDataPacketStep { transactionID, desc, data });

        TransactionMarker result { transaction->_promise.get_future(), transactionID, *this };
        --transaction->_referenceCount;     // todo -- can't stay like this
        return result;
    }

    TransactionMarker AssemblyLine::Transaction_Begin(
        const std::shared_ptr<IAsyncDataSource>& data, BindFlag::BitField bindFlags, TransactionOptions::BitField flags)
    {
        TransactionID transactionID = AllocateTransaction(flags);
        Transaction* transaction = GetTransaction(transactionID);
        assert(transaction);

        TransactionMarker result { transaction->_promise.get_future(), transactionID, *this };

        // Let's optimize the case where the desc is available immediately, since certain
        // usage patterns will allow for that
        auto descFuture = data->GetDesc();
        auto status = descFuture.wait_for(0s);
        if (status == std::future_status::ready) {

            ++transaction->_referenceCount;
            CompleteWaitForDescFuture(transactionID, std::move(descFuture), data, bindFlags);

        } else {
            ++transaction->_referenceCount;

            auto weakThis = weak_from_this();
            assert(!transaction->_waitingFuture.valid());
            transaction->_waitingFuture = thousandeyes::futures::then(
                ConsoleRig::GlobalServices::GetInstance().GetContinuationExecutor(),
                std::move(descFuture),
                [weakThis, transactionID, data, bindFlags](std::future<ResourceDesc> completedFuture) {
                    auto t = weakThis.lock();
                    if (!t)
                        Throw(std::runtime_error("Assembly line was destroyed before future completed"));

                    t->CompleteWaitForDescFuture(transactionID, std::move(completedFuture), data, bindFlags);
                });
        }

        --transaction->_referenceCount;     // todo -- can't stay like this
        return result;
    }

    TransactionMarker AssemblyLine::Transaction_Begin(ResourceLocator destinationResource, const std::shared_ptr<IAsyncDataSource>& data, TransactionOptions::BitField flags)
    {
        TransactionID transactionID = AllocateTransaction(flags);
        Transaction* transaction = GetTransaction(transactionID);
        assert(transaction);
        transaction->_finalResource = std::move(destinationResource);

        TransactionMarker result { transaction->_promise.get_future(), transactionID, *this };

        // Let's optimize the case where the desc is available immediately, since certain
        // usage patterns will allow for that
        auto descFuture = data->GetDesc();
        if (descFuture.wait_for(0s) == std::future_status::ready) {

            ++transaction->_referenceCount;
            CompleteWaitForDescFuture(transactionID, std::move(descFuture), data, 0);

        } else {
            ++transaction->_referenceCount;

            auto weakThis = weak_from_this();
            assert(!transaction->_waitingFuture.valid());
            transaction->_waitingFuture = thousandeyes::futures::then(
                ConsoleRig::GlobalServices::GetInstance().GetContinuationExecutor(),
                std::move(descFuture),
                [weakThis, transactionID, data](std::future<ResourceDesc> completedFuture) {
                    auto t = weakThis.lock();
                    if (!t)
                        Throw(std::runtime_error("Assembly line was destroyed before future completed"));

                    t->CompleteWaitForDescFuture(transactionID, std::move(completedFuture), data, 0);
                });
        }

        --transaction->_referenceCount;     // todo -- can't stay like this
        return result;
    }

    void AssemblyLine::SystemReleaseTransaction(Transaction* transaction, ThreadContext& context, bool abort)
    {
        AssemblyLineRetirement retirementBuffer;
        AssemblyLineRetirement* retirement = &retirementBuffer;
        CommandListMetrics& metrics = context.GetMetricsUnderConstruction();
        if ((metrics._retirementCount+1) <= dimof(metrics._retirements)) {
            retirement = &metrics._retirements[metrics._retirementCount];
        }
            
            //
            //      We still have to do this before doing the ref count decrement.
            //      This is because we can decrement the reference count here, then the client might release it's
            //      lock shortly afterwards in another thread. The other thread might then clear out the transaction
            //      in ClientReleaseTransaction()
            //
        retirement->_desc = transaction->_desc;
        retirement->_requestTime = transaction->_requestTime;

        auto newRefCount = --transaction->_referenceCount;
        assert(newRefCount>=0);

        if (abort) {
            // If we abort with a final resource registered in the transaction, then destruction order
            // will not be controlled correctly (ie, the _retirementCommandList is set to 0, and so any
            // commands pending on a command list will not be taken into account)
            assert(transaction->_finalResource.IsEmpty());
        }

        if ((newRefCount&0x00ffffff)==0) {
                //      After the last system reference is released (regardless of client references) we call it retired...
            retirement->_retirementTime = OSServices::GetPerformanceCounter();
            if ((metrics._retirementCount+1) <= dimof(metrics._retirements)) {
                metrics._retirementCount++;
            } else {
                metrics._retirementsOverflow.push_back(*retirement);
            }
        }

        if (newRefCount<=0) {
            transaction->_finalResource = {};

            unsigned heapIndex   = transaction->_heapIndex;

                //
                //      This is a destroy event... actually we don't need to do anything.
                //      it's already considered destroyed because the ref count is 0.
                //      But let's clear out the members, anyway. This will also free the textures (if they need freeing)
                //
            *transaction = Transaction();
            transaction->_referenceCount.store(~0x0u);    // set reference count to invalid value to signal that it's ok to reuse now. Note that this has to come after all other work has completed
            --_allocatedTransactionCount;

            ScopedLock(_transactionsLock);
            _transactionsHeap.Deallocate(heapIndex<<4, 1<<4);
        }
    }

    void AssemblyLine::ClientReleaseTransaction(Transaction* transaction)
    {
        auto newRefCount = (transaction->_referenceCount -= 0x01000000);
        assert(newRefCount>=0);
        if (newRefCount<=0) {
            transaction->_finalResource = {};

            unsigned heapIndex   = transaction->_heapIndex;

            *transaction = Transaction();
            transaction->_referenceCount.store(~0x0u);
            --_allocatedTransactionCount;

            ScopedLock(_transactionsLock);
            _transactionsHeap.Deallocate(heapIndex<<4, 1<<4);
        }
    }

    void AssemblyLine::Transaction_Release(TransactionID id)
    {
        Transaction* transaction = GetTransaction(id);
        assert(transaction);
        if (transaction) {
            ClientReleaseTransaction(transaction);    // release the client ref count
        }
    }

    static std::shared_ptr<IResource> CreateResource(
        RenderCore::IDevice& device,
        const RenderCore::ResourceDesc& desc,
        IDataPacket* initPkt = nullptr)
    {
        if (initPkt) {
            return device.CreateResource(desc, PlatformInterface::AsResourceInitializer(*initPkt));
        } else {
            return device.CreateResource(desc);
        }
    }

    ResourceLocator AssemblyLine::Transaction_Immediate(
        RenderCore::IThreadContext& threadContext,
        const ResourceDesc& descInit, IDataPacket& initialisationData)
    {
        ResourceDesc desc = descInit;

        auto finalResourceConstruction = CreateResource(*threadContext.GetDevice(), desc, &initialisationData);
        if (!finalResourceConstruction)
            return {};
    
        bool didInitialisationDuringConstruction = false;
        if (!didInitialisationDuringConstruction) {

            assert(0);      // do we need a separate staging page for immediate/main thread initializations?
#if 0
            assert(desc._bindFlags & BindFlag::TransferDst);    // need TransferDst to recieve staging data
            
            ResourceDesc stagingDesc;
            PlatformInterface::StagingToFinalMapping stagingToFinalMapping;
            std::tie(stagingDesc, stagingToFinalMapping) = PlatformInterface::CalculatePartialStagingDesc(desc, part);

            auto stagingConstruction = _resourceSource.Create(stagingDesc, &initialisationData, ResourceSource::CreationOptions::Staging);
            assert(!stagingConstruction._locator.IsEmpty());
            if (stagingConstruction._locator.IsEmpty())
                return {};
    
            PlatformInterface::ResourceUploadHelper helper(threadContext);
            if (desc._type == ResourceDesc::Type::Texture) {
                helper.WriteToTextureViaMap(
                    stagingConstruction._locator,
                    stagingDesc, Box2D(),
                    [&part, &initialisationData](RenderCore::SubResourceId sr) -> RenderCore::SubResourceInitData
                    {
                        RenderCore::SubResourceInitData result = {};
                        result._data = initialisationData.GetData(SubResourceId{sr._mip, sr._arrayLayer});
                        assert(result._data.empty());
                        result._pitches = initialisationData.GetPitches(SubResourceId{sr._mip, sr._arrayLayer});
                        return result;
                    });
            } else {
                helper.WriteToBufferViaMap(
                    stagingConstruction._locator, stagingDesc,
                    0, initialisationData.GetData());
            }
    
            helper.UpdateFinalResourceFromStaging(
                finalResourceConstruction._locator, 
                stagingConstruction._locator, desc, 
                stagingToFinalMapping);
#endif
        }
    
        return finalResourceConstruction;
    }

    void AssemblyLine::Transaction_AddRef(TransactionID id)
    {
        Transaction* transaction = GetTransaction(id);
        assert(transaction);
        if (transaction) {
            transaction->_referenceCount += 0x01000000;
        }
    }

        //////////////////////////////////////////////////////////////////////////////////////////////

    AssemblyLine::Transaction::Transaction(unsigned idTopPart, unsigned heapIndex)
    {
        _idTopPart = idTopPart;
        _statusLock = 0;
        _referenceCount = 0;
        _creationOptions = 0;
        _heapIndex = heapIndex;
    }

    AssemblyLine::Transaction::Transaction()
    {
        _idTopPart = 0;
        _statusLock = 0;
        _referenceCount = 0;
        _creationOptions = 0;
        _heapIndex = ~unsigned(0x0);
    }

    AssemblyLine::Transaction::Transaction(Transaction&& moveFrom) never_throws
    {
        _referenceCount = 0;
        _statusLock = 0;

        _idTopPart = moveFrom._idTopPart;
        _finalResource = std::move(moveFrom._finalResource);
        _desc = moveFrom._desc;
        _requestTime = moveFrom._requestTime;
        _promise = std::move(moveFrom._promise);
        _waitingFuture = std::move(moveFrom._waitingFuture);

        _creationOptions = moveFrom._creationOptions;
        _heapIndex = moveFrom._heapIndex;

        moveFrom._idTopPart = 0;
        moveFrom._statusLock = 0;
        moveFrom._referenceCount = 0;
        moveFrom._creationOptions = 0;
        moveFrom._heapIndex = ~unsigned(0x0);
    }

    auto AssemblyLine::Transaction::operator=(Transaction&& moveFrom) never_throws -> Transaction&
    {
        for (;;) {
            bool expected = false;
            if (_statusLock.compare_exchange_strong(expected, true)) break;
            Threading::Pause();
        }

        _idTopPart = moveFrom._idTopPart;
        _finalResource = std::move(moveFrom._finalResource);
        _desc = moveFrom._desc;
        _requestTime = moveFrom._requestTime;
        _promise = std::move(moveFrom._promise);
        _waitingFuture = std::move(moveFrom._waitingFuture);

        _creationOptions = moveFrom._creationOptions;
        _heapIndex = moveFrom._heapIndex;

        moveFrom._idTopPart = 0;
        moveFrom._statusLock = 0;
        moveFrom._referenceCount = 0;
        moveFrom._creationOptions = 0;
        moveFrom._heapIndex = ~unsigned(0x0);

        auto lockRelease = _statusLock.exchange(false);
        assert(lockRelease==1); (void)lockRelease;

            // note that reference counts are unaffected here!
            // the reference count for "this" and "moveFrom" don't change

        return *this;
    }

    AssemblyLine::AssemblyLine(RenderCore::IDevice& device)
    :   _device(&device)
    ,   _transactionsHeap((2*1024)<<4)
    {
        _nextTransactionIdTopPart = 64;
        _transactions.resize(2*1024);
        for (auto i=_transactions.begin(); i!=_transactions.end(); ++i)
            i->_referenceCount.store(~0x0u);
        _peakPrepareStaging = _peakTransferStagingToFinal =_peakCreateFromDataPacket = 0;
        _allocatedTransactionCount = 0;
        XlZeroMemory(_currentQueuedBytes);
        _transactions_resolvedEventID = _transactions_postPublishResolvedEventID = 0;
        _framePriority_WritingQueueSet = 0;
    }

    AssemblyLine::~AssemblyLine()
    {
        // Ensure we destroy all transactions before we destroy the resource source
        // (otherwise the resource source will consider allocations left in transactions as leaks)
        _transactions.clear();
    }

    TransactionID AssemblyLine::AllocateTransaction(TransactionOptions::BitField flags)
    {
            //  Note; some of the vector code here is not thread safe... We can't have 
            //      two threads in AllocateTransaction at the same time. Let's just use a mutex.
        ScopedLock(_transactionsLock);

        TransactionID result;
        uint32_t idTopPart = _nextTransactionIdTopPart++;

        if (_transactionsHeap.CalculateHeapSize() + (1<<4) > 0xffff)
            Throw(::Exceptions::BasicLabel("Buffer uploads spanning heap reached maximium size. Aborting transaction."));

        result = _transactionsHeap.Allocate(1<<4);
        if (result == ~unsigned(0x0)) {
            result = _transactionsHeap.AppendNewBlock(1<<4);
        }

        result >>= 4;
        if (result >= _transactions.size()) {
            _transactions.resize((unsigned int)(result+1));
        }
        auto destinationPosition = _transactions.begin() + ptrdiff_t(result);
        result |= uint64_t(idTopPart)<<32ull;

        Transaction newTransaction(idTopPart, uint32_t(result));
        newTransaction._requestTime = OSServices::GetPerformanceCounter();
        newTransaction._creationOptions = flags;

            // Start with a client ref count 1 & system ref count 1
        destinationPosition->_referenceCount.store(0x01000001);
        ++_allocatedTransactionCount;

        *destinationPosition = std::move(newTransaction);

        return result;
    }

    AssemblyLine::Transaction* AssemblyLine::GetTransaction(TransactionID id)
    {
        unsigned index = unsigned(id);
        unsigned key = unsigned(id>>32ull);
        AssemblyLine::Transaction* result = nullptr;
        ScopedLock(_transactionsLock);       // must be locked when using the deque method... if the deque is resized at the same time, operator[] can seem to fail
        if ((index < _transactions.size()) && (key == _transactions[index]._idTopPart))
            result = &_transactions[index];
        if (result) assert(result->_referenceCount.load());     // this is only thread safe if there's some kind of reference on the transaction
        return result;
    }

    void AssemblyLine::Wait(unsigned stepMask, ThreadContext& context)
    {
        int64_t startTime = OSServices::GetPerformanceCounter();
        _wakeupEvent.Wait();

        CommandListMetrics& metricsUnderConstruction = context.GetMetricsUnderConstruction();
        metricsUnderConstruction._waitTime += OSServices::GetPerformanceCounter() - startTime;
        metricsUnderConstruction._wakeCount++;
    }

    void AssemblyLine::TriggerWakeupEvent()
    {
        _wakeupEvent.Increment();
    }

    bool AssemblyLine::SortSize_LargestToSmallest(const CreateFromDataPacketStep& lhs, const CreateFromDataPacketStep& rhs)     { return RenderCore::ByteCount(lhs._creationDesc) > RenderCore::ByteCount(rhs._creationDesc); }
    bool AssemblyLine::SortSize_SmallestToLargest(const CreateFromDataPacketStep& lhs, const CreateFromDataPacketStep& rhs)     { return RenderCore::ByteCount(lhs._creationDesc) < RenderCore::ByteCount(rhs._creationDesc); }

    void AssemblyLine::CopyIntoBatchedBuffer(   
        IteratorRange<void*> destination, 
        IteratorRange<const CreateFromDataPacketStep*> steps,
        IteratorRange<unsigned*> offsetList, 
        CommandListMetrics& metricsUnderConstruction)
    {
        assert(offsetList.size() == steps.size());
        unsigned queuedBytesAdjustment[dimof(_currentQueuedBytes)];
        XlZeroMemory(queuedBytesAdjustment);

        unsigned offset = 0;
        unsigned* offsetWriteIterator=offsetList.begin();
        for (const CreateFromDataPacketStep* i=steps.begin(); i!=steps.end(); ++i, ++offsetWriteIterator) {
            Transaction* transaction = GetTransaction(i->_id);
            assert(transaction);
            unsigned size = RenderCore::ByteCount(transaction->_desc);
            IteratorRange<void*> sourceData;
            if (i->_initialisationData)
                sourceData = i->_initialisationData->GetData();
            if (!sourceData.empty() && !destination.empty()) {
                assert(size == sourceData.size());
                assert(offset+size <= destination.size());
                XlCopyMemoryAlign16(PtrAdd(destination.begin(), offset), sourceData.begin(), size);
            }
            (*offsetWriteIterator) = offset;
            queuedBytesAdjustment[(unsigned)AsUploadDataType(transaction->_desc)] -= size;
            offset += MarkerHeap<uint16_t>::AlignSize(size);
        }

        for (unsigned c=0; c<dimof(queuedBytesAdjustment); ++c) {
            _currentQueuedBytes[c] += queuedBytesAdjustment[c];
        }
    }

    static unsigned ResolveOffsetValue(unsigned inputOffset, unsigned size, const std::vector<DefragStep>& steps)
    {
        for (std::vector<DefragStep>::const_iterator i=steps.begin(); i!=steps.end(); ++i) {
            if (inputOffset >= i->_sourceStart && inputOffset < i->_sourceEnd) {
                assert((inputOffset+size) <= i->_sourceEnd);
                return inputOffset + i->_destination - i->_sourceStart;
            }
        }
        assert(0);
        return inputOffset;
    }

    void AssemblyLine::ApplyRepositionEvent(ThreadContext& context, unsigned id)
    {
            //
            //      We need to prevent GetTransaction from returning a partial result while this is occuring
            //      Since we modify both transaction._finalResource & transaction._resourceOffsetValue, it's
            //      possible that another thread could get the update of one, but not the other. So we have
            //      to lock. It might be ok if we went through and cleared all of the _finalResource values
            //      of the transactions we're going to change first -- but there's still a tiny chance that
            //      that method would fail.
            //
        ScopedLock(_transactionsRepositionLock);

        Event_ResourceReposition* begin = NULL, *end = NULL;
        context.EventList_Get(id, begin, end);
        const size_t temporaryCount = _transactions.size();
        for (const Event_ResourceReposition*e = begin; e!=end; ++e) {
            assert(e->_newResource && e->_originalResource && !e->_defragSteps.empty());

                // ... check temporary transactions ...
            for (unsigned c=0; c<temporaryCount; ++c) {
                Transaction& transaction = _transactions[c];
                if (transaction._finalResource.GetContainingResource().get() == e->_originalResource.get()) {
                    auto size = RenderCore::ByteCount(transaction._desc);

                    ResourceLocator oldLocator = std::move(transaction._finalResource);
                    unsigned oldOffset = oldLocator.GetRangeInContainingResource().first;

                    unsigned newOffsetValue = ResolveOffsetValue(oldOffset, RenderCore::ByteCount(transaction._desc), e->_defragSteps);
                    transaction._finalResource = ResourceLocator{
                        e->_newResource, newOffsetValue, size, e->_pool, e->_poolMarker};
                }
            }
        }
        context.EventList_Release(id, true);
    }

    IManager::EventListID AssemblyLine::TickResourceSource(unsigned stepMask, ThreadContext& context, bool isLoading)
    {
        IManager::EventListID processedEventList     = context.EventList_GetProcessedID();
        IManager::EventListID publishableEventList   = context.EventList_GetWrittenID();

        if ((stepMask & Step_BatchedDefrag) && !isLoading) {        // don't do the defrag while we're loading

                //
                //      It's annoying, but we have to do the repositioning of the transactions list twice...
                //      Once to remove any new references to the old resource. And second to remove any 
                //      references that might have been added by the client through Transaction_Begin
                //
            if (_transactions_postPublishResolvedEventID < processedEventList) {
                for (unsigned c=_transactions_postPublishResolvedEventID+1; c<=processedEventList; ++c) { ApplyRepositionEvent(context, c); }
                _transactions_postPublishResolvedEventID = processedEventList;
            }

            #if 0
                static bool doDefrag = true;
                if (doDefrag) {
                    _resourceSource.Tick(context, processedEventList);
                }
            #endif

            publishableEventList = context.EventList_GetWrittenID();

                //
                //      If we've got any completed/resolved reposition events, we need to modify any transactions in flight
                //      But -- don't lock the transactions list for this. Any newly added transactions from this pointer
                //          will be in the new coordinate system (because we're only resolving our references after the
                //          client has also done so)
                //

            if (_transactions_resolvedEventID < publishableEventList) {
                for (unsigned c=_transactions_resolvedEventID+1; c<=publishableEventList; ++c) { ApplyRepositionEvent(context, c); }
                _transactions_resolvedEventID = publishableEventList;
            }

                //
                //      Because we took EventList_GetProcessedID before we did FlushDelayedReleases(), all remaining releases in 
                //      the delayed releases list should be pointing to the new resource, and in the new coordinate system
                //
        }

        return publishableEventList;
    }

    void AssemblyLine::ResolveBatchOperation(BatchPreparation& batchOperation, ThreadContext& context, unsigned stepMask)
    {
#if 0
        CommandListMetrics& metricsUnderConstruction = context.GetMetricsUnderConstruction();
        if (!batchOperation._batchedSteps.empty() && batchOperation._batchedAllocationSize) {

                //
                //      Sort largest to smallest. This is an attempt to reduce fragmentation slightly by grouping
                //      large and small allocations. Plus, this should guarantee good packing into the batch size limit.
                //

            std::sort(batchOperation._batchedSteps.begin(), batchOperation._batchedSteps.end(), SortSize_SmallestToLargest);

                //
                //      Perform all batched operations before resolving a command list...
                //

            const unsigned maxSingleBatch = RenderCore::ByteCount(_resourceSource.GetBatchedResources().GetPrototype());
            auto batchingI      = batchOperation._batchedSteps.begin();
            auto batchingStart  = batchOperation._batchedSteps.begin();
            unsigned currentBatchSize = 0;
            if (batchOperation._batchedAllocationSize <= maxSingleBatch) {
                // If we know we can fit the whole thing with one go; just go ahead and do it
                batchingI = batchOperation._batchedSteps.end();
                currentBatchSize = batchOperation._batchedAllocationSize;
            }

            for (;;) {
                unsigned nextSize = 0;
                if (batchingI!=batchOperation._batchedSteps.end())
                    nextSize = MarkerHeap<uint16_t>::AlignSize(RenderCore::ByteCount(batchingI->_creationDesc));

                if (batchingI == batchOperation._batchedSteps.end() || (currentBatchSize+nextSize) > maxSingleBatch) {
                    ResourceLocator batchedResource;
                    for (;;) {
                        batchedResource = _resourceSource.GetBatchedResources().Allocate(currentBatchSize, "SuperBlock");
                        if (!batchedResource.IsEmpty()) {
                            break;
                        }
                        Log(Warning) << "Resource creation failed in BatchedResources::Allocate(). Sleeping and attempting again" << std::endl;
                        Threading::Sleep(16);
                    }

                    std::vector<unsigned> offsets;
                    offsets.resize(std::distance(batchingStart, batchingI), 0);

                        //
                        //      Success! Map & memcpy the data in. Use just one Map for all of the batched buffers, but
                        //      we can do separate memcpys.
                        //      We're using no-overwrite memcpys... so let's hope we get immediate access to the GPU buffer,
                        //      and can copy in the data (with a CPU-assisted copy, with no GPU cost associated)
                        //
                        //      We must do a discard map after a device creation on a D3D11 deferred context. But; there are some
                        //      cases were we should do a discard map on the resource even when deviceAllocation is false
                        //

                    std::vector<uint8_t> midwayBuffer(currentBatchSize);
                    CopyIntoBatchedBuffer(
                        MakeIteratorRange(midwayBuffer),
                        MakeIteratorRange(batchingStart, batchingI),
                        MakeIteratorRange(offsets), 
                        metricsUnderConstruction);
                            
                    if (stepMask & Step_BatchingUpload) {

                        assert(_resourceSource.GetBatchedResources().GetPrototype()._type == ResourceDesc::Type::LinearBuffer);
                        context.GetResourceUploadHelper().WriteToBufferViaMap(
                            batchedResource, _resourceSource.GetBatchedResources().GetPrototype(), 
                            0, MakeIteratorRange(midwayBuffer));
                        midwayBuffer = {};

                    } else {

                            //
                            //      This will offload the actual map & copy into another thread. This seems to be a little better for D3D when
                            //      we want to write directly into video memory. Note that there's a copy step here, though -- so we don't get 
                            //      the minimum number of copies
                            //

                        context.GetCommitStepUnderConstruction().Add(
                            CommitStep::DeferredCopy{batchedResource, _resourceSource.GetBatchedResources().GetPrototype(), std::move(midwayBuffer)});

                    }

                    metricsUnderConstruction._batchedUploadBytes += currentBatchSize;
                    metricsUnderConstruction._bytesUploadTotal += currentBatchSize;
                    metricsUnderConstruction._batchedUploadCount ++;

                        // now apply the result to the transactions, and release them...
                    auto o=offsets.begin();
                    for (auto i=batchingStart; i!=batchingI; ++i, ++o) {
                        auto byteCount = RenderCore::ByteCount(i->_creationDesc);
                        unsigned uploadDataType = (unsigned)AsUploadDataType(i->_creationDesc);
                        metricsUnderConstruction._bytesUploaded[uploadDataType] += byteCount;
                        metricsUnderConstruction._countUploaded[uploadDataType] += 1;

                        Transaction* transaction = GetTransaction(i->_id);
                        transaction->_finalResource = batchedResource.MakeSubLocator(*o, byteCount);
                        transaction->_promise.set_value(transaction->_finalResource);
                        SystemReleaseTransaction(transaction, context);
                    }

                    if (batchingI == batchOperation._batchedSteps.end()) {
                        break;
                    }
                    batchingStart = batchingI;
                    currentBatchSize = 0;
                } else {
                    ++batchingI;
                    currentBatchSize += nextSize;
                }
            }
        }
#endif
    }

    AssemblyLine::CommandListBudget::CommandListBudget(bool isLoading)
    {
        if (true) { // isLoading) {
            _limit_BytesUploaded     = ~unsigned(0x0);
            _limit_Operations        = ~unsigned(0x0);
        } else {
                // ~    Default budget during run-time    ~ //
            _limit_BytesUploaded     = 5 * 1024 * 1024;
            _limit_Operations        = 64;
        }
    }

    bool AssemblyLine::Process(const CreateFromDataPacketStep& resourceCreateStep, ThreadContext& context, const CommandListBudget& budgetUnderConstruction)
    {
        auto& metricsUnderConstruction = context.GetMetricsUnderConstruction();
        if ((metricsUnderConstruction._contextOperations+1) >= budgetUnderConstruction._limit_Operations)
            return false;

        auto* transaction = GetTransaction(resourceCreateStep._id);
        assert(transaction);

        assert(resourceCreateStep._initialisationData);
        auto objectSize = RenderCore::ByteCount(resourceCreateStep._creationDesc);
        auto uploadRequestSize = objectSize;
        auto uploadDataType = (unsigned)AsUploadDataType(resourceCreateStep._creationDesc);

        /*if (!(transaction->_referenceCount & 0xff000000)) {
                //  If there are no client references, we can consider this cancelled...
            transaction->_promise.set_exception(std::make_exception_ptr(std::runtime_error("Aborted because client references were released")));
            ReleaseTransaction(transaction, context, true);
            _currentQueuedBytes[uploadDataType] -= uploadRequestSize;
            return true;
        }*/

        if ((metricsUnderConstruction._bytesUploadTotal+uploadRequestSize) > budgetUnderConstruction._limit_BytesUploaded && metricsUnderConstruction._bytesUploadTotal !=0)
            return false;

        ResourceLocator finalConstruction;
        bool deviceConstructionInvoked = false;
        bool didInitialisationDuringCreation = false;
        if (transaction->_finalResource.IsEmpty()) {
            // No resource provided beforehand -- have to create it now
            #if 0       // batching path
                if (resourceCreateStep._creationDesc._type == ResourceDesc::Type::LinearBuffer && _resourceSource.CanBeBatched(resourceCreateStep._creationDesc)) {
                        //      In the batched path, we pop now, and perform all of the batched operations as once when we resolve the 
                        //      command list. But don't release the transaction -- that will happen after the batching operation is 
                        //      performed.
                    _batchPreparation_Main._batchedSteps.push_back(resourceCreateStep);
                    _batchPreparation_Main._batchedAllocationSize += MarkerHeap<uint16_t>::AlignSize(objectSize);
                    return true;
                }
            #endif

            auto supportInit = 
                (resourceCreateStep._creationDesc._type == ResourceDesc::Type::Texture)
                ? PlatformInterface::SupportsResourceInitialisation_Texture
                : PlatformInterface::SupportsResourceInitialisation_Buffer;

            if (resourceCreateStep._initialisationData && supportInit) {
                finalConstruction = CreateResource(
                    context.GetRenderCoreDevice(),
                    resourceCreateStep._creationDesc, resourceCreateStep._initialisationData.get());
                didInitialisationDuringCreation = true;
            } else {
                auto modifiedDesc = resourceCreateStep._creationDesc;
                modifiedDesc._bindFlags |= BindFlag::TransferDst;
                finalConstruction = CreateResource(context.GetRenderCoreDevice(), modifiedDesc);
            }
            deviceConstructionInvoked = true;

            if (finalConstruction.IsEmpty())
                return false;
        } else {
            finalConstruction = transaction->_finalResource;
        }

        if (!didInitialisationDuringCreation) {
            assert(finalConstruction.GetContainingResource()->GetDesc()._bindFlags & BindFlag::TransferDst);    // need TransferDst to recieve staging data

            auto& helper = context.GetResourceUploadHelper();
            if (!helper.CanDirectlyMap(*finalConstruction.GetContainingResource())) {

                auto stagingByteCount = objectSize;
                auto alignment = helper.CalculateStagingBufferOffsetAlignment(resourceCreateStep._creationDesc);

                auto stagingConstruction = context.GetStagingPage().Allocate(stagingByteCount, alignment);
                if (!stagingConstruction) {
                    // we will return, so keep the resource until then
                    transaction->_finalResource = finalConstruction;
                    return false;
                }
                metricsUnderConstruction._stagingBytesUsed[uploadDataType] += stagingConstruction.GetAllocationSize();

                if (resourceCreateStep._creationDesc._type == ResourceDesc::Type::Texture) {
                    helper.WriteViaMap(
                        context.GetStagingPage().GetStagingResource(),
                        stagingConstruction.GetResourceOffset(), stagingConstruction.GetAllocationSize(),
                        resourceCreateStep._creationDesc._textureDesc,
                        PlatformInterface::AsResourceInitializer(*resourceCreateStep._initialisationData));
                } else {
                    helper.WriteViaMap(
                        context.GetStagingPage().GetStagingResource(),
                        stagingConstruction.GetResourceOffset(), stagingConstruction.GetAllocationSize(),
                        resourceCreateStep._initialisationData->GetData());
                }
        
                helper.UpdateFinalResourceFromStaging(
                    finalConstruction,
                    context.GetStagingPage().GetStagingResource(),
                    stagingConstruction.GetResourceOffset(), stagingConstruction.GetAllocationSize());

                stagingConstruction.Release(context.GetProducerQueueMarker());

            } else {

                // destination is in host-visible memory, we can just write directly to it
                if (resourceCreateStep._creationDesc._type == ResourceDesc::Type::Texture) {
                    helper.WriteViaMap(
                        *finalConstruction.AsIndependentResource(),
                        [initialisationData{resourceCreateStep._initialisationData.get()}](RenderCore::SubResourceId sr) -> RenderCore::SubResourceInitData
                        {
                            RenderCore::SubResourceInitData result = {};
                            result._data = initialisationData->GetData(SubResourceId{sr._mip, sr._arrayLayer});
                            assert(!result._data.empty());
                            result._pitches = initialisationData->GetPitches(SubResourceId{sr._mip, sr._arrayLayer});
                            return result;
                        });
                } else {
                    helper.WriteViaMap(
                        finalConstruction,
                        resourceCreateStep._initialisationData->GetData());
                }
            }

            ++metricsUnderConstruction._contextOperations;
        }

        metricsUnderConstruction._bytesUploaded[uploadDataType] += uploadRequestSize;
        metricsUnderConstruction._countUploaded[uploadDataType] += 1;
        metricsUnderConstruction._bytesUploadTotal += uploadRequestSize;
        _currentQueuedBytes[uploadDataType] -= uploadRequestSize;
        metricsUnderConstruction._bytesCreated[uploadDataType] += objectSize;
        metricsUnderConstruction._countCreations[uploadDataType] += 1;
        if (deviceConstructionInvoked) {
            ++metricsUnderConstruction._countDeviceCreations[uploadDataType];
            ++metricsUnderConstruction._deviceCreateOperations;
        }

        // Embue the final resource with the completion command list information
        transaction->_finalResource = ResourceLocator { std::move(finalConstruction), context.CommandList_GetUnderConstruction() };
        transaction->_promise.set_value(transaction->_finalResource);

        SystemReleaseTransaction(transaction, context);
        return true;
    }

    bool AssemblyLine::Process(const PrepareStagingStep& prepareStagingStep, ThreadContext& context, const CommandListBudget& budgetUnderConstruction)
    {
        auto& metricsUnderConstruction = context.GetMetricsUnderConstruction();
        if ((metricsUnderConstruction._contextOperations+1) >= budgetUnderConstruction._limit_Operations)
            return false;

        // todo -- should we limit this based on the number of items in the WaitForDataFutureStep
        //      stage?

        auto* transaction = GetTransaction(prepareStagingStep._id);
        assert(transaction);

        /*if (!(transaction->_referenceCount & 0xff000000)) {
            transaction->_promise.set_exception(std::make_exception_ptr(std::runtime_error("Aborted because client references were released")));
            ReleaseTransaction(transaction, context, true);
            return true;
        }*/

        try {
            const auto& desc = prepareStagingStep._desc;
            auto byteCount = RenderCore::ByteCount(desc);
            auto alignment = context.GetResourceUploadHelper().CalculateStagingBufferOffsetAlignment(desc);
            auto stagingConstruction = context.GetStagingPage().Allocate(byteCount, alignment);
            if (!stagingConstruction)   // hit our limit right now -- might have to wait until some of the scheduled uploads have completed
                return false;
            metricsUnderConstruction._stagingBytesUsed[(unsigned)AsUploadDataType(desc)] += stagingConstruction.GetAllocationSize();

            using namespace RenderCore;
            Metal::ResourceMap map{
                context.GetRenderCoreDevice(),      // we can also get the device context with *Metal::DeviceContext::Get(*context.GetRenderCoreThreadContext())
                context.GetStagingPage().GetStagingResource(),
                Metal::ResourceMap::Mode::WriteDiscardPrevious,
                stagingConstruction.GetResourceOffset(), stagingConstruction.GetAllocationSize()};
            std::vector<IAsyncDataSource::SubResource> uploadList;
            if (desc._type == ResourceDesc::Type::Texture) {

                // array the upload locations as per required for a staging texture
                auto arrayCount = ActualArrayLayerCount(desc._textureDesc);
                auto mipCount = desc._textureDesc._mipCount;
                assert(mipCount >= 1);
                assert(arrayCount >= 1);

                uploadList.resize(mipCount*arrayCount);
                for (unsigned a=0; a<arrayCount; ++a) {
                    for (unsigned mip=0; mip<mipCount; ++mip) {
                        SubResourceId subRes { mip, a };
                        auto& upload = uploadList[subRes._arrayLayer*mipCount+subRes._mip];
                        upload._id = subRes;
                        auto offset = GetSubResourceOffset(desc._textureDesc, mip, a);
                        upload._destination = { PtrAdd(map.GetData().begin(), offset._offset), PtrAdd(map.GetData().begin(), offset._offset+offset._size) };
                        upload._pitches = offset._pitches;
                    }
                }

            } else {
                uploadList.resize(1);
                auto& upload = uploadList[0];
                upload._id = {};
                upload._destination = map.GetData(upload._id);
                upload._pitches = map.GetPitches(upload._id);
            }

            auto future = prepareStagingStep._packet->PrepareData(uploadList);

            RenderCore::ResourceDesc finalResourceDesc = desc;
            finalResourceDesc._bindFlags = prepareStagingStep._bindFlags;
            finalResourceDesc._bindFlags |= BindFlag::TransferDst;         // since we're using a staging buffer to prepare, we must allow for transfers

            // inc reference count for the lambda that waits on the future
            ++transaction->_referenceCount;

            auto weakThis = weak_from_this();
            assert(!transaction->_waitingFuture.valid());
            transaction->_waitingFuture = thousandeyes::futures::then(
                ConsoleRig::GlobalServices::GetInstance().GetContinuationExecutor(),
                std::move(future),
                [   captureMap{std::move(map)}, 
                    weakThis, 
                    transactionID{prepareStagingStep._id}, 
                    pkt{prepareStagingStep._packet},        // need to retain pkt until PrepareData completes
                    stagingConstruction{std::move(stagingConstruction)},
                    finalResourceDesc]
                (std::future<void> prepareFuture) mutable {
                    auto t = weakThis.lock();
                    if (!t)
                        Throw(std::runtime_error("Assembly line was destroyed before future completed"));

                    captureMap = {};
                    t->CompleteWaitForDataFuture(transactionID, std::move(prepareFuture), std::move(stagingConstruction), finalResourceDesc);
                });

        } catch (...) {
            transaction->_promise.set_exception(std::current_exception());
            _currentQueuedBytes[(unsigned)AsUploadDataType(transaction->_desc)] -= RenderCore::ByteCount(transaction->_desc);
        }

        SystemReleaseTransaction(transaction, context);
        return true;
    }

    void    AssemblyLine::CompleteWaitForDescFuture(TransactionID transactionID, std::future<ResourceDesc> descFuture, const std::shared_ptr<IAsyncDataSource>& data, BindFlag::BitField bindFlags)
    {
        Transaction* transaction = GetTransaction(transactionID);
        assert(transaction);

        transaction->_waitingFuture = {};
        
        try {
            auto desc = descFuture.get();
            transaction->_desc = desc;
            _currentQueuedBytes[(unsigned)AsUploadDataType(desc)] += RenderCore::ByteCount(desc);
            PushStep(
                GetQueueSet(transaction->_creationOptions),
                *transaction,
                PrepareStagingStep { transactionID, desc, data, bindFlags });
        } catch (...) {
            transaction->_promise.set_exception(std::current_exception());
        }

        _queuedFunctions.push_overflow(
            [transactionID](AssemblyLine& assemblyLine, ThreadContext& context) {
                Transaction* transaction = assemblyLine.GetTransaction(transactionID);
                assert(transaction);
                assemblyLine.SystemReleaseTransaction(transaction, context);
            });
        _wakeupEvent.Increment();
    }

    void AssemblyLine::CompleteWaitForDataFuture(TransactionID transactionID, std::future<void> prepareFuture, PlatformInterface::StagingPage::Allocation&& stagingAllocation, const ResourceDesc& finalResourceDesc)
    {
        auto* transaction = GetTransaction(transactionID);
        assert(transaction);
        assert(stagingAllocation);

        transaction->_waitingFuture = {};

        // Any exceptions get passed along to the transaction's future. Otherwise we just queue up the
        // next step
        try {
            prepareFuture.get();
            PushStep(
                GetQueueSet(transaction->_creationOptions),
                *transaction,
                TransferStagingToFinalStep { transactionID, finalResourceDesc, std::move(stagingAllocation) });
        } catch(...) {
            transaction->_promise.set_exception(std::current_exception());
        }

        _queuedFunctions.push_overflow(
            [transactionID](AssemblyLine& assemblyLine, ThreadContext& context) {
                Transaction* transaction = assemblyLine.GetTransaction(transactionID);
                assert(transaction);
                assemblyLine.SystemReleaseTransaction(transaction, context);
            });
        _wakeupEvent.Increment();
    }

    bool AssemblyLine::Process(TransferStagingToFinalStep& transferStagingToFinalStep, ThreadContext& context, const CommandListBudget& budgetUnderConstruction)
    {
        CommandListMetrics& metricsUnderConstruction = context.GetMetricsUnderConstruction();
        if ((metricsUnderConstruction._contextOperations+1) >= budgetUnderConstruction._limit_Operations)
            return false;

        Transaction* transaction = GetTransaction(transferStagingToFinalStep._id);
        assert(transaction);
        auto dataType = (unsigned)AsUploadDataType(transferStagingToFinalStep._finalResourceDesc);

        /*if (!(transaction->_referenceCount & 0xff000000)) {
            transaction->_promise.set_exception(std::make_exception_ptr(std::runtime_error("Aborted because client references were released")));
            ReleaseTransaction(transaction, context, true);
            _currentQueuedBytes[dataType] -= transferStagingToFinalStep._stagingByteCount;
            return true;
        }*/

        //if ((metricsUnderConstruction._bytesUploadTotal+transferStagingToFinalStep._stagingByteCount) > budgetUnderConstruction._limit_BytesUploaded && metricsUnderConstruction._bytesUploadTotal !=0)
            // return false;

        try {
            if (transaction->_finalResource.IsEmpty()) {
                auto finalConstruction = CreateResource(context.GetRenderCoreDevice(), transferStagingToFinalStep._finalResourceDesc);
                if (!finalConstruction)
                    return false;                   // failed to allocate the resource. Return false and We'll try again later...

                transaction->_finalResource = finalConstruction;

                metricsUnderConstruction._bytesCreated[dataType] += RenderCore::ByteCount(transferStagingToFinalStep._finalResourceDesc);
                metricsUnderConstruction._countCreations[dataType] += 1;
                metricsUnderConstruction._countDeviceCreations[dataType] += 1;
            }

            // Do the actual data copy step here
            assert(transferStagingToFinalStep._stagingResource);
            context.GetResourceUploadHelper().UpdateFinalResourceFromStaging(
                transaction->_finalResource,
                context.GetStagingPage().GetStagingResource(),
                transferStagingToFinalStep._stagingResource.GetResourceOffset(), transferStagingToFinalStep._stagingResource.GetAllocationSize());

            // Don't delete the staging buffer immediately. It must stick around until the command list is resolved
            // and done with it
            transferStagingToFinalStep._stagingResource.Release(context.GetProducerQueueMarker());

            // Embue the final resource with the completion command list information
            transaction->_finalResource = ResourceLocator { std::move(transaction->_finalResource), context.CommandList_GetUnderConstruction() };

            auto byteCount = RenderCore::ByteCount(transaction->_desc);     // needs to match CompleteWaitForDescFuture in order to reset _currentQueuedBytes correctly
            metricsUnderConstruction._bytesUploadTotal += byteCount;
            metricsUnderConstruction._bytesUploaded[dataType] += byteCount;
            metricsUnderConstruction._countUploaded[dataType] += 1;
            _currentQueuedBytes[dataType] -= byteCount;
            ++metricsUnderConstruction._contextOperations;
            transaction->_promise.set_value(transaction->_finalResource);
        } catch (...) {
            transaction->_promise.set_exception(std::current_exception());
            _currentQueuedBytes[dataType] -= RenderCore::ByteCount(transaction->_desc);
        }

        SystemReleaseTransaction(transaction, context);
        return true;
    }

    bool        AssemblyLine::DrainPriorityQueueSet(QueueSet& queueSet, unsigned stepMask, ThreadContext& context)
    {
        bool didSomething = false;
        CommandListBudget budgetUnderConstruction(true);

            /////////////// ~~~~ /////////////// ~~~~ ///////////////
        for (;;) {
            bool continueLooping = false;
            if (stepMask & Step_PrepareStaging) {
                PrepareStagingStep* step = nullptr;
                if (queueSet._prepareStagingSteps.try_front(step)) {
                    if (Process(*step, context, budgetUnderConstruction)) {
                        didSomething = true;
                    } else {
                        _queueSet_Main._prepareStagingSteps.push_overflow(std::move(*step));
                    }
                    continueLooping = true;
                    queueSet._prepareStagingSteps.pop();
                }
            }

            if (stepMask & Step_TransferStagingToFinal) {
                TransferStagingToFinalStep* step = 0;
                if (queueSet._transferStagingToFinalSteps.try_front(step)) {
                    if (Process(*step, context, budgetUnderConstruction)) {
                        didSomething = true;
                    } else {
                        _queueSet_Main._transferStagingToFinalSteps.push_overflow(std::move(*step));
                    }
                    continueLooping = true;
                    queueSet._transferStagingToFinalSteps.pop();
                }
            }
            if (!continueLooping) break;
        }

            /////////////// ~~~~ /////////////// ~~~~ ///////////////
        if (stepMask & Step_CreateFromDataPacket) {
            CreateFromDataPacketStep* step = 0;
            while (queueSet._createFromDataPacketSteps.try_front(step)) {
                if (Process(*step, context, budgetUnderConstruction)) {
                    didSomething = true;
                } else {
                    _queueSet_Main._createFromDataPacketSteps.push_overflow(std::move(*step));
                }
                queueSet._createFromDataPacketSteps.pop();
            }
        }

        return didSomething;
    }

    bool AssemblyLine::ProcessQueueSet(QueueSet& queueSet, unsigned stepMask, ThreadContext& context, const CommandListBudget& budgetUnderConstruction)
    {
        bool didSomething = false;
        bool prepareStagingBlocked = false;
        bool transferStagingBlocked = false;

            /////////////// ~~~~ /////////////// ~~~~ ///////////////
        for (;;) {
            // Continue looping until both prepare staging & transfer staging have nothing to do
            // try to alternate prepare staging then transfer staging to final. But if one queue
            // gets blocked (eg, can't allocate staging space), then stop checking it
            bool continueLooping = false;
            PrepareStagingStep* prepareStaging = nullptr;
            if ((stepMask & Step_PrepareStaging) && !prepareStagingBlocked && queueSet._prepareStagingSteps.try_front(prepareStaging)) {
                if (Process(*prepareStaging, context, budgetUnderConstruction)) {
                    didSomething = continueLooping = true;
                    queueSet._prepareStagingSteps.pop();
                } else
                    prepareStagingBlocked = true;
            }

            TransferStagingToFinalStep* transferStaging = nullptr;
            if ((stepMask & Step_TransferStagingToFinal) && !transferStagingBlocked && queueSet._transferStagingToFinalSteps.try_front(transferStaging)) {
                if (Process(*transferStaging, context, budgetUnderConstruction)) {
                    didSomething = continueLooping = true;
                    queueSet._transferStagingToFinalSteps.pop();
                } else
                    transferStagingBlocked = true;
            }
            if (!continueLooping) break;
        }

            /////////////// ~~~~ /////////////// ~~~~ ///////////////
        if (stepMask & Step_CreateFromDataPacket) {
            CreateFromDataPacketStep* step = 0;
            while (queueSet._createFromDataPacketSteps.try_front(step)) {
                if (Process(*step, context, budgetUnderConstruction)) {
                    didSomething = true;
                    queueSet._createFromDataPacketSteps.pop();
                } else
                    break;
            }
        }

        return didSomething;
    }

    void AssemblyLine::Process(unsigned stepMask, ThreadContext& context, LockFreeFixedSizeQueue<unsigned, 4>& pendingFramePriorityCommandLists)
    {
        const bool          isLoading = false;
        CommandListMetrics& metricsUnderConstruction = context.GetMetricsUnderConstruction();
        CommandListBudget   budgetUnderConstruction(isLoading);

        bool atLeastOneRealAction = false;

            /////////////// ~~~~ /////////////// ~~~~ ///////////////
        IManager::EventListID publishableEventList = TickResourceSource(stepMask, context, isLoading);

        {
            std::function<void(AssemblyLine&, ThreadContext&)>* fn;
            while (_queuedFunctions.try_front(fn)) {
                fn->operator()(*this, context);
                _queuedFunctions.pop();
            }
        }

        bool framePriorityResolve = false;
        bool popFromFramePriority = false;
        unsigned *qs = NULL;

        if (pendingFramePriorityCommandLists.try_front(qs)) {

                //      --~<   Drain all frame priority steps   >~--      //
            framePriorityResolve = DrainPriorityQueueSet(_queueSet_FramePriority[*qs], stepMask, context);
            atLeastOneRealAction |= framePriorityResolve;
            popFromFramePriority = true;

        }

        if (!framePriorityResolve) {

                //
                //      Process the queue set, but do everything in the "frame priority" queue set that we're writing 
                //      to first. This may sometimes do things out of order, but it means the higher priority
                //      things will complete first
                //

            atLeastOneRealAction |= ProcessQueueSet(_queueSet_FramePriority[_framePriority_WritingQueueSet], stepMask, context, budgetUnderConstruction);
            atLeastOneRealAction |= ProcessQueueSet(_queueSet_Main, stepMask, context, budgetUnderConstruction);

        }

        CommandListID commandListIdCommitted = ~unsigned(0x0);

            /////////////// ~~~~ /////////////// ~~~~ ///////////////
        const bool somethingToResolve = 
                (metricsUnderConstruction._contextOperations!=0)
            ||  _batchPreparation_Main._batchedAllocationSize
            || !context.GetDeferredOperationsUnderConstruction().IsEmpty()
            ||  publishableEventList > context.EventList_GetPublishedID();
        
        // The commit count is a scheduling scheme
        //    -- we will generally "resolve" a command list and queue it for submission
        //      once per call to Manager::Update(). The exception is when there are frame
        //      priority requests
        const unsigned commitCountCurrent = context.CommitCount_Current();
        const bool normalPriorityResolve = commitCountCurrent > context.CommitCount_LastResolve();
        if ((framePriorityResolve||normalPriorityResolve) && somethingToResolve) {
            commandListIdCommitted = context.CommandList_GetUnderConstruction();
            context.CommitCount_LastResolve() = commitCountCurrent;

            ResolveBatchOperation(_batchPreparation_Main, context, stepMask);
            _batchPreparation_Main = BatchPreparation();
            metricsUnderConstruction._assemblyLineMetrics = CalculateMetrics(context);

            context.ResolveCommandList();
            context.EventList_Publish(publishableEventList);

            atLeastOneRealAction = true;
        }

        if (popFromFramePriority) {
            pendingFramePriorityCommandLists.pop();
            assert(!_batchPreparation_Main._batchedAllocationSize);
        }
    }

    PoolSystemMetrics   AssemblyLine::CalculatePoolMetrics() const
    {
        return {};
    }

    AssemblyLineMetrics AssemblyLine::CalculateMetrics(ThreadContext& context)
    {
        AssemblyLineMetrics result;
        result._queuedPrepareStaging            = (unsigned)_queueSet_Main._prepareStagingSteps.size();
        result._queuedTransferStagingToFinal    = (unsigned)_queueSet_Main._transferStagingToFinalSteps.size();
        result._queuedCreateFromDataPacket      = (unsigned)_queueSet_Main._createFromDataPacketSteps.size();
        for (unsigned c=0; c<dimof(_queueSet_FramePriority); ++c) {
            result._queuedPrepareStaging            += (unsigned)_queueSet_FramePriority[c]._prepareStagingSteps.size();
            result._queuedTransferStagingToFinal    += (unsigned)_queueSet_FramePriority[c]._transferStagingToFinalSteps.size();
            result._queuedCreateFromDataPacket      += (unsigned)_queueSet_FramePriority[c]._createFromDataPacketSteps.size();
        }
        _peakPrepareStaging = result._peakPrepareStaging = std::max(_peakPrepareStaging, result._queuedPrepareStaging);
        _peakTransferStagingToFinal = result._peakTransferStagingToFinal = std::max(_peakTransferStagingToFinal, result._queuedTransferStagingToFinal);
        _peakCreateFromDataPacket = result._peakCreateFromDataPacket = std::max(_peakCreateFromDataPacket, result._queuedCreateFromDataPacket);
        std::copy(_currentQueuedBytes, &_currentQueuedBytes[(unsigned)UploadDataType::Max], result._queuedBytes);

        result._transactionCount                 = _allocatedTransactionCount;
        result._temporaryTransactionsAllocated   = (unsigned)_transactions.size();
        result._stagingPageMetrics = context.GetStagingPage().GetQuickMetrics();
        return result;
    }

    ResourceLocator     AssemblyLine::GetResource(TransactionID id)
    {
        ScopedLock(_transactionsRepositionLock);
        Transaction* transaction = GetTransaction(id);
        return transaction ? transaction->_finalResource : ResourceLocator{};
    }

    AssemblyLine::QueueSet& AssemblyLine::GetQueueSet(TransactionOptions::BitField transactionOptions)
    {
        if (transactionOptions & TransactionOptions::FramePriority) {
            return _queueSet_FramePriority[_framePriority_WritingQueueSet];    // not 100% thread safe
        } else {
            return _queueSet_Main;
        }
    }

    void AssemblyLine::PushStep(QueueSet& queueSet, Transaction& transaction, PrepareStagingStep&& step)
    {
        ++transaction._referenceCount;
        queueSet._prepareStagingSteps.push_overflow(std::move(step));
        _wakeupEvent.Increment();
    }

    void AssemblyLine::PushStep(QueueSet& queueSet, Transaction& transaction, TransferStagingToFinalStep&& step)
    {
        ++transaction._referenceCount;
        queueSet._transferStagingToFinalSteps.push_overflow(std::move(step));
        _wakeupEvent.Increment();
    }

    void AssemblyLine::PushStep(QueueSet& queueSet, Transaction& transaction, CreateFromDataPacketStep&& step)
    {
        ++transaction._referenceCount;
        queueSet._createFromDataPacketSteps.push_overflow(std::move(step));
        _wakeupEvent.Increment();
    }

    unsigned AssemblyLine::FlipWritingQueueSet()
    {
            //      This works best if we're only accessing _currentFramePriorityQueueSet from a single
            //      thread. Eg; we should schedule operations for frame priority transactions from the 
            //      main thread, and set the barrier at the end of the main thread;
        unsigned oldWritingQueueSet = _framePriority_WritingQueueSet;
        _framePriority_WritingQueueSet = (_framePriority_WritingQueueSet+1)%dimof(_queueSet_FramePriority);
        return oldWritingQueueSet;
    }

        ///////////////////   M A N A G E R   ///////////////////

    class Manager : public IManager
    {
    public:
        TransactionMarker       Transaction_Begin(const ResourceDesc& desc, const std::shared_ptr<IDataPacket>& data, TransactionOptions::BitField flags) override;
        TransactionMarker       Transaction_Begin(ResourceLocator destinationResource, const std::shared_ptr<IDataPacket>& data, TransactionOptions::BitField flags) override;
        TransactionMarker       Transaction_Begin(const std::shared_ptr<IAsyncDataSource>& data, BindFlag::BitField bindFlags, TransactionOptions::BitField flags) override;
        TransactionMarker       Transaction_Begin(ResourceLocator destinationResource, const std::shared_ptr<IAsyncDataSource>& data, TransactionOptions::BitField flags) override;
        void                    Transaction_Release(TransactionID id) override;

        ResourceLocator         Transaction_Immediate(
                                    RenderCore::IThreadContext& threadContext,
                                    const ResourceDesc& desc, IDataPacket& data) override;
        
        ResourceLocator         GetResource(TransactionID id);
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
        unsigned _frameId = 0;

        uint32_t DoBackgroundThread();

        ThreadContext* MainContext();
        const ThreadContext* MainContext() const;
    };

    TransactionMarker           Manager::Transaction_Begin(const ResourceDesc& desc, const std::shared_ptr<IDataPacket>& data, TransactionOptions::BitField flags)
    {
        return _assemblyLine->Transaction_Begin(desc, data, flags);
    }

    TransactionMarker           Manager::Transaction_Begin(ResourceLocator destinationResource, const std::shared_ptr<IDataPacket>& data, TransactionOptions::BitField flags)
    {
        return _assemblyLine->Transaction_Begin(destinationResource, data, flags);
    }

    TransactionMarker           Manager::Transaction_Begin(const std::shared_ptr<IAsyncDataSource>& data, BindFlag::BitField bindFlags, TransactionOptions::BitField flags)
    {
        return _assemblyLine->Transaction_Begin(data, bindFlags, flags);
    }

    TransactionMarker           Manager::Transaction_Begin(ResourceLocator destinationResource, const std::shared_ptr<IAsyncDataSource>& data, TransactionOptions::BitField flags)
    {
        return _assemblyLine->Transaction_Begin(std::move(destinationResource), data, flags);
    }

    ResourceLocator         Manager::GetResource(TransactionID id)
    {
        return _assemblyLine->GetResource(id);
    }

        /////////////////////////////////////////////

    void                    Manager::Transaction_Release(TransactionID id)
    {
        _assemblyLine->Transaction_Release(id);
    }

    ResourceLocator         Manager::Transaction_Immediate(
        RenderCore::IThreadContext& threadContext,
        const ResourceDesc& desc, IDataPacket& data)
    {
        return _assemblyLine->Transaction_Immediate(threadContext, desc, data);
    }

    inline ThreadContext*          Manager::MainContext() 
    { 
        return _backgroundStepMask ? _backgroundContext.get() : _foregroundContext.get(); 
    }

    inline const ThreadContext*          Manager::MainContext() const
    { 
        return _backgroundStepMask ? _backgroundContext.get() : _foregroundContext.get(); 
    }

    bool                    Manager::IsComplete(CommandListID id)
    {
        return id <= MainContext()->CommandList_GetCommittedToImmediate();
    }

    void                    Manager::StallUntilCompletion(RenderCore::IThreadContext& immediateContext, CommandListID id)
    {
        if (!id || id == CommandListID_Invalid) return;
        while (!IsComplete(id)) {
            Update(immediateContext);
            std::this_thread::sleep_for(std::chrono::nanoseconds(500*1000));
        }
    }

    CommandListMetrics      Manager::PopMetrics()
    {
        CommandListMetrics result = _backgroundContext->PopMetrics();
        if (result._commitTime != 0x0) {
            return result;
        }
        return _foregroundContext->PopMetrics();
    }

    PoolSystemMetrics       Manager::CalculatePoolMetrics() const
    {
        return _assemblyLine->CalculatePoolMetrics();
    }

    Manager::EventListID    Manager::EventList_GetLatestID()
    {
        if (_backgroundStepMask&AssemblyLine::Step_BatchedDefrag) {
            return _backgroundContext->EventList_GetPublishedID();
        }
        return _foregroundContext->EventList_GetPublishedID();
    }

    void                    Manager::EventList_Get(EventListID id, Event_ResourceReposition*& begin, Event_ResourceReposition*& end)
    {
        if (_backgroundStepMask&AssemblyLine::Step_BatchedDefrag) {
            return _backgroundContext->EventList_Get(id, begin, end);
        }
        return _foregroundContext->EventList_Get(id, begin, end);
    }

    void                    Manager::EventList_Release(EventListID id)
    {
        if (_backgroundStepMask&AssemblyLine::Step_BatchedDefrag) {
            return _backgroundContext->EventList_Release(id);
        }
        return _foregroundContext->EventList_Release(id);
    }

    void                    Manager::Update(RenderCore::IThreadContext& immediateContext)
    {
        if (_foregroundStepMask & ~unsigned(AssemblyLine::Step_BatchingUpload)) {
            _assemblyLine->Process(_foregroundStepMask, *_foregroundContext.get(), _pendingFramePriority_CommandLists);
        }
            //  Commit both the foreground and background contexts here
        ++_frameId;
        _foregroundContext->CommitToImmediate(immediateContext, _frameId);
        _backgroundContext->CommitToImmediate(immediateContext, _frameId, &_pendingFramePriority_CommandLists);
        
            // Assembly line uses the number of times we've run CommitToImmediate() for some
            // internal scheduling -- so we need to wake it up now, because it may do something
        _assemblyLine->TriggerWakeupEvent();

        PlatformInterface::Resource_RecalculateVideoMemoryHeadroom();
    }

    void Manager::FramePriority_Barrier()
    {
        unsigned oldQueueSetId = _assemblyLine->FlipWritingQueueSet();
        if (_backgroundStepMask) {
            while (!_pendingFramePriority_CommandLists.push(oldQueueSetId)) {
                _assemblyLine->TriggerWakeupEvent();
                Threading::Sleep(0); 
            }
            _assemblyLine->TriggerWakeupEvent();
        }
    }

    uint32_t Manager::DoBackgroundThread()
    {
        while (!_shutdownBackgroundThread && _backgroundStepMask) {
            _assemblyLine->Process(_backgroundStepMask, *_backgroundContext, _pendingFramePriority_CommandLists);
            if (!_shutdownBackgroundThread)
                _assemblyLine->Wait(_backgroundStepMask, *_backgroundContext);
        }
        return 0;
    }

    Manager::Manager(RenderCore::IDevice& renderDevice) : _assemblyLine(std::make_shared<AssemblyLine>(renderDevice))
    {
        _shutdownBackgroundThread = false;

        bool multithreadingOk = true;
        bool doBatchingUploadInForeground = !PlatformInterface::CanDoNooverwriteMapInBackground;

        // multithreadingOk = false;

        const auto nsightMode = ConsoleRig::CrossModule::GetInstance()._services.CallDefault(Hash64("nsight"), false);
        if (nsightMode)
            multithreadingOk = false;

        auto immediateDeviceContext = renderDevice.GetImmediateContext();
        decltype(immediateDeviceContext) backgroundDeviceContext;

        if (multithreadingOk) {
            backgroundDeviceContext = renderDevice.CreateDeferredContext();

                //
                //      When using an older feature level, we can fail while
                //      creating a deferred context. In these cases, we have
                //      to drop back to single threaded mode.
                //
            if (!backgroundDeviceContext) {
                backgroundDeviceContext = immediateDeviceContext;
            }
        } else {
            backgroundDeviceContext = immediateDeviceContext;
        }

        multithreadingOk = !backgroundDeviceContext->IsImmediate() && (backgroundDeviceContext != immediateDeviceContext);
        _backgroundContext   = std::make_unique<ThreadContext>(backgroundDeviceContext);
        _foregroundContext   = std::make_unique<ThreadContext>(std::move(immediateDeviceContext));

            //  todo --     if we don't have driver support for concurrent creates, we should try to do this
            //              in the main render thread. Also, if we've created the device with the single threaded
            //              parameter, we should do the same.

        if (multithreadingOk) {
            _foregroundStepMask = doBatchingUploadInForeground?AssemblyLine::Step_BatchingUpload:0;        // (do this with the immediate context (main thread) in order to allow writing directly to video memory
            _backgroundStepMask = 
                    AssemblyLine::Step_PrepareStaging
                |   AssemblyLine::Step_TransferStagingToFinal
                |   AssemblyLine::Step_CreateFromDataPacket
                |   AssemblyLine::Step_BatchedDefrag
                |   ((!doBatchingUploadInForeground)?AssemblyLine::Step_BatchingUpload:0)
                ;
        } else {
            _foregroundStepMask = 
                    AssemblyLine::Step_PrepareStaging
                |   AssemblyLine::Step_TransferStagingToFinal
                |   AssemblyLine::Step_CreateFromDataPacket
                |   AssemblyLine::Step_BatchingUpload
                |   AssemblyLine::Step_BatchedDefrag
                ;
            _backgroundStepMask = 0;
        }
        if (_backgroundStepMask) {
            _backgroundThread = std::make_unique<std::thread>([this](){ return DoBackgroundThread(); });
        }
    }

    Manager::~Manager()
    {
        _shutdownBackgroundThread = true;       // this will cause the background thread to terminate at it's next opportunity
        _assemblyLine->TriggerWakeupEvent();
        if (_backgroundThread) {
            _backgroundThread->join();
        }
    }

    bool TransactionMarker::IsValid() const
    {
        return _transactionID != TransactionID_Invalid && _future.valid();
    }

    TransactionMarker::TransactionMarker(std::future<ResourceLocator>&& future, TransactionID transactionID, AssemblyLine& assemblyLine)
    : _future(std::move(future))
    , _transactionID(transactionID)
    , _assemblyLine(&assemblyLine)
    {}

    TransactionMarker::TransactionMarker() = default;
    TransactionMarker::~TransactionMarker()
    {
        if (_assemblyLine)
            _assemblyLine->Transaction_Release(_transactionID);
    }
    TransactionMarker::TransactionMarker(TransactionMarker&& moveFrom) never_throws
    : _future(std::move(moveFrom._future))
    , _transactionID(std::move(moveFrom._transactionID))
    , _assemblyLine(std::move(moveFrom._assemblyLine))
    {
        moveFrom._transactionID = TransactionID_Invalid;
        moveFrom._assemblyLine = nullptr;
    }
    TransactionMarker& TransactionMarker::operator=(TransactionMarker&& moveFrom) never_throws
    {
        if (&moveFrom == this) return *this;
        if (_assemblyLine)
            _assemblyLine->Transaction_Release(_transactionID);
        _future = std::move(moveFrom._future);
        _transactionID = std::move(moveFrom._transactionID);
        _assemblyLine = std::move(moveFrom._assemblyLine);
        moveFrom._transactionID = TransactionID_Invalid;
        moveFrom._assemblyLine = nullptr;
        return *this;
    }

    std::unique_ptr<IManager> CreateManager(RenderCore::IDevice& renderDevice)
    {
        return std::make_unique<Manager>(renderDevice);
    }

    IManager::~IManager() {}
}


