// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "UnitTestHelper.h"
#include "MetalUnitTest.h"
#include "MetalTestShaders.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Metal/State.h"
#include "../RenderCore/Metal/PipelineLayout.h"
#include "../RenderCore/Metal/QueryPool.h"
#include "../RenderCore/AppleMetal/Device.h"
#include "../RenderCore/ResourceDesc.h"
#include "../Math/Vector.h"
#include "../Math/Transformations.h"
#include <map>
#include <deque>
#include <queue>

#if !defined(XC_TEST_ADAPTER)
    #include <CppUnitTest.h>
    using namespace Microsoft::VisualStudio::CppUnitTestFramework;
	#define ThrowsException ExpectException<std::runtime_error>
#endif

namespace UnitTests
{

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

////////////////////////////////////////////////////////////////////////////////////////////////////
            //    T E S T   I N P U T   D A T A

    const RenderCore::ConstantBufferElementDesc ConstantBufferElementDesc_Transform[] {
        RenderCore::ConstantBufferElementDesc { Hash64("inputToClip"), RenderCore::Format::Matrix4x4, 0 },
    };

////////////////////////////////////////////////////////////////////////////////////////////////////
            //    C O D E

    TEST_CLASS(QueryPool)
	{
	public:
		std::unique_ptr<MetalTestHelper> _testHelper;

		QueryPool()
		{
            _testHelper = MakeTestHelper();
		}

		~QueryPool()
		{
			_testHelper.reset();
		}

		TEST_METHOD(QueryPool_TimeStamp)
		{
            // -------------------------------------------------------------------------------------
            // Create a TimeStampQueryPool and use it to profile a few rendering operations
            // -------------------------------------------------------------------------------------
			using namespace RenderCore;
			auto threadContext = _testHelper->_device->GetImmediateContext();
            auto shaderProgram = MakeShaderProgram(*_testHelper, vsText_clipInputTransform, psText);
            auto targetDesc = CreateDesc(
                BindFlag::RenderTarget,
                TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
                "temporary-out");

            UnitTestFBHelper fbHelper(*_testHelper->_device, *threadContext, targetDesc);

            Metal::TimeStampQueryPool tsQuery(Metal::GetObjectFactory());

            const unsigned frameCount = 10;
            const unsigned rendersPerFrame = 100;
            const unsigned markerDraws[] = { 0, 33, 45, 75, 0 };

            std::vector<std::pair<Metal::TimeStampQueryPool::FrameId, Metal::TimeStampQueryPool::FrameResults>> frameResults;
            std::queue<Metal::TimeStampQueryPool::FrameId> pendingFrameIds;

			////////////////////////////////////////////////////////////////////////////////////////
			{
				auto vertexBuffer = CreateVB(*_testHelper->_device, MakeIteratorRange(vertices_randomTriangle));

                // Using the InputElementDesc version of BoundInputLayout constructor
				Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputElePC), shaderProgram);
                Assert::IsTrue(inputLayout.AllAttributesBound());

				VertexBufferView vbv { vertexBuffer.get() };
				auto& metalContext = *Metal::DeviceContext::Get(*threadContext);

                for (unsigned f=0; f<frameCount; ++f) {
                    auto frameId = tsQuery.BeginFrame(metalContext);

                    auto rpi = fbHelper.BeginRenderPass();

                    inputLayout.Apply(metalContext, MakeIteratorRange(&vbv, &vbv+1));
                    metalContext.Bind(shaderProgram);
                    metalContext.Bind(Topology::TriangleList);

                    UniformsStreamInterface usi;
                    usi.BindConstantBuffer(0, {Hash64("Transform"), MakeIteratorRange(ConstantBufferElementDesc_Transform)});
                    Metal::BoundUniforms uniforms { shaderProgram, Metal::PipelineLayoutConfig {}, usi };

                    const unsigned* nextMarker = markerDraws;

                    for (unsigned c=0; c<rendersPerFrame; ++c) {
                        float angle = c / float(rendersPerFrame) * 3.14159f * 2.0f;

                        auto transform = AsFloat4x4(RotationZ{angle});
                        ConstantBufferView cbvs[] = { MakeSharedPkt(transform) };
                        uniforms.Apply(metalContext, 0, UniformsStream { MakeIteratorRange(cbvs) });

                        metalContext.Draw(dimof(vertices_randomTriangle));

                        if (c == *nextMarker) {
                            auto q = tsQuery.SetTimeStampQuery(metalContext);
                            (void)q;
                            ++nextMarker;
                        }
                    }

			        rpi = {};     // end RPI

                    tsQuery.EndFrame(metalContext, frameId);
                    pendingFrameIds.push(frameId);

                    while (!pendingFrameIds.empty()) {
                        auto front = pendingFrameIds.front();
                        auto results = tsQuery.GetFrameResults(metalContext, front);
                        if (results._resultsReady) {
                            pendingFrameIds.pop();
                            frameResults.push_back(std::make_pair(front, results));
                        } else {
                            break;
                        }
                    }
                }

                threadContext->GetDevice()->Stall();

                // finish any results as they come in
                while (!pendingFrameIds.empty()) {
                    auto front = pendingFrameIds.front();
                    auto results = tsQuery.GetFrameResults(metalContext, front);
                    if (results._resultsReady) {
                        pendingFrameIds.pop();
                        frameResults.push_back(std::make_pair(front, results));
                    } else {
                        Threading::Sleep(8);
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////////////////////////
		}

        TEST_METHOD(QueryPool_SyncEventSet)
        {
            // -------------------------------------------------------------------------------------
            // Create a SyncEventSet ensure that the events get triggered as expected
            // -------------------------------------------------------------------------------------
            using namespace RenderCore;
            auto threadContext = _testHelper->_device->GetImmediateContext();
            auto shaderProgram = MakeShaderProgram(*_testHelper, vsText_clipInputTransform, psText);
            auto targetDesc = CreateDesc(
                BindFlag::RenderTarget,
                TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
                "temporary-out");

            UnitTestFBHelper fbHelper(*_testHelper->_device, *threadContext, targetDesc);

            Metal::SyncEventSet syncEventSet(threadContext.get());
            Assert::IsTrue(Metal::SyncEventSet::IsSupported());

            const unsigned frameCount = 10;
            const unsigned rendersPerFrame = 100;
            const unsigned markerDraws[] = { 0, 33, 45, 75, 0 };

            Metal::SyncEventSet::SyncEvent lastEventScheduled = ~0u;
            std::deque<Metal::SyncEventSet::SyncEvent> pendingEvents;

            ////////////////////////////////////////////////////////////////////////////////////////
            {
                auto vertexBuffer = CreateVB(*_testHelper->_device, MakeIteratorRange(vertices_randomTriangle));

                // Using the InputElementDesc version of BoundInputLayout constructor
                Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputElePC), shaderProgram);
                Assert::IsTrue(inputLayout.AllAttributesBound());

                VertexBufferView vbv { vertexBuffer.get() };
                auto& metalContext = *Metal::DeviceContext::Get(*threadContext);

                for (unsigned f=0; f<frameCount; ++f) {
                    auto rpi = fbHelper.BeginRenderPass();

                    inputLayout.Apply(metalContext, MakeIteratorRange(&vbv, &vbv+1));
                    metalContext.Bind(shaderProgram);
                    metalContext.Bind(Topology::TriangleList);

                    UniformsStreamInterface usi;
                    usi.BindConstantBuffer(0, {Hash64("Transform"), MakeIteratorRange(ConstantBufferElementDesc_Transform)});
                    Metal::BoundUniforms uniforms { shaderProgram, Metal::PipelineLayoutConfig {}, usi };

                    const unsigned* nextMarker = markerDraws;

                    for (unsigned c=0; c<rendersPerFrame; ++c) {
                        float angle = c / float(rendersPerFrame) * 3.14159f * 2.0f;

                        auto transform = AsFloat4x4(RotationZ{angle});
                        ConstantBufferView cbvs[] = { MakeSharedPkt(transform) };
                        uniforms.Apply(metalContext, 0, UniformsStream { MakeIteratorRange(cbvs) });

                        metalContext.Draw(dimof(vertices_randomTriangle));

                        if (c == *nextMarker) {
                            auto lastCompleted = syncEventSet.LastCompletedEvent();
                            (void)lastCompleted;
                            auto evnt = syncEventSet.SetEvent();
                            for (auto&e:pendingEvents) {
                                Assert::AreNotEqual(e, evnt);               // ensure no other evnt matches
                                (void)e;
                            }
                            Assert::AreNotEqual(lastCompleted, evnt);       // can't be completed before it's set¨
                            Assert::IsTrue(evnt > lastCompleted);
                            pendingEvents.push_back(evnt);
                            lastEventScheduled = evnt;
                            ++nextMarker;
                        }
                    }

                    rpi = {};     // end RPI

                    threadContext->CommitCommands();

                    while (!pendingEvents.empty()) {
                        if (pendingEvents.front() <= syncEventSet.LastCompletedEvent()) {
                            pendingEvents.pop_front();
                        } else {
                            break;
                        }
                    }
                }

                threadContext->CommitCommands();
                threadContext->GetDevice()->Stall();

                // finish any results as they come in
                // Note that we're assuming they finish in the order they were scheduled
                while (!pendingEvents.empty()) {
                    if (pendingEvents.front() <= syncEventSet.LastCompletedEvent()) {
                        pendingEvents.pop_front();
                    } else {
                        Threading::Sleep(8);
                    }
                }

                Assert::IsTrue(lastEventScheduled == syncEventSet.LastCompletedEvent());
            }
            ////////////////////////////////////////////////////////////////////////////////////////
        }

	};
}
