// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../IDevice.h"
#include "../DeviceInitialization.h"
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

namespace RenderCore { namespace Metal_Vulkan { class SubmissionQueue; class GlobalsContainer; }}

namespace RenderCore { namespace ImplVulkan
{
    class SelectedPhysicalDevice
	{
	public:
		VkPhysicalDevice _dev;
		unsigned _graphicsQueueFamily = ~0u;
        unsigned _dedicatedTransferQueueFamily = ~0u;
        unsigned _dedicatedComputeQueueFamily = ~0u;
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
        void ChangeConfiguration(IThreadContext&, const PresentationChainDesc&) override;

        PresentationChainDesc GetDesc() const override;
        std::shared_ptr<IDevice> GetDevice() const override;
        const TextureDesc& GetBufferDesc() { return _bufferDesc; }

		void PresentToQueue(Metal_Vulkan::SubmissionQueue& queue, IteratorRange<const VkSemaphore*>);
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
			
            std::optional<Metal_Vulkan::IAsyncTracker::Marker> _presentFence;      // (owned by the IAsyncTrackerVulkan)
		};
		PresentSync& GetSyncs() { return _presentSyncs[_activePresentSync]; }

        PresentationChain(
            std::shared_ptr<Device> device,
			Metal_Vulkan::ObjectFactory& factory,
            VulkanSharedPtr<VkSurfaceKHR> surface, 
			const PresentationChainDesc& requestDesc,
            Metal_Vulkan::SubmissionQueue* submissionQueue,
            const void* platformValue);
        ~PresentationChain();
    private:
		VulkanSharedPtr<VkSurfaceKHR>   _surface;
        VulkanSharedPtr<VkSwapchainKHR> _swapChain;
        VulkanSharedPtr<VkDevice>       _vulkanDevice;
        Metal_Vulkan::ObjectFactory*    _factory;
        unsigned		_activeImageIndex;

        std::vector<std::shared_ptr<IResource>> _images;

		TextureDesc     _bufferDesc;
		PresentationChainDesc	_desc;

        PresentSync     _presentSyncs[3];
        unsigned        _activePresentSync;
        Metal_Vulkan::SubmissionQueue*	_submissionQueue;

		Metal_Vulkan::CommandBufferPool _primaryBufferPool;
		VulkanSharedPtr<VkCommandBuffer> _primaryBuffers[3];

        std::weak_ptr<Device> _device;

        void BuildImages();
    };

////////////////////////////////////////////////////////////////////////////////

    class ThreadContext : public IThreadContext, public IThreadContextVulkan
    {
    public:
		void	        Present(IPresentationChain&) override;
		IResourcePtr	BeginFrame(IPresentationChain& presentationChain) override;
		void			CommitCommands(CommitCommandsFlags::BitField) override;

        void AddPreFrameCommandList(Metal_Vulkan::CommandList&& cmdList) override;
        float GetThreadingPressure() override;
        bool IsDedicatedTransferContext() override;

        std::shared_ptr<Metal_Vulkan::IAsyncTracker> GetQueueTracker() override;
        void UpdateGPUTracking() override;

        void AttachNameToCommandList(std::string name) override;
        void ReleaseCommandBufferPool() override;

        bool                        IsImmediate() const override;
        ThreadContextStateDesc      GetStateDesc() const override;
        std::shared_ptr<IDevice>    GetDevice() const override;
        void                        IncrFrameId();
		void						InvalidateCachedState() const override;

		IAnnotator&					GetAnnotator() override;

        virtual void*   QueryInterface(size_t guid) override;
        const std::shared_ptr<Metal_Vulkan::DeviceContext>& GetMetalContext() override;
        std::shared_ptr<Metal_Vulkan::DeviceContext> BeginPrimaryCommandList() override;
        std::shared_ptr<Metal_Vulkan::DeviceContext> BeginSecondaryCommandList() override;

		void AttachDestroyer(const std::shared_ptr<Metal_Vulkan::IDestructionQueue>&);
        void PumpDestructionQueues();

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
        std::shared_ptr<Metal_Vulkan::CommandBufferPool> _commandBufferPool;

		VkDevice							_underlyingDevice;
		Metal_Vulkan::ObjectFactory*	    _factory;
		Metal_Vulkan::GlobalPools*			_globalPools;

        VkSemaphore                         _nextQueueShouldWaitOnAcquire = VK_NULL_HANDLE;

        std::vector<Metal_Vulkan::CommandList> _interimCmdLists;

        Metal_Vulkan::IAsyncTracker::Marker CommitToQueue_Internal(
            IteratorRange<const std::pair<VkSemaphore, uint64_t>*> waitBeforeBegin,
            IteratorRange<const std::pair<VkSemaphore, uint64_t>*> signalOnCompletion);
    };

////////////////////////////////////////////////////////////////////////////////

