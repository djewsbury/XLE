// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ToneMapOperator.h"
#include "SequenceIterator.h"
#include "RenderStepFragments.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/Services.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Metal/Resource.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../IAnnotator.h"
#include "../UniformsStream.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine 
{
	struct CB_Params
	{
		Float3x4 _preToneScale;
		Float3x4 _postToneScale;
		float _exposureControl;
		uint32_t _dummy[3];
	};

	struct CB_BrightPassParams
	{
		float _bloomThreshold;
		float _bloomDesaturationFactor;
		float _smallBlurWeights[6];
		Float4 _largeRadiusBrightness;
		Float4 _smallRadiusBrightness;

		void CalculateSmallBlurWeights(float radius);
	};

	struct AllParams
	{
		CB_Params _tonemapParams;
		CB_BrightPassParams _brightPassParams;
	};

	static const unsigned s_shaderMipChainUniformCount = 8;		// there's a limit to how many mip levels are actually useful

	static Float4x4 BuildPreToneScaleTransform();
	static Float4x4 BuildPostToneScaleTransform_SRGB();
	static void InitializeAcesLookupTable(Metal::DeviceContext&, IResource&);

	void ToneMapAcesOperator::Execute(
		Techniques::ParsingContext& parsingContext,
		IResourceView& ldrOutput, IResourceView& hdrInput,
		IteratorRange<IResourceView const*const*> brightPassMipChainUAV,
		IResourceView* brightPassMipChainSRV,
		IResourceView* brightPassHighResBlurWorkingUAV, IResourceView* brightPassHighResBlurWorkingSRV)
	{
		assert(_secondStageConstructionState == 2);
		assert(_toneMap);

		auto& metalContext = *Metal::DeviceContext::Get(parsingContext.GetThreadContext());
		vkCmdFillBuffer(	// we could alternatively clear this in the "BrightPassFilter" shader
			metalContext.GetActiveCommandList().GetUnderlying().get(),
			checked_cast<Metal::Resource*>(_atomicCounterBufferView->GetResource().get())->GetBuffer(), 
			0, VK_WHOLE_SIZE, 0);

		static_assert(dimof(_brightPassParams) == dimof(_params));
		_paramsBufferCounter = (_paramsBufferCounter+1)%dimof(_params);
		if (_paramsBufferCopyCountdown) {
			metalContext.BeginBlitEncoder().Write(
				CopyPartial_Dest{*_params[0]->GetResource(), unsigned(_paramsBufferCounter*_paramsData.size())},
				_paramsData);
			_paramsBufferCopyCountdown--;
		}

		auto fbProps = parsingContext._rpi->GetFrameBufferDesc().GetProperties();
		assert(fbProps._width != 0 && fbProps._height != 0);
		assert(_brightPassMipCountCount <= s_shaderMipChainUniformCount);
		assert(brightPassMipChainUAV.size() == _brightPassMipCountCount);

		////////////////////////////////////////////////////////////
		
		if (brightPassMipChainSRV && !brightPassMipChainUAV.empty()) {
			auto mipChainTopWidth = fbProps._width>>1, mipChainTopHeight = fbProps._height>>1;

			auto encoder = metalContext.BeginComputeEncoder(*_compiledPipelineLayout);
			Metal::CapturedStates capturedStates;
			encoder.BeginStateCapture(capturedStates);

			// Set the uniforms once, and forget
			// We just use push constants on a per-dispatch basis
			auto* dummyUav = Techniques::Services::GetCommonResources()->_undefined2DUAV.get();
			const IResourceView* views[5+s_shaderMipChainUniformCount];
			views[0] = &hdrInput;
			views[1] = _atomicCounterBufferView.get();
			views[2] = _brightPassParams[_paramsBufferCounter].get();
			views[3] = brightPassHighResBlurWorkingUAV ? brightPassHighResBlurWorkingUAV : dummyUav;
			views[4] = brightPassMipChainSRV;
			unsigned c=0;
			for (; c<brightPassMipChainUAV.size(); ++c) views[5+c] = brightPassMipChainUAV[c];
			for (; c<s_shaderMipChainUniformCount; ++c) views[5+c] = dummyUav;

			UniformsStream uniforms;
			uniforms._resourceViews = views;
			_brightPassBoundUniforms->ApplyLooseUniforms(
				metalContext, encoder,
				uniforms);

			{
				const unsigned dispatchGroupWidth = 8;
				const unsigned dispatchGroupHeight = 8;
				encoder.Dispatch(
					*_brightPass,
					(mipChainTopWidth + dispatchGroupWidth - 1) / dispatchGroupWidth,
					(mipChainTopHeight + dispatchGroupHeight - 1) / dispatchGroupHeight,
					1);
			}

			Metal::BarrierHelper(metalContext).Add(
				*brightPassMipChainUAV[0]->GetResource(), TextureViewDesc::SubResourceRange{0, 1}, TextureViewDesc::All,
				Metal::BarrierResourceUsage{BindFlag::UnorderedAccess, ShaderStage::Compute},
				Metal::BarrierResourceUsage{BindFlag::ShaderResource, ShaderStage::Compute});

			if (_desc._enablePreciseBloom) {
				const unsigned blockSize = 16;
				encoder.Dispatch(
					*_gaussianFilter,
					(mipChainTopWidth + blockSize - 1) / blockSize,
					(mipChainTopHeight + blockSize - 1) / blockSize,
					1);
			}

			if (_desc._broadBloomMaxRadius > 0.f) {

				unsigned upsampleCount = unsigned(std::log2(_brightPassLargeRadius) - 1.f);		// see below for calculation
				upsampleCount = std::min(upsampleCount, _brightPassMipCountCount - 1);

				auto* mipChainResource = brightPassMipChainUAV[0]->GetResource().get();
				{
					// note -- thread group counts based on the size of the input texture, not any of the mip levels
					const auto threadGroupX = (mipChainTopWidth+63)>>6, threadGroupY = (mipChainTopHeight+63)>>6;
					struct FastMipChain_ControlUniforms {
						Float2 _reciprocalInputDims;
						unsigned _dummy[2];
						uint32_t _threadGroupCount;
						unsigned _dummy2;
						uint32_t _mipCount;
						unsigned _dummy3;
					} controlUniforms {
						Float2 { 1.f/float(mipChainTopWidth), 1.f/float(mipChainTopHeight) },
						{0,0},
						threadGroupX * threadGroupY,
						0,
						upsampleCount,
						0
					};
					encoder.PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, MakeOpaqueIteratorRange(controlUniforms));
					encoder.Dispatch(
						*_brightDownsample,
						threadGroupX, threadGroupY, 1);
				}

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
						if (dstMip == 0) {
							barrierHelper.Add(
								*mipChainResource, TextureViewDesc::SubResourceRange{0, 1}, TextureViewDesc::All,
								Metal::BarrierResourceUsage{BindFlag::ShaderResource, ShaderStage::Compute},
								Metal::BarrierResourceUsage{BindFlag::UnorderedAccess, ShaderStage::Compute});

							if (brightPassHighResBlurWorkingUAV)
								barrierHelper.Add(
									*brightPassHighResBlurWorkingUAV->GetResource(),
									Metal::BarrierResourceUsage{BindFlag::UnorderedAccess, ShaderStage::Compute},
									Metal::BarrierResourceUsage{BindFlag::UnorderedAccess, ShaderStage::Compute});
						}
					}

					const unsigned dispatchGroupWidth = 8;
					const unsigned dispatchGroupHeight = 8;
					auto topMipWidth = fbProps._width >> 1, topMipHeight = fbProps._height >> 1;
					const auto
						threadGroupX = ((topMipWidth>>dstMip)+dispatchGroupWidth)/dispatchGroupWidth,
						threadGroupY = ((topMipHeight>>dstMip)+dispatchGroupHeight)/dispatchGroupHeight;

					struct ControlUniforms {
						Float2 _reciprocalDstDims;
						unsigned _dummy2[2];
						UInt2 _threadGroupCount;
						unsigned _mipIndex;
						unsigned _copyHighResBlur;
					} controlUniforms {
						Float2 { 1.f/float(topMipWidth>>dstMip), 1.f/float(topMipHeight>>dstMip) },
						{0,0},
						{ threadGroupX, threadGroupY },
						dstMip,
						(dstMip == 0) && brightPassHighResBlurWorkingUAV
					};
					encoder.PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, MakeOpaqueIteratorRange(controlUniforms));
					encoder.Dispatch(
						*_brightUpsample,
						threadGroupX, threadGroupY, 1);
				}

				// final blurred texture now shifted to ShaderResource
				Metal::BarrierHelper{metalContext}.Add(
					*brightPassMipChainSRV->GetResource(), TextureViewDesc::SubResourceRange{0, 1}, TextureViewDesc::All,
					Metal::BarrierResourceUsage{BindFlag::UnorderedAccess, ShaderStage::Compute},
					Metal::BarrierResourceUsage{BindFlag::ShaderResource, ShaderStage::Compute});
			} else if (_desc._enablePreciseBloom) {
				Metal::BarrierHelper{metalContext}.Add(
					*brightPassHighResBlurWorkingUAV->GetResource(),
					Metal::BarrierResourceUsage{BindFlag::UnorderedAccess, ShaderStage::Compute},
					Metal::BarrierResourceUsage{BindFlag::ShaderResource, ShaderStage::Compute});
			}
		}

		////////////////////////////////////////////////////////////

		if (!_lookupTableInitialized) {
			InitializeAcesLookupTable(metalContext, *_lookupTable->GetResource());
			_lookupTableInitialized = true;
		}

		{
			GPUProfilerBlock profileBlock(parsingContext.GetThreadContext(), "Tonemap");

			const unsigned dispatchGroupWidth = 8;
			const unsigned dispatchGroupHeight = 8;
			ResourceViewStream uniforms {
				hdrInput, ldrOutput,
				*_params[_paramsBufferCounter],
				*((_desc._broadBloomMaxRadius > 0.f) ? brightPassMipChainSRV : brightPassHighResBlurWorkingSRV),
				*_lookupTable
			};
			_toneMap->Dispatch(
				parsingContext,
				(fbProps._width + dispatchGroupWidth - 1) / dispatchGroupWidth,
				(fbProps._height + dispatchGroupHeight - 1) / dispatchGroupHeight,
				1,
				uniforms);
		}
		
	}

	::Assets::DependencyValidation ToneMapAcesOperator::GetDependencyValidation() const { assert(_secondStageConstructionState==2); return _depVal; }

	RenderStepFragmentInterface ToneMapAcesOperator::CreateFragment(const FrameBufferProperties& fbProps)
	{
		assert(_secondStageConstructionState == 0);
		_samples = fbProps._samples;
		RenderStepFragmentInterface result{PipelineType::Compute};

		// todo -- what should we set the final state for ColorLDR to be here? just go directly to PresentationSrc?
		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR).NoInitialState().FinalState(BindFlag::RenderTarget), BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::Aspect::ColorLinear});
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).Discard());
		unsigned brightPassMipChainSRVIdx = ~0u, brightPassMipChainUAVIdx = ~0u, brightPassHighResBlurWorkingUAVIdx = ~0u, brightPassHighResBlurWorkingSRVIdx = ~0u;
		if (_desc._broadBloomMaxRadius > 0.f || _desc._enablePreciseBloom) {
			auto brightPassMipChain = result.DefineAttachment("brightpass-working"_h).NoInitialState().Discard();
			{
				TextureViewDesc view;
				// view._flags |= TextureViewDesc::Flags::SimultaneouslyUnorderedAccess;
				brightPassMipChainSRVIdx = spDesc.AppendNonFrameBufferAttachmentView(brightPassMipChain, BindFlag::ShaderResource, view);
			}
			for (unsigned c=0; c<_brightPassMipCountCount; ++c) {
				TextureViewDesc view;
				view._mipRange._min = c;
				view._mipRange._count = 1;
				auto idx = spDesc.AppendNonFrameBufferAttachmentView(brightPassMipChain, BindFlag::UnorderedAccess, view);
				if (c == 0) brightPassMipChainUAVIdx = idx;
			}
		}
		if (_desc._enablePreciseBloom) {
			auto highResBlur = result.DefineAttachment("brightpass-highres-blur-working"_h).NoInitialState().Discard();
			brightPassHighResBlurWorkingUAVIdx = spDesc.AppendNonFrameBufferAttachmentView(highResBlur, BindFlag::UnorderedAccess);
			brightPassHighResBlurWorkingSRVIdx = spDesc.AppendNonFrameBufferAttachmentView(highResBlur, BindFlag::ShaderResource);
		}
		spDesc.SetName("tone-map-aces-operator");

		result.AddSubpass(
			std::move(spDesc),
			[op=shared_from_this(), brightPassMipChainSRVIdx, brightPassMipChainUAVIdx, brightPassHighResBlurWorkingUAVIdx, brightPassHighResBlurWorkingSRVIdx](SequenceIterator& iterator) {
				auto& ldrOutput = *iterator._rpi.GetNonFrameBufferAttachmentView(0);
				auto& hdrInput = *iterator._rpi.GetNonFrameBufferAttachmentView(1);
				IResourceView* brightPassHighResBlurWorkingUAV = nullptr, *brightPassHighResBlurWorkingSRV = nullptr, *brightPassMipChainSRV = nullptr;
				if (brightPassMipChainSRVIdx != ~0u)
					brightPassMipChainSRV = iterator._rpi.GetNonFrameBufferAttachmentView(brightPassMipChainSRVIdx).get();
				if (brightPassHighResBlurWorkingUAVIdx != ~0u)
					brightPassHighResBlurWorkingUAV = iterator._rpi.GetNonFrameBufferAttachmentView(brightPassHighResBlurWorkingUAVIdx).get();
				if (brightPassHighResBlurWorkingSRVIdx != ~0u)
					brightPassHighResBlurWorkingSRV = iterator._rpi.GetNonFrameBufferAttachmentView(brightPassHighResBlurWorkingSRVIdx).get();

				assert(op->_brightPassMipCountCount <= s_shaderMipChainUniformCount);
				const IResourceView* brightPassMipChainUAV[s_shaderMipChainUniformCount];
				for (unsigned c=0; c<op->_brightPassMipCountCount; ++c)
					brightPassMipChainUAV[c] = iterator._rpi.GetNonFrameBufferAttachmentView(brightPassMipChainUAVIdx+c).get();

				iterator._rpi.AutoNonFrameBufferBarrier({
					{1, BindFlag::ShaderResource, ShaderStage::Compute}
				});
				{
					Metal::BarrierHelper barrierHelper{iterator._parsingContext->GetThreadContext()};
					barrierHelper.Add(*ldrOutput.GetResource(), Metal::BarrierResourceUsage::NoState(), BindFlag::UnorderedAccess);
					if (brightPassMipChainSRV)
						barrierHelper.Add(*brightPassMipChainSRV->GetResource(), Metal::BarrierResourceUsage::NoState(), BindFlag::UnorderedAccess);
					if (brightPassHighResBlurWorkingUAV)
						barrierHelper.Add(*brightPassHighResBlurWorkingUAV->GetResource(), Metal::BarrierResourceUsage::NoState(), BindFlag::UnorderedAccess);
				}
				
				op->Execute(
					*iterator._parsingContext, ldrOutput, hdrInput,
					MakeIteratorRange(brightPassMipChainUAV, brightPassMipChainUAV+op->_brightPassMipCountCount), brightPassMipChainSRV,
					brightPassHighResBlurWorkingUAV, brightPassHighResBlurWorkingSRV);

				Metal::BarrierHelper{iterator._parsingContext->GetThreadContext()}.Add(*ldrOutput.GetResource(), {BindFlag::UnorderedAccess, ShaderStage::Compute}, BindFlag::RenderTarget);
			});

		return result;
	}

	void ToneMapAcesOperator::PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext)
	{
		UInt2 fbSize{stitchingContext._workingProps._width, stitchingContext._workingProps._height};
		stitchingContext.DefineAttachment(
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::ColorHDR,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], _desc._lightAccumulationBufferFormat, stitchingContext._workingProps._samples)),
				"color-hdr"
			});

		const auto bloomTextureFormat = Format::R10G10B10A2_UNORM;
		_brightPassMipCountCount = 0;

		if (_desc._broadBloomMaxRadius > 0.f || _desc._enablePreciseBloom) {

			// We're using "tent" weights at each mip level as we upsample
			// If we say that our filter is radius=2 (somewhat arbitrarily), then that 
			// radius effectively doubles every time we upsample. So the final radius is 2^(1+upsample steps)
			// mip count = upsample steps + 1, so therefor:

			auto radiusFactor = (_desc._broadBloomMaxRadius > 0.f) ? (std::log2(_desc._broadBloomMaxRadius)) : 1.f;
			_brightPassMipCountCount = (unsigned)radiusFactor;
			_brightPassMipCountCount = std::min(IntegerLog2(std::max(fbSize[0], fbSize[1])) - 1, _brightPassMipCountCount);
			_brightPassMipCountCount = std::min(_brightPassMipCountCount, s_shaderMipChainUniformCount);

			stitchingContext.DefineAttachment(
				Techniques::PreregisteredAttachment {
					"brightpass-working"_h,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource,
						TextureDesc::Plain2D(fbSize[0]>>1, fbSize[1]>>1, bloomTextureFormat, _brightPassMipCountCount)),
					"brightpass-working"
				});
		}

		if (_desc._enablePreciseBloom) {
			stitchingContext.DefineAttachment(
				Techniques::PreregisteredAttachment {
					"brightpass-highres-blur-working"_h,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource,
						TextureDesc::Plain2D(fbSize[0]>>1, fbSize[1]>>1, bloomTextureFormat)),
					"brightpass-highres-blur-working"
				});
		}
	}

	ToneMapAcesOperator::ToneMapAcesOperator(
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
		const ToneMapAcesOperatorDesc& desc)
	: _secondStageConstructionState(0)
	, _desc(desc)
	{
		_pool = std::move(pipelinePool);

		if (_desc._broadBloomMaxRadius > 0.f) _desc._broadBloomMaxRadius = std::max(_desc._broadBloomMaxRadius, 4.f);
		_brightPassLargeRadius = std::min(1.f, _desc._broadBloomMaxRadius);
		_brightPassSmallRadius = _desc._enablePreciseBloom ? 3.5f : 0.f;
		_paramsData.resize(sizeof(AllParams));
		auto& params = *(AllParams*)_paramsData.data();
		params._tonemapParams._preToneScale = Truncate(BuildPreToneScaleTransform());
		params._tonemapParams._postToneScale = Truncate(BuildPostToneScaleTransform_SRGB());
		params._tonemapParams._exposureControl = 1;
		for (auto& c:params._tonemapParams._dummy) c = 0;
		params._brightPassParams._bloomDesaturationFactor = .5f;
		params._brightPassParams._bloomThreshold = _bloomThreshold = 2.0f;
		params._brightPassParams.CalculateSmallBlurWeights(_brightPassSmallRadius);
		params._brightPassParams._largeRadiusBrightness = Float4(1,1,1,1);
		params._brightPassParams._smallRadiusBrightness = Float4(1,1,1,1);

		// we need to multi-buffer the params buffer in order to update it safely
		auto paramsBuffer = _pool->GetDevice()->CreateResource(
			CreateDesc(BindFlag::ConstantBuffer|BindFlag::TransferDst, LinearBufferDesc::Create(unsigned(3*sizeof(AllParams)))),
			"aces-tonemap-params");
		_params[0] = paramsBuffer->CreateBufferView(BindFlag::ConstantBuffer, unsigned(0*sizeof(AllParams)), (unsigned)sizeof(CB_Params));
		_brightPassParams[0] = paramsBuffer->CreateBufferView(BindFlag::ConstantBuffer, unsigned(0*sizeof(AllParams)+sizeof(CB_Params)), (unsigned)sizeof(CB_BrightPassParams));
		_params[1] = paramsBuffer->CreateBufferView(BindFlag::ConstantBuffer, unsigned(1*sizeof(AllParams)), (unsigned)sizeof(CB_Params));
		_brightPassParams[1] = paramsBuffer->CreateBufferView(BindFlag::ConstantBuffer, unsigned(1*sizeof(AllParams)+sizeof(CB_Params)), (unsigned)sizeof(CB_BrightPassParams));
		_params[2] = paramsBuffer->CreateBufferView(BindFlag::ConstantBuffer, unsigned(2*sizeof(AllParams)), (unsigned)sizeof(CB_Params));
		_brightPassParams[2] = paramsBuffer->CreateBufferView(BindFlag::ConstantBuffer, unsigned(2*sizeof(AllParams)+sizeof(CB_Params)), (unsigned)sizeof(CB_BrightPassParams));
		_paramsBufferCopyCountdown = 3;

		auto atomicBuffer = _pool->GetDevice()->CreateResource(
			CreateDesc(
				BindFlag::TransferDst | BindFlag::UnorderedAccess | BindFlag::TexelBuffer,
				LinearBufferDesc::Create(4*4)),
			"tonemap-aces-atomic-counter");
		_atomicCounterBufferView = atomicBuffer->CreateTextureView(BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});

		_lookupTable = _pool->GetDevice()->CreateResource(
			CreateDesc(BindFlag::ConstantBuffer, LinearBufferDesc::Create(256*sizeof(float))),
			"aces-tonemap-fixed-curve")->CreateBufferView();
		_lookupTableInitialized = false;
	}

	ToneMapAcesOperator::~ToneMapAcesOperator()
	{}

	void ToneMapAcesOperator::SecondStageConstruction(
		std::promise<std::shared_ptr<ToneMapAcesOperator>>&& promise,
		const Techniques::FrameBufferTarget& fbTarget)
	{
		assert(_secondStageConstructionState == 0);
		_secondStageConstructionState = 1;

		// We could do tonemapping in a pixel shader with an input attachment
		// but it's probanly more practical to just use a compute shader
		//
		// note -- we could consider having all of the shaders share a pipeline layout, and then
		// just use a single BoundUniforms applied once

		auto pipelineLayout = ::Assets::MakeAssetPtr<RenderCore::Assets::PredefinedPipelineLayout>(BLOOM_PIPELINE ":ComputeMain");
		::Assets::WhenAll(pipelineLayout).ThenConstructToPromise(
			std::move(promise),
			[strongThis=shared_from_this()](auto&& promise, auto predefinedPipelineLayout)
			{
				TRY {
					UniformsStreamInterface toneMapUsi;
					toneMapUsi.BindResourceView(0, "HDRInput"_h);
					toneMapUsi.BindResourceView(1, "LDROutput"_h);
					toneMapUsi.BindResourceView(2, "Params"_h);
					toneMapUsi.BindResourceView(3, "BrightPass"_h);
					toneMapUsi.BindResourceView(4, "LookupTable"_h);

					bool hasBrightPass = strongThis->_desc._enablePreciseBloom || (strongThis->_desc._broadBloomMaxRadius > 0.f);
					ParameterBox toneMapParameters;
					toneMapParameters.SetParameter("HAS_BRIGHT_PASS", hasBrightPass ? 1 : 0);
					toneMapParameters.SetParameter("HDR_INPUT_SAMPLE_COUNT", strongThis->_samples._sampleCount);

					auto futureToneMap = Techniques::CreateComputeOperator(
						strongThis->_pool,
						TONEMAP_ACES_COMPUTE_HLSL ":main",
						std::move(toneMapParameters),
						GENERAL_OPERATOR_PIPELINE ":ComputeMain",
						toneMapUsi);

					auto& commonResources = *Techniques::Services::GetCommonResources();
					auto compiledPipelineLayout = strongThis->_pool->GetDevice()->CreatePipelineLayout(
						predefinedPipelineLayout->MakePipelineLayoutInitializer(Techniques::GetDefaultShaderLanguage(), &commonResources._samplerPool),
						"tone-map-aces");

					// We want to use an identical pipeline layout for all of the shader operators, and share
					// uniform bindings for all of the bloom operators
					// Since this is a little different, we'll forgo the IComputeShaderOperator object and
					// just use the lower level PipelineCollection object

					std::promise<Techniques::ComputePipelineAndLayout> promisedBrightPass;
					auto futureBrightPass = promisedBrightPass.get_future();
					strongThis->_pool->CreateComputePipeline(
						std::move(promisedBrightPass),
						compiledPipelineLayout,
						BLOOM_COMPUTE_HLSL ":BrightPassFilter",
						{});

					std::promise<Techniques::ComputePipelineAndLayout> promisedDownsample;
					auto futureDownsample = promisedDownsample.get_future();
					ParameterBox fastMipChainSelectors;
					fastMipChainSelectors.SetParameter("MIP_OFFSET", 1);
					const ParameterBox* selectorsList[] { &fastMipChainSelectors };
					strongThis->_pool->CreateComputePipeline(
						std::move(promisedDownsample),
						compiledPipelineLayout,
						FAST_MIP_CHAIN_COMPUTE_HLSL ":main",
						selectorsList);

					std::promise<Techniques::ComputePipelineAndLayout> promisedUpsample;
					auto futureUpsample = promisedUpsample.get_future();
					strongThis->_pool->CreateComputePipeline(
						std::move(promisedUpsample),
						compiledPipelineLayout,
						BLOOM_COMPUTE_HLSL ":UpsampleStep",
						{});

					std::promise<Techniques::ComputePipelineAndLayout> promisedGaussianFilter;
					auto futureGaussianFilter = promisedGaussianFilter.get_future();
					strongThis->_pool->CreateComputePipeline(
						std::move(promisedGaussianFilter),
						compiledPipelineLayout,
						BLOOM_FILTER_COMPUTE_HLSL ":Gaussian11RGB",
						{});

					UniformsStreamInterface brightPassUsi;
					brightPassUsi.BindResourceView(0, "HDRInput"_h);
					brightPassUsi.BindResourceView(1, "AtomicBuffer"_h);
					brightPassUsi.BindResourceView(2, "BloomParameters"_h);
					brightPassUsi.BindResourceView(3, "HighResBlurTemp"_h);
					brightPassUsi.BindResourceView(4, "MipChainSRV"_h);
					for (unsigned c=0; c<s_shaderMipChainUniformCount; ++c)
						brightPassUsi.BindResourceView(5+c, "MipChainUAV"_h+c);
					UniformsStreamInterface usi2;
					usi2.BindImmediateData(0, "ControlUniforms"_h);
					auto brightPassBoundUniforms = std::make_shared<Metal::BoundUniforms>(compiledPipelineLayout, brightPassUsi, usi2);

					::Assets::WhenAll(std::move(futureToneMap), std::move(futureBrightPass), std::move(futureDownsample), std::move(futureUpsample), std::move(futureGaussianFilter)).ThenConstructToPromise(
						std::move(promise),
						[strongThis, compiledPipelineLayout, brightPassBoundUniforms, pipelineLayoutDepVal=predefinedPipelineLayout->GetDependencyValidation()]
						(auto toneMap, auto brightPass, auto brightPassDownsample, auto brightPassUpsample, auto guassianFilter) mutable
						{
							assert(strongThis->_secondStageConstructionState == 1);
							strongThis->_toneMap = std::move(toneMap);
							strongThis->_brightPass = std::move(brightPass._pipeline);
							strongThis->_brightDownsample = std::move(brightPassDownsample._pipeline);
							strongThis->_brightUpsample = std::move(brightPassUpsample._pipeline);
							strongThis->_gaussianFilter = std::move(guassianFilter._pipeline);
							strongThis->_compiledPipelineLayout = std::move(compiledPipelineLayout);
							strongThis->_brightPassBoundUniforms = std::move(brightPassBoundUniforms);
							::Assets::DependencyValidationMarker depVals[] {
								strongThis->_toneMap->GetDependencyValidation(),
								brightPass.GetDependencyValidation(),
								brightPassDownsample.GetDependencyValidation(),
								brightPassUpsample.GetDependencyValidation(),
								guassianFilter.GetDependencyValidation(),
								pipelineLayoutDepVal
							};
							strongThis->_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
							strongThis->_secondStageConstructionState = 2;
							return strongThis;
						});
				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	void ToneMapAcesOperator::SetBroadRadius(float radius)
	{
		if (_desc._broadBloomMaxRadius <= 0.f)
			Throw(std::runtime_error("Cannot set large bloom radius because this feature was disabled in the operator desc"));
		_brightPassLargeRadius = std::clamp(radius, 4.f, _desc._broadBloomMaxRadius);
	}

	float ToneMapAcesOperator::GetBroadRadius() const
	{
		return _brightPassLargeRadius;
	}

	void ToneMapAcesOperator::SetPreciseRadius(float radius)
	{
		if (_desc._broadBloomMaxRadius <= 0.f)
			Throw(std::runtime_error("Cannot set small bloom radius because this feature was disabled in the operator desc"));
		_brightPassSmallRadius = radius;
		auto& params = *(AllParams*)_paramsData.data();
		params._brightPassParams.CalculateSmallBlurWeights(_brightPassSmallRadius);
		_paramsBufferCopyCountdown = dimof(_params);
	}

	float ToneMapAcesOperator::GetPreciseRadius() const
	{
		return _brightPassSmallRadius;
	}

	void ToneMapAcesOperator::SetThreshold(float bloomThreshold)
	{
		if (_desc._broadBloomMaxRadius <= 0.f && !_desc._enablePreciseBloom)
			Throw(std::runtime_error("Cannot set bloom property because this feature was disabled in the operator desc"));
		auto& params = *(AllParams*)_paramsData.data();
		_bloomThreshold = bloomThreshold;
		params._brightPassParams._bloomThreshold = bloomThreshold / params._tonemapParams._exposureControl;
		_paramsBufferCopyCountdown = dimof(_params);
	}

	float ToneMapAcesOperator::GetThreshold() const
	{
		return _bloomThreshold;
	}

	void ToneMapAcesOperator::SetDesaturationFactor(float desatFactor)
	{
		if (_desc._broadBloomMaxRadius <= 0.f && !_desc._enablePreciseBloom)
			Throw(std::runtime_error("Cannot set bloom property because this feature was disabled in the operator desc"));
		auto& params = *(AllParams*)_paramsData.data();
		params._brightPassParams._bloomDesaturationFactor = desatFactor;
		_paramsBufferCopyCountdown = dimof(_params);
	}

	float ToneMapAcesOperator::GetDesaturationFactor() const
	{
		auto& params = *(AllParams*)_paramsData.data();
		return params._brightPassParams._bloomDesaturationFactor;
	}

	void ToneMapAcesOperator::SetBroadBrightness(Float3 brightness)
	{
		if (_desc._broadBloomMaxRadius <= 0.f)
			Throw(std::runtime_error("Cannot set bloom property because this feature was disabled in the operator desc"));
		auto& params = *(AllParams*)_paramsData.data();
		params._brightPassParams._largeRadiusBrightness = Expand(brightness, 1.f);
		_paramsBufferCopyCountdown = dimof(_params);
	}

	Float3 ToneMapAcesOperator::GetBroadBrightness() const
	{
		auto& params = *(AllParams*)_paramsData.data();
		return Truncate(params._brightPassParams._largeRadiusBrightness);
	}

	void ToneMapAcesOperator::SetPreciseBrightness(Float3 brightness)
	{
		if (!_desc._enablePreciseBloom)
			Throw(std::runtime_error("Cannot set bloom property because this feature was disabled in the operator desc"));
		auto& params = *(AllParams*)_paramsData.data();
		params._brightPassParams._smallRadiusBrightness = Expand(brightness, 1.f);
		_paramsBufferCopyCountdown = dimof(_params);
	}

	Float3 ToneMapAcesOperator::GetPreciseBrightness() const
	{
		auto& params = *(AllParams*)_paramsData.data();
		return Truncate(params._brightPassParams._smallRadiusBrightness);
	}

	void ToneMapAcesOperator::SetExposure(float exposureControl)
	{
		auto& params = *(AllParams*)_paramsData.data();
		params._tonemapParams._exposureControl = exposureControl;
		params._brightPassParams._bloomThreshold = _bloomThreshold / exposureControl;
		_paramsBufferCopyCountdown = dimof(_params);
	}

	float ToneMapAcesOperator::GetExposure() const
	{
		auto& params = *(AllParams*)_paramsData.data();
		return params._tonemapParams._exposureControl;
	}

	uint64_t ToneMapAcesOperatorDesc::GetHash(uint64_t seed) const
	{
		return seed;
	}

	static float GaussianWeight1D(float offset, float stdDevSq)
	{
		// See https://en.wikipedia.org/wiki/Gaussian_blur
		const float twiceStdDevSq = 2.0f * stdDevSq;
		const float C = 1.0f / std::sqrt(gPI * twiceStdDevSq);
		return C * std::exp(-offset*offset / twiceStdDevSq);
	}

	void CB_BrightPassParams::CalculateSmallBlurWeights(float radius)
	{
		// Calculate radius such that 1.5*stdDev = radius
		// This is selected because it just tends to match the blur size we get with the large radius blur
		float stdDevSq = radius * radius / (1.5f * 1.5f);
		for (unsigned c=0; c<dimof(_smallBlurWeights); ++c)
			_smallBlurWeights[c] = GaussianWeight1D(float(c), stdDevSq);
	}

	void InitializeAcesLookupTable(Metal::DeviceContext& metalContext, IResource& resource)
	{
		// curve between [1.0/4096.0, 2.0)
		float fixedCurve[256] = {
			0.02f, 0.0487438f, 0.11277f, 0.20191f, 0.312459f, 0.442387f, 0.590982f, 0.755897f, 0.933821f, 1.12363f, 1.32439f, 1.53534f, 1.75584f, 1.98635f, 2.22684f, 2.477f, 2.73654f, 3.00519f, 3.28272f, 3.56891f, 3.86354f, 4.16642f, 4.47739f, 4.79627f, 5.12033f, 5.44652f, 5.77446f, 6.10376f, 6.43412f, 6.76524f, 7.09685f, 7.42872f, 7.76063f, 8.09239f, 8.42382f, 8.75476f, 9.08507f, 9.41462f, 9.7433f, 10.071f, 10.3975f, 10.7227f, 11.0466f, 11.3692f, 11.6902f, 12.0098f, 12.3277f, 12.6441f, 12.9588f, 13.2719f, 13.5832f, 13.8927f, 14.2006f, 14.5066f, 14.8108f, 15.1132f, 15.4138f, 15.7125f, 16.0094f, 16.3044f, 16.5976f, 16.889f, 17.1784f, 17.4661f, 17.7518f, 18.0357f, 18.3173f, 18.5951f, 18.8692f, 19.1396f, 19.4062f, 19.6692f, 19.9285f, 20.1841f, 20.4361f, 20.6846f, 20.9295f, 21.1709f, 21.4088f, 21.6432f, 21.8743f, 22.1019f, 22.3262f, 22.5472f, 22.765f, 22.9795f, 23.1908f, 23.399f, 23.604f, 23.8059f, 24.0049f, 24.2008f, 24.3937f, 24.5837f, 24.7708f, 24.9551f, 25.1365f, 25.3152f, 25.4911f, 25.6643f, 25.8348f, 26.0027f, 26.168f, 26.3308f, 26.491f, 26.6487f, 26.8039f, 26.9568f, 27.1072f, 27.2553f, 27.401f, 27.5445f, 27.6857f, 27.8246f, 27.9614f, 28.096f, 28.2285f, 28.3594f, 28.4888f, 28.6167f, 28.7432f, 28.8683f, 28.992f, 29.1143f, 29.2353f, 29.3549f, 29.4732f, 29.5902f, 29.7059f, 29.8203f, 29.9335f, 30.0454f, 30.1561f, 30.2656f, 30.3739f, 30.4811f, 30.5871f, 30.6919f, 30.7956f, 30.8982f, 30.9997f, 31.1001f, 31.1995f, 31.2978f, 31.395f, 31.4912f, 31.5864f, 31.6806f, 31.7738f, 31.866f, 31.9573f, 32.0476f, 32.1369f, 32.2253f, 32.3128f, 32.3994f, 32.4851f, 32.5699f, 32.6538f, 32.7369f, 32.8191f, 32.9005f, 32.981f, 33.0607f, 33.1396f, 33.2177f, 33.295f, 33.3715f, 33.4473f, 33.5222f, 33.5964f, 33.6699f, 33.7426f, 33.8146f, 33.8859f, 33.9564f, 34.0263f, 34.0954f, 34.1639f, 34.2317f, 34.2988f, 34.3652f, 34.431f, 34.4961f, 34.5606f, 34.6244f, 34.6876f, 34.7502f, 34.8122f, 34.8736f, 34.9344f, 34.9945f, 35.0541f, 35.1131f, 35.1715f, 35.2294f, 35.2867f, 35.3434f, 35.3996f, 35.4552f, 35.5103f, 35.5649f, 35.619f, 35.6725f, 35.7255f, 35.778f, 35.8299f, 35.8814f, 35.9324f, 35.9829f, 36.0329f, 36.0825f, 36.1315f, 36.1801f, 36.2283f, 36.276f, 36.3234f, 36.3705f, 36.4173f, 36.4637f, 36.5098f, 36.5557f, 36.6012f, 36.6464f, 36.6914f, 36.736f, 36.7803f, 36.8244f, 36.8681f, 36.9116f, 36.9548f, 36.9977f, 37.0404f, 37.0827f, 37.1248f, 37.1666f, 37.2082f, 37.2495f, 37.2905f, 37.3312f, 37.3718f, 37.412f, 37.452f, 37.4918f, 37.5312f, 37.5705f, 37.6095f, 37.6483f, 37.6868f, 37.7251f, 37.7631f, 37.801f, 37.8385f, 37.8759f, 37.913f, 37.9499f
		};
		
		metalContext.BeginBlitEncoder().Write(resource, MakeIteratorRange(fixedCurve));
	}

	namespace ACES
	{
		static Float3x3 Init3x3(Float3 A, Float3 B, Float3 C)
		{
			return MakeFloat3x3(
				A[0], B[0], C[0],
				A[1], B[1], C[1],
				A[2], B[2], C[2]);
		}

		// note i, j flipped (required because of ordering described in https://github.com/ampas/aces-dev/blob/dev/transforms/ctl/README-MATRIX.md)
		template<typename Matrix> float Element(const Matrix& m, int j, int i) { return m(i, j); }
		template<typename Matrix> float& Element(Matrix& m, int j, int i) { return m(i, j); }
		static Float4x4 mult_f44_f44(const Float4x4& lhs, const Float4x4& rhs) { return lhs * rhs; }
		static float pow10(float x) { return pow(10.f, x); }

		struct Chromaticities { Float2 red, green, blue, white; };

		static Float4x4 RGBtoXYZ(const Chromaticities &chroma, float Y)
		{
			// Reference -- http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
			// See also interesting papers (not sure how relevant they are) https://www.cs.sfu.ca/~mark/ftp/Cic97/cic97.pdf, https://www.researchgate.net/publication/3183222_A_New_Method_for_RGB_to_XYZ_Transformation_Based_on_Pattern_Search_Optimization
			float X = chroma.white[0] * Y / chroma.white[1];
			float Z = (1 - chroma.white[0] - chroma.white[1]) * Y / chroma.white[1];

			float d =
				chroma.red[0]   * (chroma.blue[1]  - chroma.green[1]) +
				chroma.blue[0]  * (chroma.green[1] - chroma.red[1]) +
				chroma.green[0] * (chroma.red[1]   - chroma.blue[1]);

			float Sr = (X * (chroma.blue[1] - chroma.green[1]) -
				chroma.green[0] * (Y * (chroma.blue[1] - 1) +
				chroma.blue[1]  * (X + Z)) +
				chroma.blue[0]  * (Y * (chroma.green[1] - 1) +
				chroma.green[1] * (X + Z))) / d;

			float Sg = (X * (chroma.red[1] - chroma.blue[1]) +
				chroma.red[0]   * (Y * (chroma.blue[1] - 1) +
				chroma.blue[1]  * (X + Z)) -
				chroma.blue[0]  * (Y * (chroma.red[1] - 1) +
				chroma.red[1]   * (X + Z))) / d;

			float Sb = (X * (chroma.green[1] - chroma.red[1]) -
				chroma.red[0]   * (Y * (chroma.green[1] - 1) +
				chroma.green[1] * (X + Z)) +
				chroma.green[0] * (Y * (chroma.red[1] - 1) +
				chroma.red[1]   * (X + Z))) / d;

			Float4x4 M = Identity<Float4x4>();
			Element(M, 0, 0) = Sr * chroma.red[0];
			Element(M, 0, 1) = Sr * chroma.red[1];
			Element(M, 0, 2) = Sr * (1 - chroma.red[0] - chroma.red[1]);

			Element(M, 1, 0) = Sg * chroma.green[0];
			Element(M, 1, 1) = Sg * chroma.green[1];
			Element(M, 1, 2) = Sg * (1 - chroma.green[0] - chroma.green[1]);

			Element(M, 2, 0) = Sb * chroma.blue[0];
			Element(M, 2, 1) = Sb * chroma.blue[1];
			Element(M, 2, 2) = Sb * (1 - chroma.blue[0] - chroma.blue[1]);
			return M;
		}

		static Float4x4 XYZtoRGB (const Chromaticities &chroma, float Y)
		{
			return Inverse(RGBtoXYZ(chroma, Y));
		}

		static Float3x3 calc_sat_adjust_matrix(float sat, Float3 rgb2Y)
		{
			// Following the ACES reference transform, this just causes some percentage
			// of each color channel to be added to the other channels -- thereby decreasing saturation
			Float3x3 M;
			Element(M, 0, 0) = (1.0f - sat) * rgb2Y[0] + sat;
			Element(M, 1, 0) = (1.0f - sat) * rgb2Y[0];
			Element(M, 2, 0) = (1.0f - sat) * rgb2Y[0];
			
			Element(M, 0, 1) = (1.0f - sat) * rgb2Y[1];
			Element(M, 1, 1) = (1.0f - sat) * rgb2Y[1] + sat;
			Element(M, 2, 1) = (1.0f - sat) * rgb2Y[1];
			
			Element(M, 0, 2) = (1.0f - sat) * rgb2Y[2];
			Element(M, 1, 2) = (1.0f - sat) * rgb2Y[2];
			Element(M, 2, 2) = (1.0f - sat) * rgb2Y[2] + sat;
			M = Transpose(M);    
			return M;
		}

		// Reference -- ACESlib.Utilities_Color.ctl
		static const Chromaticities AP0 = // From reference, this is the definition of AP0 color space
		{
			{ 0.73470f,  0.26530f},
			{ 0.00000f,  1.00000f},
			{ 0.00010f, -0.07700f},
			{ 0.32168f,  0.33767f}
		};

		const Chromaticities AP1 = // As above, this is the definition of AP1 color space
		{
			{ 0.71300f,  0.29300f},
			{ 0.16500f,  0.83000f},
			{ 0.12800f,  0.04400f},
			{ 0.32168f,  0.33767f}
		};

		static const Chromaticities REC709_PRI =
		{
			{ 0.64000f,  0.33000f},
			{ 0.30000f,  0.60000f},
			{ 0.15000f,  0.06000f},
			{ 0.31270f,  0.32900f}
		};

		// Reference -- ACESlib.Transform_Common.ctl
		// Using the same names as the ACES reference code here to ensure that following the code is a little clearer
		static const Float4x4 AP0_2_XYZ_MAT = RGBtoXYZ( AP0, 1.0);
		static const Float4x4 XYZ_2_AP0_MAT = XYZtoRGB( AP0, 1.0);

		static const Float4x4 AP1_2_XYZ_MAT = RGBtoXYZ( AP1, 1.0);
		static const Float4x4 XYZ_2_AP1_MAT = XYZtoRGB( AP1, 1.0);

		static const Float4x4 AP0_2_AP1_MAT = mult_f44_f44( AP0_2_XYZ_MAT, XYZ_2_AP1_MAT);
		static const Float4x4 AP1_2_AP0_MAT = mult_f44_f44( AP1_2_XYZ_MAT, XYZ_2_AP0_MAT);

		static const Float3 AP1_RGB2Y = {
			Element(AP1_2_XYZ_MAT, 0, 1),
			Element(AP1_2_XYZ_MAT, 1, 1),
			Element(AP1_2_XYZ_MAT, 2, 1) };

		// Reference -- ACESlib.RRT_Common.ctl
		static const float RRT_SAT_FACTOR = 0.96f;
		static const Float3x3 RRT_SAT_MAT = calc_sat_adjust_matrix(RRT_SAT_FACTOR, AP1_RGB2Y);

		// Reference -- ACESlib.ODT_Common.ctl
		static const float ODT_SAT_FACTOR = 0.93f;
		static const Float3x3 ODT_SAT_MAT = calc_sat_adjust_matrix( ODT_SAT_FACTOR, AP1_RGB2Y);
		static const float CINEMA_WHITE = 48.0f;
		static const float CINEMA_BLACK = pow10(std::log10(0.02f));

		static const Chromaticities DISPLAY_PRI = REC709_PRI;
		static const Float4x4 XYZ_2_DISPLAY_PRI_MAT = XYZtoRGB(DISPLAY_PRI, 1.0f);
	}

	static Float4x4 BuildPreToneScaleTransform()
	{
		auto A = Expand(ACES::Init3x3(			// sRGB to XYZ (D65 white) http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
			{0.4124564, 0.2126729, 0.0193339},		// (transposed from textbook form)
			{0.3575761, 0.7151522, 0.1191920},
			{0.1804375, 0.0721750, 0.9503041}),
			Float3(0,0,0));
		auto XYZtoAP0 = ACES::XYZtoRGB( ACES::AP0, 1.0);		// aces matrix conventions
		auto result = Expand(ACES::RRT_SAT_MAT, {0,0,0}) * ACES::AP0_2_AP1_MAT * XYZtoAP0 * A;
		return result;
	}

	static Float4x4 BuildPostToneScaleTransform_SRGB()
	{
		// Note that the output color uses the SRGB primaries, but it's still linear (in that the reverse monitor curve is not applied)
		float A = 1.0f / (ACES::CINEMA_WHITE - ACES::CINEMA_BLACK);
		Float4x4 {
			  A, 0.f, 0.f, -ACES::CINEMA_BLACK * A,
			0.f,   A, 0.f, -ACES::CINEMA_BLACK * A,
			0.f, 0.f,   A, -ACES::CINEMA_BLACK * A,
			0.f, 0.f, 0.f, 1.f};
		// Aces uses a unique whitepoint (which is commonly called D60, though there are some technicalities there)
		// The reference ODT compensates for this by adjusting the color in XYZ space using the following transform
		const Float3x3 D60_2_D65_CAT {
			1.00744021f, 0.00458632875f, 0.00342495739f,
			0.00197348557f, 0.997794211f, -0.00621009618f,
			0.0135383308f, 0.00393609330f, 1.08976591f
		};
		auto result = ACES::XYZ_2_DISPLAY_PRI_MAT * Expand(D60_2_D65_CAT, {0,0,0}) * ACES::AP1_2_XYZ_MAT * Expand(ACES::ODT_SAT_MAT, {0,0,0}) * A;
		return result;
	}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void CopyToneMapOperator::Execute(Techniques::ParsingContext& parsingContex, IResourceView& hdrInput)
	{
		assert(_secondStageConstructionState == 2);
		assert(_shader);

		ResourceViewStream us { hdrInput };
		_shader->Draw(parsingContex, us);
	}

	RenderStepFragmentInterface CopyToneMapOperator::CreateFragment(const FrameBufferProperties& fbProps)
	{
		RenderStepFragmentInterface fragment { RenderCore::PipelineType::Graphics };
		auto hdrInput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).Discard();
		auto ldrOutput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR).NoInitialState();

		Techniques::FrameBufferDescFragment::SubpassDesc subpass;
		subpass.AppendOutput(ldrOutput);
		subpass.AppendInput(hdrInput);
		subpass.SetName("tonemap");

		fragment.AddSubpass(
			std::move(subpass),
			[op=this](SequenceIterator& iterator) {
				op->Execute(
					*iterator._parsingContext,
					*iterator._rpi.GetInputAttachmentView(0));
			});
		return fragment;
	}

	void CopyToneMapOperator::PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext)
	{
		const bool precisionTargets = false;
		UInt2 fbSize{stitchingContext._workingProps._width, stitchingContext._workingProps._height};
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::ColorHDR,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::InputAttachment,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], (!precisionTargets) ? Format::R16G16B16A16_FLOAT : Format::R32G32B32A32_FLOAT)),
				"color-hdr"
			}
		};
		for (const auto& a:attachments)
			stitchingContext.DefineAttachment(a);
	}

	::Assets::DependencyValidation CopyToneMapOperator::GetDependencyValidation() const
	{
		assert(_secondStageConstructionState == 2);
		return _shader->GetDependencyValidation();
	}

	void CopyToneMapOperator::SecondStageConstruction(
		std::promise<std::shared_ptr<CopyToneMapOperator>>&& promise,
		const Techniques::FrameBufferTarget& fbTarget)
	{
		assert(_secondStageConstructionState == 0);
		_secondStageConstructionState = 1;

		Techniques::PixelOutputStates outputStates;
		outputStates.Bind(*fbTarget._fbDesc, fbTarget._subpassIdx);
		outputStates.Bind(Techniques::CommonResourceBox::s_dsDisable);
		AttachmentBlendDesc blendStates[] { Techniques::CommonResourceBox::s_abOpaque };
		outputStates.Bind(MakeIteratorRange(blendStates));
		UniformsStreamInterface usi;
		usi.BindResourceView(0, "SubpassInputAttachment"_h);
		auto shaderFuture = Techniques::CreateFullViewportOperator(
			_pool, Techniques::FullViewportOperatorSubType::DisableDepth,
			BASIC_PIXEL_HLSL ":copy_inputattachment",
			{}, GENERAL_OPERATOR_PIPELINE ":GraphicsMain",
			outputStates, usi);
		::Assets::WhenAll(std::move(shaderFuture)).ThenConstructToPromise(
			std::move(promise),
			[strongThis=shared_from_this()](auto shader) {
				assert(strongThis->_secondStageConstructionState == 1);
				strongThis->_shader = std::move(shader);
				strongThis->_secondStageConstructionState = 2;
				return strongThis;
			});
	}

	void CopyToneMapOperator::CompleteInitialization(IThreadContext& threadContext) {}

	CopyToneMapOperator::CopyToneMapOperator(std::shared_ptr<Techniques::PipelineCollection> pipelinePool)
	: _pool(std::move(pipelinePool)) {}
	CopyToneMapOperator::~CopyToneMapOperator() {}


	IBloom::~IBloom() {}
	IExposure::~IExposure() {}

}}

