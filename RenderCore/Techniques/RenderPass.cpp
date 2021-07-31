// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RenderPass.h"
#include "ParsingContext.h"
#include "Techniques.h"
#include "CommonBindings.h"     // (for semantic dehash)
#include "../Metal/FrameBuffer.h"
#include "../Metal/TextureView.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/ObjectFactory.h"
#include "../Metal/State.h"
#include "../Metal/Resource.h"
#include "../Format.h"
#include "../ResourceUtils.h"
#include "../IThreadContext.h"
#include "../../OSServices/Log.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../Utility/StreamUtils.h"
#include <cmath>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <set>

namespace RenderCore
{
    static std::ostream& operator<<(std::ostream& str, const AttachmentDesc& attachment)
    {
        str << "AttachmentDesc { "
            #if defined(_DEBUG)
                << (!attachment._name.empty()?attachment._name:std::string("<<no name>>")) << ", "
            #endif
            << AsString(attachment._format)
            << ", 0x" << std::hex << attachment._flags << std::dec
            << ", " << AsString(attachment._loadFromPreviousPhase) 
            << ", " << AsString(attachment._storeToNextPhase)
            << ", 0x" << std::hex << attachment._initialLayout << std::dec
            << ", 0x" << std::hex << attachment._finalLayout << std::dec
            << " }";
        return str;
    }

    static std::ostream& operator<<(std::ostream& str, const SubpassDesc& subpass)
    {
        str << "SubpassDesc { "
            #if defined(_DEBUG)
                << (!subpass._name.empty()?subpass._name:std::string("<<no name>>")) << ", "
            #endif
            << "outputs [";
        for (unsigned c=0; c<subpass.GetOutputs().size(); ++c) { if (c!=0) str << ", "; str << subpass.GetOutputs()[c]._resourceName; }
        str << "], DepthStencil: ";
        if (subpass.GetDepthStencil()._resourceName != ~0u) { str << subpass.GetDepthStencil()._resourceName; } else { str << "<<none>>"; }
        str << ", inputs [";
        for (unsigned c=0; c<subpass.GetInputs().size(); ++c) { if (c!=0) str << ", "; str << subpass.GetInputs()[c]._resourceName; }
        /*str << "], preserve [";
        for (unsigned c=0; c<subpass._preserve.size(); ++c) { if (c!=0) str << ", "; str << subpass._preserve[c]._resourceName; }*/
        str << "], resolve [";
        for (unsigned c=0; c<subpass.GetResolveOutputs().size(); ++c) { if (c!=0) str << ", "; str << subpass.GetResolveOutputs()[c]._resourceName; }
        str << "], resolveDepthStencil: ";
        if (subpass.GetResolveDepthStencil()._resourceName != ~0u) { str << subpass.GetResolveDepthStencil()._resourceName << " }"; }
        else { str << "<<none>> }"; }
        return str;
    }

    static std::ostream& operator<<(std::ostream& str, const TextureDesc& textureDesc)
    {
        switch (textureDesc._dimensionality) {
            case TextureDesc::Dimensionality::T1D: str << textureDesc._width; break;
            case TextureDesc::Dimensionality::T2D: str << textureDesc._width << "x" << textureDesc._height; break;
            case TextureDesc::Dimensionality::T3D: str << textureDesc._width << "x" << textureDesc._height << "x" << textureDesc._depth; break;
            case TextureDesc::Dimensionality::CubeMap: str << textureDesc._width << "x" << textureDesc._height << " cube"; break;
            default: str << "<<unknown dimensionality>>";
        }
        str << ", " << AsString(textureDesc._format)
            << ", " << (unsigned)textureDesc._mipCount
            << ", " << (unsigned)textureDesc._arrayCount
            << ", " << (unsigned)textureDesc._samples._sampleCount
            << ", " << (unsigned)textureDesc._samples._samplingQuality;
        return str;
    }

    static std::ostream& operator<<(std::ostream& str, const ResourceDesc& desc)
    {
        str << "ResourceDesc { ";
        if (desc._type == ResourceDesc::Type::Texture) {
            str << "[Texture] " << desc._textureDesc;
        } else {
            str << "[Buffer] " << Utility::ByteCount(desc._linearBufferDesc._sizeInBytes);
        }
        str << ", 0x" << std::hex << desc._bindFlags << std::dec;
        return str;
    }
}

namespace RenderCore { namespace Techniques
{
    static std::shared_ptr<IResource> s_nullResourcePtr;
    static std::shared_ptr<IResourceView> s_nullResourceViewPtr;
    static BindFlag::BitField CalculateBindFlags(const FrameBufferDescFragment& fragment, unsigned attachmentName);
    struct AttachmentSemantic { uint64_t _value = 0; };
    static std::ostream& operator<<(std::ostream& str, AttachmentSemantic semantic)
    {
        auto dehash = AttachmentSemantics::TryDehash(semantic._value);
        if (dehash) str << dehash;
        else str << "0x" << std::hex << semantic._value << std::dec;
        return str;
    }

    static std::ostream& operator<<(std::ostream& str, const FrameBufferDescFragment::SubpassDesc& subpass)
    {
        str << (const RenderCore::SubpassDesc&)subpass;
        str << ", viewed [";
        for (unsigned c=0; c<subpass.GetViews().size(); ++c) { if (c!=0) str << ", "; str << subpass.GetViews()[c]._resourceName; }
        str << "]";
        return str;
    }

    AttachmentName FrameBufferDescFragment::DefineAttachment(
        uint64_t semantic, 
        LoadStore loadOp, LoadStore storeOp,
        BindFlag::BitField initialLayout, BindFlag::BitField finalLayout)
    {
        auto name = (AttachmentName)_attachments.size();
        Attachment attachment;
        attachment._inputSemanticBinding = attachment._outputSemanticBinding = semantic;
        attachment._desc._loadFromPreviousPhase = loadOp;
        attachment._desc._storeToNextPhase = storeOp;
        attachment._desc._initialLayout = initialLayout;
        attachment._desc._finalLayout = finalLayout;
        _attachments.push_back(attachment);
        return name;
    }

    AttachmentName FrameBufferDescFragment::DefineAttachment(
        uint64_t semantic,
        const AttachmentDesc& request)
    {
        auto name = (AttachmentName)_attachments.size();
        Attachment attachment;
        attachment._inputSemanticBinding = attachment._outputSemanticBinding = semantic;
        attachment._desc = request;
        _attachments.push_back(attachment);
        return name;
    }

    void FrameBufferDescFragment::AddSubpass(SubpassDesc&& subpass)
    {
        _subpasses.emplace_back(std::move(subpass));
    }

    void FrameBufferDescFragment::AddSubpass(RenderCore::SubpassDesc&& subpass)
    {
        _subpasses.emplace_back(SubpassDesc{std::move(subpass)});
    }

    FrameBufferDescFragment::FrameBufferDescFragment() {}
    FrameBufferDescFragment::~FrameBufferDescFragment() {}

