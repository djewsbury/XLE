// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineOperators.h"
#include "RenderPass.h"
#include "ParsingContext.h"
#include "PipelineLayoutDelegate.h"
#include "DrawablesInternal.h"
#include "DrawableDelegates.h"
#include "PipelineAcceleratorInternal.h"		// for BoundUniformsPool
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Shader.h"
#include "../Metal/ObjectFactory.h"
#include "../ShaderService.h"		// for ShaderCompileResourceName::CalculateHash
#include "../../Assets/Assets.h"
#include "../../Assets/Marker.h"
#include "../../Assets/Continuation.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace Techniques
{
	static const UniformsStreamInterface s_usiNull;

	class FullViewportOperator : public IShaderOperator
	{
	public:
		std::shared_ptr<Metal::GraphicsPipeline> _pipeline;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		BoundUniformsPool _boundUniforms;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _predefinedPipelineLayout;

		::Assets::DependencyValidation GetDependencyValidation() const override { return _depVal; }
		::Assets::DependencyValidation _depVal;

		virtual void Draw(
			ParsingContext& parsingContext,
			const UniformsStreamInterface* usi, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			auto& sysUsi = parsingContext.GetUniformDelegateManager()->GetInterfaceGraphics();
			auto& boundUniforms = usi ? _boundUniforms.Get(*_pipeline, sysUsi, *usi) : _boundUniforms.Get(*_pipeline, sysUsi);

			auto& metalContext = *Metal::DeviceContext::Get(parsingContext.GetThreadContext());
			auto encoder = metalContext.BeginGraphicsEncoder(*_pipelineLayout);

			// A little awkward, but mirroring what we do in Drawables::Draw(), we set the viewport immediately after beginning
			// the encoder. This might be better if we made this a DeviceContext function, rather than an encoder function
			//	-- and just called it after beginning the render pass
			auto viewport = parsingContext.GetViewport();
			Rect2D scissorRect { (int)viewport._x, (int)viewport._y, (unsigned)viewport._width, (unsigned)viewport._height };
			encoder.Bind(MakeIteratorRange(&viewport, &viewport+1), MakeIteratorRange(&scissorRect, &scissorRect+1));

			ApplyUniformsGraphics(*parsingContext.GetUniformDelegateManager(), metalContext, encoder, parsingContext, boundUniforms, 0);
			if (!descSets.empty())
				boundUniforms.ApplyDescriptorSets(metalContext, encoder, descSets, 1);
			boundUniforms.ApplyLooseUniforms(metalContext, encoder, us, 1);
			
			encoder.Draw(*_pipeline, 4);
		}

		virtual void Draw(
			IThreadContext& threadContext,
			const UniformsStreamInterface* usi, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder(*_pipelineLayout);

			if (usi) {
				auto& boundUniforms = _boundUniforms.Get(*_pipeline, *usi);
				if (!descSets.empty())
					boundUniforms.ApplyDescriptorSets(metalContext, encoder, descSets, 0);
				boundUniforms.ApplyLooseUniforms(metalContext, encoder, us, 0);
			} else {
				auto& boundUniforms = _boundUniforms.Get(*_pipeline, s_usiNull);
				boundUniforms.ApplyLooseUniforms(metalContext, encoder, us, 0);
			}
			
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
			const FrameBufferTarget& fbTarget)
		{
			assert(pool);
			VertexInputStates vInputStates { {}, {}, Topology::TriangleStrip };
			const ParameterBox* selectorList[] { &selectors };
			auto pipelineFuture = std::make_shared<::Assets::Marker<Techniques::GraphicsPipelineAndLayout>>();
			pool->CreateGraphicsPipeline(pipelineFuture->AdoptPromise(), pipelineLayout, pipelineDesc, MakeIteratorRange(selectorList), vInputStates, fbTarget);
			::Assets::WhenAll(pipelineFuture).ThenConstructToPromise(
				std::move(promise),
				[pipelineLayout=pipelineLayout](auto pipelineAndLayout) {
					auto op = std::make_shared<FullViewportOperator>();
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
			const FrameBufferTarget& fbTarget)
		{
			assert(pool);
			VertexInputStates vInputStates { {}, {}, Topology::TriangleStrip };
			const ParameterBox* selectorList[] { &selectors };
			auto pipelineFuture = std::make_shared<::Assets::Marker<Techniques::GraphicsPipelineAndLayout>>();
			pool->CreateGraphicsPipeline(pipelineFuture->AdoptPromise(), {}, pipelineDesc, MakeIteratorRange(selectorList), vInputStates, fbTarget);
			::Assets::WhenAll(pipelineFuture).ThenConstructToPromise(
				std::move(promise),
				[](auto pipelineAndLayout) {
					auto op = std::make_shared<FullViewportOperator>();
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
			const FrameBufferTarget& fbTarget)
		{
			assert(pool);
			auto futurePipelineLayout = ::Assets::GetAssetFuturePtr<RenderCore::Assets::PredefinedPipelineLayout>(pipelineLayoutAssetName);
			::Assets::WhenAll(std::move(futurePipelineLayout)).ThenConstructToPromise(
				std::move(promise),
				[pool, selectors, plname=pipelineLayoutAssetName.AsString(), pipelineDesc, fbDesc=*fbTarget._fbDesc, spIdx=fbTarget._subpassIdx](auto&& promise, const auto& predefinedPipelineLayout) {
					
					auto pipelineFuture = std::make_shared<::Assets::Marker<Techniques::GraphicsPipelineAndLayout>>();
					const ParameterBox* selectorList[] { &selectors };
					VertexInputStates vInputStates { {}, {}, Topology::TriangleStrip };
					pool->CreateGraphicsPipeline(pipelineFuture->AdoptPromise(), {predefinedPipelineLayout, Hash64(plname), std::string{plname}}, pipelineDesc, MakeIteratorRange(selectorList), vInputStates, FrameBufferTarget{&fbDesc, spIdx});

					::Assets::WhenAll(pipelineFuture).ThenConstructToPromise(
						std::move(promise),
						[predefinedPipelineLayout](auto pipelineAndLayout) {
							auto op = std::make_shared<FullViewportOperator>();
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
		pipelineDesc->_shaders[(unsigned)ShaderStage::Pixel] = MakeShaderCompileResourceName(pixelShader);
		if (subType == FullViewportOperatorSubType::DisableDepth) {
			pipelineDesc->_shaders[(unsigned)ShaderStage::Vertex] = ShaderCompileResourceName{BASIC2D_VERTEX_HLSL, "fullscreen_viewfrustumvector"};
		} else {
			assert(subType == FullViewportOperatorSubType::MaxDepth);
			pipelineDesc->_shaders[(unsigned)ShaderStage::Vertex] = ShaderCompileResourceName{BASIC2D_VERTEX_HLSL, "fullscreen_viewfrustumvector_deep"};
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
		const PixelOutputStates& po)
	{
		assert(!pixelShader.IsEmpty());
		auto pipelineDesc = CreatePipelineDesc(pixelShader, subType, po);
		auto op = ::Assets::GetAssetMarkerPtr<FullViewportOperator>(pool, pipelineDesc, selectors, pipelineLayout, FrameBufferTarget{po._fbDesc, po._subpassIdx});
		return *reinterpret_cast<::Assets::PtrToMarkerPtr<IShaderOperator>*>(&op);
	}

	::Assets::PtrToMarkerPtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		FullViewportOperatorSubType subType,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAssetName,
		const PixelOutputStates& po)
	{
		assert(!pixelShader.IsEmpty());
		auto pipelineDesc = CreatePipelineDesc(pixelShader, subType, po);
		auto op = ::Assets::GetAssetMarkerPtr<FullViewportOperator>(pool, pipelineDesc, selectors, pipelineLayoutAssetName, FrameBufferTarget{po._fbDesc, po._subpassIdx});
		return *reinterpret_cast<::Assets::PtrToMarkerPtr<IShaderOperator>*>(&op);
	}

	class ComputeOperator : public Techniques::IComputeShaderOperator
	{
	public:
		std::shared_ptr<Metal::ComputePipeline> _pipeline;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		BoundUniformsPool _boundUniforms;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _predefinedPipelineLayout;

		::Assets::DependencyValidation GetDependencyValidation() const override { return _depVal; }
		::Assets::DependencyValidation _depVal;
		DEBUG_ONLY(unsigned _usiCount = 1);

		void BeginDispatchesInternal(
			ParsingContext& parsingContext,
			const UniformsStreamInterface* usi, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets,
			uint64_t pushConstantsBinding = 0)
		{
			assert(!_betweenBeginEnd);
			DEBUG_ONLY(assert(_usiCount == 1));
			auto& sysUsi = parsingContext.GetUniformDelegateManager()->GetInterfaceCompute();
			UniformsStreamInterface pushConstantsUSI;
			if (pushConstantsBinding) pushConstantsUSI.BindImmediateData(0, pushConstantsBinding);
			auto& boundUniforms = _boundUniforms.Get(*_pipeline, sysUsi, usi?*usi:s_usiNull, pushConstantsUSI);

			auto& metalContext = *Metal::DeviceContext::Get(parsingContext.GetThreadContext());
			_activeEncoder = {};
			auto newEncoder = metalContext.BeginComputeEncoder(*_pipelineLayout);
			_capturedStates = {};
			newEncoder.BeginStateCapture(_capturedStates);

			ApplyUniformsCompute(*parsingContext.GetUniformDelegateManager(), metalContext, newEncoder, parsingContext, boundUniforms, 0);
			if (!descSets.empty())
				boundUniforms.ApplyDescriptorSets(metalContext, newEncoder, descSets, 1);
			boundUniforms.ApplyLooseUniforms(metalContext, newEncoder, us, 1);
			_activeEncoder = std::move(newEncoder);
			_betweenBeginEnd = true;
		}

		void BeginDispatchesInternal(
			IThreadContext& threadContext, 
			const UniformsStreamInterface* usi, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets, 
			uint64_t pushConstantsBinding = 0)
		{
			assert(!_betweenBeginEnd);
			DEBUG_ONLY(assert(_usiCount == 1));
			UniformsStreamInterface pushConstantsUSI;
			if (pushConstantsBinding) pushConstantsUSI.BindImmediateData(0, pushConstantsBinding);
			auto& boundUniforms = _boundUniforms.Get(*_pipeline, {}, usi?*usi:s_usiNull, pushConstantsUSI);
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			_activeEncoder = {};
			auto newEncoder = metalContext.BeginComputeEncoder(*_pipelineLayout);
			_capturedStates = {};
			newEncoder.BeginStateCapture(_capturedStates);
			if (!descSets.empty())
				boundUniforms.ApplyDescriptorSets(metalContext, newEncoder, descSets, 1);
			boundUniforms.ApplyLooseUniforms(metalContext, newEncoder, us, 1);
			_activeEncoder = std::move(newEncoder);
			_betweenBeginEnd = true;
		}

		void BeginDispatchesInternal(
			ParsingContext& parsingContext,
			const UniformsStreamInterface* usi0, const UniformsStream& us0,
			const UniformsStreamInterface* usi1, const UniformsStream& us1,
			IteratorRange<const IDescriptorSet* const*> descSets,
			uint64_t pushConstantsBinding = 0)
		{
			assert(usi0); assert(usi1);		// if you're using this variant, you should pass both
			assert(!_betweenBeginEnd);
			DEBUG_ONLY(assert(_usiCount == 2));
			auto& sysUsi = parsingContext.GetUniformDelegateManager()->GetInterfaceCompute();
			UniformsStreamInterface pushConstantsUSI;
			if (pushConstantsBinding) pushConstantsUSI.BindImmediateData(0, pushConstantsBinding);
			auto& boundUniforms = _boundUniforms.Get(*_pipeline, sysUsi, *usi0, *usi1, pushConstantsUSI);

			auto& metalContext = *Metal::DeviceContext::Get(parsingContext.GetThreadContext());
			_activeEncoder = {};
			auto newEncoder = metalContext.BeginComputeEncoder(*_pipelineLayout);
			_capturedStates = {};
			newEncoder.BeginStateCapture(_capturedStates);

			ApplyUniformsCompute(*parsingContext.GetUniformDelegateManager(), metalContext, newEncoder, parsingContext, boundUniforms, 0);
			if (!descSets.empty())
				boundUniforms.ApplyDescriptorSets(metalContext, newEncoder, descSets, 1);
			boundUniforms.ApplyLooseUniforms(metalContext, newEncoder, us0, 1);
			boundUniforms.ApplyLooseUniforms(metalContext, newEncoder, us1, 2);
			_activeEncoder = std::move(newEncoder);
			_betweenBeginEnd = true;
		}

		void BeginDispatchesInternal(
			IThreadContext& threadContext, 
			const UniformsStreamInterface* usi0, const UniformsStream& us0,
			const UniformsStreamInterface* usi1, const UniformsStream& us1,
			IteratorRange<const IDescriptorSet* const*> descSets, 
			uint64_t pushConstantsBinding = 0)
		{
			assert(usi0); assert(usi1);		// if you're using this variant, you should pass both
			assert(!_betweenBeginEnd);
			DEBUG_ONLY(assert(_usiCount == 2));
			UniformsStreamInterface pushConstantsUSI;
			if (pushConstantsBinding) pushConstantsUSI.BindImmediateData(0, pushConstantsBinding);
			auto& boundUniforms = _boundUniforms.Get(*_pipeline, {}, *usi0, *usi1, pushConstantsUSI);
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			_activeEncoder = {};
			auto newEncoder = metalContext.BeginComputeEncoder(*_pipelineLayout);
			_capturedStates = {};
			newEncoder.BeginStateCapture(_capturedStates);
			if (!descSets.empty())
				boundUniforms.ApplyDescriptorSets(metalContext, newEncoder, descSets, 1);
			boundUniforms.ApplyLooseUniforms(metalContext, newEncoder, us0, 1);
			boundUniforms.ApplyLooseUniforms(metalContext, newEncoder, us1, 2);
			_activeEncoder = std::move(newEncoder);
			_betweenBeginEnd = true;
		}

		virtual DispatchGroupHelper BeginDispatches(
			ParsingContext& parsingContext,
			const UniformsStreamInterface* usi, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets,
			uint64_t pushConstantsBinding = 0) override
		{
			BeginDispatchesInternal(parsingContext, usi, us, descSets, pushConstantsBinding);
			return DispatchGroupHelper{this};
		}

		virtual DispatchGroupHelper BeginDispatches(IThreadContext& threadContext, const UniformsStreamInterface* usi, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets, uint64_t pushConstantsBinding = 0) override
		{
			BeginDispatchesInternal(threadContext, usi, us, descSets, pushConstantsBinding);
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
			const UniformsStreamInterface* usi, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			TRY {
				BeginDispatchesInternal(parsingContext, usi, us, descSets);
				_activeEncoder.Dispatch(*_pipeline, countX, countY, countZ);
			} CATCH(...) {
				_activeEncoder = {};
				_betweenBeginEnd = false;
				throw;
			} CATCH_END
			_activeEncoder = {};
			_betweenBeginEnd = false;
		}

		virtual void Dispatch(
			IThreadContext& threadContext,
			unsigned countX, unsigned countY, unsigned countZ,
			const UniformsStreamInterface* usi, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			TRY {
				BeginDispatchesInternal(threadContext, usi, us, descSets);
				_activeEncoder.Dispatch(*_pipeline, countX, countY, countZ);
			} CATCH(...) {
				_activeEncoder = {};
				_betweenBeginEnd = false;
				throw;
			} CATCH_END
			_activeEncoder = {};
			_betweenBeginEnd = false;
		}

		virtual void Dispatch(
			ParsingContext& parsingContext,
			unsigned countX, unsigned countY, unsigned countZ, 
			const UniformsStreamInterface* usi0, const UniformsStream& us0,
			const UniformsStreamInterface* usi1, const UniformsStream& us1,
			IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			TRY {
				BeginDispatchesInternal(parsingContext, usi0, us0, usi1, us1, descSets);
				_activeEncoder.Dispatch(*_pipeline, countX, countY, countZ);
			} CATCH(...) {
				_activeEncoder = {};
				_betweenBeginEnd = false;
				throw;
			} CATCH_END
			_activeEncoder = {};
			_betweenBeginEnd = false;
		}

		virtual void Dispatch(
			IThreadContext& threadContext,
			unsigned countX, unsigned countY, unsigned countZ,
			const UniformsStreamInterface* usi0, const UniformsStream& us0,
			const UniformsStreamInterface* usi1, const UniformsStream& us1,
			IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			TRY {
				BeginDispatchesInternal(threadContext, usi0, us0, usi1, us1, descSets);
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
			PipelineLayoutOptions&& pipelineLayout,
			const Internal::ShaderVariant& computeShader,
			const ParameterBox& selectors)
		{
			assert(pool);
			const ParameterBox* selectorList[] { &selectors };
			auto pipelineFuture = std::make_shared<::Assets::Marker<Techniques::ComputePipelineAndLayout>>();
			pool->CreateComputePipeline(pipelineFuture->AdoptPromise(), std::move(pipelineLayout), computeShader, MakeIteratorRange(selectorList));
			::Assets::WhenAll(pipelineFuture).ThenConstructToPromise(
				std::move(promise),
				[pipelineLayout](auto pipelineAndLayout) {
					auto op = std::make_shared<ComputeOperator>();
					op->_depVal = pipelineAndLayout.GetDependencyValidation();
					op->_pipelineLayout = std::move(pipelineAndLayout._layout);
					op->_pipeline = std::move(pipelineAndLayout._pipeline);
					DEBUG_ONLY(op->_usiCount = 1);
					assert(op->_pipeline);
					return op;
				});
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ComputeOperator>>&& promise,
			const std::shared_ptr<PipelineCollection>& pool,
			StringSection<> pipelineLayoutAssetName,
			const Internal::ShaderVariant& computeShader,
			const ParameterBox& selectors)
		{
			assert(pool);
			auto futurePipelineLayout = ::Assets::GetAssetFuturePtr<RenderCore::Assets::PredefinedPipelineLayout>(pipelineLayoutAssetName);
			::Assets::WhenAll(std::move(futurePipelineLayout)).ThenConstructToPromise(
				std::move(promise),
				[pool, selectors, plname=pipelineLayoutAssetName.AsString(), computeShader](auto&& promise, auto pipelineLayout) mutable {
					ConstructToPromise(
						std::move(promise),
						pool,
						PipelineLayoutOptions{pipelineLayout, Hash64(plname), plname},
						computeShader, selectors);
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
		const ParameterBox& selectors)
	{
		assert(pipelineLayout);
		assert(!computeShader.IsEmpty());
		auto op = ::Assets::GetAssetMarkerPtr<ComputeOperator>(pool, pipelineLayout, MakeShaderCompileResourceName(computeShader), selectors);
		return *reinterpret_cast<::Assets::PtrToMarkerPtr<IComputeShaderOperator>*>(&op);
	}

	::Assets::PtrToMarkerPtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAssetName)
	{
		auto op = ::Assets::GetAssetMarkerPtr<ComputeOperator>(
			pool, pipelineLayoutAssetName,
			MakeShaderCompileResourceName(computeShader), selectors);
		return *reinterpret_cast<::Assets::PtrToMarkerPtr<IComputeShaderOperator>*>(&op);
	}

	::Assets::PtrToMarkerPtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		StringSection<> computeShader,
		const ParameterBox& selectors)
	{
		auto op = ::Assets::GetAssetMarkerPtr<ComputeOperator>(pool, PipelineLayoutOptions{}, MakeShaderCompileResourceName(computeShader), selectors);
		return *reinterpret_cast<::Assets::PtrToMarkerPtr<IComputeShaderOperator>*>(&op);
	}

	::Assets::PtrToMarkerPtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		const Internal::ShaderVariant& computeShader,
		const ParameterBox& selectors)
	{
		auto op = ::Assets::GetAssetMarkerPtr<ComputeOperator>(pool, PipelineLayoutOptions{}, computeShader, selectors);
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
