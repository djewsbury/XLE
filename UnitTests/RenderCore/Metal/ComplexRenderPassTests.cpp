// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MetalTestHelper.h"
#include "MetalTestShaders.h"
#include "../../../RenderCore/Metal/Shader.h"
#include "../../../RenderCore/Metal/InputLayout.h"
#include "../../../RenderCore/Metal/State.h"
#include "../../../RenderCore/Metal/TextureView.h"
#include "../../../RenderCore/Metal/PipelineLayout.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
#include "../../../RenderCore/Metal/FrameBuffer.h"
#include "../../../RenderCore/ResourceDesc.h"
#include "../../../RenderCore/Format.h"
#include "../../../RenderCore/BufferView.h"
#include "../../../Math/Vector.h"
#include "../../../Utility/MemoryUtils.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace Utility::Literals;

namespace UnitTests
{
	class VertexPCT
	{
	public:
		Float3      _position;
		unsigned    _color;
		Float2      _texCoord;
	};

	static VertexPCT vertices_topQuad_Red[] = {
		// Clockwise-winding triangle
		VertexPCT { Float3 {  -1.0f,  1.0f,  0.25f }, 0xff7f7fff, Float2 { 0.f, 1.f } },
		VertexPCT { Float3 {   1.0f,  1.0f,  0.25f }, 0xff7f7fff, Float2 { 1.f, 1.f } },
		VertexPCT { Float3 {  -1.0f,  0.5f,  0.25f }, 0xff7f7fff, Float2 { 0.f, 0.f } },

		// Counter clockwise-winding triangle
		VertexPCT { Float3 {   1.0f,  1.0f,  0.25f }, 0xff7f7fff, Float2 { 1.f, 1.f } },
		VertexPCT { Float3 {  -1.0f,  0.5f,  0.25f }, 0xff7f7fff, Float2 { 0.f, 0.f } },
		VertexPCT { Float3 {   1.0f,  0.5f,  0.25f }, 0xff7f7fff, Float2 { 1.f, 0.f } }
	};

	static VertexPCT vertices_middleQuad_Green[] = {
		// Clockwise-winding triangle
		VertexPCT { Float3 {  -0.7f,   0.7f,  0.5f }, 0xff7fff7f, Float2 { 0.f, 1.f } },
		VertexPCT { Float3 {   0.7f,   0.7f,  0.5f }, 0xff7fff7f, Float2 { 1.f, 1.f } },
		VertexPCT { Float3 {  -0.7f,  -0.7f,  0.5f }, 0xff7fff7f, Float2 { 0.f, 0.f } },

		// Counter clockwise-winding triangle
		VertexPCT { Float3 {   0.7f,   0.7f,  0.5f }, 0xff7fff7f, Float2 { 1.f, 1.f } },
		VertexPCT { Float3 {  -0.7f,  -0.7f,  0.5f }, 0xff7fff7f, Float2 { 0.f, 0.f } },
		VertexPCT { Float3 {   0.7f,  -0.7f,  0.5f }, 0xff7fff7f, Float2 { 1.f, 0.f } }
	};

	static VertexPCT vertices_stripe_Green[] = {
		// Clockwise-winding triangle
		VertexPCT { Float3 {  -0.1f,   1.0f,  0.5f }, 0xff7fff7f, Float2 { 0.f, 1.f } },
		VertexPCT { Float3 {   0.1f,   1.0f,  0.5f }, 0xff7fff7f, Float2 { 1.f, 1.f } },
		VertexPCT { Float3 {  -0.1f,  -1.0f,  0.5f }, 0xff7fff7f, Float2 { 0.f, 0.f } },

		// Counter clockwise-winding triangle
		VertexPCT { Float3 {   0.1f,   1.0f,  0.5f }, 0xff7fff7f, Float2 { 1.f, 1.f } },
		VertexPCT { Float3 {  -0.1f,  -1.0f,  0.5f }, 0xff7fff7f, Float2 { 0.f, 0.f } },
		VertexPCT { Float3 {   0.1f,  -1.0f,  0.5f }, 0xff7fff7f, Float2 { 1.f, 0.f } }
	};

