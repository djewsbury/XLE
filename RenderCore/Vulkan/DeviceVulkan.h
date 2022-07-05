// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../IDevice.h"
#include "IDeviceVulkan.h"
#include "Metal/VulkanCore.h"
#include "Metal/ObjectFactory.h"
#include "Metal/DeviceContext.h"
#include "Metal/Pools.h"
#include "Metal/IncludeVulkan.h"
#include "Metal/FrameBuffer.h"
#include "Metal/TextureView.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../Utility/IntrusivePtr.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>
#include <vector>
#include <type_traits>


namespace RenderCore { namespace Metal_Vulkan
{
    class EventBasedTracker;
    class FenceBasedTracker;
    class SubmissionQueue;
    class GlobalsContainer;
}}

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

////////////////////////////////////////////////////////////////////////////////

    class PresentationChain : public IPresentationChain
    {
    public:
        void Resize(unsigned newWidth, unsigned newHeight) /*override*/;

        const std::shared_ptr<PresentationChainDesc>& GetDesc() const;
        const TextureDesc& GetBufferDesc() { return _bufferDesc; }

		void PresentToQueue(Metal_Vulkan::SubmissionQueue& queue);
        struct AquireResult
        {
            std::shared_ptr<IResource> _resource;
            VulkanSharedPtr<VkCommandBuffer> _primaryCommandBuffer;
        };
        AquireResult AcquireNextImage(Metal_Vulkan::SubmissionQueue& queue);

		class PresentSync
		{
		public:
			VulkanUniquePtr<VkSemaphore>		_onAcquireComplete;
			VulkanUniquePtr<VkSemaphore>		_onCommandBufferComplete;
			
            std::optional<Metal_Vulkan::IAsyncTracker::Marker> _presentFence;      // (owned by the FenceBasedTracker)
		};
		PresentSync& GetSyncs() { return _presentSyncs[_activePresentSync]; }
		VkCommandBuffer GetPrimaryBuffer() { return _primaryBuffers[_activePresentSync].get(); }

        PresentationChain(
			Metal_Vulkan::ObjectFactory& factory,
            VulkanSharedPtr<VkSurfaceKHR> surface, 
			VectorPattern<unsigned, 2> extent,
            Metal_Vulkan::SubmissionQueue* submissionQueue,
			unsigned queueFamilyIndex,
            const void* platformValue);
        ~PresentationChain();
    private:
		VulkanSharedPtr<VkSurfaceKHR>   _surface;
        VulkanSharedPtr<VkSwapchainKHR> _swapChain;
        VulkanSharedPtr<VkDevice>       _device;
        Metal_Vulkan::ObjectFactory*    _factory;
        const void*		_platformValue;
        unsigned		_activeImageIndex;

        std::vector<std::shared_ptr<IResource>> _images;

		TextureDesc     _bufferDesc;
		std::shared_ptr<PresentationChainDesc>	_desc;

        PresentSync     _presentSyncs[3];
        unsigned        _activePresentSync;
        Metal_Vulkan::SubmissionQueue*	_submissionQueue;

		Metal_Vulkan::CommandBufferPool _primaryBufferPool;
		VulkanSharedPtr<VkCommandBuffer> _primaryBuffers[3];

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
        float GetThreadingPressure() override;
        unsigned GetCmdListSpecificMarker() override;

        bool                        IsImmediate() const override;
        ThreadContextStateDesc      GetStateDesc() const override;
        std::shared_ptr<IDevice>    GetDevice() const override;
        void                        IncrFrameId();
		void						InvalidateCachedState() const override;

		IAnnotator&					GetAnnotator() override;

        virtual void*   QueryInterface(size_t guid) override;
        const std::shared_ptr<Metal_Vulkan::DeviceContext>& GetMetalContext() override;

		void AttachDestroyer(const std::shared_ptr<Metal_Vulkan::IDestructionQueue>&);

        ThreadContext(
            std::shared_ptr<Device> device, 
			std::shared_ptr<Metal_Vulkan::SubmissionQueue> submissionQueue);
        ~ThreadContext();
    protected:
		std::shared_ptr<Metal_Vulkan::DeviceContext> _metalContext;
        std::weak_ptr<Device>           _device;  // (must be weak, because Device holds a shared_ptr to the immediate context)
		unsigned                        _frameId;
        std::shared_ptr<Metal_Vulkan::SubmissionQueue> _submissionQueue;
		std::unique_ptr<IAnnotator>		_annotator;
        std::shared_ptr<Metal_Vulkan::IDestructionQueue> _destrQueue;

		VkDevice							_underlyingDevice;
		Metal_Vulkan::ObjectFactory*	    _factory;
		Metal_Vulkan::GlobalPools*			_globalPools;

        VulkanUniquePtr<VkSemaphore>		_interimCommandBufferComplete;
        VulkanUniquePtr<VkSemaphore>		_interimCommandBufferComplete2;
        bool                                _nextQueueShouldWaitOnInterimBuffer = false;
        VkSemaphore                         _nextQueueShouldWaitOnAcquire = VK_NULL_HANDLE;

        Metal_Vulkan::IAsyncTracker::Marker QueuePrimaryContext(IteratorRange<const VkSemaphore*> completionSignals);
        Metal_Vulkan::IAsyncTracker::Marker CommitPrimaryCommandBufferToQueue_Internal(Metal_Vulkan::CommandList& cmdList, IteratorRange<const VkSemaphore*> completionSignals);
        void PumpDestructionQueues();
    };

