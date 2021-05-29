// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShadowPreparer.h"
#include "ShadowUniforms.h"
#include "RenderStepFragments.h"
#include "LightingEngineApparatus.h"
#include "../Techniques/PipelineAccelerator.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/TechniqueDelegates.h"
#include "../Techniques/RenderStateResolver.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/CommonBindings.h"
#include "../Metal/DeviceContext.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../IDevice.h"
#include "../IThreadContext.h"
#include "../../Assets/AssetFuture.h"
#include <vector>

namespace RenderCore { namespace LightingEngine
{
	class PreparedShadowResult : public IPreparedShadowResult
	{
	public:
		std::shared_ptr<IDescriptorSet> _descriptorSet;
		ILightScene::ShadowOperatorId _operatorId = ~0u;
		virtual const std::shared_ptr<IDescriptorSet>& GetDescriptorSet() const override { return _descriptorSet; }
		virtual ILightScene::ShadowOperatorId GetShadowOperatorId() const override { return _operatorId; }
		virtual ~PreparedShadowResult() {}
	};

	IPreparedShadowResult::~IPreparedShadowResult() {}

	class DMShadowPreparer : public ICompiledShadowPreparer
	{
	public:
		Techniques::RenderPassInstance Begin(
			IThreadContext& threadContext, 
			Techniques::ParsingContext& parsingContext,
			ILightBase& projection,
			Techniques::FrameBufferPool& shadowGenFrameBufferPool,
			Techniques::AttachmentPool& shadowGenAttachmentPool) override;

		void End(
			IThreadContext& threadContext, 
			Techniques::ParsingContext& parsingContext,
			Techniques::RenderPassInstance& rpi,
			IPreparedShadowResult& res) override;

		std::pair<std::shared_ptr<Techniques::SequencerConfig>, std::shared_ptr<Techniques::IShaderResourceDelegate>> GetSequencerConfig() override;
		std::shared_ptr<IPreparedShadowResult> CreatePreparedShadowResult() override;

		DMShadowPreparer(
			const ShadowOperatorDesc& desc,
			ILightScene::ShadowOperatorId operatorId,
			const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
			const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox,
			const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout);
		~DMShadowPreparer();

	private:
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;

		Techniques::FragmentStitchingContext::StitchResult _fbDesc;
		std::shared_ptr<Techniques::SequencerConfig> _sequencerConfigs;
		std::shared_ptr<Techniques::IShaderResourceDelegate> _uniformDelegate;

		Techniques::ProjectionDesc _savedProjectionDesc;

		Internal::PreparedDMShadowFrustum _workingDMFrustum;

		DescriptorSetSignature _descSetSig;
		std::vector<DescriptorSetInitializer::BindTypeAndIdx> _descSetSlotBindings;
		float _shadowTextureSize = 0.f;
		unsigned _maxFrustumCount = 0;
		ILightScene::ShadowOperatorId _operatorId;

		class UniformDelegate : public Techniques::IShaderResourceDelegate
		{
		public:
			virtual const UniformsStreamInterface& GetInterface() override { return _interface; }
			void WriteImmediateData(Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
			{
				switch (idx) {
				case 0:
					assert(_preparer->_workingDMFrustum._cbSource.size() == dst.size());
					std::memcpy(dst.begin(), _preparer->_workingDMFrustum._cbSource.begin(), dst.size());
					break;
				default:
					assert(0);
					break;
				}
			}

			size_t GetImmediateDataSize(Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override
			{
				switch (idx) {
				case 0: return _preparer->_workingDMFrustum._cbSource.size();
				default:
					assert(0);
					return 0;
				}
			}
		
			UniformDelegate(DMShadowPreparer& preparer) : _preparer(&preparer)
			{
				_interface.BindImmediateData(0, Utility::Hash64("ShadowProjection"), {});
			}
			UniformsStreamInterface _interface;
			DMShadowPreparer* _preparer;
		};
	};

	ICompiledShadowPreparer::~ICompiledShadowPreparer() {}

