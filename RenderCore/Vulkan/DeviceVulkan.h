// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#define FLEX_CONTEXT_Device					FLEX_CONTEXT_CONCRETE
#define FLEX_CONTEXT_DeviceVulkan			FLEX_CONTEXT_CONCRETE
#define FLEX_CONTEXT_PresentationChain		FLEX_CONTEXT_CONCRETE
#define FLEX_CONTEXT_ThreadContext			FLEX_CONTEXT_CONCRETE
#define FLEX_CONTEXT_ThreadContextVulkan	FLEX_CONTEXT_CONCRETE

#include "../IDevice.h"
#include "../IThreadContext.h"
#include "IDeviceVulkan.h"
#include "Metal/VulkanCore.h"
#include "Metal/ObjectFactory.h"
#include "Metal/DeviceContext.h"
#include "Metal/Pools.h"
#include "Metal/IncludeVulkan.h"
#include "Metal/FrameBuffer.h"
#include "Metal/TextureView.h"
#include "../../Utility/IntrusivePtr.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>
#include <vector>
#include <type_traits>

namespace RenderCore { namespace ImplVulkan
{
    class SelectedPhysicalDevice
	{
	public:
		VkPhysicalDevice _dev;
		unsigned _renderingQueueFamily;
	};

    template<typename Type>
        using VulkanSharedPtr = Metal_Vulkan::VulkanSharedPtr<Type>;

    template<typename Type>
        using VulkanUniquePtr = Metal_Vulkan::VulkanUniquePtr<Type>;

////////////////////////////////////////////////////////////////////////////////

    class Device;
    class EventBasedTracker;
    class SemaphoreBasedTracker;
    class FenceBasedTracker;

////////////////////////////////////////////////////////////////////////////////

    class PresentationChain : public IPresentationChain
    {
    public:
        void Resize(unsigned newWidth, unsigned newHeight) /*override*/;

        const std::shared_ptr<PresentationChainDesc>& GetDesc() const;
        Metal_Vulkan::ResourceView* AcquireNextImage();
        const TextureDesc& GetBufferDesc() { return _bufferDesc; }

		void PresentToQueue(VkQueue queue);
        void SetInitialLayout(
            const Metal_Vulkan::ObjectFactory& factory, 
            Metal_Vulkan::CommandPool& cmdPool, VkQueue queue);

		class PresentSync
		{
		public:
			VulkanUniquePtr<VkSemaphore>		_onAcquireComplete;
			VulkanUniquePtr<VkSemaphore>		_onCommandBufferComplete;
			
            std::optional<Metal_Vulkan::IAsyncTracker::Marker> _presentFence;      // (owned by the FenceBasedTracker)
		};
		PresentSync& GetSyncs() { return _presentSyncs[_activePresentSync]; }
		VkCommandBuffer GetPrimaryBuffer() { return _primaryBuffers[_activePresentSync].get(); }
		VulkanSharedPtr<VkCommandBuffer> SharePrimaryBuffer() { return _primaryBuffers[_activePresentSync]; }

        PresentationChain(
			Metal_Vulkan::ObjectFactory& factory,
            VulkanSharedPtr<VkSurfaceKHR> surface, 
			VectorPattern<unsigned, 2> extent,
			unsigned queueFamilyIndex,
            const void* platformValue,
            std::shared_ptr<FenceBasedTracker> gpuTracker);
        ~PresentationChain();
    private:
		VulkanSharedPtr<VkSurfaceKHR>   _surface;
        VulkanSharedPtr<VkSwapchainKHR> _swapChain;
        VulkanSharedPtr<VkDevice>       _device;
        Metal_Vulkan::ObjectFactory*    _factory;
        const void*		_platformValue;
        unsigned		_activeImageIndex;

        class Image
        {
        public:
            VkImage     _image;
			Metal_Vulkan::ResourceView      _rtv;
        };
        std::vector<Image> _images;

		TextureDesc     _bufferDesc;
		std::shared_ptr<PresentationChainDesc>	_desc;

        PresentSync     _presentSyncs[3];
        unsigned        _activePresentSync;

		Metal_Vulkan::CommandPool _primaryBufferPool;
		VulkanSharedPtr<VkCommandBuffer> _primaryBuffers[3];

        std::shared_ptr<FenceBasedTracker>	_gpuTracker;

        void BuildImages();
    };

////////////////////////////////////////////////////////////////////////////////

    class ThreadContext : public IThreadContext, public IThreadContextVulkan, public std::enable_shared_from_this<ThreadContext>
    {
    public:
		void	        Present(IPresentationChain&) override;
		IResourcePtr	BeginFrame(IPresentationChain& presentationChain) override;
		void			CommitCommands(CommitCommandsFlags::BitField) override;

        void CommitPrimaryCommandBufferToQueue(Metal_Vulkan::CommandList& cmdList) override;

