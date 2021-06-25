// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TextureView.h"
#include "FrameBuffer.h"        // for AttachmentPool
#include "DX11.h"
#include "../../IDevice_Forward.h"
#include "../../ResourceList.h"
#include "../../Format.h"
#include "../../StateDesc.h"
#include "../../../Utility/Threading/ThreadingUtils.h"
#include "../../../Utility/IntrusivePtr.h"
#include "../../../Utility/IteratorUtils.h"
#include "../../../Core/Prefix.h"
#include <functional>

namespace RenderCore { enum class ShaderStage; class ViewportDesc; class ScissorRect; class IndexBufferView; class AttachmentBlendDesc; class RasterizationDesc; class DepthStencilDesc; class VertexBufferView; }
namespace Assets { class DependencyValidation; }

namespace RenderCore { namespace Metal_DX11
{
    class Buffer;
    class ShaderResourceView;
    class SamplerState;
	class BoundInputLayout;
    class ComputeShader;
    class ShaderProgram;
    class RasterizerState;
    class BlendState;
    class DepthStencilState;
    class DepthStencilView;
    class RenderTargetView;
    class BoundClassInterfaces;
	class ObjectFactory;
	class Resource;
	class NumericUniformsInterface;

        //  todo ---    DeviceContext, ObjectFactory & CommandList -- maybe these
        //              should go into RenderCore (because it's impossible to do anything without them)

    class CommandList : public RefCountedObject
    {
    public:
        CommandList(ID3D::CommandList* underlying);
        CommandList(intrusive_ptr<ID3D::CommandList>&& underlying);
        ID3D::CommandList* GetUnderlying() { return _underlying.get(); }

		CommandList(const CommandList&) = delete;
		CommandList& operator=(const CommandList&) = delete;
    private:
        intrusive_ptr<ID3D::CommandList> _underlying;
    };

	class GraphicsPipeline
    {
    public:
        uint64_t GetGUID() const { return _guid; }

		void ApplyVertexBuffers(DeviceContext& context, IteratorRange<const VertexBufferView*> vertexBuffers) const never_throws;

		GraphicsPipeline();
		~GraphicsPipeline();

		GraphicsPipeline(const GraphicsPipeline&) = delete;
		GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

		const ::Assets::DependencyValidation& GetDependencyValidation() const;

		// internal use only -- 
		const ShaderProgram& GetShaderProgram() const;
	private:
		uint64_t _guid;
    };

    class GraphicsPipelineBuilder
    {
    public:
        void Bind(const ShaderProgram& shaderProgram);
		void Bind(const ShaderProgram& shaderProgram, const BoundClassInterfaces& dynLinkage);

        void Bind(const AttachmentBlendDesc& blendState);
        void Bind(const DepthStencilDesc& depthStencil);
        void Bind(Topology topology);
        void Bind(const RasterizationDesc& desc);

        void SetInputLayout(const BoundInputLayout& inputLayout);
		void SetRenderPassConfiguration(const FrameBufferDesc& fbDesc, unsigned subPass = 0);

        const std::shared_ptr<GraphicsPipeline>& CreatePipeline(ObjectFactory&);

		static uint64_t CalculateFrameBufferRelevance(const FrameBufferDesc& fbDesc, unsigned subPass = 0);

        GraphicsPipelineBuilder();
		~GraphicsPipelineBuilder();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
    };

    class DeviceContext
    {
    public:
		///////////////////////////////////////////////////////////////////////////////////////////////////
		// Core Draw / Dispatch / Clear operations
		///////////////////////////////////////////////////////////////////////////////////////////////////
        void        Draw(unsigned vertexCount, unsigned startVertexLocation=0);
        void        DrawIndexed(unsigned indexCount, unsigned startIndexLocation=0, unsigned baseVertexLocation=0);
		void		DrawInstances(unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation=0);
        void		DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation=0, unsigned baseVertexLocation=0);
        void        DrawAuto();
        void        Dispatch(unsigned countX, unsigned countY=1, unsigned countZ=1);

		void        Draw(const GraphicsPipeline& pipeline, unsigned vertexCount, unsigned startVertexLocation=0);
        void        DrawIndexed(const GraphicsPipeline& pipeline, unsigned indexCount, unsigned startIndexLocation=0, unsigned baseVertexLocation=0);
		void		DrawInstances(const GraphicsPipeline& pipeline, unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation=0);
        void		DrawIndexedInstances(const GraphicsPipeline& pipeline, unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation=0, unsigned baseVertexLocation=0);
		void        DrawAuto(const GraphicsPipeline& pipeline);

        void        Clear(const RenderTargetView& renderTargets, const VectorPattern<float,4>& values);
        void        Clear(const DepthStencilView& depthStencil, ClearFilter::BitField clearFilter, float depth, unsigned stencil);
        void        ClearUInt(const UnorderedAccessView& unorderedAccess, const VectorPattern<unsigned,4>& values);
        void        ClearFloat(const UnorderedAccessView& unorderedAccess, const VectorPattern<float,4>& values);

		///////////////////////////////////////////////////////////////////////////////////////////////////
		// Input geometry binding
		///////////////////////////////////////////////////////////////////////////////////////////////////
		void		Bind(const IndexBufferView& IB);
		XLE_DEPRECATED_ATTRIBUTE void        Bind(const Resource& ib, Format indexFormat, unsigned offset=0);
		void		UnbindInputLayout();

