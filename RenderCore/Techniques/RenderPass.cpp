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
#include "../IDevice.h"
#include "../../OSServices/Log.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../Utility/StreamUtils.h"
#include "../../Utility/StringFormat.h"
#include <cmath>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <set>

namespace RenderCore
{
    static std::ostream& operator<<(std::ostream& str, const AttachmentDesc& attachment)
    {
        str << "AttachmentDesc {";
        #if defined(_DEBUG)
            if (!attachment._name.empty()) str << "\"" << attachment._name << "\"";
        #endif
        str << " " << AsString(attachment._format)
            << ", L:" << AsString(attachment._loadFromPreviousPhase) << " " << BindFlagsAsString(attachment._initialLayout)
            << ", S:" << AsString(attachment._storeToNextPhase) << " " << BindFlagsAsString(attachment._finalLayout)
            << ", 0x" << std::hex << attachment._flags << std::dec
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
        str << ", " << BindFlagsAsString(desc._bindFlags);
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

    AttachmentMatchingRules& AttachmentMatchingRules::FixedFormat(Format fmt)
    {
        _flagsSet &= ~(uint32_t(Flags::SystemFormat)|uint32_t(Flags::CopyFormatFromSemantic));
        _flagsSet |= uint32_t(Flags::FixedFormat);
        _fixedFormat = fmt;
        return *this;
    }

    AttachmentMatchingRules& AttachmentMatchingRules::SystemAttachmentFormat(Techniques::SystemAttachmentFormat fmt)
    {
        _flagsSet &= ~(uint32_t(Flags::CopyFormatFromSemantic)|uint32_t(Flags::FixedFormat));
        _flagsSet |= uint32_t(Flags::SystemFormat);
        _systemFormat = fmt;
        return *this;
    }

    AttachmentMatchingRules& AttachmentMatchingRules::CopyFormat(uint64_t srcSemantic)
    {
        _flagsSet &= ~(uint32_t(Flags::SystemFormat)|uint32_t(Flags::FixedFormat));
        _flagsSet |= uint32_t(Flags::CopyFormatFromSemantic);
        _copyFormatSrc = srcSemantic;
        return *this;
    }

    AttachmentMatchingRules& AttachmentMatchingRules::RequireBindFlags(BindFlag::BitField flags)
    {
        _requiredBindFlags |= flags;
        return *this;
    }

    AttachmentMatchingRules& AttachmentMatchingRules::MultisamplingMode(bool enable)
    {
        _flagsSet |= uint32_t(Flags::MultisamplingMode);
        _multisamplingMode = enable;
        return *this;
    }

    FrameBufferDescFragment::DefineAttachmentHelper FrameBufferDescFragment::DefineAttachment(uint64_t semantic)
    {
        auto name = (AttachmentName)_attachments.size();
        Attachment attachment;
        attachment._semantic = semantic;
        attachment._loadFromPreviousPhase = LoadStore::Retain;
        attachment._storeToNextPhase = LoadStore::Retain;
        attachment._initialLayout = 0;
        attachment._finalLayout = 0;
        _attachments.push_back(attachment);
        return DefineAttachmentHelper{this, name};
    }

    auto FrameBufferDescFragment::DefineAttachment(const Attachment& attachment) -> DefineAttachmentHelper
    {
        if (attachment._semantic != 0) {
            for (const auto& a:_attachments) assert(a._semantic != attachment._semantic);
        }
        auto name = (AttachmentName)_attachments.size();
        _attachments.push_back(attachment);
        return DefineAttachmentHelper{this, name};
    }

    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::Clear()
    {
        _fragment->_attachments[_attachmentName]._initialLayout = 0;
        _fragment->_attachments[_attachmentName]._loadFromPreviousPhase = LoadStore::Clear;
        return *this;
    }

    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::Discard()
    {
        _fragment->_attachments[_attachmentName]._finalLayout = 0;
        _fragment->_attachments[_attachmentName]._storeToNextPhase = LoadStore::DontCare;
        return *this;
    }

    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::InitialState(BindFlag::BitField flags)
    {
        _fragment->_attachments[_attachmentName]._initialLayout = flags;
        _fragment->_attachments[_attachmentName]._loadFromPreviousPhase = LoadStore::Retain;
        return *this;
    }

    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::FinalState(BindFlag::BitField flags)
    {
        _fragment->_attachments[_attachmentName]._finalLayout = flags;
        _fragment->_attachments[_attachmentName]._storeToNextPhase = LoadStore::Retain;
        return *this;
    }

    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::NoInitialState()
    {
        _fragment->_attachments[_attachmentName]._initialLayout = 0;
        _fragment->_attachments[_attachmentName]._loadFromPreviousPhase = LoadStore::DontCare;
        return *this;
    }

    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::InitialState(LoadStore loadStore, BindFlag::BitField flags)
    {
        _fragment->_attachments[_attachmentName]._initialLayout = flags;
        _fragment->_attachments[_attachmentName]._loadFromPreviousPhase = loadStore;
        return *this;
    }

    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::FinalState(LoadStore loadStore, BindFlag::BitField flags)
    {
        _fragment->_attachments[_attachmentName]._finalLayout = flags;
        _fragment->_attachments[_attachmentName]._storeToNextPhase = loadStore;
        return *this;
    }

    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::FixedFormat(Format fmt)
    {
        _fragment->_attachments[_attachmentName]._matchingRules.FixedFormat(fmt);
        return *this;
    }

    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::SystemAttachmentFormat(Techniques::SystemAttachmentFormat fmt)
    {
        _fragment->_attachments[_attachmentName]._matchingRules.SystemAttachmentFormat(fmt);
        return *this;
    }

    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::RequireBindFlags(BindFlag::BitField flags)
    {
        _fragment->_attachments[_attachmentName]._matchingRules.RequireBindFlags(flags);
        return *this;
    }
    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::MultisamplingMode(bool enable)
    {
        _fragment->_attachments[_attachmentName]._matchingRules.MultisamplingMode(enable);
        return *this;
    }
    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::CopyFormat(uint64_t srcSemantic)
    {
        _fragment->_attachments[_attachmentName]._matchingRules.CopyFormat(srcSemantic);
        return *this;
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

    unsigned FrameBufferDescFragment::SubpassDesc::AppendNonFrameBufferAttachmentView(AttachmentName name, BindFlag::Enum usage, TextureViewDesc window)
    {
        auto result = (unsigned)_views.size();
        ViewedAttachment view;
        view._resourceName = name;
        view._window = window;
        view._usage = usage;
        _views.push_back(view);
        return result;
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
            unsigned frameIdx,
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
        Entry _entries[24];
        unsigned _currentTickId = 0;

        void IncreaseTickId();
    };

    void FrameBufferPool::IncreaseTickId()
    {
        // look for old FBs, and evict; then just increase the tick id
        const unsigned evictionRange = 2*dimof(_entries);
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
        unsigned frameIdx,
        AttachmentPool& attachmentPool) -> Result
    {    
        auto poolAttachments = attachmentPool.Reserve(resolvedAttachmentDescs, frameIdx);
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
        // if ((_currentTickId - tickIdOfEarliestEntry) < 2*dimof(_entries)) {
        //      Log(Warning) << "Creating frame buffers frequently, consider increasing depth of framebuffer pool" << std::endl;
        // }

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
                _attachedContext->BeginLabel(_layout->GetSubpasses()[_currentSubpassIndex+1]._name.empty() ? "<<unnnamed subpass>>" : _layout->GetSubpasses()[_currentSubpassIndex+1]._name.c_str());
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

        if (_attachedParsingContext) {
            assert(_attachedParsingContext->_rpi == this);
            _attachedParsingContext->_rpi = nullptr;
            _attachedParsingContext = nullptr;
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
		const auto& subPass = _layout->GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetInputs()[inputAttachmentSlot]._resourceName;
		assert(_attachmentPool);
        if (resName < _attachmentPoolReservation.GetResourceIds().size())
            return _attachmentPool->GetResource(_attachmentPoolReservation.GetResourceIds()[resName]);
        return s_nullResourcePtr;
	}

    auto RenderPassInstance::GetInputAttachmentView(unsigned inputAttachmentSlot) const -> const std::shared_ptr<IResourceView>&
	{
		const auto& subPass = _layout->GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetInputs()[inputAttachmentSlot]._resourceName;
		assert(_attachmentPool);
        if (resName < _attachmentPoolReservation.GetResourceIds().size())
            return _attachmentPool->GetView(_attachmentPoolReservation.GetResourceIds()[resName], BindFlag::InputAttachment, subPass.GetInputs()[inputAttachmentSlot]._window);
        return s_nullResourceViewPtr;
	}
	
	auto RenderPassInstance::GetOutputAttachmentResource(unsigned outputAttachmentSlot) const -> const std::shared_ptr<IResource>&
	{
		const auto& subPass = _layout->GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetOutputs()[outputAttachmentSlot]._resourceName;
		assert(_attachmentPool);
        if (resName < _attachmentPoolReservation.GetResourceIds().size())
            return _attachmentPool->GetResource(_attachmentPoolReservation.GetResourceIds()[resName]);
        return s_nullResourcePtr;
	}
	
	auto RenderPassInstance::GetOutputAttachmentSRV(unsigned outputAttachmentSlot, const TextureViewDesc& window) const -> const std::shared_ptr<IResourceView>&
	{
		const auto& subPass = _layout->GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetOutputs()[outputAttachmentSlot]._resourceName;
		assert(_attachmentPool);
        if (resName < _attachmentPoolReservation.GetResourceIds().size())
            return _attachmentPool->GetSRV(_attachmentPoolReservation.GetResourceIds()[resName], window);
        return s_nullResourceViewPtr;
	}

	auto RenderPassInstance::GetDepthStencilAttachmentSRV(const TextureViewDesc& window) const -> const std::shared_ptr<IResourceView>&
	{
		const auto& subPass = _layout->GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetDepthStencil()._resourceName;
		assert(_attachmentPool);
        if (resName < _attachmentPoolReservation.GetResourceIds().size())
            return _attachmentPool->GetSRV(_attachmentPoolReservation.GetResourceIds()[resName], window);
        return s_nullResourceViewPtr;
	}

	auto RenderPassInstance::GetDepthStencilAttachmentResource() const -> const std::shared_ptr<IResource>&
	{
		const auto& subPass = _layout->GetSubpasses()[GetCurrentSubpassIndex()];
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
            layout, fullAttachmentsDescription, beginInfo._frameIdx, attachmentPool);
        fb._poolReservation.CompleteInitialization(context);

        _frameBuffer = std::move(fb._frameBuffer);
        _attachmentPoolReservation = std::move(fb._poolReservation);
        _attachmentPool = &attachmentPool;
        _layout = fb._completedDesc;        // expecting this to be retained by the pool until at least the destruction of this
        // todo -- we might need to pass offset & extent parameters to BeginRenderPass
        // this could be derived from _attachmentPool->GetFrameBufferProperties()?
        _trueRenderPass = true;

        #if defined(_DEBUG)
            _attachedContext->BeginLabel(_layout->GetSubpasses()[0]._name.empty() ? "<<unnnamed subpass>>" : _layout->GetSubpasses()[0]._name.c_str());
        #endif
        _attachedContext->BeginRenderPass(*_frameBuffer, beginInfo._clearValues);
        _attachedParsingContext = nullptr;
    }

    RenderPassInstance::RenderPassInstance(
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
                parsingContext.GetThreadContext(), stitchedFragment._fbDesc, stitchedFragment._fullAttachmentDescriptions,
                *parsingContext.GetTechniqueContext()._frameBufferPool,
                *parsingContext.GetTechniqueContext()._attachmentPool,
                beginInfo };
            parsingContext.GetViewport() = _frameBuffer->GetDefaultViewport();
        } else {
            auto& attachmentPool = *parsingContext.GetTechniqueContext()._attachmentPool;
            _attachmentPoolReservation = attachmentPool.Reserve(stitchedFragment._fullAttachmentDescriptions, beginInfo._frameIdx);
            _attachmentPool = &attachmentPool;
            _attachmentPoolReservation.CompleteInitialization(parsingContext.GetThreadContext());
            _layout = &stitchedFragment._fbDesc;
            // clear not supported in this mode
            for (const auto& a:_layout->GetAttachments())
                assert(!HasClear(a._loadFromPreviousPhase));

            _attachedContext = Metal::DeviceContext::Get(parsingContext.GetThreadContext()).get();
            #if defined(_DEBUG)
                _attachedContext->BeginLabel(_layout->GetSubpasses()[0]._name.empty() ? "<<unnnamed subpass>>" : _layout->GetSubpasses()[0]._name.c_str());
            #endif
        }

        assert(!parsingContext._rpi);
        parsingContext._rpi = this;
        _attachedParsingContext = &parsingContext;

        // Update the parsing context with the changes to attachments
        stitchContext.UpdateAttachments(stitchedFragment);
        for (unsigned aIdx=0; aIdx<stitchedFragment._attachmentTransforms.size(); ++aIdx) {
            auto semantic = stitchedFragment._fullAttachmentDescriptions[aIdx]._semantic;
            if (!semantic) continue;

            switch (stitchedFragment._attachmentTransforms[aIdx]._type) {
            case FragmentStitchingContext::AttachmentTransform::Temporary:
            case FragmentStitchingContext::AttachmentTransform::Preserved:
                break;
            case FragmentStitchingContext::AttachmentTransform::Written:
            case FragmentStitchingContext::AttachmentTransform::Generated:
                parsingContext.GetTechniqueContext()._attachmentPool->Bind(semantic, _attachmentPoolReservation.GetResourceIds()[aIdx]);
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
        ParsingContext& parsingContext,
        const FrameBufferDescFragment& layout,
        const RenderPassBeginDesc& beginInfo)
    {
        _attachedContext = nullptr;
        _attachmentPool = nullptr;
        _trueRenderPass = false;
        _attachedParsingContext = nullptr;
        auto& stitchContext = parsingContext.GetFragmentStitchingContext();
        auto stitchResult = stitchContext.TryStitchFrameBufferDesc(MakeIteratorRange(&layout, &layout+1));
        // todo -- have to protect lifetime of stitchResult._fbDesc in this case
        // candidate for subframe heap
        // just copy stitchResult._fbDesc somewhere that will last to the end of the frame
        *this = RenderPassInstance { parsingContext, stitchResult, beginInfo };
    }

	RenderPassInstance::RenderPassInstance(
        const FrameBufferDesc& layout,
        IteratorRange<const PreregisteredAttachment*> resolvedAttachmentDescs,
        AttachmentPool& attachmentPool,
        unsigned frameIdx)
	: _layout(&layout)
    {
		// This constructs a kind of "non-metal" RenderPassInstance
		// It allows us to use the RenderPassInstance infrastructure (for example, for remapping attachment requests)
		// without actually constructing a underlying metal renderpass.
		// This is used with compute pipelines sometimes -- since in Vulkan, those have some similarities with
		// graphics pipelines, but are incompatible with the vulkan render passes
		_attachedContext = nullptr;
        _trueRenderPass = false;
        _attachedParsingContext = nullptr;
		_attachmentPoolReservation = attachmentPool.Reserve(resolvedAttachmentDescs, frameIdx);
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
        moveFrom._layout = nullptr;

        if (moveFrom._attachedParsingContext) {
            assert(moveFrom._attachedParsingContext->_rpi == &moveFrom);
            moveFrom._attachedParsingContext->_rpi = this;
            _attachedParsingContext = moveFrom._attachedParsingContext;
            moveFrom._attachedParsingContext = nullptr;
        }
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
        moveFrom._layout = nullptr;
        _viewedAttachments = std::move(moveFrom._viewedAttachments);
        _viewedAttachmentsMap = std::move(moveFrom._viewedAttachmentsMap);
        _currentSubpassIndex = moveFrom._currentSubpassIndex;
        moveFrom._currentSubpassIndex = 0;
        _trueRenderPass = moveFrom._trueRenderPass;
        moveFrom._trueRenderPass = false;

        if (moveFrom._attachedParsingContext) {
            assert(moveFrom._attachedParsingContext->_rpi == &moveFrom);
            moveFrom._attachedParsingContext->_rpi = this;
            _attachedParsingContext = moveFrom._attachedParsingContext;
            moveFrom._attachedParsingContext = nullptr;
        }
        return *this;
    }

    RenderPassInstance::RenderPassInstance()
    {
        _attachedContext = nullptr;
        _attachmentPool = nullptr;
        _trueRenderPass = false;
        _attachedParsingContext = nullptr;
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
        std::vector<std::pair<uint64_t, unsigned>> _poolSemanticAttachments;

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
        attach->_pendingCompleteInitialization = true;
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
    }

    static unsigned GetArrayCount(unsigned arrayCount) { return arrayCount; }           // { return (arrayCount == 0) ? 1 : arrayCount; }

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
        VLA(bool, consumed, _pimpl->_attachments.size());
        VLA(bool, consumedSemantic, _pimpl->_semanticAttachments.size());
        for (unsigned c=0; c<_pimpl->_attachments.size(); ++c) consumed[c] = false;
        for (unsigned c=0; c<_pimpl->_semanticAttachments.size(); ++c) consumedSemantic[c] = false;
        auto originalAttachmentsSize = _pimpl->_attachments.size();

        // Treat any attachments that are bound to semantic values as "consumed" already.
        // In other words, we can't give these attachments to requests without a semantic,
        // or using another semantic.
        for (unsigned c=0; c<_pimpl->_attachments.size(); ++c)
            consumed[c] = _pimpl->_attachments[c]._lockCount > 0;
        for (unsigned s=0; s<_pimpl->_semanticAttachments.size(); ++s)
            consumedSemantic[s] = _pimpl->_semanticAttachments[s]._lockCount > 0;

        std::vector<AttachmentName> selectedAttachments;
        selectedAttachments.resize(attachmentRequests.size(), ~0u);

        for (unsigned r=0; r<attachmentRequests.size(); ++r) {
            const auto& request = attachmentRequests[r];

            // If a semantic value is set, we should first check to see if the request can match
            // one of the bound attachments.
            bool foundMatch = false;
            if (!request._semantic) continue;

            auto requestSemantic = request._semantic;
            for (unsigned q=0; q<_pimpl->_semanticAttachments.size(); ++q) {
                if (requestSemantic == _pimpl->_semanticAttachments[q]._semantic && !consumedSemantic[q] && _pimpl->_semanticAttachments[q]._resource) {
                    #if defined(_DEBUG)
                        if (!MatchRequest(request._desc, _pimpl->_semanticAttachments[q]._desc)) {
                            Log(Warning) << "Attachment bound to the pool for semantic (" << AttachmentSemantic{request._semantic} << ") does not match the request for this semantic. Attempting to use it anyway. Request: "
                                << request._desc << ", Bound to pool: " << _pimpl->_semanticAttachments[q]._desc
                                << std::endl;
                        }
                    #endif

                    consumedSemantic[q] = true;
                    foundMatch = true;
                    selectedAttachments[r] = q | (1u<<31u);
                    break;
                }
            }
            if (!foundMatch) {
                auto i = LowerBound(_pimpl->_poolSemanticAttachments, requestSemantic);
                if (i!=_pimpl->_poolSemanticAttachments.end() && i->first == requestSemantic && !consumed[i->second]) {
                    consumed[i->second] = true;
                    selectedAttachments[r] = i->second;
                }
            }
        }

        for (unsigned r=0; r<attachmentRequests.size(); ++r) {
            const auto& request = attachmentRequests[r];
            
            // If we didn't find a match in one of our bound semantic attachments, we must flow
            // through and treat it as a temporary attachment.
            if (selectedAttachments[r] != ~0u) continue;

            // If we haven't found a match yet, we must treat the request as a temporary buffer
            // We will go through and either find an existing buffer or create a new one
            bool foundMatch = false;
            for (unsigned q=0; q<_pimpl->_attachments.size(); ++q) {
                // Any of the attachments in _poolSemanticAttachments can potentially be used for that semantic data at a later time.
                // We can't really tell if the data has been abandoned for sure -- so side on the safe side, and don't attempt to reuse it
                auto i = std::find_if(_pimpl->_poolSemanticAttachments.begin(), _pimpl->_poolSemanticAttachments.end(), [q](const auto& c) { return c.second == q; });
                if (i != _pimpl->_poolSemanticAttachments.end()) continue;

                if (MatchRequest(request._desc, _pimpl->_attachments[q]._desc) && q < originalAttachmentsSize && !consumed[q]) {
                    consumed[q] = true;
                    selectedAttachments[r] = q;
                    foundMatch = true;
                    break;
                }
            }

            if (!foundMatch) {
                _pimpl->_attachments.push_back(Pimpl::Attachment{nullptr, request._desc});
                selectedAttachments[r] = unsigned(_pimpl->_attachments.size()-1);
            }
        }

        AddRef(MakeIteratorRange(selectedAttachments), flags);
        return Reservation(std::move(selectedAttachments), this, flags);
    }

    void AttachmentPool::Bind(uint64_t semantic, const IResourcePtr& resource)
    {
        assert(resource);
        assert(semantic != 0);      // using zero as a semantic is not supported; this is used as a sentinel for "no semantic"
        {
            
            auto i = std::find_if(
                _pimpl->_attachments.begin(), _pimpl->_attachments.end(),
                [res=resource.get()](const auto& attach) {
                    return attach._resource.get() == res;
                });
            if (i != _pimpl->_attachments.end()) {
                Bind(semantic, (AttachmentName)std::distance(_pimpl->_attachments.begin(), i));
                return;
            }
        }
        
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
                [](const Pimpl::SemanticAttachment& a) {
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

    void AttachmentPool::Bind(uint64_t semantic, AttachmentName resName)
    {
        assert(semantic);
        if (resName & (1u<<31u)) return;

        auto existing = std::find_if(
            _pimpl->_poolSemanticAttachments.begin(), _pimpl->_poolSemanticAttachments.end(),
            [resName](const auto& c) { return c.second == resName; });
        if (existing != _pimpl->_poolSemanticAttachments.end()) {
            if (existing->first == semantic) return;
            _pimpl->_poolSemanticAttachments.erase(existing);
        }

        auto i = LowerBound(_pimpl->_poolSemanticAttachments, semantic);
        if (i != _pimpl->_poolSemanticAttachments.end() && i->first == semantic) {
            i->second = resName;
        } else
            _pimpl->_poolSemanticAttachments.insert(i, {semantic, resName});
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
        assert(semantic);
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

        auto binding2 = LowerBound(_pimpl->_poolSemanticAttachments, semantic);
        if (binding2 != _pimpl->_poolSemanticAttachments.end() && binding2->first == semantic)
            _pimpl->_poolSemanticAttachments.erase(binding2);
    }

    void AttachmentPool::UnbindAll()
    {
        // we only unbind the external resources bound to semantics.
        // _pimpl->_poolSemanticAttachments is left alone. This is because this contains
        // some assignments that last from frame-to-frame (such as ping pong buffer)
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

        auto binding2 = LowerBound(_pimpl->_poolSemanticAttachments, semantic);
        if (binding2 != _pimpl->_poolSemanticAttachments.end() && binding2->first == semantic)
            return _pimpl->_attachments[binding2->second]._resource;
		return nullptr;
	}

    void AttachmentPool::ResetActualized()
    {
        // Reset all actualized attachments. They will get recreated on demand
        _pimpl->_attachments.clear();
        _pimpl->_poolSemanticAttachments.clear();
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

    void AttachmentPool::FlipDoubleBufferAttachments(
        IThreadContext& threadContext,
        IteratorRange<const DoubleBufferAttachment*> attachments)
    {
        // todo -- flip into _initialLayout? particularly post clear op

        for (const auto& a:attachments) {
            auto A = GetBoundResource(a._todaySemantic);
            auto B = GetBoundResource(a._yesterdaySemantic);
            Unbind(a._todaySemantic);
            Unbind(a._yesterdaySemantic);
            assert(!A || A != B);
            if (A) {
                Bind(a._yesterdaySemantic, A);
            } else {
                // There was no last frame -- we need to create or find an attachment for this, and
                // make sure it's cleared with the initial data required
                PreregisteredAttachment preReg;
                preReg._desc = a._desc;
                preReg._desc._bindFlags |= BindFlag::TransferDst;     // need TransferDst for a clear op
                auto reservation = Reserve(MakeIteratorRange(&preReg, &preReg+1));
                if (reservation.HasPendingCompleteInitialization()) {
                    reservation.CompleteInitialization(threadContext);
                    auto& metalContext = *Metal::DeviceContext::Get(threadContext);
                    if (a._desc._bindFlags & BindFlag::RenderTarget) {
                        auto rtv = GetView(reservation.GetResourceIds()[0], BindFlag::RenderTarget);
                        metalContext.Clear(*rtv, a._initialContents._float);
                    } else if (a._desc._bindFlags & BindFlag::UnorderedAccess) {
                        auto uav = GetView(reservation.GetResourceIds()[0], BindFlag::UnorderedAccess);
                        metalContext.ClearFloat(*uav, a._initialContents._float);
                    } else if (a._desc._bindFlags & BindFlag::DepthStencil) {
                        auto dsv = GetView(reservation.GetResourceIds()[0], BindFlag::DepthStencil);
                        auto components = GetComponents(preReg._desc._textureDesc._format);
                        ClearFilter::BitField clearFilter = 0;
                        if (components == FormatComponents::Depth || components == FormatComponents::DepthStencil)
                            clearFilter |= ClearFilter::Depth;
                        if (components == FormatComponents::Stencil || components == FormatComponents::DepthStencil)
                            clearFilter |= ClearFilter::Stencil;
                        metalContext.Clear(*dsv, clearFilter, a._initialContents._depthStencil._depth, a._initialContents._depthStencil._stencil);
                    } else {
                        Throw(std::runtime_error("Unable to initialize double buffered attachment, because no writable bind flags were given"));
                    }
                }
                Bind(a._yesterdaySemantic, reservation.GetResourceIds()[0]);
            }
            if (B) {
                // Explicitly use the "yesterday" attachment as the "today" attachment, rather
                // than just dropping it back in the pool
                // This will minimize the number of separate metal frame buffers actually created
                // We don't have to do anything special when it's missing -- the standard path for this
                // is fine
                Bind(a._todaySemantic, B);
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
        VLA(IResource*, resources, _reservedAttachments.size());
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
        auto loadOp = fragment._attachments[attachment]._loadFromPreviousPhase;
        auto storeOp = fragment._attachments[attachment]._storeToNextPhase;
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

    class WorkingAttachmentContext
    {
    public:
        struct Attachment
        {
            AttachmentName _name = ~0u;

            uint64_t _shouldReceiveDataForSemantic = 0;         // when looking for an attachment to write the data for this semantic, prefer this attachment
            uint64_t _containsDataForSemantic = 0;              // the data for this semantic is already written to this attachment

            uint64_t _firstAccessSemantic = 0;
            LoadStore _firstAccessLoad = LoadStore::DontCare;
            BindFlag::BitField _firstAccessInitialLayout = 0;
            uint64_t _lastWriteSemantic = 0;
            LoadStore _lastAccessStore = LoadStore::DontCare;
            BindFlag::BitField _lastAccessFinalLayout = 0;

            bool _hasBeenAccessed = false;
            std::optional<PreregisteredAttachment> _fullyDefinedAttachment;
            AttachmentMatchingRules _matchingRules;

            std::optional<Attachment> TryMerge(const AttachmentMatchingRules& matchingRules, const FrameBufferProperties& fbProps) const;

            Attachment(const PreregisteredAttachment& attachment);
            Attachment(const AttachmentMatchingRules& matchingRules);
            Attachment() {}
        };
        std::vector<Attachment> _attachments;

        std::optional<Attachment> MatchAttachment(const AttachmentMatchingRules& matchingRules, uint64_t semantic, LoadStore loadMode, const FrameBufferProperties& fbProps);
    };

    static Format ResolveSystemFormat(SystemAttachmentFormat fmt)
    {
        assert(0);
        return Format::Unknown;
    }

    static TextureSamples GetSamples(const AttachmentMatchingRules& matchingRules, const FrameBufferProperties& props)
    {
        if (!(matchingRules._flagsSet & (uint32_t)AttachmentMatchingRules::Flags::MultisamplingMode))
            return TextureSamples::Create();
        return (matchingRules._multisamplingMode & AttachmentDesc::Flags::Multisampled) ? props._samples : TextureSamples::Create();
    }

    static bool FormatCompatible(Format lhs, Format rhs)
    {
        if (lhs == rhs) return true;
        auto    lhsTypeless = AsTypelessFormat(lhs),
                rhsTypeless = AsTypelessFormat(rhs);
        return lhsTypeless == rhsTypeless;
    }

    static AttachmentName NextName(IteratorRange<const WorkingAttachmentContext::Attachment*> attachments0, IteratorRange<const WorkingAttachmentContext::Attachment*> attachments1)
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

    static bool IsCompatible(const AttachmentMatchingRules& matchingRules, const PreregisteredAttachment& pregAttach, const FrameBufferProperties& fbProps)
    {
        if (matchingRules._flagsSet & (uint32_t)AttachmentMatchingRules::Flags::FixedFormat)
            if (matchingRules._fixedFormat != pregAttach._desc._textureDesc._format)
                return false;
        if (matchingRules._flagsSet & (uint32_t)AttachmentMatchingRules::Flags::SystemFormat) {
            auto fmt = ResolveSystemFormat(matchingRules._systemFormat);
            if (fmt != pregAttach._desc._textureDesc._format)
                return false;
        }
        if (matchingRules._flagsSet & (uint32_t)AttachmentMatchingRules::Flags::CopyFormatFromSemantic) {
            assert(0);      // todo
            return false;
        }
        // Note that when the multisample mode flag is not set it will mean that we ignore multisampling as a criteria -- ie, either
        // multisampled or non multisampled can be selected
        if (matchingRules._flagsSet & (uint32_t)AttachmentMatchingRules::Flags::MultisamplingMode) {
            if (!(GetSamples(matchingRules, fbProps) == pregAttach._desc._textureDesc._samples))
                return false;
        }

        if ((matchingRules._requiredBindFlags & pregAttach._desc._bindFlags) != matchingRules._requiredBindFlags)
            return false;      // doesn't have all of the bind flags we need
        return true;
    }

    auto WorkingAttachmentContext::Attachment::TryMerge(const AttachmentMatchingRules& matchingRules, const FrameBufferProperties& fbProps) const -> std::optional<Attachment>
    {
        if (_fullyDefinedAttachment) {
            if (!IsCompatible(matchingRules, *_fullyDefinedAttachment, fbProps))
                return {};
            return *this;
        } else {
            Attachment merge = *this;

            if (    matchingRules._flagsSet & ((uint32_t)AttachmentMatchingRules::Flags::CopyFormatFromSemantic|(uint32_t)AttachmentMatchingRules::Flags::SystemFormat)
                ||  merge._matchingRules._flagsSet & ((uint32_t)AttachmentMatchingRules::Flags::CopyFormatFromSemantic|(uint32_t)AttachmentMatchingRules::Flags::SystemFormat)) {
                // We should not get there, because the matching rules should be simplied by converting CopyFormatFromSemantic & SystemFormat
                // to FixedFormat before we get here
                assert(0);
            }

            if (    matchingRules._flagsSet & (uint32_t)AttachmentMatchingRules::Flags::FixedFormat) {
                if (merge._matchingRules._flagsSet & (uint32_t)AttachmentMatchingRules::Flags::FixedFormat) {
                    if (!FormatCompatible(matchingRules._fixedFormat, merge._matchingRules._fixedFormat))
                        return {};
                    // If the formats are not exactly the same, we must downgrade to a typeless format
                    if (matchingRules._fixedFormat != merge._matchingRules._fixedFormat)
                        merge._matchingRules._fixedFormat = AsTypelessFormat(matchingRules._fixedFormat);
                } else {
                    // Original didn't have any rules for the format.
                    // copy across the format matching rules to the output
                    merge._matchingRules._flagsSet |= uint32_t(AttachmentMatchingRules::Flags::FixedFormat);
                    merge._matchingRules._fixedFormat = matchingRules._fixedFormat;
                }
            }

            if (matchingRules._flagsSet & (uint32_t)AttachmentMatchingRules::Flags::MultisamplingMode) {
                if (merge._matchingRules._flagsSet & (uint32_t)AttachmentMatchingRules::Flags::MultisamplingMode) {
                    if (matchingRules._multisamplingMode != merge._matchingRules._multisamplingMode)
                        return {};
                } else {
                    merge._matchingRules._flagsSet |= (uint32_t)AttachmentMatchingRules::Flags::MultisamplingMode;
                    merge._matchingRules._multisamplingMode = matchingRules._multisamplingMode;
                }
            }

            merge._matchingRules._requiredBindFlags |= matchingRules._requiredBindFlags;

            return merge;
        }
    }

    WorkingAttachmentContext::Attachment::Attachment(const PreregisteredAttachment& attachment)
    {
        if (attachment._state == PreregisteredAttachment::State::Initialized
            || attachment._state == PreregisteredAttachment::State::Initialized_StencilUninitialized
            || attachment._state == PreregisteredAttachment::State::Uninitialized_StencilInitialized)
            _containsDataForSemantic = attachment._semantic;
        _shouldReceiveDataForSemantic = attachment._semantic;
        _firstAccessInitialLayout = attachment._layoutFlags;
        _fullyDefinedAttachment = attachment;
    }

    WorkingAttachmentContext::Attachment::Attachment(
        const AttachmentMatchingRules& matchingRules)
    {
        _matchingRules = matchingRules;
    }

    auto WorkingAttachmentContext::MatchAttachment(const AttachmentMatchingRules& matchingRules, uint64_t semantic, LoadStore loadMode, const FrameBufferProperties& fbProps) -> std::optional<Attachment>
    {
        // Attempt to find an attachment in "_attachments" that matches the request
        // If we find one, remove it from our array and return it
        std::optional<Attachment> result;
        
        bool requiresPreinitData = HasRetain(loadMode); // requires preinitialized data -- only match against attachments with the right semantic & already have some data present
        if (requiresPreinitData) {
            assert(semantic != 0);
        }

        // look for an existing attachment that matches the given matching rules
        if (requiresPreinitData) {
            for (auto i=_attachments.begin(); i!=_attachments.end(); ++i) {
                if (i->_containsDataForSemantic == semantic) {
                    result = i->TryMerge(matchingRules, fbProps);
                    if (!result.has_value()) {
                        if (!semantic) continue;
                        return {};     // fail immediately if the matching semantic attachment is incompatible with the request
                    }
                    _attachments.erase(i);                  // remove from our array
                    break;
                }
            }
        } else {
            for (auto i=_attachments.begin(); i!=_attachments.end(); ++i) {
                if (i->_shouldReceiveDataForSemantic == semantic) {
                    result  = i->TryMerge(matchingRules, fbProps);
                    if (!result.has_value()) {
                        if (!semantic) continue;
                        return {};     // fail immediately if the matching semantic attachment is incompatible with the request
                    }
                    _attachments.erase(i);                  // remove from our array
                    break;
                }
            }

            // For attachments that don't require preinitialized data, widen the search and good for something we can 
            // reuse, even if the semantic doesn't match
            if (!result.has_value()) {
                for (auto i=_attachments.begin(); i!=_attachments.end(); ++i) {
                    // only consider uninitialized/empty attachments in this case
                    if (i->_shouldReceiveDataForSemantic != 0 || i->_containsDataForSemantic != 0) continue;
                    if (auto newState = i->TryMerge(matchingRules, fbProps)) {
                        _attachments.erase(i);      // remove from our array
                        result = newState;
                        break;
                    }
                }
            }

            // nothing found, create something new and fill as much as we can
            if (!result.has_value())
                result = Attachment{matchingRules};
        }

        return result;
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
            << BindFlagsAsString(attachment._layoutFlags) << "}";
        return str;
    }

    static const char* AsString(SystemAttachmentFormat fmt)
    {
        switch (fmt) {
        case SystemAttachmentFormat::LDRColor: return "LDRColor";
        case SystemAttachmentFormat::HDRColor: return "HDRColor";
        case SystemAttachmentFormat::TargetColor: return "TargetColor";
        case SystemAttachmentFormat::MainDepthStencil: return "MainDepthStencil";
        case SystemAttachmentFormat::LowDetailDepth: return "LowDetailDepth";
        case SystemAttachmentFormat::ShadowDepth: return "ShadowDepth";
        default: return "<<unknown>>";
        }
    }

    static std::ostream& operator<<(std::ostream& str, const AttachmentMatchingRules& rules)
    {
        str << "Matching {";
        const char* pending = " ";
        if (rules._flagsSet & (uint32_t)AttachmentMatchingRules::Flags::FixedFormat) {
            str << pending << AsString(rules._fixedFormat); pending = ", ";
        }
        if (rules._flagsSet & (uint32_t)AttachmentMatchingRules::Flags::SystemFormat) {
            str << pending << AsString(rules._systemFormat); pending = ", ";
        }
        if (rules._flagsSet & (uint32_t)AttachmentMatchingRules::Flags::CopyFormatFromSemantic) {
            str << pending << "copy format from " << AttachmentSemantic{rules._copyFormatSrc}; pending = ", ";
        }
        if (rules._flagsSet & (uint32_t)AttachmentMatchingRules::Flags::MultisamplingMode) {
            str << pending << (rules._multisamplingMode ? "no multisampling" : "multisampling"); pending = ", ";
        }
        if (rules._requiredBindFlags) {
            str << pending << BindFlagsAsString(rules._requiredBindFlags); pending = ", ";
        }
        str << " }";
        return str;
    }

    static std::ostream& operator<<(std::ostream& str, const WorkingAttachmentContext::Attachment& attachment)
    {
        str << "WorkingAttachment {"
            << attachment._name << ", "
            << "{";
        if (attachment._fullyDefinedAttachment.has_value()) str << *attachment._fullyDefinedAttachment;
        else str << attachment._matchingRules;

        str << ", " << std::hex 
            << "Contains: " << AttachmentSemantic{attachment._containsDataForSemantic} << ", "
            << "ShouldReceive: " << AttachmentSemantic{attachment._shouldReceiveDataForSemantic} << ", "
            << "FirstAccess: {" << AttachmentSemantic{attachment._firstAccessSemantic} << ", " << BindFlagsAsString(attachment._firstAccessInitialLayout) << ", " << AsString(attachment._firstAccessLoad) << "}, "
            << "LastAccess: {" << AttachmentSemantic{attachment._lastWriteSemantic} << ", " << BindFlagsAsString(attachment._lastAccessFinalLayout) << ", " << AsString(attachment._lastAccessStore) << "}, "
            << std::dec << "}";
        return str;
    }

    static std::ostream& operator<<(std::ostream& str, const FrameBufferDescFragment& fragment)
    {
        str << "FrameBufferDescFragment with attachments: " << std::endl;
        for (unsigned c=0; c<fragment._attachments.size(); ++c) {
            str << StreamIndent(4) << "[" << c << "] ";
            if (fragment._attachments[c].GetInputSemanticBinding() == fragment._attachments[c].GetOutputSemanticBinding()) {
                str << AttachmentSemantic{fragment._attachments[c].GetInputSemanticBinding()};
            } else
                str << AttachmentSemantic{fragment._attachments[c].GetInputSemanticBinding()} << ", " << AttachmentSemantic{fragment._attachments[c].GetOutputSemanticBinding()};
            str << ": " << fragment._attachments[c]._matchingRules << std::endl;
        }
        str << "Subpasses: " << std::endl;
        for (unsigned c=0; c<fragment._subpasses.size(); ++c) {
            str << StreamIndent(4) << "[" << c << "] " << fragment._subpasses[c] << std::endl;
        }
        return str;
    }

    static std::ostream& operator<<(std::ostream& str, const FrameBufferDescFragment::Attachment& attachment)
    {
        str << AttachmentSemantic{attachment._semantic} << " : " << attachment._matchingRules;
        return str;
    }

    static bool CompareAttachmentName(const WorkingAttachmentContext::Attachment& lhs, const WorkingAttachmentContext::Attachment& rhs)
    {
        return lhs._name < rhs._name;
    }

    static PreregisteredAttachment BuildPreregisteredAttachment(
        const FrameBufferDescFragment::Attachment& attachmentDesc,
        unsigned usageBindFlags, 
        const FrameBufferProperties& props)
    {
        Format fmt = Format::Unknown;
        if (attachmentDesc._matchingRules._flagsSet & uint32_t(AttachmentMatchingRules::Flags::FixedFormat))
            fmt = attachmentDesc._matchingRules._fixedFormat;
        else if (attachmentDesc._matchingRules._flagsSet & uint32_t(AttachmentMatchingRules::Flags::SystemFormat))
            fmt = ResolveSystemFormat(attachmentDesc._matchingRules._systemFormat);
        assert(fmt != Format::Unknown);

        TextureDesc tDesc = TextureDesc::Plain2D(
            (unsigned)props._outputWidth, (unsigned)props._outputHeight,
            fmt, 1, 0u,
            GetSamples(attachmentDesc._matchingRules, props));
        auto bindFlags = usageBindFlags | attachmentDesc._initialLayout | attachmentDesc._finalLayout | attachmentDesc._matchingRules._requiredBindFlags;

        PreregisteredAttachment result;
        result._desc = CreateDesc(bindFlags, AllocationRules::ResizeableRenderTarget, tDesc, "attachment-pool");
        assert(result._desc._textureDesc._format != Format::Unknown);       // at this point we must have a resolved format. If it's still unknown, we can't created a preregistered attachment
        result._semantic = attachmentDesc._semantic;
        result._state = PreregisteredAttachment::State::Uninitialized;
        result._layoutFlags = attachmentDesc._finalLayout ? attachmentDesc._finalLayout : usageBindFlags;
        return result;
    }

    static FrameBufferDesc BuildFrameBufferDesc(
        const FrameBufferDescFragment& fragment,
        const FrameBufferProperties& props,
        IteratorRange<const PreregisteredAttachment*> fullAttachmentDescriptions,
        IteratorRange<const FragmentStitchingContext::AttachmentTransform*> transforms)
    {
        std::vector<AttachmentDesc> fbAttachments;
        fbAttachments.reserve(fragment._attachments.size());
        assert(transforms.size() == fragment._attachments.size());
        auto ti = transforms.begin();
        for (const auto& inputFrag:fragment._attachments) {
            AttachmentDesc desc;
            desc._loadFromPreviousPhase = inputFrag._loadFromPreviousPhase;
            desc._storeToNextPhase = inputFrag._storeToNextPhase;
            desc._initialLayout = inputFrag._initialLayout;
            desc._finalLayout = inputFrag._finalLayout;

            // try to get the format & multisample information from the full description
            // (or get it from the matching rules if it doesn't exist)
            auto fullDescription = std::find_if(fullAttachmentDescriptions.begin(), fullAttachmentDescriptions.end(),
                [semantic=inputFrag._semantic](const auto& q) { return q._semantic == semantic; });
            if (fullDescription != fullAttachmentDescriptions.end()) {
                desc._format = fullDescription->_desc._textureDesc._format;
                if (fullDescription->_desc._textureDesc._samples._sampleCount > 1u)
                    desc._flags |= AttachmentDesc::Flags::Multisampled;
            } else {
                // build the attachment we'd expect to get if we used the matching rules directly
                auto prereg = BuildPreregisteredAttachment(inputFrag, 0, props);
                desc._format = prereg._desc._textureDesc._format;
                if (prereg._desc._textureDesc._samples._sampleCount > 1u)
                    desc._flags |= AttachmentDesc::Flags::Multisampled;
            }

            assert(desc._format != Format::Unknown);
            fbAttachments.push_back(desc);
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
            // assert(directionFlags & DirectionFlags::Reference);         // if you hit this, it probably means an attachment is defined in the fragment, but not used by any subpass
            auto usageFlags = CalculateBindFlags(fragment, idx);

            // Try to the match the attachment request to an existing preregistered attachment,
            // or created a new one if we can't match
            auto i = std::find_if(
                _workingAttachments.begin(), _workingAttachments.end(),
                [semantic=a._semantic](const auto& c) { return c._semantic == semantic; });
            if (i != _workingAttachments.end()) {
                #if defined(_DEBUG)
                    if (!IsCompatible(a._matchingRules, *i, _workingProps)) {     // todo -- check layout flags
                        Log(Warning) << "Preregistered attachment for semantic (" << AttachmentSemantic{a._semantic} << " does not match the request for this semantic. Attempting to use it anyway. Request: "
                            << a << ", Preregistered: " << *i << std::endl;
                    }
                #endif
                result._fullAttachmentDescriptions.push_back(*i);

                auto requiredBindFlags = usageFlags | a._initialLayout | a._finalLayout; 
                if ((i->_desc._bindFlags & requiredBindFlags) != requiredBindFlags)
                    Throw(std::runtime_error((StringMeld<512>() << "FrameBufferDescFragment requires attachment bind flags that are not present in the preregistered attachment. Attachment semantic (" << AttachmentSemantic{a._semantic} << "). Preregistered attachment bind flags: (" << BindFlagsAsString(i->_desc._bindFlags) << "), Frame buffer request bind flags: (" << BindFlagsAsString(requiredBindFlags) << ")").AsString()));

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
                    transform._newLayout = a._finalLayout ? a._finalLayout : usageFlags;
                } else {
                    transform._newLayout = a._finalLayout ? a._finalLayout : (i->_layoutFlags | usageFlags);
                }
                result._attachmentTransforms.push_back(transform);
            } else {
                auto newAttachment = BuildPreregisteredAttachment(a, usageFlags, _workingProps);
                #if defined(_DEBUG)
                    if (newAttachment._desc._textureDesc._format == Format::Unknown)
                        Log(Warning) << "Missing format information for attachment with semantic: " << AttachmentSemantic{a._semantic} << std::endl;
                #endif 
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

        result._fbDesc = BuildFrameBufferDesc(fragment, _workingProps, MakeIteratorRange(result._fullAttachmentDescriptions), MakeIteratorRange(result._attachmentTransforms));
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
        auto merged = MergeFragments(MakeIteratorRange(_workingAttachments), fragments, _workingProps, MakeIteratorRange(_systemFormats));
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
        assert(attachment._desc._textureDesc._format != Format::Unknown);
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

    void FragmentStitchingContext::DefineDoubleBufferAttachment(uint64_t semantic, ClearValue initialContents, unsigned initialLayoutFlags)
    {
        auto i = std::find_if(
			_workingAttachments.begin(), _workingAttachments.end(),
			[semantic](const auto& c) { return c._semantic == semantic; });
        if (i == _workingAttachments.end())
            Throw(std::runtime_error("Attempting to call DefineDoubleBufferAttachment() for a semantic that doesn't have a predefined attachment yet. Define the attachment for the current frame first, before requiring a double buffer of it"));

        auto i3 = std::find_if(
			_doubleBufferAttachments.begin(), _doubleBufferAttachments.end(),
			[semantic](const auto& c) { return c._todaySemantic == semantic; });
        if (i3 != _doubleBufferAttachments.end()) {
            // can't easily check if the clear values are the same, because it's an enum
            assert(i3->_initialLayoutFlags == initialLayoutFlags);
            return; // already defined
        }

        auto i2 = std::find_if(
			_workingAttachments.begin(), _workingAttachments.end(),
			[semantic](const auto& c) { return c._semantic == semantic+1; });
        if (i2 != _workingAttachments.end())
            Throw(std::runtime_error("Attempting to call DefineDoubleBufferAttachment(), but there is an overlapping predefined attachment for the double buffer. Only predefine the attachment one."));

        DoubleBufferAttachment a;
        a._todaySemantic = semantic;
        a._yesterdaySemantic = semantic+1;
        a._initialContents = initialContents;
        a._initialLayoutFlags = initialLayoutFlags;
        a._desc = i->_desc;
        _doubleBufferAttachments.push_back(a);
        DefineAttachment(a._yesterdaySemantic, a._desc, PreregisteredAttachment::State::Initialized, initialLayoutFlags);
    }

    Format FragmentStitchingContext::GetSystemAttachmentFormat(SystemAttachmentFormat fmt) const
    {
        if ((unsigned)fmt < dimof(_systemFormats))
            return _systemFormats[(unsigned(fmt))];
        return Format::Unknown;
    }

    IteratorRange<const DoubleBufferAttachment*> FragmentStitchingContext::GetDoubleBufferAttachments() const
    {
        return _doubleBufferAttachments;
    }

    FragmentStitchingContext::FragmentStitchingContext(
        IteratorRange<const PreregisteredAttachment*> preregAttachments,
        const FrameBufferProperties& fbProps,
        IteratorRange<const Format*> systemFormats)
    : _workingProps(fbProps)
    {
        for (const auto&attach:preregAttachments)
            DefineAttachment(attach);
        
        auto q = (unsigned)std::min(systemFormats.size(), dimof(_systemFormats)), c=0u;
        for (; c<q; ++c) _systemFormats[c] = systemFormats[c];
        for (; c<dimof(_systemFormats); ++c) _systemFormats[c] = Format::Unknown;
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
        result.SetViewInstanceMask(input.GetViewInstanceMask());
		return result;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////

    MergeFragmentsResult MergeFragments(
        IteratorRange<const PreregisteredAttachment*> preregisteredInputs,
        IteratorRange<const FrameBufferDescFragment*> fragments,
		const FrameBufferProperties& fbProps,
        IteratorRange<const Format*> systemAttachmentFormats)
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
        
        WorkingAttachmentContext workingAttachments;
        workingAttachments._attachments.reserve(preregisteredInputs.size());
        for (unsigned c=0; c<preregisteredInputs.size(); c++) {
            workingAttachments._attachments.push_back(WorkingAttachmentContext::Attachment{ preregisteredInputs[c] });
        }

        for (auto f=fragments.begin(); f!=fragments.end(); ++f) {
            std::vector<std::pair<AttachmentName, AttachmentName>> attachmentRemapping;

            assert(f->_pipelineType == result._pipelineType);       // all fragments must have the same pipeline type

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

            std::vector<WorkingAttachmentContext::Attachment> newWorkingAttachments;
            newWorkingAttachments.reserve(sortedInterfaceAttachments.size());

            for (const auto&pair:sortedInterfaceAttachments) {
                const auto& interfaceAttachment = f->_attachments[pair.first];
                AttachmentName interfaceAttachmentName = pair.first;
                DirectionFlags::BitField directionFlags = pair.second;

                auto simplifiedMatchingRules = interfaceAttachment._matchingRules;
                if (simplifiedMatchingRules._flagsSet & uint32_t(AttachmentMatchingRules::Flags::SystemFormat)) {
                    if ((unsigned)simplifiedMatchingRules._systemFormat >= systemAttachmentFormats.size() ||
                        systemAttachmentFormats[(unsigned)simplifiedMatchingRules._systemFormat] == Format::Unknown)
                        Throw(std::runtime_error(StringMeld<256>() << "No system attachment format given for attachment " << interfaceAttachment));
                    simplifiedMatchingRules.FixedFormat(systemAttachmentFormats[(unsigned)simplifiedMatchingRules._systemFormat]);
                }

                if (simplifiedMatchingRules._flagsSet & uint32_t(AttachmentMatchingRules::Flags::CopyFormatFromSemantic)) {
                    for (const auto& a:preregisteredInputs)
                        if (a._semantic == simplifiedMatchingRules._copyFormatSrc) {
                            simplifiedMatchingRules.FixedFormat(a._desc._textureDesc._format);
                            break;
                        }
                    if (simplifiedMatchingRules._flagsSet & uint32_t(AttachmentMatchingRules::Flags::CopyFormatFromSemantic))
                        Throw(std::runtime_error(StringMeld<256>() << "Could not find source attachment with required semantic to copy format from for attachment " << interfaceAttachment));
                }
                
                auto newState = workingAttachments.MatchAttachment(
                    simplifiedMatchingRules, interfaceAttachment._semantic,
                    interfaceAttachment._loadFromPreviousPhase,
                    fbProps);

                if (!newState.has_value()) {
                    #if defined(_DEBUG)
                        debugInfo << "      * Failed to find compatible attachment for request: " << interfaceAttachment._matchingRules;
                        if (interfaceAttachment.GetInputSemanticBinding()) {
                            debugInfo << ". Semantic: " << AttachmentSemantic{interfaceAttachment.GetInputSemanticBinding()} << std::endl;
                            for (const auto& a:workingAttachments._attachments)
                                if (a._containsDataForSemantic == interfaceAttachment.GetInputSemanticBinding()) {
                                    debugInfo << "        Couldn't be matched against: " << a << std::endl;
                                }
                        } else {
                            debugInfo << std::endl;
                        }
                        debugInfo << "      * Working attachments are: " << std::endl;
                        for (const auto& att : workingAttachments._attachments)
                            debugInfo << att << std::endl;
                        auto debugInfoStr = debugInfo.str();
                        Log(Error) << "MergeFragments() failed. Details:" << std::endl << debugInfoStr << std::endl;
                        Throw(::Exceptions::BasicLabel("Couldn't bind renderpass fragment input request. Details:\n%s\n", debugInfoStr.c_str()));
                    #else
                        Throw(::Exceptions::BasicLabel("Couldn't bind renderpass fragment input request"));
                    #endif
                }

                if (!newState->_hasBeenAccessed) {
                    newState->_hasBeenAccessed = true;
                    newState->_firstAccessSemantic = interfaceAttachment.GetInputSemanticBinding();
                    newState->_firstAccessLoad = interfaceAttachment._loadFromPreviousPhase;
                    if (interfaceAttachment._initialLayout)       // otherwise inherit from what we set from the PreregisteredAttachment
                        newState->_firstAccessInitialLayout = interfaceAttachment._initialLayout;
                }

                if (directionFlags & DirectionFlags::WritesData) {
                    newState->_containsDataForSemantic = interfaceAttachment.GetOutputSemanticBinding();
                    newState->_lastWriteSemantic = interfaceAttachment.GetOutputSemanticBinding();
                }

                newState->_lastAccessStore = interfaceAttachment._storeToNextPhase;
                newState->_lastAccessFinalLayout = interfaceAttachment._finalLayout;
                if (!HasRetain(interfaceAttachment._storeToNextPhase))
                    newState->_containsDataForSemantic = 0;     // if we don't explicitly retain the data, let's forget it exists
                newWorkingAttachments.push_back(*newState);

                attachmentRemapping.push_back({interfaceAttachmentName, (unsigned)newWorkingAttachments.size()-1});
            }

            /////////////////////////////////////////////////////////////////////////////

                // setup the subpasses & PassFragment
            std::sort(attachmentRemapping.begin(), attachmentRemapping.end(), CompareFirst<AttachmentName, AttachmentName>());

            // assign names to all of the attachments we created in "newWorkingAttachments"
            // But do this in the order of attachmentRemapping, so that we assign names in the order that they appeared in
            // the input fragment (because we actually process them in a different order)
            for (auto&mapping:attachmentRemapping) {
                auto& newState = newWorkingAttachments[mapping.second];
                if (newState._name == ~0u)
                    newState._name = NextName(MakeIteratorRange(workingAttachments._attachments), MakeIteratorRange(newWorkingAttachments));
                // attachmentRemapping morphs from <old attachment name> -> <index in newWorkingAttachments>
                // to <old attachment name> -> <new attachment name>
                mapping.second = newState._name;
            }

            for (unsigned p=0; p<(unsigned)f->_subpasses.size(); ++p) {
                auto newSubpass = RemapSubpassDesc(
					f->_subpasses[p],
					std::bind(&Remap, std::ref(attachmentRemapping), std::placeholders::_1));
                result.AddSubpass(std::move(newSubpass));
            }

            /////////////////////////////////////////////////////////////////////////////

            workingAttachments._attachments.insert(workingAttachments._attachments.end(), newWorkingAttachments.begin(), newWorkingAttachments.end());

            #if defined(_DEBUG)
                debugInfo << "Merge calculated this attachment remapping:" << std::endl;
                for (const auto&r:attachmentRemapping)
                    debugInfo << StreamIndent(4) << "[" << r.first << "] remapped to " << r.second << " ("
                        << f->_attachments[r.first]
                        << ")" << std::endl;
                debugInfo << "Current fragment interface:" << std::endl;
                for (const auto&w:workingAttachments._attachments)
                    debugInfo << StreamIndent(4) << w << std::endl;
            #endif
        }

        // The workingAttachments array is now the list of attachments that must go into
        // the output fragment;
        result._attachments.reserve(workingAttachments._attachments.size());
        std::sort(workingAttachments._attachments.begin(), workingAttachments._attachments.end(), CompareAttachmentName);
        for (auto& a:workingAttachments._attachments) {
            if (a._name == ~0u) continue;
            // The AttachmentNames in FrameBufferDescFragment are just indices into the attachment
            // list -- so we must ensure that we insert in order, and without gaps
            assert(a._name == result._attachments.size());
            assert(a._firstAccessSemantic == 0 || a._containsDataForSemantic == 0 || a._firstAccessSemantic == a._containsDataForSemantic);       // split semantic case
            FrameBufferDescFragment::Attachment r { a._containsDataForSemantic ? a._containsDataForSemantic : a._firstAccessSemantic };
            // r._desc._format = a._format;
            // r._desc._flags = (a._samples._sampleCount > 1) ? AttachmentDesc::Flags::Multisampled : 0u;
            if (a._fullyDefinedAttachment.has_value()) {
                // Setting the matching rules should be redundant here, since the semantic should map it to a
                // predefined attachment, anyway
                assert(a._fullyDefinedAttachment->_semantic == r.GetInputSemanticBinding());
            } else {
                r._matchingRules = a._matchingRules;
                // matching rules must specify the format through some method --
                assert(r._matchingRules._flagsSet & (uint32_t(AttachmentMatchingRules::Flags::FixedFormat)|uint32_t(AttachmentMatchingRules::Flags::SystemFormat)|uint32_t(AttachmentMatchingRules::Flags::CopyFormatFromSemantic)));
            }
            r._initialLayout = a._firstAccessInitialLayout;
            r._finalLayout = a._lastAccessFinalLayout;
            r._loadFromPreviousPhase = a._firstAccessLoad;
            r._storeToNextPhase = a._lastAccessStore;
            result._attachments.push_back(r);
        }

        MergeFragmentsResult finalResult;
        finalResult._mergedFragment = std::move(result);

        for (auto& a:workingAttachments._attachments) {
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
                debugInfo << StreamIndent(4) << "[" << c << "] " << result._attachments[c] << std::endl;
            debugInfo << "Final subpasses" << std::endl;
            for (unsigned c=0; c<result._subpasses.size(); ++c)
                debugInfo << StreamIndent(4) << "[" << c << "] " << result._subpasses[c] << std::endl;
            debugInfo << "Interface summary" << std::endl;
            for (unsigned c=0; c<finalResult._inputAttachments.size(); ++c)
                debugInfo << StreamIndent(4) << "Input [" << c << "] " << finalResult._inputAttachments[c].second << " (" << finalResult._mergedFragment._attachments[finalResult._inputAttachments[c].second] << ")" << std::endl;
            for (unsigned c=0; c<finalResult._outputAttachments.size(); ++c)
                debugInfo << StreamIndent(4) << "Output [" << c << "] "  << finalResult._outputAttachments[c].second << " (" << finalResult._mergedFragment._attachments[finalResult._outputAttachments[c].second] << ")" << std::endl;
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
            auto newName = dstFragment.DefineAttachment(srcFragment._attachments[input]);
            existing = remapping.insert(existing, {input, newName});
        }

        return existing->second;
    }

    bool CanBeSimplified(
        const FrameBufferDescFragment& inputFragment,
        IteratorRange<const PreregisteredAttachment*> systemAttachments,
        const FrameBufferProperties& fbProps,
        IteratorRange<const Format*> systemFormats)
    {
        return false;       // don't check in
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
                systemAttachments, MakeIteratorRange(testFragments), fbProps, systemFormats);
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

    static Format FallbackChain(IDevice& device, std::initializer_list<Format> fmts, BindFlag::BitField bindFlags)
    {
        for (auto f:fmts)
            if (device.QueryFormatCapability(f, bindFlags) == FormatCapability::Supported)
                return f;
        assert(0);
        return Format::Unknown;
    }

    std::vector<Format> CalculateDefaultSystemFormats(IDevice& device)
    {
        std::vector<Format> result;
        result.resize((unsigned)SystemAttachmentFormat::Max, Format::Unknown);
        result[(unsigned)SystemAttachmentFormat::LDRColor] = FallbackChain(device, {Format::R8G8B8A8_UNORM_SRGB}, BindFlag::RenderTarget);
        result[(unsigned)SystemAttachmentFormat::HDRColor] = FallbackChain(device, {Format::R11G11B10_FLOAT, Format::R16G16B16A16_FLOAT, Format::R32G32B32A32_FLOAT}, BindFlag::RenderTarget|BindFlag::ShaderResource);
        result[(unsigned)SystemAttachmentFormat::MainDepthStencil] = FallbackChain(device, {Format::D24_UNORM_S8_UINT, Format::D32_SFLOAT_S8_UINT}, BindFlag::DepthStencil);
        result[(unsigned)SystemAttachmentFormat::LowDetailDepth] = FallbackChain(device, {Format::D16_UNORM, Format::D32_FLOAT}, BindFlag::DepthStencil);
        result[(unsigned)SystemAttachmentFormat::ShadowDepth] = FallbackChain(device, {Format::D16_UNORM, Format::D32_FLOAT}, BindFlag::DepthStencil|BindFlag::ShaderResource);
        return result;
    }

}}

