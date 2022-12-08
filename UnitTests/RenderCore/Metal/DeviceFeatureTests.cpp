// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MetalTestHelper.h"
#include "MetalTestShaders_HLSL.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../RenderCore/DeviceInitialization.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/Shader.h"
#include "../../../RenderCore/Metal/FrameBuffer.h"
#include "../../../RenderCore/Metal/InputLayout.h"
#include "../../../RenderCore/Metal/State.h"
#include "../../../RenderCore/Metal/QueryPool.h"
#include "../../../Math/Vector.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
	static RenderCore::UnderlyingAPI GetUnderlyingAPI()
	{
		#if GFXAPI_TARGET == GFXAPI_APPLEMETAL
			return RenderCore::UnderlyingAPI::AppleMetal;
		#elif GFXAPI_TARGET == GFXAPI_OPENGLES
			return RenderCore::UnderlyingAPI::OpenGLES;
		#elif GFXAPI_TARGET == GFXAPI_VULKAN
			return RenderCore::UnderlyingAPI::Vulkan;
		#elif GFXAPI_TARGET == GFXAPI_DX11
			return RenderCore::UnderlyingAPI::DX11;
		#else
			#error GFX-API not handled in GetUnderlyingAPI()
		#endif
	}

	std::string BuildSODefinesString(IteratorRange<const RenderCore::InputElementDesc*> desc);

	static void RunFeaturesTest(std::shared_ptr<RenderCore::IDevice> device)
	{
		using namespace RenderCore;
		MetalTestHelper testHelper{device};
		auto& factory = Metal::GetObjectFactory(*testHelper._device);
		UnitTestFBHelper simpleFBHelper{
			*testHelper._device, *testHelper._device->GetImmediateContext(),
			RenderCore::CreateDesc(BindFlag::RenderTarget, TextureDesc::Plain2D(256, 256, Format::R8G8B8A8_UNORM_SRGB))};

		SECTION("Geometry Shader")
		{
			// attempt to create a pipeline with a geometry shader
			Metal::ShaderProgram shader(
				factory,
				testHelper._pipelineLayout,
				MakeShader(testHelper._shaderSource, vsText, "vs_*"),
				MakeShader(testHelper._shaderSource, gsText_Passthrough, "gs_*"),
				MakeShader(testHelper._shaderSource, psText, "ps_*"));

			static InputElementDesc inputElePC[] = {
				InputElementDesc { "position", 0, Format::R32G32B32A32_FLOAT },
				InputElementDesc { "color", 0, Format::R8G8B8A8_UNORM }
			};
			Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputElePC), shader);

			Metal::GraphicsPipelineBuilder pipelineBuilder;
			AttachmentBlendDesc blendDescs[] { AttachmentBlendDesc{} };
			pipelineBuilder.Bind(blendDescs);
			pipelineBuilder.SetRenderPassConfiguration(simpleFBHelper.GetDesc(), 0);
			pipelineBuilder.Bind(inputLayout, Topology::TriangleList);
			pipelineBuilder.Bind(shader);

			if (factory.GetXLEFeatures()._geometryShaders) {
				REQUIRE_NOTHROW(pipelineBuilder.CreatePipeline(factory));
			} else {
				REQUIRE_THROWS(pipelineBuilder.CreatePipeline(factory));
			}
		}

		SECTION("View instancing frame buffer")
		{
			// attempt to create a frame buffer for view instancing
			std::vector<AttachmentDesc> attachments;
			attachments.push_back(AttachmentDesc{Format::B8G8R8A8_UNORM_SRGB});
			std::vector<SubpassDesc> subpasses;
			subpasses.push_back(SubpassDesc{}.AppendOutput(0).SetViewInstanceMask(~0u));

			FrameBufferDesc fbDesc{std::move(attachments), std::move(subpasses)};
			struct DummyNamedAttachments : public INamedAttachments
			{
				std::shared_ptr<IResourceView> GetResourceView(
					AttachmentName,
					BindFlag::Enum bindFlags, TextureViewDesc textureView,
					const AttachmentDesc&, const FrameBufferProperties&)
				{
					return _viewPool.GetTextureView(_mainTarget, bindFlags, textureView);
				}
				std::shared_ptr<RenderCore::IResource> _mainTarget;
				RenderCore::ViewPool _viewPool;
				DummyNamedAttachments(std::shared_ptr<RenderCore::IResource> t) : _mainTarget(std::move(t)) {}
			} dummyNamedAttachments { simpleFBHelper.GetMainTarget() };

			if (factory.GetXLEFeatures()._viewInstancingRenderPasses) {
				REQUIRE_NOTHROW(Metal::FrameBuffer{factory, fbDesc, dummyNamedAttachments});
			} else {
				REQUIRE_THROWS(Metal::FrameBuffer{factory, fbDesc, dummyNamedAttachments});
			}
		}

		SECTION("Stream Output")
		{
			// attempt to create a pipeline with stream output enabled
			const InputElementDesc soEles[] = { InputElementDesc("POINT", 0, Format::R32G32B32A32_FLOAT) };
			const unsigned soStrides[] = { (unsigned)sizeof(Float4) };
			
			auto vs = testHelper.MakeShader(vsText_JustPosition, "vs_5_0");
			auto gs = testHelper.MakeShader(gsText_StreamOutput, "gs_5_0", BuildSODefinesString(MakeIteratorRange(soEles)));
			Metal::ShaderProgram shaderProgram{
				Metal::GetObjectFactory(), testHelper._pipelineLayout,
				vs, gs, {},
				StreamOutputInitializers { MakeIteratorRange(soEles), MakeIteratorRange(soStrides) }};

			InputElementDesc inputEle[] = { InputElementDesc{"INPUT", 0, Format::R32G32B32A32_FLOAT} };
			Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputEle), shaderProgram);

			UnitTestFBHelper dummyFBHelper(*testHelper._device, *testHelper._device->GetImmediateContext());

			Metal::GraphicsPipelineBuilder pipelineBuilder;
			pipelineBuilder.SetRenderPassConfiguration(dummyFBHelper.GetDesc(), 0);
			pipelineBuilder.Bind(inputLayout, Topology::TriangleList);
			pipelineBuilder.Bind(shaderProgram);

			if (factory.GetXLEFeatures()._streamOutput) {
				REQUIRE_NOTHROW(pipelineBuilder.CreatePipeline(factory));
			} else {
				REQUIRE_THROWS(pipelineBuilder.CreatePipeline(factory));
			}
		}

		SECTION("Sampler Anisotrophy")
		{
			SamplerDesc sampler;
			sampler._filter = FilterMode::Anisotropic;
			if (factory.GetXLEFeatures()._samplerAnisotrophy) {
				REQUIRE_NOTHROW(testHelper._device->CreateSampler(sampler));
			} else {
				REQUIRE_THROWS(testHelper._device->CreateSampler(sampler));
			}
		}

		SECTION("Conservative Raster")
		{
			RasterizationDesc rasterDesc;
			rasterDesc._flags = RasterizationDescFlags::ConservativeRaster;

			Metal::ShaderProgram shader(
				factory,
				testHelper._pipelineLayout,
				MakeShader(testHelper._shaderSource, vsText, "vs_*"),
				MakeShader(testHelper._shaderSource, psText, "ps_*"));

			static InputElementDesc inputElePC[] = {
				InputElementDesc { "position", 0, Format::R32G32B32A32_FLOAT },
				InputElementDesc { "color", 0, Format::R8G8B8A8_UNORM }
			};
			Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputElePC), shader);

			Metal::GraphicsPipelineBuilder pipelineBuilder;
			AttachmentBlendDesc blendDescs[] { AttachmentBlendDesc{} };
			pipelineBuilder.Bind(blendDescs);
			pipelineBuilder.SetRenderPassConfiguration(simpleFBHelper.GetDesc(), 0);
			pipelineBuilder.Bind(inputLayout, Topology::TriangleList);
			pipelineBuilder.Bind(shader);
			pipelineBuilder.Bind(rasterDesc);

			if (factory.GetXLEFeatures()._conservativeRaster) {
				REQUIRE_NOTHROW(pipelineBuilder.CreatePipeline(factory));
			} else {
				REQUIRE_THROWS(pipelineBuilder.CreatePipeline(factory));
			}
		}

		SECTION("Query types")
		{
			if (factory.GetXLEFeatures()._queryShaderInvocation) {
				REQUIRE_NOTHROW(Metal::QueryPool{factory, Metal::QueryPool::QueryType::ShaderInvocations, 8});
			} else {
				REQUIRE_THROWS(Metal::QueryPool{factory, Metal::QueryPool::QueryType::ShaderInvocations, 8});
			}

			if (factory.GetXLEFeatures()._queryStreamOutput) {
				REQUIRE_NOTHROW(Metal::QueryPool{factory, Metal::QueryPool::QueryType::StreamOutput_Stream0, 8});
			} else {
				REQUIRE_THROWS(Metal::QueryPool{factory, Metal::QueryPool::QueryType::StreamOutput_Stream0, 8});
			}
		}
	}

	TEST_CASE( "DeviceFeatures-ThrowOnDisabledFeature", "[rendercore_metal]" )
	{
		// Attempting to use certain metal features should fail if the corresponding feature flags was set false on device
		// construction
		// Generally we throw exceptions on during construction operations, functions called during the rendering process
		// might just be asserts

		auto renderAPI = CreateAPIInstance(GetUnderlyingAPI());
		SECTION("features disabled")
		{
			auto renderDevice = renderAPI->CreateDevice(0, {});		// creating using default physical device & no features
			RunFeaturesTest(renderDevice);
		}
		SECTION("features enabled")
		{
			// note that we can't check correct construction for features that aren't supported by the current driver/hardware
			auto capabilities = renderAPI->QueryFeatureCapability(0);
			auto renderDevice = renderAPI->CreateDevice(0, capabilities);
			RunFeaturesTest(renderDevice);
		}
	}
}

