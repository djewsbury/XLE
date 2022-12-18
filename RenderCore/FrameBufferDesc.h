// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ResourceDesc.h"
#include "IDevice_Forward.h"
#include "../Utility/IteratorUtils.h"
#include <memory>
#include <string>

namespace RenderCore
{
    using AttachmentName = uint32_t;

	enum class LoadStore
	{
		DontCare, Retain, Clear,

		DontCare_StencilRetain, DontCare_StencilClear, 
        Retain_StencilDontCare, Retain_StencilClear, 
		Clear_StencilDontCare, Clear_StencilRetain
	};

    const char* AsString(LoadStore);
    std::pair<LoadStore, LoadStore> SplitAspects(LoadStore);
    LoadStore CombineAspects(LoadStore mainAspect, LoadStore stencilAspect);

    /// <summary>Attachments are part of a frame buffer, and typically represent a rendering surface</summary>
    /// This description object can define an attachment. Typically the attachment is defined in terms of
    /// some global frame buffer properties (such as output dimensions and sample count).
    class AttachmentViewDesc
    {
    public:
        AttachmentName _resourceName = ~0u;
		TextureViewDesc _window = {};
    };

    class AttachmentDesc
    {
    public:
        Format _format = Format(0);
        struct Flags
        {
            enum Enum
            {
                Multisampled		            = 1<<0,     ///< use the current multisample settings (otherwise just set to single sampled mode)
            };
            using BitField = unsigned;
        };
        Flags::BitField _flags = 0;

        LoadStore _loadFromPreviousPhase = LoadStore::Retain;       ///< equivalent to "load op" in a Vulkan attachment
        LoadStore _storeToNextPhase = LoadStore::Retain;            ///< equivalent to "store op" in a Vulkan attachment
        BindFlag::BitField _initialLayout = 0u;                     ///< we use this to select the optimal final layout of the resource. This is how the resource is left post-renderpass (for example, for presentation targets)
        BindFlag::BitField _finalLayout = 0u;                       ///< we use this to select the optimal final layout of the resource. This is how the resource is left post-renderpass (for example, for presentation targets)

        uint64_t CalculateHash() const;

        #if defined(_DEBUG)
            mutable std::string _name = std::string();
            inline void SetName(const std::string& name) const { _name = name; }
            inline void SetName(const std::string& name) { _name = name; }
        #else
            inline void SetName(const std::string& name) const {}
            inline void SetName(const std::string& name) {}
        #endif
    };

	class FrameBufferDesc;

    /// <summary>Defines which attachments are used during a subpass (and ordering)</summary>
    /// Input attachments are read by shader stages. Output attachments are for color data written
    /// from pixel shaders. There can be 0 or 1 depth stencil attachments.
    /// Finally, "preserved" attachments are not used during this subpass, but their contents are
    /// preserved to be used in future subpasses.
    class SubpassDesc
    {
    public:
		static const AttachmentViewDesc Unused;

        SubpassDesc& AppendOutput(AttachmentName attachment, const TextureViewDesc& = {});
        SubpassDesc& AppendInput(AttachmentName attachment, const TextureViewDesc& = {});
        SubpassDesc& SetDepthStencil(AttachmentName attachment, const TextureViewDesc& = {});
		SubpassDesc& AppendResolveOutput(AttachmentName attachment, const TextureViewDesc& = {});
        SubpassDesc& SetResolveDepthStencil(AttachmentName attachment, const TextureViewDesc& = {});
        SubpassDesc& SetViewInstanceMask(uint32_t);

        uint64_t CalculateHash() const;

		IteratorRange<const AttachmentViewDesc*> GetOutputs() const;
		const AttachmentViewDesc& GetDepthStencil() const;
		IteratorRange<const AttachmentViewDesc*> GetInputs() const;
		IteratorRange<const AttachmentViewDesc*> GetResolveOutputs() const;
		const AttachmentViewDesc& GetResolveDepthStencil() const;
        IteratorRange<const AttachmentViewDesc*> GetViews() const;
        uint32_t GetViewInstanceMask() const;

