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

namespace RenderCore { namespace Metal_Vulkan
{
	static uint64_t s_nextResourceGUID = 1;

    static VkDevice ExtractUnderlyingDevice(IDevice& idev)
    {
        auto* vulkanDevice = (RenderCore::IDeviceVulkan*)idev.QueryInterface(typeid(RenderCore::IDeviceVulkan).hash_code());
		return vulkanDevice ? vulkanDevice->GetUnderlyingDevice() : nullptr;
    }

	static unsigned CopyViaMemoryMap(
		VkDevice device, VkImage image, VkDeviceMemory mem,
		const TextureDesc& desc,
		const std::function<SubResourceInitData(SubResourceId)>& initData);

	static unsigned CopyViaMemoryMap(
		IDevice& dev, Resource& resource,
		const std::function<SubResourceInitData(SubResourceId)>& initData);

	static void CopyPartial(
        DeviceContext& context, 
        const BlitEncoder::CopyPartial_Dest& dst, const BlitEncoder::CopyPartial_Src& src,
        Internal::ImageLayout dstLayout, Internal::ImageLayout srcLayout);

	static void Copy(DeviceContext& context, Resource& dst, Resource& src, Internal::ImageLayout dstLayout, Internal::ImageLayout srcLayout);

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
				assert(r.GetDesc()._type == ResourceDesc::Type::Texture);
				if (!r.GetImage()) continue;   // (staging buffer case)

				auto& b = barriers[barrierCount++];

				// unforunately, we can't just blanket aspectMask with all bits enabled.
				// We must select a correct aspect mask. The nvidia drivers seem to be fine with all
				// bits enabled, but the documentation says that this is not allowed
				const auto& desc = r.GetDesc();

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
			struct Record { IResource* _resource; ImageLayout _layout; unsigned _accessMask; unsigned _stageMask; };
			std::vector<Record> _captures;
		};

		struct ImageLayoutMode
		{
			ImageLayout _optimalLayout;
			unsigned _accessFlags;
			unsigned _pipelineStageFlags;
		};

		ImageLayoutMode GetLayoutForBindType(BindFlag::Enum bindType)
		{
			switch (bindType) {
			case BindFlag::TransferSrc:
				return { ImageLayout::TransferSrcOptimal, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };
			case BindFlag::TransferDst:
				return { ImageLayout::TransferDstOptimal, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };
			case BindFlag::ShaderResource:
				// VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
				// VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT
				// VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT
				// VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT
				// VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
				// VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
				return { 
					ImageLayout::ShaderReadOnlyOptimal,
					VK_ACCESS_SHADER_READ_BIT,
					VK_PIPELINE_STAGE_VERTEX_SHADER_BIT /*| VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT*/
					| VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
				};
			case BindFlag::InputAttachment:
				return { ImageLayout::ShaderReadOnlyOptimal, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT };	// can only read from input attachments from the fragment shader
			case BindFlag::UnorderedAccess:
				return { 
					ImageLayout::General,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
					VK_PIPELINE_STAGE_VERTEX_SHADER_BIT /*| VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT*/
					| VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
				};
			case BindFlag::RenderTarget:
				return { ImageLayout::ColorAttachmentOptimal, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
			case BindFlag::DepthStencil:
				return { ImageLayout::DepthStencilAttachmentOptimal, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT };
			default:
				assert(0);
				return { ImageLayout::General, 0, 0 };
			}
		}

		CaptureForBind::CaptureForBind(DeviceContext& context, IResource& resource, BindFlag::Enum bindType)
		: _context(&context), _resource(&resource), _bindType(bindType), _releaseCapture(true), _usingCompatibleSteadyState(false)
		{
			if (!context._captureForBindRecords)
				context._captureForBindRecords = std::make_shared<Internal::CaptureForBindRecords>();

			auto newMode = Internal::GetLayoutForBindType(bindType);
			auto* res = checked_cast<Resource*>(&resource);

			bool pendingInit = (bool)res->_pendingInitialization;

			// try to mix this with the steady state from the resource
			auto steadyLayout = res->_steadyStateLayout;
			auto steadyAccessMask = res->_steadyStateAccessMask;
			if (!pendingInit 
				&& (steadyLayout == newMode._optimalLayout || steadyLayout == Internal::ImageLayout::General) 
				&& (res->_steadyStateAccessMask & newMode._accessFlags) == newMode._accessFlags) {

				// The steady state is already compatible with what we want
				// we still consider this a capture, but we don't actually have to change the layout or
				// access mode at all
				_capturedLayout = steadyLayout;
				_capturedAccessMask = steadyAccessMask;
				_capturedStageMask = res->_steadyStateAssociatedStageMask;
				_usingCompatibleSteadyState = true;
			} else {
				// We do have to change the layout. Prefer to swap to the optimal layout if we can
				_capturedLayout = newMode._optimalLayout;
				_capturedAccessMask = newMode._accessFlags;
				_capturedStageMask = newMode._pipelineStageFlags;
			}

			auto existing = std::find_if(
				context._captureForBindRecords->_captures.begin(), context._captureForBindRecords->_captures.end(),
				[&resource](const auto& i) { return i._resource == &resource; });
			if (existing != context._captureForBindRecords->_captures.end()) {
				// We're allowed to nest captures so long as they are of the same type,
				// and we release them in opposite order to creation order (ie shoes and socks order)
				if (existing->_layout != _capturedLayout)
					Throw(std::runtime_error("Attempting to CaptureForBind a resource that is already captured in another state"));
				_capturedLayout = existing->_layout;
				_capturedAccessMask = existing->_accessMask;
				_capturedStageMask = existing->_stageMask;
				_releaseCapture = false;
				return;
			}

			context._captureForBindRecords->_captures.push_back(
				{&resource, _capturedLayout, _capturedAccessMask, _capturedStageMask});

			if (!_usingCompatibleSteadyState) {
				if (!pendingInit) {
					if (res->GetImage())
						Internal::SetImageLayout(
							*_context, *res,
							res->_steadyStateLayout, res->_steadyStateAccessMask, res->_steadyStateAssociatedStageMask,
							_capturedLayout, _capturedAccessMask, newMode._pipelineStageFlags);
				} else {
					// The init operation will normally shift from undefined layout -> steady state
					// We're just going to skip that and jump directly to our captured layout
					res->_pendingInitialization = {};
					if (res->GetImage())
						Internal::SetImageLayout(
							*_context, *res,
							Internal::ImageLayout::Undefined, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
							_capturedLayout, _capturedAccessMask, newMode._pipelineStageFlags);
				}
			}
		}

		CaptureForBind::~CaptureForBind()
		{
			if (!_context) return;

			auto* res = checked_cast<Resource*>(_resource);
			auto existing = std::find_if(
				_context->_captureForBindRecords->_captures.begin(), _context->_captureForBindRecords->_captures.end(),
				[res](const auto& i) { return i._resource == res; });
			if (existing == _context->_captureForBindRecords->_captures.end()) {
				// You might get here if you have multiple nested CaptureForBind with the same constructor parameters, 
				// but they get destroyed in the wrong order
				Log(Error) << "Missing capture record in CaptureForBind destructor" << std::endl;
				assert(0);
			} else {
				if (existing->_layout != _capturedLayout || existing->_accessMask != _capturedAccessMask)
					Log(Error) << "Capture record has unexpected type in CaptureForBind destructor" << std::endl;	// Likewise this might be caused by a complex set of nested captures that are destroyed in an incorrect order
				if (_releaseCapture)
					_context->_captureForBindRecords->_captures.erase(existing);
			}

			// always return back to the "steady state" layout for this resource
			if (!_usingCompatibleSteadyState) {
				if (res->GetImage())
					Internal::SetImageLayout(
						*_context, *res,
						_capturedLayout, _capturedAccessMask, _capturedStageMask,
						res->_steadyStateLayout, res->_steadyStateAccessMask, res->_steadyStateAssociatedStageMask);
			}
		}

		void ValidateIsEmpty(CaptureForBindRecords& records)
		{
			assert(records._captures.empty());
		}

		class ResourceInitializationHelper
		{
		public:
			DeviceContext& GetDeviceContext() { return *_devContext; }

			void SetImageLayout(
				Resource& res, 
				ImageLayout oldLayout, unsigned oldAccessMask, unsigned srcStages, 
				ImageLayout newLayout, unsigned newAccessMask, unsigned dstStages)
			{
				_transitions.push_back({
					&res, 
					oldLayout, oldAccessMask, srcStages,
					newLayout, newAccessMask, dstStages});
			}

			void MakeResourceVisible(uint64_t res)
			{
				_makeResourcesVisible.push_back(res);
			}

			void CommitLayoutChanges()
			{
				if (!_transitions.empty())
					SetImageLayouts(*_devContext, MakeIteratorRange(_transitions));
				if (!_makeResourcesVisible.empty())
					_devContext->GetActiveCommandList().MakeResourcesVisible(MakeIteratorRange(_makeResourcesVisible));
			}

			ResourceInitializationHelper(DeviceContext& devContext) : _devContext(&devContext) {}
		private:
			DeviceContext* _devContext;
			std::vector<LayoutTransition> _transitions;
			std::vector<uint64_t> _makeResourcesVisible;
		};
	}

