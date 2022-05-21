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
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/Services.h"
#include "../Metal/DeviceContext.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../Assets/ContinuationUtil.h"
#include "../IDevice.h"
#include "../../Assets/Marker.h"
#include <vector>

namespace RenderCore { namespace LightingEngine
{
	class PreparedShadowResult : public IPreparedShadowResult
	{
	public:
		std::shared_ptr<IDescriptorSet> _descriptorSet;
		virtual const std::shared_ptr<IDescriptorSet>& GetDescriptorSet() const override { return _descriptorSet; }
		virtual ~PreparedShadowResult() {}
	};

	IPreparedShadowResult::~IPreparedShadowResult() {}

	class DMShadowPreparer : public ICompiledShadowPreparer
	{
	public:
		Techniques::RenderPassInstance Begin(
			IThreadContext& threadContext, 
			Techniques::ParsingContext& parsingContext,
			Internal::ILightBase& projection,
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

		class UniformDelegate : public Techniques::IShaderResourceDelegate
		{
		public:
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
				case 0: 
					assert(_preparer->_workingDMFrustum._cbSource.size());		// we can hit this when the subprojection count is 0
					return _preparer->_workingDMFrustum._cbSource.size();
				default:
					assert(0);
					return 0;
				}
			}
		
			UniformDelegate(DMShadowPreparer& preparer) : _preparer(&preparer)
			{
				BindImmediateData(0, Utility::Hash64("ShadowProjection"), {});
			}
			DMShadowPreparer* _preparer;
		};
	};

	ICompiledShadowPreparer::~ICompiledShadowPreparer() {}

	static Internal::PreparedDMShadowFrustum SetupPreparedDMShadowFrustum(
		Internal::ILightBase& projectionBase, float shadowTextureSize, unsigned operatorMaxFrustumCount)
	{
		assert(projectionBase.QueryInterface(typeid(Internal::StandardShadowProjection).hash_code()) == &projectionBase);
		auto& projection = *(Internal::StandardShadowProjection*)&projectionBase;
		auto projectionCount = std::min(projection._projections.Count(), Internal::MaxShadowTexturesPerLight);
		if (!projectionCount)
			return Internal::PreparedDMShadowFrustum{};

		Internal::PreparedDMShadowFrustum preparedResult;
		preparedResult.InitialiseConstants(projection._projections, operatorMaxFrustumCount, projection._maxBlurSearchPixels / shadowTextureSize);
		preparedResult._resolveParameters._worldSpaceResolveBias = projection._worldSpaceResolveBias;
		preparedResult._resolveParameters._tanBlurAngle = projection._tanBlurAngle;
		preparedResult._resolveParameters._minBlurSearchNorm = projection._minBlurSearchPixels / shadowTextureSize;
		preparedResult._resolveParameters._maxBlurSearchNorm = projection._maxBlurSearchPixels / shadowTextureSize;
		preparedResult._resolveParameters._shadowTextureSize = shadowTextureSize;
		preparedResult._resolveParameters._casterDistanceExtraBias = projection._casterDistanceExtraBias;
		XlZeroMemory(preparedResult._resolveParameters._dummy);

		return preparedResult;
	}

	Techniques::RenderPassInstance DMShadowPreparer::Begin(
		IThreadContext& threadContext, 
		Techniques::ParsingContext& parsingContext,
		Internal::ILightBase& projectionBase,
		Techniques::FrameBufferPool& shadowGenFrameBufferPool,
		Techniques::AttachmentPool& shadowGenAttachmentPool)
	{
		assert(projectionBase.QueryInterface(typeid(Internal::StandardShadowProjection).hash_code()) == &projectionBase);
		auto& projection = *(Internal::StandardShadowProjection*)&projectionBase;
		_workingDMFrustum = SetupPreparedDMShadowFrustum(projection, _shadowTextureSize, _maxFrustumCount);
		assert(_workingDMFrustum.IsReady());
		assert(!_fbDesc._fbDesc.GetSubpasses().empty());
		_savedProjectionDesc = parsingContext.GetProjectionDesc();
		auto rpi = Techniques::RenderPassInstance{
			threadContext,
			_fbDesc._fbDesc, _fbDesc._fullAttachmentDescriptions,
			shadowGenFrameBufferPool, shadowGenAttachmentPool, {}};
		parsingContext.GetViewport() = rpi.GetDefaultViewport();
		return rpi;
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
		const IResourceView* srvs[] = { rpi.GetDepthStencilAttachmentSRV({}).get() };
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
		return std::make_shared<PreparedShadowResult>();
	}

	static const auto s_shadowCascadeModeString = "SHADOW_CASCADE_MODE";
    static const auto s_shadowEnableNearCascadeString = "SHADOW_ENABLE_NEAR_CASCADE";
	static const auto s_shadowSubProjectionCountString = "SHADOW_SUB_PROJECTION_COUNT";

