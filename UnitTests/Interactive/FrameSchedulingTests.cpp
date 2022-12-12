// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../UnitTestHelper.h"
#include "../RenderCore/Metal/MetalTestHelper.h"
#include "../RenderCore/Metal/MetalTestShaders.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/DeviceInitialization.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Metal/Shader.h"
#include "../../RenderCore/Metal/InputLayout.h"
#include "../../PlatformRig/OverlappedWindow.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetServices.h"
#include "../../xleres/FileList.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <chrono>

using namespace Catch::literals;

namespace UnitTests
{
	static const char psText_Expensive[] = 
		HLSLPrefix
		R"--(
			float4 main(float4 position : SV_Position) : SV_Target0
			{
				// just a muddle of expensive operation (that don't really mean anything)
				float4 A = log(exp(position) + sin(position)) / cos(position);
				float4 B = exp(rsqrt(position) + cos(position)) / log(position);
				A += sin(B);
				B -= rsqrt(saturate(A));
				return min(A, B) / max(A, B);
			}
		)--";

	struct ShaderKit
	{
		std::shared_ptr<RenderCore::ICompiledPipelineLayout> _pipelineLayout;
		std::shared_ptr<RenderCore::Metal::GraphicsPipeline> _pipeline;
		RenderCore::Techniques::FragmentStitchingContext::StitchResult _stitchedFrameBufferDesc;

		ShaderKit(MetalTestHelper& testHelper, UInt2 outputSize)
		{
			using namespace RenderCore;
			Techniques::FragmentStitchingContext stitchingContext;
			stitchingContext.DefineAttachment(
				Techniques::AttachmentSemantics::ColorLDR, 
				CreateDesc(BindFlag::TransferDst|BindFlag::RenderTarget, TextureDesc::Plain2D(outputSize[0], outputSize[1], Format::B8G8R8A8_UNORM_SRGB)),
				"color-ldr");

			Techniques::FrameBufferDescFragment fragment;
			fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR).NoInitialState();
			fragment.AddSubpass(std::move(SubpassDesc{}.AppendOutput(0)));
			_stitchedFrameBufferDesc = stitchingContext.TryStitchFrameBufferDesc(fragment);

			_pipelineLayout = testHelper._device->CreatePipelineLayout({}, "empty");
			auto shader = MakeShaderProgram(
				testHelper._shaderSource, _pipelineLayout,
				vsText_FullViewport,
				psText_Expensive);

			Metal::GraphicsPipelineBuilder pipelineBuilder;
			pipelineBuilder.Bind(shader);
			AttachmentBlendDesc blendDescs[] { Techniques::CommonResourceBox::s_abStraightAlpha };
			pipelineBuilder.Bind(blendDescs);
			pipelineBuilder.Bind(Techniques::CommonResourceBox::s_dsDisable);
			Metal::BoundInputLayout boundInputLayout{IteratorRange<const InputElementDesc*>{}, shader};
			pipelineBuilder.Bind(boundInputLayout, Topology::TriangleList);
			pipelineBuilder.SetRenderPassConfiguration(_stitchedFrameBufferDesc._fbDesc, 0);
			_pipeline = pipelineBuilder.CreatePipeline(Metal::GetObjectFactory());
		}
	};

	static unsigned EstimateLayersPerFrame(MetalTestHelper& testHelper, RenderCore::IThreadContext& threadContext, UInt2 outputSize)
	{
		// Get a rough estimate of the number of shader layers we can maintain at 60fps
		using namespace RenderCore;

		auto frameBufferPool = Techniques::CreateFrameBufferPool();
		auto attachmentPool = std::make_shared<Techniques::AttachmentPool>(threadContext.GetDevice());

		ShaderKit shaderKit{testHelper, outputSize};

		unsigned iterationCount = 8;
		unsigned currentLayerEstimateCount = 300;
		unsigned minEstimate = 0, maxEstimate = 2048;	// maxEstimate limits how far we will go on the most powerful hardware (consider increasing the complexity in the shader if this is a limitation)
		for (unsigned c=0; c<iterationCount; ++c) {
			threadContext.CommitCommands(CommitCommandsFlags::WaitForCompletion);
			{
				Techniques::RenderPassInstance rpi {
					threadContext,
					shaderKit._stitchedFrameBufferDesc._fbDesc,
					shaderKit._stitchedFrameBufferDesc._fullAttachmentDescriptions,
					*frameBufferPool, *attachmentPool };

				auto& metalContext = *Metal::DeviceContext::Get(threadContext);
				auto encoder = metalContext.BeginGraphicsEncoder(*shaderKit._pipelineLayout);
				encoder.DrawInstances(*shaderKit._pipeline, 4, currentLayerEstimateCount);
			}

			// Using the CPU to time the GPU (at least to get a rough estimate)
			auto preSubmit = std::chrono::steady_clock::now();
			threadContext.CommitCommands(CommitCommandsFlags::WaitForCompletion);
			auto postSubmit = std::chrono::steady_clock::now();

			auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(postSubmit-preSubmit).count();
			Log(Verbose) << "Completed " << currentLayerEstimateCount << " instances in " << microseconds / 1000.f << "ms" << std::endl;

			if (c == 0) {
				// first one tends to be odd
			} else {
				if (microseconds > 16000) maxEstimate = currentLayerEstimateCount;
				else minEstimate = currentLayerEstimateCount;
				currentLayerEstimateCount = (minEstimate + maxEstimate) / 2;
			}
		}

		return currentLayerEstimateCount;
	}

	TEST_CASE( "FrameScheduling-BasicTiming", "[rendercore_metal]" )
	{
		using namespace RenderCore;
		ConsoleRig::AttachablePtr<ConsoleRig::GlobalServices> globalServices;
		globalServices = std::make_shared<ConsoleRig::GlobalServices>();
		auto testHelper = MakeTestHelper();
		ConsoleRig::AttachablePtr<::Assets::Services> assetServices;
		if (!assetServices) assetServices = std::make_shared<::Assets::Services>();
		auto threadContext = testHelper->_device->GetImmediateContext();

		const UInt2 outputSize { 1920, 1080 };

		auto osWindow = std::make_unique<PlatformRig::OverlappedWindow>();
		osWindow->Resize(outputSize[0], outputSize[1]);

		RenderCore::PresentationChainDesc pChainDesc;
		auto presentationChain = testHelper->_device->CreatePresentationChain(osWindow->GetUnderlyingHandle(), pChainDesc);
		REQUIRE(presentationChain);

		EstimateLayersPerFrame(*testHelper, *threadContext, outputSize);
	}

}