////////////////////////////////////////////////////////////////////////////////

	class Device : public IDevice, public IDeviceVulkan, public std::enable_shared_from_this<Device>
    {
    public:
        std::unique_ptr<IPresentationChain>     CreatePresentationChain(
			const void* platformValue, const PresentationChainDesc& desc) override;

        DeviceDesc                              GetDesc() override;
        uint64_t                                GetGUID() const override;

        std::shared_ptr<IThreadContext>         GetImmediateContext() override;
        std::unique_ptr<IThreadContext>         CreateDeferredContext() override;

        Metal_Vulkan::GlobalPools&              GetGlobalPools() override;
		Metal_Vulkan::ObjectFactory&			GetObjectFactory();
        VkDevice	                            GetUnderlyingDevice() override { return _underlying.get(); }
        std::shared_ptr<Metal_Vulkan::IAsyncTracker> GetAsyncTracker() override;

		IResourcePtr CreateResource(
			const ResourceDesc& desc, 
			const std::function<SubResourceInitData(SubResourceId)>&) override;
		FormatCapability    QueryFormatCapability(Format format, BindFlag::BitField bindingType) override;

        std::shared_ptr<ICompiledPipelineLayout> CreatePipelineLayout(const PipelineLayoutInitializer& desc) override;
        std::shared_ptr<IDescriptorSet> CreateDescriptorSet(const DescriptorSetInitializer& desc) override;
        std::shared_ptr<ISampler> CreateSampler(const SamplerDesc& desc) override;

		void			Stall() override;
        void            PrepareForDestruction() override;

		std::shared_ptr<ILowLevelCompiler>		CreateShaderCompiler() override;
        std::shared_ptr<ILowLevelCompiler>		CreateShaderCompiler(const VulkanCompilerConfiguration& cfg) override;

        virtual void*   QueryInterface(size_t guid) override;
		VkInstance	    GetVulkanInstance() override;

        Device();
        ~Device();
    protected:
		VulkanSharedPtr<VkInstance>         _instance;
		VulkanSharedPtr<VkDevice>		    _underlying;
        SelectedPhysicalDevice              _physDev;
		ConsoleRig::AttachablePtr<Metal_Vulkan::GlobalsContainer> _globalsContainer;
        std::shared_ptr<Metal_Vulkan::SubmissionQueue>	_graphicsQueue;

		std::shared_ptr<ThreadContext>	_foregroundPrimaryContext;
        std::thread::id _initializationThread;

        void DoSecondStageInit(VkSurfaceKHR surface = nullptr);
    };

////////////////////////////////////////////////////////////////////////////////
}}
