// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)


#include "../../UnitTestHelper.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../RenderCore/ShaderLangUtil.h"
#include "../../../RenderCore/UniformsStream.h"
#include "../../../Assets/AssetUtils.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../Assets/AssetTraits.h"
#include "../../../Assets/PreprocessorIncludeHandler.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <unordered_map>

using namespace Catch::literals;
namespace UnitTests
{
	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair(
			"sequencer-ds.pipeline",
			::Assets::AsBlob(R"--(
				DescriptorSet Sequencer {
					UniformBuffer GlobalTransform
					{
						float4x4 WorldToClip;
						float4 FrustumCorners[4];
						float3 WorldSpaceView;
						float FarClip;
						float4 MinimalProjection;
						float4x4 CameraBasis;
					};

					UniformBuffer LocalTransform;
					
					UniformBuffer GlobalState
					{
						float GlobalTime;
						uint GlobalSamplingPassIndex;
						uint GlobalSamplingPassCount;
					};

					UniformBuffer cb0;
					UniformBuffer cb1;

					SampledTexture tex0;
					SampledTexture tex1;
					SampledTexture tex2;
					SampledTexture tex3;
					SampledTexture tex4;
					SampledTexture tex5;
				};
			)--")
		),

		std::make_pair(
			"graphics-main.pipeline",
			::Assets::AsBlob(R"--(
				#include "sequencer-ds.pipeline"

				DescriptorSet Material {
					UniformBuffer cb0;
					UniformBuffer cb1;
					UniformBuffer cb2;

					SampledTexture tex0;
					SampledTexture tex1;
					SampledTexture tex2;
					SampledTexture tex3;
					SampledImage glslNaming4;
					SampledImage glslNaming5;
					SampledImage glslNaming6;
					SampledImage glslNaming7;

					UnorderedAccessBuffer uab0;
					StorageBuffer uab1;

					UnorderedAccessTexture uat0;

					Sampler sampler0;
				};

				DescriptorSet DescriptorSetWithArrays {
					UniformBuffer arrayOfCBs[3];
					SampledTexture arrayOfTextures[5];
					StorageImage uabt[5];
					Sampler samplerArray[2];
				};

				PipelineLayout GraphicsMain {
					DescriptorSet Sequencer;
					DescriptorSet Material;

					VSPushConstants LocalTransform
					{
						float3x4 LocalToWorld;
						float3 LocalSpaceView;
						uint Dummy;
					};

					PSPushConstants pspush
					{
						float4x4 SomeTransforms[4];
					};

					GSPushConstants gspush
					{
						float4x4 SomeTransforms[4];
					};
				};
			)--"))
	};

	TEST_CASE( "PipelineConfiguration", "[rendercore_assets]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto mnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));

		auto layoutFile = ::Assets::AutoConstructAsset<std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayoutFile>>("ut-data/graphics-main.pipeline");
		REQUIRE(layoutFile->_pipelineLayouts.size() == 1);
		REQUIRE(layoutFile->_pipelineLayouts.begin()->first == "GraphicsMain");
		auto& pipelineLayout = *layoutFile->_pipelineLayouts.begin()->second;
		REQUIRE(pipelineLayout._descriptorSets.size() == 2);
		REQUIRE(pipelineLayout._vsPushConstants.first == "LocalTransform");
		REQUIRE(pipelineLayout._psPushConstants.first == "pspush");
		REQUIRE(pipelineLayout._gsPushConstants.first == "gspush");

		// Attempt to build an actual ICompiledPipelineLayout from the configuration we loaded
		auto pipelineLayoutInitializer = RenderCore::Assets::PredefinedPipelineLayout{*layoutFile, "GraphicsMain"}.MakePipelineLayoutInitializer(RenderCore::ShaderLanguage::HLSL);
		auto testHelper = MakeTestHelper();
		auto compiledLayout = testHelper->_device->CreatePipelineLayout(pipelineLayoutInitializer);
		REQUIRE(compiledLayout != nullptr);

		::Assets::MainFileSystem::GetMountingTree()->Unmount(mnt);
	}

	TEST_CASE( "PipelineConfiguration-BadSyntax", "[rendercore_assets]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());

		using PredefinedPipelineLayoutFile = RenderCore::Assets::PredefinedPipelineLayoutFile;

		REQUIRE_THROWS(
			PredefinedPipelineLayoutFile{"#include <file-without-include-handler>", {}, {}});

		REQUIRE_THROWS(
			PredefinedPipelineLayoutFile{R"(
				PipelineLayout GraphicsMain {
					DescriptorSet UndeclaredDescriptorSet;
					DescriptorSet UndeclaredDescriptorSet2;
				}
			)", {}, {}});

		REQUIRE_THROWS(
			PredefinedPipelineLayoutFile{R"(
				DescriptorSet Material {
					UnknownObject obj0;
				};
			)", {}, {}});

		REQUIRE_THROWS(
			PredefinedPipelineLayoutFile{R"(
				DescriptorSet MissingSemi1 {}
				DescriptorSet MissingSemi2 {}
			)", {}, {}});
	}
}


/*


			DescriptorSet Draw {
				SampledTexture tex0;
				SampledTexture tex1;
				SampledTexture tex2;
				SampledTexture tex3;
				SampledTexture tex4;
				SampledTexture tex5;
				SampledTexture tex6;
				SampledTexture tex7;
				SampledTexture tex8;
				SampledTexture tex9;
			}

			DescriptorSet Numeric {
				SampledTexture tex0;
				SampledTexture tex1;
				SampledTexture tex2;
				SampledTexture tex3;
				SampledTexture tex4;
				SampledTexture tex5;
				SampledTexture tex6;
				SampledTexture tex7;
				SampledTexture tex8;
				SampledTexture tex9;
				SampledTexture tex10;
				SampledTexture tex11;
				SampledTexture tex12;
				SampledTexture tex13;
				SampledTexture tex14;

				Sampler sampler0;
				Sampler sampler1;
				Sampler sampler2;
				Sampler sampler3;
				Sampler sampler4;
				Sampler sampler5;
				Sampler sampler6;

				UniformBuffer cb0;
				UniformBuffer cb1;
				UniformBuffer cb2;
				UniformBuffer cb3;

				Sampler sampler7;
			}

*/