// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MetalTestHelper.h"
#include "MetalTestShaders.h"
#include "../../../RenderCore/Metal/Shader.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/Metal/InputLayout.h"
#include "../../../RenderCore/Metal/State.h"
#include "../../../RenderCore/Metal/QueryPool.h"
#include "../../../RenderCore/Format.h"
#include "../../../RenderCore/BufferView.h"
#include "../../../Math/Vector.h"
#include "../../../Utility/StringFormat.h"

#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
	std::string BuildSODefinesString(IteratorRange<const RenderCore::InputElementDesc*> desc)
	{
		std::stringstream str;
		str << "SO_OFFSETS=";
		unsigned rollingOffset = 0;
		for (const auto&e:desc) {
			assert(e._alignedByteOffset == ~0x0u);		// expecting to use packed sequential ordering
			if (rollingOffset!=0) str << ",";
			str << Hash64(e._semanticName) + e._semanticIndex << "," << rollingOffset;
			rollingOffset += BitsPerPixel(e._nativeFormat) / 8;
		}
		return str.str();
	}

    TEST_CASE( "StreamOutput-SimpleStreamOutput", "[rendercore_metal]" )
	{
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();

		auto soBuffer = testHelper->_device->CreateResource(
			CreateDesc(
				BindFlag::StreamOutput | BindFlag::TransferSrc,
				LinearBufferDesc::Create(1024, 1024)),
			"soBuffer");

		const InputElementDesc soEles[] = { InputElementDesc("POINT", 0, Format::R32G32B32A32_FLOAT) };
		const unsigned soStrides[] = { (unsigned)sizeof(Float4) };
		
		auto vs = testHelper->MakeShader(vsText_JustPosition, "vs_5_0");
		auto gs = testHelper->MakeShader(gsText_StreamOutput, "gs_5_0", BuildSODefinesString(MakeIteratorRange(soEles)));
		Metal::ShaderProgram shaderProgram{
			Metal::GetObjectFactory(), testHelper->_pipelineLayout,
			vs, gs, {},
			StreamOutputInitializers { MakeIteratorRange(soEles), MakeIteratorRange(soStrides) }};

		Float4 inputVertices[] = {
			Float4{ 1.0f, 2.0f, 3.0f, 4.0f },
			Float4{ 5.0f, 6.0f, 7.0f, 8.0f },
			Float4{ 11.0f, 12.0f, 13.0f, 14.0f },

			Float4{ 15.0f, 16.0f, 17.0f, 18.0f },
			Float4{ 21.0f, 22.0f, 23.0f, 24.0f },
			Float4{ 25.0f, 26.0f, 27.0f, 28.0f },

			Float4{ 31.0f, 32.0f, 33.0f, 34.0f },
			Float4{ 35.0f, 36.0f, 37.0f, 38.0f },
			Float4{ 41.0f, 42.0f, 43.0f, 44.0f }
		};

		auto vertexBuffer = testHelper->_device->CreateResource(
			CreateDesc(
				BindFlag::VertexBuffer, AllocationRules::HostVisibleSequentialWrite,
				LinearBufferDesc::Create(1024, 1024)),
			"vertexBuffer",
			SubResourceInitData{MakeIteratorRange(inputVertices)});
		InputElementDesc inputEle[] = { InputElementDesc{"INPUT", 0, Format::R32G32B32A32_FLOAT} };
		Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputEle), shaderProgram);

		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
		UnitTestFBHelper dummyFBHelper(*testHelper->_device, *threadContext);

		Metal::GraphicsPipelineBuilder pipelineBuilder;
		pipelineBuilder.SetRenderPassConfiguration(dummyFBHelper.GetDesc(), 0);
		pipelineBuilder.Bind(inputLayout, Topology::TriangleList);
		pipelineBuilder.Bind(shaderProgram);
		auto pipeline = pipelineBuilder.CreatePipeline(Metal::GetObjectFactory(*testHelper->_device));

		{
			auto rpi = dummyFBHelper.BeginRenderPass(*threadContext);

			VertexBufferView sov { soBuffer.get() };
			auto encoder = metalContext.BeginStreamOutputEncoder(
				*testHelper->_pipelineLayout, MakeIteratorRange(&sov, &sov+1));

			VertexBufferView vbv { vertexBuffer.get() };
			encoder.Bind(MakeIteratorRange(&vbv, &vbv+1), {});
			encoder.Draw(*pipeline, dimof(inputVertices));
		}

		auto readbackBuffer = soBuffer->ReadBackSynchronized(*threadContext);
		auto* readbackData = (Float4*)readbackBuffer.data();

		REQUIRE(Equivalent(readbackData[0], Float4{11.f, 12.f, 13.f, 14.f}, 1e-6f));
		REQUIRE(Equivalent(readbackData[1], Float4{25.f, 26.f, 27.f, 28.f}, 1e-6f));
		REQUIRE(Equivalent(readbackData[2], Float4{41.f, 42.f, 43.f, 44.f}, 1e-6f));
		REQUIRE(1024 == readbackBuffer.size());

		// try same thing again, this time with a query wrapped around the operation
		SECTION("with query")
		{
			auto streamOutputQueryPool = std::make_unique<RenderCore::Metal::QueryPool>(
				Metal::GetObjectFactory(*testHelper->_device), 
				Metal::QueryPool::QueryType::StreamOutput_Stream0, 4);

			auto queryId = streamOutputQueryPool->Begin(metalContext);

			auto rpi = dummyFBHelper.BeginRenderPass(*threadContext);

			VertexBufferView sov { soBuffer.get() };
			auto encoder = metalContext.BeginStreamOutputEncoder(
				*testHelper->_pipelineLayout, MakeIteratorRange(&sov, &sov+1));

			VertexBufferView vbv { vertexBuffer.get() };
			encoder.Bind(MakeIteratorRange(&vbv, &vbv+1), {});
			encoder.Draw(*pipeline, dimof(inputVertices));

			encoder = {};
			rpi = {};
			streamOutputQueryPool->End(metalContext, queryId);
			threadContext->CommitCommands(CommitCommandsFlags::WaitForCompletion);

			Metal::QueryPool::QueryResult_StreamOutput out;
			streamOutputQueryPool->GetResults_Stall(metalContext, queryId, MakeOpaqueIteratorRange(out));
			REQUIRE(out._primitivesWritten == 3);
			REQUIRE(out._primitivesNeeded == 3);

			auto readbackBuffer = soBuffer->ReadBackSynchronized(*threadContext);
			auto* readbackData = (Float4*)readbackBuffer.data();

			REQUIRE(Equivalent(readbackData[0], Float4{11.f, 12.f, 13.f, 14.f}, 1e-6f));
			REQUIRE(Equivalent(readbackData[1], Float4{25.f, 26.f, 27.f, 28.f}, 1e-6f));
			REQUIRE(Equivalent(readbackData[2], Float4{41.f, 42.f, 43.f, 44.f}, 1e-6f));
			REQUIRE(1024 == readbackBuffer.size());
		}
	}
}