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
#include "../UniformsStream.h"
#include "../../Assets/Continuation.h"
#include "../../xleres/FileList.h"

#define FFX_CPU 1
#include "../../Foreign/FidelityFX-SDK/sdk/include/FidelityFX/gpu/ffx_core.h"
#include "../../Foreign/FidelityFX-SDK/sdk/include/FidelityFX/gpu/cas/ffx_cas.h"

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine 
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
				} controlUniforms;

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

				const unsigned groupSize = 16;
				op->_shader->Dispatch(
					parsingContext,
					(outputDims[0] + groupSize - 1) / groupSize, (outputDims[1] + groupSize - 1) / groupSize, 1,
					pass.GetNextUniformsStream(),
					ImmediateDataStream { controlUniforms });
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
		UniformsStreamInterface nonAttachmentUsi;
		nonAttachmentUsi.BindImmediateData(0, "ControlUniforms"_h);

		auto shader = Techniques::CreateComputeOperator(
			_pool,
			POSTPROCESS_COMPUTE_HLSL ":main",
			std::move(selectors),
			GENERAL_OPERATOR_PIPELINE ":ComputeMain",
			_attachmentUsi,
			nonAttachmentUsi);

		::Assets::WhenAll(shader).ThenConstructToPromise(
			std::move(promise),
			[strongThis = shared_from_this()](auto shader) {
				assert(strongThis->_secondStageConstructionState == 1);
				strongThis->_shader = std::move(shader);
				strongThis->_secondStageConstructionState = 2;
				return strongThis;
			});
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