	static Internal::PreparedDMShadowFrustum SetupPreparedDMShadowFrustum(
		ILightBase& projectionBase, float shadowTextureSize, unsigned operatorMaxFrustumCount)
	{
		assert(projectionBase.QueryInterface(typeid(Internal::ShadowProjectionDesc).hash_code()) == &projectionBase);
		auto& projection = *(Internal::ShadowProjectionDesc*)&projectionBase;
		auto projectionCount = std::min(projection._projections.Count(), Internal::MaxShadowTexturesPerLight);
		if (!projectionCount)
			return Internal::PreparedDMShadowFrustum{};

		Internal::PreparedDMShadowFrustum preparedResult;
		preparedResult.InitialiseConstants(projection._projections, operatorMaxFrustumCount);
		preparedResult._resolveParameters._worldSpaceBias = projection._worldSpaceResolveBias;
		preparedResult._resolveParameters._tanBlurAngle = projection._tanBlurAngle;
		preparedResult._resolveParameters._minBlurSearchNorm = projection._minBlurSearchPixels / shadowTextureSize;
		preparedResult._resolveParameters._maxBlurSearchNorm = projection._maxBlurSearchPixels / shadowTextureSize;
		preparedResult._resolveParameters._shadowTextureSize = shadowTextureSize;
		preparedResult._resolveParameters._casterLookupExtraBias = projection._casterLookupExtraBias;
		XlZeroMemory(preparedResult._resolveParameters._dummy);

		return preparedResult;
	}

	Techniques::RenderPassInstance DMShadowPreparer::Begin(
		IThreadContext& threadContext, 
		Techniques::ParsingContext& parsingContext,
		ILightBase& projectionBase,
		Techniques::FrameBufferPool& shadowGenFrameBufferPool,
		Techniques::AttachmentPool& shadowGenAttachmentPool)
	{
		assert(projectionBase.QueryInterface(typeid(Internal::ShadowProjectionDesc).hash_code()) == &projectionBase);
		auto& projection = *(Internal::ShadowProjectionDesc*)&projectionBase;
		_workingDMFrustum = SetupPreparedDMShadowFrustum(projection, _shadowTextureSize, _maxFrustumCount);
		assert(_workingDMFrustum.IsReady());
		assert(!_fbDesc._fbDesc.GetSubpasses().empty());
		_savedProjectionDesc = parsingContext.GetProjectionDesc();
		parsingContext.GetProjectionDesc()._worldToProjection = projection._worldToClip;
		return Techniques::RenderPassInstance{
			threadContext,
			_fbDesc._fbDesc, _fbDesc._fullAttachmentDescriptions,
			shadowGenFrameBufferPool, shadowGenAttachmentPool, {}};
	}

	void DMShadowPreparer::End(
		IThreadContext& threadContext, 
		Techniques::ParsingContext& parsingContext,
		Techniques::RenderPassInstance& rpi,
		IPreparedShadowResult& res)
	{
		/*
		if (lightingParserContext._preparedDMShadows.size() == Tweakable("ShadowGenDebugging", 0)) {
			auto srvForDebugging = *rpi.GetRenderPassInstance().GetDepthStencilAttachmentSRV(TextureViewDesc{TextureViewDesc::Aspect::ColorLinear});
			parsingContext._pendingOverlays.push_back(
				std::bind(
					&ShadowGen_DrawDebugging, 
					std::placeholders::_1, std::placeholders::_2,
					srvForDebugging));
		}

		if (lightingParserContext._preparedDMShadows.size() == Tweakable("ShadowGenFrustumDebugging", 0)) {
			parsingContext._pendingOverlays.push_back(
				std::bind(
					&ShadowGen_DrawShadowFrustums, 
					std::placeholders::_1, std::placeholders::_2,
					lightingParserContext.GetMainTargets(),
					shadowDelegate._shadowProj));
		}
		*/

		auto& device = *threadContext.GetDevice();
		DescriptorSetInitializer descSetInit;
		descSetInit._signature = &_descSetSig;
		descSetInit._slotBindings = _descSetSlotBindings;
		const IResourceView* srvs[] = { rpi.GetDepthStencilAttachmentSRV({}) };
		IteratorRange<const void*> immediateData[3];
		immediateData[0] = {_workingDMFrustum._cbSource.begin(), _workingDMFrustum._cbSource.end()};
		immediateData[1] = MakeOpaqueIteratorRange(_workingDMFrustum._resolveParameters);
		auto screenToShadow = Internal::BuildScreenToShadowProjection(
			_workingDMFrustum._mode,
			_workingDMFrustum._frustumCount,
			_workingDMFrustum._cbSource,
			_savedProjectionDesc._cameraToWorld,
			_savedProjectionDesc._cameraToProjection);
		immediateData[2] = {screenToShadow.begin(), screenToShadow.end()};
		descSetInit._bindItems._resourceViews = MakeIteratorRange(srvs);
		descSetInit._bindItems._immediateData = MakeIteratorRange(immediateData);
		auto descSet = device.CreateDescriptorSet(descSetInit);
		checked_cast<PreparedShadowResult*>(&res)->_descriptorSet = std::move(descSet);

		parsingContext.GetProjectionDesc() = _savedProjectionDesc;
	}

