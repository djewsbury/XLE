// Copyright 2016 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../ResourceDesc.h"        // needed for TextureViewDesc constructor
#include "../Types.h"
#include "../FrameBufferDesc.h"
#include "../Metal/Forward.h"
#include "../../Math/Vector.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>

namespace RenderCore 
{
    class FrameBufferDesc;
    class AttachmentDesc;
    class TextureViewDesc;
    class TextureSamples;
    class FrameBufferProperties;
    class ViewportDesc;
    using AttachmentName = uint32_t;
    class IResource;
    using IResourcePtr = std::shared_ptr<IResource>;
}

namespace RenderCore { namespace Techniques
{
    struct PreregisteredAttachment;

////////////////////////////////////////////////////////////////////////////////////////////////////

    class FrameBufferDescFragment
    {
    public:
        AttachmentName DefineAttachment(
            uint64_t semantic,
            LoadStore loadOp = LoadStore::Retain, LoadStore storeOp = LoadStore::Retain,
            BindFlag::BitField initialLayout = 0, BindFlag::BitField finalLayout = 0);
		AttachmentName DefineAttachment(
			uint64_t semantic,
            const AttachmentDesc& request);
        
        struct ViewedAttachment : public RenderCore::AttachmentViewDesc { BindFlag::Enum _usage = BindFlag::ShaderResource; };
        struct SubpassDesc : public RenderCore::SubpassDesc
        {
            IteratorRange<const ViewedAttachment*> GetViews() const { return MakeIteratorRange(_views); }
            void AppendNonFrameBufferAttachmentView(AttachmentName name, BindFlag::Enum usage = BindFlag::ShaderResource, TextureViewDesc window = {});
            std::vector<ViewedAttachment> _views;
        };
        void AddSubpass(SubpassDesc&& subpass);
        void AddSubpass(RenderCore::SubpassDesc&& subpass);

        FrameBufferDescFragment();
        ~FrameBufferDescFragment();

        struct Attachment
        {
            uint64_t _inputSemanticBinding;
            uint64_t _outputSemanticBinding;
            AttachmentDesc _desc;

            uint64_t GetInputSemanticBinding() const { return _inputSemanticBinding; }
            uint64_t GetOutputSemanticBinding() const { return _outputSemanticBinding; }
        };
        std::vector<Attachment>     _attachments;
        std::vector<SubpassDesc>    _subpasses;
		PipelineType				_pipelineType = PipelineType::Graphics;
    };

    struct PreregisteredAttachment
    {
    public:
        uint64_t _semantic = 0ull;
        ResourceDesc _desc;
        enum class State { Uninitialized, Initialized, Initialized_StencilUninitialized, Uninitialized_StencilInitialized };
        State _state = State::Uninitialized;
        BindFlag::BitField _layoutFlags = 0;

        uint64_t CalculateHash() const;
    };

    uint64_t HashPreregisteredAttachments(
        IteratorRange<const PreregisteredAttachment*> attachments,
        const FrameBufferProperties& fbProps,
        uint64_t seed = DefaultSeed64);

    class FragmentStitchingContext
    {
    public:
        void DefineAttachment(
            uint64_t semantic, const ResourceDesc&,
            PreregisteredAttachment::State state = PreregisteredAttachment::State::Uninitialized, 
            BindFlag::BitField initialLayoutFlags = 0);
        void DefineAttachment(const PreregisteredAttachment& attachment);
        void Undefine(uint64_t semantic);

        struct AttachmentTransform
        {
            enum Type 
            {
                Preserved, Generated, Written, Consumed, Temporary
            };
            Type _type = Temporary;
            BindFlag::BitField _newLayout = 0;
        };
        struct StitchResult
        {
            FrameBufferDesc _fbDesc;
            std::vector<PreregisteredAttachment> _fullAttachmentDescriptions;
            std::vector<AttachmentTransform> _attachmentTransforms;
            std::string _log;
            std::vector<FrameBufferDescFragment::ViewedAttachment> _viewedAttachments;
            std::vector<unsigned> _viewedAttachmentsMap;      // subpassIdx -> index in _viewedAttachments
            PipelineType _pipelineType = PipelineType::Graphics;
        };

        StitchResult TryStitchFrameBufferDesc(IteratorRange<const FrameBufferDescFragment*> fragments);

        void UpdateAttachments(const StitchResult& res);
        IteratorRange<const PreregisteredAttachment*> GetPreregisteredAttachments() const { return MakeIteratorRange(_workingAttachments); }

        FrameBufferProperties _workingProps;

        FragmentStitchingContext(IteratorRange<const PreregisteredAttachment*> preregAttachments = {}, const FrameBufferProperties& fbProps = {});
        ~FragmentStitchingContext();
    private:
        std::vector<PreregisteredAttachment> _workingAttachments;
        StitchResult TryStitchFrameBufferDescInternal(const FrameBufferDescFragment& fragment);
    };

