// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineOperators.h"
#include "CommonResources.h"
#include "RenderPass.h"
#include "ParsingContext.h"
#include "CompiledLayoutPool.h"
#include "DrawablesInternal.h"
#include "DrawableDelegates.h"
#include "PipelineAcceleratorInternal.h"		// for BoundUniformsPool
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Shader.h"
#include "../Metal/ObjectFactory.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Marker.h"
#include "../../Assets/Continuation.h"
#include "../../xleres/FileList.h"

namespace Assets 
{
	uint64_t Hash64(const DependencyValidation& depVal, uint64_t seed) { return seed; }
	static std::ostream& StreamWithHashFallback(std::ostream& str, const DependencyValidation& value, bool) { return str; }
}

namespace RenderCore { namespace Techniques
{
	class FullViewportOperator : public IShaderOperator
	{
	public:
		std::shared_ptr<Metal::GraphicsPipeline> _pipeline;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		BoundUniformsPool _boundUniforms;
		UniformsStreamInterface _usi;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _predefinedPipelineLayout;

		::Assets::DependencyValidation GetDependencyValidation() const override { return _depVal; }
		::Assets::DependencyValidation _depVal;

		virtual void Draw(
			ParsingContext& parsingContext,
			const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			auto& sysUsi = parsingContext.GetUniformDelegateManager()->GetInterface();
			auto sysUsiHash = sysUsi.GetHash();
			auto& boundUniforms = _boundUniforms.Get(*_pipeline, sysUsi, _usi);

			auto& metalContext = *Metal::DeviceContext::Get(parsingContext.GetThreadContext());
			auto encoder = metalContext.BeginGraphicsEncoder(_pipelineLayout);

			ApplyUniformsGraphics(*parsingContext.GetUniformDelegateManager(), metalContext, encoder, parsingContext, boundUniforms, 0);
			if (!descSets.empty())
				boundUniforms.ApplyDescriptorSets(metalContext, encoder, descSets, 1);
			boundUniforms.ApplyLooseUniforms(metalContext, encoder, us, 1);
			
			encoder.Draw(*_pipeline, 4);
		}

		virtual void Draw(
			IThreadContext& threadContext,
			const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			auto& boundUniforms = _boundUniforms.Get(*_pipeline, _usi);	// maybe silly to do a lookup here because it's the same every time

			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder(_pipelineLayout);

			if (!descSets.empty())
				boundUniforms.ApplyDescriptorSets(metalContext, encoder, descSets, 0);
			boundUniforms.ApplyLooseUniforms(metalContext, encoder, us, 0);
			
			encoder.Draw(*_pipeline, 4);
		}

		virtual const Assets::PredefinedPipelineLayout& GetPredefinedPipelineLayout() const override
		{
			if (!_predefinedPipelineLayout)
				Throw(std::runtime_error("Cannot get a predefined pipeline layout from a shader operator that was constructed directly from a compiled pipeline layout"));
			return *_predefinedPipelineLayout;
		}

		// ICompiledPipelineLayout
		static void ConstructToPromise(
			std::promise<std::shared_ptr<FullViewportOperator>>&& promise,
			const std::shared_ptr<PipelineCollection>& pool,
			const std::shared_ptr<GraphicsPipelineDesc>& pipelineDesc,
			const ParameterBox& selectors,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			const FrameBufferTarget& fbTarget,
			const UniformsStreamInterface& usi)
		{
			assert(pool);
			VertexInputStates vInputStates { {}, {}, Topology::TriangleStrip };
			const ParameterBox* selectorList[] { &selectors };
			auto pipelineFuture = std::make_shared<::Assets::Marker<Techniques::GraphicsPipelineAndLayout>>();
			pool->CreateGraphicsPipeline(pipelineFuture->AdoptPromise(), pipelineLayout, pipelineDesc, MakeIteratorRange(selectorList), vInputStates, fbTarget);
			::Assets::WhenAll(pipelineFuture).ThenConstructToPromise(
				std::move(promise),
				[pipelineLayout=pipelineLayout, usi=usi](auto pipelineAndLayout) {
					auto op = std::make_shared<FullViewportOperator>();
					op->_usi = std::move(usi);
					op->_depVal = pipelineAndLayout.GetDependencyValidation();
					op->_pipelineLayout = std::move(pipelineAndLayout._layout);
					op->_pipeline = std::move(pipelineAndLayout._pipeline);
					return op;
				});
		}

