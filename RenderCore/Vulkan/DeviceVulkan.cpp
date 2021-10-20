// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeviceVulkan.h"
#include "../IAnnotator.h"
#include "../Format.h"
#include "../Init.h"
#include "Metal/VulkanCore.h"
#include "Metal/ObjectFactory.h"
#include "Metal/Format.h"
#include "Metal/Pools.h"
#include "Metal/Resource.h"
#include "Metal/PipelineLayout.h"
#include "Metal/Shader.h"
#include "Metal/State.h"
#include "Metal/ExtensionFunctions.h"
#include "Metal/AsyncTracker.h"
#include "Metal/SubmissionQueue.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../OSServices/Log.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../Utility/Threading/ThreadingUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/PtrUtils.h"
#include "../../Core/SelectConfiguration.h"
#include <memory>

#if defined(_DEBUG)
    #define ENABLE_DEBUG_EXTENSIONS
#endif

namespace RenderCore { namespace ImplVulkan
{
    using VulkanAPIFailure = Metal_Vulkan::VulkanAPIFailure;

	std::unique_ptr<IAnnotator> CreateAnnotator(IDevice& device, std::weak_ptr<IThreadContext> threadContext);

	static std::string GetApplicationName()
	{
		return ConsoleRig::CrossModule::GetInstance()._services.CallDefault<std::string>(
			ConstHash64<'appn', 'ame'>::Value, std::string("<<unnamed>>"));
	}

	static const char* s_instanceExtensions[] = 
	{
		VK_KHR_SURFACE_EXTENSION_NAME
		#if PLATFORMOS_TARGET  == PLATFORMOS_WINDOWS
			, VK_KHR_WIN32_SURFACE_EXTENSION_NAME
		#endif
        #if defined(ENABLE_DEBUG_EXTENSIONS)
            , VK_EXT_DEBUG_UTILS_EXTENSION_NAME
        #endif
		, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME		// (this extension now rolled into Vulkan 1.1, so technically deprecated)
	};

	static const char* s_deviceExtensions[] =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
		, VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME
	};

    #if defined(ENABLE_DEBUG_EXTENSIONS)
	    static const char* s_instanceLayers[] =
	    {
			// "VK_LAYER_LUNARG_api_dump",
			"VK_LAYER_LUNARG_assistant_layer",
			"VK_LAYER_LUNARG_core_validation",
			// "VK_LAYER_LUNARG_device_simulation",
			// "VK_LAYER_LUNARG_monitor",
			"VK_LAYER_LUNARG_object_tracker",
			"VK_LAYER_LUNARG_parameter_validation",
			// "VK_LAYER_LUNARG_screenshot",
			"VK_LAYER_LUNARG_standard_validation",

		    "VK_LAYER_LUNARG_device_limits",
		    "VK_LAYER_LUNARG_draw_state",
		    "VK_LAYER_LUNARG_image",
		    "VK_LAYER_LUNARG_mem_tracker",
		    "VK_LAYER_LUNARG_object_tracker",
		    "VK_LAYER_LUNARG_param_checker",
		    "VK_LAYER_LUNARG_swapchain",

			// "VK_LAYER_LUNARG_vktrace"

		    "VK_LAYER_GOOGLE_threading",
			"VK_LAYER_GOOGLE_unique_objects",
            // "VK_LAYER_RENDERDOC_Capture",

			// "VK_LAYER_NV_optimus",

			"VK_LAYER_KHRONOS_validation"
	    };

	    static const char* s_deviceLayers[] =
	    {
			// "VK_LAYER_LUNARG_api_dump",
			"VK_LAYER_LUNARG_assistant_layer",
			"VK_LAYER_LUNARG_core_validation",
			// "VK_LAYER_LUNARG_device_simulation",
			// "VK_LAYER_LUNARG_monitor",
			"VK_LAYER_LUNARG_object_tracker",
			"VK_LAYER_LUNARG_parameter_validation",
			// "VK_LAYER_LUNARG_screenshot",
			"VK_LAYER_LUNARG_standard_validation",

		    "VK_LAYER_LUNARG_device_limits",
		    "VK_LAYER_LUNARG_draw_state",
		    "VK_LAYER_LUNARG_image",
		    "VK_LAYER_LUNARG_mem_tracker",
		    "VK_LAYER_LUNARG_object_tracker",
		    "VK_LAYER_LUNARG_param_checker",
		    "VK_LAYER_LUNARG_swapchain",

			// "VK_LAYER_LUNARG_vktrace"

		    "VK_LAYER_GOOGLE_threading",
			"VK_LAYER_GOOGLE_unique_objects",
            // "VK_LAYER_RENDERDOC_Capture",

			// "VK_LAYER_NV_optimus",

			"VK_LAYER_KHRONOS_validation"
		};

        static std::vector<VkLayerProperties> EnumerateLayers()
	    {
		    for (;;) {
			    uint32_t layerCount = 0;
			    auto res = vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
			    if (res != VK_SUCCESS)
				    Throw(VulkanAPIFailure(res, "Failure in during enumeration of Vulkan layer capabilities. You must have an up-to-date Vulkan driver installed."));

			    if (layerCount == 0)
				    return std::vector<VkLayerProperties>();

			    std::vector<VkLayerProperties> layerProps;
			    layerProps.resize(layerCount);
			    res = vkEnumerateInstanceLayerProperties(&layerCount, AsPointer(layerProps.begin()));
			    if (res == VK_INCOMPLETE) continue;	// doc's arent clear as to whether layerCount is updated in this case
                if (res != VK_SUCCESS)
				    Throw(VulkanAPIFailure(res, "Failure in during enumeration of Vulkan layer capabilities. You must have an up-to-date Vulkan driver installed."));

			    return layerProps;
		    }
	    }

        static VkDebugUtilsMessengerEXT msg_callback;
		static bool s_debugInitialized = false;
        static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback( 
			VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
			const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
			void*                                            pUserData)
        {
			if (!Verbose.IsEnabled()) return false;
			const auto* pMsg = pCallbackData->pMessage;
			if (XlFindString(pMsg, "layout")) return false;
            Log(Verbose) << pCallbackData->pMessageIdName << ": " << pMsg << std::endl;
	        return false;
        }
    
