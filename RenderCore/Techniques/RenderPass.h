// Copyright 2016 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../ResourceDesc.h"        // needed for TextureViewDesc constructor
#include "../Types.h"
#include "../FrameBufferDesc.h"
#include "../ResourceUtils.h"           // for ViewPool
#include "../Metal/Forward.h"
#include "../../Math/Vector.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>

namespace RenderCore 
{
    class FrameBufferDesc;
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

    enum class SystemAttachmentFormat
    {
        LDRColor, HDRColor, TargetColor,
        MainDepthStencil, LowDetailDepth,
        ShadowDepth,
        Max
    };

    class AttachmentMatchingRules
    {
    public:
        AttachmentMatchingRules& FixedFormat(Format);
        AttachmentMatchingRules& SystemAttachmentFormat(Techniques::SystemAttachmentFormat);
        AttachmentMatchingRules& RequireBindFlags(BindFlag::BitField);
        AttachmentMatchingRules& MultisamplingMode(bool);
        AttachmentMatchingRules& CopyFormat(uint64_t srcSemantic);

        // avoid changing the members directly -- prefer the accessors above to ensure compatibility
        enum class Flags { FixedFormat=1<<0, SystemFormat=1<<1, MultisamplingMode=1<<2, CopyFormatFromSemantic=1<<3 };
        Format _fixedFormat;
        Techniques::SystemAttachmentFormat _systemFormat;
        bool _multisamplingMode;
        uint64_t _copyFormatSrc;
        uint32_t _flagsSet = 0u;
        BindFlag::BitField _requiredBindFlags = 0u;
    };

    class FrameBufferDescFragment
    {
    public:
        struct DefineAttachmentHelper
        {
            AttachmentName GetAttachmentName() const { return _attachmentName; }
            operator AttachmentName() const { return _attachmentName; }
            DefineAttachmentHelper& Clear();
            DefineAttachmentHelper& Discard();
            DefineAttachmentHelper& NoInitialState();
            DefineAttachmentHelper& InitialState(BindFlag::BitField);
            DefineAttachmentHelper& FinalState(BindFlag::BitField);
            DefineAttachmentHelper& InitialState(LoadStore, BindFlag::BitField);
            DefineAttachmentHelper& FinalState(LoadStore, BindFlag::BitField);
            DefineAttachmentHelper& InitialState(LoadStore);
            DefineAttachmentHelper& FinalState(LoadStore);

            // Matching rules
            DefineAttachmentHelper& FixedFormat(Format);
            DefineAttachmentHelper& SystemAttachmentFormat(Techniques::SystemAttachmentFormat);
            DefineAttachmentHelper& RequireBindFlags(BindFlag::BitField);
            DefineAttachmentHelper& MultisamplingMode(bool);
            DefineAttachmentHelper& CopyFormat(uint64_t srcSemantic);

            FrameBufferDescFragment* _fragment = nullptr;
            AttachmentName _attachmentName = ~0u;
        };
        DefineAttachmentHelper DefineAttachment(uint64_t semantic);

        struct ViewedAttachment : public RenderCore::AttachmentViewDesc { BindFlag::Enum _usage = BindFlag::ShaderResource; };
        struct SubpassDesc : public RenderCore::SubpassDesc
        {
            IteratorRange<const ViewedAttachment*> GetNonFrameBufferAttachmentViews() const { return MakeIteratorRange(_nonfbViews); }
            unsigned AppendNonFrameBufferAttachmentView(AttachmentName name, BindFlag::Enum usage = BindFlag::ShaderResource, TextureViewDesc window = {});
            std::vector<ViewedAttachment> _nonfbViews;
        };
        void AddSubpass(SubpassDesc&& subpass);
        void AddSubpass(RenderCore::SubpassDesc&& subpass);

        struct Attachment;
        IteratorRange<const Attachment*> GetAttachments() const     { return _attachments; }
        IteratorRange<Attachment*> GetAttachments()                 { return MakeIteratorRange(_attachments); }
        IteratorRange<const SubpassDesc*> GetSubpasses() const      { return _subpasses; }
        IteratorRange<SubpassDesc*> GetSubpasses()                  { return MakeIteratorRange(_subpasses); }

        FrameBufferDescFragment();
        ~FrameBufferDescFragment();

        struct Attachment
        {
            uint64_t _semantic;
            AttachmentMatchingRules _matchingRules;
            LoadStore _loadFromPreviousPhase = LoadStore::Retain;
            LoadStore _storeToNextPhase = LoadStore::Retain;
            std::optional<BindFlag::BitField> _initialLayout;       // layouts that are not specfied will have defaults generated from the first and last usage
            std::optional<BindFlag::BitField> _finalLayout;         // 0 can be used for an undefined initial layout

