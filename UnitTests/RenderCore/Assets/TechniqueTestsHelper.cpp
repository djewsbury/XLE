// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueTestsHelper.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../../RenderCore/Techniques/Drawables.h"
#include "../../../RenderCore/Techniques/PipelineLayoutDelegate.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../Assets/AssetSetManager.h"
#include "../../../Assets/AssetServices.h"

using namespace Utility::Literals;

namespace UnitTests
{
	TechniqueTestApparatus::TechniqueTestApparatus(MetalTestHelper& testHelper)
	{
		using namespace RenderCore;

		_techniqueServices = std::make_shared<Techniques::Services>(testHelper._device);
		_bufferUploads = BufferUploads::CreateManager(*testHelper._device);
		_techniqueServices->SetBufferUploads(_bufferUploads);
		_commonResources = std::make_shared<Techniques::CommonResourceBox>(*testHelper._device);
		_techniqueServices->SetCommonResources(_commonResources);
		_techniqueServices->RegisterTextureLoader("*.[dD][dD][sS]", RenderCore::Assets::CreateDDSTextureLoader());
		_techniqueServices->RegisterTextureLoader("*", RenderCore::Assets::CreateWICTextureLoader());

		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		_filteringRegistration = ShaderSourceParser::RegisterShaderSelectorFilteringCompiler(compilers);
		_shaderCompilerRegistration = RenderCore::RegisterShaderCompiler(testHelper._shaderSource, compilers, GetDefaultShaderCompilationFlags(*testHelper._device));
		_shaderCompiler2Registration = RenderCore::Techniques::RegisterInstantiateShaderGraphCompiler(testHelper._shaderSource, compilers);

		RenderCore::Assets::PredefinedPipelineLayout graphicsMainLayout {
			RenderCore::Assets::PredefinedPipelineLayoutFile{ UnitTestPipelineLayout, {}, {} }, 
			"GraphicsMain" };

		auto matDescSetLayout = RenderCore::Techniques::FindLayout(graphicsMainLayout, "Material", PipelineType::Graphics);
		_pipelineLayoutDelegate = Techniques::CreatePipelineLayoutDelegate(matDescSetLayout);
		_drawablesPool = Techniques::CreateDrawablesPool();
		_pipelineCollection = std::make_shared<RenderCore::Techniques::PipelineCollection>(testHelper._device);
		_pipelineAccelerators = Techniques::CreatePipelineAcceleratorPool(
			testHelper._device, _drawablesPool, _pipelineCollection, _pipelineLayoutDelegate, Techniques::PipelineAcceleratorPoolFlags::RecordDescriptorSetBindingInfo);

		_techniqueContext = std::make_shared<Techniques::TechniqueContext>();
		_techniqueContext->_commonResources = _commonResources;
		_techniqueContext->_uniformDelegateManager = RenderCore::Techniques::CreateUniformDelegateManager();
		auto sequencerDescSetLayout = RenderCore::Techniques::FindLayout(graphicsMainLayout, "Sequencer", PipelineType::Graphics);
		auto seqSemiConstantGraphics = Techniques::CreateSemiConstantDescriptorSet(*sequencerDescSetLayout->GetLayout(), "unittest", PipelineType::Graphics, *testHelper._device);
		auto seqSemiConstantCompute = Techniques::CreateSemiConstantDescriptorSet(*sequencerDescSetLayout->GetLayout(), "unittest", PipelineType::Compute, *testHelper._device);
		_techniqueContext->_uniformDelegateManager->BindSemiConstantDescriptorSet("Sequencer"_h, std::move(seqSemiConstantGraphics));
		_techniqueContext->_uniformDelegateManager->BindSemiConstantDescriptorSet("Sequencer"_h, std::move(seqSemiConstantCompute));
		_techniqueContext->_pipelineAccelerators = _pipelineAccelerators;
		_techniqueContext->_drawablesPool = _drawablesPool;
		_techniqueContext->_systemAttachmentFormats = Techniques::CalculateDefaultSystemFormats(*testHelper._device);
		// _techniqueContext->_uniformDelegateManager->BindShaderResourceDelegate(std::make_shared<SystemUniformsDelegate>(*testHelper._device));

		_commonResources->CompleteInitialization(*testHelper._device->GetImmediateContext());
	}

	TechniqueTestApparatus::~TechniqueTestApparatus()
	{
		// we have to clear the asset sets here, because we're starting to pull down manager
		// like the drawables pool
		if (::Assets::Services::HasAssetSets())
			::Assets::Services::GetAssetSets().Clear();
	}

	RenderCore::Techniques::PreparedResourcesVisibility PrepareAndStall(
		TechniqueTestApparatus& testApparatus,
		const RenderCore::Techniques::SequencerConfig& sequencerConfig,
		const RenderCore::Techniques::DrawablesPacket& drawablePkt)
	{
		using namespace RenderCore;
		std::promise<Techniques::PreparedResourcesVisibility> preparePromise;
		auto prepareFuture = preparePromise.get_future();
		Techniques::PrepareResources(std::move(preparePromise), *testApparatus._pipelineAccelerators, sequencerConfig, drawablePkt);
		auto requiredVisibility = prepareFuture.get();		// stall
		testApparatus._pipelineAccelerators->VisibilityBarrier(requiredVisibility._pipelineAcceleratorsVisibility);		// must call this to flip completed pipelines, etc, to visible
		testApparatus._bufferUploads->StallAndMarkCommandListDependency(
			*testApparatus._pipelineAccelerators->GetDevice()->GetImmediateContext(),
			requiredVisibility._bufferUploadsVisibility);
		return requiredVisibility;
	}

	RenderCore::Techniques::ParsingContext BeginParsingContext(TechniqueTestApparatus& testApparatus, RenderCore::IThreadContext& threadContext)
	{
		RenderCore::Techniques::ParsingContext parsingContext{*testApparatus._techniqueContext, threadContext};
		parsingContext.SetPipelineAcceleratorsVisibility(testApparatus._pipelineAccelerators->VisibilityBarrier());
		return parsingContext;
	}

	const char TechniqueTestApparatus::UnitTestPipelineLayout[] = R"--(
		
		DescriptorSet Material
		{
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
		};

		DescriptorSet Sequencer
		{
			UniformBuffer GlobalTransform;
			UniformBuffer ReciprocalViewportDimensionsCB;
			UniformBuffer SeqBuffer0;
			UniformBuffer b3;
			UniformBuffer b4;
			UniformBuffer b5;

			SampledTexture SeqTex0;
			SampledTexture t7;
			SampledTexture t8;
			SampledTexture t9;
			SampledTexture t10;

			// Samplers here must be "fixed" samplers in order to be compatible with sequencer.pipeline. Since main.pipeline
			// (which brings in sequencer.pipeline) is referred to by TechniqueDelegates.cpp, we will otherwise end up mixing
			// these together in some tests
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
		};

		DescriptorSet Numeric {};
		DescriptorSet Spacer {};

		PipelineLayout GraphicsMain
		{
			DescriptorSet Numeric;
			DescriptorSet Sequencer;
			DescriptorSet Spacer;
			DescriptorSet Spacer;
			DescriptorSet Material;
		};

	)--";
}

