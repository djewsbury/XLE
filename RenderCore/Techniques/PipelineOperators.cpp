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
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			const FrameBufferTarget& fbTarget,
			StringSection<> pixelShader,
			StringSection<> definesTable,
			const UniformsStreamInterface& usi)
		{
			auto shaderProgram = ::Assets::MakeAsset<Metal::ShaderProgram>(
				pipelineLayout,
				BASIC2D_VERTEX_HLSL ":fullscreen_viewfrustumvector",
				pixelShader,
				definesTable);

			::Assets::WhenAll(shaderProgram).ThenConstructToFuture(
				future,
				[usi=usi, pipelineLayout, fbDesc=*fbTarget._fbDesc, subpassIdx=fbTarget._subpassIdx](std::shared_ptr<Metal::ShaderProgram> shader) {
					auto op = std::make_shared<FullViewportOperator>();
					op->_pipelineLayout = pipelineLayout;
					
					Metal::GraphicsPipelineBuilder pipelineBuilder;
					pipelineBuilder.Bind(*shader);
					pipelineBuilder.Bind(CommonResourceBox::s_dsDisable);
					AttachmentBlendDesc blends[] = { CommonResourceBox::s_abStraightAlpha, CommonResourceBox::s_abStraightAlpha };
					pipelineBuilder.Bind(MakeIteratorRange(blends));
					pipelineBuilder.Bind({}, Topology::TriangleStrip);
					pipelineBuilder.SetRenderPassConfiguration(fbDesc, subpassIdx);
					op->_pipeline = pipelineBuilder.CreatePipeline(Metal::GetObjectFactory());

					UniformsStreamInterface sysUSI;
					sysUSI.BindImmediateData(0, Hash64("GlobalTransform"));
					op->_boundUniforms = Metal::BoundUniforms{*op->_pipeline, usi, sysUSI};
					return op;
				});
		}
	};

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const RenderPassInstance& rpi,
		StringSection<> pixelShader,
		StringSection<> definesTable,
		const UniformsStreamInterface& usi)
	{
		assert(pipelineLayout);
		assert(!pixelShader.IsEmpty());
		auto op = ::Assets::MakeAsset<FullViewportOperator>(
			pipelineLayout, 
			FrameBufferTarget{&rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex()},
			pixelShader, definesTable, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IShaderOperator>*>(&op);
	}

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const FrameBufferTarget& fbTarget,
		StringSection<> pixelShader,
		StringSection<> definesTable,
		const UniformsStreamInterface& usi)
	{
		assert(pipelineLayout);
		assert(!pixelShader.IsEmpty());
		auto op = ::Assets::MakeAsset<FullViewportOperator>(pipelineLayout, fbTarget, pixelShader, definesTable, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IShaderOperator>*>(&op);
	}

	class ComputeOperator : public Techniques::IShaderOperator
	{
	public:
		std::shared_ptr<Metal::ComputePipeline> _pipeline;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		Metal::BoundUniforms _boundUniforms;

		virtual void Draw(IThreadContext& threadContext, ParsingContext& parsingContext, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			auto encoder = metalContext.BeginComputeEncoder(_pipelineLayout);
			if (!descSets.empty())
				_boundUniforms.ApplyDescriptorSets(metalContext, encoder, descSets);
			_boundUniforms.ApplyLooseUniforms(metalContext, encoder, us);
			// encoder.Dispatch(*_pipeline, 640/16, 360/8, 1);
			encoder.Dispatch(*_pipeline, 640/2, 360/2, 1);
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

	::Assets::PtrToFuturePtr<IShaderOperator> CreateComputeOperator(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StringSection<> computeShader,
		StringSection<> definesTable,
		const UniformsStreamInterface& usi)
	{
		assert(pipelineLayout);
		assert(!computeShader.IsEmpty());
		auto op = ::Assets::MakeAsset<ComputeOperator>(pipelineLayout, computeShader, definesTable, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IShaderOperator>*>(&op);
	}

	IShaderOperator::~IShaderOperator() {}
}}
