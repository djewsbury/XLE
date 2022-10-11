// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace RenderCore { class IThreadContext; class IResource; }
namespace RenderCore { namespace Techniques { class ParsingContext; }}

namespace GUILayer 
{
	class RenderTargetWrapper;
    public ref class IOverlaySystem abstract
    {
    public:
        virtual void Render(
            RenderCore::Techniques::ParsingContext& parserContext) = 0; 
        virtual ~IOverlaySystem();
    };
}

