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
#include "../../Assets/AssetFuture.h"
#include "../../Assets/Assets.h"

namespace RenderCore { namespace LightingEngine
{
	static const uint64_t s_shadowTemplate = Utility::Hash64("ShadowTemplate");
	static const unsigned s_shadowProbeShadowFlag = 1u<<31u;

	class ForwardPlusLightDesc : public Internal::StandardPositionalLight
	{
	public:
		unsigned _staticProbeDatabaseEntry = 0;

		using StandardPositionalLight::StandardPositionalLight; 
	};

	class ForwardPlusLightScene::AmbientLightConfig
	{
	public:
		::Assets::PtrToFuturePtr<Techniques::DeferredShaderResource> _specularIBL;
		::Assets::PtrToFuturePtr<Techniques::DeferredShaderResource> _ambientRawCubemap;
		::Assets::PtrToFuturePtr<SHCoefficientsAsset> _diffuseIBL;

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
			_specularIBL = ::Assets::MakeFuture<std::shared_ptr<Techniques::DeferredShaderResource>>(request);

			Assets::TextureCompilationRequest request2;
			request2._operation = Assets::TextureCompilationRequest::Operation::EquRectToCubeMap; 
			request2._srcFile = _sourceImage;
			request2._format = Format::BC6H_UF16;
			request2._faceDim = 1024;
			request2._mipMapFilter = Assets::TextureCompilationRequest::MipMapFilter::FromSource;
			_ambientRawCubemap = ::Assets::MakeFuture<std::shared_ptr<Techniques::DeferredShaderResource>>(request2);
		}
	};

