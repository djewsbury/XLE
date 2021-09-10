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

namespace RenderCore { class IThreadContext; class UniformsStreamInterface; }
namespace RenderCore { namespace Techniques { class IImmediateDrawables; class ImmediateDrawingApparatus; } }

namespace RenderOverlays
{
    class FontRenderingManager;

    class ImmediateOverlayContext : public IOverlayContext
    {
    public:
        void    DrawPoint      (ProjectionMode proj, const Float3& v,     const ColorB& col,      uint8_t size) override;
        void    DrawPoints     (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB& col,    uint8_t size) override;
        void    DrawPoints     (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB col[],   uint8_t size) override;

        void    DrawLine       (ProjectionMode proj, const Float3& v0,    const ColorB& colV0,    const Float3& v1,     const ColorB& colV1, float thickness) override;
        void    DrawLines      (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB& col,    float thickness) override;
        void    DrawLines      (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB col[],   float thickness) override;

        void    DrawTriangles  (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB& col) override;
        void    DrawTriangles  (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB col[]) override;

        void    DrawTriangle   (ProjectionMode proj, const Float3& v0,    const ColorB& colV0,    const Float3& v1,     
                                const ColorB& colV1, const Float3& v2,       const ColorB& colV2) override;

        void    DrawQuad       (ProjectionMode proj, 
                                const Float3& mins, const Float3& maxs, 
                                ColorB color0, ColorB color1,
                                const Float2& minTex0, const Float2& maxTex0, 
                                const Float2& minTex1, const Float2& maxTex1,
								StringSection<> shaderSelectorTable) override;

        void    DrawQuad(
            ProjectionMode proj, 
            const Float3& mins, const Float3& maxs, 
            ColorB color0, ColorB color1,
            const Float2& minTex0, const Float2& maxTex0, 
            const Float2& minTex1, const Float2& maxTex1,
            std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> shaderPatches) override;

        void    DrawQuad(
            ProjectionMode proj, 
            const Float3& mins, const Float3& maxs, 
            ColorB color,
            StringSection<> shaderSelectorTable) override;

        void    DrawTexturedQuad(
            ProjectionMode proj, 
            const Float3& mins, const Float3& maxs, 
            std::shared_ptr<RenderCore::IResourceView> textureResource,
            ColorB color, const Float2& minTex0, const Float2& maxTex0) override;

        float   DrawText(
            const std::tuple<Float3, Float3>& quad, 
            const std::shared_ptr<Font>& font, const TextStyle& textStyle, 
            ColorB col, TextAlignment alignment, StringSection<char> text) override;

        RenderCore::Techniques::IImmediateDrawables& GetImmediateDrawables() override { return *_immediateDrawables; }
        BufferUploads::CommandListID GetRequiredBufferUploadsCommandList() const override;
        void RequireCommandList(BufferUploads::CommandListID) override;

        void CaptureState() override;
        void ReleaseState() override;
        void SetState(const OverlayState& state) override;

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
        std::shared_ptr<RenderCore::UniformsStreamInterface> _texturedUSI;
        BufferUploads::CommandListID _requiredBufferUploadsCommandList = 0;

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
