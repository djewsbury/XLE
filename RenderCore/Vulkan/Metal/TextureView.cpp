// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextureView.h"
#include "ObjectFactory.h"
#include "State.h"
#include "Format.h"
#include "../../Format.h"
#include <stdexcept>

namespace RenderCore { namespace Metal_Vulkan
{
    static VkImageViewType AsImageViewType(TextureDesc::Dimensionality dims, bool isArray)
    {
        switch (dims) {
        case TextureDesc::Dimensionality::T1D:      return isArray?VK_IMAGE_VIEW_TYPE_1D_ARRAY:VK_IMAGE_VIEW_TYPE_1D;
        case TextureDesc::Dimensionality::T2D:      return isArray?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D;
        case TextureDesc::Dimensionality::T3D:      return VK_IMAGE_VIEW_TYPE_3D;
        case TextureDesc::Dimensionality::CubeMap:  return isArray?VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:VK_IMAGE_VIEW_TYPE_CUBE;
        default:                                    return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
        }
    }

    static Format ResolveVkFormat(Format baseFormat, TextureViewDesc::FormatFilter filter, BindFlag::Enum usage)
    {
        if (filter._explicitFormat != Format(0))
            return filter._explicitFormat;
        
        // Format conversion is different between Vulkan and DX11
        // Depth/stencil formats just stay as is. We just need to switch
        // between linear and SRGB formats when required --
        switch (filter._aspect) {
        case TextureViewDesc::Depth:
        case TextureViewDesc::DepthStencil:
        case TextureViewDesc::Stencil:
            return AsDepthStencilFormat(baseFormat);

        case TextureViewDesc::ColorLinear:
            return AsLinearFormat(baseFormat);

        case TextureViewDesc::ColorSRGB:
            return AsSRGBFormat(baseFormat);

        default:
            return baseFormat;
        }
    }

    static VkImageViewCreateInfo MakeImageViewCreateInfo(TextureViewDesc window, VkImage image, bool isArray)
    {
        // Note that the arrayCount value is sometimes set to 1 when we want 
        // an array texture with a single array slice (as opposed to 0, meaning no array at all).
        // Current single array slice views become non-array views... But we could make "1" mean
        // an array view.
        isArray &= window._arrayLayerRange._count > 1;

        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.pNext = nullptr;
        view_info.image = image;
        view_info.viewType = AsImageViewType(window._dimensionality, isArray);
        view_info.format = (VkFormat)AsVkFormat(window._format._explicitFormat);
        view_info.components.r = VK_COMPONENT_SWIZZLE_R;
        view_info.components.g = VK_COMPONENT_SWIZZLE_G;
        view_info.components.b = VK_COMPONENT_SWIZZLE_B;
        view_info.components.a = VK_COMPONENT_SWIZZLE_A;
        view_info.subresourceRange.baseMipLevel = window._mipRange._min;
        view_info.subresourceRange.levelCount = std::max(1u, (unsigned)window._mipRange._count);
        view_info.subresourceRange.baseArrayLayer = window._arrayLayerRange._min;
        view_info.subresourceRange.layerCount = std::max(1u, (unsigned)window._arrayLayerRange._count);

        switch (window._format._aspect) {
        case TextureViewDesc::Depth:
            view_info.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
            break;
        case TextureViewDesc::DepthStencil:
            view_info.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            break;
        case TextureViewDesc::Stencil:
            view_info.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            break;
        case TextureViewDesc::ColorLinear:
        case TextureViewDesc::ColorSRGB:
            view_info.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_COLOR_BIT;
            break;

        default:
            view_info.subresourceRange.aspectMask = AsImageAspectMask(window._format._explicitFormat);
            break;
        }

        // disable depth or stencil when requiring just a single subaspect
        if (window._flags & TextureViewDesc::Flags::JustDepth) view_info.subresourceRange.aspectMask &= ~VK_IMAGE_ASPECT_STENCIL_BIT;
        if (window._flags & TextureViewDesc::Flags::JustStencil) view_info.subresourceRange.aspectMask &= ~VK_IMAGE_ASPECT_DEPTH_BIT;

        return view_info;
    }

    static VkBufferViewCreateInfo MakeBufferViewCreateInfo(Format fmt, uint64_t rangeOffset, uint64_t rangeSize, VkBuffer buffer)
    {
        VkBufferViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        view_info.pNext = nullptr;
        view_info.flags = 0;
        view_info.buffer = buffer;
        view_info.format = (VkFormat)AsVkFormat(fmt);
        if (rangeOffset == 0 && rangeSize == 0) {
            view_info.offset = 0;
            view_info.range = VK_WHOLE_SIZE;
        } else {
            view_info.offset = rangeOffset;
            view_info.range = rangeSize;
        }
        return view_info;
    }

    ResourceView::ResourceView(ObjectFactory& factory, VkImage image, const TextureViewDesc& window)
    : _type(Type::ImageView)
    {
        // We don't know anything about the "image" in this case. We need to rely on "image" containing all
        // of the relevant information.
        auto createInfo = MakeImageViewCreateInfo(window, image, true);
        _imageView = factory.CreateImageView(createInfo);
    }

