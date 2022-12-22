// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeviceContext.h"
#include "DeviceContextImpl.h"
#include "InputLayout.h"
#include "Shader.h"
#include "State.h"
#include "Buffer.h"
#include "Resource.h"
#include "ObjectFactory.h"
#include "../../BufferView.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/Threading/Mutex.h"
#include "../../../Utility/MemoryUtils.h"

#include "../IDeviceDX11.h"
#include "IncludeDX11.h"
#include "DX11Utils.h"
#include <map>

namespace RenderCore { namespace Metal_DX11
{
    static_assert(  (unsigned)Topology::PointList      == D3D11_PRIMITIVE_TOPOLOGY_POINTLIST
                &&  (unsigned)Topology::LineList       == D3D11_PRIMITIVE_TOPOLOGY_LINELIST
                &&  (unsigned)Topology::LineStrip      == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP
                &&  (unsigned)Topology::TriangleList   == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
                &&  (unsigned)Topology::TriangleStrip  == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
                "Toplogy flags are out-of-sync");

    void DeviceContext::Bind(Topology topology)
    {
        _underlying->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY(topology));
		_boundGraphicsPipeline = 0;
    }

    void DeviceContext::Bind(const ComputeShader& computeShader)
    {
        _underlying->CSSetShader(computeShader.GetUnderlying(), nullptr, 0);
    }

    void DeviceContext::Bind(const ShaderProgram& shaderProgram)
    {
		shaderProgram.Apply(*this);
		_boundGraphicsPipeline = 0;
    }

    void DeviceContext::Bind(const ShaderProgram& shaderProgram, const BoundClassInterfaces& dynLinkage)
    {
		shaderProgram.Apply(*this, dynLinkage);
		_boundGraphicsPipeline = 0;
    }

    void DeviceContext::Bind(const RasterizerState& rasterizer)
    {
        _underlying->RSSetState(rasterizer.GetUnderlying());
		_boundGraphicsPipeline = 0;
    }

    void DeviceContext::Bind(const BlendState& blender)
    {
        const FLOAT blendFactors[] = {1.f, 1.f, 1.f, 1.f};
        _underlying->OMSetBlendState(blender.GetUnderlying(), blendFactors, 0xffffffff);
		_boundGraphicsPipeline = 0;
    }

    void DeviceContext::Bind(const DepthStencilState& depthStencil, unsigned stencilRef)
    {
        _underlying->OMSetDepthStencilState(depthStencil.GetUnderlying(), stencilRef);
		_boundGraphicsPipeline = 0;
		_boundStencilRefValue = stencilRef;
    }

    void DeviceContext::Bind(const IndexBufferView& IB)
	{
		assert(IB._resource);
		auto* d3dResource = (Resource*)const_cast<IResource*>(IB._resource)->QueryInterface(TypeHashCode<Resource>);
		assert(d3dResource);
		assert(QueryInterfaceCast<ID3D::Buffer>(d3dResource->_underlying.get()));
        _underlying->IASetIndexBuffer((ID3D::Buffer*)d3dResource->_underlying.get(), AsDXGIFormat(IB._indexFormat), IB._offset);
	}
	
	void DeviceContext::Bind(const Resource& ib, Format indexFormat, unsigned offset)
    {
		assert(QueryInterfaceCast<ID3D::Buffer>(ib._underlying.get()));
        _underlying->IASetIndexBuffer((ID3D::Buffer*)ib._underlying.get(), AsDXGIFormat(indexFormat), offset);
    }

    void DeviceContext::Bind(const Viewport& viewport)
    {
            // ("ViewportDesc" is equivalent to D3D11_VIEWPORT)
            //      --  we could do static_asserts to check the offsets of the members
            //          to make sure.
        _underlying->RSSetViewports(1, (D3D11_VIEWPORT*)&viewport);
    }

