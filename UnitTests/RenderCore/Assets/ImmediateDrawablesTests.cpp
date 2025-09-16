// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../ReusableDataFiles.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderOverlays/ShapesRendering.h"
#include "../../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../../RenderCore/Techniques/Drawables.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/Services.h"
#include "../../../RenderCore/Techniques/ShaderPatchInstantiationUtil.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/DeferredShaderResource.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Techniques/SystemUniformsDelegate.h"
#include "../../../RenderCore/Techniques/PipelineOperators.h"
#include "../../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../../RenderCore/Assets/PredefinedDescriptorSetLayout.h"
#include "../../../RenderCore/BufferUploads/IBufferUploads.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../RenderCore/Format.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../Assets/AssetSetManager.h"
#include "../../../Assets/Assets.h"
#include "../../../Tools/ToolsRig/VisualisationGeo.h"
#include "../../../Math/Transformations.h"
#include "../../../ConsoleRig/Console.h"
#include "../../../OSServices/Log.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "../../../Utility/StringFormat.h"
#include "../../../xleres/FileList.h"
#include "thousandeyes/futures/then.h"
#include "thousandeyes/futures/DefaultExecutor.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <chrono>

using namespace Catch::literals;
using namespace std::chrono_literals;
using namespace Utility::Literals;

namespace UnitTests
{
	static const char* s_sequencerDescSetLayout = R"(
		ConstantBuffer GlobalTransform;
		ConstantBuffer b1;
		ConstantBuffer b2;
		ConstantBuffer b3;
		ConstantBuffer b4;
		ConstantBuffer b5;

		SampledTexture t6;
		SampledTexture t7;
		SampledTexture t8;
		SampledTexture t9;
		SampledTexture t10;

