// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FakeModelCompiler.h"
#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../../../RenderCore/Assets/MaterialCompiler.h"
#include "../../../RenderCore/Assets/MaterialScaffold.h"
#include "../../../RenderCore/Assets/ModelScaffold.h"
#include "../../../RenderCore/Assets/RawMaterial.h"
#include "../../../RenderCore/Assets/MaterialMachine.h"
#include "../../../Assets/IntermediatesStore.h"
#include "../../../Assets/IntermediateCompilers.h"
#include "../../../Assets/IArtifact.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../Assets/AssetTraits.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/CompileAndAsyncManager.h"
#include "../../../Assets/Assets.h"
#include "../../../Math/Vector.h"
#include "../../../Math/MathSerialization.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "../../../ConsoleRig/GlobalServices.h"
#include "thousandeyes/futures/DefaultExecutor.h"
#include "thousandeyes/futures/Executor.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <filesystem>


using namespace Catch::literals;
namespace UnitTests
{
	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair(
			"test.material",
			::Assets::AsBlob(R"--(
				*=~
					Uniforms=~
						OnEverything=75
				Material0=~
					Inherit=~; ./base.material:BaseSetting
					Selectors=~
						MAT_DOUBLE_SIDED_LIGHTING=1u
					Uniforms=~
						MaterialDiffuse={0.1f, 0.1f, 0.1f}c
					States=~
						DoubleSided=1u
					Patches=~
						PerPixel=~
							some.pixel.hlsl::PerPixelCustomLighting
						DescriptorSet=some.pipeline
			)--")),
		std::make_pair(
			"base.material",
			::Assets::AsBlob(R"--(
				BaseSetting=~
					Uniforms=~
						SharedConstant={1.0f, 1.0f, 1.0f}c
			)--"))
	};

	TEST_CASE( "RenderCoreCompilation-Materials", "[rendercore_assets]" )
	{
		UnitTest_SetWorkingDirectory();
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto mnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));
		// auto assetServices = ConsoleRig::MakeAttachablePtr<::Assets::Services>(0);

		auto tempDirPath = std::filesystem::temp_directory_path() / "xle-unit-tests";
		std::filesystem::remove_all(tempDirPath);	// ensure we're starting from an empty temporary directory
		std::filesystem::create_directories(tempDirPath);

		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();

		auto matRegistration = RenderCore::Assets::RegisterMaterialCompiler(compilers);
		auto modelRegistration = UnitTests::RegisterFakeModelCompiler(compilers);

