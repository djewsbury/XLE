// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/Prefix.h"
#include <memory>

namespace RenderCore
{
    class IPresentationChain;
    class IDevice;
    class IResource;
    class IThreadContext;
    class IAnnotator;
    class ICompiledPipelineLayout;
    class IResourceView;
    class ISampler;
    class IDescriptorSet;

    using IResourcePtr = std::shared_ptr<IResource>;
    class ResourceDesc;
    class PresentationChainDesc;
    class SubResourceInitData;
    class PresentationChainDesc;
    class SubResourceId;
    class PipelineLayoutInitializer;
}

