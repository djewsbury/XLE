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
#include "../../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../../Math/Transformations.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/AssetTraits.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
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
			#include "xleres/TechniqueLibrary/Framework/DeformVertex.hlsl"
			#include "xleres/TechniqueLibrary/Core/BuildVSOUT.vertex.hlsl"

			VSOUT vs_main(VSIN input)
			{
				DeformedVertex deformedVertex = DeformedVertex_Initialize(input);
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
			#include "xleres/TechniqueLibrary/Framework/DeformVertex.hlsl"
			#include "xleres/TechniqueLibrary/Core/BuildVSOUT.vertex.hlsl"

			VSOUT vs_main(VSIN input)
			{
				DeformedVertex deformedVertex = DeformedVertex_Initialize(input);
				VSOUT result = BuildVSOUT(deformedVertex, input);
				result.renderTargetIndex = 0;		// embued properly in the geometry shader
				return result;
			}

			cbuffer MultiProbeProperties BIND_SEQ_B1
			{
				uint MultiProbeCount; uint4 Dummy[3];
				row_major float4x4 MultiProbeViews[64];
			}

			[maxvertexcount(3*64)]
				void gs_main(	triangle VSOUT input[3],
								inout TriangleStream<VSOUT> outputStream)
			{
				// amplify out to up to 64 views
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
		)--"))
	};

	static const UInt2 s_testResolution { 64, 64 };
	const unsigned s_probesToRender = 64;

	const uint64_t s_attachmentProbeTarget = 100;
	const uint64_t s_attachmentProbeDepth = 101;

	static RenderCore::Techniques::ParsingContext InitializeParsingContext(RenderCore::Techniques::TechniqueContext& techniqueContext)
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

		Techniques::ParsingContext parsingContext{techniqueContext};

		auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
		stitchingContext._workingProps = fbProps;
		for (const auto&a:preregisteredAttachments)
			stitchingContext.DefineAttachment(a._semantic, a._desc, a._state, a._layoutFlags);
		return parsingContext;
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
			virtual ::Assets::PtrToFuturePtr<GraphicsPipelineDesc> GetPipelineDesc(
				const RenderCore::Techniques::CompiledShaderPatchCollection::Interface& shaderPatches,
				const RenderCore::Assets::RenderStateSet& renderStates)
			{
				using namespace RenderCore;
				auto result = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDesc>>("from-probe-prepare-delegate");
				auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
				nascentDesc->_depthStencil = Techniques::CommonResourceBox::s_dsReadWriteLessThan;
				nascentDesc->_blend.push_back(Techniques::CommonResourceBox::s_abOpaque);
				nascentDesc->_shaders[(unsigned)ShaderStage::Vertex] = "ut-data/simple.hlsl:vs_main";
				nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = "ut-data/simple.hlsl:ps_main";
				nascentDesc->_selectorPreconfigurationFile = "xleres/TechniqueLibrary/Framework/SelectorPreconfiguration.hlsl";
				result->SetAsset(std::move(nascentDesc), {});
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
			Techniques::RenderPassInstance rpi{threadContext, parsingContext, _fragment};
			for (unsigned c=0; ;) {
				auto& projDesc = parsingContext.GetProjectionDesc();
				projDesc = BuildProjectionDesc(cameras[c], s_testResolution);

				RenderCore::Techniques::DrawablesPacket pkt;
				drawablesWriter.WriteDrawables(pkt, projDesc._worldToProjection);

				RenderCore::Techniques::SequencerUniformsHelper sequencerUniforms {parsingContext};
				Techniques::Draw(threadContext, parsingContext, *testApparatus._pipelineAcceleratorPool, *_cfg, sequencerUniforms, pkt);

				++c;
				if (c == cameras.size()) break;
				rpi.NextSubpass();
			}
		}

		SimpleRendering(const LightingEngineTestApparatus& testApparatus)
		{
			using namespace RenderCore;
			_fragment.DefineAttachment(s_attachmentProbeTarget, LoadStore::Clear);
			_fragment.DefineAttachment(s_attachmentProbeDepth, LoadStore::Clear);
			for (unsigned c=0; c<s_probesToRender; ++c) {
				TextureViewDesc viewDesc;
				viewDesc._arrayLayerRange._min = c;
				viewDesc._arrayLayerRange._count = 1;
				SubpassDesc sp;
				sp.AppendOutput(0, viewDesc);
				sp.SetDepthStencil(1, viewDesc);
				_fragment.AddSubpass(std::move(sp));
			}

			{
				std::vector<AttachmentDesc> attachments {
					AttachmentDesc{ Format::B8G8R8A8_UNORM_SRGB, 0, LoadStore::Clear, LoadStore::Retain, 0, BindFlag::ShaderResource },
					AttachmentDesc{ Format::D16_UNORM, 0, LoadStore::Clear, LoadStore::DontCare }
				};
				SubpassDesc sp;
				sp.AppendOutput(0); sp.SetDepthStencil(1);
				sp.SetName("prepare-probe");
				FrameBufferDesc representativeFB(std::move(attachments), std::vector<SubpassDesc>{sp});
				auto techDel = std::make_shared<TechniqueDelegate>();
				_cfg = testApparatus._pipelineAcceleratorPool->CreateSequencerConfig(techDel, ParameterBox{}, representativeFB, 0);
			}
		}

	private:
		RenderCore::Techniques::FrameBufferDescFragment _fragment;
	};

	// Amplifying geo shader -- one draw as input, with a geo shader that creates primitives for all of the different views
	class AmplifyingGeoShader
	{
	public:
		std::shared_ptr<RenderCore::Techniques::SequencerConfig> _cfg;
		
		class TechniqueDelegate : public RenderCore::Techniques::ITechniqueDelegate
		{
		public:
			virtual ::Assets::PtrToFuturePtr<GraphicsPipelineDesc> GetPipelineDesc(
				const RenderCore::Techniques::CompiledShaderPatchCollection::Interface& shaderPatches,
				const RenderCore::Assets::RenderStateSet& renderStates)
			{
				using namespace RenderCore;
				auto result = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDesc>>("from-probe-prepare-delegate");
				auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
				nascentDesc->_depthStencil = Techniques::CommonResourceBox::s_dsReadWriteLessThan;
				nascentDesc->_blend.push_back(Techniques::CommonResourceBox::s_abOpaque);
				nascentDesc->_shaders[(unsigned)ShaderStage::Vertex] = "ut-data/amplifying_geo_shader.hlsl:vs_main";
				nascentDesc->_shaders[(unsigned)ShaderStage::Geometry] = "ut-data/amplifying_geo_shader.hlsl:gs_main";
				nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = "ut-data/amplifying_geo_shader.hlsl:ps_main";
				nascentDesc->_selectorPreconfigurationFile = "xleres/TechniqueLibrary/Framework/SelectorPreconfiguration.hlsl";
				nascentDesc->_manualSelectorFiltering._setValues.SetParameter("VSOUT_HAS_RENDER_TARGET_INDEX", 1);
				result->SetAsset(std::move(nascentDesc), {});
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
			struct MultiProbeProperties
			{
				unsigned _probeCount; unsigned _dummy[15];
				Float4x4 _worldToProjection[64];
			};
			MultiProbeProperties _multProbeProperties;

			virtual void WriteImmediateData(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
			{
				REQUIRE(idx == 0);
				REQUIRE(dst.size() == sizeof(MultiProbeProperties));
				std::memcpy(dst.begin(), &_multProbeProperties, sizeof(_multProbeProperties));
			}

			virtual size_t GetImmediateDataSize(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override
			{
				REQUIRE(idx == 0);
				return sizeof(MultiProbeProperties);
			}

			ShaderResourceDelegate(IteratorRange<const RenderCore::Techniques::CameraDesc*> cameras, UInt2 viewportDims)
			{
				_multProbeProperties._probeCount = cameras.size();
				REQUIRE(_multProbeProperties._probeCount <= dimof(MultiProbeProperties::_worldToProjection));
				for (unsigned c=0; c<_multProbeProperties._probeCount; ++c) {
					auto projDesc = RenderCore::Techniques::BuildProjectionDesc(cameras[c], viewportDims);
					_multProbeProperties._worldToProjection[c] = projDesc._worldToProjection;
				}
				BindImmediateData(0, Hash64("MultiProbeProperties"));
			}
		};

		void Execute(
			RenderCore::IThreadContext& threadContext, RenderCore::Techniques::ParsingContext& parsingContext,
			const LightingEngineTestApparatus& testApparatus,
			IteratorRange<const RenderCore::Techniques::CameraDesc*> cameras,
			ToolsRig::IDrawablesWriter& drawablesWriter)
		{
			using namespace RenderCore;
			auto uniformDel = std::make_shared<ShaderResourceDelegate>(cameras, UInt2{64, 64});
			parsingContext.AddShaderResourceDelegate(uniformDel);

			auto& projDesc = parsingContext.GetProjectionDesc();
			projDesc = Techniques::ProjectionDesc{};		// identity world-to-projection

			{
				Techniques::RenderPassInstance rpi{threadContext, parsingContext, _fragment};

				RenderCore::Techniques::DrawablesPacket pkt;
				drawablesWriter.WriteDrawables(pkt);

				RenderCore::Techniques::SequencerUniformsHelper sequencerUniforms {parsingContext};
				Techniques::Draw(threadContext, parsingContext, *testApparatus._pipelineAcceleratorPool, *_cfg, sequencerUniforms, pkt);
			}

			parsingContext.RemoveShaderResourceDelegate(*uniformDel);
		}

		AmplifyingGeoShader(const LightingEngineTestApparatus& testApparatus)
		{
			using namespace RenderCore;
			_fragment.DefineAttachment(s_attachmentProbeTarget, LoadStore::Clear);
			_fragment.DefineAttachment(s_attachmentProbeDepth, LoadStore::Clear);
			SubpassDesc sp;
			sp.AppendOutput(0);
			sp.SetDepthStencil(1);
			_fragment.AddSubpass(std::move(sp));

			{
				std::vector<AttachmentDesc> attachments {
					AttachmentDesc{ Format::B8G8R8A8_UNORM_SRGB, 0, LoadStore::Clear, LoadStore::Retain, 0, BindFlag::ShaderResource },
					AttachmentDesc{ Format::D16_UNORM, 0, LoadStore::Clear, LoadStore::DontCare }
				};
				SubpassDesc sp;
				sp.AppendOutput(0); sp.SetDepthStencil(1);
				sp.SetName("prepare-probe");
				FrameBufferDesc representativeFB(std::move(attachments), std::vector<SubpassDesc>{sp});
				auto techDel = std::make_shared<TechniqueDelegate>();
				_cfg = testApparatus._pipelineAcceleratorPool->CreateSequencerConfig(techDel, ParameterBox{}, representativeFB, 0);
			}
		}

	private:
		RenderCore::Techniques::FrameBufferDescFragment _fragment;
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
		auto parsingContext = InitializeParsingContext(*testApparatus._techniqueContext);

		const Float2 worldMins{0.f, 0.f}, worldMaxs{100.f, 100.f};
		auto drawablesWriter = ToolsRig::CreateShapeWorldDrawableWriter(
			*testHelper->_device, *testApparatus._pipelineAcceleratorPool,
			worldMins, worldMaxs);

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
		RunTest<AmplifyingGeoShader>(*threadContext, parsingContext, testApparatus, MakeIteratorRange(cameras), *drawablesWriter);
		testHelper->EndFrameCapture();

		// test: lots of geo vs minimal geo
		// test: fewer than 64 views
		// test: extra attributes (tangent space, tex coord, etc)

		::Assets::MainFileSystem::GetMountingTree()->Unmount(utdatamnt);
	}
}