    static VulkanSharedPtr<VkDeviceMemory> AllocateDeviceMemory(
        const Metal_Vulkan::ObjectFactory& factory, VkMemoryRequirements memReqs, VkMemoryPropertyFlags requirementMask)
    {
        auto type = factory.FindMemoryType(memReqs.memoryTypeBits, requirementMask);
        if (type >= 32)
            Throw(Exceptions::BasicLabel("Could not find compatible memory type for image"));
        return factory.AllocateMemory(memReqs.size, type);
    }

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

		_steadyStateLayout = Internal::ImageLayout::Undefined;
		_steadyStateAccessMask = 0;

		VkMemoryRequirements mem_reqs = {}; 
		if (desc._type == Desc::Type::LinearBuffer) {
			VkBufferCreateInfo buf_info = {};
			buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			buf_info.pNext = nullptr;
			buf_info.usage = AsBufferUsageFlags(desc._bindFlags);
			buf_info.size = desc._linearBufferDesc._sizeInBytes;
			buf_info.queueFamilyIndexCount = 0;
			buf_info.pQueueFamilyIndices = nullptr;
			buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;   // sharing between queues
			buf_info.flags = 0;     // flags for sparse buffers

                    // set this flag to enable usage with "vkCmdUpdateBuffer"
			if ((desc._cpuAccess & CPUAccess::WriteDynamic) == CPUAccess::WriteDynamic)
				buf_info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

			assert(buf_info.usage != 0);
			_underlyingBuffer = factory.CreateBuffer(buf_info);

			vkGetBufferMemoryRequirements(factory.GetDevice().get(), _underlyingBuffer.get(), &mem_reqs);

			#if defined(_DEBUG)
				if (factory.GetExtensionFunctions()._setObjectName && desc._name[0]) {
					const VkDebugUtilsObjectNameInfoEXT imageNameInfo {
						VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, // sType
						NULL,                                               // pNext
						VK_OBJECT_TYPE_BUFFER,                              // objectType
						(uint64_t)_underlyingBuffer.get(),                  // object
						desc._name
					};
					factory.GetExtensionFunctions()._setObjectName(factory.GetDevice().get(), &imageNameInfo);
				}
			#endif

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
			image_create_info.mipLevels = std::max(1u, unsigned(tDesc._mipCount));
			image_create_info.arrayLayers = std::max(1u, unsigned(tDesc._arrayCount));
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
			image_create_info.tiling = (desc._cpuAccess != 0 || hasInitData) ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
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

            if (image_create_info.tiling == VK_IMAGE_TILING_LINEAR && (desc._gpuAccess == 0)) {

                VkBufferCreateInfo buf_info = {};
			    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			    buf_info.pNext = nullptr;
			    buf_info.usage = AsBufferUsageFlags(desc._bindFlags);
			    buf_info.size = ByteCount(tDesc);
			    buf_info.queueFamilyIndexCount = 0;
			    buf_info.pQueueFamilyIndices = nullptr;
			    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			    buf_info.flags = 0;

                _underlyingBuffer = factory.CreateBuffer(buf_info);
    			vkGetBufferMemoryRequirements(factory.GetDevice().get(), _underlyingBuffer.get(), &mem_reqs);

            } else {
    			_underlyingImage = factory.CreateImage(image_create_info, _guid);
	    		vkGetImageMemoryRequirements(factory.GetDevice().get(), _underlyingImage.get(), &mem_reqs);
            }

			ConfigureDefaultSteadyState(desc._bindFlags);

			// queue transition into our steady-state
			_pendingInitialization = [](Internal::ResourceInitializationHelper& helper, Resource& res) {
					helper.SetImageLayout(
						res,
						Internal::ImageLayout::Undefined, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
						res._steadyStateLayout, res._steadyStateAccessMask, res._steadyStateAssociatedStageMask);
					helper.MakeResourceVisible(res.GetGUID());
				};

			#if defined(_DEBUG)
				if (factory.GetExtensionFunctions()._setObjectName && desc._name[0]) {
					const VkDebugUtilsObjectNameInfoEXT imageNameInfo {
						VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, // sType
						NULL,                                               // pNext
						(_underlyingImage != nullptr) ? VK_OBJECT_TYPE_IMAGE : VK_OBJECT_TYPE_BUFFER,							// objectType
						(_underlyingImage != nullptr) ? (uint64_t)_underlyingImage.get() : (uint64_t)_underlyingBuffer.get(),	// object
						desc._name
					};
					factory.GetExtensionFunctions()._setObjectName(factory.GetDevice().get(), &imageNameInfo);
				}
			#endif

			#if defined(TRACK_RESOURCE_GUIDS)
				AssociateResourceGUID(_guid, desc._name);
			#endif
		}

