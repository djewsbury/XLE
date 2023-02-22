// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OverlayEffects.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/PipelineOperators.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/DrawableDelegates.h"		// for IUnformDelegateManager
#include "../RenderCore/UniformsStream.h"
#include "../Assets/Marker.h"
#include "../xleres/FileList.h"

using namespace Utility::Literals;

namespace RenderOverlays
{
	struct CB_BlurControlUniforms
	{
		float _blurWeights[6];
		uint32_t _srgbConversionOnInput = false;
		uint32_t _srgbConversionOnOutput = false;
		void CalculateBlurWeights(float radius);
	};

	static float GaussianWeight1D(float offset, float stdDevSq)
	{
		// See https://en.wikipedia.org/wiki/Gaussian_blur
		const float twiceStdDevSq = 2.0f * stdDevSq;
		const float C = 1.0f / std::sqrt(gPI * twiceStdDevSq);
		return C * std::exp(-offset*offset / twiceStdDevSq);
	}

	void CB_BlurControlUniforms::CalculateBlurWeights(float radius)
	{
		// Calculate radius such that 1.5*stdDev = radius
		// This is selected because it just tends to match the blur size we get with the large radius blur
		float stdDevSq = radius * radius / (1.5f * 1.5f);
		float weightSum = 0;
		for (unsigned c=0; c<dimof(_blurWeights); ++c) {
			_blurWeights[c] = GaussianWeight1D(float(c), stdDevSq);
			weightSum += _blurWeights[c];
			if (c!=0) weightSum += _blurWeights[c];
		}

		// renormalize weights, to ensure we don't darken the colour, even when blur radius is too big for
		// the kernel to handle
		for (unsigned c=0; c<dimof(_blurWeights); ++c)
			_blurWeights[c] /= weightSum;
	}

	static std::shared_ptr<RenderCore::IResourceView> GenerateBlurryBackground(
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::Techniques::IComputeShaderOperator& pipelineOperator)
	{
		using namespace RenderCore;

		// bring up-to-date compute, because it's typically invalidated at this point
		parsingContext.GetUniformDelegateManager()->BringUpToDateCompute(parsingContext);

		Techniques::FrameBufferDescFragment fbFragment;
		fbFragment._pipelineType = PipelineType::Compute;
		fbFragment.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR).FinalState(BindFlag::UnorderedAccess);
		fbFragment.DefineAttachment("BlurryBackground"_h).NoInitialState().FinalState(BindFlag::ShaderResource).FixedFormat(Format::R8G8B8A8_UNORM).RequireBindFlags(BindFlag::UnorderedAccess);
		fbFragment.DefineAttachment("BlurryBackgroundTemp"_h).NoInitialState().FinalState(BindFlag::ShaderResource).FixedFormat(Format::R8G8B8A8_UNORM).RequireBindFlags(BindFlag::UnorderedAccess);
		Techniques::FrameBufferDescFragment::SubpassDesc sp;
		sp.AppendNonFrameBufferAttachmentView(0, BindFlag::UnorderedAccess);
		sp.AppendNonFrameBufferAttachmentView(1, BindFlag::UnorderedAccess);
		sp.AppendNonFrameBufferAttachmentView(2, BindFlag::UnorderedAccess);
		sp.SetName("blurry-background");
		fbFragment.AddSubpass(std::move(sp));

		Techniques::RenderPassInstance rpi { parsingContext, fbFragment };
		rpi.AutoNonFrameBufferBarrier({
			{0, BindFlag::UnorderedAccess, ShaderStage::Compute},
			{1, BindFlag::UnorderedAccess, ShaderStage::Compute},
			{2, BindFlag::UnorderedAccess, ShaderStage::Compute}
		});

		CB_BlurControlUniforms params;
		params.CalculateBlurWeights(4.f);
		params._srgbConversionOnInput = false;
		params._srgbConversionOnOutput = true;
		IResourceView* srvs[] { rpi.GetNonFrameBufferAttachmentView(0).get(), rpi.GetNonFrameBufferAttachmentView(2).get() };
		UniformsStream::ImmediateData immDatas[] { MakeOpaqueIteratorRange(params) };
		UniformsStream uniforms;
		uniforms._resourceViews = MakeIteratorRange(srvs);
		uniforms._immediateData = MakeIteratorRange(immDatas);
		pipelineOperator.Dispatch(
			parsingContext,
			(parsingContext.GetFragmentStitchingContext()._workingProps._width + 7) / 8,
			(parsingContext.GetFragmentStitchingContext()._workingProps._height + 7) / 8,
			1,
			uniforms);

		// Blur again, since with the kernel, successive blurs is the same as blurring with
		// a broader kernel

		srvs[0] = rpi.GetNonFrameBufferAttachmentView(2).get();
		srvs[1] = rpi.GetNonFrameBufferAttachmentView(1).get();
		params._srgbConversionOnInput = true;
		params._srgbConversionOnOutput = true;
		pipelineOperator.Dispatch(
			parsingContext,
			(parsingContext.GetFragmentStitchingContext()._workingProps._width + 7) / 8,
			(parsingContext.GetFragmentStitchingContext()._workingProps._height + 7) / 8,
			1,
			uniforms);

		rpi.AutoNonFrameBufferBarrier({
			{1, BindFlag::ShaderResource, ShaderStage::Compute}
		});

		// return an SRGB embued texture view
		return rpi.GetNonFrameBufferAttachmentView(1)->GetResource()->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::Aspect::ColorSRGB});
	}

	std::shared_ptr<RenderCore::IResourceView> BlurryBackgroundEffect::GetResourceView()
	{
		assert(_parsingContext);
		if (!_backgroundResource) {
			// generate the blurry background now (at least, if the shader has finished loading)
			auto *pipelineOperator = _pipelineOperator->TryActualize();
			if (pipelineOperator)
				_backgroundResource = GenerateBlurryBackground(*_parsingContext, *pipelineOperator->get());
		}
		return _backgroundResource;
	}

	BlurryBackgroundEffect::BlurryBackgroundEffect(RenderCore::Techniques::ParsingContext& parsingContext)
	: _parsingContext(&parsingContext)
	{
		RenderCore::UniformsStreamInterface usi;
		usi.BindResourceView(0, "InputTexture"_h);
		usi.BindResourceView(1, "OutputTexture"_h);
		usi.BindImmediateData(0, "ControlUniforms"_h);
		_pipelineOperator = RenderCore::Techniques::CreateComputeOperator(
			parsingContext.GetTechniqueContext()._graphicsPipelinePool,
			RENDEROVERLAYS_SEPARABLE_FILTER ":Gaussian11RGB",
			ParameterBox{},
			GENERAL_OPERATOR_PIPELINE ":ComputeMain",
			usi);
	}

	BlurryBackgroundEffect::~BlurryBackgroundEffect()
	{}
}

