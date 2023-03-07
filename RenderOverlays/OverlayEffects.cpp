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
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/Services.h"
#include "../RenderCore/UniformsStream.h"
#include "../RenderCore/Metal/Resource.h"		// metal only required for barriers
#include "../RenderCore/Metal/DeviceContext.h"
#include "../ConsoleRig/Console.h"
#include "../Assets/Marker.h"
#include "../Assets/Continuation.h"
#include "../Assets/Assets.h"
#include "../xleres/FileList.h"

using namespace Utility::Literals;

#pragma warning(disable:4200)		// nonstandard extension used: zero-sized array in struct/union

namespace RenderOverlays
{
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class GaussianBlurOperator
	{
	public:
		struct CB_BlurControlUniforms
		{
			uint32_t _srgbConversionOnInput = false;
			uint32_t _srgbConversionOnOutput = false;
			uint32_t _dummy[2];
			Float4 _blurWeights[];

			void CalculateBlurWeights(float radius, unsigned blurTapCount);
			static unsigned CalculateSize(unsigned blurTapCount);
		};

		std::shared_ptr<RenderCore::IResourceView> Execute(
			RenderCore::Techniques::ParsingContext& parsingContext,
			float blurRadius,
			uint64_t inputAttachment = RenderCore::Techniques::AttachmentSemantics::ColorLDR);

		::Assets::DependencyValidation GetDependencyValidation() const { return _pipelineOperator->GetDependencyValidation(); }

		GaussianBlurOperator(std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> pipelineOperator, unsigned tapCount = 11);
		static void ConstructToPromise(
			std::promise<std::shared_ptr<GaussianBlurOperator>>&& promise,
			const std::shared_ptr<RenderCore::Techniques::PipelineCollection>& pool,
			unsigned tapCount);
	private:
		std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> _pipelineOperator;
		unsigned _tapCount;
	};

	static float GaussianWeight1D(float offset, float stdDevSq)
	{
		// See https://en.wikipedia.org/wiki/Gaussian_blur
		const float twiceStdDevSq = 2.0f * stdDevSq;
		const float C = 1.0f / std::sqrt(gPI * twiceStdDevSq);
		return C * std::exp(-offset*offset / twiceStdDevSq);
	}

	unsigned GaussianBlurOperator::CB_BlurControlUniforms::CalculateSize(unsigned blurTapCount)
	{
		auto weightCount = 1+(blurTapCount-1)/2;
		return sizeof(uint32_t)*4*(1+weightCount);
	}

	void GaussianBlurOperator::CB_BlurControlUniforms::CalculateBlurWeights(float radius, unsigned blurTapCount)
	{
		// Calculate radius such that 1.5*stdDev = radius
		// This is selected because it just tends to match the blur size we get with the large radius blur
		float stdDevSq = radius * radius / (1.5f * 1.5f);
		float weightSum = 0;
		auto weightCount = 1+(blurTapCount-1)/2;
		for (unsigned c=0; c<weightCount; ++c) {
			_blurWeights[c] = Float4{GaussianWeight1D(float(c), stdDevSq), 0.f, 0.f, 0.f};
			weightSum += _blurWeights[c][0];
			if (c!=0) weightSum += _blurWeights[c][0];
		}

		// renormalize weights, to ensure we don't darken the colour, even when blur radius is too big for
		// the kernel to handle
		for (unsigned c=0; c<weightCount; ++c)
			_blurWeights[c][0] /= weightSum;
	}