    struct MergeFragmentsResult
    {
        FrameBufferDescFragment _mergedFragment;
        std::vector<std::pair<uint64_t, AttachmentName>> _inputAttachments;
        std::vector<std::pair<uint64_t, AttachmentName>> _outputAttachments;
        std::string _log;
    };

    MergeFragmentsResult MergeFragments(
        IteratorRange<const PreregisteredAttachment*> preregisteredAttachments,
        IteratorRange<const FrameBufferDescFragment*> fragments,
		const FrameBufferProperties& fbProps);

////////////////////////////////////////////////////////////////////////////////////////////////////

    class AttachmentPool
    {
    public:
        void Bind(uint64_t semantic, const IResourcePtr& resource);
        void Unbind(const IResource& resource);
        void Unbind(uint64_t semantic);
        void UnbindAll();
		auto GetBoundResource(uint64_t semantic) -> IResourcePtr;

        const std::shared_ptr<IResource>& GetResource(AttachmentName resName) const;
        auto GetSRV(AttachmentName resName, const TextureViewDesc& window = {}) const -> const std::shared_ptr<IResourceView>&;
        auto GetView(AttachmentName resName, BindFlag::Enum usage, const TextureViewDesc& window = {}) const -> const std::shared_ptr<IResourceView>&;

        struct ReservationFlag
        {
            enum class Enum {};
            using BitField=unsigned; 
        };
        class Reservation;
        Reservation Reserve(IteratorRange<const PreregisteredAttachment*>, ReservationFlag::BitField = 0);

        void ResetActualized();
        std::string GetMetrics() const;

        AttachmentPool(const std::shared_ptr<IDevice>& device);
        ~AttachmentPool();
    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        void AddRef(IteratorRange<const AttachmentName*>, ReservationFlag::BitField flags);
        void Release(IteratorRange<const AttachmentName*>, ReservationFlag::BitField flags);
    };

    class AttachmentPool::Reservation
    {
    public:
        IteratorRange<const AttachmentName*> GetResourceIds() const { return MakeIteratorRange(_reservedAttachments); }
        void CompleteInitialization(IThreadContext&);
        bool HasPendingCompleteInitialization() const;
        Reservation();
        ~Reservation();
        Reservation(Reservation&&);
        Reservation& operator=(Reservation&&);
        Reservation(const Reservation&);
        Reservation& operator=(const Reservation&);
    private:
        std::vector<AttachmentName> _reservedAttachments;
        AttachmentPool* _pool;
        ReservationFlag::BitField _reservationFlags;

        Reservation(
            std::vector<AttachmentName>&& reservedAttachments,
            AttachmentPool* pool,
            ReservationFlag::BitField flags);
        friend class AttachmentPool;
    };

    struct RenderPassBeginDesc
    {
        IteratorRange<const ClearValue*>    _clearValues;
    };

    /// <summary>Stores a set of retained frame buffers, which can be reused frame-to-frame</summary>
    /// Client code typically just wants to define the size and formats of frame buffers, without
    /// manually retaining and managing the objects themselves. It's a result of typical usage patterns
    /// of RenderPassInstance.
    ///
    /// This helper class allows client code to simply declare what it needs and the actual management
    /// of the device objects will be handled within the cache.
    class FrameBufferPool;
    std::shared_ptr<FrameBufferPool> CreateFrameBufferPool();

    void ResetFrameBufferPool(FrameBufferPool& fbPool);

    class ParsingContext;

////////////////////////////////////////////////////////////////////////////////////////////////////

    /// <summary>Begins and ends a render pass on the given context</summary>
    /// Creates and begins a render pass using the given frame buffer layout. This will also automatically
    /// allocate the buffers required
    ///
    /// The "hashName" parameter to the constructor can be used to reuse buffers from previous instances
    /// of a similar render pass. For example, a render pass instance might be for rendering and resolving
    /// the lighting for a deferred lighting scheme. This will be rendered every frame, usually using the same
    /// parameters. Use the same hashName to ensure that the same cached frame buffer will be reused (when possible).
    ///
    /// If an output attachment is required after the render pass instance is finished, call GetAttachment().
    /// This can be used to retrieve the rendered results.
    class RenderPassInstance
    {
    public:
        void NextSubpass();
        void End();
        unsigned GetCurrentSubpassIndex() const;

        Metal::FrameBuffer& GetFrameBuffer() { return *_frameBuffer; }
        const Metal::FrameBuffer& GetFrameBuffer() const { return *_frameBuffer; }
        const FrameBufferDesc& GetFrameBufferDesc() const { return _layout; }
        ViewportDesc GetDefaultViewport() const;
        const AttachmentPool::Reservation& GetAttachmentReservation() const { return _attachmentPoolReservation; }

