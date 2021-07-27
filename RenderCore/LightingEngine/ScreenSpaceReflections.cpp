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
#include "../Metal/DeviceContext.h"
#include "../Metal/Resource.h"
#include "../IAnnotator.h"
#include "../../Math/Transformations.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace LightingEngine
{
	struct ScreenSpaceReflectionsOperator::ResolutionDependentResources
	{
	public:
		std::shared_ptr<IResource> _outputTexture;
		std::shared_ptr<IResource> _rayListBuffer;
		std::shared_ptr<IResource> _tileMetaDataMask;
		std::shared_ptr<IResource> _tileTemporalVarianceMask;
		std::shared_ptr<IResource> _temporalDenoiseResult[2];
		std::shared_ptr<IResource> _rayLengthsTexture;

		std::shared_ptr<IResourceView> _outputTextureUAV;
		std::shared_ptr<IResourceView> _rayListBufferUAV;
		std::shared_ptr<IResourceView> _rayListBufferSRV;
		std::shared_ptr<IResourceView> _tileMetaDataMaskUAV;
		std::shared_ptr<IResourceView> _tileMetaDataMaskSRV;
		std::shared_ptr<IResourceView> _tileTemporalVarianceMaskUAV;
		std::shared_ptr<IResourceView> _tileTemporalVarianceMaskSRV;
		std::shared_ptr<IResourceView> _temporalDenoiseResultUAV[2];
		std::shared_ptr<IResourceView> _temporalDenoiseResultSRV[2];
		std::shared_ptr<IResourceView> _rayLengthsUAV;
		std::shared_ptr<IResourceView> _rayLengthsSRV;

		bool _pendingCompleteInitialization = false;
		
		void CompleteInitialization(Metal::DeviceContext& metalContext)
		{
			Metal::CompleteInitialization(
				metalContext,
				{ _outputTexture.get(), _temporalDenoiseResult[0].get(), _temporalDenoiseResult[1].get(), _rayLengthsTexture.get() });

			metalContext.Clear(*_temporalDenoiseResultUAV[0], Float4{0.f,0.f,0.f,0.f});
			metalContext.Clear(*_temporalDenoiseResultUAV[1], Float4{0.f,0.f,0.f,0.f});
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
			_outputTexture = device.CreateResource(
				CreateDesc(
					BindFlag::TransferDst | BindFlag::UnorderedAccess | BindFlag::ShaderResource,
					0, 0, TextureDesc::Plain2D(fbProps._outputWidth, fbProps._outputHeight, Format::R11G11B10_FLOAT),
					"ssr-output"
				));
			_outputTextureUAV = _outputTexture->CreateTextureView(BindFlag::UnorderedAccess);

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
					BindFlag::UnorderedAccess,
					0, 0, LinearBufferDesc::Create(tileCount*2*sizeof(uint32_t)),
					"ssr-tile-temporal-variance"
				));
			_tileTemporalVarianceMaskUAV = _tileTemporalVarianceMask->CreateBufferView(BindFlag::UnorderedAccess);
			_tileTemporalVarianceMaskSRV = _tileTemporalVarianceMask->CreateBufferView(BindFlag::ShaderResource);

			for (unsigned c=0; c<2; ++c) {
				_temporalDenoiseResult[c] = device.CreateResource(
					CreateDesc(
						BindFlag::UnorderedAccess | BindFlag::ShaderResource | BindFlag::TransferSrc | BindFlag::TransferDst,
						0, 0, TextureDesc::Plain2D(fbProps._outputWidth, fbProps._outputHeight, Format::R11G11B10_FLOAT),
						"ssr-tile-temporal-variance"
					));
				_temporalDenoiseResultUAV[c] = _temporalDenoiseResult[c]->CreateTextureView(BindFlag::UnorderedAccess);
				_temporalDenoiseResultSRV[c] = _temporalDenoiseResult[c]->CreateTextureView(BindFlag::ShaderResource);
			}
		}
		ResolutionDependentResources() {};
	};

	void ScreenSpaceReflectionsOperator::Execute(LightingEngine::LightingTechniqueIterator& iterator)
	{
		if (_res->_pendingCompleteInitialization)
			CompleteInitialization(*iterator._threadContext);

		auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);

		IResourceView* srvs[25];
		srvs[0] = iterator._rpi.GetNonFrameBufferAttachmentView(0).get();			// g_denoised_reflections
		srvs[1] = _res->_temporalDenoiseResultUAV[_pingPongCounter&1].get();		// g_intersection_result
		srvs[2] = _res->_temporalDenoiseResultSRV[_pingPongCounter&1].get();		// g_intersection_result_read
		srvs[3] = _res->_rayListBufferUAV.get();									// g_ray_list
		srvs[4] = _res->_rayListBufferSRV.get();									// g_ray_list_read
		srvs[5] = _rayCounterBufferUAV.get();										// g_ray_counter
		srvs[6] = _res->_rayLengthsUAV.get();										// g_ray_lengths
		srvs[7] = _res->_rayLengthsSRV.get();										// g_ray_lengths_read
		srvs[8] = _res->_tileMetaDataMaskUAV.get();									// g_tile_meta_data_mask
		srvs[9] = _res->_tileMetaDataMaskSRV.get();									// g_tile_meta_data_mask_read
		srvs[10] = _res->_tileTemporalVarianceMaskUAV.get();						// g_temporal_variance_mask
		srvs[11] = _res->_tileTemporalVarianceMaskSRV.get();						// g_temporal_variance_mask_read
		srvs[12] = _res->_temporalDenoiseResultUAV[_pingPongCounter&1].get();		// g_temporally_denoised_reflections
		srvs[13] = _res->_temporalDenoiseResultSRV[_pingPongCounter&1].get();		// g_temporally_denoised_reflections_read
		srvs[14] = _res->_temporalDenoiseResultSRV[(_pingPongCounter+1)&1].get();	// g_temporally_denoised_reflections_history
		srvs[15] = iterator._rpi.GetNonFrameBufferAttachmentView(0).get();			// g_spatially_denoised_reflections
		srvs[16] = iterator._rpi.GetNonFrameBufferAttachmentView(1).get();			// g_spatially_denoised_reflections_read
		srvs[17] = _indirectArgsBufferUAV.get();									// g_intersect_args

		srvs[18] = iterator._rpi.GetNonFrameBufferAttachmentView(3).get();
		srvs[19] = iterator._rpi.GetNonFrameBufferAttachmentView(2).get();
		srvs[20] = iterator._rpi.GetNonFrameBufferAttachmentView(4).get();

		srvs[21] = _blueNoiseRes->_sobolBufferView.get();
		srvs[22] = _blueNoiseRes->_rankingTileBufferView.get();
		srvs[23] = _blueNoiseRes->_scramblingTileBufferView.get();

		// srvs[21] = _skyCubeSRV.get();

		struct ExtendedTransforms
		{
			Float4x4 _clipToView, _clipToWorld, _worldToView;
			Float4x4 _viewToWorld, _viewToProj, _prevWorldToClip;
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
		struct FrameId
		{
			unsigned _frameId; unsigned _dummy[3];
		} frameId { _pingPongCounter, 0, 0, 0 };
		UniformsStream::ImmediateData immData[] { MakeOpaqueIteratorRange(extendedTransforms), MakeOpaqueIteratorRange(frameId) };

		UniformsStream us;
		us._resourceViews = MakeIteratorRange(srvs);
		us._immediateData = MakeIteratorRange(immData);

		Techniques::SequencerUniformsHelper uniformsHelper{*iterator._parsingContext};
		UInt2 outputDims { iterator._rpi.GetFrameBufferDesc().GetProperties()._outputWidth, iterator._rpi.GetFrameBufferDesc().GetProperties()._outputHeight };
		_classifyTiles->Dispatch(
			*iterator._threadContext, *iterator._parsingContext, uniformsHelper,
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
			*iterator._threadContext, *iterator._parsingContext, uniformsHelper,
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

		_intersect->BeginDispatches(*iterator._threadContext, *iterator._parsingContext, uniformsHelper, us);
		_intersect->DispatchIndirect(*_indirectArgsBuffer);
		_intersect->EndDispatches();

		{
			Metal::Internal::CaptureForBind cap0{metalContext, *_res->_temporalDenoiseResult[_pingPongCounter&1], BindFlag::ShaderResource};
			_resolveSpatial->Dispatch(
				*iterator._threadContext, *iterator._parsingContext, uniformsHelper,
				(outputDims[0]+7) / 8, (outputDims[1]+7) / 8, 1,
				us);
		}

		{
			// note: _res._temporalDenoiseResult[_pingPongCounter&1] will transition from BindFlag::ShaderResource -> BindFlag::UnorderedAccess for this (as a result of the end of the previous capture)
			Metal::Internal::CaptureForBind cap0{metalContext, *_res->_temporalDenoiseResult[(_pingPongCounter+1)&1], BindFlag::ShaderResource};
			Metal::Internal::CaptureForBind cap1{metalContext, *_res->_rayLengthsTexture, BindFlag::ShaderResource};
			_resolveTemporal->Dispatch(
				*iterator._threadContext, *iterator._parsingContext, uniformsHelper,
				(outputDims[0]+7) / 8, (outputDims[1]+7) / 8, 1,
				us);
		}

		_reflectionsBlur->Dispatch(
			*iterator._threadContext, *iterator._parsingContext, uniformsHelper,
			(outputDims[0]+7) / 8, (outputDims[1]+7) / 8, 1,
			us);

		// g_lastIntersectionResultSRV = _res._temporalDenoiseResultSRV[_pingPongCounter&1].get();

		++_pingPongCounter;
	}

	LightingEngine::RenderStepFragmentInterface ScreenSpaceReflectionsOperator::CreateFragment(const FrameBufferProperties& fbProps)
	{
		LightingEngine::RenderStepFragmentInterface result{PipelineType::Compute};
		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		auto outputReflections = result.DefineAttachment(Hash64("SSRReflections"));
		spDesc.AppendNonFrameBufferAttachmentView(outputReflections, BindFlag::UnorderedAccess);
		spDesc.AppendNonFrameBufferAttachmentView(outputReflections, BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Hash64("HierarchicalDepths")), BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal), BindFlag::ShaderResource);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion), BindFlag::ShaderResource);
		spDesc.SetName("ssr-operator");
		result.AddSubpass(
			std::move(spDesc),
			[op=shared_from_this()](LightingEngine::LightingTechniqueIterator& iterator) {
				op->Execute(iterator);
			});
		_res = std::make_unique<ResolutionDependentResources>(*_device, fbProps);
		return result;
	}

	void ScreenSpaceReflectionsOperator::PregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext) 
	{
		UInt2 fbSize{stitchingContext._workingProps._outputWidth, stitchingContext._workingProps._outputHeight};
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				Hash64("SSRReflections"),
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::R11G11B10_FLOAT),
					"ssr-reflections"),
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

	ScreenSpaceReflectionsOperator::ScreenSpaceReflectionsOperator(
		std::shared_ptr<Techniques::IComputeShaderOperator> classifyTiles,
		std::shared_ptr<Techniques::IComputeShaderOperator> prepareIndirectArgs,
		std::shared_ptr<Techniques::IComputeShaderOperator> intersect,
		std::shared_ptr<Techniques::IComputeShaderOperator> resolveSpatial,
		std::shared_ptr<Techniques::IComputeShaderOperator> resolveTemporal,
		std::shared_ptr<Techniques::IComputeShaderOperator> reflectionsBlur,
		std::shared_ptr<IDevice> device)
	: _classifyTiles(std::move(classifyTiles))
	, _prepareIndirectArgs(std::move(prepareIndirectArgs))
	, _intersect(std::move(intersect))
	, _resolveSpatial(std::move(resolveSpatial))
	, _resolveTemporal(std::move(resolveTemporal))
	, _reflectionsBlur(std::move(reflectionsBlur))
	, _device(std::move(device))
	{
		_blueNoiseRes = std::make_unique<BlueNoiseGeneratorTables>(*device);

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
		std::shared_ptr<Techniques::PipelinePool> pipelinePool)
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

		usi.BindResourceView(18, Hash64("GBufferNormal"));
		usi.BindResourceView(19, Hash64("DownsampleDepths"));
		usi.BindResourceView(20, Hash64("GBufferMotion"));

		usi.BindResourceView(21, Hash64("BN_Sobol"));
		usi.BindResourceView(22, Hash64("BN_Ranking"));
		usi.BindResourceView(23, Hash64("BN_Scrambling"));

		// usi.BindResourceView(24, Hash64("SkyCube"));

		usi.BindImmediateData(0, Hash64("ExtendedTransforms"));
		usi.BindImmediateData(1, Hash64("FrameIdBuffer"));

		ParameterBox selectors;
		auto classifyTiles = Techniques::CreateComputeOperator(
			pipelinePool,
			SSR_CLASSIFY_TILES_HLSL ":ClassifyTiles",
			selectors,
			SSR_PIPELINE ":ClassifyTiles",
			usi);

		auto prepareIndirectArgs = Techniques::CreateComputeOperator(
			pipelinePool,
			SSR_CLASSIFY_TILES_HLSL ":PrepareIndirectArgs",
			selectors,
			SSR_PIPELINE ":ClassifyTiles",
			usi);

		auto intersect = Techniques::CreateComputeOperator(
			pipelinePool,
			SSR_INTERSECT_HLSL ":SSRIntersect",
			selectors,
			SSR_PIPELINE ":Intersect",
			usi);

		auto resolveSpatial = Techniques::CreateComputeOperator(
			pipelinePool,
			SSR_RESOLVE_SPATIAL_HLSL ":ResolveSpatial",
			selectors,
			SSR_PIPELINE ":ResolveSpatial",
			usi);

		auto resolveTemporal = Techniques::CreateComputeOperator(
			pipelinePool,
			SSR_RESOLVE_TEMPORAL_HLSL ":ResolveTemporal",
			selectors,
			SSR_PIPELINE ":ResolveTemporal",
			usi);

		auto reflectionsBlur = Techniques::CreateComputeOperator(
			pipelinePool,
			SSR_REFLECTIONS_BLUR_HLSL ":ReflectionsBlur",
			selectors,
			SSR_PIPELINE ":ReflectionsBlur",
			usi);

		// auto skyCube = ::Assets::MakeAsset<Techniques::DeferredShaderResource>("rawos/shaderlab/test-process0.texture");
		::Assets::WhenAll(classifyTiles, prepareIndirectArgs, intersect, resolveSpatial, resolveTemporal, reflectionsBlur).ThenConstructToFuture(
			future, 
			[dev=pipelinePool->GetDevice()](auto classifyTiles, auto prepareIndirectArgs, auto intersect, auto resolveSpatial, auto resolveTemporal, auto reflectionsBlur) { 
				return std::make_shared<ScreenSpaceReflectionsOperator>(
					std::move(classifyTiles), std::move(prepareIndirectArgs), std::move(intersect), std::move(resolveSpatial), std::move(resolveTemporal), std::move(reflectionsBlur),
					std::move(dev)); 
			});
	}
	
}}

