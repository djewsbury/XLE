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
	SharedTechniqueDelegateBox::SharedTechniqueDelegateBox()
	{
		_techniqueSetFile = ::Assets::MakeAsset<RenderCore::Techniques::TechniqueSetFile>(ILLUM_TECH);
		_forwardIllumDelegate_DisableDepthWrite = RenderCore::Techniques::CreateTechniqueDelegate_Forward(_techniqueSetFile, RenderCore::Techniques::TechniqueDelegateForwardFlags::DisableDepthWrite);
		_depthOnlyDelegate = RenderCore::Techniques::CreateTechniqueDelegate_PreDepth(_techniqueSetFile, Techniques::PreDepthType::DepthOnly);
		_depthMotionDelegate = RenderCore::Techniques::CreateTechniqueDelegate_PreDepth(_techniqueSetFile, Techniques::PreDepthType::DepthMotion);
		_depthMotionNormalDelegate = RenderCore::Techniques::CreateTechniqueDelegate_PreDepth(_techniqueSetFile, Techniques::PreDepthType::DepthMotionNormal);
		_deferredIllumDelegate = RenderCore::Techniques::CreateTechniqueDelegate_Deferred(_techniqueSetFile);
	}

	SharedTechniqueDelegateBox::SharedTechniqueDelegateBox(Techniques::DrawingApparatus& drawingApparatus)
	: SharedTechniqueDelegateBox()
	{}

	LightingEngineApparatus::LightingEngineApparatus(std::shared_ptr<Techniques::DrawingApparatus> drawingApparatus)
	{
		_depVal = ::Assets::GetDepValSys().Make();

		_device = drawingApparatus->_device;
		_pipelineAccelerators = drawingApparatus->_pipelineAccelerators;
		_sharedDelegates = std::make_shared<SharedTechniqueDelegateBox>(*drawingApparatus);

		auto pipelineLayoutFileFuture = ::Assets::MakeAsset<RenderCore::Assets::PredefinedPipelineLayoutFile>(LIGHTING_OPERATOR_PIPELINE);
		pipelineLayoutFileFuture->StallWhilePending();
		_lightingOperatorsPipelineLayoutFile = pipelineLayoutFileFuture->Actualize();
		_depVal.RegisterDependency(_lightingOperatorsPipelineLayoutFile->GetDependencyValidation());

		const std::string pipelineLayoutName = "LightingOperator";
		auto pipelineInit = RenderCore::Assets::PredefinedPipelineLayout(*_lightingOperatorsPipelineLayoutFile, pipelineLayoutName).MakePipelineLayoutInitializer(drawingApparatus->_shaderCompiler->GetShaderLanguage(), &drawingApparatus->_commonResources->_samplerPool);
		_lightingOperatorLayout = _device->CreatePipelineLayout(pipelineInit);

		auto i = _lightingOperatorsPipelineLayoutFile->_descriptorSets.find("DMShadow");
		if (i == _lightingOperatorsPipelineLayoutFile->_descriptorSets.end())
			Throw(std::runtime_error("Missing ShadowTemplate entry in pipeline layout file"));
		_dmShadowDescSetTemplate = i->second;

		_lightingOperatorCollection = std::make_shared<Techniques::PipelinePool>(_device);
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

