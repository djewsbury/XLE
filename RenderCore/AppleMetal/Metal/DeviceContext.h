// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "InputLayout.h"
#include "State.h"
#include "TextureView.h"
#include "../../IDevice_Forward.h"
#include "../../ResourceList.h"
#include "../../Format.h"
#include "../../BufferView.h"
#include "../../../Utility/Threading/ThreadingUtils.h"
#include "../../../Utility/IteratorUtils.h"
#include <assert.h>
#include <memory>
#include <vector>
#include <utility>
#include <string>
#include <Foundation/NSObject.h>

@class MTLRenderPassDescriptor;
@class MTLRenderPipelineReflection;
@protocol MTLBlitCommandEncoder;
@protocol MTLCommandBuffer;
@protocol MTLDevice;
@protocol MTLRenderCommandEncoder;
@protocol MTLFunction;
@protocol MTLDepthStencilState;
@protocol MTLRenderPipelineState;
@protocol MTLDepthStencilState;

namespace RenderCore { class FrameBufferDesc; class AttachmentBlendDesc; }

namespace RenderCore { namespace Metal_AppleMetal
{
    class ShaderResourceView;
    class SamplerState;
    class ConstantBuffer;
    class BoundInputLayout;
    class ShaderProgram;

    class UnboundInterface;

////////////////////////////////////////////////////////////////////////////////////////////////////

    class GraphicsPipeline
    {
    public:
        // --------------- Cross-GFX-API interface ---------------
        uint64_t GetInterfaceBindingGUID() const { return _interfaceBindingGUID; }
        const ::Assets::DependencyValidation& GetDependencyValidation() const;

    private:
        OCPtr<NSObject<MTLRenderPipelineState>> _underlying;
        OCPtr<MTLRenderPipelineReflection> _reflection;
        OCPtr<NSObject<MTLDepthStencilState>> _depthStencilState;
        unsigned _primitiveType = 0;                // MTLPrimitiveType
        unsigned _cullMode = 0;
        unsigned _faceWinding = 0;
        uint64_t _interfaceBindingGUID = 0;

        GraphicsPipeline(
            OCPtr<NSObject<MTLRenderPipelineState>> underlying,
            OCPtr<MTLRenderPipelineReflection> reflection,
            OCPtr<NSObject<MTLDepthStencilState>> depthStencilState,
            unsigned primitiveType,
            unsigned cullMode,
            unsigned faceWinding,
            uint64_t interfaceBindingGUID);

        #if defined(_DEBUG)
            std::string _shaderSourceIdentifiers;
        #endif

        friend class DeviceContext;
        friend class GraphicsPipelineBuilder;
    };

    class GraphicsPipelineBuilder
    {
    public:
        // --------------- Cross-GFX-API interface ---------------
        void Bind(const ShaderProgram& shaderProgram);

        void Bind(IteratorRange<const AttachmentBlendDesc*> blendStates);
        void Bind(const DepthStencilDesc& depthStencil);
        void Bind(const RasterizationDesc& rasterizer);

        void Bind(const BoundInputLayout& inputLayout, Topology topology);
		void UnbindInputLayout();

        void SetRenderPassConfiguration(const FrameBufferDesc& fbDesc, unsigned subPass);
        void SetRenderPassConfiguration(MTLRenderPassDescriptor* desc, unsigned sampleCount);
        uint64_t GetRenderPassConfigurationHash() const;

        static uint64_t CalculateFrameBufferRelevance(const FrameBufferDesc& fbDesc, unsigned subPass = 0);

        const std::shared_ptr<GraphicsPipeline>& CreatePipeline(ObjectFactory&);

        // --------------- Apple Metal specific interface ---------------
        bool IsPipelineStale() const { return _dirty; }

        GraphicsPipelineBuilder();
        ~GraphicsPipelineBuilder();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
        bool _dirty = false;
        unsigned _cullMode = 0;
        unsigned _faceWinding = 0;

        OCPtr<NSObject<MTLDepthStencilState>> CreateDepthStencilState(ObjectFactory& factory);
    };

////////////////////////////////////////////////////////////////////////////////////////////////////

