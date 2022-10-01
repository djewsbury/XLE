// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Resource.h"
#include "ObjectFactory.h"
#include "Format.h"
#include "DeviceContext.h"
#include "TextureView.h"
#include "ExtensionFunctions.h"
#include "../IDeviceVulkan.h"
#include "../../ResourceUtils.h"
#include "../../Format.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/BitUtils.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/StringFormat.h"
#include "../../../Utility/Threading/Mutex.h"

// #define TRACK_RESOURCE_GUIDS

#pragma clang diagnostic ignored "-Wswitch"		// warning: case value not in enumerated type 'BindFlag::Enum'

namespace RenderCore { namespace Metal_Vulkan
{
	static uint64_t s_nextResourceGUID = 1;

    static VkDevice ExtractUnderlyingDevice(IDevice& idev)
    {
        auto* vulkanDevice = (IDeviceVulkan*)idev.QueryInterface(typeid(IDeviceVulkan).hash_code());
		return vulkanDevice ? vulkanDevice->GetUnderlyingDevice() : nullptr;
    }

	static unsigned CopyViaMemoryMap(
		VkDevice device, ResourceMap& map,
		VkImage imageForLayout, const TextureDesc& descForLayout,
		const std::function<SubResourceInitData(SubResourceId)>& initData);

	static void CopyPartial(
        DeviceContext& context, 
        const CopyPartial_Dest& dst, const CopyPartial_Src& src,
        VkImageLayout dstLayout, VkImageLayout srcLayout);

	static void Copy(DeviceContext& context, Resource& dst, Resource& src, VkImageLayout dstLayout, VkImageLayout srcLayout);
	static bool ResourceMapViable(Resource& res, ResourceMap::Mode mode);

	static VkBufferUsageFlags AsBufferUsageFlags(BindFlag::BitField bindFlags)
	{
		VkBufferUsageFlags result = 0;
		if (bindFlags & BindFlag::VertexBuffer) result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		if (bindFlags & BindFlag::IndexBuffer) result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		if (bindFlags & BindFlag::DrawIndirectArgs) result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        if (bindFlags & BindFlag::TransferSrc) result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (bindFlags & BindFlag::TransferDst) result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		if (bindFlags & BindFlag::TexelBuffer) {
			if (bindFlags & BindFlag::UnorderedAccess) result |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
			if (bindFlags & (BindFlag::ShaderResource|BindFlag::ConstantBuffer)) result |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
			assert((bindFlags&(BindFlag::UnorderedAccess|BindFlag::ShaderResource|BindFlag::ConstantBuffer)) != 0);		// must combine TexelBuffer with one of the usage flags
		} else {
        	if (bindFlags & BindFlag::UnorderedAccess) result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			if (bindFlags & BindFlag::ConstantBuffer) result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		}

		// from VK_EXT_transform_feedback
		if (bindFlags & BindFlag::StreamOutput) result |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;

		// Other Vulkan flags:
		// VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
		// VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
		return result;
	}
	
	static VkImageUsageFlags AsImageUsageFlags(BindFlag::BitField bindFlags)
	{
		// note -- we're assuming shader resources are sampled here (rather than storage type textures)
		// Also, assuming that the ShaderResource flag means it can be used as an input attachment
		//			-- could we disable the SAMPLED bit for input attachments were we don't use any filtering/sampling?
		VkImageUsageFlags result = 0;
		if (bindFlags & BindFlag::ShaderResource) result |= VK_IMAGE_USAGE_SAMPLED_BIT;
		if (bindFlags & BindFlag::RenderTarget) result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if (bindFlags & BindFlag::DepthStencil) result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		if (bindFlags & BindFlag::UnorderedAccess) result |= VK_IMAGE_USAGE_STORAGE_BIT;
        if (bindFlags & BindFlag::TransferSrc) result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (bindFlags & BindFlag::TransferDst) result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		if (bindFlags & BindFlag::InputAttachment) result |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

		// Other Vulkan flags:
		// VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT
		return result;
	}

	VkImageAspectFlags AsImageAspectMask(Format fmt)
	{
        // if (view_info.format == VK_FORMAT_D16_UNORM_S8_UINT ||
        //     view_info.format == VK_FORMAT_D24_UNORM_S8_UINT ||
        //     view_info.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        //     view_info.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        // }

		if (fmt == Format::Unknown) return 0;

        auto components = GetComponents(fmt);
        switch (components) {
        case FormatComponents::Depth:           return VK_IMAGE_ASPECT_DEPTH_BIT;
        case FormatComponents::DepthStencil:    return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        case FormatComponents::Stencil:         return VK_IMAGE_ASPECT_STENCIL_BIT;
        default:                                return VK_IMAGE_ASPECT_COLOR_BIT;
        }
	}

	namespace Internal
	{
		VkImageLayout_ AsVkImageLayout(Internal::ImageLayout input) { return (VkImageLayout)input; }
	}

	VkSampleCountFlagBits_ AsSampleCountFlagBits(TextureSamples samples)
	{
        // we just want to isolate the most significant bit. If it's already a power
        // of two, then we can just return as is.
        assert(IsPowerOfTwo(samples._sampleCount));
        assert(samples._sampleCount > 0);
        return (VkSampleCountFlagBits_)samples._sampleCount;
	}

	static VkImageType AsImageType(TextureDesc::Dimensionality dims)
	{
		switch (dims) {
		case TextureDesc::Dimensionality::T1D: return VK_IMAGE_TYPE_1D;
		default:
		case TextureDesc::Dimensionality::T2D: return VK_IMAGE_TYPE_2D;
		case TextureDesc::Dimensionality::T3D: return VK_IMAGE_TYPE_3D;
		case TextureDesc::Dimensionality::CubeMap: return VK_IMAGE_TYPE_2D;
		}
	}

	namespace Internal
	{
		static VkImageLayout GetLayoutForBindType(BindFlag::Enum bindType);
		static VkImageLayout SelectDefaultSteadyStateLayout(BindFlag::BitField allBindFlags);
		static BarrierResourceUsage DefaultBarrierResourceUsageFromLayout(VkImageLayout prevLayout);
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////

	#if defined(TRACK_RESOURCE_GUIDS)
		static std::vector<std::pair<unsigned, std::string>> s_resourceGUIDToName;
		static void AssociateResourceGUID(unsigned guid, std::string name)
		{
			static Threading::Mutex lock;
			ScopedLock(lock);
			auto existing = std::find_if(s_resourceGUIDToName.begin(), s_resourceGUIDToName.end(), [name](const auto& p) { return p.second == name; });
			if (existing != s_resourceGUIDToName.end()) {
				int c=0;
				(void)c;
			}
			s_resourceGUIDToName.push_back(std::make_pair(guid, name));
		}
	#endif

	static void AssignObjectName(ObjectFactory& factory, VkImage underlyingImage, VkBuffer underlyingBuffer, const char* name)
	{
		#if defined(_DEBUG)
			if (factory.GetExtensionFunctions()._setObjectName && name && name[0]) {
				const VkDebugUtilsObjectNameInfoEXT imageNameInfo {
					VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, // sType
					NULL,                                               // pNext
					(underlyingImage != nullptr) ? VK_OBJECT_TYPE_IMAGE : VK_OBJECT_TYPE_BUFFER,					// objectType
					(underlyingImage != nullptr) ? (uint64_t)underlyingImage : (uint64_t)underlyingBuffer,			// object
					name
				};
				factory.GetExtensionFunctions()._setObjectName(factory.GetDevice().get(), &imageNameInfo);
			}
		#endif
	}

    static std::pair<VulkanUniquePtr<VkDeviceMemory>, unsigned> AllocateDeviceMemory(
        const Metal_Vulkan::ObjectFactory& factory, VkMemoryRequirements memReqs, VkMemoryPropertyFlags requirementMask)
    {
        auto type = factory.FindMemoryType(memReqs.memoryTypeBits, requirementMask);
        if (type >= 32)
            Throw(Exceptions::BasicLabel("Could not find compatible memory type for image"));
        return {factory.AllocateMemoryDirectFromVulkan(memReqs.size, type), type};
    }

	static void WriteInitDataViaMap(ObjectFactory& factory, const ResourceDesc& desc, Resource& resource, const std::function<SubResourceInitData(SubResourceId)>& initData)
	{
		// After allocation, we must initialise the data. True linear buffers don't have subresources,
		// so it's reasonably easy. But if this buffer is really a staging texture, then we need to 
		// copy in all of the sub resources.
		if (desc._type == ResourceDesc::Type::LinearBuffer) {
			auto subResData = initData({0, 0});
			if (subResData._data.size()) {
				ResourceMap map{factory, resource, ResourceMap::Mode::WriteDiscardPrevious};
				std::memcpy(map.GetData().begin(), subResData._data.begin(), std::min(subResData._data.size(), (size_t)desc._linearBufferDesc._sizeInBytes));
			}
		} else {
			// This is the staging texture path. Rather that getting the arrangement of subresources from
			// the VkImage, we specify it ourselves.
			ResourceMap map{factory, resource, ResourceMap::Mode::WriteDiscardPrevious};
			CopyViaMemoryMap(
				factory.GetDevice().get(), map, 
				resource.GetImage(), desc._textureDesc, initData);
		}
	}

	static VkMemoryPropertyFlags AsMemoryPropertyFlags(AllocationRules::BitField rules)
	{
		VkMemoryPropertyFlags result = 0;
		if (rules & (AllocationRules::HostVisibleSequentialWrite|AllocationRules::HostVisibleRandomAccess)) {
			result |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			if (!(rules & AllocationRules::DisableAutoCacheCoherency))
				result |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		}
		return result;
	}

	static std::pair<VulkanUniquePtr<VkDeviceMemory>, unsigned> AttachDedicatedMemory(ObjectFactory& factory, const ResourceDesc& desc, const VkMemoryRequirements& mem_reqs, VkBuffer underlyingBuffer)
	{
		auto memoryRequirements = AsMemoryPropertyFlags(desc._allocationRules);
		auto result = AllocateDeviceMemory(factory, mem_reqs, memoryRequirements);
		auto res = vkBindBufferMemory(factory.GetDevice().get(), underlyingBuffer, result.first.get(), 0);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failed while binding a buffer to device memory"));
		return result;
	}

	static std::pair<VulkanUniquePtr<VkDeviceMemory>, unsigned> AttachDedicatedMemory(ObjectFactory& factory, const ResourceDesc& desc, const VkMemoryRequirements& mem_reqs, VkImage underlyingImage)
	{
		auto memoryRequirements = AsMemoryPropertyFlags(desc._allocationRules);
		auto result = AllocateDeviceMemory(factory, mem_reqs, memoryRequirements);
		auto res = vkBindImageMemory(factory.GetDevice().get(), underlyingImage, result.first.get(), 0);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failed while binding an image to device memory"));
		return result;
	}

