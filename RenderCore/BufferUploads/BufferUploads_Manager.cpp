// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IBufferUploads.h"
#include "ResourceUploadHelper.h"
#include "Metrics.h"
#include "../IDevice.h"
#include "../ResourceUtils.h"
#include "../ResourceDesc.h"
#include "../Metal/Resource.h"
#include "../../OSServices/Log.h"
#include "../../OSServices/TimeUtils.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../Utility/Threading/ThreadingUtils.h"
#include "../../Utility/Threading/LockFree.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/BitUtils.h"
#include "../../Utility/HeapUtils.h"
#include "../../Utility/FunctionUtils.h"
#include <assert.h>
#include <utility>
#include <algorithm>
#include <chrono>
#include <functional>
#include "thousandeyes/futures/then.h"

// #define BU_SEPARATELY_THREADED_CONTINUATIONS 1
#if defined(BU_SEPARATELY_THREADED_CONTINUATIONS)
    #include "../ConsoleRig/GlobalServices.h"
    #include "../Assets/ContinuationExecutor.h"
#endif

#pragma warning(disable:4127)       // conditional expression is constant
#pragma warning(disable:4018)       // signed/unsigned mismatch

using namespace std::chrono_literals;

namespace RenderCore { namespace BufferUploads
{
                    /////////////////////////////////////////////////
                ///////////////////   M A N A G E R   ///////////////////
                    /////////////////////////////////////////////////