        IteratorRange<AttachmentViewDesc*> GetOutputs();
		AttachmentViewDesc& GetDepthStencil();
		IteratorRange<AttachmentViewDesc*> GetInputs();
		IteratorRange<AttachmentViewDesc*> GetResolveOutputs();
		AttachmentViewDesc& GetResolveDepthStencil();
        IteratorRange<AttachmentViewDesc*> GetViews();

		#if defined(_DEBUG)
            mutable std::string _name = std::string();
            inline const SubpassDesc& SetName(const std::string& name) const { _name = name; return *this; }
            inline SubpassDesc& SetName(const std::string& name) { _name = name; return *this; }
        #else
            inline const SubpassDesc& SetName(const std::string& name) const { return *this; }
            inline SubpassDesc& SetName(const std::string& name) { return *this; }
        #endif

		friend FrameBufferDesc SeparateSingleSubpass(const FrameBufferDesc& input, unsigned subpassIdx);

	private:
		static const unsigned s_maxAttachmentCount = 32u;
		AttachmentViewDesc _attachmentViewBuffer[s_maxAttachmentCount];

		unsigned _outputAttachmentCount = 0;
		unsigned _inputAttachmentCount = 0;
		unsigned _resolveOutputAttachmentCount = 0;

		AttachmentViewDesc _depthStencil = Unused;
        AttachmentViewDesc _resolveDepthStencil = Unused;

        uint32_t _viewInstancingMask = 0;

		unsigned BufferSpaceUsed() const { return _outputAttachmentCount+_inputAttachmentCount+_resolveOutputAttachmentCount; }
    };

    class FrameBufferProperties
    {
    public:
        unsigned _width = 0, _height = 0;
        TextureSamples _samples = TextureSamples::Create();

        uint64_t CalculateHash() const;
        uint64_t CalculateHashResolutionIndependent() const;
    };

    class FrameBufferDesc
	{
	public:
        auto	GetSubpasses() const -> IteratorRange<const SubpassDesc*> { return MakeIteratorRange(_subpasses); }
        auto    GetAttachments() const -> IteratorRange<const AttachmentDesc*> { return MakeIteratorRange(_attachments); }
        auto	GetSubpasses() -> IteratorRange<SubpassDesc*> { return MakeIteratorRange(_subpasses); }
        auto    GetAttachments() -> IteratorRange<AttachmentDesc*> { return MakeIteratorRange(_attachments); }

        const FrameBufferProperties& GetProperties() const { return _props; }

        uint64_t    GetHash() const { return _hash; }
        uint64_t    GetHashExcludingDimensions() const { return _hashExcludingDimensions; }

		FrameBufferDesc(
            std::vector<AttachmentDesc>&& attachments,
            std::vector<SubpassDesc>&& subpasses,
            const FrameBufferProperties& props = {});
		FrameBufferDesc();
		~FrameBufferDesc();

		static FrameBufferDesc s_empty;

	private:
        std::vector<AttachmentDesc>     _attachments;
        std::vector<SubpassDesc>        _subpasses;
        FrameBufferProperties           _props;
        uint64_t                        _hash, _hashExcludingDimensions;
	};

    union ClearValue
    {
        float       _float[4];
        int         _int[4];
        unsigned    _uint[4];
        struct DepthStencilValue
        {
            float _depth;
            unsigned _stencil;
        };
        DepthStencilValue _depthStencil;
    };

	class INamedAttachments
	{
	public:
		virtual std::shared_ptr<IResourceView> GetResourceView(
            AttachmentName resName,
            BindFlag::Enum bindFlag, TextureViewDesc viewDesc,
            const AttachmentDesc& requestDesc, const FrameBufferProperties& props) = 0;
		virtual ~INamedAttachments();

        // note -- considering removing this. The Metal layer only needs it for MSAA configuration data
        // virtual const FrameBufferProperties& GetFrameBufferProperties() const = 0;
	};

	FrameBufferDesc SeparateSingleSubpass(const FrameBufferDesc& input, unsigned subpassIdx);

////////////////////////////////////////////////////////////////////////////////////////////////////////////

    inline ClearValue MakeClearValue(const VectorPattern<float, 4>& v)
    {
        ClearValue result;
        for (unsigned c=0; c<4; ++c) result._float[c] = v[c];
        return result;
    }

