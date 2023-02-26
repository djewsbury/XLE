// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "OverlayPrimitives.h"
#include <future>

namespace RenderCore { namespace Techniques { class ITechniqueDelegate; class IPipelineLayoutDelegate; }}

namespace RenderOverlays
{
	class IOverlayContext;

	///////////////////////////////////////////////////////////////////////////////////
    //          2 D   S H A P E   R E N D E R I N G

    void        OutlineEllipse(IOverlayContext& context, const Rect& rect, ColorB colour);
    void        FillEllipse(IOverlayContext& context, const Rect& rect, ColorB colour);

    namespace Corner
    {
        static const auto TopLeft = 1u;
        static const auto TopRight = 2u;
        static const auto BottomLeft = 4u;
        static const auto BottomRight = 8u;
        using BitField = unsigned;
    }

    void FillRoundedRectangle(
        IOverlayContext& context, const Rect& rect, 
        ColorB fillColor,
        float roundedProportion = 1.f / 8.f,
        Corner::BitField cornerFlags = 0xf);
    void FillAndOutlineRoundedRectangle(
        IOverlayContext& context, const Rect& rect, 
        ColorB fillColor, ColorB outlineColour,
        float outlineWidth = 1.f, float roundedProportion = 1.f / 8.f,
        Corner::BitField cornerFlags = 0xf);
    void OutlineRoundedRectangle(
        IOverlayContext& context, const Rect& rect, 
        ColorB colour, 
        float outlineWidth = 1.f, float roundedProportion = 1.f / 8.f,
        Corner::BitField cornerFlags = 0xf);

    void FillRaisedRectangle(
        IOverlayContext& context, const Rect& rect,
        ColorB fillColor);
    void FillRaisedRoundedRectangle(
        IOverlayContext& context, const Rect& rect,
        ColorB fillColor,
        float roundedProportion = 1.f / 8.f,
        Corner::BitField cornerFlags = 0xf);
    void FillDepressedRoundedRectangle(
        IOverlayContext& context, const Rect& rect,
        ColorB fillColor,
        float roundedProportion = 1.f / 8.f,
        Corner::BitField cornerFlags = 0xf);

    void        FillRectangle(IOverlayContext& context, const Rect& rect, ColorB colour);
    void        OutlineRectangle(IOverlayContext& context, const Rect& rect, ColorB outlineColour, float outlineWidth = 1.f);
    void        FillAndOutlineRectangle(IOverlayContext& context, const Rect& rect, ColorB fillColour, ColorB outlineColour, float outlineWidth = 1.f);

    void        SoftShadowRectangle(IOverlayContext& context, const Rect& rect);

    void        DashLine(IOverlayContext& context, IteratorRange<const Float2*> linePts, ColorB colour, float width);


	///////////////////////////////////////////////////////////////////////////////////
    //          C O N F I G U R A T I O N   U T I L S

	class ShapesRenderingDelegate
	{
	public:
		const std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate>& GetTechniqueDelegate();
		const std::shared_ptr<RenderCore::Techniques::IPipelineLayoutDelegate>& GetPipelineLayoutDelegate();

		ShapesRenderingDelegate();
		~ShapesRenderingDelegate();
	private:
		std::shared_ptr<RenderCore::Techniques::IPipelineLayoutDelegate> _pipelineLayoutDelegate;
		std::shared_future<std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate>> _futureTechniqueDelegate;
	};
}
