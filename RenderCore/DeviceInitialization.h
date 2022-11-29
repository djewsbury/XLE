// Copyright 2016 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IDevice.h"
#include <memory>

namespace RenderCore
{
    enum class UnderlyingAPI
    {
        DX11, Vulkan, OpenGLES, AppleMetal
    };

    std::shared_ptr<IAPIInstance> CreateAPIInstance(UnderlyingAPI api);

    class DeviceFeatures;
    class DeviceConfigurationProps;

    class IAPIInstance
    {
    public:
        virtual std::shared_ptr<IDevice>    CreateDevice(unsigned configurationIdx, const DeviceFeatures& features) = 0;

        virtual unsigned                    GetDeviceConfigurationCount() = 0;
        virtual DeviceConfigurationProps    GetDeviceConfigurationProps(unsigned configurationIdx) = 0;

        virtual DeviceFeatures              QueryFeatures(unsigned configurationIdx) = 0;
        // "platformWindowHandle" here is the same value passed to IDevice::CreatePresentationChain()
        // will return false if we expect IDevice::CreatePresentationChain() to fail with the given parameter
        virtual bool                        QueryPresentationChainCompatibility(
            unsigned configurationIdx,
            const void* platformWindowHandle) = 0;
        virtual FormatCapability            QueryFormatCapability(
            unsigned configurationIdx,
            Format format, BindFlag::BitField bindingType) = 0;

        virtual void*       QueryInterface(size_t guid) = 0;
        virtual ~IAPIInstance();
    };

    // "features" can be toggled on or off at device construction time, and
    // may not be supported by all physical devices / drivers / graphics APIs
    class DeviceFeatures
    {
    public:
        // ShaderStages supported
        bool _geometryShaders = false;

        // General rendering features
        bool _multiViewRenderPasses = false;
        bool _streamOutput = false;                 // "transform feedback" in GL/Vulkan parlance
        bool _depthBounds = false;
        bool _samplerAnisotrophy = false;
        bool _wideLines = false;

        // Resource types
        bool _cubemapArrays = false;

        // Query types
        bool _queryShaderInvocation = false;        // for QueryPool::Type::ShaderInvocations

        // Additional shader instructions
        bool _shaderImageGatherExtended = false;

        // texture compression types
        bool _textureCompressionETC2 = false;
        bool _textureCompressionATSC_LDR = false;
        bool _textureCompressionBC = false;

        // queues
        bool _separateTransferQueue = false;
        bool _separateComputeQueue = false;
    };

    enum class PhysicalDeviceType { Unknown, DiscreteGPU, IntegratedGPU, VirtualGPU, CPU };

    class DeviceConfigurationProps
    {
    public:
        char _driverName[256];
        uint64_t _driverVersion = 0;
        PhysicalDeviceType _physicalDeviceType = PhysicalDeviceType::Unknown;
    };

	using InstanceCreationFunction = std::shared_ptr<IAPIInstance>();
	void RegisterInstanceCreationFunction(
		UnderlyingAPI api,
		InstanceCreationFunction* fn);
}

