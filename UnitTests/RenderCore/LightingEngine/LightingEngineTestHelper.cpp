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
#include "../../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../../RenderCore/Techniques/PipelineOperators.h"
#include "../../../RenderCore/Techniques/CompiledLayoutPool.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/AssetSetManager.h"
#include "../../../Assets/CompileAndAsyncManager.h"
#include "../../../Assets/Assets.h"
#include "../../../Tools/ToolsRig/VisualisationGeo.h"
#include "../../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../../Math/Transformations.h"
#include "../../../Utility/Threading/CompletionThreadPool.h"
#include "../../../xleres/FileList.h"
#include <regex>
#include <chrono>

namespace UnitTests
{
	static std::shared_ptr<RenderCore::Techniques::DescriptorSetLayoutAndBinding> MakeMaterialDescriptorSetLayout();
	static std::shared_ptr<RenderCore::Techniques::DescriptorSetLayoutAndBinding> MakeSequencerDescriptorSetLayout();

	LightingEngineTestApparatus::LightingEngineTestApparatus()
	{
		using namespace RenderCore;
		_globalServices = std::make_shared<ConsoleRig::GlobalServices>(GetStartupConfig());
		_xleresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		_metalTestHelper = MakeTestHelper();

		_techniqueServices = std::make_shared<Techniques::Services>(_metalTestHelper->_device);
		_techniqueServices->RegisterTextureLoader(std::regex(R"(.*\.[dD][dD][sS])"), RenderCore::Assets::CreateDDSTextureLoader());
		_techniqueServices->RegisterTextureLoader(std::regex(R"(.*)"), RenderCore::Assets::CreateWICTextureLoader());
		_bufferUploads = BufferUploads::CreateManager(*_metalTestHelper->_device);
		_techniqueServices->SetBufferUploads(_bufferUploads);

		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		_compilerRegistrations.push_back(ShaderSourceParser::RegisterShaderSelectorFilteringCompiler(compilers));
		_compilerRegistrations.push_back(RenderCore::RegisterShaderCompiler(_metalTestHelper->_shaderSource, compilers));
		_compilerRegistrations.push_back(RenderCore::Techniques::RegisterInstantiateShaderGraphCompiler(_metalTestHelper->_shaderSource, compilers));

		auto matDescSetLayout = MakeMaterialDescriptorSetLayout();
		auto compiledLayoutPool = CreateCompiledLayoutPool(_metalTestHelper->_device, matDescSetLayout);
		_drawablesPool = Techniques::CreateDrawablesPool();
		_pipelineAccelerators = Techniques::CreatePipelineAcceleratorPool(
			_metalTestHelper->_device, _drawablesPool, compiledLayoutPool, Techniques::PipelineAcceleratorPoolFlags::RecordDescriptorSetBindingInfo);

		_sharedDelegates = std::make_shared<LightingEngine::SharedTechniqueDelegateBox>();
		_commonResources = std::make_shared<RenderCore::Techniques::CommonResourceBox>(*_metalTestHelper->_device);
		_techniqueServices->SetCommonResources(_commonResources);

		_pipelinePool = std::make_shared<Techniques::PipelineCollection>(_metalTestHelper->_device);

		_techniqueContext = std::make_shared<Techniques::TechniqueContext>();
		_techniqueContext->_commonResources = _commonResources;
		_techniqueContext->_attachmentPool = std::make_shared<Techniques::AttachmentPool>(_metalTestHelper->_device);
		_techniqueContext->_frameBufferPool = Techniques::CreateFrameBufferPool();
		_techniqueContext->_drawablesPool = _drawablesPool;
		_techniqueContext->_graphicsPipelinePool = _pipelinePool;
		_techniqueContext->_pipelineAccelerators = _pipelineAccelerators;
		_techniqueContext->_systemAttachmentFormats = Techniques::CalculateDefaultSystemFormats(*_metalTestHelper->_device);

		_techniqueContext->_uniformDelegateManager = RenderCore::Techniques::CreateUniformDelegateManager();
		_techniqueContext->_uniformDelegateManager->AddSemiConstantDescriptorSet(Hash64("Sequencer"), *MakeSequencerDescriptorSetLayout()->GetLayout(), *_metalTestHelper->_device);
		_techniqueContext->_uniformDelegateManager->AddShaderResourceDelegate(std::make_shared<Techniques::SystemUniformsDelegate>(*_metalTestHelper->_device));
	}