		// just auto pipeline layout
		static void ConstructToPromise(
			std::promise<std::shared_ptr<FullViewportOperator>>&& promise,
			const std::shared_ptr<PipelineCollection>& pool,
			const std::shared_ptr<GraphicsPipelineDesc>& pipelineDesc,
			const ParameterBox& selectors,
			const FrameBufferTarget& fbTarget,
			const UniformsStreamInterface& usi)
		{
			assert(pool);
			VertexInputStates vInputStates { {}, {}, Topology::TriangleStrip };
			const ParameterBox* selectorList[] { &selectors };
			auto pipelineFuture = std::make_shared<::Assets::Marker<Techniques::GraphicsPipelineAndLayout>>();
			pool->CreateGraphicsPipeline(pipelineFuture->AdoptPromise(), {}, pipelineDesc, MakeIteratorRange(selectorList), vInputStates, fbTarget);
			::Assets::WhenAll(pipelineFuture).ThenConstructToPromise(
				std::move(promise),
				[usi=usi](auto pipelineAndLayout) {
					auto op = std::make_shared<FullViewportOperator>();
					op->_usi = std::move(usi);
					op->_depVal = pipelineAndLayout.GetDependencyValidation();
					op->_pipelineLayout = std::move(pipelineAndLayout._layout);
					op->_pipeline = std::move(pipelineAndLayout._pipeline);
					return op;
				});
		}

		// pipeline layout asset (by name)
		static void ConstructToPromise(
			std::promise<std::shared_ptr<FullViewportOperator>>&& promise,
			const std::shared_ptr<PipelineCollection>& pool,
			const std::shared_ptr<GraphicsPipelineDesc>& pipelineDesc,
			const ParameterBox& selectors,
			StringSection<> pipelineLayoutAssetName,
			const FrameBufferTarget& fbTarget,
			const UniformsStreamInterface& usi)
		{
			assert(pool);
			auto futurePipelineLayout = ::Assets::MakeAssetPtr<RenderCore::Assets::PredefinedPipelineLayout>(pipelineLayoutAssetName);
			::Assets::WhenAll(std::move(futurePipelineLayout)).ThenConstructToPromise(
				std::move(promise),
				[pool, selectors, usi, plan=Hash64(pipelineLayoutAssetName), pipelineDesc, fbTarget](auto&& promise, const auto& predefinedPipelineLayout) {
					
					auto pipelineFuture = std::make_shared<::Assets::Marker<Techniques::GraphicsPipelineAndLayout>>();
					const ParameterBox* selectorList[] { &selectors };
					VertexInputStates vInputStates { {}, {}, Topology::TriangleStrip };
					pool->CreateGraphicsPipeline(pipelineFuture->AdoptPromise(), {predefinedPipelineLayout, plan}, pipelineDesc, MakeIteratorRange(selectorList), vInputStates, fbTarget);

					::Assets::WhenAll(pipelineFuture).ThenConstructToPromise(
						std::move(promise),
						[predefinedPipelineLayout, usi=usi](auto pipelineAndLayout) {
							auto op = std::make_shared<FullViewportOperator>();
							op->_usi = std::move(usi);
							::Assets::DependencyValidationMarker depVals[] { pipelineAndLayout.GetDependencyValidation(), predefinedPipelineLayout->GetDependencyValidation() };
							op->_depVal = ::Assets::GetDepValSys().MakeOrReuse(MakeIteratorRange(depVals));
							op->_pipelineLayout = std::move(pipelineAndLayout._layout);
							op->_pipeline = std::move(pipelineAndLayout._pipeline);
							op->_predefinedPipelineLayout = predefinedPipelineLayout;
							return op;
						});
				});
		}
	};

