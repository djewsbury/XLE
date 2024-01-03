// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "InputLayout.h"
#include "ObjectFactory.h"
#include "TextureView.h"
#include "Format.h"     // (for FeatureSet)
#include "State.h"
#include "../IDeviceOpenGLES.h"
#include "../../IDevice_Forward.h"
#include "../../ResourceList.h"
#include "../../Format.h"
#include "../../BufferView.h"
#include "../../../Utility/Threading/ThreadingUtils.h"
#include <assert.h>
#include <vector>
#include <utility>
#include "IncludeGLES.h"

namespace RenderCore { namespace Metal_OpenGLES
{
    class ShaderResourceView;
    class SamplerState;
    class BoundInputLayout;
    class ShaderProgram;
    class BlendState;

////////////////////////////////////////////////////////////////////////////////////////////////////

    class CommandList : public RefCountedObject
    {
    public:
        CommandList() {}
        CommandList(const CommandList&) = delete;
        CommandList& operator=(const CommandList&) = delete;
    };

    class CapturedStates
    {
    public:
        RawGLHandle     _boundVAO = ~0u;
        unsigned        _activeVertexAttrib = 0;
        unsigned        _instancedVertexAttrib = 0;
        unsigned        _captureGUID = ~0u;
        unsigned        _activeTextureIndex = ~0u;

        std::vector<unsigned> _samplerStateBindings;
        std::vector<std::pair<uint64_t, uint64_t>> _customBindings;

        void VerifyIntegrity();
    };

    #if !defined(_DEBUG)
        inline void CapturedStates::VerifyIntegrity() {}
    #endif

    class GraphicsPipeline
    {
    public:
        uint64_t GetGUID() const { return 0; }
    };

    class GraphicsPipelineBuilder
    {
    public:
        void Bind(const ShaderProgram& shaderProgram);

        void Bind(const AttachmentBlendDesc& blendState);
        void Bind(const DepthStencilDesc& depthStencil);
        void Bind(Topology topology);
        void Bind(const RasterizationDesc& desc);

        DepthStencilDesc ActiveDepthStencilDesc();

        const std::shared_ptr<GraphicsPipeline>& CreatePipeline(ObjectFactory&);

        FeatureSet::BitField GetFeatureSet() const { return _featureSet; }

        GraphicsPipelineBuilder(FeatureSet::BitField featureSet);

    protected:
        unsigned    _nativeTopology;
        FeatureSet::BitField _featureSet;
        RasterizationDesc _rs;
    };

////////////////////////////////////////////////////////////////////////////////////////////////////

    class DeviceContext : public GraphicsPipelineBuilder
    {
    public:
        void Bind(const IndexBufferView& IB);
        void UnbindInputLayout();

        template<int Count> void BindPS(const ResourceList<ShaderResourceView, Count>& shaderResources);
        template<int Count> void BindPS(const ResourceList<SamplerState, Count>& samplerStates);

        void SetViewportAndScissorRects(IteratorRange<const ViewportDesc*> viewports, IteratorRange<const Rect2D*> scissorRects);

        using GraphicsPipelineBuilder::Bind;

        void Draw(unsigned vertexCount, unsigned startVertexLocation=0);
        void DrawIndexed(unsigned indexCount, unsigned startIndexLocation=0, unsigned baseVertexLocation=0);
        void DrawInstances(unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation=0);
        void DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation=0, unsigned baseVertexLocation=0);

        void    Draw(
            const GraphicsPipeline& pipeline,
            unsigned vertexCount, unsigned startVertexLocation=0)
        {
            Draw(vertexCount, startVertexLocation);
        }

        void    DrawIndexed(
            const GraphicsPipeline& pipeline,
            unsigned indexCount, unsigned startIndexLocation=0)
        {
            DrawIndexed(indexCount, startIndexLocation);
        }

        void    DrawInstances(
            const GraphicsPipeline& pipeline,
            unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation=0)
        {
            DrawInstances(vertexCount, instanceCount, startVertexLocation);
        }

        void    DrawIndexedInstances(
            const GraphicsPipeline& pipeline,
            unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation=0)
        {
            DrawIndexedInstances(indexCount, instanceCount, startIndexLocation);
        }

        void    DrawAuto(
            const GraphicsPipeline& pipeline)
        {
            // todo -- not implemented
            assert(0);
        }

        CapturedStates* GetCapturedStates() { return _capturedStates; }
        void        BeginStateCapture(CapturedStates& capturedStates);
        void        EndStateCapture();

        void    BeginRenderPass();
        void    EndRenderPass();
        bool    InRenderPass();
        void    OnEndRenderPass(std::function<void(void)> fn);

        void    BeginSubpass(unsigned renderTargetWidth, unsigned renderTargetHeight);
        void    EndSubpass();

        void            BeginCommandList();
        std::shared_ptr<CommandList>  ResolveCommandList();
        void            ExecuteCommandList(CommandList& commandList);

        std::shared_ptr<IDevice> GetDevice();

        static void PrepareForDestruction(IDevice* device);

        static const std::shared_ptr<DeviceContext>& Get(IThreadContext& threadContext);

        DeviceContext(std::shared_ptr<IDevice> device, FeatureSet::BitField featureSet);
        DeviceContext(const DeviceContext&) = delete;
        DeviceContext& operator=(const DeviceContext&) = delete;
        ~DeviceContext();
    private:
        friend class Device;
        friend class DeviceOpenGLES;

        unsigned    _indicesFormat;
        unsigned    _indexFormatBytes;
        unsigned    _indexBufferOffsetBytes;

        CapturedStates* _capturedStates;

        std::weak_ptr<IDevice> _device;

        unsigned _renderTargetWidth;
        unsigned _renderTargetHeight;

        bool _inRenderPass;
        std::vector<std::function<void(void)>> _onEndRenderPassFunctions;
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    template<int Count> void DeviceContext::BindPS(const ResourceList<ShaderResourceView, Count>& shaderResources)
    {
        for (int c=0; c<Count; ++c) {
            glActiveTexture(GL_TEXTURE0 + c + shaderResources._startingPoint);
            glBindTexture(GL_TEXTURE_2D, shaderResources._buffers[c]->AsRawGLHandle());
        }
        if (_capturedStates)
            _capturedStates->_activeTextureIndex = Count + shaderResources._startingPoint;
    }

    template<int Count> void DeviceContext::BindPS(const ResourceList<SamplerState, Count>& samplerStates)
    {
        for (int c=0; c<Count; ++c) {
            samplerStates._buffers[c].Apply(c+samplerStates._startingPoint);
        }
    }

    inline void GraphicsPipelineBuilder::Bind(Topology topology)
    {
        switch (topology)
        {
        case Topology::PointList: _nativeTopology = GL_POINTS; break;
        case Topology::LineList: _nativeTopology = GL_LINES; break;
        case Topology::LineStrip: _nativeTopology = GL_LINE_STRIP; break;
        case Topology::TriangleList: _nativeTopology = GL_TRIANGLES; break;
        case Topology::TriangleStrip: _nativeTopology = GL_TRIANGLE_STRIP; break;
        default: _nativeTopology = GL_ZERO; break;
        }
    }

}}
