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
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/PipelineAccelerator.h"
#include "../Techniques/Techniques.h"
#include "../Techniques/CommonResources.h"
#include "../Assets/TextureCompiler.h"
#include "../Metal/Resource.h"
#include "../../Assets/Marker.h"
#include "../../Assets/Assets.h"

namespace RenderCore { namespace LightingEngine
{
	static const unsigned s_shadowProbeShadowFlag = 1u<<31u;

	using ForwardPlusLightDesc = Internal::StandardPositionalLight;

	class ForwardPlusLightScene::AmbientLightConfig
	{
	public:
		::Assets::PtrToMarkerPtr<Techniques::DeferredShaderResource> _specularIBL;
		::Assets::PtrToMarkerPtr<Techniques::DeferredShaderResource> _ambientRawCubemap;
		std::shared_ptr<::Assets::Marker<SHCoefficientsAsset>> _diffuseIBL;

		enum class SourceImageType { Equirectangular };
		SourceImageType _sourceImageType = SourceImageType::Equirectangular;
		std::string _sourceImage;

		AmbientLightOperatorDesc _ambientLightOperator;
		bool _ambientLightEnabled = false;

		void SetEquirectangularSource(StringSection<> input)
		{
			if (XlEqString(input, _sourceImage)) return;
			_sourceImage = input.AsString();
			_sourceImageType = SourceImageType::Equirectangular;
			_diffuseIBL = ::Assets::MakeAsset<SHCoefficientsAsset>(input);

			Assets::TextureCompilationRequest request;
			request._operation = Assets::TextureCompilationRequest::Operation::EquiRectFilterGlossySpecular; 
			request._srcFile = _sourceImage;
			request._format = Format::BC6H_UF16;
			request._faceDim = 512;
			_specularIBL = ::Assets::NewMarkerPtr<Techniques::DeferredShaderResource>(request);

			Assets::TextureCompilationRequest request2;
			request2._operation = Assets::TextureCompilationRequest::Operation::EquRectToCubeMap; 
			request2._srcFile = _sourceImage;
			request2._format = Format::BC6H_UF16;
			request2._faceDim = 1024;
			request2._mipMapFilter = Assets::TextureCompilationRequest::MipMapFilter::FromSource;
			_ambientRawCubemap = ::Assets::NewMarkerPtr<Techniques::DeferredShaderResource>(request2);
		}
	};

	void ForwardPlusLightScene::FinalizeConfiguration()
	{
		AllocationRules::BitField allocationRulesForDynamicCBs = AllocationRules::HostVisibleRandomAccess|AllocationRules::DisableAutoCacheCoherency|AllocationRules::PermanentlyMapped;
		auto& device = *_pipelineAccelerators->GetDevice();
		auto tilerConfig = _lightTiler->GetConfiguration();
		for (unsigned c=0; c<dimof(_uniforms); c++) {
			_uniforms[c]._propertyCB = device.CreateResource(
				CreateDesc(BindFlag::ConstantBuffer, allocationRulesForDynamicCBs, LinearBufferDesc::Create(sizeof(Internal::CB_EnvironmentProps)), "env-props"));
			_uniforms[c]._propertyCBView = _uniforms[c]._propertyCB->CreateBufferView(BindFlag::ConstantBuffer);

			_uniforms[c]._lightList = device.CreateResource(
				CreateDesc(BindFlag::UnorderedAccess, allocationRulesForDynamicCBs, LinearBufferDesc::Create(sizeof(Internal::CB_Light)*tilerConfig._maxLightsPerView, sizeof(Internal::CB_Light)), "light-list"));
			_uniforms[c]._lightListUAV = _uniforms[c]._lightList->CreateBufferView(BindFlag::UnorderedAccess);

			_uniforms[c]._lightDepthTable = device.CreateResource(
				CreateDesc(BindFlag::UnorderedAccess, allocationRulesForDynamicCBs, LinearBufferDesc::Create(sizeof(unsigned)*tilerConfig._depthLookupGradiations, sizeof(unsigned)), "light-depth-table"));
			_uniforms[c]._lightDepthTableUAV = _uniforms[c]._lightDepthTable->CreateBufferView(BindFlag::UnorderedAccess);
		}
		_pingPongCounter = 0;

		// Default to using the first light operator & first shadow operator for the dominant light
		_dominantLightSet._operatorId = ~0u;
		_dominantLightSet._shadowOperatorId = ~0u;
		for (unsigned c=0; c<_positionalLightOperators.size(); ++c)
			if (_positionalLightOperators[c]._flags & LightSourceOperatorDesc::Flags::DominantLight) {
				if (_dominantLightSet._operatorId != ~0u)
					Throw(std::runtime_error("Multiple dominant light operators detected. This isn't supported -- there must be either 0 or 1"));
				_dominantLightSet._operatorId = c;
			}
		for (unsigned c=0; c<_shadowOperators.size(); ++c) {
			if (_shadowOperators[c]._dominantLight) {
				if (_dominantLightSet._shadowOperatorId != ~0u)
					Throw(std::runtime_error("Multiple dominant shadow operators detected. This isn't supported -- there must be either 0 or 1"));
				_dominantLightSet._shadowOperatorId = c;
			}
		}
	}

