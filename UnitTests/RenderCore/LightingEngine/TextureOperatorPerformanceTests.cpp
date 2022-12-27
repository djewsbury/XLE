// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineTestHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../RenderCore/Techniques/PipelineCollection.h"
#include "../../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../../RenderCore/Techniques/PipelineOperators.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Techniques/PipelineLayoutDelegate.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/QueryPool.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
#include "../../../RenderCore/Metal/InputLayout.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../../Math/Transformations.h"
#include "../../../Assets/IAsyncMarker.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../xleres/FileList.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace std::chrono_literals;
using namespace Utility::Literals;

namespace UnitTests
{
	//
	// from 2560x1440 Format::R8G8B8A8_UNORM -> Format::R8_UNORM
	// -> 1/4 downsample with 4*bilinear 									0.0534ms
	// -> 1/4 downsample with 1*bilinear in 2x2 pattern 					0.026ms
	// -> 1/4 downsample with 16*Texture.Load 								0.0530ms
	// -> 1/4 downsample with 16*Texture.Load (dynamic sampler)				0.0528ms
	// -> 1/3 downsample with 1*bilinear in 2x2 pattern 					0.0395ms			408108 pixels vs 230400 (1.77 ratio)
	// -> 1/3 downsample with 9*Texture.Load 								0.0622ms
	// -> 1/3 downsample with 9*Texture.Load (to R8G8B8A8_UNORM) 			0.0680ms
	// -> 1/3 downsample with 9*Texture.Load (from R11G11B10_FLOAT) 		0.0624ms
	// -> 1/3 downsample with 9*Texture.Load (from R32G32B32A32_FLOAT) 		0.3627ms
	// -> 1/3 downsample with 9*Texture.Load with 3 hideable rsqrts			0.0622ms
	// -> 1/3 downsample with 9*Texture.Load with 3 unhideable rsqrts		0.0623ms
	// -> 1/3 downsample with 9*Texture.Load with random access pattern		0.0728ms
	// -> 1/3 downsample with 9*Texture.Load with first 16 source texels	0.0143ms		(even here unhideable trig & rsqrts, not a big impact)
	//
	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair("downsample.pixel.hlsl", ::Assets::AsBlob(R"--(
			Texture2D InputTexture : register(t0, space0);
			
			SamplerState BilinearClampSampler : register(s14, space0);
			SamplerState UnnormalizedBilinearClampSampler : register(s15, space0);
			SamplerState DynamicSampler : register(s12, space1);

			float4 main(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
			{
				// Position is offset for the texel center
				// so the first pixel in the top left will get position.xy = (0.5, 0.5)
				// When we scale for the input texture, we still end up in the center of the block of
				// pixels we're sampling from.
				// For example, if we use 4*position.xy, we end up sampling the 2x2 pixels in the center of each 4x4 block

				// float2 offset = (floor(position.xy)%2) * 2.0.xx - 1.0.xx; 
				// return float4(InputTexture.SampleLevel(UnnormalizedBilinearClampSampler, 4*position.xy + offset, 0).rgb, 1);
				/*return float4(
					( InputTexture.SampleLevel(UnnormalizedBilinearClampSampler, 4*position.xy + float2(-1,-1), 0).rgb
					+ InputTexture.SampleLevel(UnnormalizedBilinearClampSampler, 4*position.xy + float2( 1,-1), 0).rgb
					+ InputTexture.SampleLevel(UnnormalizedBilinearClampSampler, 4*position.xy + float2(-1, 1), 0).rgb
					+ InputTexture.SampleLevel(UnnormalizedBilinearClampSampler, 4*position.xy + float2( 1, 1), 0).rgb) * 0.25, 
					1);*/
				/*return float4(
					( InputTexture.SampleLevel(DynamicSampler, 4*position.xy + float2(-1,-1), 0).rgb
					+ InputTexture.SampleLevel(DynamicSampler, 4*position.xy + float2( 1,-1), 0).rgb
					+ InputTexture.SampleLevel(DynamicSampler, 4*position.xy + float2(-1, 1), 0).rgb
					+ InputTexture.SampleLevel(DynamicSampler, 4*position.xy + float2( 1, 1), 0).rgb) * 0.25, 
					1);*/
				
				/*int2 base = int2(position.xy);
				float3 result = InputTexture.Load(int3(4*base + int2(0,0), 0)).rgb;
				result += InputTexture.Load(int3(4*base + int2(1,0), 0)).rgb;
				result += InputTexture.Load(int3(4*base + int2(0,1), 0)).rgb;
				result += InputTexture.Load(int3(4*base + int2(1,1), 0)).rgb;

				result += InputTexture.Load(int3(4*base + int2(2,0), 0)).rgb;
				result += InputTexture.Load(int3(4*base + int2(3,0), 0)).rgb;
				result += InputTexture.Load(int3(4*base + int2(2,1), 0)).rgb;
				result += InputTexture.Load(int3(4*base + int2(3,1), 0)).rgb;

				result += InputTexture.Load(int3(4*base + int2(0,2), 0)).rgb;
				result += InputTexture.Load(int3(4*base + int2(1,2), 0)).rgb;
				result += InputTexture.Load(int3(4*base + int2(0,3), 0)).rgb;
				result += InputTexture.Load(int3(4*base + int2(1,3), 0)).rgb;

				result += InputTexture.Load(int3(4*base + int2(2,2), 0)).rgb;
				result += InputTexture.Load(int3(4*base + int2(3,2), 0)).rgb;
				result += InputTexture.Load(int3(4*base + int2(2,3), 0)).rgb;
				result += InputTexture.Load(int3(4*base + int2(3,3), 0)).rgb;
				return float4(result * 0.0625f, 1);*/

				/*float2 offset = (floor(position.xy)%2) - 0.5.xx; 
				return float4(InputTexture.SampleLevel(UnnormalizedBilinearClampSampler, 3*position.xy + offset, 0).rgb, 1);*/

				int2 base = int2(position.xy);
				base %= 4;
				// base.x = (3482 * base.x) % 2560;
				// base.y = (1723 * base.y) % 1440;
				float3 result = InputTexture.Load(int3(3*base + int2(0,0), 0)).rgb;
				// float v = rsqrt(result.x);
				result += InputTexture.Load(int3(3*base + int2(1,0), 0)).rgb;
				result += InputTexture.Load(int3(3*base + int2(0,1), 0)).rgb;
				// float v2 = rsqrt(result.x);
				result += InputTexture.Load(int3(3*base + int2(1,1), 0)).rgb;
				result += InputTexture.Load(int3(3*base + int2(2,0), 0)).rgb;
				// float v3 = rsqrt(result.x);
				result += InputTexture.Load(int3(3*base + int2(2,1), 0)).rgb;
				result += InputTexture.Load(int3(3*base + int2(0,2), 0)).rgb;
				result += InputTexture.Load(int3(3*base + int2(1,2), 0)).rgb;
				result += InputTexture.Load(int3(3*base + int2(2,2), 0)).rgb;
				// result += v + v2 + v3;
				result *= 0.1111f;
				// result.x = rsqrt(result.x);
				// result.y = rsqrt(result.y);
				// result.z = rsqrt(result.z);
				// result.x = sin(result.x);
				// result.y = cos(result.y);
				// result.z = sin(result.z);
				return float4(result, 1);
			}
		)--")),

		std::make_pair("pattern0.pixel.hlsl", ::Assets::AsBlob(R"--(
			float4 main(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
			{
				if ((position.x+position.y)%2 == 0) {
					return 1.0.xxxx;
				} else {
					return float4(0.0.xxx, 1);
				}
			}
		)--")),

		std::make_pair("pattern1.pixel.hlsl", ::Assets::AsBlob(R"--(
			float4 main(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
			{
				uint2 p = position.xy % 4;
				if ((p.x == 1 || p.x == 2) && (p.y == 1 || p.y == 2))
					return float4(0.0.xxx, 1);
				return 1.0.xxxx;
			}
		)--")),

		std::make_pair("minimal_compute.pipeline", ::Assets::AsBlob(R"--(
			DescriptorSet ds
			{
				SampledTexture InputTexture;
				UnorderedAccessTexture OutputTexture;
				Sampler DynamicSampler;
			};
			PipelineLayout ComputeMain
			{
				ComputeDescriptorSet ds;
			};
		)--")),

		std::make_pair("downsample.compute.hlsl", ::Assets::AsBlob(R"--(
			Texture2D<float> InputTexture : register(t0, space0);
			RWTexture2D<float> OutputTexture : register(u1, space0);
			SamplerState DynamicSampler : register(s2, space0);

			// [numthreads(16, 8, 1)]
			[numthreads(2, 2, 1)]
				void main(uint3 dispatchThreadId : SV_DispatchThreadID)
			{
				// OutputTexture[dispatchThreadId.xy] = InputTexture.Load(uint3(dispatchThreadId.xy*4, 0));

				float result = InputTexture.Load(uint3(dispatchThreadId.xy*4, 0));
				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(1,0), 0));
				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(0,1), 0));
				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(1,1), 0));

				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(2,0), 0));
				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(3,0), 0));
				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(2,1), 0));
				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(3,1), 0));

				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(0,2), 0));
				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(1,2), 0));
				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(0,3), 0));
				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(1,3), 0));

				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(2,2), 0));
				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(3,2), 0));
				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(2,3), 0));
				result += InputTexture.Load(uint3(dispatchThreadId.xy*4 + uint2(3,3), 0));

				OutputTexture[dispatchThreadId.xy] = result * 0.0625f;
			}
		)--"))

	};

	template<typename Type>
		static std::shared_ptr<Type> StallAndRequireReady(::Assets::MarkerPtr<Type>& future)
	{
		future.StallWhilePending();
		INFO(::Assets::AsString(future.GetActualizationLog()));
		REQUIRE(future.GetAssetState() == ::Assets::AssetState::Ready);
		return future.Actualize();
	}

	static void WriteDownsampleInput(
		LightingEngineTestApparatus& testApparatus, 
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::Techniques::RenderPassInstance& rpi,
		ToolsRig::IDrawablesWriter& drawableWriter)
	{
		using namespace RenderCore;
		std::promise<std::shared_ptr<Techniques::ITechniqueDelegate>> promisedTechDel;
		auto futureTechDel = promisedTechDel.get_future();
		RenderCore::Techniques::CreateTechniqueDelegate_Utility(
			std::move(promisedTechDel),
			testApparatus._sharedDelegates->GetTechniqueSetFile(),
			RenderCore::Techniques::UtilityDelegateType::CopyDiffuseAlbedo);
		auto sequencerConfig = testApparatus._pipelineAccelerators->CreateSequencerConfig(
			"WriteDownsampleInput",
			futureTechDel.get(),		// note -- stall
			{}, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());

		if (1) {
			Techniques::DrawablesPacket pkt;
			drawableWriter.WriteDrawables(pkt);
			auto newVisibility = PrepareAndStall(testApparatus, *sequencerConfig, pkt);
			parsingContext.SetPipelineAcceleratorsVisibility(newVisibility._pipelineAcceleratorsVisibility);
			parsingContext.RequireCommandList(newVisibility._bufferUploadsVisibility);
			Techniques::Draw(
				parsingContext,
				*testApparatus._pipelineAccelerators,
				*sequencerConfig,
				pkt);
		}

		if (0) {
			UniformsStreamInterface usi;
			UniformsStream us;
			Techniques::PixelOutputStates outputStates;
			outputStates.Bind(rpi);
			outputStates.Bind(Techniques::CommonResourceBox::s_dsDisable);
			AttachmentBlendDesc blendStates[] { Techniques::CommonResourceBox::s_abStraightAlpha };
			outputStates.Bind(MakeIteratorRange(blendStates));
			auto op = Techniques::CreateFullViewportOperator(
				testApparatus._pipelineCollection,
				Techniques::FullViewportOperatorSubType::DisableDepth,
				"ut-data/pattern1.pixel.hlsl:main",
				{}, testApparatus._metalTestHelper->_pipelineLayout,
				outputStates, usi);

			REQUIRE(op->StallWhilePending().value() == ::Assets::AssetState::Ready);
			op->Actualize()->Draw(parsingContext, us);
		}
	}

	static void PixelShaderBasedDownsample(
		LightingEngineTestApparatus& testApparatus, 
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::Techniques::RenderPassInstance& rpi,
		RenderCore::IResourceView& inputSRV,
		RenderCore::Techniques::CommonResourceBox& commonResourceBox)
	{
		using namespace RenderCore;
		UniformsStreamInterface usi;
		usi.BindResourceView(0, "InputTexture"_h);
		usi.BindSampler(0, "DynamicSampler"_h);
		UniformsStream us;
		IResourceView* srvs[] = { &inputSRV };
		us._resourceViews = MakeIteratorRange(srvs);
		ISampler* samplers[] = { commonResourceBox._unnormalizedBilinearClampSampler.get() };
		us._samplers = MakeIteratorRange(samplers);

		Techniques::PixelOutputStates outputStates;
		outputStates.Bind(rpi);
		outputStates.Bind(Techniques::CommonResourceBox::s_dsDisable);
		AttachmentBlendDesc blendStates[] { Techniques::CommonResourceBox::s_abStraightAlpha };
		outputStates.Bind(MakeIteratorRange(blendStates));
		auto op = Techniques::CreateFullViewportOperator(
			testApparatus._pipelineCollection,
			Techniques::FullViewportOperatorSubType::DisableDepth,
			"ut-data/downsample.pixel.hlsl:main",
			{}, testApparatus._metalTestHelper->_pipelineLayout,
			outputStates, usi);

		REQUIRE(op->StallWhilePending().value() == ::Assets::AssetState::Ready);
		op->Actualize()->Draw(parsingContext, us);
	}

	static void ComputeShaderBasedDownsample(
		LightingEngineTestApparatus& testApparatus, 
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::IResourceView& outputUAV,
		RenderCore::IResourceView& inputSRV,
		std::shared_ptr<RenderCore::Techniques::CommonResourceBox>& commonResourceBox)
	{
		using namespace RenderCore;
		UniformsStreamInterface usi;
		usi.BindResourceView(0, "InputTexture"_h);
		usi.BindResourceView(1, "OutputTexture"_h);
		usi.BindSampler(0, "DynamicSampler"_h);
		UniformsStream us;
		IResourceView* srvs[] = { &inputSRV, &outputUAV };
		us._resourceViews = MakeIteratorRange(srvs);
		ISampler* samplers[] = { commonResourceBox->_unnormalizedBilinearClampSampler.get() };
		us._samplers = MakeIteratorRange(samplers);

		auto pipelineLayouts = ::Assets::ActualizeAssetPtr<Techniques::CompiledPipelineLayoutAsset>(
			testApparatus._metalTestHelper->_device,
			"ut-data/minimal_compute.pipeline:ComputeMain");
		
		auto op = Techniques::CreateComputeOperator(
			testApparatus._pipelineCollection,
			pipelineLayouts->GetPipelineLayout(),
			"ut-data/downsample.compute.hlsl:main",
			{}, usi);

		REQUIRE(op->StallWhilePending().value() == ::Assets::AssetState::Ready);
		op->Actualize()->Dispatch(
			parsingContext,
			// 640/16, 360/8, 1,
			640/2, 360/2, 1,
			us);
	}

	static RenderCore::Techniques::CameraDesc SetupCamera(UInt2 workingRes)
	{
		RenderCore::Techniques::CameraDesc camera;
		camera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{-1.f, 0.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, 1.0f}), Float3{10.0f, 0.f, 0.0f});
		camera._projection = RenderCore::Techniques::CameraDesc::Projection::Orthogonal;
		camera._nearClip = 0.0f;
		camera._farClip = 100.f;
		const auto aspectRatio = workingRes[0] / (float)workingRes[1]; 
		camera._left = -2.f * aspectRatio;
		camera._top = 2.f;
		camera._right = 2.f * aspectRatio;
		camera._bottom = -2.f;
		return camera;
	}

	static std::shared_ptr<RenderCore::IResourceView> DrawStartingImage(LightingEngineTestApparatus& testApparatus, RenderCore::Techniques::ParsingContext& parsingContext)
	{
		using namespace RenderCore;

		auto drawableWriter = ToolsRig::DrawablesWriterHelper(*testApparatus._pipelineAccelerators->GetDevice(), *testApparatus._drawablesPool, *testApparatus._pipelineAccelerators).CreateShapeStackDrawableWriter();

		Techniques::FrameBufferDescFragment fragDesc;
			
		// write-input-texture
		// output 0: some arbitrary pixels for downsampling (Format::R11G11B10_FLOAT precision)
		SubpassDesc writeInputTexture;
		writeInputTexture.SetName("write-input-texture");
		auto preDownsampleAttachment = fragDesc.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR)
			.Clear().FinalState(BindFlag::ShaderResource);
		writeInputTexture.AppendOutput(preDownsampleAttachment);
		fragDesc.AddSubpass(std::move(writeInputTexture));

		Techniques::RenderPassInstance rpi { parsingContext, fragDesc };
		WriteDownsampleInput(testApparatus, parsingContext, rpi, *drawableWriter);
		return rpi.GetOutputAttachmentSRV(0, {});
	}

	static RenderCore::Metal::TimeStampQueryPool::FrameResults StallAndGetFrameResults(RenderCore::Metal::DeviceContext& metalContext, RenderCore::Metal::TimeStampQueryPool& queryPool, unsigned frameId)
	{
		for (;;) {
			auto queryResults = queryPool.GetFrameResults(metalContext, frameId);
			if (queryResults._resultsReady) {
				REQUIRE(queryResults._resultsEnd != queryResults._resultsStart);
				REQUIRE(queryResults._frequency != 0);
				return queryResults;
			}
		}
	}

	TEST_CASE( "LightingEngine-DownsamplePerformance", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto mnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));

		UInt2 workingRes { 2560, 1440 };
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::ShaderResource,
			TextureDesc::Plain2D(workingRes[0], workingRes[1], Format::R8G8B8A8_UNORM_SRGB /*Format::R11G11B10_FLOAT*/ /*Format::R32G32B32A32_FLOAT*/ ));

		auto parsingContext = BeginParsingContext(testApparatus, *threadContext, targetDesc, SetupCamera(workingRes));
		parsingContext.GetFragmentStitchingContext()._workingProps._width = workingRes[0];
		parsingContext.GetFragmentStitchingContext()._workingProps._height = workingRes[1];

		testHelper->BeginFrameCapture();

		const auto downsampledResult = "Downsampled"_h;
		auto commonResourceBox = std::make_shared<Techniques::CommonResourceBox>(*testHelper->_device);

		{
			Metal::TimeStampQueryPool queryPool(Metal::GetObjectFactory());
			auto queryPoolFrameId = queryPool.BeginFrame(*Metal::DeviceContext::Get(*threadContext));
			const unsigned iterationCount = 512;

			auto downsampleSrcSRV = DrawStartingImage(testApparatus, parsingContext);
			std::shared_ptr<IResource> downsampledResource = nullptr;
			if (1) {
				// downsample
				// input 0: attachment to downsample
				// output 0: downsampled result
				parsingContext.GetFragmentStitchingContext().DefineAttachment(
					Techniques::PreregisteredAttachment {
						downsampledResult,
						CreateDesc(
							BindFlag::RenderTarget,
							TextureDesc::Plain2D(workingRes[0]/3, workingRes[1]/3, Format::R8_UNORM)),
						"downsampled-attachment",
						Techniques::PreregisteredAttachment::State::Uninitialized
					});
				Techniques::FrameBufferDescFragment fragDesc;
				SubpassDesc downsampleStep;
				downsampleStep.SetName("downsample");
				downsampleStep.AppendOutput(fragDesc.DefineAttachment(downsampledResult).FixedFormat(Format::R8_UNORM).NoInitialState());
				fragDesc.AddSubpass(std::move(downsampleStep));

				Techniques::RenderPassInstance rpi { parsingContext, fragDesc };
				queryPool.SetTimeStampQuery(*Metal::DeviceContext::Get(*threadContext));
				for (unsigned c=0; c<iterationCount; ++c)
					PixelShaderBasedDownsample(testApparatus, parsingContext, rpi, *downsampleSrcSRV, *commonResourceBox);
				queryPool.SetTimeStampQuery(*Metal::DeviceContext::Get(*threadContext));
				downsampledResource = rpi.GetOutputAttachmentResource(0);
			}

			if (0) {
				auto downsampledDesc = CreateDesc(
					BindFlag::UnorderedAccess,
					TextureDesc::Plain2D(workingRes[0] / 4, workingRes[1] / 4, Format::R8_UNORM));
				downsampledResource = testApparatus._metalTestHelper->_device->CreateResource(downsampledDesc, "downsampled");
				Metal::CompleteInitialization(*Metal::DeviceContext::Get(*threadContext), { downsampledResource.get() });
				auto downsampleDstUAV = downsampledResource->CreateTextureView(BindFlag::UnorderedAccess);

				queryPool.SetTimeStampQuery(*Metal::DeviceContext::Get(*threadContext));
				for (unsigned c=0; c<iterationCount; ++c)
					ComputeShaderBasedDownsample(testApparatus, parsingContext, *downsampleDstUAV, *downsampleSrcSRV, commonResourceBox);
				queryPool.SetTimeStampQuery(*Metal::DeviceContext::Get(*threadContext));
			}

			queryPool.EndFrame(*Metal::DeviceContext::Get(*threadContext), queryPoolFrameId);
			threadContext->CommitCommands();
			auto queryResults = StallAndGetFrameResults(*Metal::DeviceContext::Get(*threadContext), queryPool, queryPoolFrameId);
			auto elapsed = *(queryResults._resultsStart+1) - *queryResults._resultsStart;
			std::cout << "Pixel shader based downsample: " << elapsed / float(queryResults._frequency) * 1000.0f / float(iterationCount) << "ms" << std::endl;

			// SaveImage(*threadContext, *downsampleSrcSRV->GetResource(), "downsampled-src");
			SaveImage(*threadContext, *downsampledResource, "downsampled");
		}

		testHelper->EndFrameCapture();

		::Assets::MainFileSystem::GetMountingTree()->Unmount(mnt);
	}

	static RenderCore::Techniques::ComputePipelineAndLayout ActualizePipeline(
		RenderCore::Techniques::PipelineCollection& pipelineCollection,
		RenderCore::Techniques::PipelineLayoutOptions&& pipelineLayout,
		StringSection<> shader,
		const ParameterBox& selectors = {})
	{
		const ParameterBox* pBoxes[] { &selectors };
		std::promise<RenderCore::Techniques::ComputePipelineAndLayout> promisedPipeline;
		auto futurePipeline = promisedPipeline.get_future();
		pipelineCollection.CreateComputePipeline(std::move(promisedPipeline), std::move(pipelineLayout), shader, pBoxes);
		return futurePipeline.get();
	}

	TEST_CASE( "LightingEngine-BlurPerformance", "[rendercore_lighting_engine]" )
	{
		// Testing performance for blurring operators (eg, for bloom)

		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto mnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));

		UInt2 workingRes { 2560, 1440 };
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::ShaderResource,
			TextureDesc::Plain2D(workingRes[0], workingRes[1], Format::R8G8B8A8_UNORM_SRGB /*Format::R11G11B10_FLOAT*/ /*Format::R32G32B32A32_FLOAT*/ ));

		auto parsingContext = BeginParsingContext(testApparatus, *threadContext, targetDesc, SetupCamera(workingRes));
		parsingContext.GetFragmentStitchingContext()._workingProps._width = workingRes[0];
		parsingContext.GetFragmentStitchingContext()._workingProps._height = workingRes[1];

		auto& commonResources = *Techniques::Services::GetCommonResources();
		auto predefinedPipelineLayout = ::Assets::ActualizeAssetPtr<RenderCore::Assets::PredefinedPipelineLayout>(BLOOM_PIPELINE ":ComputeMain");
		auto compiledPipelineLayout = testApparatus._pipelineCollection->GetDevice()->CreatePipelineLayout(
			predefinedPipelineLayout->MakePipelineLayoutInitializer(Techniques::GetDefaultShaderLanguage(), &commonResources._samplerPool),
			"tone-map-aces");

		auto brightPassFilter = ActualizePipeline(*testApparatus._pipelineCollection, compiledPipelineLayout, BLOOM_COMPUTE_HLSL ":BrightPassFilter");
		auto fastMipChain = ActualizePipeline(*testApparatus._pipelineCollection, compiledPipelineLayout, BLOOM_COMPUTE_HLSL ":FastMipChain");
		auto upsampleStep = ActualizePipeline(*testApparatus._pipelineCollection, compiledPipelineLayout, BLOOM_COMPUTE_HLSL ":UpsampleStep");

		const unsigned s_shaderMipChainUniformCount = 6;
		std::shared_ptr<Metal::BoundUniforms> brightPassBoundUniforms;
		{
			UniformsStreamInterface brightPassUsi;
			brightPassUsi.BindResourceView(0, "HDRInput"_h);
			brightPassUsi.BindResourceView(1, "AtomicBuffer"_h);
			brightPassUsi.BindResourceView(2, "MipChainSRV"_h);
			for (unsigned c=0; c<s_shaderMipChainUniformCount; ++c)
				brightPassUsi.BindResourceView(3+c, "MipChainUAV"_h+c);
			UniformsStreamInterface usi2;
			usi2.BindImmediateData(0, "ControlUniforms"_h);
			brightPassBoundUniforms = std::make_shared<Metal::BoundUniforms>(compiledPipelineLayout, brightPassUsi, usi2);
		}

		testHelper->BeginFrameCapture();

		{
			Metal::TimeStampQueryPool queryPool(Metal::GetObjectFactory());
			auto queryPoolFrameId = queryPool.BeginFrame(*Metal::DeviceContext::Get(*threadContext));
			const unsigned iterationCount = 512;

			auto downsampleSrcSRV = DrawStartingImage(testApparatus, parsingContext);

			auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
			Metal::BarrierHelper(metalContext).Add(
				*downsampleSrcSRV->GetResource(),
				Metal::BarrierResourceUsage{BindFlag::RenderTarget},
				Metal::BarrierResourceUsage{BindFlag::ShaderResource, ShaderStage::Compute});

			if (1) {
				auto brightPassMipCountCount = IntegerLog2(std::max(workingRes[0], workingRes[1])) - 1;
				brightPassMipCountCount = std::min(brightPassMipCountCount, s_shaderMipChainUniformCount);

				auto atomicBuffer = testHelper->_device->CreateResource(
					CreateDesc(
						BindFlag::TransferDst | BindFlag::UnorderedAccess | BindFlag::TexelBuffer,
						LinearBufferDesc::Create(4*4)),
					"atomic-counter");
				auto atomicCounterBufferView = atomicBuffer->CreateTextureView(BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});

				// setup "render pass" and begin...
				parsingContext.GetFragmentStitchingContext().DefineAttachment(
					Techniques::PreregisteredAttachment {
						"blur-mip-chain"_h,
						CreateDesc(
							BindFlag::UnorderedAccess | BindFlag::ShaderResource,
							TextureDesc::Plain2D(workingRes[0]/2, workingRes[1]/2, Format::B8G8R8A8_UNORM, brightPassMipCountCount)),
						"blur-mip-chain",
						Techniques::PreregisteredAttachment::State::Uninitialized
					});
				Techniques::FrameBufferDescFragment fragDesc;
				fragDesc._pipelineType = PipelineType::Compute;
				Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
				spDesc.SetName("downsample-test");
				auto attachment = fragDesc.DefineAttachment("blur-mip-chain"_h).NoInitialState();
				spDesc.AppendNonFrameBufferAttachmentView(attachment, BindFlag::ShaderResource);
				for (unsigned c=0; c<brightPassMipCountCount; ++c) {
					TextureViewDesc view;
					view._mipRange._min = c;
					view._mipRange._count = 1;
					spDesc.AppendNonFrameBufferAttachmentView(attachment, BindFlag::UnorderedAccess, view);
				}
				fragDesc.AddSubpass(std::move(spDesc));
				Techniques::RenderPassInstance rpi { parsingContext, fragDesc };

				Metal::BarrierHelper(metalContext).Add(
					*rpi.GetNonFrameBufferAttachmentView(0)->GetResource(),
					Metal::BarrierResourceUsage::NoState(),
					Metal::BarrierResourceUsage{BindFlag::UnorderedAccess, ShaderStage::Compute});
				auto mipChainTopWidth = workingRes[0]/2, mipChainTopHeight = workingRes[1]/2;

				auto encoder = metalContext.BeginComputeEncoder(*compiledPipelineLayout);
				Metal::CapturedStates capturedStates;
				encoder.BeginStateCapture(capturedStates);

				// setup uniforms

				{
					VLA(const IResourceView*, views, 3+s_shaderMipChainUniformCount);
					views[0] = downsampleSrcSRV.get();
					views[1] = atomicCounterBufferView.get();
					views[2] = rpi.GetNonFrameBufferAttachmentView(0).get();
					unsigned c=0;
					for (; c<brightPassMipCountCount; ++c) views[3+c] = rpi.GetNonFrameBufferAttachmentView(1+c).get();
					auto* dummyUav = Techniques::Services::GetCommonResources()->_black2DSRV.get();
					for (; c<s_shaderMipChainUniformCount; ++c) views[3+c] = dummyUav;

					UniformsStream uniforms;
					uniforms._resourceViews = MakeIteratorRange(views, views+3+s_shaderMipChainUniformCount);
					brightPassBoundUniforms->ApplyLooseUniforms(
						metalContext, encoder,
						uniforms);
				}

				// "bright pass filter" step

				{
					queryPool.SetTimeStampQuery(*Metal::DeviceContext::Get(*threadContext));
					for (unsigned c=0; c<iterationCount; ++c) {
						const unsigned dispatchGroupWidth = 8;
						const unsigned dispatchGroupHeight = 8;
						encoder.Dispatch(
							*brightPassFilter._pipeline,
							(mipChainTopWidth + dispatchGroupWidth - 1) / dispatchGroupWidth,
							(mipChainTopHeight + dispatchGroupHeight - 1) / dispatchGroupHeight,
							1);
					}
					Metal::BarrierHelper(metalContext).Add(
						*rpi.GetNonFrameBufferAttachmentView(0)->GetResource(), TextureViewDesc::SubResourceRange{0, 1}, TextureViewDesc::All,
						Metal::BarrierResourceUsage{BindFlag::UnorderedAccess, ShaderStage::Compute},
						Metal::BarrierResourceUsage{BindFlag::ShaderResource, ShaderStage::Compute});
					queryPool.SetTimeStampQuery(*Metal::DeviceContext::Get(*threadContext));
				}

				// "downsample" step

				{
					for (unsigned c=0; c<iterationCount; ++c) {
						// have to freshly clear atomicCounterBufferView for every call fast mip chain invocation
						vkCmdFillBuffer(
							metalContext.GetActiveCommandList().GetUnderlying().get(),
							checked_cast<Metal::Resource*>(atomicCounterBufferView->GetResource().get())->GetBuffer(), 
							0, VK_WHOLE_SIZE, 0);

						const auto threadGroupX = (mipChainTopWidth+63)>>6, threadGroupY = (mipChainTopHeight+63)>>6;
						struct FastMipChain_ControlUniforms {
							Float2 _reciprocalInputDims;
							unsigned _dummy[2];
							uint32_t _threadGroupCount;
							unsigned _dummy2;
							uint32_t _mipCount;
							unsigned _dummy3;
						} controlUniforms {
							Float2 { 1.f/float(mipChainTopWidth), 1.f/float(mipChainTopHeight) },
							{0,0},
							threadGroupX * threadGroupY,
							0,
							brightPassMipCountCount - 1,
							0
						};
						encoder.PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, MakeOpaqueIteratorRange(controlUniforms));
						encoder.Dispatch(
							*fastMipChain._pipeline,
							threadGroupX, threadGroupY, 1);
					}
					queryPool.SetTimeStampQuery(*Metal::DeviceContext::Get(*threadContext));
				}

				// end, report results

				encoder = {};
				queryPool.EndFrame(*Metal::DeviceContext::Get(*threadContext), queryPoolFrameId);
				threadContext->CommitCommands(CommitCommandsFlags::WaitForCompletion);
				auto queryResults = StallAndGetFrameResults(*Metal::DeviceContext::Get(*threadContext), queryPool, queryPoolFrameId);
				auto elapsed0 = *(queryResults._resultsStart+1) - *queryResults._resultsStart;
				auto elapsed1 = *(queryResults._resultsStart+2) - *(queryResults._resultsStart+1);
				std::cout << "BrightPassFilter: " << elapsed0 / float(queryResults._frequency) * 1000.0f / float(iterationCount) << "ms" << std::endl;
				std::cout << "DownsampleStep: " << elapsed1 / float(queryResults._frequency) * 1000.0f / float(iterationCount) << "ms" << std::endl;
			}
		}

		testHelper->EndFrameCapture();

		::Assets::MainFileSystem::GetMountingTree()->Unmount(mnt);
	}
}