        bool                        IsImmediate() const override;
        ThreadContextStateDesc      GetStateDesc() const override;
        std::shared_ptr<IDevice>    GetDevice() const override;
        void                        IncrFrameId();
		void						InvalidateCachedState() const override;

        Metal_Vulkan::CommandPool&  GetRenderingCommandPool()   { return _renderingCommandPool; }
        VkQueue                     GetQueue()                  { return _queue; }

		IAnnotator&					GetAnnotator() override;

        virtual void*   QueryInterface(size_t guid) override;
        const std::shared_ptr<Metal_Vulkan::DeviceContext>& GetMetalContext() override;

		void SetGPUTracker(const std::shared_ptr<FenceBasedTracker>&);
		void AttachDestroyer(const std::shared_ptr<Metal_Vulkan::IDestructionQueue>&);

        ThreadContext(
            std::shared_ptr<Device> device, 
			VkQueue queue,
            Metal_Vulkan::CommandPool&& cmdPool,
			Metal_Vulkan::CommandBufferType cmdBufferType);
        ~ThreadContext();
    protected:
        std::weak_ptr<Device>           _device;  // (must be weak, because Device holds a shared_ptr to the immediate context)
		unsigned                        _frameId;
        Metal_Vulkan::CommandPool		_renderingCommandPool;
		std::shared_ptr<Metal_Vulkan::DeviceContext>     _metalContext;
		std::unique_ptr<IAnnotator>		_annotator;

		VkDevice							_underlyingDevice;
		VkQueue								_queue;
		Metal_Vulkan::ObjectFactory*	    _factory;
		Metal_Vulkan::GlobalPools*			_globalPools;

		std::shared_ptr<FenceBasedTracker>	_gpuTracker;
		std::shared_ptr<Metal_Vulkan::IDestructionQueue> _destrQueue;

        VulkanUniquePtr<VkSemaphore>		_interimCommandBufferComplete;
        VulkanUniquePtr<VkSemaphore>		_interimCommandBufferComplete2;
        bool                                _nextQueueShouldWaitOnInterimBuffer = false;
        VkSemaphore                         _nextQueueShouldWaitOnAcquire = VK_NULL_HANDLE;

        void QueuePrimaryContext(
		    IteratorRange<const VkSemaphore*> completionSignals,
		    VkFence fence = VK_NULL_HANDLE);
    };

////////////////////////////////////////////////////////////////////////////////

	class Device : public IDevice, public IDeviceVulkan, public std::enable_shared_from_this<Device>
    {
    public:
        std::unique_ptr<IPresentationChain>     CreatePresentationChain(
			const void* platformValue, const PresentationChainDesc& desc) override;

        DeviceDesc                              GetDesc() override;

        std::shared_ptr<IThreadContext>         GetImmediateContext() override;
        std::unique_ptr<IThreadContext>         CreateDeferredContext() override;

        Metal_Vulkan::GlobalPools&              GetGlobalPools() override { return _pools; }
		Metal_Vulkan::ObjectFactory&			GetObjectFactory() { return _objectFactory; }
        VkDevice	                            GetUnderlyingDevice() override { return _underlying.get(); }

		IResourcePtr CreateResource(
			const ResourceDesc& desc, 
			const std::function<SubResourceInitData(SubResourceId)>&) override;
		FormatCapability    QueryFormatCapability(Format format, BindFlag::BitField bindingType) override;

        std::shared_ptr<ICompiledPipelineLayout> CreatePipelineLayout(const PipelineLayoutInitializer& desc) override;
        std::shared_ptr<IDescriptorSet> CreateDescriptorSet(const DescriptorSetInitializer& desc) override;
        std::shared_ptr<ISampler> CreateSampler(const SamplerDesc& desc) override;

		void			Stall() override;

		std::shared_ptr<ILowLevelCompiler>		CreateShaderCompiler() override;
        std::shared_ptr<ILowLevelCompiler>		CreateShaderCompiler(const VulkanCompilerConfiguration& cfg) override;

        virtual void*   QueryInterface(size_t guid) override;
		VkInstance	    GetVulkanInstance() override;
        VkQueue         GetRenderingQueue() override;

        Device();
        ~Device();
    protected:
		VulkanSharedPtr<VkInstance>         _instance;
		VulkanSharedPtr<VkDevice>		    _underlying;
        SelectedPhysicalDevice              _physDev;
		Metal_Vulkan::ObjectFactory		    _objectFactory;
        Metal_Vulkan::GlobalPools           _pools;
        std::shared_ptr<FenceBasedTracker>	_gpuTracker;

		std::shared_ptr<ThreadContext>	_foregroundPrimaryContext;

        void DoSecondStageInit(VkSurfaceKHR surface = nullptr);
    };

////////////////////////////////////////////////////////////////////////////////
}}