	static std::shared_ptr<GraphicsPipelineDesc> CreatePipelineDesc(StringSection<> pixelShader, FullViewportOperatorSubType subType, const PixelOutputStates& po)
	{
		auto pipelineDesc = std::make_shared<GraphicsPipelineDesc>();
		pipelineDesc->_shaders[(unsigned)ShaderStage::Pixel] = pixelShader.AsString();
		if (subType == FullViewportOperatorSubType::DisableDepth) {
			pipelineDesc->_shaders[(unsigned)ShaderStage::Vertex] = BASIC2D_VERTEX_HLSL ":fullscreen_viewfrustumvector";
		} else {
			assert(subType == FullViewportOperatorSubType::MaxDepth);
			pipelineDesc->_shaders[(unsigned)ShaderStage::Vertex] = BASIC2D_VERTEX_HLSL ":fullscreen_viewfrustumvector_deep";
		}

		pipelineDesc->_depthStencil = po._depthStencilState;
		pipelineDesc->_rasterization = po._rasterizationState;
		pipelineDesc->_blend = {po._attachmentBlendStates.begin(), po._attachmentBlendStates.end()};
		while (pipelineDesc->_blend.size() < po._fbDesc->GetSubpasses()[po._subpassIdx].GetOutputs().size())
			pipelineDesc->_blend.push_back(AttachmentBlendDesc{});		// fill in remaining with defaults
		return pipelineDesc;
	}

	::Assets::PtrToMarkerPtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		FullViewportOperatorSubType subType,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const PixelOutputStates& po,
		const UniformsStreamInterface& usi)
	{
		assert(!pixelShader.IsEmpty());
		auto pipelineDesc = CreatePipelineDesc(pixelShader, subType, po);
		auto op = ::Assets::MakeAssetMarkerPtr<FullViewportOperator>(pool, pipelineDesc, selectors, pipelineLayout, FrameBufferTarget{po._fbDesc, po._subpassIdx}, usi);
		return *reinterpret_cast<::Assets::PtrToMarkerPtr<IShaderOperator>*>(&op);
	}

