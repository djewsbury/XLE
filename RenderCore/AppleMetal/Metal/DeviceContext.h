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
#include "../../FrameBufferDesc.h"
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
@class NSThread;

namespace RenderCore { class FrameBufferDesc; class AttachmentBlendDesc; }

namespace RenderCore { namespace Metal_AppleMetal
{
    class ShaderResourceView;
    class SamplerState;
    class ConstantBuffer;
    class BoundInputLayout;
    class ShaderProgram;
    class UnboundInterface;
    class ICompiledPipelineLayout;
    class FrameBuffer;

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
        friend class GraphicsEncoder_Optimized;
        friend class GraphicsEncoder_ProgressivePipeline;
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
        unsigned _captureGUID = ~0u;
        std::vector<std::pair<uint64_t, uint64_t>> _customBindings;
    };

////////////////////////////////////////////////////////////////////////////////////////////////////

    class AppleMetalEncoderSharedState;

    class GraphicsEncoder
	{
	public:
		//	------ Non-pipeline states (that can be changed mid-render pass) -------
		void        Bind(IteratorRange<const VertexBufferView*> vbViews, const IndexBufferView& ibView);
		void		SetStencilRef(unsigned frontFaceStencilRef, unsigned backFaceStencilRef);
		void		SetDepthBounds(float minDepthValue, float maxDepthValue);		// the 0-1 value stored in the depth buffer is compared directly to these bounds
		void 		Bind(IteratorRange<const ViewportDesc*> viewports, IteratorRange<const ScissorRect*> scissorRects);

        // --------------- Apple Metal specific interface ---------------
        void    PushDebugGroup(const char annotationName[]);
        void    PopDebugGroup();
        id<MTLRenderCommandEncoder> GetRenderCommandEncoder();
	protected:
		enum class Type { Normal, StreamOutput };
		GraphicsEncoder(
            MTLRenderPassDescriptor* renderPassDescriptor,
            std::shared_ptr<AppleMetalEncoderSharedState> sharedState,
            Type type = Type::Normal);
		~GraphicsEncoder();
		GraphicsEncoder(GraphicsEncoder&&);		// (hide these to avoid slicing in derived types)
		GraphicsEncoder& operator=(GraphicsEncoder&&);

        IdPtr _commandEncoder; 
        unsigned _indexType;        // MTLIndexType
        unsigned _indexFormatBytes;
        unsigned _indexBufferOffsetBytes;
        IdPtr _activeIndexBuffer; // MTLBuffer

        Type _type;
        std::shared_ptr<AppleMetalEncoderSharedState> _sharedState;

		friend class DeviceContext;
	};

    class GraphicsEncoder_Optimized : public GraphicsEncoder
	{
	public:
		//	------ Draw & Clear -------
		void        Draw(const GraphicsPipeline& pipeline, unsigned vertexCount, unsigned startVertexLocation=0);
		void        DrawIndexed(const GraphicsPipeline& pipeline, unsigned indexCount, unsigned startIndexLocation=0);
		void    	DrawInstances(const GraphicsPipeline& pipeline, unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation=0);
		void    	DrawIndexedInstances(const GraphicsPipeline& pipeline, unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation=0);
		void        DrawAuto(const GraphicsPipeline& pipeline);

		GraphicsEncoder_Optimized(GraphicsEncoder_Optimized&&);
		GraphicsEncoder_Optimized& operator=(GraphicsEncoder_Optimized&&);
		GraphicsEncoder_Optimized();
		~GraphicsEncoder_Optimized();
	protected:
		GraphicsEncoder_Optimized(
            MTLRenderPassDescriptor* renderPassDescriptor,
            std::shared_ptr<AppleMetalEncoderSharedState> sharedState,
            Type type = Type::Normal);

        const GraphicsPipeline* _boundGraphicsPipeline = nullptr;
		friend class DeviceContext;
	};

    class GraphicsEncoder_ProgressivePipeline : public GraphicsEncoder, public GraphicsPipelineBuilder
	{
	public:
		//	------ Draw & Clear -------
		void        Draw(unsigned vertexCount, unsigned startVertexLocation=0);
		void        DrawIndexed(unsigned indexCount, unsigned startIndexLocation=0);
		void    	DrawInstances(unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation=0);
		void    	DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation=0);
		void        DrawAuto();

