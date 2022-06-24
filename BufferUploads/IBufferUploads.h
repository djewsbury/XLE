// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/IDevice_Forward.h"
#include "../RenderCore/ResourceDesc.h"
#include "../RenderCore/ResourceUtils.h"
#include "../RenderCore/BufferView.h"
#include <memory>
#include <future>

#if OUTPUT_DLL
    #define buffer_upload_dll_export       dll_export
#else
    #define buffer_upload_dll_export
#endif

namespace Assets { class DependencyValidation; }

namespace BufferUploads
{
	using LinearBufferDesc = RenderCore::LinearBufferDesc;
	using TextureSamples = RenderCore::TextureSamples;
	using TextureDesc = RenderCore::TextureDesc;
    using ResourceDesc = RenderCore::ResourceDesc;
    using IResource = RenderCore::IResource;
    using TexturePitches = RenderCore::TexturePitches;
    using SubResourceId = RenderCore::SubResourceId;
    namespace BindFlag = RenderCore::BindFlag;

        /////////////////////////////////////////////////

    class IDataPacket;
    class IAsyncDataSource;
    class ResourceLocator;
    class TransactionMarker;

        /////////////////////////////////////////////////

    using TransactionID = uint64_t;
    using CommandListID = uint32_t;
    static const CommandListID CommandListID_Invalid = ~CommandListID(0);
    static const TransactionID TransactionID_Invalid = ~TransactionID(0);
    class Event_ResourceReposition;

    struct BatchedHeapMetrics;
    struct BatchingSystemMetrics;
    struct PoolSystemMetrics;
    struct CommandListMetrics;

        /////////////////////////////////////////////////

    namespace TransactionOptions
    {
        enum {
            FramePriority    = 1<<0
        };
        using BitField = unsigned;
    }

    class IResourcePool;

    class ResourceLocator
    {
    public:
        std::shared_ptr<IResource> AsIndependentResource() const;

        RenderCore::VertexBufferView CreateVertexBufferView() const;
        RenderCore::IndexBufferView CreateIndexBufferView(RenderCore::Format indexFormat) const;
        RenderCore::ConstantBufferView CreateConstantBufferView() const;
        std::shared_ptr<RenderCore::IResourceView> CreateTextureView(BindFlag::Enum usage = BindFlag::ShaderResource, const RenderCore::TextureViewDesc& window = RenderCore::TextureViewDesc{}) const;
        std::shared_ptr<RenderCore::IResourceView> CreateBufferView(BindFlag::Enum usage = BindFlag::ConstantBuffer, unsigned rangeOffset = 0, unsigned rangeSize = 0) const;

        const std::shared_ptr<IResource>& GetContainingResource() const { return _resource; }
        std::pair<size_t, size_t> GetRangeInContainingResource() const { return std::make_pair(_interiorOffset, _interiorOffset+_interiorSize); }

        CommandListID GetCompletionCommandList() const { return _completionCommandList; }

        ResourceLocator MakeSubLocator(size_t offset, size_t size);

        bool IsEmpty() const { return _resource == nullptr; }
        bool IsWholeResource() const;

        ResourceLocator(
            std::shared_ptr<IResource> independentResource);
        ResourceLocator(
            std::shared_ptr<IResource> containingResource,
            size_t interiorOffset, size_t interiorSize,
            std::weak_ptr<IResourcePool> pool, uint64_t poolMarker,
            bool initialReferenceAlreadyTaken = false,
            CommandListID completionCommandList = CommandListID_Invalid);
        ResourceLocator(
            std::shared_ptr<IResource> containingResource,
            size_t interiorOffset, size_t interiorSize,
            CommandListID completionCommandList = CommandListID_Invalid);
        ResourceLocator(
            ResourceLocator&& moveFrom,
            CommandListID completionCommandList);
        ResourceLocator();
        ~ResourceLocator();

        ResourceLocator(ResourceLocator&&) never_throws;
        ResourceLocator& operator=(ResourceLocator&&) never_throws;
        ResourceLocator(const ResourceLocator&);
        ResourceLocator& operator=(const ResourceLocator&);
    private:
        std::shared_ptr<IResource> _resource;
        size_t _interiorOffset = ~size_t(0), _interiorSize = ~size_t(0);
        std::weak_ptr<IResourcePool> _pool;
        uint64_t _poolMarker = ~0ull;
        bool _managedByPool = false;
        CommandListID _completionCommandList = CommandListID_Invalid;
    };

        /////////////////////////////////////////////////

    class IManager
    {
    public:
            /// \name Begin and End transactions
            /// @{

            /// <summary>Begin a new transaction</summary>
            /// Begin a new transaction, either by creating a new resource, or by attaching
            /// to an existing resource.
        virtual TransactionMarker   Transaction_Begin    (const std::shared_ptr<IAsyncDataSource>& data, BindFlag::BitField bindFlags = BindFlag::ShaderResource, TransactionOptions::BitField flags=0) = 0;
        virtual TransactionMarker   Transaction_Begin    (ResourceLocator destinationResource, const std::shared_ptr<IAsyncDataSource>& data, TransactionOptions::BitField flags=0) = 0;

