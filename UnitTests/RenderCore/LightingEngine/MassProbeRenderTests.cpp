// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineTestHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/CommonBindings.h"
#include "../../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/Drawables.h"
#include "../../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../../Math/Transformations.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/AssetTraits.h"
#include "../../../xleres/FileList.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <random>

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
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

	TEST_CASE( "LightingEngine-MassProbeRender", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();

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
			camera._nearClip = 0.01f;
			camera._farClip = 100.f;
		}

		// Simpliest method -- we just create a massive render target with separate subpasses for each
		// array layer and just draw each item normally

		Techniques::FrameBufferDescFragment fragment;
		fragment.DefineAttachment(s_attachmentProbeTarget, LoadStore::Clear);
		fragment.DefineAttachment(s_attachmentProbeDepth, LoadStore::Clear);
		for (unsigned c=0; c<s_probesToRender; ++c) {
			TextureViewDesc viewDesc;
			viewDesc._arrayLayerRange._min = c;
			viewDesc._arrayLayerRange._count = 1;
			SubpassDesc sp;
			sp.AppendOutput(0, viewDesc);
			sp.SetDepthStencil(1, viewDesc);
			fragment.AddSubpass(std::move(sp));
		}

		std::shared_ptr<Techniques::SequencerConfig> cfg;
		{
			std::vector<AttachmentDesc> attachments {
				AttachmentDesc{ Format::B8G8R8A8_UNORM_SRGB, 0, LoadStore::Clear, LoadStore::Retain, 0, BindFlag::ShaderResource },
				AttachmentDesc{ Format::D16_UNORM, 0, LoadStore::Clear, LoadStore::DontCare }
			};
			SubpassDesc sp;
			sp.AppendOutput(0); sp.SetDepthStencil(1);
			sp.SetName("prepare-probe");
			FrameBufferDesc representativeFB(std::move(attachments), std::vector<SubpassDesc>{sp});
			auto techniqueSetFile = ::Assets::MakeFuture<std::shared_ptr<Techniques::TechniqueSetFile>>(ILLUM_TECH);
			auto techDel = Techniques::CreateTechniqueDelegate_ProbePrepare(techniqueSetFile);
			cfg = testApparatus._pipelineAcceleratorPool->CreateSequencerConfig(techDel, ParameterBox{}, representativeFB, 0);
		}

		{
			RenderCore::Techniques::DrawablesPacket pkt;
			drawablesWriter->WriteDrawables(pkt);
			auto marker = Techniques::PrepareResources(*testApparatus._pipelineAcceleratorPool, *cfg, pkt);
			if (marker) {
				marker->StallWhilePending();
				REQUIRE(marker->GetAssetState() == ::Assets::AssetState::Ready);
			}
		}

		testHelper->BeginFrameCapture();

		{
			Techniques::RenderPassInstance rpi{*threadContext, parsingContext, fragment};
			for (unsigned c=0; ;) {
				auto& projDesc = parsingContext.GetProjectionDesc();
				projDesc = BuildProjectionDesc(cameras[c], s_testResolution);

				RenderCore::Techniques::DrawablesPacket pkt;
				drawablesWriter->WriteDrawables(pkt, projDesc._worldToProjection);

				RenderCore::Techniques::SequencerUniformsHelper sequencerUniforms {parsingContext};
				Techniques::Draw(*threadContext, parsingContext, *testApparatus._pipelineAcceleratorPool, *cfg, sequencerUniforms, pkt);

				++c;
				if (c == s_probesToRender) break;
				rpi.NextSubpass();
			}
		}
		
		testHelper->EndFrameCapture();
	}
}

