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
#include "../../../RenderCore/Techniques/CompiledLayoutPool.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/QueryPool.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
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
			Texture2D InputTexture : register(t6, space0);
			
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

	static void PumpBufferUploads(LightingEngineTestApparatus& testApparatus)
	{
		auto& immContext= *testApparatus._metalTestHelper->_device->GetImmediateContext();
		testApparatus._bufferUploads->Update(immContext);
		Threading::Sleep(16);
		testApparatus._bufferUploads->Update(immContext);
	}

	static void WriteDownsampleInput(
		LightingEngineTestApparatus& testApparatus, 
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::Techniques::RenderPassInstance& rpi,
		ToolsRig::IDrawablesWriter& drawableWriter)
	{
		using namespace RenderCore;
		auto sequencerConfig = testApparatus._pipelineAcceleratorPool->CreateSequencerConfig(
			"WriteDownsampleInput",
			testApparatus._sharedDelegates->_deferredIllumDelegate,
			{}, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());

		if (1) {
			Techniques::DrawablesPacket pkt;
			drawableWriter.WriteDrawables(pkt);
			auto prepare = Techniques::PrepareResources(*testApparatus._pipelineAcceleratorPool, *sequencerConfig, pkt);
			if (prepare) {
				prepare->StallWhilePending();
				REQUIRE(prepare->GetAssetState() == ::Assets::AssetState::Ready);
			}
			testApparatus._pipelineAcceleratorPool->RebuildAllOutOfDatePipelines();
			::Assets::Services::GetAssetSets().OnFrameBarrier();
			Techniques::Draw(
				parsingContext,
				*testApparatus._pipelineAcceleratorPool,
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
				testApparatus._pipelinePool,
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
		usi.BindResourceView(0, Hash64("InputTexture"));
		usi.BindSampler(0, Hash64("DynamicSampler"));
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
			testApparatus._pipelinePool,
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
		usi.BindResourceView(0, Hash64("InputTexture"));
		usi.BindResourceView(1, Hash64("OutputTexture"));
		usi.BindSampler(0, Hash64("DynamicSampler"));
		UniformsStream us;
		IResourceView* srvs[] = { &inputSRV, &outputUAV };
		us._resourceViews = MakeIteratorRange(srvs);
		ISampler* samplers[] = { commonResourceBox->_unnormalizedBilinearClampSampler.get() };
		us._samplers = MakeIteratorRange(samplers);

		auto pipelineLayouts = ::Assets::ActualizeAssetPtr<Techniques::CompiledPipelineLayoutAsset>(
			testApparatus._metalTestHelper->_device,
			"ut-data/minimal_compute.pipeline:ComputeMain");
		
		auto op = Techniques::CreateComputeOperator(
			testApparatus._pipelinePool,
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

	TEST_CASE( "LightingEngine-DownsamplePerformance", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto mnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));

		UInt2 workingRes { 2560, 1440 };
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::ShaderResource, 0, GPUAccess::Write,
			TextureDesc::Plain2D(workingRes[0], workingRes[1], Format::R8G8B8A8_UNORM /*Format::R11G11B10_FLOAT*/ /*Format::R32G32B32A32_FLOAT*/ ),
			"temporary-out");

		RenderCore::Techniques::CameraDesc camera;
		camera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{-1.f, 0.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, 1.0f}), Float3{10.0f, 0.f, 0.0f});
		camera._projection = Techniques::CameraDesc::Projection::Orthogonal;
		camera._nearClip = 0.0f;
		camera._farClip = 100.f;
		const auto aspectRatio = workingRes[0] / (float)workingRes[1]; 
		camera._left = -2.f * aspectRatio;
		camera._top = 2.f;
		camera._right = 2.f * aspectRatio;
		camera._bottom = -2.f;
		auto parsingContext = InitializeParsingContext(*testApparatus._techniqueContext, targetDesc, camera, *threadContext);
		parsingContext.GetFragmentStitchingContext()._workingProps._outputWidth = workingRes[0];
		parsingContext.GetFragmentStitchingContext()._workingProps._outputHeight = workingRes[1];

		testHelper->BeginFrameCapture();

		const auto downsampledResult = Hash64("Downsampled");
		auto drawableWriter = ToolsRig::CreateShapeStackDrawableWriter(*testHelper->_device, *testApparatus._pipelineAcceleratorPool);
		auto commonResourceBox = std::make_shared<Techniques::CommonResourceBox>(*testHelper->_device);

		{
			Metal::TimeStampQueryPool queryPool(Metal::GetObjectFactory());
			auto queryPoolFrameId = queryPool.BeginFrame(*Metal::DeviceContext::Get(*threadContext));
			const unsigned iterationCount = 512;

			std::shared_ptr<IResourceView> downsampleSrcSRV = nullptr;
			{
				Techniques::FrameBufferDescFragment fragDesc;
			
				// write-input-texture
				// output 0: some arbitrary pixels for downsampling (Format::R11G11B10_FLOAT precision)
				SubpassDesc writeInputTexture;
				writeInputTexture.SetName("write-input-texture");
				auto preDownsampleAttachment = fragDesc.DefineAttachment(
					Techniques::AttachmentSemantics::ColorLDR,
					{Format::Unknown, 0, LoadStore::Clear, LoadStore::Retain, 0, BindFlag::ShaderResource});
				writeInputTexture.AppendOutput(preDownsampleAttachment);
				fragDesc.AddSubpass(std::move(writeInputTexture));

				Techniques::RenderPassInstance rpi { parsingContext, fragDesc };
				WriteDownsampleInput(testApparatus, parsingContext, rpi, *drawableWriter);
				downsampleSrcSRV = rpi.GetOutputAttachmentSRV(0, {});
			}

			std::shared_ptr<IResource> downsampledResource = nullptr;
			if (1) {
				// downsample
				// input 0: attachment to downsample
				// output 0: downsampled result
				parsingContext.GetFragmentStitchingContext().DefineAttachment(
					Techniques::PreregisteredAttachment {
						downsampledResult,
						CreateDesc(
							BindFlag::RenderTarget, 0, 0, 
							TextureDesc::Plain2D(workingRes[0]/3, workingRes[1]/3, Format::R8_UNORM),
							"downsampled-attachment"),
						Techniques::PreregisteredAttachment::State::Uninitialized
					});
				Techniques::FrameBufferDescFragment fragDesc;
				SubpassDesc downsampleStep;
				downsampleStep.SetName("downsample");
				downsampleStep.AppendOutput(fragDesc.DefineAttachment(downsampledResult, {Format::R8_UNORM, 0, LoadStore::DontCare, LoadStore::Retain}));
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
					BindFlag::UnorderedAccess, 0, GPUAccess::Write,
					TextureDesc::Plain2D(workingRes[0] / 4, workingRes[1] / 4, Format::R8_UNORM),
					"downsampled");
				downsampledResource = testApparatus._metalTestHelper->_device->CreateResource(downsampledDesc);
				Metal::CompleteInitialization(*Metal::DeviceContext::Get(*threadContext), { downsampledResource.get() });
				auto downsampleDstUAV = downsampledResource->CreateTextureView(BindFlag::UnorderedAccess);

				queryPool.SetTimeStampQuery(*Metal::DeviceContext::Get(*threadContext));
				for (unsigned c=0; c<iterationCount; ++c)
					ComputeShaderBasedDownsample(testApparatus, parsingContext, *downsampleDstUAV, *downsampleSrcSRV, commonResourceBox);
				queryPool.SetTimeStampQuery(*Metal::DeviceContext::Get(*threadContext));
			}

			queryPool.EndFrame(*Metal::DeviceContext::Get(*threadContext), queryPoolFrameId);
			threadContext->CommitCommands();
			for (;;) {
				auto queryResults = queryPool.GetFrameResults(*Metal::DeviceContext::Get(*threadContext), queryPoolFrameId);
				if (queryResults._resultsReady) {
					REQUIRE(queryResults._resultsEnd != queryResults._resultsStart);
					REQUIRE(queryResults._frequency != 0);
					auto elapsed = *(queryResults._resultsStart+1) - *queryResults._resultsStart;
					Log(Warning) << "Pixel shader based downsample: " << elapsed / float(queryResults._frequency) * 1000.0f / float(iterationCount) << "ms" << std::endl;
					break;
				}
			}

			// SaveImage(*threadContext, *downsampleSrcSRV->GetResource(), "downsampled-src");
			SaveImage(*threadContext, *downsampledResource, "downsampled");
		}

		testHelper->EndFrameCapture();

		::Assets::MainFileSystem::GetMountingTree()->Unmount(mnt);
	}
}
