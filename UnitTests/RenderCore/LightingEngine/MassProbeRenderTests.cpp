// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineTestHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/CommonBindings.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/Drawables.h"
#include "../../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../../RenderCore/Techniques/SimpleModelRenderer.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/QueryPool.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../../Math/Transformations.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/AssetTraits.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../Utility/ArithmeticUtils.h"
#include "../../../xleres/FileList.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <random>

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair("simple.hlsl", ::Assets::AsBlob(R"--(
			#include "xleres/TechniqueLibrary/Framework/VSIN.hlsl"
			#include "xleres/TechniqueLibrary/Framework/VSOUT.hlsl"
			#include "xleres/TechniqueLibrary/Framework/WorkingVertex.hlsl"
			#include "xleres/TechniqueLibrary/Core/BuildVSOUT.vertex.hlsl"

			VSOUT vs_main(VSIN input)
			{
				WorkingVertex deformedVertex = WorkingVertex_DefaultInitialize(input);
				return BuildVSOUT(deformedVertex, input);
			}

			float4 ps_main(VSOUT geo) : SV_Target0
			{
				return float4(1,1,1,1);
			}
		)--")),

		std::make_pair("amplifying_geo_shader.hlsl", ::Assets::AsBlob(R"--(
			#include "xleres/TechniqueLibrary/Framework/VSIN.hlsl"
			#include "xleres/TechniqueLibrary/Framework/VSOUT.hlsl"
			#include "xleres/TechniqueLibrary/Framework/WorkingVertex.hlsl"
			#include "xleres/TechniqueLibrary/Core/BuildVSOUT.vertex.hlsl"

			VSOUT vs_main(VSIN input)
			{
				WorkingVertex deformedVertex = WorkingVertex_DefaultInitialize(input);
				VSOUT result = BuildVSOUT(deformedVertex, input);
				result.renderTargetIndex = 0;		// embued properly in the geometry shader
				return result;
			}

			cbuffer MultiViewProperties BIND_SEQ_B1
			{
				uint MultiProbeCount; uint4 Dummy[3];
				row_major float4x4 MultiProbeViews[64];
			}

			[maxvertexcount(3*32)]
				void gs_main(	triangle VSOUT input[3],
								inout TriangleStream<VSOUT> outputStream)
			{
				// amplify out to up to 32 views
				// coords from VS are actually in world coords, not projection space
				for (uint c=0; c<MultiProbeCount; ++c) {
					[unroll] for (uint q=0; q<3; ++q) {
						VSOUT v = input[q];
						v.position = mul(MultiProbeViews[c], v.position);
						v.renderTargetIndex = c;
						outputStream.Append(v);
					}
					outputStream.RestartStrip();
				}
			}

			float4 ps_main(VSOUT geo) : SV_Target0
			{
				return float4(1,1,1,1);
			}
		)--")),

		std::make_pair("instancing_multiprobe_shader.hlsl", ::Assets::AsBlob(R"--(
			#define VERTEX_ID_VIEW_INSTANCING 1
			#undef GEO_HAS_TEXCOORD
			#undef GEO_HAS_NORMAL
			#undef GEO_HAS_TEXTANGENT
			#undef GEO_HAS_TEXBITANGENT
			#undef VSOUT_HAS_TEXCOORD
			#undef VSOUT_HAS_NORMAL
			#undef VSOUT_HAS_TEXTANGENT
			#undef VSOUT_HAS_TEXBITANGENT
			#include "xleres/TechniqueLibrary/Framework/VSIN.hlsl"
			#include "xleres/TechniqueLibrary/Framework/VSOUT.hlsl"
			#include "xleres/TechniqueLibrary/Framework/WorkingVertex.hlsl"
			#include "xleres/TechniqueLibrary/Core/BuildVSOUT.vertex.hlsl"

			cbuffer MultiViewProperties BIND_SEQ_B1
			{
				uint MultiProbeCount; uint4 Dummy[3];
				row_major float4x4 MultiProbeViews[64];
			}

			VSOUT vs_main(VSIN input, uint instanceId : SV_InstanceID)
			{
				WorkingVertex deformedVertex = WorkingVertex_DefaultInitialize(input);

				float3 worldPosition;
				TangentFrame worldSpaceTangentFrame;

				if (deformedVertex.coordinateSpace == 0) {
					worldPosition = mul(SysUniform_GetLocalToWorld(), float4(deformedVertex.position,1)).xyz;
					worldSpaceTangentFrame = AsTangentFrame(TransformLocalToWorld(deformedVertex.tangentFrame));
				} else {
					worldPosition = deformedVertex.position;
					worldSpaceTangentFrame = AsTangentFrame(deformedVertex.tangentFrame);
				}

				VSOUT output;

				uint viewIndex;
				/*
				if ((instanceId/4)%4 == 0) 			viewIndex = LocalTransform.ViewIndices[instanceId/16].x;
				else if ((instanceId/4)%4 == 1) 	viewIndex = LocalTransform.ViewIndices[instanceId/16].y;
				else if ((instanceId/4)%4 == 2) 	viewIndex = LocalTransform.ViewIndices[instanceId/16].z;
				else 								viewIndex = LocalTransform.ViewIndices[instanceId/16].w;
				viewIndex >>= (instanceId%4) * 8;
				viewIndex &= 0xff;*/

				// Find the position of the instanceId'th bit set
				uint mask = LocalTransform.ViewMask;
				while (instanceId) {
					mask ^= 1 << firstbithigh(mask);
					--instanceId;
				}
				viewIndex = firstbithigh(mask);

				output.position = mul(MultiProbeViews[viewIndex], float4(worldPosition,1));

				#if VSOUT_HAS_TEXCOORD
					output.texCoord = VSIN_GetTexCoord0(input);
				#endif

				#if VSOUT_HAS_NORMAL
					output.normal = mul(GetLocalToWorldUniformScale(), DeriveLocalNormal(input));
				#endif

				#if VSOUT_HAS_WORLD_POSITION
					output.worldPosition = worldPosition;
				#endif

				output.renderTargetIndex = viewIndex;
				return output;
			}

			float4 ps_main(VSOUT geo) : SV_Target0
			{
				// output normal & tex coord to ensure they get passed down as attributes, but still keep a minimal shader
				return float4(VSOUT_GetWorldVertexNormal(geo).xyz + VSOUT_GetTexCoord0(geo).xyx, 1);
			}
		)--")),

		std::make_pair("multiview_shader.hlsl", ::Assets::AsBlob(R"--(
			#undef GEO_HAS_TEXCOORD
			#undef GEO_HAS_NORMAL
			#undef GEO_HAS_TEXTANGENT
			#undef GEO_HAS_TEXBITANGENT
			#undef VSOUT_HAS_TEXCOORD
			#undef VSOUT_HAS_NORMAL
			#undef VSOUT_HAS_TEXTANGENT
			#undef VSOUT_HAS_TEXBITANGENT
			#include "xleres/TechniqueLibrary/Framework/VSIN.hlsl"
			#include "xleres/TechniqueLibrary/Framework/VSOUT.hlsl"
			#include "xleres/TechniqueLibrary/Framework/WorkingVertex.hlsl"
			#include "xleres/TechniqueLibrary/Core/BuildVSOUT.vertex.hlsl"

			cbuffer MultiViewProperties BIND_SEQ_B1
			{
				uint MultiProbeCount; uint4 Dummy[3];
				row_major float4x4 MultiProbeViews[32];
			}

			VSOUT vs_main(VSIN input, in uint viewId : SV_ViewID)
			{
				WorkingVertex deformedVertex = WorkingVertex_DefaultInitialize(input);

				float3 worldPosition;
				TangentFrame worldSpaceTangentFrame;

				if (deformedVertex.coordinateSpace == 0) {
					worldPosition = mul(SysUniform_GetLocalToWorld(), float4(deformedVertex.position,1)).xyz;
					worldSpaceTangentFrame = TransformLocalToWorld(deformedVertex.tangentFrame, DefaultTangentVectorToReconstruct());
				} else {
					worldPosition = deformedVertex.position;
					worldSpaceTangentFrame = deformedVertex.tangentFrame;
				}

				VSOUT output;
				output.position = mul(MultiProbeViews[viewId], float4(worldPosition,1));

				#if VSOUT_HAS_TEXCOORD
					output.texCoord = VSIN_GetTexCoord0(input);
				#endif

				#if VSOUT_HAS_NORMAL
					output.normal = worldSpaceTangentFrame.normal;
				#endif

				#if VSOUT_HAS_WORLD_POSITION
					output.worldPosition = worldPosition;
				#endif
				return output;
			}

			float4 ps_main(VSOUT geo) : SV_Target0
			{
				// output normal & tex coord to ensure they get passed down as attributes, but still keep a minimal shader
				return float4(VSOUT_GetWorldVertexNormal(geo).xyz + VSOUT_GetTexCoord0(geo).xyx, 1);
			}
		)--"))
	};

	static const UInt2 s_testResolution { 32, 32 };
	const unsigned s_probesToRender = 64;

	const uint64_t s_attachmentProbeTarget = 100;
	const uint64_t s_attachmentProbeDepth = 101;

	static RenderCore::Techniques::ParsingContext InitializeParsingContext(RenderCore::Techniques::TechniqueContext& techniqueContext, RenderCore::IThreadContext& threadContext)
	{
		using namespace RenderCore;

		Techniques::PreregisteredAttachment preregisteredAttachments[] {
			Techniques::PreregisteredAttachment {
				s_attachmentProbeTarget,
				CreateDesc(
					BindFlag::TransferSrc | BindFlag::RenderTarget, 0, 0, 
					TextureDesc::Plain2D(s_testResolution[0], s_testResolution[1], Format::B8G8R8A8_UNORM_SRGB, 1, 64),
					"probe-target"
				),
				Techniques::PreregisteredAttachment::State::Uninitialized
			},
			Techniques::PreregisteredAttachment {
				s_attachmentProbeDepth,
				CreateDesc(
					BindFlag::TransferSrc | BindFlag::DepthStencil, 0, 0, 
					TextureDesc::Plain2D(s_testResolution[0], s_testResolution[1], Format::D16_UNORM, 1, 64),
					"probe-depth"
				),
				Techniques::PreregisteredAttachment::State::Uninitialized
			}
		};
		FrameBufferProperties fbProps { s_testResolution[0], s_testResolution[1] };

		Techniques::ParsingContext parsingContext{techniqueContext, threadContext};

		auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
		stitchingContext._workingProps = fbProps;
		for (const auto&a:preregisteredAttachments)
			stitchingContext.DefineAttachment(a._semantic, a._desc, a._state, a._layoutFlags);
		return parsingContext;
	}

	static std::shared_ptr<RenderCore::Techniques::SequencerConfig> CreateSequencerConfig(
		const std::string& name,
		RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> techniqueDelegate,
		bool multiView = false)
	{
		using namespace RenderCore;
		std::vector<AttachmentDesc> attachments {
			AttachmentDesc{ Format::B8G8R8A8_UNORM_SRGB, 0, LoadStore::Clear, LoadStore::Retain, 0, BindFlag::ShaderResource },
			AttachmentDesc{ Format::D16_UNORM, 0, LoadStore::Clear, LoadStore::DontCare }
		};
		SubpassDesc sp;
		sp.AppendOutput(0); sp.SetDepthStencil(1);
		sp.SetName("prepare-probe");
		if (multiView) sp.SetViewInstanceMask(~0u);
		FrameBufferDesc representativeFB(std::move(attachments), std::vector<SubpassDesc>{sp});
		return pipelineAccelerators.CreateSequencerConfig(name, techniqueDelegate, ParameterBox{}, representativeFB, 0);
	}

	// Simpliest method -- we just create a massive render target with separate subpasses for each
	// array layer and just draw each item normally
	class SimpleRendering
	{
	public:
		std::shared_ptr<RenderCore::Techniques::SequencerConfig> _cfg;
		
		class TechniqueDelegate : public RenderCore::Techniques::ITechniqueDelegate
		{
		public:
			virtual ::Assets::PtrToMarkerPtr<GraphicsPipelineDesc> GetPipelineDesc(
				const RenderCore::Techniques::CompiledShaderPatchCollection::Interface& shaderPatches,
				const RenderCore::Assets::RenderStateSet& renderStates)
			{
				using namespace RenderCore;
				auto result = std::make_shared<::Assets::MarkerPtr<GraphicsPipelineDesc>>("from-probe-prepare-delegate");
				auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
				nascentDesc->_depthStencil = Techniques::CommonResourceBox::s_dsReadWriteCloserThan;
				nascentDesc->_blend.push_back(Techniques::CommonResourceBox::s_abOpaque);
				nascentDesc->_shaders[(unsigned)ShaderStage::Vertex] = "ut-data/simple.hlsl:vs_main";
				nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = "ut-data/simple.hlsl:ps_main";
				nascentDesc->_selectorPreconfigurationFile = "xleres/TechniqueLibrary/Framework/SelectorPreconfiguration.hlsl";
				result->SetAsset(std::move(nascentDesc));
				return result;
			}

			virtual std::string GetPipelineLayout()
			{
				return MAIN_PIPELINE ":GraphicsProbePrepare";
			}
		};

		void Execute(
			RenderCore::IThreadContext& threadContext, RenderCore::Techniques::ParsingContext& parsingContext,
			const LightingEngineTestApparatus& testApparatus,
			IteratorRange<const RenderCore::Techniques::CameraDesc*> cameras,
			ToolsRig::IDrawablesWriter& drawablesWriter)
		{
			using namespace RenderCore;
			auto* extWriter = dynamic_cast<ToolsRig::IExtendedDrawablesWriter*>(&drawablesWriter);
			assert(extWriter);
			Techniques::RenderPassInstance rpi{parsingContext, _fragment};
			for (unsigned c=0; ;) {
				auto& projDesc = parsingContext.GetProjectionDesc();
				projDesc = BuildProjectionDesc(cameras[c], s_testResolution);

				RenderCore::Techniques::DrawablesPacket pkt;
				extWriter->WriteDrawables(pkt, projDesc._worldToProjection);

				Techniques::Draw(parsingContext, *testApparatus._pipelineAcceleratorPool, *_cfg, pkt);

				++c;
				if (c == cameras.size()) break;
				rpi.NextSubpass();
			}
		}

		SimpleRendering(const LightingEngineTestApparatus& testApparatus)
		{
			using namespace RenderCore;
			_fragment.DefineAttachment(s_attachmentProbeTarget).Clear();
			_fragment.DefineAttachment(s_attachmentProbeDepth).Clear();
			for (unsigned c=0; c<s_probesToRender; ++c) {
				TextureViewDesc viewDesc;
				viewDesc._arrayLayerRange._min = c;
				viewDesc._arrayLayerRange._count = 1;
				SubpassDesc sp;
				sp.AppendOutput(0, viewDesc);
				sp.SetDepthStencil(1, viewDesc);
				_fragment.AddSubpass(std::move(sp));
			}

			_cfg = CreateSequencerConfig("mass-probe-simple", *testApparatus._pipelineAcceleratorPool, std::make_shared<TechniqueDelegate>());
		}

	private:
		RenderCore::Techniques::FrameBufferDescFragment _fragment;
	};

	static std::vector<RenderCore::Techniques::FrameBufferDescFragment> MakeFragments(unsigned totalViews, unsigned maxViewsPerDraw, bool multiView = false)
	{
		using namespace RenderCore;
		std::vector<RenderCore::Techniques::FrameBufferDescFragment> result;
		std::pair<unsigned, unsigned> range{0,totalViews};
		while (range.second != range.first) {
			auto batchRange = range;
			batchRange.second = std::min(batchRange.second, batchRange.first+maxViewsPerDraw);

			Techniques::FrameBufferDescFragment fragment;
			fragment.DefineAttachment(s_attachmentProbeTarget).Clear();
			fragment.DefineAttachment(s_attachmentProbeDepth).Clear();
			TextureViewDesc viewDesc;
			viewDesc._arrayLayerRange._min = batchRange.first;
			viewDesc._arrayLayerRange._count = batchRange.second-batchRange.first;
			SubpassDesc sp;
			sp.AppendOutput(0, viewDesc);
			sp.SetDepthStencil(1, viewDesc);
			if (multiView) sp.SetViewInstanceMask(~0u);
			fragment.AddSubpass(std::move(sp));
			result.push_back(std::move(fragment));

			range.first = batchRange.second;
		}
		return result;
	}

	// Amplifying geo shader -- one draw as input, with a geo shader that creates primitives for all of the different views
	class AmplifyingGeoShader
	{
	public:
		std::shared_ptr<RenderCore::Techniques::SequencerConfig> _cfg;
		
		class TechniqueDelegate : public RenderCore::Techniques::ITechniqueDelegate
		{
		public:
			virtual ::Assets::PtrToMarkerPtr<GraphicsPipelineDesc> GetPipelineDesc(
				const RenderCore::Techniques::CompiledShaderPatchCollection::Interface& shaderPatches,
				const RenderCore::Assets::RenderStateSet& renderStates)
			{
				using namespace RenderCore;
				auto result = std::make_shared<::Assets::MarkerPtr<GraphicsPipelineDesc>>("from-probe-prepare-delegate");
				auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
				nascentDesc->_depthStencil = Techniques::CommonResourceBox::s_dsReadWriteCloserThan;
				nascentDesc->_blend.push_back(Techniques::CommonResourceBox::s_abOpaque);
				nascentDesc->_shaders[(unsigned)ShaderStage::Vertex] = "ut-data/amplifying_geo_shader.hlsl:vs_main";
				nascentDesc->_shaders[(unsigned)ShaderStage::Geometry] = "ut-data/amplifying_geo_shader.hlsl:gs_main";
				nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = "ut-data/amplifying_geo_shader.hlsl:ps_main";
				nascentDesc->_selectorPreconfigurationFile = "xleres/TechniqueLibrary/Framework/SelectorPreconfiguration.hlsl";
				nascentDesc->_manualSelectorFiltering._setValues.SetParameter("VSOUT_HAS_RENDER_TARGET_INDEX", 1);
				result->SetAsset(std::move(nascentDesc));
				return result;
			}

			virtual std::string GetPipelineLayout()
			{
				return MAIN_PIPELINE ":GraphicsProbePrepare";
			}
		};

		class ShaderResourceDelegate : public RenderCore::Techniques::IShaderResourceDelegate
		{
		public:
			struct MultiViewProperties
			{
				unsigned _probeCount; unsigned _dummy[15];
				Float4x4 _worldToProjection[64];
			};
			MultiViewProperties _multProbeProperties;

			virtual void WriteImmediateData(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
			{
				REQUIRE(idx == 0);
				REQUIRE(dst.size() == sizeof(MultiViewProperties));
				std::memcpy(dst.begin(), &_multProbeProperties, sizeof(_multProbeProperties));
			}

			virtual size_t GetImmediateDataSize(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override
			{
				REQUIRE(idx == 0);
				return sizeof(MultiViewProperties);
			}

			ShaderResourceDelegate(IteratorRange<const RenderCore::Techniques::CameraDesc*> cameras, UInt2 viewportDims)
			{
				_multProbeProperties._probeCount = cameras.size();
				REQUIRE(_multProbeProperties._probeCount <= dimof(MultiViewProperties::_worldToProjection));
				for (unsigned c=0; c<_multProbeProperties._probeCount; ++c) {
					auto projDesc = RenderCore::Techniques::BuildProjectionDesc(cameras[c], viewportDims);
					_multProbeProperties._worldToProjection[c] = projDesc._worldToProjection;
				}
				BindImmediateData(0, Hash64("MultiViewProperties"));
			}
		};

		static constexpr unsigned maxPerBatch = 32;

		void Execute(
			RenderCore::IThreadContext& threadContext, RenderCore::Techniques::ParsingContext& parsingContext,
			const LightingEngineTestApparatus& testApparatus,
			IteratorRange<const RenderCore::Techniques::CameraDesc*> cameras,
			ToolsRig::IDrawablesWriter& drawablesWriter)
		{
			using namespace RenderCore;
			
			RenderCore::Techniques::DrawablesPacket pkt;
			drawablesWriter.WriteDrawables(pkt);

			auto frag = _fragments.begin();
			while (!cameras.empty()) {
				auto batchCameras = cameras;
				batchCameras.second = std::min(batchCameras.second, batchCameras.first+maxPerBatch);

				auto uniformDel = std::make_shared<ShaderResourceDelegate>(batchCameras, s_testResolution);
				parsingContext.GetUniformDelegateManager()->AddShaderResourceDelegate(uniformDel);

				auto& projDesc = parsingContext.GetProjectionDesc();
				projDesc = Techniques::ProjectionDesc{};		// identity world-to-projection

				{
					Techniques::RenderPassInstance rpi{parsingContext, *frag};
					Techniques::Draw(parsingContext, *testApparatus._pipelineAcceleratorPool, *_cfg, pkt);
				}

				parsingContext.GetUniformDelegateManager()->RemoveShaderResourceDelegate(*uniformDel);
				cameras.first = batchCameras.second;
				++frag;
			}
		}

		AmplifyingGeoShader(const LightingEngineTestApparatus& testApparatus)
		{
			using namespace RenderCore;
			_fragments = MakeFragments(s_probesToRender, maxPerBatch);
			_cfg = CreateSequencerConfig("mass-probe-amplifying-gs", *testApparatus._pipelineAcceleratorPool, std::make_shared<TechniqueDelegate>());
		}

	private:
		std::vector<RenderCore::Techniques::FrameBufferDescFragment> _fragments;
	};

	// Instancing multi probe shader -- one instancing draw call per input, and one instance per probe draw. The vertex shader
	// uses the instance id to select the probe view to use
	class VertexInstancingShader
	{
	public:
		std::shared_ptr<RenderCore::Techniques::SequencerConfig> _cfg;
		
		class TechniqueDelegate : public RenderCore::Techniques::ITechniqueDelegate
		{
		public:
			virtual ::Assets::PtrToMarkerPtr<GraphicsPipelineDesc> GetPipelineDesc(
				const RenderCore::Techniques::CompiledShaderPatchCollection::Interface& shaderPatches,
				const RenderCore::Assets::RenderStateSet& renderStates)
			{
				using namespace RenderCore;
				auto result = std::make_shared<::Assets::MarkerPtr<GraphicsPipelineDesc>>("from-probe-prepare-delegate");
				auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
				nascentDesc->_depthStencil = Techniques::CommonResourceBox::s_dsReadWriteCloserThan;
				nascentDesc->_blend.push_back(Techniques::CommonResourceBox::s_abOpaque);
				nascentDesc->_shaders[(unsigned)ShaderStage::Vertex] = "ut-data/instancing_multiprobe_shader.hlsl:vs_main";
				nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = "ut-data/instancing_multiprobe_shader.hlsl:ps_main";
				nascentDesc->_selectorPreconfigurationFile = "xleres/TechniqueLibrary/Framework/SelectorPreconfiguration.hlsl";
				nascentDesc->_manualSelectorFiltering._setValues.SetParameter("VSOUT_HAS_RENDER_TARGET_INDEX", 1);
				result->SetAsset(std::move(nascentDesc));
				return result;
			}

			virtual std::string GetPipelineLayout()
			{
				return MAIN_PIPELINE ":GraphicsProbePrepare";
			}
		};

		class ShaderResourceDelegate : public RenderCore::Techniques::IShaderResourceDelegate
		{
		public:
			struct MultiViewProperties
			{
				unsigned _probeCount; unsigned _dummy[15];
				Float4x4 _worldToProjection[64];
			};
			MultiViewProperties _multProbeProperties;

			virtual void WriteImmediateData(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
			{
				REQUIRE(idx == 0);
				REQUIRE(dst.size() == sizeof(MultiViewProperties));
				std::memcpy(dst.begin(), &_multProbeProperties, sizeof(_multProbeProperties));
			}

			virtual size_t GetImmediateDataSize(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override
			{
				REQUIRE(idx == 0);
				return sizeof(MultiViewProperties);
			}

			ShaderResourceDelegate(IteratorRange<const RenderCore::Techniques::CameraDesc*> cameras, UInt2 viewportDims)
			{
				_multProbeProperties._probeCount = cameras.size();
				REQUIRE(_multProbeProperties._probeCount <= dimof(MultiViewProperties::_worldToProjection));
				for (unsigned c=0; c<_multProbeProperties._probeCount; ++c) {
					auto projDesc = RenderCore::Techniques::BuildProjectionDesc(cameras[c], viewportDims);
					_multProbeProperties._worldToProjection[c] = projDesc._worldToProjection;
				}
				BindImmediateData(0, Hash64("MultiViewProperties"));
			}
		};

		class CustomDrawDelegate : public ToolsRig::IExtendedDrawablesWriter::CustomDrawDelegate
		{
		public:
			virtual void OnDraw(
				RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& executeContext,
				const RenderCore::Techniques::Drawable& d,
				unsigned vertexCount, const Float4x4& localToWorld,
				uint64_t viewMask) override
			{
				struct CustomConstants
				{
					Float3x4 _localToWorld;
					Float3 _localSpaceView; unsigned _viewMask;
					// uint8_t _viewIndices[64];
				} constants;
				constants._localToWorld = AsFloat3x4(localToWorld);
				constants._viewMask = viewMask;
				unsigned v=0;
				while (viewMask) {
					auto lz = xl_ctz8(viewMask);
					// constants._viewIndices[v++] = lz;
					v++;
					viewMask ^= 1ull<<lz;
				}
				unsigned viewCount = v; 
				if (!viewCount) return;

				// for (; v<dimof(CustomConstants::_viewIndices); ++v) constants._viewIndices[v] = 0;
				executeContext.ApplyLooseUniforms(RenderCore::ImmediateDataStream(constants));
				executeContext.DrawInstances(vertexCount, viewCount);
			}
		};

		class CullingDelegate : public ToolsRig::IExtendedDrawablesWriter::CullingDelegate
		{
		public:
			std::vector<Float4x4> _cullingFrustums;
			virtual void TestSphere(
				/* out */ uint64_t& boundaryViewMask,
				/* out */ uint64_t& withinViewMask,
				uint64_t testViewMask,
				Float3 center, float radius) const override
			{
				/*boundaryViewMask = 0;
				withinViewMask = testViewMask;
				return;*/

				boundaryViewMask = withinViewMask = 0;
				while (testViewMask) {
					auto lz = xl_ctz8(testViewMask);
					auto test = XLEMath::TestAABB(_worldToCullingFrustums[lz], center-Float3{radius, radius, radius}, center+Float3{radius, radius, radius}, RenderCore::Techniques::GetDefaultClipSpaceType());
					withinViewMask |= (test == CullTestResult::Within)<<lz;
					boundaryViewMask |= (test == CullTestResult::Boundary)<<lz;
					testViewMask ^= 1ull<<lz;
				}
			}
			virtual void TestAABB(
				/* out */ uint64_t& boundaryViewMask,
				/* out */ uint64_t& withinViewMask,
				uint64_t testViewMask,
				Float3 mins, Float3 maxs) const override
			{
				/*boundaryViewMask = 0;
				withinViewMask = testViewMask;
				return;*/

				boundaryViewMask = withinViewMask = 0;
				while (testViewMask) {
					auto lz = xl_ctz8(testViewMask);
					auto test = XLEMath::TestAABB(_worldToCullingFrustums[lz], mins, maxs, RenderCore::Techniques::GetDefaultClipSpaceType());
					withinViewMask |= (test == CullTestResult::Within)<<lz;
					boundaryViewMask |= (test == CullTestResult::Boundary)<<lz;
					testViewMask ^= 1ull<<lz;
				}
			}
			std::vector<Float4x4> _worldToCullingFrustums;
			CullingDelegate(IteratorRange<const RenderCore::Techniques::CameraDesc*> cameras, UInt2 viewportDims)
			{
				_worldToCullingFrustums.reserve(cameras.size());
				for (const auto& c:cameras)
					_worldToCullingFrustums.push_back(BuildProjectionDesc(c, viewportDims)._worldToProjection);
			}
		};

		static constexpr unsigned maxViewsPerDraw = 32;

		void Execute(
			RenderCore::IThreadContext& threadContext, RenderCore::Techniques::ParsingContext& parsingContext,
			const LightingEngineTestApparatus& testApparatus,
			IteratorRange<const RenderCore::Techniques::CameraDesc*> cameras,
			ToolsRig::IDrawablesWriter& drawablesWriter)
		{
			using namespace RenderCore;
			auto& projDesc = parsingContext.GetProjectionDesc();
			projDesc = Techniques::ProjectionDesc{};		// identity world-to-projection

			auto drawDelegate = std::make_shared<CustomDrawDelegate>();
			
			auto* extWriter = dynamic_cast<ToolsRig::IExtendedDrawablesWriter*>(&drawablesWriter);
			assert(extWriter);

			auto frag = _fragments.begin();
			while (!cameras.empty()) {
				auto batchCameras = cameras;
				batchCameras.second = std::min(batchCameras.second, batchCameras.first+maxViewsPerDraw);

				CullingDelegate cullingDelegate(batchCameras, s_testResolution);
				RenderCore::Techniques::DrawablesPacket pkt;
				uint64_t testViewMask = (batchCameras.size() < 64) ? (1ull<<uint64_t(batchCameras.size()))-1 : ~0ull;
				extWriter->WriteDrawables(pkt, cullingDelegate, testViewMask, drawDelegate);

				auto uniformDel = std::make_shared<ShaderResourceDelegate>(batchCameras, s_testResolution);
				parsingContext.GetUniformDelegateManager()->AddShaderResourceDelegate(uniformDel);

				Techniques::RenderPassInstance rpi{parsingContext, *frag};
				Techniques::Draw(parsingContext, *testApparatus._pipelineAcceleratorPool, *_cfg, pkt);

				parsingContext.GetUniformDelegateManager()->RemoveShaderResourceDelegate(*uniformDel);
				cameras.first = batchCameras.second;
				++frag;
			}
		}

		VertexInstancingShader(const LightingEngineTestApparatus& testApparatus)
		{
			_fragments = MakeFragments(s_probesToRender, maxViewsPerDraw);
			_cfg = CreateSequencerConfig("mass-probe-vertex-instancing", *testApparatus._pipelineAcceleratorPool, std::make_shared<TechniqueDelegate>());
		}

	private:
		std::vector<RenderCore::Techniques::FrameBufferDescFragment> _fragments;
	};

	// View instancing shader (multiview in Vulkan parlance) -- use the multiview functionality built into the api to broadcast draws to multiple array layers 
	class ViewInstancingShader
	{
	public:
		std::shared_ptr<RenderCore::Techniques::SequencerConfig> _cfg;
		
		class TechniqueDelegate : public RenderCore::Techniques::ITechniqueDelegate
		{
		public:
			virtual ::Assets::PtrToMarkerPtr<GraphicsPipelineDesc> GetPipelineDesc(
				const RenderCore::Techniques::CompiledShaderPatchCollection::Interface& shaderPatches,
				const RenderCore::Assets::RenderStateSet& renderStates)
			{
				using namespace RenderCore;
				auto result = std::make_shared<::Assets::MarkerPtr<GraphicsPipelineDesc>>("from-probe-prepare-delegate");
				auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
				nascentDesc->_depthStencil = Techniques::CommonResourceBox::s_dsReadWriteCloserThan;
				nascentDesc->_blend.push_back(Techniques::CommonResourceBox::s_abOpaque);
				nascentDesc->_shaders[(unsigned)ShaderStage::Vertex] = "ut-data/multiview_shader.hlsl:vs_main:vs_6_1";
				nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = "ut-data/multiview_shader.hlsl:ps_main:ps_6_1";
				nascentDesc->_selectorPreconfigurationFile = "xleres/TechniqueLibrary/Framework/SelectorPreconfiguration.hlsl";
				result->SetAsset(std::move(nascentDesc));
				return result;
			}

			virtual std::string GetPipelineLayout()
			{
				return MAIN_PIPELINE ":GraphicsProbePrepare";
			}
		};

		class ShaderResourceDelegate : public RenderCore::Techniques::IShaderResourceDelegate
		{
		public:
			struct MultiViewProperties
			{
				unsigned _probeCount; unsigned _dummy[15];
				Float4x4 _worldToProjection[32];
			};
			MultiViewProperties _multProbeProperties;

			virtual void WriteImmediateData(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
			{
				REQUIRE(idx == 0);
				REQUIRE(dst.size() == sizeof(MultiViewProperties));
				std::memcpy(dst.begin(), &_multProbeProperties, sizeof(_multProbeProperties));
			}

			virtual size_t GetImmediateDataSize(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override
			{
				REQUIRE(idx == 0);
				return sizeof(MultiViewProperties);
			}

			ShaderResourceDelegate(IteratorRange<const RenderCore::Techniques::CameraDesc*> cameras, UInt2 viewportDims)
			{
				_multProbeProperties._probeCount = cameras.size();
				REQUIRE(_multProbeProperties._probeCount <= dimof(MultiViewProperties::_worldToProjection));
				for (unsigned c=0; c<_multProbeProperties._probeCount; ++c) {
					auto projDesc = RenderCore::Techniques::BuildProjectionDesc(cameras[c], viewportDims);
					_multProbeProperties._worldToProjection[c] = projDesc._worldToProjection;
				}
				BindImmediateData(0, Hash64("MultiViewProperties"));
			}
		};

		static constexpr unsigned maxMultiview = 32;

		void Execute(
			RenderCore::IThreadContext& threadContext, RenderCore::Techniques::ParsingContext& parsingContext,
			const LightingEngineTestApparatus& testApparatus,
			IteratorRange<const RenderCore::Techniques::CameraDesc*> cameras,
			ToolsRig::IDrawablesWriter& drawablesWriter)
		{
			using namespace RenderCore;
			
			RenderCore::Techniques::DrawablesPacket pkt;
			drawablesWriter.WriteDrawables(pkt);

			auto frag = _fragments.begin();
			while (!cameras.empty()) {
				auto batchCameras = cameras;
				batchCameras.second = std::min(batchCameras.second, batchCameras.first+maxMultiview);

				auto uniformDel = std::make_shared<ShaderResourceDelegate>(batchCameras, s_testResolution);
				parsingContext.GetUniformDelegateManager()->AddShaderResourceDelegate(uniformDel);

				auto& projDesc = parsingContext.GetProjectionDesc();
				projDesc = Techniques::ProjectionDesc{};		// identity world-to-projection

				{
					Techniques::RenderPassInstance rpi{parsingContext, *frag};
					Techniques::Draw(parsingContext, *testApparatus._pipelineAcceleratorPool, *_cfg, pkt);
				}

				parsingContext.GetUniformDelegateManager()->RemoveShaderResourceDelegate(*uniformDel);
				cameras.first = batchCameras.second;
				++frag;
			}
		}

		ViewInstancingShader(const LightingEngineTestApparatus& testApparatus)
		{
			_fragments = MakeFragments(s_probesToRender, maxMultiview, true);
			_cfg = CreateSequencerConfig("mass-probe-view-instancing", *testApparatus._pipelineAcceleratorPool, std::make_shared<TechniqueDelegate>(), true);
		}

	private:
		std::vector<RenderCore::Techniques::FrameBufferDescFragment> _fragments;
	};

	template<typename TestClass>
		void RunTest(
			RenderCore::IThreadContext& threadContext, RenderCore::Techniques::ParsingContext& parsingContext, 
			const LightingEngineTestApparatus& testApparatus, IteratorRange<const RenderCore::Techniques::CameraDesc*> cameras,
			ToolsRig::IDrawablesWriter& drawablesWriter)
	{
		TestClass tester(testApparatus);

		{
			RenderCore::Techniques::DrawablesPacket pkt;
			drawablesWriter.WriteDrawables(pkt);
			auto marker = RenderCore::Techniques::PrepareResources(*testApparatus._pipelineAcceleratorPool, *tester._cfg, pkt);
			if (marker) {
				marker->StallWhilePending();
				REQUIRE(marker->GetAssetState() == ::Assets::AssetState::Ready);
			}
			testApparatus._pipelineAcceleratorPool->RebuildAllOutOfDatePipelines();		// must call this to flip completed pipelines, etc, to visible
			::Assets::Services::GetAssetSets().OnFrameBarrier();
		}
		tester.Execute(threadContext, parsingContext, testApparatus, cameras, drawablesWriter);
	}

	TEST_CASE( "LightingEngine-MassProbeRender", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();
		auto utdatamnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));

		auto threadContext = testHelper->_device->GetImmediateContext();
		auto parsingContext = InitializeParsingContext(*testApparatus._techniqueContext, *threadContext);

		const Float2 worldMins{0.f, 0.f}, worldMaxs{100.f, 100.f};
		auto drawableWriter = ToolsRig::DrawablesWriterHelper(*testHelper->_device, *testApparatus._drawablesPool, *testApparatus._pipelineAcceleratorPool)
			.CreateShapeWorldDrawableWriter(worldMins, worldMaxs);

		RenderCore::Techniques::CameraDesc cameras[s_probesToRender];
		std::mt19937_64 rng(745023620);
		for (unsigned c=0; c<s_probesToRender; ++c) {
			// Position a camera at a random point, facing downwards in random direction
			Float3 position {
				std::uniform_real_distribution<>(worldMins[0], worldMaxs[1])(rng),
				10.f,
				std::uniform_real_distribution<>(worldMins[0], worldMaxs[1])(rng)
			};
			float angle = std::uniform_real_distribution<>(0, 2.0f*gPI)(rng);
			Float3 forward = Normalize(Float3{std::cos(angle), -2.f, std::sin(angle)});
			auto& camera = cameras[c];
			camera._cameraToWorld = MakeCameraToWorld(forward, Float3{0.0f, 1.0f, 0.0f}, position);
			camera._projection = Techniques::CameraDesc::Projection::Perspective;
			camera._nearClip = 0.1f;
			camera._farClip = 10.f;
		}

		testHelper->BeginFrameCapture();
		{
			Metal::TimeStampQueryPool queryPool(Metal::GetObjectFactory());
			auto queryPoolFrameId = queryPool.BeginFrame(*Metal::DeviceContext::Get(*threadContext));

			queryPool.SetTimeStampQuery(*Metal::DeviceContext::Get(*threadContext));
			const unsigned iterationCount = 512;
			for (unsigned c=0; c<iterationCount; ++c)
				RunTest<ViewInstancingShader>(*threadContext, parsingContext, testApparatus, MakeIteratorRange(cameras), *drawableWriter);
			queryPool.SetTimeStampQuery(*Metal::DeviceContext::Get(*threadContext));

			queryPool.EndFrame(*Metal::DeviceContext::Get(*threadContext), queryPoolFrameId);
			auto cpuTimeStart = std::chrono::steady_clock::now();
			threadContext->CommitCommands();
			for (;;) {
				auto queryResults = queryPool.GetFrameResults(*Metal::DeviceContext::Get(*threadContext), queryPoolFrameId);
				if (queryResults._resultsReady) {
					REQUIRE(queryResults._resultsEnd != queryResults._resultsStart);
					REQUIRE(queryResults._frequency != 0);
					auto elapsed = *(queryResults._resultsStart+1) - *queryResults._resultsStart;
					std::cout << "Mass probe rendering (per iteration): " << elapsed / float(queryResults._frequency) * 1000.0f / float(iterationCount) << "ms" << std::endl;
					std::cout << "CPU time waiting for GPU (per iteration): " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cpuTimeStart).count() / float(iterationCount) << "ms" << std::endl;
					break;
				}
			}
		}
		testHelper->EndFrameCapture();

		// test: lots of geo vs minimal geo
		// test: fewer than 64 views
		// test: extra attributes (tangent space, tex coord, etc)

		::Assets::MainFileSystem::GetMountingTree()->Unmount(utdatamnt);
	}
}

