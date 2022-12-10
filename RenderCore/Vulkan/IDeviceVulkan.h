// Copyright 2016 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../IDevice.h"
#include "../UniformsStream.h"
#include "../../Assets/IFileSystem.h"		// for DependentFileState
#include "Metal/VulkanForward.h"
#include <memory>

namespace Assets { struct DependentFileState; }

namespace RenderCore
{
	namespace Metal_Vulkan { class DeviceContext; class CommandList; class IAsyncTracker; }
	struct VulkanCompilerConfiguration;

	////////////////////////////////////////////////////////////////////////////////

	class IDeviceVulkan
	{
	public:
		virtual VkInstance	GetVulkanInstance() = 0;
		virtual VkDevice	GetUnderlyingDevice() = 0;

		virtual std::shared_ptr<ILowLevelCompiler> CreateShaderCompiler(
			const VulkanCompilerConfiguration&) = 0;

		virtual std::shared_ptr<Metal_Vulkan::IAsyncTracker> GetGraphicsQueueAsyncTracker() = 0;
		virtual std::shared_ptr<Metal_Vulkan::IAsyncTracker> GetDedicatedTransferAsyncTracker() = 0;

		virtual std::unique_ptr<IThreadContext> CreateDedicatedTransferContext() = 0;

		enum InternalMetricsType { MainDescriptorPoolMetrics, LongTermDescriptorPoolMetrics };
		virtual void GetInternalMetrics(InternalMetricsType type, IteratorRange<void*> dst) const = 0;

		virtual ~IDeviceVulkan();
	};

	////////////////////////////////////////////////////////////////////////////////

	class IThreadContextVulkan
	{
	public:
		virtual const std::shared_ptr<Metal_Vulkan::DeviceContext>& GetMetalContext() = 0;
		virtual std::shared_ptr<Metal_Vulkan::DeviceContext> BeginPrimaryCommandList() = 0;
		virtual std::shared_ptr<Metal_Vulkan::DeviceContext> BeginSecondaryCommandList() = 0;

		virtual void AddPreFrameCommandList(Metal_Vulkan::CommandList&& cmdList) = 0;

		using CommandListMarker = uint64_t;
		virtual std::pair<VkSemaphore, CommandListMarker> GetCommandListSpecificMarker() = 0;
		virtual std::shared_ptr<Metal_Vulkan::IAsyncTracker> GetQueueTracker() = 0;

		virtual void AttachNameToCommandList(std::string name) = 0;
		virtual void ReleaseCommandBufferPool() = 0;

		virtual float GetThreadingPressure() = 0;
		virtual bool IsDedicatedTransferContext() = 0;

		virtual ~IThreadContextVulkan();
	};

	////////////////////////////////////////////////////////////////////////////////

	class IAPIInstanceVulkan
	{
	public:
		virtual std::shared_ptr<IDevice> CreateDevice(VkPhysicalDevice, unsigned renderingQueueFamily, const DeviceFeatures&) = 0;

		virtual VkInstance GetVulkanInstance() = 0;
		virtual VkPhysicalDevice GetPhysicalDevice(unsigned configurationIdx) = 0;
		
		virtual std::string LogPhysicalDevice(unsigned configurationIdx) = 0;
		virtual std::string LogInstance(const void* presentationChainPlatformValue = nullptr) = 0;
		virtual ~IAPIInstanceVulkan();
	};

	////////////////////////////////////////////////////////////////////////////////

	enum class VulkanShaderMode
	{
		GLSLToSPIRV,
		HLSLToSPIRV,
		HLSLCrossCompiled
	};

	struct VulkanCompilerConfiguration
	{
		VulkanShaderMode _shaderMode = VulkanShaderMode::HLSLToSPIRV;
		LegacyRegisterBindingDesc _legacyBindings = {};
		std::vector<PipelineLayoutInitializer::PushConstantsBinding> _pushConstants = {};
		std::vector<::Assets::DependentFileState> _additionalDependencies = {};		// (if the legacy bindings, etc, are loaded from a file, you can register extra dependencies with this)
	};

}
