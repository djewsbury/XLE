// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueTestsHelper.h"
#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../ReusableDataFiles.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../../RenderCore/Techniques/PipelineAcceleratorInternal.h"
#include "../../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../../RenderCore/Techniques/CompiledShaderPatchCollection.h"
#include "../../../RenderCore/Techniques/DescriptorSetAccelerator.h"
#include "../../../RenderCore/Techniques/Services.h"
#include "../../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/DescriptorSetAccelerator.h"
#include "../../../RenderCore/Techniques/PipelineOperators.h"
#include "../../../RenderCore/Metal/Shader.h"
#include "../../../RenderCore/Metal/InputLayout.h"
#include "../../../RenderCore/Metal/State.h"
#include "../../../RenderCore/Metal/PipelineLayout.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Assets/Services.h"
#include "../../../RenderCore/Assets/MaterialScaffold.h"
#include "../../../RenderCore/Assets/PredefinedCBLayout.h"
#include "../../../RenderCore/ResourceDesc.h"
#include "../../../RenderCore/FrameBufferDesc.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../BufferUploads/BufferUploads_Manager.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../Assets/DepVal.h"
#include "../../../Assets/AssetTraits.h"
#include "../../../Assets/AssetSetManager.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/CompileAndAsyncManager.h"
#include "../../../Math/Matrix.h"
#include "../../../Math/MathSerialization.h"
#include "../../../ConsoleRig/Console.h"
#include "../../../OSServices/Log.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "thousandeyes/futures/then.h"
#include "thousandeyes/futures/DefaultExecutor.h"
#include <map>
#include <regex>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

static const char s_exampleTechniqueFragments[] = R"--(
	main=~
		ut-data/complicated.graph::Bind2_PerPixel
)--";

static const char* s_colorFromSelectorShaderFile = R"--(
	#include "xleres/TechniqueLibrary/Framework/VSOUT.hlsl"
	#include "xleres/TechniqueLibrary/Framework/gbuffer.hlsl"
	#include "xleres/Nodes/Templates.pixel.sh"

	GBufferValues PerPixel(VSOUT geo)
	{
		GBufferValues result = GBufferValues_Default();
		#if VSOUT_HAS_TEXCOORD
			#if defined(COLOR_RED)
				result.diffuseAlbedo = float3(1,0,0);
			#elif defined(COLOR_GREEN)
				result.diffuseAlbedo = float3(0,1,0);
			#else
				#error Intentional compile error
			#endif
		#endif
		result.material.roughness = 1.0;		// (since this is written to SV_Target0.a, ensure it's set to 1)
		return result;
	}
)--";

static const char s_techniqueForColorFromSelector[] = R"--(
	main=~
		ut-data/colorFromSelector.pixel.hlsl::PerPixel
)--";

static const char* s_basicTexturingGraph = R"--(
	import templates = "xleres/Nodes/Templates.pixel.sh"
	import output = "xleres/Nodes/Output.sh"
	import texture = "xleres/Nodes/Texture.sh"
	import basic = "xleres/Nodes/Basic.sh"
	import materialParam = "xleres/Nodes/MaterialParam.sh"

	GBufferValues Bind_PerPixel(VSOUT geo) implements templates::PerPixel
	{
		captures MaterialUniforms = ( float3 Multiplier = "{1,1,1}", float Adder = "{0,0,0}", Texture2D BoundTexture, SamplerState BoundSampler );
		node samplingCoords = basic::Multiply2(lhs:texture::GetPixelCoords(geo:geo).result, rhs:"float2(.1, .1)");
		node loadFromTexture = texture::SampleWithSampler(
			inputTexture:MaterialUniforms.BoundTexture, 
			inputSampler:MaterialUniforms.BoundSampler,
			texCoord:samplingCoords.result);
		node multiply = basic::Multiply3(lhs:loadFromTexture.result, rhs:MaterialUniforms.Multiplier);
		node add = basic::Add3(lhs:multiply.result, rhs:MaterialUniforms.Adder);
		node mat = materialParam::CommonMaterialParam_Make(roughness:"1", specular:"1", metal:"1");
		return output::Output_PerPixel(
			diffuseAlbedo:add.result, 
			material:mat.result).result;
	}
)--";

static const char s_patchCollectionBasicTexturing[] = R"--(
	main=~
		ut-data/basicTexturingGraph.graph::Bind_PerPixel
)--";

