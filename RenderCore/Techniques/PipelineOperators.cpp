// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineOperators.h"
#include "CommonResources.h"
#include "RenderPass.h"
#include "ParsingContext.h"
#include "Drawables.h"
#include "DrawablesInternal.h"
#include "PipelineAcceleratorInternal.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Shader.h"
#include "../Metal/ObjectFactory.h"
#include "../../Assets/Assets.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../xleres/FileList.h"

namespace Assets
{
	uint64_t Hash64(const ::Assets::DependencyValidation&, uint64_t seed = DefaultSeed64) { return seed; }
}

namespace RenderCore { namespace Techniques
{
	const ::Assets::DependencyValidation CompiledPipelineLayoutAsset::GetDependencyValidation() const { return _containingFile->GetDependencyValidation(); };

	CompiledPipelineLayoutAsset::CompiledPipelineLayoutAsset(
		std::shared_ptr<RenderCore::IDevice> device,
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayoutFile> predefinedLayouts,
		std::shared_ptr<RenderCore::Techniques::CommonResourceBox> commonResources,
		std::string section,
		RenderCore::ShaderLanguage shaderLanguage)
	: _containingFile(std::move(predefinedLayouts))
	{
		if (!section.empty()) {
			for (const auto& l:_containingFile->_pipelineLayouts)
				if (l.first == section) {
					auto initializer = l.second->MakePipelineLayoutInitializer(shaderLanguage, &commonResources->_samplerPool);
					_pipelineLayout = device->CreatePipelineLayout(initializer);
					break;
				}
			if (!_pipelineLayout)
				Throw(::Assets::Exceptions::ConstructionError(
					::Assets::Exceptions::ConstructionError::Reason::MissingFile,
					_containingFile->GetDependencyValidation(),
					"Could not find section (%s) in given pipeline layout file", section.c_str()));
		} else {
			if (_containingFile->_pipelineLayouts.empty())
				Throw(::Assets::Exceptions::ConstructionError(
					::Assets::Exceptions::ConstructionError::Reason::MissingFile,
					_containingFile->GetDependencyValidation(),
					"No pipeline layouts in pipeline layout file"));
			auto initializer = _containingFile->_pipelineLayouts.begin()->second->MakePipelineLayoutInitializer(shaderLanguage, &commonResources->_samplerPool);
			_pipelineLayout = device->CreatePipelineLayout(initializer);
		}
	}

	void CompiledPipelineLayoutAsset::ConstructToFuture(
		::Assets::FuturePtr<CompiledPipelineLayoutAsset>& future,
		const std::shared_ptr<RenderCore::IDevice>& device,
		const std::shared_ptr<RenderCore::Techniques::CommonResourceBox>& commonResources,
		StringSection<> srcFile,
		RenderCore::ShaderLanguage shaderLanguage)
	{
		using namespace RenderCore;
		auto splitter = MakeFileNameSplitter(srcFile);
		auto src = ::Assets::MakeAsset<RenderCore::Assets::PredefinedPipelineLayoutFile>(splitter.AllExceptParameters());
		::Assets::WhenAll(src).ThenConstructToFuture(
			future,
			[device, shaderLanguage, section=splitter.Parameters().AsString(), commonResources=commonResources](std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayoutFile> predefinedLayouts) {
				return std::make_shared<CompiledPipelineLayoutAsset>(device, std::move(predefinedLayouts), commonResources, section, shaderLanguage);
			});
	}

	class FullViewportOperator : public IShaderOperator
	{
	public:
		std::shared_ptr<Metal::GraphicsPipeline> _pipeline;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		BoundUniformsPool _boundUniforms;
		UniformsStreamInterface _usi;

		::Assets::DependencyValidation GetDependencyValidation() const override { return _depVal; }
		::Assets::DependencyValidation _depVal;

		virtual void Draw(
			IThreadContext& threadContext, ParsingContext& parsingContext, SequencerUniformsHelper& uniformsHelper, 
			const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			auto& sysUsi = uniformsHelper.GetLooseUniformsStreamInterface();
			auto sysUsiHash = sysUsi.GetHash();
			auto& boundUniforms = _boundUniforms.Get(*_pipeline, sysUsi, _usi);

			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder(_pipelineLayout);

			ApplyLooseUniforms(uniformsHelper, metalContext, encoder, parsingContext, boundUniforms, 0);
			if (!descSets.empty())
				boundUniforms.ApplyDescriptorSets(metalContext, encoder, descSets, 1);
			boundUniforms.ApplyLooseUniforms(metalContext, encoder, us, 1);
			
			encoder.Draw(*_pipeline, 4);
		}