	void DeviceContext::SetViewportAndScissorRects(IteratorRange<const Viewport*> viewports, IteratorRange<const ScissorRect*> scissorRects)
	{
		// "Viewport" is compatible with D3D11_VIEWPORT, but not the same size
		D3D11_VIEWPORT d3dViewports[D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX];
		auto cnt = std::min(viewports.size(), dimof(d3dViewports));
		for (unsigned c=0; c<cnt; ++c) {
			const auto& srcViewport = viewports[c];
			d3dViewports[c] = *(const D3D11_VIEWPORT*)&srcViewport;
			if (!viewports[c].OriginIsUpperLeft) {
				d3dViewports[c].TopLeftY = _renderTargetHeight - srcViewport.Y - srcViewport.Height;
			}
		}
		_underlying->RSSetViewports((UINT)cnt, d3dViewports);

		D3D11_RECT rects[D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX];
		cnt = std::min(scissorRects.size(), dimof(rects));
		for (unsigned c=0; c<cnt; ++c) {
			rects[c].left = scissorRects[c].X;
			rects[c].top = scissorRects[c].Y;
			rects[c].right = scissorRects[c].X + scissorRects[c].Width;
			rects[c].bottom = scissorRects[c].Y + scissorRects[c].Height;
			if (!viewports[c].OriginIsUpperLeft) {
				rects[c].top = _renderTargetHeight - scissorRects[c].Y - scissorRects[c].Height;
			}
		}
		_underlying->RSSetScissorRects((UINT)cnt, rects);
	}

	static_assert(  offsetof(ViewportDesc, X) == offsetof(D3D11_VIEWPORT, TopLeftX)
                &&  offsetof(ViewportDesc, Y) == offsetof(D3D11_VIEWPORT, TopLeftY)
                &&  offsetof(ViewportDesc, Width) == offsetof(D3D11_VIEWPORT, Width)
                &&  offsetof(ViewportDesc, Height) == offsetof(D3D11_VIEWPORT, Height)
                &&  offsetof(ViewportDesc, MinDepth) == offsetof(D3D11_VIEWPORT, MinDepth)
                &&  offsetof(ViewportDesc, MaxDepth) == offsetof(D3D11_VIEWPORT, MaxDepth),
                "ViewportDesc is no longer compatible with D3D11_VIEWPORT");

	Viewport DeviceContext::GetBoundViewport()
	{
		Viewport result;
		UINT viewportsToGet = 1;
        GetUnderlying()->RSGetViewports(&viewportsToGet, (D3D11_VIEWPORT*)&result);
		result.OriginIsUpperLeft = true;
		return result;
	}

    void DeviceContext::Draw(unsigned vertexCount, unsigned startVertexLocation)
    {
        _underlying->Draw(vertexCount, startVertexLocation);
    }

    void DeviceContext::DrawIndexed(unsigned indexCount, unsigned startIndexLocation, unsigned baseVertexLocation)
    {
        _underlying->DrawIndexed(indexCount, startIndexLocation, baseVertexLocation);
    }

	void DeviceContext::DrawInstances(unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation)
	{
		_underlying->DrawInstanced(vertexCount, instanceCount, startVertexLocation, 0);
	}

