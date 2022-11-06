// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TextureView.h"
#include "State.h"
#include "FrameBuffer.h"
#include "ObjectFactory.h"
#include "VulkanCore.h"
#include "PipelineLayout.h"
#include "../../../Utility/IteratorUtils.h"
#include "../../../Utility/HeapUtils.h"
#include "../../../Utility/Threading/Mutex.h"
#include <vector>

namespace RenderCore { namespace Metal_Vulkan
{
    class ObjectFactory;

///////////////////////////////////////////////////////////////////////////////////////////////////

	enum class CommandBufferType { Primary, Secondary };

    class CommandBufferPool
	{
	public:
		VulkanSharedPtr<VkCommandBuffer> Allocate(CommandBufferType type);

		void FlushDestroys();

		CommandBufferPool(ObjectFactory& factory, unsigned queueFamilyIndex, bool resetable, const std::shared_ptr<IAsyncTracker>& tracker);
		CommandBufferPool();
		~CommandBufferPool();

        CommandBufferPool(CommandBufferPool&& moveFrom) never_throws;
        CommandBufferPool& operator=(CommandBufferPool&& moveFrom) never_throws;
	private:
		VulkanSharedPtr<VkCommandPool> _pool;
		VulkanSharedPtr<VkDevice> _device;
		std::shared_ptr<IAsyncTracker> _gpuTracker;

		struct MarkedDestroys { IAsyncTracker::Marker _marker; unsigned _pendingCount; };
        ResizableCircularBuffer<MarkedDestroys, 32> _markedDestroys;
		std::vector<VkCommandBuffer> _pendingDestroys;
        Threading::Mutex _lock;

		void QueueDestroy(VkCommandBuffer buffer);
	};

    class DescriptorPool
    {
    public:
        void Allocate(
            IteratorRange<VulkanUniquePtr<VkDescriptorSet>*> dst,
            IteratorRange<const VkDescriptorSetLayout*> layouts);
		VulkanUniquePtr<VkDescriptorSet> Allocate(VkDescriptorSetLayout layout);

        void FlushDestroys();
        VkDevice GetDevice() { return _device.get(); }

        DescriptorPool(ObjectFactory& factory, const std::shared_ptr<IAsyncTracker>& tracker);
        DescriptorPool();
        ~DescriptorPool();

        DescriptorPool(const DescriptorPool&) = delete;
        DescriptorPool& operator=(const DescriptorPool&) = delete;
        DescriptorPool(DescriptorPool&&) never_throws;
        DescriptorPool& operator=(DescriptorPool&&) never_throws;
    private:
        VulkanSharedPtr<VkDescriptorPool> _pool;
		VulkanSharedPtr<VkDevice> _device;
		std::shared_ptr<IAsyncTracker> _gpuTracker;

		struct MarkedDestroys { IAsyncTracker::Marker _marker; unsigned _pendingCount; };
		ResizableCircularBuffer<MarkedDestroys, 32> _markedDestroys;
        std::vector<VkDescriptorSet> _pendingDestroys;
        Threading::Mutex _lock;

		void QueueDestroy(VkDescriptorSet buffer);
    };

    class VulkanRenderPassPool
    {
    public:
        VulkanSharedPtr<VkRenderPass> CreateVulkanRenderPass(const FrameBufferDesc& layout);

        VulkanRenderPassPool(ObjectFactory& factory);
        VulkanRenderPassPool();
        ~VulkanRenderPassPool();

        VulkanRenderPassPool(VulkanRenderPassPool&&) never_throws;
        VulkanRenderPassPool& operator=(VulkanRenderPassPool&&) never_throws;
    private:
        std::vector<std::pair<uint64_t, VulkanSharedPtr<VkRenderPass>>> _cachedRenderPasses;
        ObjectFactory* _factory;
        Threading::Mutex _lock;
    };

    class DummyResources
    {
    public:
        ResourceView        _blankImage1DSrv;
        ResourceView        _blankImage2DSrv;
        ResourceView        _blankImage3DSrv;
        ResourceView        _blankImageCubeSrv;

        ResourceView        _blankImage1DArraySrv;
        ResourceView        _blankImage2DArraySrv;
        ResourceView        _blankImageCubeArraySrv;

        ResourceView        _blankImage1DUav;
        ResourceView        _blankImage2DUav;
        ResourceView        _blankImage3DUav;
        ResourceView        _blankImageCubeUav;

        ResourceView        _blankImage1DArrayUav;
        ResourceView        _blankImage2DArrayUav;
        ResourceView        _blankImageCubeArrayUav;

        ResourceView        _blankBufferUav;
        std::shared_ptr<Resource> _blankBuffer;
        std::unique_ptr<SamplerState> _blankSampler;

        void CompleteInitialization(DeviceContext&);

        DummyResources(ObjectFactory& factory);
        DummyResources();
        ~DummyResources();

        DummyResources(DummyResources&& moveFrom) never_throws;
        DummyResources& operator=(DummyResources&& moveFrom) never_throws;
    };

    namespace Internal { class CompiledDescriptorSetLayoutCache; }
    class TemporaryStorageManager;

    class GlobalPools
    {
    public:
        std::unique_ptr<TemporaryStorageManager> _temporaryStorageManager;
        DescriptorPool                      _mainDescriptorPool;
		DescriptorPool                      _longTermDescriptorPool;
        VulkanRenderPassPool                _renderPassPool;
        VulkanSharedPtr<VkPipelineCache>    _mainPipelineCache;
        DummyResources                      _dummyResources;

        Threading::Mutex _idleCommandBufferPoolsLock;
        std::vector<std::pair<unsigned, std::shared_ptr<CommandBufferPool>>> _idleCommandBufferPools;   // associated with a queue family index

        std::shared_ptr<Internal::CompiledDescriptorSetLayoutCache> _descriptorSetLayoutCache;

        GlobalPools();
        ~GlobalPools();
        GlobalPools(const GlobalPools&) = delete;
        GlobalPools& operator=(const GlobalPools&) = delete;
    };

    GlobalPools& GetGlobalPools();
}}
