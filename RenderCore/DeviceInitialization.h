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

    class APIFeatures;
    class DeviceFeatures;
    class DeviceConfigurationProps;
    const APIFeatures& DefaultAPIFeatures();
    const DeviceFeatures& DefaultDeviceFeatures();

    std::shared_ptr<IAPIInstance> CreateAPIInstance(UnderlyingAPI api, const APIFeatures& features = DefaultAPIFeatures());

    class IAPIInstance
    {
    public:
        virtual std::shared_ptr<IDevice>    CreateDevice(unsigned configurationIdx = 0, const DeviceFeatures& features = DefaultDeviceFeatures()) = 0;

        virtual unsigned                    GetDeviceConfigurationCount() = 0;
        virtual DeviceConfigurationProps    GetDeviceConfigurationProps(unsigned configurationIdx) = 0;

        virtual DeviceFeatures              QueryFeatureCapability(unsigned configurationIdx) = 0;
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

    class APIFeatures
    {
    public:
        bool _debugValidation = false;
    };

    // "features" can be toggled on or off at device construction time, and
    // may not be supported by all physical devices / drivers / graphics APIs
    class DeviceFeatures
    {
    public:
        // ShaderStages supported
        bool _geometryShaders = false;

        // General rendering features
        bool _viewInstancingRenderPasses = false;
        bool _streamOutput = false;                 // "transform feedback" in GL/Vulkan parlance
        bool _depthBounds = false;
        bool _samplerAnisotrophy = false;
        bool _wideLines = false;
        bool _conservativeRaster = false;
        bool _independentBlend = false;
        bool _multiViewport = false;
        bool _separateDepthStencilLayouts = false;

        // Resource types
        bool _cubemapArrays = false;

        // Query & scheduling types
        bool _queryShaderInvocation = false;        // for QueryPool::Type::ShaderInvocations
        bool _queryStreamOutput = false;            // for QueryPool::Type::StreamOutput_Stream0 (etc)
        bool _timelineSemaphore = false;

        // Additional shader instructions
        bool _shaderImageGatherExtended = false;
        bool _pixelShaderStoresAndAtomics = false;
        bool _vertexGeoTessellationShaderStoresAndAtomics = false;

        // texture compression types
        bool _textureCompressionETC2 = false;
        bool _textureCompressionASTC_LDR = false;
        bool _textureCompressionASTC_HDR = false;
        bool _textureCompressionBC = false;

        // queues
        bool _dedicatedTransferQueue = false;
        bool _dedicatedComputeQueue = false;
    };

    enum class PhysicalDeviceType { Unknown, DiscreteGPU, IntegratedGPU, VirtualGPU, CPU };

    class DeviceConfigurationProps
    {
    public:
        char _driverName[256];
        uint64_t _driverVersion = 0;
        uint32_t _vendorId = 0;
        uint32_t _deviceId = 0;
        PhysicalDeviceType _physicalDeviceType = PhysicalDeviceType::Unknown;
    };

	using InstanceCreationFunction = std::shared_ptr<IAPIInstance>(const APIFeatures&);
	void RegisterInstanceCreationFunction(
		UnderlyingAPI api,
		InstanceCreationFunction* fn);
}

