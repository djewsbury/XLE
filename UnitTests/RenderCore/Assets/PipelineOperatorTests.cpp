// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueTestsHelper.h"
#include "../ReusableDataFiles.h"
#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Techniques/PipelineCollection.h"
#include "../../../RenderCore/Techniques/PipelineOperators.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Techniques/CommonBindings.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../../RenderCore/Techniques/SystemUniformsDelegate.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../xleres/FileList.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <unordered_map>

using namespace Catch::literals;
namespace UnitTests
{
	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair(
			"operator-test.pixel.hlsl",
			::Assets::AsBlob(R"--(

				#include "xleres/TechniqueLibrary/Framework/SystemUniforms.hlsl"
				
				[[vk::input_attachment_index(0)]] SubpassInput<float4> SubpassInputAttachment;
				float4 copy_inputattachment(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
				{
					return SubpassInputAttachment.SubpassLoad();
				}

				float4 prime_attachment(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
				{
					return float4(
						position.xy * SysUniform_ReciprocalViewportDimensions().xy,
						0, 1);
				}

			)--"))
	};

	TEST_CASE( "ShaderOperators-InputAttachmentOperator", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto mnt1 = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::EnableChangeMonitoring));

		auto testHelper = MakeTestHelper();
		TechniqueTestApparatus testApparatus(*testHelper);	
		testApparatus._techniqueContext->_attachmentPool = std::make_shared<Techniques::AttachmentPool>(testHelper->_device);
		testApparatus._techniqueContext->_frameBufferPool = Techniques::CreateFrameBufferPool();

		auto threadContext = testHelper->_device->GetImmediateContext();

		// Define our attachments, and create a frame buffer desc fragment
		// then stitch it together
		Techniques::PreregisteredAttachment predefAttachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::ColorHDR,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::InputAttachment,
					TextureDesc::Plain2D(256, 256, Format::R8G8B8A8_UNORM),
					"color-hdr")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::ColorLDR,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::TransferSrc,
					TextureDesc::Plain2D(256, 256, Format::R8G8B8A8_UNORM),
					"color-ldr")
			}
		};

		Techniques::FrameBufferDescFragment frag;
		auto colorHdr = frag.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).NoInitialState().Discard();
		auto colorLdr = frag.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR).NoInitialState().FinalState(BindFlag::TransferSrc);
		frag.AddSubpass(std::move(SubpassDesc{}.AppendOutput(colorHdr).SetName("prime-color-hdr")));
		frag.AddSubpass(std::move(SubpassDesc{}.AppendOutput(colorLdr).AppendInput(colorHdr).SetName("copy-to-color-ldr")));

		Techniques::FragmentStitchingContext stitchingContext{ MakeIteratorRange(predefAttachments), FrameBufferProperties{256, 256}};
		auto stitch = stitchingContext.TryStitchFrameBufferDesc(frag);

		// Create the pipeline operators we're going to use
		// Both are full viewport operators, and we just need to specify the shaders & states they will use
		auto pipelineCollection = std::make_shared<Techniques::PipelineCollection>(testHelper->_device);
		std::shared_ptr<Techniques::IShaderOperator> operator0, operator1;

		{
			Techniques::PixelOutputStates outputStates;
			outputStates.Bind(stitch._fbDesc, 0);
			outputStates._attachmentBlendStates = {&Techniques::CommonResourceBox::s_abOpaque, &Techniques::CommonResourceBox::s_abOpaque+1};
			UniformsStreamInterface usi;
			auto opMarker = Techniques::CreateFullViewportOperator(
				pipelineCollection,
				Techniques::FullViewportOperatorSubType::DisableDepth,
				"ut-data/operator-test.pixel.hlsl:prime_attachment",
				{},
				GENERAL_OPERATOR_PIPELINE ":GraphicsMain",
				outputStates,
				usi);
			opMarker->StallWhilePending();
			operator0 = opMarker->Actualize();
		}

		{
			Techniques::PixelOutputStates outputStates;
			outputStates.Bind(stitch._fbDesc, 1);
			outputStates._attachmentBlendStates = {&Techniques::CommonResourceBox::s_abOpaque, &Techniques::CommonResourceBox::s_abOpaque+1};
			UniformsStreamInterface usi;
			usi.BindResourceView(0, Hash64("SubpassInputAttachment"));
			auto opMarker = Techniques::CreateFullViewportOperator(
				pipelineCollection,
				Techniques::FullViewportOperatorSubType::DisableDepth,
				"ut-data/operator-test.pixel.hlsl:copy_inputattachment",
				{},
				GENERAL_OPERATOR_PIPELINE ":GraphicsMain",
				outputStates,
				usi);
			opMarker->StallWhilePending();
			operator1 = opMarker->Actualize();
		}

		testHelper->BeginFrameCapture();

		// Start a render pass and execute the operators we've created
		std::shared_ptr<IResource> outputResource;
		{
			auto parsingContext = BeginParsingContext(testApparatus, *threadContext);
			Techniques::RenderPassInstance rpi{parsingContext, stitch};
			parsingContext.GetUniformDelegateManager()->AddShaderResourceDelegate(std::make_shared<Techniques::SystemUniformsDelegate>(*testHelper->_device));
			parsingContext.GetUniformDelegateManager()->BringUpToDateGraphics(parsingContext);
			operator0->Draw(parsingContext, {});
			rpi.NextSubpass();
			operator1->Draw(parsingContext, ResourceViewStream{*rpi.GetInputAttachmentView(0)});
			outputResource = rpi.GetOutputAttachmentResource(0);
		}

		testHelper->EndFrameCapture();

		// Save out the result for debugging
		SaveImage(*threadContext, *outputResource, "input-attachment-operator");
	}
}
