// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace RenderCore { namespace Techniques
{
    class FrameBufferPool;
    class AttachmentPool;
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
		Techniques::FrameBufferPool& shadowGenFrameBufferPool,
		Techniques::AttachmentPool& shadowGenAttachmentPool);
}}}

