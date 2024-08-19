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
namespace RenderCore { namespace Techniques { class IImmediateDrawables; } }
namespace RenderOverlays { class OverlayApparatus; }

namespace RenderOverlays
{
    class FontRenderingManager;

    class ImmediateOverlayContext : public IOverlayContext
    {
    public:
        void    DrawPoint      (ProjectionMode proj, const Float3& v,     const ColorB& col,      uint8_t size) override;
        void    DrawPoints     (ProjectionMode proj, const Float3 v[],    uint32_t numPoints,     const ColorB& col,    uint8_t size) override;
        void    DrawPoints     (ProjectionMode proj, const Float3 v[],    uint32_t numPoints,     const ColorB col[],   uint8_t size) override;

        void    DrawLine       (ProjectionMode proj, const Float3& v0,    const ColorB& colV0,    const Float3& v1,     const ColorB& colV1, float thickness) override;
        void    DrawLines      (ProjectionMode proj, const Float3 v[],    uint32_t numPoints,     const ColorB& col,    float thickness) override;
        void    DrawLines      (ProjectionMode proj, const Float3 v[],    uint32_t numPoints,     const ColorB col[],   float thickness) override;

        void    DrawTriangles  (ProjectionMode proj, const Float3 v[],    uint32_t numPoints,     const ColorB& col) override;
        void    DrawTriangles  (ProjectionMode proj, const Float3 v[],    uint32_t numPoints,     const ColorB col[]) override;

        void    DrawTriangle   (ProjectionMode proj, const Float3& v0,    const ColorB& colV0,    const Float3& v1,     
                                const ColorB& colV1, const Float3& v2,    const ColorB& colV2) override;

        IteratorRange<void*> DrawGeometry(
            unsigned vertexCount,
            IteratorRange<const RenderCore::MiniInputElementDesc*> inputLayout,
            const RenderCore::Techniques::ImmediateDrawableMaterial& material,
            RenderCore::Techniques::RetainedUniformsStream&& uniforms,
            RenderCore::Topology) override;

        void    DrawTexturedQuad(
            ProjectionMode proj, 
            const Float3& mins, const Float3& maxs, 
            std::shared_ptr<RenderCore::IResourceView> textureResource,
            ColorB color, const Float2& minTex0, const Float2& maxTex0) override;

        void CaptureState() override;
        void ReleaseState() override;
        void SetState(const OverlayState& state) override;
        void SetEncoderState(const RenderCore::Techniques::EncoderState&) override;

        void* GetService(uint64_t) override;
        void AttachService(uint64_t, void*) override;

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
        OverlayState _currentState;
        std::shared_ptr<RenderCore::UniformsStreamInterface> _texturedUSI;
        std::vector<std::pair<uint64_t, void*>> _services;

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
			RenderOverlays::OverlayApparatus& apparatus);
}
