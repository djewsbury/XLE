// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "../../IDevice.h"
#include "../../ResourceDesc.h"
#include "../../ResourceUtils.h"        // for CopyPartial_Dest, CopyPartial_Src
#include "../../Types.h"
#include "../../ResourceUtils.h"
#include "../../../Utility/IteratorUtils.h"
#include "IncludeVulkan.h"				// require for BarrierHelper

using VkSampleCountFlagBits_ = uint32_t;
using VkImageLayout_ = uint32_t;

extern "C" 
{
	#define FAKE_VK_DEFINE_HANDLE(object) typedef struct object##_T* object;
	FAKE_VK_DEFINE_HANDLE(VmaAllocation)
	FAKE_VK_DEFINE_HANDLE(VmaAllocator)
	#undef FAKE_VK_DEFINE_HANDLE
}

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
		VmaAllocation GetVmaMemory() const	{ return _vmaMem; }
		IteratorRange<void*> GetPermanentlyMappedRange() const { return _permanentlyMappedRange; }

		const VulkanSharedPtr<VkImage>& ShareImage() const { return _underlyingImage; }
		const VulkanSharedPtr<VkBuffer>& ShareBuffer() const { return _underlyingBuffer; }
		const VulkanSharedPtr<VkDeviceMemory>& ShareDeviceMemory() const { return _mem; }
		unsigned GetMemoryType() const { return _memoryType; }

		const Desc& AccessDesc() const { return _desc; }
		#if defined(_DEBUG)
			StringSection<> GetName() const { return _name; }
		#else
			StringSection<> GetName() const { return {}; }
		#endif

		void ChangeSteadyState(BindFlag::BitField);

		Resource(
			ObjectFactory& factory, const Desc& desc,
			StringSection<> name,
			const SubResourceInitData& initData = SubResourceInitData{});
		Resource(
			ObjectFactory& factory, const Desc& desc,
			StringSection<> name,
			const std::function<SubResourceInitData(SubResourceId)>&);
		Resource(VkImage image, const Desc& desc, StringSection<> name);
		Resource();
		~Resource();
		
		VkImageLayout _steadyStateImageLayout = (VkImageLayout)0;
		bool _pendingInit = false;
	protected:
		VulkanSharedPtr<VkImage> _underlyingImage;
		VulkanSharedPtr<VkBuffer> _underlyingBuffer;
		VulkanSharedPtr<VkDeviceMemory> _mem;
		VmaAllocation _vmaMem = nullptr;
		IteratorRange<void*> _permanentlyMappedRange;
		unsigned _memoryType = 0;

		Desc _desc;
		uint64_t _guid;

		#if defined(_DEBUG)
			std::string _name;
		#endif
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

		void FlushCache();
		void InvalidateCache();

		enum class Mode { Read, WriteDiscardPrevious };

		static bool CanMap(IDevice& device, IResource& resource, Mode mode);

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
			ObjectFactory& factory, IResource& resource,
			Mode mapMode);
		ResourceMap(
			ObjectFactory& factory, IResource& resource,
			Mode mapMode,
			SubResourceId subResource);
		ResourceMap(
			ObjectFactory& factory, IResource& resource,
			Mode mapMode,
			VkDeviceSize offset, VkDeviceSize size);

		ResourceMap(
			VkDevice dev, VkDeviceMemory memory,
			VkDeviceSize offset = 0, VkDeviceSize size = ~0ull);
		ResourceMap(VmaAllocator dev, VmaAllocation memory);

	private:
		VkDevice            _dev = nullptr;
		VkDeviceMemory      _mem = nullptr;
		VmaAllocator		_vmaAllocator = nullptr;
		VmaAllocation       _vmaMem = nullptr;
		void*               _data;
		size_t              _dataSize;
		VkDeviceSize		_resourceOffset = 0;
		bool 				_permanentlyMappedResource = false;

		std::vector<std::pair<SubResourceId, SubResourceOffset>> _subResources;

		void TryUnmap();
	};

///////////////////////////////////////////////////////////////////////////////////////////////////
		//      B L T P A S S       //
