// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineOperators.h"
#include "CommonResources.h"
#include "RenderPass.h"
#include "ParsingContext.h"
#include "Drawables.h"
#include "DrawablesInternal.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Shader.h"
#include "../Metal/ObjectFactory.h"
#include "../../Assets/Assets.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../xleres/FileList.h"

namespace RenderCore
{
	uint64_t Hash64(const std::shared_ptr<RenderCore::IDevice>& device, uint64_t seed = DefaultSeed64) { return seed; }
}

namespace RenderCore { namespace Techniques
{
	class CompiledPipelineLayoutAsset
	{
	public:
		std::shared_ptr<RenderCore::ICompiledPipelineLayout> _pipelineLayout;
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayoutFile> _containingFile;

		CompiledPipelineLayoutAsset(
			std::shared_ptr<RenderCore::IDevice> device,
			std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayoutFile> predefinedLayouts,
			std::string section,
			RenderCore::ShaderLanguage shaderLanguage = RenderCore::Techniques::GetDefaultShaderLanguage())
		: _containingFile(std::move(predefinedLayouts))
		{
			if (!section.empty()) {
				for (const auto& l:_containingFile->_pipelineLayouts)
					if (l.first == section) {
						auto initializer = l.second->MakePipelineLayoutInitializer(shaderLanguage);
						_pipelineLayout = device->CreatePipelineLayout(initializer);
						break;
					}
				if (!_pipelineLayout)
					Throw(::Assets::Exceptions::ConstructionError(
						::Assets::Exceptions::ConstructionError::Reason::MissingFile,
						predefinedLayouts->GetDependencyValidation(),
						"Could not find section (%s) in given pipeline layout file"));
			} else {
				if (_containingFile->_pipelineLayouts.empty())
					Throw(::Assets::Exceptions::ConstructionError(
						::Assets::Exceptions::ConstructionError::Reason::MissingFile,
						predefinedLayouts->GetDependencyValidation(),
						"No pipeline layouts in pipeline layout file"));
				auto initializer = _containingFile->_pipelineLayouts.begin()->second->MakePipelineLayoutInitializer(shaderLanguage);
				_pipelineLayout = device->CreatePipelineLayout(initializer);
			}
		}

		static void ConstructToFuture(
			::Assets::FuturePtr<CompiledPipelineLayoutAsset>& future,
			const std::shared_ptr<RenderCore::IDevice>& device,
			StringSection<> srcFile,
			RenderCore::ShaderLanguage shaderLanguage = RenderCore::Techniques::GetDefaultShaderLanguage())
		{
			using namespace RenderCore;
			auto splitter = MakeFileNameSplitter(srcFile);
			auto src = ::Assets::MakeAsset<RenderCore::Assets::PredefinedPipelineLayoutFile>(splitter.AllExceptParameters());
			::Assets::WhenAll(src).ThenConstructToFuture(
				future,
				[device, shaderLanguage, section=splitter.Parameters().AsString()](std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayoutFile> predefinedLayouts) {
					return std::make_shared<CompiledPipelineLayoutAsset>(device, std::move(predefinedLayouts), section, shaderLanguage);
				});
		}
	};

	class FullViewportOperator : public IShaderOperator
	{
	public:
		std::shared_ptr<Metal::GraphicsPipeline> _pipeline;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		std::vector<std::pair<uint64_t, Metal::BoundUniforms>> _boundUniforms;
		UniformsStreamInterface _usi;

		::Assets::DependencyValidation GetDependencyValidation() const override { return _pipeline->GetDependencyValidation(); }

