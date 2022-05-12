// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueTestsHelper.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"

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

		_pipelineAccelerators = Techniques::CreatePipelineAcceleratorPool(
			testHelper._device, _materialDescSetLayout, Techniques::PipelineAcceleratorPoolFlags::RecordDescriptorSetBindingInfo);

		_techniqueContext = std::make_shared<Techniques::TechniqueContext>();
		_techniqueContext->_commonResources = _commonResources;
		_techniqueContext->_uniformDelegateManager = RenderCore::Techniques::CreateUniformDelegateManager();
		_techniqueContext->_uniformDelegateManager->AddSemiConstantDescriptorSet(Hash64("Sequencer"), *_sequencerDescSetLayout->GetLayout(), *testHelper._device);
		// _techniqueContext->_uniformDelegateManager->AddShaderResourceDelegate(std::make_shared<SystemUniformsDelegate>(*testHelper._device));
	}

	TechniqueTestApparatus::~TechniqueTestApparatus()
	{
	}

	const char TechniqueTestApparatus::UnitTestPipelineLayout[] = R"--(
		
		DescriptorSet Material
		{
			UniformBuffer b0;
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