            uint64_t GetInputSemanticBinding() const { return _semantic; }
            uint64_t GetOutputSemanticBinding() const { return _semantic; }
        };
        std::vector<Attachment>     _attachments;
        std::vector<SubpassDesc>    _subpasses;
		PipelineType				_pipelineType = PipelineType::Graphics;

        DefineAttachmentHelper DefineAttachment(const Attachment& attachment);
    };

    struct PreregisteredAttachment
    {
        uint64_t _semantic = 0ull;
        ResourceDesc _desc;
        enum class State { 
            Uninitialized, Initialized, 
            Initialized_StencilUninitialized, Uninitialized_StencilInitialized
        };
        StringSection<> _name;
        State _state = State::Uninitialized;
        BindFlag::BitField _layout = 0;

        uint64_t CalculateHash() const;
        uint64_t CalculateHashResolutionIndependent() const;
    };

    uint64_t HashPreregisteredAttachments(
        IteratorRange<const PreregisteredAttachment*> attachments,
        const FrameBufferProperties& fbProps,
        uint64_t seed = DefaultSeed64);

    uint64_t HashPreregisteredAttachmentsResolutionIndependent(
        IteratorRange<const PreregisteredAttachment*> attachments,
        const FrameBufferProperties& fbProps,
        uint64_t seed = DefaultSeed64);

    struct DoubleBufferAttachment
    {
        uint64_t _yesterdaySemantic;
        uint64_t _todaySemantic;
        BindFlag::BitField _initialLayout;      // ie, layout at the start of "today"
        ClearValue _initialContents;
        ResourceDesc _desc;
    };

    struct AttachmentTransform
    {
        enum Type
        {
            LoadedAndStored, Generated, Consumed, Temporary
        };
        Type _type = Temporary;
        BindFlag::BitField _initialLayout = 0;
        BindFlag::BitField _finalLayout = 0;
    };

    class FragmentStitchingContext
    {
    public:
        void DefineAttachment(
            uint64_t semantic, const ResourceDesc&,
            StringSection<> name,
            PreregisteredAttachment::State state = PreregisteredAttachment::State::Uninitialized, 
            BindFlag::BitField initialLayout = 0);
        void DefineAttachment(const PreregisteredAttachment& attachment);
        void Undefine(uint64_t semantic);

        // Declare that we will need the data from the previous frame for the given attachment
        // The data will be accessable in the attachment semantic+1
        // (eg, if you call DefineDoubleBufferAttachment(AttachmentSemantics::MultisampleDepth), 
        // you can then use AttachmentSemantics::MultisampleDepthPrev)
        void DefineDoubleBufferAttachment(
            uint64_t semantic,
            ClearValue defaultContents,
            BindFlag::BitField initialLayout);

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
        StitchResult TryStitchFrameBufferDesc(const FrameBufferDescFragment& fragment);

        void UpdateAttachments(const StitchResult& res);
        IteratorRange<const PreregisteredAttachment*> GetPreregisteredAttachments() const { return _workingAttachments; }
        Format GetSystemAttachmentFormat(SystemAttachmentFormat) const;

        IteratorRange<const DoubleBufferAttachment*> GetDoubleBufferAttachments() const { return _doubleBufferAttachments; }

        FrameBufferProperties _workingProps;
        Format _systemFormats[(unsigned)SystemAttachmentFormat::Max];

        FragmentStitchingContext(
            IteratorRange<const PreregisteredAttachment*> preregAttachments = {}, 
            const FrameBufferProperties& fbProps = {},
            IteratorRange<const Format*> systemFormats = {});
        ~FragmentStitchingContext();
    private:
        std::vector<PreregisteredAttachment> _workingAttachments;
        std::vector<DoubleBufferAttachment> _doubleBufferAttachments;
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
		const FrameBufferProperties& fbProps,
        IteratorRange<const Format*> systemAttachmentFormats);      // systemAttachmentFormats indexed by SystemAttachmentFormat

////////////////////////////////////////////////////////////////////////////////////////////////////

    class AttachmentReservation;
    struct ReservationFlag
    {
        enum class Enum {};
        using BitField=unsigned; 
    };

    struct AttachmentTransform;
    struct AttachmentBarrier;

    class AttachmentPool
    {
    public:
        const std::shared_ptr<IResource>& GetResource(AttachmentName resName) const;
        const ResourceDesc& GetResourceDesc(AttachmentName resName) const;
        AttachmentName GetNameForResource(IResource&) const;

