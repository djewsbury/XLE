// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueTestsHelper.h"
#include "../../UnitTestHelper.h"
#include "FakeModelCompiler.h"
#include "../../../RenderCore/Assets/ModelScaffold.h"
#include "../../../RenderCore/Assets/ScaffoldCmdStream.h"
#include "../../../RenderCore/Techniques/DrawableProvider.h"
#include "../../../Assets/IntermediatesStore.h"
#include "../../../Assets/IntermediateCompilers.h"
#include "../../../Assets/CompileAndAsyncManager.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/InitializerPack.h"
#include "../../../Assets/AssetTraits.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "../../../ConsoleRig/GlobalServices.h"
#include "../../../OSServices/Log.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

namespace UnitTests
{

	TEST_CASE( "ConstructRenderer-FakeModel", "[rendercore_techniques]" )
	{
		using namespace RenderCore;

		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		auto modelRegistration = UnitTests::RegisterFakeModelCompiler(compilers);
		auto testHelper = MakeTestHelper();
		TechniqueTestApparatus testApparatus(*testHelper);

		// compile a fake scaffold using some simple input data
		auto targetCode = RenderCore::Assets::ModelScaffold::CompileProcessType;
		auto marker = compilers.Prepare(targetCode, ::Assets::InitializerPack { "fake-model" });
		REQUIRE(marker != nullptr);

		auto compile = marker->InvokeCompile();
		REQUIRE(compile != nullptr);

		compile->StallWhilePending();
		auto collection = compile->GetArtifactCollection(targetCode);
		INFO(::Assets::AsString(::Assets::GetErrorMessage(*collection)));		// exception here is normal -- it's expected when there is no output log
		REQUIRE(compile->GetAssetState() == ::Assets::AssetState::Ready);
		
		SECTION("Load as scaffold")
		{
			auto finalScaffold = ::Assets::AutoConstructAsset<std::shared_ptr<RenderCore::Assets::ScaffoldAsset>>(*collection);
			auto cmdStream = finalScaffold->GetCmdStream();
			REQUIRE(!cmdStream.empty());
			for (auto cmd:cmdStream) {
				Log(Warning) << "Cmd: " << (uint32_t)cmd.Cmd() << std::endl;
				Log(Warning) << "Data: " << cmd.BlockSize() << std::endl;
			}

			SECTION("Create RendererConstruction")
			{
				auto rendererConstruction = std::make_shared<RenderCore::Assets::RendererConstruction>();
				rendererConstruction->AddElement().SetModelScaffold(finalScaffold).SetName("test-element");
				
				auto future = rendererConstruction->ReadyFuture();
				future.wait();
				REQUIRE(future.get() == rendererConstruction);
				REQUIRE(rendererConstruction->GetAssetState() == ::Assets::AssetState::Ready);
			
				SECTION("Create DrawableProvider")
				{
					RenderCore::Techniques::DrawableProvider provider{testApparatus._pipelineAccelerators};
					provider.Add(*rendererConstruction);
				}
			}
		}
	}

}