		using GraphicsEncoder::Bind;
		using GraphicsPipelineBuilder::Bind;
		void        Bind(const ShaderProgram& shaderProgram);

		GraphicsEncoder_ProgressivePipeline(GraphicsEncoder_ProgressivePipeline&&);
		GraphicsEncoder_ProgressivePipeline& operator=(GraphicsEncoder_ProgressivePipeline&&);
		GraphicsEncoder_ProgressivePipeline();
		~GraphicsEncoder_ProgressivePipeline();
	protected:
		GraphicsEncoder_ProgressivePipeline(
            MTLRenderPassDescriptor* renderPassDescriptor,
            std::shared_ptr<AppleMetalEncoderSharedState> sharedState,
			Type type = Type::Normal);
		friend class DeviceContext;
        OCPtr<MTLRenderPipelineReflection> _graphicsPipelineReflection;
        void FinalizePipeline();
	};

////////////////////////////////////////////////////////////////////////////////////////////////////

    class DeviceContext : public GraphicsPipelineBuilder
    {
    public:
        // --------------- Cross-GFX-API interface --------------- 
        void BeginRenderPass(
			FrameBuffer& frameBuffer,
			IteratorRange<const ClearValue*> clearValues = {});
		void BeginNextSubpass(FrameBuffer& frameBuffer);
		void EndRenderPass();
		unsigned GetCurrentSubpassIndex() const;

        GraphicsEncoder_Optimized BeginGraphicsEncoder(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout);
		GraphicsEncoder_ProgressivePipeline BeginGraphicsEncoder_ProgressivePipeline(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout);
		// ComputeEncoder BeginComputeEncoder(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout);
		GraphicsEncoder_Optimized BeginStreamOutputEncoder(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout, IteratorRange<const VertexBufferView*> outputBuffers);
		// BlitEncoder BeginBlitEncoder();

		void Clear(const IResourceView& renderTarget, const VectorPattern<float,4>& clearColour);
		void Clear(const IResourceView& depthStencil, ClearFilter::BitField clearFilter, float depth, unsigned stencil);
		void ClearUInt(const IResourceView& unorderedAccess, const VectorPattern<unsigned,4>& clearColour);
		void ClearFloat(const IResourceView& unorderedAccess, const VectorPattern<float,4>& clearColour);
		void ClearStencil(const IResourceView& depthStencil, unsigned stencil);

		// TemporaryStorageResourceMap MapTemporaryStorage(size_t byteCount, BindFlag::Enum type);

        static const std::shared_ptr<DeviceContext>& Get(IThreadContext& threadContext);

        // --------------- Apple Metal specific interface ---------------
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        //      C M D L I S T
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        void            BeginCommandList();
        std::shared_ptr<CommandList>  ResolveCommandList();
        void            CommitCommandList(CommandList& commandList);

        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        //      E N C O D E R
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        bool    HasEncoder();
        bool    HasRenderCommandEncoder();
        bool    HasBlitCommandEncoder();
        id<MTLBlitCommandEncoder> GetBlitCommandEncoder();
        void    EndEncoding();
        // void    OnEndEncoding(std::function<void(void)> fn);
        // METAL_TODO: This function shouldn't be needed; it's here only as a temporary substitute for OnEndRenderPass (which is a safe time when we know we will not have a current encoder).
        // void    OnDestroyEncoder(std::function<void(void)> fn);
        // void    OnEndRenderPass(std::function<void(void)> fn);
        void    DestroyRenderCommandEncoder();
        void    DestroyBlitCommandEncoder();

        void QueueUniformSet(
            const std::shared_ptr<UnboundInterface>& unboundInterf,
            unsigned streamIdx,
            const UniformsStream& stream);

        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        //      C A P T U R E D S T A T E S
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        CapturedStates*     GetCapturedStates();
        void                BeginStateCapture(CapturedStates& capturedStates);
        void                EndStateCapture();

        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        //      U T I L I T Y
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        void                    HoldCommandBuffer(id<MTLCommandBuffer>);
        void                    ReleaseCommandBuffer();
        id<MTLCommandBuffer>    RetrieveCommandBuffer();

        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        //      D E V I C E
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        static void PrepareForDestruction(IDevice* device);

        DeviceContext(std::shared_ptr<IDevice> device);
        DeviceContext(const DeviceContext&) = delete;
        DeviceContext& operator=(const DeviceContext&) = delete;
        virtual ~DeviceContext();

    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

}}
