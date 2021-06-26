// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Resource.h"
#include "VulkanCore.h"
#include "../../IDevice.h"
#include "../../Types.h"
#include "../../ResourceDesc.h"
#include <memory>

namespace RenderCore { namespace Metal_Vulkan
{
    class ObjectFactory;
	class Resource;

	/// <summary>Shared base class for various view objects</summary>
	/// In Vulkan, views of shader resources can be either VkImageViews or VkBufferViews.
	/// Both types represent an array of texels with a given format. VkBufferViews represent
	/// single dimensional linear array. VkImageViews arrange the texels in much more complex
	/// fashion (eg, 2D and 3D textures, with mipchains, etc).
	///
	/// Only VkImageViews can be used with render passes and frame buffers. But both image views
	/// and buffer views can be used as shader resources.
    class ResourceView : public IResourceView
    {
    public:
        ResourceView(ObjectFactory& factory, VkImage image, const TextureViewDesc& window = TextureViewDesc());
        ResourceView(ObjectFactory& factory, const VkBuffer buffer, Format texelBufferFormat, unsigned rangeOffset = 0, unsigned rangeSize = 0);

        ResourceView(ObjectFactory& factory, const std::shared_ptr<IResource>& image, BindFlag::Enum usage, const TextureViewDesc& window);
        ResourceView(ObjectFactory& factory, const std::shared_ptr<IResource>& buffer, Format texelBufferFormat, unsigned rangeOffset, unsigned rangeSize);
        ResourceView(ObjectFactory& factory, const std::shared_ptr<IResource>& buffer, unsigned rangeOffset, unsigned rangeSize);
        ResourceView(ObjectFactory& factory, const std::shared_ptr<IResource>& resource);
        ResourceView();
        ~ResourceView();

		std::shared_ptr<IResource>	        GetResource() const override { return _resource; }
        VkImageView							GetImageView() const { return _imageView.get(); }
        VkBufferView						GetBufferView() const { return _bufferView.get(); }
        std::pair<unsigned, unsigned>       GetBufferRangeOffsetAndSize() const { return _bufferRange; }
        const VkImageSubresourceRange&      GetImageSubresourceRange() const { return (VkImageSubresourceRange&)_imageSubresourceRange; }

        Resource* GetVulkanResource() const { return _resource.get(); }

        enum class Type { ImageView, BufferView, BufferAndRange };
        Type GetType() const { return _type; }

    private:
        VulkanSharedPtr<VkImageView>	_imageView;
        VulkanSharedPtr<VkBufferView>	_bufferView;        // used for VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
        Type                            _type;
        std::pair<unsigned, unsigned>   _bufferRange;       // used for basic VkBuffer bindings (eg, with VkDescriptorBufferInfo)
        uint8_t                         _imageSubresourceRange[5*4];
		std::shared_ptr<Resource>		_resource;
    };
}}

