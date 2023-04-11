// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineApparatus.h"
#include "GBufferOperator.h"
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
#include "../../Assets/ConfigFileContainer.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace LightingEngine
{
	auto SharedTechniqueDelegateBox::GetForwardIllumDelegate_DisableDepthWrite() -> TechniqueDelegateFuture
	{
		if (::Assets::IsInvalidated(_forwardIllumDelegate_DisableDepthWrite)) {
			_forwardIllumDelegate_DisableDepthWrite = ::Assets::MarkerPtr<Techniques::ITechniqueDelegate>{};
			Techniques::CreateTechniqueDelegate_Forward(_forwardIllumDelegate_DisableDepthWrite.AdoptPromise(), GetTechniqueSetFile(), Techniques::TechniqueDelegateForwardFlags::DisableDepthWrite);
		}
		return _forwardIllumDelegate_DisableDepthWrite.ShareFuture();
	}

	auto SharedTechniqueDelegateBox::GetGBufferDelegate(GBufferDelegateType type) -> TechniqueDelegateFuture
	{
		assert(unsigned(type) < dimof(_gbufferDelegates));
		if (::Assets::IsInvalidated(_gbufferDelegates[unsigned(type)]))
			LoadGBufferDelegate(type);
		return _gbufferDelegates[unsigned(type)].ShareFuture();
	}

	void SharedTechniqueDelegateBox::LoadGBufferDelegate(GBufferDelegateType type)
	{
		_gbufferDelegates[unsigned(type)] = ::Assets::MarkerPtr<Techniques::ITechniqueDelegate>{};
		CreateTechniqueDelegate_GBuffer(_gbufferDelegates[unsigned(type)].AdoptPromise(), GetTechniqueSetFile(), type);
	}

	void CreateTechniqueDelegate_GBuffer(
		std::promise<std::shared_ptr<Techniques::ITechniqueDelegate>>&& promise,
		const Techniques::TechniqueSetFileFuture& techniqueSet,
		GBufferDelegateType type)
	{
		switch (type) {
		case GBufferDelegateType::Depth:
			Techniques::CreateTechniqueDelegate_PreDepth(std::move(promise), techniqueSet, Techniques::PreDepthType::DepthOnly);
			break;

		case GBufferDelegateType::DepthMotion:
			Techniques::CreateTechniqueDelegate_PreDepth(std::move(promise), techniqueSet, Techniques::PreDepthType::DepthMotion);
			break;

		case GBufferDelegateType::DepthMotionNormal:
			Techniques::CreateTechniqueDelegate_PreDepth(std::move(promise), techniqueSet, Techniques::PreDepthType::DepthMotionNormal);
			break;

		case GBufferDelegateType::DepthMotionNormalRoughness:
			Techniques::CreateTechniqueDelegate_PreDepth(std::move(promise), techniqueSet, Techniques::PreDepthType::DepthMotionNormalRoughness);
			break;

		case GBufferDelegateType::DepthMotionNormalRoughnessAccumulation:
			Techniques::CreateTechniqueDelegate_PreDepth(std::move(promise), techniqueSet, Techniques::PreDepthType::DepthMotionNormalRoughnessAccumulation);
			break;

		case GBufferDelegateType::DepthNormal:
			Techniques::CreateTechniqueDelegate_Deferred(std::move(promise), techniqueSet, 0);
			break;

		case GBufferDelegateType::DepthNormalParameters:
			Techniques::CreateTechniqueDelegate_Deferred(std::move(promise), techniqueSet, 1);
			break;

		default:
			assert(0);
			break;
		}
	}

	auto SharedTechniqueDelegateBox::GetTechniqueSetFile() -> std::shared_future<std::shared_ptr<Techniques::TechniqueSetFile>>
	{
		if (::Assets::IsInvalidated(_techniqueSetFile)) {
			_techniqueSetFile = ::Assets::MarkerPtr<Techniques::TechniqueSetFile>{};
			::Assets::AutoConstructToPromise(_techniqueSetFile.AdoptPromise(), ILLUM_TECH);
		}
		return _techniqueSetFile.ShareFuture();
	}

	SharedTechniqueDelegateBox::SharedTechniqueDelegateBox(IDevice& device, ShaderLanguage shaderLanguage, SamplerPool* samplerPool)
	{
		_depVal = ::Assets::GetDepValSys().Make();
		::Assets::AutoConstructToPromise(_techniqueSetFile.AdoptPromise(), ILLUM_TECH);
		Techniques::CreateTechniqueDelegate_Forward(_forwardIllumDelegate_DisableDepthWrite.AdoptPromise(), _techniqueSetFile.ShareFuture(), Techniques::TechniqueDelegateForwardFlags::DisableDepthWrite);
		for (unsigned c=0; c<dimof(_gbufferDelegates); ++c)
			LoadGBufferDelegate(GBufferDelegateType(c));

		_lightingOperatorsPipelineLayoutFile = ::Assets::ActualizeAssetPtr<Assets::PredefinedPipelineLayoutFile>(LIGHTING_OPERATOR_PIPELINE);
		_depVal.RegisterDependency(_lightingOperatorsPipelineLayoutFile->GetDependencyValidation());

		const std::string pipelineLayoutName = "LightingOperator";
		auto pipelineInit = Assets::PredefinedPipelineLayout(*_lightingOperatorsPipelineLayoutFile, pipelineLayoutName).MakePipelineLayoutInitializer(shaderLanguage, samplerPool);
		_lightingOperatorLayout = device.CreatePipelineLayout(pipelineInit, "LightingOperator");

		auto i = _lightingOperatorsPipelineLayoutFile->_descriptorSets.find("DMShadow");
		if (i == _lightingOperatorsPipelineLayoutFile->_descriptorSets.end())
			Throw(std::runtime_error("Missing ShadowTemplate entry in pipeline layout file"));
		_dmShadowDescSetTemplate = i->second;

		auto forwardPipelineLayout = ::Assets::ActualizeAssetPtr<Assets::PredefinedPipelineLayoutFile>(FORWARD_PIPELINE);
		_depVal.RegisterDependency(forwardPipelineLayout->GetDependencyValidation());

		i = forwardPipelineLayout->_descriptorSets.find("ForwardLighting");
		if (i == forwardPipelineLayout->_descriptorSets.end())
			Throw(std::runtime_error("Missing ForwardLighting entry in pipeline layout file"));
		_forwardLightingDescSetTemplate = i->second;
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

