// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueTestsHelper.h"
#include "../../UnitTestHelper.h"
#include "FakeModelCompiler.h"
#include "../../../RenderCore/Assets/ModelScaffold.h"
#include "../../../RenderCore/Assets/MaterialScaffold.h"
#include "../../../RenderCore/Assets/ScaffoldCmdStream.h"
#include "../../../RenderCore/Assets/MaterialCompiler.h"
#include "../../../RenderCore/Techniques/DrawableConstructor.h"
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
		auto matRegistration = RenderCore::Assets::RegisterMaterialCompiler(compilers);
		auto modelRegistration = UnitTests::RegisterFakeModelCompiler(compilers);
		auto testHelper = MakeTestHelper();
		TechniqueTestApparatus testApparatus(*testHelper);

		// compile a fake scaffold using some simple input data
		std::shared_ptr<::Assets::ArtifactCollectionFuture> modelCompile;
		std::shared_ptr<::Assets::ArtifactCollectionFuture> materialCompile;
		{
			auto targetCode = RenderCore::Assets::ModelScaffold::CompileProcessType;
			auto marker = compilers.Prepare(targetCode, ::Assets::InitializerPack { "fake-model" });
			REQUIRE(marker != nullptr);

			modelCompile = marker->InvokeCompile();
			REQUIRE(modelCompile != nullptr);
		}
		{
			auto targetCode = RenderCore::Assets::MaterialScaffold::CompileProcessType;
			auto marker = compilers.Prepare(targetCode, ::Assets::InitializerPack { "fake-model", "fake-model" });
			REQUIRE(marker != nullptr);

			materialCompile = marker->InvokeCompile();
			REQUIRE(materialCompile != nullptr);
		}

		modelCompile->StallWhilePending();
		materialCompile->StallWhilePending();
		
		SECTION("Load as scaffold")
		{
			auto modelCollection = modelCompile->GetArtifactCollection(RenderCore::Assets::ModelScaffold::CompileProcessType);
			REQUIRE(modelCompile->GetAssetState() == ::Assets::AssetState::Ready);

			auto modelScaffold = ::Assets::AutoConstructAsset<std::shared_ptr<RenderCore::Assets::ModelScaffoldCmdStreamForm>>(*modelCollection);
			auto cmdStream = modelScaffold->CommandStream();
			REQUIRE(!cmdStream.empty());
			for (auto cmd:cmdStream) {
				Log(Warning) << "Cmd: " << (uint32_t)cmd.Cmd() << std::endl;
				Log(Warning) << "Data: " << cmd.BlockSize() << std::endl;
			}

			auto materialScaffold = ::Assets::AutoConstructAsset<std::shared_ptr<RenderCore::Assets::MaterialScaffold>>(
				*materialCompile->GetArtifactCollection(RenderCore::Assets::MaterialScaffold::CompileProcessType));

			SECTION("Create RendererConstruction")
			{
				auto rendererConstruction = std::make_shared<RenderCore::Assets::RendererConstruction>();
				auto& ele = rendererConstruction->AddElement().SetModelScaffold(modelScaffold).SetName("test-element");
				ele.SetMaterialScaffold(materialScaffold);
				
				std::promise<std::shared_ptr<RenderCore::Assets::RendererConstruction>> promise;
				auto future = promise.get_future();
				rendererConstruction->FulfillWhenNotPending(std::move(promise));
				future.wait();
				REQUIRE(future.get() == rendererConstruction);
				REQUIRE(rendererConstruction->GetAssetState() == ::Assets::AssetState::Ready);
			
				SECTION("Create DrawableConstructor")
				{
					auto constructor = std::make_shared<Techniques::DrawableConstructor>(testApparatus._pipelineAccelerators, *testApparatus._bufferUploads, *rendererConstruction);
					std::promise<std::shared_ptr<Techniques::DrawableConstructor>> promise;
					auto future = promise.get_future();
					constructor->FulfillWhenNotPending(std::move(promise));
					future.wait();
					auto fulfilledPromise = future.get();
					REQUIRE(fulfilledPromise == constructor);
					REQUIRE(constructor->_completionCommandList > 0);
				}
			}
		}
	}

}