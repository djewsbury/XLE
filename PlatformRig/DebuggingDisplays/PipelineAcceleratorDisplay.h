// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace RenderOverlays { namespace DebuggingDisplay { class IWidget; }}
namespace RenderCore { namespace Techniques { class IPipelineAcceleratorPool; }}

namespace PlatformRig { namespace Overlays
{
    std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreatePipelineAcceleratorPoolDisplay(
        std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators);
}}