		SECTION("Compile material scaffold")
		{
			auto targetCode = RenderCore::Assets::MaterialScaffold::CompileProcessType;
			auto marker = compilers.Prepare(
				targetCode, 
				::Assets::InitializerPack { "ut-data/test.material", "fake-model" });
			REQUIRE(marker != nullptr);
			REQUIRE(marker->GetExistingAsset(targetCode) == nullptr);

			auto compile = marker->InvokeCompile();
			REQUIRE(compile != nullptr);

			compile->StallWhilePending();
			REQUIRE(compile->GetAssetState() == ::Assets::AssetState::Ready);

			auto newScaffold = ::Assets::AutoConstructAsset<std::shared_ptr<RenderCore::Assets::MaterialScaffold>>(
				*compile->GetArtifactCollection(targetCode));
			auto material0 = newScaffold->GetMaterialMachine(Hash64("Material0"));
			REQUIRE(!material0.empty());

			bool foundSelectors = false, foundConstants = false, foundPatches = false;
			for (auto cmd:material0) {
				switch (cmd.Cmd()) {
				case (uint32_t)RenderCore::Assets::MaterialCommand::AttachSelectors:
					REQUIRE(!foundSelectors);
					foundSelectors = true;
					{
						auto& selectors = cmd.As<ParameterBox>();
						REQUIRE(selectors.GetParameter<unsigned>("MAT_DOUBLE_SIDED_LIGHTING").value() == 1);
					}
					break;
				case (uint32_t)RenderCore::Assets::MaterialCommand::AttachConstants:
					REQUIRE(!foundConstants);
					foundConstants = true;
					{
						auto& constants = cmd.As<ParameterBox>();
						REQUIRE(Equivalent(constants.GetParameter<Float3>("Emissive").value(), Float3{0.5f, 0.5f, 0.5f}, 1e-3f));
						REQUIRE(Equivalent(constants.GetParameter<Float3>("MaterialDiffuse").value(), Float3{0.1f, 0.1f, 0.1f}, 1e-3f));
						REQUIRE(Equivalent(constants.GetParameter<Float3>("SharedConstant").value(), Float3{1.0f, 1.0f, 1.0f}, 1e-3f));
						REQUIRE(constants.GetParameter<float>("Brightness") == 50_a);
						REQUIRE(constants.GetParameter<float>("OnEverything") == 75_a);
					}
					break;
				case (uint32_t)RenderCore::Assets::MaterialCommand::AttachPatchCollectionId:
					REQUIRE(!foundPatches);
					foundPatches = true;
					{
						auto guid = cmd.As<uint64_t>();
						auto patchCollection = newScaffold->GetShaderPatchCollection(guid);
						REQUIRE(patchCollection);
						REQUIRE(patchCollection->GetDescriptorSetFileName().AsString() == "some.pipeline");
						REQUIRE(patchCollection->GetPatches().size() == 1);
						REQUIRE(patchCollection->GetPatches()[0].first == "PerPixel");
					}
					break;
				default:
					break;
				}
			}
			REQUIRE(foundSelectors);
			REQUIRE(foundConstants);
			REQUIRE(foundPatches);

			// material1 actually comes from the fake-model-compiler, which has some default materials
			auto material1 = newScaffold->GetMaterialMachine(Hash64("Material1"));
			REQUIRE(!material1.empty());
			foundConstants = false;
			for (auto cmd:material1) {
				switch (cmd.Cmd()) {
				case (uint32_t)RenderCore::Assets::MaterialCommand::AttachConstants:
					REQUIRE(!foundConstants);
					foundConstants = true;
					{
						auto& constants = cmd.As<ParameterBox>();
						REQUIRE(Equivalent(constants.GetParameter<Float3>("Emissive").value(), Float3{2.5f, 0.25f, 0.15f}, 1e-3f));
						REQUIRE(constants.GetParameter<float>("Brightness") == 33_a);
						REQUIRE(constants.GetParameter<float>("OnEverything") == 75_a);
					}
					break;
				}
			}
			REQUIRE(foundConstants);

			auto dehashedName0 = newScaffold->DehashMaterialName(Hash64("Material0")).AsString();
			auto dehashedName1 = newScaffold->DehashMaterialName(Hash64("Material1")).AsString();
			REQUIRE(dehashedName0 == "fake-model:Material0;ut-data/test.material:*;ut-data/test.material:Material0");
			REQUIRE(dehashedName1 == "fake-model:Material1;ut-data/test.material:*;ut-data/test.material:Material1");
		}