using namespace Catch::literals;
namespace UnitTests
{
	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair("basic.tech", ::Assets::AsBlob(s_basicTechniqueFile)),
		std::make_pair("example-perpixel.pixel.hlsl", ::Assets::AsBlob(s_examplePerPixelShaderFile)),
		std::make_pair("example.graph", ::Assets::AsBlob(s_exampleGraphFile)),
		std::make_pair("complicated.graph", ::Assets::AsBlob(s_complicatedGraphFile)),
		std::make_pair("internalShaderFile.pixel.hlsl", ::Assets::AsBlob(s_internalShaderFile)),
		std::make_pair("internalComplicatedGraph.graph", ::Assets::AsBlob(s_internalComplicatedGraph)),
		std::make_pair("colorFromSelector.pixel.hlsl", ::Assets::AsBlob(s_colorFromSelectorShaderFile)),
		std::make_pair("basicTexturingGraph.graph", ::Assets::AsBlob(s_basicTexturingGraph))
	};

	static RenderCore::FrameBufferDesc MakeSimpleFrameBufferDesc()
	{
		using namespace RenderCore;
		SubpassDesc mainSubpass;
		mainSubpass.AppendOutput(0);

		std::vector<AttachmentDesc> attachments;
		attachments.push_back(AttachmentDesc{Format::R8G8B8A8_UNORM});
		std::vector<SubpassDesc> subpasses;
		subpasses.push_back(mainSubpass);

		return FrameBufferDesc { std::move(attachments), std::move(subpasses) };
	}

	class VertexPCT
	{
	public:
		Float3      _position;
		unsigned    _color;
		Float2      _texCoord;
	};

	static VertexPCT vertices_fullViewport[] = {
		// Counter clockwise-winding triangle
		VertexPCT { Float3 {  -1.0f, -1.0f,  0.0f }, 0xffffffff, Float2 { 0.f, 0.f } },
		VertexPCT { Float3 {  -1.0f,  1.0f,  0.0f }, 0xffffffff, Float2 { 0.f, 1.f } },
		VertexPCT { Float3 {   1.0f, -1.0f,  0.0f }, 0xffffffff, Float2 { 1.f, 0.f } },

		// Counter clockwise-winding triangle
		VertexPCT { Float3 {  -1.0f,  1.0f,  0.0f }, 0xffffffff, Float2 { 0.f, 1.f } },
		VertexPCT { Float3 {   1.0f, -1.0f,  0.0f }, 0xffffffff, Float2 { 1.f, 0.f } },
		VertexPCT { Float3 {   1.0f,  1.0f,  0.0f }, 0xffffffff, Float2 { 1.f, 1.f } }
	};

	static void RenderQuad(
		MetalTestHelper& testHelper,
		RenderCore::IThreadContext& threadContext,
		const RenderCore::IResource& vb, unsigned vertexCount,
		const RenderCore::Metal::GraphicsPipeline& pipeline,
		const std::shared_ptr<RenderCore::ICompiledPipelineLayout>& pipelineLayout,
		const RenderCore::IDescriptorSet* descSet = nullptr);

	static std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> GetPatchCollectionFromText(StringSection<> techniqueText)
	{
		using namespace RenderCore;

		InputStreamFormatter<utf8> formattr { techniqueText.Cast<utf8>() };
		return std::make_shared<RenderCore::Assets::ShaderPatchCollection>(formattr, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});
	}

	template<typename Type>
		void RequireReady(::Assets::Future<Type>& future)
	{
		INFO(::Assets::AsString(future.GetActualizationLog()));
		REQUIRE(future.GetAssetState() == ::Assets::AssetState::Ready);
	}

	TEST_CASE( "PipelineAcceleratorTests-ConfigurationAndCreation", "[rendercore_techniques]" )
	{
		using namespace RenderCore;

		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto utdatamnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));
		auto testHelper = MakeTestHelper();
		TechniqueTestApparatus testApparatus(*testHelper);

		auto techniqueSetFile = ::Assets::MakeAsset<Techniques::TechniqueSetFile>("ut-data/basic.tech");
		auto techniqueDelegate = Techniques::CreateTechniqueDelegate_Deferred(techniqueSetFile);

		auto mainPool = testApparatus._pipelineAccelerators;
		mainPool->SetGlobalSelector("GLOBAL_SEL", 55);
		auto cfgId = mainPool->CreateSequencerConfig(
			"cfgId",
			techniqueDelegate,
			ParameterBox { std::make_pair("SEQUENCER_SEL", "37") },
			MakeSimpleFrameBufferDesc());

		RenderCore::Assets::RenderStateSet doubledSidedStateSet;
		doubledSidedStateSet._doubleSided = true;
		doubledSidedStateSet._flag |= RenderCore::Assets::RenderStateSet::Flag::DoubleSided;

			//
			//	Create a pipeline, and ensure that we get something valid out of it
			//
		{
			auto patches = GetPatchCollectionFromText(s_exampleTechniqueFragments);
			auto pipelineAccelerator = mainPool->CreatePipelineAccelerator(
				patches,
				ParameterBox { std::make_pair("SIMPLE_BIND", "1") },
				GlobalInputLayouts::PNT,
				Topology::TriangleList,
				doubledSidedStateSet);

			auto finalPipeline = mainPool->GetPipelineFuture(*pipelineAccelerator, *cfgId);
			REQUIRE(finalPipeline);
			finalPipeline->StallWhilePending();
			RequireReady(*finalPipeline);
		}

			//
			//	Now create another pipeline, this time one that will react to some of the
			//	selectors as we change them
			//
		{
			auto patches = GetPatchCollectionFromText(s_techniqueForColorFromSelector);
			auto pipelineNoTexCoord = mainPool->CreatePipelineAccelerator(
				patches,
				ParameterBox {},
				GlobalInputLayouts::P,
				Topology::TriangleList,
				doubledSidedStateSet);

			{
					//
					//	We should get a valid pipeline in this case; since there are no texture coordinates
					//	on the geometry, this disables the code that triggers a compiler warning
					//
				auto finalPipelineFuture = mainPool->GetPipelineFuture(*pipelineNoTexCoord, *cfgId);
				REQUIRE(finalPipelineFuture);
				finalPipelineFuture->StallWhilePending();
				RequireReady(*finalPipelineFuture);
			}

			auto pipelineWithTexCoord = mainPool->CreatePipelineAccelerator(
				patches,
				ParameterBox {},
				GlobalInputLayouts::PCT,
				Topology::TriangleList,
				doubledSidedStateSet);

			{
					//
					//	Here, the pipeline will fail to compile. We should ensure we get a reasonable
					//	error message -- that is the shader compile error should propagate through
					//	to the pipeline error log
					//
				auto finalPipelineFuture = mainPool->GetPipelineFuture(*pipelineWithTexCoord, *cfgId);
				REQUIRE(finalPipelineFuture);
				finalPipelineFuture->StallWhilePending();
				REQUIRE(finalPipelineFuture->GetAssetState() == ::Assets::AssetState::Invalid);
				auto log = ::Assets::AsString(finalPipelineFuture->GetActualizationLog());
				REQUIRE(!log.empty());
			}

			// Now we'll create a new sequencer config, and we're actually going to use
			// this to render
			
			auto threadContext = testHelper->_device->GetImmediateContext();
			auto targetDesc = CreateDesc(
				BindFlag::RenderTarget, 0, GPUAccess::Write,
				TextureDesc::Plain2D(64, 64, Format::R8G8B8A8_UNORM),
				"temporary-out");
			UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
			auto cfgIdWithColor = mainPool->CreateSequencerConfig(
				"cfgIdWithColor",
				techniqueDelegate,
				ParameterBox { std::make_pair("COLOR_RED", "1") },
				MakeSimpleFrameBufferDesc());

			auto vertexBuffer = testHelper->CreateVB(vertices_fullViewport);

			{
				auto finalPipelineFuture = mainPool->GetPipelineFuture(*pipelineWithTexCoord, *cfgIdWithColor);
				REQUIRE(finalPipelineFuture);
				finalPipelineFuture->StallWhilePending();
				RequireReady(*finalPipelineFuture);

				auto rpi = fbHelper.BeginRenderPass(*threadContext);
				RenderQuad(
					*testHelper, *threadContext, *vertexBuffer, (unsigned)dimof(vertices_fullViewport), 
					*finalPipelineFuture->Actualize()._metalPipeline,
					mainPool->GetCompiledPipelineLayoutFuture(*cfgIdWithColor)->Actualize()->GetPipelineLayout());
			}

			// We should have filled the entire framebuffer with red 
			// (due to the COLOR_RED selector in the sequencer config)
			auto breakdown0 = fbHelper.GetFullColorBreakdown(*threadContext);
			REQUIRE(breakdown0.size() == 1);
			REQUIRE(breakdown0.begin()->first == 0xff0000ff);

			// Change the sequencer config to now set the COLOR_GREEN selector
			cfgIdWithColor = mainPool->CreateSequencerConfig(
				"cfgIdWithColor",
				techniqueDelegate,
				ParameterBox { std::make_pair("COLOR_GREEN", "1") },
				MakeSimpleFrameBufferDesc());

			{
				auto finalPipelineFuture = mainPool->GetPipelineFuture(*pipelineWithTexCoord, *cfgIdWithColor);
				REQUIRE(finalPipelineFuture);
				finalPipelineFuture->StallWhilePending();
				RequireReady(*finalPipelineFuture);

				auto rpi = fbHelper.BeginRenderPass(*threadContext);
				RenderQuad(
					*testHelper, *threadContext, *vertexBuffer, (unsigned)dimof(vertices_fullViewport), 
					*finalPipelineFuture->Actualize()._metalPipeline,
					mainPool->GetCompiledPipelineLayoutFuture(*cfgIdWithColor)->Actualize()->GetPipelineLayout());
			}

			auto breakdown1 = fbHelper.GetFullColorBreakdown(*threadContext);
			REQUIRE(breakdown1.size() == 1);
			REQUIRE(breakdown1.begin()->first == 0xff00ff00);
		}

		::Assets::MainFileSystem::GetMountingTree()->Unmount(utdatamnt);
		::Assets::MainFileSystem::GetMountingTree()->Unmount(xlresmnt);
	}

	static void StallForDescriptorSet(RenderCore::IThreadContext& threadContext, ::Assets::Future<RenderCore::Techniques::ActualizedDescriptorSet>& descriptorSetFuture)
	{
		auto state = descriptorSetFuture.StallWhilePending();
		if (state.has_value() && state.value() == ::Assets::AssetState::Ready)
			RenderCore::Techniques::Services::GetBufferUploads().StallUntilCompletion(threadContext, descriptorSetFuture.Actualize().GetCompletionCommandList());
	}

	TEST_CASE( "PipelineAcceleratorTests-DescriptorSetAcceleratorConstruction", "[rendercore_techniques]" )
	{
		//
		// Create descriptor set accelerators and pipeline accelerators from the pipeline accelerator pool
		// using configurations that require shader inputs
		// Test rendering after assigning those shader inputs 
		// Also in this case, we have a technique delegate that uses the standard infrastructure (ie, instead
		// of something that's simplified for unit tests)
		//

		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto utdatamnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));
		auto testHelper = MakeTestHelper();
		TechniqueTestApparatus techniqueTestHelper(*testHelper);
		auto pipelineAcceleratorPool = techniqueTestHelper._pipelineAccelerators;

		SECTION("FindShaderUniformBindings")
		{
			// Create a CompiledShaderPatchCollection from a typical input, and get the 
			// descriptor set layout from that.
			// Construct a DestructorSetAccelerator from it
			auto patches = GetPatchCollectionFromText(s_exampleTechniqueFragments);

			ParameterBox constantBindings;
			constantBindings.SetParameter("DiffuseColor", Float3{1.0f, 0.5f, 0.2f});

			ParameterBox resourceBindings;
			ParameterBox materialSelectors;
			auto descriptorSetAccelerator = pipelineAcceleratorPool->CreateDescriptorSetAccelerator(
				patches,
				materialSelectors, constantBindings, resourceBindings);
			auto descriptorSetFuture = pipelineAcceleratorPool->GetDescriptorSetFuture(*descriptorSetAccelerator);
			if (descriptorSetFuture) descriptorSetFuture->StallWhilePending();
			auto& descSet = descriptorSetFuture->Actualize();
			auto* bindingInfo = &descSet._bindingInfo;

			// we should have 2 constant buffers and no shader resources
			auto materialUniformsI = std::find_if(bindingInfo->_slots.begin(), bindingInfo->_slots.end(), [](const auto& slot) { return slot._layoutName == "MaterialUniforms"; });
			auto anotherCapturesI = std::find_if(bindingInfo->_slots.begin(), bindingInfo->_slots.end(), [](const auto& cb) { return cb._layoutName == "AnotherCaptures"; });
			REQUIRE(materialUniformsI != bindingInfo->_slots.end());
			REQUIRE(anotherCapturesI != bindingInfo->_slots.end());

			// Check the data in the constants buffers we would bind
			// here, we're checking that the layout is what we expect, and that values (either from constantBindings or preset defaults)
			// actually got through

			{
				INFO(materialUniformsI->_binding);
				REQUIRE(materialUniformsI->_bindType == DescriptorSetInitializer::BindType::ResourceView);
				REQUIRE(materialUniformsI->_layoutSlotType == DescriptorType::UniformBuffer);
				RenderCore::Assets::PredefinedCBLayout parsedBinding(materialUniformsI->_binding, {}, {});

				auto diffuseColorI = std::find_if(parsedBinding._elements.begin(), parsedBinding._elements.end(), [](const auto& c) { return c._name == "DiffuseColor"; });
				REQUIRE(diffuseColorI != parsedBinding._elements.end());
				REQUIRE(diffuseColorI->_type == ImpliedTyping::TypeOf<Float3>());
				REQUIRE(Equivalent(parsedBinding._defaults.GetParameter<Float3>("DiffuseColor").value(), Float3{1.0f, 0.5f, 0.2f}, 1e-3f));

				auto someFloatI = std::find_if(parsedBinding._elements.begin(), parsedBinding._elements.end(), [](const auto& c) { return c._name == "SomeFloat"; });
				REQUIRE(someFloatI != parsedBinding._elements.end());
				REQUIRE(someFloatI->_type == ImpliedTyping::TypeOf<float>());
				REQUIRE(parsedBinding._defaults.GetParameter<float>("SomeFloat").value() == Catch::Approx(0.25));
			}

			{
				INFO(anotherCapturesI->_binding);
				REQUIRE(anotherCapturesI->_bindType == DescriptorSetInitializer::BindType::ResourceView);
				REQUIRE(anotherCapturesI->_layoutSlotType == DescriptorType::UniformBuffer);
				RenderCore::Assets::PredefinedCBLayout parsedBinding(anotherCapturesI->_binding, {}, {});

				auto test2I = std::find_if(parsedBinding._elements.begin(), parsedBinding._elements.end(), [](const auto& c) { return c._name == "Test2"; });
				REQUIRE(test2I != parsedBinding._elements.end());
				REQUIRE(test2I->_type == ImpliedTyping::TypeOf<Float4>());
				REQUIRE(Equivalent(parsedBinding._defaults.GetParameter<Float4>("Test2").value(), Float4{1.0f, 2.0f, 3.0f, 4.0f}, 1e-3f));

				auto Test0 = std::find_if(parsedBinding._elements.begin(), parsedBinding._elements.end(), [](const auto& c) { return c._name == "Test0"; });
				REQUIRE(Test0 != parsedBinding._elements.end());
				REQUIRE(Test0->_type == ImpliedTyping::TypeOf<Float2>());
				REQUIRE(Equivalent(parsedBinding._defaults.GetParameter<Float2>("Test0").value(), Float2{0.0f, 0.0f}, 1e-3f));

				auto SecondaryCaptures = std::find_if(parsedBinding._elements.begin(), parsedBinding._elements.end(), [](const auto& c) { return c._name == "SecondaryCaptures"; });
				REQUIRE(SecondaryCaptures != parsedBinding._elements.end());
				REQUIRE(SecondaryCaptures->_type == ImpliedTyping::TypeOf<float>());
				REQUIRE(parsedBinding._defaults.GetParameter<float>("SecondaryCaptures").value() == Catch::Approx(0.7));
			}
		}

		// try actually rendering (including background loading of textures)
		SECTION("RenderTexturedQuad")
		{
			auto threadContext = testHelper->_device->GetImmediateContext();
			auto targetDesc = CreateDesc(
				BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
				TextureDesc::Plain2D(64, 64, Format::R8G8B8A8_UNORM),
				"temporary-out");
			UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
			
			auto patches = GetPatchCollectionFromText(s_patchCollectionBasicTexturing);

			ParameterBox constantBindings;
			constantBindings.SetParameter("Multiplier", Float3{1.0f, 0.5f, 0.0f});

			ParameterBox resourceBindings;
			resourceBindings.SetParameter("BoundTexture", "xleres/DefaultResources/waternoise.png");

			auto descriptorSetAccelerator = pipelineAcceleratorPool->CreateDescriptorSetAccelerator(
				patches,
				ParameterBox {}, constantBindings, resourceBindings);
			auto descriptorSetFuture = pipelineAcceleratorPool->GetDescriptorSetFuture(*descriptorSetAccelerator);

			// Put together the pieces we need to create a pipeline
			auto techniqueSetFile = ::Assets::MakeAsset<Techniques::TechniqueSetFile>("ut-data/basic.tech");
			auto cfgId = pipelineAcceleratorPool->CreateSequencerConfig(
				"cfgId",
				Techniques::CreateTechniqueDelegate_Deferred(techniqueSetFile),
				ParameterBox {},
				fbHelper.GetDesc());

			RenderCore::Assets::RenderStateSet doubledSidedStateSet;
			doubledSidedStateSet._doubleSided = true;
			doubledSidedStateSet._flag |= RenderCore::Assets::RenderStateSet::Flag::DoubleSided;

			auto pipelineWithTexCoord = pipelineAcceleratorPool->CreatePipelineAccelerator(
				patches,
				ParameterBox {},
				GlobalInputLayouts::PCT,
				Topology::TriangleList,
				doubledSidedStateSet);

			auto vertexBuffer = testHelper->CreateVB(vertices_fullViewport);

			{
				auto finalPipeline = pipelineAcceleratorPool->GetPipelineFuture(*pipelineWithTexCoord, *cfgId);
				finalPipeline->StallWhilePending();
				RequireReady(*finalPipeline);

				StallForDescriptorSet(*threadContext, *descriptorSetFuture);
				RequireReady(*descriptorSetFuture);
				auto& descSet = descriptorSetFuture->Actualize();
				auto* bindingInfo = &descSet._bindingInfo;
				auto boundTextureI = std::find_if(bindingInfo->_slots.begin(), bindingInfo->_slots.end(), [](const auto& slot) { return slot._layoutName == "BoundTexture"; });
				REQUIRE(boundTextureI != bindingInfo->_slots.end());
				REQUIRE(boundTextureI->_layoutSlotType == DescriptorType::SampledTexture);
				REQUIRE(boundTextureI->_bindType == DescriptorSetInitializer::BindType::ResourceView);
				REQUIRE(!boundTextureI->_binding.empty());
				
				auto rpi = fbHelper.BeginRenderPass(*threadContext);
				RenderQuad(
					*testHelper, *threadContext, *vertexBuffer, (unsigned)dimof(vertices_fullViewport), 
					*finalPipeline->Actualize()._metalPipeline, 
					pipelineAcceleratorPool->GetCompiledPipelineLayoutFuture(*cfgId)->Actualize()->GetPipelineLayout(),
					descriptorSetFuture->Actualize()._descriptorSet.get());
			}

			auto breakdown = fbHelper.GetFullColorBreakdown(*threadContext);

			// If it's successful, we should get a lot of different color. And in each one, the blue channel will be zero
			// Because we're checking that there are a number of unique colors (and because the alpha values are fixed)
			// this can only succeed if the red and/or green channels have non-zero data for at least some pixels
			REQUIRE(breakdown.size() > 32);
			for (const auto&c:breakdown)
				REQUIRE((c.first & 0x00ff0000) == 0);
		}

		// check that depvals react to texture updates

		::Assets::MainFileSystem::GetMountingTree()->Unmount(utdatamnt);
		::Assets::MainFileSystem::GetMountingTree()->Unmount(xlresmnt);
	}

	TEST_CASE( "PipelineAcceleratorTests-IncorrectConfiguration", "[rendercore_techniques]" )
	{
		//
		// Create descriptor set via the the pipeline accelerator pool, but configure it incorrectly
		// in a number of ways.
		//

		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto utdatamnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));
		auto testHelper = MakeTestHelper();
		TechniqueTestApparatus techniqueTestHelper(*testHelper);
		auto pipelineAcceleratorPool = techniqueTestHelper._pipelineAccelerators;

		////////////////////////////////////////

		{
			auto threadContext = testHelper->_device->GetImmediateContext();
			auto targetDesc = CreateDesc(
				BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
				TextureDesc::Plain2D(64, 64, Format::R8G8B8A8_UNORM),
				"temporary-out");
			UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);

			auto techniqueSetFile = ::Assets::MakeAsset<Techniques::TechniqueSetFile>("ut-data/basic.tech");
			auto cfgId = pipelineAcceleratorPool->CreateSequencerConfig(
				"cfgId",
				Techniques::CreateTechniqueDelegate_Deferred(techniqueSetFile),
				ParameterBox {},
				fbHelper.GetDesc());

			auto patches = GetPatchCollectionFromText(s_patchCollectionBasicTexturing);
			RenderCore::Assets::RenderStateSet doubledSidedStateSet;
			doubledSidedStateSet._doubleSided = true;
			doubledSidedStateSet._flag |= RenderCore::Assets::RenderStateSet::Flag::DoubleSided;
			auto pipelineWithTexCoord = pipelineAcceleratorPool->CreatePipelineAccelerator(
				patches,
				ParameterBox {},
				GlobalInputLayouts::PCT,
				Topology::TriangleList,
				doubledSidedStateSet);
			auto finalPipeline = pipelineAcceleratorPool->GetPipelineFuture(*pipelineWithTexCoord, *cfgId);
			REQUIRE(finalPipeline);
			finalPipeline->StallWhilePending();
			RequireReady(*finalPipeline);

			auto vertexBuffer = testHelper->CreateVB(vertices_fullViewport);

			SECTION("Missing bindings")
			{
				// Nothing is bound -- we can still render, but in this case we'll just get
				// black output
				auto descriptorSetAccelerator = pipelineAcceleratorPool->CreateDescriptorSetAccelerator(
					patches,
					ParameterBox {}, ParameterBox {}, ParameterBox {});
				auto descriptorSetFuture = pipelineAcceleratorPool->GetDescriptorSetFuture(*descriptorSetAccelerator);
				StallForDescriptorSet(*threadContext, *descriptorSetFuture);
				RequireReady(*descriptorSetFuture);
				auto& descSet = descriptorSetFuture->Actualize();
				auto* bindingInfo = &descSet._bindingInfo;
				REQUIRE(bindingInfo->_slots.size() > 0);
				
				{
					auto rpi = fbHelper.BeginRenderPass(*threadContext);
					RenderQuad(
						*testHelper, *threadContext, *vertexBuffer, (unsigned)dimof(vertices_fullViewport), 
						*finalPipeline->Actualize()._metalPipeline,
						pipelineAcceleratorPool->GetCompiledPipelineLayoutFuture(*cfgId)->Actualize()->GetPipelineLayout(),
						descriptorSetFuture->Actualize()._descriptorSet.get());
				}

				auto breakdown = fbHelper.GetFullColorBreakdown(*threadContext);
				REQUIRE(breakdown.size() == 1);
				REQUIRE(breakdown.find(0xff000000) != breakdown.end());
			}

			SECTION("Bind missing texture")
			{
				ParameterBox resourceBindings;
				resourceBindings.SetParameter("BoundTexture", "xleres/texture_does_not_exist.png");
				resourceBindings.SetParameter("ExtraneousTexture", "xleres/DefaultResources/waternoise.png");
				auto descriptorSetAccelerator = pipelineAcceleratorPool->CreateDescriptorSetAccelerator(
					patches,
					ParameterBox {}, ParameterBox {}, resourceBindings);
				auto descriptorSetFuture = pipelineAcceleratorPool->GetDescriptorSetFuture(*descriptorSetAccelerator);
				StallForDescriptorSet(*threadContext, *descriptorSetFuture);
				REQUIRE_THROWS(descriptorSetFuture->Actualize());		// if any texture is bad, the entire descriptor set is considered bad
			}

			SECTION("Bind invalid texture")
			{
				// we'll try the load the following text file as a texture; it should just give us an invalid descriptor set
				ParameterBox resourceBindings;
				resourceBindings.SetParameter("BoundTexture", "xleres/TechniqueLibrary/Config/Illum.tech");
				auto descriptorSetAccelerator = pipelineAcceleratorPool->CreateDescriptorSetAccelerator(
					patches,
					ParameterBox {}, ParameterBox {}, resourceBindings);
				auto descriptorSetFuture = pipelineAcceleratorPool->GetDescriptorSetFuture(*descriptorSetAccelerator);
				StallForDescriptorSet(*threadContext, *descriptorSetFuture);
				REQUIRE_THROWS(descriptorSetFuture->Actualize());		// if any texture is bad, the entire descriptor set is considered bad
			}

			SECTION("Bind bad uniform inputs")
			{
				// Pass in invalid inputs for shader constants. They will get casted and converted as much as possible,
				// and we'll still get a valid descriptor set at the end
				ParameterBox constantBindings;
				constantBindings.SetParameter("Multiplier", "{50, 23, 100}");
				constantBindings.SetParameter("Adder", -40);	// too few elements
				auto descriptorSetAccelerator = pipelineAcceleratorPool->CreateDescriptorSetAccelerator(
					patches,
					ParameterBox {}, constantBindings, ParameterBox {});
				auto descriptorSetFuture = pipelineAcceleratorPool->GetDescriptorSetFuture(*descriptorSetAccelerator);
				REQUIRE(descriptorSetFuture);
				StallForDescriptorSet(*threadContext, *descriptorSetFuture);
				RequireReady(*descriptorSetFuture);
				auto& descSet = descriptorSetFuture->Actualize();
				auto* bindingInfo = &descSet._bindingInfo;
				REQUIRE(bindingInfo->_slots.size() > 0);

				// If we try to create another accelerator with the same settings, we'll get the same
				// one returned
				auto secondDescriptorSetAccelerator = pipelineAcceleratorPool->CreateDescriptorSetAccelerator(
					patches,
					ParameterBox {}, constantBindings, ParameterBox {});
				REQUIRE(descriptorSetAccelerator == secondDescriptorSetAccelerator);
			}
			
			SECTION("Bind wrong type")
			{
				ParameterBox constantBindings;
				constantBindings.SetParameter("BoundTexture", Float3{1,1,1});
				constantBindings.SetParameter("BoundSampler", 3);
				ParameterBox resourceBindings;
				resourceBindings.SetParameter("MaterialUniforms", "xleres/DefaultResources/waternoise.png");
				resourceBindings.SetParameter("Adder", "xleres/DefaultResources/waternoise.png");
				auto descriptorSetAccelerator = pipelineAcceleratorPool->CreateDescriptorSetAccelerator(
					patches,
					ParameterBox {}, constantBindings, resourceBindings);
				auto descriptorSetFuture = pipelineAcceleratorPool->GetDescriptorSetFuture(*descriptorSetAccelerator);
				StallForDescriptorSet(*threadContext, *descriptorSetFuture);
				REQUIRE_THROWS(descriptorSetFuture->Actualize());

				// do the same, but messing up sampler configurations
				std::vector<std::pair<uint64_t, SamplerDesc>> samplerBindings {
					std::make_pair(Hash64("BoundTexture"), SamplerDesc{ FilterMode::Point }),
					std::make_pair(Hash64("MaterialUniforms"), SamplerDesc{ FilterMode::Bilinear }),
					std::make_pair(Hash64("Multiplier"), SamplerDesc{ FilterMode::Trilinear })
				};
				resourceBindings = {};
				resourceBindings.SetParameter("BoundSampler", "xleres/DefaultResources/waternoise.png");
				descriptorSetAccelerator = pipelineAcceleratorPool->CreateDescriptorSetAccelerator(
					patches,
					ParameterBox {}, ParameterBox {}, resourceBindings, samplerBindings);
				descriptorSetFuture = pipelineAcceleratorPool->GetDescriptorSetFuture(*descriptorSetAccelerator);
				StallForDescriptorSet(*threadContext, *descriptorSetFuture);
				REQUIRE_THROWS(descriptorSetFuture->Actualize());
			}
		}

		////////////////////////////////////////

		::Assets::MainFileSystem::GetMountingTree()->Unmount(utdatamnt);
		::Assets::MainFileSystem::GetMountingTree()->Unmount(xlresmnt);
	}

	static void BindPassThroughTransform(
		RenderCore::Metal::DeviceContext& metalContext,
		RenderCore::Metal::GraphicsEncoder& encoder,
		const RenderCore::Metal::GraphicsPipeline& pipeline,
		const RenderCore::IDescriptorSet* descSet = nullptr)
	{
		// Bind the 2 main transform packets ("GlobalTransformConstants" and "LocalTransformConstants")
		// with identity transforms for local to clip
		using namespace RenderCore;
		UniformsStreamInterface usi;
		usi.BindImmediateData(0, Hash64("GlobalTransform"));
		usi.BindImmediateData(1, Hash64("LocalTransform"));
		if (descSet)
			usi.BindFixedDescriptorSet(0, Hash64("Material"));

		Techniques::GlobalTransformConstants globalTransform;
		XlZeroMemory(globalTransform);
		globalTransform._worldToClip = Identity<Float4x4>();
		auto localTransform = Techniques::MakeLocalTransform(Identity<Float4x4>(), Float3{0.f, 0.f, 0.f});

		Metal::BoundUniforms boundUniforms(pipeline, usi);

		UniformsStream::ImmediateData cbvs[] = { MakeOpaqueIteratorRange(globalTransform), MakeOpaqueIteratorRange(localTransform) };
		UniformsStream us;
		us._immediateData = MakeIteratorRange(cbvs);
		boundUniforms.ApplyLooseUniforms(metalContext, encoder, us);

		if (descSet) {
			const RenderCore::IDescriptorSet* ds[] = { descSet };
			boundUniforms.ApplyDescriptorSets(metalContext, encoder, MakeIteratorRange(ds));
		}
	}

	static void RenderQuad(
		MetalTestHelper& testHelper,
		RenderCore::IThreadContext& threadContext,
		const RenderCore::IResource& vb, unsigned vertexCount,
		const RenderCore::Metal::GraphicsPipeline& pipeline,
		const std::shared_ptr<RenderCore::ICompiledPipelineLayout>& pipelineLayout,
		const RenderCore::IDescriptorSet* descSet)
	{
		auto& metalContext = *RenderCore::Metal::DeviceContext::Get(threadContext);

		auto encoder = metalContext.BeginGraphicsEncoder(pipelineLayout);
		RenderCore::VertexBufferView vbv { &vb };
		encoder.Bind(MakeIteratorRange(&vbv, &vbv+1), RenderCore::IndexBufferView{});
		BindPassThroughTransform(metalContext, encoder, pipeline, descSet);
		encoder.Draw(pipeline, vertexCount);
	}
}

