// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IOverlayContext.h"
#include "../RenderCore/StateDesc.h"
#include "../Utility/IteratorUtils.h"
#include <vector>
#include <memory>

#pragma warning(disable:4324)

namespace RenderCore { class VertexBufferView; class IThreadContext; }
namespace RenderCore { namespace Techniques { class IImmediateDrawables; class ImmediateDrawingApparatus; } }

namespace RenderOverlays
{
    class FontRenderingManager;

    class ImmediateOverlayContext : public IOverlayContext
    {
    public:
        void    DrawPoint      (ProjectionMode proj, const Float3& v,     const ColorB& col,      uint8_t size);
        void    DrawPoints     (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB& col,    uint8_t size);
        void    DrawPoints     (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB col[],   uint8_t size);

        void    DrawLine       (ProjectionMode proj, const Float3& v0,    const ColorB& colV0,    const Float3& v1,     const ColorB& colV1, float thickness);
        void    DrawLines      (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB& col,    float thickness);
        void    DrawLines      (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB col[],   float thickness);

        void    DrawTriangles  (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB& col);
        void    DrawTriangles  (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB col[]);

        void    DrawTriangle   (ProjectionMode proj, const Float3& v0,    const ColorB& colV0,    const Float3& v1,     
                                const ColorB& colV1, const Float3& v2,       const ColorB& colV2);

        void    DrawQuad       (ProjectionMode proj, 
                                const Float3& mins, const Float3& maxs, 
                                ColorB color0, ColorB color1,
                                const Float2& minTex0, const Float2& maxTex0, 
                                const Float2& minTex1, const Float2& maxTex1,
								StringSection<> shaderSelectorTable);

        void    DrawQuad(
            ProjectionMode proj, 
            const Float3& mins, const Float3& maxs, 
            ColorB color,
            StringSection<> shaderSelectorTable);

        void    DrawTexturedQuad(
            ProjectionMode proj, 
            const Float3& mins, const Float3& maxs, 
            const std::string& texture,
            ColorB color, const Float2& minTex0, const Float2& maxTex0);

        float   DrawText(
            const std::tuple<Float3, Float3>& quad, 
            const std::shared_ptr<Font>& font, const TextStyle& textStyle, 
            ColorB col, TextAlignment alignment, StringSection<char> text);

        RenderCore::Techniques::IImmediateDrawables& GetImmediateDrawables() { return *_immediateDrawables; }

        void CaptureState();
        void ReleaseState();
        void SetState(const OverlayState& state);

        ImmediateOverlayContext(
            RenderCore::IThreadContext& threadContext,
            RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
            FontRenderingManager* fontRenderingManager);
        ImmediateOverlayContext(
            RenderCore::IThreadContext& threadContext,
            RenderCore::Techniques::IImmediateDrawables& immediateDrawables);
        ~ImmediateOverlayContext();

        class ShaderBox;

    private:
        RenderCore::Techniques::IImmediateDrawables* _immediateDrawables;
        RenderCore::IThreadContext* _threadContext;
        FontRenderingManager* _fontRenderingManager;
        std::shared_ptr<Font> _defaultFont;
        OverlayState _currentState;

        class DrawCall;
        IteratorRange<void*>    BeginDrawCall(const DrawCall& drawCall);
    };

	std::unique_ptr<ImmediateOverlayContext>
		MakeImmediateOverlayContext(
            RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
            FontRenderingManager* fontRenderingManager = nullptr);

    std::unique_ptr<ImmediateOverlayContext>
		MakeImmediateOverlayContext(
            RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::ImmediateDrawingApparatus& apparatus);
}