	static VertexPCT vertices_fullViewport[] = {
		// Counter clockwise-winding triangle
		VertexPCT { Float3 {  -1.0f, -1.0f,  1.0f }, 0xffffffff, Float2 { 0.f, 0.f } },
		VertexPCT { Float3 {   1.0f, -1.0f,  1.0f }, 0xffffffff, Float2 { 1.f, 0.f } },
		VertexPCT { Float3 {  -1.0f,  1.0f,  1.0f }, 0xffffffff, Float2 { 0.f, 1.f } },

		// Counter clockwise-winding triangle
		VertexPCT { Float3 {  -1.0f,  1.0f,  1.0f }, 0xffffffff, Float2 { 0.f, 1.f } },
		VertexPCT { Float3 {   1.0f, -1.0f,  1.0f }, 0xffffffff, Float2 { 1.f, 0.f } },
		VertexPCT { Float3 {   1.0f,  1.0f,  1.0f }, 0xffffffff, Float2 { 1.f, 1.f } }
	};

	static RenderCore::InputElementDesc inputElePCT[] = {
		RenderCore::InputElementDesc { "position", 0, RenderCore::Format::R32G32B32_FLOAT },
		RenderCore::InputElementDesc { "color", 0, RenderCore::Format::R8G8B8A8_UNORM },
		RenderCore::InputElementDesc { "texCoord", 0, RenderCore::Format::R32G32_FLOAT }
	};

	static const char psText_DoubleInputAttachments[] = 
        HLSLPrefix
		R"(
			[[vk::input_attachment_index(0)]] SubpassInput<float4> SubpassInputAttachment0 : register(t2, space0);
			[[vk::input_attachment_index(1)]] SubpassInput<float> SubpassInputAttachment1 : register(t6, space0);