	std::pair<std::shared_ptr<Techniques::SequencerConfig>, std::shared_ptr<Techniques::IShaderResourceDelegate>> DMShadowPreparer::GetSequencerConfig()
	{
		return std::make_pair(_sequencerConfigs, _uniformDelegate);
	}

	std::shared_ptr<IPreparedShadowResult> DMShadowPreparer::CreatePreparedShadowResult()
	{
		auto result = std::make_shared<PreparedShadowResult>();
		result->_operatorId = _operatorId;
		return result;
	}

	static const auto s_shadowCascadeModeString = "SHADOW_CASCADE_MODE";
    static const auto s_shadowEnableNearCascadeString = "SHADOW_ENABLE_NEAR_CASCADE";
	static const auto s_shadowSubProjectionCountString = "SHADOW_SUB_PROJECTION_COUNT";

	DMShadowPreparer::DMShadowPreparer(
		const ShadowOperatorDesc& desc,
		ILightScene::ShadowOperatorId operatorId,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout)
	: _pipelineAccelerators(pipelineAccelerators)
	, _operatorId(operatorId)
	{
		assert(desc._resolveType == ShadowResolveType::DepthTexture);

		unsigned arrayCount = 0u;
		if (desc._projectionMode != ShadowProjectionMode::ArbitraryCubeMap)
			arrayCount = desc._normalProjCount + (desc._enableNearCascade ? 1 : 0);

		Techniques::RSDepthBias singleSidedBias {
			desc._rasterDepthBias, desc._depthBiasClamp, desc._slopeScaledBias };
		Techniques::RSDepthBias doubleSidedBias {
			desc._dsRasterDepthBias, desc._dsDepthBiasClamp, desc._dsSlopeScaledBias };
		auto shadowGenDelegate = delegatesBox->GetShadowGenTechniqueDelegate(singleSidedBias, doubleSidedBias, desc._cullMode);

		ParameterBox sequencerSelectors;
		if (desc._projectionMode == ShadowProjectionMode::Ortho) {
			sequencerSelectors.SetParameter(s_shadowCascadeModeString, 2);
		} else if (desc._projectionMode == ShadowProjectionMode::ArbitraryCubeMap) {
			sequencerSelectors.SetParameter(s_shadowCascadeModeString, 3);
		} else {
			assert(desc._projectionMode == ShadowProjectionMode::Arbitrary);
			sequencerSelectors.SetParameter(s_shadowCascadeModeString, 1);
		}
		sequencerSelectors.SetParameter(s_shadowEnableNearCascadeString, desc._enableNearCascade?1:0);
		sequencerSelectors.SetParameter(s_shadowSubProjectionCountString, desc._normalProjCount);

		///////////////////////////////
		Techniques::FrameBufferDescFragment fragment;
		{
			SubpassDesc subpass;
			subpass.SetDepthStencil(
				fragment.DefineAttachment(
					Techniques::AttachmentSemantics::ShadowDepthMap, 
					desc._width, desc._height, arrayCount,
					AttachmentDesc{desc._format, 0, LoadStore::Clear, LoadStore::Retain, 0, BindFlag::ShaderResource | BindFlag::DepthStencil}));
			fragment.AddSubpass(std::move(subpass));
		}
		///////////////////////////////
		
		Techniques::FragmentStitchingContext stitchingContext;
		
		// Create a preregistered attachmentso we can specify a full resource desc
		// for the shadow texture. This helps distinquish between drawing to a cubemap
		// vs drawing to texture array
		Techniques::PreregisteredAttachment pregAttach;
		pregAttach._semantic = Techniques::AttachmentSemantics::ShadowDepthMap;
		pregAttach._layoutFlags = BindFlag::ShaderResource | BindFlag::DepthStencil;
		pregAttach._state = Techniques::PreregisteredAttachment::State::Uninitialized;
		if (desc._projectionMode == ShadowProjectionMode::ArbitraryCubeMap) {
			pregAttach._desc = CreateDesc(
				BindFlag::ShaderResource | BindFlag::DepthStencil, 0, GPUAccess::Read|GPUAccess::Write, 
				TextureDesc::PlainCube(desc._width, desc._height, desc._format),
				"shadow-map-cube");
		} else {
			pregAttach._desc = CreateDesc(
				BindFlag::ShaderResource | BindFlag::DepthStencil, 0, GPUAccess::Read|GPUAccess::Write, 
				TextureDesc::Plain2D(desc._width, desc._height, desc._format, 1, arrayCount),
				"shadow-map");
		}
		stitchingContext.DefineAttachment(pregAttach);

		stitchingContext._workingProps = FrameBufferProperties { desc._width, desc._height };
		_fbDesc = stitchingContext.TryStitchFrameBufferDesc(fragment);

		_sequencerConfigs = pipelineAccelerators->CreateSequencerConfig(
			shadowGenDelegate,
			sequencerSelectors,
			_fbDesc._fbDesc,
			0);
		_uniformDelegate = std::make_shared<UniformDelegate>(*this);

		if (descSetLayout) {
			_descSetSig = descSetLayout->MakeDescriptorSetSignature();
			_descSetSlotBindings.reserve(descSetLayout->_slots.size());
			for (const auto& s:descSetLayout->_slots) {
				if (s._name == "DMShadow") {
					_descSetSlotBindings.push_back({DescriptorSetInitializer::BindType::ResourceView, 0});
				} else if (s._name == "ShadowProjection") {
					_descSetSlotBindings.push_back({DescriptorSetInitializer::BindType::ImmediateData, 0});
				} else if (s._name == "ShadowResolveParameters") {
					_descSetSlotBindings.push_back({DescriptorSetInitializer::BindType::ImmediateData, 1});
				} else if (s._name == "ScreenToShadowProjection") {
					_descSetSlotBindings.push_back({DescriptorSetInitializer::BindType::ImmediateData, 2});
				} else 
					_descSetSlotBindings.push_back({});
			}
		}

		_shadowTextureSize = (float)std::min(desc._width, desc._height);
		_maxFrustumCount = desc._normalProjCount;
	}

