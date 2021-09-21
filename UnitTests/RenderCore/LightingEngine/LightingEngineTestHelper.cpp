// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineTestHelper.h"
#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../../RenderCore/Techniques/CompiledShaderPatchCollection.h"
#include "../../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Techniques/SystemUniformsDelegate.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/PipelineCollection.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/AssetSetManager.h"
#include "../../../Assets/CompileAndAsyncManager.h"
#include "../../../Tools/ToolsRig/VisualisationGeo.h"
#include "../../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../../Math/Transformations.h"
#include "../../../Utility/Threading/CompletionThreadPool.h"
#include <regex>
#include <chrono>

namespace UnitTests
{
	static RenderCore::Techniques::DescriptorSetLayoutAndBinding MakeMaterialDescriptorSetLayout();
	static RenderCore::Techniques::DescriptorSetLayoutAndBinding MakeSequencerDescriptorSetLayout();

	LightingEngineTestApparatus::LightingEngineTestApparatus()
	{
		using namespace RenderCore;
		_globalServices = std::make_shared<ConsoleRig::GlobalServices>(GetStartupConfig());
		_xleresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		_metalTestHelper = MakeTestHelper();

		// Verbose.SetConfiguration(OSServices::MessageTargetConfiguration{});

		_techniqueServices = std::make_shared<Techniques::Services>(_metalTestHelper->_device);
		_techniqueServices->RegisterTextureLoader(std::regex(R"(.*\.[dD][dD][sS])"), RenderCore::Assets::CreateDDSTextureLoader());
		_techniqueServices->RegisterTextureLoader(std::regex(R"(.*)"), RenderCore::Assets::CreateWICTextureLoader());
		_bufferUploads = BufferUploads::CreateManager(*_metalTestHelper->_device);
		_techniqueServices->SetBufferUploads(_bufferUploads);

		_futureExecutor = std::make_shared<thousandeyes::futures::DefaultExecutor>(std::chrono::milliseconds(2));
		_futureExecSetter = std::make_unique<thousandeyes::futures::Default<thousandeyes::futures::Executor>::Setter>(_futureExecutor);

		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		_compilerRegistrations.push_back(ShaderSourceParser::RegisterShaderSelectorFilteringCompiler(compilers));
		_compilerRegistrations.push_back(RenderCore::RegisterShaderCompiler(_metalTestHelper->_shaderSource, compilers));
		_compilerRegistrations.push_back(RenderCore::Techniques::RegisterInstantiateShaderGraphCompiler(_metalTestHelper->_shaderSource, compilers));

		_pipelineAcceleratorPool = Techniques::CreatePipelineAcceleratorPool(
			_metalTestHelper->_device, MakeMaterialDescriptorSetLayout(), Techniques::PipelineAcceleratorPoolFlags::RecordDescriptorSetBindingInfo);

		_sharedDelegates = std::make_shared<LightingEngine::SharedTechniqueDelegateBox>();
		_commonResources = std::make_shared<RenderCore::Techniques::CommonResourceBox>(*_metalTestHelper->_device);
		_techniqueServices->SetCommonResources(_commonResources);

		_pipelinePool = std::make_shared<Techniques::PipelinePool>(_metalTestHelper->_device);

		_techniqueContext = std::make_shared<Techniques::TechniqueContext>();
		_techniqueContext->_commonResources = _commonResources;
		_techniqueContext->_systemUniformsDelegate = std::make_shared<Techniques::SystemUniformsDelegate>(*_metalTestHelper->_device);
		_techniqueContext->_sequencerDescSetLayout = MakeSequencerDescriptorSetLayout().GetLayout();
		_techniqueContext->_attachmentPool = std::make_shared<Techniques::AttachmentPool>(_metalTestHelper->_device);
		_techniqueContext->_frameBufferPool = Techniques::CreateFrameBufferPool();
		_techniqueContext->_drawablesPacketsPool = std::make_shared<RenderCore::Techniques::DrawablesPacketPool>();
	}

