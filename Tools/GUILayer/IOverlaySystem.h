// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/IteratorUtils.h"
#include <memory>

namespace RenderCore { class IThreadContext; class IResource; class FrameBufferProperties; enum class Format; }
namespace RenderCore { namespace Techniques { class ParsingContext; struct PreregisteredAttachment; }}

namespace GUILayer 
{
	class RenderTargetWrapper;
    public ref class IOverlaySystem abstract
    {
    public:
        virtual void Render(
            RenderCore::Techniques::ParsingContext& parserContext) = 0; 
        virtual void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps,
            IteratorRange<const RenderCore::Format*> systemAttachmentFormats) = 0;
        virtual ~IOverlaySystem();
    };
}

