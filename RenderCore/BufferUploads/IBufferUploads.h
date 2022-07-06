// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../IDevice_Forward.h"
#include "../ResourceDesc.h"
#include "../ResourceUtils.h"
#include "../BufferView.h"
#include <memory>
#include <future>
#include <functional>

#if OUTPUT_DLL
    #define buffer_upload_dll_export       dll_export
#else
    #define buffer_upload_dll_export
#endif

namespace Assets { class DependencyValidation; }
namespace Utility { struct RepositionStep; }

namespace RenderCore { namespace BufferUploads
{
        /////////////////////////////////////////////////

    class IDataPacket;
    class IAsyncDataSource;
    class ResourceLocator;
    class TransactionMarker;
    class IResourcePool;

        /////////////////////////////////////////////////

    using TransactionID = uint64_t;
    using CommandListID = uint32_t;
    static const CommandListID CommandListID_Invalid = ~CommandListID(0);
    static const TransactionID TransactionID_Invalid = ~TransactionID(0);

    struct CommandListMetrics;

        /////////////////////////////////////////////////

    namespace TransactionOptions
    {
        enum { FramePriority    = 1<<0 };
        using BitField = unsigned;
    }

        /////////////////////////////////////////////////

    class IManager
    {
    public:
            /// \name Begin and End transactions
            /// @{

            /// <summary>Begin a new transaction</summary>
            /// Begin a new transaction, either by creating a new resource, or by attaching
            /// to an existing resource.
        virtual TransactionMarker   Begin    (std::shared_ptr<IAsyncDataSource> data, BindFlag::BitField bindFlags = BindFlag::ShaderResource, TransactionOptions::BitField flags=0) = 0;
        virtual TransactionMarker   Begin    (ResourceLocator destinationResource, std::shared_ptr<IAsyncDataSource> data, TransactionOptions::BitField flags=0) = 0;
        virtual TransactionMarker   Begin    (std::shared_ptr<IAsyncDataSource> data, std::shared_ptr<IResourcePool>, TransactionOptions::BitField flags=0) = 0;

        virtual TransactionMarker   Begin    (const ResourceDesc& desc, std::shared_ptr<IDataPacket> data, TransactionOptions::BitField flags=0) = 0;
        virtual TransactionMarker   Begin    (ResourceLocator destinationResource, std::shared_ptr<IDataPacket> data, TransactionOptions::BitField flags=0) = 0;
        virtual TransactionMarker   Begin    (const ResourceDesc& desc, std::shared_ptr<IDataPacket> data, std::shared_ptr<IResourcePool>, TransactionOptions::BitField flags=0) = 0;

        virtual std::future<CommandListID>   Begin    (ResourceLocator destinationResource, ResourceLocator sourceResource, IteratorRange<const Utility::RepositionStep*> repositionOperations) = 0;

        virtual void            Cancel      (IteratorRange<const TransactionID*>) = 0;

        virtual void            OnCompletion(IteratorRange<const TransactionID*>, std::function<void()>&& fn) = 0;

            /// @}

            /// \name Immediate creation
            /// @{

            /// <summary>Create a new buffer synchronously</summary>
            /// Creates a new resource synchronously. All creating objects will
            /// execute in the current thread, and a new resource will be returned from
            /// the call. Use these methods when uploads can't be delayed.
        virtual ResourceLocator
            ImmediateTransaction(  IThreadContext& threadContext,
                                    const ResourceDesc& desc, IDataPacket& data) = 0;
            /// @}

            /// \name Frame management
            /// @{

            /// <summary>Called every frame to update uploads</summary>
            /// Performs once-per-frame tasks. Normally called by the render device once per frame.
        virtual void                    Update  (IThreadContext& immediateContext) = 0;
        virtual void                    StallUntilCompletion(IThreadContext& immediateContext, CommandListID id) = 0;
        virtual bool                    IsComplete (CommandListID id) = 0;
            /// @}

            /// <summary>Registers a function to be executed in the background thread on a semi-regular basis</summary>
            /// The function will not be called more frequently than about once per frame, but will only be called when
            /// the background thread is active with other operations
        virtual unsigned                BindOnBackgroundFrame(std::function<void()>&&) = 0;
        virtual void                    UnbindOnBackgroundFrame(unsigned) = 0;

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

        virtual ::Assets::DependencyValidation GetDependencyValidation() const = 0;

        virtual ~IAsyncDataSource();
    };

    class IResourcePool;
    class ResourceLocator
    {
    public:
        std::shared_ptr<IResource> AsIndependentResource() const;

        VertexBufferView CreateVertexBufferView() const;
        IndexBufferView CreateIndexBufferView(Format indexFormat) const;
        ConstantBufferView CreateConstantBufferView() const;
        std::shared_ptr<IResourceView> CreateTextureView(BindFlag::Enum usage = BindFlag::ShaderResource, const TextureViewDesc& window = TextureViewDesc{}) const;
        std::shared_ptr<IResourceView> CreateBufferView(BindFlag::Enum usage = BindFlag::ConstantBuffer, unsigned rangeOffset = 0, unsigned rangeSize = 0) const;

        const std::shared_ptr<IResource>& GetContainingResource() const { return _resource; }
        std::pair<size_t, size_t> GetRangeInContainingResource() const { return std::make_pair(_interiorOffset, _interiorOffset+_interiorSize); }

        CommandListID GetCompletionCommandList() const { return _completionCommandList; }

        ResourceLocator MakeSubLocator(size_t offset, size_t size) const;
        const std::weak_ptr<IResourcePool>& GetPool() const { return _pool; }

        bool IsEmpty() const { return _resource == nullptr; }
        bool IsWholeResource() const;

        ResourceLocator(
            std::shared_ptr<IResource> independentResource);
        ResourceLocator(
            std::shared_ptr<IResource> containingResource,
            size_t interiorOffset, size_t interiorSize,
            std::weak_ptr<IResourcePool> pool,
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
        bool _managedByPool = false;
        CommandListID _completionCommandList = CommandListID_Invalid;
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

    class IResourcePool
	{
	public:
		virtual ResourceLocator Allocate(size_t size, StringSection<> name) = 0;
		virtual ResourceDesc MakeFallbackDesc(size_t size, StringSection<> name) = 0;

		virtual bool AddRef(IResource& resource, size_t offset, size_t size) = 0;
		virtual bool Release(IResource& resource, size_t offset, size_t size) = 0;
		virtual ~IResourcePool();
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
    buffer_upload_dll_export std::unique_ptr<IManager> CreateManager(IDevice& renderDevice);

}}