    inline ClearValue MakeClearValue(const VectorPattern<int, 4>& v)
    {
        ClearValue result;
        for (unsigned c=0; c<4; ++c) result._int[c] = v[c];
        return result;
    }

    inline ClearValue MakeClearValue(const VectorPattern<unsigned, 4>& v)
    {
        ClearValue result;
        for (unsigned c=0; c<4; ++c) result._uint[c] = v[c];
        return result;
    }

    inline ClearValue MakeClearValue(float r, float g, float b, float a = 1.f)
    {
        ClearValue result;
        result._float[0] = r;
        result._float[1] = g;
        result._float[2] = b;
        result._float[3] = a;
        return result;
    }

    inline ClearValue MakeClearValue(int r, int g, int b, int a)
    {
        ClearValue result;
        result._int[0] = r;
        result._int[1] = g;
        result._int[2] = b;
        result._int[3] = a;
        return result;
    }

    inline ClearValue MakeClearValue(unsigned r, unsigned g, unsigned b, unsigned a)
    {
        ClearValue result;
        result._uint[0] = r;
        result._uint[1] = g;
        result._uint[2] = b;
        result._uint[3] = a;
        return result;
    }

    inline ClearValue MakeClearValue(float depth, unsigned stencil)
    {
        ClearValue result;
        result._depthStencil._depth = depth;
        result._depthStencil._stencil = stencil;
        return result;
    }

    /** <summary>Add a "output" attachment to the given subpass</summary>
        This appends a given output attachment to the next available slot in the subpass.
        "Output attachment" is another name for a render target. Ie, this is the texture we're going
        to render onto.

        We can select the load/store operations to use when we do this. This determines whether we care
        about any previous contents in the buffer before this subpass, and whether we want to use the
        contents in future subpasses.
    */
	inline SubpassDesc& SubpassDesc::AppendOutput(AttachmentName attachment, const TextureViewDesc& viewDesc)
    {
        assert((BufferSpaceUsed()+1) <= s_maxAttachmentCount);
		for (unsigned c=_outputAttachmentCount+_inputAttachmentCount+_resolveOutputAttachmentCount; c>_outputAttachmentCount; --c)
			_attachmentViewBuffer[c] = _attachmentViewBuffer[c-1];

        _attachmentViewBuffer[_outputAttachmentCount] = {attachment, viewDesc};
		++_outputAttachmentCount;
        return *this;
    }

    /** <summary>Add a "input" attachment to the given subpass</summary>
        This appends an input attachment to the given subpass. An input attachment is another word
        for a shader resource (or texture). They are attachments that have been written to by a previous
        attachment, and that we're going to bind as a shader resource to read from in this subpass.

        Note that the system doesn't automatically bind the attachment as a shader resource -- we still
        have to do that manually. This is because we may need to specify some parameters when creating
        the ShaderResourceView (which determines how the attachment is presented to the shader).
        Typically this involves RenderCore::Techniques::RenderPassInstance::GetInputAttachmentSRV.
    */
    inline SubpassDesc& SubpassDesc::AppendInput(AttachmentName attachment, const TextureViewDesc& viewDesc)
    {
		assert((BufferSpaceUsed()+1) <= s_maxAttachmentCount);
		for (unsigned c=_outputAttachmentCount+_inputAttachmentCount+_resolveOutputAttachmentCount; c>_outputAttachmentCount+_inputAttachmentCount; --c)
			_attachmentViewBuffer[c] = _attachmentViewBuffer[c-1];

        _attachmentViewBuffer[_outputAttachmentCount+_inputAttachmentCount] = {attachment, viewDesc};
		++_inputAttachmentCount;
        return *this;
    }

	inline SubpassDesc& SubpassDesc::AppendResolveOutput(AttachmentName attachment, const TextureViewDesc& viewDesc)
	{
		assert((BufferSpaceUsed()+1) <= s_maxAttachmentCount);
        _attachmentViewBuffer[_outputAttachmentCount+_inputAttachmentCount+_resolveOutputAttachmentCount] = {attachment, viewDesc};
		++_resolveOutputAttachmentCount;
        return *this;
	}

