// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AAOperators.h"
#include "RenderStepFragments.h"
#include "SequenceIterator.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/CommonBindings.h"
#include "../Metal/Resource.h"
#include "../UniformsStream.h"
#include "../../Assets/Continuation.h"
#include "../../xleres/FileList.h"

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine 
{

	void TAAOperator::Execute(
		Techniques::ParsingContext& parsingContext,
		IResourceView& hdrInputAndOutput,
		IResourceView& hdrColorPrev,
		IResourceView& motion,
		IResourceView& depth)
	{
		assert(_secondStageConstructionState == 2);

		IResourceView* srvs[] {
			&hdrInputAndOutput, &hdrColorPrev, &motion, &depth
		};

		struct ControlUniforms
		{
			unsigned _hasHistory = true;
			unsigned _u[3];
		} controlUniforms;
		UniformsStream::ImmediateData immDatas[] = { MakeOpaqueIteratorRange(controlUniforms) };
		UniformsStream uniforms;
		uniforms._resourceViews = srvs;
		uniforms._immediateData = immDatas;

		UInt2 outputDims { parsingContext.GetFrameBufferProperties()._width, parsingContext.GetFrameBufferProperties()._height };
		const unsigned groupSize = 16;
		_aaResolve->Dispatch(
			parsingContext,
			(outputDims[0] + groupSize - 1) / groupSize, (outputDims[1] + groupSize - 1) / groupSize, 1,
			uniforms);
	}

	RenderStepFragmentInterface TAAOperator::CreateFragment(const FrameBufferProperties& fbProps)
	{
		assert(_secondStageConstructionState == 0);
		RenderStepFragmentInterface result{PipelineType::Compute};

		auto colorHDR = result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).InitialState(BindFlag::RenderTarget).FinalState(BindFlag::UnorderedAccess);
		auto colorHDRPrev = result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDRPrev).InitialState(BindFlag::UnorderedAccess).Discard();
		auto gbufferMotion = result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion).InitialState(BindFlag::ShaderResource).Discard();
		auto depth = result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).InitialState(BindFlag::DepthStencil).FinalState(BindFlag::ShaderResource);

		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		spDesc.AppendNonFrameBufferAttachmentView(colorHDR, BindFlag::UnorderedAccess);
		spDesc.AppendNonFrameBufferAttachmentView(colorHDRPrev, BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(gbufferMotion, BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(depth, BindFlag::ShaderResource);
		spDesc.SetName("taa-operator");

		result.AddSubpass(
			std::move(spDesc),
			[op=shared_from_this()](SequenceIterator& iterator) {

				{
					Metal::BarrierHelper barrierHelper{iterator._parsingContext->GetThreadContext()};
					// ColorHDRPrev UnorderedAccess -> ShaderResource
					barrierHelper.Add(
						*iterator._rpi.GetNonFrameBufferAttachmentView(1)->GetResource(), 
						{BindFlag::UnorderedAccess},
						{BindFlag::ShaderResource, ShaderStage::Compute});
					barrierHelper.Add(
						*iterator._rpi.GetNonFrameBufferAttachmentView(3)->GetResource(), 
						{BindFlag::DepthStencil, ShaderStage::Pixel},
						{BindFlag::ShaderResource, ShaderStage::Compute});
				}

				op->Execute(
					*iterator._parsingContext,
					*iterator._rpi.GetNonFrameBufferAttachmentView(0),
					*iterator._rpi.GetNonFrameBufferAttachmentView(1),
					*iterator._rpi.GetNonFrameBufferAttachmentView(2),
					*iterator._rpi.GetNonFrameBufferAttachmentView(3));

				// Metal::BarrierHelper{iterator._parsingContext->GetThreadContext()}.Add(*ldrOutput.GetResource(), {BindFlag::UnorderedAccess, ShaderStage::Compute}, BindFlag::RenderTarget);
			});

		return result;
	}

	void TAAOperator::PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, const FrameBufferProperties& fbProps)
	{
		stitchingContext.DefineDoubleBufferAttachment(Techniques::AttachmentSemantics::ColorHDR, MakeClearValue(0.f, 0.f, 0.f, 0.f), BindFlag::UnorderedAccess);
	}

	void TAAOperator::SecondStageConstruction(
		std::promise<std::shared_ptr<TAAOperator>>&& promise,
		const Techniques::FrameBufferTarget& fbTarget)
	{
		assert(_secondStageConstructionState == 0);
		_secondStageConstructionState = 1;

		UniformsStreamInterface usi;
		usi.BindResourceView(0, "ColorHDR"_h);
		usi.BindResourceView(1, "ColorHDRPrev"_h);
		usi.BindResourceView(2, "GBufferMotion"_h);
		usi.BindResourceView(3, "Depth"_h);
		usi.BindImmediateData(0, "ControlUniforms"_h);

		auto futureAAResolve = Techniques::CreateComputeOperator(
			_pool,
			TAA_COMPUTE_HLSL ":ResolveTemporal",
			{},
			GENERAL_OPERATOR_PIPELINE ":ComputeMain",
			usi);

		::Assets::WhenAll(futureAAResolve).ThenConstructToPromise(
			std::move(promise),
			[strongThis = shared_from_this()](auto aaResolve) {
				assert(strongThis->_secondStageConstructionState == 1);
				strongThis->_aaResolve = std::move(aaResolve);
				strongThis->_secondStageConstructionState = 2;
				return strongThis;
			});
	}

	::Assets::DependencyValidation TAAOperator::GetDependencyValidation() const
	{
		assert(_secondStageConstructionState==2);
		return _aaResolve->GetDependencyValidation();
	}

	TAAOperator::TAAOperator(
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
		const TAAOperatorDesc& desc)
	: _pool(std::move(pipelinePool))
	, _secondStageConstructionState(0)
	, _desc(desc)
	{
	}

	TAAOperator::~TAAOperator()
	{}

}}
