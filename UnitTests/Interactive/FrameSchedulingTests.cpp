// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../UnitTestHelper.h"
#include "../RenderCore/Metal/MetalTestHelper.h"
#include "../RenderCore/Metal/MetalTestShaders.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/Vulkan/IDeviceVulkan.h"
#include "../../RenderCore/DeviceInitialization.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Metal/Shader.h"
#include "../../RenderCore/Metal/InputLayout.h"
#include "../../PlatformRig/OverlappedWindow.h"
#include "../../OSServices/TimeUtils.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetServices.h"
#include "../../Utility/Profiling/CPUProfiler.h"
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
				float4 result = 0;
				float frequency = 1.0 / 64.0;
				float amplitude = 1.0;
				for (uint c=0; c<8; ++c) {
					result += amplitude * acos(sin(cos(position / frequency)));
					frequency /= 2.1042;	// lacunarity
					amplitude *= 0.5;		// gain
				}
				return result / 8;
			}
		)--";

	struct ShaderKit
	{
		std::shared_ptr<RenderCore::ICompiledPipelineLayout> _pipelineLayout;
		std::shared_ptr<RenderCore::Metal::GraphicsPipeline> _pipeline;
		RenderCore::Techniques::FragmentStitchingContext::StitchResult _stitchedFrameBufferDesc;

		ShaderKit(MetalTestHelper& testHelper, const RenderCore::PresentationChainDesc& presentationChainDesc)
		{
			using namespace RenderCore;
			Techniques::FragmentStitchingContext stitchingContext;
			stitchingContext.DefineAttachment(
				Techniques::AttachmentSemantics::ColorLDR, 
				CreateDesc(BindFlag::TransferDst|BindFlag::RenderTarget|BindFlag::PresentationSrc, TextureDesc::Plain2D(presentationChainDesc._width, presentationChainDesc._height, presentationChainDesc._format)),
				"color-ldr");

			Techniques::FrameBufferDescFragment fragment;
			fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR).NoInitialState().FinalState(BindFlag::PresentationSrc);
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
			pipelineBuilder.Bind(boundInputLayout, Topology::TriangleStrip);
			pipelineBuilder.SetRenderPassConfiguration(_stitchedFrameBufferDesc._fbDesc, 0);
			_pipeline = pipelineBuilder.CreatePipeline(Metal::GetObjectFactory());
		}
	};

	static std::pair<float, float> LinearRegression(IteratorRange<const std::pair<float, float>*> inputs)
	{
		assert(inputs.size() > 1);
		float sumX = 0, sumXSq = 0, sumY = 0, sumXY = 0;
		for (auto c:inputs) {
			sumX += c.first;
			sumXSq += c.first * c.first;
			sumY += c.second;
			sumXY += c.first * c.second;
		}

		float m = (inputs.size()*sumXY - sumX*sumY) / float(inputs.size()*sumXSq - sumX*sumX);
		float c = (sumY - m*sumX) / float(inputs.size());
		return { m, c };
	}

	static unsigned EstimateLayersPerFrame(MetalTestHelper& testHelper, ShaderKit& shaderKit)
	{
		// Get a rough estimate of the number of shader layers we can maintain at 60fps
		using namespace RenderCore;

		auto& threadContext = *testHelper._device->GetImmediateContext();
		auto frameBufferPool = Techniques::CreateFrameBufferPool();
		auto attachmentPool = std::make_shared<Techniques::AttachmentPool>(testHelper._device);

		unsigned iterationCount = 16;
		unsigned currentLayerEstimateCount = 300;
		unsigned minEstimate = 0, maxEstimate = 2048;	// maxEstimate limits how far we will go on the most powerful hardware (consider increasing the complexity in the shader if this is a limitation)
		using MillisecondsAndLayerCount = std::pair<float, float>;
		std::vector<MillisecondsAndLayerCount> results;
		for (unsigned c=0; c<iterationCount; ++c) {
			testHelper._device->Stall();

			currentLayerEstimateCount = 50 + 25 * c;

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
			std::cout << "Completed " << currentLayerEstimateCount << " instances in " << microseconds / 1000.f << "ms (" << minEstimate << "-" << maxEstimate << ")";

			if (c == 0) {
				// first one tends to be longer that subsequent iterations
			} else {
				results.emplace_back(microseconds / 1000.f, currentLayerEstimateCount);
				unsigned newGuess;
				if (results.size() > 1) {
					// try to use a linear best fit to separate any constant overheads / timing inaccuracies
					auto bestFit = LinearRegression(results);
					std::cout << " Regression: " << bestFit.first << ", " << bestFit.second;
					newGuess = 16.667f * bestFit.first + bestFit.second;
				} else {
					newGuess = currentLayerEstimateCount * 16667.f / microseconds;
				}

				currentLayerEstimateCount = std::clamp(newGuess, minEstimate, maxEstimate);
			}

			std::cout << std::endl;
		}

		std::cout << "Final guess " << currentLayerEstimateCount << std::endl;
		return currentLayerEstimateCount;
	}

	struct FrameRateConsistencyResults
	{
		float _meanIntervalMS;
		float _standardDeviationIntervalMS;
		float _maxIntervalMS;
		float _minIntervalMS;
		std::vector<float> _intervals;
	};

	static FrameRateConsistencyResults CalculateFrameRateConsistency(
		MetalTestHelper& testHelper, ShaderKit& shaderKit,
		RenderCore::IPresentationChain& presentationChain,
		unsigned layerCount, HierarchicalCPUProfiler* profiler)
	{
		using namespace RenderCore;
		
		auto& threadContext = *testHelper._device->GetImmediateContext();
		auto frameBufferPool = Techniques::CreateFrameBufferPool();
		auto attachmentPool = std::make_shared<Techniques::AttachmentPool>(testHelper._device);

		const unsigned framesToRender = 1*60;
		
		std::vector<std::chrono::steady_clock::time_point> intervalPoints;
		intervalPoints.reserve(framesToRender+1);

		testHelper._device->Stall();	// start from idle
		
		for (unsigned c=0; c<framesToRender+1; ++c) {
			auto presentationTarget = threadContext.BeginFrame(presentationChain);
			Techniques::AttachmentReservation frameReservation(*attachmentPool);
			frameReservation.Bind(Techniques::AttachmentSemantics::ColorLDR, presentationTarget, 0);
			{
				Techniques::RenderPassInstance rpi {
					threadContext,
					shaderKit._stitchedFrameBufferDesc._fbDesc,
					shaderKit._stitchedFrameBufferDesc._fullAttachmentDescriptions,
					*frameBufferPool, *attachmentPool, &frameReservation };

				auto& metalContext = *Metal::DeviceContext::Get(threadContext);
				auto encoder = metalContext.BeginGraphicsEncoder(*shaderKit._pipelineLayout);
				encoder.DrawInstances(*shaderKit._pipeline, 4, layerCount);
			}
			threadContext.Present(presentationChain);

			if (profiler) profiler->EndFrame();

			// we don't time the first few frame, because we'll use them to align with the vsync
			if (c > 2)
				intervalPoints.emplace_back(std::chrono::steady_clock::now());
		}
		REQUIRE(intervalPoints.size() >= 2);

		// calculate the statistics we're interested in
		FrameRateConsistencyResults results;
		results._meanIntervalMS = 0.f;
		results._minIntervalMS = std::numeric_limits<float>::max();
		results._maxIntervalMS = -std::numeric_limits<float>::max();
		results._intervals.reserve(intervalPoints.size()-1);
		for (auto i=intervalPoints.begin(); (i+1)!=intervalPoints.end(); ++i) {
			float interval = std::chrono::duration_cast<std::chrono::microseconds>(*(i+1)-*i).count() / 1000.f;
			results._meanIntervalMS += interval;
			results._minIntervalMS = std::min(results._minIntervalMS, interval);
			results._maxIntervalMS = std::max(results._maxIntervalMS, interval);
			results._intervals.push_back(interval);
		}
		auto N = intervalPoints.size()-1;
		results._meanIntervalMS /= N;
		float sumDiffSq = 0.f;
		for (auto i=intervalPoints.begin(); (i+1)!=intervalPoints.end(); ++i) {
			float interval = std::chrono::duration_cast<std::chrono::microseconds>(*(i+1)-*i).count() / 1000.f;
			sumDiffSq += (interval-results._meanIntervalMS)*(interval-results._meanIntervalMS);
		}
		results._standardDeviationIntervalMS = std::sqrt(sumDiffSq / (N-1));
		return results;
	}

	struct HierarchicalProfilerRecords : IHierarchicalProfiler
	{
		void AbsorbFrameData(IteratorRange<const void*> rawData)
		{
			FrameData fd;
			// fd._data.insert(fd._data.end(), (const uint8_t*)rawData.begin(), (const uint8_t*)rawData.end());
			fd._data = IHierarchicalProfiler::CalculateResolvedEvents(rawData);
			_frames.emplace_back(std::move(fd));
		}

		void LogEvents(std::ostream& str, const char evnt[])
		{
			auto freq = OSServices::GetPerformanceCounterFrequency();
			double divisor = freq/1000;
			bool pendingComma = false;
			for (const auto& fd:_frames) {
				if (pendingComma) str << ", ";
				pendingComma = true;
				
				uint64_t inclusiveTime = 0;
				for (auto& e:fd._data) 
					if (e._label == evnt) {
						inclusiveTime = e._inclusiveTime;
						break;
					}

				if (!inclusiveTime) {
					str << "{}";
				} else {
					str << double(inclusiveTime) / divisor << "ms";
				}
			}
		}

		struct FrameData
		{
			std::vector<ResolvedEvent> _data;
		};
		std::vector<FrameData> _frames;
	};

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
		pChainDesc._bindFlags = BindFlag::RenderTarget|BindFlag::TransferDst;
		pChainDesc._vsync = true;
		pChainDesc._imageCount = 3;
		auto presentationChain = testHelper->_device->CreatePresentationChain(osWindow->GetUnderlyingHandle(), pChainDesc);
		REQUIRE(presentationChain);
		osWindow->Show();

		ShaderKit shaderKit{*testHelper, presentationChain->GetDesc()};
		auto estimateLayers = EstimateLayersPerFrame(*testHelper, shaderKit);

		HierarchicalCPUProfiler profiler;
		HierarchicalProfilerRecords profilerRecords;
		profiler.AddEventListener([&profilerRecords](auto data){profilerRecords.AbsorbFrameData(data);});
		if (auto* vulkanThreadContext = query_interface_cast<IThreadContextVulkan*>(threadContext.get()))
			vulkanThreadContext->AttachCPUProfiler(&profiler);

		float gpuLoad = 1.2f;
		auto testResults = CalculateFrameRateConsistency(*testHelper, shaderKit, *presentationChain, estimateLayers * gpuLoad, &profiler);

		std::cout
			<< "At " << gpuLoad << " load, average interval " << testResults._meanIntervalMS << "ms, expected: " << 16.667f * gpuLoad << "ms (" << 1000.f/testResults._meanIntervalMS << "fps, " 
			<< testResults._minIntervalMS << "ms-" << testResults._maxIntervalMS << "ms, stddev: " << testResults._standardDeviationIntervalMS << "ms)" << std::endl;
		std::cout << "Intervals: " << testResults._intervals.front();
		for (auto i=testResults._intervals.begin()+1; i!=testResults._intervals.end(); ++i) std::cout << ", " << *i;
		std::cout << std::endl;

		std::cout << "Stall/command list: ";
		profilerRecords.LogEvents(std::cout, "Stall/commandlist");
		std::cout << std::endl;

		std::cout << "Stall/image: ";
		profilerRecords.LogEvents(std::cout, "Stall/image");
		std::cout << std::endl;
	}

}