    class CommandList
    {
    public:
        CommandList() {}
        CommandList(const CommandList&) = delete;
        CommandList& operator=(const CommandList&) = delete;
    };

    class CapturedStates
    {
    public:
        unsigned        _captureGUID = ~0u;

        std::vector<std::pair<uint64_t, uint64_t>> _customBindings;
    };

////////////////////////////////////////////////////////////////////////////////////////////////////

    class DeviceContext : public GraphicsPipelineBuilder
    {
    public:
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        //      E N C O D E R
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        void    BindVS(id<MTLBuffer> buffer, unsigned offset, unsigned bufferIndex);
        void    Bind(const IndexBufferView& IB);

        void    Bind(IteratorRange<const ViewportDesc*> viewports, IteratorRange<const ScissorRect*> scissorRects);

        using GraphicsPipelineBuilder::Bind;

        void    Draw(unsigned vertexCount, unsigned startVertexLocation=0);
        void    DrawIndexed(unsigned indexCount, unsigned startIndexLocation=0, unsigned baseVertexLocation=0);
        void    DrawInstances(unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation=0);
        void    DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation=0, unsigned baseVertexLocation=0);

        void    Draw(
            const GraphicsPipeline& pipeline,
            unsigned vertexCount, unsigned startVertexLocation=0);
        void    DrawIndexed(
            const GraphicsPipeline& pipeline,
            unsigned indexCount, unsigned startIndexLocation=0);
        void    DrawInstances(
            const GraphicsPipeline& pipeline,
            unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation=0);
        void    DrawIndexedInstances(
            const GraphicsPipeline& pipeline,
            unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation=0);

        void    PushDebugGroup(const char annotationName[]);
        void    PopDebugGroup();

        void    BeginRenderPass();
        void    EndRenderPass();
        bool    InRenderPass();
        // void    OnEndRenderPass(std::function<void(void)> fn);

        bool    HasEncoder();
        bool    HasRenderCommandEncoder();
        bool    HasBlitCommandEncoder();
        id<MTLRenderCommandEncoder> GetCommandEncoder();
        id<MTLRenderCommandEncoder> GetRenderCommandEncoder();
        id<MTLBlitCommandEncoder> GetBlitCommandEncoder();
        void    CreateRenderCommandEncoder(MTLRenderPassDescriptor* renderPassDescriptor);
        void    CreateBlitCommandEncoder();
        void    EndEncoding();
        // void    OnEndEncoding(std::function<void(void)> fn);
        // METAL_TODO: This function shouldn't be needed; it's here only as a temporary substitute for OnEndRenderPass (which is a safe time when we know we will not have a current encoder).
        // void    OnDestroyEncoder(std::function<void(void)> fn);
        void    DestroyRenderCommandEncoder();
        void    DestroyBlitCommandEncoder();

        void QueueUniformSet(
            const std::shared_ptr<UnboundInterface>& unboundInterf,
            unsigned streamIdx,
            const UniformsStream& stream);

        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        //      C A P T U R E D S T A T E S
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        CapturedStates* GetCapturedStates();
        void        BeginStateCapture(CapturedStates& capturedStates);
        void        EndStateCapture();

        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        //      U T I L I T Y
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        void            HoldCommandBuffer(id<MTLCommandBuffer>);
        void            ReleaseCommandBuffer();
        id<MTLCommandBuffer>            RetrieveCommandBuffer();

        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        //      C M D L I S T
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        void            BeginCommandList();
        std::shared_ptr<CommandList>  ResolveCommandList();
        void            CommitCommandList(CommandList& commandList);

        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        //      D E V I C E
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        std::shared_ptr<IDevice> GetDevice();

        static void PrepareForDestruction(IDevice* device);

        static const std::shared_ptr<DeviceContext>& Get(IThreadContext& threadContext);

        DeviceContext(std::shared_ptr<IDevice> device);
        DeviceContext(const DeviceContext&) = delete;
        DeviceContext& operator=(const DeviceContext&) = delete;
        virtual ~DeviceContext();

    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        void FinalizePipeline();
    };

}}