	Resource::Resource(
		ObjectFactory& factory, const Desc& desc,
		const std::function<SubResourceInitData(SubResourceId)>& initData)
	: _desc(desc)
	, _guid(s_nextResourceGUID++)
	{
		// Our resource can either be a linear buffer, or an image
		// These correspond to the 2 types of Desc
		// We need to create the buffer/image first, so we can called vkGetXXXMemoryRequirements
		const bool hasInitData = !!initData;

		_steadyStateImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		_pendingInit = false;

		const bool allocateDirectFromVulkan = false;
		VmaAllocationInfo allocationInfo;
		VkMemoryRequirements mem_reqs = {}; 
		if (desc._type == Desc::Type::LinearBuffer) {
			assert(desc._linearBufferDesc._sizeInBytes);	// zero sized buffer is can cause Vulkan to crash (and is silly, anyway)
			VkBufferCreateInfo buf_info = {};
			buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			buf_info.pNext = nullptr;
			buf_info.usage = AsBufferUsageFlags(desc._bindFlags);
			buf_info.size = desc._linearBufferDesc._sizeInBytes;
			buf_info.queueFamilyIndexCount = 0;
			buf_info.pQueueFamilyIndices = nullptr;
			buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;   // sharing between queues
			buf_info.flags = 0;     // flags for sparse buffers

			/*
			if (buf_info.usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
				buf_info.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			*/

			assert(buf_info.usage != 0);
			if (!allocateDirectFromVulkan) {
				_underlyingBuffer = factory.CreateBufferWithAutoMemory(_vmaMem, allocationInfo, buf_info, desc._allocationRules);
			} else {
				_underlyingBuffer = factory.CreateBuffer(buf_info);
				vkGetBufferMemoryRequirements(factory.GetDevice().get(), _underlyingBuffer.get(), &mem_reqs);
			}

		} else {
			if (desc._type != Desc::Type::Texture)
				Throw(::Exceptions::BasicLabel("Invalid desc passed to buffer constructor"));

			const auto& tDesc = desc._textureDesc;

			VkImageCreateInfo image_create_info = {};
			image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			image_create_info.pNext = nullptr;
			image_create_info.imageType = AsImageType(tDesc._dimensionality);
			image_create_info.format = (VkFormat)AsVkFormat(tDesc._format);
			image_create_info.extent.width = tDesc._width;
			image_create_info.extent.height = tDesc._height;
			image_create_info.extent.depth = tDesc._depth;
			image_create_info.mipLevels = tDesc._mipCount;
			image_create_info.arrayLayers = ActualArrayLayerCount(tDesc);
			image_create_info.samples = (VkSampleCountFlagBits)AsSampleCountFlagBits(tDesc._samples);
			image_create_info.queueFamilyIndexCount = 0;
			image_create_info.pQueueFamilyIndices = nullptr;
			image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			image_create_info.flags = 0;

			assert(image_create_info.format != VK_FORMAT_UNDEFINED);
			if (tDesc._dimensionality == TextureDesc::Dimensionality::CubeMap) {
				image_create_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
				assert((image_create_info.arrayLayers%6u) == 0);		// "arrayLayers" should be the number of cubemap faces -- ie, 6 for each cubemap in the array
			}

            // We don't need to use mutable formats in many cases in Vulkan. 
            // D32_ (etc) formats don't need to be cast to R32_ (etc). We should
            // only really need to do this when moving between SRGB and Linear formats
            // (though we can also to bitwise casts between unsigned and signed and float
            // and int formats like this)
            if (GetComponentType(tDesc._format) == FormatComponentType::Typeless)
                image_create_info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

			// The tiling, initialLayout and usage flags depend on the bind flags and cpu/gpu usage
			// set in the input desc (and also if we have initData provided)
			// Tiling can only be OPTIMAL or LINEAR, 
			// and initialLayout can only be UNDEFINED or PREINITIALIZED at this stage
			bool requireHostVisibility = !!(desc._allocationRules & (AllocationRules::HostVisibleRandomAccess|AllocationRules::HostVisibleSequentialWrite));
			image_create_info.tiling = requireHostVisibility ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
			image_create_info.initialLayout = hasInitData ? VK_IMAGE_LAYOUT_PREINITIALIZED : VK_IMAGE_LAYOUT_UNDEFINED;
			image_create_info.usage = AsImageUsageFlags(desc._bindFlags);

			// minor validations
            if (image_create_info.tiling == VK_IMAGE_TILING_OPTIMAL && (image_create_info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
                // For depth/stencil textures, if the device doesn't support optimal tiling, they switch back to linear
                // Maybe this is unnecessary, because the device could just define "optimal" to mean linear in this case.
                // But the vulkan samples do something similar (though they prefer to use linear mode when it's available...?)
				auto depthFormat = AsVkFormat(AsDepthStencilFormat(tDesc._format));
                const auto formatProps = factory.GetFormatProperties(depthFormat);
                if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
                    image_create_info.tiling = VK_IMAGE_TILING_LINEAR;
                    if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
                        Throw(Exceptions::BasicLabel("Format (%i) can't be used for a depth stencil", unsigned(image_create_info.format)));
                }
            }

			if (image_create_info.tiling == VK_IMAGE_TILING_LINEAR && (image_create_info.usage & VK_IMAGE_USAGE_SAMPLED_BIT)) {
				const auto formatProps = factory.GetFormatProperties(image_create_info.format);
				const bool canSampleLinearTexture =
					!!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
				if (!canSampleLinearTexture)
					Throw(::Exceptions::BasicLabel("Hardware does not support sampling from a linear texture. A staging texture is required"));
			}

            // When constructing a staging (or readback) texture with multiple mip levels or array layers,
            // we must actually allocate a "buffer". We will treat this buffer as a linear texture, and we
            // will manually layout the miplevels within the device memory.
            //
            // This is because Vulkan doesn't support creating VK_IMAGE_TILING_LINEAR with more than 1
            // mip level or array layers. And linear texture must be 2D (they can't be 1d or 3d textures)
			// However, our solution more or less emulates what would happen
            // if it did...?! (Except, of course, we can never bind it as a sampled texture)
            //
            // See (for example) this post from nvidia:
            // https://devtalk.nvidia.com/default/topic/926085/texture-memory-management/

            const auto gpuAccessFlag = BindFlag::ShaderResource|BindFlag::RenderTarget|BindFlag::DepthStencil|BindFlag::UnorderedAccess|BindFlag::InputAttachment;
			if (!(desc._bindFlags & gpuAccessFlag)) {
                VkBufferCreateInfo buf_info = {};
			    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			    buf_info.pNext = nullptr;
			    buf_info.usage = AsBufferUsageFlags(desc._bindFlags);
			    buf_info.size = ByteCount(tDesc);
			    buf_info.queueFamilyIndexCount = 0;
			    buf_info.pQueueFamilyIndices = nullptr;
			    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			    buf_info.flags = 0;

                if (!allocateDirectFromVulkan) {
					_underlyingBuffer = factory.CreateBufferWithAutoMemory(_vmaMem, allocationInfo, buf_info, desc._allocationRules);
				} else {
					_underlyingBuffer = factory.CreateBuffer(buf_info);
    				vkGetBufferMemoryRequirements(factory.GetDevice().get(), _underlyingBuffer.get(), &mem_reqs);
				}
            } else {
				if (!allocateDirectFromVulkan) {
					_underlyingImage = factory.CreateImageWithAutoMemory(_vmaMem, allocationInfo, image_create_info, desc._allocationRules, _guid);
				} else {
					_underlyingImage = factory.CreateImage(image_create_info, _guid);
					vkGetImageMemoryRequirements(factory.GetDevice().get(), _underlyingImage.get(), &mem_reqs);
				}
            }

			_steadyStateImageLayout = Internal::SelectDefaultSteadyStateLayout(desc._bindFlags);

			#if defined(TRACK_RESOURCE_GUIDS)
				AssociateResourceGUID(_guid, desc._name);
			#endif
		}

		AssignObjectName(factory, _underlyingImage.get(), _underlyingBuffer.get(), desc._name);

		if (allocateDirectFromVulkan) {
			if (_underlyingBuffer) {
				auto p = AttachDedicatedMemory(factory, desc, mem_reqs, _underlyingBuffer.get());
				_mem = std::move(p.first);
				_memoryType = p.second;
			} else {
				assert(_underlyingImage);
				auto p = AttachDedicatedMemory(factory, desc, mem_reqs, _underlyingImage.get());
				_mem = std::move(p.first);
				_memoryType = p.second;
			}
		} else {
			if (desc._allocationRules & AllocationRules::PermanentlyMapped && allocationInfo.pMappedData)
				_permanentlyMappedRange = MakeIteratorRange(allocationInfo.pMappedData, PtrAdd(allocationInfo.pMappedData, allocationInfo.size));
			_memoryType = allocationInfo.memoryType;
		}

		// Setup init data / initialization
		if (hasInitData) {

			if (ResourceMapViable(*this, ResourceMap::Mode::WriteDiscardPrevious)) {
				WriteInitDataViaMap(factory, _desc, *this, initData);
			} else {
				Throw(std::runtime_error("You must explicitly use a \"host visible\" allocation rule on resources that have init data"));
			}

		} else if (desc._type == Desc::Type::Texture) {
			// queue transition into our steady-state
			_pendingInit = true;
		}
	}

	void Resource::ChangeSteadyState(BindFlag::Enum usage)
	{
		_steadyStateImageLayout = Internal::GetLayoutForBindType(usage);
	}

	std::shared_ptr<IResourceView>  Resource::CreateTextureView(BindFlag::Enum usage, const TextureViewDesc& window)
	{
		return std::make_shared<ResourceView>(GetObjectFactory(), shared_from_this(), usage, window);
	}

    std::shared_ptr<IResourceView>  Resource::CreateBufferView(BindFlag::Enum usage, unsigned rangeOffset, unsigned rangeSize)
	{
		// note that we can't create a "texel buffer" view via this interface
		return std::make_shared<ResourceView>(GetObjectFactory(), shared_from_this(), rangeOffset, rangeSize);
	}

	std::vector<uint8_t>    Resource::ReadBackSynchronized(IThreadContext& context, SubResourceId subRes) const
	{
		bool requiresDestaging = !ResourceMap::CanMap(*context.GetDevice(), *const_cast<Resource*>(this), ResourceMap::Mode::Read);
		if (requiresDestaging) {
			// todo -- we could destaging only a single sub resource...?
			auto stagingCopyDesc = _desc;
			stagingCopyDesc._allocationRules = AllocationRules::HostVisibleRandomAccess;
			stagingCopyDesc._bindFlags = BindFlag::TransferDst;
			XlCopyString(stagingCopyDesc._name, "ReadBackSynchronized");
			Resource destaging { GetObjectFactory(), stagingCopyDesc };

			auto& ctx = *DeviceContext::Get(context);
			{
				CompleteInitialization(ctx, {(IResource*)&destaging});

				// We need a barrier here to ensure all shader operations have completed before
				// we start the transfer. This is required for buffers, but is sort of implied by
				// the layout change for images, anyway
				// The barrier is a bit overly broad; but this path will result in a full stall 
				// for the GPU, anyway.
				{
					VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
					barrier.pNext = nullptr;
					barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					vkCmdPipelineBarrier(
						ctx.GetActiveCommandList().GetUnderlying().get(),
						VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
						VK_PIPELINE_STAGE_TRANSFER_BIT,
						0,
						1, &barrier,
						0, nullptr,
						0, nullptr);
				}

				Internal::CaptureForBind capture{ctx, *const_cast<Resource*>(this), BindFlag::TransferSrc};
				Copy(ctx, destaging, *const_cast<Resource*>(this), destaging._steadyStateImageLayout, capture.GetLayout());

				// "7.9. Host Write Ordering Guarantees" suggests we shouldn't need a transfer -> host barrier here
			}

			return destaging.ReadBackSynchronized(context, subRes);
		}

		// Commit all commands up to this point, and wait for completion
		// Technically, we don't need to wait for all commands. We only really need to wait for
		// any commands that might write to this resource (including the destaging copy)
		// In theory, using render passes & blt passes, etc we could do that. But that seems 
		// like an over-optimization, ReadBack is not intended for use in performance critical
		// scenarios. Clients that need to guarantee best possible readback performance would be
		// better off with a custom rolled solution that tracks the specific operations involved
		context.CommitCommands(CommitCommandsFlags::WaitForCompletion);

		DeviceContext::Get(context);		// trigger recreation of command list, due to CommitCommands() finishing the previous one

		const bool doPartialResourceMap = false;
		if (doPartialResourceMap) {
			ResourceMap map{
				GetObjectFactory(*context.GetDevice()),
				*const_cast<Resource*>(this),
				ResourceMap::Mode::Read,
				subRes};

			return std::vector<uint8_t>{
				(const uint8_t*)map.GetData(subRes).begin(),
				(const uint8_t*)map.GetData(subRes).end()};
		} else {
			ResourceMap map{
				GetObjectFactory(*context.GetDevice()),
				*const_cast<Resource*>(this),
				ResourceMap::Mode::Read};

			return std::vector<uint8_t>{
				(const uint8_t*)map.GetData(subRes).begin(),
				(const uint8_t*)map.GetData(subRes).end()};
		}
	}

	static std::function<SubResourceInitData(SubResourceId)> AsResInitializer(const SubResourceInitData& initData)
	{
		if (initData._data.size()) {
			return [&initData](SubResourceId sr) { return (sr._mip==0&&sr._arrayLayer==0) ? initData : SubResourceInitData{}; };
		 } else {
			 return {};
		 }
	}

	Resource::Resource(
		ObjectFactory& factory, const Desc& desc,
		const SubResourceInitData& initData)
	: Resource(factory, desc, AsResInitializer(initData))
	{}

    Resource::Resource(VkImage image, const Desc& desc)
    : _desc(desc)
	, _guid(s_nextResourceGUID++)
    {
        // do not destroy the image, even on the last release --
        //      this is used with the presentation chain images, which are only
        //      released by the vulkan presentation chain itself
        _underlyingImage = VulkanSharedPtr<VkImage>(image, [](const VkImage) {});

		_steadyStateImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		_pendingInit = true;
    }

	Resource::Resource() : _guid(s_nextResourceGUID++) {}
	Resource::~Resource() {}

	void* Resource::QueryInterface(size_t guid)
	{
		if (guid == typeid(Resource).hash_code())
			return this;
		return nullptr;
	}

    namespace Internal
	{
		class ResourceAllocator : public std::allocator<Metal_Vulkan::Resource>
		{
		public:
			using BaseAllocatorTraits = std::allocator_traits<std::allocator<Metal_Vulkan::Resource>>;
			typename BaseAllocatorTraits::pointer allocate(typename BaseAllocatorTraits::size_type n, typename BaseAllocatorTraits::const_void_pointer ptr)
			{
				Throw(::Exceptions::BasicLabel("Allocation attempted via ResourceAllocator"));
			}

