// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineApparatus.h"
#include "../Techniques/TechniqueDelegates.h"
#include "../Techniques/Apparatuses.h"
#include "../Techniques/Techniques.h"
#include "../Techniques/PipelineCollection.h"
#include "../Techniques/CommonResources.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Assets/PipelineConfigurationUtils.h"
#include "../IDevice.h"
#include "../../Assets/AssetTraits.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace LightingEngine
{
	SharedTechniqueDelegateBox::SharedTechniqueDelegateBox(IDevice& device, ShaderLanguage shaderLanguage, SamplerPool* samplerPool)
	{
		_depVal = ::Assets::GetDepValSys().Make();
		_techniqueSetFile = ::Assets::MakeAssetPtr<RenderCore::Techniques::TechniqueSetFile>(ILLUM_TECH);
		_forwardIllumDelegate_DisableDepthWrite = RenderCore::Techniques::CreateTechniqueDelegate_Forward(_techniqueSetFile, RenderCore::Techniques::TechniqueDelegateForwardFlags::DisableDepthWrite);
		_depthOnlyDelegate = RenderCore::Techniques::CreateTechniqueDelegate_PreDepth(_techniqueSetFile, Techniques::PreDepthType::DepthOnly);
		_depthMotionDelegate = RenderCore::Techniques::CreateTechniqueDelegate_PreDepth(_techniqueSetFile, Techniques::PreDepthType::DepthMotion);
		_depthMotionNormalDelegate = RenderCore::Techniques::CreateTechniqueDelegate_PreDepth(_techniqueSetFile, Techniques::PreDepthType::DepthMotionNormal);
		_depthMotionNormalRoughnessDelegate = RenderCore::Techniques::CreateTechniqueDelegate_PreDepth(_techniqueSetFile, Techniques::PreDepthType::DepthMotionNormalRoughness);
		_deferredIllumDelegate = RenderCore::Techniques::CreateTechniqueDelegate_Deferred(_techniqueSetFile);

		_lightingOperatorsPipelineLayoutFile = ::Assets::ActualizeAssetPtr<RenderCore::Assets::PredefinedPipelineLayoutFile>(LIGHTING_OPERATOR_PIPELINE);
		_depVal.RegisterDependency(_lightingOperatorsPipelineLayoutFile->GetDependencyValidation());

		const std::string pipelineLayoutName = "LightingOperator";
		auto pipelineInit = RenderCore::Assets::PredefinedPipelineLayout(*_lightingOperatorsPipelineLayoutFile, pipelineLayoutName).MakePipelineLayoutInitializer(shaderLanguage, samplerPool);
		_lightingOperatorLayout = device.CreatePipelineLayout(pipelineInit);

		auto i = _lightingOperatorsPipelineLayoutFile->_descriptorSets.find("DMShadow");
		if (i == _lightingOperatorsPipelineLayoutFile->_descriptorSets.end())
			Throw(std::runtime_error("Missing ShadowTemplate entry in pipeline layout file"));
		_dmShadowDescSetTemplate = i->second;
	}

	LightingEngineApparatus::LightingEngineApparatus(std::shared_ptr<Techniques::DrawingApparatus> drawingApparatus)
	{
		_device = drawingApparatus->_device;
		_pipelineAccelerators = drawingApparatus->_pipelineAccelerators;
		_sharedDelegates = std::make_shared<SharedTechniqueDelegateBox>(
			*_device,
			drawingApparatus->_shaderCompiler->GetShaderLanguage(), &drawingApparatus->_commonResources->_samplerPool);
		_lightingOperatorCollection = std::make_shared<Techniques::PipelineCollection>(_device);
		_systemUniformsDelegate = drawingApparatus->_systemUniformsDelegate;
	}

	LightingEngineApparatus::~LightingEngineApparatus() {}
}}

namespace RenderCore
{
	namespace Techniques 
	{ 
		uint64_t Hash64(RSDepthBias depthBias, uint64_t seed)
		{
			unsigned t0 = *(unsigned*)&depthBias._depthBias;
			unsigned t1 = *(unsigned*)&depthBias._depthBiasClamp;
			unsigned t2 = *(unsigned*)&depthBias._slopeScaledBias;
			return HashCombine(((uint64_t(t0) << 32ull) | uint64_t(t1)) ^ (uint64_t(t2) << 16ull), seed);
		} 
	}
}