	class Device : public IDevice, public IDeviceVulkan, public std::enable_shared_from_this<Device>
    {
    public:
        std::unique_ptr<IPresentationChain>     CreatePresentationChain(
			const void* platformValue, const PresentationChainDesc& desc) override;

        DeviceDesc                              GetDesc() override;
        uint64_t                                GetGUID() const override;
        const DeviceFeatures&                   GetDeviceFeatures() const override;
		const DeviceLimits&						GetDeviceLimits() const override;

        std::shared_ptr<IThreadContext>         GetImmediateContext() override;
        std::unique_ptr<IThreadContext>         CreateDeferredContext() override;
        std::unique_ptr<IThreadContext>         CreateDedicatedTransferContext() override;

        Metal_Vulkan::GlobalPools&              GetGlobalPools();
		Metal_Vulkan::ObjectFactory&			GetObjectFactory();
        VkDevice	                            GetUnderlyingDevice() override { return _underlying.get(); }
        std::shared_ptr<Metal_Vulkan::IAsyncTracker> GetGraphicsQueueAsyncTracker() override;
        std::shared_ptr<Metal_Vulkan::IAsyncTracker> GetDedicatedTransferAsyncTracker() override;

        void GetInternalMetrics(InternalMetricsType type, IteratorRange<void*>) const override;

		IResourcePtr CreateResource(
			const ResourceDesc& desc, 
            StringSection<> name,
			const std::function<SubResourceInitData(SubResourceId)>&) override;
		FormatCapability    QueryFormatCapability(Format format, BindFlag::BitField bindingType) override;

        std::shared_ptr<ICompiledPipelineLayout> CreatePipelineLayout(const PipelineLayoutInitializer& desc, StringSection<> name) override;
        std::shared_ptr<IDescriptorSet> CreateDescriptorSet(PipelineType pipelineType, const DescriptorSetSignature& signature, StringSection<> name) override;
        std::shared_ptr<ISampler> CreateSampler(const SamplerDesc& desc) override;

		void			Stall() override;
        void            PrepareForDestruction() override;

		std::shared_ptr<ILowLevelCompiler>		CreateShaderCompiler() override;
        std::shared_ptr<ILowLevelCompiler>		CreateShaderCompiler(const VulkanCompilerConfiguration& cfg) override;

        virtual void*   QueryInterface(size_t guid) override;
		VkInstance	    GetVulkanInstance() override;

        Device(
            VulkanSharedPtr<VkInstance> instance,
            SelectedPhysicalDevice physDev,
            const DeviceFeatures& xleFeatures,
            bool enableDebugLayer);
        ~Device();
    protected:
		VulkanSharedPtr<VkInstance>         _instance;
        VulkanSharedPtr<VkDevice>		    _underlying;
        SelectedPhysicalDevice              _physDev;
		ConsoleRig::AttachablePtr<Metal_Vulkan::GlobalsContainer> _globalsContainer;
        std::shared_ptr<Metal_Vulkan::SubmissionQueue> _graphicsQueue;
        std::shared_ptr<Metal_Vulkan::SubmissionQueue> _dedicatedTransferQueue;

		std::shared_ptr<ThreadContext>	_foregroundPrimaryContext;
        std::thread::id _initializationThread;

        std::shared_ptr<Metal_Vulkan::IDestructionQueue> _destrQueue;
		DeviceLimits _limits;
    };

////////////////////////////////////////////////////////////////////////////////

    class DebugMessageHandler;

    class APIInstance : public IAPIInstance, public IAPIInstanceVulkan
    {
    public:
        std::shared_ptr<IDevice>    CreateDevice(unsigned configurationIdx, const DeviceFeatures& features) override;
        std::shared_ptr<IDevice>    CreateDevice(VkPhysicalDevice, unsigned renderingQueueFamily, const DeviceFeatures&) override;

        unsigned                    GetDeviceConfigurationCount() override;
        DeviceConfigurationProps    GetDeviceConfigurationProps(unsigned configurationIdx) override;

        DeviceFeatures              QueryFeatureCapability(unsigned configurationIdx) override;
        bool                        QueryPresentationChainCompatibility(unsigned configurationIdx, const void* platformWindowHandle) override;
        FormatCapability            QueryFormatCapability(unsigned configurationIdx, Format format, BindFlag::BitField bindingType) override;

        VkInstance                  GetVulkanInstance() override;
        VkPhysicalDevice            GetPhysicalDevice(unsigned configurationIdx) override;

        std::string                 LogPhysicalDevice(unsigned configurationIdx) override;
		std::string                 LogInstance(const void* presentationChainPlatformValue) override;

        void*       QueryInterface(size_t guid) override;
        APIInstance(const APIFeatures& features);
        ~APIInstance();
    private:
		VulkanSharedPtr<VkInstance>         _instance;
        std::vector<SelectedPhysicalDevice> _physicalDevices;
        std::unique_ptr<DebugMessageHandler> _msgHandler;
        APIFeatures _features;
    };

////////////////////////////////////////////////////////////////////////////////

}}
