// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../UnitTestHelper.h"
#include "../ReusableDataFiles.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/ShaderVariationSet.h"
#include "../../../RenderCore/Assets/ModelCompilationConfiguration.h"
#include "../../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../../ShaderParser/ShaderAnalysis.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../Assets/DepVal.h"
#include "../../../Assets/AssetTraits.h"
#include "../../../Assets/AssetSetManager.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/AssetMixins.h"
#include "../../../Assets/ConfigFileContainer.h"
#include "../../../ConsoleRig/Console.h"
#include "../../../OSServices/Log.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace Utility::Literals;

namespace UnitTests
{
	static std::string Filter(
		const RenderCore::Techniques::TechniqueEntry& entry,
		std::initializer_list<std::pair<const utf8*, const char*>> parameters)
	{
		ParameterBox preFiltered(parameters);
		return BuildFlatStringTable(ShaderSourceParser::FilterSelectors(
			preFiltered,
			entry._selectorFiltering, ShaderSourceParser::SelectorFilteringRules{}));
	}

	TEST_CASE( "TechniqueFileTests-TechniqueSelectorFiltering", "[rendercore_techniques]" )
	{
		SECTION("file1")
		{
			const char* techniqueFile = R"--(
				Shared=~
					Selectors=~
						CLASSIFY_NORMAL_MAP
						SKIP_MATERIAL_DIFFUSE=~; relevance=<:(value!=0):>
						SELECTOR_0=~; relevance=1

				Config=~
					Inherit=~; Shared
					Selectors=~
						SELECTOR_0=1
			)--";

			Formatters::TextInputFormatter<utf8> formattr { MakeStringSectionNullTerm(techniqueFile) };
			RenderCore::Techniques::TechniqueSetFile techniqueSetFile(formattr, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});
			(void)techniqueSetFile;

			auto* entry = techniqueSetFile.FindEntry("Config"_h);
			REQUIRE(entry != nullptr);

			// The value given to SELECTOR_0 should overide the default set value in the technique
			// SKIP_MATERIAL_DIFFUSE is filtered out by the relevance check
			auto test0 = Filter(
				*entry,
				{
					std::make_pair("SELECTOR_0", "2"),
					std::make_pair("SKIP_MATERIAL_DIFFUSE", "0")
				});
			REQUIRE(std::string{"SELECTOR_0=2"} == test0);

			// SELECTOR_0 gets it's default value from the technique file,
			// and SKIP_MATERIAL_DIFFUSE is filtered in this time
			// CLASSIFY_NORMAL_MAP this time is overridden, and filtered in
			auto test1 = Filter(
				*entry,
				{
					std::make_pair("SKIP_MATERIAL_DIFFUSE", "3"),
					std::make_pair("CLASSIFY_NORMAL_MAP", "5")
				});
			REQUIRE(std::string{"SELECTOR_0=1;CLASSIFY_NORMAL_MAP=5;SKIP_MATERIAL_DIFFUSE=3"} == test1);
		}

		SECTION("file2")
		{
			const char techniqueFile[] = R"--(
				Shared=~
					Selectors=~
						SELECTOR_0=~; relevance=<:(value!=0):>
						SELECTOR_1=~; set=2; relevance=1
						SELECTOR_2=~; relevance=<:(value!=5):>

				Config=~
					Inherit=~; Shared
					Selectors=~
						SELECTOR_0=~; relevance=<:(value!=1):>
						SELECTOR_1=3
						SELECTOR_2=4
			)--";

			Formatters::TextInputFormatter<utf8> formattr { MakeStringSectionLiteral(techniqueFile) };
			RenderCore::Techniques::TechniqueSetFile techniqueSetFile(formattr, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});
			(void)techniqueSetFile;

			auto* entry = techniqueSetFile.FindEntry("Config"_h);
			REQUIRE(entry != nullptr);

			// The settings in the "Config" group should override what we inherited from the 
			// basic configuration "Shared"
			auto test0 = Filter(
				*entry,
				{
					std::make_pair("SELECTOR_0", "0"),
					std::make_pair("UNKNOWN_SELECTOR", "6")
				});
			REQUIRE(std::string{"SELECTOR_2=4;SELECTOR_0=0;SELECTOR_1=3"} == test0);

			// If we set SELECTOR_2 to make it different from it's default set value, but the
			// new value is now not considered relevant, than we should remove it completely
			auto test1 = Filter(
				*entry,
				{
					std::make_pair("SELECTOR_2", "5")
				});
			REQUIRE(std::string{"SELECTOR_1=3"} == test1);
		}
	}

	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair(
			"one.model",
			::Assets::AsBlob(R"--(
				RawGeoRules=~
					*=~
						16Bit=true
						ExcludeAttributes=~
							COLOR

				CommandStreams=~
					0

				SkeletonRules=~
					*=~
						PreserveParameters=~
							Bone0; Bone1; Bone2
						PreserveOutputs=~
							Bone3; Bone4; Bone5

				Material=~
					AutoProcessTextures=true

				Inherit=~
					two.model:Default
			)--")),

		std::make_pair(
			"two.model",
			::Assets::AsBlob(R"--(
				Default=~
					CommandStreams=~
						adjacency

					SkeletonRules=~
						*=~
							PreserveParameters=~
								ABone0; ABone1; ABone2
			)--")),
	};

	TEST_CASE( "ModelScaffoldConfigurationFiles", "[rendercore_assets]" )
	{
		auto globalServices = ConsoleRig::MakeGlobalServices(GetStartupConfig());
		auto mnt0 = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::EnableChangeMonitoring));

		using namespace RenderCore::Assets;

		auto marker = ::Assets::ActualizeAssetFn<::Assets::ResolveAssetToPromise<ModelCompilationConfiguration>>("ut-data/one.model");
		(void)marker;
		
		::Assets::MainFileSystem::GetMountingTree()->Unmount(mnt0);
	}

}