    /** <summary>Set the depth/stencil attachment for the given subpass</summary>
        This sets the depth/stencil attachment. There can be only one attachment of this type,
        so it will overwrite anything that was previously set.
    */
    inline SubpassDesc& SubpassDesc::SetDepthStencil(AttachmentName attachment, const TextureViewDesc& viewDesc)
    {
        _depthStencil = {attachment, viewDesc};
        return *this;
    }

	inline SubpassDesc& SubpassDesc::SetResolveDepthStencil(AttachmentName attachment, const TextureViewDesc& viewDesc)
	{
		_resolveDepthStencil = {attachment, viewDesc};
        return *this;
	}

    inline SubpassDesc& SubpassDesc::SetViewInstanceMask(uint32_t mask)
    {
        _viewInstancingMask = mask;
        return *this;
    }

    inline uint32_t SubpassDesc::GetViewInstanceMask() const
    {
        return _viewInstancingMask;
    }

	inline IteratorRange<const AttachmentViewDesc*> SubpassDesc::GetOutputs() const
	{
		return MakeIteratorRange(_attachmentViewBuffer, &_attachmentViewBuffer[_outputAttachmentCount]);
	}

	inline const AttachmentViewDesc& SubpassDesc::GetDepthStencil() const
	{
		return _depthStencil;
	}

    inline IteratorRange<const AttachmentViewDesc*> SubpassDesc::GetViews() const
    {
        return MakeIteratorRange(&_attachmentViewBuffer[_outputAttachmentCount], &_attachmentViewBuffer[_outputAttachmentCount]);
    }

	inline IteratorRange<const AttachmentViewDesc*> SubpassDesc::GetInputs() const
	{
		return MakeIteratorRange(&_attachmentViewBuffer[_outputAttachmentCount], &_attachmentViewBuffer[_outputAttachmentCount+_inputAttachmentCount]);
	}

	inline IteratorRange<const AttachmentViewDesc*> SubpassDesc::GetResolveOutputs() const
	{
		return MakeIteratorRange(&_attachmentViewBuffer[_outputAttachmentCount+_inputAttachmentCount], &_attachmentViewBuffer[_outputAttachmentCount+_inputAttachmentCount+_resolveOutputAttachmentCount]);
	}

	inline const AttachmentViewDesc& SubpassDesc::GetResolveDepthStencil() const
	{
		return _resolveDepthStencil;
	}

    inline IteratorRange<AttachmentViewDesc*> SubpassDesc::GetOutputs()
	{
		return MakeIteratorRange(_attachmentViewBuffer, &_attachmentViewBuffer[_outputAttachmentCount]);
	}

	inline AttachmentViewDesc& SubpassDesc::GetDepthStencil()
	{
		return _depthStencil;
	}

    inline IteratorRange<AttachmentViewDesc*> SubpassDesc::GetViews()
    {
        return MakeIteratorRange(&_attachmentViewBuffer[_outputAttachmentCount], &_attachmentViewBuffer[_outputAttachmentCount]);
    }

	inline IteratorRange<AttachmentViewDesc*> SubpassDesc::GetInputs()
	{
		return MakeIteratorRange(&_attachmentViewBuffer[_outputAttachmentCount], &_attachmentViewBuffer[_outputAttachmentCount+_inputAttachmentCount]);
	}

	inline IteratorRange<AttachmentViewDesc*> SubpassDesc::GetResolveOutputs()
	{
		return MakeIteratorRange(&_attachmentViewBuffer[_outputAttachmentCount+_inputAttachmentCount], &_attachmentViewBuffer[_outputAttachmentCount+_inputAttachmentCount+_resolveOutputAttachmentCount]);
	}

	inline AttachmentViewDesc& SubpassDesc::GetResolveDepthStencil()
	{
		return _resolveDepthStencil;
	}

	inline bool operator==(const FrameBufferProperties& lhs, const FrameBufferProperties& rhs)
	{
		return lhs._width == rhs._height
			&& lhs._height == rhs._height
			&& lhs._samples._sampleCount == rhs._samples._sampleCount
			&& lhs._samples._samplingQuality == rhs._samples._samplingQuality
			;
	}

	inline bool operator!=(const FrameBufferProperties& lhs, const FrameBufferProperties& rhs) { return !(lhs == rhs); }

}
