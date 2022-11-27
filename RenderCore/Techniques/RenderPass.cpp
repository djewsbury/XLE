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
            << ", L:" << AsString(attachment._loadFromPreviousPhase) << "/" << BindFlagsAsString(attachment._initialLayout)
            << ", S:" << AsString(attachment._storeToNextPhase) << "/" << BindFlagsAsString(attachment._finalLayout)
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

    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::InitialState(LoadStore loadStore)
    {
        _fragment->_attachments[_attachmentName]._initialLayout = {};
        _fragment->_attachments[_attachmentName]._loadFromPreviousPhase = loadStore;
        return *this;
    }

    FrameBufferDescFragment::DefineAttachmentHelper& FrameBufferDescFragment::DefineAttachmentHelper::FinalState(LoadStore loadStore)
    {
        _fragment->_attachments[_attachmentName]._finalLayout = {};
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

    class NamedAttachmentsAdapter : public INamedAttachments
    {
    public:
		virtual std::shared_ptr<IResourceView> GetResourceView(
            AttachmentName resName,
            BindFlag::Enum bindFlag, TextureViewDesc viewDesc,
            const AttachmentDesc& requestDesc, const FrameBufferProperties& props) const override;

        NamedAttachmentsAdapter(
            const AttachmentReservation&);
        ~NamedAttachmentsAdapter();
    private:
        const AttachmentReservation* _reservation;
    };

    std::shared_ptr<IResourceView> NamedAttachmentsAdapter::GetResourceView(
        AttachmentName resName,
        BindFlag::Enum bindFlag, TextureViewDesc viewDesc,
        const AttachmentDesc& requestDesc, const FrameBufferProperties& props) const
    {
        assert(resName < _reservation->GetResourceCount());
        auto view = _reservation->GetView(resName, bindFlag, viewDesc);

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

    NamedAttachmentsAdapter::NamedAttachmentsAdapter(
        const AttachmentReservation& reservation)
    : _reservation(&reservation) {}
    NamedAttachmentsAdapter::~NamedAttachmentsAdapter() {}

////////////////////////////////////////////////////////////////////////////////////////////////////

    class FrameBufferPool
    {
    public:
        class Result
        {
        public:
            std::shared_ptr<Metal::FrameBuffer> _frameBuffer;
            AttachmentReservation _poolReservation;
            const FrameBufferDesc* _completedDesc;
        };
        Result BuildFrameBuffer(
            Metal::ObjectFactory& factory,
            const FrameBufferDesc& desc,
            IteratorRange<const PreregisteredAttachment*> resolvedAttachmentDescs,
            AttachmentPool& attachmentPool,
            AttachmentReservation* parentReservation);

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

    

    static bool HasRetain(LoadStore loadStore)
    {
        return  loadStore == LoadStore::Retain
            ||  loadStore == LoadStore::DontCare_StencilRetain
            ||  loadStore == LoadStore::Clear_StencilRetain
            ||  loadStore == LoadStore::Retain_StencilDontCare
            ||  loadStore == LoadStore::Retain_StencilClear
            ;
    }

    static void CalculateAttachmentTransforms(
        IteratorRange<AttachmentTransform*> dstTransforms,
        const FrameBufferDesc& fbDesc)
    {
        for (unsigned c=0; c<fbDesc.GetAttachments().size(); ++c) {
            dstTransforms[c]._initialLayout = fbDesc.GetAttachments()[c]._initialLayout;
            dstTransforms[c]._finalLayout = fbDesc.GetAttachments()[c]._finalLayout;
            
            if (HasRetain(fbDesc.GetAttachments()[c]._loadFromPreviousPhase)) {
                if (HasRetain(fbDesc.GetAttachments()[c]._storeToNextPhase)) {
                    dstTransforms[c]._type = AttachmentTransform::LoadedAndStored;
                } else {
                    dstTransforms[c]._type = AttachmentTransform::Consumed;
                }
            } else {
                if (HasRetain(fbDesc.GetAttachments()[c]._storeToNextPhase)) {
                    dstTransforms[c]._type = AttachmentTransform::Generated;
                } else {
                    dstTransforms[c]._type = AttachmentTransform::Temporary;
                }
            }
        }
    }

    static bool HasExplicitAspect(const TextureViewDesc& viewDesc) { return viewDesc._format._aspect != TextureViewDesc::Aspect::UndefinedAspect || viewDesc._format._explicitFormat != Format(0); }

    auto FrameBufferPool::BuildFrameBuffer(
        Metal::ObjectFactory& factory,
        const FrameBufferDesc& desc,
        IteratorRange<const PreregisteredAttachment*> resolvedAttachmentDescs,
        AttachmentPool& attachmentPool,
        AttachmentReservation* parentReservation) -> Result
    {    
        auto poolAttachments = attachmentPool.Reserve(resolvedAttachmentDescs, parentReservation);
		assert(poolAttachments.GetResourceCount() == desc.GetAttachments().size());

        std::vector<AttachmentDesc> adjustedAttachments;
        adjustedAttachments.reserve(desc.GetAttachments().size());
        Result result;

        uint64_t hashValue = DefaultSeed64;
        for (unsigned c=0; c<desc.GetAttachments().size(); ++c) {
            auto* matchedAttachment = poolAttachments.GetResource(c).get();
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
            completeAttachmentDesc._format = AsTypelessFormat(resDesc._textureDesc._format);
            adjustedAttachments.push_back({completeAttachmentDesc});
        }

        FrameBufferDesc adjustedDesc(
            std::move(adjustedAttachments),
            {desc.GetSubpasses().begin(), desc.GetSubpasses().end()},
            desc.GetProperties()); 
        
        // patch up the view aspects, to compensate from that falling off during the attachment reservation operation
        // ie, the format of the attachment request can have an implied aspect (eg, using one of the _SRGB formats). Since
        // we always drop these off by going to a typeless attachment format, we have to compensate by shifting the aspect
        // back into the view
        for (auto& sp:adjustedDesc.GetSubpasses()) {
            for (auto& o:sp.GetOutputs())
                if (!HasExplicitAspect(o._window)) {
                    o._window._format = ImpliedFormatFilter(desc.GetAttachments()[o._resourceName]._format);
                    assert(
                        ResolveFormat(resolvedAttachmentDescs[o._resourceName]._desc._textureDesc._format, o._window._format, BindFlag::RenderTarget)
                        == ResolveFormat(adjustedDesc.GetAttachments()[o._resourceName]._format, o._window._format, BindFlag::RenderTarget));
                }
            if (sp.GetDepthStencil()._resourceName != ~0u && !HasExplicitAspect(sp.GetDepthStencil()._window)) {
                sp.GetDepthStencil()._window._format = ImpliedFormatFilter(desc.GetAttachments()[sp.GetDepthStencil()._resourceName]._format);
                assert(
                    ResolveFormat(resolvedAttachmentDescs[sp.GetDepthStencil()._resourceName]._desc._textureDesc._format, sp.GetDepthStencil()._window._format, BindFlag::DepthStencil)
                    == ResolveFormat(adjustedDesc.GetAttachments()[sp.GetDepthStencil()._resourceName]._format, sp.GetDepthStencil()._window._format, BindFlag::DepthStencil));
            }
            for (auto& i:sp.GetInputs())
                if (!HasExplicitAspect(i._window)) {
                    i._window._format = ImpliedFormatFilter(desc.GetAttachments()[i._resourceName]._format);
                    assert(
                        ResolveFormat(resolvedAttachmentDescs[i._resourceName]._desc._textureDesc._format, i._window._format, BindFlag::InputAttachment)
                        == ResolveFormat(adjustedDesc.GetAttachments()[i._resourceName]._format, i._window._format, BindFlag::InputAttachment));
                }
            for (auto& v:sp.GetViews())
                if (!HasExplicitAspect(v._window)) {
                    v._window._format = ImpliedFormatFilter(desc.GetAttachments()[v._resourceName]._format);
                    assert(
                        ResolveFormat(resolvedAttachmentDescs[v._resourceName]._desc._textureDesc._format, v._window._format, BindFlag::ShaderResource)
                        == ResolveFormat(adjustedDesc.GetAttachments()[v._resourceName]._format, v._window._format, BindFlag::ShaderResource));
                }
            assert(sp.GetResolveOutputs().empty());
            assert(sp.GetResolveDepthStencil()._resourceName == ~0u);
        }

        hashValue = HashCombine(adjustedDesc.GetHash(), hashValue);
        assert(hashValue != ~0ull);     // using ~0ull has a sentinel, so this will cause some problems

        unsigned earliestEntry = 0;
        unsigned tickIdOfEarliestEntry = ~0u;
        for (unsigned c=0; c<dimof(_entries); ++c) {
            if (_entries[c]._hash == hashValue) {
                _entries[c]._tickId = _currentTickId;
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

        NamedAttachmentsAdapter namedAttachments{poolAttachments};
        assert(adjustedDesc.GetSubpasses().size());
        _entries[earliestEntry]._fb = std::make_shared<Metal::FrameBuffer>(
            factory,
            adjustedDesc, namedAttachments);
        _entries[earliestEntry]._tickId = _currentTickId;
        _entries[earliestEntry]._hash = hashValue;
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

        ViewPool                    _srvPool;
        std::shared_ptr<IDevice>    _device;

        bool BuildAttachment(AttachmentName attach);
    };

    bool AttachmentPool::Pimpl::BuildAttachment(AttachmentName attachName)
    {
        Attachment* attach = &_attachments[attachName];
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
        if (attachName >= _pimpl->_attachments.size()) return s_nullResourcePtr;
        auto* attach = &_pimpl->_attachments[attachName];
        assert(attach);
        if (attach->_resource)
            return attach->_resource;
            
        _pimpl->BuildAttachment(attachName);
        return attach->_resource;
	}

    AttachmentName AttachmentPool::GetNameForResource(IResource& res) const
    {
        for (unsigned c=0; c<_pimpl->_attachments.size(); ++c)
            if (_pimpl->_attachments[c]._resource.get() == &res)
                return c;
        return ~0u;
    }

	const std::shared_ptr<IResourceView>& AttachmentPool::GetSRV(AttachmentName attachName, const TextureViewDesc& window) const
	{
        return GetView(attachName, BindFlag::ShaderResource, window);
	}

    const std::shared_ptr<IResourceView>& AttachmentPool::GetView(AttachmentName attachName, BindFlag::Enum usage, const TextureViewDesc& window) const
    {
        static std::shared_ptr<IResourceView> dummy;
        if (attachName >= _pimpl->_attachments.size()) return dummy;
        auto* attach = &_pimpl->_attachments[attachName];
        assert(attach);
        if (!attach->_resource)
        _pimpl->BuildAttachment(attachName);
		assert(attach->_resource);
        return _pimpl->_srvPool.GetTextureView(attach->_resource, usage, window);
    }

    static bool MatchRequest(const ResourceDesc& preregisteredDesc, const ResourceDesc& concreteObjectDesc)
    {
        assert(preregisteredDesc._type == ResourceDesc::Type::Texture && concreteObjectDesc._type == ResourceDesc::Type::Texture);
        return
            preregisteredDesc._textureDesc._arrayCount == concreteObjectDesc._textureDesc._arrayCount
            && (AsTypelessFormat(preregisteredDesc._textureDesc._format) == AsTypelessFormat(concreteObjectDesc._textureDesc._format) || preregisteredDesc._textureDesc._format == Format::Unknown)
            && preregisteredDesc._textureDesc._width == concreteObjectDesc._textureDesc._width
            && preregisteredDesc._textureDesc._height == concreteObjectDesc._textureDesc._height
            && preregisteredDesc._textureDesc._samples == concreteObjectDesc._textureDesc._samples
            && (concreteObjectDesc._bindFlags & preregisteredDesc._bindFlags) == preregisteredDesc._bindFlags
            ;
    }

    auto AttachmentPool::Reserve(
        IteratorRange<const PreregisteredAttachment*> attachmentRequests,
        AttachmentReservation* parentReservation,
        ReservationFlag::BitField flags) -> AttachmentReservation
    {
        AttachmentReservation emptyReservation;
        if (!parentReservation) parentReservation = &emptyReservation;

        VLA(bool, consumed, _pimpl->_attachments.size());
        for (unsigned c=0; c<_pimpl->_attachments.size(); ++c) consumed[c] = false;
        auto originalAttachmentsSize = _pimpl->_attachments.size();

        assert(!parentReservation || !parentReservation->_pool || parentReservation->_pool == this);     // parentReservation must be associated with this pool, or none at all

        // Treat any attachments that are bound to semantic values as "consumed" already.
        // In other words, we can't give these attachments to requests without a semantic,
        // or using another semantic.
        for (unsigned c=0; c<_pimpl->_attachments.size(); ++c)
            consumed[c] = _pimpl->_attachments[c]._lockCount > 0;

        std::vector<AttachmentReservation::AttachmentToReserve> selectedAttachments;
        selectedAttachments.resize(attachmentRequests.size());

        for (unsigned r=0; r<attachmentRequests.size(); ++r) {
            const auto& request = attachmentRequests[r];

            // If a semantic value is set, we should first check to see if the request can match
            // something bound to that semantic in the parent reservation
            if (!request._semantic) continue;

            auto matchingParent = std::find_if(
                parentReservation->_entries.begin(), parentReservation->_entries.end(),
                [semantic=request._semantic](const auto& q) { return q._semantic == semantic; });

            if (matchingParent != parentReservation->_entries.end()) {
                #if defined(_DEBUG)
                    auto q = unsigned(matchingParent - parentReservation->_entries.begin());
                    if (!MatchRequest(request._desc, parentReservation->GetResourceDesc(q))) {
                        Log(Warning) << "Attachment previously used for the semantic (" << AttachmentSemantic{request._semantic} << ") does not match the request for this semantic. Attempting to use it anyway. Request: "
                            << request._desc << ", Bound previously: " << parentReservation->GetResourceDesc(q)
                            << std::endl;
                    }
                #endif

                selectedAttachments[r]._resource = matchingParent->_resource;
                selectedAttachments[r]._poolName = matchingParent->_poolResource;
                selectedAttachments[r]._currentLayout = matchingParent->_currentLayout;
                selectedAttachments[r]._semantic = request._semantic;

                if (request._layout && (matchingParent->_pendingSwitchToLayout.value_or(matchingParent->_currentLayout) != request._layout)) {
                    // If you hit this, it probably means that initial/final layouts for sequential render passes do not agree
                    // Continuing will spit out a lot of underlying graphics API warnings and could potentially cause synchronization
                    // bugs on certain hardware
                    Log(Warning) << "Request for attachment with semantic (" << AttachmentSemantic{request._semantic} << ") found mismatch between layouts" << std::endl;
                    Log(Warning) << "Requested layout: (" << BindFlagsAsString(request._layout) << "), resource last left in layout: (" << BindFlagsAsString(matchingParent->_pendingSwitchToLayout.value_or(matchingParent->_currentLayout)) << ")" << std::endl;
                    assert(0);
                }
            }
        }

        // If we didn't find a match in one of our bound semantic attachments, we must flow
        // through and treat it as a temporary attachment.
        for (unsigned r=0; r<attachmentRequests.size(); ++r) {
            const auto& request = attachmentRequests[r];

            if (selectedAttachments[r]._poolName != ~0u || selectedAttachments[r]._resource) continue;

            // We will never attempt to reuse something from the parent reservation (unless the semantics match),
            // even another temporary -- ie, we're expecting any temporaries in the parent reservation are there
            // because they are expected to be used later

            bool foundMatch = false;
            unsigned poolAttachmentName = 0;
            for (unsigned q=0; q<_pimpl->_attachments.size(); ++q) {
                if (MatchRequest(request._desc, _pimpl->_attachments[q]._desc) && q < originalAttachmentsSize && !consumed[q]) {
                    consumed[q] = true;
                    poolAttachmentName = q;
                    foundMatch = true;
                    break;
                }
            }

            if (!foundMatch) {
                _pimpl->_attachments.push_back(Pimpl::Attachment{nullptr, request._desc});
                poolAttachmentName = unsigned(_pimpl->_attachments.size()-1);
            }

            selectedAttachments[r]._poolName = poolAttachmentName;
            selectedAttachments[r]._semantic = request._semantic;
            // selectedAttachments[r]._currentLayout = ...
            if (request._layout)
                selectedAttachments[r]._pendingSwitchToLayout = request._layout;

            // If the request was expecting an initialized input, it must match either with something explicitly bound to the semantic,
            // or with something with double buffer attachment rules
            // If we don't have either of these, then we can't fulfill it correctly -- we'd be passing uninitialized data into something
            // that asked for an initialized attachment
            if (request._state != PreregisteredAttachment::State::Uninitialized) {
                char buffer[256];
                if (request._semantic) {
                    Throw(std::runtime_error(StringMeldInPlace(buffer) << "Cannot find initialized attachment for request with semantic " << AttachmentSemantic{request._semantic}));
                } else
                    Throw(std::runtime_error(StringMeldInPlace(buffer) << "Cannot find initialized attachment for non-semantic request"));
            }
        }

        return AttachmentReservation{std::move(selectedAttachments), this, flags};
    }

#if 0
    void AttachmentPool::Bind(uint64_t semantic, const IResourcePtr& resource, bool completeInitialisationBeforeUse)
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
        // By default, we must check attachments registered this way for possible CompleteInitialization() calls
        // it's possible that the caller has given us an attachment that has just been freshly created
        // so the first time it's used, it may not have had complete initialization called
        binding->_pendingCompleteInitialization = completeInitialisationBeforeUse;
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
#endif

    void AttachmentPool::ResetActualized()
    {
        // Reset all actualized attachments. They will get recreated on demand
        _pimpl->_attachments.clear();
        _pimpl->_srvPool.Reset();
    }

    void AttachmentPool::AddRef(IteratorRange<const AttachmentName*> attachments, ReservationFlag::BitField flags)
    {
        for (auto a:attachments) {
            assert(a<_pimpl->_attachments.size());
            ++_pimpl->_attachments[a]._lockCount;
        }
    }

    void AttachmentPool::Release(IteratorRange<const AttachmentName*> attachments, ReservationFlag::BitField flags)
    {
        for (auto a:attachments) {
            assert(a<_pimpl->_attachments.size());
            assert(_pimpl->_attachments[a]._lockCount >= 1);
            --_pimpl->_attachments[a]._lockCount;
        }
    }

    static void InitializeEmptyYesterdayAttachment(
        IThreadContext& threadContext,
        AttachmentReservation& attachmentReservation,
        AttachmentName attachmentName,
        ClearValue initialContents)
    {
        // initialize a "yesterday" attachment where there is no prior data (eg, on the first frame)
        auto& metalContext = *Metal::DeviceContext::Get(threadContext);
        auto desc = attachmentReservation.GetResourceDesc(attachmentName);
        if (desc._bindFlags & BindFlag::RenderTarget) {
            auto rtv = attachmentReservation.GetView(attachmentName, BindFlag::RenderTarget);
            metalContext.Clear(*rtv, initialContents._float);
        } else if (desc._bindFlags & BindFlag::UnorderedAccess) {
            auto uav = attachmentReservation.GetView(attachmentName, BindFlag::UnorderedAccess);
            metalContext.ClearFloat(*uav, initialContents._float);
        } else if (desc._bindFlags & BindFlag::DepthStencil) {
            auto dsv = attachmentReservation.GetView(attachmentName, BindFlag::DepthStencil);
            auto components = GetComponents(desc._textureDesc._format);
            ClearFilter::BitField clearFilter = 0;
            if (components == FormatComponents::Depth || components == FormatComponents::DepthStencil)
                clearFilter |= ClearFilter::Depth;
            if (components == FormatComponents::Stencil || components == FormatComponents::DepthStencil)
                clearFilter |= ClearFilter::Stencil;
            metalContext.Clear(*dsv, clearFilter, initialContents._depthStencil._depth, initialContents._depthStencil._stencil);
        } else {
            Throw(std::runtime_error("Unable to initialize double buffered attachment, because no writable bind flags were given"));
        }
    }

#if 0
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
                    InitializeEmptyYesterdayAttachment ... 
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
#endif

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

    AttachmentName AttachmentReservation::Bind(uint64_t semantic, std::shared_ptr<IResource> resource, BindFlag::BitField currentLayout)
    {
        Entry newEntry;
        newEntry._resource = std::move(resource);
        newEntry._semantic = semantic;
        newEntry._currentLayout = currentLayout;    // current layout can be ~0u, which means never initialized

        // replace an existing binding to this semantic, if one exists
        for (auto e=_entries.begin(); e!=_entries.end(); ++e) {
            if (e->_semantic == semantic) {
                if (_pool && e->_poolResource != ~0u) {
                    AttachmentName toRelease = e->_poolResource;
                    _pool->Release(MakeIteratorRange(&toRelease, &toRelease+1), _reservationFlags);
                }
                *e = std::move(newEntry);
                return (AttachmentName)std::distance(_entries.begin(), e);
            }
        }

        _entries.push_back(std::move(newEntry));
        return AttachmentName(_entries.size()-1);
    }

    void AttachmentReservation::Unbind(const IResource& resource)
    {
        // note that this will end up reshuffling all of the resNames
        for (auto i=_entries.begin(); i!=_entries.end();) {
            if (i->_resource.get() == &resource) {
                i = _entries.erase(i);
            } else
                ++i;
        }
    }

    void AttachmentReservation::UpdateAttachments(AttachmentReservation& childReservation, IteratorRange<const AttachmentTransform*> transforms)
    {
        assert(transforms.size() == childReservation._entries.size());

        VLA(bool, removeEntry, _entries.size());
        for (unsigned c=0; c<_entries.size(); ++c)
            removeEntry[c] = false;

        std::vector<Entry> newEntries;
        for (unsigned aIdx=0; aIdx<transforms.size(); ++aIdx) {
            auto transform = transforms[aIdx];
            auto& childEntry = childReservation._entries[aIdx];

            if (    transform._type == AttachmentTransform::Temporary
                ||  transform._type == AttachmentTransform::Consumed) {
                // unbind this attachment if it appears anywhere, and unbind the semantic as well
                for (unsigned c=0; c<_entries.size(); ++c)
                    if (_entries[c]._poolResource == childEntry._poolResource && _entries[c]._resource == childEntry._resource)
                        removeEntry[c] = true;
                if (childEntry._semantic != ~0ull && childEntry._semantic != 0ull)
                    for (unsigned c=0; c<_entries.size(); ++c)
                        if (_entries[c]._semantic == childEntry._semantic)
                            removeEntry[c] = true;
            } else if (transform._type == AttachmentTransform::LoadedAndStored
                    || transform._type == AttachmentTransform::Generated) {
                // unbind any alternative bindings of this semantic, and update the layout for this resource
                // note that we allow the resource to keep any previous binding it might have
                for (unsigned c=0; c<_entries.size(); ++c)
                    if (_entries[c]._poolResource == childEntry._poolResource && _entries[c]._resource == childEntry._resource) {
                        _entries[c]._currentLayout = transform._finalLayout;
                        _entries[c]._pendingClear = {};
                        _entries[c]._pendingSwitchToLayout = {};
                    }

                if (childEntry._semantic != ~0ull && childEntry._semantic != 0ull) {
                    bool foundExistingBinding = false;
                    for (unsigned c=0; c<_entries.size(); ++c)
                        if (_entries[c]._semantic == childEntry._semantic) {
                            if (_entries[c]._poolResource == childEntry._poolResource && _entries[c]._resource == childEntry._resource) {
                                foundExistingBinding = true;
                                removeEntry[c] = false;
                            } else
                                removeEntry[c] = true;
                        }
                    if (!foundExistingBinding) {
                        Entry newEntry;
                        newEntry._poolResource = childEntry._poolResource;
                        newEntry._resource = childEntry._resource;
                        newEntry._currentLayout = transform._finalLayout;
                        newEntry._semantic = childEntry._semantic;
                        newEntries.emplace_back(std::move(newEntry));
                    }
                }
            }
        }

        // release fixup
        {
            auto initialEntriesSize = _entries.size();
            VLA(AttachmentName, toRelease, initialEntriesSize);
            unsigned toReleaseCount = 0;
            for (int c=(int)initialEntriesSize-1; c>=0; c--)
                if (removeEntry[c]) {
                    if (_entries[c]._poolResource != ~0u)
                        toRelease[toReleaseCount++] = _entries[c]._poolResource;
                    _entries.erase(_entries.begin()+c);
                }

            if (toReleaseCount)
                _pool->Release(MakeIteratorRange(toRelease, toRelease+toReleaseCount), _reservationFlags);
        }

        // addref fixup
        if (!newEntries.empty()) {
            VLA(AttachmentName, toAddRef, newEntries.size());
            unsigned toAddRefCount = 0;

            _entries.reserve(_entries.size()+newEntries.size());
            for (auto& e:newEntries) {
                if (e._poolResource != ~0u)
                    toAddRef[toAddRefCount++] = e._poolResource;
                _entries.emplace_back(std::move(e));
            }

            if (!_pool) {
                _pool = childReservation._pool;
                _reservationFlags = childReservation._reservationFlags;
            }

            _pool->AddRef(MakeIteratorRange(toAddRef, toAddRef+toAddRefCount), _reservationFlags);
        }

        assert(!_pool || _pool == childReservation._pool);
        assert(!_pool || _reservationFlags == childReservation._reservationFlags);
    }

    // AttachmentReservation has it's own indexing for attachments
    // this will be zero-based and agree with the ordering of requests when returned from AttachmentPool::Reserve
    // this can be used to make it compatible with the AttachmentName in a FrameBufferDesc
    const std::shared_ptr<IResource>& AttachmentReservation::GetResource(AttachmentName resName) const
    {
        assert(resName < _entries.size());
        const auto& e = _entries[resName];
        if (e._poolResource == ~0u) return e._resource;
        return _pool->GetResource(e._poolResource);
    }

    ResourceDesc AttachmentReservation::GetResourceDesc(AttachmentName resName) const
    {
        assert(resName < _entries.size());
        const auto& e = _entries[resName];
        if (e._poolResource == ~0u) return e._resource->GetDesc();
        return _pool->GetResource(e._poolResource)->GetDesc();
    }

    auto AttachmentReservation::GetView(AttachmentName resName, BindFlag::Enum usage, const TextureViewDesc& window) const -> const std::shared_ptr<IResourceView>&
    {
        assert(resName < _entries.size());
        const auto& e = _entries[resName];
        if (e._poolResource == ~0u) {
            return _viewPool.GetTextureView(e._resource, usage, window);
        }
        return _pool->GetView(e._poolResource, usage, window);
    }

    auto AttachmentReservation::GetSRV(AttachmentName resName, const TextureViewDesc& window) const -> const std::shared_ptr<IResourceView>&
    {
        return GetView(resName, BindFlag::ShaderResource, window);
    }

    const std::shared_ptr<IResource>& AttachmentReservation::GetSemanticResource(uint64_t semantic) const
    {
        static std::shared_ptr<IResource> dummy;
        for (const auto& e:_entries)
            if (e._semantic == semantic) {
                if (e._poolResource == ~0u) return e._resource;
                return _pool->GetResource(e._poolResource);
            }
        return dummy;
    }

    void AttachmentReservation::DefineDoubleBufferAttachment(
        uint64_t yesterdaySemantic,
        uint64_t todaySemantic,
        const ResourceDesc& desc,
        ClearValue defaultContents,
        BindFlag::BitField initialLayout)
    {
        auto existingRegistration = std::find_if(_doubleBufferAttachments.begin(), _doubleBufferAttachments.end(), [y=yesterdaySemantic, t=todaySemantic](const auto& q) { return q._yesterdaySemantic == y || q._todaySemantic == t; });
        if (existingRegistration != _doubleBufferAttachments.end())
            if (!MatchRequest(desc, existingRegistration->_desc) || initialLayout != existingRegistration->_initialLayout || yesterdaySemantic != existingRegistration->_yesterdaySemantic || todaySemantic != existingRegistration->_todaySemantic)
                Throw(std::runtime_error("Double buffer attachment registered multiple times, and both registrations don't agree"));

        // figure out if we have the given attachment already. If not, we'll declare a new one with an initial clear operation
        auto existing = std::find_if(_entries.begin(), _entries.end(), [s=todaySemantic](const auto& q) { return q._semantic == s; });
        if (existing != _entries.end()) {
            // check compatibility, using the same behaviour as AttachmentPool::Reserve
            // We can get a mis-match if (for example) resolution changed and there's still a registered attachment with the previous size
            auto* res = (existing->_poolResource != ~0u) ? _pool->GetResource(existing->_poolResource).get() : existing->_resource.get();
            if (!MatchRequest(desc, res->GetDesc()))
                Throw(std::runtime_error("Double buffer attachment description mismatch between an existing registered attachment and requested attachment"));
            if (existing->_pendingSwitchToLayout.value_or(existing->_currentLayout) != initialLayout)
                Throw(std::runtime_error("Double buffer attachment layout mismatch between an existing registered attachment and requested attachment"));
            return;
        }

        // no existing entry, create a new one and ensure that there's a pending clear registered
        assert(_pool);
        PreregisteredAttachment reservation {
            0, desc, PreregisteredAttachment::State::Uninitialized, 0
        };
        auto newReservation = _pool->Reserve(MakeIteratorRange(&reservation, &reservation+1));
        assert(newReservation._entries.size() == 1);
        auto newEntry = std::move(newReservation._entries[0]);
        newReservation._entries.clear();

        newEntry._semantic = todaySemantic;
        newEntry._pendingClear = defaultContents;
        newEntry._pendingSwitchToLayout = initialLayout;
        _entries.emplace_back(std::move(newEntry));

        if (existingRegistration == _doubleBufferAttachments.end()) {
            _doubleBufferAttachments.push_back({
                yesterdaySemantic, todaySemantic, initialLayout, defaultContents, desc
            });
        }
    }

    void AttachmentReservation::DefineDoubleBufferAttachments(IteratorRange<const DoubleBufferAttachment*> attachments)
    {
        for (auto& a:attachments)
            DefineDoubleBufferAttachment(a._yesterdaySemantic, a._todaySemantic, a._desc, a._initialContents, a._initialLayout);
    }

    AttachmentReservation AttachmentReservation::CaptureDoubleBufferAttachments()
    {
        // Double buffer attachment reservations work both ways -- we register what we want to use from last frame, and those
        // same reservations are used to determine what we will pass on from this frame to the next
        AttachmentReservation result;
        result._pool = _pool;
        result._reservationFlags = _reservationFlags;
        for (const auto& res:_doubleBufferAttachments) {
            auto e = std::find_if(_entries.begin(), _entries.end(), [s=res._yesterdaySemantic](const auto& q) { return q._semantic == s; });
            if (e == _entries.end()) continue;      // didn't actually write out any information for this semantic
            if (e->_pendingClear.has_value() || e->_currentLayout == 0) continue;
            Entry newEntry = *e;
            newEntry._semantic = res._todaySemantic;        // flip the semantic
            if (newEntry._pendingSwitchToLayout.has_value() || (newEntry._currentLayout != res._initialLayout))
                newEntry._pendingSwitchToLayout = res._initialLayout;
            result._entries.emplace_back(std::move(newEntry));
        }
        result.AddRefAll();
        return result;
    }

    void AttachmentReservation::Absorb(AttachmentReservation&& src)
    {
        if (src._entries.empty()) return;
        _entries.reserve(_entries.size() + src._entries.size());
        for (auto& e:src._entries) {
            assert((e._poolResource == ~0u) || (src._pool == _pool || src._reservationFlags == _reservationFlags));

            auto existing = std::find_if(_entries.begin(), _entries.end(), [s=e._semantic](const auto& q) { return q._semantic == s; });
            if (existing != _entries.end()) {
                assert(0);      // if you hit this, it means we already have an attachment for this semantic
                continue;
            }

            _entries.emplace_back(std::move(e));
        }
        src._entries.clear();
    }

    void AttachmentReservation::CompleteInitialization(IThreadContext& threadContext)
    {
        for (unsigned c=0; c<_entries.size(); ++c)
            if (_entries[c]._pendingClear) {
                InitializeEmptyYesterdayAttachment(threadContext, *this, c, _entries[c]._pendingClear.value());
                _entries[c]._pendingClear = {};
            }

        VLA(IResource*, completeInitializationResources, _entries.size());
        size_t completeInitializationCount = 0;
        Metal::BarrierHelper barrierHelper{threadContext};

        for (auto& a:_entries) {
            if (a._pendingSwitchToLayout) {
                if (a._poolResource == ~0u) {
                    if (a._currentLayout == ~0u) {
                        barrierHelper.Add(*a._resource, Metal::BarrierResourceUsage::NoState(), *a._pendingSwitchToLayout);
                    } else
                        barrierHelper.Add(*a._resource, a._currentLayout, *a._pendingSwitchToLayout);
                } else {
                    auto& poolResource = _pool->_pimpl->_attachments[a._poolResource];
                    if (!poolResource._resource) {
                        _pool->_pimpl->BuildAttachment(a._poolResource);
                        assert(poolResource._resource);
                        barrierHelper.Add(*poolResource._resource, Metal::BarrierResourceUsage::NoState(), *a._pendingSwitchToLayout);
                    } else if (a._currentLayout == ~0u) {
                        // not a new texture, but we don't know the previous layout / usage
                        barrierHelper.Add(*poolResource._resource, Metal::BarrierResourceUsage::AllCommandsReadAndWrite(), *a._pendingSwitchToLayout);
                    } else {
                        barrierHelper.Add(*poolResource._resource, a._currentLayout, *a._pendingSwitchToLayout);
                    }
                    poolResource._pendingCompleteInitialization = false;
                }
                a._currentLayout = a._pendingSwitchToLayout.value();
                a._pendingSwitchToLayout = {};
            } else if (a._poolResource != ~0u) {
                auto& poolResource = _pool->_pimpl->_attachments[a._poolResource];
                if (poolResource._pendingCompleteInitialization) {
                    if (!poolResource._resource)
                        _pool->_pimpl->BuildAttachment(a._poolResource);
                    assert(poolResource._resource);
                    completeInitializationResources[completeInitializationCount++] = poolResource._resource.get();
                    poolResource._pendingCompleteInitialization = false;
                }
            }
        }

        Metal::CompleteInitialization(
            *Metal::DeviceContext::Get(threadContext),
            MakeIteratorRange(completeInitializationResources, &completeInitializationResources[completeInitializationCount]));
    }

    bool AttachmentReservation::HasPendingCompleteInitialization() const
    {
        for (auto& a:_entries) {
            if (a._pendingSwitchToLayout)
                return true;

            if (a._poolResource != ~0u) {
                auto& poolResource = _pool->_pimpl->_attachments[a._poolResource];
                if (poolResource._pendingCompleteInitialization)
                    return true;
            }
        }
        return false;
    }

    AttachmentReservation::AttachmentReservation() = default;
    
    AttachmentReservation::AttachmentReservation(AttachmentPool& pool) : _pool(&pool)
    {
    }

    AttachmentReservation::~AttachmentReservation()
    {
        ReleaseAll();
    }

    AttachmentReservation::AttachmentReservation(AttachmentReservation&& moveFrom)
    : _entries(std::move(moveFrom._entries))
    , _pool(std::move(moveFrom._pool))
    , _reservationFlags(std::move(moveFrom._reservationFlags))
    {
        moveFrom._pool = nullptr;
    }

    auto AttachmentReservation::operator=(AttachmentReservation&& moveFrom) -> AttachmentReservation&
    {
        ReleaseAll();
        _entries = std::move(moveFrom._entries);
        _pool = std::move(moveFrom._pool);
        _reservationFlags = std::move(moveFrom._reservationFlags);
        moveFrom._pool = nullptr;
        return *this;
    }

    AttachmentReservation::AttachmentReservation(const AttachmentReservation& copyFrom)
    : _entries(copyFrom._entries)
    , _pool(copyFrom._pool)
    , _reservationFlags(copyFrom._reservationFlags)
    {
        AddRefAll();
    }

    auto AttachmentReservation::operator=(const AttachmentReservation& copyFrom) -> AttachmentReservation&
    {
        ReleaseAll();
        _entries = copyFrom._entries;
        _pool = copyFrom._pool;
        _reservationFlags = copyFrom._reservationFlags;
        AddRefAll();
        return *this;
    }

    AttachmentReservation::AttachmentReservation(
        std::vector<AttachmentToReserve>&& reservedAttachments,
        AttachmentPool* pool,
        ReservationFlag::BitField flags)
    : _pool(pool)
    , _reservationFlags(flags)
    {
        _entries.reserve(reservedAttachments.size());
        for (const auto& a:reservedAttachments) {
            Entry e;
            e._poolResource = a._poolName;
            e._resource = std::move(a._resource);
            e._semantic = a._semantic;
            e._pendingClear = a._pendingClear;
            e._pendingSwitchToLayout = a._pendingSwitchToLayout;
            e._currentLayout = a._currentLayout.value_or(~0u);
            _entries.push_back(e);
        }

        AddRefAll();
    }

    void AttachmentReservation::Remove(AttachmentName resName)
    {
        // remove a specific entry from the attachment reservation (and release the ref count)
        // note that this will rebind all of the attachment names (since they are just indices into the internal array)
        assert(resName < _entries.size());
        if (_pool && _entries[resName]._poolResource != ~0u) {
            AttachmentName toRelease = _entries[resName]._poolResource;
            _pool->Release(MakeIteratorRange(&toRelease, &toRelease+1), _reservationFlags);
        }
        _entries.erase(_entries.begin()+resName);
    }

    void AttachmentReservation::ReleaseAll()
    {
        if (_pool) {
            VLA(AttachmentName, poolAttachmentsToRelease, _entries.size());
            unsigned attachmentsToReleaseCount = 0;
            for (auto& a:_entries)
                if (a._poolResource != ~0u)
                    poolAttachmentsToRelease[attachmentsToReleaseCount++] = a._poolResource;
            if (attachmentsToReleaseCount)
                _pool->Release(MakeIteratorRange(poolAttachmentsToRelease, &poolAttachmentsToRelease[attachmentsToReleaseCount]), _reservationFlags);
        }
    }

    void AttachmentReservation::AddRefAll()
    {
        if (_pool) {
            VLA(AttachmentName, poolAttachmentsToRelease, _entries.size());
            unsigned attachmentsToReleaseCount = 0;
            for (auto& a:_entries)
                if (a._poolResource != ~0u)
                    poolAttachmentsToRelease[attachmentsToReleaseCount++] = a._poolResource;
            if (attachmentsToReleaseCount)
                _pool->AddRef(MakeIteratorRange(poolAttachmentsToRelease, &poolAttachmentsToRelease[attachmentsToReleaseCount]), _reservationFlags);
        }
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
        return _attachmentPoolReservation.GetResource(resName);
    }

    auto RenderPassInstance::GetSRVForAttachmentName(AttachmentName resName, const TextureViewDesc& window) const -> const std::shared_ptr<IResourceView>&
    {
        return _attachmentPoolReservation.GetSRV(resName, window);
    }

    auto RenderPassInstance::GetInputAttachmentResource(unsigned inputAttachmentSlot) const -> const std::shared_ptr<IResource>&
	{
		const auto& subPass = _layout->GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetInputs()[inputAttachmentSlot]._resourceName;
        return _attachmentPoolReservation.GetResource(resName);
	}

    auto RenderPassInstance::GetInputAttachmentView(unsigned inputAttachmentSlot) const -> const std::shared_ptr<IResourceView>&
	{
		const auto& subPass = _layout->GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetInputs()[inputAttachmentSlot]._resourceName;
        return _attachmentPoolReservation.GetView(resName, BindFlag::InputAttachment, subPass.GetInputs()[inputAttachmentSlot]._window);
	}
	
	auto RenderPassInstance::GetOutputAttachmentResource(unsigned outputAttachmentSlot) const -> const std::shared_ptr<IResource>&
	{
		const auto& subPass = _layout->GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetOutputs()[outputAttachmentSlot]._resourceName;
        return _attachmentPoolReservation.GetResource(resName);
	}
	
	auto RenderPassInstance::GetOutputAttachmentSRV(unsigned outputAttachmentSlot, const TextureViewDesc& window) const -> const std::shared_ptr<IResourceView>&
	{
		const auto& subPass = _layout->GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetOutputs()[outputAttachmentSlot]._resourceName;
        return _attachmentPoolReservation.GetSRV(resName, window);
	}

	auto RenderPassInstance::GetDepthStencilAttachmentResource() const -> const std::shared_ptr<IResource>&
	{
		const auto& subPass = _layout->GetSubpasses()[GetCurrentSubpassIndex()];
		auto resName = subPass.GetDepthStencil()._resourceName;
        return _attachmentPoolReservation.GetResource(resName);
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
        AttachmentReservation* parentReservation,
        const RenderPassBeginDesc& beginInfo)
    {
        _attachedContext = Metal::DeviceContext::Get(context).get();

        auto fb = frameBufferPool.BuildFrameBuffer(
            Metal::GetObjectFactory(*context.GetDevice()),
            layout, fullAttachmentsDescription, attachmentPool, parentReservation);
        fb._poolReservation.CompleteInitialization(context);

        _frameBuffer = std::move(fb._frameBuffer);
        _attachmentPoolReservation = std::move(fb._poolReservation);
        _layout = fb._completedDesc;        // expecting this to be retained by the pool until at least the destruction of this
        _trueRenderPass = true;

        #if defined(_DEBUG)
            _attachedContext->BeginLabel(_layout->GetSubpasses()[0]._name.empty() ? "<<unnnamed subpass>>" : _layout->GetSubpasses()[0]._name.c_str());
        #endif
        _attachedContext->BeginRenderPass(*_frameBuffer, beginInfo._clearValues);
        _attachedParsingContext = nullptr;
    }

    static bool operator==(const AttachmentTransform& lhs, const AttachmentTransform& rhs)
    {
        return (lhs._type == rhs._type) && (lhs._initialLayout == rhs._initialLayout) && (lhs._finalLayout == rhs._finalLayout);
    }

    RenderPassInstance::RenderPassInstance(
        ParsingContext& parsingContext,
        const FragmentStitchingContext::StitchResult& stitchedFragment,
        const RenderPassBeginDesc& beginInfo)
    {
        _attachedContext = nullptr;
        _trueRenderPass = false;

        auto& stitchContext = parsingContext.GetFragmentStitchingContext();
        auto& parentReservation = parsingContext.GetAttachmentReservation();
        if (stitchedFragment._pipelineType == PipelineType::Graphics) {

            #if defined(_DEBUG)
                {
                    // attachment transforms in the stitched fragment must agree with what we get from the fbDesc
                    VLA_UNSAFE_FORCE(AttachmentTransform, generatedAttachmentTransforms, stitchedFragment._fbDesc.GetAttachments().size());
                    CalculateAttachmentTransforms(
                        MakeIteratorRange(generatedAttachmentTransforms, &generatedAttachmentTransforms[stitchedFragment._fbDesc.GetAttachments().size()]),
                        stitchedFragment._fbDesc);
                    assert(stitchedFragment._fbDesc.GetAttachments().size() == stitchedFragment._attachmentTransforms.size());
                    for (unsigned c=0; c<stitchedFragment._fbDesc.GetAttachments().size(); ++c)
                        assert(generatedAttachmentTransforms[c] == stitchedFragment._attachmentTransforms[c]);
                }
            #endif

            *this = RenderPassInstance {
                parsingContext.GetThreadContext(), stitchedFragment._fbDesc, stitchedFragment._fullAttachmentDescriptions,
                *parsingContext.GetTechniqueContext()._frameBufferPool,
                *parsingContext.GetTechniqueContext()._attachmentPool,
                &parentReservation,
                beginInfo };
            parsingContext.GetViewport() = _frameBuffer->GetDefaultViewport();
        } else {
            auto& attachmentPool = *parsingContext.GetTechniqueContext()._attachmentPool;
            _attachmentPoolReservation = attachmentPool.Reserve(stitchedFragment._fullAttachmentDescriptions, &parentReservation);
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

        // Update the records in the parsing context with what's changed
        stitchContext.UpdateAttachments(stitchedFragment);
        parentReservation.UpdateAttachments(_attachmentPoolReservation, stitchedFragment._attachmentTransforms);

        _viewedAttachmentsMap = stitchedFragment._viewedAttachmentsMap;
        _viewedAttachments.reserve(stitchedFragment._viewedAttachments.size());
        for (const auto&view:stitchedFragment._viewedAttachments)
            _viewedAttachments.push_back(_attachmentPoolReservation.GetView(view._resourceName, view._usage, view._window));
    }

    RenderPassInstance::RenderPassInstance(
        ParsingContext& parsingContext,
        const FrameBufferDescFragment& layout,
        const RenderPassBeginDesc& beginInfo)
    {
        _attachedContext = nullptr;
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
        AttachmentPool& attachmentPool)
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
		_attachmentPoolReservation = attachmentPool.Reserve(resolvedAttachmentDescs);
        assert(!_attachmentPoolReservation.HasPendingCompleteInitialization());
	}
    
    RenderPassInstance::~RenderPassInstance() 
    {
        End();
    }

    RenderPassInstance::RenderPassInstance(RenderPassInstance&& moveFrom) never_throws
    : _frameBuffer(std::move(moveFrom._frameBuffer))
    , _attachedContext(moveFrom._attachedContext)
    , _attachmentPoolReservation(std::move(moveFrom._attachmentPoolReservation))
	, _layout(std::move(moveFrom._layout))
    , _viewedAttachments(std::move(moveFrom._viewedAttachments))
    , _viewedAttachmentsMap(std::move(moveFrom._viewedAttachmentsMap))
    , _currentSubpassIndex(moveFrom._currentSubpassIndex)
    , _trueRenderPass(moveFrom._trueRenderPass)
    {
        moveFrom._attachedContext = nullptr;
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
        _attachmentPoolReservation = std::move(moveFrom._attachmentPoolReservation);
        moveFrom._attachedContext = nullptr;
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
        _trueRenderPass = false;
        _attachedParsingContext = nullptr;
        _layout = nullptr;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

	uint64_t PreregisteredAttachment::CalculateHash() const
	{
		uint64_t result = HashCombine(_semantic, _desc.CalculateHash());
		auto shift = (unsigned)_state;
		lrot(result, shift);
        result += _layout;
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
            std::optional<BindFlag::BitField> _firstAccessInitialLayout;
            uint64_t _lastWriteSemantic = 0;
            LoadStore _lastAccessStore = LoadStore::DontCare;
            std::optional<BindFlag::BitField> _lastAccessFinalLayout;

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
        _firstAccessInitialLayout = attachment._layout;
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
            << AsString(attachment._state) << "/"
            << BindFlagsAsString(attachment._layout) << "}";
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

    static std::string s_defaultLayout = "<<default layout>>";

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
            << "FirstAccess: {" << AttachmentSemantic{attachment._firstAccessSemantic} << ", " << (attachment._firstAccessInitialLayout ? BindFlagsAsString(*attachment._firstAccessInitialLayout) : s_defaultLayout) << ", " << AsString(attachment._firstAccessLoad) << "}, "
            << "LastAccess: {" << AttachmentSemantic{attachment._lastWriteSemantic} << ", " << (attachment._lastAccessFinalLayout ? BindFlagsAsString(*attachment._lastAccessFinalLayout) : s_defaultLayout) << ", " << AsString(attachment._lastAccessStore) << "}, "
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
            str << ": " << fragment._attachments[c]._matchingRules;
            str << ", L: " << AsString(fragment._attachments[c]._loadFromPreviousPhase) << "/" << (fragment._attachments[c]._initialLayout ? BindFlagsAsString(*fragment._attachments[c]._initialLayout) : s_defaultLayout)
                << ", S: " << AsString(fragment._attachments[c]._storeToNextPhase) << "/" << (fragment._attachments[c]._finalLayout ? BindFlagsAsString(*fragment._attachments[c]._finalLayout) : s_defaultLayout)
                << std::endl;
        }
        str << "Subpasses: " << std::endl;
        for (unsigned c=0; c<fragment._subpasses.size(); ++c) {
            str << StreamIndent(4) << "[" << c << "] " << fragment._subpasses[c] << std::endl;
        }
        return str;
    }

    static std::ostream& operator<<(std::ostream& str, const FrameBufferDescFragment::Attachment& attachment)
    {
        str << AttachmentSemantic{attachment._semantic} 
            << " : " << attachment._matchingRules
            << ", L: " << AsString(attachment._loadFromPreviousPhase) << "/" << (attachment._initialLayout ? BindFlagsAsString(*attachment._initialLayout) : s_defaultLayout)
            << ", S: " << AsString(attachment._storeToNextPhase) << "/" << (attachment._finalLayout ? BindFlagsAsString(*attachment._finalLayout) : s_defaultLayout);
        return str;
    }

    static bool CompareAttachmentName(const WorkingAttachmentContext::Attachment& lhs, const WorkingAttachmentContext::Attachment& rhs)
    {
        return lhs._name < rhs._name;
    }

    static PreregisteredAttachment BuildPreregisteredAttachment(
        const FrameBufferDescFragment::Attachment& attachmentDesc,
        BindFlag::BitField usageBindFlags,
        const FrameBufferProperties& props)
    {
        Format fmt = Format::Unknown;
        if (attachmentDesc._matchingRules._flagsSet & uint32_t(AttachmentMatchingRules::Flags::FixedFormat))
            fmt = attachmentDesc._matchingRules._fixedFormat;
        else if (attachmentDesc._matchingRules._flagsSet & uint32_t(AttachmentMatchingRules::Flags::SystemFormat))
            fmt = ResolveSystemFormat(attachmentDesc._matchingRules._systemFormat);
        assert(fmt != Format::Unknown);

        TextureDesc tDesc = TextureDesc::Plain2D(
            (unsigned)props._width, (unsigned)props._height,
            fmt, 1, 0u,
            GetSamples(attachmentDesc._matchingRules, props));
        auto bindFlags = usageBindFlags | attachmentDesc._initialLayout.value_or(0) | attachmentDesc._finalLayout.value_or(0) | attachmentDesc._matchingRules._requiredBindFlags;

        PreregisteredAttachment result;
        result._desc = CreateDesc(bindFlags, AllocationRules::ResizeableRenderTarget, tDesc, "attachment-pool");
        assert(result._desc._textureDesc._format != Format::Unknown);       // at this point we must have a resolved format. If it's still unknown, we can't created a preregistered attachment
        result._semantic = attachmentDesc._semantic;
        result._state = PreregisteredAttachment::State::Uninitialized;
        result._layout = attachmentDesc._initialLayout.value_or(0);
        return result;
    }

    static FrameBufferDesc BuildFrameBufferDesc(
        const FrameBufferDescFragment& fragment,
        const FrameBufferProperties& props,
        IteratorRange<const PreregisteredAttachment*> fullAttachmentDescriptions)
    {
        std::vector<AttachmentDesc> fbAttachments;
        fbAttachments.reserve(fragment._attachments.size());
        for (const auto& inputFrag:fragment._attachments) {
            AttachmentDesc desc;
            desc._loadFromPreviousPhase = inputFrag._loadFromPreviousPhase;
            desc._storeToNextPhase = inputFrag._storeToNextPhase;
            desc._initialLayout = inputFrag._initialLayout.value_or(0);
            desc._finalLayout = inputFrag._finalLayout.value_or(0);

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
        for (const auto&a:fragment._attachments) {
            auto idx = unsigned(&a - AsPointer(fragment._attachments.begin()));
            auto directionFlags = GetDirectionFlags(fragment, idx);
            assert(directionFlags & DirectionFlags::Reference);         // if you hit this, it probably means an attachment is defined in the fragment, but not used by any subpass
            auto usageFlags = CalculateBindFlags(fragment, idx);
            assert(a._initialLayout && a._finalLayout && *a._finalLayout);

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
                i->_layout = a._initialLayout.value();
                result._fullAttachmentDescriptions.push_back(*i);

                auto requiredBindFlags = usageFlags | *a._initialLayout | *a._finalLayout; 
                if ((i->_desc._bindFlags & requiredBindFlags) != requiredBindFlags)
                    Throw(std::runtime_error((StringMeld<512>() << "FrameBufferDescFragment requires attachment bind flags that are not present in the preregistered attachment. Attachment semantic (" << AttachmentSemantic{a._semantic} << "). Preregistered attachment bind flags: (" << BindFlagsAsString(i->_desc._bindFlags) << "), Frame buffer request bind flags: (" << BindFlagsAsString(requiredBindFlags) << ")").AsString()));

                AttachmentTransform transform;
                if (directionFlags & DirectionFlags::RetainsOnExit) {
                    // directionFlags will have DirectionFlags::WritesData if we actually write something
                    if (directionFlags & DirectionFlags::RequirePreinitializedData) transform._type = AttachmentTransform::LoadedAndStored;
                    else transform._type = AttachmentTransform::Generated;
                } else {
                    assert(directionFlags & DirectionFlags::Reference);
                    if (directionFlags & DirectionFlags::RequirePreinitializedData) transform._type = AttachmentTransform::Consumed;
                    else transform._type = AttachmentTransform::Temporary;
                }

                transform._initialLayout = *a._initialLayout;
                transform._finalLayout = *a._finalLayout;
                assert(transform._finalLayout);     // we must have a defined final layout
                result._attachmentTransforms.push_back(transform);
            } else {
                auto newAttachment = BuildPreregisteredAttachment(a, usageFlags, _workingProps);
                #if defined(_DEBUG)
                    if (newAttachment._desc._textureDesc._format == Format::Unknown)
                        Log(Warning) << "Missing format information for attachment with semantic: " << AttachmentSemantic{a._semantic} << std::endl;
                #endif 
                newAttachment._layout = a._initialLayout.value();
                result._fullAttachmentDescriptions.push_back(newAttachment);
                AttachmentTransform transform;
                assert(!(directionFlags & DirectionFlags::RequirePreinitializedData));      // If you hit this, it means the fragment has an attachment that loads data, but there's no matching attachment in the stitching context
                if (directionFlags & DirectionFlags::RetainsOnExit) {
                    assert(directionFlags & DirectionFlags::WritesData);
                    transform._type = AttachmentTransform::Generated;
                } else
                    transform._type = AttachmentTransform::Temporary;
                transform._initialLayout = *a._initialLayout;
                transform._finalLayout = *a._finalLayout;
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

        result._fbDesc = BuildFrameBufferDesc(
            fragment, _workingProps,
            MakeIteratorRange(result._fullAttachmentDescriptions));

        #if defined(_DEBUG)
            {
                // attachment transforms in the stitched fragment must agree with what we get from the fbDesc
                VLA_UNSAFE_FORCE(AttachmentTransform, generatedAttachmentTransforms, result._fbDesc.GetAttachments().size());
                CalculateAttachmentTransforms(
                    MakeIteratorRange(generatedAttachmentTransforms, &generatedAttachmentTransforms[result._fbDesc.GetAttachments().size()]),
                    result._fbDesc);
                assert(result._fbDesc.GetAttachments().size() == result._attachmentTransforms.size());
                for (unsigned c=0; c<result._fbDesc.GetAttachments().size(); ++c)
                    assert(generatedAttachmentTransforms[c] == result._attachmentTransforms[c]);
            }
        #endif

        return result;
    }
    
    void FragmentStitchingContext::UpdateAttachments(const StitchResult& stitchResult)
    {
        for (unsigned aIdx=0; aIdx<stitchResult._attachmentTransforms.size(); ++aIdx) {
            auto semantic = stitchResult._fullAttachmentDescriptions[aIdx]._semantic;
            if (!semantic) continue;
            switch (stitchResult._attachmentTransforms[aIdx]._type) {
            case AttachmentTransform::LoadedAndStored:
            case AttachmentTransform::Generated:
                {
                    auto desc = stitchResult._fullAttachmentDescriptions[aIdx];
                    desc._state = PreregisteredAttachment::State::Initialized;
                    desc._layout = stitchResult._attachmentTransforms[aIdx]._finalLayout;
                    DefineAttachment(desc);
                    break;
                }
            case AttachmentTransform::Temporary:
            case AttachmentTransform::Consumed:
                Undefine(semantic);
                break;
            }
        }
    }

    static void PatchInDefaultLayouts(FrameBufferDescFragment& fragment);
    static void CheckNonFrameBufferAttachmentLayouts(FrameBufferDescFragment& fragment);

    auto FragmentStitchingContext::TryStitchFrameBufferDesc(IteratorRange<const FrameBufferDescFragment*> fragments) -> StitchResult
    {
        auto merged = MergeFragments(MakeIteratorRange(_workingAttachments), fragments, _workingProps, MakeIteratorRange(_systemFormats));
        PatchInDefaultLayouts(merged._mergedFragment);
        CheckNonFrameBufferAttachmentLayouts(merged._mergedFragment);
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
            assert(MatchRequest(resourceDesc, i->_desc));
            if (initialLayoutFlags)
                i->_layout = initialLayoutFlags;
            i->_state = state;
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
            assert(MatchRequest(attachment._desc, i->_desc));
            if (attachment._layout)
                i->_layout = attachment._layout;
            i->_state = attachment._state;
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
            Throw(std::runtime_error("Attempting to call DefineDoubleBufferAttachment() for a semantic that doesn't have a predefined attachment yet. Define the attachment for the current frame first, before requiring a double buffer of it."));

        auto i3 = std::find_if(
			_doubleBufferAttachments.begin(), _doubleBufferAttachments.end(),
			[semantic](const auto& c) { return c._todaySemantic == semantic; });
        if (i3 != _doubleBufferAttachments.end()) {
            // can't easily check if the clear values are the same, because it's an enum
            assert(i3->_initialLayout == initialLayoutFlags);
            return; // already defined
        }

        auto i2 = std::find_if(
			_workingAttachments.begin(), _workingAttachments.end(),
			[semantic](const auto& c) { return c._semantic == semantic+1; });
        if (i2 != _workingAttachments.end())
            Throw(std::runtime_error("Attempting to call DefineDoubleBufferAttachment(), but there is an overlapping predefined attachment for the double buffer. Only predefine the attachment once."));

        DoubleBufferAttachment a;
        a._todaySemantic = semantic+1;
        a._yesterdaySemantic = semantic;
        a._initialContents = initialContents;
        a._initialLayout = initialLayoutFlags;
        a._desc = i->_desc;
        XlCatString(a._desc._name, "-prev");
        _doubleBufferAttachments.push_back(a);
        assert(initialLayoutFlags != 0);
        DefineAttachment(a._todaySemantic, a._desc, PreregisteredAttachment::State::Initialized, initialLayoutFlags);
    }

    Format FragmentStitchingContext::GetSystemAttachmentFormat(SystemAttachmentFormat fmt) const
    {
        if ((unsigned)fmt < dimof(_systemFormats))
            return _systemFormats[(unsigned(fmt))];
        return Format::Unknown;
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
                        newState->_firstAccessInitialLayout = *interfaceAttachment._initialLayout;
                }

                if (directionFlags & DirectionFlags::WritesData) {
                    newState->_containsDataForSemantic = interfaceAttachment.GetOutputSemanticBinding();
                    newState->_lastWriteSemantic = interfaceAttachment.GetOutputSemanticBinding();
                }

                newState->_lastAccessStore = interfaceAttachment._storeToNextPhase;
                newState->_lastAccessFinalLayout = interfaceAttachment._finalLayout;
                if (!HasRetain(interfaceAttachment._storeToNextPhase)) {
                    newState->_shouldReceiveDataForSemantic = 0;
                    newState->_containsDataForSemantic = 0;     // if we don't explicitly retain the data, let's forget it exists
                }
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

    static void PatchInDefaultLayouts(FrameBufferDescFragment& fragment)
    {
        // For all attachments that don't have initial or final layouts explicitly set, we'll give them
        // layouts based on the first or last usage
        VLA(BindFlag::BitField, finalUsages, fragment.GetAttachments().size());
        VLA(BindFlag::BitField, subpassUsages, fragment.GetAttachments().size());
        for (unsigned c=0; c<fragment.GetAttachments().size(); ++c) finalUsages[c] = 0;

        for (const auto& sp:fragment.GetSubpasses()) {
            for (unsigned c=0; c<fragment.GetAttachments().size(); ++c) subpassUsages[c] = 0;

            for (const auto& v:sp.GetOutputs())
                subpassUsages[v._resourceName] |= BindFlag::RenderTarget;
            for (const auto& v:sp.GetInputs())
                subpassUsages[v._resourceName] |= BindFlag::InputAttachment;
            if (sp.GetDepthStencil()._resourceName != ~0u)
                subpassUsages[sp.GetDepthStencil()._resourceName] |= BindFlag::DepthStencil;
            for (const auto& v:sp.GetViews())
                subpassUsages[v._resourceName] |= v._usage;

            for (unsigned c=0; c<fragment.GetAttachments().size(); ++c)
                if (!fragment.GetAttachments()[c]._initialLayout && subpassUsages[c])
                    fragment.GetAttachments()[c]._initialLayout = subpassUsages[c];

            for (unsigned c=0; c<fragment.GetAttachments().size(); ++c)
                if (subpassUsages[c])
                    finalUsages[c] = subpassUsages[c];
        }

        for (unsigned c=0; c<fragment.GetAttachments().size(); ++c) {
            if (!fragment.GetAttachments()[c]._finalLayout && finalUsages[c])
                fragment.GetAttachments()[c]._finalLayout = finalUsages[c];

            // we must have resolved a reasonable final layout
            assert(fragment.GetAttachments()[c]._finalLayout && *fragment.GetAttachments()[c]._finalLayout != 0);

            // clear/dont-care load attachments can have an initial layout of 0; but otherwise we must have an initial layout
            assert(!HasRetain(fragment.GetAttachments()[c]._loadFromPreviousPhase) || fragment.GetAttachments()[c]._initialLayout.value_or(0) != 0);
        }
    }

    static void CheckNonFrameBufferAttachmentLayouts(FrameBufferDescFragment& fragment)
    {
        if (fragment._pipelineType == PipelineType::Graphics) {
            // With graphics pipelines, we can't change the layout of attachments based on the non frame buffer attachment requests
            // So, the non frame buffer requests must be compatible with whatever layout the attachment is going to end up in
            #if defined(_DEBUG)
                VLA(BindFlag::BitField, attachmentState, fragment.GetAttachments().size());
                for (unsigned c=0; c<fragment.GetAttachments().size(); ++c)
                    attachmentState[c] = fragment.GetAttachments()[c]._initialLayout.value_or(0);

                for (unsigned spIdx=0; spIdx<fragment.GetSubpasses().size(); ++spIdx) {
                    auto& sp = fragment.GetSubpasses()[spIdx];
                    
                    for (const auto& o:sp.GetOutputs())
                        attachmentState[o._resourceName] = BindFlag::RenderTarget;
                    for (const auto& i:sp.GetInputs())
                        attachmentState[i._resourceName] = BindFlag::ShaderResource;
                    if (sp.GetDepthStencil()._resourceName != ~0u)
                        attachmentState[sp.GetDepthStencil()._resourceName] = BindFlag::DepthStencil;

                    for (const auto& nonfb:sp.GetViews()) {
                        // usage must agree with expectations (or at least have "simultaneous" flags set)
                        // note -- we're not validating that we're using the correct "simultaneous" flags; just that we're using at least one
                        auto simultaneousFlags = nonfb._window._flags &
                            ( TextureViewDesc::Flags::SimultaneouslyColorAttachment | TextureViewDesc::Flags::SimultaneouslyColorReadOnly
                            | TextureViewDesc::Flags::SimultaneouslyDepthAttachment | TextureViewDesc::Flags::SimultaneouslyDepthReadOnly
                            | TextureViewDesc::Flags::SimultaneouslyStencilAttachment | TextureViewDesc::Flags::SimultaneouslyStencilReadOnly );
                        assert(nonfb._usage == attachmentState[nonfb._resourceName] || simultaneousFlags);
                    }
                }
            #endif
        } else {
            // For compute pipelines, we could potential figure out the barriers that need to be added here
            // it's also possible for the client to just add the barriers as needed, though, and the client must have more context to control the barriers better?
            assert(fragment._pipelineType == PipelineType::Compute);
        }
    }

    static AttachmentName RemapAttachmentNameForSimplifyTest(
        AttachmentName input,
        const RenderCore::Techniques::FrameBufferDescFragment& srcFragment,
        RenderCore::Techniques::FrameBufferDescFragment& dstFragment,
        std::vector<std::pair<RenderCore::AttachmentName, RenderCore::AttachmentName>>& remapping,
        IteratorRange<const RenderCore::AttachmentName*> prevWrittenAttachments)
    {
        if (input == ~0u) return input;

        auto existing = LowerBound(remapping, input);
        if (existing == remapping.end() || existing->first != input) {
            auto a = srcFragment._attachments[input];
            // Always have to store, because an attachment that is passed from one subpass to the next will
            // be transformed in passing an attachment from one fragment to the next.
            // Since we don't know if there's an upcoming subpass that will use this attachment, we need to
            // assume it exists
            a._storeToNextPhase = LoadStore::Retain;
            // Also, if we used this attachment in a previous subpass, we havet oa ssume this is now a retain
            auto q = std::lower_bound(prevWrittenAttachments.begin(), prevWrittenAttachments.end(), input);
            if (q != prevWrittenAttachments.end() && *q == input)
                a._loadFromPreviousPhase = LoadStore::Retain;
            auto newName = dstFragment.DefineAttachment(a);
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
        TRY
        {
            std::vector<FrameBufferDescFragment> testFragments;
            std::vector<AttachmentName> allWrittenAttachments;

            // Create a separate fragment for each subpass
            for (const auto&subpass:inputFragment._subpasses) {
                std::vector<std::pair<AttachmentName, AttachmentName>> remapping;
                FrameBufferDescFragment separatedFragment;
                auto remappedSubpass = RemapSubpassDesc(
					subpass,
					std::bind(&RemapAttachmentNameForSimplifyTest, std::placeholders::_1, std::ref(inputFragment), std::ref(separatedFragment), std::ref(remapping), MakeIteratorRange(allWrittenAttachments)));
                separatedFragment.AddSubpass(std::move(remappedSubpass));
                testFragments.emplace_back(std::move(separatedFragment));

                // record each attachment that the subpass touched
                auto q = allWrittenAttachments.begin();
                for (auto a:remapping) {
                    auto i = std::lower_bound(q, allWrittenAttachments.end(), a.first);
                    if (i == allWrittenAttachments.end() || *i != a.first)
                        q = allWrittenAttachments.insert(i, a.first);
                }
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