		virtual void Draw(
			IThreadContext& threadContext, ParsingContext& parsingContext, SequencerUniformsHelper& uniformsHelper, 
			const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			auto& sysUsi = uniformsHelper.GetLooseUniformsStreamInterface();
			auto sysUsiHash = sysUsi.GetHash();
			auto i = LowerBound(_boundUniforms, sysUsiHash);
			if (i == _boundUniforms.end() || i->first != sysUsiHash) {
				Metal::BoundUniforms boundUniforms(*_pipeline, sysUsi, _usi);
				i = _boundUniforms.insert(i, std::pair(sysUsiHash, std::move(boundUniforms)));
			}

			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder(_pipelineLayout);
			ApplyLooseUniforms(uniformsHelper, metalContext, encoder, parsingContext, i->second, 0);

			if (!descSets.empty())
				i->second.ApplyDescriptorSets(metalContext, encoder, descSets, 1);
			i->second.ApplyLooseUniforms(metalContext, encoder, us, 1);
			
			encoder.Draw(*_pipeline, 4);
		}

		static void ConstructToFuture(
			::Assets::FuturePtr<FullViewportOperator>& future,
			const std::shared_ptr<GraphicsPipelinePool>& pool,
			StringSection<> pixelShader,
			const ParameterBox& selectors,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			const FrameBufferTarget& fbTarget,
			const UniformsStreamInterface& usi)
		{
			GraphicsPipelineDesc pipelineDesc;
			pipelineDesc._shaders[(unsigned)ShaderStage::Vertex] = BASIC2D_VERTEX_HLSL ":fullscreen_viewfrustumvector";
			pipelineDesc._shaders[(unsigned)ShaderStage::Pixel] = pixelShader.AsString();
			pipelineDesc._depthStencil = CommonResourceBox::s_dsDisable;
			pipelineDesc._blend.push_back(CommonResourceBox::s_abStraightAlpha);
			pipelineDesc._blend.push_back(CommonResourceBox::s_abStraightAlpha);

			VertexInputStates vInputStates { {}, Topology::TriangleStrip };
			auto pipelineFuture = pool->CreatePipeline(pipelineLayout, pipelineDesc, selectors, vInputStates, fbTarget);
			::Assets::WhenAll(pipelineFuture).ThenConstructToFuture(
				future,
				[pipelineLayout=pipelineLayout, usi=usi](std::shared_ptr<Metal::GraphicsPipeline> pipeline) {
					auto op = std::make_shared<FullViewportOperator>();
					op->_pipelineLayout = pipelineLayout;
					op->_pipeline = std::move(pipeline);
					op->_usi = std::move(usi);
					return op;
				});
		}
	};

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<GraphicsPipelinePool>& pool,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const FrameBufferTarget& fbTarget,
		const UniformsStreamInterface& usi)
	{
		assert(!pixelShader.IsEmpty());
		auto op = ::Assets::MakeAsset<FullViewportOperator>(pool, pixelShader, selectors, pipelineLayout, fbTarget, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IShaderOperator>*>(&op);
	}

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<GraphicsPipelinePool>& pool,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAssetName,
		const FrameBufferTarget& fbTarget,
		const UniformsStreamInterface& usi)
	{
		auto pipelineLayoutAsset = ::Assets::MakeAsset<CompiledPipelineLayoutAsset>(pool->GetDevice(), pipelineLayoutAssetName);
		auto fastLayout = pipelineLayoutAsset->TryActualize();
		if (fastLayout) {
			return CreateFullViewportOperator(pool, pixelShader, selectors, (*fastLayout)->_pipelineLayout, fbTarget, usi);
		} else {
			auto result = std::make_shared<::Assets::FuturePtr<FullViewportOperator>>();
			::Assets::WhenAll(pipelineLayoutAsset).ThenConstructToFuture(
				*result,
				[pool=pool, pixelShader=pixelShader.AsString(), selectors=selectors,
				fbDesc=*fbTarget._fbDesc, subPassIdx=fbTarget._subpassIdx,
				usi=usi](::Assets::FuturePtr<FullViewportOperator>& resultFuture,
					std::shared_ptr<CompiledPipelineLayoutAsset> pipelineLayout) {
					FullViewportOperator::ConstructToFuture(
						resultFuture, pool, pixelShader, selectors, 
						pipelineLayout->_pipelineLayout, {&fbDesc, subPassIdx}, usi);
				});
			return *reinterpret_cast<::Assets::PtrToFuturePtr<IShaderOperator>*>(&result);
		}
	}