///////////////////////////////////////////////////////////////////////////////////////////////////

	class BlitEncoder
	{
	public:
		void    Write(
			const CopyPartial_Dest& dst,
			const SubResourceInitData& srcData,
			Format srcDataFormat,
			VectorPattern<unsigned, 3> srcDataDimensions,
			TexturePitches srcDataPitches);

		void    Write(
			const CopyPartial_Dest& dst,
			IteratorRange<const void*> srcData);

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

	struct BarrierResourceUsage
	{
		VkAccessFlags _accessFlags = 0;
		VkPipelineStageFlags _pipelineStageFlags = 0;
		VkImageLayout _imageLayout = (VkImageLayout)0;
		uint32_t _queueFamily = VK_QUEUE_FAMILY_IGNORED;
		BarrierResourceUsage(BindFlag::BitField immediateUsage);
		BarrierResourceUsage(BindFlag::BitField immediateUsage, ShaderStage);
		enum class Queue { Graphics, DedicatedTransfer, DedicatedCompute };
		BarrierResourceUsage(BindFlag::BitField immediateUsage, Queue queue);
		BarrierResourceUsage() = default;
		static BarrierResourceUsage HostRead();
		static BarrierResourceUsage HostWrite();
		static BarrierResourceUsage AllCommandsRead();
		static BarrierResourceUsage AllCommandsWrite();
		static BarrierResourceUsage AllCommandsReadAndWrite();
		static BarrierResourceUsage NoState();
		static BarrierResourceUsage Preinitialized();

		BarrierResourceUsage SpecializeForResource(BindFlag::BitField lifetimeResourceUsage);
	private:
		BarrierResourceUsage(BindFlag::BitField, VkPipelineStageFlags);
	};

	struct BarrierHelper
	{
		VkBufferMemoryBarrier _bufferBarriers[8];
		unsigned _bufferBarrierCount = 0;
		VkImageMemoryBarrier _imageBarriers[8];
		unsigned _imageBarrierCount = 0;
		VkPipelineStageFlags _srcStageMask = 0;
		VkPipelineStageFlags _dstStageMask = 0;
		DeviceContext* _deviceContext;

		BarrierHelper& Add(
			IResource& resource,
			BarrierResourceUsage preBarrierUsage, BarrierResourceUsage postBarrierUsage);
		BarrierHelper& Add(
			IResource& resource,
			TextureViewDesc::SubResourceRange mipRange,
			TextureViewDesc::SubResourceRange arrayLayerRange,
			BarrierResourceUsage preBarrierUsage, BarrierResourceUsage postBarrierUsage);
		BarrierHelper(IThreadContext& threadContext);
		BarrierHelper(DeviceContext& metalContext);
		~BarrierHelper();
		BarrierHelper(const BarrierHelper&) = delete;
		BarrierHelper& operator=(const BarrierHelper&) = delete;
	private:
		void Flush();
		std::pair<uint64_t, bool> _imageBarrierGuids[8];
	};

	namespace Internal
	{
		using ResourceInitializer = std::function<SubResourceInitData(SubResourceId)>;
		std::shared_ptr<Resource> CreateResource(
			ObjectFactory& factory,
			const ResourceDesc& desc,
			StringSection<> name,
			const ResourceInitializer& init = ResourceInitializer());

		VkImageLayout LayoutForImmediateUsage(BindFlag::BitField immediateUsage);

		class CaptureForBindRecords;
		void ValidateIsEmpty(CaptureForBindRecords&);

		class CaptureForBind
		{
		public:
			VkImageLayout GetLayout() { return _capturedLayout; }
			CaptureForBind(DeviceContext&, IResource&, BarrierResourceUsage usage);
			~CaptureForBind();
			CaptureForBind(const CaptureForBind&) = delete;
			CaptureForBind& operator=(const CaptureForBind&) = delete;
		private:
			DeviceContext* _context;
			IResource* _resource;
			VkImageLayout _capturedLayout;
			unsigned _capturedAccessMask;
			unsigned _capturedStageMask;
			std::optional<VkImageLayout> _restoreLayout;
		};

		unsigned CopyViaMemoryMap(
			IDevice& dev, IResource& resource, unsigned resourceOffset, unsigned resourceSize,
			const TextureDesc& descForLayout,
			const std::function<SubResourceInitData(SubResourceId)>& initData);
	}

	VkSampleCountFlagBits_	AsSampleCountFlagBits(TextureSamples samples);
	VkImageAspectFlags      AsImageAspectMask(Format fmt);
}}