	ILightScene::LightSourceId ForwardPlusLightScene::CreateLightSource(ILightScene::LightOperatorId opId)
	{
		if (opId == _positionalLightOperators.size()) {
			if (_ambientLight->_ambientLightEnabled)
				Throw(std::runtime_error("Attempting to create multiple ambient light sources. Only one is supported at a time"));
			_ambientLight->_ambientLightEnabled = true;
			return 0;
		}
		auto desc = std::make_unique<ForwardPlusLightDesc>(Internal::StandardPositionalLight::Flags::SupportFiniteRange);
		return AddLightSource(opId, std::move(desc));
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

	ILightScene::ShadowProjectionId ForwardPlusLightScene::CreateShadowProjection(ShadowOperatorId opId, LightSourceId associatedLight)
	{
		auto preparerId = _shadowPreparerIdMapping._operatorToShadowPreparerId[opId];
		if (preparerId != ~0u) {
			auto desc = _shadowPreparers->CreateShadowProjection(preparerId);
			return AddShadowProjection(opId, associatedLight, std::move(desc));
		} else if (opId == _shadowPreparerIdMapping._operatorForStaticProbes) {
			Throw(std::runtime_error("Use the multi-light shadow projection constructor for shadow probes"));
		}
		return ~0u;
	}

	ILightScene::ShadowProjectionId ForwardPlusLightScene::CreateShadowProjection(ShadowOperatorId opId, IteratorRange<const LightSourceId*> associatedLights)
	{
		if (opId == _shadowPreparerIdMapping._operatorForStaticProbes) {
			if (_shadowProbes)
				Throw(std::runtime_error("Cannot create multiple shadow probe databases in on light scene."));
			
			_shadowProbes = std::make_shared<ShadowProbes>(
				_pipelineAccelerators, *_techDelBox, _shadowPreparerIdMapping._shadowProbesCfg);
			_spPrepareDelegate = Internal::CreateShadowProbePrepareDelegate(_shadowProbes, associatedLights, this);
			return s_shadowProbeShadowFlag;
		} else {
			Throw(std::runtime_error("This shadow projection operation can't be used with the multi-light constructor variation"));
		}
		return ~0u;
	}

	void ForwardPlusLightScene::DestroyShadowProjection(ShadowProjectionId projectionId)
	{
		if (projectionId == s_shadowProbeShadowFlag) {
			_shadowProbes.reset();
			_spPrepareDelegate.reset();
		} else {
			return Internal::StandardLightScene::DestroyShadowProjection(projectionId);
		}
	}

	void* ForwardPlusLightScene::TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode)
	{
		if (sourceId == 0) {
			if (interfaceTypeCode == typeid(IDistantIBLSource).hash_code()) return (IDistantIBLSource*)this;
			if (interfaceTypeCode == typeid(ISSAmbientOcclusion).hash_code()) return (ISSAmbientOcclusion*)this;
			return nullptr;
		} else {
			return Internal::StandardLightScene::TryGetLightSourceInterface(sourceId, interfaceTypeCode);
		}
	}

	void* ForwardPlusLightScene::TryGetShadowProjectionInterface(ShadowProjectionId projectionid, uint64_t interfaceTypeCode)
	{
		if (projectionid == s_shadowProbeShadowFlag) {
			if (interfaceTypeCode == typeid(IPreparable).hash_code()) return (IPreparable*)_spPrepareDelegate.get();
			else if (interfaceTypeCode == typeid(IShadowProbeDatabase).hash_code()) return dynamic_cast<IShadowProbeDatabase*>(_spPrepareDelegate.get());
			return nullptr;
		} else {
			return Internal::StandardLightScene::TryGetShadowProjectionInterface(projectionid, interfaceTypeCode);
		} 
	}

