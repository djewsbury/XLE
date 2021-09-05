// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ForwardPlusLightScene.h"
#include "LightScene.h"
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
#include "../Assets/TextureCompiler.h"
#include "../Metal/Resource.h"
#include "../../Assets/AssetFuture.h"
#include "../../Assets/Assets.h"

namespace RenderCore { namespace LightingEngine
{
	static const uint64_t s_shadowTemplate = Utility::Hash64("ShadowTemplate");
	static const unsigned s_shadowProbeShadowFlag = 1u<<31u;

	class ForwardPlusLightDesc : public Internal::StandardLightDesc
	{
	public:
		unsigned _staticProbeDatabaseEntry = 0;

		using StandardLightDesc::StandardLightDesc; 
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

	std::vector<ShadowProbes::Probe> ForwardPlusLightScene::ShadowProbesInterface::GetPendingProbes(ForwardPlusLightScene& lightScene)
	{
		std::vector<ShadowProbes::Probe> result;
		result.reserve(_pendingProbes.size());
		for (const auto& pending:_pendingProbes) {
			ShadowProbes::Probe probe;
			probe._position = Zero<Float3>();
			probe._radius = 1024;
			auto* positional = ((ILightScene&)lightScene).TryGetLightSourceInterface<IPositionalLightSource>(pending._attachedSource);
			if (positional)
				probe._position = ExtractTranslation(positional->GetLocalToWorld());
			auto* finite = ((ILightScene&)lightScene).TryGetLightSourceInterface<IFiniteLightSource>(pending._attachedSource);
			if (finite)
				probe._radius = finite->GetCutoffRange();

			auto& internalLightDesc = *dynamic_cast<ForwardPlusLightDesc*>(positional);
			assert(internalLightDesc._staticProbeDatabaseEntry == 0);
			// we use zero as a sentinal, so add one to the actual index
			internalLightDesc._staticProbeDatabaseEntry = unsigned(result.size()+1);

			result.push_back(probe);
		}
		return result;
	}

