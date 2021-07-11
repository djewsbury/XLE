// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "../../IDevice.h"
#include "../../ResourceDesc.h"
#include "../../Types.h"
#include "../../ResourceUtils.h"
#include "../../../Utility/IteratorUtils.h"

using VkSampleCountFlagBits_ = uint32_t;
using VkImageLayout_ = uint32_t;

namespace RenderCore { namespace Metal_Vulkan
{
	class ObjectFactory;
	class DeviceContext;
	class Resource;

	namespace Internal
	{
		enum class ImageLayout;
		class ResourceInitializationHelper;
	}

	/// <summary>Abstraction for a device memory resource</summary>
	/// A Resource can either be a buffer or an image. In Vulkan, both types reference a VkDeviceMemory
	/// object that represents the actual allocation. This object maintains that allocation, and provides
	/// interfaces for copying data.
	///
	/// Images and buffers are combined into a single object for convenience. This allows us to use the 
	/// single "Desc" object to describe both, and it also fits in better with other APIs (eg, DirectX).
	/// This adds a small amount of redundancy to the Resource object -- but it seems to be trivial.
	class Resource : public IResource, public std::enable_shared_from_this<Resource>
	{
	public:
		using Desc = ResourceDesc;
		void* QueryInterface(size_t guid) override;
		std::vector<uint8_t> ReadBackSynchronized(IThreadContext& context, SubResourceId subRes) const override;
		Desc GetDesc() const override		{ return _desc; }
		uint64_t GetGUID() const override	{ return _guid; }

		std::shared_ptr<IResourceView>  CreateTextureView(BindFlag::Enum usage, const TextureViewDesc& window) override;
        std::shared_ptr<IResourceView>  CreateBufferView(BindFlag::Enum usage, unsigned rangeOffset, unsigned rangeSize) override;

		// ----------- Vulkan specific interface -----------

		VkDeviceMemory GetMemory() const    { return _mem.get(); }
		VkImage GetImage() const            { return _underlyingImage.get(); }
		VkBuffer GetBuffer() const          { return _underlyingBuffer.get(); }

		const VulkanSharedPtr<VkImage>& ShareImage() const { return _underlyingImage; }
		const VulkanSharedPtr<VkBuffer>& ShareBuffer() const { return _underlyingBuffer; }
		const VulkanSharedPtr<VkDeviceMemory>& ShareDeviceMemory() const { return _mem; }

		Resource(
			const ObjectFactory& factory, const Desc& desc,
			const SubResourceInitData& initData = SubResourceInitData{});
		Resource(
			const ObjectFactory& factory, const Desc& desc,
			const std::function<SubResourceInitData(SubResourceId)>&);
		Resource(VkImage image, const Desc& desc);
		Resource();
		~Resource();
		
		Internal::ImageLayout _steadyStateLayout;
		unsigned _steadyStateAccessMask;
		unsigned _steadyStateAssociatedStageMask;
		std::function<void(Internal::ResourceInitializationHelper&, Resource&)> _pendingInitialization;
	protected:
		VulkanSharedPtr<VkImage> _underlyingImage;
		VulkanSharedPtr<VkBuffer> _underlyingBuffer;
		VulkanSharedPtr<VkDeviceMemory> _mem;

		Desc _desc;
		uint64_t _guid;
		void ConfigureDefaultSteadyState(BindFlag::BitField);
	};

	void CompleteInitialization(
		DeviceContext& context,
		IteratorRange<IResource* const*> resources);

///////////////////////////////////////////////////////////////////////////////////////////////////
		//      M E M O R Y   M A P       //
///////////////////////////////////////////////////////////////////////////////////////////////////

	/// <summary>Locks a resource's memory for access from the CPU</summary>
	/// This is a low level mapping operation that happens immediately. The GPU must not
	/// be using the resource at the same time. If the GPU attempts to read while the CPU
	/// is written, the results will be undefined.
	/// A resource cannot be mapped more than once at the same time. However, multiple 
	/// subresources can be mapped in a single mapping operation.
	/// The caller is responsible for ensuring that the map is safe.
	class ResourceMap
	{
	public:
		IteratorRange<void*>        GetData();
		IteratorRange<const void*>  GetData() const;
		TexturePitches				GetPitches() const;

		IteratorRange<void*>        GetData(SubResourceId);
		IteratorRange<const void*>  GetData(SubResourceId) const;
		TexturePitches				GetPitches(SubResourceId) const;

		enum class Mode { Read, WriteDiscardPrevious };

		ResourceMap(
			DeviceContext& context, IResource& resource,
			Mode mapMode);
		ResourceMap(
			DeviceContext& context, IResource& resource,
			Mode mapMode,
			SubResourceId subResource);
		ResourceMap(
			DeviceContext& context, IResource& resource,
			Mode mapMode,
			VkDeviceSize offset, VkDeviceSize size);

		ResourceMap(
			IDevice& context, IResource& resource,
			Mode mapMode);
		ResourceMap(
			IDevice& context, IResource& resource,
			Mode mapMode,
			SubResourceId subResource);
		ResourceMap(
			IDevice& context, IResource& resource,
			Mode mapMode,
			VkDeviceSize offset, VkDeviceSize size);

		ResourceMap();
		~ResourceMap();

