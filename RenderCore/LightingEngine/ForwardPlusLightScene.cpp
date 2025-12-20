// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ForwardPlusLightScene.h"
#include "ILightScene.h"
#include "SHCoefficients.h"
#include "HierarchicalDepths.h"
#include "ScreenSpaceReflections.h"
#include "LightTiler.h"
#include "ShadowPreparer.h"
#include "ShadowProbes.h"
#include "LightUniforms.h"
#include "LightingDelegateUtil.h"
#include "RenderStepFragments.h"
#include "LightingEngineApparatus.h"
#include "SkyOperator.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/PipelineAccelerator.h"
#include "../Techniques/Techniques.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/Services.h"
#include "../Assets/TextureCompiler.h"
#include "../Metal/Resource.h"
#include "../Metal/DeviceContext.h"
#include "../../Assets/Marker.h"
#include "../../Assets/Assets.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../xleres/FileList.h"

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine
{
	class ForwardPlusLightScene::AmbientLightConfig
	{
	public:
		bool _ambientLightEnabled = false;
	};

	void ForwardPlusLightScene::FinalizeConfiguration()
	{
		// ensure FiniteRange flag is set for tilable lights
		for (auto& op:_lightOperatorsMapping._operatorInfos) {
			if (op._tileable) AssociateFlag(&op-_lightOperatorsMapping._operatorInfos.data(), Internal::StandardPositionLightFlags::SupportFiniteRange);
			if (op._uniformShapeCode == 3) AssociateFlag(&op-_lightOperatorsMapping._operatorInfos.data(), Internal::StandardPositionLightFlags::SupportConeSource);
		}

		// construct uniform buffers, etc
		AllocationRules::BitField allocationRulesForDynamicCBs = AllocationRules::HostVisibleSequentialWrite|AllocationRules::DisableAutoCacheCoherency|AllocationRules::PermanentlyMapped;
		auto& device = *_pipelineAccelerators->GetDevice();
		for (unsigned c=0; c<dimof(_uniforms); c++)
			_uniforms[c]._propertyCB = device.CreateResource(
				CreateDesc(BindFlag::ConstantBuffer, allocationRulesForDynamicCBs, LinearBufferDesc::Create(sizeof(Internal::CB_EnvironmentProps))), "env-props");
		_unmapPropertyCB = device.CreateResource(CreateDesc(BindFlag::ConstantBuffer, LinearBufferDesc::Create(sizeof(Internal::CB_EnvironmentProps))), "env-props");
		if (_unmapPropertyCB) {
			_uniforms[0]._propertyCBView = _unmapPropertyCB->CreateBufferView(BindFlag::ConstantBuffer);
			for (unsigned c=0; c<dimof(_uniforms); c++) _uniforms[c]._propertyCBView = _uniforms[0]._propertyCBView;
		} else
			for (unsigned c=0; c<dimof(_uniforms); c++)
				_uniforms[c]._propertyCBView = _uniforms[c]._propertyCB->CreateBufferView(BindFlag::ConstantBuffer);
		_pingPongCounter = 0;

		// Default to using the first light operator & first shadow operator for the dominant light
		if (_lightOperatorsMapping._dominantLightOperator != ~0u) {
			_dominantLightSet = std::make_shared<Internal::DominantLightSet>(_lightOperatorsMapping._dominantLightOperator, _lightOperatorsMapping._operatorInfos[_lightOperatorsMapping._dominantLightOperator]._uniformShapeCode);
			RegisterComponent(_dominantLightSet);
		}

		if (_lightTiler) {
			std::vector<Internal::TiledLightScheduler::LightOperatorInfo> infosForTiledScheduler; infosForTiledScheduler.reserve(_lightOperatorsMapping._operatorInfos.size());
			for (auto& i:_lightOperatorsMapping._operatorInfos) infosForTiledScheduler.push_back({i._tileable, i._uniformShapeCode});
			_tiledLightScheduler = std::make_shared<Internal::TiledLightScheduler>(_lightTiler, infosForTiledScheduler);
			RegisterComponent(_tiledLightScheduler);
		}

		if (_lightOperatorsMapping._staticShadowProbesCfg) {
			_shadowProbes = std::make_shared<ShadowProbes>(_pipelineAccelerators, *_techDelBox, *_lightOperatorsMapping._staticShadowProbesCfg);
			_staticProbeScheduler = std::make_shared<Internal::SemiStaticShadowProbeScheduler>(_shadowProbes, _lightOperatorsMapping._staticShadowProbeMask);
			RegisterComponent(_staticProbeScheduler);
		}

		if (_lightOperatorsMapping._dynamicShadowProbesCfg) {
			_dynamicShadowProbes = std::make_shared<DynamicShadowProbes>(_pipelineAccelerators, *_techDelBox, *_lightOperatorsMapping._dynamicShadowProbesCfg);
			_dynamicProbeScheduler = std::make_shared<Internal::DynamicShadowProbeScheduler>(_dynamicShadowProbes, _lightOperatorsMapping._dynamicShadowProbeMask);
			RegisterComponent(_dynamicProbeScheduler);
		}

		if (_shadowPreparers) {
			std::vector<unsigned> operatorToPriorityShadowPreparer; operatorToPriorityShadowPreparer.reserve(_lightOperatorsMapping._operatorInfos.size());
			for (auto& i:_lightOperatorsMapping._operatorInfos) operatorToPriorityShadowPreparer.push_back(i._shadowPreparerId);
			_priorityShadowScheduler = std::make_shared<Internal::PriorityShadowProjectionScheduler>(_shadowPreparers, operatorToPriorityShadowPreparer);
			_priorityShadowScheduler->SetDescriptorSetLayout(_techDelBox->_dmShadowDescSetTemplate, PipelineType::Graphics);
			RegisterComponent(_priorityShadowScheduler);
		}
	}

	ILightScene::LightSourceId ForwardPlusLightScene::CreateLightSource(LightOperatorId op)
	{
		if (_lightOperatorsMapping._ambientLightOperator == op) {
			if (_ambientLight->_ambientLightEnabled)
				Throw(std::runtime_error("Attempting to create multiple ambient light sources. Only one is supported at a time"));
			_ambientLight->_ambientLightEnabled = true;
			return 0;
		} 
		return Internal::StandardLightScene::CreateLightSource(op);
	}

	void ForwardPlusLightScene::DestroyLightSource(LightSourceId sourceId)
	{
		if (sourceId == 0) {
			if (!_ambientLight->_ambientLightEnabled)
				Throw(std::runtime_error("Attempting to destroy the ambient light source, but it has not been created"));
			_ambientLight->_ambientLightEnabled = false;
		} else {
			Internal::StandardLightScene::DestroyLightSource(sourceId);
		}
	}

	void ForwardPlusLightScene::Clear()
	{
		_ambientLight->_ambientLightEnabled = false;
		Internal::StandardLightScene::Clear();
	}

	void* ForwardPlusLightScene::TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode)
	{
		if (sourceId == 0) {
			switch (interfaceTypeCode) {
			case TypeHashCode<ISkyTextureProcessor>:
				if (_queryInterfaceHelper)
					return _queryInterfaceHelper(interfaceTypeCode);	// for the ambient light, get the global ISkyTextureProcessor
			default: return nullptr;
			}
		} else {
			return Internal::StandardLightScene::TryGetLightSourceInterface(sourceId, interfaceTypeCode);
		}
	}

	void* ForwardPlusLightScene::QueryInterface(uint64_t typeCode)
	{
		switch (typeCode) {
		case TypeHashCode<ISemiStaticShadowProbeScheduler>:
			return (ISemiStaticShadowProbeScheduler*)_staticProbeScheduler.get();
		case TypeHashCode<Internal::IPriorityShadowProjectionScheduler>:
			return (Internal::IPriorityShadowProjectionScheduler*)_priorityShadowScheduler.get();
		case TypeHashCode<Internal::IDynamicShadowProbeSchedulerMetrics>:
			return (Internal::IDynamicShadowProbeSchedulerMetrics*)_dynamicProbeScheduler.get();
		default:
			// We get a lambda from the lighting delegate to query for more interfaces. It's a bit awkward, but it's convenient
			if (_queryInterfaceHelper)
				if (auto* result = _queryInterfaceHelper(typeCode))
					return result;
			return StandardLightScene::QueryInterface(typeCode);
		}
	}

	void ForwardPlusLightScene::ConfigureParsingContext(Techniques::ParsingContext& parsingContext, bool enableSSR)
	{
		/////////////////
		++_pingPongCounter;

		auto& device = *parsingContext.GetThreadContext().GetDevice();
		auto& uniforms = _uniforms[_pingPongCounter%dimof(_uniforms)];
		{
			Metal::ResourceMap map{
				device, *uniforms._propertyCB,
				Metal::ResourceMap::Mode::WriteDiscardPrevious};
			auto* i = (Internal::CB_EnvironmentProps*)map.GetData().begin();
			i->_dominantLight = {};

			if (_dominantLightSet)
				_dominantLightSet->WriteEnvProps(*i);

			if (_tiledLightScheduler)
				_tiledLightScheduler->WriteEnvProps(*i);
			
			i->_enableSSR = enableSSR;
			std::memcpy(i->_diffuseSHCoefficients, _diffuseSHCoefficients, sizeof(_diffuseSHCoefficients));
			map.FlushCache();
		}

		if (_unmapPropertyCB)
			Metal::DeviceContext::Get(parsingContext.GetThreadContext())->BeginBlitEncoder().Copy(*_unmapPropertyCB, *uniforms._propertyCB);

		if (_tiledLightScheduler)
			_tiledLightScheduler->DoPrepareUniforms(parsingContext);		// should be called after the RasterizationLightTileOperator has been updated

		if (_completionCommandListID)
			parsingContext.RequireCommandList(_completionCommandListID);
	}

	void ForwardPlusLightScene::Prerender(IThreadContext& threadContext)
	{
		if (_lightTiler) _lightTiler->CompleteInitialization(threadContext);
		if (_shadowProbes) _shadowProbes->CompleteInitialization(threadContext);
		if (_dynamicShadowProbes) _dynamicShadowProbes->CompleteInitialization(threadContext);
	}

	const IPreparedShadowResult* ForwardPlusLightScene::GetDominantPreparedShadow()
	{
		if (!_priorityShadowScheduler || !_dominantLightSet || !_dominantLightSet->_hasLight) return nullptr;
		return _priorityShadowScheduler->GetPreparedShadow(_dominantLightSet->_setIdx, 0);
	}

	class ForwardPlusLightScene::ShaderResourceDelegate : public Techniques::IShaderResourceDelegate
	{
	public:
		void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst) override
		{
			if (bindingFlags & 7) {
				assert((bindingFlags & 7) == 7);
				if (_lightScene->_tiledLightScheduler) {
					dst[0] = &_lightScene->_tiledLightScheduler->GetLightDepthTableUAV();
					dst[1] = &_lightScene->_tiledLightScheduler->GetLightListUAV();
					dst[2] = _lightScene->_lightTiler->_outputs._tiledLightBitFieldSRV.get();
				} else {
					dst[0] = context.GetTechniqueContext()._commonResources->_undefinedBufferUAV.get();
					dst[1] = context.GetTechniqueContext()._commonResources->_undefinedBufferUAV.get();
					dst[2] = context.GetTechniqueContext()._commonResources->_black2DSRV.get();
				}
			}

			auto& uniforms = _lightScene->_uniforms[_lightScene->_pingPongCounter%dimof(_lightScene->_uniforms)];
			if (bindingFlags & (1ull<<3ull))
				dst[3] = uniforms._propertyCBView.get();

			if (bindingFlags & (1ull<<4ull)) {
				assert(bindingFlags & (1ull<<5ull));
				if (_lightScene->_shadowProbes && _lightScene->_shadowProbes->IsReady() && _lightScene->_staticProbeScheduler->DoneInitialBackgroundPrepare()) {
					dst[4] = &_lightScene->_shadowProbes->GetStaticProbeTable();
					dst[5] = &_lightScene->_shadowProbes->GetShadowProbeUniforms();
				} else {
					// We need a white dummy texture in reverseZ modes, or black in non-reverseZ modes
					assert(Techniques::GetDefaultClipSpaceType() == ClipSpaceType::Positive_ReverseZ || Techniques::GetDefaultClipSpaceType() == ClipSpaceType::PositiveRightHanded_ReverseZ);
					dst[4] = context.GetTechniqueContext()._commonResources->_whiteCubeArraySRV.get();
					dst[5] = context.GetTechniqueContext()._commonResources->_undefinedBufferUAV.get();
				}
			}

			if (bindingFlags & (1ull<<6ull)) {
				assert(bindingFlags & (1ull<<7ull));
				if (_lightScene->_dynamicShadowProbes) {
					dst[6] = &_lightScene->_dynamicShadowProbes->GetDynamicProbeTable();
					dst[7] = &_lightScene->_dynamicShadowProbes->GetDynamicProbeUniforms();
				} else {
					// We need a white dummy texture in reverseZ modes, or black in non-reverseZ modes
					assert(Techniques::GetDefaultClipSpaceType() == ClipSpaceType::Positive_ReverseZ || Techniques::GetDefaultClipSpaceType() == ClipSpaceType::PositiveRightHanded_ReverseZ);
					dst[6] = context.GetTechniqueContext()._commonResources->_whiteCubeArraySRV.get();
					dst[7] = context.GetTechniqueContext()._commonResources->_undefinedBufferUAV.get();
				}
			}

			if (bindingFlags & ((1ull<<8ull)|(1ull<<9ull))) {
				dst[8] = _lightScene->_distantSpecularIBL.get();
				dst[9] = _lightScene->_glossLut.get();
				context.RequireCommandList(_lightScene->_distantSpecularIBLAndGlossLutCompletion);
			}
		}
		ForwardPlusLightScene* _lightScene = nullptr;
		ShaderResourceDelegate(ForwardPlusLightScene& lightScene)
		{
			_lightScene = &lightScene;
			BindResourceView(0, "LightDepthTable"_h);
			BindResourceView(1, "LightList"_h);
			BindResourceView(2, "TiledLightBitField"_h);
			BindResourceView(3, "EnvironmentProps"_h);
			BindResourceView(4, "StaticShadowProbeDatabase"_h);
			BindResourceView(5, "StaticShadowProbeProperties"_h);
			BindResourceView(6, "DynamicCubeShadowDatabase"_h);
			BindResourceView(7, "DynamicCubeShadowProperties"_h);
			BindResourceView(8, "SpecularIBL"_h);
			BindResourceView(9, "GlossLUT"_h);
		}
	};

	std::shared_ptr<Techniques::IShaderResourceDelegate> ForwardPlusLightScene::CreateMainSceneResourceDelegate()
	{
		return std::make_shared<ShaderResourceDelegate>(*this);
	}

	void ForwardPlusLightScene::SetDiffuseSHCoefficients(const SHCoefficients& coeffients)
	{
		std::memset(_diffuseSHCoefficients, 0, sizeof(_diffuseSHCoefficients));
		std::memcpy(_diffuseSHCoefficients, coeffients.GetCoefficients().begin(), sizeof(Float4)*std::min(coeffients.GetCoefficients().size(), dimof(_diffuseSHCoefficients)));
	}

	void ForwardPlusLightScene::SetDistantSpecularIBL(std::shared_ptr<IResourceView> resource, BufferUploads::CommandListID completion)
	{
		// When distant specular IBL is disabled, _glossLut will be nullptr
		if (_glossLut) {
			_distantSpecularIBL = std::move(resource);
			if (!_distantSpecularIBL) _distantSpecularIBL = Techniques::Services::GetCommonResources()->_blackCubeSRV;
			_distantSpecularIBLAndGlossLutCompletion = std::max(_distantSpecularIBLAndGlossLutCompletion, completion);
		}
	}

	ForwardPlusLightScene::ForwardPlusLightScene()
	{
		_ambientLight = std::make_shared<AmbientLightConfig>();

		// We'll maintain the first few ids for system lights (ambient surrounds, etc)
		ReserveLightSourceIds(32);
		std::memset(_diffuseSHCoefficients, 0, sizeof(_diffuseSHCoefficients));
	}

	std::shared_ptr<ForwardPlusLightScene> ForwardPlusLightScene::CreateInternal(
		const ConstructionServices& constructionServices,
		std::shared_ptr<Internal::PriorityShadowSchedulerUtil> shadowPreparers,
		std::shared_ptr<RasterizationLightTileOperator> lightTiler,
		LightOperatorsMapping&& lightOperatorsMapping,
		std::shared_ptr<IResourceView> glossLut,
		BufferUploads::CommandListID glossLutCompletion,
		::Assets::DependencyValidation depVal)
	{
		auto lightScene = std::make_shared<ForwardPlusLightScene>();
		lightScene->_shadowPreparers = shadowPreparers;
		lightScene->_lightOperatorsMapping = std::move(lightOperatorsMapping);
		lightScene->_pipelineAccelerators = constructionServices._pipelineAccelerators;
		lightScene->_techDelBox = constructionServices._techDelBox;
		lightScene->_depVal = std::move(depVal);

		lightScene->_lightTiler = lightTiler;
		lightScene->_glossLut = glossLut ? std::move(glossLut) : Techniques::Services::GetCommonResources()->_black2DSRV;
		lightScene->_distantSpecularIBLAndGlossLutCompletion = glossLutCompletion;
		lightScene->_distantSpecularIBL = Techniques::Services::GetCommonResources()->_blackCubeSRV;

		lightScene->FinalizeConfiguration();
		return lightScene;
	}

	void ForwardPlusLightScene::ConstructToPromise(
		std::promise<std::shared_ptr<ForwardPlusLightScene>>&& promise,
		const ConstructionServices& constructionServices,
		LightOperatorsMapping&& lightOperatorsMapping,
		const RasterizationLightTileOperatorDesc& tilerCfg,
		const IntegrationParams& integrationParams)
	{
		struct Helper
		{
			std::future<std::shared_ptr<Internal::PriorityShadowSchedulerUtil>> _shadowPreparationOperatorsFuture;
			std::future<std::shared_ptr<RasterizationLightTileOperator>> _lightTilerFuture;
			std::shared_future<std::shared_ptr<Techniques::DeferredShaderResource>> _glossLUTFuture;
		};
		auto helper = std::make_shared<Helper>();

		if (!lightOperatorsMapping._priorityShadowPreparers.empty())
			helper->_shadowPreparationOperatorsFuture = Internal::CreatePriorityShadowSchedulerUtil(
				lightOperatorsMapping._priorityShadowPreparers,
				constructionServices._pipelineAccelerators, constructionServices._techDelBox);

		bool atLeastOneTilable = false;
		for (auto& info:lightOperatorsMapping._operatorInfos) atLeastOneTilable |= info._tileable;
		if (atLeastOneTilable) helper->_lightTilerFuture = ::Assets::ConstructToFuturePtr<RasterizationLightTileOperator>(constructionServices._pipelinePool, tilerCfg);

		helper->_glossLUTFuture = ::Assets::GetAssetFuturePtr<Techniques::DeferredShaderResource>(GLOSS_LUT_TEXTURE);

		using namespace std::placeholders;
		::Assets::PollToPromise(
			std::move(promise),
			[helper](auto timeout) {
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				if (helper->_shadowPreparationOperatorsFuture.valid() && Internal::MarkerTimesOut(helper->_shadowPreparationOperatorsFuture, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (helper->_lightTilerFuture.valid() && Internal::MarkerTimesOut(helper->_lightTilerFuture, timeoutTime)) return ::Assets::PollStatus::Continue;
				if (helper->_glossLUTFuture.valid() && Internal::MarkerTimesOut(helper->_glossLUTFuture, timeoutTime)) return ::Assets::PollStatus::Continue;
				return ::Assets::PollStatus::Finish;
			},
			[helper, lightOperatorsMapping=std::move(lightOperatorsMapping), constructionServices] () mutable
			{
				std::shared_ptr<IResourceView> glossLut;
				BufferUploads::CommandListID glossLutCompletion = 0;
				::Assets::DependencyValidationMarker depVals[2] { ::Assets::DependencyValidationMarker_Invalid, ::Assets::DependencyValidationMarker_Invalid };
				if (helper->_glossLUTFuture.valid()) {
					auto defRes = helper->_glossLUTFuture.get();
					glossLut = defRes->GetShaderResource();
					glossLutCompletion = defRes->GetCompletionCommandList();
					depVals[0] = defRes->GetDependencyValidation();
				}
				std::shared_ptr<RasterizationLightTileOperator> lightTiler;
				if (helper->_lightTilerFuture.valid()) {
					lightTiler = helper->_lightTilerFuture.get();
					depVals[1] = lightTiler->GetDependencyValidation();
				}
				auto depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
				std::shared_ptr<Internal::PriorityShadowSchedulerUtil> priorityPreparers;
				if (helper->_shadowPreparationOperatorsFuture.valid()) priorityPreparers = helper->_shadowPreparationOperatorsFuture.get();
				return CreateInternal(constructionServices, std::move(priorityPreparers), std::move(lightTiler), std::move(lightOperatorsMapping), glossLut, glossLutCompletion, std::move(depVal));
			});
	}

}}