	void ForwardPlusLightScene::FinalizeConfiguration()
	{
		auto tilerConfig = _lightTiler->GetConfiguration();
		for (unsigned c=0; c<dimof(_uniforms); c++) {
			_uniforms[c]._propertyCB = _device->CreateResource(
				CreateDesc(BindFlag::ConstantBuffer, CPUAccess::Write, 0, LinearBufferDesc::Create(sizeof(Internal::CB_EnvironmentProps)), "env-props"));
			_uniforms[c]._propertyCBView = _uniforms[c]._propertyCB->CreateBufferView(BindFlag::ConstantBuffer);

			_uniforms[c]._lightList = _device->CreateResource(
				CreateDesc(BindFlag::UnorderedAccess, CPUAccess::Write, 0, LinearBufferDesc::Create(sizeof(Internal::CB_Light)*tilerConfig._maxLightsPerView, sizeof(Internal::CB_Light)), "light-list"));
			_uniforms[c]._lightListUAV = _uniforms[c]._lightList->CreateBufferView(BindFlag::UnorderedAccess);

			_uniforms[c]._lightDepthTable = _device->CreateResource(
				CreateDesc(BindFlag::UnorderedAccess, CPUAccess::Write, 0, LinearBufferDesc::Create(sizeof(unsigned)*tilerConfig._depthLookupGradiations, sizeof(unsigned)), "light-depth-table"));
			_uniforms[c]._lightDepthTableUAV = _uniforms[c]._lightDepthTable->CreateBufferView(BindFlag::UnorderedAccess);
		}
		_pingPongCounter = 0;

		// Default to using the first light operator & first shadow operator for the dominant light
		_dominantLightOperatorId = ~0u;
		for (unsigned c=0; c<_positionalLightOperators.size(); ++c)
			if (_positionalLightOperators[c]._flags & LightSourceOperatorDesc::Flags::DominantLight) {
				if (_dominantLightOperatorId != ~0u)
					Throw(std::runtime_error("Multiple dominant light operators detected. This isn't supported -- there must be either 0 or 1"));
				_dominantLightOperatorId = c;
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
		auto desc = std::make_unique<ForwardPlusLightDesc>(Internal::StandardLightDesc::Flags::SupportFiniteRange);
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
			if (_shadowProbes._builtProbes)
				Throw(std::runtime_error("New shadow probes cannot be added after the probe database has been built"));
			_shadowProbes._pendingProbes.push_back({associatedLight});
			return s_shadowProbeShadowFlag | unsigned(_shadowProbes._pendingProbes.size());
		}
		return ~0u;
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
		LightSourceId dominantLightId = ~0u;

		auto& uniforms = _uniforms[_pingPongCounter%dimof(_uniforms)];
		auto& tilerOutputs = _lightTiler->_outputs;
		{
			Metal::ResourceMap map(
				*_device, *uniforms._lightDepthTable,
				Metal::ResourceMap::Mode::WriteDiscardPrevious, 
				0, sizeof(unsigned)*tilerOutputs._lightDepthTable.size());
			std::memcpy(map.GetData().begin(), tilerOutputs._lightDepthTable.data(), sizeof(unsigned)*tilerOutputs._lightDepthTable.size());
		}
		if (tilerOutputs._lightCount) {
			Metal::ResourceMap map(
				*_device, *uniforms._lightList,
				Metal::ResourceMap::Mode::WriteDiscardPrevious, 
				0, sizeof(Internal::CB_Light)*tilerOutputs._lightCount);
			auto* i = (Internal::CB_Light*)map.GetData().begin();
			auto end = tilerOutputs._lightOrdering.begin() + tilerOutputs._lightCount;
			for (auto idx=tilerOutputs._lightOrdering.begin(); idx!=end; ++idx, ++i) {
				auto set = *idx >> 16, light = (*idx)&0xffff;
				auto op = _lightSets[set]._operatorId;
				auto& lightDesc = *(ForwardPlusLightDesc*)_lightSets[set]._lights[light]._desc.get();
				*i = MakeLightUniforms(lightDesc, _positionalLightOperators[op]);
				i->_staticProbeDatabaseEntry = lightDesc._staticProbeDatabaseEntry;
			}
		}

		{
			Metal::ResourceMap map(
				*_device, *uniforms._propertyCB,
				Metal::ResourceMap::Mode::WriteDiscardPrevious);
			auto* i = (Internal::CB_EnvironmentProps*)map.GetData().begin();
			i->_dominantLight = {};

			if (_dominantLightOperatorId != ~0u) {
				auto dominantLightOperator = _lightSets.begin();
				for (; dominantLightOperator!=_lightSets.end(); ++dominantLightOperator)
					if (dominantLightOperator->_operatorId == _dominantLightOperatorId && !dominantLightOperator->_lights.empty())
						break;
				if (dominantLightOperator != _lightSets.end()) {
					if (dominantLightOperator->_lights.size() > 1)
						Throw(std::runtime_error("Multiple lights in the non-tiled dominant light category. There can be only one dominant light, but it can support more features than the tiled lights"));
					i->_dominantLight = Internal::MakeLightUniforms(
						*checked_cast<ForwardPlusLightDesc*>(dominantLightOperator->_lights[0]._desc.get()),
						_positionalLightOperators[_dominantLightOperatorId]);
					dominantLightId = dominantLightOperator->_lights[0]._id;
				}
			}

			i->_lightCount = tilerOutputs._lightCount;
			i->_enableSSR = lastFrameBuffersPrimed;
			std::memcpy(i->_diffuseSHCoefficients, _diffuseSHCoefficients, sizeof(_diffuseSHCoefficients));
		}

		if (_completionCommandListID)
			parsingContext.RequireCommandList(_completionCommandListID);

		if (dominantLightId != ~0u) {
			// find the prepared shadow associated with the dominant light (if it exists) and make sure it's descriptor set is accessible
			assert(!parsingContext._extraSequencerDescriptorSet.second);
			for (unsigned c=0; c<_dynamicShadowProjections.size(); ++c)
				if (_dynamicShadowProjections[c]._lightId == dominantLightId) {
					assert(_dynamicShadowProjections[c]._operatorId == 0);		// we require the shadow op used with the dominant light to be 0 currently
					parsingContext._extraSequencerDescriptorSet = {s_shadowTemplate, _preparedShadows[c].second->GetDescriptorSet().get()};
				}
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

	std::shared_ptr<IProbeRenderingInstance> ForwardPlusLightScene::PrepareStaticShadowProbes(IThreadContext& threadContext)
	{
		if (_shadowProbes._builtProbes)
			Throw(std::runtime_error("Rebuilding shadow probes after they have been built once is not supported"));
		
		auto pendingProbes = _shadowProbes.GetPendingProbes(*this); 
		if (pendingProbes.empty())
			return nullptr;

		_shadowProbes._pendingProbes.clear();
		_shadowProbes._builtProbes = true;
		return _shadowProbes._probes->PrepareStaticProbes(threadContext, MakeIteratorRange(pendingProbes));
	}

	class ForwardPlusLightScene::ShaderResourceDelegate : public Techniques::IShaderResourceDelegate
	{
	public:
		void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst) override
		{
			assert(dst.size() >= 4);
			auto& uniforms = _lightScene->_uniforms[_lightScene->_pingPongCounter%dimof(_lightScene->_uniforms)];
			dst[0] = uniforms._lightDepthTableUAV.get();
			dst[1] = uniforms._lightListUAV.get();
			dst[2] = uniforms._propertyCBView.get();
			dst[3] = _lightScene->_lightTiler->_outputs._tiledLightBitFieldSRV.get();
			if (bindingFlags & (1ull<<4ull)) {
				assert(context._rpi);
				dst[4] = context._rpi->GetNonFrameBufferAttachmentView(0).get();
			}
			dst[5] = &_lightScene->_shadowProbes._probes->GetStaticProbesTable();
		}
		ForwardPlusLightScene* _lightScene = nullptr;
		ShaderResourceDelegate(ForwardPlusLightScene& lightScene)
		{
			_lightScene = &lightScene;
			BindResourceView(0, Utility::Hash64("LightDepthTable"));
			BindResourceView(1, Utility::Hash64("LightList"));
			BindResourceView(2, Utility::Hash64("EnvironmentProps"));
			BindResourceView(3, Utility::Hash64("TiledLightBitField"));
			BindResourceView(4, Utility::Hash64("SSR"));
			BindResourceView(5, Utility::Hash64("StaticShadowProbeDatabase"));
		}
	};

	std::shared_ptr<Techniques::IShaderResourceDelegate> ForwardPlusLightScene::CreateMainSceneResourceDelegate()
	{
		return std::make_shared<ShaderResourceDelegate>(*this);
	}

	std::optional<LightSourceOperatorDesc> ForwardPlusLightScene::GetDominantLightOperator() const
	{
		if (_dominantLightOperatorId == ~0u)
			return {};
		return _positionalLightOperators[_dominantLightOperatorId];
	}

	std::optional<ShadowOperatorDesc> ForwardPlusLightScene::GetDominantShadowOperator() const
	{
		if (_shadowPreparationOperators->_operators.empty())
			return {};
		// assume the shadow operator that will be associated is index 0
		return _shadowPreparationOperators->_operators[0]._desc;
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
		const std::shared_ptr<Techniques::PipelinePool>& pipelinePool,
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
					shadowOperatorMapping._shadowProbesCfg._staticFaceDims = shadowGenerators[c]._width;
					shadowOperatorMapping._shadowProbesCfg._staticFormat = shadowGenerators[c]._format;
				} else {
					dynShadowGens[dynShadowCount] = shadowGenerators[c];
					shadowOperatorMapping._operatorToDynamicShadowOperator[c] = dynShadowCount;
					++dynShadowCount;
				}
			}
			shadowPreparationOperatorsFuture = CreateDynamicShadowPreparationOperators(
				MakeIteratorRange(dynShadowGens, &dynShadowGens[shadowGenerators.size()]),
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
				lightScene->_device = pipelineAccelerators->GetDevice();
				lightScene->_ssrOperator = ssr;
				lightScene->_hierarchicalDepthsOperator = hierarchicalDepthsOperator;

				lightScene->_lightTiler = lightTiler;
				lightTiler->SetLightScene(*lightScene);

				lightScene->_shadowOperatorIdMapping = std::move(shadowOperatorMapping);
				if (lightScene->_shadowOperatorIdMapping._operatorForStaticProbes != ~0u) {
					lightScene->_shadowProbes._probes = std::make_shared<ShadowProbes>(
						pipelineAccelerators, *techDelBox, lightScene->_shadowOperatorIdMapping._shadowProbesCfg);
				}

				lightScene->FinalizeConfiguration();
				return lightScene;
			});

	}

}}
