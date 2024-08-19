// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "OverlayPrimitives.h"
#include "../RenderCore/StateDesc.h"        // for Topology
#include "../Math/Vector.h"
#include "../Utility/IteratorUtils.h"

namespace RenderCore { class IResourceView; class MiniInputElementDesc; class IThreadContext; }
namespace RenderCore { namespace Techniques { class IImmediateDrawables; class ImmediateDrawableMaterial; class RetainedUniformsStream; class EncoderState; } }
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}

namespace RenderOverlays
{
	class Font;
    class FontRenderingManager;

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
        virtual void    DrawPoints      (ProjectionMode proj, const Float3 v[],      uint32_t numPoints,     const ColorB& col,    uint8_t size = 1) = 0;
        virtual void    DrawPoints      (ProjectionMode proj, const Float3 v[],      uint32_t numPoints,     const ColorB col[],   uint8_t size = 1) = 0;

        virtual void    DrawLine        (ProjectionMode proj, const Float3& v0,      const ColorB& colV0,    const Float3& v1,     const ColorB& colV1,        float thickness = 1.0f) = 0;
        virtual void    DrawLines       (ProjectionMode proj, const Float3 v[],      uint32_t numPoints,     const ColorB& col,    float thickness = 1.0f) = 0;
        virtual void    DrawLines       (ProjectionMode proj, const Float3 v[],      uint32_t numPoints,     const ColorB col[],   float thickness = 1.0f) = 0;

        virtual void    DrawTriangles   (ProjectionMode proj, const Float3 v[],      uint32_t numPoints,     const ColorB& col)    = 0;
        virtual void    DrawTriangles   (ProjectionMode proj, const Float3 v[],      uint32_t numPoints,     const ColorB col[])   = 0;
        virtual void    DrawTriangle(
            ProjectionMode proj,
            const Float3& v0,      const ColorB& colV0,    const Float3& v1,
            const ColorB& colV1,   const Float3& v2,       const ColorB& colV2) = 0;

        virtual IteratorRange<void*> DrawGeometry(
            unsigned vertexCount,
            IteratorRange<const RenderCore::MiniInputElementDesc*> inputLayout,
            const RenderCore::Techniques::ImmediateDrawableMaterial& material,
            RenderCore::Techniques::RetainedUniformsStream&& uniforms,
            RenderCore::Topology topology = RenderCore::Topology::TriangleList) = 0;

        virtual void    DrawTexturedQuad(
            ProjectionMode proj,
            const Float3& mins, const Float3& maxs,
            std::shared_ptr<RenderCore::IResourceView> textureResource,
            ColorB color = ColorB(0xffffffff),
            const Float2& minTex0 = Float2(0.f, 0.f), const Float2& maxTex0 = Float2(1.0f, 1.f)) = 0;

        virtual void    CaptureState    () = 0;
        virtual void    ReleaseState    () = 0;
        virtual void    SetState        (const OverlayState&) = 0;
        virtual void    SetEncoderState (const RenderCore::Techniques::EncoderState&) = 0;

        virtual void* GetService(uint64_t) = 0;
        virtual void AttachService(uint64_t, void*) = 0;

        RenderCore::Techniques::IImmediateDrawables& GetImmediateDrawables();
        RenderCore::IThreadContext& GetThreadContext();
        FontRenderingManager* GetFontRenderingManager();
        RenderCore::BufferUploads::CommandListID GetRequiredBufferUploadsCommandList() const;
        void RequireCommandList(RenderCore::BufferUploads::CommandListID);

        template<typename Type>
            Type* GetService() { return (Type*)GetService(typeid(std::decay_t<Type>).hash_code()); }
        template<typename Type>
            void AttachService2(Type& type) { AttachService(typeid(std::decay_t<Type>).hash_code(), &type); }

        IOverlayContext();
        virtual ~IOverlayContext();

    protected:
        RenderCore::Techniques::IImmediateDrawables* _immediateDrawables;
        RenderCore::IThreadContext* _threadContext;
        FontRenderingManager* _fontRenderingManager;
        RenderCore::BufferUploads::CommandListID _requiredBufferUploadsCommandList;
    };

    inline RenderCore::Techniques::IImmediateDrawables& IOverlayContext::GetImmediateDrawables() { return *_immediateDrawables; }
    inline RenderCore::IThreadContext& IOverlayContext::GetThreadContext() { return *_threadContext; }
    inline FontRenderingManager* IOverlayContext::GetFontRenderingManager() { return _fontRenderingManager; }

	inline RenderCore::BufferUploads::CommandListID IOverlayContext::GetRequiredBufferUploadsCommandList() const
	{ 
		return _requiredBufferUploadsCommandList;
	}

	inline void IOverlayContext::RequireCommandList(RenderCore::BufferUploads::CommandListID cmdList)
	{
		_requiredBufferUploadsCommandList = std::max(_requiredBufferUploadsCommandList, cmdList);
	}

}