    static UploadDataType AsUploadDataType(const ResourceDesc& desc, BindFlag::BitField extraBindFlags)
    {
        switch (desc._type) {
        case ResourceDesc::Type::LinearBuffer:     return ((desc._bindFlags|extraBindFlags)&(BindFlag::VertexBuffer|BindFlag::IndexBuffer))?(UploadDataType::GeometryBuffer):(UploadDataType::UniformBuffer);
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
        std::atomic<unsigned> _semaphoreCount;

        void Increment()
        {
            std::unique_lock<std::mutex> ul(_l);
            ++_semaphoreCount;
            _cv.notify_one();
        }
        void Wait()
        {
            auto exchange = _semaphoreCount.exchange(0);
            if (!exchange) {
                std::unique_lock<std::mutex> ul(_l);
                _cv.wait(ul);
                _semaphoreCount.store(0);
            }
        }
        bool Peek()
        {
            return _semaphoreCount.load() != 0;
        }
    };
    
    class AssemblyLine : public std::enable_shared_from_this<AssemblyLine>, public thousandeyes::futures::Executor
    {
    public:
        enum 
        {
            Step_PrepareStaging         = (1<<0),
            Step_TransferStagingToFinal = (1<<1),
            Step_CreateFromDataPacket   = (1<<2),
            Step_BatchedDefrag          = (1<<4),
            Step_BackgroundMisc         = (1<<5)
        };
        
        TransactionMarker       Begin(const ResourceDesc& desc, std::shared_ptr<IDataPacket> data, std::shared_ptr<IResourcePool> pool, TransactionOptions::BitField flags);
        TransactionMarker       Begin(ResourceLocator destinationResource, std::shared_ptr<IDataPacket> data, TransactionOptions::BitField flags);
        TransactionMarker       Begin(std::shared_ptr<IAsyncDataSource> data, std::shared_ptr<IResourcePool> pool, BindFlag::BitField bindFlags, TransactionOptions::BitField flags);
        TransactionMarker       Begin(ResourceLocator destinationResource, std::shared_ptr<IAsyncDataSource> data, TransactionOptions::BitField flags);
        std::future<CommandListID>   Begin (ResourceLocator destinationResource, ResourceLocator sourceResource, IteratorRange<const Utility::RepositionStep*> repositionOperations);
        void            Cancel(IteratorRange<const TransactionID*>);
        void            OnCompletion(IteratorRange<const TransactionID*>, std::function<void()>&& fn);

        ResourceLocator         ImmediateTransaction(
                                    IThreadContext& threadContext,
                                    const ResourceDesc& desc, IDataPacket& data);

        void                Process(unsigned stepMask, PlatformInterface::UploadsThreadContext& context, LockFreeFixedSizeQueue<unsigned, 4>& pendingFramePriorityCommandLists);

        void                Resource_Release(ResourceLocator& locator);
        void                Resource_AddRef(const ResourceLocator& locator);
        void                Resource_AddRef_IndexBuffer(const ResourceLocator& locator);

        AssemblyLineMetrics CalculateMetrics(PlatformInterface::UploadsThreadContext& context);
        void                Wait(unsigned stepMask, PlatformInterface::UploadsThreadContext& context);
        void                TriggerWakeupEvent();

        unsigned            FlipWritingQueueSet();
        unsigned            BindOnBackgroundFrame(std::function<void()>&& fn);
        void                UnbindOnBackgroundFrame(unsigned marker);
        void                BindBackgroundThread();

        virtual void watch(std::unique_ptr<thousandeyes::futures::Waitable> w) override;
        virtual void stop() override;
        
        AssemblyLine(IDevice& device);
        ~AssemblyLine();

    protected:
        struct OnCompletionAttachment;
        struct Transaction
        {
            uint32_t _idTopPart;
            std::atomic<unsigned> _referenceCount;
            ResourceLocator _finalResource;
            ResourceDesc _desc;
            TimeMarker _requestTime;
            std::promise<ResourceLocator> _promise;
            std::future<void> _waitingFuture;
            bool _promisePending = false;

            std::atomic<bool> _cancelledByClient;
            std::atomic<bool> _statusLock;
            TransactionOptions::BitField _creationOptions;
            unsigned _heapIndex;

            std::shared_ptr<OnCompletionAttachment> _completionAttachment;

            Transaction(unsigned idTopPart, unsigned heapIndex);
            Transaction();
            Transaction(Transaction&& moveFrom) never_throws;
            Transaction& operator=(Transaction&& moveFrom) never_throws;
            Transaction& operator=(const Transaction& cloneFrom) = delete;
        };

        struct TransactionRefHolder
        {
            Transaction* _transaction = nullptr;
            AssemblyLine* _assemblyLine = nullptr;
            TransactionID GetID() const;
            void SuccessfulRetirement();
            TransactionRefHolder() = default;
            TransactionRefHolder(Transaction& transaction, AssemblyLine& assemblyLine);
            TransactionRefHolder(TransactionRefHolder&& moveFrom);
            TransactionRefHolder(const TransactionRefHolder& copyFrom);
            TransactionRefHolder& operator=(TransactionRefHolder&& moveFrom);
            TransactionRefHolder& operator=(const TransactionRefHolder& copyFrom);
            ~TransactionRefHolder();
        };

        std::deque<Transaction>     _transactions;
        SimpleSpanningHeap          _transactionsHeap;

        Threading::Mutex        _transactionsLock;
        std::atomic<unsigned>   _allocatedTransactionCount;

        IDevice*    _device;

        TransactionRefHolder    GetTransaction(TransactionID id);
        TransactionRefHolder    AllocateTransaction(TransactionOptions::BitField flags);
        void                    ApplyRepositions(const ResourceLocator& dst, IResource& src, IteratorRange<const RepositionStep*> steps);

        std::atomic<int>        _currentQueuedBytes[(unsigned)UploadDataType::Max];
        unsigned                _nextTransactionIdTopPart;
        unsigned                _peakPrepareStaging, _peakTransferStagingToFinal, _peakCreateFromDataPacket;
        int64_t                 _waitTime;

        Threading::Mutex _pendingRetirementsLock;
        std::vector<AssemblyLineRetirement> _pendingRetirements;

        struct PrepareStagingStep
        {
            TransactionRefHolder _transactionRef;
            ResourceDesc _desc;
            std::shared_ptr<IAsyncDataSource> _packet;
            std::shared_ptr<IResourcePool> _pool;
            BindFlag::BitField _bindFlags = 0;
        };

        struct TransferStagingToFinalStep
        {
            TransactionRefHolder _transactionRef;
            std::shared_ptr<IResourcePool> _pool;
            ResourceDesc _finalResourceDesc;
            PlatformInterface::StagingPage::Allocation _stagingResource;
            std::shared_ptr<IResource> _oversizeResource;
        };

        struct CreateFromDataPacketStep
        {
            TransactionRefHolder _transactionRef;
            std::shared_ptr<IResourcePool> _pool;
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

        LockFreeFixedSizeQueue<std::function<void(AssemblyLine&, PlatformInterface::UploadsThreadContext&)>, 256> _queuedFunctions;
        SimpleWakeupEvent _wakeupEvent;

        Signal<> _onBackgroundFrame;
        Threading::Mutex _onBackgroundFrameLock;
        unsigned _commitCountLastOnBackgroundFrame = 0;

        #if !defined(BU_SEPARATELY_THREADED_CONTINUATIONS)
            std::vector<std::unique_ptr<thousandeyes::futures::Waitable>> _activeFutureWaitables;
            unsigned _futureWaitablesIterator = 0;
            std::thread::id _futureWaitablesThread;
            Threading::Mutex _stagingFutureWaitablesLock;
            std::vector<std::unique_ptr<thousandeyes::futures::Waitable>> _stagingFutureWaitables;
        #else
            std::shared_ptr<thousandeyes::futures::Executor> _continuationExecutor;
        #endif
        void StallWhileCheckingFutures();

        class CommandListBudget
        {
        public:
            unsigned _limit_BytesUploaded, _limit_Operations;
            CommandListBudget(bool isLoading);
        };

        struct OnCompletionAttachment
        {
            std::vector<TransactionID> _transactions;
            std::function<void()> _fn;
        };

        void    SystemReleaseTransaction(Transaction* transaction, bool abort = false);

        bool    Process(CreateFromDataPacketStep& resourceCreateStep, PlatformInterface::UploadsThreadContext& context, const CommandListBudget& budgetUnderConstruction);
        bool    Process(PrepareStagingStep& prepareStagingStep, PlatformInterface::UploadsThreadContext& context, const CommandListBudget& budgetUnderConstruction);
        bool    Process(TransferStagingToFinalStep& transferStagingToFinalStep, PlatformInterface::UploadsThreadContext& context, const CommandListBudget& budgetUnderConstruction);

        bool    ProcessQueueSet(QueueSet& queueSet, unsigned stepMask, PlatformInterface::UploadsThreadContext& context, const CommandListBudget& budgetUnderConstruction);
        bool    DrainPriorityQueueSet(QueueSet& queueSet, unsigned stepMask, PlatformInterface::UploadsThreadContext& context);

        auto    GetQueueSet(TransactionOptions::BitField transactionOptions) -> QueueSet &;
        void    PushStep(QueueSet&, PrepareStagingStep&& step);
        void    PushStep(QueueSet&, TransferStagingToFinalStep&& step);
        void    PushStep(QueueSet&, CreateFromDataPacketStep&& step);

        void    CompleteWaitForDescFuture(TransactionRefHolder&& ref, std::future<ResourceDesc> descFuture, std::shared_ptr<IAsyncDataSource> data, std::shared_ptr<IResourcePool> pool, BindFlag::BitField);
        void    CompleteWaitForDataFuture(TransactionRefHolder&& ref, std::future<void> prepareFuture, PlatformInterface::StagingPage::Allocation&& stagingAllocation, std::shared_ptr<IResource> oversizeResource, std::shared_ptr<IResourcePool> pool, const ResourceDesc& finalResourceDesc);
        void    UnqueueBytes(UploadDataType type, unsigned bytes);
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

    TransactionMarker AssemblyLine::Begin(
        const ResourceDesc& desc, std::shared_ptr<IDataPacket> data, std::shared_ptr<IResourcePool> pool, TransactionOptions::BitField flags)
    {
        auto ref = AllocateTransaction(flags);
        assert(ref._transaction);
        ref._transaction->_desc = desc;
        if (data) ValidatePacketSize(desc, *data);

            //
            //      Have to increase _currentQueuedBytes before we push in the create step... Otherwise the create 
            //      step can actually happen first, causing _currentQueuedBytes to actually go negative! it actually
            //      happens frequently enough to create blips in the graph.
            //  
        _currentQueuedBytes[(unsigned)AsUploadDataType(desc, desc._bindFlags)] += RenderCore::ByteCount(desc);

        TransactionMarker result { ref._transaction->_promise.get_future(), ref.GetID(), *this };
        ref._transaction->_promisePending = true;
        PushStep(
            GetQueueSet(flags),
            CreateFromDataPacketStep { std::move(ref), std::move(pool), desc, std::move(data) });
        return result;
    }

    TransactionMarker AssemblyLine::Begin(ResourceLocator destinationResource, std::shared_ptr<IDataPacket> data, TransactionOptions::BitField flags)
    {
        auto rangeInDest = destinationResource.GetRangeInContainingResource();
        if (rangeInDest.first != ~size_t(0))
            Throw(std::runtime_error("Attempting to begin IDataPacket upload on partial/internal resource. Only full resources are supported for this variation."));
        
        auto ref = AllocateTransaction(flags);
        assert(ref._transaction);
        auto desc = destinationResource.GetContainingResource()->GetDesc();
        ref._transaction->_desc = desc;
        if (data) ValidatePacketSize(desc, *data);
        _currentQueuedBytes[(unsigned)AsUploadDataType(desc, desc._bindFlags)] += RenderCore::ByteCount(desc);

        TransactionMarker result{ ref._transaction->_promise.get_future(), ref.GetID(), *this };
        ref._transaction->_promisePending = true;
        PushStep(
            GetQueueSet(flags),
            CreateFromDataPacketStep { std::move(ref), nullptr, desc, std::move(data) });
        return result;
    }

    TransactionMarker AssemblyLine::Begin(
        std::shared_ptr<IAsyncDataSource> data, std::shared_ptr<IResourcePool> pool, BindFlag::BitField bindFlags, TransactionOptions::BitField flags)
    {
        auto ref = AllocateTransaction(flags);
        assert(ref._transaction);

        TransactionMarker result { ref._transaction->_promise.get_future(), ref.GetID(), *this };
        ref._transaction->_promisePending = true;

        // Let's optimize the case where the desc is available immediately, since certain
        // usage patterns will allow for that
        auto descFuture = data->GetDesc();
        auto status = descFuture.wait_for(0s);
        if (status == std::future_status::ready) {

            CompleteWaitForDescFuture(std::move(ref), std::move(descFuture), std::move(data), std::move(pool), bindFlags);

        } else {

            auto weakThis = weak_from_this();
            auto* t = ref._transaction;
            assert(!t->_waitingFuture.valid());
            t->_waitingFuture = thousandeyes::futures::then(
                shared_from_this(),
                std::move(descFuture),
                [weakThis, ref=std::move(ref), data=std::move(data), pool=std::move(pool), bindFlags](std::future<ResourceDesc> completedFuture) mutable {
                    auto t = weakThis.lock();
                    if (!t)
                        Throw(std::runtime_error("Assembly line was destroyed before future completed"));

                    t->CompleteWaitForDescFuture(std::move(ref), std::move(completedFuture), std::move(data), std::move(pool), bindFlags);
                });
        }

        return result;
    }

    TransactionMarker AssemblyLine::Begin(ResourceLocator destinationResource, std::shared_ptr<IAsyncDataSource> data, TransactionOptions::BitField flags)
    {
        auto ref = AllocateTransaction(flags);
        assert(ref._transaction);
        ref._transaction->_finalResource = std::move(destinationResource);

        TransactionMarker result { ref._transaction->_promise.get_future(), ref.GetID(), *this };
        ref._transaction->_promisePending = true;

        // Let's optimize the case where the desc is available immediately, since certain
        // usage patterns will allow for that
        auto descFuture = data->GetDesc();
        if (descFuture.wait_for(0s) == std::future_status::ready) {

            CompleteWaitForDescFuture(std::move(ref), std::move(descFuture), data, nullptr, 0);

        } else {

            auto weakThis = weak_from_this();
            auto *t = ref._transaction;
            assert(!t->_waitingFuture.valid());
            t->_waitingFuture = thousandeyes::futures::then(
                shared_from_this(),
                std::move(descFuture),
                [weakThis, ref=std::move(ref), data=std::move(data)](std::future<ResourceDesc> completedFuture) mutable {
                    auto t = weakThis.lock();
                    if (!t)
                        Throw(std::runtime_error("Assembly line was destroyed before future completed"));

                    t->CompleteWaitForDescFuture(std::move(ref), std::move(completedFuture), std::move(data), nullptr, 0);
                });
        }

        return result;
    }

    std::future<CommandListID>   AssemblyLine::Begin (ResourceLocator dst, ResourceLocator src, IteratorRange<const Utility::RepositionStep*> repositionOperations)
    {
        struct Helper
        {
            std::vector<Utility::RepositionStep> _steps;
            ResourceLocator _dst, _src;
            std::promise<CommandListID> _promise;
        };
        auto helper = std::make_shared<Helper>();
        helper->_steps = {repositionOperations.begin(), repositionOperations.end()};
        helper->_dst = std::move(dst);
        helper->_src = std::move(src);

        auto result = helper->_promise.get_future();
        assert(dst.IsWholeResource() && src.IsWholeResource());
        
        _queuedFunctions.push_overflow(
            [helper=std::move(helper)](AssemblyLine& assemblyLine, PlatformInterface::UploadsThreadContext& context) mutable {
                TRY {
                    // Update any transactions that are pointing at one of the moved blocks
                    assemblyLine.ApplyRepositions(helper->_dst.GetContainingResource(), *helper->_src.GetContainingResource(), helper->_steps);
                    // Copy between the resources using the GPU
                    context.GetResourceUploadHelper().DeviceBasedCopy(*helper->_dst.GetContainingResource(), *helper->_src.GetContainingResource(), helper->_steps);
                    helper->_promise.set_value(context.CommandList_GetUnderConstruction());
                    ++context.GetMetricsUnderConstruction()._contextOperations;
                } CATCH (...) {
                    helper->_promise.set_exception(std::current_exception());
                } CATCH_END
            });
        _wakeupEvent.Increment();

        return result;
    }

    void AssemblyLine::SystemReleaseTransaction(Transaction* transaction, bool abort)
    {
        auto newRefCount = --transaction->_referenceCount;
        assert(newRefCount>=0);

        if (newRefCount==0) {
            {
                AssemblyLineRetirement retirement;
                retirement._desc = transaction->_desc;
                retirement._requestTime = transaction->_requestTime;
                retirement._retirementTime = OSServices::GetPerformanceCounter();
                ScopedLock(_pendingRetirementsLock);
                _pendingRetirements.push_back(retirement);
            }
            transaction->_finalResource = {};

            if (transaction->_promisePending) {
                transaction->_promise.set_exception(std::make_exception_ptr(std::runtime_error("Transactions aborted")));
                transaction->_promisePending = false;
            }

            // potentially call the completion attachment if it's now ready
            if (transaction->_completionAttachment) {
                auto i = std::find(transaction->_completionAttachment->_transactions.begin(), transaction->_completionAttachment->_transactions.end(), (uint64_t(transaction->_idTopPart) << 32ull) | transaction->_heapIndex);
                assert(i != transaction->_completionAttachment->_transactions.end());
                transaction->_completionAttachment->_transactions.erase(i);
                if (transaction->_completionAttachment->_transactions.empty())
                    transaction->_completionAttachment->_fn();
                transaction->_completionAttachment = nullptr;
            }

            unsigned heapIndex   = transaction->_heapIndex;

                //
                //      This is a destroy event... actually we don't need to do anything.
                //      it's already considered destroyed because the ref count is 0.
                //      But let's clear out the members, anyway. This will also free the textures (if they need freeing)
                //
            *transaction = Transaction();
            --_allocatedTransactionCount;

            ScopedLock(_transactionsLock);
            _transactionsHeap.Deallocate(heapIndex<<4, 1<<4);
        }
    }

    TransactionID AssemblyLine::TransactionRefHolder::GetID() const
    {
        assert(_transaction && _transaction->_heapIndex != ~0u);
        return uint64_t(_transaction->_heapIndex) | uint64(_transaction->_idTopPart) << 32ull;
    }

    void AssemblyLine::TransactionRefHolder::SuccessfulRetirement()
    {
        if (_transaction) {
            _assemblyLine->SystemReleaseTransaction(_transaction, false);
            _transaction = nullptr; _assemblyLine = nullptr;
        }
    }

    AssemblyLine::TransactionRefHolder::TransactionRefHolder(Transaction& transaction, AssemblyLine& assemblyLine)
    : _transaction(&transaction), _assemblyLine(&assemblyLine)
    {
        if (_transaction)
            ++_transaction->_referenceCount;
    }
    AssemblyLine::TransactionRefHolder::TransactionRefHolder(TransactionRefHolder&& moveFrom)
    {
        _transaction = moveFrom._transaction; moveFrom._transaction = nullptr;
        _assemblyLine = moveFrom._assemblyLine; moveFrom._assemblyLine = nullptr;
    }
    AssemblyLine::TransactionRefHolder::TransactionRefHolder(const TransactionRefHolder& copyFrom)
    {
        _transaction = copyFrom._transaction;
        _assemblyLine = copyFrom._assemblyLine;
        if (_transaction)
            ++_transaction->_referenceCount;
    }
    auto AssemblyLine::TransactionRefHolder::operator=(TransactionRefHolder&& moveFrom) -> TransactionRefHolder&
    {
        if (_transaction)
            _assemblyLine->SystemReleaseTransaction(_transaction, true);
        _transaction = moveFrom._transaction; moveFrom._transaction = nullptr;
        _assemblyLine = moveFrom._assemblyLine; moveFrom._assemblyLine = nullptr;
        return *this;
    }
    auto AssemblyLine::TransactionRefHolder::operator=(const TransactionRefHolder& copyFrom) -> TransactionRefHolder&
    {
        if (_transaction)
            _assemblyLine->SystemReleaseTransaction(_transaction, true);
        _transaction = copyFrom._transaction;
        _assemblyLine = copyFrom._assemblyLine;
        if (_transaction)
            ++_transaction->_referenceCount;
        return *this;
    }
    AssemblyLine::TransactionRefHolder::~TransactionRefHolder()
    {
        if (_transaction)
            _assemblyLine->SystemReleaseTransaction(_transaction, true);
    }

    static std::shared_ptr<IResource> CreateResource(
        IDevice& device,
        const ResourceDesc& desc,
        IDataPacket* initPkt = nullptr)
    {
        if (initPkt) {
            return device.CreateResource(desc, PlatformInterface::AsResourceInitializer(*initPkt));
        } else {
            return device.CreateResource(desc);
        }
    }

    ResourceLocator AssemblyLine::ImmediateTransaction(
        IThreadContext& threadContext,
        const ResourceDesc& descInit, IDataPacket& initialisationData)
    {
        ResourceDesc desc = descInit;

        auto supportInit = 
            (desc._type == ResourceDesc::Type::Texture)
            ? PlatformInterface::SupportsResourceInitialisation_Texture
            : PlatformInterface::SupportsResourceInitialisation_Buffer;

        if (supportInit)
            return CreateResource(*threadContext.GetDevice(), desc, &initialisationData);

        desc._bindFlags |= BindFlag::TransferDst;
        auto finalResourceConstruction = CreateResource(*threadContext.GetDevice(), desc);
        if (!finalResourceConstruction)
            return {};

        PlatformInterface::ResourceUploadHelper helper{threadContext};
        helper.UpdateFinalResourceViaCmdListAttachedStaging(threadContext, finalResourceConstruction, initialisationData);
        return finalResourceConstruction;
    }

    void AssemblyLine::watch(std::unique_ptr<thousandeyes::futures::Waitable> w)
    {
        #if !defined(BU_SEPARATELY_THREADED_CONTINUATIONS)
            if (std::this_thread::get_id() == _futureWaitablesThread) {
                _activeFutureWaitables.push_back(std::move(w));
            } else {
                ScopedLock(_stagingFutureWaitablesLock);
                _stagingFutureWaitables.push_back(std::move(w));
                _wakeupEvent.Increment();
            }
        #else
            _continuationExecutor->watch(std::move(w));
        #endif
    }

    void AssemblyLine::stop()
    {
        assert(0);
    }

    void AssemblyLine::BindBackgroundThread()
    {
        #if !defined(BU_SEPARATELY_THREADED_CONTINUATIONS)
            _futureWaitablesThread = std::this_thread::get_id();
        #endif
    }

    void AssemblyLine::StallWhileCheckingFutures()
    {
        #if !defined(BU_SEPARATELY_THREADED_CONTINUATIONS)
            assert(std::this_thread::get_id() == _futureWaitablesThread);

            {
                ScopedLock(_stagingFutureWaitablesLock);
                _activeFutureWaitables.reserve(_activeFutureWaitables.size() + _stagingFutureWaitables.size());
                for (auto& w:_stagingFutureWaitables) _activeFutureWaitables.emplace_back(std::move(w));
                _stagingFutureWaitables.clear();
            }

            auto timeout = std::chrono::microseconds{500};
            while (!_activeFutureWaitables.empty()) {
                if (_wakeupEvent.Peek())
                    break;      // still have to do _wakeupEvent.Wait() to clear out the signal

                bool readyForDispatch = _activeFutureWaitables[_futureWaitablesIterator]->wait(timeout);
                if (!readyForDispatch) {
                    _futureWaitablesIterator = (_futureWaitablesIterator+1)%_activeFutureWaitables.size();
                    continue;
                }
                auto w = std::move(_activeFutureWaitables[_futureWaitablesIterator]);
                _activeFutureWaitables.erase(_activeFutureWaitables.begin()+_futureWaitablesIterator);
                w->dispatch();
                if (_futureWaitablesIterator >= _activeFutureWaitables.size()) _futureWaitablesIterator = 0;
            }

            _wakeupEvent.Wait();
        #else
            _wakeupEvent.Wait();
        #endif
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
        _promisePending = moveFrom._promisePending;
        _waitingFuture = std::move(moveFrom._waitingFuture);
        _completionAttachment = std::move(moveFrom._completionAttachment);

        _creationOptions = moveFrom._creationOptions;
        _heapIndex = moveFrom._heapIndex;

        moveFrom._idTopPart = 0;
        moveFrom._statusLock = 0;
        moveFrom._referenceCount = 0;
        moveFrom._creationOptions = 0;
        moveFrom._heapIndex = ~unsigned(0x0);
        moveFrom._promisePending = false;
    }

    auto AssemblyLine::Transaction::operator=(Transaction&& moveFrom) never_throws -> Transaction&
    {
        for (;;) {
            bool expected = false;
            if (_statusLock.compare_exchange_strong(expected, true)) break;
            Threading::Pause();
        }
        assert(!_promisePending);

        _idTopPart = moveFrom._idTopPart;
        _finalResource = std::move(moveFrom._finalResource);
        _desc = moveFrom._desc;
        _requestTime = moveFrom._requestTime;
        _promise = std::move(moveFrom._promise);
        _promisePending = moveFrom._promisePending;
        _waitingFuture = std::move(moveFrom._waitingFuture);
        _completionAttachment = std::move(moveFrom._completionAttachment);

        _creationOptions = moveFrom._creationOptions;
        _heapIndex = moveFrom._heapIndex;

        moveFrom._idTopPart = 0;
        moveFrom._statusLock = 0;
        moveFrom._referenceCount = 0;
        moveFrom._creationOptions = 0;
        moveFrom._heapIndex = ~unsigned(0x0);
        moveFrom._promisePending = false;

        auto lockRelease = _statusLock.exchange(false);
        assert(lockRelease==1); (void)lockRelease;

            // note that reference counts are unaffected here!
            // the reference count for "this" and "moveFrom" don't change

        return *this;
    }

    AssemblyLine::AssemblyLine(IDevice& device)
    :   _device(&device)
    ,   _transactionsHeap((2*1024)<<4)
    {
        _nextTransactionIdTopPart = 64;
        _transactions.resize(2*1024);
        _peakPrepareStaging = _peakTransferStagingToFinal =_peakCreateFromDataPacket = 0;
        _allocatedTransactionCount = 0;
        XlZeroMemory(_currentQueuedBytes);
        _framePriority_WritingQueueSet = 0;
        _pendingRetirements.reserve(64);

        #if !defined(BU_SEPARATELY_THREADED_CONTINUATIONS)
            _activeFutureWaitables.reserve(2048);
            _stagingFutureWaitables.reserve(2048);
        #else
            _continuationExecutor = std::make_shared<::Assets::ContinuationExecutor>(
                std::chrono::microseconds(500),
                thousandeyes::futures::detail::InvokerWithNewThread{},
                ::Assets::InvokerToThreadPool{ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool()});
        #endif
    }

    AssemblyLine::~AssemblyLine()
    {
    }

    auto AssemblyLine::AllocateTransaction(TransactionOptions::BitField flags) -> TransactionRefHolder
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
        if (result >= _transactions.size())
            _transactions.resize(result+1);
        auto destinationPosition = _transactions.begin() + ptrdiff_t(result);
        result |= uint64_t(idTopPart)<<32ull;

        *destinationPosition = Transaction{idTopPart, uint32_t(result)};
        destinationPosition->_requestTime = OSServices::GetPerformanceCounter();
        destinationPosition->_creationOptions = flags;
        ++_allocatedTransactionCount;
        return TransactionRefHolder{*destinationPosition, *this};       // will increment refcount before we unlock _transactionsLock
    }

    auto AssemblyLine::GetTransaction(TransactionID id) -> TransactionRefHolder
    {
        unsigned index = unsigned(id);
        unsigned key = unsigned(id>>32ull);
        AssemblyLine::Transaction* result = nullptr;
        ScopedLock(_transactionsLock);       // must be locked when using the deque method... if the deque is resized at the same time, operator[] can seem to fail
        if ((index < _transactions.size()) && (key == _transactions[index]._idTopPart))
            result = &_transactions[index];
        if (result) {
            assert(result->_referenceCount.load());     // this is only thread safe if there's some kind of reference on the transaction
            return TransactionRefHolder{*result, *this};
        }
        return {};
    }

    void AssemblyLine::Cancel(IteratorRange<const TransactionID*> ids)
    {
        ScopedLock(_transactionsLock);
        for (auto i:ids) {
            assert(i!=TransactionID_Invalid);
            auto idx = uint32_t(i);
            assert(idx < _transactions.size());
            if (_transactions[idx]._idTopPart == i>>32ull)
                _transactions[idx]._cancelledByClient = true;
        }
    }

    void AssemblyLine::OnCompletion(IteratorRange<const TransactionID*> transactionsInit, std::function<void()>&& fn)
    {
        std::vector<TransactionID> transactions{transactionsInit.begin(), transactionsInit.end()};
        _queuedFunctions.push_overflow(
            [transactions=std::move(transactions), fn=std::move(fn)](auto& assemblyLine, auto&) {
                ScopedLock(assemblyLine._transactionsLock);
                auto attachment = std::make_shared<OnCompletionAttachment>();
                attachment->_transactions.reserve(transactions.size());
                for (auto t:transactions) {
                    assert(t!=TransactionID_Invalid);
                    auto idx = uint32_t(t);
                    assert(idx < assemblyLine._transactions.size());
                    if (assemblyLine._transactions[idx]._idTopPart == t>>32ull) {
                        attachment->_transactions.push_back(t); // not retired yet
                        assert(!assemblyLine._transactions[idx]._completionAttachment);
                        assemblyLine._transactions[idx]._completionAttachment = attachment;
                    }
                }
                if (!attachment->_transactions.empty()) {
                    attachment->_fn = std::move(fn);
                } else {
                    fn();       // everything completed already, can execute right now
                }
            });
    }

    void AssemblyLine::Wait(unsigned stepMask, PlatformInterface::UploadsThreadContext& context)
    {
        int64_t startTime = OSServices::GetPerformanceCounter();
        StallWhileCheckingFutures();

        CommandListMetrics& metricsUnderConstruction = context.GetMetricsUnderConstruction();
        metricsUnderConstruction._waitTime += OSServices::GetPerformanceCounter() - startTime;
        metricsUnderConstruction._wakeCount++;
    }

    void AssemblyLine::TriggerWakeupEvent()
    {
        _wakeupEvent.Increment();
    }

    static std::optional<unsigned> ResolveOffsetValue(unsigned inputOffset, unsigned size, IteratorRange<const RepositionStep*> steps)
    {
        for (const auto& s:steps)
            if (inputOffset >= s._sourceStart && inputOffset < s._sourceEnd) {
                assert((inputOffset+size) <= s._sourceEnd);
                return inputOffset + s._destination - s._sourceStart;
            }
        return {};
    }

    void AssemblyLine::ApplyRepositions(const ResourceLocator& dst, IResource& src, IteratorRange<const RepositionStep*> steps)
    {
            //
            //      We need to prevent GetTransaction from returning a partial result while this is occuring
            //      Since we modify both transaction._finalResource & transaction._resourceOffsetValue, it's
            //      possible that another thread could get the update of one, but not the other. So we have
            //      to lock. It might be ok if we went through and cleared all of the _finalResource values
            //      of the transactions we're going to change first -- but there's still a tiny chance that
            //      that method would fail.
            //
        ScopedLock(_transactionsLock);
        assert(dst.IsWholeResource());

        const size_t temporaryCount = _transactions.size();
        for (auto& s:steps) {
            for (unsigned c=0; c<temporaryCount; ++c) {
                Transaction& transaction = _transactions[c];
                if (transaction._finalResource.GetContainingResource().get() == &src) {
                    auto size = RenderCore::ByteCount(transaction._desc);
                    if (!transaction._finalResource.IsWholeResource())
                        assert((transaction._finalResource.GetRangeInContainingResource().second-transaction._finalResource.GetRangeInContainingResource().first) == size);

                    ResourceLocator oldLocator = std::move(transaction._finalResource);
                    unsigned oldOffset = oldLocator.GetRangeInContainingResource().first;

                    auto newOffsetValue = ResolveOffsetValue(oldOffset, RenderCore::ByteCount(transaction._desc), steps);
                    if (newOffsetValue.has_value())
                        transaction._finalResource = dst.MakeSubLocator(newOffsetValue.value(), size);
                }
            }
        }
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

    void AssemblyLine::UnqueueBytes(UploadDataType type, unsigned bytes)
    {
        auto newValue = _currentQueuedBytes[(unsigned)type] -= bytes;
        assert(newValue >= 0);
    }

    bool AssemblyLine::Process(CreateFromDataPacketStep& resourceCreateStep, PlatformInterface::UploadsThreadContext& context, const CommandListBudget& budgetUnderConstruction)
    {
        auto& metricsUnderConstruction = context.GetMetricsUnderConstruction();
        if ((metricsUnderConstruction._contextOperations+1) >= budgetUnderConstruction._limit_Operations)
            return false;

        auto* transaction = resourceCreateStep._transactionRef._transaction;
        assert(transaction);

        assert(resourceCreateStep._initialisationData);
        auto objectSize = RenderCore::ByteCount(resourceCreateStep._creationDesc);
        auto uploadRequestSize = objectSize;
        auto uploadDataType = (unsigned)AsUploadDataType(resourceCreateStep._creationDesc, resourceCreateStep._creationDesc._bindFlags);

        if (transaction->_cancelledByClient.load()) {
            transaction->_promise.set_exception(std::make_exception_ptr(std::runtime_error("Cancelled before completion")));
            transaction->_promisePending = false;
            UnqueueBytes((UploadDataType)uploadDataType, uploadRequestSize);
            return true;
        }

        if ((metricsUnderConstruction._bytesUploadTotal+uploadRequestSize) > budgetUnderConstruction._limit_BytesUploaded && metricsUnderConstruction._bytesUploadTotal !=0)
            return false;

        TRY {
            ResourceLocator finalConstruction;
            bool deviceConstructionInvoked = false;
            bool didInitialisationDuringCreation = false;
            auto desc = resourceCreateStep._creationDesc;
            if (transaction->_finalResource.IsEmpty()) {
                // No resource provided beforehand -- have to create it now
                
                if (resourceCreateStep._pool && desc._type == ResourceDesc::Type::LinearBuffer) {
                    finalConstruction = resourceCreateStep._pool->Allocate(desc._linearBufferDesc._sizeInBytes, desc._name);
                    if (finalConstruction.IsEmpty())
                        desc = resourceCreateStep._pool->MakeFallbackDesc(desc._linearBufferDesc._sizeInBytes, desc._name);
                }

                if (finalConstruction.IsEmpty()) {
                    auto supportInit = 
                        (desc._type == ResourceDesc::Type::Texture)
                        ? PlatformInterface::SupportsResourceInitialisation_Texture
                        : PlatformInterface::SupportsResourceInitialisation_Buffer;

                    if (resourceCreateStep._initialisationData && supportInit) {
                        finalConstruction = CreateResource(
                            context.GetRenderCoreDevice(),
                            desc, resourceCreateStep._initialisationData.get());
                        didInitialisationDuringCreation = true;
                    } else {
                        auto modifiedDesc = desc;
                        modifiedDesc._bindFlags |= BindFlag::TransferDst;
                        finalConstruction = CreateResource(context.GetRenderCoreDevice(), modifiedDesc);
                    }
                    deviceConstructionInvoked = true;
                }

                if (finalConstruction.IsEmpty())
                    Throw(std::runtime_error("Device resource allocation failed"));
            } else {
                finalConstruction = transaction->_finalResource;
            }

            if (!didInitialisationDuringCreation) {
                assert(finalConstruction.GetContainingResource()->GetDesc()._bindFlags & BindFlag::TransferDst);    // need TransferDst to recieve staging data

                auto& helper = context.GetResourceUploadHelper();
                if (!helper.CanDirectlyMap(*finalConstruction.GetContainingResource())) {

                    auto stagingByteCount = objectSize;
                    auto alignment = helper.CalculateStagingBufferOffsetAlignment(desc);

                    if (stagingByteCount <= context.GetStagingPage().MaxSize()) {
                        auto stagingConstruction = context.GetStagingPage().Allocate(stagingByteCount, alignment);
                        if (!stagingConstruction) {
                            // we will return, so keep the resource until then
                            transaction->_finalResource = finalConstruction;
                            return false;
                        }
                        metricsUnderConstruction._stagingBytesAllocated[uploadDataType] += stagingConstruction.GetAllocationSize();

                        if (desc._type == ResourceDesc::Type::Texture) {
                            helper.WriteViaMap(
                                context.GetStagingPage().GetStagingResource(),
                                stagingConstruction.GetResourceOffset(), stagingConstruction.GetAllocationSize(),
                                desc._textureDesc,
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

                        stagingConstruction.Release(context.GetProducerCmdListSpecificMarker());
                    } else {
                        // oversized allocations will go via a cmd list staging allocation, which has provisions
                        // to create short-lived large staging buffers
                        helper.UpdateFinalResourceViaCmdListAttachedStaging(
                            context.GetRenderCoreThreadContext(), finalConstruction, *resourceCreateStep._initialisationData);
                    }

                } else {

                    // destination is in host-visible memory, we can just write directly to it
                    if (desc._type == ResourceDesc::Type::Texture) {
                        helper.WriteViaMap(
                            *finalConstruction.AsIndependentResource(),
                            [initialisationData{resourceCreateStep._initialisationData.get()}](SubResourceId sr) -> SubResourceInitData
                            {
                                SubResourceInitData result = {};
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
            metricsUnderConstruction._bytesCreated[uploadDataType] += objectSize;
            metricsUnderConstruction._countCreations[uploadDataType] += 1;
            if (deviceConstructionInvoked) {
                ++metricsUnderConstruction._countDeviceCreations[uploadDataType];
                ++metricsUnderConstruction._deviceCreateOperations;
            }

            // Embue the final resource with the completion command list information
            transaction->_finalResource = ResourceLocator { std::move(finalConstruction), context.CommandList_GetUnderConstruction() };
            transaction->_promise.set_value(transaction->_finalResource);
            transaction->_promisePending = false;
            resourceCreateStep._transactionRef.SuccessfulRetirement();
        } CATCH (...) {
            transaction->_promise.set_exception(std::current_exception());
            transaction->_promisePending = false;
        } CATCH_END

        UnqueueBytes((UploadDataType)uploadDataType, uploadRequestSize);
        return true;
    }

    bool AssemblyLine::Process(PrepareStagingStep& prepareStagingStep, PlatformInterface::UploadsThreadContext& context, const CommandListBudget& budgetUnderConstruction)
    {
        auto& metricsUnderConstruction = context.GetMetricsUnderConstruction();
        if ((metricsUnderConstruction._contextOperations+1) >= budgetUnderConstruction._limit_Operations)
            return false;

        // todo -- should we limit this based on the number of items in the WaitForDataFutureStep
        //      stage?

        auto* transaction = prepareStagingStep._transactionRef._transaction;
        assert(transaction);

        if (transaction->_cancelledByClient.load()) {
            transaction->_promise.set_exception(std::make_exception_ptr(std::runtime_error("Cancelled before completion")));
            transaction->_promisePending = false;
            UnqueueBytes((UploadDataType)AsUploadDataType(transaction->_desc, prepareStagingStep._bindFlags), RenderCore::ByteCount(transaction->_desc));
            return true;
        }

        try {
            const auto& desc = prepareStagingStep._desc;
            auto byteCount = RenderCore::ByteCount(desc);
            auto alignment = context.GetResourceUploadHelper().CalculateStagingBufferOffsetAlignment(desc);

            using namespace RenderCore;
            struct Captures
            {
                Metal::ResourceMap _map;
                std::shared_ptr<IResource> _oversizeResource;
                TransactionRefHolder _transactionRef;
                std::shared_ptr<IAsyncDataSource> _pkt;
                PlatformInterface::StagingPage::Allocation _stagingConstruction;
                std::shared_ptr<IResourcePool> _pool;
                ResourceDesc _finalResourceDesc;
                std::weak_ptr<AssemblyLine> _weakThis;

                ~Captures()
                {
                    // If transaction->_waitingFuture (constructe below) is destroyed before calling get(),
                    // we can end up here (there's another uncommon case if an exception is thrown from CompleteWaitForDataFuture, also)
                    // We still have to ensure that _stagingConstruction is destroyed in the assembly line thread, since it's not thread safe
                    if (_stagingConstruction)
                        if (auto l = _weakThis.lock()) {
                            auto helper = std::make_shared<PlatformInterface::StagingPage::Allocation>(std::move(_stagingConstruction));
                            l->_queuedFunctions.push_overflow(
                                [helper=std::move(helper)](auto&, auto&) {
                                    // just holding onto _stagingConstruction to release it in the assembly line thread
                                });
                            l->_wakeupEvent.Increment();
                        }
                }
                Captures() = default;
                Captures(Captures&&) = default;
                Captures& operator=(Captures&&) = default;
            } captures;
            captures._weakThis = weak_from_this();

            std::vector<IAsyncDataSource::SubResource> uploadList;
            if (byteCount < context.GetStagingPage().MaxSize()) {
                auto stagingConstruction = context.GetStagingPage().Allocate(byteCount, alignment);
                if (!stagingConstruction)   // hit our limit right now -- might have to wait until some of the scheduled uploads have completed
                    return false;
                metricsUnderConstruction._stagingBytesAllocated[(unsigned)AsUploadDataType(desc, prepareStagingStep._bindFlags)] += stagingConstruction.GetAllocationSize();

                Metal::ResourceMap map{
                    context.GetRenderCoreDevice(),      // we can also get the device context with *Metal::DeviceContext::Get(*context.GetRenderCoreThreadContext())
                    context.GetStagingPage().GetStagingResource(),
                    Metal::ResourceMap::Mode::WriteDiscardPrevious,
                    stagingConstruction.GetResourceOffset(), stagingConstruction.GetAllocationSize()};
                uploadList = context.GetResourceUploadHelper().CalculateUploadList(map, desc);

                captures._map = std::move(map);
                captures._stagingConstruction = std::move(stagingConstruction);
            } else {
                auto oversizeDesc = CreateDesc(
                    BindFlag::TransferSrc,
                    AllocationRules::PermanentlyMapped | AllocationRules::HostVisibleSequentialWrite | AllocationRules::DedicatedPage,
                    LinearBufferDesc::Create(byteCount), "oversize-staging");
                captures._oversizeResource = context.GetRenderCoreDevice().CreateResource(oversizeDesc);
                Metal::ResourceMap map{context.GetRenderCoreDevice(), *captures._oversizeResource, Metal::ResourceMap::Mode::WriteDiscardPrevious};
                uploadList = context.GetResourceUploadHelper().CalculateUploadList(map, desc);
                captures._map = std::move(map);
            }

            captures._finalResourceDesc = desc;
            captures._finalResourceDesc._bindFlags |= prepareStagingStep._bindFlags;
            captures._finalResourceDesc._bindFlags |= BindFlag::TransferDst;         // since we're using a staging buffer to prepare, we must allow for transfers

            auto future = prepareStagingStep._packet->PrepareData(uploadList);
            captures._transactionRef = std::move(prepareStagingStep._transactionRef);
            captures._pkt = std::move(prepareStagingStep._packet);        // need to retain pkt until PrepareData completes
            captures._pool = std::move(prepareStagingStep._pool);

            assert(!transaction->_waitingFuture.valid());
            transaction->_waitingFuture = thousandeyes::futures::then(
                shared_from_this(),
                std::move(future),
                [captures=std::move(captures)](std::future<void> prepareFuture) mutable {
                    TRY {
                        auto t = captures._weakThis.lock();
                        if (!t)
                            Throw(std::runtime_error("Assembly line was destroyed before future completed"));

                        captures._map = {};
                        t->CompleteWaitForDataFuture(std::move(captures._transactionRef), std::move(prepareFuture), std::move(captures._stagingConstruction), std::move(captures._oversizeResource), std::move(captures._pool), captures._finalResourceDesc);
                    } CATCH (...) {
                        if (captures._transactionRef._transaction) {
                            captures._transactionRef._transaction->_promise.set_exception(std::current_exception());
                            captures._transactionRef._transaction->_promisePending = false;
                        }
                    } CATCH_END
                });

        } catch (...) {
            transaction->_promise.set_exception(std::current_exception());
            transaction->_promisePending = false;
            UnqueueBytes((UploadDataType)AsUploadDataType(transaction->_desc, prepareStagingStep._bindFlags), RenderCore::ByteCount(transaction->_desc));
        }

        return true;
    }

    void    AssemblyLine::CompleteWaitForDescFuture(TransactionRefHolder&& ref, std::future<ResourceDesc> descFuture, std::shared_ptr<IAsyncDataSource> data, std::shared_ptr<IResourcePool> pool, BindFlag::BitField bindFlags)
    {
        Transaction* transaction = ref._transaction;
        assert(transaction);

        transaction->_waitingFuture = {};

        if (transaction->_cancelledByClient.load()) {
            transaction->_promise.set_exception(std::make_exception_ptr(std::runtime_error("Cancelled before completion")));
            transaction->_promisePending = false;
            return;
        }
        
        try {
            auto desc = descFuture.get();
            transaction->_desc = desc;
            _currentQueuedBytes[(unsigned)AsUploadDataType(desc, bindFlags)] += RenderCore::ByteCount(desc);
            PushStep(
                GetQueueSet(transaction->_creationOptions),
                PrepareStagingStep { std::move(ref), desc, std::move(data), std::move(pool), bindFlags });
        } catch (...) {
            transaction->_promise.set_exception(std::current_exception());
            transaction->_promisePending = false;
        }
    }

    void AssemblyLine::CompleteWaitForDataFuture(TransactionRefHolder&& ref, std::future<void> prepareFuture, PlatformInterface::StagingPage::Allocation&& stagingAllocation, std::shared_ptr<IResource> oversizeResource, std::shared_ptr<IResourcePool> pool, const ResourceDesc& finalResourceDesc)
    {
        auto* transaction = ref._transaction;
        assert(transaction);
        assert(stagingAllocation || oversizeResource);

        transaction->_waitingFuture = {};

        if (transaction->_cancelledByClient.load()) {
            transaction->_promise.set_exception(std::make_exception_ptr(std::runtime_error("Cancelled before completion")));
            transaction->_promisePending = false;
            _currentQueuedBytes[(unsigned)AsUploadDataType(finalResourceDesc, finalResourceDesc._bindFlags)] += RenderCore::ByteCount(finalResourceDesc);
            return;
        }

        // Any exceptions get passed along to the transaction's future. Otherwise we just queue up the
        // next step
        try {
            prepareFuture.get();
            PushStep(
                GetQueueSet(transaction->_creationOptions),
                TransferStagingToFinalStep { std::move(ref), std::move(pool), finalResourceDesc, std::move(stagingAllocation), std::move(oversizeResource) });
        } catch(...) {
            transaction->_promise.set_exception(std::current_exception());
            transaction->_promisePending = false;
            _currentQueuedBytes[(unsigned)AsUploadDataType(finalResourceDesc, finalResourceDesc._bindFlags)] += RenderCore::ByteCount(finalResourceDesc);
        }
    }

    bool AssemblyLine::Process(TransferStagingToFinalStep& transferStagingToFinalStep, PlatformInterface::UploadsThreadContext& context, const CommandListBudget& budgetUnderConstruction)
    {
        CommandListMetrics& metricsUnderConstruction = context.GetMetricsUnderConstruction();
        if ((metricsUnderConstruction._contextOperations+1) >= budgetUnderConstruction._limit_Operations)
            return false;

        Transaction* transaction = transferStagingToFinalStep._transactionRef._transaction;
        assert(transaction);
        auto dataType = (unsigned)AsUploadDataType(transferStagingToFinalStep._finalResourceDesc, transferStagingToFinalStep._finalResourceDesc._bindFlags);
        auto descByteCount = RenderCore::ByteCount(transaction->_desc);     // needs to match CompleteWaitForDescFuture in order to reset _currentQueuedBytes correctly

        if (transaction->_cancelledByClient.load()) {
            transaction->_promise.set_exception(std::make_exception_ptr(std::runtime_error("Cancelled before completion")));
            transaction->_promisePending = false;
            UnqueueBytes((UploadDataType)dataType, descByteCount);
            return true;
        }

        //if ((metricsUnderConstruction._bytesUploadTotal+transferStagingToFinalStep._stagingByteCount) > budgetUnderConstruction._limit_BytesUploaded && metricsUnderConstruction._bytesUploadTotal !=0)
            // return false;

        TRY {
            if (transaction->_finalResource.IsEmpty()) {
                ResourceLocator finalConstruction;
                if (transferStagingToFinalStep._pool && transferStagingToFinalStep._finalResourceDesc._type == ResourceDesc::Type::LinearBuffer) {
                    finalConstruction = transferStagingToFinalStep._pool->Allocate(transferStagingToFinalStep._finalResourceDesc._linearBufferDesc._sizeInBytes, transferStagingToFinalStep._finalResourceDesc._name);
                    if (finalConstruction.IsEmpty())
                        transferStagingToFinalStep._finalResourceDesc = transferStagingToFinalStep._pool->MakeFallbackDesc(transferStagingToFinalStep._finalResourceDesc._linearBufferDesc._sizeInBytes, transferStagingToFinalStep._finalResourceDesc._name);
                }
                
                if (finalConstruction.IsEmpty()) {
                    finalConstruction = CreateResource(context.GetRenderCoreDevice(), transferStagingToFinalStep._finalResourceDesc);
                    metricsUnderConstruction._countDeviceCreations[dataType] += 1;
                }

                if (finalConstruction.IsEmpty())
                    Throw(std::runtime_error("Device resource allocation failed"));

                transaction->_finalResource = finalConstruction;
                metricsUnderConstruction._bytesCreated[dataType] += RenderCore::ByteCount(transferStagingToFinalStep._finalResourceDesc);
                metricsUnderConstruction._countCreations[dataType] += 1;
            }

            // Do the actual data copy step here
            if (transferStagingToFinalStep._stagingResource) {
                context.GetResourceUploadHelper().UpdateFinalResourceFromStaging(
                    transaction->_finalResource,
                    context.GetStagingPage().GetStagingResource(),
                    transferStagingToFinalStep._stagingResource.GetResourceOffset(), transferStagingToFinalStep._stagingResource.GetAllocationSize());

                // Don't delete the staging buffer immediately. It must stick around until the command list is resolved
                // and done with it
                transferStagingToFinalStep._stagingResource.Release(context.GetProducerCmdListSpecificMarker());
            } else {
                assert(transferStagingToFinalStep._oversizeResource);
                auto stagingSize = ByteCount(transferStagingToFinalStep._oversizeResource->GetDesc());
                context.GetResourceUploadHelper().UpdateFinalResourceFromStaging(
                    transaction->_finalResource, *transferStagingToFinalStep._oversizeResource, 0, stagingSize);
                // we'd ideally like to destroy transferStagingToFinalStep._oversizeResource with a cmd list specific destruction order
                // but that can't be done without adding a whole bunch of extra infrastructure
            }

            // Embue the final resource with the completion command list information
            transaction->_finalResource = ResourceLocator { std::move(transaction->_finalResource), context.CommandList_GetUnderConstruction() };

            metricsUnderConstruction._bytesUploadTotal += descByteCount;
            metricsUnderConstruction._bytesUploaded[dataType] += descByteCount;
            metricsUnderConstruction._countUploaded[dataType] += 1;
            ++metricsUnderConstruction._contextOperations;
            transaction->_promise.set_value(transaction->_finalResource);
            transaction->_promisePending = false;
            transferStagingToFinalStep._transactionRef.SuccessfulRetirement();
        } CATCH (...) {
            transaction->_promise.set_exception(std::current_exception());
            transaction->_promisePending = false;
        } CATCH_END

        UnqueueBytes((UploadDataType)dataType, descByteCount);
        return true;
    }

    bool        AssemblyLine::DrainPriorityQueueSet(QueueSet& queueSet, unsigned stepMask, PlatformInterface::UploadsThreadContext& context)
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

    bool AssemblyLine::ProcessQueueSet(QueueSet& queueSet, unsigned stepMask, PlatformInterface::UploadsThreadContext& context, const CommandListBudget& budgetUnderConstruction)
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

    void AssemblyLine::Process(unsigned stepMask, PlatformInterface::UploadsThreadContext& context, LockFreeFixedSizeQueue<unsigned, 4>& pendingFramePriorityCommandLists)
    {
        const bool          isLoading = false;
        CommandListMetrics& metricsUnderConstruction = context.GetMetricsUnderConstruction();
        CommandListBudget   budgetUnderConstruction(isLoading);

        bool atLeastOneRealAction = false;

            /////////////// ~~~~ /////////////// ~~~~ ///////////////
        if (stepMask & Step_BackgroundMisc) {
            std::function<void(AssemblyLine&, PlatformInterface::UploadsThreadContext&)>* fn;
            while (_queuedFunctions.try_front(fn)) {
                fn->operator()(*this, context);
                _queuedFunctions.pop();
            }

            auto cc = context.CommitCount_Current();
            if (cc > _commitCountLastOnBackgroundFrame) {
                ScopedLock(_onBackgroundFrameLock);
                _onBackgroundFrame.Invoke();
                _commitCountLastOnBackgroundFrame = cc;
            }

            context.GetStagingPage().UpdateConsumerMarker();        // update at least once per frame, not strictly necessary, but improves metrics
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

        if (stepMask & Step_BackgroundMisc) {
            // move from _pendingRetirements into the 
            ScopedLock(_pendingRetirementsLock);
            auto& metrics = context.GetMetricsUnderConstruction();
            auto nonOverflow = std::min(_pendingRetirements.size(), dimof(metrics._retirements) - metrics._retirementCount);
            std::copy(_pendingRetirements.begin(), _pendingRetirements.begin()+nonOverflow, &metrics._retirements[metrics._retirementCount]);
            metrics._retirementCount += nonOverflow;
            if (_pendingRetirements.size() > nonOverflow)
                metrics._retirementsOverflow.insert(metrics._retirementsOverflow.end(), _pendingRetirements.begin()+nonOverflow, _pendingRetirements.end());
            _pendingRetirements.clear();
        }

        CommandListID commandListIdCommitted = ~unsigned(0x0);

            /////////////// ~~~~ /////////////// ~~~~ ///////////////
        const bool somethingToResolve = 
                (metricsUnderConstruction._contextOperations!=0)
            || !context.GetDeferredOperationsUnderConstruction().IsEmpty();
        
        // The commit count is a scheduling scheme
        //    -- we will generally "resolve" a command list and queue it for submission
        //      once per call to Manager::Update(). The exception is when there are frame
        //      priority requests
        const unsigned commitCountCurrent = context.CommitCount_Current();
        const bool normalPriorityResolve = commitCountCurrent > context.CommitCount_LastResolve();
        if ((framePriorityResolve||normalPriorityResolve) && somethingToResolve) {
            commandListIdCommitted = context.CommandList_GetUnderConstruction();
            context.CommitCount_LastResolve() = commitCountCurrent;

            metricsUnderConstruction._assemblyLineMetrics = CalculateMetrics(context);

            context.ResolveCommandList();

            atLeastOneRealAction = true;
        }

        if (popFromFramePriority)
            pendingFramePriorityCommandLists.pop();
    }

    AssemblyLineMetrics AssemblyLine::CalculateMetrics(PlatformInterface::UploadsThreadContext& context)
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

    AssemblyLine::QueueSet& AssemblyLine::GetQueueSet(TransactionOptions::BitField transactionOptions)
    {
        if (transactionOptions & TransactionOptions::FramePriority) {
            return _queueSet_FramePriority[_framePriority_WritingQueueSet];    // not 100% thread safe
        } else {
            return _queueSet_Main;
        }
    }

    void AssemblyLine::PushStep(QueueSet& queueSet, PrepareStagingStep&& step)
    {
        queueSet._prepareStagingSteps.push_overflow(std::move(step));
        _wakeupEvent.Increment();
    }

    void AssemblyLine::PushStep(QueueSet& queueSet, TransferStagingToFinalStep&& step)
    {
        queueSet._transferStagingToFinalSteps.push_overflow(std::move(step));
        _wakeupEvent.Increment();
    }

    void AssemblyLine::PushStep(QueueSet& queueSet, CreateFromDataPacketStep&& step)
    {
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

    unsigned AssemblyLine::BindOnBackgroundFrame(std::function<void()>&& fn)
    {
        ScopedLock(_onBackgroundFrameLock);
        return _onBackgroundFrame.Bind(std::move(fn));
    }

    void AssemblyLine::UnbindOnBackgroundFrame(unsigned marker)
    {
        ScopedLock(_onBackgroundFrameLock);
        _onBackgroundFrame.Unbind(marker);
    }

        ///////////////////   M A N A G E R   ///////////////////

    class Manager : public IManager
    {
    public:
        TransactionMarker           Begin(const ResourceDesc& desc, std::shared_ptr<IDataPacket> data, TransactionOptions::BitField flags) override
        {
            return _assemblyLine->Begin(desc, std::move(data), nullptr, flags);
        }

        TransactionMarker           Begin(ResourceLocator destinationResource, std::shared_ptr<IDataPacket> data, TransactionOptions::BitField flags) override
        {
            return _assemblyLine->Begin(destinationResource, std::move(data), flags);
        }

        TransactionMarker           Begin(const ResourceDesc& desc, std::shared_ptr<IDataPacket> data, std::shared_ptr<IResourcePool> pool, TransactionOptions::BitField flags) override
        {
            return _assemblyLine->Begin(desc, std::move(data), std::move(pool), flags);
        }

        TransactionMarker           Begin(std::shared_ptr<IAsyncDataSource> data, BindFlag::BitField bindFlags, TransactionOptions::BitField flags) override
        {
            return _assemblyLine->Begin(std::move(data), nullptr, bindFlags, flags);
        }

        TransactionMarker           Begin(std::shared_ptr<IAsyncDataSource> data, std::shared_ptr<IResourcePool> pool, TransactionOptions::BitField flags) override
        {
            return _assemblyLine->Begin(std::move(data), std::move(pool), 0, flags);
        }

        TransactionMarker           Begin(ResourceLocator destinationResource, std::shared_ptr<IAsyncDataSource> data, TransactionOptions::BitField flags) override
        {
            return _assemblyLine->Begin(std::move(destinationResource), std::move(data), flags);
        }

        std::future<CommandListID>  Begin(ResourceLocator destinationResource, ResourceLocator sourceResource, IteratorRange<const Utility::RepositionStep*> repositionOperations) override
        {
            return _assemblyLine->Begin(std::move(destinationResource), std::move(sourceResource), repositionOperations);
        }

        void                    Cancel      (IteratorRange<const TransactionID*> ids) override
        {
            _assemblyLine->Cancel(ids);
        }

        void                    OnCompletion(IteratorRange<const TransactionID*> transactions, std::function<void()>&& fn) override
        {
            _assemblyLine->OnCompletion(transactions, std::move(fn));
        }

        unsigned                BindOnBackgroundFrame(std::function<void()>&& fn) override
        {
            return _assemblyLine->BindOnBackgroundFrame(std::move(fn));
        }

        void                    UnbindOnBackgroundFrame(unsigned marker) override
        {
            _assemblyLine->UnbindOnBackgroundFrame(marker);
        }

        ResourceLocator         ImmediateTransaction(
                                    IThreadContext& threadContext,
                                    const ResourceDesc& desc, IDataPacket& data) override
        {
            return _assemblyLine->ImmediateTransaction(threadContext, desc, data);
        }
        
        bool                    IsComplete(CommandListID id) override;
        void                    StallUntilCompletion(IThreadContext& immediateContext, CommandListID id) override;

        CommandListMetrics      PopMetrics() override;

        void                    Update(IThreadContext&) override;
        void                    FramePriority_Barrier() override;

        Manager(IDevice& renderDevice);
        ~Manager();

    private:
        std::shared_ptr<AssemblyLine> _assemblyLine;
        unsigned _foregroundStepMask, _backgroundStepMask;

        std::unique_ptr<std::thread> _backgroundThread;
        std::unique_ptr<PlatformInterface::UploadsThreadContext> _backgroundContext;
        std::unique_ptr<PlatformInterface::UploadsThreadContext> _foregroundContext;

        volatile bool _shutdownBackgroundThread;

        LockFreeFixedSizeQueue<unsigned, 4> _pendingFramePriority_CommandLists;
        unsigned _frameId = 0;

        uint32_t DoBackgroundThread();
    };

        /////////////////////////////////////////////

    bool                    Manager::IsComplete(CommandListID id)
    {
        return id <= (_backgroundStepMask ? _backgroundContext.get() : _foregroundContext.get())->CommandList_GetCommittedToImmediate();
    }

    void                    Manager::StallUntilCompletion(IThreadContext& immediateContext, CommandListID id)
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

    void                    Manager::Update(IThreadContext& immediateContext)
    {
        if (_foregroundStepMask)
            _assemblyLine->Process(_foregroundStepMask, *_foregroundContext.get(), _pendingFramePriority_CommandLists);

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

    Manager::Manager(IDevice& renderDevice) : _assemblyLine(std::make_shared<AssemblyLine>(renderDevice))
    {
        _shutdownBackgroundThread = false;

        bool multithreadingOk = true;

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
        _backgroundContext   = std::make_unique<PlatformInterface::UploadsThreadContext>(backgroundDeviceContext);
        _foregroundContext   = std::make_unique<PlatformInterface::UploadsThreadContext>(std::move(immediateDeviceContext));

            //  todo --     if we don't have driver support for concurrent creates, we should try to do this
            //              in the main render thread. Also, if we've created the device with the single threaded
            //              parameter, we should do the same.

        if (multithreadingOk) {
            _foregroundStepMask = 0;        // (do this with the immediate context (main thread) in order to allow writing directly to video memory
            _backgroundStepMask = 
                    AssemblyLine::Step_PrepareStaging
                |   AssemblyLine::Step_TransferStagingToFinal
                |   AssemblyLine::Step_CreateFromDataPacket
                |   AssemblyLine::Step_BatchedDefrag
                |   AssemblyLine::Step_BackgroundMisc
                ;
        } else {
            _foregroundStepMask = 
                    AssemblyLine::Step_PrepareStaging
                |   AssemblyLine::Step_TransferStagingToFinal
                |   AssemblyLine::Step_CreateFromDataPacket
                |   AssemblyLine::Step_BatchedDefrag
                |   AssemblyLine::Step_BackgroundMisc
                ;
            _backgroundStepMask = 0;
        }
        if (_backgroundStepMask) {
            _backgroundThread = std::make_unique<std::thread>(
                [this](){ 
                    _backgroundContext->GetStagingPage().BindThread();
                    _assemblyLine->BindBackgroundThread();
                    return DoBackgroundThread(); 
                });
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
        _future = std::move(moveFrom._future);
        _transactionID = std::move(moveFrom._transactionID);
        _assemblyLine = std::move(moveFrom._assemblyLine);
        moveFrom._transactionID = TransactionID_Invalid;
        moveFrom._assemblyLine = nullptr;
        return *this;
    }

    std::unique_ptr<IManager> CreateManager(IDevice& renderDevice)
    {
        return std::make_unique<Manager>(renderDevice);
    }

    IManager::~IManager() {}
}}


