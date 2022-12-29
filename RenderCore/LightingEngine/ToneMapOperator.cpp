// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ToneMapOperator.h"
#include "LightingEngineIterator.h"
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
			Metal::ResourceMap map { *parsingContext.GetThreadContext().GetDevice(), *_params[0]->GetResource(), Metal::ResourceMap::Mode::WriteDiscardPrevious };
			std::memcpy(
				PtrAdd(map.GetData().begin(), _paramsBufferCounter*_paramsData.size()),
				_paramsData.data(), _paramsData.size());
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
			auto* dummyUav = Techniques::Services::GetCommonResources()->_black2DSRV.get();
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

			if (_desc._enableSmallBloom) {
				const unsigned blockSize = 16;
				encoder.Dispatch(
					*_gaussianFilter,
					(mipChainTopWidth + blockSize - 1) / blockSize,
					(mipChainTopHeight + blockSize - 1) / blockSize,
					1);
			}

			if (_desc._maxLargeBloomRadius > 0.f) {

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
			} else if (_desc._enableSmallBloom) {
				Metal::BarrierHelper{metalContext}.Add(
					*brightPassHighResBlurWorkingUAV->GetResource(),
					Metal::BarrierResourceUsage{BindFlag::UnorderedAccess, ShaderStage::Compute},
					Metal::BarrierResourceUsage{BindFlag::ShaderResource, ShaderStage::Compute});
			}
		}

		////////////////////////////////////////////////////////////

		{
			const unsigned dispatchGroupWidth = 8;
			const unsigned dispatchGroupHeight = 8;
			ResourceViewStream uniforms {
				hdrInput, ldrOutput,
				*_params[_paramsBufferCounter],
				*((_desc._maxLargeBloomRadius > 0.f) ? brightPassMipChainSRV : brightPassHighResBlurWorkingSRV)
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
		RenderStepFragmentInterface result{PipelineType::Compute};

		// todo -- what should we set the final state for ColorLDR to be here? just go directly to PresentationSrc?
		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR).NoInitialState().FinalState(BindFlag::RenderTarget), BindFlag::UnorderedAccess);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).Discard());
		unsigned brightPassMipChainSRVIdx = ~0u, brightPassMipChainUAVIdx = ~0u, brightPassHighResBlurWorkingUAVIdx = ~0u, brightPassHighResBlurWorkingSRVIdx = ~0u;
		if (_desc._maxLargeBloomRadius > 0.f || _desc._enableSmallBloom) {
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
		if (_desc._enableSmallBloom) {
			auto highResBlur = result.DefineAttachment("brightpass-highres-blur-working"_h).NoInitialState().Discard();
			brightPassHighResBlurWorkingUAVIdx = spDesc.AppendNonFrameBufferAttachmentView(highResBlur, BindFlag::UnorderedAccess);
			brightPassHighResBlurWorkingSRVIdx = spDesc.AppendNonFrameBufferAttachmentView(highResBlur, BindFlag::ShaderResource);
		}
		spDesc.SetName("tone-map-aces-operator");

		result.AddSubpass(
			std::move(spDesc),
			[op=this, brightPassMipChainSRVIdx, brightPassMipChainUAVIdx, brightPassHighResBlurWorkingUAVIdx, brightPassHighResBlurWorkingSRVIdx](LightingTechniqueIterator& iterator) {
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
					TextureDesc::Plain2D(fbSize[0], fbSize[1], _desc._lightAccumulationBufferFormat)),
				"color-hdr"
			});

		const auto bloomTextureFormat = Format::B8G8R8A8_UNORM;
		_brightPassMipCountCount = 0;

		if (_desc._maxLargeBloomRadius > 0.f || _desc._enableSmallBloom) {

			// We're using "tent" weights at each mip level as we upsample
			// If we say that our filter is radius=2 (somewhat arbitrarily), then that 
			// radius effectively doubles every time we upsample. So the final radius is 2^(1+upsample steps)
			// mip count = upsample steps + 1, so therefor:

			auto radiusFactor = (_desc._maxLargeBloomRadius > 0.f) ? (std::log2(_desc._maxLargeBloomRadius)) : 1.f;
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

		if (_desc._enableSmallBloom) {
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

		if (_desc._maxLargeBloomRadius > 0.f) _desc._maxLargeBloomRadius = std::max(_desc._maxLargeBloomRadius, 4.f);
		_brightPassLargeRadius = std::min(1.f, _desc._maxLargeBloomRadius);
		_brightPassSmallRadius = _desc._enableSmallBloom ? 3.5f : 0.f;
		_paramsData.resize(sizeof(AllParams));
		auto& params = *(AllParams*)_paramsData.data();
		params._tonemapParams._preToneScale = Truncate(BuildPreToneScaleTransform());
		params._tonemapParams._postToneScale = Truncate(BuildPostToneScaleTransform_SRGB());
		params._brightPassParams._bloomDesaturationFactor = .5f;
		params._brightPassParams._bloomThreshold = .8f;
		params._brightPassParams.CalculateSmallBlurWeights(_brightPassSmallRadius);
		params._brightPassParams._largeRadiusBrightness = Float4(1,1,1,1);
		params._brightPassParams._smallRadiusBrightness = Float4(1,1,1,1);

		// we need to multi-buffer the params buffer in order to update it safely
		auto paramsBuffer = _pool->GetDevice()->CreateResource(
			CreateDesc(BindFlag::ConstantBuffer, AllocationRules::HostVisibleSequentialWrite, LinearBufferDesc::Create(unsigned(3*sizeof(AllParams)))),
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

					bool hasBrightPass = strongThis->_desc._enableSmallBloom || (strongThis->_desc._maxLargeBloomRadius > 0.f);

					auto futureToneMap = Techniques::CreateComputeOperator(
						strongThis->_pool,
						TONEMAP_ACES_COMPUTE_HLSL ":main",
						ParameterBox{ std::make_pair("HAS_BRIGHT_PASS", hasBrightPass ? "1" : "0") },
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
					strongThis->_pool->CreateComputePipeline(
						std::move(promisedDownsample),
						compiledPipelineLayout,
						BLOOM_COMPUTE_HLSL ":FastMipChain",
						{});

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
						SEPARABLE_FILTER_2_COMPUTE_HLSL ":Gaussian11RGB",
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

	void ToneMapAcesOperator::SetLargeRadius(float radius)
	{
		if (_desc._maxLargeBloomRadius <= 0.f)
			Throw(std::runtime_error("Cannot set large bloom radius because this feature was disabled in the operator desc"));
		_brightPassLargeRadius = std::clamp(radius, 4.f, _desc._maxLargeBloomRadius);
	}

	float ToneMapAcesOperator::GetLargeRadius() const
	{
		return _brightPassLargeRadius;
	}

	void ToneMapAcesOperator::SetSmallRadius(float radius)
	{
		if (_desc._maxLargeBloomRadius <= 0.f)
			Throw(std::runtime_error("Cannot set small bloom radius because this feature was disabled in the operator desc"));
		_brightPassSmallRadius = radius;
		auto& params = *(AllParams*)_paramsData.data();
		params._brightPassParams.CalculateSmallBlurWeights(_brightPassSmallRadius);
		_paramsBufferCopyCountdown = dimof(_params);
	}

	float ToneMapAcesOperator::SetSmallRadius() const
	{
		return _brightPassSmallRadius;
	}

	void ToneMapAcesOperator::SetThreshold(float bloomThreshold)
	{
		if (_desc._maxLargeBloomRadius <= 0.f && !_desc._enableSmallBloom)
			Throw(std::runtime_error("Cannot set bloom property because this feature was disabled in the operator desc"));
		auto& params = *(AllParams*)_paramsData.data();
		params._brightPassParams._bloomThreshold = bloomThreshold;
		_paramsBufferCopyCountdown = dimof(_params);
	}

	float ToneMapAcesOperator::GetThreshold() const
	{
		auto& params = *(AllParams*)_paramsData.data();
		return params._brightPassParams._bloomThreshold;
	}

	void ToneMapAcesOperator::SetDesaturationFactor(float desatFactor)
	{
		if (_desc._maxLargeBloomRadius <= 0.f && !_desc._enableSmallBloom)
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

	void ToneMapAcesOperator::SetLargeBloomBrightness(Float3 brightness)
	{
		if (_desc._maxLargeBloomRadius <= 0.f)
			Throw(std::runtime_error("Cannot set bloom property because this feature was disabled in the operator desc"));
		auto& params = *(AllParams*)_paramsData.data();
		params._brightPassParams._largeRadiusBrightness = Expand(brightness, 1.f);
		_paramsBufferCopyCountdown = dimof(_params);
	}

	Float3 ToneMapAcesOperator::GetLargeBloomBrightness() const
	{
		auto& params = *(AllParams*)_paramsData.data();
		return Truncate(params._brightPassParams._largeRadiusBrightness);
	}

	void ToneMapAcesOperator::SetSmallBloomBrightness(Float3 brightness)
	{
		if (!_desc._enableSmallBloom)
			Throw(std::runtime_error("Cannot set bloom property because this feature was disabled in the operator desc"));
		auto& params = *(AllParams*)_paramsData.data();
		params._brightPassParams._smallRadiusBrightness = Expand(brightness, 1.f);
		_paramsBufferCopyCountdown = dimof(_params);
	}

	Float3 ToneMapAcesOperator::GetSmallBloomBrightness() const
	{
		auto& params = *(AllParams*)_paramsData.data();
		return Truncate(params._brightPassParams._smallRadiusBrightness);
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
			[op=this](LightingTechniqueIterator& iterator) {
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

}}