        static void debug_init(VkInstance instance)
        {
			assert(!s_debugInitialized);

			VkDebugUtilsMessengerCreateInfoEXT callback1 = {
				VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,  // sType
				NULL,                                                     // pNext
				0,                                                        // flags
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |           // messageSeverity
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
				VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |             // messageType
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
				debug_callback,                                           // pfnUserCallback
				NULL                                                      // pUserData
			};
	
	        auto proc = ((PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr( instance, "vkCreateDebugUtilsMessengerEXT" ));
			assert(proc);
            proc( instance, &callback1, Metal_Vulkan::g_allocationCallbacks, &msg_callback );
			s_debugInitialized = true;
        }
    
        static void debug_destroy(VkInstance instance)
        {
			assert(s_debugInitialized);
	        ((PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr( instance, "vkDestroyDebugUtilsMessengerEXT" ))( instance, msg_callback, 0 );
			s_debugInitialized = false;
        }
    #else
        static void debug_init(VkInstance instance) {}
        static void debug_destroy(VkInstance instance) {}
    #endif

	static VulkanSharedPtr<VkInstance> CreateVulkanInstance()
	{
		auto appname = GetApplicationName();

		VkApplicationInfo app_info = {};
		app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app_info.pNext = NULL;
		app_info.pApplicationName = appname.c_str();
		app_info.applicationVersion = 1;
		app_info.pEngineName = "XLE";
		app_info.engineVersion = 1;
		app_info.apiVersion = VK_HEADER_VERSION_COMPLETE;

		VkInstanceCreateInfo inst_info = {};
		inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		inst_info.pNext = NULL;
		inst_info.flags = 0;
		inst_info.pApplicationInfo = &app_info;
		inst_info.enabledExtensionCount = (uint32_t)dimof(s_instanceExtensions);
		inst_info.ppEnabledExtensionNames = s_instanceExtensions;

        #if defined(ENABLE_DEBUG_EXTENSIONS)
            auto availableLayers = EnumerateLayers();

            std::vector<const char*> filteredLayers;
            for (unsigned c=0; c<dimof(s_instanceLayers); ++c) {
                auto i = std::find_if(
                    availableLayers.begin(), availableLayers.end(),
                    [c](VkLayerProperties layer) { return XlEqString(layer.layerName, s_instanceLayers[c]); });
                if (i != availableLayers.end())
                    filteredLayers.push_back(s_instanceLayers[c]);
            }

            inst_info.enabledLayerCount = (uint32_t)filteredLayers.size();
		    inst_info.ppEnabledLayerNames = AsPointer(filteredLayers.begin());
        #else
            inst_info.enabledLayerCount = 0;
            inst_info.ppEnabledLayerNames = nullptr;
        #endif

		VkInstance rawResult = nullptr;
		VkResult res = vkCreateInstance(&inst_info, Metal_Vulkan::g_allocationCallbacks, &rawResult);
		auto instance = VulkanSharedPtr<VkInstance>(
			rawResult,
			[](VkInstance inst) { debug_destroy(inst); vkDestroyInstance(inst, Metal_Vulkan::g_allocationCallbacks); });
        if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure in Vulkan instance construction. You must have an up-to-date Vulkan driver installed."));

        debug_init(instance.get());
        return std::move(instance);
    }

	static std::vector<VkPhysicalDevice> EnumeratePhysicalDevices(VkInstance vulkan)
	{
		for (;;) {
			uint32_t count = 0;
			auto res = vkEnumeratePhysicalDevices(vulkan, &count, nullptr);
			if (res != VK_SUCCESS)
				Throw(VulkanAPIFailure(res, "Failure in during enumeration of physical devices. You must have an up-to-date Vulkan driver installed."));

			if (count == 0)
				return std::vector<VkPhysicalDevice>();

			std::vector<VkPhysicalDevice> props;
			props.resize(count);
			res = vkEnumeratePhysicalDevices(vulkan, &count, AsPointer(props.begin()));
			if (res == VK_INCOMPLETE) continue;	// doc's arent clear as to whether layerCount is updated in this case
            if (res != VK_SUCCESS)
				Throw(VulkanAPIFailure(res, "Failure in during enumeration of physical devices. You must have an up-to-date Vulkan driver installed."));

			return props;
		}
	}

	static std::vector<VkQueueFamilyProperties> EnumerateQueueFamilyProperties(VkPhysicalDevice dev)
	{
		uint32_t count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
		if (count == 0)
			return std::vector<VkQueueFamilyProperties>();

		std::vector<VkQueueFamilyProperties> props;
		props.resize(count);
		vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, AsPointer(props.begin()));
		return props;
	}

	static const char* AsString(VkPhysicalDeviceType type)
	{
		switch (type)
		{
		case VK_PHYSICAL_DEVICE_TYPE_OTHER: return "Other";
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated";
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "Discrete";
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "Virtual";
		case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
		default: return "Unknown";
		}
	}

	static VulkanSharedPtr<VkSurfaceKHR> CreateSurface(VkInstance vulkan, const void* platformValue)
	{
		#if PLATFORMOS_TARGET  == PLATFORMOS_WINDOWS
			VkWin32SurfaceCreateInfoKHR createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
			createInfo.pNext = NULL;
			createInfo.hinstance = GetModuleHandle(nullptr);
			createInfo.hwnd = (HWND)platformValue;

			VkSurfaceKHR rawResult = nullptr;
			auto res = vkCreateWin32SurfaceKHR(vulkan, &createInfo, Metal_Vulkan::g_allocationCallbacks, &rawResult);
			if (res != VK_SUCCESS)
				Throw(VulkanAPIFailure(res, "Failure in Vulkan surface construction. You must have an up-to-date Vulkan driver installed."));

			// note --	capturing "vulkan" with an unprotected pointer here. We could use a protected
			//			pointer easily enough... But I guess this approach is in-line with Vulkan design ideas.
			return VulkanSharedPtr<VkSurfaceKHR>(
				rawResult,
				[vulkan](VkSurfaceKHR inst) { vkDestroySurfaceKHR(vulkan, inst, Metal_Vulkan::g_allocationCallbacks); });
		#else
			#error Windowing platform not supported
		#endif
	}

	static SelectedPhysicalDevice SelectPhysicalDeviceForRendering(VkInstance vulkan, VkSurfaceKHR surface)
	{
		auto devices = EnumeratePhysicalDevices(vulkan);
		if (devices.empty())
			Throw(Exceptions::BasicLabel("Could not find any Vulkan physical devices. You must have an up-to-date Vulkan driver installed."));

		// Iterate through the list of devices -- and if it matches our requirements, select that device.
		// We're expecting the Vulkan driver to return the devices in priority order.
		for (auto dev:devices) {
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(dev, &props);

			// We need a device with the QUEUE_GRAPHICS bit set, and that supports presenting.
			auto queueProps = EnumerateQueueFamilyProperties(dev);
			for (unsigned qi=0; qi<unsigned(queueProps.size()); ++qi) {
				if (!(queueProps[qi].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;

				// Awkwardly, we need to create the "VkSurfaceKHR" in order to check for
				// compatibility with the physical device. And creating the surface requires
				// a windows handle... So we can't select the physical device (or create the
				// logical device) until we have the windows handle.
				if (surface != nullptr) {
					VkBool32 supportsPresent = false;
					vkGetPhysicalDeviceSurfaceSupportKHR(
						dev, qi, surface, &supportsPresent);
					if (!supportsPresent) continue;
				}

				Log(Verbose)
					<< "Selecting physical device (" << props.deviceName 
					<< "). API Version: (" << props.apiVersion 
					<< "). Driver version: (" << props.driverVersion 
					<< "). Type: (" << AsString(props.deviceType) << ")"
					<< std::endl;
				return SelectedPhysicalDevice { dev, qi };
			}
		}

		Throw(Exceptions::BasicLabel("There are physical Vulkan devices, but none of them support rendering. You must have an up-to-date Vulkan driver installed."));
	}

	static void LogPhysicalDeviceExtensions(std::ostream& str, VkPhysicalDevice physDev)
	{
		VkExtensionProperties extensions[64];
		std::vector<VkExtensionProperties> overflowExtensions;
		IteratorRange<const VkExtensionProperties*> foundExtensions;
		unsigned extPropertyCount = dimof(extensions);
		auto res = vkEnumerateDeviceExtensionProperties(
			physDev, nullptr,
			&extPropertyCount, extensions);
		if (res == VK_INCOMPLETE) {
			res = vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extPropertyCount, nullptr);
			assert(res == VK_SUCCESS);
			overflowExtensions.resize(extPropertyCount);
			res = vkEnumerateDeviceExtensionProperties(
				physDev, nullptr,
				&extPropertyCount, overflowExtensions.data());
			assert(res == VK_SUCCESS);
			foundExtensions = MakeIteratorRange(overflowExtensions);
		} else {
			foundExtensions = MakeIteratorRange(extensions);
		}

		str << "[" << foundExtensions.size() << "] Vulkan physical device extensions" << std::endl;
		for (auto c:foundExtensions)
			str << "  " << c.extensionName << " (" << c.specVersion << ")" << std::endl;
	}

	static VulkanSharedPtr<VkDevice> CreateUnderlyingDevice(SelectedPhysicalDevice physDev)
	{
		VkDeviceQueueCreateInfo queue_info = {};
		queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info.pNext = nullptr;
		queue_info.queueCount = 1;
		// The queue priority value are specific to a single VkDevice -- so it shouldn't affect priorities
		// relative to another application.
		float queue_priorities[1] = { 0.5f };
		queue_info.pQueuePriorities = queue_priorities;
		queue_info.queueFamilyIndex = physDev._renderingQueueFamily;

		VkPhysicalDeviceFeatures physicalDeviceFeatures = {};
		physicalDeviceFeatures.geometryShader = true;
		physicalDeviceFeatures.samplerAnisotropy = true;
		physicalDeviceFeatures.pipelineStatisticsQuery = true;
		// physicalDeviceFeatures.wideLines = true;
		// physicalDeviceFeatures.independentBlend = true;
		// physicalDeviceFeatures.robustBufferAccess = true;
		// physicalDeviceFeatures.multiViewport = true;
		physicalDeviceFeatures.shaderImageGatherExtended = true;
		physicalDeviceFeatures.fragmentStoresAndAtomics = true;
		physicalDeviceFeatures.imageCubeArray = true;

		VkPhysicalDeviceTransformFeedbackFeaturesEXT transformFeedbackFeatures = {};
		transformFeedbackFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
		transformFeedbackFeatures.geometryStreams = true;
		transformFeedbackFeatures.transformFeedback = true;

		VkPhysicalDeviceMultiviewFeatures multiViewFeatures = {};
        multiViewFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
		multiViewFeatures.multiview = true;
		multiViewFeatures.multiviewGeometryShader = true;
        transformFeedbackFeatures.pNext = &multiViewFeatures;

		VkPhysicalDeviceFeatures2KHR enabledFeatures2 = {};
		enabledFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR;
		enabledFeatures2.pNext = &transformFeedbackFeatures;
		enabledFeatures2.features = physicalDeviceFeatures;

		VkDeviceCreateInfo device_info = {};
		device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		device_info.pNext = &enabledFeatures2;
		device_info.queueCreateInfoCount = 1;
		device_info.pQueueCreateInfos = &queue_info;

		std::vector<const char*> extensions;
		for (auto c:s_deviceExtensions) extensions.push_back(c);
		extensions.push_back("VK_EXT_conservative_rasterization");
		device_info.enabledExtensionCount = (uint32_t)extensions.size();
		device_info.ppEnabledExtensionNames = extensions.data();

        #if defined(ENABLE_DEBUG_EXTENSIONS)
            auto availableLayers = EnumerateLayers();
            std::vector<const char*> filteredLayers;
            for (unsigned c=0; c<dimof(s_deviceLayers); ++c) {
                auto i = std::find_if(
                    availableLayers.begin(), availableLayers.end(),
                    [c](VkLayerProperties layer) { return XlEqString(layer.layerName, s_deviceLayers[c]); });
                if (i != availableLayers.end())
                    filteredLayers.push_back(s_deviceLayers[c]);
            }

		    device_info.enabledLayerCount = (uint32)filteredLayers.size();
		    device_info.ppEnabledLayerNames = AsPointer(filteredLayers.begin());
        #else
            device_info.enabledLayerCount = 0;
		    device_info.ppEnabledLayerNames = nullptr;
        #endif

		VkDevice rawResult = nullptr;
		auto res = vkCreateDevice(physDev._dev, &device_info, Metal_Vulkan::g_allocationCallbacks, &rawResult);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while creating Vulkan logical device. You must have an up-to-date Vulkan driver installed."));
		return VulkanSharedPtr<VkDevice>(
			rawResult,
			[](VkDevice dev) { vkDestroyDevice(dev, Metal_Vulkan::g_allocationCallbacks); });
	}

    Device::Device()
    {
            // todo -- we need to do this in a bind to DLL step
        Metal_Vulkan::InitFormatConversionTables();

			//
			//	Create the instance. This will attach the Vulkan DLL. If there are no valid Vulkan drivers
			//	available, it will throw an exception here.
			//
		_instance = CreateVulkanInstance();
        _physDev = { nullptr, ~0u };

			// We can't create the underlying device immediately... Because we need a pointer to
			// the "platformValue" (window handle) in order to check for physical device capabilities.
			// So, we must do a lazy initialization of _underlying.
    }

    Device::~Device()
    {
		_foregroundPrimaryContext.reset();
		Metal_Vulkan::Internal::VulkanGlobalsTemp::GetInstance()._globalPools = nullptr;

        Metal_Vulkan::SetDefaultObjectFactory(nullptr);
		/*
			While exiting post a vulkan failure (eg, device lost), we will can end up in an infinite loop if we stall here
		if (_underlying.get())
			vkDeviceWaitIdle(_underlying.get());*/
    }

    static std::vector<VkSurfaceFormatKHR> GetSurfaceFormats(VkPhysicalDevice physDev, VkSurfaceKHR surface)
    {
        for (;;)
        {
            uint32_t count;
            auto res = vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &count, nullptr);
            if (res != VK_SUCCESS)
                Throw(VulkanAPIFailure(res, "Failure while querying physical device surface formats"));
            if (count == 0) return std::vector<VkSurfaceFormatKHR>();

            std::vector<VkSurfaceFormatKHR> result;
            result.resize(count);
            res = vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &count, AsPointer(result.begin()));
            if (res == VK_INCOMPLETE) continue;
            if (res != VK_SUCCESS)
                Throw(VulkanAPIFailure(res, "Failure while querying physical device surface formats"));

            return result;
        }
    }

