// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../UnitTestHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../RenderCore/Techniques/CommonBindings.h"
#include "../../../RenderCore/IDevice.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
	static void DefineTestAttachments(
		RenderCore::Techniques::FragmentStitchingContext& stitchingContext,
		unsigned semanticOffset,
		UInt2 dims)
	{
		using namespace RenderCore;
		using namespace RenderCore::Techniques;

		stitchingContext.DefineAttachment(
			AttachmentSemantics::ColorLDR + semanticOffset,
			CreateDesc(
				BindFlag::RenderTarget | BindFlag::TransferSrc | BindFlag::PresentationSrc, 0, 0, 
				TextureDesc::Plain2D(dims[0], dims[1], Format::R8G8B8A8_UNORM_SRGB),
				"color-ldr"),
			PreregisteredAttachment::State::Uninitialized,
			BindFlag::PresentationSrc);

		stitchingContext.DefineAttachment(
			AttachmentSemantics::MultisampleDepth + semanticOffset,
			CreateDesc(
				BindFlag::DepthStencil | BindFlag::InputAttachment, 0, 0, 
				TextureDesc::Plain2D(dims[0], dims[1], Format::D24_UNORM_S8_UINT),
				"depth-stencil"),
			PreregisteredAttachment::State::Uninitialized,
			BindFlag::DepthStencil);

		stitchingContext.DefineAttachment(
			AttachmentSemantics::ShadowDepthMap + semanticOffset,
			CreateDesc(
				BindFlag::DepthStencil | BindFlag::ShaderResource, 0, 0, 
				TextureDesc::Plain2D(dims[0], dims[1], Format::D16_UNORM),
				"depth-stencil"),
			PreregisteredAttachment::State::Initialized,
			BindFlag::DepthStencil);
	}

	TEST_CASE( "RenderPassManagement-BuildFromFragments", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		using namespace RenderCore::Techniques;

		auto testHelper = MakeTestHelper();
		auto frameBufferPool = CreateFrameBufferPool();
		AttachmentPool attachmentPool(testHelper->_device);

		SECTION("Basic construction")
		{
			FragmentStitchingContext stitchingContext;
			stitchingContext._workingProps._outputWidth = 1024;
			stitchingContext._workingProps._outputHeight = 1024;
			DefineTestAttachments(stitchingContext, 0, UInt2(1024, 1024));

			FrameBufferDescFragment fragment;
			SubpassDesc subpass[3];
			auto colorLDR = fragment.DefineAttachment(AttachmentSemantics::ColorLDR).Clear();
			auto depthAttachment = fragment.DefineAttachment(AttachmentSemantics::MultisampleDepth).Clear();
			subpass[0].AppendOutput(colorLDR);
			subpass[0].SetDepthStencil(depthAttachment);
			
			auto tempAttach0 = fragment.DefineAttachment(0).FixedFormat(Format::R8G8B8A8_UNORM_SRGB).Clear().Discard();
			auto tempAttach1 = fragment.DefineAttachment(0).FixedFormat(Format::R8G8B8A8_UNORM_SRGB).Clear();
			subpass[1].AppendInput(depthAttachment);
			subpass[1].AppendOutput(tempAttach0);
			subpass[1].AppendOutput(tempAttach1);

			subpass[2].AppendInput(tempAttach0);
			subpass[2].AppendInput(tempAttach1);
			subpass[2].AppendOutput(fragment.DefineAttachment(0).FixedFormat(Format::R8G8B8A8_UNORM_SRGB).Clear());
			subpass[2].AppendOutput(colorLDR);

			for (auto& sp:subpass) fragment.AddSubpass(std::move(sp));

			auto stitched = stitchingContext.TryStitchFrameBufferDesc(MakeIteratorRange(&fragment, &fragment+1));
			(void)stitched;

			RenderPassInstance rpi{
				*testHelper->_device->GetImmediateContext(),
				stitched._fbDesc,
				stitched._fullAttachmentDescriptions,
				*frameBufferPool,
				attachmentPool};
			rpi.NextSubpass();
			rpi.NextSubpass();
			rpi.End();
		}

		SECTION("Merging with some reuse")
		{
			FrameBufferDescFragment fragments[3];

			{
				// Subpass 0
				//		Clear & retain ColorLDR
				//		Write tempAttach0
				// Subpass 1
				//		Read and discard tempAttach0
				//		Write and retain ColorLDR
				//		Write and discard tempAttach2 & tempAttach3
				SubpassDesc subpass[2];
				auto colorLDR = fragments[0].DefineAttachment(AttachmentSemantics::ColorLDR).Clear();
				auto tempAttach0 = fragments[0].DefineAttachment(0).FixedFormat(Format::R8G8B8A8_UNORM_SRGB).NoInitialState().Discard();
				auto tempAttach2 = fragments[0].DefineAttachment(0).FixedFormat(Format::R32_FLOAT).NoInitialState().Discard();
				auto tempAttach3 = fragments[0].DefineAttachment(0).FixedFormat(Format::R32_FLOAT).NoInitialState().Discard();
				subpass[0].AppendOutput(colorLDR);
				subpass[0].AppendOutput(tempAttach0);

				subpass[1].AppendInput(tempAttach0);
				subpass[1].AppendOutput(colorLDR);
				subpass[1].AppendOutput(tempAttach2);
				subpass[1].AppendOutput(tempAttach3);
				for (auto& sp:subpass) fragments[0].AddSubpass(std::move(sp));
			}

			{
				// Subpass 0
				//		Write tempAttach0
				//		Write and retain tempAttach2
				SubpassDesc subpass[1];
				auto tempAttach0 = fragments[1].DefineAttachment(0).FixedFormat(Format::R8G8B8A8_UNORM_SRGB).NoInitialState().Discard();
				auto tempAttach2 = fragments[1].DefineAttachment(0).FixedFormat(Format::R32_FLOAT).NoInitialState();
				subpass[0].AppendOutput(tempAttach0);
				subpass[0].AppendOutput(tempAttach2);
				for (auto& sp:subpass) fragments[1].AddSubpass(std::move(sp));
			}

			{
				// Subpass 0
				//		Write tempAttach3
				// Subpass
				//		Read tempAttach3
				//		Write tempAttach4
				SubpassDesc subpass[2];
				auto tempAttach3 = fragments[2].DefineAttachment(0).FixedFormat(Format::R32_FLOAT).NoInitialState().Discard();
				auto tempAttach4 = fragments[2].DefineAttachment(0).FixedFormat(Format::R32_FLOAT).NoInitialState().Discard();
				subpass[0].AppendOutput(tempAttach3);
				subpass[1].AppendInput(tempAttach3);
				subpass[1].AppendOutput(tempAttach4);
				for (auto& sp:subpass) fragments[2].AddSubpass(std::move(sp));
			}

			FragmentStitchingContext stitchingContext;
			stitchingContext._workingProps._outputWidth = 1024;
			stitchingContext._workingProps._outputHeight = 1024;
			DefineTestAttachments(stitchingContext, 0, UInt2(1024, 1024));

			auto stitched = stitchingContext.TryStitchFrameBufferDesc(MakeIteratorRange(fragments));
			(void)stitched;

			RenderPassInstance rpi{
				*testHelper->_device->GetImmediateContext(),
				stitched._fbDesc,
				stitched._fullAttachmentDescriptions,
				*frameBufferPool,
				attachmentPool};

			const auto& finalFBDesc = rpi.GetFrameBufferDesc();
			REQUIRE(finalFBDesc.GetAttachments().size() == 4);
		}
	}
}
