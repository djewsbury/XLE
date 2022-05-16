// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueTestsHelper.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../../RenderCore/Techniques/Drawables.h"
#include "../../../RenderCore/Techniques/CompiledLayoutPool.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../Assets/AssetSetManager.h"
#include "../../../Assets/AssetServices.h"

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
		_techniqueServices->RegisterTextureLoader(std::regex(R"(.*\.[dD][dD][sS])"), RenderCore::Assets::CreateDDSTextureLoader());
		_techniqueServices->RegisterTextureLoader(std::regex(R"(.*)"), RenderCore::Assets::CreateWICTextureLoader());

		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		_filteringRegistration = ShaderSourceParser::RegisterShaderSelectorFilteringCompiler(compilers);
		_shaderCompilerRegistration = RenderCore::RegisterShaderCompiler(testHelper._shaderSource, compilers);
		_shaderCompiler2Registration = RenderCore::Techniques::RegisterInstantiateShaderGraphCompiler(testHelper._shaderSource, compilers);

        RenderCore::Assets::PredefinedPipelineLayoutFile layoutFile { UnitTestPipelineLayout, {}, {} };
        _materialDescSetLayout = RenderCore::Techniques::FindLayout(layoutFile, "GraphicsMain", "Material", PipelineType::Graphics);
        _sequencerDescSetLayout = RenderCore::Techniques::FindLayout(layoutFile, "GraphicsMain", "Sequencer", PipelineType::Graphics);

		_compiledLayoutPool = CreateCompiledLayoutPool(testHelper._device, _materialDescSetLayout);
		_pipelineAccelerators = Techniques::CreatePipelineAcceleratorPool(
			testHelper._device, _compiledLayoutPool, Techniques::PipelineAcceleratorPoolFlags::RecordDescriptorSetBindingInfo);
		_drawablesPool = Techniques::CreateDrawablesPool();

		_techniqueContext = std::make_shared<Techniques::TechniqueContext>();
		_techniqueContext->_commonResources = _commonResources;
		_techniqueContext->_uniformDelegateManager = RenderCore::Techniques::CreateUniformDelegateManager();
		_techniqueContext->_uniformDelegateManager->AddSemiConstantDescriptorSet(Hash64("Sequencer"), *_sequencerDescSetLayout->GetLayout(), *testHelper._device);
		// _techniqueContext->_uniformDelegateManager->AddShaderResourceDelegate(std::make_shared<SystemUniformsDelegate>(*testHelper._device));
	}

	TechniqueTestApparatus::~TechniqueTestApparatus()
	{
		// we have to clear the asset sets here, because we're starting to pull down manager
		// like the drawables pool
		if (::Assets::Services::HasAssetSets())
			::Assets::Services::GetAssetSets().Clear();
	}

	const char TechniqueTestApparatus::UnitTestPipelineLayout[] = R"--(
		
		DescriptorSet Material
		{
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

			UniformBuffer b1;
			UniformBuffer b2;

			SampledTexture t3;
			SampledTexture t4;
			SampledTexture t5;
			SampledTexture t6;
			SampledTexture t7;
			SampledTexture t8;
			SampledTexture t9;
			SampledTexture t10;

			UnorderedAccessBuffer u11;
			Sampler s12;
		};

		DescriptorSet Sequencer
		{
			UniformBuffer GlobalTransform;
			UniformBuffer LocalTransform;
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

		PipelineLayout GraphicsMain
		{
			DescriptorSet Sequencer;
			DescriptorSet Material;
		};

	)--";
}