        auto GetSRV(AttachmentName resName, const TextureViewDesc& window = {}) const -> const std::shared_ptr<IResourceView>&;
        auto GetView(AttachmentName resName, BindFlag::Enum usage, const TextureViewDesc& window = {}) const -> const std::shared_ptr<IResourceView>&;

        AttachmentReservation Reserve(
            IteratorRange<const PreregisteredAttachment*>,
            AttachmentReservation* parentReservation = nullptr,
            ReservationFlag::BitField = 0);

        void ResetActualized();
        std::string GetMetrics() const;

        AttachmentPool(const std::shared_ptr<IDevice>& device);
        ~AttachmentPool();
    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        void AddRef(IteratorRange<const AttachmentName*>, ReservationFlag::BitField flags);
        void Release(IteratorRange<const AttachmentName*>, ReservationFlag::BitField flags);

        friend class AttachmentReservation;
    };

    class AttachmentReservation
    {
    public:
        AttachmentName Bind(uint64_t semantic, std::shared_ptr<IResource> resource, BindFlag::BitField currentLayout);     // set currentLayout to ~0u for never initialized/no state
        AttachmentName Bind(uint64_t semantic, std::shared_ptr<IPresentationChain> presentationChain, const ResourceDesc& resourceDesc, BindFlag::BitField currentLayout);
        void Unbind(const IResource& resource);
        void UpdateAttachments(AttachmentReservation& childReservation, IteratorRange<const AttachmentTransform*> transforms);

        // Bind(uint64_t semantic, IPresentationChain...)   ?

        // AttachmentReservation has it's own indexing for attachments
        // this will be zero-based and agree with the ordering of requests when returned from AttachmentPool::Reserve
        // this can be used to make it compatible with the AttachmentName in a FrameBufferDesc
        const std::shared_ptr<IResource>& GetResource(AttachmentName resName) const;
        ResourceDesc GetResourceDesc(AttachmentName resName) const;
        auto GetSRV(AttachmentName resName, const TextureViewDesc& window = {}) const -> const std::shared_ptr<IResourceView>&;
        auto GetView(AttachmentName resName, BindFlag::Enum usage, const TextureViewDesc& window = {}) const -> const std::shared_ptr<IResourceView>&;

        const std::shared_ptr<IResource>& MapSemanticToResource(uint64_t semantic) const;
        AttachmentName MapSemanticToName(uint64_t semantic) const;

        unsigned GetResourceCount() const { return (unsigned)_entries.size(); }

        void CompleteInitialization(IThreadContext&);
        bool HasPendingCompleteInitialization() const;
        void AutoBarrier(IThreadContext&, IteratorRange<const AttachmentBarrier*>);

        void Absorb(AttachmentReservation&&);

        void DefineDoubleBufferAttachments(IteratorRange<const DoubleBufferAttachment*>);
        void DefineDoubleBufferAttachment(
            uint64_t yesterdaySemantic,
            uint64_t todaySemantic,
            const ResourceDesc& desc,
            ClearValue defaultContents,
            BindFlag::BitField initialLayout);
        AttachmentReservation CaptureDoubleBufferAttachments();

        const AttachmentPool& GetAttachmentPool() const { return *_pool; }

        AttachmentReservation();
        AttachmentReservation(AttachmentPool& pool);
        ~AttachmentReservation();
        AttachmentReservation(AttachmentReservation&&);
        AttachmentReservation& operator=(AttachmentReservation&&);
        AttachmentReservation(const AttachmentReservation&);
        AttachmentReservation& operator=(const AttachmentReservation&);
    private:
        struct Entry
        {
            ResourceDesc _desc;
            unsigned _poolResource = ~0u;
            std::shared_ptr<IResource> _resource;
            std::shared_ptr<IPresentationChain> _presentationChain;
            uint64_t _semantic = ~0ull;
            BindFlag::BitField _currentLayout = (BindFlag::BitField)0;
            std::optional<ClearValue> _pendingClear;
            std::optional<BindFlag::BitField> _pendingSwitchToLayout;
        };
        std::vector<Entry> _entries;                 // candidate for subframe heap
        AttachmentPool* _pool = nullptr;
        ReservationFlag::BitField _reservationFlags = 0;
        mutable ViewPool _viewPool;

        std::vector<DoubleBufferAttachment> _doubleBufferAttachments;

