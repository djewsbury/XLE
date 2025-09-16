// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineTestHelper.h"
#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../../RenderCore/Techniques/ShaderPatchInstantiationUtil.h"
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
#include "../../../RenderCore/Techniques/PipelineLayoutDelegate.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/AssetSetManager.h"
#include "../../../Assets/Assets.h"
#include "../../../Tools/ToolsRig/VisualisationGeo.h"
#include "../../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../../Math/Transformations.h"
#include "../../../Utility/Threading/CompletionThreadPool.h"
#include "../../../xleres/FileList.h"
#include <chrono>

using namespace Utility::Literals;

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
		_techniqueServices->RegisterTextureLoader("*.[dD][dD][sS])", RenderCore::Assets::CreateDDSTextureLoader());
		_techniqueServices->RegisterTextureLoader("*", RenderCore::Assets::CreateWICTextureLoader());
		_bufferUploads = BufferUploads::CreateManager({}, *_metalTestHelper->_device);
		_techniqueServices->SetBufferUploads(_bufferUploads);

		auto& compilers = ::Assets::Services::GetIntermediateCompilers();
		_compilerRegistrations.push_back(ShaderSourceParser::RegisterShaderSelectorFilteringCompiler(compilers));
		_compilerRegistrations.push_back(RenderCore::RegisterShaderCompiler(_metalTestHelper->_shaderSource, compilers, GetDefaultShaderCompilationFlags(*_metalTestHelper->_device)));
		_compilerRegistrations.push_back(RenderCore::Techniques::RegisterInstantiateShaderGraphCompiler(_metalTestHelper->_shaderSource, compilers));

		auto matDescSetLayout = MakeMaterialDescriptorSetLayout();
		auto pipelineLayoutDelegate = Techniques::CreatePipelineLayoutDelegate(matDescSetLayout);
		_drawablesPool = Techniques::CreateDrawablesPool();
		_pipelineCollection = std::make_shared<Techniques::PipelineCollection>(_metalTestHelper->_device);
		_pipelineAccelerators = Techniques::CreatePipelineAcceleratorPool(
			_metalTestHelper->_device, _drawablesPool, _pipelineCollection, pipelineLayoutDelegate, Techniques::PipelineAcceleratorPoolFlags::RecordDescriptorSetBindingInfo);

		_commonResources = std::make_shared<RenderCore::Techniques::CommonResourceBox>(*_metalTestHelper->_device);
		_sharedDelegates = std::make_shared<LightingEngine::SharedTechniqueDelegateBox>(*_metalTestHelper->_device, _metalTestHelper->_shaderCompiler->GetShaderLanguage(), &_commonResources->_samplerPool);
		_techniqueServices->SetCommonResources(_commonResources);

		_techniqueContext = std::make_shared<Techniques::TechniqueContext>();
		_techniqueContext->_commonResources = _commonResources;
		_techniqueContext->_attachmentPool = Techniques::CreateAttachmentPool(_metalTestHelper->_device);
		_techniqueContext->_frameBufferPool = Techniques::CreateFrameBufferPool();
		_techniqueContext->_drawablesPool = _drawablesPool;
		_techniqueContext->_graphicsPipelinePool = _pipelineCollection;
		_techniqueContext->_pipelineAccelerators = _pipelineAccelerators;
		_techniqueContext->_systemAttachmentFormats = Techniques::CalculateDefaultSystemFormats(*_metalTestHelper->_device);

		_techniqueContext->_graphicsSequencerDS = Techniques::CreateSemiConstantDescriptorSet(*MakeSequencerDescriptorSetLayout()->GetLayout(), "unittest", PipelineType::Graphics, *_metalTestHelper->_device);
		_techniqueContext->_computeSequencerDS = Techniques::CreateSemiConstantDescriptorSet(*MakeSequencerDescriptorSetLayout()->GetLayout(), "unittest", PipelineType::Compute, *_metalTestHelper->_device);
		_techniqueContext->_systemUniformsDelegate = std::make_shared<Techniques::SystemUniformsDelegate>(*_metalTestHelper->_device);

		_commonResources->CompleteInitialization(*_metalTestHelper->_device->GetImmediateContext());
	}

	LightingEngineTestApparatus::~LightingEngineTestApparatus()
	{
		// we have to clear the asset sets here, because we're starting to pull down manager
		// like the drawables pool
		if (auto assetSets = ::Assets::Services::GetAssetSetsPtr())
			assetSets->Clear();
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

	void ParseScene(RenderCore::LightingEngine::SequencePlayback& lightingIterator, ToolsRig::IDrawablesWriter& drawableWriter)
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
		RenderCore::IThreadContext& threadContext,
		const RenderCore::Techniques::SequencerConfig& sequencerConfig,
		const RenderCore::Techniques::DrawablesPacket& drawablePkt,
		RenderCore::BufferUploads::MarkCommandListDependencyFlags::BitField markDependencyFlags)
	{
		using namespace RenderCore;
		std::promise<Techniques::PreparedResourcesVisibility> preparePromise;
		auto prepareFuture = preparePromise.get_future();
		Techniques::PrepareResources(std::move(preparePromise), *testApparatus._pipelineAccelerators, sequencerConfig, drawablePkt);
		auto requiredVisibility = prepareFuture.get();		// stall
		testApparatus._pipelineAccelerators->VisibilityBarrier(requiredVisibility._pipelineAcceleratorsVisibility);		// must call this to flip completed pipelines, etc, to visible
		testApparatus._bufferUploads->StallAndMarkCommandListDependency(
			threadContext,
			requiredVisibility._bufferUploadsVisibility,
			markDependencyFlags);
		return requiredVisibility;
	}

	RenderCore::Techniques::PreparedResourcesVisibility PrepareAndStall(
		LightingEngineTestApparatus& testApparatus,
		RenderCore::IThreadContext& threadContext,
		std::future<RenderCore::Techniques::PreparedResourcesVisibility> visibility,
		RenderCore::BufferUploads::MarkCommandListDependencyFlags::BitField markDependencyFlags)
	{
		auto requiredVisibility = visibility.get();		// stall
		testApparatus._pipelineAccelerators->VisibilityBarrier(requiredVisibility._pipelineAcceleratorsVisibility);		// must call this to flip completed pipelines, etc, to visible
		testApparatus._bufferUploads->StallAndMarkCommandListDependency(
			threadContext,
			requiredVisibility._bufferUploadsVisibility,
			markDependencyFlags);
		return requiredVisibility;
	}

	static std::shared_ptr<RenderCore::Techniques::DescriptorSetLayoutAndBinding> MakeMaterialDescriptorSetLayout()
	{
		const char* unitTestsMaterialDescSet = R"(
			UniformBuffer BasicMaterialConstants : 0		// this CB layout used by "no patches" techniques for linking with material info
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

			SampledTexture t1 : 1;
			SampledTexture t2 : 2;
			SampledTexture t3 : 3;
			SampledTexture t4 : 4;
			SampledTexture t5 : 5;

			Sampler sampler0 :  6;
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
				"color-ldr",
				Techniques::PreregisteredAttachment::State::Uninitialized
			}
		};
		FrameBufferProperties fbProps { targetDesc._textureDesc._width, targetDesc._textureDesc._height };

		auto parsingContext = BeginParsingContext(testApparatus, threadContext);
		parsingContext.GetProjectionDesc() = BuildProjectionDesc(camera, targetDesc._textureDesc._width / float(targetDesc._textureDesc._height));
		parsingContext.GetFrameBufferProperties() = fbProps;
		
		auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
		for (const auto&a:preregisteredAttachments)
			stitchingContext.DefineAttachment(a._semantic, a._desc, a._name, a._state, a._layout);
		return parsingContext;
	}

}