	void DeviceContext::DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation, unsigned baseVertexLocation)
	{
		_underlying->DrawIndexedInstanced(indexCount, indexCount, startIndexLocation, baseVertexLocation, 0);
	}

    void DeviceContext::DrawAuto()
    {
        _underlying->DrawAuto();
    }

	void DeviceContext::Draw(const GraphicsPipeline& pipeline, unsigned vertexCount, unsigned startVertexLocation)
	{
		if (pipeline.GetGUID() != _boundGraphicsPipeline)
			Bind(pipeline);
		_underlying->Draw(vertexCount, startVertexLocation);
	}

    void DeviceContext::DrawIndexed(const GraphicsPipeline& pipeline, unsigned indexCount, unsigned startIndexLocation, unsigned baseVertexLocation)
	{
		if (pipeline.GetGUID() != _boundGraphicsPipeline)
			Bind(pipeline);
		_underlying->DrawIndexed(indexCount, startIndexLocation, baseVertexLocation);
	}

	void DeviceContext::DrawInstances(const GraphicsPipeline& pipeline, unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation)
	{
		if (pipeline.GetGUID() != _boundGraphicsPipeline)
			Bind(pipeline);
		_underlying->DrawInstanced(vertexCount, instanceCount, startVertexLocation, 0);
	}

    void DeviceContext::DrawIndexedInstances(const GraphicsPipeline& pipeline, unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation, unsigned baseVertexLocation)
	{
		if (pipeline.GetGUID() != _boundGraphicsPipeline)
			Bind(pipeline);
		_underlying->DrawIndexedInstanced(indexCount, indexCount, startIndexLocation, baseVertexLocation, 0);
	}

	void DeviceContext::DrawAuto(const GraphicsPipeline& pipeline)
	{
		if (pipeline.GetGUID() != _boundGraphicsPipeline)
			Bind(pipeline);
		_underlying->DrawAuto();
	}

    void DeviceContext::Clear(const RenderTargetView& renderTargets, const VectorPattern<float,4>& values)
    {
        _underlying->ClearRenderTargetView(renderTargets.GetUnderlying(), values._values);
    }

    void DeviceContext::Clear(const DepthStencilView& depthStencil, ClearFilter::BitField clearFilter, float depth, unsigned stencil)
    {
        unsigned underlyingFilter = 0u;
        if (clearFilter & ClearFilter::Depth) underlyingFilter |= D3D11_CLEAR_DEPTH;
        if (clearFilter & ClearFilter::Stencil) underlyingFilter |= D3D11_CLEAR_STENCIL;
        _underlying->ClearDepthStencilView(depthStencil.GetUnderlying(), underlyingFilter, depth, (UINT8)stencil);
    }

    void DeviceContext::ClearUInt(const UnorderedAccessView& unorderedAccess, const VectorPattern<unsigned,4>& values)
    {
        _underlying->ClearUnorderedAccessViewUint(unorderedAccess.GetUnderlying(), values._values);
    }

    void DeviceContext::ClearFloat(const UnorderedAccessView& unorderedAccess, const VectorPattern<float,4>& values)
    {
        _underlying->ClearUnorderedAccessViewFloat(unorderedAccess.GetUnderlying(), values._values);
    }

    void DeviceContext::UnbindInputLayout()
    {
        _underlying->IASetInputLayout(nullptr);

		ID3D::Buffer* vb = nullptr;
        UINT strides = 0, offsets = 0;
        _underlying->IASetVertexBuffers(0, 1, &vb, &strides, &offsets);
    }

    void DeviceContext::UnbindVS() { _underlying->VSSetShader(nullptr, nullptr, 0); }
    void DeviceContext::UnbindPS() { _underlying->PSSetShader(nullptr, nullptr, 0); }
    void DeviceContext::UnbindGS() { _underlying->GSSetShader(nullptr, nullptr, 0); }
	void DeviceContext::UnbindHS() { _underlying->HSSetShader(nullptr, nullptr, 0); }
	void DeviceContext::UnbindDS() { _underlying->DSSetShader(nullptr, nullptr, 0); }
	void DeviceContext::UnbindCS() { _underlying->CSSetShader(nullptr, nullptr, 0); }

    void DeviceContext::Dispatch(unsigned countX, unsigned countY, unsigned countZ)
    {
        _underlying->Dispatch(countX, countY, countZ);
    }

    void        DeviceContext::InvalidateCachedState()
    {
        XlZeroMemory(_currentCBs);
        XlZeroMemory(_currentSRVs);
		XlZeroMemory(_currentSSs);
    }

	NumericUniformsInterface& DeviceContext::GetNumericUniforms(ShaderStage stage)
	{
		assert(unsigned(stage) < _numericUniforms.size());
		return _numericUniforms[unsigned(stage)];
	}

	void DeviceContext::BeginRenderPass()
    {
        assert(!_inRenderPass);
        _inRenderPass = true;
    }

    void DeviceContext::BeginSubpass(unsigned renderTargetWidth, unsigned renderTargetHeight)
	{
        _renderTargetWidth = renderTargetWidth;
        _renderTargetHeight = renderTargetHeight;
		_boundStencilRefValue = 0;		// reset this to some default value, to try to prevent state leakage
    }

    void DeviceContext::EndSubpass()
	{
        _renderTargetWidth = 0;
        _renderTargetHeight = 0;
    }

    void DeviceContext::EndRenderPass()
    {
        assert(_inRenderPass);
        _inRenderPass = false;
        _renderTargetWidth = 0;
        _renderTargetHeight = 0;

        for (auto fn: _onEndRenderPassFunctions) { fn(); }
        _onEndRenderPassFunctions.clear();
    }

    bool DeviceContext::InRenderPass()
    {
        return _inRenderPass;
    }

    void DeviceContext::OnEndRenderPass(std::function<void ()> fn)
    {
        if (!_inRenderPass) {
            _onEndRenderPassFunctions.emplace_back(std::move(fn));
        } else {
            fn();
        }
    }

    DeviceContext::DeviceContext(ID3D::DeviceContext* context)
	: DeviceContext(intrusive_ptr<ID3D::DeviceContext>(context)) {}

    DeviceContext::DeviceContext(intrusive_ptr<ID3D::DeviceContext>&& context)
    : _underlying(std::forward<intrusive_ptr<ID3D::DeviceContext>>(context))
	, _factory(nullptr)
    {
        XlZeroMemory(_currentCBs);
        XlZeroMemory(_currentSRVs);
		XlZeroMemory(_currentSSs);
        _annotations = QueryInterfaceCast<ID3D::UserDefinedAnnotation>(_underlying);

		ID3D::Device* devRaw = nullptr;
		_underlying->GetDevice(&devRaw);
        intrusive_ptr<ID3D::Device> dev = moveptr(devRaw);
		if (dev) _factory = &GetObjectFactory(*dev);

		_numericUniforms.resize(6);
		for (unsigned c=0; c<_numericUniforms.size(); ++c)
			_numericUniforms[c] = NumericUniformsInterface{*this, (ShaderStage)c};

		_inRenderPass = false;
        _renderTargetWidth = 0;
        _renderTargetHeight = 0;
		_boundStencilRefValue = 0;
    }

    DeviceContext::~DeviceContext()
    {}


    void                        DeviceContext::BeginCommandList()
    {
            // (nothing required in D3D11)
    }

    std::shared_ptr<CommandList>     DeviceContext::ResolveCommandList()
    {
        ID3D::CommandList* commandListTemp = nullptr;
        HRESULT hresult = _underlying->FinishCommandList(FALSE, &commandListTemp);
        if (SUCCEEDED(hresult) && commandListTemp) {
            intrusive_ptr<ID3D::CommandList> underlyingCommandList = moveptr(commandListTemp);
            return make_intrusive<CommandList>(underlyingCommandList.get());
        }
        return std::shared_ptr<CommandList>();
    }

    bool    DeviceContext::IsImmediate() const
    {
        auto type = _underlying->GetType();
        return type == D3D11_DEVICE_CONTEXT_IMMEDIATE;
    }

    void    DeviceContext::ExecuteCommandList(CommandList& commandList, bool preserveRenderState)
    {
        // Note that if "preserveRenderState" isn't set, the device will be reset to it's default
        // state.
        _underlying->ExecuteCommandList(commandList.GetUnderlying(), preserveRenderState);
    }

    std::shared_ptr<DeviceContext>  DeviceContext::Get(IThreadContext& threadContext)
    {
        auto tc = (IThreadContextDX11*)threadContext.QueryInterface(TypeHashCode<IThreadContextDX11>);
        if (tc) {
            return tc->GetUnderlying();
        }
        return nullptr;
    }

    void DeviceContext::PrepareForDestruction(IDevice* device, IPresentationChain* presentationChain)
    {
		auto immContext = device->GetImmediateContext();
        auto metalContext = Get(*immContext);
        if (metalContext) {
            metalContext->GetUnderlying()->ClearState();
            for (unsigned c=0; c<6; ++c) {
				immContext->BeginFrame(*presentationChain);
                metalContext->GetUnderlying()->Flush();
            }
        }
    }

    std::shared_ptr<DeviceContext> DeviceContext::Fork()
    {
        ID3D::Device* devRaw = nullptr;
		_underlying->GetDevice(&devRaw);
        intrusive_ptr<ID3D::Device> dev = moveptr(devRaw);

        ID3D::DeviceContext* rawContext = nullptr;
        auto hresult = dev->CreateDeferredContext(0, &rawContext);
        intrusive_ptr<ID3D::DeviceContext> context = moveptr(rawContext);
        if (!SUCCEEDED(hresult) || !context) 
            Throw(::Exceptions::BasicLabel("Failure while forking device context"));

        return std::make_shared<DeviceContext>(std::move(context));
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    CommandList::CommandList(ID3D::CommandList* underlying)
    :   _underlying(underlying)
    {
    }

    CommandList::CommandList(intrusive_ptr<ID3D::CommandList>&& underlying)
    :   _underlying(std::forward<intrusive_ptr<ID3D::CommandList>>(underlying))
    {}

////////////////////////////////////////////////////////////////////////////////////////////////////

	class RealGraphicsPipeline : public GraphicsPipeline
	{
	public:
		DepthStencilState _depthStencil;
		RasterizerState _rasterizer;
		BlendState _blend;
		Topology _topology;

		ShaderProgram _program;
		BoundClassInterfaces _boundClassInterfaces;
		intrusive_ptr<ID3D::InputLayout> _inputLayout;
		std::vector<unsigned> _vertexStrides;

		RealGraphicsPipeline(
			const DepthStencilState& depthStencil,
			const RasterizerState& rasterizer,
			const BlendState& blend,
			Topology topology,
			const ShaderProgram& program,
			const BoundClassInterfaces& boundClassInterfaces,
			const intrusive_ptr<ID3D::InputLayout>& inputLayout,
			IteratorRange<const unsigned*> vertexStrides)
		: _depthStencil(depthStencil)
		, _rasterizer(rasterizer)
		, _blend(blend)
		, _topology(topology)
		, _program(program)
		, _boundClassInterfaces(boundClassInterfaces)
		, _inputLayout(inputLayout)
		, _vertexStrides(vertexStrides.begin(), vertexStrides.end())
		{}
	};

	static uint64_t s_nextGraphicsPipelineNextGuid = 1;

	const ShaderProgram& GraphicsPipeline::GetShaderProgram() const
	{
		// All GraphicsPipeline objects on D3D are actually "RealGraphicsPipeline", so we can do this trick -- 
		// It just helps keep the internal objects of the class completely isolated
		return ((const RealGraphicsPipeline*)this)->_program;
	}

	void GraphicsPipeline::ApplyVertexBuffers(DeviceContext& context, IteratorRange<const VertexBufferView*> vertexBuffers) const never_throws
	{
		auto& rgp = *((const RealGraphicsPipeline*)this);

		ID3D::Buffer* buffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		UINT strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		UINT offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		auto vbCount = std::min(vertexBuffers.size(), rgp._vertexStrides.size());
		assert(vbCount < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT);
		for (unsigned c = 0; c < vbCount; ++c) {
			assert(vertexBuffers[c]._resource);
			auto* res = AsID3DResource(*const_cast<IResource*>(vertexBuffers[c]._resource));
			assert(QueryInterfaceCast<ID3D::Buffer>(res));
			buffers[c] = (ID3D::Buffer*)res;
			offsets[c] = vertexBuffers[c]._offset;
			strides[c] = rgp._vertexStrides[c];
		}
		const unsigned startSlot = 0;
		context.GetUnderlying()->IASetVertexBuffers(startSlot, (UINT)vbCount, buffers, strides, offsets);
		context.GetUnderlying()->IASetInputLayout(rgp._inputLayout.get());
	}

	const ::Assets::DependencyValidation& GraphicsPipeline::GetDependencyValidation() const 
	{ 
		return GetShaderProgram().GetDependencyValidation(); 
	}

	GraphicsPipeline::GraphicsPipeline()
	: _guid(s_nextGraphicsPipelineNextGuid++)
	{
	}

	GraphicsPipeline::~GraphicsPipeline()
	{
	}

	class GraphicsPipelineBuilder::Pimpl
	{
	public:
		ShaderProgram _program;
		BoundClassInterfaces _boundClassInterfaces;
		intrusive_ptr<ID3D::InputLayout> _inputLayout;
		std::vector<unsigned> _vertexStrides;

		DepthStencilDesc _depthStencil;
		RasterizationDesc _rasterization;
		AttachmentBlendDesc _blend;
		Topology _topology;

		uint64_t _depthStencilHash;
		uint64_t _rasterizationHash;
		uint64_t _blendHash;

		std::map<uint64_t, std::shared_ptr<GraphicsPipeline>> _pipelineCache;

		Pimpl()
		{
			_depthStencilHash = _depthStencil.Hash();
			_rasterizationHash = _rasterization.Hash();
			_blendHash = _blend.Hash();
			_topology = Topology::TriangleList;
		}
	};

	void GraphicsPipelineBuilder::Bind(const ShaderProgram& shaderProgram)
	{
		_pimpl->_program = shaderProgram;
		_pimpl->_boundClassInterfaces = {};
	}

	void GraphicsPipelineBuilder::Bind(const ShaderProgram& shaderProgram, const BoundClassInterfaces& dynLinkage)
	{
		_pimpl->_program = shaderProgram;
		_pimpl->_boundClassInterfaces = dynLinkage;
	}

    void GraphicsPipelineBuilder::Bind(const AttachmentBlendDesc& blendState)
	{
		_pimpl->_blend = blendState;
		_pimpl->_blendHash = blendState.Hash();
	}

    void GraphicsPipelineBuilder::Bind(const DepthStencilDesc& depthStencil)
	{
		_pimpl->_depthStencil = depthStencil;
		_pimpl->_depthStencilHash = depthStencil.Hash();
	}

    void GraphicsPipelineBuilder::Bind(Topology topology)
	{
		_pimpl->_topology = topology;
	}

    void GraphicsPipelineBuilder::Bind(const RasterizationDesc& desc)
	{
		_pimpl->_rasterization = desc;
		_pimpl->_rasterizationHash = desc.Hash();
	}

	void GraphicsPipelineBuilder::SetInputLayout(const BoundInputLayout& inputLayout)
	{
		// not critical on DX11; since we still call BoundInputLayout::Apply() to set VBs and IA state
		_pimpl->_inputLayout = inputLayout.GetUnderlying();
		_pimpl->_vertexStrides.clear();
		_pimpl->_vertexStrides.insert(_pimpl->_vertexStrides.end(), inputLayout.GetVertexStrides().begin(), inputLayout.GetVertexStrides().end());
	}

    void GraphicsPipelineBuilder::SetRenderPassConfiguration(const FrameBufferDesc& fbDesc, unsigned subPass)
	{
		// this state information not needed in DX11, since this gets configured via the FrameBuffer interface
	}

	uint64_t GraphicsPipelineBuilder::CalculateFrameBufferRelevance(const FrameBufferDesc& fbDesc, unsigned subPass)
	{
		// This function should normally filter what parts of the fbDesc are actually important to the graphics pipeline, and return
		// a hash based on that.
		// But for DX11, nothing is important, so we'll just return a constant
		return 0;
	}

    const std::shared_ptr<GraphicsPipeline>& GraphicsPipelineBuilder::CreatePipeline(ObjectFactory&)
	{
		uint64_t finalHash = HashCombine(_pimpl->_depthStencilHash, _pimpl->_rasterizationHash);
		finalHash = HashCombine(_pimpl->_blendHash, finalHash);
		finalHash = HashCombine((uint64_t)_pimpl->_topology, finalHash);
		finalHash = HashCombine(_pimpl->_program.GetGUID(), finalHash);
		finalHash = HashCombine(_pimpl->_boundClassInterfaces.GetGUID(), finalHash);

		auto i = _pimpl->_pipelineCache.find(finalHash);
		if (i == _pimpl->_pipelineCache.end()) {
			auto newPipeline = std::make_shared<RealGraphicsPipeline>(
				_pimpl->_depthStencil,
				_pimpl->_rasterization,
				_pimpl->_blend,
				_pimpl->_topology,
				_pimpl->_program,
				_pimpl->_boundClassInterfaces,
				_pimpl->_inputLayout,
				MakeIteratorRange(_pimpl->_vertexStrides));
			i = _pimpl->_pipelineCache.insert(std::make_pair(finalHash, newPipeline)).first;
		}

		return i->second;
	}

    GraphicsPipelineBuilder::GraphicsPipelineBuilder()
	{
		_pimpl = std::make_unique<Pimpl>();
	}

	GraphicsPipelineBuilder::~GraphicsPipelineBuilder()
	{}

	void DeviceContext::Bind(const GraphicsPipeline& pipeline)
	{
		// All GraphicsPipeline objects on D3D are actually "RealGraphicsPipeline"
		auto& realPipeline = (RealGraphicsPipeline&)pipeline;
		_underlying->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY(realPipeline._topology));

		if (realPipeline._boundClassInterfaces.GetGUID() == 0) {
			realPipeline._program.Apply(*this);
		} else {
			realPipeline._program.Apply(*this, realPipeline._boundClassInterfaces);
		}

		_underlying->RSSetState(realPipeline._rasterizer.GetUnderlying());

		const FLOAT blendFactors[] = {1.f, 1.f, 1.f, 1.f};
        _underlying->OMSetBlendState(realPipeline._blend.GetUnderlying(), blendFactors, 0xffffffff);
        _underlying->OMSetDepthStencilState(realPipeline._depthStencil.GetUnderlying(), _boundStencilRefValue);		// (reuse the stencil ref that was last passed to the DSS Bind() function)
		_underlying->IASetInputLayout(realPipeline._inputLayout.get());
	}

}}

namespace RenderCore { namespace Metal_DX11
{
    template void DeviceContext::Bind<1>(const ResourceList<RenderTargetView, 1>&, const DepthStencilView*);
    template void DeviceContext::Bind<2>(const ResourceList<RenderTargetView, 2>&, const DepthStencilView*);
    template void DeviceContext::Bind<3>(const ResourceList<RenderTargetView, 3>&, const DepthStencilView*);
    template void DeviceContext::Bind<4>(const ResourceList<RenderTargetView, 4>&, const DepthStencilView*);
    template void DeviceContext::Bind<1,1>(const ResourceList<RenderTargetView, 1>&, const DepthStencilView*, const ResourceList<UnorderedAccessView, 1>&);
    template void DeviceContext::Bind<1,2>(const ResourceList<RenderTargetView, 1>&, const DepthStencilView*, const ResourceList<UnorderedAccessView, 2>&);
}}
