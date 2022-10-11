// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CLIXAutoPtr.h"
#include "../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../Core/Types.h"
#include <memory>

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; class IImmediateDrawables; } }

namespace GUILayer
{
    ref class RetainedRenderResources;

    /// <summary>Context for simple rendering commands</summary>
    /// Some tools need to perform basic rendering commands: create a vertex buffer,
    /// set a technique, draw some polygons. 
    /// 
    /// For example, a manipulator might want to draw a 3D arrow or tube as part
    /// of a widget.
    ///
    /// This provides this kind of basic behaviour via the CLI interface so it
    /// can be used by C# (or other C++/CLI) code.
    public ref class SimpleRenderingContext
    {
    public:
        void DrawPrimitive(
            unsigned primitiveType,
            uint64 vb,
            unsigned startVertex,
            unsigned vertexCount,
            const float color[], const float xform[]);

        void DrawIndexedPrimitive(
            unsigned primitiveType,
            uint64 vb, uint64 ib,
            unsigned startIndex,
            unsigned indexCount,
            unsigned startVertex,
            const float color[], const float xform[]);

        void InitState(bool depthTest, bool depthWrite);
        RenderCore::Techniques::ParsingContext& GetParsingContext() { return *_parsingContext; }
        RenderCore::IThreadContext& GetThreadContext() { return *_threadContext; }
        RenderCore::Techniques::IPipelineAcceleratorPool& GetPipelineAccelerators() { return *_pipelineAccelerators; }

        SimpleRenderingContext(
            RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
            RetainedRenderResources^ savedRes,
            RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
            void* parsingContext);
        ~SimpleRenderingContext();
        !SimpleRenderingContext();
    protected:
        RetainedRenderResources^ _retainedRes;
        RenderCore::Techniques::IImmediateDrawables* _immediateDrawables;     // note -- keeping an unprotected pointer here (SimpleRenderingContext is typically short lived). Create must be careful to manage lifetimes
        RenderCore::Techniques::ParsingContext* _parsingContext;
        RenderCore::Techniques::IPipelineAcceleratorPool* _pipelineAccelerators;
        RenderCore::IThreadContext* _threadContext;     // note -- keeping an unprotected pointer here (SimpleRenderingContext is typically short lived). Create must be careful to manage lifetimes
        bool _depthWriteEnable = true;
        bool _depthTestEnable = true;
        clix::shared_ptr<RenderCore::UniformsStreamInterface> _mainUniformsInterface;
    };
}

