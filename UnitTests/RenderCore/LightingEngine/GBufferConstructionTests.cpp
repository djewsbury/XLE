// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineTestHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../../RenderCore/LightingEngine/ForwardLightingDelegate.h"
#include "../../../RenderCore/LightingEngine/DeferredLightingDelegate.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../../RenderCore/Techniques/CommonBindings.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../RenderCore/Techniques/PipelineCollection.h"
#include "../../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Techniques/DeferredShaderResource.h"
#include "../../../RenderCore/Techniques/PipelineOperators.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../Math/Transformations.h"
#include "../../../Math/ProjectionMath.h"
#include "../../../Math/Geometry.h"
#include "../../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../../Assets/IAsyncMarker.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../xleres/FileList.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Vulkan/Metal/IncludeVulkan.h"

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair("reconstruct_from_gbuffer.pixel.hlsl", ::Assets::AsBlob(R"--(
			#define GBUFFER_SHADER_RESOURCE 1
			#include "xleres/TechniqueLibrary/Framework/gbuffer.hlsl"
			#include "xleres/TechniqueLibrary/Utility/LoadGBuffer.hlsl"
			#include "xleres/Deferred/operator-util.hlsl"

			void main(	float4 position : SV_Position,
						float2 texCoord : TEXCOORD0,
						float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
						SystemInputs sys,
						out float4 out_position : SV_Target0,
						out float4 out_normal : SV_Target1)
			{
				LightOperatorInputs resolvePixel = LightOperatorInputs_Create(position, viewFrustumVector, sys);
				if (resolvePixel.ndcDepth == 0.0f) discard;

				out_position = float4(resolvePixel.worldPosition, 1);
				GBufferValues sample = LoadGBuffer(position, sys);
				out_normal = float4(sample.worldSpaceNormal, 1);
			}
		)--")),

		std::make_pair("write_world_coords.pixel.hlsl", ::Assets::AsBlob(R"--(
			#include "xleres/TechniqueLibrary/Framework/VSOUT.hlsl"

			void frameworkEntry(
				VSOUT geo,
				out float4 out_position : SV_Target0,
				out float4 out_normal : SV_Target1)
			{
				out_position = float4(VSOUT_GetWorldPosition(geo), 1);
				out_normal = float4(VSOUT_GetWorldVertexNormal(geo), 1);
			}
		)--"))
	};

	class WriteWorldCoordsDelegate : public RenderCore::Techniques::ITechniqueDelegate
	{
	public:
		virtual ::Assets::PtrToMarkerPtr<GraphicsPipelineDesc> GetPipelineDesc(
			const RenderCore::Techniques::CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& renderStates) override
		{
			using namespace RenderCore;
			auto pipelineDesc = std::make_shared<GraphicsPipelineDesc>();
			pipelineDesc->_shaders[(unsigned)ShaderStage::Vertex] = NO_PATCHES_VERTEX_HLSL ":main";
			pipelineDesc->_shaders[(unsigned)ShaderStage::Pixel] = "ut-data/write_world_coords.pixel.hlsl:frameworkEntry";
			pipelineDesc->_manualSelectorFiltering._setValues.SetParameter("VSOUT_HAS_WORLD_POSITION", 1);
			pipelineDesc->_manualSelectorFiltering._setValues.SetParameter("VSOUT_HAS_NORMAL", 1);
			pipelineDesc->_blend.push_back(Techniques::CommonResourceBox::s_abOpaque);
			pipelineDesc->_blend.push_back(Techniques::CommonResourceBox::s_abOpaque);
			pipelineDesc->_rasterization = Techniques::CommonResourceBox::s_rsDefault;
			pipelineDesc->_depthStencil = Techniques::CommonResourceBox::s_dsDisable;

			auto result = std::make_shared<::Assets::MarkerPtr<GraphicsPipelineDesc>>();
			result->SetAsset(std::move(pipelineDesc));
			return result;
		}

		virtual std::string GetPipelineLayout() override { return MAIN_PIPELINE ":GraphicsMain"; }
	};

	static UInt2 s_testResolution { 2048, 2048 };

	static RenderCore::Techniques::ParsingContext InitializeParsingContext(
		LightingEngineTestApparatus& testApparatus,
		const RenderCore::Techniques::CameraDesc& camera,
		RenderCore::IThreadContext& threadContext)
	{
		using namespace RenderCore;

		Techniques::PreregisteredAttachment preregisteredAttachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferDiffuse,
				CreateDesc(
					BindFlag::TransferSrc | BindFlag::RenderTarget | BindFlag::ShaderResource, 0, 0, 
					TextureDesc::Plain2D(s_testResolution[0], s_testResolution[1], Format::B8G8R8A8_UNORM_SRGB),
					"gbuffer-diffuse"
				),
				Techniques::PreregisteredAttachment::State::Uninitialized
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferNormal,
				CreateDesc(
					BindFlag::TransferSrc | BindFlag::RenderTarget | BindFlag::ShaderResource, 0, 0, 
					TextureDesc::Plain2D(s_testResolution[0], s_testResolution[1], Format::R8G8B8A8_SNORM),
					"gbuffer-normals"
				),
				Techniques::PreregisteredAttachment::State::Uninitialized
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferParameter,
				CreateDesc(
					BindFlag::TransferSrc | BindFlag::RenderTarget | BindFlag::ShaderResource, 0, 0, 
					TextureDesc::Plain2D(s_testResolution[0], s_testResolution[1], Format::R8G8B8A8_UNORM),
					"gbuffer-parameters"
				),
				Techniques::PreregisteredAttachment::State::Uninitialized
			},
			Techniques::PreregisteredAttachment {
				Hash64("ReconstructedWorldPosition"),
				CreateDesc(
					BindFlag::TransferSrc | BindFlag::RenderTarget, 0, 0, 
					TextureDesc::Plain2D(s_testResolution[0], s_testResolution[1], Format::R32G32B32A32_FLOAT),
					"reconstructed-world-position"
				),
				Techniques::PreregisteredAttachment::State::Uninitialized
			},
			Techniques::PreregisteredAttachment {
				Hash64("ReconstructedWorldNormal"),
				CreateDesc(
					BindFlag::TransferSrc | BindFlag::RenderTarget, 0, 0, 
					TextureDesc::Plain2D(s_testResolution[0], s_testResolution[1], Format::R32G32B32A32_FLOAT),
					"reconstructed-world-normal"
				),
				Techniques::PreregisteredAttachment::State::Uninitialized
			},
			Techniques::PreregisteredAttachment {
				Hash64("DirectWorldPosition"),
				CreateDesc(
					BindFlag::TransferSrc | BindFlag::RenderTarget, 0, 0, 
					TextureDesc::Plain2D(s_testResolution[0], s_testResolution[1], Format::R32G32B32A32_FLOAT),
					"direct-world-position"
				),
				Techniques::PreregisteredAttachment::State::Uninitialized
			},
			Techniques::PreregisteredAttachment {
				Hash64("DirectWorldNormal"),
				CreateDesc(
					BindFlag::TransferSrc | BindFlag::RenderTarget, 0, 0, 
					TextureDesc::Plain2D(s_testResolution[0], s_testResolution[1], Format::R32G32B32A32_FLOAT),
					"direct-world-normal"
				),
				Techniques::PreregisteredAttachment::State::Uninitialized
			}
		};
		FrameBufferProperties fbProps { s_testResolution[0], s_testResolution[1] };

		auto parsingContext = BeginParsingContext(testApparatus, threadContext);
		parsingContext.GetProjectionDesc() = BuildProjectionDesc(camera, s_testResolution);

		auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
		stitchingContext._workingProps = fbProps;
		for (const auto&a:preregisteredAttachments)
			stitchingContext.DefineAttachment(a._semantic, a._desc, a._state, a._layoutFlags);
		return parsingContext;
	}

	template<typename Type>
		static std::shared_ptr<Type> StallAndRequireReady(::Assets::MarkerPtr<Type>& future)
	{
		future.StallWhilePending();
		INFO(::Assets::AsString(future.GetActualizationLog()));
		REQUIRE(future.GetAssetState() == ::Assets::AssetState::Ready);
		return future.Actualize();
	}

	class GBufferConstructionUnitTestGlobalUniforms : public RenderCore::Techniques::IShaderResourceDelegate
	{
	public:
		void WriteImmediateData(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
		{
			switch (idx) {
			case 0:
				*(RenderCore::Techniques::GlobalTransformConstants*)dst.begin() = BuildGlobalTransformConstants(context.GetProjectionDesc());
				break;
			case 1:
				*(RenderCore::Techniques::LocalTransformConstants*)dst.begin() = RenderCore::Techniques::MakeLocalTransform(Identity<Float4x4>(), Zero<Float3>());
				break;
			}
		}

		size_t GetImmediateDataSize(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override
		{
			switch (idx) {
			case 0:
				return sizeof(RenderCore::Techniques::GlobalTransformConstants);
			case 1:
				return sizeof(RenderCore::Techniques::LocalTransformConstants);
			default:
				return 0;
			}
		}

		void WriteResourceViews(RenderCore::Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<RenderCore::IResourceView**> dst) override
		{
			assert(dst.size() == 1);
			dst[0] = _normalsFittingSRV.get();
		}

		GBufferConstructionUnitTestGlobalUniforms()
		{
			BindImmediateData(0, Hash64("GlobalTransform"));
			BindImmediateData(1, Hash64("LocalTransform"));
			BindResourceView(0, Hash64("NormalsFittingTexture"));

			auto normalsFittingTexture = ::Assets::ActualizeAssetPtr<RenderCore::Techniques::DeferredShaderResource>(NORMALS_FITTING_TEXTURE);
			_normalsFittingSRV = normalsFittingTexture->GetShaderResource();
		}

		RenderCore::UniformsStreamInterface _interface;
		std::shared_ptr<RenderCore::IResourceView> _normalsFittingSRV;
	};

	static void RunSimpleFullscreen(
		RenderCore::Techniques::ParsingContext& parsingContext,
		const std::shared_ptr<RenderCore::Techniques::PipelineCollection>& pipelinePool,
		const RenderCore::Techniques::RenderPassInstance& rpi,
		StringSection<> pixelShader,
		StringSection<> pipelayoutLayoutAsset,
		RenderCore::UniformsStreamInterface& usi,
		RenderCore::UniformsStream& us)
	{
		RenderCore::Techniques::PixelOutputStates outputStates;
		outputStates.Bind(rpi);
		outputStates.Bind(RenderCore::Techniques::CommonResourceBox::s_dsDisable);
		RenderCore::AttachmentBlendDesc blendStates[] { RenderCore::Techniques::CommonResourceBox::s_abOpaque };
		outputStates.Bind(MakeIteratorRange(blendStates));
		ParameterBox selectors;
		auto op = RenderCore::Techniques::CreateFullViewportOperator(
			pipelinePool,
			RenderCore::Techniques::FullViewportOperatorSubType::DisableDepth,
			pixelShader, selectors, pipelayoutLayoutAsset,
			outputStates, usi);
		op->StallWhilePending();
		op->Actualize()->Draw(parsingContext, us);
	}

	static void CalculateSimularity(IteratorRange<const Float4*> A, IteratorRange<const Float4*> B)
	{
		// note -- this takes an extraordinarily large amount of time because it's a just a very naive implementation
		REQUIRE(A.size() == B.size());
		REQUIRE(A.size() > 0);
		std::vector<double> differences;
		differences.reserve(A.size()*3);
		double totalDiff = 0;
		differences.reserve(A.size() * 3);
		for (auto a=A.begin(), b=B.begin(); a<A.end(); ++a, ++b) {
			auto diff = Double3{double((*a)[0]) - double((*b)[0]), double((*a)[1]) - double((*b)[1]), double((*a)[2]) - double((*b)[2])};
			differences.push_back(diff[0]);
			differences.push_back(diff[1]);
			differences.push_back(diff[2]);
			totalDiff += diff[0] + diff[1] + diff[2];
		}

		auto meanDiff = totalDiff / (double)differences.size();
		std::sort(differences.begin(), differences.end());
		auto medianDiff = differences[differences.size()/2];

		double varianceAccumulator = 0;
		for (auto d:differences)
			varianceAccumulator += (d-meanDiff) * (d-meanDiff);
		auto standardDev = std::sqrt(varianceAccumulator / (double)differences.size());

		Log(Warning) << "Position comparison: " << std::endl;
		Log(Warning) << "Mean: " << meanDiff << ", Median: " << medianDiff << ", StandardDev: " << standardDev << std::endl;
		Log(Warning) << "Smallest difference: " << differences[0] << ", largest differences: " << differences[differences.size()-1] << std::endl;

		REQUIRE(standardDev < 1e-3f);
	}

	static void CalculateDirectionalSimularity(IteratorRange<const Float4*> A, IteratorRange<const Float4*> B)
	{
		REQUIRE(A.size() == B.size());
		REQUIRE(A.size() > 0);
		std::vector<double> differences;
		double totalDiff = 0;
		differences.reserve(A.size() * 3);
		for (auto a=A.begin(), b=B.begin(); a<A.end(); ++a, ++b) {
			if (!MagnitudeSquared(Truncate(*a))) {
				assert(!MagnitudeSquared(Truncate(*b)));
				continue;
			}
			auto sphericalA = CartesianToSpherical(Truncate(*a));
			auto sphericalB = CartesianToSpherical(Truncate(*b));
			auto diff = Double2{double(sphericalA[0] - sphericalB[0]), double(sphericalA[1] - sphericalB[1])};
			if (diff[0] >= gPI) diff[0] -= 2.f * gPI;
			if (diff[1] >= gPI) diff[1] -= 2.f * gPI;
			if (diff[0] <= -gPI) diff[0] += 2.f * gPI;
			if (diff[1] <= -gPI) diff[1] += 2.f * gPI;
			differences.push_back(diff[0]);
			differences.push_back(diff[1]);
			totalDiff += diff[0] + diff[1];
		}

		auto meanDiff = totalDiff / (double)differences.size();
		std::sort(differences.begin(), differences.end());
		auto medianDiff = differences[differences.size()/2];

		double varianceAccumulator = 0;
		for (auto d:differences)
			varianceAccumulator += (d-meanDiff) * (d-meanDiff);
		auto standardDev = std::sqrt(varianceAccumulator / (double)differences.size());

		Log(Warning) << "Directional comparison (in radians): " << std::endl;
		Log(Warning) << "Mean: " << meanDiff << ", Median: " << medianDiff << ", StandardDev: " << standardDev << std::endl;
		Log(Warning) << "Smallest difference: " << differences[0] << ", largest differences: " << differences[differences.size()-1] << std::endl;

		REQUIRE(standardDev < 5e-2f);
	}

	static void ReadyForTransfer(RenderCore::IThreadContext& threadContext, RenderCore::IResource& resource)
	{
		// todo -- final image layout solution
		using namespace RenderCore;
		Metal::Internal::SetImageLayout(
			*Metal::DeviceContext::Get(threadContext),
			*dynamic_cast<Metal::Resource*>(&resource),
			Metal::Internal::ImageLayout::ColorAttachmentOptimal, 0, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
			Metal::Internal::ImageLayout::General, 0, VK_PIPELINE_STAGE_HOST_BIT);
	}

	TEST_CASE( "LightingEngine-GBufferAccuracy", "[rendercore_lighting_engine]" )
	{
		using namespace RenderCore;
		LightingEngineTestApparatus testApparatus;
		auto testHelper = testApparatus._metalTestHelper.get();
		auto utdatamnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));
		
		auto threadContext = testHelper->_device->GetImmediateContext();

		auto drawableWriter = ToolsRig::DrawablesWriterHelper(*testHelper->_device, *testApparatus._drawablesPool, *testApparatus._pipelineAccelerators).CreateSphereDrawablesWriter();

		RenderCore::Techniques::CameraDesc cameras[3];
		cameras[0]._cameraToWorld = MakeCameraToWorld(Float3{1.0f, 0.0f, 0.0f}, Float3{0.0f, 1.0f, 0.0f}, Float3{-3.33f, 0.f, 0.f});
		cameras[0]._nearClip = 0.1f;
		cameras[0]._farClip = 100.f;

		cameras[1]._cameraToWorld = MakeCameraToWorld(-Normalize(Float3{-3.0f, 1.5f, 0.f}), Float3{0.0f, 1.0f, 0.0f}, Float3{-3.0f, 1.5f, 0.f});
		cameras[1]._nearClip = 0.1f;
		cameras[1]._farClip = 100.f;

		cameras[2]._cameraToWorld = MakeCameraToWorld(-Normalize(Float3{-3.0f, 1.5f, 0.f}), Float3{0.0f, 1.0f, 0.0f}, Float3{-3.0f, 1.5f, 0.f});		
		cameras[2]._projection = Techniques::CameraDesc::Projection::Orthogonal;
		cameras[2]._left = -1.0f; cameras[0]._top = 1.0f;
		cameras[2]._right = 1.0f; cameras[0]._bottom = -1.0f;
		cameras[2]._nearClip = 0.f;
		cameras[2]._farClip = 100.f;

		///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		SECTION("write gbuffer")
		{
			auto techniqueSetFile = ::Assets::MakeAssetPtr<RenderCore::Techniques::TechniqueSetFile>(ILLUM_TECH);
			auto deferredIllumDelegate = RenderCore::Techniques::CreateTechniqueDelegate_Deferred(techniqueSetFile);

			auto pipelinePool = std::make_shared<Techniques::PipelineCollection>(testHelper->_device);

			for (unsigned c=0; c<dimof(cameras); ++c) {
				INFO("Camera: " + std::to_string(c));
				testHelper->BeginFrameCapture();
				const auto& camera = cameras[c];
				auto parsingContext = InitializeParsingContext(testApparatus, camera, *threadContext);

				auto globalDelegate = std::make_shared<GBufferConstructionUnitTestGlobalUniforms>();
				parsingContext.GetUniformDelegateManager()->AddShaderResourceDelegate(globalDelegate);

				std::shared_ptr<IResource> diffuseResource, normalResource, parameterResource, depthResource;
				std::shared_ptr<IResource> reconstructedWorldPosition, reconstructedWorldNormal;
				std::shared_ptr<IResource> directWorldPosition, directWorldNormal;
				RenderCore::Techniques::AttachmentPool::Reservation attachmentReservation;

				testApparatus._bufferUploads->Update(*threadContext);
				Threading::Sleep(16);
				testApparatus._bufferUploads->Update(*threadContext);

				// Prepare gbuffer using standard technique delegate
				{
					RenderCore::Techniques::FrameBufferDescFragment fbFrag;
					SubpassDesc subpass;
					subpass.AppendOutput(fbFrag.DefineAttachment(Techniques::AttachmentSemantics::GBufferDiffuse).Clear().FinalState(BindFlag::ShaderResource));
					subpass.AppendOutput(fbFrag.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal).Clear().FinalState(BindFlag::ShaderResource));
					subpass.AppendOutput(fbFrag.DefineAttachment(Techniques::AttachmentSemantics::GBufferParameter).Clear().FinalState(BindFlag::ShaderResource));
					subpass.SetDepthStencil(fbFrag.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).SystemAttachmentFormat(Techniques::SystemAttachmentFormat::MainDepthStencil).Clear().FinalState(BindFlag::ShaderResource));
					fbFrag.AddSubpass(std::move(subpass));

					RenderCore::Techniques::RenderPassInstance rpi(parsingContext, fbFrag);
					diffuseResource = rpi.GetOutputAttachmentResource(0);
					normalResource = rpi.GetOutputAttachmentResource(1);
					parameterResource = rpi.GetOutputAttachmentResource(2);
					depthResource = rpi.GetDepthStencilAttachmentResource();

					auto gbufferWriteCfg = testApparatus._pipelineAccelerators->CreateSequencerConfig(
						"gbufferWriteCfg",
						Techniques::CreateTechniqueDelegate_Deferred(techniqueSetFile),
						ParameterBox {},
						rpi.GetFrameBufferDesc());

					{
						Techniques::DrawablesPacket pkt;
						drawableWriter->WriteDrawables(pkt);
						auto newVisibility = PrepareAndStall(testApparatus, *gbufferWriteCfg.get(), pkt);
						parsingContext.SetPipelineAcceleratorsVisibility(newVisibility._pipelineAcceleratorsVisibility);
						parsingContext.RequireCommandList(newVisibility._bufferUploadsVisibility);
						Techniques::Draw(
							parsingContext, 
							*testApparatus._pipelineAccelerators,
							*gbufferWriteCfg,
							pkt);
					}
				}

				// Run per pixel pass to convert gbuffer textures -> world position & normal textures
				{
					RenderCore::Techniques::FrameBufferDescFragment frag;
					RenderCore::Techniques::FrameBufferDescFragment::SubpassDesc subpass;
					subpass.AppendOutput(frag.DefineAttachment(Hash64("ReconstructedWorldPosition")).Clear().FinalState(BindFlag::TransferSrc));
					subpass.AppendOutput(frag.DefineAttachment(Hash64("ReconstructedWorldNormal")).Clear().FinalState(BindFlag::TransferSrc));
					subpass.AppendNonFrameBufferAttachmentView(frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferDiffuse));
					subpass.AppendNonFrameBufferAttachmentView(frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal));
					subpass.AppendNonFrameBufferAttachmentView(frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferParameter));
					subpass.AppendNonFrameBufferAttachmentView(frag.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth), BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::Aspect::Depth});
					frag.AddSubpass(std::move(subpass));
					RenderCore::Techniques::RenderPassInstance rpi(parsingContext, frag);
					reconstructedWorldPosition = rpi.GetOutputAttachmentResource(0);
					reconstructedWorldNormal = rpi.GetOutputAttachmentResource(1);

					UniformsStreamInterface usi;
					usi.BindResourceView(0, Utility::Hash64("GBuffer_Diffuse"));
					usi.BindResourceView(1, Utility::Hash64("GBuffer_Normals"));
					usi.BindResourceView(2, Utility::Hash64("GBuffer_Parameters"));
					usi.BindResourceView(3, Utility::Hash64("DepthTexture"));
					UniformsStream us;
					IResourceView* srvs[] = { 
						rpi.GetNonFrameBufferAttachmentView(0).get(),
						rpi.GetNonFrameBufferAttachmentView(1).get(),
						rpi.GetNonFrameBufferAttachmentView(2).get(),
						rpi.GetNonFrameBufferAttachmentView(3).get()
					};
					us._resourceViews = MakeIteratorRange(srvs);
					RunSimpleFullscreen(parsingContext, pipelinePool, rpi, "ut-data/reconstruct_from_gbuffer.pixel.hlsl:main", GENERAL_OPERATOR_PIPELINE ":GraphicsMain", usi, us);

					attachmentReservation = rpi.GetAttachmentReservation();
				}

				// Redraw from original geo, but this time render world position and normal directly
				{
					RenderCore::Techniques::FrameBufferDescFragment frag;
					SubpassDesc subpass;
					subpass.AppendOutput(frag.DefineAttachment(Hash64("DirectWorldPosition")).Clear().FinalState(BindFlag::TransferSrc));
					subpass.AppendOutput(frag.DefineAttachment(Hash64("DirectWorldNormal")).Clear().FinalState(BindFlag::TransferSrc));
					subpass.SetDepthStencil(frag.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).SystemAttachmentFormat(Techniques::SystemAttachmentFormat::MainDepthStencil).Clear());
					frag.AddSubpass(std::move(subpass));
					RenderCore::Techniques::RenderPassInstance rpi(parsingContext, frag);

					auto writeDirectCfg = testApparatus._pipelineAccelerators->CreateSequencerConfig(
						"writeDirectCfg",
						std::make_shared<WriteWorldCoordsDelegate>(),
						ParameterBox {},
						rpi.GetFrameBufferDesc());

					directWorldPosition = rpi.GetOutputAttachmentResource(0);
					directWorldNormal = rpi.GetOutputAttachmentResource(1);

					{
						Techniques::DrawablesPacket pkt;
						drawableWriter->WriteDrawables(pkt);
						auto newVisibility = PrepareAndStall(testApparatus, *writeDirectCfg.get(), pkt);
						parsingContext.SetPipelineAcceleratorsVisibility(newVisibility._pipelineAcceleratorsVisibility);
						parsingContext.RequireCommandList(newVisibility._bufferUploadsVisibility);
						Techniques::Draw(
							parsingContext, 
							*testApparatus._pipelineAccelerators,
							*writeDirectCfg,
							pkt);
					}
				}
				testHelper->EndFrameCapture();

				ReadyForTransfer(*threadContext, *reconstructedWorldPosition);
				ReadyForTransfer(*threadContext, *reconstructedWorldNormal);
				ReadyForTransfer(*threadContext, *directWorldPosition);
				ReadyForTransfer(*threadContext, *directWorldNormal);

				SaveImage(*threadContext, *diffuseResource, "gbuffer-diffuse");
				SaveImage(*threadContext, *normalResource, "gbuffer-normals");
				SaveImage(*threadContext, *parameterResource, "gbuffer-parameters");
				// SaveImage(*threadContext, *reconstructedWorldPosition, "reconstructed-world-position");
				// SaveImage(*threadContext, *directWorldPosition, "direct-world-position");
				auto reconstructedPositionData = reconstructedWorldPosition->ReadBackSynchronized(*threadContext);
				auto reconstructedNormalData = reconstructedWorldNormal->ReadBackSynchronized(*threadContext);

				// By comparing the reconstructed vs direct rendering outputs, we can see how much precision
				// is lost via the gbuffer. So, for example, we may loose some precision related to the direction of
				// the normal
				if (1) {
					auto directData = directWorldPosition->ReadBackSynchronized(*threadContext);
					CalculateSimularity(
						MakeIteratorRange((const Float4*)AsPointer(reconstructedPositionData.begin()), (const Float4*)AsPointer(reconstructedPositionData.end())), 
						MakeIteratorRange((const Float4*)AsPointer(directData.begin()), (const Float4*)AsPointer(directData.end())));
				}

				if (1) {
					auto directData = directWorldNormal->ReadBackSynchronized(*threadContext);
					CalculateDirectionalSimularity(
						MakeIteratorRange((const Float4*)AsPointer(reconstructedNormalData.begin()), (const Float4*)AsPointer(reconstructedNormalData.end())), 
						MakeIteratorRange((const Float4*)AsPointer(directData.begin()), (const Float4*)AsPointer(directData.end())));
				}
			}
		}
	}
}
