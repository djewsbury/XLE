// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PostProcessOperators.h"
#include "RenderStepFragments.h"
#include "SequenceIterator.h"
#include "LightingEngine.h"				// for ChainedOperatorDesc
#include "LightingDelegateUtil.h"		// for ChainedOperatorCast
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Assets/TextureCompiler.h"
#include "../UniformsStream.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"

#define FFX_CPU 1
#include "../../Foreign/FidelityFX-SDK/sdk/include/FidelityFX/gpu/ffx_core.h"
#include "../../Foreign/FidelityFX-SDK/sdk/include/FidelityFX/gpu/cas/ffx_cas.h"

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine 
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template <int Base>
		inline float CalculateHaltonNumber(unsigned index)
	{
		// See https://pbr-book.org/3ed-2018/Sampling_and_Reconstruction/The_Halton_Sampler
		// AMD's capsaicin implementation does not seem perfect. Instead, let's take some cures from the pbr-book
		// Note not bothering with the reverse bit trick for base 2
		float reciprocalBaseN = 1.0f, result = 0.0f;
		float reciprocalBase = 1.f / float(Base);
		while (index) {
			auto next = index / Base;
			auto digit = index - next * Base;
			result = result * Base + digit;
			reciprocalBaseN *= reciprocalBase;
			index = next;
		}
		return result * reciprocalBaseN;
	}

	RenderStepFragmentInterface PostProcessOperator::CreateFragment(const FrameBufferProperties& fbProps)
	{
		assert(_secondStageConstructionState == 0);
		RenderStepFragmentInterface result{PipelineType::Compute};

		assert(!_uniformsHelper);
		_uniformsHelper = std::make_unique<ComputeAttachmentUniformsTracker>();
		_uniformsHelper->ExpectAttachment("PostProcessInput"_h, {BindFlag::UnorderedAccess, ShaderStage::Pixel});

		_uniformsHelper->BindWithBarrier("Input"_h, "PostProcessInput"_h);		// barrier without layout change, to ensure prior compute shader is finished
		_uniformsHelper->BindWithBarrier("Output"_h, Techniques::AttachmentSemantics::ColorLDR, BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::Aspect::ColorLinear});
		_attachmentUsi = _uniformsHelper->EndUniformsStream();
		_uniformsHelper->Barrier(Techniques::AttachmentSemantics::ColorLDR, {BindFlag::RenderTarget, ShaderStage::Pixel});
		_uniformsHelper->Discard("PostProcessInput"_h);

		result.AddSubpass(
			_uniformsHelper->CreateSubpass(result, "post-process"),
			[op=shared_from_this()](SequenceIterator& iterator) {
				auto pass = op->_uniformsHelper->BeginPass(iterator._parsingContext->GetThreadContext(), iterator._rpi);

				struct ControlUniforms
				{
					FfxUInt32x4 _casConstants0;
					FfxUInt32x4 _casConstants1;
					UInt4 _noiseUniforms;
				} controlUniforms;

				UniformsStream::ImmediateData immDatas[] = { MakeOpaqueIteratorRange(controlUniforms) };
				IResourceView* srvs[] = { nullptr };

				auto& parsingContext = *iterator._parsingContext;
				UInt2 outputDims { parsingContext.GetFrameBufferProperties()._width, parsingContext.GetFrameBufferProperties()._height };
				if (op->_desc._sharpen) {
					ffxCasSetup(
						controlUniforms._casConstants0,
						controlUniforms._casConstants1,
						op->_desc._sharpen->_amount,
						(float)outputDims[0], (float)outputDims[1],
						(float)outputDims[0], (float)outputDims[1]);
				}

				if (op->_desc._filmGrain) {
					unsigned jitteringIndex = (iterator.GetFrameToFrameProperties()._frameIdx + 17) % (32*27);		// mod some arbitrary number, but small to avoid precision issues in CalculateHaltonNumber
					controlUniforms._noiseUniforms[0] = unsigned(CalculateHaltonNumber<2>(jitteringIndex) * 32);
					controlUniforms._noiseUniforms[1] = unsigned(CalculateHaltonNumber<3>(jitteringIndex) * 27);
					controlUniforms._noiseUniforms[2] = *(uint32_t*)&op->_desc._filmGrain->_strength;
					srvs[0] = op->_noise.get();
				}

				const unsigned groupSize = 16;
				op->_shader->Dispatch(
					parsingContext,
					(outputDims[0] + groupSize - 1) / groupSize, (outputDims[1] + groupSize - 1) / groupSize, 1,
					pass.GetNextUniformsStream(),
					UniformsStream { srvs, immDatas });
			});

		return result;
	}

	void PostProcessOperator::SecondStageConstruction(
		std::promise<std::shared_ptr<PostProcessOperator>>&& promise,
		const Techniques::FrameBufferTarget& fbTarget)
	{
		assert(_secondStageConstructionState == 0);
		assert(_uniformsHelper);		// CreateFragment must have already been called
		_secondStageConstructionState = 1;

		ParameterBox selectors;
		selectors.SetParameter("SHARPEN", _desc._sharpen.has_value());
		selectors.SetParameter("FILM_GRAIN", _desc._filmGrain.has_value());
		UniformsStreamInterface nonAttachmentUsi;
		nonAttachmentUsi.BindImmediateData(0, "ControlUniforms"_h);
		nonAttachmentUsi.BindResourceView(0, "NoiseTexture"_h);

		auto shader = Techniques::CreateComputeOperator(
			_pool,
			POSTPROCESS_COMPUTE_HLSL ":main",
			std::move(selectors),
			GENERAL_OPERATOR_PIPELINE ":ComputeMain",
			_attachmentUsi,
			nonAttachmentUsi);

		if (!_desc._filmGrain) {

			::Assets::WhenAll(shader).ThenConstructToPromise(
				std::move(promise),
				[strongThis = shared_from_this()](auto shader) {
					assert(strongThis->_secondStageConstructionState == 1);
					strongThis->_shader = std::move(shader);
					strongThis->_secondStageConstructionState = 2;
					return strongThis;
				});

		} else {

			Assets::TextureCompilationRequest compileRequest;
			compileRequest._operation = Assets::TextureCompilationRequest::Operation::BalancedNoise;
			compileRequest._width = compileRequest._height = 256;		// probably could use a smaller texture
			compileRequest._format = Format::R8_UNORM;
			auto balancedNoiseFuture = ::Assets::MakeAssetPtr<RenderCore::Techniques::DeferredShaderResource>(compileRequest);

			::Assets::WhenAll(shader, balancedNoiseFuture).ThenConstructToPromise(
				std::move(promise),
				[strongThis = shared_from_this()](auto shader, auto noise) {
					assert(strongThis->_secondStageConstructionState == 1);
					strongThis->_shader = std::move(shader);
					strongThis->_noise = noise->GetShaderResource();
					strongThis->_secondStageConstructionState = 2;
					return strongThis;
				});

		 }
	}

	void PostProcessOperator::PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, const FrameBufferProperties& fbProps)
	{
		UInt2 fbSize{fbProps._width, fbProps._height};
		stitchingContext.DefineAttachment(
			Techniques::PreregisteredAttachment {
				"PostProcessInput"_h,
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], AsTypelessFormat(stitchingContext.GetSystemAttachmentFormat(Techniques::SystemAttachmentFormat::LDRColor)))),
				"post-process-input"
			});
	}

	::Assets::DependencyValidation PostProcessOperator::GetDependencyValidation() const
	{
		assert(_secondStageConstructionState==2);
		return _shader->GetDependencyValidation();
	}

	auto PostProcessOperator::MakeCombinedDesc(const ChainedOperatorDesc* descChain) -> std::optional<CombinedDesc>
	{
		CombinedDesc result;
		bool foundSomething = false;

		while (descChain) {
			switch(descChain->_structureType) {
			case TypeHashCode<SharpenOperatorDesc>:
				result._sharpen = Internal::ChainedOperatorCast<SharpenOperatorDesc>(*descChain);
				foundSomething = true;
				break;

			case TypeHashCode<FilmGrainDesc>:
				result._filmGrain = Internal::ChainedOperatorCast<FilmGrainDesc>(*descChain);
				foundSomething = true;
				break;
			}
			descChain = descChain->_next;
		}

		if (foundSomething)
			return result;
		return {};
	}

	PostProcessOperator::PostProcessOperator(
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
		const CombinedDesc& desc)
	: _pool(std::move(pipelinePool))
	, _secondStageConstructionState(0)
	, _desc(desc)
	{}

	PostProcessOperator::~PostProcessOperator()
	{}
}}
