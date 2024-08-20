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
#include "../../../RenderCore/Techniques/PipelineLayoutDelegate.h"
#include "../../../RenderCore/Techniques/SpriteTechnique.h"
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
#include "../../../Assets/AssetTraits.h"
#include "../../../Assets/InitializerPack.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/IArtifact.h"
#include "../../../ConsoleRig/Console.h"
#include "../../../OSServices/Log.h"
#include "../../../OSServices/FileSystemMonitor.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "../../../Formatters/TextFormatter.h"
#include "../../../Formatters/TextOutputFormatter.h"
#include "../../../Utility/Streams/StreamTypes.h"
#include "../../../Utility/MemoryUtils.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace Utility::Literals;

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
			Implements=xleres/Objects/Templates.pixel.hlsl::PerPixel
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
				import templates = "xleres/Objects/Templates.pixel.hlsl"
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
				import templates = "xleres/Objects/Templates.pixel.hlsl"
				import output = "xleres/Nodes/Output.sh"
				import materialParam = "xleres/Objects/MaterialParam.hlsl"

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

				Texture2D       TextureDif		BIND_MAT_T1;
				Texture2D       TextureNorm		BIND_MAT_T2;

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
					#if VSOUT_HAS_TEXCOORD && (RES_HAS_TextureDif!=0)
						diffuseTextureSample = TextureDif.Sample(MaybeAnisotropicSampler, geo.texCoord);
						result.diffuseAlbedo = diffuseTextureSample.rgb;
						result.blendingAlpha = diffuseTextureSample.a;
					#endif

					#if VSOUT_HAS_TEXCOORD && (RES_HAS_TextureNorm!=0)
						float3 normalMapSample = SampleNormalMap(TextureNorm, DefaultSampler, true, geo.texCoord);
						result.worldSpaceNormal = normalMapSample; // TransformTangentSpaceToWorld(normalMapSample, geo);
					#elif VSOUT_HAS_NORMAL
						result.worldSpaceNormal = normalize(geo.normal);
					#endif

					return result;
				}
			)--")),

		std::make_pair(
			"shader_with_selectors_adapter.graph",
			::Assets::AsBlob(R"--(
				import templates = "xleres/Objects/Templates.pixel.hlsl"
				import output = "xleres/Nodes/Output.sh"
				import materialParam = "xleres/Objects/MaterialParam.hlsl"
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

		std::make_pair(
			"sprite-patch-test.hlsl",
			::Assets::AsBlob(R"--(
#include "xleres/TechniqueLibrary/Framework/CommonResources.hlsl"
#include "xleres/TechniqueLibrary/Math/Misc.hlsl"

void vs(
	out float rotation : ROTATION,
	float4 colorLinear : COLOR0,
	uint vIdx : SV_VertexID)
{
	rotation = colorLinear.r * 3.14159f + vIdx * 0.1f;
}

void gs(
	float gs_param : GSPARAM,
	out float2 tc0 : TEXCOORD0,
	out float2 tc1 : TEXCOORD1,
	out float2 tc2 : TEXCOORD2,
	out float2 tc3 : TEXCOORD3)
{
	tc0 = float2(0.f, 0.f);
	tc1 = float2(0.f, 1.f);
	tc2 = float2(1.f, 0.f);
	tc3 = float2(1.f, 1.f);
}

Texture2D<float> ParticleTexture;

cbuffer MaterialConstants
{
	float3 BlueBadge;
};

void ps(
	out float4 emissive : SV_Target0,
	float4 position : SV_Position,
	float4 color : COLOR,
	float2 texCoord : TEXCOORD)
{
	float r = length(texCoord - 0.5.xx) * 2.f;
	r += DitherPatternInt(position.xy) / 30.f;
	if (r < 1) discard;
	float part = ParticleTexture.SampleLevel(PointClampSampler, texCoord, 0).r;
	return float4(max(1, r)*BlueBadge, .75f * part);
}
		)--")),

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
		auto globalServices = ConsoleRig::MakeGlobalServices(GetStartupConfig());
		auto mnt0 = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto mnt1 = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::EnableChangeMonitoring));
		auto& compilers = ::Assets::Services::GetIntermediateCompilers();
		auto filteringRegistration = ShaderSourceParser::RegisterShaderSelectorFilteringCompiler(compilers);

		RenderCore::Assets::PredefinedPipelineLayoutFile pipelineLayoutFile(TechniqueTestApparatus::UnitTestPipelineLayout, {}, {});
		auto matDescSetLayout = RenderCore::Techniques::FindLayout(pipelineLayoutFile, "GraphicsMain", "Material", RenderCore::PipelineType::Graphics);

		SECTION( "DeserializeShaderPatchCollection" )
		{
			// Normally a ShaderPatchCollection is deserialized from a material file
			// We'll test the serialization and deserialization code here, and ensure
			Formatters::TextInputFormatter<> formattr { MakeStringSection(s_exampleTechniqueFragments) };
			RenderCore::Assets::ShaderPatchCollection patchCollection(formattr);

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
			Formatters::TextOutputFormatter outFmttr(strm);
			SerializationOperator(outFmttr, patchCollection);

			// Now let's verify that we can deserialize in what we just wrote out
			auto& serializedStream = strm.GetBuffer();
			Formatters::TextInputFormatter<utf8> formattr2 { MakeStringSection(serializedStream.Begin(), serializedStream.End()) };
			RenderCore::Assets::ShaderPatchCollection patchCollection2(formattr2);

			// we should have the same contents in both patch collections
			REQUIRE(patchCollection.GetPatches().size() == patchCollection2.GetPatches().size());
			REQUIRE(patchCollection.GetHash() == patchCollection2.GetHash());
		}

		SECTION( "ShaderSourceParser::InstantiateShader" )
		{
			// Ensure that we can correctly compile the shader graph in the test data
			// (otherwise the following tests won't work)
			Formatters::TextInputFormatter<utf8> formattr { MakeStringSection(s_exampleTechniqueFragments) };
			RenderCore::Assets::ShaderPatchCollection patchCollection(formattr);

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
			Formatters::TextInputFormatter<utf8> formattr { MakeStringSection(s_fragmentsWithRename) };
			RenderCore::Assets::ShaderPatchCollection patchCollection(formattr);

			std::vector<ShaderSourceParser::InstantiationRequest> instantiations;
			for (const auto& p:patchCollection.GetPatches())
				instantiations.push_back(p.second);

			ShaderSourceParser::GenerateFunctionOptions generateOptions;
			generateOptions._shaderLanguage = RenderCore::ShaderLanguage::HLSL;
			auto instantiation = ShaderSourceParser::InstantiateShader(MakeIteratorRange(instantiations), generateOptions);
			REQUIRE(instantiation._sourceFragments.size() != (size_t)0);

			auto i = std::find_if(instantiation._entryPoints.begin(), instantiation._entryPoints.end(), [](const auto& c) { return c._implementsName == "PerPixel"; });
			REQUIRE(i != instantiation._entryPoints.end());
		}

		SECTION( "InstantiateShaderGraphCompiler" )
		{
			// Ensure that we can compile a shader graph via the intermediate compilers
			// mechanisms
			auto metalTestHelper = MakeTestHelper();
			auto customShaderSource = RenderCore::CreateMinimalShaderSource(
				CreateDefaultShaderCompiler(*metalTestHelper->_device, *metalTestHelper->_defaultLegacyBindings),
				std::make_shared<ExpandIncludesPreprocessor>());
			auto compilerRegistration = RenderCore::Techniques::RegisterInstantiateShaderGraphCompiler(customShaderSource, compilers);

			const uint64_t CompileProcess_InstantiateShaderGraph = ConstHash64Legacy<'Inst', 'shdr'>::Value;
			
			Formatters::TextInputFormatter<utf8> formattr { MakeStringSection(s_fragmentsWithSelectors) };
			RenderCore::Assets::ShaderPatchCollection patchCollection(formattr);
			auto compiledCollection = std::make_shared<RenderCore::Techniques::CompiledShaderPatchCollection>(patchCollection, *matDescSetLayout);
			std::vector<uint64_t> instantiations { "PerPixel"_h };

			::Assets::InitializerPack initializers {
				"ut-data/frameworkEntry.pixel.hlsl:frameworkEntry:ps_*", 
				"SOME_DEFINE=1",
				compiledCollection,
				instantiations
			};
			auto compileMarker = ::Assets::Internal::BeginCompileOperation(CompileProcess_InstantiateShaderGraph, std::move(initializers));
			REQUIRE(compileMarker != nullptr);
			auto compiledFromFile = compileMarker->InvokeCompile(CompileProcess_InstantiateShaderGraph);
			REQUIRE(compiledFromFile.Valid());
			compiledFromFile.StallWhilePending();
			REQUIRE(compiledFromFile.GetAssetState() == ::Assets::AssetState::Ready);
			auto& artifacts = compiledFromFile.GetArtifactCollection();
			REQUIRE((bool)artifacts.GetDependencyValidation());
			REQUIRE(artifacts.GetAssetState() == ::Assets::AssetState::Ready);
		}

		SECTION( "CompileShaderPatchCollection1" )
		{
			Formatters::TextInputFormatter<utf8> formattr { MakeStringSection(s_exampleTechniqueFragments) };
			RenderCore::Assets::ShaderPatchCollection patchCollection(formattr);

			using RenderCore::Techniques::CompiledShaderPatchCollection;
			CompiledShaderPatchCollection compiledCollection(patchCollection, *matDescSetLayout);

			// Check for some of the expected interface elements
			REQUIRE(compiledCollection.GetInterface().HasPatchType("CoordinatesToColor"_h));
			const auto& descSet = compiledCollection.GetInterface().GetMaterialDescriptorSet();
			const auto& slots = descSet._slots;
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
			Formatters::TextInputFormatter<utf8> formattr { MakeStringSection(s_fragmentsWithSelectors) };
			RenderCore::Assets::ShaderPatchCollection patchCollection(formattr);

			using RenderCore::Techniques::CompiledShaderPatchCollection;
			CompiledShaderPatchCollection compiledCollection(patchCollection, *matDescSetLayout);

			// Check for some of the recognized properties, in particular look for shader selectors
			// We're expecting the selectors "RES_HAS_TextureDif" and "RES_HAS_TextureNorm"
			ParameterBox testBox { std::make_pair("VSOUT_HAS_TEXCOORD", "1") };
			const ParameterBox* env[] = { &testBox };
			REQUIRE(compiledCollection.GetInterface().GetSelectorFilteringRules(0).IsRelevant("RES_HAS_TextureDif", {}, MakeIteratorRange(env)));
			REQUIRE(compiledCollection.GetInterface().GetSelectorFilteringRules(0).IsRelevant("RES_HAS_TextureNorm", {}, MakeIteratorRange(env)));
		}

		SECTION( "TestCompiledShaderDependencyChecking" )
		{
			// Let's make sure that the CompiledShaderPatchCollection recognizes when it has become 
			// out-of-date due to a source file change
			{
				const char* dependenciesToCheck[] = {
					"ut-data/shader_with_selectors_adapter.graph",		// root graph
					"xleres/Objects/Templates.pixel.hlsl",					// import into root graph, used only by "implements" part of signature
					"ut-data/shader_with_selectors.pixel.hlsl",			// shader directly imported by root graph
					"xleres/TechniqueLibrary/Framework/gbuffer.hlsl",	// 1st level include from shader
					"xleres/TechniqueLibrary/Framework/Binding.hlsl"	// 2nd level include from shader
				};

				const char* nonDependencies[] = {
					"xleres/Nodes/Output.hlsl",				// imported but not used
					"ut-data/complicated.graph",			// not even referenced
					"shader_with_selectors_adapter.graph"	// incorrect path
				};

				Formatters::TextInputFormatter<utf8> formattr { MakeStringSection(s_fragmentsWithSelectors) };
				RenderCore::Assets::ShaderPatchCollection patchCollection(formattr);

				for (unsigned c=0; c<std::max(dimof(dependenciesToCheck), dimof(nonDependencies)); ++c) {
					RenderCore::Techniques::CompiledShaderPatchCollection compiledCollection(patchCollection, *matDescSetLayout);
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

	TEST_CASE( "ShaderParser-SpriteStyleLinking", "[shader_parser]" )
	{
		auto globalServices = ConsoleRig::MakeGlobalServices(GetStartupConfig());
		auto xleresMount = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto utDataMount = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));
		auto shaderFilteringRegistration = ShaderSourceParser::RegisterShaderSelectorFilteringCompiler(::Assets::Services::GetIntermediateCompilers());

		// auto metalTestHelper = MakeTestHelper();
		// auto customShaderSource = RenderCore::CreateMinimalShaderSource(
		// 	CreateDefaultShaderCompiler(*metalTestHelper->_device, *metalTestHelper->_defaultLegacyBindings),
		// 	std::make_shared<ExpandIncludesPreprocessor>());

		// Create a CompiledShaderPatchCollection containing the patches we need
		std::unique_ptr<RenderCore::Techniques::CompiledShaderPatchCollection> compiledShaderPatchCollection;
		{
			ShaderSourceParser::InstantiationRequest instRequests[] {
				{ "ut-data/sprite-patch-test.hlsl::vs", "SV_SpriteVS" },
				{ "ut-data/sprite-patch-test.hlsl::gs", "SV_SpriteGS" },
				{ "ut-data/sprite-patch-test.hlsl::ps", "SV_SpritePS" }
			};
			RenderCore::Assets::ShaderPatchCollection patchCollection;
			unsigned idx=0;
			for (const auto& p:instRequests)
				patchCollection.AddPatch(std::to_string(idx++), p);
			compiledShaderPatchCollection = std::make_unique<RenderCore::Techniques::CompiledShaderPatchCollection>(patchCollection, RenderCore::Techniques::DescriptorSetLayoutAndBinding{});
		}

		std::vector<RenderCore::Techniques::PatchDelegateInput> patchesInterface;
		unsigned idx=0; for (const auto& p:compiledShaderPatchCollection->GetInterface().GetPatches())
			patchesInterface.emplace_back(RenderCore::Techniques::PatchDelegateInput{"patch" + std::to_string(idx++), p._scaffoldSignature.get(), p._implementsHash});

		// Generate the pipeline instantiation using the patches provided
		std::vector<uint64_t> iaAttributes { "POSITION"_h, "COLOR"_h };
		auto spritePipelineInstantiation = RenderCore::Techniques::BuildSpritePipeline(
			patchesInterface, MakeIteratorRange(iaAttributes));
		assert(!spritePipelineInstantiation.empty());
	}

}

