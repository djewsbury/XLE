// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ScreenSpaceReflections.h"
#include "BlueNoiseGenerator.h"
#include "LightingEngineInternal.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/Services.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/Resource.h"
#include "../IAnnotator.h"
#include "../../Math/Transformations.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace LightingEngine
{
	struct ScreenSpaceReflectionsOperator::ResolutionDependentResources
	{
	public:
		std::shared_ptr<IResource> _rayListBuffer;
		std::shared_ptr<IResource> _tileMetaDataMask;
		std::shared_ptr<IResource> _tileTemporalVarianceMask;
		std::shared_ptr<IResource> _rayLengthsTexture;

		std::shared_ptr<IResourceView> _rayListBufferUAV;
		std::shared_ptr<IResourceView> _rayListBufferSRV;
		std::shared_ptr<IResourceView> _tileMetaDataMaskUAV;
		std::shared_ptr<IResourceView> _tileMetaDataMaskSRV;
		std::shared_ptr<IResourceView> _tileTemporalVarianceMaskUAV;
		std::shared_ptr<IResourceView> _tileTemporalVarianceMaskSRV;
		std::shared_ptr<IResourceView> _rayLengthsUAV;
		std::shared_ptr<IResourceView> _rayLengthsSRV;

		bool _pendingCompleteInitialization = false;
		
		void CompleteInitialization(Metal::DeviceContext& metalContext)
		{
			Metal::CompleteInitialization(metalContext, { _rayLengthsTexture.get() });
			_pendingCompleteInitialization = false;

			// Ensure the image is cleared
			VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.pNext = nullptr;
			barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			vkCmdPipelineBarrier(
				metalContext.GetActiveCommandList().GetUnderlying().get(),
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				1, &barrier,
				0, nullptr,
				0, nullptr);
		}

		ResolutionDependentResources(IDevice& device, const FrameBufferProperties& fbProps)
		: _pendingCompleteInitialization(true)
		{
			_rayLengthsTexture = device.CreateResource(
				CreateDesc(
					BindFlag::TransferDst | BindFlag::UnorderedAccess | BindFlag::ShaderResource,
					0, 0, TextureDesc::Plain2D(fbProps._outputWidth, fbProps._outputHeight, Format::R16_FLOAT),
					"ssr-ray-lengths"
				));
			_rayLengthsUAV = _rayLengthsTexture->CreateTextureView(BindFlag::UnorderedAccess);
			_rayLengthsSRV = _rayLengthsTexture->CreateTextureView(BindFlag::ShaderResource);

			auto tileCount = ((fbProps._outputWidth+7)/8)*((fbProps._outputHeight+7)/8);
			auto pixelCount = fbProps._outputWidth*fbProps._outputHeight;
			uint32_t ray_list_element_count = pixelCount;
			uint32_t ray_counter_element_count = 1;

			_rayListBuffer = device.CreateResource(
				CreateDesc(
					BindFlag::TexelBuffer | BindFlag::UnorderedAccess | BindFlag::ShaderResource,
					0, 0, LinearBufferDesc::Create(ray_list_element_count*sizeof(uint32_t)),
					"ssr-ray-list"
				));
			_rayListBufferUAV = _rayListBuffer->CreateTextureView(BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});
			_rayListBufferSRV = _rayListBuffer->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});

			_tileMetaDataMask = device.CreateResource(
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource,
					0, 0, LinearBufferDesc::Create(tileCount*sizeof(uint32_t)),
					"ssr-tile-meta-data"
				));
			_tileMetaDataMaskUAV = _tileMetaDataMask->CreateBufferView(BindFlag::UnorderedAccess);
			_tileMetaDataMaskSRV = _tileMetaDataMask->CreateBufferView(BindFlag::ShaderResource);

			_tileTemporalVarianceMask = device.CreateResource(
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource,
					0, 0, LinearBufferDesc::Create(tileCount*2*sizeof(uint32_t)),
					"ssr-tile-temporal-variance"
				));
			_tileTemporalVarianceMaskUAV = _tileTemporalVarianceMask->CreateBufferView(BindFlag::UnorderedAccess);
			_tileTemporalVarianceMaskSRV = _tileTemporalVarianceMask->CreateBufferView(BindFlag::ShaderResource);
		}
		ResolutionDependentResources() {};
	};

	constexpr uint64_t SSRReflections = ConstHash64<'SSRe', 'flec', 'tion'>::Value;
	constexpr uint64_t SSRConfidence = ConstHash64<'SSRC', 'onfi', 'denc', 'e'>::Value;
	constexpr uint64_t SSRInt = ConstHash64<'SSRI', 'nt'>::Value;
	constexpr uint64_t SSRDebug = ConstHash64<'SSRD', 'ebug'>::Value;

	constexpr unsigned s_nfb_outputUAV = 0;
	constexpr unsigned s_nfb_outputSRV = 1;

	constexpr unsigned s_nfb_hierarchicalDepthsSRV = 2;
	constexpr unsigned s_nfb_gbufferMotionSRV = 3;
	constexpr unsigned s_nfb_gbufferNormalSRV = 4;
	constexpr unsigned s_nfb_colorHDRSRV = 5;

	constexpr unsigned s_nfb_intUAV = 6;
	constexpr unsigned s_nfb_intSRV = 7;

	// depending on whether the find blur is enabled, you'll get one of the following
	constexpr unsigned s_nfb_intPrevSRV = 8;
	constexpr unsigned s_nfb_SSRPrevSRV = 8;

	constexpr unsigned s_nfb_debugUAV = 9;

	void ScreenSpaceReflectionsOperator::Execute(LightingEngine::LightingTechniqueIterator& iterator)
	{
		if (_res->_pendingCompleteInitialization) {
			CompleteInitialization(*iterator._threadContext);

			// hack -- force clear here
			auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);
			metalContext.Clear(*iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_intUAV), Float4{0.f,0.f,0.f,0.f});
			metalContext.Clear(*iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_intPrevSRV), Float4{0.f,0.f,0.f,0.f});
		}

		auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);

		IResourceView* rvs[27];
		rvs[0] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_outputUAV).get();			// g_denoised_reflections
		rvs[3] = _res->_rayListBufferUAV.get();													// g_ray_list
		rvs[4] = _res->_rayListBufferSRV.get();													// g_ray_list_read
		rvs[5] = _rayCounterBufferUAV.get();													// g_ray_counter
		rvs[6] = _res->_rayLengthsUAV.get();													// g_ray_lengths
		rvs[7] = _res->_rayLengthsSRV.get();													// g_ray_lengths_read
		rvs[8] = _res->_tileMetaDataMaskUAV.get();												// g_tile_meta_data_mask
		rvs[9] = _res->_tileMetaDataMaskSRV.get();												// g_tile_meta_data_mask_read
		rvs[10] = _res->_tileTemporalVarianceMaskUAV.get();										// g_temporal_variance_mask
		rvs[11] = _res->_tileTemporalVarianceMaskSRV.get();										// g_temporal_variance_mask_read
		if (_desc._enableFinalBlur) {
			rvs[1] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_intUAV).get();			// g_intersection_result
			rvs[2] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_intSRV).get();			// g_intersection_result_read
			rvs[12] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_intUAV).get(); 		// g_temporally_denoised_reflections
			rvs[13] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_intSRV).get();		// g_temporally_denoised_reflections_read
			rvs[14] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_intPrevSRV).get();	// g_temporally_denoised_reflections_history
			rvs[15] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_outputUAV).get();		// g_spatially_denoised_reflections
			rvs[16] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_outputSRV).get();		// g_spatially_denoised_reflections_read
		} else {
			rvs[1] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_outputUAV).get();		// g_intersection_result
			rvs[2] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_outputSRV).get();		// g_intersection_result_read
			rvs[12] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_outputUAV).get(); 	// g_temporally_denoised_reflections
			rvs[13] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_outputSRV).get();		// g_temporally_denoised_reflections_read
			rvs[14] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_SSRPrevSRV).get();	// g_temporally_denoised_reflections_history
			rvs[15] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_intUAV).get();		// g_spatially_denoised_reflections
			rvs[16] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_intSRV).get();		// g_spatially_denoised_reflections_read
		}
		rvs[17] = _indirectArgsBufferUAV.get();													// g_intersect_args

		rvs[18] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_hierarchicalDepthsSRV).get();		// HierarchicalDepths
		rvs[19] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_gbufferMotionSRV).get();			// GBufferMotion
		rvs[20] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_gbufferNormalSRV).get();			// GBufferNormal
		rvs[21] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_colorHDRSRV).get();				// LastFrameLit

		rvs[22] = _blueNoiseRes->_sobolBufferView.get();
		rvs[23] = _blueNoiseRes->_rankingTileBufferView.get();
		rvs[24] = _blueNoiseRes->_scramblingTileBufferView.get();

		rvs[25] = _skyCubeSRV ? _skyCubeSRV.get() : Techniques::Services::GetCommonResources()->_blackCubeSRV.get();

		rvs[26] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_debugUAV).get();			// SSRDebug

		UInt2 outputDims { iterator._rpi.GetFrameBufferDesc().GetProperties()._outputWidth, iterator._rpi.GetFrameBufferDesc().GetProperties()._outputHeight };
		struct ExtendedTransforms
		{
			Float4x4 _clipToView, _clipToWorld, _worldToView;
			Float4x4 _viewToWorld, _viewToProj, _prevWorldToClip;
			Float2 _negativeReciprocalScreenSize;
			unsigned _dummy[2];
		} extendedTransforms;
		auto& projDesc = iterator._parsingContext->GetProjectionDesc();
		extendedTransforms._clipToView = Inverse(projDesc._cameraToProjection);
		extendedTransforms._clipToWorld = Inverse(projDesc._worldToProjection);
		extendedTransforms._worldToView = InvertOrthonormalTransform(projDesc._cameraToWorld);
		extendedTransforms._viewToWorld = projDesc._cameraToWorld;
		extendedTransforms._viewToProj = projDesc._cameraToProjection;
		if (iterator._parsingContext->GetEnablePrevProjectionDesc()) {
			extendedTransforms._prevWorldToClip = iterator._parsingContext->GetPrevProjectionDesc()._worldToProjection;
		} else {
			extendedTransforms._prevWorldToClip = iterator._parsingContext->GetProjectionDesc()._worldToProjection;
		}
		extendedTransforms._negativeReciprocalScreenSize = {-1.0f/outputDims[0], -1.0f/outputDims[1]};
		struct FrameId
		{
			unsigned _frameId; unsigned _dummy[3];
		} frameId { _pingPongCounter, 0, 0, 0 };
		UniformsStream::ImmediateData immData[] { MakeOpaqueIteratorRange(extendedTransforms), MakeOpaqueIteratorRange(frameId) };

		UniformsStream us;
		us._resourceViews = MakeIteratorRange(rvs);
		us._immediateData = MakeIteratorRange(immData);

		_classifyTiles->Dispatch(
			*iterator._parsingContext,
			(outputDims[0]+7) / 8, (outputDims[1]+7) / 8, 1,
			us);

		{
			VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.pNext = nullptr;
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(
				metalContext.GetActiveCommandList().GetUnderlying().get(),
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				1, &barrier,
				0, nullptr,
				0, nullptr);
		}

		_prepareIndirectArgs->Dispatch(
			*iterator._parsingContext,
			1, 1, 1,
			us);

		{
			VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.pNext = nullptr;
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
			vkCmdPipelineBarrier(
				metalContext.GetActiveCommandList().GetUnderlying().get(),
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
				0,
				1, &barrier,
				0, nullptr,
				0, nullptr);
		}

		_intersect->BeginDispatches(*iterator._parsingContext, us);
		_intersect->DispatchIndirect(*_indirectArgsBuffer);
		_intersect->EndDispatches();

		{
			auto* res = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_intSRV)->GetResource().get();
			Metal::Internal::CaptureForBind cap0{metalContext, *res, BindFlag::ShaderResource};
			_resolveSpatial->Dispatch(
				*iterator._parsingContext,
				(outputDims[0]+7) / 8, (outputDims[1]+7) / 8, 1,
				us);
		}

		{
			// note: s_nfb_intPrevSRV will transition from BindFlag::ShaderResource -> BindFlag::UnorderedAccess for this (as a result of the end of the previous capture)
			auto* res = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_intPrevSRV)->GetResource().get();
			Metal::Internal::CaptureForBind cap0{metalContext, *res, BindFlag::ShaderResource};
			Metal::Internal::CaptureForBind cap1{metalContext, *_res->_rayLengthsTexture, BindFlag::ShaderResource};
			_resolveTemporal->Dispatch(
				*iterator._parsingContext,
				(outputDims[0]+7) / 8, (outputDims[1]+7) / 8, 1,
				us);
		}

		if (_desc._enableFinalBlur)
			_reflectionsBlur->Dispatch(
				*iterator._parsingContext,
				(outputDims[0]+7) / 8, (outputDims[1]+7) / 8, 1,
				us);

		++_pingPongCounter;
	}

	LightingEngine::RenderStepFragmentInterface ScreenSpaceReflectionsOperator::CreateFragment(const FrameBufferProperties& fbProps)
	{
		LightingEngine::RenderStepFragmentInterface result{PipelineType::Compute};
		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		auto outputReflections = result.DefineAttachment(SSRReflections).NoInitialState().FinalState(BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(outputReflections, BindFlag::UnorderedAccess);
		spDesc.AppendNonFrameBufferAttachmentView(outputReflections, BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::HierarchicalDepths), BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion), BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal), BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR), BindFlag::ShaderResource);

		auto intAttachment = result.DefineAttachment(SSRInt).NoInitialState().FinalState(BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(intAttachment, BindFlag::UnorderedAccess);
		spDesc.AppendNonFrameBufferAttachmentView(intAttachment, BindFlag::ShaderResource);
		if (_desc._enableFinalBlur) {
			auto intPrevAttachment = result.DefineAttachment(SSRInt+1).InitialState(BindFlag::ShaderResource).Discard();
			spDesc.AppendNonFrameBufferAttachmentView(intPrevAttachment, BindFlag::ShaderResource);
		} else {
			auto intPrevSSRAttachment = result.DefineAttachment(SSRReflections+1).InitialState(BindFlag::ShaderResource).Discard();
			spDesc.AppendNonFrameBufferAttachmentView(intPrevSSRAttachment, BindFlag::ShaderResource);
		}

		auto debugAttachment = result.DefineAttachment(SSRDebug).NoInitialState().FinalState(BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(debugAttachment, BindFlag::UnorderedAccess);

		spDesc.SetName("ssr-operator");
		result.AddSubpass(
			std::move(spDesc),
			[op=shared_from_this()](LightingEngine::LightingTechniqueIterator& iterator) {
				op->Execute(iterator);
			});
		_res = std::make_unique<ResolutionDependentResources>(*_device, fbProps);
		return result;
	}

	void ScreenSpaceReflectionsOperator::PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext) 
	{
		UInt2 fbSize{stitchingContext._workingProps._outputWidth, stitchingContext._workingProps._outputHeight};
		const auto colorFormat = Format::R11G11B10_FLOAT;
		if (_desc._enableFinalBlur) {	/////////////////////////////////////////////
			Techniques::PreregisteredAttachment attachments[] {
				Techniques::PreregisteredAttachment {
					SSRReflections,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource, 0, 0, 
						TextureDesc::Plain2D(fbSize[0], fbSize[1], colorFormat),
						"ssr-reflections"),
					Techniques::PreregisteredAttachment::State::Uninitialized
				},
				Techniques::PreregisteredAttachment {
					SSRInt,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource, 0, 0, 
						TextureDesc::Plain2D(fbSize[0], fbSize[1], colorFormat),
						"ssr-intermediate0"),
					Techniques::PreregisteredAttachment::State::PingPongBuffer0
				},
				Techniques::PreregisteredAttachment {
					SSRInt+1,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource, 0, 0, 
						TextureDesc::Plain2D(fbSize[0], fbSize[1], colorFormat),
						"ssr-intermediate1"),
					Techniques::PreregisteredAttachment::State::PingPongBuffer1
				}
			};
			for (const auto& a:attachments)
				stitchingContext.DefineAttachment(a);
		} else {	/////////////////////////////////////////////
			Techniques::PreregisteredAttachment attachments[] {
				Techniques::PreregisteredAttachment {
					SSRReflections,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource, 0, 0, 
						TextureDesc::Plain2D(fbSize[0], fbSize[1], colorFormat),
						"ssr-reflections0"),
					Techniques::PreregisteredAttachment::State::PingPongBuffer0
				},
				Techniques::PreregisteredAttachment {
					SSRReflections+1,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource, 0, 0, 
						TextureDesc::Plain2D(fbSize[0], fbSize[1], colorFormat),
						"ssr-reflections1"),
					Techniques::PreregisteredAttachment::State::PingPongBuffer1
				},
				Techniques::PreregisteredAttachment {
					SSRInt,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource, 0, 0, 
						TextureDesc::Plain2D(fbSize[0], fbSize[1], colorFormat),
						"ssr-intermediate"),
					Techniques::PreregisteredAttachment::State::Uninitialized
				}
			};
			for (const auto& a:attachments)
				stitchingContext.DefineAttachment(a);
		}	/////////////////////////////////////////////

		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				SSRConfidence,
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::R8_UNORM),
					"ssr-confidence0"),
				Techniques::PreregisteredAttachment::State::PingPongBuffer0
			},
			Techniques::PreregisteredAttachment {
				SSRConfidence+1,
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::R8_UNORM),
					"ssr-confidence1"),
				Techniques::PreregisteredAttachment::State::PingPongBuffer1
			},
			Techniques::PreregisteredAttachment {
				SSRDebug,
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::R32G32B32A32_FLOAT),
					"ssr-debug"),
				Techniques::PreregisteredAttachment::State::Uninitialized
			}
		};
		for (const auto& a:attachments)
			stitchingContext.DefineAttachment(a);
	}

	void ScreenSpaceReflectionsOperator::ResetAccumulation() { _pingPongCounter = ~0u; }

	void ScreenSpaceReflectionsOperator::CompleteInitialization(IThreadContext& threadContext)
	{
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		vkCmdFillBuffer(
			metalContext.GetActiveCommandList().GetUnderlying().get(),
			checked_cast<Metal::Resource*>(_rayCounterBufferUAV->GetResource().get())->GetBuffer(), 
			0, VK_WHOLE_SIZE, 0);
		_res->CompleteInitialization(metalContext);
	}

	void ScreenSpaceReflectionsOperator::SetSpecularIBL(std::shared_ptr<IResourceView> inputView)
	{
		_skyCubeSRV = inputView;
	}

	ScreenSpaceReflectionsOperator::ScreenSpaceReflectionsOperator(
		const ScreenSpaceReflectionsOperatorDesc& desc,
		std::shared_ptr<Techniques::IComputeShaderOperator> classifyTiles,
		std::shared_ptr<Techniques::IComputeShaderOperator> prepareIndirectArgs,
		std::shared_ptr<Techniques::IComputeShaderOperator> intersect,
		std::shared_ptr<Techniques::IComputeShaderOperator> resolveSpatial,
		std::shared_ptr<Techniques::IComputeShaderOperator> resolveTemporal,
		std::shared_ptr<Techniques::IComputeShaderOperator> reflectionsBlur,
		std::shared_ptr<IDevice> device)
	: _desc(desc)
	, _classifyTiles(std::move(classifyTiles))
	, _prepareIndirectArgs(std::move(prepareIndirectArgs))
	, _intersect(std::move(intersect))
	, _resolveSpatial(std::move(resolveSpatial))
	, _resolveTemporal(std::move(resolveTemporal))
	, _reflectionsBlur(std::move(reflectionsBlur))
	, _device(std::move(device))
	{
		_blueNoiseRes = std::make_unique<BlueNoiseGeneratorTables>(*_device);

		_depVal = ::Assets::GetDepValSys().Make();
		_depVal.RegisterDependency(_classifyTiles->GetDependencyValidation());
		_depVal.RegisterDependency(_prepareIndirectArgs->GetDependencyValidation());
		_depVal.RegisterDependency(_intersect->GetDependencyValidation());
		_depVal.RegisterDependency(_resolveSpatial->GetDependencyValidation());
		_depVal.RegisterDependency(_resolveTemporal->GetDependencyValidation());
		_depVal.RegisterDependency(_reflectionsBlur->GetDependencyValidation());

		///////////////////

		auto rayCounterBuffer = _device->CreateResource(
			CreateDesc(
				BindFlag::TransferDst | BindFlag::UnorderedAccess | BindFlag::ShaderResource | BindFlag::TexelBuffer,
				0, 0, LinearBufferDesc::Create(2*sizeof(uint32_t)),
				"ssr-ray-counter"
			));
		_rayCounterBufferUAV = rayCounterBuffer->CreateTextureView(BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});
		_rayCounterBufferSRV = rayCounterBuffer->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});

		///////////////////

		_indirectArgsBuffer = _device->CreateResource(
			CreateDesc(
				BindFlag::DrawIndirectArgs | BindFlag::UnorderedAccess | BindFlag::TexelBuffer,
				0, 0, LinearBufferDesc::Create(3*sizeof(uint32_t)),
				"ssr-indirect-args"
			));
		_indirectArgsBufferUAV = _indirectArgsBuffer->CreateTextureView(BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});
	}

	ScreenSpaceReflectionsOperator::~ScreenSpaceReflectionsOperator() {}
	
	void ScreenSpaceReflectionsOperator::ConstructToFuture(
		::Assets::FuturePtr<ScreenSpaceReflectionsOperator>& future,
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
		const ScreenSpaceReflectionsOperatorDesc& desc)
	{
		UniformsStreamInterface usi;
		usi.BindResourceView(0, Hash64("g_denoised_reflections"));

		usi.BindResourceView(1, Hash64("g_intersection_result"));
		usi.BindResourceView(2, Hash64("g_intersection_result_read"));

		usi.BindResourceView(3, Hash64("g_ray_list"));
		usi.BindResourceView(4, Hash64("g_ray_list_read"));
		usi.BindResourceView(5, Hash64("g_ray_counter"));
		usi.BindResourceView(6, Hash64("g_ray_lengths"));
		usi.BindResourceView(7, Hash64("g_ray_lengths_read"));

		usi.BindResourceView(8, Hash64("g_tile_meta_data_mask"));
		usi.BindResourceView(9, Hash64("g_tile_meta_data_mask_read"));

		usi.BindResourceView(10, Hash64("g_temporal_variance_mask"));
		usi.BindResourceView(11, Hash64("g_temporal_variance_mask_read"));
		usi.BindResourceView(12, Hash64("g_temporally_denoised_reflections"));
		usi.BindResourceView(13, Hash64("g_temporally_denoised_reflections_read"));
		usi.BindResourceView(14, Hash64("g_temporally_denoised_reflections_history"));
		usi.BindResourceView(15, Hash64("g_spatially_denoised_reflections"));
		usi.BindResourceView(16, Hash64("g_spatially_denoised_reflections_read"));

		usi.BindResourceView(17, Hash64("g_intersect_args"));			

		usi.BindResourceView(18, Hash64("DownsampleDepths"));
		usi.BindResourceView(19, Hash64("GBufferMotion"));
		usi.BindResourceView(20, Hash64("GBufferNormal"));
		usi.BindResourceView(21, Hash64("LastFrameLit"));

		usi.BindResourceView(22, Hash64("BN_Sobol"));
		usi.BindResourceView(23, Hash64("BN_Ranking"));
		usi.BindResourceView(24, Hash64("BN_Scrambling"));

		usi.BindResourceView(25, Hash64("SkyCube"));

		usi.BindResourceView(26, Hash64("SSRDebug"));

		usi.BindImmediateData(0, Hash64("ExtendedTransforms"));
		usi.BindImmediateData(1, Hash64("FrameIdBuffer"));

		ParameterBox selectors;
		selectors.SetParameter("DEBUGGING_PRODUCTS", 1);
		auto classifyTiles = Techniques::CreateComputeOperator(
			pipelinePool,
			SSR_CLASSIFY_TILES_HLSL ":ClassifyTiles",
			selectors,
			SSR_PIPELINE ":Main",
			usi);

		auto prepareIndirectArgs = Techniques::CreateComputeOperator(
			pipelinePool,
			SSR_CLASSIFY_TILES_HLSL ":PrepareIndirectArgs",
			selectors,
			SSR_PIPELINE ":Main",
			usi);

		auto intersect = Techniques::CreateComputeOperator(
			pipelinePool,
			SSR_INTERSECT_HLSL ":SSRIntersect",
			selectors,
			SSR_PIPELINE ":Main",
			usi);

		auto resolveSpatial = Techniques::CreateComputeOperator(
			pipelinePool,
			SSR_RESOLVE_SPATIAL_HLSL ":ResolveSpatial",
			selectors,
			SSR_PIPELINE ":Main",
			usi);

		auto resolveTemporal = Techniques::CreateComputeOperator(
			pipelinePool,
			SSR_RESOLVE_TEMPORAL_HLSL ":ResolveTemporal",
			selectors,
			SSR_PIPELINE ":Main",
			usi);

		auto reflectionsBlur = Techniques::CreateComputeOperator(
			pipelinePool,
			SSR_REFLECTIONS_BLUR_HLSL ":ReflectionsBlur",
			selectors,
			SSR_PIPELINE ":Main",
			usi);

		::Assets::WhenAll(classifyTiles, prepareIndirectArgs, intersect, resolveSpatial, resolveTemporal, reflectionsBlur).ThenConstructToFuture(
			future, 
			[dev=pipelinePool->GetDevice(), desc](auto classifyTiles, auto prepareIndirectArgs, auto intersect, auto resolveSpatial, auto resolveTemporal, auto reflectionsBlur) { 
				return std::make_shared<ScreenSpaceReflectionsOperator>(
					desc,
					std::move(classifyTiles), std::move(prepareIndirectArgs), std::move(intersect), std::move(resolveSpatial), std::move(resolveTemporal), std::move(reflectionsBlur),
					std::move(dev)); 
			});
	}

	uint64_t ScreenSpaceReflectionsOperatorDesc::GetHash(uint64_t seed) const
	{
		return rotl64(seed, !!_enableFinalBlur);
	}

	/*
		Reference for some of the main shader inputs:

		With blur step
		==========================================================================

			intersect
			------------------------------------------
			_rayListBuffer in "g_ray_list_read" -> intermediate[0] in "g_intersection_result"

			spatial
			------------------------------------------
			intermediate[0] in "g_intersection_result_read" -> SSRReflections in "g_spatially_denoised_reflections"

			temporal
			------------------------------------------
			SSRReflections in "g_spatially_denoised_reflections_read" 
			& intermediate[1] in "g_temporally_denoised_reflections_history" -> intermediate[0] in "g_temporally_denoised_reflections"

			blur
			------------------------------------------
			intermediate[0] "g_temporally_denoised_reflections_read" -> SSRReflections in "g_denoised_reflections"


		With blur step
		==========================================================================

			intersect
			------------------------------------------
			_rayListBuffer in "g_ray_list_read" -> SSRReflections[0] in "g_intersection_result"

			spatial
			------------------------------------------
			SSRReflections[0] in "g_intersection_result_read" -> intermediate in "g_spatially_denoised_reflections"

			temporal
			------------------------------------------
			intermediate in "g_spatially_denoised_reflections_read" 
			& SSRReflections[1] in "g_temporally_denoised_reflections_history" -> SSRReflections[0] in "g_temporally_denoised_reflections"
	*/
	
}}

