// Copyright 2016 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../IDevice.h"
#include "../UniformsStream.h"
#include "Metal/VulkanForward.h"
#include <memory>

namespace Assets { class DependentFileState; }

namespace RenderCore
{
	namespace Metal_Vulkan { class DeviceContext; class GlobalPools; class PipelineLayout; class CommandList; class IAsyncTracker; }

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

	////////////////////////////////////////////////////////////////////////////////

	class IDeviceVulkan
	{
	public:
		virtual VkInstance	GetVulkanInstance() = 0;
		virtual VkDevice	GetUnderlyingDevice() = 0;
		virtual Metal_Vulkan::GlobalPools& GetGlobalPools() = 0;
		virtual std::shared_ptr<ILowLevelCompiler> CreateShaderCompiler(
			const VulkanCompilerConfiguration&) = 0;
		virtual std::shared_ptr<Metal_Vulkan::IAsyncTracker> GetAsyncTracker() = 0;
		~IDeviceVulkan();
	};

	////////////////////////////////////////////////////////////////////////////////

	class IThreadContextVulkan
	{
	public:
		virtual const std::shared_ptr<Metal_Vulkan::DeviceContext>& GetMetalContext() = 0;
		virtual void CommitPrimaryCommandBufferToQueue(Metal_Vulkan::CommandList& cmdList) = 0;
		virtual float GetThreadingPressure() = 0;
		virtual unsigned GetCmdListSpecificMarker() = 0;		// Metal_Vulkan::IAsyncTracker::Marker
		virtual void AttachNameToCmdList(std::string name) = 0;
		~IThreadContextVulkan();
	};

}