    static std::vector<VkPresentModeKHR> GetPresentModes(VkPhysicalDevice physDev, VkSurfaceKHR surface)
    {
        for (;;)
        {
            uint32_t count;
            auto res = vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &count, nullptr);
            if (res != VK_SUCCESS)
                Throw(VulkanAPIFailure(res, "Failure while querying surface present modes"));
            if (count == 0) return std::vector<VkPresentModeKHR>();

            std::vector<VkPresentModeKHR> result;
            result.resize(count);
            res = vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &count, AsPointer(result.begin()));
            if (res == VK_INCOMPLETE) continue;
            if (res != VK_SUCCESS)
                Throw(VulkanAPIFailure(res, "Failure while querying surface present modes"));

            return result;
        }
    }

    static VkPresentModeKHR SelectPresentMode(IteratorRange<const VkPresentModeKHR*> availableModes)
    {
        // If mailbox mode is available, use it, as is the lowest-latency non-
        // tearing mode.  If not, try IMMEDIATE which will usually be available,
        // and is fastest (though it tears).  If not, fall back to FIFO which is
        // always available.
        VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        for (auto pm:availableModes) {
            if (pm == VK_PRESENT_MODE_MAILBOX_KHR) {
                swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                break;
            }
            if ((swapchainPresentMode != VK_PRESENT_MODE_MAILBOX_KHR) &&
                (pm == VK_PRESENT_MODE_IMMEDIATE_KHR)) {
                swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
        }
        return swapchainPresentMode;
    }

    static VkQueue GetQueue(VkDevice dev, unsigned queueFamilyIndex, unsigned queueIndex=0)
    {
        VkQueue queue = nullptr;
        vkGetDeviceQueue(dev, queueFamilyIndex, queueIndex, &queue);
        return queue;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////////////

    void Device::DoSecondStageInit(VkSurfaceKHR surface)
    {
        if (!_underlying) {
			_physDev = SelectPhysicalDeviceForRendering(_instance.get(), surface);
			#if defined(OSSERVICES_ENABLE_LOG)
				LogPhysicalDeviceExtensions(Log(Verbose), _physDev._dev);
			#endif
			_underlying = CreateUnderlyingDevice(_physDev);
			auto extensionFunctions = std::make_shared<Metal_Vulkan::ExtensionFunctions>(_instance.get());
			_objectFactory = Metal_Vulkan::ObjectFactory(_physDev._dev, _underlying, extensionFunctions);

			// Set up the object factory with a default destroyer that tracks the current
			// GPU frame progress
			_graphicsQueue = std::make_shared<Metal_Vulkan::SubmissionQueue>(_objectFactory, GetQueue(_underlying.get(), _physDev._renderingQueueFamily));
			auto destroyer = _objectFactory.CreateMarkerTrackingDestroyer(_graphicsQueue->GetTracker());
			_objectFactory.SetDefaultDestroyer(destroyer);
            Metal_Vulkan::SetDefaultObjectFactory(&_objectFactory);

            _pools._mainDescriptorPool = Metal_Vulkan::DescriptorPool(_objectFactory, _graphicsQueue->GetTracker());
			_pools._longTermDescriptorPool = Metal_Vulkan::DescriptorPool(_objectFactory, _graphicsQueue->GetTracker());
			_pools._renderPassPool = Metal_Vulkan::VulkanRenderPassPool(_objectFactory);
            _pools._mainPipelineCache = _objectFactory.CreatePipelineCache();
            _pools._dummyResources = Metal_Vulkan::DummyResources(_objectFactory);
			_pools._temporaryStorageManager = std::make_unique<Metal_Vulkan::TemporaryStorageManager>(_objectFactory, _graphicsQueue->GetTracker());

            _foregroundPrimaryContext = std::make_shared<ThreadContext>(
				shared_from_this(), 
				_graphicsQueue,
                Metal_Vulkan::CommandPool(_objectFactory, _physDev._renderingQueueFamily, false, _graphicsQueue->GetTracker()));
			_foregroundPrimaryContext->AttachDestroyer(destroyer);

			// We need to ensure that the "dummy" resources get their layout change to complete initialization
			_pools._dummyResources.CompleteInitialization(*_foregroundPrimaryContext->GetMetalContext());
		}
    }

    struct SwapChainProperties
    {
        VkFormat                        _fmt;
        VkExtent2D                      _extent;
        uint32_t                        _desiredNumberOfImages;
        VkSurfaceTransformFlagBitsKHR   _preTransform;
        VkPresentModeKHR                _presentMode;
    };

    static SwapChainProperties DecideSwapChainProperties(
        VkPhysicalDevice phyDev, VkSurfaceKHR surface,
        unsigned requestedWidth, unsigned requestedHeight)
    {
        SwapChainProperties result;

        // The following is based on the "initswapchain" sample from the vulkan SDK
        auto fmts = GetSurfaceFormats(phyDev, surface);
        assert(!fmts.empty());  // expecting at least one

        // If the format list includes just one entry of VK_FORMAT_UNDEFINED,
        // the surface has no preferred format.  Otherwise, at least one
        // supported format will be returned.
		//
		// Sometimes we get both an SRGB & non-SRGB format. Let's prefer the
		// LDR SRGB format, if we can find one.
        result._fmt = VK_FORMAT_UNDEFINED;

		for (auto f:fmts)
			if (f.format == VK_FORMAT_B8G8R8A8_SRGB)
				result._fmt = VK_FORMAT_B8G8R8A8_SRGB;

		if (result._fmt == VK_FORMAT_UNDEFINED) {
			for (auto f:fmts)
				if (f.format == VK_FORMAT_B8G8R8_SRGB)
					result._fmt = VK_FORMAT_B8G8R8_SRGB;
		}

		if (result._fmt == VK_FORMAT_UNDEFINED) {
			for (auto f:fmts)
				if (f.format != VK_FORMAT_UNDEFINED)
					result._fmt = f.format;
		}

		if (result._fmt == VK_FORMAT_UNDEFINED)
			result._fmt = VK_FORMAT_B8G8R8A8_SRGB;

        VkSurfaceCapabilitiesKHR surfCapabilities;
        auto res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phyDev, surface, &surfCapabilities);
        assert(res == VK_SUCCESS); (void)res;

        auto presentModes = GetPresentModes(phyDev, surface);
        result._presentMode = SelectPresentMode(MakeIteratorRange(presentModes));

        // width and height are either both -1, or both not -1.
        if (surfCapabilities.currentExtent.width == (uint32_t)-1) {
            // If the surface size is undefined, the size is set to
            // the size of the images requested.
            result._extent.width = requestedWidth;
            result._extent.height = requestedHeight;
        } else {
            // If the surface size is defined, the swap chain size must match
            result._extent = surfCapabilities.currentExtent;
        }
        
        // Determine the number of VkImage's to use in the swap chain (we desire to
        // own only 1 image at a time, besides the images being displayed and
        // queued for display):
        result._desiredNumberOfImages = surfCapabilities.minImageCount + 1;
        if (surfCapabilities.maxImageCount > 0)
            result._desiredNumberOfImages = std::min(result._desiredNumberOfImages, surfCapabilities.maxImageCount);

        // setting "preTransform" to current transform... but clearing out other bits if the identity bit is set
        result._preTransform = 
            (surfCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
            ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : surfCapabilities.currentTransform;
        return result;
    }

    static VulkanSharedPtr<VkSwapchainKHR> CreateUnderlyingSwapChain(
        VkDevice dev, VkSurfaceKHR  surface, 
        const SwapChainProperties& props)
    {
        // finally, fill in our SwapchainCreate structure
        VkSwapchainCreateInfoKHR swapChainInfo = {};
        swapChainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapChainInfo.pNext = nullptr;
        swapChainInfo.surface = surface;
        swapChainInfo.minImageCount = props._desiredNumberOfImages;
        swapChainInfo.imageFormat = props._fmt;
        swapChainInfo.imageExtent.width = props._extent.width;
        swapChainInfo.imageExtent.height = props._extent.height;
        swapChainInfo.preTransform = props._preTransform;
        swapChainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapChainInfo.imageArrayLayers = 1;
        swapChainInfo.presentMode = props._presentMode;
        swapChainInfo.oldSwapchain = nullptr;
        swapChainInfo.clipped = true;
        swapChainInfo.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        swapChainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapChainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapChainInfo.queueFamilyIndexCount = 0;
        swapChainInfo.pQueueFamilyIndices = nullptr;

        VkSwapchainKHR swapChainRaw = nullptr;
        auto res = vkCreateSwapchainKHR(dev, &swapChainInfo, Metal_Vulkan::g_allocationCallbacks, &swapChainRaw);
        VulkanSharedPtr<VkSwapchainKHR> result(
            swapChainRaw,
            [dev](VkSwapchainKHR chain) { vkDestroySwapchainKHR(dev, chain, Metal_Vulkan::g_allocationCallbacks); } );
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failure while creating swap chain"));
        return result;
    }

    std::unique_ptr<IPresentationChain> Device::CreatePresentationChain(
		const void* platformValue, const PresentationChainDesc& desc)
    {
		auto surface = CreateSurface(_instance.get(), platformValue);
		DoSecondStageInit(surface.get());

        // double check to make sure our physical device is compatible with this surface
        VkBool32 supportsPresent = false;
		auto res = vkGetPhysicalDeviceSurfaceSupportKHR(
			_physDev._dev, _physDev._renderingQueueFamily, surface.get(), &supportsPresent);
		if (res != VK_SUCCESS || !supportsPresent) 
            Throw(::Exceptions::BasicLabel("Presentation surface is not compatible with selected physical device. This may occur if the wrong physical device is selected, and it cannot render to the output window."));
        
        auto finalChain = std::make_unique<PresentationChain>(
            _objectFactory, std::move(surface), VectorPattern<unsigned, 2>{desc._width, desc._height}, 
			_physDev._renderingQueueFamily, platformValue);
        return std::move(finalChain);
    }

    std::shared_ptr<IThreadContext> Device::GetImmediateContext()
    {
        // Note that when we do the second stage init through this path,
        // we will not verify the selected physical device against a
        // presentation surface.
        DoSecondStageInit();
		return _foregroundPrimaryContext;
    }

    std::unique_ptr<IThreadContext> Device::CreateDeferredContext()
    {
        // Note that when we do the second stage init through this path,
        // we will not verify the selected physical device against a
        // presentation surface.
        DoSecondStageInit();
		return std::make_unique<ThreadContext>(
            shared_from_this(), 
            _graphicsQueue,
            Metal_Vulkan::CommandPool(_objectFactory, _physDev._renderingQueueFamily, false, _graphicsQueue->GetTracker()));
    }

	IResourcePtr Device::CreateResource(
		const ResourceDesc& desc,
		const std::function<SubResourceInitData(SubResourceId)>& initData)
	{
		return Metal_Vulkan::Internal::CreateResource(_objectFactory, desc, initData);
	}

	FormatCapability    Device::QueryFormatCapability(Format format, BindFlag::BitField bindingType)
	{
		return FormatCapability::Supported;
	}

	std::shared_ptr<ILowLevelCompiler>		Device::CreateShaderCompiler()
	{
		return CreateShaderCompiler(VulkanCompilerConfiguration{});
	}

	std::shared_ptr<ILowLevelCompiler>		Device::CreateShaderCompiler(const VulkanCompilerConfiguration& cfg)
	{
		Metal_Vulkan::Internal::VulkanGlobalsTemp::GetInstance()._legacyRegisterBindings = cfg._legacyBindings;
		return Metal_Vulkan::CreateLowLevelShaderCompiler(*this, cfg);
	}

	std::shared_ptr<Metal_Vulkan::IAsyncTracker> Device::GetAsyncTracker()
	{
		return _graphicsQueue->GetTracker();
	}

	void Device::Stall()
	{
		assert(0);	// unimplemented
	}

	void Device::PrepareForDestruction()
	{
		vkDeviceWaitIdle(_underlying.get());
	}

    static const char* s_underlyingApi = "Vulkan";
        
    DeviceDesc Device::GetDesc()
    {
		auto libVersion = ConsoleRig::GetLibVersionDesc();
        return DeviceDesc{s_underlyingApi, libVersion._versionString, libVersion._buildDateString};
    }

	uint64_t Device::GetGUID() const
	{
		return (uint64_t)_underlying.get();		// we just need to return something unique that will distinguish us from any other devices present in the system
	}

	std::shared_ptr<ICompiledPipelineLayout> Device::CreatePipelineLayout(const PipelineLayoutInitializer& desc)
	{
		DoSecondStageInit();
		Metal_Vulkan::Internal::ValidatePipelineLayout(_physDev._dev, desc);

		using DescriptorSetBinding = Metal_Vulkan::CompiledPipelineLayout::DescriptorSetBinding;
		using PushConstantsBinding = Metal_Vulkan::CompiledPipelineLayout::PushConstantsBinding;

		if (!_pools._descriptorSetLayoutCache)
			_pools._descriptorSetLayoutCache = Metal_Vulkan::Internal::CreateCompiledDescriptorSetLayoutCache();

		DescriptorSetBinding descSetBindings[desc.GetDescriptorSets().size()];
		for (unsigned c=0; c<desc.GetDescriptorSets().size(); ++c) {
			auto& srcBinding = desc.GetDescriptorSets()[c];
			descSetBindings[c]._name = srcBinding._name;
			auto compiled = _pools._descriptorSetLayoutCache->CompileDescriptorSetLayout(
				srcBinding._signature,
				srcBinding._name,
				srcBinding._pipelineType == PipelineType::Graphics ? VK_SHADER_STAGE_ALL_GRAPHICS : VK_SHADER_STAGE_COMPUTE_BIT );
			descSetBindings[c]._layout = compiled->_layout;
			descSetBindings[c]._blankDescriptorSet = compiled->_blankBindings;
			#if defined(VULKAN_VERBOSE_DEBUG)
				descSetBindings[c]._blankDescriptorSetDebugInfo = compiled->_blankBindingsDescription;
			#endif
		}

		PushConstantsBinding pushConstantBinding[desc.GetPushConstants().size()];
		for (unsigned c=0; c<desc.GetPushConstants().size(); ++c) {
			auto& srcBinding = desc.GetPushConstants()[c];
			pushConstantBinding[c]._name = srcBinding._name;
			pushConstantBinding[c]._cbSize = srcBinding._cbSize;
			pushConstantBinding[c]._stageFlags = Metal_Vulkan::Internal::AsVkShaderStageFlags(srcBinding._shaderStage);
			pushConstantBinding[c]._cbElements = MakeIteratorRange(srcBinding._cbElements);
		}

		return std::make_shared<Metal_Vulkan::CompiledPipelineLayout>(
			_objectFactory,
			MakeIteratorRange(descSetBindings, &descSetBindings[desc.GetDescriptorSets().size()]),
			MakeIteratorRange(pushConstantBinding, &pushConstantBinding[desc.GetPushConstants().size()]),
			desc);
	}

	std::shared_ptr<IDescriptorSet> Device::CreateDescriptorSet(const DescriptorSetInitializer& desc)
	{
		VkShaderStageFlags shaderStages = desc._pipelineType == PipelineType::Graphics ? VK_SHADER_STAGE_ALL_GRAPHICS : VK_SHADER_STAGE_COMPUTE_BIT;
		auto descSetLayout = _pools._descriptorSetLayoutCache->CompileDescriptorSetLayout(*desc._signature, {}, shaderStages); // don't have the name available here
		return std::make_shared<Metal_Vulkan::CompiledDescriptorSet>(
			_objectFactory, _pools,
			descSetLayout->_layout,
			shaderStages,
			desc._slotBindings,
			desc._bindItems);
	}

	std::shared_ptr<ISampler> Device::CreateSampler(const SamplerDesc& desc)
	{
		return std::make_shared<Metal_Vulkan::SamplerState>(_objectFactory, desc);
	}

    //////////////////////////////////////////////////////////////////////////////////////////////////

	void*   Device::QueryInterface(size_t guid)
	{
		if (guid == typeid(IDeviceVulkan).hash_code())
			return (IDeviceVulkan*)this;
		if (guid == typeid(Device).hash_code())
			return (Device*)this;
		if (guid == typeid(IDevice).hash_code())
			return (IDevice*)this;
		return nullptr;
	}

	VkInstance Device::GetVulkanInstance() { return _instance.get(); }

    //////////////////////////////////////////////////////////////////////////////////////////////////

    void            PresentationChain::Resize(unsigned newWidth, unsigned newHeight)
    {
        // We need to destroy and recreate the presentation chain here.
        auto props = DecideSwapChainProperties(_factory->GetPhysicalDevice(), _surface.get(), newWidth, newHeight);
        if (newWidth == _bufferDesc._width && newHeight == _bufferDesc._height)
            return;

        // We can't delete the old swap chain while the device is using it. The easiest
        // way to get around this is to just synchronize with the GPU here.
        // Since a resize is uncommon, this should not be a issue. It might be better to wait for
        // a queue idle -- but we don't have access to the VkQueue from here.
        vkDeviceWaitIdle(_device.get());
        _swapChain.reset();
        _images.clear();

        _swapChain = CreateUnderlyingSwapChain(_device.get(), _surface.get(), props);
        _bufferDesc = TextureDesc::Plain2D(props._extent.width, props._extent.height, Metal_Vulkan::AsFormat(props._fmt));    

        *_desc = { _bufferDesc._width, _bufferDesc._height, _bufferDesc._format, _bufferDesc._samples, BindFlag::RenderTarget | BindFlag::PresentationSrc };

        BuildImages();
    }

    const std::shared_ptr<PresentationChainDesc>& PresentationChain::GetDesc() const
    {
		return _desc;
    }

    Metal_Vulkan::ResourceView* PresentationChain::AcquireNextImage(Metal_Vulkan::SubmissionQueue& queue)
    {
        _activePresentSync = (_activePresentSync+1) % dimof(_presentSyncs);
        auto& sync = _presentSyncs[_activePresentSync];
		if (sync._presentFence.has_value())
			queue.WaitForFence(sync._presentFence.value());
		sync._presentFence = {};

        // note --  Due to the timeout here, we get a synchronise here.
        //          This will prevent issues when either the GPU or CPU is
        //          running ahead of the other... But we could do better by
        //          using the semaphores
        //
        // Note that we must handle the VK_NOT_READY result... Some implementations
        // may not block, even when timeout is some large value.
        // As stated in the documentation, we shouldn't rely on this function for
        // synchronisation -- instead, we should write an algorithm that will insert 
        // stalls as necessary
        uint32_t nextImageIndex = ~0x0u;
        const auto timeout = UINT64_MAX;
        auto res = vkAcquireNextImageKHR(
            _device.get(), _swapChain.get(), 
            timeout,
            sync._onAcquireComplete.get(), VK_NULL_HANDLE,
            &nextImageIndex);
        _activeImageIndex = nextImageIndex;

        // TODO: Deal with the VK_SUBOPTIMAL_KHR and VK_ERROR_OUT_OF_DATE_KHR
        // return codes
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failure during acquire next image"));

        return &_images[_activeImageIndex]._rtv;
    }

    static std::vector<VkImage> GetImages(VkDevice dev, VkSwapchainKHR swapChain)
    {
        for (;;)
        {
            uint32_t count;
            auto res = vkGetSwapchainImagesKHR(dev, swapChain, &count, nullptr);
            if (res != VK_SUCCESS)
                Throw(VulkanAPIFailure(res, "Failure while querying physical device surface formats"));
            if (count == 0) return std::vector<VkImage>();

            std::vector<VkImage> rawPtrs;
            rawPtrs.resize(count);
            res = vkGetSwapchainImagesKHR(dev, swapChain, &count, AsPointer(rawPtrs.begin()));
            if (res == VK_INCOMPLETE) continue;
            if (res != VK_SUCCESS)
                Throw(VulkanAPIFailure(res, "Failure while querying physical device surface formats"));

            // We don't have to destroy the images with VkDestroyImage -- they will be destroyed when the
            // swapchain is destroyed.
            return rawPtrs;
        }
    }

	void PresentationChain::PresentToQueue(Metal_Vulkan::SubmissionQueue& queue)
	{
		if (_activeImageIndex > unsigned(_images.size())) return;
		auto& sync = _presentSyncs[_activePresentSync];
		const VkSemaphore waitSema_2[] = { sync._onCommandBufferComplete.get() };
		queue.Present(_swapChain.get(), _activeImageIndex, MakeIteratorRange(waitSema_2));
		_activeImageIndex = ~0x0u;
	}

    void PresentationChain::BuildImages()
    {
        auto images = GetImages(_device.get(), _swapChain.get());
        _images.reserve(images.size());
        for (auto& i:images) {
            TextureViewDesc window{
                _bufferDesc._format, 
                TextureViewDesc::SubResourceRange{0, _bufferDesc._mipCount},
				TextureViewDesc::SubResourceRange{0, _bufferDesc._arrayCount},
				_bufferDesc._dimensionality};
            auto resDesc = CreateDesc(
                BindFlag::PresentationSrc | BindFlag::RenderTarget, 0u, GPUAccess::Write, 
                _bufferDesc, "presentationimage");
            auto resPtr = std::make_shared<Metal_Vulkan::Resource>(i, resDesc);
            _images.emplace_back(
                Image { i, Metal_Vulkan::ResourceView(*_factory, resPtr, BindFlag::RenderTarget, window) });
        }
    }

    PresentationChain::PresentationChain(
		Metal_Vulkan::ObjectFactory& factory,
        VulkanSharedPtr<VkSurfaceKHR> surface, 
		VectorPattern<unsigned, 2> extent,
		unsigned queueFamilyIndex,
        const void* platformValue)
    : _surface(std::move(surface))
    , _device(factory.GetDevice())
    , _factory(&factory)
    , _platformValue(platformValue)
	, _primaryBufferPool(factory, queueFamilyIndex, true, nullptr)
    {
        _activeImageIndex = ~0x0u;
        auto props = DecideSwapChainProperties(factory.GetPhysicalDevice(), _surface.get(), extent[0], extent[1]);
        _swapChain = CreateUnderlyingSwapChain(_device.get(), _surface.get(), props);

        _bufferDesc = TextureDesc::Plain2D(props._extent.width, props._extent.height, Metal_Vulkan::AsFormat(props._fmt));
		_desc = std::make_shared<PresentationChainDesc>();
		_desc->_width = _bufferDesc._width;
		_desc->_height = _bufferDesc._height;
        _desc->_format = _bufferDesc._format;
		_desc->_samples = _bufferDesc._samples;
		_desc->_bindFlags = BindFlag::RenderTarget | BindFlag::PresentationSrc;

        // We need to get pointers to each image and build the synchronization semaphores
        BuildImages();

        // Create the synchronisation primitives
        // This pattern is similar to the "Hologram" sample in the Vulkan SDK
        for (unsigned c=0; c<dimof(_presentSyncs); ++c) {
            _presentSyncs[c]._onCommandBufferComplete = factory.CreateSemaphore();
            _presentSyncs[c]._onAcquireComplete = factory.CreateSemaphore();
            _presentSyncs[c]._presentFence = {};
        }
		for (unsigned c = 0; c<dimof(_primaryBuffers); ++c)
			_primaryBuffers[c] = _primaryBufferPool.Allocate(Metal_Vulkan::CommandBufferType::Primary);
        _activePresentSync = 0;
    }

    PresentationChain::~PresentationChain()
    {
		_images.clear();
		_swapChain.reset();
		_device.reset();
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////

    render_dll_export std::shared_ptr<IDevice>    CreateDevice()
    {
        return std::make_shared<Device>();
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////

	Metal_Vulkan::IAsyncTracker::Marker ThreadContext::QueuePrimaryContext(IteratorRange<const VkSemaphore*> completionSignals)
	{
		auto immediateCommands = _metalContext->ResolveCommandList();
		return CommitPrimaryCommandBufferToQueue_Internal(*immediateCommands, completionSignals);
	}

	void ThreadContext::CommitPrimaryCommandBufferToQueue(Metal_Vulkan::CommandList& cmdList)
	{
		CommitPrimaryCommandBufferToQueue_Internal(cmdList, {});
	}

	Metal_Vulkan::IAsyncTracker::Marker ThreadContext::CommitPrimaryCommandBufferToQueue_Internal(
		Metal_Vulkan::CommandList& cmdList,
		IteratorRange<const VkSemaphore*> completionSignals)
	{
		if (!_interimCommandBufferComplete)
			_interimCommandBufferComplete = _factory->CreateSemaphore();

		VkSemaphore waitSema[2];
		VkPipelineStageFlags waitStages[2];
		unsigned waitCount = 0;
		if (_nextQueueShouldWaitOnAcquire != VK_NULL_HANDLE) {
			waitSema[waitCount] = _nextQueueShouldWaitOnAcquire;
			waitStages[waitCount] = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			++waitCount;
		}
		if (_nextQueueShouldWaitOnInterimBuffer) {
			waitSema[waitCount] = _interimCommandBufferComplete.get();
			waitStages[waitCount] = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			++waitCount;
		}

		auto result = _submissionQueue->Submit(
			cmdList,
			completionSignals,
			MakeIteratorRange(waitSema, &waitSema[waitCount]),
			MakeIteratorRange(waitStages, &waitStages[waitCount]));

		_nextQueueShouldWaitOnAcquire = VK_NULL_HANDLE;
		_nextQueueShouldWaitOnInterimBuffer = false;
		return result;
	}

	IResourcePtr    ThreadContext::BeginFrame(IPresentationChain& presentationChain)
	{
		// Our immediate context may have command list already, if it's been used
		// either before the first frame, or between 2 frames. Normally we switch
		// the immediate metal context over to using the "primary buffer" associated
		// with the swap chain.
		//
		// So if we have any existing command list content, that's got to be submitted
		// to the queue, and it must be processed before we get onto the main primary
		// buffer content. That requires some more submit and 
		//
		// Most clients would be better off using a different DeviceContext for the commands
		// in this buffer. These commands might be (for example) for initialization, or
		// even drawing to a shadow texture or something like that. If so, we don't necessarily
		// want to delay all commands in the primary buffer until this one is complete. It
		// would be better to synchronize only those parts that rely on the resources
		// writen to by this buffer. That's something we can do, by separating it out
		// into a different context
		if (_metalContext->HasActiveCommandList()) {
			if (!_interimCommandBufferComplete)
				_interimCommandBufferComplete = _factory->CreateSemaphore();
			VkSemaphore signalSema[] = { _interimCommandBufferComplete.get() };
			QueuePrimaryContext(MakeIteratorRange(signalSema));
			_nextQueueShouldWaitOnInterimBuffer = true;
		}

		PresentationChain* swapChain = checked_cast<PresentationChain*>(&presentationChain);
		auto nextImage = swapChain->AcquireNextImage(*_submissionQueue);
		_nextQueueShouldWaitOnAcquire = swapChain->GetSyncs()._onAcquireComplete.get();

		{
			auto cmdList = swapChain->SharePrimaryBuffer();
			auto res = vkResetCommandBuffer(cmdList.get(), VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
			if (res != VK_SUCCESS)
				Throw(VulkanAPIFailure(res, "Failure while resetting command buffer"));
			_metalContext->BeginCommandList(std::move(cmdList), _submissionQueue->GetTracker());
		}

        return nextImage->GetResource();
	}

	void            ThreadContext::Present(IPresentationChain& chain)
	{
		auto* swapChain = checked_cast<PresentationChain*>(&chain);
		auto& syncs = swapChain->GetSyncs();
		assert(!syncs._presentFence);

		//////////////////////////////////////////////////////////////////

		VkSemaphore signalSema[] = { syncs._onCommandBufferComplete.get() };
		syncs._presentFence = QueuePrimaryContext(MakeIteratorRange(signalSema));

		PumpDestructionQueues();

		//////////////////////////////////////////////////////////////////
		// Finally, we can queue the present
		//		-- do it here to allow it to run in parallel as much as possible
		swapChain->PresentToQueue(*_submissionQueue);
	}

	void	ThreadContext::CommitCommands(CommitCommandsFlags::BitField flags)
	{
		// Queue any commands that are prepared, and wait for the GPU to complete
		// processing them
		//
		// Note that we want to wait not just for any commands that are in _metalContext
		// now; but also any other command buffers that have already been submitted
		// and are still being processed
		bool waitForCompletion = !!(flags & CommitCommandsFlags::WaitForCompletion);
		if (_metalContext->HasActiveCommandList()) {
			if (!_interimCommandBufferComplete)
				_interimCommandBufferComplete = _factory->CreateSemaphore();

			VkSemaphore signalSema[] = { _interimCommandBufferComplete.get() };
			auto fenceToWaitFor = QueuePrimaryContext(MakeIteratorRange(signalSema));
			_nextQueueShouldWaitOnInterimBuffer = true;

			if (waitForCompletion) {
				_submissionQueue->WaitForFence(fenceToWaitFor);
			}
		} else {
			// note tht if we don't have an active command list, and flags is WaitForCompletion, we still don't actually wait for the GPU to catchup to any previously committed command lists
			// however, we still flush out the destruction queues, etc
		}

			
		// We need to flush the destruction queues at some point for clients that never actually call Present
		// We have less control over the frequency of CommitCommands, though, so it's going to be less clear
		// when is the right time to call it
		PumpDestructionQueues();
	}

	void ThreadContext::PumpDestructionQueues()
	{
		if (_destrQueue) {
			_submissionQueue->GetTracker()->UpdateConsumer();
			_destrQueue->Flush();
			_globalPools->_mainDescriptorPool.FlushDestroys();
			_globalPools->_longTermDescriptorPool.FlushDestroys();
			_globalPools->_temporaryStorageManager->FlushDestroys();
		}
		_renderingCommandPool.FlushDestroys();
	}

    bool ThreadContext::IsImmediate() const
    {
        return _destrQueue != nullptr;
    }

    auto ThreadContext::GetStateDesc() const -> ThreadContextStateDesc
    {
		// note; we can't get the viewport state here; or at least it's a bit ambigious (since we could have multiple viewports)
        // const auto& view = _metalContext->GetBoundViewport();
        // return ThreadContextStateDesc { {(unsigned)view.Width, (unsigned)view.Height}, _frameId };
		return ThreadContextStateDesc { {0, 0}, _frameId };
    }

	void ThreadContext::InvalidateCachedState() const {}

	IAnnotator& ThreadContext::GetAnnotator()
	{
		if (!_annotator) {
			auto d = _device.lock();
			assert(d);
			_annotator = CreateAnnotator(*d, shared_from_this());
		}
		return *_annotator;
	}

	void ThreadContext::AttachDestroyer(const std::shared_ptr<Metal_Vulkan::IDestructionQueue>& queue) { _destrQueue = queue; }

    ThreadContext::ThreadContext(
		std::shared_ptr<Device> device,
		std::shared_ptr<Metal_Vulkan::SubmissionQueue> submissionQueue,
        Metal_Vulkan::CommandPool&& cmdPool)
    : _device(device)
	, _frameId(0)
    , _renderingCommandPool(std::move(cmdPool))
	, _factory(&device->GetObjectFactory())
	, _globalPools(&device->GetGlobalPools())
	, _submissionQueue(submissionQueue)
	, _underlyingDevice(device->GetUnderlyingDevice())
    {
		_metalContext = std::make_shared<Metal_Vulkan::DeviceContext>(
			device->GetObjectFactory(), device->GetGlobalPools(), 
            _renderingCommandPool, Metal_Vulkan::CommandBufferType::Primary);
	}

    ThreadContext::~ThreadContext() 
	{
		_metalContext.reset();
		_annotator.reset();
		_submissionQueue.reset();
		_destrQueue.reset();
	}

    std::shared_ptr<IDevice> ThreadContext::GetDevice() const
    {
        return _device.lock();
    }

    void ThreadContext::IncrFrameId()
    {
        ++_frameId;
    }

    void*   ThreadContext::QueryInterface(size_t guid)
    {
        if (guid == typeid(IThreadContextVulkan).hash_code()) { return (IThreadContextVulkan*)this; }
		if (guid == typeid(ThreadContext).hash_code()) { return (ThreadContext*)this; }
		if (guid == typeid(IThreadContext).hash_code()) { return (IThreadContext*)this; }
        return nullptr;
    }

    const std::shared_ptr<Metal_Vulkan::DeviceContext>& ThreadContext::GetMetalContext()
    {
		if (!_metalContext->HasActiveCommandList())
			_metalContext->BeginCommandList(_submissionQueue->GetTracker());
        return _metalContext;
    }
}}

namespace RenderCore
{
	IDeviceVulkan::~IDeviceVulkan() {}
	IThreadContextVulkan::~IThreadContextVulkan() {}
}
