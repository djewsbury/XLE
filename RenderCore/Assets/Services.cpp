// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Services.h"
#include "MaterialCompiler.h"
#include "PipelineConfigurationUtils.h"
#include "../MinimalShaderSource.h"
#include "../IDevice.h"
#include "../Vulkan/IDeviceVulkan.h"
#include "../ShaderService.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/SerializationUtils.h"

namespace RenderCore { namespace Assets
{
    Services* Services::s_instance = nullptr;

    Services::Services(const std::shared_ptr<RenderCore::IDevice>& device)
    : _device(device)
    {
    }

    Services::~Services()
    {
    }

    void Services::AttachCurrentModule()
    {
        assert(s_instance==nullptr);
        s_instance = this;
    }

    void Services::DetachCurrentModule()
    {
        assert(s_instance==this);
        s_instance = nullptr;
    }

}}

