// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TonemapOperator.h"
#include "LightingEngineIterator.h"
#include "RenderStepFragments.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/PipelineOperators.h"
#include "../UniformsStream.h"
#include "../../Assets/Continuation.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace LightingEngine 
{
	void ToneMapAcesOperator::Execute(Techniques::ParsingContext& parsingContext, IResourceView& ldrOutput, IResourceView& hdrInput)
	{
		auto fbProps = parsingContext._rpi->GetFrameBufferDesc().GetProperties();
		assert(fbProps._width != 0 && fbProps._height != 0);
		const unsigned dispatchGroupWidth = 8;
		const unsigned dispatchGroupHeight = 8;
		ResourceViewStream uniforms {
			hdrInput, ldrOutput,
			*_params,
		};
		_shader->Dispatch(
			parsingContext,
			(fbProps._width + dispatchGroupWidth - 1) / dispatchGroupWidth,
			(fbProps._height + dispatchGroupHeight - 1) / dispatchGroupHeight,
			1,
			uniforms);
	}

	::Assets::DependencyValidation ToneMapAcesOperator::GetDependencyValidation() const { return _shader->GetDependencyValidation(); }

	RenderStepFragmentInterface ToneMapAcesOperator::CreateFragment(const FrameBufferProperties& fbProps)
    {
        RenderStepFragmentInterface result{PipelineType::Compute};

		// todo -- what should we set the final state for ColorLDR to be here? just go directly to PresentationSrc?
        Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR).NoInitialState().FinalState(BindFlag::RenderTarget), BindFlag::UnorderedAccess);
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).InitialState(BindFlag::RenderTarget).Discard());
        spDesc.SetName("tone-map-aces-operator");

        result.AddSubpass(
            std::move(spDesc),
            [op=shared_from_this()](LightingTechniqueIterator& iterator) {
                op->Execute(
                    *iterator._parsingContext,
                    *iterator._rpi.GetNonFrameBufferAttachmentView(0),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(1));
            });

        return result;
    }

	void ToneMapAcesOperator::PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext)
	{
		// todo -- should we actually define the ColorHDR attachment here?
	}

	ToneMapAcesOperator::ToneMapAcesOperator(
		const ToneMapAcesOperatorDesc& desc,
		std::shared_ptr<Techniques::IComputeShaderOperator> shader,
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool)
	: _shader(std::move(shader))
	{
		_device = pipelinePool->GetDevice();
		_pool = std::move(pipelinePool);
	}

	ToneMapAcesOperator::~ToneMapAcesOperator()
	{}

	void ToneMapAcesOperator::ConstructToPromise(
		std::promise<std::shared_ptr<ToneMapAcesOperator>>&& promise,
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
		const ToneMapAcesOperatorDesc& desc)
	{
		UniformsStreamInterface usi;
		usi.BindResourceView(0, Hash64("HDRInput"));
		usi.BindResourceView(1, Hash64("LDROutput"));
		usi.BindResourceView(2, Hash64("Params"));

		ParameterBox params;

		// We could do tonemapping in a pixel shader with an input attachment
		// but it's probanly more practical to just use a compute shader
		auto futureShader = Techniques::CreateComputeOperator(
			pipelinePool,
			TONEMAP_ACES_COMPUTE_HLSL ":main",
			params,
			GENERAL_OPERATOR_PIPELINE ":ComputeMain",
			usi);
		::Assets::WhenAll(futureShader).ThenConstructToPromise(
			std::move(promise),
			[desc, pipelinePool=std::move(pipelinePool)](auto shader) {
				return std::make_shared<ToneMapAcesOperator>(desc, std::move(shader), pipelinePool);
			});
	}

	uint64_t ToneMapAcesOperatorDesc::GetHash() const
	{
		return 0;
	}
}}

