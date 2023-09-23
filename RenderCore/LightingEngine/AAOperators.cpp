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
		IResourceView& hdrColor,
		IResourceView& output,
		IResourceView& outputPrev,
		IResourceView& motion,
		IResourceView& depth)
	{
		assert(_secondStageConstructionState == 2);

		IResourceView* srvs[] {
			&hdrColor, &output, &outputPrev, &motion, &depth
		};

		assert(_desc._timeConstant > 0.f);
		struct ControlUniforms
		{
			UInt2 _bufferDims;
			unsigned _hasHistory;
			float _blendingAlpha;
		} controlUniforms {
			UInt2 { parsingContext.GetFrameBufferProperties()._width, parsingContext.GetFrameBufferProperties()._height },
			!_firstFrame,
			1.f - exp(-1.0f / _desc._timeConstant)
		};
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
		_firstFrame = false;
	}

	RenderStepFragmentInterface TAAOperator::CreateFragment(const FrameBufferProperties& fbProps)
	{
		assert(_secondStageConstructionState == 0);
		RenderStepFragmentInterface result{PipelineType::Compute};

		auto colorHDR = result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).InitialState(BindFlag::RenderTarget).FinalState(BindFlag::ShaderResource);
		auto output = result.DefineAttachment("AAOutput"_h).NoInitialState().FinalState(BindFlag::UnorderedAccess);
		auto outputPrev = result.DefineAttachment("AAOutput"_h+1).InitialState(BindFlag::UnorderedAccess).Discard();
		auto gbufferMotion = result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion).InitialState(BindFlag::ShaderResource).Discard();
		auto depth = result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).InitialState(BindFlag::DepthStencil).FinalState(BindFlag::ShaderResource);

		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		spDesc.AppendNonFrameBufferAttachmentView(colorHDR, BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(output, BindFlag::UnorderedAccess);
		spDesc.AppendNonFrameBufferAttachmentView(outputPrev, BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(gbufferMotion, BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(depth, BindFlag::ShaderResource, TextureViewDesc { TextureViewDesc::Aspect::Depth });
		spDesc.SetName("taa-operator");

		result.AddSubpass(
			std::move(spDesc),
			[op=shared_from_this()](SequenceIterator& iterator) {
				{
					Metal::BarrierHelper barrierHelper{iterator._parsingContext->GetThreadContext()};
					// TAAOutput initialize
					barrierHelper.Add(
						*iterator._rpi.GetNonFrameBufferAttachmentView(1)->GetResource(), 
						Metal::BarrierResourceUsage::NoState(),
						{BindFlag::UnorderedAccess, ShaderStage::Compute});
					// TAAOutputPrev UnorderedAccess -> ShaderResource
					barrierHelper.Add(
						*iterator._rpi.GetNonFrameBufferAttachmentView(2)->GetResource(), 
						{BindFlag::UnorderedAccess, ShaderStage::Compute},
						{BindFlag::ShaderResource, ShaderStage::Compute});
					// depth DepthStencil -> ShaderResource
					barrierHelper.Add(
						*iterator._rpi.GetNonFrameBufferAttachmentView(4)->GetResource(), 
						{BindFlag::DepthStencil, ShaderStage::Pixel},
						{BindFlag::ShaderResource, ShaderStage::Compute});
				}

				op->Execute(
					*iterator._parsingContext,
					*iterator._rpi.GetNonFrameBufferAttachmentView(0),
					*iterator._rpi.GetNonFrameBufferAttachmentView(1),
					*iterator._rpi.GetNonFrameBufferAttachmentView(2),
					*iterator._rpi.GetNonFrameBufferAttachmentView(3),
					*iterator._rpi.GetNonFrameBufferAttachmentView(4));
			});

		return result;
	}

	void TAAOperator::PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, const FrameBufferProperties& fbProps)
	{
		auto outputFmt = Format::R11G11B10_FLOAT;

		// copy the format from ColorHDR, if we can find it
		auto i = std::find_if(
			stitchingContext.GetPreregisteredAttachments().begin(),
			stitchingContext.GetPreregisteredAttachments().end(),
			[](const auto& q) { return q._semantic == Techniques::AttachmentSemantics::ColorHDR; });
		if (i != stitchingContext.GetPreregisteredAttachments().end())
			outputFmt = i->_desc._textureDesc._format;

		UInt2 fbSize{fbProps._width, fbProps._height};
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				"AAOutput"_h,
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], outputFmt)),
				"taa-output"
			}
		};
		for (const auto& a:attachments)
			stitchingContext.DefineAttachment(a);
		stitchingContext.DefineDoubleBufferAttachment("AAOutput"_h, MakeClearValue(0.f, 0.f, 0.f, 0.f), BindFlag::UnorderedAccess);
	}

	void TAAOperator::SecondStageConstruction(
		std::promise<std::shared_ptr<TAAOperator>>&& promise,
		const Techniques::FrameBufferTarget& fbTarget)
	{
		assert(_secondStageConstructionState == 0);
		_secondStageConstructionState = 1;

		UniformsStreamInterface usi;
		usi.BindResourceView(0, "ColorHDR"_h);
		usi.BindResourceView(1, "Output"_h);
		usi.BindResourceView(2, "OutputPrev"_h);
		usi.BindResourceView(3, "GBufferMotion"_h);
		usi.BindResourceView(4, "Depth"_h);
		usi.BindImmediateData(0, "ControlUniforms"_h);

		ParameterBox selectors;
		selectors.SetParameter("PLAYDEAD_NEIGHBOURHOOD_SEARCH", _desc._findOptimalMotionVector);
		selectors.SetParameter("CATMULL_ROM_SAMPLING", _desc._catmullRomSampling);

		auto futureAAResolve = Techniques::CreateComputeOperator(
			_pool,
			TAA_COMPUTE_HLSL ":ResolveTemporal",
			std::move(selectors),
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
