// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../ReusableDataFiles.h"
#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../ShaderParser/ShaderSignatureParser.h"
#include "../../../ShaderParser/NodeGraphSignature.h"
#include "../../../ShaderParser/ShaderAnalysis.h"
#include "../../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../Assets/AssetTraits.h"
#include "../../../Assets/DepVal.h"
#include "../../../ConsoleRig/Console.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "../../../ConsoleRig/GlobalServices.h"
#include "../../../OSServices/RawFS.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/Streams/PathUtils.h"
#include "../../../Utility/Streams/PreprocessorInterpreter.h"
#include "../../../Utility/Conversion.h"
#include <cctype>
#include <sstream>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../ShaderParser/ShaderInstantiation.h"
#include "../../../ShaderParser/GraphSyntax.h"

using namespace Catch::literals;
namespace UnitTests
{
	// The following data is mounted as virtual files in the folder "ut-data"
	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair(
			"outershader.hlsl",
			::Assets::AsBlob(R"--(
#include "ut-data/innershader.hlsl"
static const int NonPreprocessorLine0 = 0;
#include "innershader.hlsl"					
				static const int NonPreprocessorLine1 = 0; /*
					block comment
				*/ #inClUdE "innershader.hlsl"
				#    INCLUDE "middleshader.hlsl"
	/*  */		#	INCLUDE		<middleshader.hlsl>			   
		/**/	#  include<ut-data/innershader.hlsl>			   random trailing stuff
			)--")),

		std::make_pair(
			"innershader.hlsl",
			::Assets::AsBlob(R"--(static const int ThisIsFromTheInnerShader = 0;)--")),

		std::make_pair(
			"middleshader.hlsl",
			::Assets::AsBlob(R"--(
				static const int ThisIsFromTheMiddleShader0 = 0;
				#include "innershader.hlsl"
				static const int ThisIsFromTheMiddleShader1 = 0;
			)--")),


		std::make_pair(
			"outershader-noincludes.hlsl",
			::Assets::AsBlob(R"--(
#include__ "ut-data/innershader0.hlsl"
/*#include "innershader1.hlsl"*/					
				/*
					block comment
				#inClUdE "innershader2.hlsl" */ 
				// #    INCLUDE "innershader3.hlsl"
				// extended line comment \
	/*  */		#	INCLUDE		<innershader4.hlsl>			   
		// /**/	#  include<ut-data/innershader5.hlsl>			   random trailing stuff
			)--")),

		std::make_pair(
			"example.tech",
			::Assets::AsBlob(R"--(
				~NoPatches
					~Inherit; xleres/Techniques/Illum.tech:Deferred

				~PerPixel
					~Inherit; xleres/Techniques/Illum.tech:Deferred
					PixelShader=xleres/TechniqueLibrary/Standard/deferred.pixel.hlsl:frameworkEntry
			)--")),

		std::make_pair("example-perpixel.pixel.hlsl", ::Assets::AsBlob(s_examplePerPixelShaderFile)),
		std::make_pair("example.graph", ::Assets::AsBlob(s_exampleGraphFile)),

		std::make_pair(
			"selector-preconfiguration.hlsl",
			::Assets::AsBlob(R"--(
#if GEO_HAS_TEXCOORD && GEO_HAS_NORMAL && RES_HAS_NormalsTexture
	#if !defined(VSOUT_HAS_TANGENT_FRAME)
		#define VSOUT_HAS_TANGENT_FRAME 1
	#endif
	#if !defined(VSOUT_HAS_TEXCOORD)
		#define VSOUT_HAS_TEXCOORD 1
	#endif
#elif GEO_HAS_TEXCOORD && (MAT_ALPHA_TEST || MAT_ALPHA_TEST_PREDEPTH) && RES_HAS_DiffuseTexture
	#if !defined(VSOUT_HAS_TEXCOORD)
		#define VSOUT_HAS_TEXCOORD 1
	#endif
#elif BLUE
#elif RED
#else
	#undef VSOUT_HAS_TANGENT_FRAME
	#undef VSOUT_HAS_TEXCOORD
	#undef GEO_HAS_TEXCOORD
#endif
			)--"))
	};

	static void FindShaderSources(
		std::vector<std::string>& dest,
		const ::Assets::FileSystemWalker& walker)
	{
		for (auto file = walker.begin_files(); file != walker.end_files(); ++file) {
			auto naturalName = file.Desc()._naturalName;
			auto splitter = MakeFileNameSplitter(naturalName);
			if (	XlEqStringI(splitter.Extension(), "h")
				||	XlEqStringI(splitter.Extension(), "sh")
				||	(splitter.Extension().size() == 3 && std::tolower(splitter.Extension()[1]) == 's' && std::tolower(splitter.Extension()[1]) == 'h')) {
				dest.push_back(naturalName);
			}
		}
		for (auto dir = walker.begin_directories(); dir != walker.end_directories(); ++dir) {
			if (dir.Name().empty() || dir.Name()[0] == '.') continue;
			FindShaderSources(dest, *dir);
		}
	}

	class LocalHelper
	{
	public:
		std::shared_ptr<::Assets::MountingTree> _mountingTree;
		::Assets::MountingTree::MountID _utDataMount;
		::Assets::MountingTree::MountID _xleresMount;
		::ConsoleRig::AttachablePtr<::Assets::IDependencyValidationSystem> _depValSys;

		LocalHelper()
		{
			if (!_depValSys) _depValSys = ::Assets::CreateDepValSys();
			_mountingTree = std::make_shared<::Assets::MountingTree>(s_defaultFilenameRules);
			_utDataMount = _mountingTree->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));
			_xleresMount = _mountingTree->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
			::Assets::MainFileSystem::Init(_mountingTree, nullptr);
		}

		~LocalHelper()
		{
			::Assets::MainFileSystem::Shutdown();
			_mountingTree->Unmount(_utDataMount);
			_mountingTree->Unmount(_xleresMount);
			_mountingTree.reset();
		}
	};

	TEST_CASE( "ShaderParser-ParseAllSystemShaderSources", "[shader_parser]" )
	{
		LocalHelper localHelper;

			// Search for all of the shader sources in the embedded xleres directory
		#define X(file, id) std::string { #file },
		std::vector<std::string> inputFiles = {
			#include "../../EmbeddedResFileList.h"
		};
		#undef X
		
		for (auto& i:inputFiles) {
			auto splitter = MakeFileNameSplitter(i);
			if (	!XlFindStringI(splitter.Extension(), "hlsl")
				&&  !XlEqStringI(splitter.Extension(), "sh")
				&&  !XlEqStringI(splitter.Extension(), "h"))
				continue;

			auto memBlock = ::Assets::MainFileSystem::TryLoadFileAsBlob(MakeStringSection("xleres/" + i));
			const char* flgId = "FunctionLinkingGraph";
			if (XlFindString(MakeStringSection((const char*)AsPointer(memBlock->begin()), (const char*)AsPointer(memBlock->end())), flgId))
				continue;

			try
			{
				auto signature = ShaderSourceParser::ParseHLSL(
					MakeStringSection((const char*)AsPointer(memBlock->begin()), (const char*)AsPointer(memBlock->end())));

				(void)signature;
			} catch (const std::exception& e) {
				Log(Warning) << "Got parsing exception in (" << i << ")" << std::endl << e.what() << std::endl;
			}
		}
	}

	TEST_CASE( "ShaderParser-ExpandOutIncludes", "[shader_parser]" )
	{
		LocalHelper localHelper;

		{
			auto outerShader = ::Assets::MainFileSystem::TryLoadFileAsBlob("ut-data/outershader.hlsl");
			REQUIRE(outerShader != nullptr);
			REQUIRE(outerShader->size() != (size_t)0);
			auto expanded = ShaderSourceParser::ExpandIncludes(
				StringSection<char>{(char*)AsPointer(outerShader->begin()), (char*)AsPointer(outerShader->end())},
				"ut-data/outershader.hlsl",
				::Assets::DefaultDirectorySearchRules("ut-data/outershader.hlsl"));
			REQUIRE(expanded._lineMarkers.size() == (size_t)16);
			REQUIRE(expanded._processedSourceLineCount == 21u);
		}

		{
			// In the following test, none of the #include statements should actually be followed
			// We will probably get an exception if they are (inside the files don't exist).
			// But we can also check the output
			auto outerShader = ::Assets::MainFileSystem::TryLoadFileAsBlob("ut-data/outershader-noincludes.hlsl");
			REQUIRE(outerShader != nullptr);
			REQUIRE(outerShader->size() != (size_t)0);
			auto expanded = ShaderSourceParser::ExpandIncludes(
				StringSection<char>{(char*)AsPointer(outerShader->begin()), (char*)AsPointer(outerShader->end())},
				"ut-data/outershader.hlsl",
				::Assets::DefaultDirectorySearchRules("ut-data/outershader-noincludes.hlsl"));
			REQUIRE(expanded._lineMarkers.size() == (size_t)1);	// one straight block of text, no includes are followed
		}
	}

	static std::string IsDefinedRelevance(
		const ShaderSourceParser::SelectorFilteringRules& filteringRules,
		StringSection<> name)
	{
		auto tkn = filteringRules._tokenDictionary.TryGetToken(Utility::Internal::TokenDictionary::TokenType::IsDefinedTest, name);
		if (!tkn.has_value())
			return {};
		auto i = filteringRules._relevanceTable.find(tkn.value());
		if (i == filteringRules._relevanceTable.end())
			return {};
		return filteringRules._tokenDictionary.AsString(i->second);
	}

	TEST_CASE( "ShaderParser-TestAnalyzeSelectors", "[shader_parser]" )
	{
		const char exampleShader[] = R"--(
			#if defined(SOME_SELECTOR) || defined(ANOTHER_SELECTOR)
				#if defined(THIRD_SELECTOR)
				#endif
			#endif

			#if defined(SELECTOR_0) || defined(SELECTOR_1)
				#define SECONDARY_DEFINE
			#endif

			#if defined(SECONDARY_DEFINE) && defined(DEPENDENT_SELECTOR)
			#endif

			#if defined(SELECTOR_3) && defined(SELECTOR_4)
				#define SECONDARY_DEFINE_2 1
			#endif

			#if (SECONDARY_DEFINE_2 == 1) && defined(DEPENDENT_SELECTOR_2)
			#endif

		)--";
		auto analysis = ShaderSourceParser::GenerateSelectorFilteringRules(exampleShader);
		REQUIRE(IsDefinedRelevance(analysis, "SOME_SELECTOR") == std::string{"!defined(ANOTHER_SELECTOR)"});
		REQUIRE(IsDefinedRelevance(analysis, "ANOTHER_SELECTOR") == std::string{"!defined(SOME_SELECTOR)"});
		REQUIRE(IsDefinedRelevance(analysis, "THIRD_SELECTOR") == std::string{"defined(SOME_SELECTOR) || defined(ANOTHER_SELECTOR)"});

		// Check some filtering conditions
		{
			auto filter0 = ShaderSourceParser::FilterSelectors(
				ParameterBox {
					std::make_pair("THIRD_SELECTOR", "1"),
				},
				{},
				analysis);
			REQUIRE(filter0.GetCount() == (size_t)0);
		}

		{
			auto filter1 = ShaderSourceParser::FilterSelectors(
				ParameterBox {
					std::make_pair("SOME_SELECTOR", "1"),
					std::make_pair("THIRD_SELECTOR", "1"),
				},
				{},
				analysis);
			REQUIRE(filter1.GetCount() == (size_t)2);
		}

		{
			auto filter1 = ShaderSourceParser::FilterSelectors(
				ParameterBox {
					std::make_pair("SOME_SELECTOR", "1"),
					std::make_pair("ANOTHER_SELECTOR", "1"),
				},
				{},
				analysis);
			REQUIRE(filter1.GetCount() == (size_t)1);
		}
	}

	TEST_CASE( "ShaderParser-BindShaderToTechnique", "[shader_parser]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto utDataMount = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));
		auto mnt0 = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());

		// Given some shader (either straight-up shader code, or something generated from a shader graph)
		// bind it to a technique, and produce both the final shader text and required meta-data

		/*
			todo -- this requires RenderCore::Techniques linked in. Maybe better off in a different unit test
		auto tech = ::Assets::AutoConstructAsset<RenderCore::Techniques::TechniqueSetFile>("ut-data/example.tech");
		const auto* entry = tech->FindEntry(Hash64("PerPixel"));
		REQUIRE(entry != nullptr);
		*/

		const std::string exampleGraphFN = "ut-data/example.graph";
		ShaderSourceParser::InstantiationRequest instRequests[] {
			{ exampleGraphFN }
		};
		
		using namespace ShaderSourceParser;
		ShaderSourceParser::GenerateFunctionOptions generateOptions;
		generateOptions._shaderLanguage = RenderCore::ShaderLanguage::GLSL;
		auto inst = InstantiateShader(MakeIteratorRange(instRequests), generateOptions);

		auto i = std::find_if(
			inst._entryPoints.begin(), inst._entryPoints.end(),
			[](const ShaderEntryPoint& ep) {
				return ep._name == "Bind_PerPixel" && ep._implementsName == "PerPixel";
			});
		REQUIRE(i != inst._entryPoints.end());

		// Expand shader and extract the selector relevance table information

		{
			std::stringstream str;
			for (const auto&f:inst._sourceFragments)
				str << f << std::endl;

			auto expanded = ExpandIncludes(str.str(), exampleGraphFN, ::Assets::DefaultDirectorySearchRules(exampleGraphFN));
			auto relevanceTable = ShaderSourceParser::GenerateSelectorFilteringRules(expanded._processedSource);
			(void)relevanceTable;
		}

		::Assets::MainFileSystem::GetMountingTree()->Unmount(mnt0);
		::Assets::MainFileSystem::GetMountingTree()->Unmount(utDataMount);
	}

