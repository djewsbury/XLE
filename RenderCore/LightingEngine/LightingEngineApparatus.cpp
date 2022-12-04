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
	auto SharedTechniqueDelegateBox::GetForwardIllumDelegate_DisableDepthWrite() -> TechniqueDelegateFuture
	{
		if (::Assets::IsInvalidated(_forwardIllumDelegate_DisableDepthWrite)) {
			_forwardIllumDelegate_DisableDepthWrite = ::Assets::MarkerPtr<Techniques::ITechniqueDelegate>{};
			Techniques::CreateTechniqueDelegate_Forward(_forwardIllumDelegate_DisableDepthWrite.AdoptPromise(), GetTechniqueSetFile(), Techniques::TechniqueDelegateForwardFlags::DisableDepthWrite);
		}
		return _forwardIllumDelegate_DisableDepthWrite.ShareFuture();
	}

	auto SharedTechniqueDelegateBox::GetDepthOnlyDelegate() -> TechniqueDelegateFuture
	{
		if (::Assets::IsInvalidated(_depthOnlyDelegate)) {
			_depthOnlyDelegate = ::Assets::MarkerPtr<Techniques::ITechniqueDelegate>{};
			Techniques::CreateTechniqueDelegate_PreDepth(_depthOnlyDelegate.AdoptPromise(), GetTechniqueSetFile(), Techniques::PreDepthType::DepthOnly);
		}
		return _depthOnlyDelegate.ShareFuture();
	}

	auto SharedTechniqueDelegateBox::GetDepthMotionDelegate() -> TechniqueDelegateFuture
	{
		if (::Assets::IsInvalidated(_depthMotionDelegate)) {
			_depthMotionDelegate = ::Assets::MarkerPtr<Techniques::ITechniqueDelegate>{};
			Techniques::CreateTechniqueDelegate_PreDepth(_depthMotionDelegate.AdoptPromise(), GetTechniqueSetFile(), Techniques::PreDepthType::DepthMotion);
		}
		return _depthMotionDelegate.ShareFuture();
	}

	auto SharedTechniqueDelegateBox::GetDepthMotionNormalDelegate() -> TechniqueDelegateFuture
	{
		if (::Assets::IsInvalidated(_depthMotionNormalDelegate)) {
			_depthMotionNormalDelegate = ::Assets::MarkerPtr<Techniques::ITechniqueDelegate>{};
			Techniques::CreateTechniqueDelegate_PreDepth(_depthMotionNormalDelegate.AdoptPromise(), GetTechniqueSetFile(), Techniques::PreDepthType::DepthMotionNormal);
		}
		return _depthMotionNormalDelegate.ShareFuture();
	}

	auto SharedTechniqueDelegateBox::GetDepthMotionNormalRoughnessDelegate() -> TechniqueDelegateFuture
	{
		if (::Assets::IsInvalidated(_depthMotionNormalRoughnessDelegate)) {
			_depthMotionNormalRoughnessDelegate = ::Assets::MarkerPtr<Techniques::ITechniqueDelegate>{};
			Techniques::CreateTechniqueDelegate_PreDepth(_depthMotionNormalRoughnessDelegate.AdoptPromise(), GetTechniqueSetFile(), Techniques::PreDepthType::DepthMotionNormalRoughness);
		}
		return _depthMotionNormalRoughnessDelegate.ShareFuture();
	}

	auto SharedTechniqueDelegateBox::GetDeferredIllumDelegate() -> TechniqueDelegateFuture
	{
		if (::Assets::IsInvalidated(_deferredIllumDelegate)) {
			_deferredIllumDelegate = ::Assets::MarkerPtr<Techniques::ITechniqueDelegate>{};
			Techniques::CreateTechniqueDelegate_Deferred(_deferredIllumDelegate.AdoptPromise(), GetTechniqueSetFile());
		}
		return _deferredIllumDelegate.ShareFuture();
	}

	auto SharedTechniqueDelegateBox::GetTechniqueSetFile() -> std::shared_future<std::shared_ptr<Techniques::TechniqueSetFile>>
	{
		if (::Assets::IsInvalidated(_techniqueSetFile)) {
			_techniqueSetFile = ::Assets::MarkerPtr<Techniques::TechniqueSetFile>{};
			::Assets::AutoConstructToPromise(_techniqueSetFile.AdoptPromise(), MakeStringSection(ILLUM_TECH));
		}
		return _techniqueSetFile.ShareFuture();
	}

	SharedTechniqueDelegateBox::SharedTechniqueDelegateBox(IDevice& device, ShaderLanguage shaderLanguage, SamplerPool* samplerPool)
	{
		_depVal = ::Assets::GetDepValSys().Make();
		::Assets::AutoConstructToPromise(_techniqueSetFile.AdoptPromise(), MakeStringSection(ILLUM_TECH));
		Techniques::CreateTechniqueDelegate_Forward(_forwardIllumDelegate_DisableDepthWrite.AdoptPromise(), _techniqueSetFile.ShareFuture(), Techniques::TechniqueDelegateForwardFlags::DisableDepthWrite);
		Techniques::CreateTechniqueDelegate_PreDepth(_depthOnlyDelegate.AdoptPromise(), _techniqueSetFile.ShareFuture(), Techniques::PreDepthType::DepthOnly);
		Techniques::CreateTechniqueDelegate_PreDepth(_depthMotionDelegate.AdoptPromise(), _techniqueSetFile.ShareFuture(), Techniques::PreDepthType::DepthMotion);
		Techniques::CreateTechniqueDelegate_PreDepth(_depthMotionNormalDelegate.AdoptPromise(), _techniqueSetFile.ShareFuture(), Techniques::PreDepthType::DepthMotionNormal);
		Techniques::CreateTechniqueDelegate_PreDepth(_depthMotionNormalRoughnessDelegate.AdoptPromise(), _techniqueSetFile.ShareFuture(), Techniques::PreDepthType::DepthMotionNormalRoughness);
		Techniques::CreateTechniqueDelegate_Deferred(_deferredIllumDelegate.AdoptPromise(), _techniqueSetFile.ShareFuture());

		_lightingOperatorsPipelineLayoutFile = ::Assets::ActualizeAssetPtr<Assets::PredefinedPipelineLayoutFile>(LIGHTING_OPERATOR_PIPELINE);
		_depVal.RegisterDependency(_lightingOperatorsPipelineLayoutFile->GetDependencyValidation());

		const std::string pipelineLayoutName = "LightingOperator";
		auto pipelineInit = Assets::PredefinedPipelineLayout(*_lightingOperatorsPipelineLayoutFile, pipelineLayoutName).MakePipelineLayoutInitializer(shaderLanguage, samplerPool);
		_lightingOperatorLayout = device.CreatePipelineLayout(pipelineInit, "LightingOperator");

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