    void FrameBufferDescFragment::SubpassDesc::AppendNonFrameBufferAttachmentView(AttachmentName name, BindFlag::Enum usage, TextureViewDesc window)
    {
        ViewedAttachment view;
        view._resourceName = name;
        view._window = window;
        view._usage = usage;
        _views.push_back(view);
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    class NamedAttachmentsWrapper : public INamedAttachments
    {
    public:
		virtual std::shared_ptr<IResourceView> GetResourceView(
            AttachmentName resName,
            BindFlag::Enum bindFlag, TextureViewDesc viewDesc,
            const AttachmentDesc& requestDesc, const FrameBufferProperties& props) const override;

        NamedAttachmentsWrapper(
            AttachmentPool& pool,
            IteratorRange<const AttachmentName*> poolMapping);
        ~NamedAttachmentsWrapper();
    private:
        AttachmentPool* _pool;
        IteratorRange<const AttachmentName*> _poolMapping;
    };

    std::shared_ptr<IResourceView> NamedAttachmentsWrapper::GetResourceView(
        AttachmentName resName,
        BindFlag::Enum bindFlag, TextureViewDesc viewDesc,
        const AttachmentDesc& requestDesc, const FrameBufferProperties& props) const
    {
        assert(resName < _poolMapping.size());
        auto view = _pool->GetView(_poolMapping[resName], bindFlag, viewDesc);

        #if defined(_DEBUG)
            auto* resource = view->GetResource().get();
            // Validate that the "desc" for the returned resource matches what the caller was requesting
            auto resultDesc = resource->GetDesc();
            assert(requestDesc._format == Format(0) || AsTypelessFormat(requestDesc._format) == AsTypelessFormat(resultDesc._textureDesc._format));
            assert((requestDesc._finalLayout & resultDesc._bindFlags) == requestDesc._finalLayout);     // if you hit this it means that the final layout type was not one of the bind flags that the resource was initially created with
            assert((requestDesc._initialLayout & resultDesc._bindFlags) == requestDesc._initialLayout);
        #endif

        return view;
    }

    NamedAttachmentsWrapper::NamedAttachmentsWrapper(
        AttachmentPool& pool,
        IteratorRange<const AttachmentName*> poolMapping)
    : _pool(&pool)
    , _poolMapping(poolMapping) {}
    NamedAttachmentsWrapper::~NamedAttachmentsWrapper() {}

////////////////////////////////////////////////////////////////////////////////////////////////////

    class FrameBufferPool
    {
    public:
        class Result
        {
        public:
            std::shared_ptr<Metal::FrameBuffer> _frameBuffer;
            AttachmentPool::Reservation _poolReservation;
            const FrameBufferDesc* _completedDesc;
        };
        Result BuildFrameBuffer(
            Metal::ObjectFactory& factory,
            const FrameBufferDesc& desc,
            IteratorRange<const PreregisteredAttachment*> resolvedAttachmentDescs,
            AttachmentPool& attachmentPool);

        void Reset();

        FrameBufferPool();
        ~FrameBufferPool();
    private:
        class Entry
        {
        public:
            uint64_t _hash = ~0ull;
            unsigned _tickId = 0;
            std::shared_ptr<Metal::FrameBuffer> _fb;
            std::vector<AttachmentName> _poolAttachmentsRemapping;
            FrameBufferDesc _completedDesc;
        };
        Entry _entries[10];
        unsigned _currentTickId = 0;

        void IncreaseTickId();
    };

    void FrameBufferPool::IncreaseTickId()
    {
        // look for old FBs, and evict; then just increase the tick id
        const unsigned evictionRange = 10;
        for (auto&e:_entries)
            if ((e._tickId + evictionRange) < _currentTickId) {
                e._fb.reset();
				e._hash = ~0ull;
			}
        ++_currentTickId;
    }

    auto FrameBufferPool::BuildFrameBuffer(
        Metal::ObjectFactory& factory,
        const FrameBufferDesc& desc,
        IteratorRange<const PreregisteredAttachment*> resolvedAttachmentDescs,
        AttachmentPool& attachmentPool) -> Result
    {    
        auto poolAttachments = attachmentPool.Reserve(resolvedAttachmentDescs);
		assert(poolAttachments.GetResourceIds().size() == desc.GetAttachments().size());

        std::vector<AttachmentDesc> adjustedAttachments;
        adjustedAttachments.reserve(desc.GetAttachments().size());
        Result result;

        uint64_t hashValue = DefaultSeed64;
        for (unsigned c=0; c<desc.GetAttachments().size(); ++c) {
            auto* matchedAttachment = attachmentPool.GetResource(poolAttachments.GetResourceIds()[c]).get();
            assert(matchedAttachment);
            hashValue = HashCombine(matchedAttachment->GetGUID(), hashValue);

            // The attachment descriptions in the input FrameBufferDesc may not be 100% complete, however
            // in the process of matching them to the attachment pool, we will have filled in any missing
            // info (ie, either from the preregistered attachments or from existing attachments in the pool)
            // We must merge this updated information back into the FrameBufferDesc.
            // This also has the effect of normalizing the attachment desc information for the hash value,
            // which would help cases where functionality identical information produces different hash value
            auto resDesc = matchedAttachment->GetDesc();
            AttachmentDesc completeAttachmentDesc = desc.GetAttachments()[c];
            completeAttachmentDesc._format = resDesc._textureDesc._format;
            adjustedAttachments.push_back({completeAttachmentDesc});
        }

        FrameBufferDesc adjustedDesc(
            std::move(adjustedAttachments),
            {desc.GetSubpasses().begin(), desc.GetSubpasses().end()},
            desc.GetProperties()); 
        hashValue = HashCombine(adjustedDesc.GetHash(), hashValue);
        assert(hashValue != ~0ull);     // using ~0ull has a sentinel, so this will cause some problems

        unsigned earliestEntry = 0;
        unsigned tickIdOfEarliestEntry = ~0u;
        for (unsigned c=0; c<dimof(_entries); ++c) {
            if (_entries[c]._hash == hashValue) {
                _entries[c]._tickId = _currentTickId;
				_entries[c]._poolAttachmentsRemapping = {poolAttachments.GetResourceIds().begin(), poolAttachments.GetResourceIds().end()};	// update the mapping, because attachments map have moved
                IncreaseTickId();
                assert(_entries[c]._fb != nullptr);
                return {
                    _entries[c]._fb,
                    poolAttachments,
                    &_entries[c]._completedDesc
                };
            }
            if (_entries[c]._tickId < tickIdOfEarliestEntry) {
                tickIdOfEarliestEntry = _entries[c]._tickId;
                earliestEntry = c;
            }
        }

        // Can't find it; we're just going to overwrite the oldest entry with a new one
        assert(earliestEntry < dimof(_entries));
//        if (_pimpl->_entries[earliestEntry]._fb) {
//            Log(Warning) << "Overwriting tail in FrameBufferPool(). There may be too many different framebuffers required from the same pool" << std::endl;
//        }

        NamedAttachmentsWrapper namedAttachments(attachmentPool, poolAttachments.GetResourceIds());
        assert(adjustedDesc.GetSubpasses().size());
        _entries[earliestEntry]._fb = std::make_shared<Metal::FrameBuffer>(
            factory,
            adjustedDesc, namedAttachments);
        _entries[earliestEntry]._tickId = _currentTickId;
        _entries[earliestEntry]._hash = hashValue;
        _entries[earliestEntry]._poolAttachmentsRemapping = {poolAttachments.GetResourceIds().begin(), poolAttachments.GetResourceIds().end()};
        _entries[earliestEntry]._completedDesc = std::move(adjustedDesc);
        IncreaseTickId();
        result._frameBuffer = _entries[earliestEntry]._fb;
        result._poolReservation = std::move(poolAttachments);
        result._completedDesc = &_entries[earliestEntry]._completedDesc;
        return result;
    }

    void FrameBufferPool::Reset()
    {
        for (unsigned c=0; c<dimof(_entries); ++c)
            _entries[c] = {};
        _currentTickId = 0;
    }

    FrameBufferPool::FrameBufferPool()
    {
    }

    FrameBufferPool::~FrameBufferPool()
    {}

    std::shared_ptr<FrameBufferPool> CreateFrameBufferPool()
    {
        return std::make_shared<FrameBufferPool>();
    }

    void ResetFrameBufferPool(FrameBufferPool& fbPool)
    {
        fbPool.Reset();
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

	void RenderPassInstance::NextSubpass()
    {
		if (_trueRenderPass) {
			assert(_frameBuffer && _attachedContext);
			_attachedContext->BeginNextSubpass(*_frameBuffer);
		}
        #if defined(_DEBUG)
            if (_attachedContext) {
                _attachedContext->EndLabel();
                _attachedContext->BeginLabel(_layout.GetSubpasses()[_currentSubpassIndex+1]._name.empty() ? "<<unnnamed subpass>>" : _layout.GetSubpasses()[_currentSubpassIndex+1]._name.c_str());
            }
        #endif
        ++_currentSubpassIndex;
    }

    void RenderPassInstance::End()
    {
		if (_trueRenderPass) {
            assert(_attachedContext);
            _attachedContext->EndRenderPass();
            #if defined(_DEBUG)
                if (_attachedContext)
                    _attachedContext->EndLabel();
            #endif
			_attachedContext = nullptr;
            _trueRenderPass = false;
		} else {
            #if defined(_DEBUG)
                if (_attachedContext)
                    _attachedContext->EndLabel();
            #endif
        }
    }
    
    unsigned RenderPassInstance::GetCurrentSubpassIndex() const
    {
		if (_attachedContext && _trueRenderPass)
			assert(_currentSubpassIndex == _attachedContext->GetCurrentSubpassIndex());
		return _currentSubpassIndex;
    }

    ViewportDesc RenderPassInstance::GetDefaultViewport() const
    {
        return _frameBuffer->GetDefaultViewport();
    }

    auto RenderPassInstance::GetResourceForAttachmentName(AttachmentName resName) const -> const std::shared_ptr<IResource>&
    {
        assert(_attachmentPool);
        if (resName < _attachmentPoolReservation.GetResourceIds().size())
            return _attachmentPool->GetResource(_attachmentPoolReservation.GetResourceIds()[resName]);
        return s_nullResourcePtr;
    }

    auto RenderPassInstance::GetSRVForAttachmentName(AttachmentName resName, const TextureViewDesc& window) const -> const std::shared_ptr<IResourceView>&
    {
        assert(_attachmentPool);
        if (resName < _attachmentPoolReservation.GetResourceIds().size())
            return _attachmentPool->GetSRV(_attachmentPoolReservation.GetResourceIds()[resName], window);
        return s_nullResourceViewPtr;
    }

    auto RenderPassInstance::GetInputAttachmentResource(unsigned inputAttachmentSlot) const -> const std::shared_ptr<IResource>&
	{
		const auto& subPass = _layout.GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetInputs()[inputAttachmentSlot]._resourceName;
		assert(_attachmentPool);
        if (resName < _attachmentPoolReservation.GetResourceIds().size())
            return _attachmentPool->GetResource(_attachmentPoolReservation.GetResourceIds()[resName]);
        return s_nullResourcePtr;
	}

    auto RenderPassInstance::GetInputAttachmentView(unsigned inputAttachmentSlot) const -> const std::shared_ptr<IResourceView>&
	{
		const auto& subPass = _layout.GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetInputs()[inputAttachmentSlot]._resourceName;
		assert(_attachmentPool);
        if (resName < _attachmentPoolReservation.GetResourceIds().size())
            return _attachmentPool->GetView(_attachmentPoolReservation.GetResourceIds()[resName], BindFlag::InputAttachment, subPass.GetInputs()[inputAttachmentSlot]._window);
        return s_nullResourceViewPtr;
	}
	
	auto RenderPassInstance::GetOutputAttachmentResource(unsigned outputAttachmentSlot) const -> const std::shared_ptr<IResource>&
	{
		const auto& subPass = _layout.GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetOutputs()[outputAttachmentSlot]._resourceName;
		assert(_attachmentPool);
        if (resName < _attachmentPoolReservation.GetResourceIds().size())
            return _attachmentPool->GetResource(_attachmentPoolReservation.GetResourceIds()[resName]);
        return s_nullResourcePtr;
	}
	
	auto RenderPassInstance::GetOutputAttachmentSRV(unsigned outputAttachmentSlot, const TextureViewDesc& window) const -> const std::shared_ptr<IResourceView>&
	{
		const auto& subPass = _layout.GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetOutputs()[outputAttachmentSlot]._resourceName;
		assert(_attachmentPool);
        if (resName < _attachmentPoolReservation.GetResourceIds().size())
            return _attachmentPool->GetSRV(_attachmentPoolReservation.GetResourceIds()[resName], window);
        return s_nullResourceViewPtr;
	}

	auto RenderPassInstance::GetDepthStencilAttachmentSRV(const TextureViewDesc& window) const -> const std::shared_ptr<IResourceView>&
	{
		const auto& subPass = _layout.GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetDepthStencil()._resourceName;
		assert(_attachmentPool);
        if (resName < _attachmentPoolReservation.GetResourceIds().size())
            return _attachmentPool->GetSRV(_attachmentPoolReservation.GetResourceIds()[resName], window);
        return s_nullResourceViewPtr;
	}

	auto RenderPassInstance::GetDepthStencilAttachmentResource() const -> const std::shared_ptr<IResource>&
	{
		const auto& subPass = _layout.GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetDepthStencil()._resourceName;
		assert(_attachmentPool);
        if (resName < _attachmentPoolReservation.GetResourceIds().size())
            return _attachmentPool->GetResource(_attachmentPoolReservation.GetResourceIds()[resName]);
        return s_nullResourcePtr;
	}

    auto RenderPassInstance::GetNonFrameBufferAttachmentView(unsigned viewedAttachmentSlot) const -> const std::shared_ptr<IResourceView>&
    {
        auto spIdx = GetCurrentSubpassIndex();
        assert((spIdx+1) < _viewedAttachmentsMap.size());
        auto base = _viewedAttachmentsMap[spIdx];
        assert((_viewedAttachmentsMap[spIdx+1] - base) > viewedAttachmentSlot);     // if you hit this, it means "viewedAttachmentSlot" is out of bounds for the current subpass
        return _viewedAttachments[base+viewedAttachmentSlot];
    }

    static bool HasClear(LoadStore ls) 
	{
		return ls == LoadStore::Clear || ls == LoadStore::DontCare_StencilClear || ls == LoadStore::Retain_StencilClear || ls == LoadStore::Clear_StencilDontCare || ls == LoadStore::Clear_StencilRetain;
	}
	
    RenderPassInstance::RenderPassInstance(
        IThreadContext& context,
        const FrameBufferDesc& layout,
        IteratorRange<const PreregisteredAttachment*> fullAttachmentsDescription,
        FrameBufferPool& frameBufferPool,
        AttachmentPool& attachmentPool,
        const RenderPassBeginDesc& beginInfo)
    {
        _attachedContext = Metal::DeviceContext::Get(context).get();

        auto fb = frameBufferPool.BuildFrameBuffer(
            Metal::GetObjectFactory(*context.GetDevice()),
            layout, fullAttachmentsDescription, attachmentPool);
        fb._poolReservation.CompleteInitialization(context);

        _frameBuffer = std::move(fb._frameBuffer);
        _attachmentPoolReservation = std::move(fb._poolReservation);
        _attachmentPool = &attachmentPool;
        _layout = *fb._completedDesc;
        // todo -- we might need to pass offset & extent parameters to BeginRenderPass
        // this could be derived from _attachmentPool->GetFrameBufferProperties()?
        _trueRenderPass = true;

        #if defined(_DEBUG)
            _attachedContext->BeginLabel(_layout.GetSubpasses()[0]._name.empty() ? "<<unnnamed subpass>>" : _layout.GetSubpasses()[0]._name.c_str());
        #endif
        _attachedContext->BeginRenderPass(*_frameBuffer, beginInfo._clearValues);
    }

    RenderPassInstance::RenderPassInstance(
        IThreadContext& context,
        ParsingContext& parsingContext,
        const FragmentStitchingContext::StitchResult& stitchedFragment,
        const RenderPassBeginDesc& beginInfo)
    {
        _attachedContext = nullptr;
        _attachmentPool = nullptr;
        _trueRenderPass = false;

        auto& stitchContext = parsingContext.GetFragmentStitchingContext();
        if (stitchedFragment._pipelineType == PipelineType::Graphics) {
            *this = RenderPassInstance {
                context, stitchedFragment._fbDesc, stitchedFragment._fullAttachmentDescriptions,
                *parsingContext.GetTechniqueContext()._frameBufferPool,
                *parsingContext.GetTechniqueContext()._attachmentPool,
                beginInfo };
            parsingContext.GetViewport() = _frameBuffer->GetDefaultViewport();
        } else {
            auto& attachmentPool = *parsingContext.GetTechniqueContext()._attachmentPool;
            _attachmentPoolReservation = attachmentPool.Reserve(stitchedFragment._fullAttachmentDescriptions);
            _attachmentPool = &attachmentPool;
            _attachmentPoolReservation.CompleteInitialization(context);
            _layout = stitchedFragment._fbDesc;
            // clear not supported in this mode
            for (const auto& a:_layout.GetAttachments())
                assert(!HasClear(a._loadFromPreviousPhase));

            _attachedContext = Metal::DeviceContext::Get(context).get();
            #if defined(_DEBUG)
                _attachedContext->BeginLabel(_layout.GetSubpasses()[0]._name.empty() ? "<<unnnamed subpass>>" : _layout.GetSubpasses()[0]._name.c_str());
            #endif
        }

        // Update the parsing context with the changes to attachments
        stitchContext.UpdateAttachments(stitchedFragment);
        for (unsigned aIdx=0; aIdx<stitchedFragment._attachmentTransforms.size(); ++aIdx) {
            auto semantic = stitchedFragment._fullAttachmentDescriptions[aIdx]._semantic;
            if (!semantic) continue;
            switch (stitchedFragment._attachmentTransforms[aIdx]._type) {
            case FragmentStitchingContext::AttachmentTransform::Preserved:
            case FragmentStitchingContext::AttachmentTransform::Temporary:
            case FragmentStitchingContext::AttachmentTransform::Written:
                break;
            case FragmentStitchingContext::AttachmentTransform::Generated:
                parsingContext.GetTechniqueContext()._attachmentPool->Bind(semantic, GetResourceForAttachmentName(aIdx));
                break;
            case FragmentStitchingContext::AttachmentTransform::Consumed:
                parsingContext.GetTechniqueContext()._attachmentPool->Unbind(semantic);
                break;
            }
        }

        _viewedAttachmentsMap = stitchedFragment._viewedAttachmentsMap;
        _viewedAttachments.reserve(stitchedFragment._viewedAttachments.size());
        for (const auto&view:stitchedFragment._viewedAttachments)
            _viewedAttachments.push_back(_attachmentPool->GetView(_attachmentPoolReservation.GetResourceIds()[view._resourceName], view._usage, view._window));
    }

    RenderPassInstance::RenderPassInstance(
        IThreadContext& context,
        ParsingContext& parsingContext,
        const FrameBufferDescFragment& layout,
        const RenderPassBeginDesc& beginInfo)
    {
        _attachedContext = nullptr;
        _attachmentPool = nullptr;
        _trueRenderPass = false;
        auto& stitchContext = parsingContext.GetFragmentStitchingContext();
        auto stitchResult = stitchContext.TryStitchFrameBufferDesc(MakeIteratorRange(&layout, &layout+1));
        *this = RenderPassInstance { context, parsingContext, stitchResult, beginInfo };
    }

	RenderPassInstance::RenderPassInstance(
        const FrameBufferDesc& layout,
        IteratorRange<const PreregisteredAttachment*> resolvedAttachmentDescs,
        AttachmentPool& attachmentPool)
	: _layout(layout)
    {
		// This constructs a kind of "non-metal" RenderPassInstance
		// It allows us to use the RenderPassInstance infrastructure (for example, for remapping attachment requests)
		// without actually constructing a underlying metal renderpass.
		// This is used with compute pipelines sometimes -- since in Vulkan, those have some similarities with
		// graphics pipelines, but are incompatible with the vulkan render passes
		_attachedContext = nullptr;
        _trueRenderPass = false;
		_attachmentPoolReservation = attachmentPool.Reserve(resolvedAttachmentDescs);
		_attachmentPool = &attachmentPool;
        assert(!_attachmentPoolReservation.HasPendingCompleteInitialization());
	}
    
    RenderPassInstance::~RenderPassInstance() 
    {
        End();
    }

    RenderPassInstance::RenderPassInstance(RenderPassInstance&& moveFrom) never_throws
    : _frameBuffer(std::move(moveFrom._frameBuffer))
    , _attachedContext(moveFrom._attachedContext)
    , _attachmentPool(moveFrom._attachmentPool)
    , _attachmentPoolReservation(std::move(moveFrom._attachmentPoolReservation))
	, _layout(std::move(moveFrom._layout))
    , _viewedAttachments(std::move(moveFrom._viewedAttachments))
    , _viewedAttachmentsMap(std::move(moveFrom._viewedAttachmentsMap))
    , _currentSubpassIndex(moveFrom._currentSubpassIndex)
    , _trueRenderPass(moveFrom._trueRenderPass)
    {
        moveFrom._attachedContext = nullptr;
        moveFrom._attachmentPool = nullptr;
        moveFrom._currentSubpassIndex = 0;
        moveFrom._trueRenderPass = false;
    }

    RenderPassInstance& RenderPassInstance::operator=(RenderPassInstance&& moveFrom) never_throws
    {
        End();
        _frameBuffer = std::move(moveFrom._frameBuffer);
        _attachedContext = moveFrom._attachedContext;
        _attachmentPool = moveFrom._attachmentPool;
        _attachmentPoolReservation = std::move(moveFrom._attachmentPoolReservation);
        moveFrom._attachedContext = nullptr;
        moveFrom._attachmentPool = nullptr;
		_layout = std::move(moveFrom._layout);
        _viewedAttachments = std::move(moveFrom._viewedAttachments);
        _viewedAttachmentsMap = std::move(moveFrom._viewedAttachmentsMap);
        _currentSubpassIndex = moveFrom._currentSubpassIndex;
        moveFrom._currentSubpassIndex = 0;
        _trueRenderPass = moveFrom._trueRenderPass;
        moveFrom._trueRenderPass = false;
        return *this;
    }

    RenderPassInstance::RenderPassInstance()
    {
        _attachedContext = nullptr;
        _attachmentPool = nullptr;
        _trueRenderPass = false;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class AttachmentPool::Pimpl
    {
    public:
        struct Attachment
        {
            IResourcePtr            _resource;
            ResourceDesc            _desc;
            unsigned                _lockCount = 0;
            bool                    _pendingCompleteInitialization = true;
        };
        std::vector<Attachment>     _attachments;

        struct SemanticAttachment : public Attachment
        {
            uint64_t        _semantic;
        };
        std::vector<SemanticAttachment>    _semanticAttachments;

        ViewPool                    _srvPool;
        std::shared_ptr<IDevice>    _device;

        bool BuildAttachment(AttachmentName attach);
    };

    bool AttachmentPool::Pimpl::BuildAttachment(AttachmentName attachName)
    {
        Attachment* attach = nullptr;
        if (attachName & (1u<<31u)) {
            auto semanticAttachIdx = attachName & ~(1u<<31u);
            attach = &_semanticAttachments[semanticAttachIdx];
        } else {
            attach = &_attachments[attachName];
        }
        assert(attach);
        if (!attach) return false;

        assert(attach->_desc._type == ResourceDesc::Type::Texture);
        assert(attach->_desc._textureDesc._width > 0);
        assert(attach->_desc._textureDesc._height > 0);
        assert(attach->_desc._textureDesc._depth > 0);
        attach->_resource = _device->CreateResource(attach->_desc);
        return attach->_resource != nullptr;
    }

    auto AttachmentPool::GetResource(AttachmentName attachName) const -> const std::shared_ptr<IResource>&
    {
        Pimpl::Attachment* attach = nullptr;
        if (attachName & (1u<<31u)) {
            auto semanticAttachIdx = attachName & ~(1u<<31u);
            if (semanticAttachIdx >= _pimpl->_semanticAttachments.size()) return s_nullResourcePtr;
            attach = &_pimpl->_semanticAttachments[semanticAttachIdx];
        } else {
            if (attachName >= _pimpl->_attachments.size()) return s_nullResourcePtr;
            attach = &_pimpl->_attachments[attachName];
        }
        assert(attach);
        if (attach->_resource)
            return attach->_resource;
            
        _pimpl->BuildAttachment(attachName);
        return attach->_resource;
	}

    static TextureViewDesc CompleteTextureViewDesc(const TextureViewDesc& viewDesc, TextureViewDesc::Aspect defaultAspect)
	{
		TextureViewDesc result = viewDesc;
		if (result._format._aspect == TextureViewDesc::Aspect::UndefinedAspect)
			result._format._aspect = defaultAspect;
		return result;
	}

	const std::shared_ptr<IResourceView>& AttachmentPool::GetSRV(AttachmentName attachName, const TextureViewDesc& window) const
	{
        return GetView(attachName, BindFlag::ShaderResource, window);
	}

    const std::shared_ptr<IResourceView>& AttachmentPool::GetView(AttachmentName attachName, BindFlag::Enum usage, const TextureViewDesc& window) const
    {
        static std::shared_ptr<IResourceView> dummy;
        Pimpl::Attachment* attach = nullptr;
        if (attachName & (1u<<31u)) {
            auto semanticAttachIdx = attachName & ~(1u<<31u);
            if (semanticAttachIdx >= _pimpl->_semanticAttachments.size()) return dummy;
            attach = &_pimpl->_semanticAttachments[semanticAttachIdx];
        } else {
            if (attachName >= _pimpl->_attachments.size()) return dummy;
            attach = &_pimpl->_attachments[attachName];
        }
        assert(attach);
        if (!attach->_resource)
            _pimpl->BuildAttachment(attachName);
		assert(attach->_resource);
        return _pimpl->_srvPool.GetTextureView(attach->_resource, usage, window);
		/*auto defaultAspect = TextureViewDesc::Aspect::ColorLinear;
		auto formatComponents = GetComponents(attach->_desc._textureDesc._format);
		if (formatComponents == FormatComponents::Depth || formatComponents == FormatComponents::DepthStencil) {
			defaultAspect = TextureViewDesc::Aspect::Depth;	// can only choose depth or stencil -- so DepthStencil defaults to Depth
		} else if (formatComponents == FormatComponents::Stencil) {
			defaultAspect = TextureViewDesc::Aspect::Stencil;
		}
		auto completeView = CompleteTextureViewDesc(window, defaultAspect);
		return _pimpl->_srvPool.GetTextureView(attach->_resource, usage, completeView);*/
    }

    static unsigned GetArrayCount(unsigned arrayCount) { return (arrayCount == 0) ? 1 : arrayCount; }

    static bool MatchRequest(const ResourceDesc& preregisteredDesc, const ResourceDesc& concreteObjectDesc)
    {
        assert(preregisteredDesc._type == ResourceDesc::Type::Texture && concreteObjectDesc._type == ResourceDesc::Type::Texture);
        return
            GetArrayCount(preregisteredDesc._textureDesc._arrayCount) == GetArrayCount(concreteObjectDesc._textureDesc._arrayCount)
            && (AsTypelessFormat(preregisteredDesc._textureDesc._format) == AsTypelessFormat(concreteObjectDesc._textureDesc._format) || preregisteredDesc._textureDesc._format == Format::Unknown)
            && preregisteredDesc._textureDesc._width == concreteObjectDesc._textureDesc._width
            && preregisteredDesc._textureDesc._height == concreteObjectDesc._textureDesc._height
            && preregisteredDesc._textureDesc._samples == concreteObjectDesc._textureDesc._samples
            && (concreteObjectDesc._bindFlags & preregisteredDesc._bindFlags) == preregisteredDesc._bindFlags
            ;
    }

    auto AttachmentPool::Reserve(
        IteratorRange<const PreregisteredAttachment*> attachmentRequests,
        ReservationFlag::BitField flags) -> Reservation
    {
        std::vector<bool> consumed(_pimpl->_attachments.size(), false);
        std::vector<bool> consumedSemantic(_pimpl->_semanticAttachments.size(), false);

        // Treat any attachments that are bound to semantic values as "consumed" already.
        // In other words, we can't give these attachments to requests without a semantic,
        // or using another semantic.
        for (unsigned c=0; c<_pimpl->_attachments.size(); ++c) {
            consumed[c] = _pimpl->_attachments[c]._lockCount > 0;
            for (unsigned s=0; s<_pimpl->_semanticAttachments.size(); ++s) {
                if (_pimpl->_semanticAttachments[s]._resource == _pimpl->_attachments[c]._resource) {
                    consumed[c] = true;
                    consumedSemantic[s] = _pimpl->_attachments[c]._lockCount > 0;
                    break;
                }
            }
        }

        std::vector<AttachmentName> selectedAttachments;
        for (const auto& request:attachmentRequests) {
            auto requestDesc = request._desc;

            // If a semantic value is set, we should first check to see if the request can match
            // one of the bound attachments.
            bool foundMatch = false;
            if (request._semantic) {
                for (unsigned q=0; q<_pimpl->_semanticAttachments.size(); ++q) {
                    if (request._semantic == _pimpl->_semanticAttachments[q]._semantic && !consumedSemantic[q] && _pimpl->_semanticAttachments[q]._resource) {
                        #if defined(_DEBUG)
							if (!MatchRequest(requestDesc, _pimpl->_semanticAttachments[q]._desc)) {
                            	Log(Warning) << "Attachment bound to the pool for semantic (" << AttachmentSemantic{request._semantic} << ") does not match the request for this semantic. Attempting to use it anyway. Request: "
                                	<< requestDesc << ", Bound to pool: " << _pimpl->_semanticAttachments[q]._desc
                                	<< std::endl;
                        	}
						#endif

                        consumedSemantic[q] = true;
                        foundMatch = true;
                        selectedAttachments.push_back(q | (1u<<31u));
                        break;
                    }
                }
                if (foundMatch) continue;

                // If we didn't find a match in one of our bound semantic attachments, we must flow
                // through and treat it as a temporary attachment.
            }

            // If we haven't found a match yet, we must treat the request as a temporary buffer
            // We will go through and either find an existing buffer or create a new one
            for (unsigned q=0; q<_pimpl->_attachments.size(); ++q) {
                if (MatchRequest(requestDesc, _pimpl->_attachments[q]._desc) && q < consumed.size() && !consumed[q]) {
                    consumed[q] = true;
                    selectedAttachments.push_back(q);
                    foundMatch = true;
                    break;
                }
            }

            if (!foundMatch) {
                _pimpl->_attachments.push_back(Pimpl::Attachment{nullptr, requestDesc});
                selectedAttachments.push_back((unsigned)(_pimpl->_attachments.size()-1));
            }
        }
        AddRef(MakeIteratorRange(selectedAttachments), flags);
        return Reservation(std::move(selectedAttachments), this, flags);
    }

    void AttachmentPool::Bind(uint64_t semantic, const IResourcePtr& resource)
    {
        assert(semantic != 0);      // using zero as a semantic is not supported; this is used as a sentinel for "no semantic"
        
        auto binding = std::find_if(
            _pimpl->_semanticAttachments.begin(),
            _pimpl->_semanticAttachments.end(),
            [semantic](const Pimpl::SemanticAttachment& a) {
                return a._semantic == semantic;
            });
        if (binding != _pimpl->_semanticAttachments.end()) {
            if (binding->_resource == resource)
                return;

            assert(!binding->_lockCount);

		    if (binding->_resource)
                _pimpl->_srvPool.Erase(*binding->_resource);
        } else {
            Pimpl::SemanticAttachment newAttach;
            newAttach._semantic = semantic;

            // Find an unused slot if we can
            binding = std::find_if(
                _pimpl->_semanticAttachments.begin(),
                _pimpl->_semanticAttachments.end(),
                [semantic](const Pimpl::SemanticAttachment& a) {
                    return a._semantic == 0 && a._lockCount == 0;
                });
            if (binding != _pimpl->_semanticAttachments.end()) {
                *binding = newAttach;
            } else {
                binding = _pimpl->_semanticAttachments.insert(_pimpl->_semanticAttachments.end(), newAttach);
            }
        }

        binding->_desc = resource->GetDesc();
		assert(binding->_desc._textureDesc._format != Format::Unknown);
        binding->_resource = resource;
        binding->_pendingCompleteInitialization = false;
    }

    void AttachmentPool::Unbind(const IResource& resource)
    {
        for (auto& binding:_pimpl->_semanticAttachments) {
            if (binding._resource.get() == &resource) {
                if (binding._lockCount == 0) {
                    _pimpl->_srvPool.Erase(*binding._resource);
                    binding._resource = nullptr;
                }
                binding._semantic = 0;
            }
        }
    }

    void AttachmentPool::Unbind(uint64_t semantic)
    {
        auto existingBinding = std::find_if(
            _pimpl->_semanticAttachments.begin(),
            _pimpl->_semanticAttachments.end(),
            [semantic](const Pimpl::SemanticAttachment& a) {
                return a._semantic == semantic;
            });
        if (existingBinding != _pimpl->_semanticAttachments.end()) {
            if (existingBinding->_lockCount == 0) {
                _pimpl->_srvPool.Erase(*existingBinding->_resource);
                existingBinding->_resource = nullptr;
            }
            existingBinding->_semantic = 0;
        }
    }

    void AttachmentPool::UnbindAll()
    {
        _pimpl->_semanticAttachments.clear();
    }

	auto AttachmentPool::GetBoundResource(uint64_t semantic) -> IResourcePtr
	{
		auto existingBinding = std::find_if(
            _pimpl->_semanticAttachments.begin(),
            _pimpl->_semanticAttachments.end(),
            [semantic](const Pimpl::SemanticAttachment& a) {
                return a._semantic == semantic;
            });
		if (existingBinding != _pimpl->_semanticAttachments.end())
			return existingBinding->_resource;
		return nullptr;
	}

    void AttachmentPool::ResetActualized()
    {
        // Reset all actualized attachments. They will get recreated on demand
        _pimpl->_attachments.clear();
        _pimpl->_srvPool.Reset();
    }

    void AttachmentPool::AddRef(IteratorRange<const AttachmentName*> attachments, ReservationFlag::BitField flags)
    {
        for (auto a:attachments) {
            if (a & (1u<<31u)) {
                auto semanticAttachIdx = a & ~(1u<<31u);
                assert(semanticAttachIdx<_pimpl->_semanticAttachments.size());
                ++_pimpl->_semanticAttachments[semanticAttachIdx]._lockCount;
            } else {
                assert(a<_pimpl->_attachments.size());
                ++_pimpl->_attachments[a]._lockCount;
            }
        }
    }

    void AttachmentPool::Release(IteratorRange<const AttachmentName*> attachments, ReservationFlag::BitField flags)
    {
        for (auto a:attachments) {
            if (a & (1u<<31u)) {
                auto semanticAttachIdx = a & ~(1u<<31u);
                assert(semanticAttachIdx<_pimpl->_semanticAttachments.size());
                auto& binding = _pimpl->_semanticAttachments[semanticAttachIdx];
                assert(binding._lockCount >= 1);
                --binding._lockCount;
                if (!binding._lockCount && !binding._semantic) {
                    _pimpl->_srvPool.Erase(*binding._resource);
                    binding._resource = nullptr;
                }
            } else {
                assert(a<_pimpl->_attachments.size());
                assert(_pimpl->_attachments[a]._lockCount >= 1);
                --_pimpl->_attachments[a]._lockCount;
            }
        }
    }

    std::string AttachmentPool::GetMetrics() const
    {
        std::stringstream str;
        size_t totalByteCount = 0;
        str << "(" << _pimpl->_attachments.size() << ") attachments:" << std::endl;
        for (unsigned c=0; c<_pimpl->_attachments.size(); ++c) {
            auto& desc = _pimpl->_attachments[c]._desc;
            str << "    [" << c << "] " << desc;
            if (_pimpl->_attachments[c]._resource) {
                totalByteCount += ByteCount(_pimpl->_attachments[c]._resource->GetDesc());
                str << " (actualized)";
            } else {
                str << " (not actualized)";
            }
            str << std::endl;
        }

        str << "Total memory: (" << std::setprecision(4) << totalByteCount / (1024.f*1024.f) << "MiB)" << std::endl;
        str << "ViewPool count: (" << _pimpl->_srvPool.GetMetrics()._viewCount << ")" << std::endl;
        return str.str();
    }

    AttachmentPool::AttachmentPool(const std::shared_ptr<IDevice>& device)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_device = device;
    }

    AttachmentPool::~AttachmentPool()
    {}

    void AttachmentPool::Reservation::CompleteInitialization(IThreadContext& threadContext)
    {
        IResource* resources[_reservedAttachments.size()];
        size_t count = 0;
        for (auto a:_reservedAttachments) {
            AttachmentPool::Pimpl::Attachment* attach; 
            if (a & (1u<<31u)) {
                attach = &_pool->_pimpl->_semanticAttachments[a & ~(1u<<31u)];
            } else {
                attach = &_pool->_pimpl->_attachments[a];
                if (!attach->_resource)
                    _pool->_pimpl->BuildAttachment(a);
            }

            if (attach->_pendingCompleteInitialization) {
                assert(attach->_resource.get());
                resources[count++] = attach->_resource.get();
                attach->_pendingCompleteInitialization = false;
            }
        }
        Metal::CompleteInitialization(
            *Metal::DeviceContext::Get(threadContext),
            MakeIteratorRange(resources, &resources[count]));
    }

    bool AttachmentPool::Reservation::HasPendingCompleteInitialization() const
    {
        for (auto a:_reservedAttachments) {
            AttachmentPool::Pimpl::Attachment* attach; 
            if (a & (1u<<31u)) {
                attach = &_pool->_pimpl->_semanticAttachments[a & ~(1u<<31u)];
            } else {
                attach = &_pool->_pimpl->_attachments[a];
            }

            if (attach->_pendingCompleteInitialization)
                return true;
        }
        return false;
    }

    AttachmentPool::Reservation::Reservation() 
    {
        _pool = nullptr;
        _reservationFlags = 0;
    }
    AttachmentPool::Reservation::~Reservation()
    {
        if (_pool)
            _pool->Release(_reservedAttachments, _reservationFlags);
    }

    AttachmentPool::Reservation::Reservation(Reservation&& moveFrom)
    : _reservedAttachments(std::move(moveFrom._reservedAttachments))
    , _pool(std::move(moveFrom._pool))
    , _reservationFlags(std::move(moveFrom._reservationFlags))
    {
        moveFrom._pool = nullptr;
    }

    auto AttachmentPool::Reservation::operator=(Reservation&& moveFrom) -> Reservation&
    {
        if (_pool)
            _pool->Release(_reservedAttachments, _reservationFlags);
        _reservedAttachments = std::move(moveFrom._reservedAttachments);
        _pool = std::move(moveFrom._pool);
        _reservationFlags = std::move(moveFrom._reservationFlags);
        moveFrom._pool = nullptr;
        return *this;
    }

    AttachmentPool::Reservation::Reservation(const Reservation& copyFrom)
    : _reservedAttachments(copyFrom._reservedAttachments)
    , _pool(copyFrom._pool)
    , _reservationFlags(copyFrom._reservationFlags)
    {
        if (_pool)
            _pool->AddRef(_reservedAttachments, _reservationFlags);
    }

    auto AttachmentPool::Reservation::operator=(const Reservation& copyFrom) -> Reservation&
    {
        if (_pool)
            _pool->Release(_reservedAttachments, _reservationFlags);
        _reservedAttachments = copyFrom._reservedAttachments;
        _pool = copyFrom._pool;
        _reservationFlags = copyFrom._reservationFlags;
        if (_pool)
            _pool->AddRef(_reservedAttachments, _reservationFlags);
        return *this;
    }

    AttachmentPool::Reservation::Reservation(
        std::vector<AttachmentName>&& reservedAttachments,
        AttachmentPool* pool,
        ReservationFlag::BitField flags)
    : _reservedAttachments(std::move(reservedAttachments))
    , _pool(pool)
    , _reservationFlags(flags)
    {
        // this variation is called by the AttachmentPool, and that will have already
        // increased the ref count.
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

	uint64_t PreregisteredAttachment::CalculateHash() const
	{
		uint64_t result = HashCombine(_semantic, _desc.CalculateHash());
		auto shift = (unsigned)_state;
		lrot(result, shift);
		return result;
	}

    uint64_t HashPreregisteredAttachments(
        IteratorRange<const PreregisteredAttachment*> attachments,
        const FrameBufferProperties& fbProps,
        uint64_t seed)
    {
        uint64_t result = HashCombine(fbProps.CalculateHash(), seed);
        for (const auto& a:attachments)
            result = HashCombine(a.CalculateHash(), result);
        return result;
    }

    static FrameBufferDesc BuildFrameBufferDesc(
        const FrameBufferDescFragment& fragment,
        const FrameBufferProperties& props,
        IteratorRange<const FragmentStitchingContext::AttachmentTransform*> transforms)
    {
        //
        // Convert a frame buffer fragment to a discrete FrameBufferDesc. We actually don't need
        // to do much conversion. The SubpassDescs can just be copied across as is.
        // Attachments need to be transformed slightly. Here we also look for "split semantics".
        // This occurs when the input attachment for a semantic is different from the output
        // attachment for a semantic. This is illegal because in the FrameBufferDesc form, we can
        // only bind a single buffer to each semantic (this is because we can only bind a single
        // attachment to each semantic in the AttachmentPool -- this restriction was intended to
        // make the interface a little simplier and clearer)
        //
        // Split semantics typically happen when we want a RenderPass that reads from one attachment and
        // then writes back to the same attachment at the end (for example, that attachment might be
        // the main color buffer, and we want to do some post processing operation on it).
        //
        // Sometimes MergeFragments can't find a solution that can achieve this without adding extra
        // subpasses. So it creates this split semantic case. There are two options to solve this:
        // * use 2 different semantics for the operation in question (for example, a tonemap operation
        //      might take in a "hdrcolor" semantic and write to a "lhdrcolor" semantic)
        // * add an extra "copy" subpass -- this should just copy to the expected output buffer
        //
        std::vector<AttachmentDesc> fbAttachments;
        std::vector<uint64_t> attachmentSemantics;
        fbAttachments.reserve(fragment._attachments.size());
        attachmentSemantics.reserve(fragment._attachments.size());
        assert(transforms.size() == fragment._attachments.size());
        auto ti = transforms.begin();
        for (const auto& inputFrag:fragment._attachments) {
            uint64_t semantic = 0;
            if (inputFrag._inputSemanticBinding != 0 || inputFrag._outputSemanticBinding != 0) {
                if (    inputFrag._inputSemanticBinding != 0 && inputFrag._outputSemanticBinding != 0
                    &&  inputFrag._inputSemanticBinding != inputFrag._outputSemanticBinding)
                    Throw(std::runtime_error("Cannot construct FrameBufferDesc because input fragment has an attachment with two different semantics for input and output. This isn't supported; we must instead use two different attachments, one for each semantic."));

                for (auto compareFrag=attachmentSemantics.begin(); compareFrag!=attachmentSemantics.end(); ++compareFrag) {
                    if (    ((inputFrag._inputSemanticBinding != 0) && *compareFrag == inputFrag._inputSemanticBinding)
                        ||  ((inputFrag._outputSemanticBinding != 0) && *compareFrag == inputFrag._outputSemanticBinding)) {
                        // Hit a duplicate semantic binding case. Let's check if it's a "split semantic" case
                        // If the two attachments in question have non-coinciding zeroes for their semantic interface
                        // (and we already know that the non-zero semantics are equal), then it must be the split
                        // semantic case
                        auto& suspect = fragment._attachments[std::distance(attachmentSemantics.begin(), compareFrag)];
                        if (    (suspect._inputSemanticBinding == 0 && inputFrag._outputSemanticBinding == 0)
                            ||  (suspect._outputSemanticBinding == 0 && inputFrag._inputSemanticBinding == 0)) {
                            Throw(std::runtime_error("Cannot construct FrameBufferDesc because input fragment has a \"split semantic\" attachment. This can occur when an extra copy subpass is required"));
                        } else {
                            Throw(std::runtime_error("Cannot construct FrameBufferDesc because input fragment has more than one buffer assigned to the same semantic. This isn't supported because the AttachmentPool can only bind a single attachment to each semantic."));
                        }
                    }
                }
                semantic = (inputFrag._inputSemanticBinding != 0) ? inputFrag._inputSemanticBinding : inputFrag._outputSemanticBinding;
            }
            AttachmentDesc desc = inputFrag._desc;
            if (ti->_newLayout) desc._finalLayout = ti->_newLayout;     // we need to ensure that this layout agrees with what the FragmentStitchingContext thinks we're going to land on
            fbAttachments.push_back(desc);
            attachmentSemantics.push_back(semantic);
            ++ti;
        }

        // Generate the final FrameBufferDesc by moving the subpasses out of the fragment
        // Usually this function is called as a final step when converting a number of fragments
        // into a final FrameBufferDesc, so it makes sense to move the subpasses from the input
        std::vector<SubpassDesc> subpasses;
        subpasses.reserve(fragment._subpasses.size());
        for (const auto& sp:fragment._subpasses) subpasses.push_back(sp);
        return FrameBufferDesc { std::move(fbAttachments), std::move(subpasses), props };
    }

    static AttachmentName Remap(const std::vector<std::pair<AttachmentName, AttachmentName>>& remapping, AttachmentName name)
    {
        if (name == ~0u) return ~0u;
        auto i = LowerBound(remapping, name);
        assert(i!=remapping.end() && i->first == name);
        return i->second;
    }

    struct DirectionFlags
    {
        enum Bits
        {
            Reference = 1<<0,
            RequirePreinitializedData = 1<<1,
            WritesData = 1<<2,
            RetainsOnExit = 1<<3
        };
        using BitField = unsigned;
    };

    static bool HasRetain(LoadStore loadStore)
    {
        return  loadStore == LoadStore::Retain
            ||  loadStore == LoadStore::DontCare_StencilRetain
            ||  loadStore == LoadStore::Clear_StencilRetain
            ||  loadStore == LoadStore::Retain_StencilDontCare
            ||  loadStore == LoadStore::Retain_StencilClear
            ;
    }

    static DirectionFlags::BitField GetDirectionFlags(const FrameBufferDescFragment& fragment, AttachmentName attachment)
    {
        auto loadOp = fragment._attachments[attachment]._desc._loadFromPreviousPhase;
        auto storeOp = fragment._attachments[attachment]._desc._storeToNextPhase;
        DirectionFlags::BitField result = 0;
        if (HasRetain(storeOp))
            result |= DirectionFlags::RetainsOnExit;
        if (HasRetain(loadOp))
            result |= DirectionFlags::RequirePreinitializedData;

        // If we only use the attachment as a resolve output, but we have "Retain" in
        // load op, the DirectionFlags::RequirePreinitializedData flag will still be
        // set. This isn't very correct -- if we only use an attachment for a resolve
        // output, the load op should be DontCare

        for (const auto&p:fragment._subpasses) {
            for (const auto&a:p.GetOutputs())
                if (a._resourceName == attachment)
                    result |= DirectionFlags::Reference | DirectionFlags::WritesData;

            if (p.GetDepthStencil()._resourceName == attachment)
                result |= DirectionFlags::Reference | DirectionFlags::WritesData;

            for (const auto&a:p.GetInputs())
                if (a._resourceName == attachment)
                    result |= DirectionFlags::Reference;

            for (const auto&a:p.GetResolveOutputs())
                if (a._resourceName == attachment)
                    result |= DirectionFlags::Reference | DirectionFlags::WritesData;
            if (p.GetResolveDepthStencil()._resourceName == attachment)
                result |= DirectionFlags::Reference | DirectionFlags::WritesData;

            for (const auto&a:p.GetViews())
                if (a._resourceName == attachment) {
                    result |= DirectionFlags::Reference;
                    if (a._usage & BindFlag::UnorderedAccess)
                        result |= DirectionFlags::WritesData;
                }
        }
        return result;
    }

    class WorkingAttachment
    {
    public:
        AttachmentName _name = ~0u;

        Format _format = Format::Unknown;
        TextureSamples _samples = TextureSamples::Create();

        uint64_t _shouldReceiveDataForSemantic = 0;         // when looking for an attachment to write the data for this semantic, prefer this attachment
        uint64_t _containsDataForSemantic = 0;              // the data for this semantic is already written to this attachment

        uint64_t _firstAccessSemantic = 0;
        LoadStore _firstAccessLoad = LoadStore::DontCare;
        BindFlag::BitField _firstAccessInitialLayout = 0;
        uint64_t _lastWriteSemantic = 0;
        LoadStore _lastAccessStore = LoadStore::DontCare;
        BindFlag::BitField _lastAccessFinalLayout = 0;

        bool _hasBeenAccessed = false;
        PreregisteredAttachment::State _state = PreregisteredAttachment::State::Uninitialized;

        WorkingAttachment(const PreregisteredAttachment& attachment);
        WorkingAttachment(const FrameBufferDescFragment::Attachment& attachment, const FrameBufferProperties& props);
        WorkingAttachment() {}
    };

    WorkingAttachment::WorkingAttachment(const PreregisteredAttachment& attachment)
    {
        _format = attachment._desc._textureDesc._format;
        _samples = attachment._desc._textureDesc._samples;
        _state = attachment._state;
        if (_state == PreregisteredAttachment::State::Initialized
            || _state == PreregisteredAttachment::State::Initialized_StencilUninitialized
            || _state == PreregisteredAttachment::State::Uninitialized_StencilInitialized)
            _containsDataForSemantic = attachment._semantic;
        _shouldReceiveDataForSemantic = attachment._semantic;
        _firstAccessInitialLayout = attachment._layoutFlags;
    }

    static TextureSamples GetSamples(const FrameBufferDescFragment::Attachment& attachment, const FrameBufferProperties& props)
    {
        return (attachment._desc._flags & AttachmentDesc::Flags::Multisampled) ? props._samples : TextureSamples::Create();
    }

    WorkingAttachment::WorkingAttachment(
        const FrameBufferDescFragment::Attachment& attachment,
        const FrameBufferProperties& props)
    {
        _samples = GetSamples(attachment, props);
        _format = attachment._desc._format;
    }

    static bool FormatCompatible(Format lhs, Format rhs)
    {
        if (lhs == rhs) return true;
        auto    lhsTypeless = AsTypelessFormat(lhs),
                rhsTypeless = AsTypelessFormat(rhs);
        return lhsTypeless == rhsTypeless;
    }
    
    static bool IsCompatible(const WorkingAttachment& testAttachment, const FrameBufferDescFragment::Attachment& request, const FrameBufferProperties& fbProps)
    {
        return 
            ( (FormatCompatible(testAttachment._format, request._desc._format)) || (testAttachment._format == Format::Unknown) || (request._desc._format == Format::Unknown) )
            && (testAttachment._samples == GetSamples(request, fbProps));
    }

    static AttachmentName NextName(IteratorRange<const WorkingAttachment*> attachments0, IteratorRange<const WorkingAttachment*> attachments1)
    {
        // find the lowest name not used by any of the attachments
        uint64_t bitField = 0;
        for (const auto& a:attachments0) {
            if (a._name == ~0u) continue;
            assert(a._name < 64);
            assert(!(bitField & (1ull << uint64_t(a._name))));
            bitField |= 1ull << uint64_t(a._name);
        }
        for (const auto& a:attachments1) {
            if (a._name == ~0u) continue;
            assert(a._name < 64);
            assert(!(bitField & (1ull << uint64_t(a._name))));
            bitField |= 1ull << uint64_t(a._name);
        }
        // Find the position of the least significant bit set in the inverse
        // That is the smallest number less than 64 that hasn't been used yet
        return xl_ctz8(~bitField);
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    static const char* AsString(PreregisteredAttachment::State state)
    {
        switch (state) {
        case PreregisteredAttachment::State::Uninitialized: return "Uninitialized";
        case PreregisteredAttachment::State::Initialized:   return "Initialized";
        case PreregisteredAttachment::State::Initialized_StencilUninitialized:   return "Initialized_StencilUninitialized";
        case PreregisteredAttachment::State::Uninitialized_StencilInitialized:   return "Uninitialized_StencilInitialized";
        default:                                            return "<<unknown>>";
        }
    }

    static std::ostream& operator<<(std::ostream& str, const PreregisteredAttachment& attachment)
    {
        str << "PreregisteredAttachment { "
            << AttachmentSemantic{attachment._semantic} << ", "
            << attachment._desc << ", "
            << AsString(attachment._state) << ", "
            << "0x" << std::hex << attachment._layoutFlags << std::dec << "}";
        return str;
    }

    static std::ostream& operator<<(std::ostream& str, const WorkingAttachment& attachment)
    {
        str << "WorkingAttachment {"
            << attachment._name << ", "
            << "{" << AsString(attachment._format) << ", " << attachment._samples._sampleCount << "}, "
            << std::hex << "Contains: " << AttachmentSemantic{attachment._containsDataForSemantic} << ", "
            << "ShouldReceive: " << AttachmentSemantic{attachment._shouldReceiveDataForSemantic} << ", "
            << "FirstAccess: {" << AttachmentSemantic{attachment._firstAccessSemantic} << ", 0x" << attachment._firstAccessInitialLayout << ", " << AsString(attachment._firstAccessLoad) << "}, "
            << "LastAccess: {" << AttachmentSemantic{attachment._lastWriteSemantic} << ", 0x" << attachment._lastAccessFinalLayout << ", " << AsString(attachment._lastAccessStore) << "}, "
            << std::dec
            << AsString(attachment._state) << "}";
        return str;
    }

    static std::ostream& operator<<(std::ostream& str, const FrameBufferDescFragment& fragment)
    {
        str << "FrameBufferDescFragment with attachments: " << std::endl;
        for (unsigned c=0; c<fragment._attachments.size(); ++c) {
            str << StreamIndent(4) << "[" << c << "] "
                << AttachmentSemantic{fragment._attachments[c].GetInputSemanticBinding()} << ", " << AttachmentSemantic{fragment._attachments[c].GetOutputSemanticBinding()} << " : " << fragment._attachments[c]._desc
                << std::endl;
        }
        str << "Subpasses: " << std::endl;
        for (unsigned c=0; c<fragment._subpasses.size(); ++c) {
            str << StreamIndent(4) << "[" << c << "] " << fragment._subpasses[c] << std::endl;
        }
        return str;
    }

    static bool CompareAttachmentName(const WorkingAttachment& lhs, const WorkingAttachment& rhs)
    {
        return lhs._name < rhs._name;
    }

    static PreregisteredAttachment BuildPreregisteredAttachment(
        const FrameBufferDescFragment::Attachment& attachmentDesc,
        unsigned usageBindFlags, 
        const FrameBufferProperties& props)
    {
        // Prefer "typeless" formats when creating the actual attachments
        // This ensures that we can have complete freedom when we create views
        TextureDesc tDesc = TextureDesc::Plain2D(
            (unsigned)props._outputWidth, (unsigned)props._outputHeight,
            attachmentDesc._desc._format, 1, 0u,
            GetSamples(attachmentDesc, props));
        auto bindFlags = usageBindFlags | attachmentDesc._desc._initialLayout | attachmentDesc._desc._finalLayout; 

        PreregisteredAttachment result;
        result._desc = CreateDesc(bindFlags, 0, 0, tDesc, "attachment-pool");
        assert(result._desc._textureDesc._format != Format::Unknown);       // at this point we must have a resolved format. If it's still unknown, we can't created a preregistered attachment
        result._semantic = attachmentDesc._inputSemanticBinding;
        result._state = PreregisteredAttachment::State::Uninitialized;
        result._layoutFlags = attachmentDesc._desc._finalLayout ? attachmentDesc._desc._finalLayout : usageBindFlags;
        return result;
    }

    auto FragmentStitchingContext::TryStitchFrameBufferDescInternal(const FrameBufferDescFragment& fragment) -> StitchResult
    {
        // Match the attachment requests to the given fragment to our list of working attachments
        // in order to fill out a full specified attachment list. Also update the preregistered
        // attachments as per inputs and outputs from the fragment
        StitchResult result;
        result._pipelineType = fragment._pipelineType;
        result._fullAttachmentDescriptions.reserve(fragment._attachments.size());
        result._attachmentTransforms.reserve(fragment._attachments.size());
        for (const auto&a:fragment._attachments) {
            auto idx = &a - AsPointer(fragment._attachments.begin());
            auto directionFlags = GetDirectionFlags(fragment, idx);
            assert(directionFlags & DirectionFlags::Reference);         // if you hit this, it probably means an attachment is defined in the fragment, but not used by any subpass
            auto usageFlags = CalculateBindFlags(fragment, idx);

            // Try to the match the attachment request to an existing preregistered attachment,
            // or created a new one if we can't match
            auto i = std::find_if(
                _workingAttachments.begin(), _workingAttachments.end(),
                [semantic=a._inputSemanticBinding](const auto& c) { return c._semantic == semantic; });
            if (i != _workingAttachments.end()) {
                #if defined(_DEBUG)
                    if (!IsCompatible(*i, a, _workingProps)) {     // todo -- check layout flags
                        Log(Warning) << "Preregistered attachment for semantic (" << AttachmentSemantic{a._inputSemanticBinding} << " does not match the request for this semantic. Attempting to use it anyway. Request: "
                            << a._desc << ", Preregistered: " << *i << std::endl;
                    }
                #endif
                result._fullAttachmentDescriptions.push_back(*i);

                AttachmentTransform transform;
                if (directionFlags & DirectionFlags::RetainsOnExit) {
                    if (directionFlags & DirectionFlags::WritesData) {
                        if (directionFlags & DirectionFlags::RequirePreinitializedData) transform._type = AttachmentTransform::Written;
                        else transform._type = AttachmentTransform::Generated;
                    } else  {
                        transform._type = AttachmentTransform::Preserved;
                    }
                } else {
                    assert(directionFlags & DirectionFlags::Reference);
                    if (directionFlags & DirectionFlags::RequirePreinitializedData) transform._type = AttachmentTransform::Consumed;
                    else transform._type = AttachmentTransform::Temporary;
                }

                // Unless we're actually generating the attachment from scratch, don't remove the layout flags that were
                // previously on the attachment
                if (transform._type == AttachmentTransform::Generated || transform._type == AttachmentTransform::Temporary) {
                    transform._newLayout = a._desc._finalLayout ? a._desc._finalLayout : usageFlags;
                } else {
                    transform._newLayout = a._desc._finalLayout ? a._desc._finalLayout : (i->_layoutFlags | usageFlags);
                }
                result._attachmentTransforms.push_back(transform);
            } else {
                #if defined(_DEBUG)
                    if (a._desc._format == Format::Unknown)
                        Log(Warning) << "Missing format information for attachment with semantic: " << AttachmentSemantic{a._inputSemanticBinding} << std::endl;
                #endif 
                auto newAttachment = BuildPreregisteredAttachment(a, usageFlags, _workingProps);
                result._fullAttachmentDescriptions.push_back(newAttachment);
                AttachmentTransform transform;
                assert(!(directionFlags & DirectionFlags::RequirePreinitializedData));      // If you hit this, it means the fragment has an attachment that loads data, but there's no matching attachment in the stitching context
                if (directionFlags & DirectionFlags::RetainsOnExit) {
                    assert(directionFlags & DirectionFlags::WritesData);
                    transform._type = AttachmentTransform::Generated;
                    transform._newLayout = newAttachment._layoutFlags;
                } else
                    transform._type = AttachmentTransform::Temporary;
                result._attachmentTransforms.push_back(transform);
            }
        }

        for (const auto&sp:fragment._subpasses) {
            result._viewedAttachmentsMap.push_back((unsigned)result._viewedAttachments.size());
            result._viewedAttachments.insert(result._viewedAttachments.end(), sp._views.begin(), sp._views.end());
        }
        result._viewedAttachmentsMap.push_back((unsigned)result._viewedAttachments.size());

        #if defined(_DEBUG)
            if (CanBeSimplified(fragment, _workingAttachments, _workingProps))
				Log(Warning) << "Detected a frame buffer fragment which be simplified. This usually means one or more of the attachments can be reused, thereby reducing the total number of attachments required." << std::endl;
        #endif

        result._fbDesc = BuildFrameBufferDesc(fragment, _workingProps, MakeIteratorRange(result._attachmentTransforms));
        return result;
    }
    
    void FragmentStitchingContext::UpdateAttachments(const StitchResult& stitchResult)
    {
        for (unsigned aIdx=0; aIdx<stitchResult._attachmentTransforms.size(); ++aIdx) {
            auto semantic = stitchResult._fullAttachmentDescriptions[aIdx]._semantic;
            if (!semantic) continue;
            switch (stitchResult._attachmentTransforms[aIdx]._type) {
            case FragmentStitchingContext::AttachmentTransform::Preserved:
            case FragmentStitchingContext::AttachmentTransform::Temporary:
                break;
            case FragmentStitchingContext::AttachmentTransform::Written:
                {
                    auto desc = stitchResult._fullAttachmentDescriptions[aIdx];
                    desc._state = PreregisteredAttachment::State::Initialized;
                    desc._layoutFlags = stitchResult._attachmentTransforms[aIdx]._newLayout;
                    DefineAttachment(desc);
                    break;
                }
            case FragmentStitchingContext::AttachmentTransform::Generated:
                {
                    auto desc = stitchResult._fullAttachmentDescriptions[aIdx];
                    desc._state = PreregisteredAttachment::State::Initialized;
                    desc._layoutFlags = stitchResult._attachmentTransforms[aIdx]._newLayout;
                    DefineAttachment(desc);
                    break;
                }
            case FragmentStitchingContext::AttachmentTransform::Consumed:
                Undefine(semantic);
                break;
            }
        }
    }

    auto FragmentStitchingContext::TryStitchFrameBufferDesc(IteratorRange<const FrameBufferDescFragment*> fragments) -> StitchResult
    {
        auto merged = MergeFragments(MakeIteratorRange(_workingAttachments), fragments, _workingProps);
        auto stitched = TryStitchFrameBufferDescInternal(merged._mergedFragment);
        stitched._log = merged._log;
        return stitched;
    }

    void FragmentStitchingContext::DefineAttachment(
        uint64_t semantic, const ResourceDesc& resourceDesc, 
        PreregisteredAttachment::State state,
        BindFlag::BitField initialLayoutFlags)
	{
		auto i = std::find_if(
			_workingAttachments.begin(), _workingAttachments.end(),
			[semantic](const auto& c) { return c._semantic == semantic; });
		if (i != _workingAttachments.end()) {
            *i = RenderCore::Techniques::PreregisteredAttachment{semantic, resourceDesc, state, initialLayoutFlags};
        } else
            _workingAttachments.push_back(
                RenderCore::Techniques::PreregisteredAttachment{semantic, resourceDesc, state, initialLayoutFlags});
	}

    void FragmentStitchingContext::DefineAttachment(
        const PreregisteredAttachment& attachment)
	{
		auto i = std::find_if(
			_workingAttachments.begin(), _workingAttachments.end(),
			[semantic=attachment._semantic](const auto& c) { return c._semantic == semantic; });
		if (i != _workingAttachments.end()) {
            *i = attachment;
        } else
            _workingAttachments.push_back(attachment);
	}

    void FragmentStitchingContext::Undefine(uint64_t semantic)
    {
        auto i = std::find_if(
			_workingAttachments.begin(), _workingAttachments.end(),
			[semantic](const auto& c) { return c._semantic == semantic; });
		if (i != _workingAttachments.end())
            _workingAttachments.erase(i);
    }

    FragmentStitchingContext::FragmentStitchingContext(IteratorRange<const PreregisteredAttachment*> preregAttachments, const FrameBufferProperties& fbProps)
    : _workingProps(fbProps)
    {
        for (const auto&attach:preregAttachments)
            DefineAttachment(attach);
    }

    FragmentStitchingContext::~FragmentStitchingContext()
    {}

////////////////////////////////////////////////////////////////////////////////////////////////////

	static FrameBufferDescFragment::SubpassDesc RemapSubpassDesc(
		const FrameBufferDescFragment::SubpassDesc& input,
		const std::function<AttachmentName(AttachmentName)>& remapFunction)
	{
		FrameBufferDescFragment::SubpassDesc result;
		#if defined(_DEBUG)
			result.SetName(input._name);
		#endif
		for (auto remapped:input.GetOutputs()) {
			result.AppendOutput(remapFunction(remapped._resourceName), remapped._window);
		}
		if (input.GetDepthStencil()._resourceName != ~0u) {
			auto remapped = input.GetDepthStencil();
			result.SetDepthStencil(remapFunction(remapped._resourceName), remapped._window);
		}
		for (auto remapped:input.GetInputs()) {
			result.AppendInput(remapFunction(remapped._resourceName), remapped._window);
		}
		for (auto remapped:input.GetResolveOutputs()) {
			result.AppendResolveOutput(remapFunction(remapped._resourceName), remapped._window);
		}
		if (input.GetResolveDepthStencil()._resourceName != ~0u) {
			auto remapped = input.GetResolveDepthStencil();
			result.SetResolveDepthStencil(remapFunction(remapped._resourceName), remapped._window);
		}
        for (auto src:input.GetViews())
			result.AppendNonFrameBufferAttachmentView(remapFunction(src._resourceName), src._usage, src._window);
		return result;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////

    MergeFragmentsResult MergeFragments(
        IteratorRange<const PreregisteredAttachment*> preregisteredInputs,
        IteratorRange<const FrameBufferDescFragment*> fragments,
		const FrameBufferProperties& fbProps)
    {
        #if defined(_DEBUG)
            std::stringstream debugInfo;
            debugInfo << "Preregistered Inputs:" << std::endl;
            for (auto a = preregisteredInputs.begin(); a != preregisteredInputs.end(); ++a) {
                debugInfo << "[" << std::distance(preregisteredInputs.begin(), a) << "] " << *a << std::endl;
            }
        #endif
        
        // Merge together the input fragments to create the final output
        // Each fragment defines an input/output interface. We need to bind these
        // together (along with the temporaries) to create a single cohesive render pass.
        // Where we can reuse the same temporary multiple times, we should do so
        FrameBufferDescFragment result;

        if (!fragments.size()) return { std::move(result) };

        result._pipelineType = fragments[0]._pipelineType;
        
        std::vector<WorkingAttachment> workingAttachments;
        workingAttachments.reserve(preregisteredInputs.size());
        for (unsigned c=0; c<preregisteredInputs.size(); c++) {
            workingAttachments.push_back(WorkingAttachment{ preregisteredInputs[c] });
        }

        for (auto f=fragments.begin(); f!=fragments.end(); ++f) {
            std::vector<WorkingAttachment> newWorkingAttachments;
            std::vector<std::pair<AttachmentName, AttachmentName>> attachmentRemapping;

            assert(f->_pipelineType == result._pipelineType);       // all fragments must have the same pipeline type

            // Capture the default properties for each semantic on the interface now
            std::unordered_map<uint64_t, Format> defaultSemanticFormats;
            for (const auto& a:preregisteredInputs) {
                defaultSemanticFormats[a._semantic] = a._desc._textureDesc._format;
            }
            for (const auto& a:workingAttachments) {
                if (a._shouldReceiveDataForSemantic)
                    defaultSemanticFormats[a._shouldReceiveDataForSemantic] = a._format;
                if (a._containsDataForSemantic)
                    defaultSemanticFormats[a._containsDataForSemantic] = a._format;
            }

            #if defined(_DEBUG)
                debugInfo << "-------------------------------" << std::endl;
                debugInfo << "Fragment [" << std::distance(fragments.begin(), f) << "] " << *f;
            #endif

            /////////////////////////////////////////////////////////////////////////////
            using AttachmentAndDirection = std::pair<AttachmentName, DirectionFlags::BitField>;
            std::vector<AttachmentAndDirection> sortedInterfaceAttachments;
            for (auto interf = f->_attachments.begin(); interf != f->_attachments.end(); ++interf) {
                AttachmentName interfaceAttachmentName = (AttachmentName)std::distance(f->_attachments.begin(), interf);

                // Look through the load/store values in the subpasses to find the "direction" for
                // the first use of this attachment;
                auto directionFlags = GetDirectionFlags(*f, interfaceAttachmentName);
                assert(directionFlags != 0);     // Note -- we can get here if we have an attachment that is defined, but never used
                sortedInterfaceAttachments.push_back({interfaceAttachmentName, directionFlags});
            }

            // sort so the attachment with "load" direction are handled first
            std::stable_sort(
                sortedInterfaceAttachments.begin(), sortedInterfaceAttachments.end(),
                [](const AttachmentAndDirection& lhs, const AttachmentAndDirection& rhs) {
                    return (lhs.second & DirectionFlags::RequirePreinitializedData) > (rhs.second & DirectionFlags::RequirePreinitializedData);
                });

            for (const auto&pair:sortedInterfaceAttachments) {
                const auto& interfaceAttachment = f->_attachments[pair.first];
                AttachmentName interfaceAttachmentName = pair.first;
                DirectionFlags::BitField directionFlags = pair.second;
                
                WorkingAttachment newState;
                if (directionFlags & DirectionFlags::RequirePreinitializedData) {
                    // We're expecting a buffer that already has some initialized contents. Look for
                    // something matching in our working attachments array
                    auto compat = std::find_if(
                        workingAttachments.begin(), workingAttachments.end(),
                        [&interfaceAttachment, fbProps](const WorkingAttachment& workingAttachment) {
                            return (workingAttachment._state == PreregisteredAttachment::State::Initialized)
                                && (workingAttachment._containsDataForSemantic == interfaceAttachment.GetInputSemanticBinding())
                                && IsCompatible(workingAttachment, interfaceAttachment, fbProps);
                        });

                    if (compat == workingAttachments.end()) {
                        // This is a new buffer that will be part of the input interface for the
                        // final fragment.
                        // Note that we don't allow an attachment with a "Unknown" format to be defined
                        // in this way -- just because that could start to get confusing to the caller.
                        if (!interfaceAttachment.GetInputSemanticBinding()) {
                            #if defined(_DEBUG)
                                auto uninitializedCheck = std::find_if(
                                    workingAttachments.begin(), workingAttachments.end(),
                                    [&interfaceAttachment, fbProps](const WorkingAttachment& workingAttachment) {
                                        return IsCompatible(workingAttachment, interfaceAttachment, fbProps);
                                    });
                                debugInfo << "      * Failed to find compatible initialized buffer for request: " << interfaceAttachment._desc << ". Semantic: " << AttachmentSemantic{interfaceAttachment.GetInputSemanticBinding()} << std::endl;
                                if (uninitializedCheck != workingAttachments.end())
                                    debugInfo << "      * Buffer " << std::distance(workingAttachments.begin(), uninitializedCheck) << " is compatible, but does not contain any initialized data (is there a missing Retain flag?)" << std::endl;
                                debugInfo << "      * Working attachments are: " << std::endl;
                                for (const auto& att : workingAttachments)
                                    debugInfo << att << std::endl;
                                auto debugInfoStr = debugInfo.str();
                                Log(Error) << "MergeFragments() failed. Details:" << std::endl << debugInfoStr << std::endl;
                                Throw(::Exceptions::BasicLabel("Couldn't bind renderpass fragment input request. Details:\n%s\n", debugInfoStr.c_str()));
                            #else
                                Throw(::Exceptions::BasicLabel("Couldn't bind renderpass fragment input request"));
                            #endif
                        }

                        newState = WorkingAttachment { interfaceAttachment, fbProps };
                        newState._name = NextName(MakeIteratorRange(workingAttachments), MakeIteratorRange(newWorkingAttachments));
                    } else {
                        // Remove from the working attachments and push back in it's new state
                        // If we're not writing to this attachment, it will lose it's semantic here
                        newState = *compat;
                        if (newState._name == ~0u)
                            newState._name = NextName(MakeIteratorRange(workingAttachments), MakeIteratorRange(newWorkingAttachments));
                        workingAttachments.erase(compat);
                    }
                } else {
                    // define a new output buffer, or reuse something that we can reuse
                    // Prefer a buffer that is uninitialized, but we can drop back to something that
                    // is initialized if we have to
                    auto compat = std::find_if(
                        workingAttachments.begin(), workingAttachments.end(),
                        [&interfaceAttachment, fbProps](const WorkingAttachment& workingAttachment) {
                            return (workingAttachment._shouldReceiveDataForSemantic == interfaceAttachment.GetOutputSemanticBinding())
                                && (workingAttachment._state == PreregisteredAttachment::State::Uninitialized)
                                && IsCompatible(workingAttachment, interfaceAttachment, fbProps);
                        });

                    if (compat == workingAttachments.end() && interfaceAttachment.GetOutputSemanticBinding()) {
                        // Look for a buffer with no assigned semantic. It will take on the semantic we
                        // give it
                        compat = std::find_if(
                            workingAttachments.begin(), workingAttachments.end(),
                            [&interfaceAttachment, fbProps](const WorkingAttachment& workingAttachment) {
                                return (workingAttachment._shouldReceiveDataForSemantic == 0)
                                    && (workingAttachment._state == PreregisteredAttachment::State::Uninitialized)
                                    && IsCompatible(workingAttachment, interfaceAttachment, fbProps);
                            });
                    }

                    if (compat == workingAttachments.end() && interfaceAttachment._desc._format != Format::Unknown) {
                        // Couldn't find a buffer in the "uninitialized" state. We're just going to
                        // find a initialized buffer and overwrite it's contents.
                        // We need this flexibility because some fragments use "retain" on their
                        // last read or write speculatively (maybe because they don't know whether the buffer
                        // will actually be read from again, or they have been mistakenly marked as
                        // retain)
                        // (this relies on us sorting the attachment list so load operations are processed
                        // first to work reliably)
						// Also, we shouldn't do this when the request format is "unknown" -- because then we
						// can match against almost anything
                        compat = std::find_if(
                            workingAttachments.begin(), workingAttachments.end(),
                            [&interfaceAttachment, fbProps](const WorkingAttachment& workingAttachment) {
                                return (workingAttachment._shouldReceiveDataForSemantic == interfaceAttachment.GetOutputSemanticBinding() || workingAttachment._shouldReceiveDataForSemantic == 0)
                                    && IsCompatible(workingAttachment, interfaceAttachment, fbProps);
                            });
                    }

                    if (compat == workingAttachments.end()) {
                        // Technically we could do a second pass looking for some initialized attachment
                        // we could write over -- but that would lead to other complications. Let's just
                        // create something new.

                        // We can steal the settings from an existing attachment with the same semantic
                        // name, if necessary. We get these from a capture of the working attachments
                        // we made at the start of the fragment
                        auto desc = interfaceAttachment._desc;
                        auto sameSemantic = defaultSemanticFormats.find(interfaceAttachment.GetOutputSemanticBinding());
                        if (sameSemantic != defaultSemanticFormats.end()) {
                            if (desc._format == Format::Unknown) desc._format = sameSemantic->second;
                        } else {
                            if (desc._format == Format::Unknown) {
                                #if defined(_DEBUG)
                                    debugInfo << "      * Could not resolve correct format for attachment: " << interfaceAttachment._desc << ". Semantic: " << AttachmentSemantic{interfaceAttachment.GetInputSemanticBinding()} << std::endl;
                                    for (const auto& att : workingAttachments)
                                        debugInfo << att << std::endl;
                                    auto debugInfoStr = debugInfo.str();
                                    Log(Error) << "MergeFragments() failed. Details:" << std::endl << debugInfoStr << std::endl;
                                    Throw(::Exceptions::BasicLabel("Couldn't bind renderpass fragment input request. Details:\n%s\n", debugInfoStr.c_str()));
                                #else
                                    Throw(::Exceptions::BasicLabel("Couldn't bind renderpass fragment input request"));
                                #endif
                            }
                        }

                        newState = WorkingAttachment { interfaceAttachment, fbProps };
                        newState._name = NextName(MakeIteratorRange(workingAttachments), MakeIteratorRange(newWorkingAttachments));

                        #if defined(_DEBUG)
                            debugInfo 
                                << "      * " 
                                << "Cannot find compatible buffer, creating #" << newState._name << ", " << newState << std::endl;
                        #endif
                    } else {
                        // remove from the working attachments and push back in it's new state
                        newState = *compat;
                        if (newState._name == ~0u)
                            newState._name = NextName(MakeIteratorRange(workingAttachments), MakeIteratorRange(newWorkingAttachments));
                        workingAttachments.erase(compat);
                    }
                }

                if (!newState._hasBeenAccessed) {
                    newState._hasBeenAccessed = true;
                    newState._firstAccessSemantic = interfaceAttachment.GetInputSemanticBinding();
                    newState._firstAccessLoad = interfaceAttachment._desc._loadFromPreviousPhase;
                    if (interfaceAttachment._desc._initialLayout)       // otherwise inherit from what we set from the PreregisteredAttachment
                        newState._firstAccessInitialLayout = interfaceAttachment._desc._initialLayout;
                }

                if (directionFlags & DirectionFlags::WritesData) {
                    newState._state = PreregisteredAttachment::State::Initialized;
                    newState._containsDataForSemantic = interfaceAttachment.GetOutputSemanticBinding();
                    newState._lastWriteSemantic = interfaceAttachment.GetOutputSemanticBinding();
                }

                newState._lastAccessStore = interfaceAttachment._desc._storeToNextPhase;
                newState._lastAccessFinalLayout = interfaceAttachment._desc._finalLayout;
                newWorkingAttachments.push_back(newState);

                attachmentRemapping.push_back({interfaceAttachmentName, newState._name});
            }

            /////////////////////////////////////////////////////////////////////////////

                // setup the subpasses & PassFragment
            std::sort(attachmentRemapping.begin(), attachmentRemapping.end(), CompareFirst<AttachmentName, AttachmentName>());

            for (unsigned p=0; p<(unsigned)f->_subpasses.size(); ++p) {
                auto newSubpass = RemapSubpassDesc(
					f->_subpasses[p],
					std::bind(&Remap, std::ref(attachmentRemapping), std::placeholders::_1));
                result.AddSubpass(std::move(newSubpass));
            }

            /////////////////////////////////////////////////////////////////////////////

            workingAttachments.insert(workingAttachments.end(), newWorkingAttachments.begin(), newWorkingAttachments.end());

            #if defined(_DEBUG)
                debugInfo << "Merge calculated this attachment remapping:" << std::endl;
                for (const auto&r:attachmentRemapping)
                    debugInfo << StreamIndent(4) << "[" << r.first << "] remapped to " << r.second << " ("
                        << f->_attachments[r.first]._desc
                        << ")" << std::endl;
                debugInfo << "Current fragment interface:" << std::endl;
                for (const auto&w:workingAttachments)
                    debugInfo << StreamIndent(4) << w << std::endl;
            #endif
        }

        // The workingAttachments array is now the list of attachments that must go into
        // the output fragment;
        result._attachments.reserve(workingAttachments.size());
        std::sort(workingAttachments.begin(), workingAttachments.end(), CompareAttachmentName);
        for (auto& a:workingAttachments) {
            if (a._name == ~0u) continue;
            // The AttachmentNames in FrameBufferDescFragment are just indices into the attachment
            // list -- so we must ensure that we insert in order, and without gaps
            assert(a._name == result._attachments.size());
            FrameBufferDescFragment::Attachment r { a._firstAccessSemantic, a._containsDataForSemantic };
            r._desc._format = a._format;
            r._desc._flags = (a._samples._sampleCount > 1) ? AttachmentDesc::Flags::Multisampled : 0u;
            r._desc._initialLayout = a._firstAccessInitialLayout;
            r._desc._finalLayout = a._lastAccessFinalLayout;
            r._desc._loadFromPreviousPhase = a._firstAccessLoad;
            r._desc._storeToNextPhase = a._lastAccessStore;
            result._attachments.push_back(r);
        }

        MergeFragmentsResult finalResult;
        finalResult._mergedFragment = std::move(result);

        for (auto& a:workingAttachments) {
            if (a._name == ~0u) continue;
            if (a._firstAccessSemantic && HasRetain(a._firstAccessLoad))
                finalResult._inputAttachments.push_back({a._firstAccessSemantic, a._name});
            if (a._lastWriteSemantic)
                finalResult._outputAttachments.push_back({a._lastWriteSemantic, a._name});
        }

        #if defined(_DEBUG)
            debugInfo << "-------------------------------" << std::endl;
            debugInfo << "Final attachments" << std::endl;
            for (unsigned c=0; c<result._attachments.size(); ++c)
                debugInfo << StreamIndent(4) << "[" << c << "] "
                    << AttachmentSemantic{result._attachments[c].GetInputSemanticBinding()} << ", " << AttachmentSemantic{result._attachments[c].GetOutputSemanticBinding()}
                    << " : " << result._attachments[c]._desc << std::endl;
            debugInfo << "Final subpasses" << std::endl;
            for (unsigned c=0; c<result._subpasses.size(); ++c)
                debugInfo << StreamIndent(4) << "[" << c << "] " << result._subpasses[c] << std::endl;
            debugInfo << "Interface summary" << std::endl;
            for (unsigned c=0; c<finalResult._inputAttachments.size(); ++c)
                debugInfo << StreamIndent(4) << "Input [" << c << "] " << AttachmentSemantic{finalResult._inputAttachments[c].first} << " " << finalResult._inputAttachments[c].second << " (" << finalResult._mergedFragment._attachments[finalResult._inputAttachments[c].second]._desc << ")" << std::endl;
            for (unsigned c=0; c<finalResult._outputAttachments.size(); ++c)
                debugInfo << StreamIndent(4) << "Output [" << c << "] " << AttachmentSemantic{finalResult._outputAttachments[c].first} << " " << finalResult._outputAttachments[c].second << " (" << finalResult._mergedFragment._attachments[finalResult._outputAttachments[c].second]._desc << ")" << std::endl;
            debugInfo << "MergeFragments() finished." << std::endl;
            finalResult._log = debugInfo.str();
        #endif
        
        return finalResult;
    }

    static AttachmentName RemapAttachmentName(
        AttachmentName input,
        const RenderCore::Techniques::FrameBufferDescFragment& srcFragment,
        RenderCore::Techniques::FrameBufferDescFragment& dstFragment,
        std::vector<std::pair<RenderCore::AttachmentName, RenderCore::AttachmentName>>& remapping)
    {
        if (input == ~0u) return input;

        auto existing = LowerBound(remapping, input);
        if (existing == remapping.end() || existing->first != input) {
            auto semantic = srcFragment._attachments[input].GetInputSemanticBinding();
            auto newName = dstFragment.DefineAttachment(semantic, srcFragment._attachments[input]._desc);
            existing = remapping.insert(existing, {input, newName});
        }

        return existing->second;
    }

    bool CanBeSimplified(
        const FrameBufferDescFragment& inputFragment,
        IteratorRange<const PreregisteredAttachment*> systemAttachments,
        const FrameBufferProperties& fbProps)
    {
        TRY
        {
            std::vector<FrameBufferDescFragment> testFragments;
            // Create a separate fragment for each subpass
            for (const auto&subpass:inputFragment._subpasses) {
                std::vector<std::pair<AttachmentName, AttachmentName>> remapping;
                FrameBufferDescFragment separatedFragment;
                auto remappedSubpass = RemapSubpassDesc(
					subpass,
					std::bind(&RemapAttachmentName, std::placeholders::_1, std::ref(inputFragment), std::ref(separatedFragment), std::ref(remapping)));
                separatedFragment.AddSubpass(std::move(remappedSubpass));
                testFragments.emplace_back(std::move(separatedFragment));
            }
            auto collapsed = MergeFragments(
                systemAttachments, MakeIteratorRange(testFragments), fbProps);
            assert(collapsed._mergedFragment._attachments.size() <= inputFragment._attachments.size());
            if (collapsed._mergedFragment._attachments.size() < inputFragment._attachments.size()) {
                return true;
            }
            return false;
        } CATCH(const std::exception& e) {
            Log(Warning) << "Error during AnalyzeFragment while processing render step: " << e.what() << std::endl;
        } CATCH_END
        return false;
    }

    static BindFlag::BitField CalculateBindFlags(const FrameBufferDescFragment& fragment, unsigned attachmentName)
    {
        BindFlag::BitField result = 0;
        for (const auto& spDesc:fragment._subpasses) {
            for (const auto& r:spDesc.GetOutputs())
                if (r._resourceName == attachmentName)
                    result |= BindFlag::RenderTarget;
			if (spDesc.GetDepthStencil()._resourceName == attachmentName)
				result |= BindFlag::DepthStencil;
			for (const auto& r:spDesc.GetInputs())
                if (r._resourceName == attachmentName)
                    result |= BindFlag::InputAttachment;
            for (const auto& r:spDesc.GetViews())
                if (r._resourceName == attachmentName)
                    result |= r._usage;
        }
        return result;
    }

}}

