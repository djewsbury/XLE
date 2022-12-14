// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeviceVulkan.h"
#include "../IAnnotator.h"
#include "../Format.h"
#include "../DeviceInitialization.h"
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
#include "Metal/CommandList.h"
#include "../../OSServices/Log.h"
#include "../../ConsoleRig/GlobalServices.h"		// (for GetLibVersionDesc())
#include "../../OSServices/AttachableLibrary.h"		// (for GetLibVersionDesc())
#include "../../Utility/Threading/ThreadingUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/StreamUtils.h"
#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../Core/SelectConfiguration.h"
#include <memory>

namespace RenderCore { namespace Metal_Vulkan
{
	class GlobalsContainer
	{
	public:
		ObjectFactory _objectFactory;
		GlobalPools _pools;
	};

	ConsoleRig::WeakAttachablePtr<GlobalsContainer> s_globalsContainer;

	ObjectFactory& GetObjectFactory(IDevice& device) { return s_globalsContainer.lock()->_objectFactory; }
	ObjectFactory& GetObjectFactory(DeviceContext&) { return s_globalsContainer.lock()->_objectFactory; }
	ObjectFactory& GetObjectFactory() { return s_globalsContainer.lock()->_objectFactory; }
	GlobalPools& GetGlobalPools() { return s_globalsContainer.lock()->_pools; }

	VkImageUsageFlags AsImageUsageFlags(BindFlag::BitField bindFlags);
}}

namespace RenderCore { namespace ImplVulkan
{
    using VulkanAPIFailure = Metal_Vulkan::VulkanAPIFailure;

	std::unique_ptr<IAnnotator> CreateAnnotator(IDevice& device, IThreadContext& threadContext);
	void ReleaseThreadContext(IAnnotator&);

	static std::string GetApplicationName()
	{
		return ConsoleRig::CrossModule::GetInstance()._services.CallDefault<std::string>(
			ConstHash64<'appn', 'ame'>::Value, std::string("<<unnamed>>"));
	}

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

    #if defined(VULKAN_ENABLE_DEBUG_EXTENSIONS)
		class DebugMessageHandler
		{
		public:
			static OSServices::MessageTarget<> VulkanMsgTarget;

			static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback( 
				VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
				VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
				const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
				void*                                            pUserData)
			{
				if (!VulkanMsgTarget.IsEnabled()) return false;

				const auto* pMsg = pCallbackData->pMessage;
				auto* handler = (DebugMessageHandler*)pUserData;

				bool report = true;
				if (handler->_suppressSpamRules & messageSeverity) {
					ScopedLock(handler->_suppressSpamLock);
					auto now = std::chrono::steady_clock::now();
					constexpr std::chrono::steady_clock::duration timeBetweenDuplicateMsgs = std::chrono::seconds(5);
					constexpr std::chrono::steady_clock::duration timeBetweenSuppressionReports = std::chrono::seconds(1);

					uint64_t hashedCode = pCallbackData->messageIdNumber;
					for (unsigned c=0; c<pCallbackData->cmdBufLabelCount; ++c)
						hashedCode = HashCombine((size_t)pCallbackData->pCmdBufLabels[c].pLabelName, hashedCode);
					for (unsigned c=0; c<pCallbackData->objectCount; ++c)
						hashedCode = HashCombine(pCallbackData->pObjects[c].objectHandle, hashedCode);

					auto i = LowerBound(handler->_suppressableMsgs, hashedCode);
					if (i == handler->_suppressableMsgs.end() || i->first != hashedCode) {
						i = handler->_suppressableMsgs.insert(i, {hashedCode, MsgReport{now, pCallbackData->messageIdNumber, 0}});
					} else {
						report = (now - i->second._lastReport) > timeBetweenDuplicateMsgs;
						if (report) {
							i->second._lastReport = now;
							i->second._suppressedSinceLastReport = 0;
						} else {
							++i->second._suppressedSinceLastReport;

							// Every now and again, report that there have been some suppressions
							if ((now - handler->_lastSuppressionReport) >= timeBetweenSuppressionReports) {
								handler->_lastSuppressionReport = now;

								Log(VulkanMsgTarget) << "Recent suppressed Vulkan messages: ";
								for (auto& h:handler->_suppressableMsgs) {
									if (h.second._suppressedSinceLastReport > 0) {
										Log(VulkanMsgTarget) << "[type: 0x" << std::hex << h.second._idMsgCode << std::dec << ", count: " << h.second._suppressedSinceLastReport << "]";
										h.second._suppressedSinceLastReport = 0;
									}
								}
								Log(VulkanMsgTarget) << std::endl;
							}
						}
					}
				}

				if (report)
					Log(VulkanMsgTarget) << (pCallbackData->pMessageIdName?pCallbackData->pMessageIdName:"<<>>") << ": " << pMsg << std::endl;

				return false;
			}

			DebugMessageHandler(VkInstance instance)
			: _instance(instance)
			{
				_suppressSpamRules = ~0u;

				VkDebugUtilsMessengerCreateInfoEXT callback1 = {
					VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,  // sType
					NULL,                                                     // pNext
					0,                                                        // flags
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |           // messageSeverity
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
					VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |             // messageType
					VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
					debug_callback,                                           // pfnUserCallback
					this                                                      // pUserData
				};
		
				auto proc = ((PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr( _instance, "vkCreateDebugUtilsMessengerEXT" ));
				if (!proc)
					Throw(std::runtime_error("Cannot find Vulkan debug layer function. Ensure that the SDK is fully installed, or disable the debug reporting feature"));
				proc( _instance, &callback1, Metal_Vulkan::g_allocationCallbacks, &_msgCallback );
			}

			~DebugMessageHandler()
			{
				((PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr( _instance, "vkDestroyDebugUtilsMessengerEXT" ))( _instance, _msgCallback, 0 );
			}

			DebugMessageHandler(DebugMessageHandler&&) = delete;
			DebugMessageHandler& operator=(DebugMessageHandler&&) = delete;
		private:
			VkInstance _instance;
			VkDebugUtilsMessengerEXT _msgCallback;

			unsigned _suppressSpamRules = 0;
			Threading::Mutex _suppressSpamLock;
			struct MsgReport { std::chrono::steady_clock::time_point _lastReport; int32_t _idMsgCode; unsigned _suppressedSinceLastReport; };
			std::vector<std::pair<uint64_t, MsgReport>> _suppressableMsgs;
			std::chrono::steady_clock::time_point _lastSuppressionReport;
		};

		OSServices::MessageTarget<> DebugMessageHandler::VulkanMsgTarget{"Vulkan"};
    #endif

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
					<< "). API Version: (0x" << std::hex << props.apiVersion 
					<< "). Driver version: (0x" << props.driverVersion << std::dec
					<< "). Type: (" << AsString(props.deviceType) << ")"
					<< std::endl;
				return SelectedPhysicalDevice { dev, qi };
			}
		}

		Throw(Exceptions::BasicLabel("There are physical Vulkan devices, but none of them support rendering. You must have an up-to-date Vulkan driver installed."));
	}

	static void LogInstanceLayers(std::ostream& str)
	{
		auto layers = EnumerateLayers();
		str << "[" << layers.size() << "] Vulkan instance layers" << std::endl;
		for (const auto& l:layers)
			str << "  " << l.layerName << std::hex << " (0x" << l.specVersion << ", 0x" << l.implementationVersion << ") " << std::dec << l.description << std::endl;
	}

	static void LogPhysicalDevices(std::ostream& str, VkInstance instance, VkSurfaceKHR surface)
	{
		auto devices = EnumeratePhysicalDevices(instance);
		if (devices.empty()) {
			str << "Could not find any Vulkan physical devices. You must have an up-to-date Vulkan driver installed." << std::endl;
			return;
		}

 		str << "[" << devices.size() << "] Vulkan physical devices" << std::endl;
		for (auto dev:devices) {
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(dev, &props);
			str << "  " << props.deviceName << " (" << AsString(props.deviceType) <<  ") ";
			str << "IDs: (0x" << std::hex << props.vendorID << "-0x" << props.deviceID;
			str << ") Version codes: (0x" << props.apiVersion << ", 0x" << props.driverVersion << ")" << std::dec << std::endl;
			// props.limits -> VkPhysicalDeviceLimits
			// props.sparseProperties -> VkPhysicalDeviceSparseProperties

			auto queueProps = EnumerateQueueFamilyProperties(dev);
			str << "  [" << queueProps.size() << "] queue families" << std::endl;
			for (unsigned qi=0; qi<unsigned(queueProps.size()); ++qi) {
				auto qprops = queueProps[qi];

				str << "    (";
				CommaSeparatedList list{str};
				if (qprops.queueFlags & VK_QUEUE_GRAPHICS_BIT) list << "Graphics";
				if (qprops.queueFlags & VK_QUEUE_COMPUTE_BIT) list << "Compute";
				if (qprops.queueFlags & VK_QUEUE_TRANSFER_BIT) list << "Transfer";
				if (qprops.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) list << "Sparse Binding";
				if (qprops.queueFlags & VK_QUEUE_PROTECTED_BIT) list << "Protected";
				str << "), queue count: " << qprops.queueCount;
				str << ", time stamp bits: " << qprops.timestampValidBits;
				str << ", min image gran: " << qprops.minImageTransferGranularity.width << "x" << qprops.minImageTransferGranularity.height << "x" << qprops.minImageTransferGranularity.depth;
				str << std::endl;

				if ((qprops.queueFlags & VK_QUEUE_GRAPHICS_BIT) && (surface != nullptr)) {
					VkBool32 supportsPresent = false;
					vkGetPhysicalDeviceSurfaceSupportKHR(
						dev, qi, surface, &supportsPresent);
					if (supportsPresent)
						str << "      Can present to output window" << std::endl;
				}
			}
		}
	}

	struct PhysicalDeviceExtensionQuery
	{
		IteratorRange<const VkExtensionProperties*> _extensions;

		PhysicalDeviceExtensionQuery(VkPhysicalDevice physDev)
		{
			unsigned extPropertyCount = dimof(_extensionsBuffer);
			auto res = vkEnumerateDeviceExtensionProperties(
				physDev, nullptr,
				&extPropertyCount, _extensionsBuffer);
			if (res == VK_INCOMPLETE) {
				res = vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extPropertyCount, nullptr);
				assert(res == VK_SUCCESS);
				_overflowExtensions.resize(extPropertyCount);
				res = vkEnumerateDeviceExtensionProperties(
					physDev, nullptr,
					&extPropertyCount, _overflowExtensions.data());
				assert(res == VK_SUCCESS);
				_extensions = MakeIteratorRange(_overflowExtensions);
			} else {
				_extensions = MakeIteratorRange(_extensionsBuffer, &_extensionsBuffer[extPropertyCount]);
			}
		}
	private:
		VkExtensionProperties _extensionsBuffer[256];
		std::vector<VkExtensionProperties> _overflowExtensions;
	};

	static void LogPhysicalDeviceExtensions(std::ostream& str, VkPhysicalDevice physDev)
	{
		PhysicalDeviceExtensionQuery ext{physDev};
		str << "[" << ext._extensions.size() << "] Vulkan physical device extensions" << std::endl;
		for (auto c:ext._extensions)
			str << "  " << c.extensionName << " (" << c.specVersion << ")" << std::endl;
	}
}}

#if VK_VERSION_1_1
	static std::ostream& operator<<(std::ostream& str, const VkDeviceGroupDeviceCreateInfo& features)
	{
		if (features.physicalDeviceCount)
			str << "In physical device group with " << features.physicalDeviceCount << " devices";
		else
			str << "not in physical device group";
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceMultiviewFeatures& features)
	{
		str << "Multiview features: ";
		CommaSeparatedList list{str};
		if (features.multiview) list << "core features";
		if (features.multiviewGeometryShader) list << "geometry shader";
		if (features.multiviewTessellationShader) list << "tessellation shader";
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceProtectedMemoryFeatures& features)
	{
		str << "Protected memory: " << (features.protectedMemory ? "enabled" : "disabled");
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceSamplerYcbcrConversionFeatures& features)
	{
		str << "YCbCr conversion: " << (features.samplerYcbcrConversion ? "enabled" : "disabled");
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceShaderDrawParametersFeatures& features)
	{
		str << "Shader draw parameters: " << (features.shaderDrawParameters ? "enabled" : "disabled");
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceVariablePointersFeatures& features)
	{
		str << "Shader variable pointers: ";
		CommaSeparatedList list{str};
		if (features.variablePointers) list << "basic";
		if (features.variablePointersStorageBuffer) list << "storage buffers";
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDevice16BitStorageFeatures& features)
	{
		str << "16 bit shader values: ";
		CommaSeparatedList list{str};
		if (features.storageBuffer16BitAccess) list << "Storage buffer";
		if (features.uniformAndStorageBuffer16BitAccess) list << "Uniform and storage buffer";
		if (features.storagePushConstant16) list << "Push constants";
		if (features.storageInputOutput16) list << "Input/output";
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceVulkan11Features& features)
	{
		std::vector<const char*> enabledFeatures;
		std::vector<const char*> disabledFeatures;
		enabledFeatures.reserve(16);
		disabledFeatures.reserve(16);

		// \s*VkBool32\s*(\w+)
		// (features.$1 ? &enabledFeatures : &disabledFeatures)->push_back("$1")
		(features.storageBuffer16BitAccess ? &enabledFeatures : &disabledFeatures)->push_back("storageBuffer16BitAccess");
		(features.uniformAndStorageBuffer16BitAccess ? &enabledFeatures : &disabledFeatures)->push_back("uniformAndStorageBuffer16BitAccess");
		(features.storagePushConstant16 ? &enabledFeatures : &disabledFeatures)->push_back("storagePushConstant16");
		(features.storageInputOutput16 ? &enabledFeatures : &disabledFeatures)->push_back("storageInputOutput16");
		(features.multiview ? &enabledFeatures : &disabledFeatures)->push_back("multiview");
		(features.multiviewGeometryShader ? &enabledFeatures : &disabledFeatures)->push_back("multiviewGeometryShader");
		(features.multiviewTessellationShader ? &enabledFeatures : &disabledFeatures)->push_back("multiviewTessellationShader");
		(features.variablePointersStorageBuffer ? &enabledFeatures : &disabledFeatures)->push_back("variablePointersStorageBuffer");
		(features.variablePointers ? &enabledFeatures : &disabledFeatures)->push_back("variablePointers");
		(features.protectedMemory ? &enabledFeatures : &disabledFeatures)->push_back("protectedMemory");
		(features.samplerYcbcrConversion ? &enabledFeatures : &disabledFeatures)->push_back("samplerYcbcrConversion");
		(features.shaderDrawParameters ? &enabledFeatures : &disabledFeatures)->push_back("shaderDrawParameters");

		str << "Enabled vk1.1 physical device features [";
		if (!enabledFeatures.empty()) {
			str << *enabledFeatures.begin();
			for (auto f=enabledFeatures.begin()+1; f!=enabledFeatures.end(); ++f)
				str << ", " << *f;
		}
		str << "]" << std::endl;
		str << "Disabled vk1.1 physical device features [";
		if (!disabledFeatures.empty()) {
			str << *disabledFeatures.begin();
			for (auto f=disabledFeatures.begin()+1; f!=disabledFeatures.end(); ++f)
				str << ", " << *f;
		}
		str << "]";

		return str;
	}
#endif

