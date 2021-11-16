// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ILightScene.h"
#include "Assets/Marker.h"
#include <memory>

namespace RenderCore { namespace Techniques
{
    class FrameBufferPool;
    class AttachmentPool;
    class IShaderResourceDelegate;
}}

namespace RenderCore { namespace LightingEngine
{
    class IPreparedShadowResult;
    class LightingTechniqueIterator;
}}

namespace RenderCore { namespace LightingEngine { namespace Internal
{
    class ILightBase;
    std::shared_ptr<IPreparedShadowResult> SetupShadowPrepare(
		LightingTechniqueIterator& iterator,
		ILightBase& proj,
        ILightScene& lightScene, ILightScene::LightSourceId associatedLightId,
		Techniques::FrameBufferPool& shadowGenFrameBufferPool,
		Techniques::AttachmentPool& shadowGenAttachmentPool);

    ::Assets::MarkerPtr<Techniques::IShaderResourceDelegate> CreateBuildGBufferResourceDelegate();
}}}

