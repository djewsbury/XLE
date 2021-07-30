// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../ReusableDataFiles.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../../RenderCore/Techniques/Drawables.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/Services.h"
#include "../../../RenderCore/Techniques/CompiledShaderPatchCollection.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/DeferredShaderResource.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Techniques/SystemUniformsDelegate.h"
#include "../../../RenderCore/Techniques/PipelineOperators.h"
#include "../../../RenderCore/Assets/PredefinedDescriptorSetLayout.h"
#include "../../../BufferUploads/IBufferUploads.h"
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
#include "../../../Assets/CompileAndAsyncManager.h"
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
#include <regex>
#include <chrono>

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	static const char* s_sequencerDescSetLayout = R"(
		ConstantBuffer GlobalTransform;
		ConstantBuffer cb0;
		ConstantBuffer cb1;
		ConstantBuffer cb2;
		ConstantBuffer cb3;
		ConstantBuffer cb4;

		SampledTexture tex0;
		SampledTexture tex1;
		SampledTexture tex2;
		SampledTexture tex3;
		SampledTexture tex4;
		SampledTexture tex5;
		SampledTexture tex6;

		Sampler DefaultSampler;
		Sampler ClampingSampler;
		Sampler AnisotropicSampler;
		Sampler PointClampSampler;
	)";

	TEST_CASE( "ImmediateDrawablesTests", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto testHelper = MakeTestHelper();

		Verbose.SetConfiguration(OSServices::MessageTargetConfiguration{});

		auto techniqueServices = ConsoleRig::MakeAttachablePtr<Techniques::Services>(testHelper->_device);
		std::shared_ptr<BufferUploads::IManager> bufferUploads = BufferUploads::CreateManager(*testHelper->_device);
		techniqueServices->SetBufferUploads(bufferUploads);
		techniqueServices->RegisterTextureLoader(std::regex(R"(.*\.[dD][dD][sS])"), RenderCore::Assets::CreateDDSTextureLoader());
		techniqueServices->RegisterTextureLoader(std::regex(R"(.*)"), RenderCore::Assets::CreateWICTextureLoader());

		auto executor = std::make_shared<thousandeyes::futures::DefaultExecutor>(std::chrono::milliseconds(2));
		thousandeyes::futures::Default<thousandeyes::futures::Executor>::Setter execSetter(executor);

		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		auto filteringRegistration = ShaderSourceParser::RegisterShaderSelectorFilteringCompiler(compilers);
		auto shaderCompilerRegistration = RenderCore::RegisterShaderCompiler(testHelper->_shaderSource, compilers);
		auto shaderCompiler2Registration = RenderCore::Techniques::RegisterInstantiateShaderGraphCompiler(testHelper->_shaderSource, compilers);

		auto sequencerDescriptorSetLayout = std::make_shared<RenderCore::Assets::PredefinedDescriptorSetLayout>(
			s_sequencerDescSetLayout, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});

		auto immediateDrawables = RenderCore::Techniques::CreateImmediateDrawables(testHelper->_device, {});

		auto techniqueContext = std::make_shared<RenderCore::Techniques::TechniqueContext>();
		techniqueContext->_commonResources = std::make_shared<RenderCore::Techniques::CommonResourceBox>(*testHelper->_device);
		Techniques::CommonResourceBox commonResources { *testHelper->_device };
		techniqueContext->_systemUniformsDelegate = std::make_shared<RenderCore::Techniques::SystemUniformsDelegate>(*testHelper->_device);

		auto threadContext = testHelper->_device->GetImmediateContext();
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(256, 256, Format::R8G8B8A8_UNORM),
			"temporary-out");
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);

		auto sphereGeo = ToolsRig::BuildGeodesicSphere();

		// Try drawing just a basic sphere with no material assigments
		{
			// Use remove the "TEXCOORD" input attribute from the IA (otherwise the system assume there's a texture to read)
			auto vertexLayout = ToolsRig::Vertex3D_MiniInputLayout;
			for (auto& attribute:vertexLayout)
				if (attribute._semanticHash == RenderCore::Techniques::CommonSemantics::TEXCOORD)
					attribute._semanticHash = 0;
			auto data = immediateDrawables->QueueDraw(
				sphereGeo.size(),
				vertexLayout);
			REQUIRE(data.size() == (sphereGeo.size() * sizeof(decltype(sphereGeo)::value_type)));
			std::memcpy(data.data(), sphereGeo.data(), data.size());
			
			auto asyncMarker = immediateDrawables->PrepareResources(fbHelper.GetDesc(), 0);
			if (asyncMarker) {
				auto finalState = asyncMarker->StallWhilePending();
				REQUIRE(finalState.has_value());
				REQUIRE(finalState.value() == ::Assets::AssetState::Ready);
				REQUIRE(asyncMarker->GetAssetState() == ::Assets::AssetState::Ready);
			}

			{
				auto rpi = fbHelper.BeginRenderPass(*threadContext);
				RenderCore::Techniques::ParsingContext parsingContext { *techniqueContext };
				parsingContext.GetViewport() = fbHelper.GetDefaultViewport();
				Techniques::CameraDesc camera {};
				SetTranslation(camera._cameraToWorld, ExtractForward_Cam(camera._cameraToWorld) * -5.0f);
				parsingContext.GetProjectionDesc() = Techniques::BuildProjectionDesc(camera, UInt2(parsingContext.GetViewport()._width, parsingContext.GetViewport()._height));
				immediateDrawables->ExecuteDraws(*threadContext, parsingContext, fbHelper.GetDesc(), 0);
			}

			auto breakdown = fbHelper.GetFullColorBreakdown(*threadContext);
			REQUIRE(breakdown.size() == 2);
			REQUIRE(breakdown.find(0xff000000) != breakdown.end());
			REQUIRE(breakdown.find(0xffffffff) != breakdown.end());
		}

		// Try drawing with a texture and a little bit of material information
		{
			auto tex = ::Assets::MakeAsset<Techniques::DeferredShaderResource>("xleres/DefaultResources/waternoise.png");
			tex->StallWhilePending();
			bufferUploads->StallUntilCompletion(*threadContext, tex->Actualize()->GetCompletionCommandList());

			Techniques::ImmediateDrawableMaterial material;
			material._uniformStreamInterface = std::make_shared<UniformsStreamInterface>();
			material._uniformStreamInterface->BindResourceView(0, Hash64("InputTexture"));
			material._uniforms._resourceViews.push_back(tex->Actualize()->GetShaderResource());
			auto data = immediateDrawables->QueueDraw(
				sphereGeo.size(),
				ToolsRig::Vertex3D_MiniInputLayout,
				material);
			REQUIRE(data.size() == (sphereGeo.size() * sizeof(decltype(sphereGeo)::value_type)));
			std::memcpy(data.data(), sphereGeo.data(), data.size());
			
			auto asyncMarker = immediateDrawables->PrepareResources(fbHelper.GetDesc(), 0);
			if (asyncMarker) {
				auto finalState = asyncMarker->StallWhilePending();
				REQUIRE(finalState.has_value());
				REQUIRE(finalState.value() == ::Assets::AssetState::Ready);
				REQUIRE(asyncMarker->GetAssetState() == ::Assets::AssetState::Ready);
			}

			{
				auto rpi = fbHelper.BeginRenderPass(*threadContext);
				RenderCore::Techniques::ParsingContext parsingContext { *techniqueContext };
				parsingContext.GetViewport() = fbHelper.GetDefaultViewport();
				Techniques::CameraDesc camera {};
				SetTranslation(camera._cameraToWorld, ExtractForward_Cam(camera._cameraToWorld) * -5.0f);
				parsingContext.GetProjectionDesc() = Techniques::BuildProjectionDesc(camera, UInt2(parsingContext.GetViewport()._width, parsingContext.GetViewport()._height));
				immediateDrawables->ExecuteDraws(*threadContext, parsingContext, fbHelper.GetDesc(), 0);
			}

			auto breakdown = fbHelper.GetFullColorBreakdown(*threadContext);
			REQUIRE(breakdown.size() > 5);
		}

		::Assets::MainFileSystem::GetMountingTree()->Unmount(xlresmnt);
	}
}

