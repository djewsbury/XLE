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
		IResourceView& depth,
		IResourceView* outputShaderResource,
		IResourceView* outputPrevUnorderedAccess)
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

		{
			Metal::BarrierHelper barrierHelper{parsingContext.GetThreadContext()};
			// AAOutput UnorderedAccess -> ShaderResource
			barrierHelper.Add(
				*output.GetResource(), 
				{BindFlag::UnorderedAccess, ShaderStage::Compute},
				{BindFlag::ShaderResource, ShaderStage::Compute});
			// AAOutputPrev NoState -> UnorderedAccess
			if (_desc._sharpenHistory)
				barrierHelper.Add(
					*outputPrev.GetResource(),
					{BindFlag::ShaderResource, ShaderStage::Compute},
					{BindFlag::UnorderedAccess, ShaderStage::Compute});
		}

		if (_desc._sharpenHistory) {
			assert(outputPrevUnorderedAccess);
			IResourceView* srvs[] {
				outputPrevUnorderedAccess, outputShaderResource
			};
			uniforms._resourceViews = srvs;

			const unsigned groupSize = 8;
			_sharpenFutureYesterday->Dispatch(
				parsingContext,
				(outputDims[0] + groupSize - 1) / groupSize, (outputDims[1] + groupSize - 1) / groupSize, 1,
				uniforms);
		}

		_firstFrame = false;
	}

	RenderStepFragmentInterface TAAOperator::CreateFragment(const FrameBufferProperties& fbProps)
	{
		assert(_secondStageConstructionState == 0);
		RenderStepFragmentInterface result{PipelineType::Compute};

		auto colorHDR = result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).InitialState(BindFlag::RenderTarget).FinalState(BindFlag::ShaderResource);
		auto output = result.DefineAttachment("AAOutput"_h).NoInitialState().FinalState(BindFlag::ShaderResource);
		auto outputPrev = result.DefineAttachment("AAOutput"_h+1).InitialState(BindFlag::ShaderResource).Discard();
		if (_desc._sharpenHistory)
			outputPrev.NoInitialState().FinalState(BindFlag::UnorderedAccess);
		auto gbufferMotion = result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion).InitialState(BindFlag::ShaderResource).Discard();
		auto depth = result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).InitialState(BindFlag::DepthStencil).FinalState(BindFlag::ShaderResource);

		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		spDesc.AppendNonFrameBufferAttachmentView(colorHDR, BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(output, BindFlag::UnorderedAccess);
		spDesc.AppendNonFrameBufferAttachmentView(outputPrev, BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(gbufferMotion, BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(depth, BindFlag::ShaderResource, TextureViewDesc { TextureViewDesc::Aspect::Depth });
		if (_desc._sharpenHistory) {
			spDesc.AppendNonFrameBufferAttachmentView(output, BindFlag::ShaderResource);
			spDesc.AppendNonFrameBufferAttachmentView(outputPrev, BindFlag::UnorderedAccess);
		}
		spDesc.SetName("taa-operator");

		result.AddSubpass(
			std::move(spDesc),
			[op=shared_from_this()](SequenceIterator& iterator) {
				{
					Metal::BarrierHelper barrierHelper{iterator._parsingContext->GetThreadContext()};
					// AAOutput initialize
					barrierHelper.Add(
						*iterator._rpi.GetNonFrameBufferAttachmentView(1)->GetResource(),
						Metal::BarrierResourceUsage::NoState(),
						{BindFlag::UnorderedAccess, ShaderStage::Compute});
					// depth DepthStencil -> ShaderResource
					barrierHelper.Add(
						*iterator._rpi.GetNonFrameBufferAttachmentView(4)->GetResource(),
						{BindFlag::DepthStencil, ShaderStage::Pixel},
						{BindFlag::ShaderResource, ShaderStage::Compute});
					// AAOutputPrev UnorderedAccess -> ShaderResource
					if (op->_desc._sharpenHistory) {
						if (op->_firstFrame) {
							barrierHelper.Add(
								*iterator._rpi.GetNonFrameBufferAttachmentView(2)->GetResource(),
								Metal::BarrierResourceUsage::NoState(),
								{BindFlag::ShaderResource, ShaderStage::Compute});
						} else
							barrierHelper.Add(
								*iterator._rpi.GetNonFrameBufferAttachmentView(2)->GetResource(),
								{BindFlag::UnorderedAccess, ShaderStage::Compute},
								{BindFlag::ShaderResource, ShaderStage::Compute});
					}
				}

				IResourceView* outputShaderResource = nullptr, *outputPrevUnorderedAccess = nullptr;
				if (op->_desc._sharpenHistory) {
					outputShaderResource = iterator._rpi.GetNonFrameBufferAttachmentView(5).get();
					outputPrevUnorderedAccess = iterator._rpi.GetNonFrameBufferAttachmentView(6).get();
				}

				op->Execute(
					*iterator._parsingContext,
					*iterator._rpi.GetNonFrameBufferAttachmentView(0),
					*iterator._rpi.GetNonFrameBufferAttachmentView(1),
					*iterator._rpi.GetNonFrameBufferAttachmentView(2),
					*iterator._rpi.GetNonFrameBufferAttachmentView(3),
					*iterator._rpi.GetNonFrameBufferAttachmentView(4),
					outputShaderResource, outputPrevUnorderedAccess);
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
		stitchingContext.DefineAttachment(
			Techniques::PreregisteredAttachment {
				"AAOutput"_h,
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], outputFmt)),
				"taa-output"
			});

		if (_desc._sharpenHistory) {
			// When we have this flag, ww will copy to a "prev" buffer manually (applying the sharpening as we do)
			stitchingContext.DefineAttachment(
				Techniques::PreregisteredAttachment {
					"AAOutput"_h+1,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource,
						TextureDesc::Plain2D(fbSize[0], fbSize[1], outputFmt)),
					"taa-output-prev"
				});
		} else
			stitchingContext.DefineDoubleBufferAttachment("AAOutput"_h, MakeClearValue(0.f, 0.f, 0.f, 0.f), BindFlag::ShaderResource);
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

		if (_desc._sharpenHistory) {
			UniformsStreamInterface usi2;
			usi2.BindResourceView(0, "Output"_h);
			usi2.BindResourceView(1, "ColorHDR"_h);
			usi2.BindImmediateData(0, "ControlUniforms"_h);

			auto sharpenFutureYesterday = Techniques::CreateComputeOperator(
				_pool,
				TAA_COMPUTE_HLSL ":UpdateHistory",
				{},
				GENERAL_OPERATOR_PIPELINE ":ComputeMain",
				usi2);

			::Assets::WhenAll(futureAAResolve, sharpenFutureYesterday).ThenConstructToPromise(
				std::move(promise),
				[strongThis = shared_from_this()](auto aaResolve, auto sharpenFutureYesterday) {
					assert(strongThis->_secondStageConstructionState == 1);
					strongThis->_aaResolve = std::move(aaResolve);
					strongThis->_sharpenFutureYesterday = std::move(sharpenFutureYesterday);
					strongThis->_secondStageConstructionState = 2;
					return strongThis;
				});
		} else {
			::Assets::WhenAll(futureAAResolve).ThenConstructToPromise(
				std::move(promise),
				[strongThis = shared_from_this()](auto aaResolve) {
					assert(strongThis->_secondStageConstructionState == 1);
					strongThis->_aaResolve = std::move(aaResolve);
					strongThis->_secondStageConstructionState = 2;
					return strongThis;
				});
		}
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