#if VK_VERSION_1_2
	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDevice8BitStorageFeatures& features)
	{
		str << "8 bit shader values: ";
		CommaSeparatedList list{str};
		if (features.storageBuffer8BitAccess) list << "Storage buffer";
		if (features.uniformAndStorageBuffer8BitAccess) list << "Uniform and storage buffer";
		if (features.storagePushConstant8) list << "Push constants";
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceBufferDeviceAddressFeatures& features)
	{
		// related to using vkGetBufferDeviceAddress to get addresses to buffer memory 
		str << "vkGetBufferDeviceAddress() features: ";
		CommaSeparatedList list{str};
		if (features.bufferDeviceAddress) list << "enabled";
		if (features.bufferDeviceAddressCaptureReplay) list << "capture replay";
		if (features.bufferDeviceAddressMultiDevice) list << "multi device";
		return str;
	}
	
	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceDescriptorIndexingFeatures& features)
	{
		// related to using vkGetBufferDeviceAddress to get addresses to buffer memory 
		str << "Dynamic shader indexing for arrays of: ";
		CommaSeparatedList list{str};
		if (features.shaderInputAttachmentArrayDynamicIndexing) list << "input attachments";
		if (features.shaderUniformTexelBufferArrayDynamicIndexing) list << "uniform texel buffers";
		if (features.shaderStorageTexelBufferArrayDynamicIndexing) list << "storage texel buffers";

		str << std::endl << "Non uniform shader indexing for arrays of: ";
		list = str;
		if (features.shaderUniformBufferArrayNonUniformIndexing) list << "uniform buffers";
		if (features.shaderSampledImageArrayNonUniformIndexing) list << "sampled images";
		if (features.shaderStorageBufferArrayNonUniformIndexing) list << "storage buffers";
		if (features.shaderStorageImageArrayNonUniformIndexing) list << "storage images";
		if (features.shaderInputAttachmentArrayNonUniformIndexing) list << "input attachments";
		if (features.shaderUniformTexelBufferArrayNonUniformIndexing) list << "uniform texel buffers";
		if (features.shaderStorageTexelBufferArrayNonUniformIndexing) list << "storage texel buffers";

		str << std::endl << "Update after bind for: ";
		list = str;
		if (features.descriptorBindingUniformBufferUpdateAfterBind) list << "update buffers";
		if (features.descriptorBindingSampledImageUpdateAfterBind) list << "sampled images";
		if (features.descriptorBindingStorageImageUpdateAfterBind) list << "storage images";
		if (features.descriptorBindingStorageBufferUpdateAfterBind) list << "storage buffers";
		if (features.descriptorBindingUniformTexelBufferUpdateAfterBind) list << "uniform texel buffers";
		if (features.descriptorBindingStorageTexelBufferUpdateAfterBind) list << "storage texel buffers";

		str << std::endl << "Additional features: ";
		list = str;
		if (features.descriptorBindingUpdateUnusedWhilePending) list << "update unused while pending";
		if (features.descriptorBindingPartiallyBound) list << "partially bound";
		if (features.descriptorBindingVariableDescriptorCount) list << "variable descriptor count";
		if (features.runtimeDescriptorArray) list << "runtime descriptor array";
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceHostQueryResetFeatures& features)
	{
		str << "Host query reset: " << (features.hostQueryReset ? "enabled" : "disabled");
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceImagelessFramebufferFeatures& features)
	{
		str << "Imageless frame buffer: " << (features.imagelessFramebuffer ? "enabled" : "disabled");
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceScalarBlockLayoutFeatures& features)
	{
		str << "Scalar block layout: " << (features.scalarBlockLayout ? "enabled" : "disabled");
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures& features)
	{
		str << "Separate depth stencil layouts: " << (features.separateDepthStencilLayouts ? "enabled" : "disabled");
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceShaderAtomicInt64Features& features)
	{
		str << "Shader atomic Int64 features: ";
		CommaSeparatedList list{str};
		if (features.shaderBufferInt64Atomics) list << "buffers";
		if (features.shaderSharedInt64Atomics) list << "shared memory";
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceShaderFloat16Int8Features& features)
	{
		str << "Shader additional value types: ";
		CommaSeparatedList list{str};
		if (features.shaderFloat16) list << "float16";
		if (features.shaderInt8) list << "int8";
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures& features)
	{
		str << "Shader subgroup extended types: " << (features.shaderSubgroupExtendedTypes ? "enabled" : "disabled");
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceTimelineSemaphoreFeatures& features)
	{
		str << "Semaphore type timeline: " << (features.timelineSemaphore ? "enabled" : "disabled");
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceUniformBufferStandardLayoutFeatures& features)
	{
		str << "Uniform buffer standard layout: " << (features.uniformBufferStandardLayout ? "enabled" : "disabled");
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceVulkanMemoryModelFeatures& features)
	{
		str << "Vulkan memory model: ";
		CommaSeparatedList list{str};
		if (features.vulkanMemoryModel) list << "enabled";
		if (features.vulkanMemoryModelDeviceScope) list << "device scope";
		if (features.vulkanMemoryModelAvailabilityVisibilityChains) list << "availability and visibility chains";
		return str;
	}

	static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceVulkan12Features& features)
	{
		std::vector<const char*> enabledFeatures;
		std::vector<const char*> disabledFeatures;
		enabledFeatures.reserve(56);
		disabledFeatures.reserve(56);

		(features.samplerMirrorClampToEdge ? &enabledFeatures : &disabledFeatures)->push_back("samplerMirrorClampToEdge");
		(features.drawIndirectCount ? &enabledFeatures : &disabledFeatures)->push_back("drawIndirectCount");
		(features.storageBuffer8BitAccess ? &enabledFeatures : &disabledFeatures)->push_back("storageBuffer8BitAccess");
		(features.uniformAndStorageBuffer8BitAccess ? &enabledFeatures : &disabledFeatures)->push_back("uniformAndStorageBuffer8BitAccess");
		(features.storagePushConstant8 ? &enabledFeatures : &disabledFeatures)->push_back("storagePushConstant8");
		(features.shaderBufferInt64Atomics ? &enabledFeatures : &disabledFeatures)->push_back("shaderBufferInt64Atomics");
		(features.shaderSharedInt64Atomics ? &enabledFeatures : &disabledFeatures)->push_back("shaderSharedInt64Atomics");
		(features.shaderFloat16 ? &enabledFeatures : &disabledFeatures)->push_back("shaderFloat16");
		(features.shaderInt8 ? &enabledFeatures : &disabledFeatures)->push_back("shaderInt8");
		(features.descriptorIndexing ? &enabledFeatures : &disabledFeatures)->push_back("descriptorIndexing");
		(features.shaderInputAttachmentArrayDynamicIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderInputAttachmentArrayDynamicIndexing");
		(features.shaderUniformTexelBufferArrayDynamicIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderUniformTexelBufferArrayDynamicIndexing");
		(features.shaderStorageTexelBufferArrayDynamicIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderStorageTexelBufferArrayDynamicIndexing");
		(features.shaderUniformBufferArrayNonUniformIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderUniformBufferArrayNonUniformIndexing");
		(features.shaderSampledImageArrayNonUniformIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderSampledImageArrayNonUniformIndexing");
		(features.shaderStorageBufferArrayNonUniformIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderStorageBufferArrayNonUniformIndexing");
		(features.shaderStorageImageArrayNonUniformIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderStorageImageArrayNonUniformIndexing");
		(features.shaderInputAttachmentArrayNonUniformIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderInputAttachmentArrayNonUniformIndexing");
		(features.shaderUniformTexelBufferArrayNonUniformIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderUniformTexelBufferArrayNonUniformIndexing");
		(features.shaderStorageTexelBufferArrayNonUniformIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderStorageTexelBufferArrayNonUniformIndexing");
		(features.descriptorBindingUniformBufferUpdateAfterBind ? &enabledFeatures : &disabledFeatures)->push_back("descriptorBindingUniformBufferUpdateAfterBind");
		(features.descriptorBindingSampledImageUpdateAfterBind ? &enabledFeatures : &disabledFeatures)->push_back("descriptorBindingSampledImageUpdateAfterBind");
		(features.descriptorBindingStorageImageUpdateAfterBind ? &enabledFeatures : &disabledFeatures)->push_back("descriptorBindingStorageImageUpdateAfterBind");
		(features.descriptorBindingStorageBufferUpdateAfterBind ? &enabledFeatures : &disabledFeatures)->push_back("descriptorBindingStorageBufferUpdateAfterBind");
		(features.descriptorBindingUniformTexelBufferUpdateAfterBind ? &enabledFeatures : &disabledFeatures)->push_back("descriptorBindingUniformTexelBufferUpdateAfterBind");
		(features.descriptorBindingStorageTexelBufferUpdateAfterBind ? &enabledFeatures : &disabledFeatures)->push_back("descriptorBindingStorageTexelBufferUpdateAfterBind");
		(features.descriptorBindingUpdateUnusedWhilePending ? &enabledFeatures : &disabledFeatures)->push_back("descriptorBindingUpdateUnusedWhilePending");
		(features.descriptorBindingPartiallyBound ? &enabledFeatures : &disabledFeatures)->push_back("descriptorBindingPartiallyBound");
		(features.descriptorBindingVariableDescriptorCount ? &enabledFeatures : &disabledFeatures)->push_back("descriptorBindingVariableDescriptorCount");
		(features.runtimeDescriptorArray ? &enabledFeatures : &disabledFeatures)->push_back("runtimeDescriptorArray");
		(features.samplerFilterMinmax ? &enabledFeatures : &disabledFeatures)->push_back("samplerFilterMinmax");
		(features.scalarBlockLayout ? &enabledFeatures : &disabledFeatures)->push_back("scalarBlockLayout");
		(features.imagelessFramebuffer ? &enabledFeatures : &disabledFeatures)->push_back("imagelessFramebuffer");
		(features.uniformBufferStandardLayout ? &enabledFeatures : &disabledFeatures)->push_back("uniformBufferStandardLayout");
		(features.shaderSubgroupExtendedTypes ? &enabledFeatures : &disabledFeatures)->push_back("shaderSubgroupExtendedTypes");
		(features.separateDepthStencilLayouts ? &enabledFeatures : &disabledFeatures)->push_back("separateDepthStencilLayouts");
		(features.hostQueryReset ? &enabledFeatures : &disabledFeatures)->push_back("hostQueryReset");
		(features.timelineSemaphore ? &enabledFeatures : &disabledFeatures)->push_back("timelineSemaphore");
		(features.bufferDeviceAddress ? &enabledFeatures : &disabledFeatures)->push_back("bufferDeviceAddress");
		(features.bufferDeviceAddressCaptureReplay ? &enabledFeatures : &disabledFeatures)->push_back("bufferDeviceAddressCaptureReplay");
		(features.bufferDeviceAddressMultiDevice ? &enabledFeatures : &disabledFeatures)->push_back("bufferDeviceAddressMultiDevice");
		(features.vulkanMemoryModel ? &enabledFeatures : &disabledFeatures)->push_back("vulkanMemoryModel");
		(features.vulkanMemoryModelDeviceScope ? &enabledFeatures : &disabledFeatures)->push_back("vulkanMemoryModelDeviceScope");
		(features.vulkanMemoryModelAvailabilityVisibilityChains ? &enabledFeatures : &disabledFeatures)->push_back("vulkanMemoryModelAvailabilityVisibilityChains");
		(features.shaderOutputViewportIndex ? &enabledFeatures : &disabledFeatures)->push_back("shaderOutputViewportIndex");
		(features.shaderOutputLayer ? &enabledFeatures : &disabledFeatures)->push_back("shaderOutputLayer");
		(features.subgroupBroadcastDynamicId ? &enabledFeatures : &disabledFeatures)->push_back("subgroupBroadcastDynamicId");

		str << "Enabled vk1.2 physical device features [";
		if (!enabledFeatures.empty()) {
			str << *enabledFeatures.begin();
			for (auto f=enabledFeatures.begin()+1; f!=enabledFeatures.end(); ++f)
				str << ", " << *f;
		}
		str << "]" << std::endl;
		str << "Disabled vk1.2 physical device features [";
		if (!disabledFeatures.empty()) {
			str << *disabledFeatures.begin();
			for (auto f=disabledFeatures.begin()+1; f!=disabledFeatures.end(); ++f)
				str << ", " << *f;
		}
		str << "]";

		return str;
	}
#endif

static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceLimits& limits)
{
	str << "Max image dimension -- 1D: " << limits.maxImageDimension1D << " 2D: " << limits.maxImageDimension2D << " 3D: " << limits.maxImageDimension3D << " Cube: " << limits.maxImageDimensionCube << std::endl;
	str << "Max image array layers: " << limits.maxImageArrayLayers << std::endl;

	str << "Max texel buffer elements: " << limits.maxTexelBufferElements << std::endl;
	str << "Max buffer range -- uniform: " << limits.maxUniformBufferRange << " storage: " << limits.maxStorageBufferRange << std::endl;
	str << "Max push constants size: " << limits.maxPushConstantsSize << std::endl;
	str << "Max memory allocation count: " << limits.maxMemoryAllocationCount << std::endl;
	str << "Max sampler allocation count: " << limits.maxSamplerAllocationCount << std::endl;
	str << "Buffer image granularity: " << limits.bufferImageGranularity << std::endl;

	str << "Sparse address space size: " << limits.sparseAddressSpaceSize << std::endl;

	str << "Max bound descriptor sets: " << limits.maxBoundDescriptorSets << std::endl;
	str << "Max per stage descriptors -- samplers: " << limits.maxPerStageDescriptorSamplers << ", uniform buffers: " << limits.maxPerStageDescriptorUniformBuffers << ", storage buffers: " << limits.maxPerStageDescriptorStorageBuffers
		<< ", sampled images: " << limits.maxPerStageDescriptorSampledImages << ", storage images: " << limits.maxPerStageDescriptorStorageImages << ", input attachments: " << limits.maxPerStageDescriptorInputAttachments << ", resources: " << limits.maxPerStageResources << std::endl;

	str << "Max descriptors -- samplers: " << limits.maxDescriptorSetSamplers << ", uniform buffers: " << limits.maxDescriptorSetUniformBuffers << ", uniform buffers dynamic: " << limits.maxDescriptorSetUniformBuffersDynamic
		<< ", storage buffers: " << limits.maxDescriptorSetStorageBuffers << ", storage buffers dynamic: " << limits.maxDescriptorSetStorageBuffersDynamic << ", sampled images: "  << limits.maxDescriptorSetSampledImages
		<< ", storage images: " << limits.maxDescriptorSetStorageImages << ", input attachments: " << limits.maxDescriptorSetInputAttachments << std::endl;

	str << "Max input -- attributes: " << limits.maxVertexInputAttributes << ", bindings: " << limits.maxVertexInputBindings << std::endl;
	str << "Max input attribute offset: " << limits.maxVertexInputAttributeOffset << std::endl;
	str << "Max input binding stride: " << limits.maxVertexInputBindingStride << std::endl;
	str << "Max vertex output components: " << limits.maxVertexOutputComponents << std::endl;

	str << "Max tesselation -- generation level: " << limits.maxTessellationGenerationLevel << ", patch size: " << limits.maxTessellationPatchSize << ", control per vertex input components: " << limits.maxTessellationControlPerVertexInputComponents
		<< ", control per vertex output components: " << limits.maxTessellationControlPerVertexOutputComponents << ", control per patch output components: " << limits.maxTessellationControlPerPatchOutputComponents
		<< ", control total output components: " << limits.maxTessellationControlTotalOutputComponents
		<< ", evaluation input components " << limits.maxTessellationEvaluationInputComponents << ", evaluation output components " << limits.maxTessellationEvaluationOutputComponents << std::endl;

	str << "Max geometry -- shader invocations: " << limits.maxGeometryShaderInvocations << ", input components: " << limits.maxGeometryInputComponents << ", output components " << limits.maxGeometryOutputComponents
		<< ", output vertices " << limits.maxGeometryOutputVertices << ", total output components " << limits.maxGeometryTotalOutputComponents << std::endl;

	str << "Max fragment -- input components: " << limits.maxFragmentInputComponents << ", output components: " << limits.maxFragmentOutputAttachments << ", dual src attachments: " << limits.maxFragmentDualSrcAttachments
		<< ", combined output resources: " << limits.maxFragmentCombinedOutputResources << std::endl;

	str << "Max Compute -- shared memory size: " << limits.maxComputeSharedMemorySize << ", workgroup count: " << limits.maxComputeWorkGroupCount[0] << "x" << limits.maxComputeWorkGroupCount[1] << "x" << limits.maxComputeWorkGroupCount[2]
		<< ", workgroup invocations: " << limits.maxComputeWorkGroupInvocations << ", workgroup size: " << limits.maxComputeWorkGroupSize[0] << "x" << limits.maxComputeWorkGroupSize[1] << "x" << limits.maxComputeWorkGroupSize[2] << std::endl;

	str << "Sub pixel precision bits: " << limits.subPixelPrecisionBits << ", sub texel precision bits: " << limits.subTexelPrecisionBits << ", mipmap precision bits: " << limits.mipmapPrecisionBits << std::endl;

	str << "Max DrawIndexed index value: " << limits.maxDrawIndexedIndexValue << ", max DrawIndirect count: " << limits.maxDrawIndirectCount << std::endl;

	str << "Max Sampler -- lod bias: " << limits.maxSamplerLodBias << ", anisotrophy: " << limits.maxSamplerAnisotropy << std::endl;

	str << "Max viewports: " << limits.maxViewports << ", max viewport dimensions: " << limits.maxViewportDimensions[0] << "x" << limits.maxViewportDimensions[1]
		<< ", viewport bounds range: " << limits.viewportBoundsRange[0] << " to " << limits.viewportBoundsRange[1] << ", viewport sub pixel bits: " << limits.viewportSubPixelBits << std::endl;

	str << "Min offset alignment -- map: " << limits.minMemoryMapAlignment << ", texel buffers: " << limits.minTexelBufferOffsetAlignment << ", uniform buffers: " << limits.minUniformBufferOffsetAlignment << ", storage buffers: " << limits.minStorageBufferOffsetAlignment << std::endl;

	str << "Texel offsets: " << limits.minTexelOffset << " to " << limits.maxTexelOffset << ", texel gather offsets: " << limits.minTexelGatherOffset << " to " << limits.maxTexelGatherOffset 
		<< ", interpolation offsets: " << limits.minInterpolationOffset << " to " << limits.maxInterpolationOffset << ", sub pixel interpolation offset bits: " << limits.subPixelInterpolationOffsetBits << std::endl;

	str << "Max framebuffer: " << limits.maxFramebufferWidth << "x" << limits.maxFramebufferHeight << "x" << limits.maxFramebufferLayers 
		<< ", color samples: " << limits.framebufferColorSampleCounts << ", depth samples: " << limits.framebufferDepthSampleCounts << ", stencil samples: " << limits.framebufferStencilSampleCounts << ", no attachment samples: " << limits.framebufferNoAttachmentsSampleCounts << std::endl;
	str << "Max color attachments: " << limits.maxColorAttachments << std::endl;

	str << "Max sample counts -- sampled image color: " << limits.sampledImageColorSampleCounts << ", sampled image integer: " << limits.sampledImageIntegerSampleCounts << ", sampled image depth: " << limits.sampledImageDepthSampleCounts << ", sampled image stencil: " 
		<< limits.sampledImageStencilSampleCounts << ", storage image: " << limits.storageImageSampleCounts << std::endl;
	str << "Max sample mask words: " << limits.maxSampleMaskWords << std::endl;

	str << "Timestamp -- compute and graphics: " << (limits.timestampComputeAndGraphics ? "supported": "unsupported") << ", period: " << limits.timestampPeriod << std::endl;

	str << "Max clip distances: " << limits.maxClipDistances << ", max cull distances: " << limits.maxCullDistances << ", max combined: " << limits.maxCombinedClipAndCullDistances << std::endl;

	str << "Discrete queue priorities: " << limits.discreteQueuePriorities << std::endl;

	str << "Point size: " << limits.pointSizeRange[0] << " to " << limits.pointSizeRange[1] << ", point granularity: " << limits.pointSizeGranularity 
		<< ", line width: " << limits.lineWidthRange[0] << " to " << limits.lineWidthRange[1] << ", line granularity: " << limits.lineWidthGranularity << ", strict lines: " << limits.strictLines << std::endl;

	str << "Standard sampled locations: " << (limits.standardSampleLocations ? "true" : "false") << std::endl;

	str << "Optimal buffer copy offset alignment: " << limits.optimalBufferCopyOffsetAlignment << ", optional buffer copy row pitch alignment: " << limits.optimalBufferCopyRowPitchAlignment << std::endl;
	str << "Non coherent atom size: " << limits.nonCoherentAtomSize;
	return str;
}

static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceSparseProperties& props)
{
	str << "Sparse residency standard -- 2d block shape: " << props.residencyStandard2DBlockShape << ", multisample block shape: " << props.residencyStandard2DMultisampleBlockShape << ", 3d block shape: " << props.residencyStandard3DBlockShape << std::endl;
	str << "Sparse residency aligned mip size: " << props.residencyAlignedMipSize << ", non resident strict: " << props.residencyNonResidentStrict;
	return str;
}

struct StreamShaderStageFlags
{
	VkShaderStageFlags _flags = 0; 
	friend std::ostream& operator<<(std::ostream& str, StreamShaderStageFlags input)
	{
		std::pair<unsigned, const char*> flags[]
		{
		    {VK_SHADER_STAGE_VERTEX_BIT, "Vertex"},
			{VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, "TesselationControl"},
			{VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, "TesselationEval"},
			{VK_SHADER_STAGE_GEOMETRY_BIT, "Geometry"},
			{VK_SHADER_STAGE_FRAGMENT_BIT, "Fragment"},
			{VK_SHADER_STAGE_COMPUTE_BIT, "Compute"},
			{VK_SHADER_STAGE_RAYGEN_BIT_KHR, "Raygen"},
			{VK_SHADER_STAGE_ANY_HIT_BIT_KHR, "Anyhit"},
			{VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, "ClosestHit"},
			{VK_SHADER_STAGE_MISS_BIT_KHR, "Miss"},
			{VK_SHADER_STAGE_INTERSECTION_BIT_KHR, "Intersection"},
			{VK_SHADER_STAGE_CALLABLE_BIT_KHR, "Callable"},
			{VK_SHADER_STAGE_TASK_BIT_NV, "Task"},
			{VK_SHADER_STAGE_MESH_BIT_NV, "Mesh"}
		};

		bool pendingSeparator = false;
		for (auto f:flags)
			if (input._flags & f.first) {
				if (pendingSeparator) str << " | ";
				pendingSeparator = true;
				str << f.second;
			}
		return str;
	};
};

struct StreamSubgroupFeatureFlags
{
	VkSubgroupFeatureFlags _flags = 0; 
	friend std::ostream& operator<<(std::ostream& str, StreamSubgroupFeatureFlags input)
	{
		std::pair<unsigned, const char*> flags[]
		{
		    {VK_SUBGROUP_FEATURE_BASIC_BIT, "Basic"},
			{VK_SUBGROUP_FEATURE_VOTE_BIT, "Vote"},
			{VK_SUBGROUP_FEATURE_ARITHMETIC_BIT, "Arithmetic"},
			{VK_SUBGROUP_FEATURE_BALLOT_BIT, "Ballot"},
			{VK_SUBGROUP_FEATURE_SHUFFLE_BIT, "Shuffle"},
			{VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT, "ShuffleRelative"},
			{VK_SUBGROUP_FEATURE_CLUSTERED_BIT, "Clustered"},
			{VK_SUBGROUP_FEATURE_QUAD_BIT, "Quad"},
			{VK_SUBGROUP_FEATURE_PARTITIONED_BIT_NV, "Partitioned"}
		};

		bool pendingSeparator = false;
		for (auto f:flags)
			if (input._flags & f.first) {
				if (pendingSeparator) str << " | ";
				pendingSeparator = true;
				str << f.second;
			}
		return str;
	};
};

static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceVulkan11Properties& props)
{
	str << "Device UUID: 0x" << std::hex << std::setfill('0');
	for (auto i:props.deviceUUID) str << std::setw(2) << (unsigned)i;
	str << ", driver UUID: 0x";
	for (auto i:props.driverUUID) str << std::setw(2) << (unsigned)i;
	if (props.deviceLUIDValid) {
		str << ", device LUID: 0x";
		for (auto i:props.deviceLUID) str << std::setw(2) << (unsigned)i;
	} else {
		str << ", no device LUID";
	}
	str << std::setw(0);
	str << std::endl;
	str << "Device node mask: 0x" << props.deviceNodeMask << std::dec << std::endl;
	str << "Subgroup -- size: " << props.subgroupSize << ", supported stages: (" << StreamShaderStageFlags{props.subgroupSupportedStages} 
		<< "), supported ops: (" << StreamSubgroupFeatureFlags{props.subgroupSupportedOperations} << "), quad ops in all stages: " << (props.subgroupQuadOperationsInAllStages ? "supported" : "unsupported") << std::endl;

	str << "Point clipping behaviour: ";
	switch (props.pointClippingBehavior) {
	case VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES: str << "all clip planes" << std::endl; break;
	case VK_POINT_CLIPPING_BEHAVIOR_USER_CLIP_PLANES_ONLY: str << "user clip planes only" << std::endl; break;
	default: str << "unknown" << std::endl; break;
	}

	str << "Max multiview -- view count: " << props.maxMultiviewViewCount << ", instance index: " << props.maxMultiviewInstanceIndex << std::endl;
	str << "Fault on protected memory rule break: " << (props.protectedNoFault ? "no" : "yes") << std::endl;
	str << "Max per set descriptors: " << props.maxPerSetDescriptors << ", max memory allocation size: " << props.maxMemoryAllocationSize;
	return str;
}

struct StreamShaderFloatControlsIndependence
{
	VkShaderFloatControlsIndependence v;
	friend std::ostream& operator<<(std::ostream& str, StreamShaderFloatControlsIndependence q)
	{
		switch (q.v) {
		case VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY: str << "32 bit only"; break;
		case VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL: str << "all"; break;
		case VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE: str << "none"; break;
		default: str << "unknown"; break;
		}
		return str;
	}
};

static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceVulkan12Properties& props)
{
	str << "DriverID: ";
	switch (props.driverID) {
	case VK_DRIVER_ID_AMD_PROPRIETARY: str << "AMD proprietary"; break;
	case VK_DRIVER_ID_AMD_OPEN_SOURCE: str << "AMD open source"; break;
	case VK_DRIVER_ID_MESA_RADV: str << "Mesa"; break;
	case VK_DRIVER_ID_NVIDIA_PROPRIETARY: str << "Nvidia proprietary"; break;
	case VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS: str << "Intel proprietary"; break;
	case VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA: str << "Intel open source Mesa"; break;
	case VK_DRIVER_ID_IMAGINATION_PROPRIETARY: str << "Imagination proprietary"; break;
	case VK_DRIVER_ID_QUALCOMM_PROPRIETARY: str << "Qualcomm proprietary"; break;
	case VK_DRIVER_ID_ARM_PROPRIETARY: str << "Arm proprietary"; break;
	case VK_DRIVER_ID_GOOGLE_SWIFTSHADER: str << "Google Swiftshader"; break;
	case VK_DRIVER_ID_GGP_PROPRIETARY: str << "GGP proprietary"; break;
	case VK_DRIVER_ID_BROADCOM_PROPRIETARY: str << "Broadcom proprietary"; break;
	case VK_DRIVER_ID_MESA_LLVMPIPE: str << "Mesa LLVMpipe"; break;
	case VK_DRIVER_ID_MOLTENVK: str << "MoltenVK"; break;
	default: str << "Unknown"; break;
	}

	str << ", name: " << props.driverName << ", info: " << props.driverInfo << std::endl;
	str << "VK conformance version: " << (unsigned)props.conformanceVersion.major << "." << (unsigned)props.conformanceVersion.minor << "." << (unsigned)props.conformanceVersion.subminor << "." << (unsigned)props.conformanceVersion.patch << std::endl;
	str << "Denorm behaviour independence: " << StreamShaderFloatControlsIndependence{props.denormBehaviorIndependence} << ", rounding mode independence: " << StreamShaderFloatControlsIndependence{props.roundingModeIndependence} << std::endl;

	auto floatTypesHelper = [](std::ostream& str, bool float16, bool float32, bool float64) {
		CommaSeparatedList list{str};
		if (float16) list << "float16"; if (float32) list << "float32"; if (float64) list << "float64";
	};
	str << "Shader signed-zero-inf-nan preserve: ";
	floatTypesHelper(str, props.shaderSignedZeroInfNanPreserveFloat16, props.shaderSignedZeroInfNanPreserveFloat32, props.shaderSignedZeroInfNanPreserveFloat64);
	str << std::endl << "Shader denorm preserve: ";
	floatTypesHelper(str, props.shaderDenormPreserveFloat16, props.shaderDenormPreserveFloat32, props.shaderDenormPreserveFloat64);
	str << std::endl << "Shader denorm flush to zero: ";
	floatTypesHelper(str, props.shaderDenormFlushToZeroFloat16, props.shaderDenormFlushToZeroFloat32, props.shaderDenormFlushToZeroFloat64);
	str << std::endl << "Shader rounding mode RTE: ";
	floatTypesHelper(str, props.shaderRoundingModeRTEFloat16, props.shaderRoundingModeRTEFloat32, props.shaderRoundingModeRTEFloat64);
	str << std::endl << "Shader rounding mode RTZ: ";
	floatTypesHelper(str, props.shaderRoundingModeRTZFloat16, props.shaderRoundingModeRTZFloat32, props.shaderRoundingModeRTZFloat64);
	str << std::endl;

	str << "Max update after bind descriptors: " << props.maxUpdateAfterBindDescriptorsInAllPools << std::endl;

	str << "Shader native non uniform indexing: ";
	CommaSeparatedList list{str};
	if (props.shaderUniformBufferArrayNonUniformIndexingNative) list << "uniform buffers";
	if (props.shaderSampledImageArrayNonUniformIndexingNative) list << "sampled images";
	if (props.shaderStorageBufferArrayNonUniformIndexingNative) list << "storage buffers";
	if (props.shaderStorageImageArrayNonUniformIndexingNative) list << "storage images";
	if (props.shaderInputAttachmentArrayNonUniformIndexingNative) list << "input attachments";
	str << std::endl;

	str << "Robust buffer access update after bind: " << (props.robustBufferAccessUpdateAfterBind ? "supported" : "unsupported") << std::endl;
	str << "Quad divergent implicit lod: " << (props.quadDivergentImplicitLod ? "supported" : "unsupported") << std::endl;

	str << "Max per stage descriptor update after bind -- samplers: " << props.maxPerStageDescriptorUpdateAfterBindSamplers << ", uniform buffers: " << props.maxPerStageDescriptorUpdateAfterBindUniformBuffers
		 << ", storage buffers: " << props.maxPerStageDescriptorUpdateAfterBindStorageBuffers << ", sampled images: " << props.maxPerStageDescriptorUpdateAfterBindSampledImages
		 << ", storage images: " << props.maxPerStageDescriptorUpdateAfterBindStorageImages << ", input attachments: " << props.maxPerStageDescriptorUpdateAfterBindInputAttachments << std::endl;
	str << "Max per stage update after bind resources: " << props.maxPerStageUpdateAfterBindResources << std::endl;

	str << "Max descriptor set update after bind -- samplers: " << props.maxDescriptorSetUpdateAfterBindSamplers << ", uniform buffers: " << props.maxDescriptorSetUpdateAfterBindUniformBuffers 
		<< ", uniform buffers dynamic: " << props.maxDescriptorSetUpdateAfterBindUniformBuffersDynamic << ", storage buffers: " << props.maxDescriptorSetUpdateAfterBindStorageBuffers
		<< ", storage buffers dynamic: " << props.maxDescriptorSetUpdateAfterBindStorageBuffersDynamic << ", sampled images: " << props.maxDescriptorSetUpdateAfterBindSampledImages
		<< ", storage images: " << props.maxDescriptorSetUpdateAfterBindStorageImages << ", input attachments: " << props.maxDescriptorSetUpdateAfterBindInputAttachments << std::endl;

	str << "Supported resolve modes -- depth: (";
	list = CommaSeparatedList{str};
	if (props.supportedDepthResolveModes & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) list << "sample zero";
	if (props.supportedDepthResolveModes & VK_RESOLVE_MODE_AVERAGE_BIT) list << "average";
	if (props.supportedDepthResolveModes & VK_RESOLVE_MODE_MIN_BIT) list << "min";
	if (props.supportedDepthResolveModes & VK_RESOLVE_MODE_MAX_BIT) list << "max";
	str << "), stencil: (";
	list = CommaSeparatedList{str};
	if (props.supportedStencilResolveModes & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) list << "sample zero";
	if (props.supportedStencilResolveModes & VK_RESOLVE_MODE_AVERAGE_BIT) list << "average";
	if (props.supportedStencilResolveModes & VK_RESOLVE_MODE_MIN_BIT) list << "min";
	if (props.supportedStencilResolveModes & VK_RESOLVE_MODE_MAX_BIT) list << "max";
	str << "), independent depth/stencil resolve modes: ";
	if (props.independentResolve) str << "supported";
	else if (props.independentResolveNone) str << "only with \"none\"";
	else str << "unsupported";

	str << std::endl << "Filter min/max filtering: " << (props.filterMinmaxSingleComponentFormats ? "single component formats" : "not guaranteed") << ", image component mapping: " << (props.filterMinmaxImageComponentMapping ? "supported" : "unsupported") << std::endl;

	str << "Max timeline semaphore value difference: " << props.maxTimelineSemaphoreValueDifference << std::endl;

	str << "Integer framebuffer sample counts: ";
	list = CommaSeparatedList{str};
	if (props.framebufferIntegerColorSampleCounts & VK_SAMPLE_COUNT_1_BIT) list << "1";
	if (props.framebufferIntegerColorSampleCounts & VK_SAMPLE_COUNT_2_BIT) list << "2";
	if (props.framebufferIntegerColorSampleCounts & VK_SAMPLE_COUNT_4_BIT) list << "4";
	if (props.framebufferIntegerColorSampleCounts & VK_SAMPLE_COUNT_8_BIT) list << "8";
	if (props.framebufferIntegerColorSampleCounts & VK_SAMPLE_COUNT_16_BIT) list << "16";
	if (props.framebufferIntegerColorSampleCounts & VK_SAMPLE_COUNT_32_BIT) list << "32";
	if (props.framebufferIntegerColorSampleCounts & VK_SAMPLE_COUNT_64_BIT) list << "64";

	return str;
}

static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceConservativeRasterizationPropertiesEXT& props)
{
	str << "Conservative rasterization" << std::endl;
	str << "  Primitive overestimation size: " << props.primitiveOverestimationSize << ", max extra overestimation size: " << props.maxExtraPrimitiveOverestimationSize << ", extra overestimation granularity: " << props.extraPrimitiveOverestimationSizeGranularity << std::endl;
	str << "  Primitive underestimation: " << (props.primitiveUnderestimation ? "supported" : "unsupported") << std::endl;
	str << "  Conservative point and line rasterization: " << (props.conservativePointAndLineRasterization ? "supported" : "unsupported") << std::endl;
	str << "  Degenerate triangles rasterized: " << (props.degenerateTrianglesRasterized ? "yes" : "no") << ", degenerate lines rasterized: " << (props.degenerateLinesRasterized ? "yes" : "no") << std::endl;
	str << "  Fully covered fragment shader input variable: " << (props.fullyCoveredFragmentShaderInputVariable ? "supported" : "unsupported") << ", simultaneous post depth converage: " << (props.conservativeRasterizationPostDepthCoverage ? "supported" : "unsupported");
	return str;
}

static std::ostream& operator<<(std::ostream& str, const VkPhysicalDeviceTransformFeedbackPropertiesEXT& props)
{
	str << "Transform feedback" << std::endl;
	str << "  Max -- streams: " << props.maxTransformFeedbackStreams << ", buffers: " << props.maxTransformFeedbackBuffers << ", buffer size: " << props.maxTransformFeedbackBufferSize
		<< ", stream data size: " << props.maxTransformFeedbackStreamDataSize << ", buffer data size: " << props.maxTransformFeedbackBufferDataSize << ", buffer data stride: " << props.maxTransformFeedbackBufferDataStride << std::endl;
	str << "  Queries: " << (props.transformFeedbackQueries ? "supported" : "unsupported") << ", multi stream lines/triangles: " << (props.transformFeedbackQueries ? "supported" : "unsupported") 
		<< ", shader stream select: " << (props.transformFeedbackRasterizationStreamSelect ? "supported" : "unsupported") << ", draw indirect: " << (props.transformFeedbackRasterizationStreamSelect ? "supported" : "unsupported");
	return str;
}

namespace RenderCore { namespace ImplVulkan
{
	static void LogPhysicalDeviceFeatures(std::ostream& str, const VkPhysicalDeviceFeatures2& features2)
	{
		assert(features2.sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);

		std::vector<const char*> enabledFeatures;
		std::vector<const char*> disabledFeatures;
		enabledFeatures.reserve(64);		// 55 max in Vulkan 1.0
		disabledFeatures.reserve(64);
		(features2.features.robustBufferAccess ? &enabledFeatures : &disabledFeatures)->push_back("robustBufferAccess");
		(features2.features.fullDrawIndexUint32 ? &enabledFeatures : &disabledFeatures)->push_back("fullDrawIndexUint32");
		(features2.features.imageCubeArray ? &enabledFeatures : &disabledFeatures)->push_back("imageCubeArray");
		(features2.features.independentBlend ? &enabledFeatures : &disabledFeatures)->push_back("independentBlend");
		(features2.features.geometryShader ? &enabledFeatures : &disabledFeatures)->push_back("geometryShader");
		(features2.features.tessellationShader ? &enabledFeatures : &disabledFeatures)->push_back("tessellationShader");
		(features2.features.sampleRateShading ? &enabledFeatures : &disabledFeatures)->push_back("sampleRateShading");
		(features2.features.dualSrcBlend ? &enabledFeatures : &disabledFeatures)->push_back("dualSrcBlend");
		(features2.features.logicOp ? &enabledFeatures : &disabledFeatures)->push_back("logicOp");
		(features2.features.multiDrawIndirect ? &enabledFeatures : &disabledFeatures)->push_back("multiDrawIndirect");
		(features2.features.drawIndirectFirstInstance ? &enabledFeatures : &disabledFeatures)->push_back("drawIndirectFirstInstance");
		(features2.features.depthClamp ? &enabledFeatures : &disabledFeatures)->push_back("depthClamp");
		(features2.features.depthBiasClamp ? &enabledFeatures : &disabledFeatures)->push_back("depthBiasClamp");
		(features2.features.fillModeNonSolid ? &enabledFeatures : &disabledFeatures)->push_back("fillModeNonSolid");
		(features2.features.depthBounds ? &enabledFeatures : &disabledFeatures)->push_back("depthBounds");
		(features2.features.wideLines ? &enabledFeatures : &disabledFeatures)->push_back("wideLines");
		(features2.features.largePoints ? &enabledFeatures : &disabledFeatures)->push_back("largePoints");
		(features2.features.alphaToOne ? &enabledFeatures : &disabledFeatures)->push_back("alphaToOne");
		(features2.features.multiViewport ? &enabledFeatures : &disabledFeatures)->push_back("multiViewport");
		(features2.features.samplerAnisotropy ? &enabledFeatures : &disabledFeatures)->push_back("samplerAnisotropy");
		(features2.features.textureCompressionETC2 ? &enabledFeatures : &disabledFeatures)->push_back("textureCompressionETC2");
		(features2.features.textureCompressionASTC_LDR ? &enabledFeatures : &disabledFeatures)->push_back("textureCompressionASTC_LDR");
		(features2.features.textureCompressionBC ? &enabledFeatures : &disabledFeatures)->push_back("textureCompressionBC");
		(features2.features.occlusionQueryPrecise ? &enabledFeatures : &disabledFeatures)->push_back("occlusionQueryPrecise");
		(features2.features.pipelineStatisticsQuery ? &enabledFeatures : &disabledFeatures)->push_back("pipelineStatisticsQuery");
		(features2.features.vertexPipelineStoresAndAtomics ? &enabledFeatures : &disabledFeatures)->push_back("vertexPipelineStoresAndAtomics");
		(features2.features.fragmentStoresAndAtomics ? &enabledFeatures : &disabledFeatures)->push_back("fragmentStoresAndAtomics");
		(features2.features.shaderTessellationAndGeometryPointSize ? &enabledFeatures : &disabledFeatures)->push_back("shaderTessellationAndGeometryPointSize");
		(features2.features.shaderImageGatherExtended ? &enabledFeatures : &disabledFeatures)->push_back("shaderImageGatherExtended");
		(features2.features.shaderStorageImageExtendedFormats ? &enabledFeatures : &disabledFeatures)->push_back("shaderStorageImageExtendedFormats");
		(features2.features.shaderStorageImageMultisample ? &enabledFeatures : &disabledFeatures)->push_back("shaderStorageImageMultisample");
		(features2.features.shaderStorageImageReadWithoutFormat ? &enabledFeatures : &disabledFeatures)->push_back("shaderStorageImageReadWithoutFormat");
		(features2.features.shaderStorageImageWriteWithoutFormat ? &enabledFeatures : &disabledFeatures)->push_back("shaderStorageImageWriteWithoutFormat");
		(features2.features.shaderUniformBufferArrayDynamicIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderUniformBufferArrayDynamicIndexing");
		(features2.features.shaderSampledImageArrayDynamicIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderSampledImageArrayDynamicIndexing");
		(features2.features.shaderStorageBufferArrayDynamicIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderStorageBufferArrayDynamicIndexing");
		(features2.features.shaderStorageImageArrayDynamicIndexing ? &enabledFeatures : &disabledFeatures)->push_back("shaderStorageImageArrayDynamicIndexing");
		(features2.features.shaderClipDistance ? &enabledFeatures : &disabledFeatures)->push_back("shaderClipDistance");
		(features2.features.shaderCullDistance ? &enabledFeatures : &disabledFeatures)->push_back("shaderCullDistance");
		(features2.features.shaderFloat64 ? &enabledFeatures : &disabledFeatures)->push_back("shaderFloat64");
		(features2.features.shaderInt64 ? &enabledFeatures : &disabledFeatures)->push_back("shaderInt64");
		(features2.features.shaderInt16 ? &enabledFeatures : &disabledFeatures)->push_back("shaderInt16");
		(features2.features.shaderResourceResidency ? &enabledFeatures : &disabledFeatures)->push_back("shaderResourceResidency");
		(features2.features.shaderResourceMinLod ? &enabledFeatures : &disabledFeatures)->push_back("shaderResourceMinLod");
		(features2.features.sparseBinding ? &enabledFeatures : &disabledFeatures)->push_back("sparseBinding");
		(features2.features.sparseResidencyBuffer ? &enabledFeatures : &disabledFeatures)->push_back("sparseResidencyBuffer");
		(features2.features.sparseResidencyImage2D ? &enabledFeatures : &disabledFeatures)->push_back("sparseResidencyImage2D");
		(features2.features.sparseResidencyImage3D ? &enabledFeatures : &disabledFeatures)->push_back("sparseResidencyImage3D");
		(features2.features.sparseResidency2Samples ? &enabledFeatures : &disabledFeatures)->push_back("sparseResidency2Samples");
		(features2.features.sparseResidency4Samples ? &enabledFeatures : &disabledFeatures)->push_back("sparseResidency4Samples");
		(features2.features.sparseResidency8Samples ? &enabledFeatures : &disabledFeatures)->push_back("sparseResidency8Samples");
		(features2.features.sparseResidency16Samples ? &enabledFeatures : &disabledFeatures)->push_back("sparseResidency16Samples");
		(features2.features.sparseResidencyAliased ? &enabledFeatures : &disabledFeatures)->push_back("sparseResidencyAliased");
		(features2.features.variableMultisampleRate ? &enabledFeatures : &disabledFeatures)->push_back("variableMultisampleRate");
		(features2.features.inheritedQueries ? &enabledFeatures : &disabledFeatures)->push_back("inheritedQueries");

		str << "VK1.0" << std::endl;
		str << "Enabled vk1.0 physical device features [";
		if (!enabledFeatures.empty()) {
			str << *enabledFeatures.begin();
			for (auto f=enabledFeatures.begin()+1; f!=enabledFeatures.end(); ++f)
				str << ", " << *f;
		}
		str << "]" << std::endl;
		str << "Disabled vk1.0 physical device features [";
		if (!disabledFeatures.empty()) {
			str << *disabledFeatures.begin();
			for (auto f=disabledFeatures.begin()+1; f!=disabledFeatures.end(); ++f)
				str << ", " << *f;
		}
		str << "]" << std::endl;

		std::pair<unsigned, const char*> versions [] { 
			{11, "VK1.1"},
			{12, "VK1.2"}
		};

		// walk through the "pNext" chain to find extended features information
		// but group by version just to improve readability a bit
		for (auto v:versions) {
			str << std::endl << v.second << std::endl;
			unsigned versionCode = v.first;
		
			auto* pNextChain = (VkBaseOutStructure*)features2.pNext;
			while (pNextChain) {
				switch (pNextChain->sType) {

#if VK_VERSION_1_1
				case VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO:
					if (versionCode == 11)
						str << *(VkDeviceGroupDeviceCreateInfo*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES:
					if (versionCode == 11)
						str << *(VkPhysicalDeviceMultiviewFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES:
					if (versionCode == 11)
						str << *(VkPhysicalDeviceProtectedMemoryFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES:
					if (versionCode == 11)
						str << *(VkPhysicalDeviceSamplerYcbcrConversionFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES:
					if (versionCode == 11)
						str << *(VkPhysicalDeviceShaderDrawParametersFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES:
					if (versionCode == 11)
						str << *(VkPhysicalDeviceVariablePointersFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES:
					if (versionCode == 11)
						str << *(VkPhysicalDevice16BitStorageFeatures*)pNextChain << std::endl;
					break;

				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
					// VkPhysicalDeviceVulkan11Features is a container that overlaps settings contained in the smaller
					// structure. However, we can sometimes get more detail from the smaller structures, so it can be preferable
					// to use them
					if (versionCode == 11)
						str << *(VkPhysicalDeviceVulkan11Features*)pNextChain << std::endl;
					break;
#endif

#if VK_VERSION_1_2
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDevice8BitStorageFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceBufferDeviceAddressFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceDescriptorIndexingFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceHostQueryResetFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceImagelessFramebufferFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceScalarBlockLayoutFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceShaderAtomicInt64Features*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceShaderFloat16Int8Features*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceTimelineSemaphoreFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceUniformBufferStandardLayoutFeatures*)pNextChain << std::endl;
					break;
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceVulkanMemoryModelFeatures*)pNextChain << std::endl;
					break;

				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceVulkan12Features*)pNextChain << std::endl;
					break;
#endif

				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2:
					assert(0);
				default:
					if (versionCode == versions[0].first)
						str << "Unknown feature 0x" << std::hex << pNextChain->sType << std::dec << std::endl;
					break;
				}

				pNextChain = pNextChain->pNext;
			}
		}
	}

	static void LogPhysicalDeviceProperties(std::ostream& str, const VkPhysicalDeviceProperties2& properties2)
	{
		assert(properties2.sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR);

		str << "API version: 0x" << std::hex << properties2.properties.apiVersion << std::endl;
		str << "Driver version: 0x" << properties2.properties.driverVersion << std::endl;
		str << "VendorID: 0x" << properties2.properties.vendorID << std::endl;
		str << "DeviceID: 0x" << properties2.properties.deviceID << std::dec << std::endl;
		switch (properties2.properties.deviceType) {
		case VK_PHYSICAL_DEVICE_TYPE_OTHER: str << "Type: \'Other\'" << std::endl; break;
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: str << "Type: Integrated GPU" << std::endl; break;
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: str << "Type: Discrete GPU" << std::endl; break;
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: str << "Type: Virtual GPU" << std::endl; break;
		case VK_PHYSICAL_DEVICE_TYPE_CPU: str << "Type: CPU" << std::endl; break;
		default: str << "Type: Unknown" << std::endl; break;
		}
		str << "Device name: " << properties2.properties.deviceName << std::endl;
		// is properties2.properties.pipelineCacheUUID useful?
		str << std::endl << "VK1.0 limits" << std::endl;
		str << properties2.properties.limits << std::endl;
		str << properties2.properties.sparseProperties << std::endl;

		std::pair<unsigned, const char*> versions [] { 
			{11, "VK1.1"},
			{12, "VK1.2"},
			{99, "Extensions"}
		};

		// walk through the "pNext" chain to find extended properties information
		// but group by version just to improve readability a bit
		for (auto v:versions) {
			str << std::endl << v.second << std::endl;
			unsigned versionCode = v.first;
		
			auto* pNextChain = (VkBaseOutStructure*)properties2.pNext;
			while (pNextChain) {
				switch (pNextChain->sType) {

#if VK_VERSION_1_1
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES:
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES:
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES:
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES:
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES:
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES:
					// These are all subsets of VkPhysicalDeviceVulkan11Properties
					break;

				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES:
					if (versionCode == 11)
						str << *(VkPhysicalDeviceVulkan11Properties*)pNextChain << std::endl;
					break;
#endif

#if VK_VERSION_1_2
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES:
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES:
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES:
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES:
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES:
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES:
					// These are all subsets of VkPhysicalDeviceVulkan12Properties
					break;

				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES:
					if (versionCode == 12)
						str << *(VkPhysicalDeviceVulkan12Properties*)pNextChain << std::endl;
					break;
#endif

				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT:
					if (versionCode == 99)
						str << *(VkPhysicalDeviceConservativeRasterizationPropertiesEXT*)pNextChain << std::endl;
					break;

				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT:
					if (versionCode == 99)
						str << *(VkPhysicalDeviceTransformFeedbackPropertiesEXT*)pNextChain << std::endl;
					break;

				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR:
					assert(0);
				default:
					if (versionCode == versions[0].first)
						str << "Unknown properties struct 0x" << std::hex << pNextChain->sType << std::dec << std::endl;
					break;
				}

				pNextChain = pNextChain->pNext;
			}
		}
	}

	static VulkanSharedPtr<VkDevice> CreateUnderlyingDevice(
		SelectedPhysicalDevice physDev,
		const DeviceFeatures& xleFeatures,
		bool enableDebugLayer)
	{
		// The queue priority value are specific to a single VkDevice -- so it shouldn't affect priorities
		// relative to another application.
		// We ideally don't want any queue to starve any other queue; so we might be safest using the same
		// priority for all queues in all families
		float queue_priorities[1] = { 0.5f };

		VkDeviceQueueCreateInfo queue_info[3] = {};
		unsigned queueCount = 0;
		queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info[0].pNext = nullptr;
		queue_info[0].queueCount = 1;
		queue_info[0].pQueuePriorities = queue_priorities;
		queue_info[0].queueFamilyIndex = physDev._graphicsQueueFamily;
		++queueCount;

		if (xleFeatures._dedicatedTransferQueue) {
			if (physDev._dedicatedTransferQueueFamily == ~0u)
				Throw(std::runtime_error("Enabled the _dedicatedTransferQueue device feature, but this feature is not supported the device capabilities"));
			queue_info[queueCount].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_info[queueCount].pNext = nullptr;
			queue_info[queueCount].queueCount = 1;
			queue_info[queueCount].pQueuePriorities = queue_priorities;
			queue_info[queueCount].queueFamilyIndex = physDev._dedicatedTransferQueueFamily;
			++queueCount;
		}

		VkPhysicalDeviceFeatures2 enabledFeatures2 = {};
		enabledFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		auto* appender = (VkBaseInStructure*)&enabledFeatures2;

		// ShaderStages supported
		enabledFeatures2.features.geometryShader = xleFeatures._geometryShaders;

		// General rendering features
		VkPhysicalDeviceMultiviewFeatures multiViewFeatures = {};
		if (xleFeatures._viewInstancingRenderPasses) {
			multiViewFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
			multiViewFeatures.multiview = true;
			appender->pNext = (VkBaseInStructure*)&multiViewFeatures;
			appender = (VkBaseInStructure*)&multiViewFeatures;
		}

		VkPhysicalDeviceTransformFeedbackFeaturesEXT transformFeedbackFeatures = {};
		if (xleFeatures._streamOutput) {
			transformFeedbackFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
			transformFeedbackFeatures.geometryStreams = true;
			transformFeedbackFeatures.transformFeedback = true;
			appender->pNext = (VkBaseInStructure*)&transformFeedbackFeatures;
			appender = (VkBaseInStructure*)&transformFeedbackFeatures;
		}

		enabledFeatures2.features.depthBounds = xleFeatures._depthBounds;
		enabledFeatures2.features.samplerAnisotropy = xleFeatures._samplerAnisotrophy;
		enabledFeatures2.features.wideLines = xleFeatures._wideLines;
		enabledFeatures2.features.independentBlend = xleFeatures._independentBlend;
		enabledFeatures2.features.multiViewport = xleFeatures._multiViewport;

		VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures separateDepthStencilLayoutFeatures = {};
		if (xleFeatures._separateDepthStencilLayouts) {
			separateDepthStencilLayoutFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES;
			separateDepthStencilLayoutFeatures.separateDepthStencilLayouts = true;
			appender->pNext = (VkBaseInStructure*)&separateDepthStencilLayoutFeatures;
			appender = (VkBaseInStructure*)&separateDepthStencilLayoutFeatures;
		}

		// Resource types
		enabledFeatures2.features.imageCubeArray = xleFeatures._cubemapArrays;

		// Query types
		enabledFeatures2.features.pipelineStatisticsQuery = xleFeatures._queryShaderInvocation;

		// Additional shader instructions
		enabledFeatures2.features.shaderImageGatherExtended = xleFeatures._shaderImageGatherExtended;
		enabledFeatures2.features.fragmentStoresAndAtomics = xleFeatures._pixelShaderStoresAndAtomics;
		enabledFeatures2.features.vertexPipelineStoresAndAtomics = xleFeatures._vertexGeoTessellationShaderStoresAndAtomics;

		// texture compression types
		enabledFeatures2.features.textureCompressionETC2 = xleFeatures._textureCompressionETC2;
		enabledFeatures2.features.textureCompressionASTC_LDR = xleFeatures._textureCompressionASTC_LDR;
		enabledFeatures2.features.textureCompressionBC = xleFeatures._textureCompressionBC;

		VkPhysicalDeviceTextureCompressionASTCHDRFeaturesEXT astHDRFeatures = {};
		if (xleFeatures._textureCompressionASTC_HDR) {
			astHDRFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES_EXT;
			astHDRFeatures.textureCompressionASTC_HDR = true;
			appender->pNext = (VkBaseInStructure*)&astHDRFeatures;
			appender = (VkBaseInStructure*)&astHDRFeatures;
		}

		#if VK_VERSION_1_2
			VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeatures = {};
			if (xleFeatures._timelineSemaphore) {
				timelineSemaphoreFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
				timelineSemaphoreFeatures.timelineSemaphore = true;
				appender->pNext = (VkBaseInStructure*)&timelineSemaphoreFeatures;
				appender = (VkBaseInStructure*)&timelineSemaphoreFeatures;
			}
		#endif

		VkDeviceCreateInfo device_info = {};
		device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		device_info.pNext = &enabledFeatures2;
		device_info.queueCreateInfoCount = queueCount;
		device_info.pQueueCreateInfos = queue_info;

		const char* deviceExtensions[8];
		unsigned deviceExtensionCount = 0;
		const char* deviceLayers[8];
		unsigned deviceLayerCount = 0;

		if (xleFeatures._conservativeRaster)
			deviceExtensions[deviceExtensionCount++] = VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME;
		if (xleFeatures._streamOutput)
			deviceExtensions[deviceExtensionCount++] = VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME;
		if (xleFeatures._textureCompressionASTC_HDR)
			deviceExtensions[deviceExtensionCount++] = VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME;

		if (xleFeatures._timelineSemaphore)
			deviceExtensions[deviceExtensionCount++] = VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;	// because it's used internally, it's always required (promoted into Vulkan 1.2)
		deviceExtensions[deviceExtensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
		if (xleFeatures._viewInstancingRenderPasses)
			deviceExtensions[deviceExtensionCount++] = VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME; 	// promoted to Vulkan 1.2, HLSL compiler likes to require it

		if (enableDebugLayer)
			deviceLayers[deviceLayerCount++] = "VK_LAYER_KHRONOS_validation";

		device_info.enabledExtensionCount = deviceExtensionCount;
		device_info.ppEnabledExtensionNames = deviceExtensions;
		device_info.enabledLayerCount = deviceLayerCount;
		device_info.ppEnabledLayerNames = deviceLayers;

		VkDevice rawResult = nullptr;
		auto res = vkCreateDevice(physDev._dev, &device_info, Metal_Vulkan::g_allocationCallbacks, &rawResult);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while creating Vulkan logical device. You must have an up-to-date Vulkan driver installed."));
		return VulkanSharedPtr<VkDevice>(
			rawResult,
			[](VkDevice dev) { vkDestroyDevice(dev, Metal_Vulkan::g_allocationCallbacks); });
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

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

    static VkPresentModeKHR SelectPresentMode(IteratorRange<const VkPresentModeKHR*> availableModes, bool vsync)
    {
        // We have 3 vsync modes:
        //		VK_PRESENT_MODE_MAILBOX_KHR
		//		VK_PRESENT_MODE_FIFO_RELAXED_KHR
		//		VK_PRESENT_MODE_FIFO_KHR
		// & one non-vsync mode:
		//		VK_PRESENT_MODE_IMMEDIATE_KHR
		//
		// VK_PRESENT_MODE_FIFO_KHR should always be available, and VK_PRESENT_MODE_MAILBOX_KHR is apparently
		// the lowest latency mode
		// VK_PRESENT_MODE_MAILBOX_KHR can also guarantee that vkAcquireNextImageKHR() is non blocking if we
		// triple buffer (with some particular platform exceptions)
		// (see Q&A in https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_KHR_swapchain.html)

		if (vsync) {
			if (std::find(availableModes.begin(), availableModes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != availableModes.end())
				return VK_PRESENT_MODE_MAILBOX_KHR;

			if (std::find(availableModes.begin(), availableModes.end(), VK_PRESENT_MODE_FIFO_RELAXED_KHR) != availableModes.end())
				return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
		} else {
			if (std::find(availableModes.begin(), availableModes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != availableModes.end())
				return VK_PRESENT_MODE_IMMEDIATE_KHR;
		}

		assert(std::find(availableModes.begin(), availableModes.end(), VK_PRESENT_MODE_FIFO_KHR) != availableModes.end());
		return VK_PRESENT_MODE_FIFO_KHR;

        // VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
		// const bool onlyVSyncModes = false;
        // for (auto pm:availableModes) {
        //     if (pm == VK_PRESENT_MODE_MAILBOX_KHR) {
        //         swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        //         break;
        //     }
        //     if (!onlyVSyncModes &&
        //         (swapchainPresentMode != VK_PRESENT_MODE_MAILBOX_KHR) &&
        //         (pm == VK_PRESENT_MODE_IMMEDIATE_KHR)) {
        //         swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        //     }
        // }
        // return swapchainPresentMode;
    }

    static VkQueue GetQueue(VkDevice dev, unsigned queueFamilyIndex, unsigned queueIndex=0)
    {
        VkQueue queue = nullptr;
        vkGetDeviceQueue(dev, queueFamilyIndex, queueIndex, &queue);
        return queue;
    }

	static FormatCapability TestFormatProperties(VkFormatProperties fmtProps, BindFlag::BitField bindingType)
	{
		// bind flags not tested:
		// 	VertexBuffer, IndexBuffer, ConstantBuffer, StreamOutput, DrawIndirectArgs, RawViews
		// 	PresentationSrc
		if (bindingType & BindFlag::ShaderResource) {
			auto req = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;		// VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
			if ((fmtProps.optimalTilingFeatures & req) != req)
				return FormatCapability::NotSupported;
		}
		if (bindingType & BindFlag::RenderTarget) {
			auto req = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
			if ((fmtProps.optimalTilingFeatures & req) != req)
				return FormatCapability::NotSupported;
		}
		if (bindingType & BindFlag::DepthStencil) {
			auto req = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
			if ((fmtProps.optimalTilingFeatures & req) != req)
				return FormatCapability::NotSupported;
		}
		if (bindingType & BindFlag::UnorderedAccess) {
			auto req = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;	// VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT
			if ((fmtProps.optimalTilingFeatures & req) != req)
				return FormatCapability::NotSupported;
		}
		if (bindingType & BindFlag::InputAttachment) {
			auto req = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
			if ((fmtProps.optimalTilingFeatures & req) != req)
				return FormatCapability::NotSupported;
		}
		if (bindingType & BindFlag::TransferSrc) {
			auto req = VK_FORMAT_FEATURE_BLIT_SRC_BIT;
			if ((fmtProps.optimalTilingFeatures & req) != req)
				return FormatCapability::NotSupported;
		}
		if (bindingType & BindFlag::TransferDst) {
			auto req = VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
			if ((fmtProps.optimalTilingFeatures & req) != req)
				return FormatCapability::NotSupported;
		}
		if ((bindingType & (BindFlag::TexelBuffer|BindFlag::UnorderedAccess)) == (BindFlag::TexelBuffer|BindFlag::UnorderedAccess)) {
			auto req = VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;	// VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT
			if ((fmtProps.optimalTilingFeatures & req) != req)
				return FormatCapability::NotSupported;
		}
		if ((bindingType & (BindFlag::TexelBuffer|BindFlag::ShaderResource)) == (BindFlag::TexelBuffer|BindFlag::ShaderResource)) {
			auto req = VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;
			if ((fmtProps.optimalTilingFeatures & req) != req)
				return FormatCapability::NotSupported;
		}

		return FormatCapability::Supported;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

	std::shared_ptr<IDevice>    APIInstance::CreateDevice(unsigned configurationIdx, const DeviceFeatures& features)
	{
		if (configurationIdx >= _physicalDevices.size())
			Throw(std::runtime_error("Invalid configuration index"));
		return std::make_shared<Device>(_instance, _physicalDevices[configurationIdx], features, _features._debugValidation);
	}

	std::shared_ptr<IDevice>    APIInstance::CreateDevice(VkPhysicalDevice physDev, unsigned renderingQueueFamily, const DeviceFeatures& features)
	{
		return std::make_shared<Device>(_instance, SelectedPhysicalDevice{physDev, renderingQueueFamily}, features, _features._debugValidation);
	}

	unsigned                    APIInstance::GetDeviceConfigurationCount()
	{
		return (unsigned)_physicalDevices.size();
	}

    DeviceConfigurationProps    APIInstance::GetDeviceConfigurationProps(unsigned configurationIdx)
	{
		if (configurationIdx >= _physicalDevices.size())
			Throw(std::runtime_error("Invalid configuration index"));

		VkPhysicalDeviceProperties2 props = {};
		props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		vkGetPhysicalDeviceProperties2(_physicalDevices[configurationIdx]._dev, &props);

		DeviceConfigurationProps result = {};
		XlCopyString(result._driverName, props.properties.deviceName);
		result._driverVersion = props.properties.driverVersion;
		switch (props.properties.deviceType) {
			case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: result._physicalDeviceType = PhysicalDeviceType::IntegratedGPU; break;
			case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: result._physicalDeviceType = PhysicalDeviceType::DiscreteGPU; break;
			case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: result._physicalDeviceType = PhysicalDeviceType::VirtualGPU; break;
			case VK_PHYSICAL_DEVICE_TYPE_CPU: result._physicalDeviceType = PhysicalDeviceType::CPU; break;
			default: result._physicalDeviceType = PhysicalDeviceType::Unknown; break;
		}
		result._vendorId = props.properties.vendorID;
		result._deviceId = props.properties.deviceID;

		return result;
	}

	DeviceFeatures              APIInstance::QueryFeatureCapability(unsigned configurationIdx)
	{
		if (configurationIdx >= _physicalDevices.size())
			Throw(std::runtime_error("Invalid configuration index"));

		#define APPEND_STRUCT(X, T)																				\
			X X##_inst = {}; X##_inst.sType = T;																\
			appender->pNext = (VkBaseOutStructure*)&X##_inst; appender = (VkBaseOutStructure*)&X##_inst;		\
			/**/;

		VkPhysicalDeviceProperties2 props = {};
		props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		vkGetPhysicalDeviceProperties2(_physicalDevices[configurationIdx]._dev, &props);
		auto* appender = (VkBaseOutStructure*)&props;
		APPEND_STRUCT(VkPhysicalDeviceConservativeRasterizationPropertiesEXT, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT);
		APPEND_STRUCT(VkPhysicalDeviceTransformFeedbackPropertiesEXT, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT);

		VkPhysicalDeviceFeatures2 features = {};
		features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		appender = (VkBaseOutStructure*)&features;
		APPEND_STRUCT(VkPhysicalDeviceVulkan11Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
		APPEND_STRUCT(VkPhysicalDeviceVulkan12Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
		APPEND_STRUCT(VkPhysicalDeviceTransformFeedbackFeaturesEXT, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT);
		APPEND_STRUCT(VkPhysicalDeviceTextureCompressionASTCHDRFeaturesEXT, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES_EXT);	// brought into vk1.3 core

		#undef APPEND_STRUCT

		vkGetPhysicalDeviceProperties2(_physicalDevices[configurationIdx]._dev, &props);
		vkGetPhysicalDeviceFeatures2(_physicalDevices[configurationIdx]._dev, &features);

		PhysicalDeviceExtensionQuery ext{_physicalDevices[configurationIdx]._dev};
		bool hasStreamOutputExt = std::find_if(ext._extensions.begin(), ext._extensions.end(), [](const auto& q) { return XlEqString(q.extensionName, VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME); }) != ext._extensions.end();
		bool hasASTCHDRExt = std::find_if(ext._extensions.begin(), ext._extensions.end(), [](const auto& q) { return XlEqString(q.extensionName, VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME); }) != ext._extensions.end();
		bool hasConservativeRasterExt = std::find_if(ext._extensions.begin(), ext._extensions.end(), [](const auto& q) { return XlEqString(q.extensionName, VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME); }) != ext._extensions.end();
		bool hasTimelineSemaphoreExt = std::find_if(ext._extensions.begin(), ext._extensions.end(), [](const auto& q) { return XlEqString(q.extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME); }) != ext._extensions.end();
		bool hasShaderViewportIndex = std::find_if(ext._extensions.begin(), ext._extensions.end(), [](const auto& q) { return XlEqString(q.extensionName, VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME); }) != ext._extensions.end();

		DeviceFeatures result;

		// ShaderStages supported
		result._geometryShaders = features.features.geometryShader;

		// General rendering features
		result._viewInstancingRenderPasses = hasShaderViewportIndex && VkPhysicalDeviceVulkan11Features_inst.multiview;
		if (hasStreamOutputExt)
			result._streamOutput = 
					VkPhysicalDeviceTransformFeedbackFeaturesEXT_inst.geometryStreams
				&&  VkPhysicalDeviceTransformFeedbackFeaturesEXT_inst.transformFeedback;
		result._depthBounds = features.features.depthBounds;
		result._samplerAnisotrophy = features.features.samplerAnisotropy;
		result._wideLines = features.features.wideLines;
		result._conservativeRaster = hasConservativeRasterExt;
		result._multiViewport = features.features.multiViewport;
		result._independentBlend = features.features.independentBlend;
		result._separateDepthStencilLayouts = VkPhysicalDeviceVulkan12Features_inst.separateDepthStencilLayouts;

		// Resource types
		result._cubemapArrays = features.features.imageCubeArray;

		// Query types
		result._queryShaderInvocation = features.features.pipelineStatisticsQuery;
		if (hasStreamOutputExt)
			result._queryStreamOutput = VkPhysicalDeviceTransformFeedbackPropertiesEXT_inst.transformFeedbackQueries;
		result._timelineSemaphore = hasTimelineSemaphoreExt;

		// Additional shader instructions
		result._shaderImageGatherExtended = features.features.shaderImageGatherExtended;
		result._pixelShaderStoresAndAtomics = features.features.fragmentStoresAndAtomics;
		result._vertexGeoTessellationShaderStoresAndAtomics = features.features.vertexPipelineStoresAndAtomics;

		// texture compression types
		result._textureCompressionETC2 = features.features.textureCompressionETC2;
		result._textureCompressionASTC_LDR = features.features.textureCompressionASTC_LDR;
		result._textureCompressionBC = features.features.textureCompressionBC;

		result._textureCompressionASTC_HDR = false;
		if (hasASTCHDRExt)
			result._textureCompressionASTC_HDR = VkPhysicalDeviceTextureCompressionASTCHDRFeaturesEXT_inst.textureCompressionASTC_HDR;

		// queues
		result._dedicatedTransferQueue = _physicalDevices[configurationIdx]._dedicatedTransferQueueFamily != ~0u;
		result._dedicatedComputeQueue = _physicalDevices[configurationIdx]._dedicatedComputeQueueFamily != ~0u;

		return result;
	}

	bool                        APIInstance::QueryPresentationChainCompatibility(unsigned configurationIdx, const void* platformWindowHandle)
	{
		if (configurationIdx >= _physicalDevices.size())
			Throw(std::runtime_error("Invalid configuration index"));
		if (!platformWindowHandle)
			Throw(std::runtime_error("Invalid platform window handle"));

		auto surface = CreateSurface(_instance.get(), platformWindowHandle);
		
		VkBool32 supportsPresent = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(
			_physicalDevices[configurationIdx]._dev, _physicalDevices[configurationIdx]._graphicsQueueFamily,
			surface.get(), &supportsPresent);
		return supportsPresent;
	}

    FormatCapability            APIInstance::QueryFormatCapability(unsigned configurationIdx, Format format, BindFlag::BitField bindingType)
	{
		if (configurationIdx >= _physicalDevices.size())
			Throw(std::runtime_error("Invalid configuration index"));

		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties(_physicalDevices[configurationIdx]._dev, (VkFormat)Metal_Vulkan::AsVkFormat(format), &formatProps);
		return TestFormatProperties(formatProps, bindingType);
	}

	std::string APIInstance::LogPhysicalDevice(unsigned configurationIdx)
	{
		if (configurationIdx >= _physicalDevices.size())
			Throw(std::runtime_error("Invalid configuration index"));

		#define APPEND_STRUCT(X, T)																				\
			X X##_inst = {}; X##_inst.sType = T;																\
			appender->pNext = (VkBaseOutStructure*)&X##_inst; appender = (VkBaseOutStructure*)&X##_inst;		\
			/**/;

		std::stringstream str;

		{
			VkPhysicalDeviceProperties2 properties = {};
			properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
			auto* appender = (VkBaseOutStructure*)&properties;

			#if VK_VERSION_1_1
				APPEND_STRUCT(VkPhysicalDeviceIDProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES);
				APPEND_STRUCT(VkPhysicalDeviceMaintenance3Properties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES);
				APPEND_STRUCT(VkPhysicalDeviceMultiviewProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES);
				APPEND_STRUCT(VkPhysicalDevicePointClippingProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES);
				APPEND_STRUCT(VkPhysicalDeviceProtectedMemoryProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES);
				APPEND_STRUCT(VkPhysicalDeviceSubgroupProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES);
			#endif
			#if VK_VERSION_1_2
				APPEND_STRUCT(VkPhysicalDeviceDepthStencilResolveProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES);
				APPEND_STRUCT(VkPhysicalDeviceDescriptorIndexingProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES);
				APPEND_STRUCT(VkPhysicalDeviceDriverProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES);
				APPEND_STRUCT(VkPhysicalDeviceFloatControlsProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES);
				APPEND_STRUCT(VkPhysicalDeviceSamplerFilterMinmaxProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES);
				APPEND_STRUCT(VkPhysicalDeviceTimelineSemaphoreProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES);
				APPEND_STRUCT(VkPhysicalDeviceVulkan11Properties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES);		// not added until 1.2
				APPEND_STRUCT(VkPhysicalDeviceVulkan12Properties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES);
			#endif
			// do we need to check if the extension is available for these objects?
			APPEND_STRUCT(VkPhysicalDeviceConservativeRasterizationPropertiesEXT, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT);
			APPEND_STRUCT(VkPhysicalDeviceTransformFeedbackPropertiesEXT, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT);
			vkGetPhysicalDeviceProperties2(_physicalDevices[configurationIdx]._dev, &properties);
			str << "PHYSICAL DEVICE PROPERTIES AND LIMITS" << std::endl;
			LogPhysicalDeviceProperties(str, properties);
		}

		{
			VkPhysicalDeviceFeatures2 features = {};
			features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			auto* appender = (VkBaseOutStructure*)&features;
			#if VK_VERSION_1_1
				APPEND_STRUCT(VkDeviceGroupDeviceCreateInfo, VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO);
				APPEND_STRUCT(VkPhysicalDeviceMultiviewFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceProtectedMemoryFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceSamplerYcbcrConversionFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceShaderDrawParametersFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceVariablePointersFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES);
				APPEND_STRUCT(VkPhysicalDevice16BitStorageFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceVulkan11Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
			#endif
			#if VK_VERSION_1_2
				APPEND_STRUCT(VkPhysicalDevice8BitStorageFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceBufferDeviceAddressFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceDescriptorIndexingFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceHostQueryResetFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceImagelessFramebufferFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceScalarBlockLayoutFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceShaderAtomicInt64Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceShaderFloat16Int8Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceTimelineSemaphoreFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceUniformBufferStandardLayoutFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceVulkanMemoryModelFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES);
				APPEND_STRUCT(VkPhysicalDeviceVulkan12Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
			#endif
			vkGetPhysicalDeviceFeatures2(_physicalDevices[configurationIdx]._dev, &features);
			str << std::endl << "TOGGLEABLE PHYSICAL DEVICE FEATURES" << std::endl;
			LogPhysicalDeviceFeatures(str, features);
		}

		str << std::endl;
		LogPhysicalDeviceExtensions(str, _physicalDevices[configurationIdx]._dev);

		#undef APPEND_STRUCT

		return str.str();
	}

	std::string APIInstance::LogInstance(const void* presentationChainPlatformValue)
	{
		std::stringstream str;
		if (presentationChainPlatformValue) {
			auto surface = CreateSurface(_instance.get(), presentationChainPlatformValue);
			LogPhysicalDevices(str, _instance.get(), surface.get());
		} else {
			LogPhysicalDevices(str, _instance.get(), nullptr);
		}
		LogInstanceLayers(str);
		return str.str();
	}

	VkInstance APIInstance::GetVulkanInstance()
	{
		return _instance.get();
	}

	VkPhysicalDevice APIInstance::GetPhysicalDevice(unsigned configurationIdx)
	{
		if (configurationIdx >= _physicalDevices.size())
			Throw(std::runtime_error("Invalid configuration index"));
		return _physicalDevices[configurationIdx]._dev;
	}

	void* APIInstance::QueryInterface(size_t guid)
	{
		if (guid == typeid(IAPIInstanceVulkan).hash_code())
			return (IAPIInstanceVulkan*)this;
		else if (guid == typeid(APIInstance).hash_code())
			return this;
		else if (guid == typeid(IAPIInstance).hash_code())
			return (IAPIInstance*)this;
		return nullptr;
	}

	APIInstance::APIInstance(const APIFeatures& features)
	: _features(features)
	{
            // todo -- we need to do this in a bind to DLL step
        Metal_Vulkan::InitFormatConversionTables();

			//
			//	Create the instance. This will attach the Vulkan DLL. If there are no valid Vulkan drivers
			//	available, it will throw an exception here.
			//
		auto appname = GetApplicationName();

		VkApplicationInfo app_info = {};
		app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app_info.pNext = nullptr;
		app_info.pApplicationName = appname.c_str();
		app_info.applicationVersion = 1;
		app_info.pEngineName = "XLE";
		app_info.engineVersion = 1;
		app_info.apiVersion = VK_HEADER_VERSION_COMPLETE;

		const char* instanceExtensions[8];
		unsigned instanceExtensionCount = 0;
		const char* instanceLayers[8];
		unsigned instanceLayerCount = 0;

		auto availableLayers = EnumerateLayers();

		instanceExtensions[instanceExtensionCount++] = VK_KHR_SURFACE_EXTENSION_NAME;
		#if PLATFORMOS_TARGET  == PLATFORMOS_WINDOWS
			instanceExtensions[instanceExtensionCount++] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
		#endif
		#if defined(VULKAN_ENABLE_DEBUG_EXTENSIONS)
			// install debug utils even if validations is off (since it's required for naming objects for RenderDoc, etc)
			instanceExtensions[instanceExtensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

			if (_features._debugValidation) {
				auto i = std::find_if(
					availableLayers.begin(), availableLayers.end(),
					[](VkLayerProperties layer) { return XlEqString(layer.layerName, "VK_LAYER_KHRONOS_validation"); });
				if (i != availableLayers.end()) {
					instanceLayers[instanceLayerCount++] = "VK_LAYER_KHRONOS_validation";
				} else {
					Log(Warning) << "Cannot enable debug validation because required Vulkan layer is not present. Ensure that the Vulkan SDK is installed" << std::endl;
					_features._debugValidation = false;
				}
			}
		#else
			if (_features._debugValidation) {
				Log(Warning) << "Cannot enable debug validation because required code was compiled out of this configuration. Check the VULKAN_ENABLE_DEBUG_EXTENSIONS preprocessor symbol" << std::endl;
				_features._debugValidation = false;
			}
		#endif

		VkInstanceCreateInfo inst_info = {};
		inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		inst_info.pNext = nullptr;
		inst_info.flags = 0;
		inst_info.pApplicationInfo = &app_info;
		inst_info.enabledExtensionCount = instanceExtensionCount;
		inst_info.ppEnabledExtensionNames = instanceExtensions;
		inst_info.enabledLayerCount = instanceLayerCount;
        inst_info.ppEnabledLayerNames = instanceLayers;

		VkInstance rawResult = nullptr;
		VkResult res = vkCreateInstance(&inst_info, Metal_Vulkan::g_allocationCallbacks, &rawResult);
		_instance = VulkanSharedPtr<VkInstance>(
			rawResult,
			[](VkInstance inst) { vkDestroyInstance(inst, Metal_Vulkan::g_allocationCallbacks); });
        if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure in Vulkan instance construction. You must have an up-to-date Vulkan driver installed."));

		if (features._debugValidation)
			_msgHandler = std::make_unique<DebugMessageHandler>(_instance.get());

		// Find the physical device options
		auto devices = EnumeratePhysicalDevices(_instance.get());
		if (devices.empty())
			Throw(Exceptions::BasicLabel("Could not find any Vulkan physical devices. You must have an up-to-date Vulkan driver installed."));

		for (auto dev:devices) {
			auto queueProps = EnumerateQueueFamilyProperties(dev);

			unsigned dedicatedTransferQueueFamily = ~0u;
        	unsigned dedicatedComputeQueueFamily = ~0u;
			for (unsigned qi=0; qi<unsigned(queueProps.size()); ++qi) {
				const auto& qprops=queueProps[qi];
				// we say a queue family is "dedicated transfer", if it can support transfer but not graphics or compute
				// likewise a dedicate compute queue family won't support graphics
				if ((qprops.queueFlags & VK_QUEUE_TRANSFER_BIT) && !(qprops.queueFlags & (VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)))
					if (dedicatedTransferQueueFamily == ~0u)
						dedicatedTransferQueueFamily = qi;
				if ((qprops.queueFlags & VK_QUEUE_COMPUTE_BIT) && !(qprops.queueFlags & VK_QUEUE_GRAPHICS_BIT))
					if (dedicatedComputeQueueFamily == ~0u)
						dedicatedComputeQueueFamily = qi;
			}

			// Add a configuration option for all queue families that have the graphics bit set
			// client can test them each separately for compatibility for rendering to a specific window
			// physical devices that don't support graphics (ie, compute-only) aren't supported
			for (unsigned qi=0; qi<unsigned(queueProps.size()); ++qi)
				if (queueProps[qi].queueFlags & VK_QUEUE_GRAPHICS_BIT)
					_physicalDevices.push_back({dev, qi, dedicatedTransferQueueFamily, dedicatedComputeQueueFamily});
		}
	}

	APIInstance::~APIInstance()
	{
	}

/////////////////////////////////////////////////////////////////////////////////////

    Device::Device(
		VulkanSharedPtr<VkInstance> instance,
    	SelectedPhysicalDevice physDev,
		const DeviceFeatures& xleFeatures,
		bool enableDebugLayer)
	: _instance(std::move(instance))
	, _physDev(std::move(physDev))
    {
		_initializationThread = std::this_thread::get_id();

		_underlying = CreateUnderlyingDevice(_physDev, xleFeatures, enableDebugLayer);
		auto extensionFunctions = std::make_shared<Metal_Vulkan::ExtensionFunctions>(_instance.get());
		_globalsContainer = std::make_shared<Metal_Vulkan::GlobalsContainer>();
		_globalsContainer->_objectFactory = Metal_Vulkan::ObjectFactory{_instance.get(), _physDev._dev, _underlying, xleFeatures, extensionFunctions};
		auto& objFactory = _globalsContainer->_objectFactory;
		auto& pools = _globalsContainer->_pools;

		_graphicsQueue = std::make_shared<Metal_Vulkan::SubmissionQueue>(objFactory, GetQueue(_underlying.get(), _physDev._graphicsQueueFamily), _physDev._graphicsQueueFamily);
		_destrQueue = objFactory.CreateMarkerTrackingDestroyer(_graphicsQueue->GetTracker());
		objFactory.SetDefaultDestroyer(_destrQueue);
		objFactory._graphicsQueueFamily = _physDev._graphicsQueueFamily;

		if (xleFeatures._dedicatedTransferQueue) {
			_dedicatedTransferQueue = std::make_shared<Metal_Vulkan::SubmissionQueue>(objFactory, GetQueue(_underlying.get(), _physDev._dedicatedTransferQueueFamily), _physDev._dedicatedTransferQueueFamily);
			objFactory._dedicatedTransferQueueFamily = _physDev._dedicatedTransferQueueFamily;
		}

		pools._mainDescriptorPool = Metal_Vulkan::DescriptorPool(objFactory, _graphicsQueue->GetTracker(), "main-descriptor-pool");
		pools._longTermDescriptorPool = Metal_Vulkan::DescriptorPool(objFactory, _graphicsQueue->GetTracker(), "long-term-descriptor-pool");
		pools._renderPassPool = Metal_Vulkan::VulkanRenderPassPool(objFactory);
		pools._mainPipelineCache = objFactory.CreatePipelineCache();
		pools._dummyResources = Metal_Vulkan::DummyResources(objFactory);
		pools._temporaryStorageManager = std::make_unique<Metal_Vulkan::TemporaryStorageManager>(objFactory, _graphicsQueue->GetTracker());

		auto& limits = objFactory.GetPhysicalDeviceProperties().limits;
		_limits._constantBufferOffsetAlignment = limits.minUniformBufferOffsetAlignment;
		_limits._unorderedAccessBufferOffsetAlignment = limits.minStorageBufferOffsetAlignment;
		_limits._texelBufferOffsetAlignment = limits.minTexelBufferOffsetAlignment;
		_limits._copyBufferOffsetAlignment = limits.optimalBufferCopyOffsetAlignment;
		_limits._maxPushConstantsSize = limits.maxPushConstantsSize;
		assert(_limits._constantBufferOffsetAlignment != 0);
		assert(_limits._unorderedAccessBufferOffsetAlignment != 0);
		assert(_limits._texelBufferOffsetAlignment != 0);
		assert(_limits._copyBufferOffsetAlignment != 0);
		assert(_limits._maxPushConstantsSize != 0);
	}

    Device::~Device()
    {
		_foregroundPrimaryContext.reset();
		_destrQueue = nullptr;
		_graphicsQueue = nullptr;
		_dedicatedTransferQueue = nullptr;
        _globalsContainer = nullptr;
    }

    struct SwapChainProperties
    {
        VkFormat                        _fmt;
        VkExtent2D                      _extent;
        uint32_t                        _desiredNumberOfImages;
        VkSurfaceTransformFlagBitsKHR   _preTransform;
        VkPresentModeKHR                _presentMode;
		BindFlag::BitField              _bindFlags;
    };

    static SwapChainProperties DecideSwapChainProperties(
        VkPhysicalDevice phyDev, VkSurfaceKHR surface,
		const PresentationChainDesc& requestedDesc)
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

		VkFormat vkPreferedFormat = VK_FORMAT_B8G8R8A8_SRGB;
		if (requestedDesc._format != Format(0))
			vkPreferedFormat = (VkFormat)Metal_Vulkan::AsVkFormat(requestedDesc._format);

		for (auto f:fmts)
			if (f.format == vkPreferedFormat)
				result._fmt = vkPreferedFormat;
				
		if (result._fmt == VK_FORMAT_UNDEFINED) {
			for (auto f:fmts)
				if (f.format == VK_FORMAT_B8G8R8A8_SRGB)
					result._fmt = VK_FORMAT_B8G8R8A8_SRGB;
		}

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
        result._presentMode = SelectPresentMode(MakeIteratorRange(presentModes), requestedDesc._vsync);

        // width and height are either both -1, or both not -1.
        if (surfCapabilities.currentExtent.width == (uint32_t)-1) {
            // If the surface size is undefined, the size is set to
            // the size of the images requested.
            result._extent.width = requestedDesc._width;
            result._extent.height = requestedDesc._height;
        } else {
            // If the surface size is defined, the swap chain size must match
            result._extent = surfCapabilities.currentExtent;
        }
        
        // Determine the number of VkImage's to use in the swap chain
		// Note that the ideal number of images is surfCapabilities.minImageCount+1
        result._desiredNumberOfImages = std::max(surfCapabilities.minImageCount, requestedDesc._imageCount);
        if (surfCapabilities.maxImageCount > 0)
            result._desiredNumberOfImages = std::min(result._desiredNumberOfImages, surfCapabilities.maxImageCount);

        // setting "preTransform" to current transform... but clearing out other bits if the identity bit is set
        result._preTransform = 
            (surfCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
            ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : surfCapabilities.currentTransform;

		result._bindFlags = BindFlag::PresentationSrc;
		if ((surfCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) && (requestedDesc._bindFlags & BindFlag::RenderTarget))
			result._bindFlags |= BindFlag::RenderTarget;
		if ((surfCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_STORAGE_BIT) && (requestedDesc._bindFlags & BindFlag::UnorderedAccess))
			result._bindFlags |= BindFlag::UnorderedAccess;
		if ((surfCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_SAMPLED_BIT) && (requestedDesc._bindFlags & BindFlag::ShaderResource))
			result._bindFlags |= BindFlag::ShaderResource;
		if ((surfCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) && (requestedDesc._bindFlags & BindFlag::TransferDst))
			result._bindFlags |= BindFlag::TransferDst;

        return result;
    }

    static VulkanSharedPtr<VkSwapchainKHR> CreateUnderlyingSwapChain(
        VkDevice dev, VkSurfaceKHR surface, VkSwapchainKHR oldSwapChain,
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
        swapChainInfo.oldSwapchain = oldSwapChain;
        swapChainInfo.clipped = true;		// note -- when this is true, reading back from the presentation image itself may not contain all of the pixels
        swapChainInfo.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        swapChainInfo.imageUsage = Metal_Vulkan::AsImageUsageFlags(props._bindFlags);
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

        // Double check to make sure our physical device is compatible with this surface
		// if you hit this, there are a few things you can do:
		//	a) check that IAPIInstanceVulkan::SetWindowPlatformValue() is called with a relevant window handle before any other IAPIInstance methods
		//	b) if you need to render to multiple windows, they must all be renderable with the same vulkan "physical device". Physical devices can be
		//		compatible with rendering to a specific window, or incompatable. We only a single physical device per IAPIInstance / IDevice, and
		//		only check at most a single window for compatibility
        VkBool32 supportsPresent = false;
		auto res = vkGetPhysicalDeviceSurfaceSupportKHR(
			_physDev._dev, _graphicsQueue->GetQueueFamilyIndex(), surface.get(), &supportsPresent);
		if (res != VK_SUCCESS || !supportsPresent) 
            Throw(::Exceptions::BasicLabel("Presentation surface is not compatible with selected physical device. This may occur if the wrong physical device is selected, and it cannot render to the output window."));
        
        return std::make_unique<PresentationChain>(
			shared_from_this(),
            _globalsContainer->_objectFactory, std::move(surface), desc,
			_graphicsQueue.get(), platformValue);
    }

    std::shared_ptr<IThreadContext> Device::GetImmediateContext()
    {
        if (!_foregroundPrimaryContext) {
			_foregroundPrimaryContext = std::make_shared<ThreadContext>(shared_from_this(), _graphicsQueue);
			_foregroundPrimaryContext->AttachDestroyer(_destrQueue);

			// We need to ensure that the "dummy" resources get their layout change to complete initialization
			auto& pools = _globalsContainer->_pools;
			pools._dummyResources.CompleteInitialization(*_foregroundPrimaryContext->GetMetalContext());
		}
		return _foregroundPrimaryContext;
    }

    std::unique_ptr<IThreadContext> Device::CreateDeferredContext()
    {
		return std::make_unique<ThreadContext>(shared_from_this(), _graphicsQueue);
    }

	std::unique_ptr<IThreadContext> Device::CreateDedicatedTransferContext()
	{
		if (!_dedicatedTransferQueue) return nullptr;
		return std::make_unique<ThreadContext>(shared_from_this(), _dedicatedTransferQueue);
	}

	IResourcePtr Device::CreateResource(
		const ResourceDesc& desc,
		StringSection<> name,
		const std::function<SubResourceInitData(SubResourceId)>& initData)
	{
		return Metal_Vulkan::Internal::CreateResource(_globalsContainer->_objectFactory, desc, name, initData);
	}

	FormatCapability    Device::QueryFormatCapability(Format format, BindFlag::BitField bindingType)
	{
		assert(_underlying);
		auto fmtProps = _globalsContainer->_objectFactory.GetFormatProperties((VkFormat)Metal_Vulkan::AsVkFormat(format));
		return TestFormatProperties(fmtProps, bindingType);
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

	std::shared_ptr<Metal_Vulkan::IAsyncTracker> Device::GetGraphicsQueueAsyncTracker()
	{
		return _graphicsQueue->GetTracker();
	}

	std::shared_ptr<Metal_Vulkan::IAsyncTracker> Device::GetDedicatedTransferAsyncTracker()
	{
		if (!_dedicatedTransferQueue) return nullptr;
		return _dedicatedTransferQueue->GetTracker();
	}

	void Device::GetInternalMetrics(InternalMetricsType type, IteratorRange<void*> dst) const
	{
		switch (type) {
		case InternalMetricsType::LongTermDescriptorPoolMetrics:
		case InternalMetricsType::MainDescriptorPoolMetrics:
			if (dst.size() != sizeof(Metal_Vulkan::DescriptorPoolMetrics))
				Throw(std::runtime_error("Bad metrics structure size in Vulkan Device::GetInternalMetrics"));
			if (type == InternalMetricsType::LongTermDescriptorPoolMetrics) {
				*(Metal_Vulkan::DescriptorPoolMetrics*)dst.begin() = _globalsContainer.get()->_pools._longTermDescriptorPool.GetMetrics();
			} else {
				*(Metal_Vulkan::DescriptorPoolMetrics*)dst.begin() = _globalsContainer.get()->_pools._mainDescriptorPool.GetMetrics();
			}
			break;

		default:
			Throw(std::runtime_error("Unknown metrics type"));
		}
	}

	void Device::Stall()
	{
		_graphicsQueue->WaitIdle();
	}

	void Device::PrepareForDestruction()
	{
		vkDeviceWaitIdle(_underlying.get());
	}

	Metal_Vulkan::GlobalPools& Device::GetGlobalPools() { return _globalsContainer->_pools; }
	Metal_Vulkan::ObjectFactory& Device::GetObjectFactory() { return _globalsContainer->_objectFactory; }

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

	const DeviceFeatures& Device::GetDeviceFeatures() const
	{
		return _globalsContainer->_objectFactory.GetXLEFeatures();
	}

	const DeviceLimits& Device::GetDeviceLimits() const
	{
		return _limits;
	}

	std::shared_ptr<ICompiledPipelineLayout> Device::CreatePipelineLayout(const PipelineLayoutInitializer& desc, StringSection<> name)
	{
		if (!_globalsContainer->_pools._descriptorSetLayoutCache)
			_globalsContainer->_pools._descriptorSetLayoutCache = Metal_Vulkan::Internal::CreateCompiledDescriptorSetLayoutCache();

		Metal_Vulkan::Internal::ValidatePipelineLayout(_physDev._dev, desc);

		using DescriptorSetBinding = Metal_Vulkan::CompiledPipelineLayout::DescriptorSetBinding;
		using PushConstantsBinding = Metal_Vulkan::CompiledPipelineLayout::PushConstantsBinding;

		std::vector<DescriptorSetBinding> descSetBindings;
		descSetBindings.resize(desc.GetDescriptorSets().size());
		for (unsigned c=0; c<desc.GetDescriptorSets().size(); ++c) {
			auto& srcBinding = desc.GetDescriptorSets()[c];
			descSetBindings[c]._name = srcBinding._name;
			auto compiled = _globalsContainer->_pools._descriptorSetLayoutCache->CompileDescriptorSetLayout(
				srcBinding._signature,
				srcBinding._name,
				srcBinding._pipelineType == PipelineType::Graphics ? VK_SHADER_STAGE_ALL_GRAPHICS : VK_SHADER_STAGE_COMPUTE_BIT );
			descSetBindings[c]._layout = compiled->_layout;
			descSetBindings[c]._blankDescriptorSet = compiled->_blankBindings;
			#if defined(VULKAN_VERBOSE_DEBUG)
				descSetBindings[c]._blankDescriptorSetDebugInfo = compiled->_blankBindingsDescription;
			#endif
		}

		std::vector<PushConstantsBinding> pushConstantBinding;
		pushConstantBinding.resize(desc.GetPushConstants().size());
		for (unsigned c=0; c<desc.GetPushConstants().size(); ++c) {
			auto& srcBinding = desc.GetPushConstants()[c];
			pushConstantBinding[c]._name = srcBinding._name;
			pushConstantBinding[c]._cbSize = srcBinding._cbSize;
			pushConstantBinding[c]._stageFlags = Metal_Vulkan::Internal::AsVkShaderStageFlags(srcBinding._shaderStage);
			pushConstantBinding[c]._cbElements = MakeIteratorRange(srcBinding._cbElements);
		}

		return std::make_shared<Metal_Vulkan::CompiledPipelineLayout>(
			_globalsContainer->_objectFactory,
			MakeIteratorRange(descSetBindings),
			MakeIteratorRange(pushConstantBinding),
			desc, name);
	}

	std::shared_ptr<IDescriptorSet> Device::CreateDescriptorSet(PipelineType pipelineType, const DescriptorSetSignature& signature, StringSection<> name)
	{
		if (!_globalsContainer->_pools._descriptorSetLayoutCache)
			_globalsContainer->_pools._descriptorSetLayoutCache = Metal_Vulkan::Internal::CreateCompiledDescriptorSetLayoutCache();

		VkShaderStageFlags shaderStages = pipelineType == PipelineType::Graphics ? VK_SHADER_STAGE_ALL_GRAPHICS : VK_SHADER_STAGE_COMPUTE_BIT;
		auto descSetLayout = _globalsContainer->_pools._descriptorSetLayoutCache->CompileDescriptorSetLayout(signature, {}, shaderStages); // don't have the name available here
		return std::make_shared<Metal_Vulkan::CompiledDescriptorSet>(
			_globalsContainer->_objectFactory, _globalsContainer->_pools,
			descSetLayout->_layout,
			shaderStages, name);
	}

	std::shared_ptr<ISampler> Device::CreateSampler(const SamplerDesc& desc)
	{
		return std::make_shared<Metal_Vulkan::SamplerState>(_globalsContainer->_objectFactory, desc);
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

	static PresentationChainDesc AsPresentationChainDesc(const SwapChainProperties& props)
	{
		return {
			props._extent.width, props._extent.height, Metal_Vulkan::AsFormat(props._fmt),
			TextureSamples::Create(),
			props._bindFlags,
			!(props._presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR),
			props._desiredNumberOfImages
		};
	}

	static bool operator==(const PresentationChainDesc& lhs, const PresentationChainDesc& rhs)
	{
		return lhs._width == rhs._width && lhs._height == rhs._height && lhs._format == rhs._format
			&& lhs._samples == rhs._samples && lhs._bindFlags == rhs._bindFlags
			&& lhs._vsync == rhs._vsync && lhs._imageCount == rhs._imageCount;
	}

    void            PresentationChain::ChangeConfiguration(IThreadContext& mainThreadContext, const PresentationChainDesc& desc)
    {
		if (desc == _desc)
            return;

        auto props = DecideSwapChainProperties(_factory->GetPhysicalDevice(), _surface.get(), desc);

        // We can't delete the old swap chain while the device is using it. The easiest
        // way to get around this is to just synchronize with the GPU here.
        // Since a resize is uncommon, this should not be a issue. It might be better to wait for
        // a queue idle -- but we don't have access to the VkQueue from here.
		#if defined(_DEBUG)
			std::vector<std::weak_ptr<IResource>> resources;
			std::vector<Metal_Vulkan::VulkanWeakPtr<VkImage>> images;
			for (auto& i:_images) {
				resources.push_back(i);
				images.push_back(checked_cast<Metal_Vulkan::Resource*>(i.get())->ShareImage());
			}
		#endif
        _images.clear();
		#if defined(_DEBUG)
			bool allExpired = true;
			for (auto& i:resources) allExpired &= i.expired();
			for (auto& i:images) allExpired &= i.expired();
			if (!allExpired) {
				Log(Warning) << "Some presentation chain images still have active reference counts while resizing presentation chain." << std::endl;
				Log(Warning) << "Ensure that all references to presentation chain images (including views) are dropped before calling PresentationChain::Resize()" << std::endl;
				Log(Warning) << "This is required to ensure that the textures for the new presentation chain do not exist at the same time as the images for the old presentation chain (since they are quite large)" << std::endl;
				assert(0);
			}
		#endif

		mainThreadContext.CommitCommands(CommitCommandsFlags::WaitForCompletion);
		vkDeviceWaitIdle(_vulkanDevice.get());
		auto oldSwapChain = std::move(_swapChain);

		// we don't want the new and old images to exist at the same time, so pump the destruction queues to try to
		// ensure they are truly gone
		checked_cast<ThreadContext*>(&mainThreadContext)->PumpDestructionQueues();

        _swapChain = CreateUnderlyingSwapChain(_vulkanDevice.get(), _surface.get(), oldSwapChain.get(), props);
        _bufferDesc = TextureDesc::Plain2D(props._extent.width, props._extent.height, Metal_Vulkan::AsFormat(props._fmt));
        _desc = AsPresentationChainDesc(props);

        BuildImages();
    }

    PresentationChainDesc PresentationChain::GetDesc() const
    {
		return _desc;
    }

	std::shared_ptr<IDevice> PresentationChain::GetDevice() const
	{
		return _device.lock();
	}

    auto PresentationChain::AcquireNextImage(Metal_Vulkan::SubmissionQueue& queue, HierarchicalCPUProfiler* profiler) -> AquireResult
    {
        _activePresentSync = (_activePresentSync+1) % dimof(_presentSyncs);
        auto& sync = _presentSyncs[_activePresentSync];
		if (sync._presentFence.has_value()) {
			CPUProfileEvent_Conditional profEvnt("Stall/commandlist", profiler);
			queue.WaitForFence(sync._presentFence.value());
		}
		sync._presentFence = {};

		// Note that vkAcquireNextImageKHR can be guaranteed to be non-blocking if 
		// we have VK_PRESENT_MODE_MAILBOX_KHR, and surfCapabilities.minImageCount+1 images.
        //
        // As stated in the documentation, we shouldn't rely on this function for
        // synchronisation -- instead, we should write an algorithm that will insert 
        // stalls as necessary
        uint32_t nextImageIndex = ~0x0u;
        const auto timeout = UINT64_MAX;
		{
			CPUProfileEvent_Conditional profEvnt("Stall/image", profiler);
			auto res = vkAcquireNextImageKHR(
				_vulkanDevice.get(), _swapChain.get(), 
				timeout,
				sync._onAcquireComplete.get(), VK_NULL_HANDLE,
				&nextImageIndex);
			_activeImageIndex = nextImageIndex;

			// TODO: Deal with the VK_SUBOPTIMAL_KHR and VK_ERROR_OUT_OF_DATE_KHR
			// return codes
			if (res != VK_SUCCESS)
				Throw(VulkanAPIFailure(res, "Failure during acquire next image"));
		}

		AquireResult result;
		result._resource = _images[_activeImageIndex];
		result._primaryCommandBuffer = _primaryBuffers[_activePresentSync];
		return result;
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

	void PresentationChain::PresentToQueue(Metal_Vulkan::SubmissionQueue& queue, IteratorRange<const VkSemaphore*> commandBufferSyncs)
	{
		if (_activeImageIndex > unsigned(_images.size())) return;
		queue.Present(_swapChain.get(), _activeImageIndex, commandBufferSyncs);
		_activeImageIndex = ~0x0u;
	}

    void PresentationChain::BuildImages()
    {
        auto images = GetImages(_vulkanDevice.get(), _swapChain.get());
        _images.reserve(images.size());
        for (auto& vkImage:images) {
            auto resDesc = CreateDesc(_desc._bindFlags, AllocationRules::ResizeableRenderTarget, _bufferDesc);
            _images.emplace_back(std::make_shared<Metal_Vulkan::Resource>(vkImage, resDesc, "presentationimage"));
        }
    }

    PresentationChain::PresentationChain(
		std::shared_ptr<Device> device,
		Metal_Vulkan::ObjectFactory& factory,
        VulkanSharedPtr<VkSurfaceKHR> surface, 
		const PresentationChainDesc& requestDesc,
		Metal_Vulkan::SubmissionQueue* submissionQueue,
        const void* platformValue)
    : _surface(std::move(surface))
    , _vulkanDevice(factory.GetDevice())
    , _factory(&factory)
	, _submissionQueue(submissionQueue)
	, _primaryBufferPool(factory, submissionQueue->GetQueueFamilyIndex(), true, nullptr)
	, _device(std::move(device))
    {
        _activeImageIndex = ~0x0u;
        auto props = DecideSwapChainProperties(factory.GetPhysicalDevice(), _surface.get(), requestDesc);
        _swapChain = CreateUnderlyingSwapChain(_vulkanDevice.get(), _surface.get(), nullptr, props);

        _bufferDesc = TextureDesc::Plain2D(props._extent.width, props._extent.height, Metal_Vulkan::AsFormat(props._fmt));
		_desc = AsPresentationChainDesc(props);

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
		// for safety -- ensure that all submitted Present() events have finished on the GPU
		if (_submissionQueue)
			for (auto& sync:_presentSyncs)
				if (sync._presentFence.has_value())
					_submissionQueue->WaitForFence(sync._presentFence.value());
		_images.clear();
		_swapChain.reset();
		_device.reset();
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////

    render_dll_export std::shared_ptr<IAPIInstance>    CreateAPIInstance(const APIFeatures& features)
    {
        return std::make_shared<APIInstance>(features);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////

	void ThreadContext::AddPreFrameCommandList(Metal_Vulkan::CommandList&& cmdList)
	{
		_interimCmdLists.emplace_back(std::move(cmdList));
	}

	float ThreadContext::GetThreadingPressure()
	{
		return _submissionQueue->GetTracker()->GetThreadingPressure();
	}

	bool ThreadContext::IsDedicatedTransferContext()
	{
		return _submissionQueue->GetQueueFamilyIndex() == _factory->_dedicatedTransferQueueFamily;
	}

	std::shared_ptr<Metal_Vulkan::IAsyncTracker> ThreadContext::GetQueueTracker()
	{
		return _submissionQueue->GetTracker();
	}

	void ThreadContext::UpdateGPUTracking()
	{
		_submissionQueue->GetTracker()->UpdateConsumer();
	}

	void ThreadContext::AttachNameToCommandList(std::string name)
	{
		assert(_metalContext && _metalContext->HasActiveCommandList());
		if (!_metalContext || !_metalContext->HasActiveCommandList())
			return;
		_submissionQueue->GetTracker()->AttachName(
			_metalContext->GetActiveCommandList().GetPrimaryTrackerMarker(), std::move(name));
	}

	Metal_Vulkan::IAsyncTracker::Marker ThreadContext::CommitToQueue_Internal(
		IteratorRange<const std::pair<VkSemaphore, uint64_t>*> waitBeforeBegin,
		IteratorRange<const std::pair<VkSemaphore, uint64_t>*> completionSignals)
	{
		using TimelineSemaPair = std::pair<VkSemaphore, uint64_t>;
		VLA_UNSAFE_FORCE(TimelineSemaPair, waitSema, waitBeforeBegin.size()+2);
		VLA(VkPipelineStageFlags, waitStages, waitBeforeBegin.size()+2);
		unsigned waitCount = 0;

		for (auto c:waitBeforeBegin) {
			waitSema[waitCount] = c;
			waitStages[waitCount] = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			++waitCount;
		}

		if (_nextQueueShouldWaitOnAcquire != VK_NULL_HANDLE) {
			waitSema[waitCount] = {_nextQueueShouldWaitOnAcquire, 0};
			waitStages[waitCount] = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			++waitCount;
		}
		_nextQueueShouldWaitOnAcquire = VK_NULL_HANDLE;

		// _interimCmdLists always come before "cmdList"
		// _interimCmdLists will be cleared regardless of whether or not _submissionQueue->Submit throws
		auto interimLists = std::move(_interimCmdLists);
		auto result = interimLists.back().GetPrimaryTrackerMarker();
		VLA(Metal_Vulkan::CommandList*, cmdLists, interimLists.size());
		unsigned cmdListsCount = 0;
		for (auto& c:interimLists) cmdLists[cmdListsCount++] = &c;

		_submissionQueue->Submit(
			MakeIteratorRange(cmdLists, &cmdLists[cmdListsCount]),
			MakeIteratorRange(waitSema, &waitSema[waitCount]),
			MakeIteratorRange(waitStages, &waitStages[waitCount]),
			completionSignals);

		assert(result && result != ~0u);
		return result;
	}

	IResourcePtr    ThreadContext::BeginFrame(IPresentationChain& presentationChain)
	{
		// Our immediate context may have command list already, if it's been used
		// either before the first frame, or between 2 frames. Normally we switch
		// the immediate metal context over to using the "primary buffer" associated
		// with the swap chain.
		//
		// To avoid another call to VkSubmit (which is discouraged by the spec),
		// we can store the cmd list and submit it along with the primary command list
		if (_metalContext->HasActiveCommandList()) {
			auto cmdList = _metalContext->ResolveCommandList();
			_interimCmdLists.emplace_back(std::move(*cmdList));
		}

		PresentationChain* swapChain = checked_cast<PresentationChain*>(&presentationChain);
		auto nextImage = swapChain->AcquireNextImage(*_submissionQueue, _cpuProfiler);
		_nextQueueShouldWaitOnAcquire = swapChain->GetSyncs()._onAcquireComplete.get();

		{
			auto res = vkResetCommandBuffer(nextImage._primaryCommandBuffer.get(), VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
			if (res != VK_SUCCESS)
				Throw(VulkanAPIFailure(res, "Failure while resetting command buffer"));
			_metalContext->BeginCommandList(nextImage._primaryCommandBuffer, _submissionQueue->GetTracker());
		}

        return std::move(nextImage._resource);
	}

	void            ThreadContext::Present(IPresentationChain& chain)
	{
		auto* swapChain = checked_cast<PresentationChain*>(&chain);
		auto& syncs = swapChain->GetSyncs();
		assert(!syncs._presentFence);
		assert(_metalContext->HasActiveCommandList());

		//////////////////////////////////////////////////////////////////

		std::pair<VkSemaphore, uint64_t> commandBufferSignal = std::make_pair(syncs._onCommandBufferComplete.get(), 0);
		bool commandBufferSubmitted = false;
		TRY {
			auto immediateCommands = _metalContext->ResolveCommandList();
			_interimCmdLists.emplace_back(std::move(*immediateCommands));
			syncs._presentFence = CommitToQueue_Internal({}, MakeIteratorRange(&commandBufferSignal, &commandBufferSignal+1));
			commandBufferSubmitted = true;
		} CATCH(const std::exception& e) {
			Log(Warning) << "Failure during queue submission for present: " << e.what() << std::endl;
		} CATCH_END

		PumpDestructionQueues();

		//////////////////////////////////////////////////////////////////
		// Finally, we can queue the present
		//		-- do it here to allow it to run in parallel as much as possible
		if (commandBufferSubmitted) {
			swapChain->PresentToQueue(*_submissionQueue, MakeIteratorRange(&commandBufferSignal.first, &commandBufferSignal.first+1));
		} else {
			swapChain->PresentToQueue(*_submissionQueue, {});
		}
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
		if (_metalContext->HasActiveCommandList() || !_interimCmdLists.empty()) {
			Metal_Vulkan::IAsyncTracker::Marker fenceToWaitFor;
			TRY {
				if (_metalContext->HasActiveCommandList()) {
					auto immediateCommands = _metalContext->ResolveCommandList();
					_interimCmdLists.emplace_back(std::move(*immediateCommands));
				}
				fenceToWaitFor = CommitToQueue_Internal({}, {});
			} CATCH (const std::exception& e) {
				Log(Warning) << "Failure during queue submission in CommitCommands:" << e.what() << std::endl;
				waitForCompletion = false;
			} CATCH_END

			if (waitForCompletion)
				_submissionQueue->WaitForFence(fenceToWaitFor);
		} else {
			// note that if we don't have an active command list, and flags is WaitForCompletion, we still don't actually wait
			// for the GPU to catchup to any previously committed command lists
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
			if (_commandBufferPool)
				_commandBufferPool->FlushDestroys();

			// we have to flush the "idle" command buffer pools, also, otherwise they may never actually
			// release their resources
			{
				ScopedLock(_globalPools->_idleCommandBufferPoolsLock);
				for (auto&p:_globalPools->_idleCommandBufferPools)
					p.second->FlushDestroys();
			}
		} else {
			// If we're don't have the _destrQueue, we're not the "immediate" context.
			// In this case, we still want to flush destroys in our own command buffer pool, because it's
			// unique to this thread context
			if (_commandBufferPool)
				_commandBufferPool->FlushDestroys();
		}
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
			_annotator = CreateAnnotator(*d, *this);
		}
		return *_annotator;
	}

	void ThreadContext::AttachCPUProfiler(HierarchicalCPUProfiler* profiler)
	{
		_cpuProfiler = profiler;
	}

	void ThreadContext::AttachDestroyer(const std::shared_ptr<Metal_Vulkan::IDestructionQueue>& queue) { _destrQueue = queue; }

	void ThreadContext::ReleaseCommandBufferPool()
	{
		if (_globalPools && _commandBufferPool) {
			ScopedLock(_globalPools->_idleCommandBufferPoolsLock);
			_globalPools->_idleCommandBufferPools.emplace_back(_submissionQueue->GetQueueFamilyIndex(), std::move(_commandBufferPool));
		}
		// we have to destroy the metal context, as well, because it holds a pointer to the command buffer pool
		_metalContext = nullptr;
	}

    ThreadContext::ThreadContext(
		std::shared_ptr<Device> device,
		std::shared_ptr<Metal_Vulkan::SubmissionQueue> submissionQueue)
    : _device(std::move(device))
	, _frameId(0)
	, _factory(&device->GetObjectFactory())
	, _globalPools(&device->GetGlobalPools())
	, _submissionQueue(std::move(submissionQueue))
	, _underlyingDevice(device->GetUnderlyingDevice())
    {
		assert(_device.lock());
		// look for compatible pool from the _idleCommandBufferPools
		unsigned queueFamilyIndex = _submissionQueue->GetQueueFamilyIndex();
		{
			ScopedLock(_globalPools->_idleCommandBufferPoolsLock);
			for (auto i=_globalPools->_idleCommandBufferPools.begin(); i!=_globalPools->_idleCommandBufferPools.end(); ++i)
				if (i->first == queueFamilyIndex) {
					_commandBufferPool = i->second;
					_globalPools->_idleCommandBufferPools.erase(i);
					break;
				}
		}

		if (!_commandBufferPool)
			_commandBufferPool = std::make_shared<Metal_Vulkan::CommandBufferPool>(
				*_factory, queueFamilyIndex, false, _submissionQueue->GetTracker());

		_metalContext = std::make_shared<Metal_Vulkan::DeviceContext>(*_factory, *_globalPools);
	}

    ThreadContext::~ThreadContext() 
	{
		if (_globalPools && _commandBufferPool) {
			ScopedLock(_globalPools->_idleCommandBufferPoolsLock);
			_globalPools->_idleCommandBufferPools.emplace_back(_submissionQueue->GetQueueFamilyIndex(), std::move(_commandBufferPool));
		}
		_metalContext.reset();
		if (_annotator) ReleaseThreadContext(*_annotator);
		_annotator.reset();
		_commandBufferPool.reset();
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
		if (!_metalContext->HasActiveCommandList()) {
			auto cmdBuffer = _commandBufferPool->Allocate(Metal_Vulkan::CommandBufferType::Primary);
			_metalContext->BeginCommandList(std::move(cmdBuffer), _submissionQueue->GetTracker());
		}
        return _metalContext;
    }

	std::shared_ptr<Metal_Vulkan::DeviceContext> ThreadContext::BeginPrimaryCommandList()
    {
		auto cmdBuffer = _commandBufferPool->Allocate(Metal_Vulkan::CommandBufferType::Primary);
		auto deviceContext = std::make_shared<Metal_Vulkan::DeviceContext>(*_factory, *_globalPools);
		deviceContext->BeginCommandList(std::move(cmdBuffer), _submissionQueue->GetTracker());
        return deviceContext;
    }

	std::shared_ptr<Metal_Vulkan::DeviceContext> ThreadContext::BeginSecondaryCommandList()
    {
		auto cmdBuffer = _commandBufferPool->Allocate(Metal_Vulkan::CommandBufferType::Secondary);
		auto deviceContext = std::make_shared<Metal_Vulkan::DeviceContext>(*_factory, *_globalPools);
		deviceContext->BeginCommandList(std::move(cmdBuffer), _submissionQueue->GetTracker());
        return deviceContext;
    }
}}

namespace RenderCore
{
	IDeviceVulkan::~IDeviceVulkan() {}
	IThreadContextVulkan::~IThreadContextVulkan() {}
	IAPIInstanceVulkan::~IAPIInstanceVulkan() {}
}