	ResourceView::ResourceView(
        ObjectFactory& factory, const std::shared_ptr<IResource>& image, 
        BindFlag::Enum formatUsage, const TextureViewDesc& window)
	: _type(Type::ImageView)
    {
        assert(image);
		auto res = (Resource*)image->QueryInterface(typeid(Resource).hash_code());
		if (!res)
			Throw(::Exceptions::BasicLabel("Incorrect resource type passed to Vulkan ResourceView"));

		if (res->GetDesc()._type != ResourceDesc::Type::Texture)
            Throw(::Exceptions::BasicLabel("Attempting to create a texture view for a resource that is not a texture. Did you intend to use CreateBufferView?"));

        const auto& tDesc = res->GetDesc()._textureDesc;
        if (res->GetImage()) {
            auto adjWindow = window;

            // Some parts of the "TextureViewDesc" can be set to "undefined". In these cases,
            // we should fill them in with the detail from the resource.
            adjWindow._format._explicitFormat = ResolveVkFormat(tDesc._format, adjWindow._format, formatUsage);
            if (adjWindow._dimensionality == TextureDesc::Dimensionality::Undefined)
                adjWindow._dimensionality = tDesc._dimensionality;
            if (adjWindow._mipRange._count == TextureViewDesc::Unlimited)
                adjWindow._mipRange._count = tDesc._mipCount - adjWindow._mipRange._min;
            if (adjWindow._arrayLayerRange._count == TextureViewDesc::Unlimited)
                adjWindow._arrayLayerRange._count = tDesc._arrayCount - adjWindow._arrayLayerRange._min;

            auto createInfo = MakeImageViewCreateInfo(adjWindow, res->GetImage(), true);
            _imageView = factory.CreateImageView(createInfo);
        } else {
            assert(res->GetBuffer());
            auto finalFmt = ResolveVkFormat(tDesc._format, window._format, formatUsage);
            auto createInfo = MakeBufferViewCreateInfo(finalFmt, 0, VK_WHOLE_SIZE, res->GetBuffer());
            _bufferView = factory.CreateBufferView(createInfo);
        }

        _resource = std::static_pointer_cast<Resource>(image);
	}

    ResourceView::ResourceView(ObjectFactory& factory, const VkBuffer buffer, Format texelBufferFormat, unsigned rangeOffset, unsigned rangeSize)
    : _type(Type::BufferView)
    {
        auto createInfo = MakeBufferViewCreateInfo(texelBufferFormat, rangeOffset, rangeSize, buffer);
        _bufferView = factory.CreateBufferView(createInfo);
    }

    ResourceView::ResourceView(ObjectFactory& factory, const IResourcePtr& buffer, Format texelBufferFormat, unsigned rangeOffset, unsigned rangeSize)
    : _type(Type::BufferView)
    {
        // This variation is for "texel buffers"
        //      -- ie, these are used with VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT or VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        //          it's a little less common a feature
		auto res = (Resource*)buffer->QueryInterface(typeid(Resource).hash_code());
		if (!res)
			Throw(::Exceptions::BasicLabel("Incorrect resource type passed to Vulkan ResourceView"));

        if (!res->GetBuffer())
            Throw(::Exceptions::BasicLabel("Attempting to create a texel buffer view for a resource that is not a buffer"));

        // note that if this resource has a TextureDesc, we're ignoring the format information inside of it
        auto createInfo = MakeBufferViewCreateInfo(texelBufferFormat, rangeOffset, rangeSize, res->GetBuffer());
        _bufferView = factory.CreateBufferView(createInfo);

        _resource = std::static_pointer_cast<Resource>(buffer);
    }

    ResourceView::ResourceView(ObjectFactory& factory, const IResourcePtr& buffer, unsigned rangeOffset, unsigned rangeSize)
    : _bufferRange(rangeOffset, rangeSize)
    , _type(Type::BufferAndRange)
    {
        auto res = (Resource*)buffer->QueryInterface(typeid(Resource).hash_code());
		if (!res)
			Throw(::Exceptions::BasicLabel("Incorrect resource type passed to Vulkan ResourceView"));

        _resource = std::static_pointer_cast<Resource>(buffer);
    }

    ResourceView::ResourceView(ObjectFactory& factory, const IResourcePtr& resource)
    {
        const auto& desc = resource->GetDesc();
        if (desc._type == ResourceDesc::Type::Texture) {
            BindFlag::Enum usage = BindFlag::ShaderResource;
            if (desc._bindFlags & BindFlag::ShaderResource) {
                usage = BindFlag::ShaderResource;
            } else if (desc._bindFlags & BindFlag::UnorderedAccess) {
                usage = BindFlag::UnorderedAccess;
            } else if (desc._bindFlags & BindFlag::RenderTarget) {
                usage = BindFlag::RenderTarget;
            } else if (desc._bindFlags & BindFlag::DepthStencil) {
                usage = BindFlag::DepthStencil;
            } else
                Throw(std::runtime_error("No relevant bind flags found for default resource view"));
            *this = ResourceView{factory, resource, usage, TextureViewDesc{}};
        } else {
            *this = ResourceView{factory, resource, 0, 0};
        }
    }

    ResourceView::ResourceView() {}
    ResourceView::~ResourceView() {}

}}