            float4 main() : SV_Target0
            {
                return SubpassInputAttachment1.SubpassLoad() * SubpassInputAttachment0.SubpassLoad();
            }
        )";

	TEST_CASE( "ComplexRenderPasses-SplitStencilDepthBuffer", "[rendercore_metal]" )
	{
		// Attempt to use a subpass where the depth aspect of an attachment is bound as an input attachment
		// while at the same time the stencil aspect is bound as a depth/stencil attachment
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();

		testHelper->BeginFrameCapture();

		std::vector<SubpassDesc> subpasses;
		std::vector<AttachmentDesc> attachments;
		attachments.push_back({Format::R16G16B16A16_FLOAT, 0, LoadStore::DontCare});		// gbuffer
		attachments.push_back({Format::D32_SFLOAT_S8_UINT, 0, LoadStore::Clear});			// main depth
		attachments.push_back({Format::R8G8B8A8_UNORM, 0, LoadStore::Clear});				// light resolve texture

		subpasses.push_back(SubpassDesc{}.AppendOutput(0).SetDepthStencil(1));

		TextureViewDesc justStencilWindow {
			TextureViewDesc::Aspect::Stencil,
			TextureViewDesc::All, TextureViewDesc::All,
			TextureDesc::Dimensionality::Undefined,
			TextureViewDesc::Flags::SimultaneouslyDepthReadOnly};

		TextureViewDesc justDepthWindow {
			TextureViewDesc::Aspect::Depth,
			TextureViewDesc::All, TextureViewDesc::All,
			TextureDesc::Dimensionality::Undefined,
			TextureViewDesc::Flags::SimultaneouslyStencilAttachment};
		subpasses.push_back(
			SubpassDesc{}
				.AppendOutput(2)
				.AppendInput(0)
				.AppendInput(1, justDepthWindow)
				.SetDepthStencil(1, justStencilWindow));

		FrameBufferDesc fbDesc{std::move(attachments), std::move(subpasses)};

		struct NamedAttachmentsHelper : public INamedAttachments
		{
			std::shared_ptr<IResourceView> GetResourceView(
				AttachmentName resName,
				BindFlag::Enum bindFlag, TextureViewDesc viewDesc,
				const AttachmentDesc& requestDesc, const FrameBufferProperties& props) override
			{
				return _viewPool.GetTextureView(_attachments[resName], bindFlag, viewDesc);
			}

			std::shared_ptr<IResource> _attachments[3];
			ViewPool _viewPool;
			NamedAttachmentsHelper(IDevice& device)
			{
				_attachments[0] = device.CreateResource(CreateDesc(BindFlag::RenderTarget|BindFlag::InputAttachment|BindFlag::TransferDst, TextureDesc::Plain2D(512, 512, Format::R16G16B16A16_FLOAT)), "attachment-0");
				_attachments[1] = device.CreateResource(CreateDesc(BindFlag::DepthStencil|BindFlag::InputAttachment|BindFlag::TransferDst, TextureDesc::Plain2D(512, 512, Format::D32_SFLOAT_S8_UINT)), "attachment-1");
				_attachments[2] = device.CreateResource(CreateDesc(BindFlag::RenderTarget|BindFlag::TransferDst|BindFlag::TransferSrc, TextureDesc::Plain2D(512, 512, Format::R8G8B8A8_UNORM)), "attachment-2");
			}
		};
		NamedAttachmentsHelper namedAttachmentsHelper(*testHelper->_device);

		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
		IResource* toComplete[] { namedAttachmentsHelper._attachments[0].get(), namedAttachmentsHelper._attachments[1].get(), namedAttachmentsHelper._attachments[2].get() };
		Metal::CompleteInitialization(metalContext, toComplete);

		auto fb = std::make_shared<Metal::FrameBuffer>(Metal::GetObjectFactory(*testHelper->_device), fbDesc, namedAttachmentsHelper);
		ClearValue clearValues[] { MakeClearValue(1.f, 0.f, 0.f, 1.f), MakeClearValue(0.f, 0), MakeClearValue(0.5f, 0.5f, 0.5f, 1.f) };
		metalContext.BeginRenderPass(*fb, clearValues);
		{
			// prime the attachments we're interested in
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(*testHelper->_pipelineLayout);

			auto shaderProgram = testHelper->MakeShaderProgram(vsText_clipInput, psText);
			encoder.Bind(shaderProgram);

			Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputElePCT), shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());
			encoder.Bind(inputLayout, Topology::TriangleList);

			encoder.Bind(RasterizationDesc{CullMode::None});
			DepthStencilDesc depthStencil;
			depthStencil._depthWrite = true;
			depthStencil._depthTest = CompareOp::GreaterEqual;
			depthStencil._stencilEnable = true;
			depthStencil._stencilWriteMask = 0xff;
			depthStencil._frontFaceStencil._passOp = StencilOp::Replace;
			depthStencil._backFaceStencil._passOp = StencilOp::Replace;
			encoder.Bind(depthStencil);
			encoder.SetStencilRef(0x80, 0x80);

			{
				auto vertexBuffer = testHelper->CreateVB(vertices_topQuad_Red);
				VertexBufferView vbv { vertexBuffer.get() };
				encoder.Bind(MakeIteratorRange(&vbv, &vbv+1), {});
				encoder.Draw((unsigned)dimof(vertices_topQuad_Red));
			}

			{
				auto vertexBuffer = testHelper->CreateVB(vertices_middleQuad_Green);
				VertexBufferView vbv { vertexBuffer.get() };
				encoder.Bind(MakeIteratorRange(&vbv, &vbv+1), {});
				encoder.Draw((unsigned)dimof(vertices_middleQuad_Green));
			}

			{
				encoder.SetStencilRef(0x30, 0x30);
				auto vertexBuffer = testHelper->CreateVB(vertices_stripe_Green);
				VertexBufferView vbv { vertexBuffer.get() };
				encoder.Bind(MakeIteratorRange(&vbv, &vbv+1), {});
				encoder.Draw((unsigned)dimof(vertices_middleQuad_Green));
			}
		}
		metalContext.BeginNextSubpass(*fb);
		{
			// this is the special subpass with stencil and depth bound in different ways
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(*testHelper->_pipelineLayout);

			auto shaderProgram = testHelper->MakeShaderProgram(vsText_clipInput, psText_DoubleInputAttachments);
			encoder.Bind(shaderProgram);

			Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputElePCT), shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());
			encoder.Bind(inputLayout, Topology::TriangleList);

			encoder.Bind(RasterizationDesc{CullMode::None});
			DepthStencilDesc depthStencil;
			depthStencil._depthWrite = false;
			depthStencil._depthTest = CompareOp::Always;
			depthStencil._stencilEnable = true;
			depthStencil._stencilReadMask = 0xff;
			depthStencil._frontFaceStencil._comparisonOp = CompareOp::Equal;
			depthStencil._backFaceStencil._comparisonOp = CompareOp::Equal;
			depthStencil._depthBoundsTestEnable = true;
			encoder.Bind(depthStencil);
			encoder.SetStencilRef(0x80, 0x80);
			encoder.SetDepthBounds(0.45f, 0.55f);

			auto srv0 = namedAttachmentsHelper.GetResourceView(0, BindFlag::InputAttachment, {}, {}, {});
			auto srv1 = namedAttachmentsHelper.GetResourceView(1, BindFlag::InputAttachment, justDepthWindow, {}, {});
			ResourceViewStream srvs { *srv0, *srv1 };
			UniformsStreamInterface usi;
			usi.BindResourceView(0, "SubpassInputAttachment0"_h);
			usi.BindResourceView(1, "SubpassInputAttachment1"_h);
			Metal::BoundUniforms boundUniforms(shaderProgram, usi);
			boundUniforms.ApplyLooseUniforms(metalContext, encoder, srvs);

			auto vertexBuffer = testHelper->CreateVB(vertices_fullViewport);
			VertexBufferView vbv { vertexBuffer.get() };
			encoder.Bind(MakeIteratorRange(&vbv, &vbv+1), {});

			encoder.Draw((unsigned)dimof(vertices_fullViewport));
		}
		metalContext.EndRenderPass();

		// In the second subpass, there were 3 input attachments that were important:
		//
		//	1. attachment-0, which contains color information
		//			green in the center, green along a vertical stripe in the middle of the image, red along the +Y edge in clip space, and undefined data around the other edges
		//	2. attachment-1 depth aspect
		//			depth .25 in the red part, .5 in the green part, and 1.0 otherwise
		//	3. attachment-2 stencil aspect
		//			0x80 in the red & green parts, except for the vertical stripe, which is 0x30. Also 0 in other parts of the image
		//
		//	The stencil aspect should stencil out the undefined parts & the vertical stripe.
		//	Then, depth bounds test is used to stencil out the red parts
		//  So, in the end we should be left with just the green color, multipled by the (constant) depth of the green part, and the color we cleared to.
		//
		// Ie, so we're testing the ability to do 3 things simultaneously:
		//		- hardware stencil test
		//		- read depth as an input attachment
		//		- depth bounds test

		auto breakdown = GetFullColorBreakdown(*threadContext, *namedAttachmentsHelper._attachments[2]);
		REQUIRE(breakdown.size() == 2);
		const unsigned expectedColor0 = (0x80 << 24u) | (0x3f << 16u) | (0x80 << 8u) | (0x3f);		// half green color (from VB)
		const unsigned expectedColor1 = (0xff << 24u) | (0x80 << 16u) | (0x80 << 8u) | (0x80);		// clear color
		REQUIRE(breakdown.begin()->first == expectedColor0);
		auto i = breakdown.begin(); ++i;
		REQUIRE(i->first == expectedColor1);

		testHelper->EndFrameCapture();
	}
}