	void ForwardPlusLightScene::SetEquirectangularSource(StringSection<> input)
	{
		if (XlEqString(input, _ambientLight->_sourceImage)) return;
		_ambientLight->SetEquirectangularSource(input);
		auto weakThis = weak_from_this();
		::Assets::WhenAll(_ambientLight->_specularIBL, _ambientLight->_diffuseIBL, _ambientLight->_ambientRawCubemap).Then(
			[weakThis](auto specularIBLFuture, auto diffuseIBLFuture, auto ambientRawCubemapFuture) {
				auto l = weakThis.lock();
				if (!l) return;

				std::shared_ptr<Techniques::DeferredShaderResource> specularIBL;
				std::optional<SHCoefficientsAsset> diffuseIBL;
				TRY {
					specularIBL = specularIBLFuture.get();
					diffuseIBL = diffuseIBLFuture.get();
				} CATCH(...) {
					// suppress bad textures
				} CATCH_END

				if (!specularIBL || !diffuseIBL.has_value()) {
					l->_onChangeSpecularIBL(nullptr);
					l->_onChangeSkyTexture(nullptr);
					l->_onChangeDiffuseIBL({});
					std::memset(l->_diffuseSHCoefficients, 0, sizeof(l->_diffuseSHCoefficients));
				} else {
					auto ambientRawCubemap = ambientRawCubemapFuture.get();
					std::memset(l->_diffuseSHCoefficients, 0, sizeof(l->_diffuseSHCoefficients));
					std::memcpy(l->_diffuseSHCoefficients, diffuseIBL.value().GetCoefficients().begin(), sizeof(Float4*)*std::min(diffuseIBL.value().GetCoefficients().size(), dimof(l->_diffuseSHCoefficients)));
					l->_completionCommandListID = std::max(l->_completionCommandListID, ambientRawCubemap->GetCompletionCommandList());
					l->_onChangeSpecularIBL(specularIBL);
					l->_onChangeSkyTexture(ambientRawCubemap);
					l->_onChangeDiffuseIBL(diffuseIBL);
				}
			});
	}

	void ForwardPlusLightScene::ConfigureParsingContext(Techniques::ParsingContext& parsingContext)
	{
		bool lastFrameBuffersPrimed = _pingPongCounter != 0;

		/////////////////
		++_pingPongCounter;

		auto& uniforms = _uniforms[_pingPongCounter%dimof(_uniforms)];
		auto& tilerOutputs = _lightTiler->_outputs;
		auto& device = *_pipelineAccelerators->GetDevice();
		{
			Metal::ResourceMap map(
				device, *uniforms._lightDepthTable,
				Metal::ResourceMap::Mode::WriteDiscardPrevious, 
				0, sizeof(unsigned)*tilerOutputs._lightDepthTable.size());
			std::memcpy(map.GetData().begin(), tilerOutputs._lightDepthTable.data(), sizeof(unsigned)*tilerOutputs._lightDepthTable.size());
			map.FlushCache();
		}
		if (tilerOutputs._lightCount) {
			Metal::ResourceMap map(
				device, *uniforms._lightList,
				Metal::ResourceMap::Mode::WriteDiscardPrevious, 
				0, sizeof(Internal::CB_Light)*tilerOutputs._lightCount);
			auto* i = (Internal::CB_Light*)map.GetData().begin();
			auto end = tilerOutputs._lightOrdering.begin() + tilerOutputs._lightCount;
			for (auto idx=tilerOutputs._lightOrdering.begin(); idx!=end; ++idx, ++i) {
				auto set = *idx >> 16, light = (*idx)&0xffff;
				auto op = _tileableLightSets[set]._operatorId;
				auto& lightDesc = *(ForwardPlusLightDesc*)_tileableLightSets[set]._lights[light]._desc.get();
				*i = MakeLightUniforms(lightDesc, _positionalLightOperators[op]);
			}
			map.FlushCache();
		}

		{
			Metal::ResourceMap map(
				device, *uniforms._propertyCB,
				Metal::ResourceMap::Mode::WriteDiscardPrevious);
			auto* i = (Internal::CB_EnvironmentProps*)map.GetData().begin();
			i->_dominantLight = {};

			if (!_dominantLightSet._lights.empty()) {
				if (_dominantLightSet._lights.size() > 1)
					Throw(std::runtime_error("Multiple lights in the non-tiled dominant light category. There can be only one dominant light, but it can support more features than the tiled lights"));
				i->_dominantLight = Internal::MakeLightUniforms(
					*checked_cast<ForwardPlusLightDesc*>(_dominantLightSet._lights[0]._desc.get()),
					_positionalLightOperators[_dominantLightSet._operatorId]);
			}

			i->_lightCount = tilerOutputs._lightCount;
			i->_enableSSR = _ambientLight->_ambientLightOperator._ssrOperator.has_value() && lastFrameBuffersPrimed;
			std::memcpy(i->_diffuseSHCoefficients, _diffuseSHCoefficients, sizeof(_diffuseSHCoefficients));
			map.FlushCache();
		}

		if (_completionCommandListID)
			parsingContext.RequireCommandList(_completionCommandListID);
	}