	LightingEngineTestApparatus::~LightingEngineTestApparatus()
	{
		// we have to clear the asset sets here, because we're starting to pull down manager
		// like the drawables pool
		if (::Assets::Services::HasAssetSets())
			::Assets::Services::GetAssetSets().Clear();
	}


	LightingOperatorsPipelineLayout::LightingOperatorsPipelineLayout(const MetalTestHelper& testHelper)
	{	
		_samplerPool = std::make_shared<RenderCore::SamplerPool>(*testHelper._device);
		_pipelineLayoutFile = ::Assets::ActualizeAssetPtr<RenderCore::Assets::PredefinedPipelineLayoutFile>(LIGHTING_OPERATOR_PIPELINE);

		const std::string pipelineLayoutName = "LightingOperator";
		auto pipelineInit = RenderCore::Assets::PredefinedPipelineLayout{*_pipelineLayoutFile, pipelineLayoutName}.MakePipelineLayoutInitializer(
			testHelper._shaderCompiler->GetShaderLanguage(), _samplerPool.get());
		_pipelineLayout = testHelper._device->CreatePipelineLayout(pipelineInit);

		auto i = _pipelineLayoutFile->_descriptorSets.find("DMShadow");
		if (i == _pipelineLayoutFile->_descriptorSets.end())
			Throw(std::runtime_error("Missing ShadowTemplate entry in pipeline layout file"));
		_dmShadowDescSetTemplate = i->second;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static void BlackOutSky(RenderCore::Techniques::ParsingContext& parsingContext)
	{
		using namespace RenderCore;
		UniformsStreamInterface usi;

		parsingContext.GetUniformDelegateManager()->BringUpToDateGraphics(parsingContext);

		Techniques::PixelOutputStates po;
		assert(parsingContext._rpi);
		po.Bind(*parsingContext._rpi);
		po.Bind(Techniques::CommonResourceBox::s_dsReadOnly);
		AttachmentBlendDesc blendDescs[] {Techniques::CommonResourceBox::s_abOpaque};
		po.Bind(MakeIteratorRange(blendDescs));
		auto futureShader = CreateFullViewportOperator(
			parsingContext.GetTechniqueContext()._graphicsPipelinePool,
			Techniques::FullViewportOperatorSubType::MaxDepth,
			BASIC_PIXEL_HLSL ":blackOpaque",
			ParameterBox{},
			GENERAL_OPERATOR_PIPELINE ":GraphicsMain",
			po, usi);
		futureShader->StallWhilePending();
		futureShader->Actualize()->Draw(parsingContext, {});
	}

	void ParseScene(RenderCore::LightingEngine::LightingTechniqueInstance& lightingIterator, ToolsRig::IDrawablesWriter& drawableWriter)
	{
		using namespace RenderCore;
		for (;;) {
			auto next = lightingIterator.GetNextStep();
			if (next._type == LightingEngine::StepType::None || next._type == LightingEngine::StepType::Abort) break;
			if (next._type == LightingEngine::StepType::ReadyInstances) continue;
			if (next._type == LightingEngine::StepType::DrawSky) {
				// we need to draw something, otherwise we'll just end up garbage in the areas of the image outside of the gbuffer
				if (next._parsingContext) BlackOutSky(*next._parsingContext);
				continue;
			}
			if (next._type == LightingEngine::StepType::ParseScene) {
				assert(!next._pkts.empty() && next._pkts[0]);
				drawableWriter.WriteDrawables(*next._pkts[0]);
			} else {
				assert(next._type == LightingEngine::StepType::MultiViewParseScene);
				uint32_t viewMask = (1u << next._multiViewDesc.size()) - 1u;
				assert(!next._pkts.empty() && next._pkts[0]);
				drawableWriter.WriteDrawables(*next._pkts[0], viewMask);
			}
		}
	}

	RenderCore::Techniques::PreparedResourcesVisibility PrepareAndStall(
		LightingEngineTestApparatus& testApparatus,
		const RenderCore::Techniques::SequencerConfig& sequencerConfig,
		const RenderCore::Techniques::DrawablesPacket& drawablePkt)
	{
		using namespace RenderCore;
		std::promise<Techniques::PreparedResourcesVisibility> preparePromise;
		auto prepareFuture = preparePromise.get_future();
		Techniques::PrepareResources(std::move(preparePromise), *testApparatus._pipelineAccelerators, sequencerConfig, drawablePkt);
		auto requiredVisibility = prepareFuture.get();		// stall
		testApparatus._pipelineAccelerators->VisibilityBarrier(requiredVisibility._pipelineAcceleratorsVisibility);		// must call this to flip completed pipelines, etc, to visible
		testApparatus._bufferUploads->StallUntilCompletion(
			*testApparatus._pipelineAccelerators->GetDevice()->GetImmediateContext(),
			requiredVisibility._bufferUploadsVisibility);
		return requiredVisibility;
	}

	RenderCore::Techniques::PreparedResourcesVisibility PrepareAndStall(
		LightingEngineTestApparatus& testApparatus,
		std::future<RenderCore::Techniques::PreparedResourcesVisibility> visibility)
	{
		auto requiredVisibility = visibility.get();		// stall
		testApparatus._pipelineAccelerators->VisibilityBarrier(requiredVisibility._pipelineAcceleratorsVisibility);		// must call this to flip completed pipelines, etc, to visible
		testApparatus._bufferUploads->StallUntilCompletion(
			*testApparatus._pipelineAccelerators->GetDevice()->GetImmediateContext(),
			requiredVisibility._bufferUploadsVisibility);
		return requiredVisibility;
	}

	static std::shared_ptr<RenderCore::Techniques::DescriptorSetLayoutAndBinding> MakeMaterialDescriptorSetLayout()
	{
		const char* unitTestsMaterialDescSet = R"(
			UniformBuffer BasicMaterialConstants		// this CB layout used by "no patches" techniques for linking with material info
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
		return std::make_shared<RenderCore::Techniques::DescriptorSetLayoutAndBinding>(layout, RenderCore::Techniques::s_defaultMaterialDescSetSlot, "Material", RenderCore::PipelineType::Graphics, ::Assets::DependencyValidation{});
	}

	static std::shared_ptr<RenderCore::Techniques::DescriptorSetLayoutAndBinding> MakeSequencerDescriptorSetLayout()
	{
		const char* unitTestsSequencerDescSet = R"(
			UniformBuffer GlobalTransform;				// 0
			UniformBuffer MultiProbeProperties;			// 1
			UniformBuffer b2;							// 2
			UniformBuffer BasicLightingEnvironment;		// 3
			UniformBuffer MultiViewProperties;			// 4
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
		return std::make_shared<RenderCore::Techniques::DescriptorSetLayoutAndBinding>(layout, 0, "Sequencer", RenderCore::PipelineType::Graphics, ::Assets::DependencyValidation{});
	}

	RenderCore::Techniques::ParsingContext BeginParsingContext(LightingEngineTestApparatus& testApparatus, RenderCore::IThreadContext& threadContext)
	{
		RenderCore::Techniques::ParsingContext parsingContext{*testApparatus._techniqueContext, threadContext};
		parsingContext.SetPipelineAcceleratorsVisibility(testApparatus._pipelineAccelerators->VisibilityBarrier());
		return parsingContext;
	}

	RenderCore::Techniques::ParsingContext BeginParsingContext(
		LightingEngineTestApparatus& testApparatus,
		RenderCore::IThreadContext& threadContext,
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

		auto parsingContext = BeginParsingContext(testApparatus, threadContext);
		parsingContext.GetProjectionDesc() = BuildProjectionDesc(camera, UInt2{targetDesc._textureDesc._width, targetDesc._textureDesc._height});
		
		auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
		stitchingContext._workingProps = fbProps;
		for (const auto&a:preregisteredAttachments)
			stitchingContext.DefineAttachment(a._semantic, a._desc, a._state, a._layoutFlags);
		return parsingContext;
	}

}

