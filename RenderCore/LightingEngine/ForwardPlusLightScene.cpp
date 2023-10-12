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
		AllocationRules::BitField allocationRulesForDynamicCBs = AllocationRules::HostVisibleSequentialWrite|AllocationRules::DisableAutoCacheCoherency|AllocationRules::PermanentlyMapped;
		auto& device = *_pipelineAccelerators->GetDevice();
		auto tilerConfig = _lightTiler->GetConfiguration();
		for (unsigned c=0; c<dimof(_uniforms); c++) {
			_uniforms[c]._propertyCB = device.CreateResource(
				CreateDesc(BindFlag::ConstantBuffer, allocationRulesForDynamicCBs, LinearBufferDesc::Create(sizeof(Internal::CB_EnvironmentProps))), "env-props");
			_uniforms[c]._propertyCBView = _uniforms[c]._propertyCB->CreateBufferView(BindFlag::ConstantBuffer);

			_uniforms[c]._lightList = device.CreateResource(
				CreateDesc(BindFlag::UnorderedAccess, allocationRulesForDynamicCBs, LinearBufferDesc::Create(sizeof(Internal::CB_Light)*tilerConfig._maxLightsPerView, sizeof(Internal::CB_Light))), "light-list");
			_uniforms[c]._lightListUAV = _uniforms[c]._lightList->CreateBufferView(BindFlag::UnorderedAccess);

			_uniforms[c]._lightDepthTable = device.CreateResource(
				CreateDesc(BindFlag::UnorderedAccess, allocationRulesForDynamicCBs, LinearBufferDesc::Create(sizeof(unsigned)*tilerConfig._depthLookupGradiations, sizeof(unsigned))), "light-depth-table");
			_uniforms[c]._lightDepthTableUAV = _uniforms[c]._lightDepthTable->CreateBufferView(BindFlag::UnorderedAccess);
		}
		_pingPongCounter = 0;

		// Default to using the first light operator & first shadow operator for the dominant light
		if (_shadowPreparerIdMapping._dominantLightOperator != ~0u) {
			_dominantLightSet = std::make_shared<Internal::DominantLightSet>(_shadowPreparerIdMapping._dominantLightOperator, _shadowPreparerIdMapping._dominantShadowOperator);
			RegisterComponent(_dominantLightSet);
		}

		for (unsigned op=0; op<_lightOperatorInfo.size(); ++op)
			if (_lightOperatorInfo[op]._standardLightFlags)
				AssociateFlag(op, _lightOperatorInfo[op]._standardLightFlags);

		if (_shadowPreparerIdMapping._operatorForStaticProbes != ~0u) {
			_shadowProbes = std::make_shared<ShadowProbes>(_pipelineAccelerators, *_techDelBox, _shadowPreparerIdMapping._shadowProbesCfg);
			_shadowProbesManager = std::make_shared<Internal::SemiStaticShadowProbeScheduler>(_shadowProbes, _shadowPreparerIdMapping._operatorForStaticProbes);
			RegisterComponent(_shadowProbesManager);
		}

		bool atLeastOneShadowPreparer = false;
		for (auto i:_shadowPreparerIdMapping._operatorToShadowPreparerId) atLeastOneShadowPreparer |= i != ~0u;

		if (atLeastOneShadowPreparer) {
			_shadowScheduler = std::make_shared<Internal::DynamicShadowProjectionScheduler>(
				_pipelineAccelerators->GetDevice(), _shadowPreparers,
				_shadowPreparerIdMapping._operatorToShadowPreparerId);
			_shadowScheduler->SetDescriptorSetLayout(_techDelBox->_dmShadowDescSetTemplate, PipelineType::Graphics);
			RegisterComponent(_shadowScheduler);
		}
	}

	ILightScene::LightSourceId ForwardPlusLightScene::CreateAmbientLightSource()
	{
		if (_ambientLight->_ambientLightEnabled)
			Throw(std::runtime_error("Attempting to create multiple ambient light sources. Only one is supported at a time"));
		_ambientLight->_ambientLightEnabled = true;
		return 0;
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
			return (ISemiStaticShadowProbeScheduler*)_shadowProbesManager.get();
		case TypeHashCode<IDynamicShadowProjectionScheduler>:
			return (IDynamicShadowProjectionScheduler*)_shadowScheduler.get();
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

		auto& uniforms = _uniforms[_pingPongCounter%dimof(_uniforms)];
		auto& tilerOutputs = _lightTiler->_outputs;
		auto& device = *_pipelineAccelerators->GetDevice();
		{
			Metal::ResourceMap map{
				device, *uniforms._lightDepthTable,
				Metal::ResourceMap::Mode::WriteDiscardPrevious, 
				0, sizeof(unsigned)*tilerOutputs._lightDepthTable.size()};
			std::memcpy(map.GetData().begin(), tilerOutputs._lightDepthTable.data(), sizeof(unsigned)*tilerOutputs._lightDepthTable.size());
			map.FlushCache();
		}
		if (tilerOutputs._lightCount) {
			Metal::ResourceMap map{
				device, *uniforms._lightList,
				Metal::ResourceMap::Mode::WriteDiscardPrevious, 
				0, sizeof(Internal::CB_Light)*tilerOutputs._lightCount};
			auto* i = (Internal::CB_Light*)map.GetData().begin();
			auto end = tilerOutputs._lightOrdering.begin() + tilerOutputs._lightCount;
			for (auto idx=tilerOutputs._lightOrdering.begin(); idx!=end; ++idx, ++i) {
				auto setIdx = *idx >> 16, lightIdx = (*idx)&0xffff;
				auto op = _lightSets[setIdx]._operatorId;
				assert(_lightSets[setIdx]._operatorId != _shadowPreparerIdMapping._dominantLightOperator);
				auto& lightDesc = _lightSets[setIdx]._baseData.GetObject(lightIdx);
				*i = MakeLightUniforms(lightDesc, _lightOperatorInfo[op]._uniformShapeCode);
			}

			if (_shadowProbesManager) {
				i = (Internal::CB_Light*)map.GetData().begin();
				for (auto idx=tilerOutputs._lightOrdering.begin(); idx!=end; ++idx, ++i) {
					auto setIdx = *idx >> 16, lightIdx = (*idx)&0xffff;
					assert(_lightSets[setIdx]._operatorId != _shadowPreparerIdMapping._dominantLightOperator);
					auto probe = _shadowProbesManager->GetAllocatedDatabaseEntry(setIdx, lightIdx);
					i->_staticProbeDatabaseEntry = probe._databaseIndex;
					++i->_staticProbeDatabaseEntry;		// ~0u becomes zero, or add one --> because we want zero to be the sentinal
				}
			}
			map.FlushCache();
		}

		{
			Metal::ResourceMap map{
				device, *uniforms._propertyCB,
				Metal::ResourceMap::Mode::WriteDiscardPrevious};
			auto* i = (Internal::CB_EnvironmentProps*)map.GetData().begin();
			i->_dominantLight = {};

			if (_dominantLightSet && _dominantLightSet->_hasLight) {
				i->_dominantLight = Internal::MakeLightUniforms(
					_lightSets[_dominantLightSet->_setIdx]._baseData.GetObject(0),
					_lightOperatorInfo[_dominantLightSet->_lightOpId]._uniformShapeCode);
			}

			i->_lightCount = tilerOutputs._lightCount;
			i->_enableSSR = enableSSR;
			std::memcpy(i->_diffuseSHCoefficients, _diffuseSHCoefficients, sizeof(_diffuseSHCoefficients));
			map.FlushCache();
		}

		if (_completionCommandListID)
			parsingContext.RequireCommandList(_completionCommandListID);
	}

	void ForwardPlusLightScene::Prerender(IThreadContext& threadContext)
	{
		_lightTiler->CompleteInitialization(threadContext);
		if (_shadowProbes)
			_shadowProbes->CompleteInitialization(threadContext);
	}

	const IPreparedShadowResult* ForwardPlusLightScene::GetDominantPreparedShadow()
	{
		if (!_shadowScheduler || !_dominantLightSet || !_dominantLightSet->_hasLight) return nullptr;
		return _shadowScheduler->GetPreparedShadow(_dominantLightSet->_setIdx, 0);
	}

	class ForwardPlusLightScene::ShaderResourceDelegate : public Techniques::IShaderResourceDelegate
	{
	public:
		void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst) override
		{
			auto& uniforms = _lightScene->_uniforms[_lightScene->_pingPongCounter%dimof(_lightScene->_uniforms)];
			if (bindingFlags & 7) {
				assert((bindingFlags & 7) == 7);
				dst[0] = uniforms._lightDepthTableUAV.get();
				dst[1] = uniforms._lightListUAV.get();
				dst[2] = _lightScene->_lightTiler->_outputs._tiledLightBitFieldSRV.get();
			}

			if (bindingFlags & (1ull<<3ull))
				dst[3] = uniforms._propertyCBView.get();

			if (bindingFlags & (1ull<<4ull)) {
				assert(bindingFlags & (1ull<<5ull));
				if (_lightScene->_shadowProbes && _lightScene->_shadowProbes->IsReady() && _lightScene->_shadowProbesManager->DoneInitialBackgroundPrepare()) {
					dst[4] = &_lightScene->_shadowProbes->GetStaticProbesTable();
					dst[5] = &_lightScene->_shadowProbes->GetShadowProbeUniforms();
				} else {
					// We need a white dummy texture in reverseZ modes, or black in non-reverseZ modes
					assert(Techniques::GetDefaultClipSpaceType() == ClipSpaceType::Positive_ReverseZ || Techniques::GetDefaultClipSpaceType() == ClipSpaceType::PositiveRightHanded_ReverseZ);
					dst[4] = context.GetTechniqueContext()._commonResources->_whiteCubeArraySRV.get();
					dst[5] = context.GetTechniqueContext()._commonResources->_undefinedBufferUAV.get();
				}
			}

			if (bindingFlags & ((1ull<<6ull)|(1ull<<7ull))) {
				dst[6] = _lightScene->_distantSpecularIBL.get();
				dst[7] = _lightScene->_glossLut.get();
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
			BindResourceView(6, "SpecularIBL"_h);
			BindResourceView(7, "GlossLUT"_h);
		}
	};

	std::shared_ptr<Techniques::IShaderResourceDelegate> ForwardPlusLightScene::CreateMainSceneResourceDelegate()
	{
		return std::make_shared<ShaderResourceDelegate>(*this);
	}

	bool ForwardPlusLightScene::ShadowProbesSupported() const
	{
		// returns true if we have an operator for shadow probes, even if the shadow probe database hasn't actually been created
		return _shadowPreparerIdMapping._operatorForStaticProbes != ~0u;
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

#if 0
	bool ForwardPlusLightScene::IsCompatible(
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const AmbientLightOperatorDesc& ambientLightOperator)
	{
		// returns true iff the given operators are exactly compatible with ours, and in the same order
		// this is typically used to determine when we need to rebuild the lighting techniques in response
		// to a configuration change
		if (_shadowPreparerIdMapping._operatorToShadowPreparerId.size() != shadowGenerators.size()) return false;
		if (_positionalLightOperators.size() != resolveOperators.size()) return false;

		for (unsigned c=0; c<_shadowPreparerIdMapping._operatorToShadowPreparerId.size(); ++c) {
			auto preparerId = _shadowPreparerIdMapping._operatorToShadowPreparerId[c];
			if (preparerId == ~0u) continue;
			if (c >= shadowGenerators.size()) return false;
			if (_shadowPreparers->_preparers[preparerId]._desc.GetHash() != shadowGenerators[c].GetHash()) return false;
		}
		if (_shadowPreparerIdMapping._operatorForStaticProbes != ~0u) {
			if (_shadowPreparerIdMapping._operatorForStaticProbes >= shadowGenerators.size()) return false;
			auto cfg = MakeShadowProbeConfiguration(shadowGenerators[_shadowPreparerIdMapping._operatorForStaticProbes]);
			if (!(cfg == _shadowPreparerIdMapping._shadowProbesCfg)) return false;
		}
		for (unsigned c=0; c<_positionalLightOperators.size(); ++c) {
			if (resolveOperators[c].GetHash() != _positionalLightOperators[c].GetHash())
				return false;
		}
		return true;
	}
#endif

	ForwardPlusLightScene::ForwardPlusLightScene()
	{
		_ambientLight = std::make_shared<AmbientLightConfig>();

		// We'll maintain the first few ids for system lights (ambient surrounds, etc)
		ReserveLightSourceIds(32);
		std::memset(_diffuseSHCoefficients, 0, sizeof(_diffuseSHCoefficients));
	}

	std::shared_ptr<ForwardPlusLightScene> ForwardPlusLightScene::CreateInternal(
		const ConstructionServices& constructionServices,
		std::shared_ptr<DynamicShadowPreparers> shadowPreparers,
		std::shared_ptr<RasterizationLightTileOperator> lightTiler,
		ForwardPlusLightScene::ShadowPreparerIdMapping&& shadowPreparerMapping,
		std::vector<LightOperatorInfo>&& lightOperatorInfo,
		std::shared_ptr<IResourceView> glossLut,
		BufferUploads::CommandListID glossLutCompletion,
		::Assets::DependencyValidation depVal)
	{
		auto lightScene = std::make_shared<ForwardPlusLightScene>();
		lightScene->_shadowPreparers = shadowPreparers;
		lightScene->_shadowPreparerIdMapping = std::move(shadowPreparerMapping);
		lightScene->_pipelineAccelerators = constructionServices._pipelineAccelerators;
		lightScene->_techDelBox = constructionServices._techDelBox;
		lightScene->_lightOperatorInfo = std::move(lightOperatorInfo);
		lightScene->_depVal = std::move(depVal);

		lightScene->_lightTiler = lightTiler;
		lightTiler->SetLightScene(*lightScene);

		lightScene->_glossLut = glossLut ? std::move(glossLut) : Techniques::Services::GetCommonResources()->_black2DSRV;
		lightScene->_distantSpecularIBLAndGlossLutCompletion = glossLutCompletion;
		lightScene->_distantSpecularIBL = Techniques::Services::GetCommonResources()->_blackCubeSRV;

		lightScene->FinalizeConfiguration();
		return lightScene;
	}

	void ForwardPlusLightScene::ConstructToPromise(
		std::promise<std::shared_ptr<ForwardPlusLightScene>>&& promise,
		const ConstructionServices& constructionServices,
		ShadowPreparerIdMapping&& shadowPreparerMapping,
		std::vector<LightOperatorInfo>&& lightOperatorInfo,
		const RasterizationLightTileOperatorDesc& tilerCfg,
		const IntegrationParams& integrationParams)
	{
		struct Helper
		{
			std::future<std::shared_ptr<DynamicShadowPreparers>> _shadowPreparationOperatorsFuture;
			std::future<std::shared_ptr<RasterizationLightTileOperator>> _lightTilerFuture;
			std::shared_future<std::shared_ptr<Techniques::DeferredShaderResource>> _glossLUTFuture;
		};
		auto helper = std::make_shared<Helper>();

		helper->_shadowPreparationOperatorsFuture = CreateDynamicShadowPreparers(
			shadowPreparerMapping._shadowPreparers,
			constructionServices._pipelineAccelerators, constructionServices._techDelBox);

		helper->_lightTilerFuture = ::Assets::ConstructToFuturePtr<RasterizationLightTileOperator>(constructionServices._pipelinePool, tilerCfg);
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
			[helper, shadowPreparerMapping=std::move(shadowPreparerMapping), lightOperatorInfo=std::move(lightOperatorInfo), constructionServices] () mutable
			{
				std::shared_ptr<IResourceView> glossLut;
				BufferUploads::CommandListID glossLutCompletion = 0;
				::Assets::DependencyValidationMarker depVals[2];
				if (helper->_glossLUTFuture.valid()) {
					auto defRes = helper->_glossLUTFuture.get();
					glossLut = defRes->GetShaderResource();
					glossLutCompletion = defRes->GetCompletionCommandList();
					depVals[0] = defRes->GetDependencyValidation();
				}
				auto lightTiler = helper->_lightTilerFuture.get();
				depVals[1] = lightTiler->GetDependencyValidation();
				auto depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
				return CreateInternal(constructionServices, helper->_shadowPreparationOperatorsFuture.get(), std::move(lightTiler), std::move(shadowPreparerMapping), std::move(lightOperatorInfo), glossLut, glossLutCompletion, std::move(depVal));
			});
	}

}}
