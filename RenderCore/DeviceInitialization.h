// Copyright 2016 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IDevice_Forward.h"
#include <memory>

namespace RenderCore
{
    enum class UnderlyingAPI
    {
        DX11, Vulkan, OpenGLES, AppleMetal
    };

    std::shared_ptr<IAPIInstance> CreateAPIInstance(UnderlyingAPI api);

    class DeviceFeatures
    {
    public:
        bool _multiViewRenderPasses = false;
        bool _transformFeedback = false;
    };

    class IAPIInstance
    {
    public:
        virtual std::shared_ptr<IDevice>    CreateDevice(const DeviceFeatures& features = {}) = 0;
        virtual DeviceFeatures              QuerySupportedFeatures() = 0;

        virtual void*       QueryInterface(size_t guid) = 0;
        virtual ~IAPIInstance();
    };

	using InstanceCreationFunction = std::shared_ptr<IAPIInstance>();
	void RegisterInstanceCreationFunction(
		UnderlyingAPI api,
		InstanceCreationFunction* fn);
}