			void deallocate(typename BaseAllocatorTraits::pointer p, typename BaseAllocatorTraits::size_type n)
			{
				delete (Metal_Vulkan::Resource*)p;
			}
		};

		std::shared_ptr<Resource> CreateResource(
			ObjectFactory& factory,
			const ResourceDesc& desc,
			const ResourceInitializer& initData)
		{
			const bool useAllocateShared = true;
			if (constant_expression<useAllocateShared>::result()) {
				auto res = std::allocate_shared<Metal_Vulkan::Resource>(
					Internal::ResourceAllocator(),
					std::ref(factory), std::ref(desc), std::ref(initData));
				return res;
			} else {
				return std::make_unique<Metal_Vulkan::Resource>(factory, desc, initData);
			}
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static std::vector<VkBufferImageCopy> GenerateBufferImageCopyOps(
		const ResourceDesc& imageDesc, const ResourceDesc& bufferDesc)
	{
		// VkBufferImageCopy is used for image -> buffer as well as buffer -> image
		// In this case, we don't care which input is the src or dst; one of them is
		// considered the "buffer" while the other is considered the "image"
		assert(imageDesc._type == Resource::Desc::Type::Texture);

		auto arrayCount = ActualArrayLayerCount(imageDesc._textureDesc);
		auto mips = imageDesc._textureDesc._mipCount;
		if (bufferDesc._type == Resource::Desc::Type::Texture)
			mips = (unsigned)std::min(mips, bufferDesc._textureDesc._mipCount);
		unsigned width = imageDesc._textureDesc._width, height = imageDesc._textureDesc._height, depth = imageDesc._textureDesc._depth;
		auto minDims = (GetCompressionType(imageDesc._textureDesc._format) == FormatCompressionType::BlockCompression) ? 4u : 1u;
		auto dstAspectMask = AsImageAspectMask(imageDesc._textureDesc._format);

		// The buffer desc doesn't need to be registered as a "texture" type; but if it is, let's
		// ensure that the format matches
		if (bufferDesc._type == Resource::Desc::Type::Texture) {
			assert(bufferDesc._textureDesc._width == width);
			assert(bufferDesc._textureDesc._height == height);
			assert(bufferDesc._textureDesc._depth == depth);
			assert(bufferDesc._textureDesc._format == imageDesc._textureDesc._format);
		}
		auto bufferSize = ByteCount(bufferDesc);
		(void)bufferSize;

		std::vector<VkBufferImageCopy> result;
		result.resize(mips*arrayCount);

		for (unsigned m=0; m<mips; ++m) {
			auto mipOffset = GetSubResourceOffset(imageDesc._textureDesc, m, 0);
			for (unsigned a=0; a<arrayCount; ++a) {
				auto& c = result[m+a*mips];
				c.bufferOffset = mipOffset._offset + mipOffset._pitches._arrayPitch * a;
				c.bufferRowLength = std::max(width, minDims);
				c.bufferImageHeight = std::max(height, minDims);
				c.imageSubresource = VkImageSubresourceLayers{ dstAspectMask, m, a, 1 };
				c.imageOffset = VkOffset3D{0,0,0};
				c.imageExtent = VkExtent3D{std::max(width, 1u), std::max(height, 1u), std::max(depth, 1u)};

				#if defined(_DEBUG)
					auto end = c.bufferOffset + c.bufferRowLength*c.bufferImageHeight*BitsPerPixel(imageDesc._textureDesc._format)/8;
					assert(end <= bufferSize);
				#endif
			}

			width >>= 1u;
			height >>= 1u;
			depth >>= 1u;
		}

		return result;
	}

	static void Copy(DeviceContext& context, Resource& dst, Resource& src, VkImageLayout dstLayout, VkImageLayout srcLayout)
	{
        if (dst.GetImage() && src.GetImage()) {
            // image to image copy

            // Each mipmap is treated as a separate copy operation (but multiple array layers can be handled
		    // in a single operation).
		    // The Vulkan API requires that the formats of each resource must be reasonably similiar
		    //		-- in practice, that means that the size of the pixels in both cases must be the same.
		    //		When copying between compressed and uncompressed images, the uncompressed pixel size must
		    //		be equal to the compressed block size.

		    const auto& srcDesc = src.AccessDesc();
		    const auto& dstDesc = dst.AccessDesc();
		    assert(srcDesc._type == Resource::Desc::Type::Texture);
		    assert(dstDesc._type == Resource::Desc::Type::Texture);

		    VkImageCopy copyOps[16];

		    unsigned copyOperations = 0;
		    auto dstAspectMask = AsImageAspectMask(dstDesc._textureDesc._format);
		    auto srcAspectMask = AsImageAspectMask(srcDesc._textureDesc._format);

		    assert(ActualArrayLayerCount(srcDesc._textureDesc) == ActualArrayLayerCount(dstDesc._textureDesc));
		
		    auto mips = (unsigned)std::min(srcDesc._textureDesc._mipCount, dstDesc._textureDesc._mipCount);
		    assert(mips <= dimof(copyOps));
		    auto width = srcDesc._textureDesc._width, height = srcDesc._textureDesc._height, depth = srcDesc._textureDesc._depth;
		    for (unsigned m = 0; m < mips; ++m) {
                assert(copyOperations < dimof(copyOps));
			    auto& c = copyOps[copyOperations++];
			    c.srcOffset = VkOffset3D { 0, 0, 0 };
			    c.dstOffset = VkOffset3D { 0, 0, 0 };
			    c.extent = VkExtent3D { width, height, depth };
			    c.srcSubresource.aspectMask = srcAspectMask;
			    c.srcSubresource.mipLevel = m;
			    c.srcSubresource.baseArrayLayer = 0;
			    c.srcSubresource.layerCount = VK_REMAINING_ARRAY_LAYERS;
			    c.dstSubresource.aspectMask = dstAspectMask;
			    c.dstSubresource.mipLevel = m;
			    c.dstSubresource.baseArrayLayer = 0;
			    c.dstSubresource.layerCount = VK_REMAINING_ARRAY_LAYERS;

			    width = std::max(1u, width>>1);
			    height = std::max(1u, height>>1);
			    depth = std::max(1u, depth>>1);
		    }

		    context.GetActiveCommandList().CopyImage(
			    src.GetImage(), srcLayout,
			    dst.GetImage(), dstLayout,
			    copyOperations, copyOps);

        } else if (dst.GetBuffer() && src.GetBuffer()) {
            // buffer to buffer copy
            const auto& srcDesc = src.AccessDesc();
		    const auto& dstDesc = dst.AccessDesc();
            VkBufferCopy copyOps[] = 
            {
                VkBufferCopy{0, 0, std::min(ByteCount(srcDesc), ByteCount(dstDesc))}
            };
            context.GetActiveCommandList().CopyBuffer(
                src.GetBuffer(),
                dst.GetBuffer(),
                dimof(copyOps), copyOps);
        } else if (dst.GetImage() && src.GetBuffer()) {
            // This copy operation is typically used when initializing a texture via staging
            // resource.
            auto copyOps = GenerateBufferImageCopyOps(dst.AccessDesc(), src.AccessDesc());
            context.GetActiveCommandList().CopyBufferToImage(
                src.GetBuffer(),
                dst.GetImage(), dstLayout,
                (uint32_t)copyOps.size(), copyOps.data());
        } else {
            auto copyOps = GenerateBufferImageCopyOps(src.AccessDesc(), dst.AccessDesc());
            context.GetActiveCommandList().CopyImageToBuffer(
                src.GetImage(), srcLayout,
				dst.GetBuffer(),
                (uint32_t)copyOps.size(), copyOps.data());
        }
	}

    static void CopyPartial(
        DeviceContext& context, 
        const CopyPartial_Dest& dst, const CopyPartial_Src& src,
        VkImageLayout dstLayout, VkImageLayout srcLayout)
    {
		// Memory alignment rules:
		//
		// offsets & sizes must be multiples of the byte width of the texel format
		// For compressed block formats, offsets must be multiples of the compressed block size
		// 		also, image offsets must be on block boundaries
		//		also width/height/depth must be multiplies of the block size, except for blocks along the right and bottom edge
		// for depth/stencil formats, buffer offsets must be a multiple of 4
		// 
		// use VkPhysicalDeviceLimits.optimalBufferCopyOffsetAlignment & VkPhysicalDeviceLimits.optimalBufferCopyRowPitchAlignment
		// for optimizing the alignment for buffer sources
		//
		// VkQueueFamilyProperties can impose special rules on image transfer, via the 
		// 		minImageTransferGranularity property. See the entry in the vk spec for more details. Queues
		// 		with this set to (1,1,1) are ideal, because that means no limitations -- however some queues
		// 		can require that only full miplevels be copied at a time

        assert(src._resource && dst._resource);
		auto dstResource = checked_cast<Resource*>(dst._resource);
		auto srcResource = checked_cast<Resource*>(src._resource);
        if (dstResource->GetImage() && srcResource->GetImage()) {
            // image to image copy
            // In this case, we're going to generate only a single copy operation. This is 
            // similar to CopySubresourceRegion in D3D

            const auto& srcDesc = srcResource->AccessDesc();
		    const auto& dstDesc = dstResource->AccessDesc();
		    assert(srcDesc._type == Resource::Desc::Type::Texture);
		    assert(dstDesc._type == Resource::Desc::Type::Texture);
			assert(src._mipLevelCount > 0);

		    auto dstAspectMask = AsImageAspectMask(dstDesc._textureDesc._format);
		    auto srcAspectMask = AsImageAspectMask(srcDesc._textureDesc._format);
			auto mipLevelCount = dstDesc._textureDesc._mipCount - dst._subResource._mip;
			auto arrayLayerCount = ActualArrayLayerCount(dstDesc._textureDesc) - dst._subResource._arrayLayer;
			if (src._flags & CopyPartial_Src::Flags::EnableSubresourceRange) {
				mipLevelCount = std::min(mipLevelCount, std::min(src._mipLevelCount, srcDesc._textureDesc._mipCount - src._subResource._mip));
				arrayLayerCount = std::min(arrayLayerCount, std::min(src._arrayLayerCount, ActualArrayLayerCount(srcDesc._textureDesc) - src._subResource._arrayLayer));
			} else {
				mipLevelCount = std::min(mipLevelCount, (unsigned)srcDesc._textureDesc._mipCount);
				arrayLayerCount = std::min(arrayLayerCount, ActualArrayLayerCount(srcDesc._textureDesc));
			}
			assert(arrayLayerCount > 0 && mipLevelCount > 0);
			assert(!(src._flags & CopyPartial_Src::Flags::EnableLinearBufferRange));

			// validate that the provided texture pitches were as expected (since we can't specify these explicitly to vulkan)
			if (src._flags & CopyPartial_Src::Flags::EnablePartialSubresourceArea) {
				auto firstMip = (src._flags & CopyPartial_Src::Flags::EnableSubresourceRange) ? src._subResource._mip : 0;
				if (!(src._partialSubresourcePitches == MakeTexturePitches(CalculateMipMapDesc(srcDesc._textureDesc, firstMip))))
					Throw(std::runtime_error("Source texture pitches do not match underlying texture desc. Use MakeTexturePitches(CalculateMipMapDesc(...)) to get the correct matching pitches"));
				if (mipLevelCount != 1)
					Throw(std::runtime_error("When copying a partial subresource area, only a single mip level is supported"));	// copying multiple mip levels wouldn't work, because the partial ranges needs to be different for each mip level
			}
			assert(!dst._leftTopFrontIsLinearBufferOffset);		// expecting an actual xyz coord since it's an image

            VLA(VkImageCopy, copies, mipLevelCount);
			for (unsigned q=0; q<mipLevelCount; ++q) {
				auto&c = copies[q];
				c.dstOffset = VkOffset3D { (int)dst._leftTopFront._values[0], (int)dst._leftTopFront._values[1], (int)dst._leftTopFront._values[2] };

				if (src._flags & CopyPartial_Src::Flags::EnablePartialSubresourceArea) {
					c.srcOffset = VkOffset3D { (int)src._leftTopFront._values[0], (int)src._leftTopFront._values[1], (int)src._leftTopFront._values[2] };
					c.extent = VkExtent3D { 
						src._rightBottomBack._values[0] - src._leftTopFront._values[0],
						src._rightBottomBack._values[1] - src._leftTopFront._values[1],
						src._rightBottomBack._values[2] - src._leftTopFront._values[2]
					};
				} else {
					c.srcOffset = VkOffset3D { 0, 0, 0 };
					c.extent = VkExtent3D { srcDesc._textureDesc._width, srcDesc._textureDesc._height, srcDesc._textureDesc._depth };
				}

				c.srcSubresource.aspectMask = srcAspectMask;
				if (src._flags & CopyPartial_Src::Flags::EnableSubresourceRange) {
					c.srcSubresource.mipLevel = src._subResource._mip;
					c.srcSubresource.baseArrayLayer = src._subResource._arrayLayer;
				} else {
					c.srcSubresource.mipLevel = 0;
					c.srcSubresource.baseArrayLayer = 0;
				}
				c.srcSubresource.layerCount = arrayLayerCount;
				c.dstSubresource.aspectMask = dstAspectMask;
				c.dstSubresource.mipLevel = dst._subResource._mip;
				c.dstSubresource.baseArrayLayer = dst._subResource._arrayLayer;
				c.dstSubresource.layerCount = arrayLayerCount;
			}

			context.GetActiveCommandList().CopyImage(
			    srcResource->GetImage(), srcLayout,
			    dstResource->GetImage(), dstLayout,
			    mipLevelCount, copies);
        } else if (dstResource->GetBuffer() && srcResource->GetBuffer()) {
            // buffer to buffer copy
            const auto& srcDesc = srcResource->AccessDesc();
		    const auto& dstDesc = dstResource->AccessDesc();
			assert(src._mipLevelCount == 1 && src._arrayLayerCount == 1);		// defaults for these values
			assert(!(src._flags & (CopyPartial_Src::Flags::EnablePartialSubresourceArea|CopyPartial_Src::Flags::EnableSubresourceRange)));
            VkBufferCopy c;
            c.srcOffset = 0;
            c.dstOffset = 0;
			if (dst._leftTopFrontIsLinearBufferOffset) c.dstOffset += dst._leftTopFront._values[0];
			else assert(dst._leftTopFront._values[0] == 0 && dst._leftTopFront._values[1] == 0 && dst._leftTopFront._values[2] == 0);
			assert(srcDesc._type == ResourceDesc::Type::LinearBuffer);
            auto end = srcDesc._linearBufferDesc._sizeInBytes;
			if (src._flags & CopyPartial_Src::Flags::EnableLinearBufferRange) {
				assert(src._linearBufferRange.first < src._linearBufferRange.second);
				c.srcOffset = src._linearBufferRange.first;
				end = std::min(end, src._linearBufferRange.second);
			}
			c.size = end - c.srcOffset;
			assert(dstDesc._type == ResourceDesc::Type::LinearBuffer);
			c.size = std::min(c.size, (VkDeviceSize)dstDesc._linearBufferDesc._sizeInBytes - c.dstOffset);
            context.GetActiveCommandList().CopyBuffer(
                srcResource->GetBuffer(),
                dstResource->GetBuffer(),
                1, &c);
        } else if (dstResource->GetImage() && srcResource->GetBuffer()) {
            // This copy operation is typically used when initializing a texture via staging
            // resource.
            const auto& srcDesc = srcResource->AccessDesc();
		    const auto& dstDesc = dstResource->AccessDesc();
		    assert(dstDesc._type == Resource::Desc::Type::Texture);

            auto dstAspectMask = AsImageAspectMask(dstDesc._textureDesc._format);
			auto mipLevelCount = dstDesc._textureDesc._mipCount - dst._subResource._mip;
			auto arrayLayerCount = ActualArrayLayerCount(dstDesc._textureDesc) - dst._subResource._arrayLayer;
			if (src._flags & CopyPartial_Src::Flags::EnableSubresourceRange) {
				if (srcDesc._type == Resource::Desc::Type::Texture) {
					mipLevelCount = std::min(mipLevelCount, std::min(src._mipLevelCount, srcDesc._textureDesc._mipCount - src._subResource._mip));
					arrayLayerCount = std::min(arrayLayerCount, std::min(src._arrayLayerCount, ActualArrayLayerCount(srcDesc._textureDesc) - src._subResource._arrayLayer));
				} else {
					mipLevelCount = std::min(mipLevelCount, src._mipLevelCount);
					arrayLayerCount = std::min(arrayLayerCount, src._arrayLayerCount);
				}
			} else if (srcDesc._type == Resource::Desc::Type::Texture) {
				mipLevelCount = std::min(mipLevelCount, (unsigned)srcDesc._textureDesc._mipCount);
				arrayLayerCount = std::min(arrayLayerCount, ActualArrayLayerCount(srcDesc._textureDesc));
			}
			assert(arrayLayerCount > 0 && mipLevelCount > 0);

			// validate that the provided texture pitches were as expected (since we can't specify these explicitly to vulkan)
			if (src._flags & CopyPartial_Src::Flags::EnablePartialSubresourceArea && srcDesc._type == Resource::Desc::Type::Texture) {
				auto firstMip = (src._flags & CopyPartial_Src::Flags::EnableSubresourceRange) ? src._subResource._mip : 0;
				if (!(src._partialSubresourcePitches == MakeTexturePitches(CalculateMipMapDesc(srcDesc._textureDesc, firstMip))))
					Throw(std::runtime_error("Source texture pitches do not match underlying texture desc. Use MakeTexturePitches(CalculateMipMapDesc(...)) to get the correct matching pitches"));
				if (mipLevelCount != 1)
					Throw(std::runtime_error("When copying a partial subresource area, only a single mip level is supported"));	// copying multiple mip levels wouldn't work, because the partial ranges needs to be different for each mip level
			}
			assert(!dst._leftTopFrontIsLinearBufferOffset);		// expecting an actual xyz coord since it's an image

			// Vulkan can copy mutliple array layers in a single VkBufferImageCopy, but that expects
			// array layers to be stored contiguously. By contrast, GetSubResourceOffset() uses a layout
			// where a full mipchain is contigous, and there's a gap between subsequent array layers of
			// the same mip level. So let's just expand out to a separate copy op per mip chain, just to
			// avoid a special requirement there.
            VLA(VkBufferImageCopy, copyOps, arrayLayerCount*mipLevelCount);

			for (unsigned m=0; m<mipLevelCount; ++m)
				for (unsigned a=0; a<arrayLayerCount; ++a) {
					auto& copyOp = copyOps[m*arrayLayerCount+a];

					auto dstSubResDesc = CalculateMipMapDesc(dstDesc._textureDesc, dst._subResource._mip+m);
					auto minDims = (GetCompressionType(dstDesc._textureDesc._format) == FormatCompressionType::BlockCompression) ? 4u : 1u;

					copyOp.imageSubresource = VkImageSubresourceLayers{ dstAspectMask, dst._subResource._mip+m, dst._subResource._arrayLayer+a, 1 };
					copyOp.imageOffset = VkOffset3D{(int32_t)dst._leftTopFront[0], (int32_t)dst._leftTopFront[1], (int32_t)dst._leftTopFront[2]};
					if (src._flags & CopyPartial_Src::Flags::EnableLinearBufferRange)
						copyOp.bufferOffset = src._linearBufferRange.first;

					if (srcDesc._type == Resource::Desc::Type::Texture) {
						auto srcMipOffset = GetSubResourceOffset(srcDesc._textureDesc, src._subResource._mip+m, src._subResource._arrayLayer+a);
						auto srcSubResDesc = CalculateMipMapDesc(srcDesc._textureDesc, dst._subResource._mip+m);

						copyOp.bufferOffset += srcMipOffset._offset;
						copyOp.bufferRowLength = std::max(srcSubResDesc._width, minDims);
						copyOp.bufferImageHeight = std::max(srcSubResDesc._height, minDims);

						if (src._flags & CopyPartial_Src::Flags::EnablePartialSubresourceArea) {
							assert(srcMipOffset._pitches == src._partialSubresourcePitches);
							copyOp.bufferOffset += 
								src._leftTopFront[2] * src._partialSubresourcePitches._slicePitch
								+ src._leftTopFront[1] * src._partialSubresourcePitches._rowPitch
								+ src._leftTopFront[0] * BitsPerPixel(srcDesc._textureDesc._format) / 8;
							copyOp.imageExtent = VkExtent3D{
								std::min(src._rightBottomBack[0], srcSubResDesc._width) - src._leftTopFront[0],
								std::min(src._rightBottomBack[1], srcSubResDesc._height) - src._leftTopFront[1],
								std::min(src._rightBottomBack[2], std::max(srcSubResDesc._depth, 1u)) - src._leftTopFront[2]};
						} else {
							copyOp.imageExtent = VkExtent3D { srcSubResDesc._width, srcSubResDesc._height, std::max(srcSubResDesc._depth, 1u) };
						}
					} else {
						auto srcMipOffset = GetSubResourceOffset(dstDesc._textureDesc, src._subResource._mip+m, src._subResource._arrayLayer+a);
						copyOp.bufferOffset += srcMipOffset._offset;
						copyOp.bufferRowLength = std::max(dstSubResDesc._width, minDims);
						copyOp.bufferImageHeight = std::max(dstSubResDesc._height, minDims);

						if (src._flags & CopyPartial_Src::Flags::EnablePartialSubresourceArea) {
							auto bpp = BitsPerPixel(dstDesc._textureDesc._format);
							assert((src._partialSubresourcePitches._rowPitch % (bpp / 8)) == 0);
							assert((src._partialSubresourcePitches._slicePitch % src._partialSubresourcePitches._rowPitch) == 0);
							assert((src._partialSubresourcePitches._arrayPitch % src._partialSubresourcePitches._slicePitch) == 0);
							copyOp.bufferRowLength = src._partialSubresourcePitches._rowPitch / (bpp / 8);
							copyOp.bufferImageHeight = src._partialSubresourcePitches._slicePitch / src._partialSubresourcePitches._rowPitch;
							copyOp.bufferOffset += 
								src._leftTopFront[2] * src._partialSubresourcePitches._slicePitch
								+ src._leftTopFront[1] * src._partialSubresourcePitches._rowPitch
								+ src._leftTopFront[0] * bpp / 8;
							copyOp.imageExtent = VkExtent3D{
								std::min(src._rightBottomBack[0], dstSubResDesc._width) - src._leftTopFront[0],
								std::min(src._rightBottomBack[1], dstSubResDesc._height) - src._leftTopFront[1],
								std::min(src._rightBottomBack[2], std::max(dstSubResDesc._depth, 1u)) - src._leftTopFront[2]};
						} else {
							copyOp.imageExtent = VkExtent3D{dstSubResDesc._width, dstSubResDesc._height, std::max(dstSubResDesc._depth, 1u)};
						}
					}
				}

            context.GetActiveCommandList().CopyBufferToImage(
                srcResource->GetBuffer(),
                dstResource->GetImage(), dstLayout,
                arrayLayerCount*mipLevelCount, copyOps);
		} else if (dstResource->GetBuffer() && srcResource->GetImage()) {
			const auto& srcDesc = srcResource->AccessDesc();
		    const auto& dstDesc = dstResource->AccessDesc();
		    assert(srcDesc._type == Resource::Desc::Type::Texture);

            auto srcAspectMask = AsImageAspectMask(srcDesc._textureDesc._format);

			unsigned mipLevelCount = srcDesc._textureDesc._mipCount;
			unsigned arrayLayerCount = ActualArrayLayerCount(srcDesc._textureDesc);
			if (dstDesc._type == Resource::Desc::Type::Texture) {
				mipLevelCount = std::min(mipLevelCount, (unsigned)dstDesc._textureDesc._mipCount - dst._subResource._mip);
				arrayLayerCount = std::min(arrayLayerCount, ActualArrayLayerCount(dstDesc._textureDesc) - dst._subResource._arrayLayer);
			}
			if (src._flags & CopyPartial_Src::Flags::EnableSubresourceRange) {
				mipLevelCount = std::min(mipLevelCount, src._mipLevelCount);
				arrayLayerCount = std::min(arrayLayerCount, src._arrayLayerCount);
			}
			assert(arrayLayerCount > 0 && mipLevelCount > 0);

			// validate that the provided texture pitches were as expected (since we can't specify these explicitly to vulkan)
			if (src._flags & CopyPartial_Src::Flags::EnablePartialSubresourceArea && srcDesc._type == Resource::Desc::Type::Texture) {
				auto firstMip = (src._flags & CopyPartial_Src::Flags::EnableSubresourceRange) ? src._subResource._mip : 0;
				if (!(src._partialSubresourcePitches == MakeTexturePitches(CalculateMipMapDesc(srcDesc._textureDesc, firstMip))))
					Throw(std::runtime_error("Source texture pitches do not match underlying texture desc. Use MakeTexturePitches(CalculateMipMapDesc(...)) to get the correct matching pitches"));
				if (mipLevelCount != 1)
					Throw(std::runtime_error("When copying a partial subresource area, only a single mip level is supported"));	// copying multiple mip levels wouldn't work, because the partial ranges needs to be different for each mip level
			}

            VLA(VkBufferImageCopy, copyOps, arrayLayerCount*mipLevelCount);
			for (unsigned m=0; m<mipLevelCount; ++m)
				for (unsigned a=0; a<arrayLayerCount; ++a) {
					auto& copyOp = copyOps[m*arrayLayerCount+a];

					auto srcSubResDesc = CalculateMipMapDesc(srcDesc._textureDesc, src._subResource._mip+m);
					auto minDims = (GetCompressionType(srcSubResDesc._format) == FormatCompressionType::BlockCompression) ? 4u : 1u;

					copyOp.bufferOffset = 0;
					if (dst._leftTopFrontIsLinearBufferOffset)
						copyOp.bufferOffset = dst._leftTopFront[0];

					if (dstDesc._type == Resource::Desc::Type::Texture) {
						auto destMipOffset = GetSubResourceOffset(dstDesc._textureDesc, dst._subResource._mip+m, dst._subResource._arrayLayer+a);
						copyOp.bufferOffset += destMipOffset._offset;
						if (!dst._leftTopFrontIsLinearBufferOffset)
							copyOp.bufferOffset += 
								  dst._leftTopFront[2] * destMipOffset._pitches._slicePitch
								+ dst._leftTopFront[1] * destMipOffset._pitches._rowPitch
								+ dst._leftTopFront[0] * BitsPerPixel(dstDesc._textureDesc._format) / 8;
						auto dstSubResDesc = CalculateMipMapDesc(dstDesc._textureDesc, dst._subResource._mip+m);
						copyOp.bufferRowLength = std::max(dstSubResDesc._width, minDims);
						copyOp.bufferImageHeight = std::max(dstSubResDesc._height, minDims);
					} else {
						auto destMipOffset = GetSubResourceOffset(srcDesc._textureDesc, dst._subResource._mip+m, dst._subResource._arrayLayer+a);
						copyOp.bufferOffset += destMipOffset._offset;
						assert(dst._leftTopFrontIsLinearBufferOffset || (dst._leftTopFront[0] == 0 && dst._leftTopFront[1] == 0 && dst._leftTopFront[2] == 0));
						copyOp.bufferRowLength = std::max(srcSubResDesc._width, minDims);
						copyOp.bufferImageHeight = std::max(srcSubResDesc._height, minDims);
					}

					copyOp.imageSubresource = VkImageSubresourceLayers{ srcAspectMask, 0, 0, 1 };
					copyOp.imageOffset = VkOffset3D{ 0, 0, 0 };
					copyOp.imageExtent = VkExtent3D{ srcSubResDesc._width, srcSubResDesc._height, std::max(srcSubResDesc._depth, 1u) };

					if (src._flags & CopyPartial_Src::Flags::EnableSubresourceRange)
						copyOp.imageSubresource = VkImageSubresourceLayers{ srcAspectMask, src._subResource._mip+m, src._subResource._arrayLayer+a, 1 };
					if (src._flags & CopyPartial_Src::Flags::EnablePartialSubresourceArea) {
						copyOp.imageOffset = VkOffset3D{(int32_t)src._leftTopFront[0], (int32_t)src._leftTopFront[1], (int32_t)src._leftTopFront[2]};
						copyOp.imageExtent = VkExtent3D{
							std::min(src._rightBottomBack[0], srcSubResDesc._width) - src._leftTopFront[0],
							std::min(src._rightBottomBack[1], srcSubResDesc._height) - src._leftTopFront[1],
							std::min(src._rightBottomBack[2], std::max(srcSubResDesc._depth, 1u)) - src._leftTopFront[2]};
					}
				}

            context.GetActiveCommandList().CopyImageToBuffer(
                srcResource->GetImage(), srcLayout,
                dstResource->GetBuffer(),
                arrayLayerCount*mipLevelCount, copyOps);
        } else {
            Throw(::Exceptions::BasicLabel("Blit copy operation not supported"));
        }
    }

    static unsigned CopyViaMemoryMap(
        VkDevice device, ResourceMap& map,
        VkImage imageForLayout, const TextureDesc& descForLayout,
        const std::function<SubResourceInitData(SubResourceId)>& initData)
    {
        // Copy all of the subresources to device member, using a MemoryMap path.
        // If "image" is not null, we will get the arrangement of subresources from
        // the images. Otherwise, we will use a default arrangement of subresources.
        unsigned bytesUploaded = 0;

		auto mipCount = unsigned(descForLayout._mipCount);
		auto arrayCount = ActualArrayLayerCount(descForLayout);
		auto aspectFlags = AsImageAspectMask(descForLayout._format);
		for (unsigned m = 0; m < mipCount; ++m) {
            auto mipDesc = CalculateMipMapDesc(descForLayout, m);
			for (unsigned a = 0; a < arrayCount; ++a) {
                auto subResData = initData({m, a});
				if (!subResData._data.size()) continue;

				VkSubresourceLayout layout = {};
                if (imageForLayout) {
                    VkImageSubresource subRes = { aspectFlags, m, a };
				    vkGetImageSubresourceLayout(
					    device, imageForLayout,
					    &subRes, &layout);
                } else {
                    auto offset = GetSubResourceOffset(descForLayout, m, a);
                    layout = VkSubresourceLayout { 
                        offset._offset, offset._size, 
                        offset._pitches._rowPitch, offset._pitches._arrayPitch, offset._pitches._slicePitch };
                }

				if (!layout.size) continue;	// couldn't find this subresource?

				auto defaultPitches = MakeTexturePitches(mipDesc);
				if (!subResData._pitches._rowPitch && !subResData._pitches._slicePitch && !subResData._pitches._arrayPitch)
					subResData._pitches = defaultPitches;

                CopyMipLevel(
                    PtrAdd(map.GetData().begin(), layout.offset), size_t(layout.size),		// assuming the map does not have multiple subresources here
                    TexturePitches{unsigned(layout.rowPitch), unsigned(layout.depthPitch), unsigned(layout.arrayPitch)},
                    mipDesc, subResData);
			}
		}
        return bytesUploaded;
    }

    namespace Internal
	{
		unsigned CopyViaMemoryMap(
			IDevice& dev, IResource& resource, unsigned resourceOffset, unsigned resourceSize,
			const TextureDesc& descForLayout,
			const std::function<SubResourceInitData(SubResourceId)>& initData)
		{
			auto& res = *checked_cast<Resource*>(&resource);
			ResourceMap map{dev, resource, ResourceMap::Mode::WriteDiscardPrevious, resourceOffset, resourceSize};
			return Metal_Vulkan::CopyViaMemoryMap(
				GetObjectFactory(dev).GetDevice().get(), map,
				res.GetImage(), descForLayout, initData);
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	void CompleteInitialization(
		DeviceContext& context,
		IteratorRange<IResource* const*> resources)
	{
		VLA(uint64_t, makeResourcesVisibleExtra, resources.size());
		unsigned makeResourcesVisibleExtraCount = 0;

		BarrierHelper barrierHelper{context};
		for (auto r:resources) {
			auto* res = checked_cast<Resource*>(r);
			if (res->_pendingInit) {
				if (res->_steadyStateImageLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
					barrierHelper.Add(*res, BarrierResourceUsage::NoState(), Internal::DefaultBarrierResourceUsageFromLayout(res->_steadyStateImageLayout));
				} else {
					// also make these resources visible, even though they don't get an actual barrier
					makeResourcesVisibleExtra[makeResourcesVisibleExtraCount++] = res->GetGUID();
				}
				res->_pendingInit = false;
			}
		}
		if (makeResourcesVisibleExtraCount)
			context.GetActiveCommandList().MakeResourcesVisible(MakeIteratorRange(makeResourcesVisibleExtra, &makeResourcesVisibleExtra[makeResourcesVisibleExtraCount]));
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static std::vector<std::pair<SubResourceId, SubResourceOffset>> FindSubresources(VkDevice dev, IResource& iresource)
	{
		std::vector<std::pair<SubResourceId, SubResourceOffset>> result;
		auto& resource = *checked_cast<Resource*>(&iresource);

		auto desc = resource.AccessDesc();
		auto actualArrayCount = ActualArrayLayerCount(desc._textureDesc);
		if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::CubeMap) {
			assert(actualArrayCount == 6u);
		}
		result.reserve(actualArrayCount * desc._textureDesc._mipCount);

		auto* image = resource.GetImage();
        if (image) {
			assert(desc._type == ResourceDesc::Type::Texture);
            auto aspectMask = AsImageAspectMask(desc._textureDesc._format);

			for (unsigned arrayLayer=0; arrayLayer<actualArrayCount; ++arrayLayer)
				for (unsigned mip=0; mip<(unsigned)desc._textureDesc._mipCount; ++mip) {
					VkImageSubresource sub = { aspectMask, mip, arrayLayer };
					VkSubresourceLayout layout = {};
					vkGetImageSubresourceLayout(dev, image, &sub, &layout);

					SubResourceOffset loc;
					loc._offset = layout.offset;
					loc._size = layout.size;
					loc._pitches = TexturePitches { unsigned(layout.rowPitch), unsigned(layout.depthPitch) };
					result.push_back(std::make_pair(SubResourceId{mip, arrayLayer}, loc));
				}
        } else if (desc._type == ResourceDesc::Type::Texture) {
			// This is the staging texture case. We can use GetSubResourceOffset to
			// calculate the arrangement of subresources
			for (unsigned arrayLayer=0; arrayLayer<actualArrayCount; ++arrayLayer)
				for (unsigned mip=0; mip<(unsigned)desc._textureDesc._mipCount; ++mip) {
					auto subResOffset = GetSubResourceOffset(desc._textureDesc, mip, arrayLayer);
					result.push_back(std::make_pair(SubResourceId{mip, arrayLayer}, subResOffset));
				}
		} else {
			SubResourceOffset sub;
			sub._offset = 0;
			sub._size = desc._linearBufferDesc._sizeInBytes;
			sub._pitches = TexturePitches { desc._linearBufferDesc._sizeInBytes, desc._linearBufferDesc._sizeInBytes, desc._linearBufferDesc._sizeInBytes };
			result.push_back(std::make_pair(SubResourceId{}, sub));
		}
		return result;
	}

	static bool ResourceMapViable(Resource& res, ResourceMap::Mode mode)
	{
		// check this resource's compatibility with this mapping mode
		auto* memType = GetObjectFactory().GetMemoryTypeInfo(res.GetMemoryType());
		if (!memType || !(memType->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) return false;

		auto& desc = res.AccessDesc();
		if (mode == ResourceMap::Mode::WriteDiscardPrevious && !(desc._allocationRules & (AllocationRules::HostVisibleSequentialWrite|AllocationRules::HostVisibleRandomAccess)))
			return false;
		if (mode == ResourceMap::Mode::Read && !(desc._allocationRules & AllocationRules::HostVisibleRandomAccess))
			return false;
		return true;
	}

	ResourceMap::ResourceMap(
		VkDevice dev, VkDeviceMemory memory,
		VkDeviceSize offset, VkDeviceSize size)
	: _dev(dev), _mem(memory)
	{
		// There are many restrictions on this call -- see the Vulkan docs.
		// * we must ensure that the memory was allocated with VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		// * we must ensure that the memory was allocated with VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		//          (because we're not performing manual memory flushes)
		// * we must ensure that this memory range is not used by the GPU during the map
		//		(though, presumably, other memory ranges within the same memory object could be in use)
		auto res = vkMapMemory(dev, memory, offset, size, 0, &_data);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failed while mapping device memory"));

        // we don't actually know the size or pitches in this case
        _dataSize = 0;
		_resourceOffset = 0;
		TexturePitches pitches { (unsigned)_dataSize, (unsigned)_dataSize, (unsigned)_dataSize };
		_subResources.push_back(std::make_pair(SubResourceId{}, SubResourceOffset{ 0, _dataSize, pitches }));
	}

	ResourceMap::ResourceMap(VmaAllocator dev, VmaAllocation memory)
	{
		auto res = vmaMapMemory(dev, memory, &_data);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failed while mapping device memory"));
		_vmaAllocator = dev;
		_vmaMem = memory;

		// we don't actually know the size or pitches in this case
        _dataSize = 0;
		_resourceOffset = 0;
		TexturePitches pitches { (unsigned)_dataSize, (unsigned)_dataSize, (unsigned)_dataSize };
		_subResources.push_back(std::make_pair(SubResourceId{}, SubResourceOffset{ 0, _dataSize, pitches }));
	}

	ResourceMap::ResourceMap(
		ObjectFactory& factory, IResource& iresource,
		Mode mapMode,
        VkDeviceSize offset, VkDeviceSize size)
	{
		///////////
			// Map a range in a linear buffer (makes less sense for textures)
		///////////////////
		auto& resource = *checked_cast<Resource*>(&iresource);
		assert(ResourceMapViable(resource, mapMode));
		auto desc = resource.AccessDesc();
		if (desc._type != ResourceDesc::Type::LinearBuffer)
			Throw(std::runtime_error("Attempting to map a linear range in a non-linear buffer resource"));

		if (offset >= desc._linearBufferDesc._sizeInBytes || (offset+size) > desc._linearBufferDesc._sizeInBytes || size == 0)
			Throw(std::runtime_error(StringMeld<256>() << "Invalid range when attempting to map a linear buffer range. Offset: " << offset << ", Size: " << size));

		_dataSize = std::min(desc._linearBufferDesc._sizeInBytes - offset, size);
		TexturePitches pitches { (unsigned)_dataSize, (unsigned)_dataSize, (unsigned)_dataSize };

		if (!resource.GetPermanentlyMappedRange().empty()) {
			if ((offset + _dataSize) > resource.GetPermanentlyMappedRange().size())
				Throw(std::runtime_error("Mapping range for permanently mapped resource exceeds resource size"));
			_data = PtrAdd(resource.GetPermanentlyMappedRange().begin(), offset);
			_vmaMem = resource.GetVmaMemory();
			_vmaAllocator = factory.GetVmaAllocator();
			_dev = factory.GetDevice().get();
			_mem = resource.GetMemory();
			_permanentlyMappedResource = true;
		} else if ((_vmaMem = resource.GetVmaMemory())) {
			if (offset != 0 || desc._linearBufferDesc._sizeInBytes != _dataSize)
				Throw(std::runtime_error("vma based Vulkan resources only support whole-resource mapping. Avoid mapping a subrange of the resource"));
			auto res = vmaMapMemory(factory.GetVmaAllocator(), _vmaMem, &_data);
			_data = PtrAdd(_data, offset);
			if (res != VK_SUCCESS)
				Throw(VulkanAPIFailure(res, "Failed while mapping device memory"));
			_vmaAllocator = factory.GetVmaAllocator();
		} else {
			_dev = factory.GetDevice().get();
			auto res = vkMapMemory(_dev, resource.GetMemory(), offset, size, 0, &_data);
			if (res != VK_SUCCESS)
				Throw(VulkanAPIFailure(res, "Failed while mapping device memory"));
			_mem = resource.GetMemory();
		}

		_subResources.push_back(std::make_pair(SubResourceId{}, SubResourceOffset{ 0, _dataSize, pitches }));
		_resourceOffset = offset;
	}

	ResourceMap::ResourceMap(
		ObjectFactory& factory, IResource& iresource,
		Mode mapMode,
        SubResourceId subResource)
	{
		///////////
			// Map a single subresource
		///////////////////
        VkDeviceSize finalOffset = 0, finalSize = VK_WHOLE_SIZE;
		TexturePitches pitches;

		auto& resource = *checked_cast<Resource*>(&iresource);
		assert(ResourceMapViable(resource, mapMode));
		if (resource.GetVmaMemory())
			Throw(std::runtime_error("vma based Vulkan resources only support whole-resource mapping. Avoid mapping a subrange of the resource"));
		if (resource.GetPermanentlyMappedRange().empty())
			Throw(std::runtime_error("Unsupported mapping range for permanently mapped resource"));

        // special case for images, where we need to take into account the requested "subresource"
		_dev = factory.GetDevice().get();
        auto* image = resource.GetImage();
        auto desc = resource.AccessDesc();
        if (image) {
			assert(desc._type == ResourceDesc::Type::Texture);
            auto aspectMask = AsImageAspectMask(desc._textureDesc._format);
            VkImageSubresource sub = { aspectMask, subResource._mip, subResource._arrayLayer };
            VkSubresourceLayout layout = {};
            vkGetImageSubresourceLayout(_dev, image, &sub, &layout);
            finalOffset += layout.offset;
            finalSize = std::min(layout.size, finalSize);
            pitches = TexturePitches { unsigned(layout.rowPitch), unsigned(layout.depthPitch) };
            _dataSize = finalSize;
        } else if (desc._type == ResourceDesc::Type::Texture) {
			// This is the staging texture case. We can use GetSubResourceOffset to
			// calculate the arrangement of subresources
			auto subResOffset = GetSubResourceOffset(desc._textureDesc, subResource._mip, subResource._arrayLayer);
			finalOffset = subResOffset._offset;
			finalSize = subResOffset._size;
			pitches = subResOffset._pitches;
			_dataSize = finalSize;
		} else {
			_dataSize = desc._linearBufferDesc._sizeInBytes;
			pitches = TexturePitches { (unsigned)_dataSize, (unsigned)_dataSize, (unsigned)_dataSize };
		}

        auto res = vkMapMemory(_dev, resource.GetMemory(), finalOffset, finalSize, 0, &_data);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failed while mapping device memory"));

        _mem = resource.GetMemory();
		_subResources.push_back(std::make_pair(subResource, SubResourceOffset{ 0, _dataSize, pitches }));
		_resourceOffset = finalOffset;
    }

	ResourceMap::ResourceMap(
		ObjectFactory& factory, IResource& iresource,
		Mode mapMode)
	{
		///////////
			// Map all subresources
		///////////////////
		auto& resource = *checked_cast<Resource*>(&iresource);
		assert(ResourceMapViable(resource, mapMode));
		_dataSize = 0;
		_resourceOffset = 0;

		if (!resource.GetPermanentlyMappedRange().empty()) {
			_data = resource.GetPermanentlyMappedRange().begin();
			_dataSize = resource.GetPermanentlyMappedRange().size();
			_vmaMem = resource.GetVmaMemory();
			_vmaAllocator = factory.GetVmaAllocator();
			_dev = factory.GetDevice().get();
			_mem = resource.GetMemory();
			_permanentlyMappedResource = true;
		} else  if ((_vmaMem = resource.GetVmaMemory())) {
			auto res = vmaMapMemory(factory.GetVmaAllocator(), _vmaMem, &_data);
			if (res != VK_SUCCESS)
				Throw(VulkanAPIFailure(res, "Failed while mapping device memory"));
			_vmaAllocator = factory.GetVmaAllocator();
		} else {
			_dev = factory.GetDevice().get(); 
			auto res = vkMapMemory(_dev, resource.GetMemory(), 0, VK_WHOLE_SIZE, 0, &_data);
			if (res != VK_SUCCESS)
				Throw(VulkanAPIFailure(res, "Failed while mapping device memory"));
			_mem = resource.GetMemory();
		}

		_subResources = FindSubresources(factory.GetDevice().get(), resource);
		if (!_dataSize)
			for (const auto& subRes:_subResources)
				_dataSize = std::max(_dataSize, subRes.second._offset + subRes.second._size);
	}

	ResourceMap::ResourceMap(
		DeviceContext& context, IResource& resource,
		Mode mapMode)
	: ResourceMap(GetObjectFactory(context), resource, mapMode)
	{
	}

	ResourceMap::ResourceMap(
		DeviceContext& context, IResource& resource,
		Mode mapMode,
		SubResourceId subResource)
	: ResourceMap(GetObjectFactory(context), resource, mapMode, subResource)
	{
	}
	
	ResourceMap::ResourceMap(
		DeviceContext& context, IResource& resource,
		Mode mapMode,
		VkDeviceSize offset, VkDeviceSize size)
	: ResourceMap(GetObjectFactory(context), resource, mapMode, offset, size)
	{
	}

	ResourceMap::ResourceMap(
		IDevice& device, IResource& iresource,
		Mode mapMode)
	: ResourceMap{GetObjectFactory(device), iresource, mapMode}
	{
	}

	ResourceMap::ResourceMap(
		IDevice& device, IResource& iresource,
		Mode mapMode,
		SubResourceId subResource)
	: ResourceMap{GetObjectFactory(device), iresource, mapMode, subResource}		// only non-vma
	{
	}

	ResourceMap::ResourceMap(
		IDevice& device, IResource& iresource,
		Mode mapMode,
		VkDeviceSize offset, VkDeviceSize size)
	: ResourceMap{GetObjectFactory(device), iresource, mapMode, offset, size}		// only non-vma
	{
	}

	bool ResourceMap::CanMap(IDevice& device, IResource& resource, Mode mode)
	{
		auto* res = checked_cast<Resource*>(&resource);
		assert(res);
		return ResourceMapViable(*res, mode);
	}

	void ResourceMap::TryUnmap()
	{
		if (!_permanentlyMappedResource) {
			if (_vmaAllocator && _vmaMem) {
				vmaUnmapMemory(_vmaAllocator, _vmaMem);
				_vmaAllocator = nullptr;
				_vmaMem = nullptr;
			} else if (_dev && _mem) {
				vkUnmapMemory(_dev, _mem);
				_dev = nullptr;
				_mem = nullptr;
			}
		}
	}

	void ResourceMap::FlushCache()
	{
		if (_vmaMem) {
			assert(_dataSize);
			assert(_vmaAllocator);
			auto res = vmaFlushAllocation(_vmaAllocator, _vmaMem, _resourceOffset, _dataSize);
			if (res != VK_SUCCESS)
				Throw(std::runtime_error("Failure while flushing cache for resource"));
		} else if (_mem) {
			assert(_dataSize);
			assert(_dev);
			VkMappedMemoryRange mappedRange[] {
				{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, _mem, _resourceOffset, _dataSize }
			};
			vkFlushMappedMemoryRanges(_dev, 1, mappedRange);
		}
	}

	void ResourceMap::InvalidateCache() 
	{
		if (_vmaMem) {
			assert(_dataSize);
			assert(_vmaAllocator);
			auto res = vmaInvalidateAllocation(_vmaAllocator, _vmaMem, _resourceOffset, _dataSize);
			if (res != VK_SUCCESS)
				Throw(std::runtime_error("Failure while flushing cache for resource"));
		} else if (_mem) {
			assert(_dataSize);
			assert(_dev);
			VkMappedMemoryRange mappedRange[] {
				{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, _mem, _resourceOffset, _dataSize }
			};
			vkInvalidateMappedMemoryRanges(_dev, 1, mappedRange);
		}
	}

	#define FIND(v, X)					\
		std::find_if(					\
			v.begin(), v.end(),			\
			[&](const auto& ele) X);	\
		/**/

	IteratorRange<void*>        ResourceMap::GetData(SubResourceId subr)
	{
		auto i = FIND(_subResources, { return ele.first == subr; })
		if (i == _subResources.end())
			Throw(std::runtime_error(StringMeld<256>() << "Requested subresource does not exist or was not mapped: " << subr));
		return MakeIteratorRange(
			PtrAdd(_data, i->second._offset),
			PtrAdd(_data, i->second._offset + i->second._size));
	}

	IteratorRange<const void*>  ResourceMap::GetData(SubResourceId subr) const
	{
		auto i = FIND(_subResources, { return ele.first == subr; })
		if (i == _subResources.end())
			Throw(std::runtime_error(StringMeld<256>() << "Requested subresource does not exist or was not mapped: " << subr));
		return MakeIteratorRange(
			PtrAdd(_data, i->second._offset),
			PtrAdd(_data, i->second._offset + i->second._size));
	}
	TexturePitches				ResourceMap::GetPitches(SubResourceId subr) const
	{
		auto i = FIND(_subResources, { return ele.first == subr; })
		if (i == _subResources.end())
			Throw(std::runtime_error(StringMeld<256>() << "Requested subresource does not exist or was not mapped: " << subr));
		return i->second._pitches;
	}

	IteratorRange<void*>        ResourceMap::GetData() { assert(_subResources.size() == 1); return GetData(SubResourceId{}); }
	IteratorRange<const void*>  ResourceMap::GetData() const { assert(_subResources.size() == 1); return GetData(SubResourceId{}); }
	TexturePitches				ResourceMap::GetPitches() const { assert(_subResources.size() == 1); return GetPitches(SubResourceId{}); }

    ResourceMap::ResourceMap() : _dev(nullptr), _mem(nullptr), _vmaMem(nullptr), _data(nullptr), _dataSize{0} {}

	ResourceMap::~ResourceMap()
	{
		TryUnmap();
	}

	ResourceMap::ResourceMap(ResourceMap&& moveFrom) never_throws
	{
		_data = moveFrom._data; moveFrom._data = nullptr;
		_dataSize = moveFrom._dataSize; moveFrom._dataSize = 0;
		_dev = moveFrom._dev; moveFrom._dev = nullptr;
		_mem = moveFrom._mem; moveFrom._mem = nullptr;
		_vmaMem = moveFrom._vmaMem; moveFrom._vmaMem = nullptr;
		_vmaAllocator = moveFrom._vmaAllocator; moveFrom._vmaAllocator = nullptr;
		_subResources = std::move(moveFrom._subResources);
		_permanentlyMappedResource = moveFrom._permanentlyMappedResource; moveFrom._permanentlyMappedResource = false;
		_resourceOffset = moveFrom._resourceOffset; moveFrom._resourceOffset = 0;
	}

	ResourceMap& ResourceMap::operator=(ResourceMap&& moveFrom) never_throws
	{
		TryUnmap();
		_data = moveFrom._data; moveFrom._data = nullptr;
		_dataSize = moveFrom._dataSize; moveFrom._dataSize = 0;
		_dev = moveFrom._dev; moveFrom._dev = nullptr;
		_mem = moveFrom._mem; moveFrom._mem = nullptr;
		_vmaMem = moveFrom._vmaMem; moveFrom._vmaMem = nullptr;
		_vmaAllocator = moveFrom._vmaAllocator; moveFrom._vmaAllocator = nullptr;
		_subResources = std::move(moveFrom._subResources);
		_permanentlyMappedResource = moveFrom._permanentlyMappedResource; moveFrom._permanentlyMappedResource = false;
		_resourceOffset = moveFrom._resourceOffset; moveFrom._resourceOffset = 0;
		return *this;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	void BlitEncoder::Write(
		const CopyPartial_Dest& dst,
		const SubResourceInitData& srcData,
		Format srcDataFormat,
		VectorPattern<unsigned, 3> srcDataDimensions,
		TexturePitches srcDataPitches)
	{
		// This is a synchronized write, which means it happens in the command list order
		// we need to create a staging resource, fill with the given information, and copy from
		// there via a command on the command list
		// Note that we only change a single subresource with this command

		assert(dst._resource);
		auto desc = dst._resource->GetDesc();
        if (desc._type != ResourceDesc::Type::Texture)
            Throw(std::runtime_error("Non-texture resource type used with texture form of BlitEncoder::Write operation"));

		if (dst._subResource._mip >= desc._textureDesc._mipCount)
            Throw(std::runtime_error("Mipmap index used in BlitEncoder::Write operation is too high"));

        if ((dst._leftTopFront[0]+srcDataDimensions[0]) > desc._textureDesc._width || (dst._leftTopFront[1]+srcDataDimensions[1]) > desc._textureDesc._height)
            Throw(std::runtime_error("Rectangle dimensions used with BlitEncoder::Write operation are outside of the destination texture area"));

		auto srcPixelCount = srcDataDimensions[0] * srcDataDimensions[1] * srcDataDimensions[2];
        if (!srcPixelCount)
            Throw(std::runtime_error("No source pixels in BlitEncoder::Write operation. The depth of the srcDataDimensions field might need to be at least 1."));

		auto expectedSize = BitsPerPixel(srcDataFormat)*srcPixelCount/8;
		if (srcData._data.size() != expectedSize)
			Throw(std::runtime_error("Source data size for BlitEncoder::Write does not match texture dimensions provided"));

		// We never map the destination resource directly here, because this write operation must happen in-order with DeviceContext commands
		auto staging = _devContext->MapTemporaryStorage(expectedSize, BindFlag::TransferSrc);
		assert(staging.GetData().size() == expectedSize);
		std::memcpy(staging.GetData().begin(), srcData._data.begin(), expectedSize);

		Internal::CaptureForBind captureDst(*_devContext, *checked_cast<Resource*>(dst._resource), BindFlag::TransferDst);
		auto src = staging.AsCopySource();
		src.PartialSubresource({0,0,0}, srcDataDimensions, srcDataPitches);
		CopyPartial(*_devContext, dst, src, captureDst.GetLayout(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	}

	void BlitEncoder::Write(
		const CopyPartial_Dest& dst,
		IteratorRange<const void*> srcData)
	{
		assert(dst._resource);
		auto desc = dst._resource->GetDesc();
		if (desc._type != ResourceDesc::Type::LinearBuffer)
			Throw(std::runtime_error("Non-linear buffer resource type used with linear buffer form of BlitEncoder::Write operation"));
		
		assert(dst._leftTopFrontIsLinearBufferOffset || (dst._leftTopFront[0] == 0 && dst._leftTopFront[1] == 0 && dst._leftTopFront[2] == 0));
		assert(dst._subResource._mip == 0 && dst._subResource._arrayLayer == 0);

		// We never map the destination resource directly here, because this write operation must happen in-order with DeviceContext commands
		auto staging = _devContext->MapTemporaryStorage(srcData.size(), BindFlag::TransferSrc);
		assert(staging.GetData().size() == srcData.size());
		std::memcpy(staging.GetData().begin(), srcData.begin(), srcData.size());

		Internal::CaptureForBind captureDst(*_devContext, *checked_cast<Resource*>(dst._resource), BindFlag::TransferDst);
		CopyPartial(*_devContext, dst, staging.AsCopySource(), captureDst.GetLayout(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	}

	void BlitEncoder::Copy(
		const CopyPartial_Dest& dst,
		const CopyPartial_Src& src)
	{
		assert(src._resource && dst._resource);
		if (src._resource != dst._resource) {
			Internal::CaptureForBind captureSrc(*_devContext, *checked_cast<Resource*>(src._resource), BindFlag::TransferSrc);
			Internal::CaptureForBind captureDst(*_devContext, *checked_cast<Resource*>(dst._resource), BindFlag::TransferDst);
			CopyPartial(*_devContext, dst, src, captureDst.GetLayout(), captureSrc.GetLayout());
		} else {
			Internal::CaptureForBind capture(*_devContext, *checked_cast<Resource*>(dst._resource), BindFlag::Enum(BindFlag::TransferSrc|BindFlag::TransferDst));
			CopyPartial(*_devContext, dst, src, capture.GetLayout(), capture.GetLayout());
		}
	}

	void BlitEncoder::Copy(
		IResource& dst,
		IResource& src)
	{
		if (&dst != &src) {
			Internal::CaptureForBind captureSrc(*_devContext, *checked_cast<Resource*>(&src), BindFlag::TransferSrc);
			Internal::CaptureForBind captureDst(*_devContext, *checked_cast<Resource*>(&dst), BindFlag::TransferDst);
			Metal_Vulkan::Copy(*_devContext, *checked_cast<Resource*>(&dst), *checked_cast<Resource*>(&src), captureDst.GetLayout(), captureSrc.GetLayout());
		} else {
			Internal::CaptureForBind capture(*_devContext, *checked_cast<Resource*>(&dst), BindFlag::Enum(BindFlag::TransferSrc|BindFlag::TransferDst));
			Metal_Vulkan::Copy(*_devContext, *checked_cast<Resource*>(&dst), *checked_cast<Resource*>(&src), capture.GetLayout(), capture.GetLayout());
		}
	}

	BlitEncoder::BlitEncoder(DeviceContext& devContext) : _devContext(&devContext)
	{
	}

	BlitEncoder::~BlitEncoder()
	{
		_devContext->EndBlitEncoder();
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		void SetImageLayouts(
			DeviceContext& context, 
			IteratorRange<const LayoutTransition*> changes)
		{
			VkImageMemoryBarrier barriers[16];
			assert(changes.size() > 0 && changes.size() < dimof(barriers));

			VkPipelineStageFlags src_stages = 0;
			VkPipelineStageFlags dest_stages = 0;

			unsigned barrierCount = 0;
			for (unsigned c=0; c<(unsigned)changes.size(); ++c) {
				auto& r = *changes[c]._res;
				assert(r.AccessDesc()._type == ResourceDesc::Type::Texture);
				if (!r.GetImage()) continue;   // (staging buffer case)

				auto& b = barriers[barrierCount++];

				// unforunately, we can't just blanket aspectMask with all bits enabled.
				// We must select a correct aspect mask. The nvidia drivers seem to be fine with all
				// bits enabled, but the documentation says that this is not allowed
				const auto& desc = r.AccessDesc();

				b = {};
				b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				b.pNext = nullptr;
				b.oldLayout = (VkImageLayout)Internal::AsVkImageLayout(changes[c]._oldLayout);
				b.newLayout = (VkImageLayout)Internal::AsVkImageLayout(changes[c]._newLayout);
				b.srcAccessMask = changes[c]._oldAccessMask;
				b.dstAccessMask = changes[c]._newAccessMask;
				b.image = r.GetImage();
				b.subresourceRange.aspectMask = AsImageAspectMask(desc._textureDesc._format);
				b.subresourceRange.baseMipLevel = 0;
				b.subresourceRange.baseArrayLayer = 0;
				b.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
				b.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

				src_stages |= changes[c]._srcStages;
				dest_stages |= changes[c]._dstStages;
			}

			if (barrierCount) {
				context.GetActiveCommandList().PipelineBarrier(
					src_stages, dest_stages,
					0, 
					0, nullptr, 0, nullptr,
					barrierCount, barriers);
			}
		}

		void SetImageLayout(
			DeviceContext& context, Resource& res, 
			ImageLayout oldLayout, unsigned oldAccessMask, unsigned srcStages, 
			ImageLayout newLayout, unsigned newAccessMask, unsigned dstStages)
		{
			LayoutTransition transition { &res, oldLayout, oldAccessMask, srcStages, newLayout, newAccessMask, dstStages };
			SetImageLayouts(context, MakeIteratorRange(&transition, &transition+1));
		}

		class CaptureForBindRecords
		{
		public:
			struct Record { VkImageLayout _layout; unsigned _accessMask; unsigned _stageMask; };
			std::vector<std::pair<uint64_t, Record>> _captures;
		};

		static VkImageLayout GetLayoutForBindType(BindFlag::Enum bindType)
		{
			switch (bindType) {
			case BindFlag::TransferSrc:
				return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			case BindFlag::TransferDst:
				return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			case (BindFlag::Enum)(BindFlag::TransferSrc|BindFlag::TransferDst):
				return VK_IMAGE_LAYOUT_GENERAL;
			case BindFlag::ShaderResource:
			case BindFlag::InputAttachment:
				return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			case BindFlag::UnorderedAccess:
				return VK_IMAGE_LAYOUT_GENERAL;
			case BindFlag::RenderTarget:
				return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			case BindFlag::DepthStencil:
				return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			default:
				return VK_IMAGE_LAYOUT_GENERAL;
			}
		}

		static VkImageLayout SelectDefaultSteadyStateLayout(BindFlag::BitField allBindFlags)
		{
			// For an image with the given bind flags, what should we select as the default "steady state" layout
			// This can be overridden on a per-resource basis, 
			VkImageLayout result = VK_IMAGE_LAYOUT_UNDEFINED;
			if (allBindFlags & BindFlag::ShaderResource) {
				result = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
			if (allBindFlags & BindFlag::InputAttachment) {
				result = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
			if (allBindFlags & BindFlag::UnorderedAccess) {
				result = VK_IMAGE_LAYOUT_GENERAL;
			}
			if (allBindFlags & BindFlag::RenderTarget) {
				// For BindFlag::RenderTarget|BindFlag::ShaderResource, we could pick either states to be the "steady state",
				// but for now the shader resource state works better with the descriptor set binding in AsVkDescriptorImageInfo
				// "General" is probably not really wanted in this case, though
				if (result != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
					result = (result == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
				}
			}
			if (allBindFlags & BindFlag::DepthStencil) {
				// Note that DepthStencilReadOnlyOptimal can't be accessed here
				result = (result == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
			}
			if (allBindFlags & BindFlag::TransferSrc) {
				if (result == VK_IMAGE_LAYOUT_UNDEFINED) {
					result = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				}
			}
			if (allBindFlags & BindFlag::TransferDst) {
				if (result == VK_IMAGE_LAYOUT_UNDEFINED) {
					result = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				}
			}
			return result;
		}

		static BarrierResourceUsage DefaultBarrierResourceUsageFromLayout(VkImageLayout prevLayout)
		{
			// If we know the layout for an image, what is the implied
			// access flags & pipeline state flags to use as the preBarrierUsage in a pipeline barrier?
			// We will sometimes end up with more broad flags here because we know only the layout, we don't
			// have extra context about how the resource was used previously
			BarrierResourceUsage preBarrierUsage;
			preBarrierUsage._imageLayout = prevLayout;
			preBarrierUsage._accessFlags = 0;
			preBarrierUsage._pipelineStageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			switch (prevLayout) {
			default:
				assert(0);
				// intential fall-through
			case VK_IMAGE_LAYOUT_GENERAL:
				preBarrierUsage._accessFlags = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
				break;
			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				preBarrierUsage._accessFlags = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				preBarrierUsage._pipelineStageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				break;
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				preBarrierUsage._accessFlags = VK_ACCESS_SHADER_READ_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				preBarrierUsage._accessFlags = VK_ACCESS_TRANSFER_READ_BIT;
				preBarrierUsage._pipelineStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				preBarrierUsage._accessFlags = VK_ACCESS_TRANSFER_WRITE_BIT;
				preBarrierUsage._pipelineStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;

			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
			case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
				preBarrierUsage._accessFlags = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				preBarrierUsage._pipelineStageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				break;

			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
			case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
			case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
			case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
				preBarrierUsage._accessFlags = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
				preBarrierUsage._pipelineStageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				break;

			case VK_IMAGE_LAYOUT_PREINITIALIZED:
				preBarrierUsage = BarrierResourceUsage::Preinitialized();
				break;
			}
			return preBarrierUsage;
		}

		CaptureForBind::CaptureForBind(DeviceContext& context, IResource& resource, BarrierResourceUsage usage)
		: _context(&context), _resource(&resource)
		{
			auto* res = checked_cast<Resource*>(&resource);

			bool pendingInit = res->_pendingInit;

			// try to mix this with the steady state from the resource
			auto steadyLayout = res->_steadyStateImageLayout;
			bool usingCompatibleSteadyState = false;
			if (!pendingInit 
				&& (steadyLayout == usage._imageLayout || steadyLayout == VK_IMAGE_LAYOUT_GENERAL)) {

				// The steady state is already compatible with what we want
				// we still consider this a capture, but we don't actually have to change the layout or
				// access mode at all
				_capturedLayout = steadyLayout;
				usingCompatibleSteadyState = true;
			} else {
				_capturedLayout = usage._imageLayout;
			}

			_capturedAccessMask = usage._accessFlags;
			_capturedStageMask = usage._pipelineStageFlags;

			if (!context._captureForBindRecords)
				context._captureForBindRecords = std::make_shared<Internal::CaptureForBindRecords>();
			auto existing = LowerBound(context._captureForBindRecords->_captures, res->GetGUID());
			if (existing != context._captureForBindRecords->_captures.end() && existing->first == res->GetGUID()) {
				// We're allowed to nest captures so long as they are of the same type,
				// and we release them in opposite order to creation order (ie shoes and socks order)
				if (existing->second._layout != _capturedLayout)
					Throw(std::runtime_error("Attempting to CaptureForBind a resource that is already captured in another state"));
				_capturedLayout = existing->second._layout;
				return;
			}

			BarrierHelper barrierHelper(context);
			if (pendingInit) {
				// The init operation will normally shift from undefined layout -> steady state
				// We're just going to skip that and jump directly to our captured layout
				res->_pendingInit = false;
				if (res->GetImage()) {
					barrierHelper.Add(*res, BarrierResourceUsage::NoState(), usage);
					_restoreLayout = steadyLayout;
				}
			} else if (!usingCompatibleSteadyState) {
				if (res->GetImage()) {
					barrierHelper.Add(*res, DefaultBarrierResourceUsageFromLayout(steadyLayout), usage);
					_restoreLayout = steadyLayout;
				} else {
					barrierHelper.Add(*res, BarrierResourceUsage::AllCommandsReadAndWrite(), usage);
				}
			}
		}

		CaptureForBind::~CaptureForBind()
		{
			if (_context && _restoreLayout) {
				auto* res = checked_cast<Resource*>(_resource);
				BarrierHelper barrierHelper(*_context);
				BarrierResourceUsage preUsage;
				preUsage._imageLayout = _capturedLayout;
				preUsage._accessFlags = _capturedAccessMask;
				preUsage._pipelineStageFlags = _capturedStageMask;
				barrierHelper.Add(*res, preUsage, DefaultBarrierResourceUsageFromLayout(res->_steadyStateImageLayout));
			}
		}

		void ValidateIsEmpty(CaptureForBindRecords& records)
		{
			// normally we want to return all images to the "steady state" layout at the end of a command list
			// If you hit this, it means the layout was changed via BarrierHelper or CaptureForBind, but wasn't
			// reset before the command list was committed
			assert(records._captures.empty());
		}
	}

	BarrierResourceUsage::BarrierResourceUsage(BindFlag::Enum usage)
	{
		switch (usage) {
		case BindFlag::VertexBuffer:
			_accessFlags = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
			_pipelineStageFlags = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
			break;
		case BindFlag::IndexBuffer:
			_accessFlags = VK_ACCESS_INDEX_READ_BIT;
			_pipelineStageFlags = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
			break;
		case BindFlag::ShaderResource:
			_accessFlags = VK_ACCESS_SHADER_READ_BIT;
			_pipelineStageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			break;
		case BindFlag::RenderTarget:
			_accessFlags = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			_pipelineStageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			break;
		case BindFlag::DepthStencil:
			_accessFlags = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			_pipelineStageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			break;
		case BindFlag::TexelBuffer:
		case BindFlag::UnorderedAccess:
			_accessFlags = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			_pipelineStageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			break;
		case BindFlag::ConstantBuffer:
			_accessFlags = VK_ACCESS_UNIFORM_READ_BIT;
			_pipelineStageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			break;
		case BindFlag::StreamOutput:
			_accessFlags = VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
			_pipelineStageFlags = VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT;
			break;
		case BindFlag::DrawIndirectArgs:
			_accessFlags = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
			_pipelineStageFlags = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
			break;
		case BindFlag::InputAttachment:
			_accessFlags = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
			_pipelineStageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;		// only fragment shader makes sense for input attachment
			break;
		case BindFlag::TransferSrc:
			_accessFlags = VK_ACCESS_TRANSFER_READ_BIT;
			_pipelineStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;		// only fragment shader makes sense for input attachment
			break;
		case BindFlag::TransferDst:
			_accessFlags = VK_ACCESS_TRANSFER_WRITE_BIT;
			_pipelineStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;		// only fragment shader makes sense for input attachment
			break;

		default:
		case BindFlag::PresentationSrc:
		case BindFlag::RawViews:
			assert(0);
			_accessFlags = _pipelineStageFlags = 0;
			break;
		}
		_imageLayout = Internal::GetLayoutForBindType(usage);
	}

	static VkPipelineStageFlags AsPipelineStage(ShaderStage shaderStage)
	{
		switch (shaderStage) {
		case ShaderStage::Vertex: return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
		case ShaderStage::Pixel: return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		case ShaderStage::Geometry: return VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
		case ShaderStage::Compute: return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		case ShaderStage::Hull:
		case ShaderStage::Domain:
		case ShaderStage::Null:
		case ShaderStage::Max:
			assert(0);	// bad shader stage
			return 0;
		}
	}

	BarrierResourceUsage::BarrierResourceUsage(BindFlag::Enum usage, ShaderStage shaderStage)
	{
		switch (usage) {
		case BindFlag::ShaderResource:
			_accessFlags = VK_ACCESS_SHADER_READ_BIT;
			_pipelineStageFlags = AsPipelineStage(shaderStage);
			break;
		case BindFlag::TexelBuffer:
		case BindFlag::UnorderedAccess:
			_accessFlags = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			_pipelineStageFlags = AsPipelineStage(shaderStage);
			break;
		case BindFlag::ConstantBuffer:
			_accessFlags = VK_ACCESS_UNIFORM_READ_BIT;
			_pipelineStageFlags = AsPipelineStage(shaderStage);
			break;

		default:
			*this = BarrierResourceUsage{usage};		// shader stage not required
			break;
		}
		_imageLayout = Internal::GetLayoutForBindType(usage);
	}

	BarrierResourceUsage BarrierResourceUsage::HostRead()
	{
		BarrierResourceUsage result;
		result._accessFlags =  VK_ACCESS_HOST_READ_BIT;
		result._pipelineStageFlags = VK_PIPELINE_STAGE_HOST_BIT;
		result._imageLayout = (VkImageLayout)0;
		return result;
	}

	BarrierResourceUsage BarrierResourceUsage::HostWrite()
	{
		BarrierResourceUsage result;
		result._accessFlags =  VK_ACCESS_HOST_WRITE_BIT;
		result._pipelineStageFlags = VK_PIPELINE_STAGE_HOST_BIT;
		result._imageLayout = (VkImageLayout)0;
		return result;
	}

	BarrierResourceUsage BarrierResourceUsage::AllCommandsRead()
	{
		BarrierResourceUsage result;
		result._accessFlags =  VK_ACCESS_MEMORY_READ_BIT;
		result._pipelineStageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		result._imageLayout = (VkImageLayout)0;
		return result;
	}

	BarrierResourceUsage BarrierResourceUsage::AllCommandsWrite()
	{
		BarrierResourceUsage result;
		result._accessFlags =  VK_ACCESS_MEMORY_WRITE_BIT;
		result._pipelineStageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		result._imageLayout = (VkImageLayout)0;
		return result;
	}

	BarrierResourceUsage BarrierResourceUsage::AllCommandsReadAndWrite()
	{
		BarrierResourceUsage result;
		result._accessFlags =  VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		result._pipelineStageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		result._imageLayout = (VkImageLayout)0;
		return result;
	}

	BarrierResourceUsage BarrierResourceUsage::NoState()
	{
		BarrierResourceUsage result;
		result._accessFlags =  0;
		result._pipelineStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		result._imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		return result;
	}

	BarrierResourceUsage BarrierResourceUsage::Preinitialized()
	{
		BarrierResourceUsage result;
		result._accessFlags =  0;
		result._pipelineStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		result._imageLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		return result;
	}

	BarrierHelper& BarrierHelper::Add(IResource& resource, BarrierResourceUsage preBarrierUsage, BarrierResourceUsage postBarrierUsage)
	{
		auto* res = checked_cast<Resource*>(&resource);
		if (res->GetBuffer()) {
			// barriers from BarrierResourceUsage::NoState() -> some state are not required for buffers (but they are for textures)
			// for API simplicity, let's just bail here for an unrequired buffer initial barrier
			if (preBarrierUsage._accessFlags == 0)
				return *this;
			assert(preBarrierUsage._pipelineStageFlags);
			assert(postBarrierUsage._accessFlags && postBarrierUsage._pipelineStageFlags);

			if (_bufferBarrierCount == dimof(_bufferBarriers)) Flush();
			_bufferBarriers[_bufferBarrierCount++] = {
				VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
				nullptr,
				preBarrierUsage._accessFlags,
				postBarrierUsage._accessFlags,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				checked_cast<Resource*>(&resource)->GetBuffer(),
				0, VK_WHOLE_SIZE
			};
		} else {
			if (_imageBarrierCount == dimof(_imageBarriers)) Flush();
			_imageBarrierGuids[_imageBarrierCount] = { resource.GetGUID(), postBarrierUsage._imageLayout == res->_steadyStateImageLayout };
			auto& barrier = _imageBarriers[_imageBarrierCount++];
			barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.pNext = nullptr;
			barrier.oldLayout = preBarrierUsage._imageLayout;
			barrier.newLayout = postBarrierUsage._imageLayout;
			barrier.srcAccessMask = preBarrierUsage._accessFlags;
			barrier.dstAccessMask = postBarrierUsage._accessFlags;
			barrier.image = checked_cast<Resource*>(&resource)->GetImage();
			barrier.dstQueueFamilyIndex = barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.subresourceRange.aspectMask = AsImageAspectMask(res->AccessDesc()._textureDesc._format);
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
			barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
		}
		_srcStageMask |= preBarrierUsage._pipelineStageFlags;
		_dstStageMask |= postBarrierUsage._pipelineStageFlags;
		return *this;
	}
	void BarrierHelper::Flush()
	{
		if (!_bufferBarrierCount && !_imageBarrierCount) return;
		assert(_deviceContext);
		vkCmdPipelineBarrier(
			_deviceContext->GetActiveCommandList().GetUnderlying().get(),
			_srcStageMask, _dstStageMask,
			0,
			0, nullptr,
			_bufferBarrierCount, _bufferBarriers,
			_imageBarrierCount, _imageBarriers);

		// Call MakeResourcesVisible & record captured layouts for images
		if (_imageBarrierCount) {
			uint64_t makeVisibleGuids[dimof(_imageBarrierGuids)];
			for (unsigned c=0; c<_imageBarrierCount; ++c) makeVisibleGuids[c] = _imageBarrierGuids[c].first;
			_deviceContext->GetActiveCommandList().MakeResourcesVisible(MakeIteratorRange(makeVisibleGuids, &makeVisibleGuids[_imageBarrierCount]));

			if (!_deviceContext->_captureForBindRecords)
				_deviceContext->_captureForBindRecords = std::make_shared<Internal::CaptureForBindRecords>();
			auto& captureRecords = _deviceContext->_captureForBindRecords->_captures;
			for (unsigned c=0; c<_imageBarrierCount; ++c) {
				auto& b = _imageBarriers[c];
				auto i = LowerBound(captureRecords, _imageBarrierGuids[c].first);
				if (i == captureRecords.end() || i->first != _imageBarrierGuids[c].first) {
					if (!_imageBarrierGuids[c].second)
						i = captureRecords.insert(i, {_imageBarrierGuids[c].first, {b.newLayout, b.dstAccessMask, _dstStageMask}});
				} else {
					if (_imageBarrierGuids[c].second) {
						i = captureRecords.erase(i);
					} else {
						i->second = {b.newLayout, b.dstAccessMask, _dstStageMask};
					}
				}
			}
		}

		_bufferBarrierCount = 0;
		_imageBarrierCount = 0;
		_srcStageMask = 0;
		_dstStageMask = 0;
	}
	BarrierHelper::BarrierHelper(IThreadContext& threadContext)
	: _deviceContext{DeviceContext::Get(threadContext).get()} {}
	BarrierHelper::BarrierHelper(DeviceContext& metalContext)
	: _deviceContext{&metalContext} {}
	BarrierHelper::~BarrierHelper() { Flush(); }

}}