	::Assets::PtrToMarkerPtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		FullViewportOperatorSubType subType,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAssetName,
		const PixelOutputStates& po,
		const UniformsStreamInterface& usi)
	{
		assert(!pixelShader.IsEmpty());
		auto pipelineDesc = CreatePipelineDesc(pixelShader, subType, po);
		auto op = ::Assets::MakeAssetMarkerPtr<FullViewportOperator>(pool, pipelineDesc, selectors, pipelineLayoutAssetName, FrameBufferTarget{po._fbDesc, po._subpassIdx}, usi);
		return *reinterpret_cast<::Assets::PtrToMarkerPtr<IShaderOperator>*>(&op);
	}

	class ComputeOperator : public Techniques::IComputeShaderOperator
	{
	public:
		std::shared_ptr<Metal::ComputePipeline> _pipeline;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		BoundUniformsPool _boundUniforms;
		UniformsStreamInterface _usi;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _predefinedPipelineLayout;

		::Assets::DependencyValidation GetDependencyValidation() const override { return _depVal; }
		::Assets::DependencyValidation _depVal;

		void BeginDispatchesInternal(
			ParsingContext& parsingContext,
			const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets,
			uint64_t pushConstantsBinding = 0)
		{
			assert(!_betweenBeginEnd);
			auto& sysUsi = parsingContext.GetUniformDelegateManager()->GetInterface();
			UniformsStreamInterface pushConstantsUSI;
			if (pushConstantsBinding) pushConstantsUSI.BindImmediateData(0, pushConstantsBinding);
			auto& boundUniforms = _boundUniforms.Get(*_pipeline, sysUsi, _usi, pushConstantsUSI);

			auto& metalContext = *Metal::DeviceContext::Get(parsingContext.GetThreadContext());
			_activeEncoder = {};
			auto newEncoder = metalContext.BeginComputeEncoder(_pipelineLayout);
			_capturedStates = {};
			newEncoder.BeginStateCapture(_capturedStates);

			ApplyUniformsCompute(*parsingContext.GetUniformDelegateManager(), metalContext, newEncoder, parsingContext, boundUniforms, 0);
			if (!descSets.empty())
				boundUniforms.ApplyDescriptorSets(metalContext, newEncoder, descSets, 1);
			boundUniforms.ApplyLooseUniforms(metalContext, newEncoder, us, 1);
			_activeEncoder = std::move(newEncoder);
			_betweenBeginEnd = true;
		}

		void BeginDispatchesInternal(IThreadContext& threadContext, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets, uint64_t pushConstantsBinding = 0)
		{
			assert(!_betweenBeginEnd);
			UniformsStreamInterface pushConstantsUSI;
			if (pushConstantsBinding) pushConstantsUSI.BindImmediateData(0, pushConstantsBinding);
			auto& boundUniforms = _boundUniforms.Get(*_pipeline, {}, _usi, pushConstantsUSI);
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			_activeEncoder = {};
			auto newEncoder = metalContext.BeginComputeEncoder(_pipelineLayout);
			_capturedStates = {};
			newEncoder.BeginStateCapture(_capturedStates);
			if (!descSets.empty())
				boundUniforms.ApplyDescriptorSets(metalContext, newEncoder, descSets, 1);
			boundUniforms.ApplyLooseUniforms(metalContext, newEncoder, us, 1);
			_activeEncoder = std::move(newEncoder);
			_betweenBeginEnd = true;
		}

		virtual DispatchGroupHelper BeginDispatches(
			ParsingContext& parsingContext,
			const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets,
			uint64_t pushConstantsBinding = 0) override
		{
			BeginDispatchesInternal(parsingContext, us, descSets, pushConstantsBinding);
			return DispatchGroupHelper{this};
		}

		virtual DispatchGroupHelper BeginDispatches(IThreadContext& threadContext, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets, uint64_t pushConstantsBinding = 0) override
		{
			BeginDispatchesInternal(threadContext, us, descSets, pushConstantsBinding);
			return DispatchGroupHelper{this};
		}

		virtual void EndDispatches() override
		{
			assert(_betweenBeginEnd);
			_activeEncoder = {};
			_betweenBeginEnd = false;
		}

		virtual void Dispatch(
			ParsingContext& parsingContext,
			unsigned countX, unsigned countY, unsigned countZ, 
			const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			TRY {
				BeginDispatchesInternal(parsingContext, us, descSets);
				_activeEncoder.Dispatch(*_pipeline, countX, countY, countZ);
			} CATCH(...) {
				_activeEncoder = {};
				_betweenBeginEnd = false;
				throw;
			} CATCH_END
			_activeEncoder = {};
			_betweenBeginEnd = false;
		}

		virtual void Dispatch(IThreadContext& threadContext, unsigned countX, unsigned countY, unsigned countZ, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			TRY {
				BeginDispatchesInternal(threadContext, us, descSets);
				_activeEncoder.Dispatch(*_pipeline, countX, countY, countZ);
			} CATCH(...) {
				_activeEncoder = {};
				_betweenBeginEnd = false;
				throw;
			} CATCH_END
			_activeEncoder = {};
			_betweenBeginEnd = false;
		}

		virtual void Dispatch(unsigned countX, unsigned countY, unsigned countZ, IteratorRange<const void*> pushConstants) override
		{
			assert(_betweenBeginEnd);
			if (!pushConstants.empty())
				_activeEncoder.PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstants);
			_activeEncoder.Dispatch(*_pipeline, countX, countY, countZ);
		}

		virtual void DispatchIndirect(const IResource& indirectArgsBuffer, unsigned offset, IteratorRange<const void*> pushConstants) override
		{
			assert(_betweenBeginEnd);
			_activeEncoder.DispatchIndirect(*_pipeline, indirectArgsBuffer, offset);
		}

		virtual const Assets::PredefinedPipelineLayout& GetPredefinedPipelineLayout() const override
		{
			if (!_predefinedPipelineLayout)
				Throw(std::runtime_error("Cannot get a predefined pipeline layout from a shader operator that was constructed directly from a compiled pipeline layout"));
			return *_predefinedPipelineLayout;
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ComputeOperator>>&& promise,
			const std::shared_ptr<PipelineCollection>& pool,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			StringSection<> computeShader,
			const ParameterBox& selectors,
			const UniformsStreamInterface& usi)
		{
			assert(pool);
			const ParameterBox* selectorList[] { &selectors };
			auto pipelineFuture = std::make_shared<::Assets::Marker<Techniques::ComputePipelineAndLayout>>();
			pool->CreateComputePipeline(pipelineFuture->AdoptPromise(), pipelineLayout, computeShader, MakeIteratorRange(selectorList));
			::Assets::WhenAll(pipelineFuture).ThenConstructToPromise(
				std::move(promise),
				[usi=usi, pipelineLayout](auto pipelineAndLayout) {
					auto op = std::make_shared<ComputeOperator>();
					op->_usi = std::move(usi);
					op->_depVal = pipelineAndLayout.GetDependencyValidation();
					op->_pipelineLayout = pipelineAndLayout._layout;
					op->_pipeline = pipelineAndLayout._pipeline;
					assert(op->_pipeline);
					return op;
				});
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ComputeOperator>>&& promise,
			const std::shared_ptr<PipelineCollection>& pool,
			StringSection<> computeShader,
			const ParameterBox& selectors,
			const UniformsStreamInterface& usi)
		{
			assert(pool);
			const ParameterBox* selectorList[] { &selectors };
			auto pipelineFuture = std::make_shared<::Assets::Marker<Techniques::ComputePipelineAndLayout>>();
			pool->CreateComputePipeline(pipelineFuture->AdoptPromise(), {}, computeShader, MakeIteratorRange(selectorList));
			::Assets::WhenAll(pipelineFuture).ThenConstructToPromise(
				std::move(promise),
				[usi=usi](auto pipelineAndLayout) {
					auto op = std::make_shared<ComputeOperator>();
					op->_usi = std::move(usi);
					op->_depVal = pipelineAndLayout.GetDependencyValidation();
					op->_pipelineLayout = std::move(pipelineAndLayout._layout);
					op->_pipeline = std::move(pipelineAndLayout._pipeline);
					return op;
				});
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ComputeOperator>>&& promise,
			const std::shared_ptr<PipelineCollection>& pool,
			StringSection<> pipelineLayoutAssetName,
			StringSection<> computeShader,
			const ParameterBox& selectors,
			const UniformsStreamInterface& usi)
		{
			assert(pool);
			auto futurePipelineLayout = ::Assets::MakeAssetPtr<RenderCore::Assets::PredefinedPipelineLayout>(pipelineLayoutAssetName);
			::Assets::WhenAll(std::move(futurePipelineLayout)).ThenConstructToPromise(
				std::move(promise),
				[pool, selectors, plan=Hash64(pipelineLayoutAssetName), computeShader=computeShader.AsString(), usi](auto&& promise, auto pipelineLayout) mutable {
					const ParameterBox* selectorList[] { &selectors };
					auto pipelineFuture = std::make_shared<::Assets::Marker<Techniques::ComputePipelineAndLayout>>();
					pool->CreateComputePipeline(pipelineFuture->AdoptPromise(), {pipelineLayout, plan}, computeShader, MakeIteratorRange(selectorList));

					::Assets::WhenAll(pipelineFuture).ThenConstructToPromise(
						std::move(promise),
						[usi=std::move(usi)](auto pipelineAndLayout) mutable {
							auto op = std::make_shared<ComputeOperator>();
							op->_usi = std::move(usi);
							op->_depVal = pipelineAndLayout.GetDependencyValidation();
							op->_pipelineLayout = std::move(pipelineAndLayout._layout);
							op->_pipeline = std::move(pipelineAndLayout._pipeline);
							return op;
						});
				});
		}

		RenderCore::Metal::ComputeEncoder _activeEncoder;
		RenderCore::Metal::CapturedStates _capturedStates;
		bool _betweenBeginEnd = false;
	};

	::Assets::PtrToMarkerPtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		const UniformsStreamInterface& usi)
	{
		assert(pipelineLayout);
		assert(!computeShader.IsEmpty());
		auto op = ::Assets::MakeAssetMarkerPtr<ComputeOperator>(pool, pipelineLayout, computeShader, selectors, usi);
		return *reinterpret_cast<::Assets::PtrToMarkerPtr<IComputeShaderOperator>*>(&op);
	}

	::Assets::PtrToMarkerPtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAssetName,
		const UniformsStreamInterface& usi)
	{
		auto op = ::Assets::MakeAssetMarkerPtr<ComputeOperator>(
			pool, pipelineLayoutAssetName,
			computeShader, selectors, usi);
		return *reinterpret_cast<::Assets::PtrToMarkerPtr<IComputeShaderOperator>*>(&op);
	}

	::Assets::PtrToMarkerPtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		const UniformsStreamInterface& usi)
	{
		auto op = ::Assets::MakeAssetMarkerPtr<ComputeOperator>(pool, computeShader, selectors, usi);
		return *reinterpret_cast<::Assets::PtrToMarkerPtr<IComputeShaderOperator>*>(&op);
	}

	uint64_t PixelOutputStates::GetHash() const 
	{
		assert(_subpassIdx < _fbDesc->GetSubpasses().size());
		auto result = RenderCore::Metal::GraphicsPipelineBuilder::CalculateFrameBufferRelevance(*_fbDesc, _subpassIdx); 
		result = HashCombine(_depthStencilState.HashDepthAspect(), result);
		result = HashCombine(_depthStencilState.HashStencilAspect(), result);
		result = HashCombine(_rasterizationState.Hash(), result);
		auto relevantBlendStateCount = _fbDesc->GetSubpasses()[_subpassIdx].GetOutputs().size();
		unsigned c=0;
		for (; c<std::min(relevantBlendStateCount, _attachmentBlendStates.size()); ++c)
			result = HashCombine(_attachmentBlendStates[c].Hash(), result);
		for (; c<relevantBlendStateCount; ++c)
			result = HashCombine(AttachmentBlendDesc{}.Hash(), result);		// fill remainder with defaults
		return result;
	}

	void PixelOutputStates::Bind(const FrameBufferDesc& fbDesc, unsigned subpassIdx) 
	{ 
		_fbDesc = &fbDesc; 
		_subpassIdx = subpassIdx; 
		assert(_subpassIdx < _fbDesc->GetSubpasses().size());
	}

	void PixelOutputStates::Bind(const RenderPassInstance& rpi)
	{
		Bind(rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
	}

	void PixelOutputStates::Bind(const DepthStencilDesc& depthStencilState)
	{
		_depthStencilState = depthStencilState;
	}

	void PixelOutputStates::Bind(const RasterizationDesc& rasterizationState)
	{
		_rasterizationState = rasterizationState;
	}

	void PixelOutputStates::Bind(IteratorRange<const AttachmentBlendDesc*> blendStates)
	{
		_attachmentBlendStates = blendStates;
	}

	IShaderOperator::~IShaderOperator() {}
	IComputeShaderOperator::~IComputeShaderOperator() {}

}}