	DMShadowPreparer::DMShadowPreparer(
		const ShadowOperatorDesc& desc,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout)
	: _pipelineAccelerators(pipelineAccelerators)
	{
		assert(desc._resolveType == ShadowResolveType::DepthTexture);

		unsigned arrayCount = 0u;
		if (desc._projectionMode != ShadowProjectionMode::ArbitraryCubeMap)
			arrayCount = desc._normalProjCount + (desc._enableNearCascade ? 1 : 0);

		auto shadowGenDelegate = delegatesBox->GetShadowGenTechniqueDelegate(
			Techniques::ShadowGenType::GSAmplify,
			desc._singleSidedBias, desc._doubleSidedBias, desc._cullMode);

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
			auto attach = fragment.DefineAttachment(Techniques::AttachmentSemantics::ShadowDepthMap)
				.Clear().FinalState(BindFlag::ShaderResource | BindFlag::DepthStencil);
			subpass.SetDepthStencil(attach);
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
		_fbDesc = stitchingContext.TryStitchFrameBufferDesc(MakeIteratorRange(&fragment, &fragment+1));

		_sequencerConfigs = pipelineAccelerators->CreateSequencerConfig(
			"shadow-prepare",
			shadowGenDelegate,
			sequencerSelectors,
			_fbDesc._fbDesc,
			0);
		_uniformDelegate = std::make_shared<UniformDelegate>(*this);

		if (descSetLayout) {
			auto& commonResources = *Techniques::Services::GetCommonResources();
			_descSetSig = descSetLayout->MakeDescriptorSetSignature(&commonResources._samplerPool);
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

	std::unique_ptr<Internal::ILightBase> DynamicShadowPreparationOperators::CreateShadowProjection(unsigned operatorIdx)
	{
		assert(operatorIdx <= _operators.size());
		auto result = std::make_unique<Internal::StandardShadowProjection>();
		auto& op = _operators[operatorIdx];
		result->_projections._mode = op._desc._projectionMode;
		result->_projections._useNearProj = op._desc._enableNearCascade;
		result->_projections._operatorNormalProjCount = op._desc._normalProjCount;
		result->_preparer = op._preparer;
		return result;
	}

	::Assets::PtrToMarkerPtr<ICompiledShadowPreparer> CreateCompiledShadowPreparer(
		const ShadowOperatorDesc& desc,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout)
	{
		auto result = std::make_shared<::Assets::MarkerPtr<ICompiledShadowPreparer>>();
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[desc, pipelineAccelerators, delegatesBox, descSetLayout, promise=result->AdoptPromise()]() mutable {
				TRY {
					promise.set_value(std::make_shared<DMShadowPreparer>(desc, pipelineAccelerators, delegatesBox, descSetLayout));
				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
		return result;
	}

	::Assets::PtrToMarkerPtr<DynamicShadowPreparationOperators> CreateDynamicShadowPreparationOperators(
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators, 
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout)
	{
		auto result = std::make_shared<::Assets::MarkerPtr<DynamicShadowPreparationOperators>>();
		if (shadowGenerators.empty()) {
			result->SetAsset(std::make_shared<DynamicShadowPreparationOperators>());
			return result;
		}

		using PreparerFuture = ::Assets::PtrToMarkerPtr<ICompiledShadowPreparer>;
		std::vector<PreparerFuture> futures;
		futures.reserve(shadowGenerators.size());
		for (unsigned operatorIdx=0; operatorIdx<shadowGenerators.size(); ++operatorIdx) {
			assert(shadowGenerators[operatorIdx]._resolveType != ShadowResolveType::Probe);
			auto preparer = CreateCompiledShadowPreparer(shadowGenerators[operatorIdx], pipelineAccelerators, delegatesBox, descSetLayout);
			futures.push_back(std::move(preparer));
		}

		std::vector<ShadowOperatorDesc> shadowGeneratorCopy { shadowGenerators.begin(), shadowGenerators.end() };
		::Assets::PollToPromise(
			result->AdoptPromise(),
			[futures]() {
				for (const auto& p:futures)
					if (p->IsBkgrndPending()) 
						return ::Assets::PollStatus::Continue;
				return ::Assets::PollStatus::Finish;
			},
			[futures,shadowGeneratorCopy=std::move(shadowGeneratorCopy)]() {
				using namespace ::Assets;
				std::vector<std::shared_ptr<ICompiledShadowPreparer>> actualized;
				actualized.resize(futures.size());
				auto a=actualized.begin();
				for (const auto& p:futures)
					*a++ = p->ActualizeBkgrnd();

				auto finalResult = std::make_shared<DynamicShadowPreparationOperators>();
				finalResult->_operators.reserve(actualized.size());
				assert(actualized.size() == shadowGeneratorCopy.size());
				auto i = shadowGeneratorCopy.begin();
				for (auto&a:actualized)
					finalResult->_operators.push_back(DynamicShadowPreparationOperators::Operator{std::move(a), *i++});

				return finalResult;
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

	uint64_t ShadowOperatorDesc::GetHash(uint64_t seed) const
	{
		uint64_t h0 = 
			  (GetBits<13>(_width)					<< 0ull)
			| (GetBits<13>(_height)					<< 13ull)
			| (GetBits<8>(_format)					<< 28ull)
			| (GetBits<4>(_normalProjCount)			<< 34ull)
			| (GetBits<4>(_projectionMode)			<< 38ull)
			| (GetBits<4>(_cullMode)				<< 42ull)
			| (GetBits<4>(_resolveType)				<< 46ull)
			| (GetBits<1>(_enableNearCascade)  		<< 50ull)
			| (GetBits<1>(_dominantLight)  			<< 51ull)
			| (GetBits<2>(_filterModel)  			<< 52ull)
			| (GetBits<1>(_enableContactHardening)	<< 54ull)
			;

		uint64_t h1 = 
				uint64_t(FloatBits(_singleSidedBias._slopeScaledBias))
			|  (uint64_t(FloatBits(_singleSidedBias._depthBiasClamp)) << 32ull)
			;

		uint64_t h2 = 
				uint64_t(FloatBits(_doubleSidedBias._slopeScaledBias))
			|  (uint64_t(FloatBits(_doubleSidedBias._depthBiasClamp)) << 32ull)
			;

		uint64_t h3 = 
				uint64_t(_singleSidedBias._depthBias)
			|  (uint64_t(_doubleSidedBias._depthBias) << 32ull)
			;

		return HashCombine(h0, HashCombine(h1, HashCombine(h2, HashCombine(h3, seed))));
	}

	namespace Internal
	{
		void ShadowResolveParam::WriteShaderSelectors(ParameterBox& selectors) const
		{
			if (_shadowing != ShadowResolveParam::Shadowing::NoShadows) {
				if (_shadowing == ShadowResolveParam::Shadowing::OrthShadows || _shadowing == ShadowResolveParam::Shadowing::OrthShadowsNearCascade || _shadowing == ShadowResolveParam::Shadowing::OrthHybridShadows) {
					selectors.SetParameter("SHADOW_CASCADE_MODE", 2u);
				} else if (_shadowing == ShadowResolveParam::Shadowing::CubeMapShadows) {
					selectors.SetParameter("SHADOW_CASCADE_MODE", 3u);
				} else
					selectors.SetParameter("SHADOW_CASCADE_MODE", 1u);
				selectors.SetParameter("SHADOW_SUB_PROJECTION_COUNT", _normalProjCount);
				selectors.SetParameter("SHADOW_ENABLE_NEAR_CASCADE", _shadowing == ShadowResolveParam::Shadowing::OrthShadowsNearCascade ? 1u : 0u);
				selectors.SetParameter("SHADOW_FILTER_MODEL", unsigned(_filterModel));
				selectors.SetParameter("SHADOW_FILTER_CONTACT_HARDENING", _enableContactHardening);
				selectors.SetParameter("SHADOW_RT_HYBRID=", unsigned(_shadowing == ShadowResolveParam::Shadowing::OrthHybridShadows));
			}
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

	const char* AsString(ShadowProjectionMode input)
	{
		switch (input) {
		case ShadowProjectionMode::Arbitrary: return "Arbitrary";
		case ShadowProjectionMode::Ortho: return "Ortho";
		case ShadowProjectionMode::ArbitraryCubeMap: return "ArbitraryCubeMap";
		default:
			return nullptr;
		}
	}

	std::optional<ShadowProjectionMode> AsShadowProjectionMode(StringSection<> input)
	{
		if (XlEqString(input, "Arbitrary")) return ShadowProjectionMode::Arbitrary;
		if (XlEqString(input, "Ortho")) return ShadowProjectionMode::Ortho;
		if (XlEqString(input, "ArbitraryCubeMap")) return ShadowProjectionMode::ArbitraryCubeMap;
		return {};
	}

	const char* AsString(ShadowResolveType input)
	{
		switch (input) {
		case ShadowResolveType::DepthTexture: return "DepthTexture";
		case ShadowResolveType::RayTraced: return "RayTraced";
		case ShadowResolveType::Probe: return "Probe";
		default:
			return nullptr;
		}
	}

	std::optional<ShadowResolveType> AsShadowResolveType(StringSection<> input)
	{
		if (XlEqString(input, "DepthTexture")) return ShadowResolveType::DepthTexture;
		if (XlEqString(input, "RayTraced")) return ShadowResolveType::RayTraced;
		if (XlEqString(input, "Probe")) return ShadowResolveType::Probe;
		return {};
	}

	const char* AsString(ShadowFilterModel input)
	{
		switch (input) {
		case ShadowFilterModel::None: return "None";
		case ShadowFilterModel::PoissonDisc: return "PoissonDisc";
		case ShadowFilterModel::Smooth: return "Smooth";
		default:
			return nullptr;
		}
	}

	std::optional<ShadowFilterModel> AsShadowFilterModel(StringSection<> input)
	{
		if (XlEqString(input, "None")) return ShadowFilterModel::None;
		if (XlEqString(input, "PoissonDisc")) return ShadowFilterModel::PoissonDisc;
		if (XlEqString(input, "Smooth")) return ShadowFilterModel::Smooth;
		return {};
	}

}}

