// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueTestsHelper.h"
#include "../ReusableDataFiles.h"
#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Assets/ShaderPatchCollection.h"
#include "../../../RenderCore/Assets/PredefinedCBLayout.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../RenderCore/Techniques/CompiledShaderPatchCollection.h"
#include "../../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../ShaderParser/ShaderInstantiation.h"
#include "../../../ShaderParser/DescriptorSetInstantiation.h"
#include "../../../ShaderParser/ShaderAnalysis.h"
#include "../../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../Assets/DepVal.h"
#include "../../../Assets/DeferredConstruction.h"
#include "../../../Assets/InitializerPack.h"
#include "../../../Assets/CompileAndAsyncManager.h"
#include "../../../Assets/AssetServices.h"
#include "../../../ConsoleRig/Console.h"
#include "../../../OSServices/Log.h"
#include "../../../OSServices/FileSystemMonitor.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "../../../Utility/Streams/StreamFormatter.h"
#include "../../../Utility/Streams/OutputStreamFormatter.h"
#include "../../../Utility/Streams/StreamTypes.h"
#include "../../../Utility/MemoryUtils.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
	static const char s_exampleTechniqueFragments[] = R"--(
		=~
			ut-data/fragment.graph::Fragment
		main=~
			ut-data/outergraph.graph::deferred_pass_main
			perPixel=~
				ut-data/perpixel.graph::Default_PerPixel
		=~
			ut-data/outergraph.graph::CoordsToColor
		)--";

	static const char s_fragmentsWithSelectors[] = R"--(
		perPixel=~
			ut-data/shader_with_selectors_adapter.graph::Default_PerPixel
		)--";

	static const char s_fragmentsWithRename[] = R"--(
		=~
			ut-data/shader_with_selectors.pixel.hlsl::PerPixelWithSelectors
			Implements=xleres/Nodes/Templates.pixel.sh::PerPixel
		)--";

	// The following data is mounted as virtual files in the folder "ut-data"
	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair(
			"fragment.graph",
			::Assets::AsBlob(R"--(
				float3 Fragment() 
				{
					return "float3(0,0,0)";
				}
			)--")),

		std::make_pair(
			"outergraph.graph",
			::Assets::AsBlob(R"--(
				import templates = "xleres/Nodes/Templates.pixel.sh"
				import texture = "xleres/Nodes/Texture.sh"
				import gbuffer = "xleres/TechniqueLibrary/Framework/gbuffer.hlsl"

				auto deferred_pass_main(
					VSOUT geo,
					graph<templates::EarlyRejectionTest> rejectionTest,
					graph<templates::PerPixel> perPixel)
				{
					/*if (rejectionTest(geo:geo).result) {
						discard;
					}*/

					node perPixelEval = perPixel(geo:geo);
					return gbuffer::Encode(values:perPixelEval.result).result;
				}

				float3 CoordsToColor(float3 coords) implements templates::CoordinatesToColor
				{
					captures MaterialUniforms = (Texture2D DiffuseTexture, Texture2D ParametersTexture, float3 MaterialSpecular, float3 MaterialDiffuse);
					captures SecondUnifomBuffer = (Texture2D AnotherTexture, float4 MoreParameters);
					return texture::Sample(inputTexture:MaterialUniforms.DiffuseTexture, texCoord:coords).result;
				}
			)--")),

		std::make_pair(
			"perpixel.graph",
			::Assets::AsBlob(R"--(
				import templates = "xleres/Nodes/Templates.pixel.sh"
				import output = "xleres/Nodes/Output.sh"
				import materialParam = "xleres/Nodes/MaterialParam.sh"

				auto Default_PerPixel(VSOUT geo) implements templates::PerPixel
				{
					return output::Output_PerPixel(
						diffuseAlbedo:"float3(1,1,1)",
						worldSpaceNormal:"float3(0,1,0)",
						material:materialParam::CommonMaterialParam_Default().result,
						blendingAlpha:"1",
						normalMapAccuracy:"1",
						cookedAmbientOcclusion:"1",
						cookedLightOcclusion:"1",
						transmission:"float3(0,0,0)").result;
				}
			)--")),

		std::make_pair(
			"shader_with_selectors.pixel.hlsl",
			::Assets::AsBlob(R"--(
				#include "xleres/TechniqueLibrary/Framework/VSOUT.hlsl"
				#include "xleres/TechniqueLibrary/Framework/CommonResources.hlsl"
				#include "xleres/TechniqueLibrary/Framework/gbuffer.hlsl"
				#include "xleres/TechniqueLibrary/Utility/Colour.hlsl"

				Texture2D       TextureDif		BIND_MAT_T3;
				Texture2D       TextureNorm		BIND_MAT_T4;

				PerPixelMaterialParam DefaultMaterialValues()
				{
					PerPixelMaterialParam result;
					result.roughness = 0.5f;
					result.specular = 0.1f;
					result.metal = 0.0f;
					return result;
				}

				GBufferValues PerPixelWithSelectors(VSOUT geo)
				{
					GBufferValues result = GBufferValues_Default();
					result.material = DefaultMaterialValues();

					float4 diffuseTextureSample = 1.0.xxxx;
					#if (VSOUT_HAS_TEXCOORD>=1) && (RES_HAS_TextureDif!=0)
						diffuseTextureSample = TextureDif.Sample(MaybeAnisotropicSampler, geo.texCoord);
						result.diffuseAlbedo = diffuseTextureSample.rgb;
						result.blendingAlpha = diffuseTextureSample.a;
					#endif

					#if (VSOUT_HAS_TEXCOORD>=1) && (RES_HAS_TextureNorm!=0)
						float3 normalMapSample = SampleNormalMap(TextureNorm, DefaultSampler, true, geo.texCoord);
						result.worldSpaceNormal = normalMapSample; // TransformTangentSpaceToWorld(normalMapSample, geo);
					#elif (VSOUT_HAS_NORMAL==1)
						result.worldSpaceNormal = normalize(geo.normal);
					#endif

					return result;
				}
			)--")),

		std::make_pair(
			"shader_with_selectors_adapter.graph",
			::Assets::AsBlob(R"--(
				import templates = "xleres/Nodes/Templates.pixel.sh"
				import output = "xleres/Nodes/Output.sh"
				import materialParam = "xleres/Nodes/MaterialParam.sh"
				import shader = "ut-data/shader_with_selectors.pixel.hlsl"

				GBufferValues Default_PerPixel(VSOUT geo) implements templates::PerPixel
				{
					return shader::PerPixelWithSelectors(geo:geo).result;
				}
			)--")),

		std::make_pair("example-perpixel.pixel.hlsl", ::Assets::AsBlob(s_examplePerPixelShaderFile)),
		std::make_pair("example.graph", ::Assets::AsBlob(s_exampleGraphFile)),
		std::make_pair("complicated.graph", ::Assets::AsBlob(s_complicatedGraphFile)),
		std::make_pair("internalShaderFile.pixel.hlsl", ::Assets::AsBlob(s_internalShaderFile)),
		std::make_pair("internalComplicatedGraph.graph", ::Assets::AsBlob(s_internalComplicatedGraph)),
		std::make_pair("frameworkEntry.pixel.hlsl", ::Assets::AsBlob(s_basicFrameworkEntryPixel)),
	};

	static void FakeChange(StringSection<> fn)
	{
		::Assets::MainFileSystem::TryFakeFileChange(fn);
	}

	class ExpandIncludesPreprocessor : public RenderCore::ISourceCodePreprocessor
	{
	public:
		virtual RenderCore::SourceCodeWithRemapping RunPreprocessor(
            StringSection<> inputSource, 
            StringSection<> definesTable,
            const ::Assets::DirectorySearchRules& searchRules) override
		{
			return ShaderSourceParser::ExpandIncludes(inputSource, "main", searchRules);
		}
	};

	TEST_CASE( "ShaderPatchCollection", "[rendercore_techniques]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto mnt0 = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto mnt1 = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::EnableChangeMonitoring));
		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		auto filteringRegistration = ShaderSourceParser::RegisterShaderSelectorFilteringCompiler(compilers);

		RenderCore::Assets::PredefinedPipelineLayoutFile pipelineLayoutFile(TechniqueTestApparatus::UnitTestPipelineLayout, {}, {});
		auto matDescSetLayout = RenderCore::Techniques::FindLayout(pipelineLayoutFile, "GraphicsMain", "Material");

		SECTION( "DeserializeShaderPatchCollection" )
		{
			// Normally a ShaderPatchCollection is deserialized from a material file
			// We'll test the serialization and deserialization code here, and ensure
			InputStreamFormatter<> formattr { MakeStringSection(s_exampleTechniqueFragments) };
			RenderCore::Assets::ShaderPatchCollection patchCollection(formattr, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});

			// Verify that a few things got deserialized correctly
			auto i = std::find_if(
				patchCollection.GetPatches().begin(),
				patchCollection.GetPatches().end(),
				[](const std::pair<std::string, ShaderSourceParser::InstantiationRequest>& r) {
					return r.first == "main";
				});
			REQUIRE(i!=patchCollection.GetPatches().end());
			REQUIRE(i->second._parameterBindings.size() == (size_t)1);
			REQUIRE(i->second._parameterBindings.begin()->first == std::string("perPixel"));
			REQUIRE(i->second._parameterBindings.begin()->second->_archiveName == std::string("ut-data/perpixel.graph::Default_PerPixel"));

			// Write out the patch collection again
			MemoryOutputStream<char> strm;
			OutputStreamFormatter outFmttr(strm);
			SerializationOperator(outFmttr, patchCollection);

			// Now let's verify that we can deserialize in what we just wrote out
			auto& serializedStream = strm.GetBuffer();
			InputStreamFormatter<utf8> formattr2 { MakeStringSection(serializedStream.Begin(), serializedStream.End()) };
			RenderCore::Assets::ShaderPatchCollection patchCollection2(formattr2, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});

			// we should have the same contents in both patch collections
			REQUIRE(patchCollection.GetPatches().size() == patchCollection2.GetPatches().size());
			REQUIRE(patchCollection.GetHash() == patchCollection2.GetHash());
		}

		SECTION( "ShaderSourceParser::InstantiateShader" )
		{
			// Ensure that we can correctly compile the shader graph in the test data
			// (otherwise the following tests won't work)
			InputStreamFormatter<utf8> formattr { MakeStringSection(s_exampleTechniqueFragments) };
			RenderCore::Assets::ShaderPatchCollection patchCollection(formattr, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});

			std::vector<ShaderSourceParser::InstantiationRequest> instantiations;
			for (const auto& p:patchCollection.GetPatches())
				instantiations.push_back(p.second);

			ShaderSourceParser::GenerateFunctionOptions generateOptions;
			generateOptions._shaderLanguage = RenderCore::ShaderLanguage::HLSL;
			auto instantiation = ShaderSourceParser::InstantiateShader(MakeIteratorRange(instantiations), generateOptions);
			REQUIRE(instantiation._sourceFragments.size() != (size_t)0);
		}

		SECTION( "ShaderSourceParser::InstantiateShader with rename" )
		{
			InputStreamFormatter<utf8> formattr { MakeStringSection(s_fragmentsWithRename) };
			RenderCore::Assets::ShaderPatchCollection patchCollection(formattr, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});

			std::vector<ShaderSourceParser::InstantiationRequest> instantiations;
			for (const auto& p:patchCollection.GetPatches())
				instantiations.push_back(p.second);

			ShaderSourceParser::GenerateFunctionOptions generateOptions;
			generateOptions._shaderLanguage = RenderCore::ShaderLanguage::HLSL;
			auto instantiation = ShaderSourceParser::InstantiateShader(MakeIteratorRange(instantiations), generateOptions);
			REQUIRE(instantiation._sourceFragments.size() != (size_t)0);

			auto expectingPatch = Hash64("PerPixel");
			auto i = std::find_if(instantiation._entryPoints.begin(), instantiation._entryPoints.end(), [](const auto& c) { return c._implementsName == "PerPixel"; });
			REQUIRE(i != instantiation._entryPoints.end());
		}

		SECTION( "InstantiateShaderGraphCompiler" )
		{
			// Ensure that we can compile a shader graph via the intermediate compilers
			// mechanisms
			auto metalTestHelper = MakeTestHelper();
			auto customShaderSource = std::make_shared<RenderCore::MinimalShaderSource>(
				CreateDefaultShaderCompiler(*metalTestHelper->_device, *metalTestHelper->_defaultLegacyBindings),
				std::make_shared<ExpandIncludesPreprocessor>());
			auto compilerRegistration = RenderCore::Techniques::RegisterInstantiateShaderGraphCompiler(customShaderSource, compilers);

			const uint64_t CompileProcess_InstantiateShaderGraph = ConstHash64<'Inst', 'shdr'>::Value;
			
			InputStreamFormatter<utf8> formattr { MakeStringSection(s_fragmentsWithSelectors) };
			RenderCore::Assets::ShaderPatchCollection patchCollection(formattr, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});
			auto compiledCollection = std::make_shared<RenderCore::Techniques::CompiledShaderPatchCollection>(patchCollection, matDescSetLayout);
			std::vector<uint64_t> instantiations { Hash64("PerPixel") };

			::Assets::InitializerPack initializers {
				"ut-data/frameworkEntry.pixel.hlsl:frameworkEntry:ps_*", 
				"SOME_DEFINE=1",
				compiledCollection,
				instantiations
			};
			auto compileMarker = ::Assets::Internal::BeginCompileOperation(CompileProcess_InstantiateShaderGraph, std::move(initializers));
			REQUIRE(compileMarker != nullptr);
			auto compiledFromFile = compileMarker->InvokeCompile();
			REQUIRE(compiledFromFile != nullptr);
			compiledFromFile->StallWhilePending();
			REQUIRE(compiledFromFile->GetAssetState() == ::Assets::AssetState::Ready);
			auto artifacts = compiledFromFile->GetArtifactCollection(CompileProcess_InstantiateShaderGraph);
			REQUIRE(artifacts != nullptr);
			REQUIRE((bool)artifacts->GetDependencyValidation());
			REQUIRE(artifacts->GetAssetState() == ::Assets::AssetState::Ready);
		}

		SECTION( "CompileShaderPatchCollection1" )
		{
			InputStreamFormatter<utf8> formattr { MakeStringSection(s_exampleTechniqueFragments) };
			RenderCore::Assets::ShaderPatchCollection patchCollection(formattr, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});

			using RenderCore::Techniques::CompiledShaderPatchCollection;
			CompiledShaderPatchCollection compiledCollection(patchCollection, matDescSetLayout);

			// Check for some of the expected interface elements
			REQUIRE(compiledCollection.GetInterface().HasPatchType(Hash64("CoordinatesToColor")));
			const auto& descSet = compiledCollection.GetInterface().GetMaterialDescriptorSet();
			const auto& slots = descSet._slots;
			REQUIRE(slots.size() == (size_t)matDescSetLayout.GetLayout()->_slots.size());
			auto material = std::find_if(slots.begin(), slots.end(), [](const auto& t) { return t._name == "MaterialUniforms"; });
			auto second = std::find_if(slots.begin(), slots.end(), [](const auto& t) { return t._name == "SecondUnifomBuffer"; });
			REQUIRE(material != slots.end());
			REQUIRE(material->_cbIdx != ~0u);
			REQUIRE(descSet._constantBuffers[material->_cbIdx]->_elements.size() == 2);
			REQUIRE(second != slots.end());
			REQUIRE(second->_cbIdx != ~0u);
			REQUIRE(descSet._constantBuffers[second->_cbIdx]->_elements.size() == 1);
		}

		SECTION( "CompileShaderPatchCollection2" )
		{
			InputStreamFormatter<utf8> formattr { MakeStringSection(s_fragmentsWithSelectors) };
			RenderCore::Assets::ShaderPatchCollection patchCollection(formattr, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});

			using RenderCore::Techniques::CompiledShaderPatchCollection;
			CompiledShaderPatchCollection compiledCollection(patchCollection, matDescSetLayout);

			// Check for some of the recognized properties, in particular look for shader selectors
			// We're expecting the selectors "RES_HAS_TextureDif" and "RES_HAS_TextureNorm"
			ParameterBox testBox { std::make_pair("VSOUT_HAS_TEXCOORD", "1") };
			const ParameterBox* env[] = { &testBox };
			REQUIRE(compiledCollection.GetInterface().GetSelectorFilteringRules().IsRelevant("RES_HAS_TextureDif", {}, MakeIteratorRange(env)));
			REQUIRE(compiledCollection.GetInterface().GetSelectorFilteringRules().IsRelevant("RES_HAS_TextureNorm", {}, MakeIteratorRange(env)));
		}

		SECTION( "TestCompiledShaderDependencyChecking" )
		{
			// Let's make sure that the CompiledShaderPatchCollection recognizes when it has become 
			// out-of-date due to a source file change
			{
				const char* dependenciesToCheck[] = {
					"ut-data/shader_with_selectors_adapter.graph",		// root graph
					"xleres/Nodes/Templates.pixel.sh",					// import into root graph, used only by "implements" part of signature
					"ut-data/shader_with_selectors.pixel.hlsl",			// shader directly imported by root graph
					"xleres/TechniqueLibrary/Framework/gbuffer.hlsl",	// 1st level include from shader
					"xleres/TechniqueLibrary/Framework/Binding.hlsl"	// 2nd level include from shader
				};

				const char* nonDependencies[] = {
					"xleres/Nodes/Output.hlsl",				// imported but not used
					"ut-data/complicated.graph",			// not even referenced
					"shader_with_selectors_adapter.graph"	// incorrect path
				};

				InputStreamFormatter<utf8> formattr { MakeStringSection(s_fragmentsWithSelectors) };
				RenderCore::Assets::ShaderPatchCollection patchCollection(formattr, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});

				for (unsigned c=0; c<std::max(dimof(dependenciesToCheck), dimof(nonDependencies)); ++c) {
					RenderCore::Techniques::CompiledShaderPatchCollection compiledCollection(patchCollection, matDescSetLayout);
					REQUIRE(compiledCollection._depVal.GetValidationIndex() == 0u);
					
					if (c < dimof(nonDependencies)) {
						INFO(std::string{"Testing non dependency: "} + nonDependencies[c]);
						FakeChange(nonDependencies[c]);
						REQUIRE(compiledCollection._depVal.GetValidationIndex() == 0u);
					}

					if (c < dimof(dependenciesToCheck)) {
						INFO(std::string{"Testing dependency: "} + dependenciesToCheck[c]);
						FakeChange(dependenciesToCheck[c]);
						REQUIRE(compiledCollection._depVal.GetValidationIndex() > 0u);
					}
				}
			}

			// Same thing again, this time with a different shader graph, with a slightly difference
			// construction process
			{
				const char* dependenciesToCheck[] = {
					"ut-data/complicated.graph",
					"ut-data/internalComplicatedGraph.graph",
					"ut-data/example.graph",
					"ut-data/example-perpixel.pixel.hlsl"
				};

				const char* nonDependencies[] = {
					"xleres/CommonResources.h",			// raw shaders will be imported, but will not show up as dep vals from InstantiateShader
					"xleres/MainGeometry.h"
				};

				for (unsigned c=0; c<std::max(dimof(dependenciesToCheck), dimof(nonDependencies)); ++c) {
					using namespace ShaderSourceParser;
					InstantiationRequest instRequest { "ut-data/complicated.graph" };
					GenerateFunctionOptions options;
					options._shaderLanguage = RenderCore::ShaderLanguage::HLSL;
					auto inst = ShaderSourceParser::InstantiateShader(
						MakeIteratorRange(&instRequest, &instRequest+1),
						options);

					// Create one dep val that references all of the children
					auto depVal = ::Assets::GetDepValSys().Make();
					for (const auto&d:inst._depVals)
						depVal.RegisterDependency(d);

					if (c < dimof(nonDependencies)) {
						FakeChange(nonDependencies[c]);
						REQUIRE(depVal.GetValidationIndex() == 0u);
					}

					if (c < dimof(dependenciesToCheck)) {
						FakeChange(dependenciesToCheck[c]);
						REQUIRE(depVal.GetValidationIndex() > 0u);
					}
				}
			}
		}

		::Assets::MainFileSystem::GetMountingTree()->Unmount(mnt1);
		::Assets::MainFileSystem::GetMountingTree()->Unmount(mnt0);
	}

}

