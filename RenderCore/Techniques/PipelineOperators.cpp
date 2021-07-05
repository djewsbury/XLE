// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineOperators.h"
#include "CommonResources.h"
#include "RenderPass.h"
#include "ParsingContext.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Shader.h"
#include "../Metal/ObjectFactory.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace Techniques
{
	class FullViewportOperator : public IShaderOperator
	{
	public:
		std::shared_ptr<Metal::GraphicsPipeline> _pipeline;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		Metal::BoundUniforms _boundUniforms;

		::Assets::DependencyValidation GetDependencyValidation() const override { return _pipeline->GetDependencyValidation(); }

		virtual void Draw(IThreadContext& threadContext, ParsingContext& parsingContext, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder(_pipelineLayout);
			if (_boundUniforms.GetBoundLooseImmediateDatas(1)) {
				ImmediateDataStream immData { BuildGlobalTransformConstants(parsingContext.GetProjectionDesc()) };
				_boundUniforms.ApplyLooseUniforms(metalContext, encoder, immData, 1);
			}
			if (!descSets.empty())
				_boundUniforms.ApplyDescriptorSets(metalContext, encoder, descSets);
			_boundUniforms.ApplyLooseUniforms(metalContext, encoder, us);
			encoder.Draw(*_pipeline, 4);
		}

		static void ConstructToFuture(
			::Assets::FuturePtr<FullViewportOperator>& future,
			const std::shared_ptr<GraphicsPipelinePool>& pool,
			const FrameBufferTarget& fbTarget,
			StringSection<> pixelShader,
			const ParameterBox& selectors,
			const UniformsStreamInterface& usi)
		{
			GraphicsPipelineDesc pipelineDesc;
			pipelineDesc._shaders[(unsigned)ShaderStage::Vertex] = BASIC2D_VERTEX_HLSL ":fullscreen_viewfrustumvector";
			pipelineDesc._shaders[(unsigned)ShaderStage::Pixel] = pixelShader.AsString();
			pipelineDesc._depthStencil = CommonResourceBox::s_dsDisable;
			pipelineDesc._blend.push_back(CommonResourceBox::s_abStraightAlpha);
			pipelineDesc._blend.push_back(CommonResourceBox::s_abStraightAlpha);

			VertexInputStates vInputStates { {}, Topology::TriangleStrip };
			PixelOutputStates pOutputStates;
			pOutputStates._fbTarget = fbTarget;

			auto pipelineFuture = pool->CreatePipeline(pipelineDesc, selectors, vInputStates, pOutputStates);
			::Assets::WhenAll(pipelineFuture).ThenConstructToFuture(
				future,
				[pipelineLayout=pool->GetPipelineLayout(), usi=usi](std::shared_ptr<Metal::GraphicsPipeline> pipeline) {
					auto op = std::make_shared<FullViewportOperator>();
					op->_pipelineLayout = pipelineLayout;
					op->_pipeline = std::move(pipeline);

					UniformsStreamInterface sysUSI;
					sysUSI.BindImmediateData(0, Hash64("GlobalTransform"));
					op->_boundUniforms = Metal::BoundUniforms{*op->_pipeline, usi, sysUSI};
					return op;
				});
		}
	};

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<GraphicsPipelinePool>& pool,
		const RenderPassInstance& rpi,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		const UniformsStreamInterface& usi)
	{
		assert(!pixelShader.IsEmpty());
		auto op = ::Assets::MakeAsset<FullViewportOperator>(
			pool, 
			FrameBufferTarget{&rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex()},
			pixelShader, selectors, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IShaderOperator>*>(&op);
	}

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<GraphicsPipelinePool>& pool,
		const FrameBufferTarget& fbTarget,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		const UniformsStreamInterface& usi)
	{
		assert(!pixelShader.IsEmpty());
		auto op = ::Assets::MakeAsset<FullViewportOperator>(pool, fbTarget, pixelShader, selectors, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IShaderOperator>*>(&op);
	}

	class ComputeOperator : public Techniques::IComputeShaderOperator
	{
	public:
		std::shared_ptr<Metal::ComputePipeline> _pipeline;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		Metal::BoundUniforms _boundUniforms;

		::Assets::DependencyValidation GetDependencyValidation() const override { return _pipeline->GetDependencyValidation(); }

		virtual void Dispatch(IThreadContext& threadContext, ParsingContext& parsingContext, unsigned countX, unsigned countY, unsigned countZ, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			auto encoder = metalContext.BeginComputeEncoder(_pipelineLayout);
			if (!descSets.empty())
				_boundUniforms.ApplyDescriptorSets(metalContext, encoder, descSets);
			_boundUniforms.ApplyLooseUniforms(metalContext, encoder, us);
			encoder.Dispatch(*_pipeline, countX, countY, countZ);
		}

		static void ConstructToFuture(
			::Assets::FuturePtr<ComputeOperator>& future,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			StringSection<> computeShader,
			StringSection<> definesTable,
			const UniformsStreamInterface& usi)
		{
			auto shader = ::Assets::MakeAsset<Metal::ComputeShader>(pipelineLayout, computeShader, definesTable);
			::Assets::WhenAll(shader).ThenConstructToFuture(
				future,
				[usi=usi, pipelineLayout](std::shared_ptr<Metal::ComputeShader> shader) {
					auto op = std::make_shared<ComputeOperator>();
					op->_pipelineLayout = pipelineLayout;
					
					Metal::ComputePipelineBuilder pipelineBuilder;
					pipelineBuilder.Bind(*shader);
					op->_pipeline = pipelineBuilder.CreatePipeline(Metal::GetObjectFactory());
					op->_boundUniforms = Metal::BoundUniforms{*op->_pipeline, usi};
					return op;
				});
		}
	};

	::Assets::PtrToFuturePtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StringSection<> computeShader,
		StringSection<> definesTable,
		const UniformsStreamInterface& usi)
	{
		assert(pipelineLayout);
		assert(!computeShader.IsEmpty());
		auto op = ::Assets::MakeAsset<ComputeOperator>(pipelineLayout, computeShader, definesTable, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IComputeShaderOperator>*>(&op);
	}

	IShaderOperator::~IShaderOperator() {}
	IComputeShaderOperator::~IComputeShaderOperator() {}
}}
