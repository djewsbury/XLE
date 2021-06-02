// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MetalTestHelper.h"
#include "../../../RenderCore/Metal/Shader.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/Metal/InputLayout.h"
#include "../../../RenderCore/Metal/State.h"
#include "../../../RenderCore/Format.h"
#include "../../../RenderCore/BufferView.h"
#include "../../../RenderCore/IThreadContext.h"
#include "../../../RenderCore/Vulkan/IDeviceVulkan.h"
#include "../../../Math/Vector.h"
#include "../../../Utility/StringFormat.h"

#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
	T2(OutputType, InputType) OutputType* QueryInterfaceCast(InputType& input)
	{
		return (OutputType*)input.QueryInterface(typeid(OutputType).hash_code());
	}

	static const char vsText[] = R"(
		float4 main(float2 input : POSITION, float2 texCoord : TEXCOORD, out float2 oTexCoord : TEXCOORD) : SV_Position 
		{
			oTexCoord = texCoord;
			return float4(input, 0, 1);
		}
	)";
	static const char psText[] = R"(
		cbuffer InputData
		{
			float4 data[32*32];
		};

		float4 main(
			float4 position : SV_Position,
			float2 texCoord : TEXCOORD) : SV_Target0
		{
			int idx = min(31, int(texCoord.y * 32.f)) * 32 + min(31, int(texCoord.x * 32.f));
			return data[idx];
		}
	)";

	TEST_CASE( "ThreadedRendering-TemporaryStorage", "[rendercore_metal]" )
	{
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget|BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(256, 256, Format::R32G32B32A32_FLOAT),
			"temporary-out1");
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc, LoadStore::Retain);

		auto* vulkanThreadContext = (IThreadContextVulkan*)threadContext->QueryInterface(typeid(IThreadContextVulkan).hash_code());
		REQUIRE(vulkanThreadContext);	// only implemented for Vulkan currently

		// Spawn a lot of threads, and each one run a simple shader that
		//      copies data from a temporary storage uniform buffer onto the back buffer
		//      however, every time we upload the texels for a random part of the full frame buffer and only draw to that back of the back buffer
		// If everything is working correctly, we should end up just writing the same value to each pixel over and over again
		
		Metal::ShaderProgram shaderProgram = testHelper->MakeShaderProgram(vsText, psText);

		InputElementDesc inputEle[] = { 
			InputElementDesc{"POSITION", 0, Format::R32G32_FLOAT},
			InputElementDesc{"TEXCOORD", 0, Format::R32G32_FLOAT} 
		};
		Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputEle), shaderProgram);

		Metal::GraphicsPipelineBuilder pipelineBuilder;
		pipelineBuilder.SetRenderPassConfiguration(fbHelper.GetDesc(), 0);
		pipelineBuilder.Bind(inputLayout, Topology::TriangleStrip);
		pipelineBuilder.Bind(shaderProgram);
		AttachmentBlendDesc blendDescs[] = {
			AttachmentBlendDesc { true, Blend::One, Blend::One, BlendOp::Add, Blend::One, Blend::One, BlendOp::Add }
		};
		pipelineBuilder.Bind(MakeIteratorRange(blendDescs));
		auto pipeline = pipelineBuilder.CreatePipeline(Metal::GetObjectFactory());

		UniformsStreamInterface usi;
		usi.BindImmediateData(0, Hash64("InputData"));
		Metal::BoundUniforms boundUniforms(*pipeline, usi);

		Int2 screenMaxs { targetDesc._textureDesc._width, targetDesc._textureDesc._height };

		std::mt19937_64 rng(94667465);
		std::vector<Float4> srcData(256*256);
		for (unsigned c=0; c<srcData.size(); ++c)
			srcData[c] = Float4{ std::uniform_real_distribution<float>(0.f, 1.f)(rng), std::uniform_real_distribution<float>(0.f, 1.f)(rng), std::uniform_real_distribution<float>(0.f, 1.f)(rng), 1.0f };

		auto threadableFunction = [&fbHelper, pipeline, testHelper=testHelper.get(), screenMaxs, &srcData, &boundUniforms](IThreadContext& threadContext, std::mt19937_64& rng)
		{		
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			auto rpi = fbHelper.BeginRenderPass(threadContext);

			auto encoder = metalContext.BeginGraphicsEncoder(testHelper->_pipelineLayout);

			Float2 mins {
				(float)std::uniform_int_distribution<int>(0, screenMaxs[0]-1)(rng),
				(float)std::uniform_int_distribution<int>(0, screenMaxs[1]-1)(rng),
			};
			Float2 maxs { mins[0] + 32, mins[1] + 32 };

			{
				const Float4 vertices[] { 
					{ mins[0] / 256.f * 2.0f - 1.0f, mins[1] / 256.f * 2.0f - 1.0f, 0.f, 0.f }, 
					{ mins[0] / 256.f * 2.0f - 1.0f, maxs[1] / 256.f * 2.0f - 1.0f, 0.f, 1.f }, 
					{ maxs[0] / 256.f * 2.0f - 1.0f, mins[1] / 256.f * 2.0f - 1.0f, 1.f, 0.f }, 
					{ maxs[0] / 256.f * 2.0f - 1.0f, maxs[1] / 256.f * 2.0f - 1.0f, 1.f, 1.f }
				};
				auto vb = metalContext.MapTemporaryStorage(sizeof(vertices), BindFlag::VertexBuffer);
				REQUIRE(vb.GetData().size() == sizeof(vertices));
				std::memcpy(vb.GetData().begin(), vertices, sizeof(vertices));
				VertexBufferView vbvs[] = { vb.AsVertexBufferView() };
				encoder.Bind(MakeIteratorRange(vbvs), {});
			}
			{
				std::vector<Float4> partialData;
				partialData.resize(32*32);
				for (unsigned y=0; y<32; ++y)
					for (unsigned x=0; x<32; ++x) {
						auto srcX = unsigned(mins[0]+x), srcY = unsigned(mins[1]+y);
						if (srcX < 256 && srcY < 256) {
							partialData[y*32+x] = srcData[srcY*256+srcX];
						} else
							partialData[y*32+x] = Float4{0,0,0,1};
					}

				UniformsStream::ImmediateData immData[] { MakeIteratorRange(partialData) };
				UniformsStream us;
				us._immediateData = MakeIteratorRange(immData);
				boundUniforms.ApplyLooseUniforms(metalContext, encoder, us);
			}
			encoder.Draw(*pipeline, 4);
		};

		testHelper->BeginFrameCapture();
		Metal::DeviceContext::Get(*threadContext)->Clear(*fbHelper.GetMainTarget()->CreateTextureView(BindFlag::RenderTarget), Float4{0,0,0,0});

		std::atomic<unsigned> drawCount;
		Threading::Mutex pendingCommandListLock;
		std::vector<std::shared_ptr<Metal::CommandList>> pendingCommandList;

		struct Thread
		{
			std::shared_ptr<IThreadContext> _threadContext;
			std::thread _thread;
		};

		const unsigned threadCount = 8;
		const unsigned drawCountLimit = 256;
		std::vector<Thread> threads(threadCount);
		for (unsigned c=0; c<threadCount; ++c) {
			std::mt19937_64 localRng(rng());
			threads[c]._threadContext = testHelper->_device->CreateDeferredContext();
			threads[c]._thread = std::thread(
				[localContext=threads[c]._threadContext, threadableFunction, localRng, &drawCount, &pendingCommandListLock, &pendingCommandList]() mutable {

					unsigned batchCount = 0;
					for (;;) {
						auto currentDrawCount = ++drawCount;
						if (currentDrawCount > drawCountLimit) break;
						threadableFunction(*localContext, localRng);
						++batchCount;
						if ((batchCount%3) == 0) {
							ScopedLock(pendingCommandListLock);
							pendingCommandList.push_back(RenderCore::Metal::DeviceContext::Get(*localContext)->ResolveCommandList());
						}
						std::this_thread::sleep_for(std::chrono::milliseconds(16));
					}

					ScopedLock(pendingCommandListLock);
					pendingCommandList.push_back(RenderCore::Metal::DeviceContext::Get(*localContext)->ResolveCommandList());
				});
		}

		unsigned commitCount = 0;
		for (;;) {
			auto currentDrawCount = drawCount.load();
			if (currentDrawCount > drawCountLimit) break;
			{
				ScopedLock(pendingCommandListLock);
				for (auto& cmdList:pendingCommandList)
					vulkanThreadContext->CommitPrimaryCommandBufferToQueue(*cmdList);
				pendingCommandList.clear();
			}
			
			// we have to trigger a CommitCommands every now and again to advance the gpu progress
			// counters (for resource usage tracking, etc)
			++commitCount;
			if ((commitCount % 4) == 0)
				threadContext->CommitCommands();
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}

		for (auto&t:threads)
			t._thread.join();

		// final cmd lists...
		for (auto& cmdList:pendingCommandList)
			vulkanThreadContext->CommitPrimaryCommandBufferToQueue(*cmdList);
		pendingCommandList.clear();

		auto readBackData = fbHelper.GetMainTarget()->ReadBackSynchronized(*threadContext);
		REQUIRE(readBackData.size() == sizeof(Float4)*256*256);

		testHelper->EndFrameCapture();

		Float4* finalColors = (Float4*) readBackData.data();
		for (unsigned y=0; y<256; ++y)
			for (unsigned x=0; x<256; ++x) {
				Float4 final = finalColors[y*256+x], src = srcData[y*256+x];
				auto writeCount = final[3];
				Float4 expected = srcData[y*256+x] * writeCount;
				// We pick up a fair amount of floating point creep here -- so we've got to be careful
				REQUIRE(Equivalent(final[0], expected[0], 1e-3f));
				REQUIRE(Equivalent(final[1], expected[1], 1e-3f));
				REQUIRE(Equivalent(final[2], expected[2], 1e-3f));
			}
	}
}