	void ForwardPlusLightScene::CompleteInitialization(IThreadContext& threadContext)
	{
		_lightTiler->CompleteInitialization(threadContext);
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
				if (_lightScene->_shadowProbes && _lightScene->_shadowProbes->IsReady()) {
					dst[4] = &_lightScene->_shadowProbes->GetStaticProbesTable();
					dst[5] = &_lightScene->_shadowProbes->GetShadowProbeUniforms();
				} else {
					// We need a white dummy texture in reverseZ modes, or black in non-reverseZ modes
					assert(Techniques::GetDefaultClipSpaceType() == ClipSpaceType::Positive_ReverseZ || Techniques::GetDefaultClipSpaceType() == ClipSpaceType::PositiveRightHanded_ReverseZ);
					dst[4] = context.GetTechniqueContext()._commonResources->_whiteCubeArraySRV.get();
					dst[5] = context.GetTechniqueContext()._commonResources->_blackBufferUAV.get();
				}
			}
		}
		ForwardPlusLightScene* _lightScene = nullptr;
		ShaderResourceDelegate(ForwardPlusLightScene& lightScene)
		{
			_lightScene = &lightScene;
			BindResourceView(0, Hash64("LightDepthTable"));
			BindResourceView(1, Hash64("LightList"));
			BindResourceView(2, Hash64("TiledLightBitField"));
			BindResourceView(3, Hash64("EnvironmentProps"));
			BindResourceView(4, Hash64("StaticShadowProbeDatabase"));
			BindResourceView(5, Hash64("StaticShadowProbeProperties"));
		}
	};

	std::shared_ptr<Techniques::IShaderResourceDelegate> ForwardPlusLightScene::CreateMainSceneResourceDelegate()
	{
		return std::make_shared<ShaderResourceDelegate>(*this);
	}

	std::optional<LightSourceOperatorDesc> ForwardPlusLightScene::GetDominantLightOperator() const
	{
		if (_dominantLightSet._operatorId == ~0u)
			return {};
		return _positionalLightOperators[_dominantLightSet._operatorId];
	}

	std::optional<ShadowOperatorDesc> ForwardPlusLightScene::GetDominantShadowOperator() const
	{
		if (_dominantLightSet._shadowOperatorId == ~0u)
			return {};
		return _shadowOperators[_dominantLightSet._shadowOperatorId];
	}

	const AmbientLightOperatorDesc& ForwardPlusLightScene::GetAmbientLightOperatorDesc() const
	{
		return _ambientLight->_ambientLightOperator;
	}

	bool ForwardPlusLightScene::ShadowProbesSupported() const
	{
		// returns true if we have an operator for shadow probes, even if the shadow probe database hasn't actually been created
		return _shadowPreparerIdMapping._operatorForStaticProbes != ~0u;
	}

	static ShadowProbes::Configuration MakeShadowProbeConfiguration(const ShadowOperatorDesc& opDesc)
	{
		ShadowProbes::Configuration result;
		assert(opDesc._width == opDesc._height);		// expecting square probe textures
		result._staticFaceDims = opDesc._width;
		result._staticFormat = opDesc._format;
		result._singleSidedBias = opDesc._singleSidedBias;
		result._doubleSidedBias = opDesc._doubleSidedBias;
		return result;
	}

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

	ForwardPlusLightScene::ForwardPlusLightScene(const AmbientLightOperatorDesc& ambientLightOperator)
	{
		_ambientLight = std::make_shared<AmbientLightConfig>();
		_ambientLight->_ambientLightOperator = ambientLightOperator;

		// We'll maintain the first few ids for system lights (ambient surrounds, etc)
		ReserveLightSourceIds(32);
		std::memset(_diffuseSHCoefficients, 0, sizeof(_diffuseSHCoefficients));
	}

	std::shared_ptr<ForwardPlusLightScene> ForwardPlusLightScene::CreateInternal(
		std::shared_ptr<DynamicShadowPreparers> shadowPreparers,
		std::shared_ptr<RasterizationLightTileOperator> lightTiler, 
		const std::vector<LightSourceOperatorDesc>& positionalLightOperators,
		const std::vector<ShadowOperatorDesc>& shadowOperators,
		const AmbientLightOperatorDesc& ambientLightOperator, 
		const ForwardPlusLightScene::ShadowPreparerIdMapping& shadowPreparerMapping, 
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators, 
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox)
	{
		auto lightScene = std::make_shared<ForwardPlusLightScene>(ambientLightOperator);
		lightScene->_positionalLightOperators = std::move(positionalLightOperators);
		lightScene->_shadowOperators = std::move(shadowOperators);
		lightScene->_shadowPreparers = shadowPreparers;
		lightScene->_pipelineAccelerators = std::move(pipelineAccelerators);
		lightScene->_techDelBox = std::move(techDelBox);

		lightScene->_lightTiler = lightTiler;
		lightTiler->SetLightScene(*lightScene);

		lightScene->_shadowPreparerIdMapping = std::move(shadowPreparerMapping);

		lightScene->FinalizeConfiguration();
		return lightScene;
	}

	void ForwardPlusLightScene::ConstructToPromise(
		std::promise<std::shared_ptr<ForwardPlusLightScene>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& shadowDescSet,
		IteratorRange<const LightSourceOperatorDesc*> positionalLightOperatorsInit,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const AmbientLightOperatorDesc& ambientLightOperator,
		const RasterizationLightTileOperator::Configuration& tilerCfg)
	{
		// We need to decode all of these operator configurations so that we have the 
		// right set of things to construct

		ShadowPreparerIdMapping shadowOperatorMapping;
		shadowOperatorMapping._operatorToShadowPreparerId.resize(shadowGenerators.size(), ~0u);
		::Assets::PtrToMarkerPtr<DynamicShadowPreparers> shadowPreparationOperatorsFuture;

		// Map the shadow operator ids onto the underlying type of shadow (dynamically generated, shadow probes, etc)
		{
			ShadowOperatorDesc preparers[shadowGenerators.size()];
			unsigned dynShadowCount = 0;
			for (unsigned c=0; c<shadowGenerators.size(); ++c) {
				if (shadowGenerators[c]._resolveType == ShadowResolveType::Probe) {
					// setup shadow operator for probes
					if (shadowOperatorMapping._operatorForStaticProbes != ~0u)
						Throw(std::runtime_error("Multiple operators for shadow probes detected. Only zero or one is supported"));
					shadowOperatorMapping._operatorForStaticProbes = c;
					shadowOperatorMapping._shadowProbesCfg = MakeShadowProbeConfiguration(shadowGenerators[c]);
				} else {
					preparers[dynShadowCount] = shadowGenerators[c];
					shadowOperatorMapping._operatorToShadowPreparerId[c] = dynShadowCount;
					++dynShadowCount;
				}
			}
			shadowPreparationOperatorsFuture = CreateDynamicShadowPreparers(
				MakeIteratorRange(preparers, &preparers[dynShadowCount]),
				pipelineAccelerators, techDelBox, shadowDescSet);
		}

		auto lightTilerFuture = ::Assets::NewMarkerPtr<RasterizationLightTileOperator>(pipelinePool, tilerCfg);
		std::vector<LightSourceOperatorDesc> positionalLightOperators { positionalLightOperatorsInit.begin(), positionalLightOperatorsInit.end() };
		std::vector<ShadowOperatorDesc> shadowOperatorsDesc { shadowGenerators.begin(), shadowGenerators.end() };

		using namespace std::placeholders;
		::Assets::WhenAll(shadowPreparationOperatorsFuture, lightTilerFuture).ThenConstructToPromise(
			std::move(promise),
			std::bind(CreateInternal, _1, _2, std::move(positionalLightOperators), std::move(shadowOperatorsDesc), ambientLightOperator, std::move(shadowOperatorMapping), pipelineAccelerators, techDelBox));
	}

}}
