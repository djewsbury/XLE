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
#include "Services.h"
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
	const ::Assets::DependencyValidation CompiledPipelineLayoutAsset::GetDependencyValidation() const { return _predefinedLayout->GetDependencyValidation(); };

	CompiledPipelineLayoutAsset::CompiledPipelineLayoutAsset(
		std::shared_ptr<RenderCore::IDevice> device,
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout> predefinedLayout,
		RenderCore::ShaderLanguage shaderLanguage)
	: _predefinedLayout(std::move(predefinedLayout))
	{
		assert(Services::HasInstance() && Services::GetCommonResources());
		auto& commonResources = *Services::GetCommonResources();
		auto initializer = _predefinedLayout->MakePipelineLayoutInitializer(shaderLanguage, &commonResources._samplerPool);
		_pipelineLayout = device->CreatePipelineLayout(initializer);
	}

	void CompiledPipelineLayoutAsset::ConstructToFuture(
		::Assets::FuturePtr<CompiledPipelineLayoutAsset>& future,
		const std::shared_ptr<RenderCore::IDevice>& device,
		StringSection<> srcFile,
		RenderCore::ShaderLanguage shaderLanguage)
	{
		using namespace RenderCore;
		auto src = ::Assets::MakeAsset<RenderCore::Assets::PredefinedPipelineLayout>(srcFile);
		::Assets::WhenAll(src).ThenConstructToFuture(
			future,
			[device, shaderLanguage](auto predefinedLayout) {
				return std::make_shared<CompiledPipelineLayoutAsset>(device, predefinedLayout, shaderLanguage);
			});
	}

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

		virtual const Assets::PredefinedPipelineLayout& GetPredefinedPipelineLayout() const override
		{
			if (!_predefinedPipelineLayout)
				Throw(std::runtime_error("Cannot get a predefined pipeline layout from a shader operator that was constructed directly from a compiled pipeline layout"));
			return *_predefinedPipelineLayout;
		}

		static void ConstructToFuture(
			::Assets::FuturePtr<FullViewportOperator>& future,
			const std::shared_ptr<PipelinePool>& pool,
			unsigned subType,
			StringSection<> pixelShader,
			const ParameterBox& selectors,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			const std::shared_ptr<Assets::PredefinedPipelineLayout>& predefinedPipelineLayout,
			const ::Assets::DependencyValidation& pipelineLayoutDepVal,
			const FrameBufferTarget& fbTarget,
			const UniformsStreamInterface& usi)
		{
			GraphicsPipelineDesc pipelineDesc;
			pipelineDesc._shaders[(unsigned)ShaderStage::Pixel] = pixelShader.AsString();
			if (subType == (unsigned)FullViewportOperatorSubType::DisableDepth) {
				pipelineDesc._shaders[(unsigned)ShaderStage::Vertex] = BASIC2D_VERTEX_HLSL ":fullscreen_viewfrustumvector";
				pipelineDesc._depthStencil = CommonResourceBox::s_dsDisable;
				pipelineDesc._blend.push_back(CommonResourceBox::s_abStraightAlpha);
				pipelineDesc._blend.push_back(CommonResourceBox::s_abStraightAlpha);
			} else {
				pipelineDesc._shaders[(unsigned)ShaderStage::Vertex] = BASIC2D_VERTEX_HLSL ":fullscreen_viewfrustumvector_deep";
				pipelineDesc._depthStencil = CommonResourceBox::s_dsReadOnly;
				pipelineDesc._blend.push_back(CommonResourceBox::s_abOpaque);
			}

			VertexInputStates vInputStates { {}, Topology::TriangleStrip };
			auto pipelineFuture = pool->CreateGraphicsPipeline(pipelineLayout, pipelineDesc, selectors, vInputStates, fbTarget);
			::Assets::WhenAll(pipelineFuture).ThenConstructToFuture(
				future,
				[pipelineLayout=pipelineLayout, usi=usi, pipelineLayoutDepVal, predefinedPipelineLayout](std::shared_ptr<Metal::GraphicsPipeline> pipeline) {
					auto op = std::make_shared<FullViewportOperator>();
					op->_pipelineLayout = pipelineLayout;
					op->_pipeline = std::move(pipeline);
					op->_usi = std::move(usi);
					op->_predefinedPipelineLayout = predefinedPipelineLayout;
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
		FullViewportOperatorSubType subType,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const FrameBufferTarget& fbTarget,
		const UniformsStreamInterface& usi)
	{
		assert(!pixelShader.IsEmpty());
		auto op = ::Assets::MakeFuture<std::shared_ptr<FullViewportOperator>>(pool, (unsigned)subType, pixelShader, selectors, pipelineLayout, std::shared_ptr<Assets::PredefinedPipelineLayout>{}, ::Assets::DependencyValidation{}, fbTarget, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IShaderOperator>*>(&op);
	}

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<PipelinePool>& pool,
		FullViewportOperatorSubType subType,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAssetName,
		const FrameBufferTarget& fbTarget,
		const UniformsStreamInterface& usi)
	{
		auto pipelineLayoutAsset = ::Assets::MakeAsset<CompiledPipelineLayoutAsset>(pool->GetDevice(), pipelineLayoutAssetName);
		auto fastLayout = pipelineLayoutAsset->TryActualize();
		if (fastLayout) {
			assert(!pixelShader.IsEmpty());
			auto op = ::Assets::MakeFuture<std::shared_ptr<FullViewportOperator>>(pool, (unsigned)subType, pixelShader, selectors, (*fastLayout)->GetPipelineLayout(), (*fastLayout)->GetPredefinedPipelineLayout(), (*fastLayout)->GetDependencyValidation(), fbTarget, usi);
			return *reinterpret_cast<::Assets::PtrToFuturePtr<IShaderOperator>*>(&op);
		} else {
			auto result = std::make_shared<::Assets::FuturePtr<FullViewportOperator>>();
			::Assets::WhenAll(pipelineLayoutAsset).ThenConstructToFuture(
				*result,
				[pool=pool, subType, pixelShader=pixelShader.AsString(), selectors=selectors,
				fbDesc=*fbTarget._fbDesc, subPassIdx=fbTarget._subpassIdx,
				usi=usi](::Assets::FuturePtr<FullViewportOperator>& resultFuture,
					std::shared_ptr<CompiledPipelineLayoutAsset> pipelineLayout) {
					FullViewportOperator::ConstructToFuture(
						resultFuture, pool, (unsigned)subType, pixelShader, selectors, 
						pipelineLayout->GetPipelineLayout(), pipelineLayout->GetPredefinedPipelineLayout(), pipelineLayout->GetDependencyValidation(),
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
		std::shared_ptr<Assets::PredefinedPipelineLayout> _predefinedPipelineLayout;

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

		virtual const Assets::PredefinedPipelineLayout& GetPredefinedPipelineLayout() const override
		{
			if (!_predefinedPipelineLayout)
				Throw(std::runtime_error("Cannot get a predefined pipeline layout from a shader operator that was constructed directly from a compiled pipeline layout"));
			return *_predefinedPipelineLayout;
		}

		static void ConstructToFuture(
			::Assets::FuturePtr<ComputeOperator>& future,
			const std::shared_ptr<PipelinePool>& pool,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			const std::shared_ptr<Assets::PredefinedPipelineLayout>& predefinedPipelineLayout,
			const ::Assets::DependencyValidation& pipelineLayoutDepVal,
			StringSection<> computeShader,
			const ParameterBox& selectors,
			const UniformsStreamInterface& usi)
		{
			auto pipelineFuture = pool->CreateComputePipeline(pipelineLayout, computeShader, selectors);
			::Assets::WhenAll(pipelineFuture).ThenConstructToFuture(
				future,
				[usi=usi, pipelineLayout, pipelineLayoutDepVal, predefinedPipelineLayout](std::shared_ptr<Metal::ComputePipeline> pipeline) {
					auto op = std::make_shared<ComputeOperator>();
					op->_pipelineLayout = std::move(pipelineLayout);
					op->_usi = std::move(usi);
					op->_pipeline = std::move(pipeline);
					op->_predefinedPipelineLayout = std::move(predefinedPipelineLayout);
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
		static_assert(::Assets::Internal::HasConstructToFutureOverride<std::shared_ptr<ComputeOperator>, 
			const std::shared_ptr<PipelinePool>&,
			const std::shared_ptr<ICompiledPipelineLayout>&,
			const std::shared_ptr<Assets::PredefinedPipelineLayout>&,
			const ::Assets::DependencyValidation&,
			StringSection<>,
			const ParameterBox&,
			const UniformsStreamInterface&>::value);
		auto op = ::Assets::MakeFuture<std::shared_ptr<ComputeOperator>>(pool, pipelineLayout, std::shared_ptr<Assets::PredefinedPipelineLayout>{}, ::Assets::DependencyValidation{}, computeShader, selectors, usi);
		return *reinterpret_cast<::Assets::PtrToFuturePtr<IComputeShaderOperator>*>(&op);
	}

	::Assets::PtrToFuturePtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelinePool>& pool,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAssetName,
		const UniformsStreamInterface& usi)
	{
		auto pipelineLayoutAsset = ::Assets::MakeAsset<CompiledPipelineLayoutAsset>(pool->GetDevice(), pipelineLayoutAssetName);
		auto fastLayout = pipelineLayoutAsset->TryActualize();
		if (fastLayout) {
			auto op = ::Assets::MakeFuture<std::shared_ptr<ComputeOperator>>(pool, (*fastLayout)->GetPipelineLayout(), (*fastLayout)->GetPredefinedPipelineLayout(), (*fastLayout)->GetDependencyValidation(), computeShader, selectors, usi);
			return *reinterpret_cast<::Assets::PtrToFuturePtr<IComputeShaderOperator>*>(&op);
		} else {
			auto result = std::make_shared<::Assets::FuturePtr<ComputeOperator>>();
			::Assets::WhenAll(pipelineLayoutAsset).ThenConstructToFuture(
				*result,
				[computeShader=computeShader.AsString(), selectors=selectors, pool=pool,
				usi=usi](::Assets::FuturePtr<ComputeOperator>& resultFuture,
					std::shared_ptr<CompiledPipelineLayoutAsset> pipelineLayout) {
					ComputeOperator::ConstructToFuture(resultFuture, pool, pipelineLayout->GetPipelineLayout(), pipelineLayout->GetPredefinedPipelineLayout(), pipelineLayout->GetDependencyValidation(), computeShader, selectors, usi);
				});
			return *reinterpret_cast<::Assets::PtrToFuturePtr<IComputeShaderOperator>*>(&result);
		}

	}

	IShaderOperator::~IShaderOperator() {}
	IComputeShaderOperator::~IComputeShaderOperator() {}
}}