        struct AttachmentToReserve
        {
            AttachmentName _poolName = ~0u;
            std::shared_ptr<IResource> _resource;
            std::shared_ptr<IPresentationChain> _presentationChain;
            uint64_t _semantic;
            std::optional<ClearValue> _pendingClear;
            std::optional<BindFlag::BitField> _currentLayout;
            std::optional<BindFlag::BitField> _pendingSwitchToLayout;
        };
        AttachmentReservation(
            std::vector<AttachmentToReserve>&& reservedAttachments,
            AttachmentPool* pool,
            ReservationFlag::BitField flags);
        void Remove(AttachmentName);
        void AddRefAll();
        void ReleaseAll();
        friend class AttachmentPool;
    };

    struct AttachmentBarrier
    {
        AttachmentName _attachment = ~AttachmentName(0);
        BindFlag::BitField _layout = 0;
        ShaderStage _shaderStage = ShaderStage::Pixel;
    };

    struct RenderPassBeginDesc
    {
        IteratorRange<const ClearValue*>    _clearValues;
        unsigned _frameIdx = 0;
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
        const FrameBufferDesc& GetFrameBufferDesc() const { return *_layout; }
        ViewportDesc GetDefaultViewport() const;
        const AttachmentReservation& GetAttachmentReservation() const { return _attachmentPoolReservation; }

        auto GetInputAttachmentResource(unsigned inputAttachmentSlot) const -> const std::shared_ptr<IResource>&;
        auto GetInputAttachmentView(unsigned inputAttachmentSlot) const -> const std::shared_ptr<IResourceView>&;

		auto GetOutputAttachmentResource(unsigned outputAttachmentSlot) const -> const std::shared_ptr<IResource>&;
	    auto GetOutputAttachmentSRV(unsigned outputAttachmentSlot, const TextureViewDesc& window) const -> const std::shared_ptr<IResourceView>&;

		auto GetDepthStencilAttachmentResource() const -> const std::shared_ptr<IResource>&;

		// The "AttachmentNames" here map onto the names used by the FrameBufferDesc used to initialize this RPI
        auto GetResourceForAttachmentName(AttachmentName resName) const -> const std::shared_ptr<IResource>&;
        auto GetSRVForAttachmentName(AttachmentName resName, const TextureViewDesc& window = {}) const -> const std::shared_ptr<IResourceView>&;

        auto GetNonFrameBufferAttachmentView(unsigned viewedAttachmentSlot) const -> const std::shared_ptr<IResourceView>&;

        void AutoNonFrameBufferBarrier(IteratorRange<const AttachmentBarrier*>);

        // Construct from a fully actualized "FrameBufferDesc" (eg, one generated via a
        // FragmentStitchingContext)
        RenderPassInstance(
            IThreadContext& context,
            const FrameBufferDesc& layout,
            IteratorRange<const PreregisteredAttachment*> fullAttachmentsDescription,
            FrameBufferPool& frameBufferPool,
            AttachmentPool& attachmentPool,
            AttachmentReservation* parentReservation = nullptr,
            const RenderPassBeginDesc& beginInfo = RenderPassBeginDesc());

        // Construct from a fully FrameBufferDescFragment fragment. This will use the
        // stitching context in the ParsingContext to link the frame buffer to the current
        // environment
        RenderPassInstance(
            ParsingContext& parsingContext,
            const FrameBufferDescFragment& layoutFragment,
            const RenderPassBeginDesc& beginInfo = RenderPassBeginDesc());

        RenderPassInstance(
            ParsingContext& parsingContext,
            const FragmentStitchingContext::StitchResult& stitchedFragment,
            const RenderPassBeginDesc& beginInfo = RenderPassBeginDesc());

        // Construct a "non-metal" RenderPassInstance (useful for compute shader work)
        // expects that the input FrameBufferDesc outlives this 
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
        AttachmentReservation _attachmentPoolReservation;
		const FrameBufferDesc* _layout;     // this is expensive to copy, so avoid it when we can
        unsigned _currentSubpassIndex = 0;
        ParsingContext* _attachedParsingContext = nullptr;
        bool _trueRenderPass = false;

        std::vector<std::pair<std::shared_ptr<IResourceView>, unsigned>> _viewedAttachments;         // good candidate for subframe heap
        std::vector<unsigned> _viewedAttachmentsMap;                            // good candidate for subframe heap
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
        const FrameBufferProperties& fbProps = {},
        IteratorRange<const Format*> systemFormats = {});

    std::vector<Format> CalculateDefaultSystemFormats(IDevice&);

    inline auto FragmentStitchingContext::TryStitchFrameBufferDesc(const FrameBufferDescFragment& fragment) -> StitchResult
    {
        return TryStitchFrameBufferDesc({&fragment, &fragment+1});
    }

}}