		static void ConstructToFuture(
			::Assets::FuturePtr<FullViewportOperator>& future,
			const std::shared_ptr<PipelinePool>& pool,
			StringSection<> pixelShader,
			const ParameterBox& selectors,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			const ::Assets::DependencyValidation& pipelineLayoutDepVal,
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
			auto pipelineFuture = pool->CreateGraphicsPipeline(pipelineLayout, pipelineDesc, selectors, vInputStates, fbTarget);
			::Assets::WhenAll(pipelineFuture).ThenConstructToFuture(
				future,
				[pipelineLayout=pipelineLayout, usi=usi, pipelineLayoutDepVal](std::shared_ptr<Metal::GraphicsPipeline> pipeline) {
					auto op = std::make_shared<FullViewportOperator>();
					op->_pipelineLayout = pipelineLayout;
					op->_pipeline = std::move(pipeline);
					op->_usi = std::move(usi);
					op->_depVal = op->_pipeline->GetDependencyValidation();
					if (pipelineLayoutDepVal) {
						op->_depVal = ::Assets::GetDepValSys().Make();
						op->_depVal.RegisterDependency(op->_pipeline->GetDependencyValidation());
						op->_depVal.RegisterDependency(pipelineLayoutDepVal);
					}
					return op;
				});
		}
	};

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<PipelinePool>& pool,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const FrameBufferTarget& fbTarget,
		const UniformsStreamInterface& usi)
	{
		assert(!pixelShader.IsEmpty());
		auto op = ::Assets::MakeAsset<FullViewportOperator>(pool, pixelShader, selectors, pipelineLayout, ::Assets::DependencyValidation{}, fbTarget, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IShaderOperator>*>(&op);
	}

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<PipelinePool>& pool,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const ::Assets::DependencyValidation& pipelineLayoutDepVal,
		const FrameBufferTarget& fbTarget,
		const UniformsStreamInterface& usi)
	{
		assert(!pixelShader.IsEmpty());
		auto op = ::Assets::MakeAsset<FullViewportOperator>(pool, pixelShader, selectors, pipelineLayout, pipelineLayoutDepVal, fbTarget, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IShaderOperator>*>(&op);
	}

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<PipelinePool>& pool,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAssetName,
		const FrameBufferTarget& fbTarget,
		const UniformsStreamInterface& usi)
	{
		auto pipelineLayoutAsset = ::Assets::MakeAsset<CompiledPipelineLayoutAsset>(pool->GetDevice(), pool->GetCommonResources(), pipelineLayoutAssetName);
		auto fastLayout = pipelineLayoutAsset->TryActualize();
		if (fastLayout) {
			return CreateFullViewportOperator(pool, pixelShader, selectors, (*fastLayout)->GetPipelineLayout(), (*fastLayout)->GetDependencyValidation(), fbTarget, usi);
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
						pipelineLayout->GetPipelineLayout(), pipelineLayout->GetDependencyValidation(),
						{&fbDesc, subPassIdx}, usi);
				});
			return *reinterpret_cast<::Assets::PtrToFuturePtr<IShaderOperator>*>(&result);
		}
	}

	class ComputeOperator : public Techniques::IComputeShaderOperator
	{
	public:
		std::shared_ptr<Metal::ComputePipeline> _pipeline;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		BoundUniformsPool _boundUniforms;
		UniformsStreamInterface _usi;

		::Assets::DependencyValidation GetDependencyValidation() const override { return _depVal; }
		::Assets::DependencyValidation _depVal;

		virtual void BeginDispatches(
			IThreadContext& threadContext, ParsingContext& parsingContext, SequencerUniformsHelper& uniformsHelper, 
			const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets,
			uint64_t pushConstantsBinding = 0) override
		{
			assert(!_betweenBeginEnd);
			auto& sysUsi = uniformsHelper.GetLooseUniformsStreamInterface();
			UniformsStreamInterface pushConstantsUSI;
			if (pushConstantsBinding) pushConstantsUSI.BindImmediateData(0, pushConstantsBinding);
			auto& boundUniforms = _boundUniforms.Get(*_pipeline, sysUsi, _usi, pushConstantsUSI);

			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			_activeEncoder = metalContext.BeginComputeEncoder(_pipelineLayout);

			ApplyLooseUniforms(uniformsHelper, metalContext, _activeEncoder, parsingContext, boundUniforms, 0);
			if (!descSets.empty())
				boundUniforms.ApplyDescriptorSets(metalContext, _activeEncoder, descSets, 1);
			boundUniforms.ApplyLooseUniforms(metalContext, _activeEncoder, us, 1);
			_betweenBeginEnd = true;
		}

		virtual void BeginDispatches(IThreadContext& threadContext, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets, uint64_t pushConstantsBinding = 0) override
		{
			assert(!_betweenBeginEnd);
			UniformsStreamInterface pushConstantsUSI;
			if (pushConstantsBinding) pushConstantsUSI.BindImmediateData(0, pushConstantsBinding);
			auto& boundUniforms = _boundUniforms.Get(*_pipeline, {}, _usi, pushConstantsUSI);
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			_activeEncoder = metalContext.BeginComputeEncoder(_pipelineLayout);
			if (!descSets.empty())
				boundUniforms.ApplyDescriptorSets(metalContext, _activeEncoder, descSets, 1);
			boundUniforms.ApplyLooseUniforms(metalContext, _activeEncoder, us, 1);
			_betweenBeginEnd = true;
		}

		virtual void EndDispatches() override
		{
			assert(_betweenBeginEnd);
			_activeEncoder = {};
			_betweenBeginEnd = false;
		}

		virtual void Dispatch(
			IThreadContext& threadContext, ParsingContext& parsingContext, SequencerUniformsHelper& uniformsHelper, 
			unsigned countX, unsigned countY, unsigned countZ, 
			const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			BeginDispatches(threadContext, parsingContext, uniformsHelper, us, descSets);
			_activeEncoder.Dispatch(*_pipeline, countX, countY, countZ);
			_activeEncoder = {};
			_betweenBeginEnd = false;
		}

		virtual void Dispatch(IThreadContext& threadContext, unsigned countX, unsigned countY, unsigned countZ, const UniformsStream& us, IteratorRange<const IDescriptorSet* const*> descSets) override
		{
			BeginDispatches(threadContext, us, descSets);
			_activeEncoder.Dispatch(*_pipeline, countX, countY, countZ);
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

		static void ConstructToFuture(
			::Assets::FuturePtr<ComputeOperator>& future,
			const std::shared_ptr<PipelinePool>& pool,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			const ::Assets::DependencyValidation& pipelineLayoutDepVal,
			StringSection<> computeShader,
			const ParameterBox& selectors,
			const UniformsStreamInterface& usi)
		{
			auto pipelineFuture = pool->CreateComputePipeline(pipelineLayout, computeShader, selectors);
			::Assets::WhenAll(pipelineFuture).ThenConstructToFuture(
				future,
				[usi=usi, pipelineLayout, pipelineLayoutDepVal](std::shared_ptr<Metal::ComputePipeline> pipeline) {
					auto op = std::make_shared<ComputeOperator>();
					op->_pipelineLayout = std::move(pipelineLayout);
					op->_usi = std::move(usi);
					op->_pipeline = std::move(pipeline);
					assert(op->_pipeline);
					if (pipelineLayoutDepVal) {
						op->_depVal = ::Assets::GetDepValSys().Make();
						op->_depVal.RegisterDependency(op->_pipeline->GetDependencyValidation());
						op->_depVal.RegisterDependency(pipelineLayoutDepVal);
					}
					return op;
				});
		}

		RenderCore::Metal::ComputeEncoder _activeEncoder;
		bool _betweenBeginEnd = false;
	};

	::Assets::PtrToFuturePtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelinePool>& pool,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		const UniformsStreamInterface& usi)
	{
		assert(pipelineLayout);
		assert(!computeShader.IsEmpty());
		auto op = ::Assets::MakeAsset<ComputeOperator>(pool, pipelineLayout, ::Assets::DependencyValidation{}, computeShader, selectors, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IComputeShaderOperator>*>(&op);
	}

	::Assets::PtrToFuturePtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelinePool>& pool,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const ::Assets::DependencyValidation& pipelineLayoutDepVal,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		const UniformsStreamInterface& usi)
	{
		assert(pipelineLayout);
		assert(!computeShader.IsEmpty());
		auto op = ::Assets::MakeAsset<ComputeOperator>(pool, pipelineLayout, pipelineLayoutDepVal, computeShader, selectors, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IComputeShaderOperator>*>(&op);
	}

	::Assets::PtrToFuturePtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelinePool>& pool,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAssetName,
		const UniformsStreamInterface& usi)
	{
		auto pipelineLayoutAsset = ::Assets::MakeAsset<CompiledPipelineLayoutAsset>(pool->GetDevice(), pool->GetCommonResources(), pipelineLayoutAssetName);
		auto fastLayout = pipelineLayoutAsset->TryActualize();
		if (fastLayout) {
			return CreateComputeOperator(pool, (*fastLayout)->GetPipelineLayout(), (*fastLayout)->GetDependencyValidation(), computeShader, selectors, usi);
		} else {
			auto result = std::make_shared<::Assets::FuturePtr<ComputeOperator>>();
			::Assets::WhenAll(pipelineLayoutAsset).ThenConstructToFuture(
				*result,
				[computeShader=computeShader.AsString(), selectors=selectors, pool=pool,
				usi=usi](::Assets::FuturePtr<ComputeOperator>& resultFuture,
					std::shared_ptr<CompiledPipelineLayoutAsset> pipelineLayout) {
					ComputeOperator::ConstructToFuture(resultFuture, pool, pipelineLayout->GetPipelineLayout(), pipelineLayout->GetDependencyValidation(), computeShader, selectors, usi);
				});
			return *reinterpret_cast<::Assets::PtrToFuturePtr<IComputeShaderOperator>*>(&result);
		}

	}

	IShaderOperator::~IShaderOperator() {}
	IComputeShaderOperator::~IComputeShaderOperator() {}
}}
