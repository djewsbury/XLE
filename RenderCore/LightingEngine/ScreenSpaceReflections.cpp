// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ScreenSpaceReflections.h"
#include "BlueNoiseGenerator.h"
#include "LightingEngineIterator.h"
#include "RenderStepFragments.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/Services.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../Assets/PredefinedCBLayout.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/Resource.h"
#include "../IAnnotator.h"
#include "../../Math/Transformations.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../xleres/FileList.h"

using namespace Utility::Literals;

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
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
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
					TextureDesc::Plain2D(fbProps._width, fbProps._height, Format::R16_FLOAT)),
				"ssr-ray-lengths");
			_rayLengthsUAV = _rayLengthsTexture->CreateTextureView(BindFlag::UnorderedAccess);
			_rayLengthsSRV = _rayLengthsTexture->CreateTextureView(BindFlag::ShaderResource);

			auto tileCount = ((fbProps._width+7)/8)*((fbProps._height+7)/8);
			auto pixelCount = fbProps._width*fbProps._height;
			uint32_t ray_list_element_count = pixelCount;

			_rayListBuffer = device.CreateResource(
				CreateDesc(
					BindFlag::TexelBuffer | BindFlag::UnorderedAccess | BindFlag::ShaderResource,
					LinearBufferDesc::Create(ray_list_element_count*sizeof(uint32_t))),
				"ssr-ray-list");
			_rayListBufferUAV = _rayListBuffer->CreateTextureView(BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});
			_rayListBufferSRV = _rayListBuffer->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});

			_tileMetaDataMask = device.CreateResource(
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource,
					LinearBufferDesc::Create(tileCount*sizeof(uint32_t))),
				"ssr-tile-meta-data");
			_tileMetaDataMaskUAV = _tileMetaDataMask->CreateBufferView(BindFlag::UnorderedAccess);
			_tileMetaDataMaskSRV = _tileMetaDataMask->CreateBufferView(BindFlag::ShaderResource);

			_tileTemporalVarianceMask = device.CreateResource(
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource,
					LinearBufferDesc::Create(tileCount*2*sizeof(uint32_t))),
				"ssr-tile-temporal-variance");
			_tileTemporalVarianceMaskUAV = _tileTemporalVarianceMask->CreateBufferView(BindFlag::UnorderedAccess);
			_tileTemporalVarianceMaskSRV = _tileTemporalVarianceMask->CreateBufferView(BindFlag::ShaderResource);
		}
		ResolutionDependentResources() {};
	};

	constexpr uint64_t SSRReflections = "SSReflection"_h;
	constexpr uint64_t SSRConfidence = "SSRConfidence"_h;
	constexpr uint64_t SSRConfidenceInt = "SSRConfidenceInt"_h;
	constexpr uint64_t SSRInt = "SSRInt"_h;
	constexpr uint64_t SSRDebug = "SSRDebug"_h;

	constexpr unsigned s_nfb_outputUAV = 0;
	constexpr unsigned s_nfb_outputSRV = 1;

	constexpr unsigned s_nfb_hierarchicalDepthsSRV = 2;
	constexpr unsigned s_nfb_gbufferMotionSRV = 3;
	constexpr unsigned s_nfb_gbufferNormalSRV = 4;
	constexpr unsigned s_nfb_gbufferNormalPrevSRV = 5;
	constexpr unsigned s_nfb_colorHDRSRV = 6;

	constexpr unsigned s_nfb_intUAV = 7;
	constexpr unsigned s_nfb_intSRV = 8;

	// depending on whether the find blur is enabled, you'll get one of the following
	constexpr unsigned s_nfb_intPrevSRV = 9;
	constexpr unsigned s_nfb_SSRPrevSRV = 9;

	constexpr unsigned s_nfb_debugUAV = 10;

	constexpr unsigned s_nfb_confidenceUAV = 11;
	constexpr unsigned s_nfb_confidenceSRV = 12;
	constexpr unsigned s_nfb_confidencePrevSRV = 13;
	constexpr unsigned s_nfb_confidenceIntUAV = 14;
	constexpr unsigned s_nfb_confidenceIntSRV = 15;

	void ScreenSpaceReflectionsOperator::Execute(LightingEngine::LightingTechniqueIterator& iterator)
	{
		assert(_secondStageConstructionState == 2);
		assert(_classifyTiles && _prepareIndirectArgs && _intersect && _resolveSpatial && _resolveTemporal);
		assert(!_desc._enableFinalBlur || _reflectionsBlur);

		if (_pendingCompleteInit) {
			CompleteInitialization(*iterator._threadContext);

			// hack -- force clear here
			auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);
			metalContext.Clear(*iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_intUAV), Float4{0.f,0.f,0.f,0.f});
			metalContext.Clear(*iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_intPrevSRV), Float4{0.f,0.f,0.f,0.f});
		}

		auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);

		IResourceView* rvs[35];
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
		rvs[21] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_gbufferNormalPrevSRV).get();		// GBufferNormalPrev
		rvs[22] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_colorHDRSRV).get();				// LastFrameLit

		rvs[23] = _blueNoiseRes->_sobolBufferView.get();
		rvs[24] = _blueNoiseRes->_rankingTileBufferView.get();
		rvs[25] = _blueNoiseRes->_scramblingTileBufferView.get();

		rvs[26] = _skyCubeSRV ? _skyCubeSRV.get() : Techniques::Services::GetCommonResources()->_blackCubeSRV.get();

		rvs[27] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_debugUAV).get();			// SSRDebug

		rvs[28] = _configCB.get();

		if (_desc._splitConfidence) {
			rvs[29] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_confidenceUAV).get();		// g_confidence_result
			rvs[30] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_confidenceSRV).get();		// g_confidence_result_read
			rvs[31] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_confidenceIntUAV).get();	// g_spatially_denoised_confidence

			rvs[32] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_confidencePrevSRV).get();	// g_temporally_denoised_confidence_history
			rvs[33] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_confidenceUAV).get();		// g_temporally_denoised_confidence
			rvs[34] = iterator._rpi.GetNonFrameBufferAttachmentView(s_nfb_confidenceIntSRV).get();	// g_spatially_denoised_confidence_read
		}

		UInt2 outputDims { iterator._rpi.GetFrameBufferDesc().GetProperties()._width, iterator._rpi.GetFrameBufferDesc().GetProperties()._height };
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
		} frameId { _pingPongCounter, {0, 0, 0} };
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

		_intersect->BeginDispatches(*iterator._parsingContext, us).DispatchIndirect(*_indirectArgsBuffer);

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
		assert(_secondStageConstructionState == 0);
		LightingEngine::RenderStepFragmentInterface result{PipelineType::Compute};
		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		auto outputReflections = result.DefineAttachment(SSRReflections).NoInitialState().FinalState(BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(outputReflections, BindFlag::UnorderedAccess);
		spDesc.AppendNonFrameBufferAttachmentView(outputReflections, BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::HierarchicalDepths), BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion), BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal), BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormalPrev), BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDRPrev).Discard(), BindFlag::ShaderResource);		// discard ColorHDRPrev to encourage reuse of that large target

		auto intAttachment = result.DefineAttachment(SSRInt).NoInitialState().FinalState(BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(intAttachment, BindFlag::UnorderedAccess);
		spDesc.AppendNonFrameBufferAttachmentView(intAttachment, BindFlag::ShaderResource);
		if (_desc._enableFinalBlur) {
			auto intPrevAttachment = result.DefineAttachment(SSRInt+1).InitialState(BindFlag::ShaderResource).Discard();
			spDesc.AppendNonFrameBufferAttachmentView(intPrevAttachment, BindFlag::ShaderResource);
		} else {
			auto SRRPrevAttachment = result.DefineAttachment(SSRReflections+1).InitialState(BindFlag::ShaderResource).Discard();
			spDesc.AppendNonFrameBufferAttachmentView(SRRPrevAttachment, BindFlag::ShaderResource);
		}

		auto debugAttachment = result.DefineAttachment(SSRDebug).NoInitialState().FinalState(BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(debugAttachment, BindFlag::UnorderedAccess);

		if (_desc._splitConfidence) {
			auto confidenceAttachment = result.DefineAttachment(SSRConfidence).NoInitialState().FinalState(BindFlag::ShaderResource);
			auto confidencePrevAttachment = result.DefineAttachment(SSRConfidence+1).InitialState(BindFlag::ShaderResource).Discard();
			auto confidenceIntAttachment = result.DefineAttachment(SSRConfidenceInt).NoInitialState().FinalState(BindFlag::ShaderResource);
			spDesc.AppendNonFrameBufferAttachmentView(confidenceAttachment, BindFlag::UnorderedAccess);
			spDesc.AppendNonFrameBufferAttachmentView(confidenceAttachment, BindFlag::ShaderResource);
			spDesc.AppendNonFrameBufferAttachmentView(confidencePrevAttachment, BindFlag::ShaderResource);
			spDesc.AppendNonFrameBufferAttachmentView(confidenceIntAttachment, BindFlag::UnorderedAccess);
			spDesc.AppendNonFrameBufferAttachmentView(confidenceIntAttachment, BindFlag::ShaderResource);
		}

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
		UInt2 fbSize{stitchingContext._workingProps._width, stitchingContext._workingProps._height};
		const auto colorFormat = Format::R11G11B10_FLOAT;
		if (_desc._enableFinalBlur) {	/////////////////////////////////////////////
			Techniques::PreregisteredAttachment attachments[] {
				Techniques::PreregisteredAttachment {
					SSRReflections,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource | BindFlag::TransferDst,
						TextureDesc::Plain2D(fbSize[0], fbSize[1], colorFormat)),
					"ssr-reflections",
					Techniques::PreregisteredAttachment::State::Uninitialized
				},
				Techniques::PreregisteredAttachment {
					SSRInt,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource | BindFlag::TransferDst,
						TextureDesc::Plain2D(fbSize[0], fbSize[1], colorFormat)),
					"ssr-intermediate",
					Techniques::PreregisteredAttachment::State::Uninitialized
				}
			};
			for (const auto& a:attachments)
				stitchingContext.DefineAttachment(a);
			stitchingContext.DefineDoubleBufferAttachment(SSRInt, MakeClearValue(0,0,0,0), BindFlag::ShaderResource);
		} else {	/////////////////////////////////////////////
			Techniques::PreregisteredAttachment attachments[] {
				Techniques::PreregisteredAttachment {
					SSRReflections,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource | BindFlag::TransferDst,
						TextureDesc::Plain2D(fbSize[0], fbSize[1], colorFormat)),
					"ssr-reflections0",
					Techniques::PreregisteredAttachment::State::Uninitialized
				},
				Techniques::PreregisteredAttachment {
					SSRInt,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource | BindFlag::TransferDst,
						TextureDesc::Plain2D(fbSize[0], fbSize[1], colorFormat)),
					"ssr-intermediate",
					Techniques::PreregisteredAttachment::State::Uninitialized
				}
			};
			for (const auto& a:attachments)
				stitchingContext.DefineAttachment(a);
			stitchingContext.DefineDoubleBufferAttachment(SSRReflections, MakeClearValue(0,0,0,0), BindFlag::ShaderResource);
		}	/////////////////////////////////////////////

		if (_desc._splitConfidence) {
			Techniques::PreregisteredAttachment attachments[] {
				Techniques::PreregisteredAttachment {
					SSRConfidence,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource,
						TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::R8_UNORM)),
					"ssr-confidence0",
					Techniques::PreregisteredAttachment::State::Uninitialized
				},
				Techniques::PreregisteredAttachment {
					SSRConfidenceInt,
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource,
						TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::R8_UNORM)),
					"ssr-confidence-intermediate",
					Techniques::PreregisteredAttachment::State::Uninitialized
				}
			};
			for (const auto& a:attachments)
				stitchingContext.DefineAttachment(a);
			stitchingContext.DefineDoubleBufferAttachment(SSRConfidence, MakeClearValue(0,0,0,0), BindFlag::ShaderResource);
		}

		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				SSRDebug,
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::R32G32B32A32_FLOAT)),
				"ssr-debug",
				Techniques::PreregisteredAttachment::State::Uninitialized
			}
		};
		for (const auto& a:attachments)
			stitchingContext.DefineAttachment(a);

		// We need last frame buffers for some of the biggest render targets (unfortunately)
		stitchingContext.DefineDoubleBufferAttachment(Techniques::AttachmentSemantics::GBufferNormal, MakeClearValue(0,0,0,0), BindFlag::ShaderResource);
		stitchingContext.DefineDoubleBufferAttachment(Techniques::AttachmentSemantics::ColorHDR, MakeClearValue(0,0,0,0), BindFlag::ShaderResource);
	}

	void ScreenSpaceReflectionsOperator::ResetAccumulation() { _pingPongCounter = ~0u; }

	void ScreenSpaceReflectionsOperator::CompleteInitialization(IThreadContext& threadContext)
	{
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		_res->CompleteInitialization(metalContext);

		if (_pendingCompleteInit) {
			vkCmdFillBuffer(
				metalContext.GetActiveCommandList().GetUnderlying().get(),
				checked_cast<Metal::Resource*>(_rayCounterBufferUAV->GetResource().get())->GetBuffer(), 
				0, VK_WHOLE_SIZE, 0);
			
			_blueNoiseRes->CompleteInitialization(threadContext);

			assert(!_configCBData.empty());
			if (!_configCBData.empty()) {
				_configCB = _device->CreateResource(CreateDesc(BindFlag::ConstantBuffer|BindFlag::TransferDst, LinearBufferDesc::Create((unsigned)_configCBData.size())), "ssr-config")->CreateBufferView();
				Metal::DeviceContext::Get(threadContext)->BeginBlitEncoder().Write(*_configCB->GetResource(), MakeIteratorRange(_configCBData));
			}

			_pendingCompleteInit = false;
		}
	}

	void ScreenSpaceReflectionsOperator::SetSpecularIBL(std::shared_ptr<IResourceView> inputView)
	{
		_skyCubeSRV = inputView;
	}

	ScreenSpaceReflectionsOperator::ScreenSpaceReflectionsOperator(
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
		const ScreenSpaceReflectionsOperatorDesc& desc)
	: _desc(desc)
	, _pipelinePool(std::move(pipelinePool))
	{
		_device = _pipelinePool->GetDevice();
		_blueNoiseRes = std::make_unique<BlueNoiseGeneratorTables>(*_device);
	}

	ScreenSpaceReflectionsOperator::~ScreenSpaceReflectionsOperator() {}

	static const RenderCore::Assets::PredefinedCBLayout& FindCBLayout(const RenderCore::Assets::PredefinedPipelineLayout& layout, StringSection<> name)
	{
		for (const auto& l:layout._descriptorSets)
			for (const auto& cb:l._descSet->_slots)
				if (XlEqString(name, cb._name) && cb._type == DescriptorType::UniformBuffer && cb._cbIdx != ~0u)
					return *l._descSet->_constantBuffers[cb._cbIdx];
		Throw(std::runtime_error("Missing CBLayout named (" + name.AsString() + ")"));
	}
	
	void ScreenSpaceReflectionsOperator::SecondStageConstruction(
		std::promise<std::shared_ptr<ScreenSpaceReflectionsOperator>>&& promise,
		const Techniques::FrameBufferTarget& fbTarget)
	{
		assert(_secondStageConstructionState == 0);
		_secondStageConstructionState = 1;

		UniformsStreamInterface usi;
		usi.BindResourceView(0, "g_denoised_reflections"_h);

		usi.BindResourceView(1, "g_intersection_result"_h);
		usi.BindResourceView(2, "g_intersection_result_read"_h);

		usi.BindResourceView(3, "g_ray_list"_h);
		usi.BindResourceView(4, "g_ray_list_read"_h);
		usi.BindResourceView(5, "g_ray_counter"_h);
		usi.BindResourceView(6, "g_ray_lengths"_h);
		usi.BindResourceView(7, "g_ray_lengths_read"_h);

		usi.BindResourceView(8, "g_tile_meta_data_mask"_h);
		usi.BindResourceView(9, "g_tile_meta_data_mask_read"_h);

		usi.BindResourceView(10, "g_temporal_variance_mask"_h);
		usi.BindResourceView(11, "g_temporal_variance_mask_read"_h);
		usi.BindResourceView(12, "g_temporally_denoised_reflections"_h);
		usi.BindResourceView(13, "g_temporally_denoised_reflections_read"_h);
		usi.BindResourceView(14, "g_temporally_denoised_reflections_history"_h);
		usi.BindResourceView(15, "g_spatially_denoised_reflections"_h);
		usi.BindResourceView(16, "g_spatially_denoised_reflections_read"_h);

		usi.BindResourceView(17, "g_intersect_args"_h);			

		usi.BindResourceView(18, "DownsampleDepths"_h);
		usi.BindResourceView(19, "GBufferMotion"_h);
		usi.BindResourceView(20, "GBufferNormal"_h);
		usi.BindResourceView(21, "GBufferNormalPrev"_h);
		usi.BindResourceView(22, "LastFrameLit"_h);

		usi.BindResourceView(23, "BN_Sobol"_h);
		usi.BindResourceView(24, "BN_Ranking"_h);
		usi.BindResourceView(25, "BN_Scrambling"_h);

		usi.BindResourceView(26, "SkyCube"_h);

		usi.BindResourceView(27, "SSRDebug"_h);
		usi.BindResourceView(28, "SSRConfiguration"_h);

		if (_desc._splitConfidence) {
			usi.BindResourceView(29, "g_confidence_result"_h);
			usi.BindResourceView(30, "g_confidence_result_read"_h);
			usi.BindResourceView(31, "g_spatially_denoised_confidence"_h);
			usi.BindResourceView(32, "g_temporally_denoised_confidence_history"_h);
			usi.BindResourceView(33, "g_temporally_denoised_confidence"_h);
			usi.BindResourceView(34, "g_spatially_denoised_confidence_read"_h);
		}

		usi.BindImmediateData(0, "ExtendedTransforms"_h);
		usi.BindImmediateData(1, "FrameIdBuffer"_h);

		ParameterBox selectors;
		selectors.SetParameter("DEBUGGING_PRODUCTS", 1);
		if (_desc._splitConfidence)
			selectors.SetParameter("SPLIT_CONFIDENCE", 1);
		auto classifyTiles = Techniques::CreateComputeOperator(
			_pipelinePool,
			SSR_CLASSIFY_TILES_HLSL ":ClassifyTiles",
			selectors,
			SSR_PIPELINE ":Main",
			usi);

		auto prepareIndirectArgs = Techniques::CreateComputeOperator(
			_pipelinePool,
			SSR_CLASSIFY_TILES_HLSL ":PrepareIndirectArgs",
			selectors,
			SSR_PIPELINE ":Main",
			usi);

		auto intersect = Techniques::CreateComputeOperator(
			_pipelinePool,
			SSR_INTERSECT_HLSL ":SSRIntersect",
			selectors,
			SSR_PIPELINE ":Main",
			usi);

		auto resolveSpatial = Techniques::CreateComputeOperator(
			_pipelinePool,
			SSR_RESOLVE_SPATIAL_HLSL ":ResolveSpatial",
			selectors,
			SSR_PIPELINE ":Main",
			usi);

		auto resolveTemporal = Techniques::CreateComputeOperator(
			_pipelinePool,
			SSR_RESOLVE_TEMPORAL_HLSL ":ResolveTemporal",
			selectors,
			SSR_PIPELINE ":Main",
			usi);

		auto reflectionsBlur = Techniques::CreateComputeOperator(
			_pipelinePool,
			SSR_REFLECTIONS_BLUR_HLSL ":ReflectionsBlur",
			selectors,
			SSR_PIPELINE ":Main",
			usi);

		auto pipelineLayoutFuture = ::Assets::MakeAssetPtr<RenderCore::Assets::PredefinedPipelineLayout>(SSR_PIPELINE ":Main");

		::Assets::WhenAll(classifyTiles, prepareIndirectArgs, intersect, resolveSpatial, resolveTemporal, reflectionsBlur, pipelineLayoutFuture).ThenConstructToPromise(
			std::move(promise), 
			[strongThis=shared_from_this()](auto classifyTiles, auto prepareIndirectArgs, auto intersect, auto resolveSpatial, auto resolveTemporal, auto reflectionsBlur, auto pipelineLayout) { 
				assert(strongThis->_secondStageConstructionState == 1);

				::Assets::DependencyValidationMarker subDepVals[] {
					classifyTiles->GetDependencyValidation(),
					prepareIndirectArgs->GetDependencyValidation(),
					intersect->GetDependencyValidation(),
					resolveSpatial->GetDependencyValidation(),
					resolveTemporal->GetDependencyValidation(),
					reflectionsBlur->GetDependencyValidation(),
					pipelineLayout->GetDependencyValidation()
				};
				strongThis->_depVal = ::Assets::GetDepValSys().MakeOrReuse(subDepVals);

				strongThis->_classifyTiles = std::move(classifyTiles);
				strongThis->_prepareIndirectArgs = std::move(prepareIndirectArgs);
				strongThis->_intersect = std::move(intersect);
				strongThis->_resolveSpatial = std::move(resolveSpatial);
				strongThis->_resolveTemporal = std::move(resolveTemporal);
				strongThis->_reflectionsBlur = std::move(reflectionsBlur);
				strongThis->_reflectionsBlur = std::move(reflectionsBlur);

				{
					ParameterBox params;
					auto configCBLayout = FindCBLayout(*pipelineLayout, "SSRConfiguration");
					strongThis->_configCBData = configCBLayout.BuildCBDataAsVector(params, Techniques::GetDefaultShaderLanguage());
				}

				///////////////////

				auto rayCounterBuffer = strongThis->_device->CreateResource(
					CreateDesc(
						BindFlag::TransferDst | BindFlag::UnorderedAccess | BindFlag::ShaderResource | BindFlag::TexelBuffer,
						LinearBufferDesc::Create(2*sizeof(uint32_t))),
					"ssr-ray-counter");
				strongThis->_rayCounterBufferUAV = rayCounterBuffer->CreateTextureView(BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});
				strongThis->_rayCounterBufferSRV = rayCounterBuffer->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});

				///////////////////

				strongThis->_indirectArgsBuffer = strongThis->_device->CreateResource(
					CreateDesc(
						BindFlag::DrawIndirectArgs | BindFlag::UnorderedAccess | BindFlag::TexelBuffer,
						LinearBufferDesc::Create(3*sizeof(uint32_t))),
					"ssr-indirect-args");
				strongThis->_indirectArgsBufferUAV = strongThis->_indirectArgsBuffer->CreateTextureView(BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});

				strongThis->_secondStageConstructionState = 2;

				return strongThis;
			});
	}

	uint64_t ScreenSpaceReflectionsOperatorDesc::GetHash(uint64_t seed) const
	{
		uint32_t value = (_enableFinalBlur<<1) | (_splitConfidence<<0);
		return rotl64(seed, value);
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

