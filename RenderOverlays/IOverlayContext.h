// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "OverlayPrimitives.h"
#include "../Math/Vector.h"
#include "../Math/Matrix.h"
#include "../Utility/StringUtils.h"

namespace RenderCore { class IResourceView; class MiniInputElementDesc; }
namespace RenderCore { namespace Techniques { class IImmediateDrawables; class ImmediateDrawableMaterial; } }
namespace RenderCore { namespace Assets { class ShaderPatchCollection; } }
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}

namespace RenderOverlays
{
	class Font;

////////////////////////////////////////////////////////////////////////////////

    class OverlayState
    {
    public:

            //
            //      Simplified render state settings for
            //      rendering basic debugging things
            //

        struct DepthMode    { enum Enum { Ignore, Read, ReadAndWrite }; };
        DepthMode::Enum     _depthMode;
        OverlayState(DepthMode::Enum depthMode) : _depthMode(depthMode) {}
        OverlayState() : _depthMode(DepthMode::Ignore) {}

    };

////////////////////////////////////////////////////////////////////////////////
    
    enum class ProjectionMode { P2D, P3D }; 

////////////////////////////////////////////////////////////////////////////////

        //
        //      IOverlayContext
        //
        //          Common utilities for rendering overlay graphics.
        //          This is mostly required for debugging tools. It should
        //          generally not be used in the shipping product.
        //
        //          
        //
    
    class IOverlayContext
    {
    public:
        virtual void    DrawPoint       (ProjectionMode proj, const Float3& v,       const ColorB& col,      uint8_t size = 1) = 0;
        virtual void    DrawPoints      (ProjectionMode proj, const Float3 v[],      uint32 numPoints,       const ColorB& col,    uint8_t size = 1) = 0;
        virtual void    DrawPoints      (ProjectionMode proj, const Float3 v[],      uint32 numPoints,       const ColorB col[],   uint8_t size = 1) = 0;

        virtual void    DrawLine        (ProjectionMode proj, const Float3& v0,      const ColorB& colV0,    const Float3& v1,     const ColorB& colV1,        float thickness = 1.0f) = 0;
        virtual void    DrawLines       (ProjectionMode proj, const Float3 v[],      uint32 numPoints,       const ColorB& col,    float thickness = 1.0f) = 0;
        virtual void    DrawLines       (ProjectionMode proj, const Float3 v[],      uint32 numPoints,       const ColorB col[],   float thickness = 1.0f) = 0;

        virtual void    DrawTriangles   (ProjectionMode proj, const Float3 v[],      uint32 numPoints,       const ColorB& col)    = 0;
        virtual void    DrawTriangles   (ProjectionMode proj, const Float3 v[],      uint32 numPoints,       const ColorB col[])   = 0;
        virtual void    DrawTriangle(
            ProjectionMode proj, 
            const Float3& v0,      const ColorB& colV0,    const Float3& v1,     
            const ColorB& colV1,   const Float3& v2,       const ColorB& colV2) = 0;

        virtual IteratorRange<void*> DrawGeometry(
            unsigned vertexCount,
            IteratorRange<const RenderCore::MiniInputElementDesc*> inputLayout,
            RenderCore::Techniques::ImmediateDrawableMaterial&& material) = 0;

        virtual void    DrawTexturedQuad(
            ProjectionMode proj, 
            const Float3& mins, const Float3& maxs, 
            std::shared_ptr<RenderCore::IResourceView> textureResource,
            ColorB color = ColorB(0xffffffff),
            const Float2& minTex0 = Float2(0.f, 0.f), const Float2& maxTex0 = Float2(1.0f, 1.f)) = 0;

        virtual Float2   DrawText(
            const std::tuple<Float3, Float3>& quad,
            const Font& font, DrawTextFlags::BitField, 
            ColorB col, TextAlignment alignment, StringSection<char> text) = 0;

        using FontPtrAndFlags = std::pair<Font*, DrawTextFlags::BitField>;
        virtual Float2  DrawTextWithTable(
            const std::tuple<Float3, Float3>& quad,
            FontPtrAndFlags fontTable[256],
            TextAlignment alignment,
            StringSection<> text,
            IteratorRange<const uint32_t*> colors = {},
            IteratorRange<const uint8_t*> fontSelectors = {},
            ColorB shadowColor = ColorB::Black) = 0;

        virtual void    CaptureState    () = 0;
        virtual void    ReleaseState    () = 0;
        virtual void    SetState        (const OverlayState& state) = 0;

        virtual RenderCore::Techniques::IImmediateDrawables& GetImmediateDrawables() = 0;
        virtual RenderCore::BufferUploads::CommandListID GetRequiredBufferUploadsCommandList() const = 0;
        virtual void RequireCommandList(RenderCore::BufferUploads::CommandListID) = 0;

        virtual void* GetService(uint64_t) = 0;
        virtual void AttachService(uint64_t, void*) = 0;

        template<typename Type>
            Type* GetService() { return (Type*)GetService(typeid(Type).hash_code()); }
        template<typename Type>
            void AttachService2(Type& type) { AttachService(typeid(Type).hash_code(), &type); }

        virtual ~IOverlayContext();
    };
}