        virtual TransactionMarker   Transaction_Begin    (const ResourceDesc& desc, const std::shared_ptr<IDataPacket>& data, TransactionOptions::BitField flags=0) = 0;
        virtual TransactionMarker   Transaction_Begin    (ResourceLocator destinationResource, const std::shared_ptr<IDataPacket>& data, TransactionOptions::BitField flags=0) = 0;

        virtual void            Transaction_Release      (TransactionID id) = 0;

            /// @}

            /// \name Immediate creation
            /// @{

            /// <summary>Create a new buffer synchronously</summary>
            /// Creates a new resource synchronously. All creating objects will
            /// execute in the current thread, and a new resource will be returned from
            /// the call. Use these methods when uploads can't be delayed.
        virtual ResourceLocator
            Transaction_Immediate(  RenderCore::IThreadContext& threadContext,
                                    const ResourceDesc& desc, IDataPacket& data) = 0;
            /// @}

            /// <summary>Checks for completion</summary>
            /// Returns true iff the given transaction has been completed.
        virtual bool            IsComplete (CommandListID id) = 0;

            /// \name Event queue
            /// @{

        typedef uint32_t EventListID;
        virtual EventListID     EventList_GetLatestID   () = 0;
        virtual void            EventList_Get           (EventListID id, Event_ResourceReposition*&begin, Event_ResourceReposition*&end) = 0;
        virtual void            EventList_Release       (EventListID id) = 0;
            /// @}

            /// \name Frame management
            /// @{

            /// <summary>Called every frame to update uploads</summary>
            /// Performs once-per-frame tasks. Normally called by the render device once per frame.
        virtual void                    Update  (RenderCore::IThreadContext& immediateContext) = 0;
            /// @}

        virtual void                    StallUntilCompletion(RenderCore::IThreadContext& immediateContext, CommandListID id) = 0;

            /// \name Utilities, profiling & debugging
            /// @{

            /// <summary>Gets performance metrics</summary>
            /// Gets the latest performance metrics. Internally the system
            /// maintains a queue of performance metrics. Every frame, a new
            /// set of metrics is pushed onto the queue (until the stack reaches
            /// it's maximum size).
            /// PopMetrics() will remove the next item from the queue. If there
        /// no more items, "_commitTime" will be 0.
        virtual CommandListMetrics      PopMetrics              () = 0;
            /// <summary>Returns metrics about pool memory</summary>
            /// Returns some profiling metrics related to the resource pooling
            /// buffers maintained by the system. Used by the BufferUploadDisplay
            /// for presenting profiling information.
        virtual PoolSystemMetrics       CalculatePoolMetrics    () const = 0;
            /// <summary>Sets a barrier for frame priority operations</summary>
            /// Sets a barrier, which determines the "end of frame" point for
            /// frame priority operations. This will normally be called from the same
            /// thread that begins most upload operations.
        virtual void                    FramePriority_Barrier   () = 0;
            /// @}

        virtual ~IManager();
    };

    class IDataPacket
    {
    public:
        virtual IteratorRange<void*>    GetData         (SubResourceId subRes = {}) = 0;
        virtual TexturePitches          GetPitches      (SubResourceId subRes = {}) const = 0;
        virtual ~IDataPacket();
    };

    class IAsyncDataSource
    {
    public:
        virtual std::future<ResourceDesc> GetDesc () = 0;

        struct SubResource
        {
            SubResourceId _id;
            IteratorRange<void*> _destination;
            TexturePitches _pitches;
        };

        virtual std::future<void> PrepareData(IteratorRange<const SubResource*> subResources) = 0;

        virtual Assets::DependencyValidation GetDependencyValidation() const = 0;

        virtual ~IAsyncDataSource();
    };

    class TransactionMarker
    {
    public:
        std::future<ResourceLocator> _future;
        TransactionID _transactionID = TransactionID_Invalid;

        bool IsValid() const;
        operator bool() const { return IsValid(); }
        
        TransactionMarker();
        ~TransactionMarker();
        TransactionMarker(TransactionMarker&& moveFrom) never_throws;
        TransactionMarker& operator=(TransactionMarker&& moveFrom) never_throws;
    private:
        friend class AssemblyLine;
        friend class Manager;
        TransactionMarker(std::future<ResourceLocator>&&, TransactionID, AssemblyLine&);
        AssemblyLine* _assemblyLine = nullptr;
    };

        /////////////////////////////////////////////////

    buffer_upload_dll_export std::shared_ptr<IDataPacket> CreateBasicPacket(
        IteratorRange<const void*> data = {}, 
        TexturePitches pitches = TexturePitches());

    buffer_upload_dll_export std::shared_ptr<IDataPacket> CreateBasicPacket(
        std::vector<uint8_t>&& data, 
        TexturePitches pitches = TexturePitches());

    buffer_upload_dll_export std::shared_ptr<IDataPacket> CreateEmptyPacket(const ResourceDesc& desc);
    buffer_upload_dll_export std::shared_ptr<IDataPacket> CreateEmptyLinearBufferPacket(size_t size);
    buffer_upload_dll_export std::unique_ptr<IManager> CreateManager(RenderCore::IDevice& renderDevice);

}

