// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Math/Vector.h"

namespace RenderCore { namespace Techniques { class ParsingContext; class AttachmentPool; class FrameBufferPool; } }
namespace RenderCore { class FrameBufferDesc; class FrameBufferProperties; class IThreadContext; class ICompiledPipelineLayout; }

namespace RenderOverlays
{
    class HighlightByStencilSettings
    {
    public:
        Float3 _outlineColor;
        unsigned _highlightedMarker;
        unsigned _backgroundMarker;

        HighlightByStencilSettings();
    };

    void ExecuteHighlightByStencil(
        RenderCore::Techniques::ParsingContext& parsingContext,
        const HighlightByStencilSettings& settings,
        bool onlyHighlighted);

    /// <summary>Utility class for rendering a highlight around some geometry</summary>
    /// Using BinaryHighlight, we can draw some geometry to an offscreen
    /// buffer, and then blend a outline or highlight over other geometry.
    /// Generally, it's used like this:
    /// <list>
    ///   <item>BinaryHighlight::BinaryHighlight() (constructor)
    ///   <item>Draw something... 
    ///         (BinaryHighlight constructor binds an offscreen buffer, so this render 
    ///         is just to provide the siholette of the thing we want to highlight
    ///   <item>BinaryHighlight::FinishWithOutline()
    ///         This rebinds the old render target, and blends in the highlight
    /// </list>
    class BinaryHighlight
    {
    public:
        void FinishWithOutline(Float3 outlineColor);
        void FinishWithOutlineAndOverlay(Float3 outlineColor, unsigned overlayColor);
        void FinishWithShadow(Float4 shadowColor);

		const RenderCore::FrameBufferDesc& GetFrameBufferDesc() const;
        
        BinaryHighlight(
            RenderCore::Techniques::ParsingContext& parsingContext);
        ~BinaryHighlight();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };
}