	TEST_CASE( "ShaderParser-SelectorPreconfiguration", "[shader_parser]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto utDataMount = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));

		ShaderSourceParser::SelectorPreconfiguration preconfig{"ut-data/selector-preconfiguration.hlsl"};
		ParameterBox inputSelectors;
		inputSelectors.SetParameter("GEO_HAS_TEXCOORD", 1);
		inputSelectors.SetParameter("MAT_ALPHA_TEST", 1);
		inputSelectors.SetParameter("RES_HAS_DiffuseTexture", 1);
		auto filteredSelectors = preconfig.Preconfigure(std::move(inputSelectors));

		std::stringstream str;
		str << preconfig << std::endl;
		INFO(str.str());

		REQUIRE(filteredSelectors.HasParameter("MAT_ALPHA_TEST"));
		REQUIRE(filteredSelectors.HasParameter("RES_HAS_DiffuseTexture"));
		REQUIRE(filteredSelectors.HasParameter("GEO_HAS_TEXCOORD"));
		REQUIRE(filteredSelectors.HasParameter("VSOUT_HAS_TEXCOORD"));

		inputSelectors = {};
		inputSelectors.SetParameter("GEO_HAS_TEXCOORD", 1);
		inputSelectors.SetParameter("BLUE", 1);
		filteredSelectors = preconfig.Preconfigure(std::move(inputSelectors));
		REQUIRE(filteredSelectors.HasParameter("GEO_HAS_TEXCOORD"));
	}

}