		///////////////////////////////////////////////////////////////////////////////////////////////////
		// Viewport & scissor states
		///////////////////////////////////////////////////////////////////////////////////////////////////
		void SetViewportAndScissorRects(IteratorRange<const ViewportDesc*> viewports, IteratorRange<const ScissorRect*> scissorRects);
		ViewportDesc GetBoundViewport();

		XLE_DEPRECATED_ATTRIBUTE void		Bind(const ViewportDesc& viewport);

		///////////////////////////////////////////////////////////////////////////////////////////////////
		// pre-GraphicsPipelineBuilder binding functions
		///////////////////////////////////////////////////////////////////////////////////////////////////
		XLE_DEPRECATED_ATTRIBUTE void		Bind(Topology topology);
        XLE_DEPRECATED_ATTRIBUTE void		Bind(const ComputeShader& computeShader);
        XLE_DEPRECATED_ATTRIBUTE void		Bind(const ShaderProgram& shaderProgram);
        XLE_DEPRECATED_ATTRIBUTE void		Bind(const RasterizerState& rasterizer);
        XLE_DEPRECATED_ATTRIBUTE void		Bind(const BlendState& blender);
        XLE_DEPRECATED_ATTRIBUTE void		Bind(const DepthStencilState& depthStencilState, unsigned stencilRef = 0x0);

        XLE_DEPRECATED_ATTRIBUTE void		Bind(const ShaderProgram& shaderProgram, const BoundClassInterfaces& dynLinkage);

		XLE_DEPRECATED_ATTRIBUTE void		UnbindVS();
		XLE_DEPRECATED_ATTRIBUTE void		UnbindPS();
		XLE_DEPRECATED_ATTRIBUTE void		UnbindGS();
		XLE_DEPRECATED_ATTRIBUTE void		UnbindHS();
		XLE_DEPRECATED_ATTRIBUTE void		UnbindDS();
		XLE_DEPRECATED_ATTRIBUTE void		UnbindCS();

		///////////////////////////////////////////////////////////////////////////////////////////////////
		// Legacy pre-render-pass interface
		///////////////////////////////////////////////////////////////////////////////////////////////////
		template<int Count> XLE_DEPRECATED_ATTRIBUTE void    Bind(const ResourceList<RenderTargetView, Count>& renderTargets, const DepthStencilView* depthStencil);
        template<int Count1, int Count2> XLE_DEPRECATED_ATTRIBUTE void    Bind(const ResourceList<RenderTargetView, Count1>& renderTargets, const DepthStencilView* depthStencil, const ResourceList<UnorderedAccessView, Count2>& unorderedAccess);

		bool		InRenderPass();

        void        BeginCommandList();
        auto        ResolveCommandList() -> std::shared_ptr<CommandList>;
        void        ExecuteCommandList(CommandList& commandList, bool preserveRenderState);

		NumericUniformsInterface& GetNumericUniforms(ShaderStage stage);

        static std::shared_ptr<DeviceContext> Get(IThreadContext& threadContext);
        static void PrepareForDestruction(IDevice* device, IPresentationChain* presentationChain);

        ID3D::DeviceContext*            GetUnderlying() const           { return _underlying.get(); }
        ID3D::UserDefinedAnnotation*    GetAnnotationInterface() const  { return _annotations.get(); }
        bool                            IsImmediate() const;

        void        InvalidateCachedState();

        ID3D::Buffer*               _currentCBs[6][14];
        ID3D::ShaderResourceView*   _currentSRVs[6][32];
		ID3D::SamplerState*			_currentSSs[6][32];

		ObjectFactory&	GetFactory() { return *_factory; }

        std::shared_ptr<DeviceContext> Fork();

        DeviceContext(ID3D::DeviceContext* context);
        DeviceContext(intrusive_ptr<ID3D::DeviceContext>&& context);
        ~DeviceContext();

        #if COMPILER_DEFAULT_IMPLICIT_OPERATORS == 1
            DeviceContext(const DeviceContext&) = delete;
            DeviceContext& operator=(const DeviceContext&) = delete;
            DeviceContext(DeviceContext&&) = default;
            DeviceContext& operator=(DeviceContext&&) = default;
        #endif
    private:
        intrusive_ptr<ID3D::DeviceContext> _underlying;
        intrusive_ptr<ID3D::UserDefinedAnnotation> _annotations;
		ObjectFactory* _factory;
        std::vector<NumericUniformsInterface> _numericUniforms;

		// The following API functions are meant for use by the FrameBuffer implementation
		void    BeginRenderPass();
        void    EndRenderPass();
        void    OnEndRenderPass(std::function<void(void)> fn);
        void    BeginSubpass(unsigned renderTargetWidth, unsigned renderTargetHeight);
        void    EndSubpass();

		unsigned _renderTargetWidth;
        unsigned _renderTargetHeight;

        bool _inRenderPass;
        std::vector<std::function<void(void)>> _onEndRenderPassFunctions;

		friend class FrameBuffer;
		friend void BeginRenderPass(
			DeviceContext& context,
			FrameBuffer& frameBuffer,
			IteratorRange<const ClearValue*> clearValues);
		friend void EndRenderPass(DeviceContext& context);

		void Bind(const GraphicsPipeline&);
		uint64_t _boundGraphicsPipeline;

		unsigned _boundStencilRefValue;
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    ObjectFactory& GetObjectFactory(IDevice& device);
	ObjectFactory& GetObjectFactory(DeviceContext&);
	ObjectFactory& GetObjectFactory();
    ObjectFactory& GetObjectFactory(ID3D::Device& device);
	ObjectFactory& GetObjectFactory(ID3D::Resource& resource);

}}
