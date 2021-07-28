// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightTiler.h"
#include "LightUniforms.h"
#include "LightingEngineInternal.h"
#include "StandardLightScene.h"
#include "../Techniques/TechniqueUtils.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/TechniqueDelegates.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/PipelineOperators.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../IAnnotator.h"
#include "../BufferView.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Math/ProjectionMath.h"
#include "../../Math/Transformations.h"
#include "../../Utility/BitUtils.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace LightingEngine
{
	static float PowerForHalfRadius(float halfRadius, float powerFraction)
	{
		const float attenuationScalar = 1.f;
		return (attenuationScalar*(halfRadius*halfRadius)+1.f) * (1.0f / (1.f-powerFraction));
	}

	static float LinearizedDepthMax(const Internal::CB_Light& light, const Techniques::ProjectionDesc& projDesc)
	{
		auto cameraForward = ExtractForward_Cam(projDesc._cameraToWorld);
		auto projected = projDesc._worldToProjection * Float4{light._position + light._cutoffRange * cameraForward, 1};
		float z = projected[2], w = projected[3];

		auto zRow = Float4{projDesc._worldToProjection(2,0), projDesc._worldToProjection(2,1), projDesc._worldToProjection(2,2), projDesc._worldToProjection(2,3)};
		auto wRow = Float4{projDesc._worldToProjection(3,0), projDesc._worldToProjection(3,1), projDesc._worldToProjection(3,2), projDesc._worldToProjection(3,3)};
		float zRowMag = Magnitude(Truncate(zRow)), wRowMag = Magnitude(Truncate(wRow));

		float z2 = Dot(Float4{light._position, 1}, zRow) + light._cutoffRange * zRowMag;
		float w2 = Dot(Float4{light._position, 1}, wRow) + light._cutoffRange * wRowMag;

		return z2/CalculateNearAndFarPlane(ExtractMinimalProjection(projDesc._cameraToProjection), Techniques::GetDefaultClipSpaceType()).second;
	}

	static float LinearizedDepthMin(const Internal::CB_Light& light, const Techniques::ProjectionDesc& projDesc)
	{
		auto cameraForward = ExtractForward_Cam(projDesc._cameraToWorld);
		auto projected = projDesc._worldToProjection * Float4{light._position - light._cutoffRange * cameraForward, 1};
		float z = projected[2], w = projected[3];

		auto zRow = Float4{projDesc._worldToProjection(2,0), projDesc._worldToProjection(2,1), projDesc._worldToProjection(2,2), projDesc._worldToProjection(2,3)};
		auto wRow = Float4{projDesc._worldToProjection(3,0), projDesc._worldToProjection(3,1), projDesc._worldToProjection(3,2), projDesc._worldToProjection(3,3)};
		float zRowMag = Magnitude(Truncate(zRow)), wRowMag = Magnitude(Truncate(wRow));

		float z2 = Dot(Float4{light._position, 1}, zRow) - light._cutoffRange * zRowMag;
		float w2 = Dot(Float4{light._position, 1}, wRow) - light._cutoffRange * wRowMag;

		// at far clip plane:
		// z * -(f) / (f-n) - (f*n) / (f-n) = -z
		// z * -(f) / (f-n) + z = (f*n) / (f-n)
		// z * (-f / (f-n) + 1) = (f*n) / (f-n)
		// z = ((f*n) / (f-n)) / (-f / (f-n) + 1)
		// z = ((f*n) / (f-n)) / (n / (f-n))
		// z = f

		return z2/CalculateNearAndFarPlane(ExtractMinimalProjection(projDesc._cameraToProjection), Techniques::GetDefaultClipSpaceType()).second;
	}

	void RasterizationLightTileOperator::Execute(RenderCore::LightingEngine::LightingTechniqueIterator& iterator)
	{
		GPUProfilerBlock profileBlock(*iterator._threadContext, "RasterizationLightTileOperator");

		UniformsStreamInterface usi;
		usi.BindResourceView(0, Utility::Hash64("TiledLightBitField"));
		usi.BindResourceView(1, Utility::Hash64("CombinedLightBuffer"));
		usi.BindResourceView(2, Utility::Hash64("DownsampleDepths"));
		usi.BindImmediateData(0, Utility::Hash64("GlobalTransform"));

		auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);

		auto& projDesc = iterator._parsingContext->GetProjectionDesc();
		auto clipSpaceType = Techniques::GetDefaultClipSpaceType();
		AccurateFrustumTester frustumTester(projDesc._worldToProjection, clipSpaceType);

		const unsigned tileLightCount = 512;
		std::shared_ptr<IResourceView> lightBufferResView;
		unsigned depthLookupTable[_depthLookupTableGradiations];
		{
			struct IntermediateLight { Float3 _position; float _cutoffRadius; float _linearizedDepthMin, _linearizedDepthMax; Float3 _brightness; };
			IntermediateLight intermediateLights[tileLightCount];
			IntermediateLight* intLight = intermediateLights;
			auto zRow = Float4{projDesc._worldToProjection(2,0), projDesc._worldToProjection(2,1), projDesc._worldToProjection(2,2), projDesc._worldToProjection(2,3)};
			float zRowMag = Magnitude(Truncate(zRow));
			float farClip = CalculateNearAndFarPlane(ExtractMinimalProjection(projDesc._cameraToProjection), Techniques::GetDefaultClipSpaceType()).second;

			static Float3 baseLightPosition = Float3(0.f, 0.f, 0.f);
			static float startingAngle = 0.f;
			for (unsigned c=0; c<tileLightCount; ++c) {
				const float X = startingAngle + c / float(tileLightCount) * gPI * 2.f;
				const float Y = 3.7397f * startingAngle + .7234f * c / float(tileLightCount) * gPI * 2.f;
				const float Z = 13.8267f * startingAngle + 0.27234f * c / float(tileLightCount) * gPI * 2.f;
				const float cutoffRadius = 20.f + 10.f * XlSin(Z);
				Float3 position = baseLightPosition + Float3(50.f * XlCos(X), 50.f * XlSin(Y), XlCos(Z));
				if (frustumTester.TestSphere(position, cutoffRadius) == AABBIntersection::Culled) continue;

				auto power = PowerForHalfRadius(0.5f*cutoffRadius, 0.05f);
				auto brightness = power * Float3{.65f + .35f * XlSin(Y), .65f + .35f * XlCos(Y), .65f + .35f * XlCos(X)};

				float zMin = Dot(Float4{position, 1}, zRow);
				float zMax = zMin + cutoffRadius * zRowMag;
				zMin -= cutoffRadius * zRowMag;

				*intLight++ = IntermediateLight{position, cutoffRadius, zMin/farClip, zMax/farClip, brightness};
			}
			assert((intLight-intermediateLights) < (1u<<16u));

			// sort by distance to camera of closest point to camera
			std::sort(intermediateLights, intLight, [](const IntermediateLight& lhs, IntermediateLight& rhs) { return lhs._linearizedDepthMin < rhs._linearizedDepthMin; });

			// split up depth space in ndc depth space; 
			// const float ndcDepthMin = clipSpaceType == ClipSpaceType::StraddlingZero ? -1.f : 0.f, ndcDepthMax = 1.0f;
			auto* i = intermediateLights;
			for (unsigned c=0; c<_depthLookupTableGradiations; ++c) {
				float min = LinearInterpolate(0.f, 1.f, c/float(_depthLookupTableGradiations));
				float max = LinearInterpolate(0.f, 1.f, (c+1)/float(_depthLookupTableGradiations));
				while (i != intLight && i->_linearizedDepthMax < min) ++i;
				auto endi = i;
				while (endi != intLight && endi->_linearizedDepthMin < max) ++endi;
				depthLookupTable[c] = (unsigned(endi-intermediateLights) << 16u) | unsigned(i-intermediateLights);
			}

			auto finalLightCount = intLight - intermediateLights;
			if (finalLightCount) {
				auto mappedStorage = metalContext.MapTemporaryStorage(finalLightCount * sizeof(Internal::CB_Light), BindFlag::UnorderedAccess);
				auto lightBufferResource = mappedStorage.GetResource();
				auto beginAndEnd = mappedStorage.GetBeginAndEndInResource();
				auto* dstLight = (Internal::CB_Light*)mappedStorage.GetData().begin();
				auto* beginLights = dstLight;
				i = intermediateLights;
				while (i!=intLight) {
					auto localToWorld = AsFloat4x4(ScaleTranslation{
						Float3{i->_cutoffRadius, i->_cutoffRadius, i->_cutoffRadius}, 
						i->_position});

					StandardLightDesc lightDesc{0};
					lightDesc.SetLocalToWorld(localToWorld);
					lightDesc.SetCutoffRange(i->_cutoffRadius);
					lightDesc.SetBrightness(i->_brightness);
					*dstLight++ = Internal::MakeLightUniforms(lightDesc);
					++i;
				}

				lightBufferResView = lightBufferResource->CreateBufferView(BindFlag::UnorderedAccess, (unsigned)beginAndEnd.first, unsigned(beginAndEnd.second-beginAndEnd.first));
			} else {
				// when there are no lights, just write a dummy empty light
				auto mappedStorage = metalContext.MapTemporaryStorage(sizeof(Internal::CB_Light), BindFlag::UnorderedAccess);
				auto lightBufferResource = mappedStorage.GetResource();
				auto beginAndEnd = mappedStorage.GetBeginAndEndInResource();
				auto* dstLight = (Internal::CB_Light*)mappedStorage.GetData().begin();
				*dstLight = {};
				lightBufferResView = lightBufferResource->CreateBufferView(BindFlag::UnorderedAccess, (unsigned)beginAndEnd.first, unsigned(beginAndEnd.second-beginAndEnd.first));
			}

			/*s_lastLightBufferResView = lightBufferResView;
			s_lastLightDepthLookupTable.resize(_depthLookupTableGradiations*sizeof(unsigned));
			std::memcpy(s_lastLightDepthLookupTable.data(), depthLookupTable, _depthLookupTableGradiations*sizeof(unsigned));*/
		}

		Metal::BoundUniforms boundUniforms(*_prepareBitFieldPipeline, usi);
		auto encoder = metalContext.BeginGraphicsEncoder(_prepareBitFieldLayout);
		ViewportDesc viewport { 0, 0, (float)_lightTileBufferSize[0], (float)_lightTileBufferSize[1] };
		ScissorRect scissorRect { 0, 0, _lightTileBufferSize[0], _lightTileBufferSize[1] };
		encoder.Bind(MakeIteratorRange(&viewport, &viewport+1), MakeIteratorRange(&scissorRect, &scissorRect+1));
		// const IDescriptorSet* descSets[] { sequencerDescSet.first.get() };
		// boundUniforms.ApplyDescriptorSets(metalContext, encoder, MakeIteratorRange(descSets));

		UniformsStream us;
		const IResourceView* resView[] { iterator._rpi.GetNonFrameBufferAttachmentView(0).get(), lightBufferResView.get(), iterator._rpi.GetNonFrameBufferAttachmentView(1).get() };
		us._resourceViews = MakeIteratorRange(resView);

		auto globalUniforms = Techniques::BuildGlobalTransformConstants(iterator._parsingContext->GetProjectionDesc());
		UniformsStream::ImmediateData immData[] { MakeOpaqueIteratorRange(globalUniforms) };
		us._immediateData = MakeIteratorRange(immData);

		boundUniforms.ApplyLooseUniforms(metalContext, encoder, us);

		VertexBufferView vbvs[] = {
			VertexBufferView { _stencilingGeo._lowDetailHemiSphereVB.get() }
		};
		encoder.Bind(MakeIteratorRange(vbvs), IndexBufferView{ _stencilingGeo._lowDetailHemiSphereIB.get(), Format::R16_UINT });
		encoder.DrawIndexedInstances(*_prepareBitFieldPipeline, _stencilingGeo._lowDetailHemiSphereIndexCount, tileLightCount);
	}

	RenderCore::LightingEngine::RenderStepFragmentInterface RasterizationLightTileOperator::CreateFragment(const FrameBufferProperties& fbProps)
	{
		LightingEngine::RenderStepFragmentInterface result{PipelineType::Graphics};

		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::TiledLightBitField), BindFlag::UnorderedAccess);
		TextureViewDesc depthBufferView;
		depthBufferView._mipRange._min = IntegerLog2(s_gridDims);
		depthBufferView._mipRange._count = 1;
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::HierarchicalDepths), BindFlag::ShaderResource, depthBufferView);
		spDesc.SetName("rasterization-light-tiler");
		result.AddSubpass(
			std::move(spDesc),
			[op=shared_from_this()](LightingEngine::LightingTechniqueIterator& iterator) {
				op->Execute(iterator);
			});

		return result;
	}

	RenderCore::LightingEngine::RenderStepFragmentInterface RasterizationLightTileOperator::CreateInitFragment(const FrameBufferProperties& fbProps)
	{
		LightingEngine::RenderStepFragmentInterface result{PipelineType::Compute};

		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		auto tiledLightBitField = result.DefineAttachment(Techniques::AttachmentSemantics::TiledLightBitField, LoadStore::DontCare, LoadStore::Retain, 0, BindFlag::UnorderedAccess);
		spDesc.AppendNonFrameBufferAttachmentView(tiledLightBitField, BindFlag::UnorderedAccess);
		/*spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::HierarchicalDepths), BindFlag::UnorderedAccess);
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Hash64("LowRezDepthBuffer")), BindFlag::DepthStencil);*/
		spDesc.SetName("rasterization-light-tiler-init");
		result.AddSubpass(
			std::move(spDesc),
			[op=shared_from_this(), lightTileBufferSize=_lightTileBufferSize](LightingEngine::LightingTechniqueIterator& iterator) {
				auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);
				/*
					Can't copy from our R32_FLOAT downsampled depth buffer onto a D32_FLOAT texture to be used for depth/stencil tests
				auto bltEncoder = metalContext.BeginBlitEncoder();
				Metal::BlitEncoder::CopyPartial_Dest copyDst;
				copyDst._resource = iterator._rpi.GetNonFrameBufferAttachmentView(2)->GetResource().get();
				copyDst._subResource = {};
				copyDst._leftTopFront = {0,0,0};
				Metal::BlitEncoder::CopyPartial_Src copySrc;
				copySrc._resource = iterator._rpi.GetNonFrameBufferAttachmentView(1)->GetResource().get();
				copySrc._subResource = {5, 0};
				copySrc._leftTopFront = {0,0,0};
				copySrc._rightBottomBack = {lightTileBufferSize[0],lightTileBufferSize[1],1};
				bltEncoder.Copy(copyDst, copySrc);
				*/
				metalContext.ClearUInt(
					*iterator._rpi.GetNonFrameBufferAttachmentView(0),
					UInt4{0,0,0,0});
			});

		return result;
	}

	void RasterizationLightTileOperator::PreregisterAttachments(RenderCore::Techniques::FragmentStitchingContext& stitchingContext) 
	{
		UInt2 fbSize{stitchingContext._workingProps._outputWidth, stitchingContext._workingProps._outputHeight};
		const unsigned maxLightsPerFrame = 1024;
		unsigned planesRequired = maxLightsPerFrame/32;
		_lightTileBufferSize = UInt2{(fbSize[0]+s_gridDims-1)/s_gridDims, (fbSize[1]+s_gridDims-1)/s_gridDims};
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::TiledLightBitField,
				CreateDesc(
					BindFlag::UnorderedAccess|BindFlag::ShaderResource|BindFlag::TransferDst, 0, 0, 
					TextureDesc::Plain3D(_lightTileBufferSize[0], _lightTileBufferSize[1], planesRequired, Format::R32_UINT),
					"tiled-light-bit-field")
			}/*,
			Techniques::PreregisteredAttachment {
				Hash64("LowRezDepthBuffer"),
				CreateDesc(
					BindFlag::DepthStencil|BindFlag::TransferDst, 0, 0, 
					TextureDesc::Plain2D(_lightTileBufferSize[0], _lightTileBufferSize[1], Format::D32_FLOAT),
					"tiled-light-depth-buffer")
			}*/
		};
		for (const auto& a:attachments)
			stitchingContext.DefineAttachment(a);
	}


	RasterizationLightTileOperator::RasterizationLightTileOperator(
		std::shared_ptr<RenderCore::Techniques::PipelinePool> pipelinePool,
		std::shared_ptr<Metal::GraphicsPipeline> prepareBitFieldPipeline,
		std::shared_ptr<ICompiledPipelineLayout> prepareBitFieldLayout)
	: _pipelinePool(std::move(pipelinePool))
	, _prepareBitFieldPipeline(std::move(prepareBitFieldPipeline))
	, _prepareBitFieldLayout(std::move(prepareBitFieldLayout))
	, _stencilingGeo(*_pipelinePool->GetDevice())
	{
		_depVal = ::Assets::GetDepValSys().Make();
		_depVal.RegisterDependency(_prepareBitFieldPipeline->GetDependencyValidation());

		auto metricsBufferDesc = CreateDesc(
			BindFlag::UnorderedAccess|BindFlag::ShaderResource, 0, GPUAccess::Read|GPUAccess::Write,
			LinearBufferDesc::Create(sizeof(unsigned)*16),
			"metrics");
		auto buffer = _pipelinePool->GetDevice()->CreateResource(metricsBufferDesc);
		_metricsBufferUAV = buffer->CreateBufferView(BindFlag::UnorderedAccess);
		_metricsBufferSRV = buffer->CreateBufferView(BindFlag::ShaderResource);

		_depthLookupTableGradiations = 1024;
	}

	RasterizationLightTileOperator::~RasterizationLightTileOperator() {}

	void RasterizationLightTileOperator::ConstructToFuture(
		::Assets::FuturePtr<RasterizationLightTileOperator>& future,
		std::shared_ptr<RenderCore::Techniques::PipelinePool> pipelinePool)
	{
		Techniques::GraphicsPipelineDesc pipelineDesc;
		pipelineDesc._shaders[(unsigned)ShaderStage::Vertex] = DEFERRED_RESOLVE_LIGHT_VERTEX_HLSL ":PrepareMany";
		pipelineDesc._shaders[(unsigned)ShaderStage::Geometry] = BASIC_GEO_HLSL ":ClipToNear";
		pipelineDesc._shaders[(unsigned)ShaderStage::Pixel] = TILED_LIGHTING_PREPARE_HLSL ":main";
		// pipelineDesc._manualSelectorFiltering._setValues.SetParameter("LIGHT_SHAPE", 1);
		pipelineDesc._manualSelectorFiltering._setValues.SetParameter("GS_OBJECT_INDEX", 1);
		// pipelineDesc._depthStencil._depthBoundsTestEnable = true;
		pipelineDesc._rasterization = Techniques::CommonResourceBox::s_rsDefault;
		pipelineDesc._rasterization._flags |= RasterizationDescFlags::ConservativeRaster;
		pipelineDesc._depthStencil = Techniques::CommonResourceBox::s_dsDisable;

		auto& pipelineLayout = *::Assets::Actualize<Techniques::CompiledPipelineLayoutAsset>(
			pipelinePool->GetDevice(),
			TILED_LIGHTING_PREPARE_PIPELINE ":GraphicsMain");

		Techniques::VertexInputStates inputStates;
		MiniInputElementDesc inputElements[] = { {Techniques::CommonSemantics::POSITION, Format::R32G32B32_FLOAT} };
		Techniques::VertexInputStates vInput;
		inputStates._inputLayout = MakeIteratorRange(inputElements);
		vInput._topology = Topology::TriangleList;
		FrameBufferDesc fbDesc{{}, std::vector<SubpassDesc>{SubpassDesc{}}};
		Techniques::FrameBufferTarget fbTarget{&fbDesc, 0};
		auto futurePipeline = pipelinePool->CreateGraphicsPipeline(
			pipelineLayout.GetPipelineLayout(),
			pipelineDesc,
			{},
			inputStates, fbTarget);

		::Assets::WhenAll(futurePipeline).ThenConstructToFuture(
			future,
			[pipelinePool, pipelineLayout=pipelineLayout.GetPipelineLayout()](auto pipeline) {
				return std::make_shared<RasterizationLightTileOperator>(std::move(pipelinePool), std::move(pipeline), std::move(pipelineLayout));
			});
	}

	void RasterizationLightTileOperator::Visualize(
		RenderCore::IThreadContext& threadContext, 
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::Techniques::SequencerUniformsHelper& uniformHelper,
		const std::shared_ptr<RenderCore::Techniques::PipelinePool>& pipelinePool)
	{
#if 0
		if (!s_lastLightBufferResView) return;

		GPUProfilerBlock profileBlock(threadContext, "VisualizeTiledLighting");

		using namespace RenderCore;

		Techniques::FrameBufferDescFragment fragment;

		UInt2 fbSize{parsingContext.GetFragmentStitchingContext()._workingProps._outputWidth, parsingContext.GetFragmentStitchingContext()._workingProps._outputHeight};
		auto lightTileBufferSize = UInt2{(fbSize[0]+s_gridDims-1)/s_gridDims, (fbSize[1]+s_gridDims-1)/s_gridDims};

		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		spDesc.AppendOutput(fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR, LoadStore::Clear));
		spDesc.AppendNonFrameBufferAttachmentView(fragment.DefineAttachment(Techniques::AttachmentSemantics::TiledLightBitField), BindFlag::UnorderedAccess);
		spDesc.AppendNonFrameBufferAttachmentView(fragment.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth), BindFlag::ShaderResource);
		spDesc.SetName("visualize-tiled-lighting");
		fragment.AddSubpass(std::move(spDesc));

		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		std::shared_ptr<IResourceView> depthLookupTableView;
		{
			auto mappedStorage = metalContext.MapTemporaryStorage(s_lastLightDepthLookupTable.size(), BindFlag::UnorderedAccess);
			auto beginAndEnd = mappedStorage.GetBeginAndEndInResource();
			depthLookupTableView = mappedStorage.GetResource()->CreateBufferView(BindFlag::UnorderedAccess, beginAndEnd.first, beginAndEnd.second-beginAndEnd.first);
			std::memcpy(mappedStorage.GetData().begin(), s_lastLightDepthLookupTable.data(), s_lastLightDepthLookupTable.size());
		}

		Techniques::RenderPassInstance rpi{threadContext, parsingContext, fragment};

		UniformsStreamInterface usi;
		usi.BindResourceView(0, Hash64("TiledLightBitField"));
		usi.BindResourceView(1, Hash64("CombinedLightBuffer"));
		usi.BindResourceView(2, Hash64("DepthTexture"));
		usi.BindResourceView(3, Hash64("LightDepthTable"));
		UniformsStream us;
		IResourceView* srvs[] = { rpi.GetNonFrameBufferAttachmentView(0).get(), s_lastLightBufferResView.get(), rpi.GetNonFrameBufferAttachmentView(1).get(), depthLookupTableView.get() };
		us._resourceViews = MakeIteratorRange(srvs);

		auto op = Techniques::CreateFullViewportOperator(
			pipelinePool,
			TILED_LIGHTING_PREPARE_HLSL ":visualize", {}, 
			TILED_LIGHTING_PREPARE_PIPELINE ":GraphicsMain", rpi,
			usi);
		op->Actualize()->Draw(threadContext, parsingContext, uniformHelper, us);
#endif
	}

}}