        auto GetInputAttachmentResource(unsigned inputAttachmentSlot) const -> const std::shared_ptr<IResource>&;
        auto GetInputAttachmentView(unsigned inputAttachmentSlot) const -> const std::shared_ptr<IResourceView>&;

		auto GetOutputAttachmentResource(unsigned outputAttachmentSlot) const -> const std::shared_ptr<IResource>&;
	    auto GetOutputAttachmentSRV(unsigned outputAttachmentSlot, const TextureViewDesc& window) const -> const std::shared_ptr<IResourceView>&;

		auto GetDepthStencilAttachmentResource() const -> const std::shared_ptr<IResource>&;
		auto GetDepthStencilAttachmentSRV(const TextureViewDesc& window = {}) const -> const std::shared_ptr<IResourceView>&;

		// The "AttachmentNames" here map onto the names used by the FrameBufferDesc used to initialize this RPI
        auto GetResourceForAttachmentName(AttachmentName resName) const -> const std::shared_ptr<IResource>&;
        auto GetSRVForAttachmentName(AttachmentName resName, const TextureViewDesc& window = {}) const -> const std::shared_ptr<IResourceView>&;

        auto GetNonFrameBufferAttachmentView(unsigned viewedAttachmentSlot) const -> const std::shared_ptr<IResourceView>&;

        // Construct from a fully actualized "FrameBufferDesc" (eg, one generated via a
        // FragmentStitchingContext)
        RenderPassInstance(
            IThreadContext& context,
            const FrameBufferDesc& layout,
            IteratorRange<const PreregisteredAttachment*> fullAttachmentsDescription,
            FrameBufferPool& frameBufferPool,
            AttachmentPool& attachmentPool,
            const RenderPassBeginDesc& beginInfo = RenderPassBeginDesc());

        // Construct from a fully FrameBufferDescFragment fragment. This will use the
        // stitching context in the ParsingContext to link the frame buffer to the current
        // environment
        RenderPassInstance(
            IThreadContext& context,
            ParsingContext& parsingContext,
            const FrameBufferDescFragment& layoutFragment,
            const RenderPassBeginDesc& beginInfo = RenderPassBeginDesc());

        RenderPassInstance(
            IThreadContext& context,
            ParsingContext& parsingContext,
            const FragmentStitchingContext::StitchResult& stitchedFragment,
            const RenderPassBeginDesc& beginInfo = RenderPassBeginDesc());

        // Construct a "non-metal" RenderPassInstance (useful for compute shader work)
		RenderPassInstance(
			const FrameBufferDesc& layout,
            IteratorRange<const PreregisteredAttachment*> resolvedAttachmentDescs,
			AttachmentPool& attachmentPool);
        ~RenderPassInstance();

        RenderPassInstance();
        RenderPassInstance(RenderPassInstance&& moveFrom) never_throws;
        RenderPassInstance& operator=(RenderPassInstance&& moveFrom) never_throws;

    private:
        std::shared_ptr<Metal::FrameBuffer> _frameBuffer;
        Metal::DeviceContext* _attachedContext;
        AttachmentPool* _attachmentPool;
        AttachmentPool::Reservation _attachmentPoolReservation;
		FrameBufferDesc _layout;
        unsigned _currentSubpassIndex = 0;
        bool _trueRenderPass = false;

        std::vector<std::shared_ptr<IResourceView>> _viewedAttachments;
        std::vector<unsigned> _viewedAttachmentsMap;
    };

////////////////////////////////////////////////////////////////////////////////////////////////////

    /// <summary>Tests to see if the attachment usage of the given fragment can be optimized</summary>
    /// Sometimes of the number of attachments used by a fragment can be reduced by reusing
    /// an existing attachment, instead of defining a new one. The common "ping-pong" rendering
    /// pattern is an example of this (in this pattern we reuse 2 attachments for many subpasses,
    /// instead of defining new attachments for each sub pass.
    ///
    /// If there are cases this like that, this function can detect them. Returns true iff there are
    /// optimizations detected.
    ///
    /// Internally, it calls MergeFragments, and to get some context on the solution it found,
    /// you can look at the "_log" member of the calculated MergeFragmentsResult.
    ///
    /// MergeInOutputs can be used to chain multiple calls to this function by merging in the
    /// outputs from each subsequent fragment into the systemAttachments array.
    bool CanBeSimplified(
        const FrameBufferDescFragment& inputFragment,
        IteratorRange<const PreregisteredAttachment*> systemAttachments,
        const FrameBufferProperties& fbProps);

/*    void MergeInOutputs(
        std::vector<PreregisteredAttachment>& workingSystemAttachments,
        const FrameBufferDescFragment& fragment,
        const FrameBufferProperties& fbProps);

    // bool IsCompatible(const AttachmentDesc& testAttachment, const AttachmentDesc& request, UInt2 dimensions);
*/

}}