	std::shared_ptr<RenderCore::IResourceView> GaussianBlurOperator::Execute(
		RenderCore::Techniques::ParsingContext& parsingContext,
		float blurRadius, uint64_t inputAttachment)
	{
		using namespace RenderCore;

		// True gaussian blur, but smaller blur radius
		Techniques::FrameBufferDescFragment fbFragment;
		fbFragment._pipelineType = PipelineType::Compute;
		fbFragment.DefineAttachment(inputAttachment).FinalState(BindFlag::UnorderedAccess);
		fbFragment.DefineAttachment("BlurryBackground"_h).NoInitialState().FinalState(BindFlag::ShaderResource).FixedFormat(Format::R8G8B8A8_UNORM).RequireBindFlags(BindFlag::UnorderedAccess);
		fbFragment.DefineAttachment("BlurryBackgroundTemp"_h).NoInitialState().FinalState(BindFlag::ShaderResource).FixedFormat(Format::R8G8B8A8_UNORM).RequireBindFlags(BindFlag::UnorderedAccess);
		Techniques::FrameBufferDescFragment::SubpassDesc sp;
		sp.AppendNonFrameBufferAttachmentView(0, BindFlag::UnorderedAccess);
		sp.AppendNonFrameBufferAttachmentView(1, BindFlag::UnorderedAccess);
		sp.AppendNonFrameBufferAttachmentView(2, BindFlag::UnorderedAccess);
		sp.SetName("gaussian-blur");
		fbFragment.AddSubpass(std::move(sp));

		Techniques::RenderPassInstance rpi { parsingContext, fbFragment };
		rpi.AutoNonFrameBufferBarrier({
			{0, BindFlag::UnorderedAccess, ShaderStage::Compute},
			{1, BindFlag::UnorderedAccess, ShaderStage::Compute},
			{2, BindFlag::UnorderedAccess, ShaderStage::Compute}
		});

		auto paramsSize = CB_BlurControlUniforms::CalculateSize(_tapCount);
		VLA(uint8_t, paramsBlock, paramsSize);
		CB_BlurControlUniforms& params = *new (paramsBlock) CB_BlurControlUniforms;
		params.CalculateBlurWeights(blurRadius, _tapCount);
		
		IResourceView* srvs[2];
		UniformsStream::ImmediateData immDatas[] { MakeIteratorRange(paramsBlock, paramsBlock + paramsSize) };
		UniformsStream uniforms;
		uniforms._resourceViews = MakeIteratorRange(srvs);
		uniforms._immediateData = MakeIteratorRange(immDatas);
		const unsigned blockSize = 16;

		// Blur multiple times, since with the kernel successive blurs is the same as blurring with
		// a broader kernel
		unsigned blurPassCount = Tweakable("BlurPassCount", 4);
		blurPassCount = std::clamp(blurPassCount, 2u, 16u);
		blurPassCount &= (~1u);		// use even number of passes
		for (unsigned c=0; c<blurPassCount; ++c) {
			// input
			if (c==0) {
				srvs[0] = rpi.GetNonFrameBufferAttachmentView(0).get();
			} else {
				if (c&1) {
					srvs[0] = rpi.GetNonFrameBufferAttachmentView(2).get();
				} else {
					srvs[0] = rpi.GetNonFrameBufferAttachmentView(1).get();
				}
			}
			// output
			if (c&1) {
				srvs[1] = rpi.GetNonFrameBufferAttachmentView(1).get();
			} else {
				srvs[1] = rpi.GetNonFrameBufferAttachmentView(2).get();
			}

			params._srgbConversionOnInput = (c==0) ? false : true;
			params._srgbConversionOnOutput = true;
			_pipelineOperator->Dispatch(
				parsingContext,
				(parsingContext.GetFragmentStitchingContext()._workingProps._width + blockSize - 1) / blockSize,
				(parsingContext.GetFragmentStitchingContext()._workingProps._height + blockSize - 1) / blockSize,
				1,
				uniforms);
		}

		rpi.AutoNonFrameBufferBarrier({
			{1, BindFlag::ShaderResource, ShaderStage::Compute}
		});

		// return an SRGB embued texture view
		return rpi.GetNonFrameBufferAttachmentView(1)->GetResource()->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::Aspect::ColorSRGB});
	}

	GaussianBlurOperator::GaussianBlurOperator(std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> pipelineOperator, unsigned tapCount)
	: _pipelineOperator(std::move(pipelineOperator)), _tapCount(tapCount) {}

	void GaussianBlurOperator::ConstructToPromise(
		std::promise<std::shared_ptr<GaussianBlurOperator>>&& promise,
		const std::shared_ptr<RenderCore::Techniques::PipelineCollection>& pool,
		unsigned tapCount)
	{
		assert((tapCount&1) == 1);		// tap count must be odd (and should generally be 11 or higher)
		RenderCore::UniformsStreamInterface usi;
		usi.BindResourceView(0, "InputTexture"_h);
		usi.BindResourceView(1, "OutputTexture"_h);
		usi.BindImmediateData(0, "ControlUniforms"_h);
		ParameterBox selectors;
		selectors.SetParameter("TAP_COUNT", tapCount);
		auto futurePipelineOperator = RenderCore::Techniques::CreateComputeOperator(
			pool,
			RENDEROVERLAYS_SEPARABLE_FILTER ":GaussianRGB",
			std::move(selectors),
			GENERAL_OPERATOR_PIPELINE ":ComputeMain",
			usi);
		::Assets::WhenAll(std::move(futurePipelineOperator)).ThenConstructToPromise(
			std::move(promise),
			[tapCount](auto pipelineOperator) {
				return std::make_shared<GaussianBlurOperator>(std::move(pipelineOperator), tapCount);
			});
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class FastMipChainOperator
	{
	public:
		void Execute(
			RenderCore::IThreadContext& threadContext,
			IteratorRange<const std::shared_ptr<RenderCore::IResourceView>*> dstUAVs,
			RenderCore::IResourceView& srcSRV,
			bool srgbOutput);

		::Assets::DependencyValidation GetDependencyValidation() const { return _op->GetDependencyValidation(); }

		FastMipChainOperator(RenderCore::IDevice& device, std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> op);
		static void ConstructToPromise(
			std::promise<std::shared_ptr<FastMipChainOperator>>&& promise,
			const std::shared_ptr<RenderCore::Techniques::PipelineCollection>& pool);
	private:
		std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> _op;
		std::shared_ptr<RenderCore::IResourceView> _atomicCounterBufferView;
	};

	void FastMipChainOperator::Execute(
		RenderCore::IThreadContext& threadContext,
		IteratorRange<const std::shared_ptr<RenderCore::IResourceView>*> dstUAVs,
		RenderCore::IResourceView& srcSRV,
		bool srgbOutput)
	{
		using namespace RenderCore;
		auto srcDesc = srcSRV.GetResource()->GetDesc();
		assert(srcDesc._type == ResourceDesc::Type::Texture);
		UInt2 srcDims { srcDesc._textureDesc._width, srcDesc._textureDesc._height };
		const auto threadGroupX = (srcDims[0]+63)>>6, threadGroupY = (srcDims[1]+63)>>6;
		struct FastMipChain_ControlUniforms {
			Float2 _reciprocalInputDims;
			unsigned _dummy[2];
			uint32_t _threadGroupCount;
			unsigned _dummy2;
			uint32_t _mipCount;
			uint32_t _srgbOutput;
		} controlUniforms {
			Float2 { 1.f/float(srcDims[0]), 1.f/float(srcDims[1]) },
			{0,0},
			threadGroupX * threadGroupY,
			0,
			(uint32_t)dstUAVs.size(),
			srgbOutput
		};
		IResourceView* srvs[2+13];
		srvs[0] = &srcSRV;
		srvs[1] = _atomicCounterBufferView.get();
		unsigned c=0;
		for (; c<dstUAVs.size(); ++c) srvs[2+c] = dstUAVs[c].get();
		auto* dummySRV = Techniques::Services::GetCommonResources()->_black2DSRV.get();
		for (; c<13; ++c) srvs[2+c] = dummySRV;
		UniformsStream::ImmediateData immDatas[] { MakeOpaqueIteratorRange(controlUniforms) };
		_op->Dispatch(threadContext, threadGroupX, threadGroupY, 1, UniformsStream { srvs, immDatas });
	}

	FastMipChainOperator::FastMipChainOperator(RenderCore::IDevice& device, std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> op)
	: _op(std::move(op))
	{
		using namespace RenderCore;
		auto atomicBuffer = device.CreateResource(
			CreateDesc(
				BindFlag::TransferDst | BindFlag::UnorderedAccess | BindFlag::TexelBuffer,
				LinearBufferDesc::Create(4*4)),
			"temporary-atomic-counter");
		_atomicCounterBufferView = atomicBuffer->CreateTextureView(BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});
	}

	void FastMipChainOperator::ConstructToPromise(
		std::promise<std::shared_ptr<FastMipChainOperator>>&& promise,
		const std::shared_ptr<RenderCore::Techniques::PipelineCollection>& pool)
	{
		RenderCore::UniformsStreamInterface usi;
		usi.BindResourceView(0, "InputTexture"_h);
		usi.BindResourceView(1, "AtomicBuffer"_h);
		for (unsigned c=0; c<13; ++c)
			usi.BindResourceView(2+c, "MipChainUAV"_h+c);
		usi.BindImmediateData(0, "ControlUniforms"_h);
		auto futureOp = RenderCore::Techniques::CreateComputeOperator(
			pool,
			FAST_MIP_CHAIN_COMPUTE_HLSL ":main",
			ParameterBox{},
			GENERAL_OPERATOR_PIPELINE ":ComputeMain",
			usi);

		::Assets::WhenAll(std::move(futureOp)).ThenConstructToPromise(
			std::move(promise),
			[dev=pool->GetDevice()](auto op) {
				return std::make_shared<FastMipChainOperator>(*dev, std::move(op));
			});
	}

	class BroadBlurOperator
	{
	public:
		std::shared_ptr<RenderCore::IResourceView> Execute(
			RenderCore::Techniques::ParsingContext& parsingContext,
			uint64_t inputAttachment = RenderCore::Techniques::AttachmentSemantics::ColorLDR);

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		BroadBlurOperator(
			std::shared_ptr<FastMipChainOperator> downsampleOperator,
			std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> upsampleOperator);
		static void ConstructToPromise(
			std::promise<std::shared_ptr<BroadBlurOperator>>&& promise,
			const std::shared_ptr<RenderCore::Techniques::PipelineCollection>& pool);
	private:
		std::shared_ptr<FastMipChainOperator> _downsampleOperator;
		std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> _upsampleOperator;
		::Assets::DependencyValidation _depVal;
	};

	std::shared_ptr<RenderCore::IResourceView> BroadBlurOperator::Execute(
		RenderCore::Techniques::ParsingContext& parsingContext,
		uint64_t inputAttachment)
	{
		using namespace RenderCore;

		// Broad blur, but using mip pyramid approach
		UInt2 srcDims { parsingContext.GetFragmentStitchingContext()._workingProps._width, parsingContext.GetFragmentStitchingContext()._workingProps._height };
		auto srcMipCount = IntegerLog2(std::max(srcDims[0], srcDims[1])) + 1;
		auto upsampleCount = 4u;
		upsampleCount = std::min(srcMipCount-1u, upsampleCount);
		UInt2 mipChainTopDims { srcDims[0] >> 1, srcDims[1] >> 1 };

		parsingContext.GetFragmentStitchingContext().DefineAttachment(
			"BroadBlurryBackground"_h,
			CreateDesc(
				BindFlag::UnorderedAccess | BindFlag::ShaderResource,
				TextureDesc::Plain2D(mipChainTopDims[0], mipChainTopDims[1], Format::R8G8B8A8_UNORM, upsampleCount+1)),
			"blurry-background",
			Techniques::PreregisteredAttachment::State::Uninitialized, 0,
			TextureViewDesc{RenderCore::TextureViewDesc::Aspect::ColorSRGB});

		Techniques::FrameBufferDescFragment fbFragment;
		fbFragment._pipelineType = PipelineType::Compute;
		const unsigned colorLDRAttachment = fbFragment.DefineAttachment(inputAttachment).FinalState(BindFlag::UnorderedAccess);
		const unsigned workingAttachment = fbFragment.DefineAttachment("BroadBlurryBackground"_h).NoInitialState().FinalState(BindFlag::ShaderResource);
		Techniques::FrameBufferDescFragment::SubpassDesc sp;
		const auto inputUAV = sp.AppendNonFrameBufferAttachmentView(colorLDRAttachment, BindFlag::UnorderedAccess);
		const auto allMipsUAV = sp.AppendNonFrameBufferAttachmentView(workingAttachment, BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::Aspect::ColorLinear});
		const auto allMipsSRV = sp.AppendNonFrameBufferAttachmentView(workingAttachment, BindFlag::ShaderResource);
		TextureViewDesc justTopMip; justTopMip._mipRange = {0, 1};
		const auto topMipSRV = sp.AppendNonFrameBufferAttachmentView(workingAttachment, BindFlag::ShaderResource, justTopMip);
		sp.SetName("broad-blur");
		fbFragment.AddSubpass(std::move(sp));

		Techniques::RenderPassInstance rpi { parsingContext, fbFragment };
		rpi.AutoNonFrameBufferBarrier({
			{inputUAV, BindFlag::ShaderResource, ShaderStage::Compute},
			{allMipsUAV, BindFlag::UnorderedAccess, ShaderStage::Compute}
		});

		std::vector<std::shared_ptr<IResourceView>> tempUAVs;
		for (unsigned c=0; c<upsampleCount+1; ++c) {
			TextureViewDesc justDstMip; justDstMip._mipRange = {c, 1};
			tempUAVs.push_back(rpi.GetNonFrameBufferAttachmentView(allMipsUAV)->GetResource()->CreateTextureView(BindFlag::UnorderedAccess, justDstMip));
		}

		// first build mip pyramid
		_downsampleOperator->Execute(
			parsingContext.GetThreadContext(),
			MakeIteratorRange(tempUAVs),
			*rpi.GetNonFrameBufferAttachmentView(inputUAV),
			true);

		// now upsample operation
		{
			auto& metalContext = *Metal::DeviceContext::Get(parsingContext.GetThreadContext());
			auto* mipChainResource = tempUAVs[0]->GetResource().get();

			for (unsigned pass=0; pass<upsampleCount; ++pass) {
				auto srcMip = upsampleCount-pass;
				auto dstMip = upsampleCount-1-pass;

				// there's a sequence of barriers as we walk up the mip chain
				// we could potentially do this smarter if we built a system like ffx_spd, but going the other way
				{
					Metal::BarrierHelper barrierHelper{metalContext};
					barrierHelper.Add(
						*mipChainResource, TextureViewDesc::SubResourceRange{srcMip, 1}, TextureViewDesc::All,
						Metal::BarrierResourceUsage{BindFlag::UnorderedAccess, ShaderStage::Compute},
						Metal::BarrierResourceUsage{BindFlag::ShaderResource, ShaderStage::Compute});
				}

				const unsigned dispatchGroupWidth = 8;
				const unsigned dispatchGroupHeight = 8;
				const auto
					threadGroupX = ((mipChainTopDims[0]>>dstMip)+dispatchGroupWidth)/dispatchGroupWidth,
					threadGroupY = ((mipChainTopDims[1]>>dstMip)+dispatchGroupHeight)/dispatchGroupHeight;

				struct ControlUniforms {
					Float2 _reciprocalDstDims;
					unsigned _dummy2[2];
					UInt2 _threadGroupCount;
					unsigned _mipIndex;
					unsigned _dummy3;
				} controlUniforms {
					Float2 { 1.f/float(mipChainTopDims[0]>>dstMip), 1.f/float(mipChainTopDims[1]>>dstMip) },
					{0,0},
					{ threadGroupX, threadGroupY },
					dstMip,
					0
				};
				IResourceView* srvs[] { tempUAVs[dstMip].get(), rpi.GetNonFrameBufferAttachmentView(allMipsSRV).get() };
				UniformsStream::ImmediateData immDatas[] { MakeOpaqueIteratorRange(controlUniforms) };
				_upsampleOperator->Dispatch(parsingContext, threadGroupX, threadGroupY, 1, UniformsStream { srvs, immDatas });
			}

			Metal::BarrierHelper barrierHelper{metalContext};
			barrierHelper.Add(
				*mipChainResource, TextureViewDesc::SubResourceRange{0, 1}, TextureViewDesc::All,
				Metal::BarrierResourceUsage{BindFlag::UnorderedAccess, ShaderStage::Compute},
				Metal::BarrierResourceUsage{BindFlag::ShaderResource, ShaderStage::Compute});
		}

		return rpi.GetNonFrameBufferAttachmentView(3);
	}

	BroadBlurOperator::BroadBlurOperator(
		std::shared_ptr<FastMipChainOperator> downsampleOperator,
		std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> upsampleOperator)
	: _downsampleOperator(std::move(downsampleOperator))
	, _upsampleOperator(std::move(upsampleOperator))
	{
		::Assets::DependencyValidationMarker depVals[] {
			_downsampleOperator->GetDependencyValidation(),
			_upsampleOperator->GetDependencyValidation()
		};
		_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
	}

	void BroadBlurOperator::ConstructToPromise(
		std::promise<std::shared_ptr<BroadBlurOperator>>&& promise,
		const std::shared_ptr<RenderCore::Techniques::PipelineCollection>& pool)
	{
		RenderCore::UniformsStreamInterface usi1;
		usi1.BindResourceView(0, "MipChainUAV"_h);
		usi1.BindResourceView(1, "MipChainSRV"_h);
		usi1.BindImmediateData(0, "ControlUniforms"_h);
		auto futureUpsampleOperator = RenderCore::Techniques::CreateComputeOperator(
			pool,
			"xleres/TechniqueLibrary/RenderOverlays/dd/hierarchical-blur.compute.hlsl" ":main",
			ParameterBox{},
			GENERAL_OPERATOR_PIPELINE ":ComputeMain",
			usi1);

		auto futureDownsampleOperator = ::Assets::MakeAssetMarkerPtr<FastMipChainOperator>(pool);

		::Assets::WhenAll(std::move(futureDownsampleOperator), std::move(futureUpsampleOperator)).ThenConstructToPromise(
			std::move(promise),
			[](auto downsampleOperator, auto upsampleOperator) {
				return std::make_shared<BroadBlurOperator>(std::move(downsampleOperator), std::move(upsampleOperator));
			});
	}

	std::shared_ptr<RenderCore::IResourceView> BlurryBackgroundEffect::GetResourceView(Type type)
	{
		assert(_parsingContext);
		if (!_backgroundResource) {
			// generate the blurry background now (at least, if the shader has finished loading)
			if (type == Type::NarrowAccurateBlur) {
				auto *op = _gaussianBlur->TryActualize();
				if (op) {
					// bring up-to-date compute, because it's typically invalidated at this point
					_parsingContext->GetUniformDelegateManager()->BringUpToDateCompute(*_parsingContext);
					_backgroundResource = (*op)->Execute(*_parsingContext, Tweakable("BlurRadius", 20.0f));
				}
			} else {
				assert(type == Type::BroadBlur);
				auto *op = _broadBlur->TryActualize();
				if (op) {
					// bring up-to-date compute, because it's typically invalidated at this point
					_parsingContext->GetUniformDelegateManager()->BringUpToDateCompute(*_parsingContext);
					_backgroundResource = (*op)->Execute(*_parsingContext);
				}
			}
		}
		return _backgroundResource ? _backgroundResource : RenderCore::Techniques::Services::GetCommonResources()->_black2DSRV;
	}

	Float2 BlurryBackgroundEffect::AsTextureCoords(Coord2 screenSpace)
	{
		if (_backgroundResource)
			return {
				screenSpace[0] / float(_parsingContext->GetFragmentStitchingContext()._workingProps._width),
				screenSpace[1] / float(_parsingContext->GetFragmentStitchingContext()._workingProps._height) };
		return {0,0};
	}

	BlurryBackgroundEffect::BlurryBackgroundEffect(RenderCore::Techniques::ParsingContext& parsingContext)
	: _parsingContext(&parsingContext)
	{
		const unsigned tapCount = Tweakable("BlurTapCount", 31);
		_gaussianBlur = ::Assets::MakeAssetMarkerPtr<GaussianBlurOperator>(parsingContext.GetTechniqueContext()._graphicsPipelinePool, tapCount);
		_broadBlur = ::Assets::MakeAssetMarkerPtr<BroadBlurOperator>(parsingContext.GetTechniqueContext()._graphicsPipelinePool);
	}

	BlurryBackgroundEffect::~BlurryBackgroundEffect()
	{}
}

