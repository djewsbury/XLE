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
#include "../../../RenderCore/ResourceDesc.h"
#include "../../../RenderCore/Format.h"
#include "../../../RenderCore/BufferView.h"
#include "../../../Math/Vector.h"
#include "../../../Utility/MemoryUtils.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
	class VertexPCT
	{
	public:
		Float3      _position;
		unsigned    _color;
		Float2      _texCoord;
	};

	static VertexPCT vertices_topLeftQuad[] = {
		// Clockwise-winding triangle
		VertexPCT { Float3 {  -1.0f,  1.0f,  0.0f }, 0xffffffff, Float2 { 0.f, 1.f } },
		VertexPCT { Float3 {   1.0f,  1.0f,  0.0f }, 0xffffffff, Float2 { 1.f, 1.f } },
		VertexPCT { Float3 {  -1.0f,  0.5f,  0.0f }, 0xffffffff, Float2 { 0.f, 0.f } },

		// Counter clockwise-winding triangle
		VertexPCT { Float3 {   1.0f,  1.0f,  0.0f }, 0xffffffff, Float2 { 1.f, 1.f } },
		VertexPCT { Float3 {  -1.0f,  0.5f,  0.0f }, 0xffffffff, Float2 { 0.f, 0.f } },
		VertexPCT { Float3 {   1.0f,  0.5f,  0.0f }, 0xffffffff, Float2 { 1.f, 0.f } }
	};

	static VertexPCT vertices_fullViewport[] = {
		// Counter clockwise-winding triangle
		VertexPCT { Float3 {  -1.0f, -1.0f,  0.0f }, 0xffffffff, Float2 { 0.f, 0.f } },
		VertexPCT { Float3 {   1.0f, -1.0f,  0.0f }, 0xffffffff, Float2 { 1.f, 0.f } },
		VertexPCT { Float3 {  -1.0f,  1.0f,  0.0f }, 0xffffffff, Float2 { 0.f, 1.f } },

		// Counter clockwise-winding triangle
		VertexPCT { Float3 {  -1.0f,  1.0f,  0.0f }, 0xffffffff, Float2 { 0.f, 1.f } },
		VertexPCT { Float3 {   1.0f, -1.0f,  0.0f }, 0xffffffff, Float2 { 1.f, 0.f } },
		VertexPCT { Float3 {   1.0f,  1.0f,  0.0f }, 0xffffffff, Float2 { 1.f, 1.f } }
	};

	static VertexPCT vertices_topLeftQuad_Red[] = {
		VertexPCT { Float3 {  -1.0f,  0.0f,  0.0f }, 0xff0000ff, Float2 { 0.f, 0.f } },
		VertexPCT { Float3 {   1.0f,  0.0f,  0.0f }, 0xff0000ff, Float2 { 1.f, 0.f } },
		VertexPCT { Float3 {   1.0f,  1.0f,  0.0f }, 0xff0000ff, Float2 { 1.f, 1.f } },

		VertexPCT { Float3 {   1.0f,  1.0f,  0.0f }, 0xff0000ff, Float2 { 1.f, 1.f } },
		VertexPCT { Float3 {  -1.0f,  1.0f,  0.0f }, 0xff0000ff, Float2 { 0.f, 1.f } },
		VertexPCT { Float3 {  -1.0f,  0.0f,  0.0f }, 0xff0000ff, Float2 { 0.f, 0.f } },
	};

	static VertexPCT vertices_bottomLeftQuad_Blue[] = {
		VertexPCT { Float3 {  -1.0f, -1.0f,  0.0f }, 0xffff0000, Float2 { 0.f, 0.f } },
		VertexPCT { Float3 {   1.0f, -1.0f,  0.0f }, 0xffff0000, Float2 { 1.f, 0.f } },
		VertexPCT { Float3 {   1.0f,  0.0f,  0.0f }, 0xffff0000, Float2 { 1.f, 1.f } },

		VertexPCT { Float3 {   1.0f,  0.0f,  0.0f }, 0xffff0000, Float2 { 1.f, 1.f } },
		VertexPCT { Float3 {  -1.0f,  0.0f,  0.0f }, 0xffff0000, Float2 { 0.f, 1.f } },
		VertexPCT { Float3 {  -1.0f, -1.0f,  0.0f }, 0xffff0000, Float2 { 0.f, 0.f } },
	};

	static RenderCore::InputElementDesc inputElePCT[] = {
		RenderCore::InputElementDesc { "position", 0, RenderCore::Format::R32G32B32_FLOAT },
		RenderCore::InputElementDesc { "color", 0, RenderCore::Format::R8G8B8A8_UNORM },
		RenderCore::InputElementDesc { "texCoord", 0, RenderCore::Format::R32G32_FLOAT }
	};

	void RenderQuad(
		MetalTestHelper& testHelper,
		RenderCore::Metal::DeviceContext& metalContext,
		RenderCore::Metal::GraphicsEncoder_ProgressivePipeline& encoder,
		IteratorRange<const VertexPCT*> vertices,
		const RenderCore::RasterizationDesc& rasterizationDesc,
		const RenderCore::IResourceView* srv = nullptr,
		const RenderCore::ISampler* samplerState = nullptr)
	{
		auto shaderProgram = testHelper.MakeShaderProgram(vsText_clipInput, psText);

		using namespace RenderCore;
		auto vertexBuffer = testHelper.CreateVB(vertices);

		// Using the InputElementDesc version of BoundInputLayout constructor
		Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputElePCT), shaderProgram);
		assert(inputLayout.AllAttributesBound());

		if (srv) {
			UniformsStreamInterface usi;
			usi.BindResourceView(0, Hash64("Texture"));
			usi.BindSampler(0, Hash64("Texture_sampler"));
			Metal::BoundUniforms uniforms { shaderProgram, usi };

			const IResourceView* resourceViews[] = { srv };
			const ISampler* samplers[] = { samplerState };
			UniformsStream uniformsStream;
			uniformsStream._resourceViews = resourceViews;
			uniformsStream._samplers = samplers;
			uniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);
		}

		encoder.Bind(shaderProgram);
		encoder.Bind(inputLayout, Topology::TriangleList);
		VertexBufferView vbv { vertexBuffer.get() };
		encoder.Bind(MakeIteratorRange(&vbv, &vbv+1), {});
		encoder.Bind(rasterizationDesc);
		encoder.Draw((unsigned)vertices.size());
	}

	TEST_CASE( "CoordinateSpaces-WindowCoordSpaceOrientation", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Render some pattern in clip coordinate space to check the orientation and
		// arrangement of window coordinate space
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc,
			TextureDesc::Plain2D(64, 64, Format::R8G8B8A8_UNORM),
			"temporary-out");
		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);

		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		{
			auto rpi = fbHelper.BeginRenderPass(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			RenderQuad(*testHelper, metalContext, encoder, MakeIteratorRange(vertices_topLeftQuad), RasterizationDesc{CullMode::None});
		}

		auto data = fbHelper.GetMainTarget()->ReadBackSynchronized(*threadContext);
		unsigned lastPixel = *(unsigned*)PtrAdd(AsPointer(data.end()), -(ptrdiff_t)sizeof(unsigned));
		unsigned firstPixel = *(unsigned*)data.data();

		// The orientation of window coordinate space is different in GLES vs other APIs
		// In this test, we draw a horizontal strip between Y = 1.0 and y = 0.5 in clip space
		// In most APIs, when we readback from the texture, this is at the start of texture
		// memory, but in OpenGLES / OpenGL, it's at the end
		#if GFXAPI_TARGET == GFXAPI_OPENGLES
			REQUIRE(firstPixel == 0xff000000);
			REQUIRE(lastPixel == 0xffffffff);
		#else
			REQUIRE(firstPixel == 0xffffffff);
			REQUIRE(lastPixel == 0xff000000);
		#endif
		(void)firstPixel; (void)lastPixel;
	}

	TEST_CASE( "CoordinateSpaces-ScissorRect", "[rendercore_metal]" )
	{
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc,
			TextureDesc::Plain2D(64, 64, Format::R8G8B8A8_UNORM),
			"temporary-out");
		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);

		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		{
			auto rpi = fbHelper.BeginRenderPass(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			RenderQuad(*testHelper, metalContext, encoder, MakeIteratorRange(vertices_topLeftQuad_Red), RasterizationDesc{CullMode::None});
			RenderQuad(*testHelper, metalContext, encoder, MakeIteratorRange(vertices_bottomLeftQuad_Blue), RasterizationDesc{CullMode::None});
		}

		auto breakdown0 = fbHelper.GetFullColorBreakdown(*threadContext);

		// For scissor rect, draw red on top half of screen and blue on bottom.
		REQUIRE(breakdown0.size() == (size_t)2);
		REQUIRE(breakdown0[0xffff0000] == breakdown0[0xff0000ff]);

		auto SetScissorRect = [&](RenderCore::Metal::GraphicsEncoder_ProgressivePipeline& encoder, float x, float y, float w, float h, bool originIsUpperLeft)
		{
			RenderCore::ViewportDesc viewports[1];
			viewports[0] = RenderCore::ViewportDesc{ 0.f, 0.f, (float)targetDesc._textureDesc._width, (float)targetDesc._textureDesc._height };
			viewports[0]._originIsUpperLeft = originIsUpperLeft;
			RenderCore::ScissorRect scissorRects[1];
			scissorRects[0] = RenderCore::ScissorRect{ (int)x, (int)y, (unsigned)w, (unsigned)h };
			scissorRects[0]._originIsUpperLeft = originIsUpperLeft;
			encoder.Bind(MakeIteratorRange(viewports), MakeIteratorRange(scissorRects));
		};

		auto TestScissor = [&](float x, float y, float w, float h, bool originIsUpperLeft)
		{
			auto rpi = fbHelper.BeginRenderPass(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			RenderQuad(*testHelper, metalContext, encoder, MakeIteratorRange(vertices_topLeftQuad_Red), RasterizationDesc{CullMode::None});
			RenderQuad(*testHelper, metalContext, encoder, MakeIteratorRange(vertices_bottomLeftQuad_Blue), RasterizationDesc{CullMode::None});
			SetScissorRect(encoder, x, y, w, h, originIsUpperLeft);
			RenderQuad(*testHelper, metalContext, encoder, MakeIteratorRange(vertices_fullViewport), RasterizationDesc{CullMode::None});
		};

		{
			// Setting scissor rect outside renderpass will fail with assertion
			//SetScissorRect(16, 48, 32, 16);
		}

		// Set scissor rect to include part of top half of screen.
		// Draw white full screen.  Readback framebuffer.
		// The ratio of white to full screen should be size of scissor rect relative to full framebuffer size.
		// The ratio of red should be less than blue.
		{
			// {16, 48, 32, 16} with origin at lower-left should result in less 0xff0000ff (red) than 0xffff0000 (blue)
			TestScissor(16, 48, 32, 16, false);
			auto breakdown = fbHelper.GetFullColorBreakdown(*threadContext);
			REQUIRE((size_t)breakdown[0xffffffff] == (size_t)32*16);
			REQUIRE(breakdown[0xff0000ff] < breakdown[0xffff0000]);
		}
		{
			// {16, 24, 32, 16} with origin at lower-left should result in equal 0xffff0000 (blue) and 0xff0000ff (red)
			TestScissor(16, 24, 32, 16, false);
			auto breakdown = fbHelper.GetFullColorBreakdown(*threadContext);
			REQUIRE(breakdown[0xff0000ff] == breakdown[0xffff0000]);
		}
		{
			// {16, 24, 32, 16} with origin at upper-left should result in equal 0xffff0000 (blue) and 0xff0000ff (red)
			TestScissor(16, 24, 32, 16, true);
			auto breakdown = fbHelper.GetFullColorBreakdown(*threadContext);
			REQUIRE(breakdown[0xff0000ff] == breakdown[0xffff0000]);
		}
		{
			// {16, 40, 32, 16} with origin at upper-left should result in less 0xffff0000 (blue) than 0xff0000ff (red)
			TestScissor(16, 40, 32, 16, true);
			auto breakdown = fbHelper.GetFullColorBreakdown(*threadContext);
			REQUIRE(breakdown[0xffff0000] < breakdown[0xff0000ff]);
		}
		{
			// {0, 32, 64, 32} with origin at lower-left should have no 0xff0000ff (red)
			TestScissor(0, 32, 64, 32, false);
			auto breakdown = fbHelper.GetFullColorBreakdown(*threadContext);
			REQUIRE(breakdown.size() == (size_t)2);
			REQUIRE((size_t)breakdown[0xffffffff] == (size_t)64*32);
			REQUIRE((size_t)breakdown[0xffff0000] == (size_t)64*32);
		}
		{
			// {0, 32, 64, 32} with origin at upper-left should have no 0xffff0000 (blue)
			TestScissor(0, 32, 64, 32, true);
			auto breakdown = fbHelper.GetFullColorBreakdown(*threadContext);
			REQUIRE(breakdown.size() == (size_t)2);
			REQUIRE((size_t)breakdown[0xffffffff] == (size_t)64*32);
			REQUIRE((size_t)breakdown[0xff0000ff] == (size_t)64*32);
		}

		// Test for scissor rect outside of framebuffer bounds.
		// We may be clipping to framebuffer bounds, so there should be no validation errors.
		{
			// origin is lower-left
			auto rpi = fbHelper.BeginRenderPass(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			REQUIRE_THROWS(
				[SetScissorRect, &encoder]() {
					SetScissorRect(encoder, 0, 0, 0, 0, false); // zero size, we throw in this case
				}());
			SetScissorRect(encoder, 0, 0, 64, 64, false); // full-frame
			SetScissorRect(encoder, -32, 0, 64, 64, false); // outside left
			//SetScissorRect(32, 0, 64, 64, false); // outside right (Metal validation error)
			//SetScissorRect(0, 32, 64, 64, false); // outside top (Metal validation error)
			SetScissorRect(encoder, 0, -32, 64, 64, false); // outside bottom

			SetScissorRect(encoder, 32, 0, -32, 64, false); // negative width
			SetScissorRect(encoder, 0, 32, 64, -32, false); // negative height
		}
	}

	TEST_CASE( "CoordinateSpaces-WindowCoordSpaceWindingOrder", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Render a quad to check the impact of window coordinates on winding oder
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);

		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc,
			TextureDesc::Plain2D(64, 64, Format::R8G8B8A8_UNORM),
			"temporary-out");
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		{
			auto rpi = fbHelper.BeginRenderPass(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			RenderQuad(*testHelper, metalContext, encoder, MakeIteratorRange(vertices_fullViewport), RasterizationDesc{CullMode::Back, FaceWinding::CCW});
		}
		auto breakdown0 = fbHelper.GetFullColorBreakdown(*threadContext);

		{
			auto rpi = fbHelper.BeginRenderPass(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			RenderQuad(*testHelper, metalContext, encoder, MakeIteratorRange(vertices_fullViewport), RasterizationDesc{CullMode::Back, FaceWinding::CW});
		}
		auto breakdown1 = fbHelper.GetFullColorBreakdown(*threadContext);

		// The differences in the window coordinate definition does not impact the winding
		// mode. Even though the handiness of window coordinates is different, the winding
		// order calculate is determined in cull space.
		REQUIRE(breakdown0.size() == (size_t)1);
		REQUIRE(breakdown0.begin()->first == 0xffffffff);
		REQUIRE(breakdown1.size() == (size_t)1);
		REQUIRE(breakdown1.begin()->first == 0xff000000);
		(void)breakdown0; (void)breakdown1;

		// If we draw only one triangle of the full screen quad, we will draw to approximately
		// half the screen. It's not exactly half, though, because of the rules for when a
		// triangle goes through the center of a pixel. We draw to slightly fewer than half
		// of the pixels. And we should get the same results regardless of API and regardless
		// of window coordinate space definition
		{
			auto rpi = fbHelper.BeginRenderPass(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			RenderQuad(*testHelper, metalContext, encoder, MakeIteratorRange(vertices_fullViewport, &vertices_fullViewport[3]), RasterizationDesc{CullMode::None});
		}
		auto breakdown2 = fbHelper.GetFullColorBreakdown(*threadContext);

		REQUIRE(breakdown2.size() == (size_t)2);
		REQUIRE(breakdown2[0xff000000] == 2080u);
		REQUIRE(breakdown2[0xffffffff] == unsigned((64*64) - 2080));

#if 0 /* To avoid confusion that might stem from flipped viewports, we will disallow them */
		// If we put a flip on the viewport (but leave everything else alone), then the handiness
		// of the winding order is actually flipped.
		// This should actually generate an error in OpenGL, where negative viewport heights are
		// not supported
		#if GFXAPI_TARGET != GFXAPI_OPENGLES
			{
				auto rpi = fbHelper.BeginRenderPass(*threadContext);
				RenderCore::Viewport viewports[1];
				viewports[0] = RenderCore::Viewport{ 0.f, (float)targetDesc._textureDesc._height, (float)targetDesc._textureDesc._width, -(float)targetDesc._textureDesc._height };
				viewports[0].OriginIsUpperLeft = false;
				RenderCore::ScissorRect scissorRects[1];
				scissorRects[0] = RenderCore::ScissorRect{ 0, 0, targetDesc._textureDesc._width, targetDesc._textureDesc._height };
				scissorRects[0].OriginIsUpperLeft = false;
				metalContext.SetViewportAndScissorRects(MakeIteratorRange(viewports), MakeIteratorRange(scissorRects));
				RenderQuad(*testHelper, metalContext, MakeIteratorRange(vertices_fullViewport), RasterizationDesc{CullMode::Back, FaceWinding::CW});
			}
			auto breakdown3 = fbHelper.GetFullColorBreakdown(*threadContext);
			REQUIRE(breakdown3.size() == (size_t)1);
			REQUIRE(breakdown3.begin()->first == 0xffffffff);
		#endif
#endif
	}

	TEST_CASE( "CoordinateSpaces-RenderCopyThenReadback", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Test coordinate space consequences when we render, copy with a draw operation
		// and then readback
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);

		auto targetDesc0 = CreateDesc(
			BindFlag::RenderTarget|BindFlag::ShaderResource|BindFlag::TransferSrc,
			TextureDesc::Plain2D(64, 64, Format::R8G8B8A8_UNORM),
			"temporary-out0");
		UnitTestFBHelper fbHelper0(*testHelper->_device, *threadContext, targetDesc0);
		{
			auto rpi = fbHelper0.BeginRenderPass(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			RenderQuad(*testHelper, metalContext, encoder, MakeIteratorRange(vertices_topLeftQuad), RasterizationDesc{CullMode::None});
		}

		auto targetDesc1 = CreateDesc(
			BindFlag::RenderTarget|BindFlag::TransferSrc,
			TextureDesc::Plain2D(64, 64, Format::R8G8B8A8_UNORM),
			"temporary-out1");
		UnitTestFBHelper fbHelper1(*testHelper->_device, *threadContext, targetDesc1);
		{
			auto rpi = fbHelper1.BeginRenderPass(*threadContext);
			auto srv = fbHelper0.GetMainTarget()->CreateTextureView(BindFlag::ShaderResource);
			auto sampler = testHelper->_device->CreateSampler(SamplerDesc{ FilterMode::Point });
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			RenderQuad(*testHelper, metalContext, encoder, MakeIteratorRange(vertices_topLeftQuad), RasterizationDesc{CullMode::None}, srv.get(), sampler.get());
		}

		// The data in fpHelper1 is should now be the same as what we got through the
		// WindowCoordSpaceOrientation test; except that we've added another copy in the middle
		// The copy is done via a draw operation, with this orientation
		//      clip space { -1, -1, 0, 1 } maps to tex coord { 0, 0 }
		//      clip space {  1,  1, 0, 1 } maps to tex coord { 1, 1 }
		auto data0 = fbHelper0.GetMainTarget()->ReadBackSynchronized(*threadContext);
		auto data1 = fbHelper1.GetMainTarget()->ReadBackSynchronized(*threadContext);
		unsigned lastPixel0 = *(unsigned*)PtrAdd(AsPointer(data0.end()), -(ptrdiff_t)sizeof(unsigned));
		unsigned firstPixel0 = *(unsigned*)data0.data();
		unsigned lastPixel1 = *(unsigned*)PtrAdd(AsPointer(data1.end()), -(ptrdiff_t)sizeof(unsigned));
		unsigned firstPixel1 = *(unsigned*)data1.data();

		// This is the same test as WindowCoordSpaceOrientation above
		#if GFXAPI_TARGET == GFXAPI_OPENGLES
			REQUIRE(firstPixel0 == 0xff000000);
			REQUIRE(lastPixel0 == 0xffffffff);
		#else
			REQUIRE(firstPixel0 == 0xffffffff);
			REQUIRE(lastPixel0 == 0xff000000);
		#endif

		// Now, test the contents of the texture we've copied into. Note that there's no
		// flip here, on either API
		#if GFXAPI_TARGET == GFXAPI_OPENGLES
			REQUIRE(firstPixel1 == 0xff000000);
			REQUIRE(lastPixel1 == 0xffffffff);
		#else
			REQUIRE(firstPixel1 == 0xffffffff);
			REQUIRE(lastPixel1 == 0xff000000);
		#endif

		(void)lastPixel0; (void)firstPixel0;
		(void)lastPixel1; (void)firstPixel1;
	}

	TEST_CASE( "CoordinateSpaces-RenderBltAndThenReadback", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Test coordinate space consequences when we render, copy with a blit
		// and then readback
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);

		auto targetDesc0 = CreateDesc(
			BindFlag::RenderTarget|BindFlag::TransferSrc,
			TextureDesc::Plain2D(64, 64, Format::R8G8B8A8_UNORM),
			"temporary-out0");
		UnitTestFBHelper fbHelper0(*testHelper->_device, *threadContext, targetDesc0);
		{
			auto rpi = fbHelper0.BeginRenderPass(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			RenderQuad(*testHelper, metalContext, encoder, MakeIteratorRange(vertices_topLeftQuad), RasterizationDesc{CullMode::None});
		}

		auto targetDesc1 = CreateDesc(
			BindFlag::RenderTarget|BindFlag::TransferDst,
			TextureDesc::Plain2D(64, 64, Format::R8G8B8A8_UNORM),
			"temporary-out1");
		UnitTestFBHelper fbHelper1(*testHelper->_device, *threadContext, targetDesc1);
		{
			auto blitPass = metalContext.BeginBlitEncoder();
			blitPass.Copy(
				CopyPartial_Dest { *fbHelper1.GetMainTarget() },
				CopyPartial_Src { *fbHelper0.GetMainTarget(), {}, {0,0,0}, {64, 64, 1} });
		}

		// The data in fpHelper1 is should now be the same as what we got through the
		// WindowCoordSpaceOrientation test; except that we've added another copy in the middle
		// The copy is done via a full texture blit operation
		auto data0 = fbHelper0.GetMainTarget()->ReadBackSynchronized(*threadContext);
		auto data1 = fbHelper1.GetMainTarget()->ReadBackSynchronized(*threadContext);
		unsigned lastPixel0 = *(unsigned*)PtrAdd(AsPointer(data0.end()), -(ptrdiff_t)sizeof(unsigned));
		unsigned firstPixel0 = *(unsigned*)data0.data();
		unsigned lastPixel1 = *(unsigned*)PtrAdd(AsPointer(data1.end()), -(ptrdiff_t)sizeof(unsigned));
		unsigned firstPixel1 = *(unsigned*)data1.data();

		// This is the same test as WindowCoordSpaceOrientation above
		#if GFXAPI_TARGET == GFXAPI_OPENGLES
			REQUIRE(firstPixel0 == 0xff000000);
			REQUIRE(lastPixel0 == 0xffffffff);
		#else
			REQUIRE(firstPixel0 == 0xffffffff);
			REQUIRE(lastPixel0 == 0xff000000);
		#endif

		// Now, test the contents of the texture we've copied into. Note that there's no
		// flip here, on either API
		#if GFXAPI_TARGET == GFXAPI_OPENGLES
			REQUIRE(firstPixel1 == 0xff000000);
			REQUIRE(lastPixel1 == 0xffffffff);
		#else
			REQUIRE(firstPixel1 == 0xffffffff);
			REQUIRE(lastPixel1 == 0xff000000);
		#endif

		(void)lastPixel0; (void)firstPixel0;
		(void)lastPixel1; (void)firstPixel1;
	}

}

