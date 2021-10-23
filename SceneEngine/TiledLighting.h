// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace RenderCore { class IResourceView; class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; class PipelineCollection; } }

namespace SceneEngine
{
    std::shared_ptr<RenderCore::IResourceView> TiledLighting_CalculateLighting(
        RenderCore::IThreadContext& context, 
        RenderCore::Techniques::ParsingContext& parsingContext,
        const std::shared_ptr<RenderCore::Techniques::PipelineCollection>& pool,
        RenderCore::IResourceView& depthsSRV, 
        RenderCore::IResourceView& normalsSRV,
		RenderCore::IResourceView& metricBufferUAV);

    void TiledLighting_RenderBeamsDebugging(  
        RenderCore::IThreadContext& context, 
        RenderCore::Techniques::ParsingContext& parsingContext,
        const std::shared_ptr<RenderCore::Techniques::PipelineCollection>& pool,
        bool active, unsigned mainViewportWidth, unsigned mainViewportHeight, 
        unsigned techniqueIndex);
}