		ResourceMap(const ResourceMap&) = delete;
		ResourceMap& operator=(const ResourceMap&) = delete;
		ResourceMap(ResourceMap&&) never_throws;
		ResourceMap& operator=(ResourceMap&&) never_throws;

		// ----------- Vulkan specific interface -----------

		ResourceMap(
			VkDevice dev, IResource& resource,
			Mode mapMode);
		ResourceMap(
			VkDevice dev, IResource& resource,
			Mode mapMode,
			SubResourceId subResource);
		ResourceMap(
			VkDevice dev, IResource& resource,
			Mode mapMode,
			VkDeviceSize offset, VkDeviceSize size);

		ResourceMap(
			VkDevice dev, VkDeviceMemory memory,
			VkDeviceSize offset = 0, VkDeviceSize size = ~0ull);

	private:
		VkDevice            _dev;
		VkDeviceMemory      _mem;
		void*               _data;
		size_t              _dataSize;

		std::vector<std::pair<SubResourceId, SubResourceOffset>> _subResources;

		void TryUnmap();
	};

///////////////////////////////////////////////////////////////////////////////////////////////////
		//      B L T P A S S       //
///////////////////////////////////////////////////////////////////////////////////////////////////

	class BlitEncoder
	{
	public:
		class CopyPartial_Dest
		{
		public:
			IResource*          _resource;
			SubResourceId       _subResource;
			VectorPattern<unsigned, 3>      _leftTopFront;
		};

		class CopyPartial_Src
		{
		public:
			IResource*          _resource;
			SubResourceId       _subResource;
			VectorPattern<unsigned, 3>      _leftTopFront;
			VectorPattern<unsigned, 3>      _rightBottomBack;
		};

		void    Write(
			const CopyPartial_Dest& dst,
			const SubResourceInitData& srcData,
			Format srcDataFormat,
			VectorPattern<unsigned, 3> srcDataDimensions);

		void    Copy(
			const CopyPartial_Dest& dst,
			const CopyPartial_Src& src);

		void    Copy(
			IResource& dst,
			IResource& src);

		~BlitEncoder();

		BlitEncoder(const BlitEncoder&) = delete;
		BlitEncoder& operator=(const BlitEncoder&) = delete;
	private:
		BlitEncoder(DeviceContext& devContext);
		DeviceContext* _devContext;
		friend class DeviceContext;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////
		//      U T I L S       //
///////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		using ResourceInitializer = std::function<SubResourceInitData(SubResourceId)>;
		std::shared_ptr<Resource> CreateResource(
			const ObjectFactory& factory,
			const ResourceDesc& desc, 
			const ResourceInitializer& init = ResourceInitializer());

		enum class ImageLayout
		{
			Undefined						= 0, // VK_IMAGE_LAYOUT_UNDEFINED,
			General							= 1, // VK_IMAGE_LAYOUT_GENERAL,
			ColorAttachmentOptimal			= 2, // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			DepthStencilAttachmentOptimal	= 3, // VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			DepthStencilReadOnlyOptimal		= 4, // VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
			ShaderReadOnlyOptimal			= 5, // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			TransferSrcOptimal				= 6, // VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			TransferDstOptimal				= 7, // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			Preinitialized					= 8, // VK_IMAGE_LAYOUT_PREINITIALIZED,
			PresentSrc						= 1000001002, // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		};
		VkImageLayout_ AsVkImageLayout(ImageLayout input);

		class LayoutTransition
		{
		public:
			Resource* _res = nullptr;
			ImageLayout _oldLayout = ImageLayout::Undefined;
			unsigned _oldAccessMask = 0;
			unsigned _srcStages = 0;
			ImageLayout _newLayout = ImageLayout::Undefined;
			unsigned _newAccessMask = 0;
			unsigned _dstStages = 0;
		};
		void SetImageLayouts(DeviceContext& context, IteratorRange<const LayoutTransition*> changes);
		void SetImageLayout(
			DeviceContext& context, Resource& res, 
			ImageLayout oldLayout, unsigned oldAccessMask, unsigned srcStages, 
			ImageLayout newLayout, unsigned newAccessMask, unsigned dstStages);

		class CaptureForBindRecords;
		void ValidateIsEmpty(CaptureForBindRecords&);

		class CaptureForBind
		{
		public:
			Internal::ImageLayout GetLayout() { return _capturedLayout; }
			CaptureForBind(DeviceContext&, IResource&, BindFlag::Enum bindType);
			~CaptureForBind();
			CaptureForBind(const CaptureForBind&) = delete;
			CaptureForBind& operator=(const CaptureForBind&) = delete;
		private:
			DeviceContext* _context;
			IResource* _resource;
			BindFlag::Enum _bindType;
			Internal::ImageLayout _capturedLayout;
			unsigned _capturedAccessMask;
			unsigned _capturedStageMask;
			bool _releaseCapture;
			bool _usingCompatibleSteadyState;
		};

		void SetupInitialLayout(DeviceContext&, IResource&);

		unsigned CopyViaMemoryMap(
			IDevice& dev, Resource& resource,
			const std::function<SubResourceInitData(SubResourceId)>& initData);
	}

	VkSampleCountFlagBits_	AsSampleCountFlagBits(TextureSamples samples);
	VkImageAspectFlags      AsImageAspectMask(Format fmt);
}}