		Sampler DefaultSampler;
		Sampler ClampingSampler;
		Sampler AnisotropicSampler;
		Sampler PointClampSampler;
	)";

	static void StallForResources(
		RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> techniqueDelegate,
		const RenderCore::FrameBufferDesc& fbDesc, unsigned subpassIndex)
	{
		using namespace RenderCore;
		std::promise<Techniques::PreparedResourcesVisibility> preparePromise;
		auto prepareFuture = preparePromise.get_future();
		immediateDrawables.PrepareResources(std::move(preparePromise), techniqueDelegate, fbDesc, subpassIndex);
		prepareFuture.get();		// stall
		immediateDrawables.OnFrameBarrier();		// annoyingly we have to call this to flip the pipelines into visibility
	}

	TEST_CASE( "ImmediateDrawablesTests", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeGlobalServices(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto testHelper = MakeTestHelper();

		Verbose.SetConfiguration(OSServices::MessageTargetConfiguration{});

		auto techniqueServices = ConsoleRig::MakeAttachablePtr<Techniques::Services>(testHelper->_device);
		std::shared_ptr<BufferUploads::IManager> bufferUploads = BufferUploads::CreateManager({}, *testHelper->_device);
		techniqueServices->SetBufferUploads(bufferUploads);
		techniqueServices->SetCommonResources(std::make_shared<Techniques::CommonResourceBox>(*testHelper->_device));
		techniqueServices->RegisterTextureLoader("*.[dD][dD][sS]", RenderCore::Assets::CreateDDSTextureLoader());
		techniqueServices->RegisterTextureLoader("*", RenderCore::Assets::CreateWICTextureLoader());

		auto& compilers = ::Assets::Services::GetIntermediateCompilers();
		auto filteringRegistration = ShaderSourceParser::RegisterShaderSelectorFilteringCompiler(compilers);
		auto shaderCompilerRegistration = RegisterShaderCompiler(testHelper->_shaderSource, compilers, GetDefaultShaderCompilationFlags(*testHelper->_device));
		auto shaderCompiler2Registration = Techniques::RegisterInstantiateShaderGraphCompiler(testHelper->_shaderSource, compilers);

		auto sequencerDescriptorSetLayout = std::make_shared<RenderCore::Assets::PredefinedDescriptorSetLayout>(
			s_sequencerDescSetLayout, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});

		auto shapeRenderingDelegates = std::make_shared<RenderOverlays::ShapesRenderingDelegate>();
		auto pipelineCollection = std::make_shared<RenderCore::Techniques::PipelineCollection>(testHelper->_device);
		auto overlayPipelineAccelerators = RenderCore::Techniques::CreatePipelineAcceleratorPool(testHelper->_device, nullptr, pipelineCollection, shapeRenderingDelegates->GetPipelineLayoutDelegate(), 0);
		auto immediateDrawables = Techniques::CreateImmediateDrawables(overlayPipelineAccelerators);

		auto techniqueContext = std::make_shared<Techniques::TechniqueContext>();
		techniqueContext->_commonResources = techniqueServices->GetCommonResources();

		techniqueContext->_graphicsSequencerDS = Techniques::CreateSemiConstantDescriptorSet(*sequencerDescriptorSetLayout, "unittest", PipelineType::Graphics, *testHelper->_device);
		techniqueContext->_systemUniformsDelegate = std::make_shared<Techniques::SystemUniformsDelegate>(*testHelper->_device);

		auto threadContext = testHelper->_device->GetImmediateContext();
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc,
			TextureDesc::Plain2D(256, 256, Format::R8G8B8A8_UNORM_SRGB));
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);

		auto sphereGeo = ToolsRig::BuildGeodesicSphere();

		// Try drawing just a basic sphere with no material assignments
		{
			// Use remove the "TEXCOORD" input attribute from the IA (otherwise the system assume there's a texture to read)
			auto vertexLayout = ToolsRig::Vertex3D_MiniInputLayout;
			for (auto& attribute:vertexLayout)
				if (attribute._semanticHash == Techniques::CommonSemantics::TEXCOORD)
					attribute._semanticHash = 0;
			auto data = immediateDrawables->QueueDraw(
				sphereGeo.size(),
				vertexLayout);
			REQUIRE(data.size() == (sphereGeo.size() * sizeof(decltype(sphereGeo)::value_type)));
			std::memcpy(data.data(), sphereGeo.data(), data.size());
			
			StallForResources(*immediateDrawables, shapeRenderingDelegates->GetTechniqueDelegate(), fbHelper.GetDesc(), 0);

			{
				auto rpi = fbHelper.BeginRenderPass(*threadContext);
				auto parsingContext = Techniques::ParsingContext { *techniqueContext, *threadContext };
				parsingContext.GetViewport() = fbHelper.GetDefaultViewport();
				Techniques::CameraDesc camera {};
				SetTranslation(camera._cameraToWorld, ExtractForward_Cam(camera._cameraToWorld) * -5.0f);
				parsingContext.GetProjectionDesc() = Techniques::BuildProjectionDesc(camera, parsingContext.GetViewport()._width / float(parsingContext.GetViewport()._height));
				immediateDrawables->ExecuteDraws(parsingContext, shapeRenderingDelegates->GetTechniqueDelegate(), fbHelper.GetDesc(), 0);
			}

			auto breakdown = fbHelper.GetFullColorBreakdown(*threadContext);
			REQUIRE(breakdown.size() == 2);
			REQUIRE(breakdown.find(0xff000000) != breakdown.end());
			REQUIRE(breakdown.find(0xffffffff) != breakdown.end());
		}

		// Try drawing with a texture and a little bit of material information
		{
			auto tex = ::Assets::GetAssetFuturePtr<Techniques::DeferredShaderResource>("xleres/DefaultResources/waternoise.png");
			bufferUploads->StallAndMarkCommandListDependency(*threadContext, tex.get()->GetCompletionCommandList());

			Techniques::ImmediateDrawableMaterial material;
			UniformsStreamInterface inputTextureUSI;
			inputTextureUSI.BindResourceView(0, "InputTexture"_h);
			material._uniformStreamInterface = &inputTextureUSI;
			Techniques::RetainedUniformsStream uniforms;
			uniforms._resourceViews.push_back(tex.get()->GetShaderResource());
			auto data = immediateDrawables->QueueDraw(
				sphereGeo.size(),
				ToolsRig::Vertex3D_MiniInputLayout,
				material, std::move(uniforms));
			REQUIRE(data.size() == (sphereGeo.size() * sizeof(decltype(sphereGeo)::value_type)));
			std::memcpy(data.data(), sphereGeo.data(), data.size());
			
			StallForResources(*immediateDrawables, shapeRenderingDelegates->GetTechniqueDelegate(), fbHelper.GetDesc(), 0);

			{
				auto rpi = fbHelper.BeginRenderPass(*threadContext);
				Techniques::ParsingContext parsingContext { *techniqueContext, *threadContext };
				parsingContext.GetViewport() = fbHelper.GetDefaultViewport();
				Techniques::CameraDesc camera {};
				SetTranslation(camera._cameraToWorld, ExtractForward_Cam(camera._cameraToWorld) * -5.0f);
				parsingContext.GetProjectionDesc() = Techniques::BuildProjectionDesc(camera, parsingContext.GetViewport()._width / float(parsingContext.GetViewport()._height));
				immediateDrawables->ExecuteDraws(parsingContext, shapeRenderingDelegates->GetTechniqueDelegate(), fbHelper.GetDesc(), 0);
			}

			auto breakdown = fbHelper.GetFullColorBreakdown(*threadContext);
			REQUIRE(breakdown.size() > 5);
		}

		::Assets::MainFileSystem::GetMountingTree()->Unmount(xlresmnt);
	}
}