		::Assets::MainFileSystem::GetMountingTree()->Unmount(mnt);
	}

	TEST_CASE( "RenderCoreCompilation-Models", "[rendercore_assets]" )
	{
		UnitTest_SetWorkingDirectory();
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		// auto assetServices = ConsoleRig::MakeAttachablePtr<::Assets::Services>(0);

		auto tempDirPath = std::filesystem::temp_directory_path() / "xle-unit-tests";
		std::filesystem::remove_all(tempDirPath);	// ensure we're starting from an empty temporary directory
		std::filesystem::create_directories(tempDirPath);

		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();

		auto modelRegistration = UnitTests::RegisterFakeModelCompiler(compilers);

		SECTION("ModelScaffold compilation")
		{
			auto targetCode = RenderCore::Assets::ModelScaffold::CompileProcessType;
			auto marker = compilers.Prepare(targetCode, ::Assets::InitializerPack { "fake-model" });
			REQUIRE(marker != nullptr);
			REQUIRE(marker->GetExistingAsset(targetCode) == nullptr);

			auto compile = marker->InvokeCompile();
			REQUIRE(compile != nullptr);

			compile->StallWhilePending();
			auto collection = compile->GetArtifactCollection(targetCode);
			INFO(::Assets::AsString(::Assets::GetErrorMessage(*collection)));		// exception here is normal -- it's expected when there is no output log
			REQUIRE(compile->GetAssetState() == ::Assets::AssetState::Ready);

			SECTION("Load into scaffold")
			{
				auto newScaffold = ::Assets::AutoConstructAsset<std::shared_ptr<RenderCore::Assets::ModelScaffold>>(
					*compile->GetArtifactCollection(targetCode));

				auto geoMachine = newScaffold->GetGeoMachine(0);
				REQUIRE(!geoMachine.empty());

				auto boundingBox = newScaffold->GetStaticBoundingBox();
				auto volume = (boundingBox.second[0]-boundingBox.first[0])*(boundingBox.second[1]-boundingBox.first[1])*(boundingBox.second[2]-boundingBox.first[2]);
				REQUIRE(volume > 0);
			}
		}

		SECTION("Get material settings from a model file")
		{
			auto& cfgs = ::Assets::ActualizeAsset<RenderCore::Assets::RawMatConfigurations>("fake-model");
			REQUIRE(cfgs._configurations.size() == 2);
			REQUIRE(cfgs._configurations[0] == "Material0");
			REQUIRE(cfgs._configurations[1] == "Material1");

			auto material0 = ::Assets::ActualizeAssetPtr<RenderCore::Assets::RawMaterial>("fake-model:Material0");
			REQUIRE(material0->_uniforms.GetParameter<float>("Brightness") == 50_a);
			REQUIRE(Equivalent(material0->_uniforms.GetParameter<Float3>("Emissive").value(), Float3{0.5f, 0.5f, 0.5f}, 1e-3f));

			auto material1 = ::Assets::ActualizeAssetPtr<RenderCore::Assets::RawMaterial>("fake-model:Material1");
			REQUIRE(material1->_uniforms.GetParameter<float>("Brightness") == 33_a);
			REQUIRE(Equivalent(material1->_uniforms.GetParameter<Float3>("Emissive").value(), Float3{2.5f, 0.25f, 0.15f}, 1e-3f));
		}
	}


	TEST_CASE( "RenderCoreCompilation-CompileFromDLL", "[rendercore_assets]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		auto matRegistration = RenderCore::Assets::RegisterMaterialCompiler(compilers);

		{
			auto discoveredCompilations = ::Assets::DiscoverCompileOperations(compilers, "ColladaConversion.dll");
			REQUIRE(!discoveredCompilations.empty());

			const char* testModelFile = "xleres/DefaultResources/materialsphere.dae";
			auto scaffoldFuture = ::Assets::MakeAssetPtr<RenderCore::Assets::ModelScaffold>(testModelFile);
			scaffoldFuture->StallWhilePending();
			INFO(::Assets::AsString(scaffoldFuture->GetActualizationLog()));
			REQUIRE(scaffoldFuture->GetAssetState() == ::Assets::AssetState::Ready);

			auto scaffold = scaffoldFuture->Actualize();
			REQUIRE(scaffold->GetGeoCount() != 0);
			REQUIRE(!scaffold->CommandStream().empty());

			auto matScaffoldFuture = ::Assets::MakeAssetPtr<RenderCore::Assets::MaterialScaffold>(testModelFile, testModelFile);
			matScaffoldFuture->StallWhilePending();
			INFO(::Assets::AsString(matScaffoldFuture->GetActualizationLog()));
			REQUIRE(matScaffoldFuture->GetAssetState() == ::Assets::AssetState::Ready);
		}

		::Assets::MainFileSystem::GetMountingTree()->Unmount(xlresmnt);
	}

}