        const auto hostVisibleReqs = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        auto memoryRequirements = (hasInitData || desc._cpuAccess != 0) ? hostVisibleReqs : 0;
        _mem = AllocateDeviceMemory(factory, mem_reqs, memoryRequirements);

		const VkDeviceSize offset = 0; 
		if (_underlyingBuffer) {

			// After allocation, we must initialise the data. True linear buffers don't have subresources,
			// so it's reasonably easy. But if this buffer is really a staging texture, then we need to 
            // copy in all of the sub resources.
			if (hasInitData) {
                if (desc._type == Desc::Type::LinearBuffer) {
                    auto subResData = initData({0, 0});
				    if (subResData._data.size()) {
					    ResourceMap map(factory.GetDevice().get(), _mem.get());
					    std::memcpy(map.GetData().begin(), subResData._data.begin(), std::min(subResData._data.size(), (size_t)mem_reqs.size));
				    }
                } else {
                    // This is the staging texture path. Rather that getting the arrangement of subresources from
                    // the VkImage, we specify it ourselves.
                    CopyViaMemoryMap(
                        factory.GetDevice().get(), nullptr, _mem.get(), 
                        _desc._textureDesc, initData);
                }
			}

			auto res = vkBindBufferMemory(factory.GetDevice().get(), _underlyingBuffer.get(), _mem.get(), offset);
			if (res != VK_SUCCESS)
				Throw(VulkanAPIFailure(res, "Failed while binding a buffer to device memory"));
		} else if (_underlyingImage) {
			
			// Initialisation for images is more complex... We just iterate through each subresource
			// and copy in the data. We're going to do this with a single map -- assuming that all
            // if any of the subresources have init data, then they must all have. 
            // Alternatively, we could map each subresource separately... But there doesn't seem to
            // be any advantage to that.
			if (hasInitData) {
                CopyViaMemoryMap(
                    factory.GetDevice().get(), _underlyingImage.get(), _mem.get(), 
                    _desc._textureDesc, initData);
			}

			auto res = vkBindImageMemory(factory.GetDevice().get(), _underlyingImage.get(), _mem.get(), 0);
			if (res != VK_SUCCESS)
				Throw(VulkanAPIFailure(res, "Failed while binding an image to device memory"));
		}
	}

	void Resource::ConfigureDefaultSteadyState(BindFlag::BitField bindFlags)
	{
		// Determine the steady state layout for this resource. If we have only one
		// usage type selected, we will default to the optimal layout for that usage method.
		// Otherwise we will fall back to "general"
		// However, there's an exception for TransferDst; since most textures will have this just to
		// fill with initial data
		_steadyStateLayout = Internal::ImageLayout::Undefined;
		_steadyStateAccessMask = 0;
		_steadyStateAssociatedStageMask = 0;
		using lyt = Internal::ImageLayout;
		if (bindFlags & BindFlag::ShaderResource) {
			_steadyStateLayout = lyt::ShaderReadOnlyOptimal;
			_steadyStateAccessMask |= Internal::GetLayoutForBindType(BindFlag::ShaderResource)._accessFlags;
			_steadyStateAssociatedStageMask |= Internal::GetLayoutForBindType(BindFlag::ShaderResource)._pipelineStageFlags;
		}
		if (bindFlags & BindFlag::InputAttachment) {
			_steadyStateLayout = lyt::ShaderReadOnlyOptimal;
			_steadyStateAccessMask |= Internal::GetLayoutForBindType(BindFlag::InputAttachment)._accessFlags;
			_steadyStateAssociatedStageMask |= Internal::GetLayoutForBindType(BindFlag::InputAttachment)._pipelineStageFlags;
		}
		if (bindFlags & BindFlag::UnorderedAccess) {
			_steadyStateLayout = lyt::General;
			_steadyStateAccessMask |= Internal::GetLayoutForBindType(BindFlag::UnorderedAccess)._accessFlags;
			_steadyStateAssociatedStageMask |= Internal::GetLayoutForBindType(BindFlag::UnorderedAccess)._pipelineStageFlags;
		}
		if (bindFlags & BindFlag::RenderTarget) {
			_steadyStateLayout = (_steadyStateLayout == lyt::Undefined) ? lyt::ColorAttachmentOptimal : lyt::General;
			_steadyStateAccessMask |= Internal::GetLayoutForBindType(BindFlag::RenderTarget)._accessFlags;
			_steadyStateAssociatedStageMask |= Internal::GetLayoutForBindType(BindFlag::RenderTarget)._pipelineStageFlags;
		}
		if (bindFlags & BindFlag::DepthStencil) {
			// Note that DepthStencilReadOnlyOptimal can't be accessed here
			_steadyStateLayout = (_steadyStateLayout == lyt::Undefined) ? lyt::DepthStencilAttachmentOptimal : lyt::General;
			_steadyStateAccessMask |= Internal::GetLayoutForBindType(BindFlag::DepthStencil)._accessFlags;
			_steadyStateAssociatedStageMask |= Internal::GetLayoutForBindType(BindFlag::DepthStencil)._pipelineStageFlags;
		}
		if (bindFlags & BindFlag::TransferSrc) {
			_steadyStateLayout = (_steadyStateLayout == lyt::Undefined) ? lyt::TransferSrcOptimal : lyt::General;
			_steadyStateAccessMask |= Internal::GetLayoutForBindType(BindFlag::TransferSrc)._accessFlags;
			_steadyStateAssociatedStageMask |= Internal::GetLayoutForBindType(BindFlag::TransferSrc)._pipelineStageFlags;
		}
		if (bindFlags & BindFlag::TransferDst) {
			// Note the exception for _steadyStateLayout for TransferDst
			if (_steadyStateLayout == lyt::Undefined) {
				_steadyStateLayout = lyt::TransferDstOptimal;
				_steadyStateAccessMask |= Internal::GetLayoutForBindType(BindFlag::TransferDst)._accessFlags;
				_steadyStateAssociatedStageMask |= Internal::GetLayoutForBindType(BindFlag::TransferDst)._pipelineStageFlags;
			}
		}
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
		bool requiresDestaging = !_desc._cpuAccess;
		if (requiresDestaging) {
			// todo -- we could destaging only a single sub resource...?
			auto stagingCopyDesc = _desc;
			stagingCopyDesc._gpuAccess = 0;
			stagingCopyDesc._cpuAccess = CPUAccess::Read;
			stagingCopyDesc._bindFlags = BindFlag::TransferDst;
			Resource destaging { GetObjectFactory(), stagingCopyDesc };

			auto& ctx = *DeviceContext::Get(context);
			{
				CompleteInitialization(ctx, {(IResource*)&destaging});
				Internal::CaptureForBind capture(ctx, *const_cast<Resource*>(this), BindFlag::TransferSrc);
				Copy(ctx, destaging, *const_cast<Resource*>(this), destaging._steadyStateLayout, capture.GetLayout());
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

		auto* vulkanDevice = (IDeviceVulkan*)context.GetDevice()->QueryInterface(typeid(IDeviceVulkan).hash_code());
		assert(vulkanDevice);
		ResourceMap map(
			vulkanDevice->GetUnderlyingDevice(),
			*const_cast<Resource*>(this),
			ResourceMap::Mode::Read,
			subRes);

		return std::vector<uint8_t>{
			(const uint8_t*)map.GetData(subRes).begin(),
			(const uint8_t*)map.GetData(subRes).end()};
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

		ConfigureDefaultSteadyState(desc._bindFlags);
		_steadyStateLayout = Internal::ImageLayout::PresentSrc;

		_pendingInitialization = [](Internal::ResourceInitializationHelper& helper, Resource& res) {
			helper.SetImageLayout(
				res,
				Internal::ImageLayout::Undefined, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				res._steadyStateLayout, res._steadyStateAccessMask, res._steadyStateAssociatedStageMask);
			helper.MakeResourceVisible(res.GetGUID());
		};
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
		assert(bufferDesc._type == Resource::Desc::Type::Texture);

		auto arrayCount = std::max(1u, (unsigned)imageDesc._textureDesc._arrayCount);
		auto mips = std::max(1u, (unsigned)std::min(imageDesc._textureDesc._mipCount, bufferDesc._textureDesc._mipCount));
		unsigned width = imageDesc._textureDesc._width, height = imageDesc._textureDesc._height, depth = imageDesc._textureDesc._depth;
		auto minDims = (GetCompressionType(imageDesc._textureDesc._format) == FormatCompressionType::BlockCompression) ? 4u : 1u;
		auto dstAspectMask = AsImageAspectMask(bufferDesc._textureDesc._format);

		assert(bufferDesc._textureDesc._width == width);
		assert(bufferDesc._textureDesc._height == height);
		assert(bufferDesc._textureDesc._depth == depth);

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
				c.imageExtent = VkExtent3D{width, height, std::max(depth, 1u)};
			}

			width >>= 1u;
			height >>= 1u;
			depth >>= 1u;
		}

		return result;
	}

	static void Copy(DeviceContext& context, Resource& dst, Resource& src, Internal::ImageLayout dstLayout, Internal::ImageLayout srcLayout)
	{
        if (dst.GetImage() && src.GetImage()) {
            // image to image copy

            // Each mipmap is treated as a separate copy operation (but multiple array layers can be handled
		    // in a single operation).
		    // The Vulkan API requires that the formats of each resource must be reasonably similiar
		    //		-- in practice, that means that the size of the pixels in both cases must be the same.
		    //		When copying between compressed and uncompressed images, the uncompressed pixel size must
		    //		be equal to the compressed block size.

		    const auto& srcDesc = src.GetDesc();
		    const auto& dstDesc = dst.GetDesc();
		    assert(srcDesc._type == Resource::Desc::Type::Texture);
		    assert(dstDesc._type == Resource::Desc::Type::Texture);

		    VkImageCopy copyOps[16];

		    unsigned copyOperations = 0;
		    auto dstAspectMask = AsImageAspectMask(dstDesc._textureDesc._format);
		    auto srcAspectMask = AsImageAspectMask(srcDesc._textureDesc._format);

		    assert(std::max(1u, (unsigned)srcDesc._textureDesc._arrayCount) == std::max(1u, (unsigned)dstDesc._textureDesc._arrayCount));
		
		    auto mips = std::max(1u, (unsigned)std::min(srcDesc._textureDesc._mipCount, dstDesc._textureDesc._mipCount));
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
			    src.GetImage(), (VkImageLayout)Internal::AsVkImageLayout(srcLayout),
			    dst.GetImage(), (VkImageLayout)Internal::AsVkImageLayout(dstLayout),
			    copyOperations, copyOps);

        } else if (dst.GetBuffer() && src.GetBuffer()) {
            // buffer to buffer copy
            const auto& srcDesc = src.GetDesc();
		    const auto& dstDesc = dst.GetDesc();
            assert(srcDesc._type == Resource::Desc::Type::LinearBuffer);
		    assert(dstDesc._type == Resource::Desc::Type::LinearBuffer);
            VkBufferCopy copyOps[] = 
            {
                VkBufferCopy{0, 0, std::min(srcDesc._linearBufferDesc._sizeInBytes, dstDesc._linearBufferDesc._sizeInBytes)}
            };
            context.GetActiveCommandList().CopyBuffer(
                src.GetBuffer(),
                dst.GetBuffer(),
                dimof(copyOps), copyOps);
        } else if (dst.GetImage() && src.GetBuffer()) {
            // This copy operation is typically used when initializing a texture via staging
            // resource. The buffer probably has a "Texture" type Desc, even though the underlying
            // resource is a buffer.
            if (src.GetDesc()._type != ResourceDesc::Type::Texture)
                Throw(::Exceptions::BasicLabel("Buffer to image copy not implemented, except for staging resources"));

            auto copyOps = GenerateBufferImageCopyOps(src.GetDesc(), dst.GetDesc());
            context.GetActiveCommandList().CopyBufferToImage(
                src.GetBuffer(),
                dst.GetImage(), (VkImageLayout)Internal::AsVkImageLayout(dstLayout),
                (uint32_t)copyOps.size(), copyOps.data());
        } else {
            if (dst.GetDesc()._type != ResourceDesc::Type::Texture)
                Throw(::Exceptions::BasicLabel("Image to buffer copy not implemented, except for destaging resources"));
            
            auto copyOps = GenerateBufferImageCopyOps(dst.GetDesc(), src.GetDesc());
            context.GetActiveCommandList().CopyImageToBuffer(
                src.GetImage(), (VkImageLayout)Internal::AsVkImageLayout(srcLayout),
				dst.GetBuffer(),
                (uint32_t)copyOps.size(), copyOps.data());
        }
	}

    static void CopyPartial(
        DeviceContext& context, 
        const BlitEncoder::CopyPartial_Dest& dst, const BlitEncoder::CopyPartial_Src& src,
        Internal::ImageLayout dstLayout, Internal::ImageLayout srcLayout)
    {
        assert(src._resource && dst._resource);
		auto dstResource = checked_cast<Resource*>(dst._resource);
		auto srcResource = checked_cast<Resource*>(src._resource);
        if (dstResource->GetImage() && srcResource->GetImage()) {
            // image to image copy
            // In this case, we're going to generate only a single copy operation. This is 
            // similar to CopySubresourceRegion in D3D

            const auto& srcDesc = srcResource->GetDesc();
		    const auto& dstDesc = dstResource->GetDesc();
		    assert(srcDesc._type == Resource::Desc::Type::Texture);
		    assert(dstDesc._type == Resource::Desc::Type::Texture);

		    auto dstAspectMask = AsImageAspectMask(dstDesc._textureDesc._format);
		    auto srcAspectMask = AsImageAspectMask(srcDesc._textureDesc._format);

            VkImageCopy c;
            c.srcOffset = VkOffset3D { (int)src._leftTopFront._values[0], (int)src._leftTopFront._values[1], (int)src._leftTopFront._values[2] };
			c.dstOffset = VkOffset3D { (int)dst._leftTopFront._values[0], (int)dst._leftTopFront._values[1], (int)dst._leftTopFront._values[2] };
			c.extent = VkExtent3D { 
                src._rightBottomBack._values[0] - src._leftTopFront._values[0],
                src._rightBottomBack._values[1] - src._leftTopFront._values[1],
                src._rightBottomBack._values[2] - src._leftTopFront._values[2]
            };
			c.srcSubresource.aspectMask = srcAspectMask;
			c.srcSubresource.mipLevel = src._subResource._mip;
			c.srcSubresource.baseArrayLayer = src._subResource._arrayLayer;
			c.srcSubresource.layerCount = 1;
			c.dstSubresource.aspectMask = dstAspectMask;
			c.dstSubresource.mipLevel = dst._subResource._mip;
			c.dstSubresource.baseArrayLayer = dst._subResource._arrayLayer;
			c.dstSubresource.layerCount = 1;

            context.GetActiveCommandList().CopyImage(
			    srcResource->GetImage(), (VkImageLayout)Internal::AsVkImageLayout(srcLayout),
			    dstResource->GetImage(), (VkImageLayout)Internal::AsVkImageLayout(dstLayout),
			    1, &c);
        } else if (dstResource->GetBuffer() && srcResource->GetBuffer()) {
            // buffer to buffer copy
            const auto& srcDesc = src._resource->GetDesc();
		    const auto& dstDesc = dst._resource->GetDesc();
            assert(srcDesc._type == Resource::Desc::Type::Texture);
		    assert(dstDesc._type == Resource::Desc::Type::Texture);
            VkBufferCopy c;
            c.srcOffset = src._leftTopFront._values[0];
            c.dstOffset = dst._leftTopFront._values[0];
            auto end = std::min(src._rightBottomBack._values[0], std::min(srcDesc._linearBufferDesc._sizeInBytes, dstDesc._linearBufferDesc._sizeInBytes));
            c.size = end - src._rightBottomBack._values[0];
            context.GetActiveCommandList().CopyBuffer(
                srcResource->GetBuffer(),
                dstResource->GetBuffer(),
                1, &c);
        } else if (dstResource->GetImage() && srcResource->GetBuffer()) {
            // This copy operation is typically used when initializing a texture via staging
            // resource. The buffer probably has a "Texture" type Desc, even though the underlying
            // resource is a buffer.
            if (src._resource->GetDesc()._type != ResourceDesc::Type::Texture)
                Throw(::Exceptions::BasicLabel("Buffer to image copy not implemented, except for staging resources"));

            const auto& srcDesc = src._resource->GetDesc();
		    const auto& dstDesc = dstResource->GetDesc();
		    assert(srcDesc._type == Resource::Desc::Type::Texture);
		    assert(dstDesc._type == Resource::Desc::Type::Texture);

            auto dstAspectMask = AsImageAspectMask(dstDesc._textureDesc._format);

            VkBufferImageCopy copyOps[8];
            auto arrayCount = std::max(1u, (unsigned)srcDesc._textureDesc._arrayCount);
		    auto mips = std::max(1u, (unsigned)std::min(srcDesc._textureDesc._mipCount, dstDesc._textureDesc._mipCount));
            unsigned width = srcDesc._textureDesc._width, height = srcDesc._textureDesc._height, depth = srcDesc._textureDesc._depth;
            auto minDims = (GetCompressionType(srcDesc._textureDesc._format) == FormatCompressionType::BlockCompression) ? 4u : 1u;

            assert(dstDesc._textureDesc._width >= width);
            assert(dstDesc._textureDesc._height >= height);
            assert(dstDesc._textureDesc._depth == depth);
		    assert(mips*arrayCount <= dimof(copyOps));

            // todo -- not adjusting the offsets/extents for mipmaps. This won't
            // work correctly in the mipmapped case.
            assert(mips <= 1);
            for (unsigned m=0; m<mips; ++m) {
                auto mipOffset = GetSubResourceOffset(srcDesc._textureDesc, m, 0);
                for (unsigned a=0; a<arrayCount; ++a) {
                    auto& c = copyOps[m+a*mips];
                    c.bufferOffset = mipOffset._offset + mipOffset._pitches._arrayPitch * a;
                    if (src._leftTopFront[0] != ~0u) {
                        c.bufferOffset += 
                              src._leftTopFront[2] * mipOffset._pitches._slicePitch
                            + src._leftTopFront[1] * mipOffset._pitches._rowPitch
                            + src._leftTopFront[0] * BitsPerPixel(srcDesc._textureDesc._format) / 8;
                    }
                    c.bufferRowLength = std::max(width, minDims);
                    c.bufferImageHeight = std::max(height, minDims);

                    c.imageSubresource = VkImageSubresourceLayers{ dstAspectMask, m, a, 1 };
                    c.imageOffset = VkOffset3D{(int32_t)dst._leftTopFront[0], (int32_t)dst._leftTopFront[1], (int32_t)dst._leftTopFront[2]};

                    if (src._leftTopFront[0] != ~0u && src._rightBottomBack[0] != ~0u) {
                        c.imageExtent = VkExtent3D{
                            src._rightBottomBack[0] - src._leftTopFront[0],
                            src._rightBottomBack[1] - src._leftTopFront[1],
                            src._rightBottomBack[2] - src._leftTopFront[2]};
                    } else {
                        c.imageExtent = VkExtent3D{
                            srcDesc._textureDesc._width,
                            srcDesc._textureDesc._height,
                            std::max(srcDesc._textureDesc._depth, 1u)};
                    }
                }

                width >>= 1u;
                height >>= 1u;
                depth >>= 1u;
            }

            const auto copyOperations = mips*arrayCount;
            context.GetActiveCommandList().CopyBufferToImage(
                srcResource->GetBuffer(),
                dstResource->GetImage(), (VkImageLayout)Internal::AsVkImageLayout(dstLayout),
                copyOperations, copyOps);
		} else if (dstResource->GetBuffer() && srcResource->GetImage()) {
            if (dst._resource->GetDesc()._type != ResourceDesc::Type::Texture)
                Throw(::Exceptions::BasicLabel("Image to buffer copy not implemented, except for staging resources"));

			const auto& srcDesc = src._resource->GetDesc();
		    const auto& dstDesc = dstResource->GetDesc();
		    assert(srcDesc._type == Resource::Desc::Type::Texture);
		    assert(dstDesc._type == Resource::Desc::Type::Texture);

            auto srcAspectMask = AsImageAspectMask(srcDesc._textureDesc._format);

            VkBufferImageCopy copyOps[8];
            auto arrayCount = std::max(1u, (unsigned)srcDesc._textureDesc._arrayCount);
		    auto mips = std::max(1u, (unsigned)std::min(srcDesc._textureDesc._mipCount, dstDesc._textureDesc._mipCount));
            unsigned width = srcDesc._textureDesc._width, height = srcDesc._textureDesc._height, depth = srcDesc._textureDesc._depth;
            auto minDims = (GetCompressionType(srcDesc._textureDesc._format) == FormatCompressionType::BlockCompression) ? 4u : 1u;

            assert(dstDesc._textureDesc._width >= width);
            assert(dstDesc._textureDesc._height >= height);
            assert(dstDesc._textureDesc._depth == depth);
		    assert(mips*arrayCount <= dimof(copyOps));

            // todo -- not adjusting the offsets/extents for mipmaps. This won't
            // work correctly in the mipmapped case.
            assert(mips <= 1);
            for (unsigned m=0; m<mips; ++m) {
                auto mipOffset = GetSubResourceOffset(dstDesc._textureDesc, m, 0);
                for (unsigned a=0; a<arrayCount; ++a) {
                    auto& c = copyOps[m+a*mips];
                    c.bufferOffset = mipOffset._offset + mipOffset._pitches._arrayPitch * a;
                    if (dst._leftTopFront[0] != ~0u) {
                        c.bufferOffset += 
                              dst._leftTopFront[2] * mipOffset._pitches._slicePitch
                            + dst._leftTopFront[1] * mipOffset._pitches._rowPitch
                            + dst._leftTopFront[0] * BitsPerPixel(dstDesc._textureDesc._format) / 8;
                    }
                    c.bufferRowLength = std::max(width, minDims);
                    c.bufferImageHeight = std::max(height, minDims);

                    c.imageSubresource = VkImageSubresourceLayers{ srcAspectMask, m, a, 1 };
                    c.imageOffset = VkOffset3D{(int32_t)src._leftTopFront[0], (int32_t)src._leftTopFront[1], (int32_t)src._leftTopFront[2]};

                    if (src._leftTopFront[0] != ~0u && src._rightBottomBack[0] != ~0u) {
                        c.imageExtent = VkExtent3D{
                            src._rightBottomBack[0] - src._leftTopFront[0],
                            src._rightBottomBack[1] - src._leftTopFront[1],
                            src._rightBottomBack[2] - src._leftTopFront[2]};
                    } else {
                        c.imageExtent = VkExtent3D{
                            srcDesc._textureDesc._width,
                            srcDesc._textureDesc._height,
                            std::max(srcDesc._textureDesc._depth, 1u)};
                    }
                }

                width >>= 1u;
                height >>= 1u;
                depth >>= 1u;
            }

            const auto copyOperations = mips*arrayCount;
            context.GetActiveCommandList().CopyImageToBuffer(
                srcResource->GetImage(), (VkImageLayout)Internal::AsVkImageLayout(srcLayout),
                dstResource->GetBuffer(),
                copyOperations, copyOps);
        } else {
            Throw(::Exceptions::BasicLabel("Blit copy operation not supported"));
        }
    }

    static unsigned CopyViaMemoryMap(
        VkDevice device, VkImage image, VkDeviceMemory mem,
        const TextureDesc& desc,
        const std::function<SubResourceInitData(SubResourceId)>& initData)
    {
        // Copy all of the subresources to device member, using a MemoryMap path.
        // If "image" is not null, we will get the arrangement of subresources from
        // the images. Otherwise, we will use a default arrangement of subresources.
        ResourceMap map(device, mem);
        unsigned bytesUploaded = 0;

		auto mipCount = std::max(1u, unsigned(desc._mipCount));
		auto arrayCount = std::max(1u, unsigned(desc._arrayCount));
		auto aspectFlags = AsImageAspectMask(desc._format);
		for (unsigned m = 0; m < mipCount; ++m) {
            auto mipDesc = CalculateMipMapDesc(desc, m);
			for (unsigned a = 0; a < arrayCount; ++a) {
                auto subResData = initData({m, a});
				if (!subResData._data.size()) continue;

				VkSubresourceLayout layout = {};
                if (image) {
                    VkImageSubresource subRes = { aspectFlags, m, a };
				    vkGetImageSubresourceLayout(
					    device, image,
					    &subRes, &layout);
                } else {
                    auto offset = GetSubResourceOffset(desc, m, a);
                    layout = VkSubresourceLayout { 
                        offset._offset, offset._size, 
                        offset._pitches._rowPitch, offset._pitches._arrayPitch, offset._pitches._slicePitch };
                }

				if (!layout.size) continue;	// couldn't find this subresource?

				auto defaultPitches = MakeTexturePitches(mipDesc);
				if (!subResData._pitches._rowPitch && !subResData._pitches._slicePitch && !subResData._pitches._arrayPitch)
					subResData._pitches = defaultPitches;

                CopyMipLevel(
                    PtrAdd(map.GetData().begin(), layout.offset), size_t(layout.size),
                    TexturePitches{unsigned(layout.rowPitch), unsigned(layout.depthPitch), unsigned(layout.arrayPitch)},
                    mipDesc, subResData);
			}
		}
        return bytesUploaded;
    }

    namespace Internal
	{
		unsigned CopyViaMemoryMap(
			IDevice& dev, Resource& resource,
			const std::function<SubResourceInitData(SubResourceId)>& initData)
		{
			assert(resource.GetDesc()._type == ResourceDesc::Type::Texture);
			return Metal_Vulkan::CopyViaMemoryMap(
				ExtractUnderlyingDevice(dev), resource.GetImage(), resource.GetMemory(),
				resource.GetDesc()._textureDesc, initData);
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	void CompleteInitialization(
		DeviceContext& context,
		IteratorRange<IResource* const*> resources)
	{
		Internal::ResourceInitializationHelper helper(context);
		for (auto r:resources) {
			auto* res = checked_cast<Resource*>(r);
			if (res->_pendingInitialization) {
				res->_pendingInitialization(helper, *res);
				res->_pendingInitialization = nullptr;
			}
		}
		helper.CommitLayoutChanges();
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static std::vector<std::pair<SubResourceId, SubResourceOffset>> FindSubresources(VkDevice dev, IResource& iresource)
	{
		std::vector<std::pair<SubResourceId, SubResourceOffset>> result;
		auto& resource = *checked_cast<Resource*>(&iresource);

		auto desc = resource.GetDesc();
		if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::CubeMap)
			assert(desc._textureDesc._arrayCount == 6u);
		result.reserve(std::max(1u, (unsigned)desc._textureDesc._arrayCount) * std::max(1u, (unsigned)desc._textureDesc._mipCount));

		auto* image = resource.GetImage();
        if (image) {
			assert(desc._type == ResourceDesc::Type::Texture);
            auto aspectMask = AsImageAspectMask(desc._textureDesc._format);

			for (unsigned arrayLayer=0; arrayLayer<std::max(1u, (unsigned)desc._textureDesc._arrayCount); ++arrayLayer)
				for (unsigned mip=0; mip<std::max(1u, (unsigned)desc._textureDesc._mipCount); ++mip) {
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
			for (unsigned arrayLayer=0; arrayLayer<std::max(1u, (unsigned)desc._textureDesc._arrayCount); ++arrayLayer)
				for (unsigned mip=0; mip<std::max(1u, (unsigned)desc._textureDesc._mipCount); ++mip) {
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
		TexturePitches pitches { (unsigned)_dataSize, (unsigned)_dataSize, (unsigned)_dataSize };
		_subResources.push_back(std::make_pair(SubResourceId{}, SubResourceOffset{ 0, _dataSize, pitches }));
	}

	ResourceMap::ResourceMap(
		VkDevice dev, IResource& iresource,
		Mode mapMode,
        VkDeviceSize offset, VkDeviceSize size)
	{
		///////////
			// Map a range in a linear buffer (makes less sense for textures)
		///////////////////
		auto& resource = *checked_cast<Resource*>(&iresource);
		auto desc = resource.GetDesc();
		if (desc._type != ResourceDesc::Type::LinearBuffer)
			Throw(std::runtime_error("Attempting to map a linear range in a non-linear buffer resource"));

		if (offset >= desc._linearBufferDesc._sizeInBytes || (offset+size) > desc._linearBufferDesc._sizeInBytes || size == 0)
			Throw(std::runtime_error(StringMeld<256>() << "Invalid range when attempting to map a linear buffer range. Offset: " << offset << ", Size: " << size));

		_dataSize = std::min(desc._linearBufferDesc._sizeInBytes - offset, size);
		TexturePitches pitches { (unsigned)_dataSize, (unsigned)_dataSize, (unsigned)_dataSize };
			
		auto res = vkMapMemory(dev, resource.GetMemory(), offset, size, 0, &_data);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failed while mapping device memory"));

        _dev = dev;
        _mem = resource.GetMemory();
		_subResources.push_back(std::make_pair(SubResourceId{}, SubResourceOffset{ 0, _dataSize, pitches }));
	}

	ResourceMap::ResourceMap(
		VkDevice dev, IResource& iresource,
		Mode mapMode,
        SubResourceId subResource)
	{
		///////////
			// Map a single subresource
		///////////////////
        VkDeviceSize finalOffset = 0, finalSize = VK_WHOLE_SIZE;
		TexturePitches pitches;

		auto& resource = *checked_cast<Resource*>(&iresource);

        // special case for images, where we need to take into account the requested "subresource"
        auto* image = resource.GetImage();
        auto desc = resource.GetDesc();
        if (image) {
			assert(desc._type == ResourceDesc::Type::Texture);
            auto aspectMask = AsImageAspectMask(desc._textureDesc._format);
            VkImageSubresource sub = { aspectMask, subResource._mip, subResource._arrayLayer };
            VkSubresourceLayout layout = {};
            vkGetImageSubresourceLayout(dev, image, &sub, &layout);
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

        auto res = vkMapMemory(dev, resource.GetMemory(), finalOffset, finalSize, 0, &_data);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failed while mapping device memory"));

        _dev = dev;
        _mem = resource.GetMemory();
		_subResources.push_back(std::make_pair(subResource, SubResourceOffset{ 0, _dataSize, pitches }));
    }

	ResourceMap::ResourceMap(
		VkDevice dev, IResource& iresource,
		Mode mapMode)
	{
		///////////
			// Map all subresources
		///////////////////
		auto& resource = *checked_cast<Resource*>(&iresource);

		auto res = vkMapMemory(dev, resource.GetMemory(), 0, VK_WHOLE_SIZE, 0, &_data);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failed while mapping device memory"));

		_subResources = FindSubresources(dev, resource);
		_dev = dev;
        _mem = resource.GetMemory();
		_dataSize = 0;
		for (const auto& subRes:_subResources)
			_dataSize = std::max(_dataSize, subRes.second._offset + subRes.second._size);
	}

	ResourceMap::ResourceMap(
		DeviceContext& context, IResource& resource,
		Mode mapMode)
	: ResourceMap(context.GetUnderlyingDevice(), resource, mapMode)
	{
	}

	ResourceMap::ResourceMap(
		DeviceContext& context, IResource& resource,
		Mode mapMode,
		SubResourceId subResource)
	: ResourceMap(context.GetUnderlyingDevice(), resource, mapMode, subResource)
	{
	}
	
	ResourceMap::ResourceMap(
		DeviceContext& context, IResource& resource,
		Mode mapMode,
		VkDeviceSize offset, VkDeviceSize size)
	: ResourceMap(context.GetUnderlyingDevice(), resource, mapMode, offset, size)
	{
	}

	static VkDevice GetUnderlyingDevice(IDevice& device)
	{
		auto vkDevice = (IDeviceVulkan*)device.QueryInterface(typeid(IDeviceVulkan).hash_code());
		if (!vkDevice) Throw("Incorrect device type passed to ResourceMap");
		return vkDevice->GetUnderlyingDevice();
	}

	ResourceMap::ResourceMap(
		IDevice& device, IResource& resource,
		Mode mapMode)
	: ResourceMap(GetUnderlyingDevice(device), resource, mapMode)
	{
	}

	ResourceMap::ResourceMap(
		IDevice& device, IResource& resource,
		Mode mapMode,
		SubResourceId subResource)
	: ResourceMap(GetUnderlyingDevice(device), resource, mapMode, subResource)
	{
	}

	ResourceMap::ResourceMap(
		IDevice& device, IResource& resource,
		Mode mapMode,
		VkDeviceSize offset, VkDeviceSize size)
	: ResourceMap(GetUnderlyingDevice(device), resource, mapMode, offset, size)
	{
	}

	void ResourceMap::TryUnmap()
	{
        if (_dev && _mem)
		    vkUnmapMemory(_dev, _mem);
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

    ResourceMap::ResourceMap() : _dev(nullptr), _mem(nullptr), _data(nullptr), _dataSize{0} {}

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
		_subResources = std::move(moveFrom._subResources);
	}

	ResourceMap& ResourceMap::operator=(ResourceMap&& moveFrom) never_throws
	{
		TryUnmap();
		_data = moveFrom._data; moveFrom._data = nullptr;
		_dataSize = moveFrom._dataSize; moveFrom._dataSize = 0;
		_dev = moveFrom._dev; moveFrom._dev = nullptr;
		_mem = moveFrom._mem; moveFrom._mem = nullptr;
		_subResources = std::move(moveFrom._subResources);
		return *this;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	void BlitEncoder::Write(
		const CopyPartial_Dest& dst,
		const SubResourceInitData& srcData,
		Format srcDataFormat,
		VectorPattern<unsigned, 3> srcDataDimensions)
	{
		// This is a synchronized write, which means it happens in the command list order
		// we need to create a staging resource, fill with the given information, and copy from
		// there via a command on the command list
		// Note that we only change a single subresource with this command

		assert(dst._resource);
		auto desc = dst._resource->GetDesc();
        if (desc._type != RenderCore::ResourceDesc::Type::Texture)
            Throw(std::runtime_error("Non-texture resource type used with WriteSynchronized operation"));

		if (dst._subResource._mip >= desc._textureDesc._mipCount)
            Throw(std::runtime_error("Mipmap index used in WriteSynchronized operation is too high"));

        if ((dst._leftTopFront[0]+srcDataDimensions[0]) > desc._textureDesc._width || (dst._leftTopFront[1]+srcDataDimensions[1]) > desc._textureDesc._height)
            Throw(std::runtime_error("Rectangle dimensions used with WriteSynchronized operation are outside of the destination texture area"));

		auto srcPixelCount = srcDataDimensions[0] * srcDataDimensions[1] * srcDataDimensions[2];
        if (!srcPixelCount)
            Throw(std::runtime_error("No source pixels in WriteSynchronized operation. The depth of the srcDataDimensions field might need to be at least 1."));

		RenderCore::TextureDesc tDesc;
		if (srcDataDimensions[2] > 1) {
			tDesc = RenderCore::TextureDesc::Plain3D(srcDataDimensions[0], srcDataDimensions[1], srcDataDimensions[2], srcDataFormat);
		} else {
			tDesc = RenderCore::TextureDesc::Plain2D(srcDataDimensions[0], srcDataDimensions[1], srcDataFormat);
		}

		auto transferSrc = Internal::CreateResource(
			GetObjectFactory(*_devContext),
            RenderCore::CreateDesc(
                RenderCore::BindFlag::TransferSrc,
                0, 0,
                tDesc,
                "blit-pass-src"),
            [srcData](SubResourceId subResId) -> SubResourceInitData {
				assert(subResId._mip == 0 && subResId._arrayLayer == 0);
            	return srcData;
			});

		CopyPartial_Src srcPartial {
			transferSrc.get(), SubResourceId{},
			{0,0,0},
			srcDataDimensions };

		Internal::CaptureForBind captureDst(*_devContext, *checked_cast<Resource*>(dst._resource), BindFlag::TransferDst);
		CopyPartial(*_devContext, dst, srcPartial, captureDst.GetLayout(), Internal::ImageLayout::TransferSrcOptimal);
	}

	void BlitEncoder::Copy(
		const CopyPartial_Dest& dst,
		const CopyPartial_Src& src)
	{
		assert(src._resource && dst._resource);
		Internal::CaptureForBind captureSrc(*_devContext, *checked_cast<Resource*>(src._resource), BindFlag::TransferSrc);
		Internal::CaptureForBind captureDst(*_devContext, *checked_cast<Resource*>(dst._resource), BindFlag::TransferDst);
		CopyPartial(*_devContext, dst, src, captureDst.GetLayout(), captureSrc.GetLayout());
	}

	void BlitEncoder::Copy(
		IResource& dst,
		IResource& src)
	{
		Internal::CaptureForBind captureSrc(*_devContext, *checked_cast<Resource*>(&src), BindFlag::TransferSrc);
		Internal::CaptureForBind captureDst(*_devContext, *checked_cast<Resource*>(&dst), BindFlag::TransferDst);
		Metal_Vulkan::Copy(*_devContext, *checked_cast<Resource*>(&dst), *checked_cast<Resource*>(&src), captureDst.GetLayout(), captureSrc.GetLayout());
	}

	BlitEncoder::BlitEncoder(DeviceContext& devContext) : _devContext(&devContext)
	{
	}

	BlitEncoder::~BlitEncoder()
	{
		_devContext->EndBlitEncoder();
	}

}}