	LightingEngineTestApparatus::~LightingEngineTestApparatus()
	{
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void ParseScene(RenderCore::LightingEngine::LightingTechniqueInstance& lightingIterator, ToolsRig::IDrawablesWriter& drawableWriter)
	{
		using namespace RenderCore;
		for (;;) {
			auto next = lightingIterator.GetNextStep();
			if (next._type == LightingEngine::StepType::None || next._type == LightingEngine::StepType::Abort) break;
			if (next._type == LightingEngine::StepType::DrawSky) continue;
			assert(next._type == LightingEngine::StepType::ParseScene);
			assert(next._pkt);
			drawableWriter.WriteDrawables(*next._pkt);
		}
	}

	static RenderCore::Techniques::DescriptorSetLayoutAndBinding MakeMaterialDescriptorSetLayout()
	{
		const char* unitTestsMaterialDescSet = R"(
			UniformBuffer BasicMaterialConstants
			{
				float3  MaterialDiffuse = {1,1,1};
				float   Opacity = 1;
				float3  MaterialSpecular = {1,1,1};
				float   AlphaThreshold = .5f;

				float   RoughnessMin = 0.1f;
				float   RoughnessMax = 0.6f;
				float   SpecularMin = 0.0f;
				float   SpecularMax = 0.5f;
				float   MetalMin = 0.f;
				float   MetalMax = 1.f;
			};
			UniformBuffer b1;						// 1
			UniformBuffer b2;						// 2

			SampledTexture t3;						// 3
			SampledTexture t4;						// 4
			SampledTexture t5;						// 5
			SampledTexture t6;						// 6
			SampledTexture t7;						// 7
			SampledTexture t8;						// 8
			SampledTexture t9;						// 9
			SampledTexture t10;						// 10

			UnorderedAccessBuffer uab0;				// 11
			Sampler sampler0;						// 12
		)";

		auto layout = std::make_shared<RenderCore::Assets::PredefinedDescriptorSetLayout>(
			unitTestsMaterialDescSet, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{}
		);
		return RenderCore::Techniques::DescriptorSetLayoutAndBinding { layout, 1 };
	}

	static RenderCore::Techniques::DescriptorSetLayoutAndBinding MakeSequencerDescriptorSetLayout()
	{
		const char* unitTestsSequencerDescSet = R"(
			UniformBuffer GlobalTransform;				// 0
			UniformBuffer MultiProbeProperties;			// 1
			UniformBuffer b2;							// 2
			UniformBuffer BasicLightingEnvironment;		// 3
			UniformBuffer b4;							// 4
			UniformBuffer ShadowProjection;				// 5

			SampledTexture tex0;						// 6
			SampledTexture tex1;						// 7
			SampledTexture tex2;						// 8
			SampledTexture tex3;						// 9
			SampledTexture NormalsFittingTexture;		// 10

			Sampler DefaultSampler						// 11
			{
				Filter = Trilinear,
				AddressU = Wrap,
				AddressV = Wrap
			};
			Sampler ClampingSampler						// 12
			{
				Filter = Trilinear,
				AddressU = Clamp,
				AddressV = Clamp
			};
			Sampler AnisotropicSampler					// 13
			{
				Filter = Anisotropic,
				AddressU = Wrap,
				AddressV = Wrap
			};
			Sampler PointClampSampler					// 14
			{
				Filter = Point,
				AddressU = Clamp,
				AddressV = Clamp
			};
		)";

		auto layout = std::make_shared<RenderCore::Assets::PredefinedDescriptorSetLayout>(
			unitTestsSequencerDescSet, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{}
		);
		return RenderCore::Techniques::DescriptorSetLayoutAndBinding { layout, 0 };
	}

	RenderCore::Techniques::ParsingContext InitializeParsingContext(
		RenderCore::Techniques::TechniqueContext& techniqueContext,
		const RenderCore::ResourceDesc& targetDesc,
		const RenderCore::Techniques::CameraDesc& camera)
	{
		using namespace RenderCore;

		Techniques::PreregisteredAttachment preregisteredAttachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::ColorLDR,
				targetDesc,
				Techniques::PreregisteredAttachment::State::Uninitialized
			}
		};
		FrameBufferProperties fbProps { targetDesc._textureDesc._width, targetDesc._textureDesc._height };

		Techniques::ParsingContext parsingContext{techniqueContext};
		parsingContext.GetProjectionDesc() = BuildProjectionDesc(camera, UInt2{targetDesc._textureDesc._width, targetDesc._textureDesc._height});
		
		auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
		stitchingContext._workingProps = fbProps;
		for (const auto&a:preregisteredAttachments)
			stitchingContext.DefineAttachment(a._semantic, a._desc, a._state, a._layoutFlags);
		return parsingContext;
	}

}

