// Copyright 2016 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../IDevice.h"
#include "../IThreadContext.h"
#include "../UniformsStream.h"
#include "Metal/VulkanForward.h"
#include <memory>

namespace Assets { class DependentFileState; }

namespace RenderCore
{
	namespace Metal_Vulkan { class DeviceContext; class GlobalPools; class PipelineLayout; }

	enum class VulkanShaderMode
	{
		GLSLToSPIRV,
		HLSLToSPIRV,
		HLSLCrossCompiled
	};
	struct VulkanCompilerConfiguration
	{
		VulkanShaderMode _shaderMode = VulkanShaderMode::GLSLToSPIRV;
		LegacyRegisterBindingDesc _legacyBindings = {};
		std::vector<PipelineLayoutInitializer::PushConstantsBinding> _pushConstants = {};
		std::vector<::Assets::DependentFileState> _additionalDependencies = {};		// (if the legacy bindings, etc, are loaded from a file, you can register extra dependencies with this)
	};

	////////////////////////////////////////////////////////////////////////////////

	/// <summary>IDevice extension for DX11</summary>
	/// Use IDevice::QueryInterface to query for this type from a
	/// plain IDevice.
	class IDeviceVulkan
	{
	public:
		virtual VkInstance	GetVulkanInstance() = 0;
		virtual VkDevice	GetUnderlyingDevice() = 0;
		virtual VkQueue     GetRenderingQueue() = 0;
		virtual Metal_Vulkan::GlobalPools& GetGlobalPools() = 0;
		virtual std::shared_ptr<ILowLevelCompiler> CreateShaderCompiler(
			const VulkanCompilerConfiguration&) = 0;
		~IDeviceVulkan();
	};

	////////////////////////////////////////////////////////////////////////////////

	/// <summary>IThreadContext extension for DX11</summary>
	class IThreadContextVulkan
	{
	public:
		virtual const std::shared_ptr<Metal_Vulkan::DeviceContext>& GetMetalContext() = 0;
		~IThreadContextVulkan();
	};

}