	DMShadowPreparer::~DMShadowPreparer() {}

	std::unique_ptr<ILightBase> ShadowPreparationOperators::CreateShadowProjection(ILightScene::ShadowOperatorId opId)
	{
		assert(opId <= _operators.size());
		auto result = std::make_unique<Internal::ShadowProjectionDesc>();
		result->_projections._mode = _operators[opId]._desc._projectionMode;
		result->_projections._useNearProj = _operators[opId]._desc._enableNearCascade;
		result->_projections._normalProjCount = _operators[opId]._desc._normalProjCount;
		return result;
	}

	::Assets::FuturePtr<ICompiledShadowPreparer> CreateCompiledShadowPreparer(
		const ShadowOperatorDesc& desc,
		ILightScene::ShadowOperatorId operatorId,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout)
	{
		auto result = std::make_shared<::Assets::AssetFuture<ICompiledShadowPreparer>>();
		result->SetAsset(std::make_shared<DMShadowPreparer>(desc, operatorId, pipelineAccelerators, delegatesBox, descSetLayout), nullptr);
		return result;
	}

	::Assets::FuturePtr<ShadowPreparationOperators> CreateShadowPreparationOperators(
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators, 
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout)
	{
		auto result = std::make_shared<::Assets::AssetFuture<ShadowPreparationOperators>>();
		if (shadowGenerators.empty()) {
			result->SetAsset(std::make_shared<ShadowPreparationOperators>(), {});
			return result;
		}

		using PreparerFuture = ::Assets::FuturePtr<ICompiledShadowPreparer>;
		std::vector<PreparerFuture> futures;
		futures.reserve(shadowGenerators.size());
		for (unsigned operatorId=0; operatorId<shadowGenerators.size(); ++operatorId)
			futures.push_back(CreateCompiledShadowPreparer(shadowGenerators[operatorId], operatorId, pipelineAccelerators, delegatesBox, descSetLayout));

		std::vector<ShadowOperatorDesc> shadowGeneratorCopy { shadowGenerators.begin(), shadowGenerators.end() };
		result->SetPollingFunction(
			[futures=std::move(futures),shadowGeneratorCopy=std::move(shadowGeneratorCopy)](::Assets::AssetFuture<ShadowPreparationOperators>& future) -> bool {
				using namespace ::Assets;
				std::vector<std::shared_ptr<ICompiledShadowPreparer>> actualized;
				actualized.resize(futures.size());
				auto a=actualized.begin();
				for (const auto& p:futures) {
					Blob queriedLog;
					DependencyValidation queriedDepVal;
					auto state = p->CheckStatusBkgrnd(*a, queriedDepVal, queriedLog);
					if (state != AssetState::Ready) {
						if (state == AssetState::Invalid) {
							future.SetInvalidAsset(queriedDepVal, queriedLog);
							return false;
						} else 
							return true;
					}
					++a;
				}

				auto finalResult = std::make_shared<ShadowPreparationOperators>();
				finalResult->_operators.reserve(actualized.size());
				assert(actualized.size() == shadowGeneratorCopy.size());
				auto i = shadowGeneratorCopy.begin();
				for (auto& a:actualized)
					finalResult->_operators.push_back({std::move(a), *i++});

				future.SetAsset(std::move(finalResult), nullptr);
				return false;
			});
		return result;
	}