	class ComputeOperator : public Techniques::IComputeShaderOperator
	{
	public:
		std::shared_ptr<Metal::ComputePipeline> _pipeline;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		std::vector<std::pair<uint64_t, Metal::BoundUniforms>> _boundUniforms;
		UniformsStreamInterface _usi;

		::Assets::DependencyValidation GetDependencyValidation() const override { return _pipeline->GetDependencyValidation(); }

		virtual void Dispatch(
			IThreadContext& threadContext, ParsingContext& parsingContext, SequencerUniformsHelper& uniformsHelper, 
			unsigned countX, unsigned countY, unsigned countZ, 
			const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			auto& sysUsi = uniformsHelper.GetLooseUniformsStreamInterface();
			auto sysUsiHash = sysUsi.GetHash();
			auto i = LowerBound(_boundUniforms, sysUsiHash);
			if (i == _boundUniforms.end() || i->first != sysUsiHash) {
				Metal::BoundUniforms boundUniforms(*_pipeline, sysUsi, _usi);
				i = _boundUniforms.insert(i, std::pair(sysUsiHash, std::move(boundUniforms)));
			}

			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			auto encoder = metalContext.BeginComputeEncoder(_pipelineLayout);

			if (!descSets.empty())
				i->second.ApplyDescriptorSets(metalContext, encoder, descSets, 1);
			i->second.ApplyLooseUniforms(metalContext, encoder, us, 1);
			encoder.Dispatch(*_pipeline, countX, countY, countZ);
		}

		static void ConstructToFuture(
			::Assets::FuturePtr<ComputeOperator>& future,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			StringSection<> computeShader,
			const ParameterBox& selectors,
			const UniformsStreamInterface& usi)
		{
			auto shader = ::Assets::MakeAsset<Metal::ComputeShader>(pipelineLayout, computeShader, BuildFlatStringTable(selectors));
			::Assets::WhenAll(shader).ThenConstructToFuture(
				future,
				[usi=usi, pipelineLayout](std::shared_ptr<Metal::ComputeShader> shader) {
					auto op = std::make_shared<ComputeOperator>();
					op->_pipelineLayout = pipelineLayout;
					op->_usi = std::move(usi);
					
					Metal::ComputePipelineBuilder pipelineBuilder;
					pipelineBuilder.Bind(*shader);
					op->_pipeline = pipelineBuilder.CreatePipeline(Metal::GetObjectFactory());
					return op;
				});
		}
	};

	::Assets::PtrToFuturePtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		const UniformsStreamInterface& usi)
	{
		assert(pipelineLayout);
		assert(!computeShader.IsEmpty());
		auto op = ::Assets::MakeAsset<ComputeOperator>(pipelineLayout, computeShader, selectors, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IComputeShaderOperator>*>(&op);
	}

	::Assets::PtrToFuturePtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<RenderCore::IDevice>& device,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAssetName,
		const UniformsStreamInterface& usi)
	{
		auto pipelineLayoutAsset = ::Assets::MakeAsset<CompiledPipelineLayoutAsset>(device, pipelineLayoutAssetName);
		auto fastLayout = pipelineLayoutAsset->TryActualize();
		if (fastLayout) {
			return CreateComputeOperator((*fastLayout)->_pipelineLayout, computeShader, selectors, usi);
		} else {
			auto result = std::make_shared<::Assets::FuturePtr<ComputeOperator>>();
			::Assets::WhenAll(pipelineLayoutAsset).ThenConstructToFuture(
				*result,
				[computeShader=computeShader.AsString(), selectors=selectors,
				usi=usi](::Assets::FuturePtr<ComputeOperator>& resultFuture,
					std::shared_ptr<CompiledPipelineLayoutAsset> pipelineLayout) {
					ComputeOperator::ConstructToFuture(resultFuture, pipelineLayout->_pipelineLayout, computeShader, selectors, usi);
				});
			return *reinterpret_cast<::Assets::PtrToFuturePtr<IComputeShaderOperator>*>(&result);
		}

	}

	IShaderOperator::~IShaderOperator() {}
	IComputeShaderOperator::~IComputeShaderOperator() {}
}}