	void ForwardPlusLightScene::FinalizeConfiguration()
	{
		auto& device = *_pipelineAccelerators->GetDevice();
		auto tilerConfig = _lightTiler->GetConfiguration();
		for (unsigned c=0; c<dimof(_uniforms); c++) {
			_uniforms[c]._propertyCB = device.CreateResource(
				CreateDesc(BindFlag::ConstantBuffer, CPUAccess::Write, 0, LinearBufferDesc::Create(sizeof(Internal::CB_EnvironmentProps)), "env-props"));
			_uniforms[c]._propertyCBView = _uniforms[c]._propertyCB->CreateBufferView(BindFlag::ConstantBuffer);

			_uniforms[c]._lightList = device.CreateResource(
				CreateDesc(BindFlag::UnorderedAccess, CPUAccess::Write, 0, LinearBufferDesc::Create(sizeof(Internal::CB_Light)*tilerConfig._maxLightsPerView, sizeof(Internal::CB_Light)), "light-list"));
			_uniforms[c]._lightListUAV = _uniforms[c]._lightList->CreateBufferView(BindFlag::UnorderedAccess);

			_uniforms[c]._lightDepthTable = device.CreateResource(
				CreateDesc(BindFlag::UnorderedAccess, CPUAccess::Write, 0, LinearBufferDesc::Create(sizeof(unsigned)*tilerConfig._depthLookupGradiations, sizeof(unsigned)), "light-depth-table"));
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
		for (unsigned c=0; c<_shadowOperatorIdMapping._operatorToDynamicShadowOperator.size(); ++c) {
			if (_shadowOperatorIdMapping._operatorToDynamicShadowOperator[c] == ~0u) continue;
			if (_shadowPreparationOperators->_operators[_shadowOperatorIdMapping._operatorToDynamicShadowOperator[c]]._desc._dominantLight) {
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
		auto dynIdx = _shadowOperatorIdMapping._operatorToDynamicShadowOperator[opId];
		if (dynIdx != ~0u) {
			auto desc = _shadowPreparationOperators->CreateShadowProjection(dynIdx);
			return AddShadowProjection(opId, associatedLight, std::move(desc));
		} else if (opId == _shadowOperatorIdMapping._operatorForStaticProbes) {
			Throw(std::runtime_error("Use the multi-light shadow projection constructor for shadow probes"));
		}
		return ~0u;
	}

	class ForwardPlusLightScene::ShadowProbePrepareDelegate : public IPreparable, public IShadowProbeDatabase
	{
	public:
		static std::vector<ShadowProbes::Probe> MakeProbes(ILightScene& lightScene, IteratorRange<const ILightScene::LightSourceId*> lights, float defaultNearRadius)
		{
			std::vector<ShadowProbes::Probe> result;
			result.reserve(lights.size());
			for (auto pending:lights) {
				ShadowProbes::Probe probe;
				probe._position = Zero<Float3>();
				probe._nearRadius = 1.f;
				probe._farRadius = 1024.f;
				float lightSourceRadius = 0.f;
				auto* positional = lightScene.TryGetLightSourceInterface<IPositionalLightSource>(pending);
				if (positional) {
					probe._position = ExtractTranslation(positional->GetLocalToWorld());
					lightSourceRadius = ExtractUniformScaleFast(AsFloat3x4(positional->GetLocalToWorld()));
				}
				auto* finite = lightScene.TryGetLightSourceInterface<IFiniteLightSource>(pending);
				if (finite) {
					probe._nearRadius = std::max(lightSourceRadius, defaultNearRadius);
					probe._farRadius = finite->GetCutoffRange();
				}

				auto& internalLightDesc = *dynamic_cast<ForwardPlusLightDesc*>(positional);
				assert(internalLightDesc._staticProbeDatabaseEntry == 0);
				// we use zero as a sentinal, so add one to the actual index
				internalLightDesc._staticProbeDatabaseEntry = unsigned(result.size()+1);

				result.push_back(probe);
			}
			return result;
		}

		std::shared_ptr<IProbeRenderingInstance> BeginPrepare(IThreadContext& threadContext) override
		{
			auto probes = MakeProbes(*_lightScene, _associatedLights, _defaultNearRadius);
			_shadowProbes->AddProbes(probes);
			return _shadowProbes->PrepareStaticProbes(threadContext);
		}

		void SetNearRadius(float nearRadius) override { _defaultNearRadius = nearRadius; }
		float GetNearRadius(float) override { return _defaultNearRadius; }

		std::shared_ptr<ShadowProbes> _shadowProbes;
		ShadowProbePrepareDelegate(std::shared_ptr<ShadowProbes> shadowProbes, IteratorRange<const LightSourceId*> associatedLights, ForwardPlusLightScene* lightScene) 
		: _shadowProbes(std::move(shadowProbes)), _associatedLights(associatedLights.begin(), associatedLights.end()), _lightScene(lightScene) {}
		std::vector<LightSourceId> _associatedLights;
		ForwardPlusLightScene* _lightScene;
		float _defaultNearRadius = 1.f;
	};

	ILightScene::ShadowProjectionId ForwardPlusLightScene::CreateShadowProjection(ShadowOperatorId opId, IteratorRange<const LightSourceId*> associatedLights)
	{
		if (opId == _shadowOperatorIdMapping._operatorForStaticProbes) {
			if (_shadowProbes)
				Throw(std::runtime_error("Cannot create multiple shadow probe databases in on light scene."));
			
			_shadowProbes = std::make_shared<ShadowProbes>(
				_pipelineAccelerators, *_techDelBox, _shadowOperatorIdMapping._shadowProbesCfg);
			_spPrepareDelegate = std::make_shared<ShadowProbePrepareDelegate>(_shadowProbes, associatedLights, this);
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
			else if (interfaceTypeCode == typeid(IShadowProbeDatabase).hash_code()) return (IShadowProbeDatabase*)_spPrepareDelegate.get();
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
				if (specularIBLFuture->GetAssetState() != ::Assets::AssetState::Ready
					|| diffuseIBLFuture->GetAssetState() != ::Assets::AssetState::Ready) {
					l->_ssrOperator->SetSpecularIBL(nullptr);
					l->_onChangeSkyTexture(nullptr);
					std::memset(l->_diffuseSHCoefficients, 0, sizeof(l->_diffuseSHCoefficients));
				} else {
					auto ambientRawCubemap = ambientRawCubemapFuture->Actualize();
					{
						TextureViewDesc adjustedViewDesc;
						adjustedViewDesc._mipRange._min = 2;
						auto adjustedView = ambientRawCubemap->GetShaderResource()->GetResource()->CreateTextureView(BindFlag::ShaderResource, adjustedViewDesc);
						l->_ssrOperator->SetSpecularIBL(adjustedView);
					}
					auto actualDiffuse = diffuseIBLFuture->Actualize();
					std::memset(l->_diffuseSHCoefficients, 0, sizeof(l->_diffuseSHCoefficients));
					std::memcpy(l->_diffuseSHCoefficients, actualDiffuse->GetCoefficients().begin(), sizeof(Float4*)*std::min(actualDiffuse->GetCoefficients().size(), dimof(l->_diffuseSHCoefficients)));
					l->_completionCommandListID = std::max(l->_completionCommandListID, ambientRawCubemap->GetCompletionCommandList());
					l->_onChangeSkyTexture(ambientRawCubemap);
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
				i->_staticProbeDatabaseEntry = lightDesc._staticProbeDatabaseEntry;
			}
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
			i->_enableSSR = lastFrameBuffersPrimed;
			std::memcpy(i->_diffuseSHCoefficients, _diffuseSHCoefficients, sizeof(_diffuseSHCoefficients));
		}

		if (_completionCommandListID)
			parsingContext.RequireCommandList(_completionCommandListID);

		if (_preparedDominantShadow) {
			// find the prepared shadow associated with the dominant light (if it exists) and make sure it's descriptor set is accessible
			assert(!parsingContext._extraSequencerDescriptorSet.second);
			parsingContext._extraSequencerDescriptorSet = {s_shadowTemplate, _preparedDominantShadow->GetDescriptorSet().get()};
		}
	}

	void ForwardPlusLightScene::SetupProjection(Techniques::ParsingContext& parsingContext)
	{
		if (_hasPrevProjection) {
			parsingContext.GetPrevProjectionDesc() = _prevProjDesc;
			parsingContext.GetEnablePrevProjectionDesc() = true;
		}
		_prevProjDesc = parsingContext.GetProjectionDesc();
		_hasPrevProjection = true;
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
				assert(context._rpi);
				dst[4] = context._rpi->GetNonFrameBufferAttachmentView(0).get();
			}

			if (bindingFlags & (1ull<<5ull)) {
				// assert(bindingFlags & (1ull<<6ull));
				if (_lightScene->_shadowProbes && _lightScene->_shadowProbes->IsReady()) {
					dst[5] = &_lightScene->_shadowProbes->GetStaticProbesTable();
					dst[6] = &_lightScene->_shadowProbes->GetShadowProbeUniforms();
				} else {
					// We need a white dummy texture in reverseZ modes, or black in non-reverseZ modes
					assert(Techniques::GetDefaultClipSpaceType() == ClipSpaceType::Positive_ReverseZ || Techniques::GetDefaultClipSpaceType() == ClipSpaceType::PositiveRightHanded_ReverseZ);
					dst[5] = context.GetTechniqueContext()._commonResources->_whiteCubeArraySRV.get();
					dst[6] = context.GetTechniqueContext()._commonResources->_blackBufferUAV.get();
				}
			}
			if (bindingFlags & (1ull<<7ull)) {
				dst[7] = _noise.get();
				context.RequireCommandList(_completionCmdList);
			}
		}
		ForwardPlusLightScene* _lightScene = nullptr;
		ShaderResourceDelegate(ForwardPlusLightScene& lightScene, Techniques::DeferredShaderResource& balanceNoiseTexture)
		{
			_lightScene = &lightScene;
			BindResourceView(0, Hash64("LightDepthTable"));
			BindResourceView(1, Hash64("LightList"));
			BindResourceView(2, Hash64("TiledLightBitField"));
			BindResourceView(3, Hash64("EnvironmentProps"));
			BindResourceView(4, Hash64("SSR"));
			BindResourceView(5, Hash64("StaticShadowProbeDatabase"));
			BindResourceView(6, Hash64("StaticShadowProbeProperties"));
			BindResourceView(7, Hash64("NoiseTexture"));

			_noise = balanceNoiseTexture.GetShaderResource();
			_completionCmdList = balanceNoiseTexture.GetCompletionCommandList();
		}

		std::shared_ptr<IResourceView> _noise;
		BufferUploads::CommandListID _completionCmdList;
	};

	std::shared_ptr<Techniques::IShaderResourceDelegate> ForwardPlusLightScene::CreateMainSceneResourceDelegate(Techniques::DeferredShaderResource& balanceNoiseTexture)
	{
		return std::make_shared<ShaderResourceDelegate>(*this, balanceNoiseTexture);
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
		return _shadowPreparationOperators->_operators[_shadowOperatorIdMapping._operatorToDynamicShadowOperator[_dominantLightSet._shadowOperatorId]]._desc;
	}

	static ShadowProbes::Configuration MakeShadowProbeConfiguration(const ShadowOperatorDesc& opDesc)
	{
		ShadowProbes::Configuration result;
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
		if (_shadowOperatorIdMapping._operatorToDynamicShadowOperator.size() != shadowGenerators.size()) return false;
		if (_positionalLightOperators.size() != resolveOperators.size()) return false;

		for (unsigned c=0; c<_shadowOperatorIdMapping._operatorToDynamicShadowOperator.size(); ++c) {
			auto dynShadowOp = _shadowOperatorIdMapping._operatorToDynamicShadowOperator[c];
			if (dynShadowOp == ~0u) continue;
			if (c >= shadowGenerators.size()) return false;
			if (_shadowPreparationOperators->_operators[dynShadowOp]._desc.Hash() != shadowGenerators[c].Hash()) return false;
		}
		if (_shadowOperatorIdMapping._operatorForStaticProbes != ~0u) {
			if (_shadowOperatorIdMapping._operatorForStaticProbes >= shadowGenerators.size()) return false;
			auto cfg = MakeShadowProbeConfiguration(shadowGenerators[_shadowOperatorIdMapping._operatorForStaticProbes]);
			if (!(cfg == _shadowOperatorIdMapping._shadowProbesCfg)) return false;
		}
		for (unsigned c=0; c<_positionalLightOperators.size(); ++c) {
			if (resolveOperators[c].Hash() != _positionalLightOperators[c].Hash())
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

	void ForwardPlusLightScene::ConstructToFuture(
		::Assets::FuturePtr<ForwardPlusLightScene>& future,
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

		ShadowOperatorIdMapping shadowOperatorMapping;
		shadowOperatorMapping._operatorToDynamicShadowOperator.resize(shadowGenerators.size(), ~0u);
		::Assets::PtrToFuturePtr<DynamicShadowPreparationOperators> shadowPreparationOperatorsFuture;

		// Map the shadow operator ids onto the underlying type of shadow (dynamically generated, shadow probes, etc)
		{
			ShadowOperatorDesc dynShadowGens[shadowGenerators.size()];
			unsigned dynShadowCount = 0;
			for (unsigned c=0; c<shadowGenerators.size(); ++c) {
				if (shadowGenerators[c]._resolveType == ShadowResolveType::Probe) {
					// setup shadow operator for probes
					if (shadowOperatorMapping._operatorForStaticProbes != ~0u)
						Throw(std::runtime_error("Multiple operators for shadow probes detected. Only zero or one is supported"));
					shadowOperatorMapping._operatorForStaticProbes = c;
					shadowOperatorMapping._shadowProbesCfg = MakeShadowProbeConfiguration(shadowGenerators[c]);
				} else {
					dynShadowGens[dynShadowCount] = shadowGenerators[c];
					shadowOperatorMapping._operatorToDynamicShadowOperator[c] = dynShadowCount;
					++dynShadowCount;
				}
			}
			shadowPreparationOperatorsFuture = CreateDynamicShadowPreparationOperators(
				MakeIteratorRange(dynShadowGens, &dynShadowGens[dynShadowCount]),
				pipelineAccelerators, techDelBox, shadowDescSet);
		}

		auto hierarchicalDepthsOperatorFuture = ::Assets::MakeFuture<std::shared_ptr<HierarchicalDepthsOperator>>(pipelinePool);
		auto lightTilerFuture = ::Assets::MakeFuture<std::shared_ptr<RasterizationLightTileOperator>>(pipelinePool, tilerCfg);
		auto ssrFuture = ::Assets::MakeFuture<std::shared_ptr<ScreenSpaceReflectionsOperator>>(pipelinePool);

		std::vector<LightSourceOperatorDesc> positionalLightOperators { positionalLightOperatorsInit.begin(), positionalLightOperatorsInit.end() };
		::Assets::WhenAll(shadowPreparationOperatorsFuture, hierarchicalDepthsOperatorFuture, lightTilerFuture, ssrFuture).ThenConstructToFuture(
			future,
			[positionalLightOperators, ambientLightOperator, shadowOperatorMapping=std::move(shadowOperatorMapping), pipelineAccelerators, techDelBox]
			(auto shadowPreparationOperators, auto hierarchicalDepthsOperator, auto lightTiler, auto ssr) {

				auto lightScene = std::make_shared<ForwardPlusLightScene>(ambientLightOperator);
				lightScene->_positionalLightOperators = std::move(positionalLightOperators);
				lightScene->_shadowPreparationOperators = shadowPreparationOperators;
				lightScene->_ssrOperator = ssr;
				lightScene->_hierarchicalDepthsOperator = hierarchicalDepthsOperator;
				lightScene->_pipelineAccelerators = std::move(pipelineAccelerators);
				lightScene->_techDelBox = std::move(techDelBox);

				lightScene->_lightTiler = lightTiler;
				lightTiler->SetLightScene(*lightScene);

				lightScene->_shadowOperatorIdMapping = std::move(shadowOperatorMapping);

				lightScene->FinalizeConfiguration();
				return lightScene;
			});

	}

}}