	template<int BitCount, typename Input>
		static uint64_t GetBits(Input i)
	{
		auto mask = (1ull<<uint64_t(BitCount))-1ull;
		assert((uint64_t(i) & ~mask) == 0);
		return uint64_t(i) & mask;
	}

	inline uint32_t FloatBits(float i) { return *(uint32_t*)&i; }

	uint64_t ShadowOperatorDesc::Hash(uint64_t seed) const
	{
		uint64_t h0 = 
			  (GetBits<12>(_width)				<< 0ull)
			| (GetBits<12>(_height)				<< 12ull)
			| (GetBits<8>(_format)				<< 24ull)
			| (GetBits<4>(_normalProjCount)		<< 32ull)
			| (GetBits<4>(_projectionMode)		<< 36ull)
			| (GetBits<4>(_cullMode)			<< 40ull)
			| (GetBits<4>(_resolveType)			<< 44ull)
			| (GetBits<1>(_enableNearCascade)  	<< 48ull)
			;

		uint64_t h1 = 
				uint64_t(FloatBits(_slopeScaledBias))
			|  (uint64_t(FloatBits(_depthBiasClamp)) << 32ull)
			;

		uint64_t h2 = 
				uint64_t(FloatBits(_dsSlopeScaledBias))
			|  (uint64_t(FloatBits(_dsDepthBiasClamp)) << 32ull)
			;

		uint64_t h3 = 
				uint64_t(_rasterDepthBias)
			|  (uint64_t(_dsRasterDepthBias) << 32ull)
			;

		return HashCombine(h0, HashCombine(h1, HashCombine(h2, HashCombine(h3, seed))));
	}

	namespace Internal
	{
		std::string ShadowResolveParam::WriteShaderSelectors() const
		{
			StringMeld<256, ::Assets::ResChar> str;
			if (_shadowing != ShadowResolveParam::Shadowing::NoShadows) {
				if (_shadowing == ShadowResolveParam::Shadowing::OrthShadows || _shadowing == ShadowResolveParam::Shadowing::OrthShadowsNearCascade || _shadowing == ShadowResolveParam::Shadowing::OrthHybridShadows) {
					str << "SHADOW_CASCADE_MODE=" << 2u;
				} else if (_shadowing == ShadowResolveParam::Shadowing::CubeMapShadows) {
					str << "SHADOW_CASCADE_MODE=" << 3u;
				} else
					str << "SHADOW_CASCADE_MODE=" << 1u;
				str << ";SHADOW_SUB_PROJECTION_COUNT=" << _normalProjCount;
				str << ";SHADOW_ENABLE_NEAR_CASCADE=" << (_shadowing == ShadowResolveParam::Shadowing::OrthShadowsNearCascade ? 1u : 0u);
				str << ";SHADOW_FILTER_MODEL=" << unsigned(_filterModel);
				str << ";SHADOW_FILTER_CONTACT_HARDENING=" << unsigned(_enableContactHardening);
				str << ";SHADOW_RT_HYBRID=" << unsigned(_shadowing == ShadowResolveParam::Shadowing::OrthHybridShadows);
			}
			return str.AsString();
		}

		ShadowResolveParam MakeShadowResolveParam(const ShadowOperatorDesc& shadowOp)
		{
			ShadowResolveParam param;
			param._filterModel = shadowOp._filterModel;
			switch (shadowOp._projectionMode) {
			case ShadowProjectionMode::Arbitrary:
				param._shadowing = ShadowResolveParam::Shadowing::PerspectiveShadows;
				assert(!shadowOp._enableNearCascade);
				break;
			case ShadowProjectionMode::Ortho:
				param._shadowing = shadowOp._enableNearCascade ? ShadowResolveParam::Shadowing::OrthShadowsNearCascade : ShadowResolveParam::Shadowing::OrthShadows;
				break;
			case ShadowProjectionMode::ArbitraryCubeMap:
				param._shadowing = ShadowResolveParam::Shadowing::CubeMapShadows;
				assert(!shadowOp._enableNearCascade);
				break;
			}
			param._normalProjCount = shadowOp._normalProjCount;
			param._enableContactHardening = shadowOp._enableContactHardening;
			return param;
		}
	}
}}

