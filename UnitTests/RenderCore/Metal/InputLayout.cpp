// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MetalTestHelper.h"
#include "MetalTestShaders.h"
#include "../../../RenderCore/Metal/Shader.h"
#include "../../../RenderCore/Metal/InputLayout.h"
#include "../../../RenderCore/Metal/State.h"
#include "../../../RenderCore/Metal/PipelineLayout.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/Metal/TextureView.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
#include "../../../RenderCore/ResourceDesc.h"
#include "../../../RenderCore/Format.h"
#include "../../../RenderCore/ResourceUtils.h"
#include "../../../RenderCore/BufferView.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../RenderCore/IAnnotator.h"
#include "../../../Math/Vector.h"
#include "../../../Utility/MemoryUtils.h"
#include <map>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
	static bool ComponentsMatch(uint32_t c1, uint32_t c2) {
		return (c1 == c2 || c1+1 == c2 || c1 == c2+1);
	}

	static bool ColorsMatch(uint32_t c1, uint32_t c2) {
		unsigned char *p1 = reinterpret_cast<unsigned char *>(&c1);
		unsigned char *p2 = reinterpret_cast<unsigned char *>(&c2);
		return (
			ComponentsMatch(p1[0], p2[0]) &&
			ComponentsMatch(p1[1], p2[1]) &&
			ComponentsMatch(p1[2], p2[2]) &&
			ComponentsMatch(p1[3], p2[3])
		);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////
			//    T E S T   I N P U T   D A T A

	class VertexPC
	{
	public:
		Float4      _position;
		unsigned    _color;
	};

	static const unsigned fixedColors[] = { 0xff7f7f7fu, 0xff007f7fu, 0xff7f0000u, 0xff7f007fu };

	static VertexPC vertices_randomTriangle[] = {
		VertexPC { Float4 {  -0.25f, -0.5f,  0.0f,  1.0f }, fixedColors[0] },
		VertexPC { Float4 {  -0.33f,  0.1f,  0.0f,  1.0f }, fixedColors[0] },
		VertexPC { Float4 {   0.33f, -0.2f,  0.0f,  1.0f }, fixedColors[0] },

		VertexPC { Float4 { -0.1f, -0.7f, 0.0f, 1.0f }, fixedColors[1] },
		VertexPC { Float4 {  0.5f, -0.4f, 0.0f, 1.0f }, fixedColors[1] },
		VertexPC { Float4 {  0.8f,  0.8f, 0.0f, 1.0f }, fixedColors[1] },

		VertexPC { Float4 { 0.25f, -0.6f, 0.0f, 1.0f }, fixedColors[2] },
		VertexPC { Float4 { 0.75f,  0.1f, 0.0f, 1.0f }, fixedColors[2] },
		VertexPC { Float4 { 0.4f,   0.7f, 0.0f, 1.0f }, fixedColors[2] }
	};

	static RenderCore::InputElementDesc inputElePC[] = {
		RenderCore::InputElementDesc { "position", 0, RenderCore::Format::R32G32B32A32_FLOAT },
		RenderCore::InputElementDesc { "color", 0, RenderCore::Format::R8G8B8A8_UNORM }
	};

	static RenderCore::MiniInputElementDesc miniInputElePC[] = {
		{ Hash64("position"), RenderCore::Format::R32G32B32A32_FLOAT },
		{ Hash64("color"), RenderCore::Format::R8G8B8A8_UNORM }
	};

	static unsigned boxesSize = (96 - 32) * (96 - 32);
	static Int2 boxOffsets[] = { Int2(0, 0), Int2(768, 0), Int2(0, 768), Int2(768, 768) };
	static Int2 vertices_4Boxes[] = {
		Int2(32, 32), Int2(32, 96), Int2(96, 32), 
		Int2(96, 32), Int2(32, 96), Int2(96, 96),

		Int2(768 + 32, 32), Int2(768 + 32, 96), Int2(768 + 96, 32), 
		Int2(768 + 96, 32), Int2(768 + 32, 96), Int2(768 + 96, 96),

		Int2(32, 768 + 32), Int2(32, 768 + 96), Int2(96, 768 + 32),
		Int2(96, 768 + 32), Int2(32, 768 + 96), Int2(96, 768 + 96),

		Int2(768 + 32, 768 + 32), Int2(768 + 32, 768 + 96), Int2(768 + 96, 768 + 32),
		Int2(768 + 96, 768 + 32), Int2(768 + 32, 768 + 96), Int2(768 + 96, 768 + 96)
	};

	static unsigned vertices_colors[] = {
		fixedColors[0], fixedColors[0], fixedColors[0], fixedColors[0], fixedColors[0], fixedColors[0],
		fixedColors[1], fixedColors[1], fixedColors[1], fixedColors[1], fixedColors[1], fixedColors[1],
		fixedColors[2], fixedColors[2], fixedColors[2], fixedColors[2], fixedColors[2], fixedColors[2],
		fixedColors[3], fixedColors[3], fixedColors[3], fixedColors[3], fixedColors[3], fixedColors[3]
	};

	static RenderCore::InputElementDesc inputEleVIdx[] = {
		RenderCore::InputElementDesc { "vertexID", 0, RenderCore::Format::R32_SINT }
	};

	static unsigned vertices_vIdx[] = { 0, 1, 2, 3 };

	struct Values
	{
		float A, B, C;
		unsigned dummy;
		Float4 vA;
	};

	const RenderCore::ConstantBufferElementDesc ConstantBufferElementDesc_Values[] {
		RenderCore::ConstantBufferElementDesc { Hash64("A"), RenderCore::Format::R32_FLOAT, offsetof(Values, A) },
		RenderCore::ConstantBufferElementDesc { Hash64("B"), RenderCore::Format::R32_FLOAT, offsetof(Values, B) },
		RenderCore::ConstantBufferElementDesc { Hash64("C"), RenderCore::Format::R32_FLOAT, offsetof(Values, C) },
		RenderCore::ConstantBufferElementDesc { Hash64("vA"), RenderCore::Format::R32G32B32A32_FLOAT, offsetof(Values, vA) }
	};

////////////////////////////////////////////////////////////////////////////////////////////////////

	struct ColorBreakdown
	{
		unsigned _blackPixels = 0;
		unsigned _coloredPixels[dimof(fixedColors)] = {};
		unsigned _otherPixels = 0;
	};

	static ColorBreakdown GetColorBreakdown(RenderCore::IThreadContext& threadContext, UnitTestFBHelper& fbHelper)
	{
		ColorBreakdown result;

		auto data = fbHelper.GetMainTarget()->ReadBackSynchronized(threadContext);

		assert(data.size() == (size_t)RenderCore::ByteCount(fbHelper.GetMainTarget()->GetDesc()));
		auto pixels = MakeIteratorRange((unsigned*)AsPointer(data.begin()), (unsigned*)AsPointer(data.end()));
		for (auto p:pixels) {
			if (p == 0xff000000) { ++result._blackPixels; continue; }

			for (unsigned c=0; c<dimof(fixedColors); ++c) {
				if (p == fixedColors[c])
					++result._coloredPixels[c];
			}
		}
		result._otherPixels = (unsigned)pixels.size() - result._blackPixels;
		for (unsigned c=0; c<dimof(fixedColors); ++c) result._otherPixels -= result._coloredPixels[c];

		return result;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////
			//    C O D E

	TEST_CASE( "InputLayout-BasicBinding_LongForm", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Bind some geometry and render it using the "InputElementDesc" version of the
		// BoundInputLayout constructor
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgram = testHelper->MakeShaderProgram(vsText_clipInput, psText);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc | BindFlag::TransferDst, 0, GPUAccess::Write,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");

		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		
		/*Metal::Internal::SetupInitialLayout(
			*Metal::DeviceContext::Get(*threadContext),
			*fbHelper.GetMainTarget());*/

		{
			auto stagingDesc = CreateDesc(
				BindFlag::TransferSrc, 0, 0,
				TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
				"staging-temp");
			std::vector<uint8_t> initBuffer(RenderCore::ByteCount(stagingDesc), 0xdd);
			SubResourceInitData initData { MakeIteratorRange(initBuffer), MakeTexturePitches(stagingDesc._textureDesc) };
			auto stagingRes = testHelper->_device->CreateResource(stagingDesc, initData);
			auto blt = Metal::DeviceContext::Get(*threadContext)->BeginBlitEncoder();
			blt.Copy(*fbHelper.GetMainTarget(), *stagingRes);
		}

		auto rpi = fbHelper.BeginRenderPass(*threadContext);

		////////////////////////////////////////////////////////////////////////////////////////
		{
			auto vertexBuffer = testHelper->CreateVB(MakeIteratorRange(vertices_randomTriangle));

			// Using the InputElementDesc version of BoundInputLayout constructor
			Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputElePC), shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());

			VertexBufferView vbv { vertexBuffer.get() };
			auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);

			encoder.Bind(shaderProgram);
			encoder.Bind(inputLayout, Topology::TriangleList);
			encoder.Bind(MakeIteratorRange(&vbv, &vbv+1), {});
			encoder.Draw(dimof(vertices_randomTriangle));
		}
		////////////////////////////////////////////////////////////////////////////////////////

		rpi = {};     // end RPI

		auto colorBreakdown = GetColorBreakdown(*threadContext, fbHelper);
		REQUIRE(colorBreakdown._otherPixels == 0u);
		(void)colorBreakdown;
	}

	TEST_CASE( "InputLayout-BasicBinding_ShortForm", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Bind some geometry and render it using the "MiniInputElementDesc" version of the
		// BoundInputLayout constructor
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgram = testHelper->MakeShaderProgram(vsText_clipInput, psText);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");

		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		auto rpi = fbHelper.BeginRenderPass(*threadContext);

		////////////////////////////////////////////////////////////////////////////////////////
		{
			auto vertexBuffer = testHelper->CreateVB(MakeIteratorRange(vertices_randomTriangle));

			// Using the MiniInputElementDesc version of BoundInputLayout constructor
			Metal::BoundInputLayout::SlotBinding slotBinding { MakeIteratorRange(miniInputElePC), 0 };
			Metal::BoundInputLayout inputLayout(MakeIteratorRange(&slotBinding, &slotBinding+1), shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());

			VertexBufferView vbv { vertexBuffer.get() };
			auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);

			encoder.Bind(shaderProgram);
			encoder.Bind(inputLayout, Topology::TriangleList);
			encoder.Bind(MakeIteratorRange(&vbv, &vbv+1), {});
			encoder.Draw(dimof(vertices_randomTriangle));
		}
		////////////////////////////////////////////////////////////////////////////////////////

		rpi = {};     // end RPI

		auto colorBreakdown = GetColorBreakdown(*threadContext, fbHelper);
		REQUIRE(colorBreakdown._otherPixels == 0u);
		REQUIRE(colorBreakdown._blackPixels < 1024*1024);
		(void)colorBreakdown;
	}

	TEST_CASE( "InputLayout-BasicBinding_2VBs", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Bind some geometry and render it using both the "InputElementDesc" the and
		// "MiniInputElementDesc" versions of the BoundInputLayout constructor, but with 2
		// separate vertex buffers (each containing a different geometry stream)
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgram = testHelper->MakeShaderProgram(vsText, psText);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");

		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);

		auto vertexBuffer0 = testHelper->CreateVB(MakeIteratorRange(vertices_4Boxes));
		auto vertexBuffer1 = testHelper->CreateVB(MakeIteratorRange(vertices_colors));

		////////////////////////////////////////////////////////////////////////////////////////
		{
			auto rpi = fbHelper.BeginRenderPass(*threadContext);

			InputElementDesc inputEles[] = {
				InputElementDesc { "position", 0, Format::R32G32_SINT, 0 },
				InputElementDesc { "color", 0, Format::R8G8B8A8_UNORM, 1 }
			};

			// Using the InputElementDesc version of BoundInputLayout constructor
			Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputEles), shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());

			VertexBufferView vbvs[] {
				VertexBufferView { vertexBuffer0.get() },
				VertexBufferView { vertexBuffer1.get() }
			};
			auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			encoder.Bind(shaderProgram);
			encoder.Bind(inputLayout, Topology::TriangleList);
			encoder.Bind(MakeIteratorRange(vbvs), {});
			encoder.Draw(dimof(vertices_4Boxes));
		}
		////////////////////////////////////////////////////////////////////////////////////////

		auto colorBreakdown = GetColorBreakdown(*threadContext, fbHelper);
		REQUIRE(colorBreakdown._otherPixels == 0u);
		REQUIRE(colorBreakdown._coloredPixels[0] == boxesSize);
		REQUIRE(colorBreakdown._coloredPixels[1] == boxesSize);
		REQUIRE(colorBreakdown._coloredPixels[2] == boxesSize);
		REQUIRE(colorBreakdown._coloredPixels[3] == boxesSize);
		REQUIRE(colorBreakdown._blackPixels == targetDesc._textureDesc._width * targetDesc._textureDesc._height - 4 * boxesSize);

		////////////////////////////////////////////////////////////////////////////////////////
		{
			auto rpi = fbHelper.BeginRenderPass(*threadContext);

			MiniInputElementDesc miniInputElePC1[] { { Hash64("position"), Format::R32G32_SINT } };
			MiniInputElementDesc miniInputElePC2[] { { Hash64("color"), Format::R8G8B8A8_UNORM } };

			Metal::BoundInputLayout::SlotBinding slotBindings[] {
				{ MakeIteratorRange(miniInputElePC1), 0 },
				{ MakeIteratorRange(miniInputElePC2), 0 }
			};

			// Using the InputElementDesc version of BoundInputLayout constructor
			Metal::BoundInputLayout inputLayout(MakeIteratorRange(slotBindings), shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());

			VertexBufferView vbvs[] {
				VertexBufferView { vertexBuffer0.get() },
				VertexBufferView { vertexBuffer1.get() }
			};
			auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			encoder.Bind(shaderProgram);
			encoder.Bind(inputLayout, Topology::TriangleList);
			encoder.Bind(MakeIteratorRange(vbvs), {});
			encoder.Draw(dimof(vertices_4Boxes));
		}
		////////////////////////////////////////////////////////////////////////////////////////

		colorBreakdown = GetColorBreakdown(*threadContext, fbHelper);
		REQUIRE(colorBreakdown._otherPixels == 0u);
		REQUIRE(colorBreakdown._coloredPixels[0] == boxesSize);
		REQUIRE(colorBreakdown._coloredPixels[1] == boxesSize);
		REQUIRE(colorBreakdown._coloredPixels[2] == boxesSize);
		REQUIRE(colorBreakdown._coloredPixels[3] == boxesSize);
		REQUIRE(colorBreakdown._blackPixels == targetDesc._textureDesc._width * targetDesc._textureDesc._height - 4 * boxesSize);
	}

	TEST_CASE( "InputLayout-BasicBinding_DataRate", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Bind some geometry and render it using the "InputElementDesc" version of the
		// BoundInputLayout constructor, with 3 separate vertex buffers, and some attributes
		// using per instance data rate settings
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgram = testHelper->MakeShaderProgram(vsText_Instanced, psText);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");

		auto vertexBuffer0 = testHelper->CreateVB(MakeIteratorRange(vertices_4Boxes));
		auto vertexBuffer1 = testHelper->CreateVB(MakeIteratorRange(fixedColors));
		auto vertexBuffer2 = testHelper->CreateVB(MakeIteratorRange(boxOffsets));

		InputElementDesc inputEles[] = {
			InputElementDesc { "position", 0, Format::R32G32_SINT, 0 },
			InputElementDesc { "color", 0, Format::R8G8B8A8_UNORM, 1, ~0u, InputDataRate::PerInstance, 1 },
			InputElementDesc { "instanceOffset", 0, Format::R32G32_SINT, 2, ~0u, InputDataRate::PerInstance, 1 }
		};

		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		auto rpi = fbHelper.BeginRenderPass(*threadContext);

		////////////////////////////////////////////////////////////////////////////////////////
		{
			Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputEles), shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());

			VertexBufferView vbvs[] {
				VertexBufferView { vertexBuffer0.get() },
				VertexBufferView { vertexBuffer1.get() },
				VertexBufferView { vertexBuffer2.get() },
			};
			auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);

			encoder.Bind(shaderProgram);
			encoder.Bind(inputLayout, Topology::TriangleList);
			encoder.Bind(MakeIteratorRange(vbvs), {});
			encoder.DrawInstances(6, 4);
		}
		////////////////////////////////////////////////////////////////////////////////////////

		rpi = {};     // end RPI

		auto colorBreakdown = GetColorBreakdown(*threadContext, fbHelper);
		REQUIRE(colorBreakdown._otherPixels == 0u);
		REQUIRE(colorBreakdown._coloredPixels[0] == boxesSize);
		REQUIRE(colorBreakdown._coloredPixels[1] == boxesSize);
		REQUIRE(colorBreakdown._coloredPixels[2] == boxesSize);
		REQUIRE(colorBreakdown._coloredPixels[3] == boxesSize);
		REQUIRE(colorBreakdown._blackPixels == targetDesc._textureDesc._width * targetDesc._textureDesc._height - 4 * boxesSize);
		(void)colorBreakdown;

		rpi = fbHelper.BeginRenderPass(*threadContext);

		////////////////////////////////////////////////////////////////////////////////////////
		{
			// Same, except using an index buffer
			unsigned idxBufferData[6];
			for (unsigned c=0; c<dimof(idxBufferData); ++c) idxBufferData[c] = c;
			auto idxBuffer = testHelper->CreateIB(MakeIteratorRange(idxBufferData));

			Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputEles), shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());

			VertexBufferView vbvs[] {
				VertexBufferView { vertexBuffer0.get() },
				VertexBufferView { vertexBuffer1.get() },
				VertexBufferView { vertexBuffer2.get() },
			};
			IndexBufferView ibv { idxBuffer, Format::R32_UINT };
			auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);

			encoder.Bind(shaderProgram);
			encoder.Bind(inputLayout, Topology::TriangleList);
			encoder.Bind(MakeIteratorRange(vbvs), ibv);
			encoder.DrawIndexedInstances(6, 4);
		}
		////////////////////////////////////////////////////////////////////////////////////////

		rpi = {};     // end RPI

		colorBreakdown = GetColorBreakdown(*threadContext, fbHelper);
		REQUIRE(colorBreakdown._otherPixels == 0u);
		REQUIRE(colorBreakdown._coloredPixels[0] == boxesSize);
		REQUIRE(colorBreakdown._coloredPixels[1] == boxesSize);
		REQUIRE(colorBreakdown._coloredPixels[2] == boxesSize);
		REQUIRE(colorBreakdown._coloredPixels[3] == boxesSize);
		REQUIRE(colorBreakdown._blackPixels == targetDesc._textureDesc._width * targetDesc._textureDesc._height - 4 * boxesSize);
		(void)colorBreakdown;
	}

	TEST_CASE( "InputLayout-BasicBinding_BindAttributeToGeneratorShader", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Bind an attribute (of any kind) to some shader that doesn't take any attributes as
		// input at all
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgram = testHelper->MakeShaderProgram(vsText_FullViewport, psText);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");
		auto& metalContext = *Metal::DeviceContext::Get(*testHelper->_device->GetImmediateContext());
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		auto rpi = fbHelper.BeginRenderPass(*threadContext);

		Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputElePC), shaderProgram);
		REQUIRE(inputLayout.AllAttributesBound());

		auto vertexBuffer = testHelper->CreateVB(MakeIteratorRange(vertices_randomTriangle));
		VertexBufferView vbv { vertexBuffer.get() };
		auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
		encoder.Bind(MakeIteratorRange(&vbv, &vbv+1), {});
	}

	TEST_CASE( "InputLayout-BasicBinding_BindMissingAttribute", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Bind an attribute (and actually a full VB) to a shader that doesn't actually need
		// that attribute. In this case, the entire VB binding gets rejected because none of
		// that attributes from that VB are needed (but other attribute bindings -- from other
		// VBs -- do apply)
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgram = testHelper->MakeShaderProgram(vsText, psText);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");
		auto& metalContext = *Metal::DeviceContext::Get(*testHelper->_device->GetImmediateContext());
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		auto rpi = fbHelper.BeginRenderPass(*threadContext);

		InputElementDesc inputEles[] = {
			InputElementDesc { "position", 0, Format::R32G32_SINT, 0 },
			InputElementDesc { "color", 0, Format::R8G8B8A8_UNORM, 1, ~0u, InputDataRate::PerInstance, 1 },
			InputElementDesc { "instanceOffset", 0, Format::R32G32_SINT, 2, ~0u, InputDataRate::PerInstance, 1 }
		};

		Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputEles), shaderProgram);
		REQUIRE(inputLayout.AllAttributesBound());

		auto vertexBuffer0 = testHelper->CreateVB(MakeIteratorRange(vertices_4Boxes));
		auto vertexBuffer1 = testHelper->CreateVB(MakeIteratorRange(fixedColors));
		auto vertexBuffer2 = testHelper->CreateVB(MakeIteratorRange(boxOffsets));
		VertexBufferView vbvs[] {
			VertexBufferView { vertexBuffer0.get() },
			VertexBufferView { vertexBuffer1.get() },
			VertexBufferView { vertexBuffer2.get() },
		};

		auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
		encoder.Bind(MakeIteratorRange(vbvs), {});
	}

	TEST_CASE( "InputLayout-BasicBinding_Uniforms", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Bind some geometry and render it, and bind some uniforms using the the BoundUniforms
		// class. Also render using a "vertex generator" shader with no input attributes.
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgram = testHelper->MakeShaderProgram(vsText_FullViewport, psText_Uniforms);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");

		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		auto rpi = fbHelper.BeginRenderPass(*threadContext);

		////////////////////////////////////////////////////////////////////////////////////////
		{
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			
			Metal::BoundInputLayout inputLayout(IteratorRange<const InputElementDesc*>{}, shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());
			encoder.Bind(inputLayout, Topology::TriangleStrip);
			encoder.Bind(IteratorRange<const VertexBufferView*>{}, {});

			// NOTE -- special case in AppleMetal implementation -- the shader must be bound
			// here first, before we get to the uniform binding
			encoder.Bind(shaderProgram);

			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Hash64("Values"));
			Metal::BoundUniforms uniforms { shaderProgram, usi };
			REQUIRE(uniforms.GetBoundLooseConstantBuffers() == 1ull);

			Values v { 0.4f, 0.5f, 0.2f, 0, Float4 { 0.1f, 1.0f, 1.0f, 1.0f } };
			UniformsStream us;
			us._immediateData = {
				MakeOpaqueIteratorRange(v)
			};
			uniforms.ApplyLooseUniforms(metalContext, encoder, us);

			encoder.Draw(4);
		}
		////////////////////////////////////////////////////////////////////////////////////////

		rpi = {};     // end RPI

		// we should have written the same color to every pixel, based on the uniform inputs we gave
		auto colorBreakdown = fbHelper.GetFullColorBreakdown(*threadContext);
		REQUIRE(colorBreakdown.size() == (size_t)1);
		REQUIRE(ColorsMatch(colorBreakdown.begin()->first, 0xff198066));
		REQUIRE(colorBreakdown.begin()->second == targetDesc._textureDesc._width * targetDesc._textureDesc._height);

		////////////////////////////////////////////////////////////////////////////////////////
		//  Do it again, this time with the full CB layout provided in the binding call

		rpi = fbHelper.BeginRenderPass(*threadContext);

		{
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);

			Metal::BoundInputLayout inputLayout(IteratorRange<const InputElementDesc*>{}, shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());
			encoder.Bind(inputLayout, Topology::TriangleStrip);
			encoder.Bind(IteratorRange<const VertexBufferView*>{}, {});
			encoder.Bind(shaderProgram);

			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Hash64("Values"), MakeIteratorRange(ConstantBufferElementDesc_Values));
			Metal::BoundUniforms uniforms { shaderProgram, usi };
			REQUIRE(uniforms.GetBoundLooseConstantBuffers() == 1ull);

			Values v { 0.1f, 0.7f, 0.4f, 0, Float4 { 0.8f, 1.0f, 1.0f, 1.0f } };
			UniformsStream us;
			us._immediateData = {
				MakeOpaqueIteratorRange(v)
			};
			uniforms.ApplyLooseUniforms(metalContext, encoder, us);

			encoder.Draw(4);
		}

		rpi = {};     // end RPI

		// we should have written the same color to every pixel, based on the uniform inputs we gave
		colorBreakdown = fbHelper.GetFullColorBreakdown(*threadContext);
		REQUIRE(colorBreakdown.size() == (size_t)1);
		REQUIRE(ColorsMatch(colorBreakdown.begin()->first, 0xffccb219));
		REQUIRE(colorBreakdown.begin()->second == targetDesc._textureDesc._width * targetDesc._textureDesc._height);
	}

	TEST_CASE( "InputLayout-BasicBinding_IncorrectUSI", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Bind uniform buffers using the BoundUniforms interface with various error conditions
		// (such as incorrect arrangement of uniform buffer elements, missing values, etc)
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgram = testHelper->MakeShaderProgram(vsText_FullViewport2, psText_Uniforms);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");

		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		auto rpi = fbHelper.BeginRenderPass(*threadContext);

		auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
		encoder.Bind(shaderProgram);

		{
			// incorrect arrangement of constant buffer elements
			const ConstantBufferElementDesc ConstantBufferElementDesc_IncorrectBinding[] {
				ConstantBufferElementDesc { Hash64("A"), Format::R32_FLOAT, sizeof(Values) - sizeof(Values::A) - offsetof(Values, A) },
				ConstantBufferElementDesc { Hash64("B"), Format::R32_FLOAT, sizeof(Values) - sizeof(Values::A) - offsetof(Values, B) },
				ConstantBufferElementDesc { Hash64("C"), Format::R32_FLOAT, sizeof(Values) - sizeof(Values::A) - offsetof(Values, C) },
				ConstantBufferElementDesc { Hash64("vA"), Format::R32G32B32A32_FLOAT, sizeof(Values) - sizeof(Values::vA) - offsetof(Values, vA) }
			};

			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Hash64("Values"), MakeIteratorRange(ConstantBufferElementDesc_IncorrectBinding));
			Metal::BoundUniforms uniforms { shaderProgram, usi };
			(void)uniforms;
		}

		{
			// some missing constant buffer elements
			const ConstantBufferElementDesc ConstantBufferElementDesc_MissingValues[] {
				RenderCore::ConstantBufferElementDesc { Hash64("A"), RenderCore::Format::R32_FLOAT, offsetof(Values, A) },
				RenderCore::ConstantBufferElementDesc { Hash64("vA"), RenderCore::Format::R32G32B32A32_FLOAT, offsetof(Values, vA) }
			};

			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Hash64("Values"), MakeIteratorRange(ConstantBufferElementDesc_MissingValues));
			Metal::BoundUniforms uniforms { shaderProgram, usi };
			(void)uniforms;
		}

		{
			// Incorrect formats of elements within the constant buffer
			const ConstantBufferElementDesc ConstantBufferElementDesc_IncorrectFormats[] {
				RenderCore::ConstantBufferElementDesc { Hash64("A"), RenderCore::Format::R32G32_FLOAT, offsetof(Values, A) },
				RenderCore::ConstantBufferElementDesc { Hash64("C"), RenderCore::Format::R8G8B8A8_UNORM, offsetof(Values, C) },
				RenderCore::ConstantBufferElementDesc { Hash64("vA"), RenderCore::Format::R32G32B32_FLOAT, offsetof(Values, vA) }
			};

			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Hash64("Values"), MakeIteratorRange(ConstantBufferElementDesc_IncorrectFormats));
			Metal::BoundUniforms uniforms { shaderProgram, usi };
			(void)uniforms;
		}

		{
			// overlapping values in the constant buffer elements
			const ConstantBufferElementDesc ConstantBufferElementDesc_OverlappingValues[] {
				RenderCore::ConstantBufferElementDesc { Hash64("A"), RenderCore::Format::R32G32_FLOAT, offsetof(Values, A) },
				RenderCore::ConstantBufferElementDesc { Hash64("B"), RenderCore::Format::R32G32_FLOAT, offsetof(Values, B) },
				RenderCore::ConstantBufferElementDesc { Hash64("C"), RenderCore::Format::R32G32_FLOAT, offsetof(Values, C) },
				RenderCore::ConstantBufferElementDesc { Hash64("vA"), RenderCore::Format::R32G32B32A32_FLOAT, offsetof(Values, vA) }
			};

			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Hash64("Values"), MakeIteratorRange(ConstantBufferElementDesc_OverlappingValues));
			Metal::BoundUniforms uniforms { shaderProgram, usi };
			(void)uniforms;
		}

		{
			// missing CB binding
			UniformsStreamInterface usi;
			Metal::BoundUniforms uniforms { shaderProgram, usi };
			(void)uniforms;
		}

		encoder = {};
		rpi = {};     // end RPI
	}

	class TestTexture
	{
	public:
		RenderCore::ResourceDesc _resDesc;
		std::vector<unsigned> _initData;
		std::shared_ptr<RenderCore::IResource> _res;

		TestTexture(RenderCore::IDevice& device)
		{
			using namespace RenderCore;
			_resDesc = CreateDesc(
				BindFlag::ShaderResource, 0, GPUAccess::Read,
				TextureDesc::Plain2D(16, 16, Format::R8G8B8A8_UNORM),
				"input-tex");
			for (unsigned y=0; y<16; ++y)
				for (unsigned x=0; x<16; ++x)
					_initData.push_back(((x+y)&1) ? 0xff7f7f7f : 0xffcfcfcf);
			_res = device.CreateResource(
				_resDesc,
				[this](SubResourceId subResId) {
					assert(subResId._mip == 0 && subResId._arrayLayer == 0);
					return SubResourceInitData { MakeIteratorRange(_initData), MakeTexturePitches(_resDesc._textureDesc) };
				});
		}
	};

	TEST_CASE( "InputLayout-BasicBinding_IncorrectUniformsStreamShader", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Bind uniform buffers using the BoundUniforms interface with various error conditions
		// But this time, the errors are in the UniformsStream object passed to the Apply method
		// (With Apple Metal, the Apply method only queues up uniforms to be applied at Draw,
		// so it's the Draw that will throw.)
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgramCB = testHelper->MakeShaderProgram(vsText_FullViewport2, psText_Uniforms);
		auto shaderProgramSRV = testHelper->MakeShaderProgram(vsText_FullViewport2, psText_TextureBinding);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");

		TestTexture testTexture(*testHelper->_device);

		// -------------------------------------------------------------------------------------

		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		Metal::CompleteInitialization(metalContext, {testTexture._res.get()});

		auto rpi = fbHelper.BeginRenderPass(*threadContext);
		auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);

		auto vertexBuffer0 = testHelper->CreateVB(MakeIteratorRange(vertices_vIdx));
		Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputEleVIdx), shaderProgramCB);
		REQUIRE(inputLayout.AllAttributesBound());
		VertexBufferView vbvs[] = { vertexBuffer0.get() };
		encoder.Bind(MakeIteratorRange(vbvs), {});
		encoder.Bind(inputLayout, Topology::TriangleList);

		{
			// Shader takes a CB called "Values", but we will incorrectly attempt to bind
			// a shader resource there (and not bind the CB)
			encoder.Bind(shaderProgramCB);

			UniformsStreamInterface usi;
			usi.BindResourceView(0, Hash64("Values"));
			usi.BindSampler(0, Hash64("Values_sampler"));
			Metal::BoundUniforms uniforms { shaderProgramCB, usi };
			
			auto srv = testTexture._res->CreateTextureView(BindFlag::ShaderResource);
			auto pointSampler = testHelper->_device->CreateSampler(SamplerDesc{ FilterMode::Point, AddressMode::Clamp, AddressMode::Clamp });

			UniformsStream uniformsStream;
			uniformsStream._resourceViews = {
				srv.get()
			};
			uniformsStream._samplers = {
				pointSampler.get()
			};
			uniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);
		}

		{
			// Shader takes a SRV called "Texture", but we will incorrectly attempt to bind
			// a constant buffer there (and not bind the SRV)
			encoder.Bind(shaderProgramSRV);

			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Hash64("Texture"));
			Metal::BoundUniforms uniforms { shaderProgramSRV, usi };
			
			Values v;
			UniformsStream uniformsStream;
			uniformsStream._immediateData = {
				MakeOpaqueIteratorRange(v)
			};
			uniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);                
		}

		{
			// Shader takes a CB called "Values", we will promise to bind it, but then not
			// actually include it into the UniformsStream
			encoder.Bind(shaderProgramCB);

			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Hash64("Values"), MakeIteratorRange(ConstantBufferElementDesc_Values));
			Metal::BoundUniforms uniforms { shaderProgramCB, usi };

			REQUIRE_THROWS(
				[&]() {
					uniforms.ApplyLooseUniforms(metalContext, encoder, UniformsStream {});
					encoder.Draw(4);
				});
		}

		{
			// Shader takes a SRV called "Texture", we will promise to bind it, but then not
			// actually include it into the UniformsStream
			encoder.Bind(shaderProgramSRV);

			UniformsStreamInterface usi;
			usi.BindResourceView(0, Hash64("Texture"));
			Metal::BoundUniforms uniforms { shaderProgramSRV, usi };

			REQUIRE_THROWS(
				[&]() {
					uniforms.ApplyLooseUniforms(metalContext, encoder, UniformsStream {});
					encoder.Draw(4);
				});
		}

		encoder = {};
		rpi = {};     // end RPI
	}

	TEST_CASE( "InputLayout-BasicBinding_IncorrectUniformsStreamPipeline", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Bind uniform buffers using the BoundUniforms interface with various error conditions
		// But this time, the errors are in the UniformsStream object passed to the Apply method
		// (Here we construct with the graphics pipeline, instead of the shader, so we should
		//  get an immediate exception from Apply.)
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgramCB = testHelper->MakeShaderProgram(vsText_FullViewport2, psText_Uniforms);
		auto shaderProgramSRV = testHelper->MakeShaderProgram(vsText_FullViewport2, psText_TextureBinding);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");

		TestTexture testTexture(*testHelper->_device);

		// -------------------------------------------------------------------------------------

		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		Metal::CompleteInitialization(metalContext, {testTexture._res.get()});

		auto rpi = fbHelper.BeginRenderPass(*threadContext);
		auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);

		auto vertexBuffer0 = testHelper->CreateVB(MakeIteratorRange(vertices_vIdx));
		Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputEleVIdx), shaderProgramCB);
		REQUIRE(inputLayout.AllAttributesBound());
		VertexBufferView vbvs[] = { vertexBuffer0.get() };
		encoder.Bind(MakeIteratorRange(vbvs), {});

		{
			// Shader takes a CB called "Values", but we will incorrectly attempt to bind
			// a shader resource there (and not bind the CB)
			encoder.Bind(shaderProgramCB);

			UniformsStreamInterface usi;
			usi.BindResourceView(0, Hash64("Values"));
			usi.BindSampler(0, Hash64("Values_sampler"));
			Metal::BoundUniforms uniforms { shaderProgramCB, usi };

			auto srv = testTexture._res->CreateTextureView(BindFlag::ShaderResource);
			auto pointSampler = testHelper->_device->CreateSampler(SamplerDesc{ FilterMode::Point, AddressMode::Clamp, AddressMode::Clamp });

			UniformsStream uniformsStream;
			uniformsStream._resourceViews = {
				srv.get()
			};
			uniformsStream._samplers = {
				pointSampler.get()
			};
			uniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);
		}

		{
			// Shader takes a SRV called "Texture", but we will incorrectly attempt to bind
			// a constant buffer there (and not bind the SRV)
			encoder.Bind(shaderProgramSRV);

			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Hash64("Texture"));
			Metal::BoundUniforms uniforms { shaderProgramSRV, usi };

			Values v;
			UniformsStream uniformsStream;
			uniformsStream._immediateData = {
				MakeOpaqueIteratorRange(v)
			};
			uniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);
		}

		{
			// Shader takes a CB called "Values", we will promise to bind it, but then not
			// actually include it into the UniformsStream
			encoder.Bind(shaderProgramCB);

			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Hash64("Values"), MakeIteratorRange(ConstantBufferElementDesc_Values));
			#if GFXAPI_TARGET == GFXAPI_APPLEMETAL
				auto pipeline = metalContext.CreatePipeline(Metal::GetObjectFactory());
				Metal::BoundUniforms uniforms { *pipeline, usi };
			#else
				Metal::BoundUniforms uniforms { shaderProgramCB, usi };
			#endif

			REQUIRE_THROWS(
				[&]() {
					uniforms.ApplyLooseUniforms(metalContext, encoder, UniformsStream {});
				});
		}

		{
			// Shader takes a SRV called "Texture", we will promise to bind it, but then not
			// actually include it into the UniformsStream
			encoder.Bind(shaderProgramSRV);

			UniformsStreamInterface usi;
			usi.BindResourceView(0, Hash64("Texture"));
			#if GFXAPI_TARGET == GFXAPI_APPLEMETAL
				auto pipeline = metalContext.CreatePipeline(Metal::GetObjectFactory());
				Metal::BoundUniforms uniforms { *pipeline, usi };
			#else
				Metal::BoundUniforms uniforms { shaderProgramSRV, usi };
			#endif

			REQUIRE_THROWS(
				[&]() {
					uniforms.ApplyLooseUniforms(metalContext, encoder, UniformsStream {});
				});
		}

		encoder = {};
		rpi = {};     // end RPI
	}

	TEST_CASE( "InputLayout-BasicBinding_TextureBinding", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Bind some geometry and bind a texture using the BoundUniforms interface
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgram = testHelper->MakeShaderProgram(vsText_FullViewport2, psText_TextureBinding);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");

		TestTexture testTexture(*testHelper->_device);

		// -------------------------------------------------------------------------------------

		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		Metal::CompleteInitialization(metalContext, {testTexture._res.get()});

		////////////////////////////////////////////////////////////////////////////////////////
		{
			auto rpi = fbHelper.BeginRenderPass(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);

			auto vertexBuffer0 = testHelper->CreateVB(MakeIteratorRange(vertices_vIdx));
			Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputEleVIdx), shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());
			VertexBufferView vbvs[] = { vertexBuffer0.get() };
			encoder.Bind(MakeIteratorRange(vbvs), {});

			// NOTE -- special case in AppleMetal implementation -- the shader must be bound
			// here first, before we get to the uniform binding
			encoder.Bind(shaderProgram);

			UniformsStreamInterface usi;
			usi.BindResourceView(0, Hash64("Texture"));
			usi.BindSampler(0, Hash64("Texture_sampler"));
			Metal::BoundUniforms uniforms { shaderProgram, usi };

			auto srv = testTexture._res->CreateTextureView(BindFlag::ShaderResource);
			auto pointSampler = testHelper->_device->CreateSampler(SamplerDesc{ FilterMode::Point, AddressMode::Clamp, AddressMode::Clamp });

			UniformsStream uniformsStream;
			uniformsStream._resourceViews = {
				srv.get()
			};
			uniformsStream._samplers = {
				pointSampler.get()
			};
			uniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);

			encoder.Bind(inputLayout, Topology::TriangleStrip);
			encoder.Draw(4);
		}
		////////////////////////////////////////////////////////////////////////////////////////

		// We're expecting the output texture to directly match the input, just scaled up by
		// the dimensional difference. Since we're using point sampling, there should be no
		// filtering applied
		auto data = fbHelper.GetMainTarget()->ReadBackSynchronized(*threadContext);
		assert(data.size() == (size_t)RenderCore::ByteCount(targetDesc));
		auto pixels = MakeIteratorRange((unsigned*)AsPointer(data.begin()), (unsigned*)AsPointer(data.end()));

		for (unsigned y=0; y<targetDesc._textureDesc._height; ++y)
			for (unsigned x=0; x<targetDesc._textureDesc._width; ++x) {
				unsigned inputX = x * 16 / targetDesc._textureDesc._width;
				unsigned inputY = y * 16 / targetDesc._textureDesc._height;
				REQUIRE(pixels[y*targetDesc._textureDesc._width+x] == testTexture._initData[inputY*16+inputX]);
			}
	}

	TEST_CASE( "InputLayout-BasicBinding_TextureSampling", "[rendercore_metal]" )
	{
		// -------------------------------------------------------------------------------------
		// Bind some geometry and sample a texture using a filtering sampler
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgram = testHelper->MakeShaderProgram(vsText_FullViewport2, psText_TextureBinding);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");

		TestTexture testTexture(*testHelper->_device);

		// -------------------------------------------------------------------------------------

		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		Metal::CompleteInitialization(metalContext, {testTexture._res.get()});

		auto rpi = fbHelper.BeginRenderPass(*threadContext);

		////////////////////////////////////////////////////////////////////////////////////////
		{
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			auto vertexBuffer0 = testHelper->CreateVB(MakeIteratorRange(vertices_vIdx));
			Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputEleVIdx), shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());
			VertexBufferView vbvs[] = { vertexBuffer0.get() };
			encoder.Bind(MakeIteratorRange(vbvs), {});

			encoder.Bind(shaderProgram);

			UniformsStreamInterface usi;
			usi.BindResourceView(0, Hash64("Texture"));
			usi.BindSampler(0, Hash64("Texture_sampler"));
			Metal::BoundUniforms uniforms { shaderProgram, usi };

			auto srv = testTexture._res->CreateTextureView(BindFlag::ShaderResource);
			auto pointSampler = testHelper->_device->CreateSampler(SamplerDesc{ FilterMode::Point, AddressMode::Clamp, AddressMode::Clamp });

			UniformsStream uniformsStream;
			uniformsStream._resourceViews = {
				srv.get()
			};
			uniformsStream._samplers = {
				pointSampler.get()
			};
			uniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);

			encoder.Bind(inputLayout, Topology::TriangleStrip);
			encoder.Draw(4);
		}
		////////////////////////////////////////////////////////////////////////////////////////

		rpi = {};     // end RPI

		auto breakdown = fbHelper.GetFullColorBreakdown(*threadContext);
		REQUIRE(breakdown.size() == 2);       // if point sampling is working, we should have two colors

		////////////////////////////////////////////////////////////////////////////////////////

		rpi = fbHelper.BeginRenderPass(*threadContext);

		////////////////////////////////////////////////////////////////////////////////////////
		{
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
			auto vertexBuffer0 = testHelper->CreateVB(MakeIteratorRange(vertices_vIdx));
			Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputEleVIdx), shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());
			VertexBufferView vbvs[] = { vertexBuffer0.get() };
			encoder.Bind(MakeIteratorRange(vbvs), {});

			encoder.Bind(shaderProgram);

			UniformsStreamInterface usi;
			usi.BindResourceView(0, Hash64("Texture"));
			usi.BindSampler(0, Hash64("Texture_sampler"));
			Metal::BoundUniforms uniforms { shaderProgram, usi };

			auto srv = testTexture._res->CreateTextureView(BindFlag::ShaderResource);
			auto linearSampler = testHelper->_device->CreateSampler(SamplerDesc{ FilterMode::Bilinear, AddressMode::Clamp, AddressMode::Clamp });

			UniformsStream uniformsStream;
			uniformsStream._resourceViews = {
				srv.get()
			};
			uniformsStream._samplers = {
				linearSampler.get()
			};
			uniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);

			encoder.Bind(inputLayout, Topology::TriangleStrip);
			encoder.Draw(4);
		}
		////////////////////////////////////////////////////////////////////////////////////////

		rpi = {};     // end RPI

		breakdown = fbHelper.GetFullColorBreakdown(*threadContext);
		REQUIRE(breakdown.size() > 2);       // if filtering is working, we will get a large variety of colors
	}

	// error cases we could try:
	//      * not binding all attributes
	//      * refering to a vertex buffer in the InputElementDesc, and then not providing it
	//          in the Apply() method
	//      * providing a vertex buffer that isn't used at all (eg, unused attribute)
	//      * overlapping elements in the input binding
	//      * mismatched attribute
}
