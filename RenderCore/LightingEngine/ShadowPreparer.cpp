// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShadowPreparer.h"
#include "ShadowUniforms.h"
#include "StandardLightScene.h"
#include "RenderStepFragments.h"
#include "LightingEngineApparatus.h"
#include "LightingEngineInitialization.h"
#include "../Techniques/PipelineAccelerator.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/TechniqueDelegates.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/SubFrameUtil.h"
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
		IDescriptorSet* _descriptorSet;
		virtual IDescriptorSet* GetDescriptorSet() const override { return _descriptorSet; }
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
			PipelineType,
			IPreparedShadowResult& res) override;

		std::pair<std::shared_ptr<Techniques::SequencerConfig>, std::shared_ptr<Techniques::IShaderResourceDelegate>> GetSequencerConfig() override;
		std::shared_ptr<IPreparedShadowResult> CreatePreparedShadowResult() override;
		void SetDescriptorSetLayout(const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout, PipelineType pipelineType) override;

		DMShadowPreparer(
			const ShadowOperatorDesc& desc,
			const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
			const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox);
		~DMShadowPreparer();

	private:
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;

		Techniques::FragmentStitchingContext::StitchResult _fbDesc;
		std::shared_ptr<Techniques::SequencerConfig> _sequencerConfigs;
		std::shared_ptr<Techniques::IShaderResourceDelegate> _uniformDelegate;

		Techniques::ProjectionDesc _savedProjectionDesc;

		Internal::PreparedDMShadowFrustum _workingDMFrustum;

		Techniques::SubFrameDescriptorSetHeap _descSetHeap;
		std::vector<DescriptorSetInitializer::BindTypeAndIdx> _descSetSlotBindings;
		bool _descSetGood = false;
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
				case 1:
					{
						#if defined(_DEBUG)
							unsigned projCount = _preparer->_workingDMFrustum._frustumCount;
							if (_preparer->_workingDMFrustum._enableNearCascade) ++projCount;
							assert(dst.size() == sizeof(Float4x4)*projCount);
						#endif
						std::memcpy(dst.begin(), _preparer->_workingDMFrustum._multiViewWorldToClip, dst.size());
					}
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
				case 1:
					{
						unsigned projCount = _preparer->_workingDMFrustum._frustumCount;
						if (_preparer->_workingDMFrustum._enableNearCascade) ++projCount;
						return sizeof(Float4x4)*projCount;
					}
				default:
					assert(0);
					return 0;
				}
			}
		
			UniformDelegate(DMShadowPreparer& preparer) : _preparer(&preparer)
			{
				BindImmediateData(0, Utility::Hash64("ShadowProjection"), {});
				BindImmediateData(1, Utility::Hash64("MultiViewProperties"), {});
			}
			DMShadowPreparer* _preparer;
		};
	};

	ICompiledShadowPreparer::~ICompiledShadowPreparer() {}

	namespace Internal
	{
		class StandardShadowProjection : public Internal::ILightBase, public IDepthTextureResolve, public IArbitraryShadowProjections, public IOrthoShadowProjections, public INearShadowProjection
		{
		public:
			using Projections = MultiProjection<MaxShadowTexturesPerLight>;
			Projections     _projections;

			float           _worldSpaceResolveBias = 0.f;
			float           _tanBlurAngle = 0.00436f;
			float           _minBlurSearchPixels = 0.5f, _maxBlurSearchPixels = 25.f;
			float			_casterDistanceExtraBias = 0.f;

			bool 			_multiViewInstancingPath = false;

			virtual void SetDesc(const Desc& newDesc) override
			{
				_worldSpaceResolveBias = newDesc._worldSpaceResolveBias;
				_tanBlurAngle = newDesc._tanBlurAngle;
				_minBlurSearchPixels = newDesc._minBlurSearch;
				_maxBlurSearchPixels = newDesc._maxBlurSearch;
				_casterDistanceExtraBias = newDesc._casterDistanceExtraBias;
			}
			virtual Desc GetDesc() const override
			{
				return Desc { _worldSpaceResolveBias, _tanBlurAngle, _minBlurSearchPixels, _maxBlurSearchPixels, _casterDistanceExtraBias };
			}

			virtual void SetArbitrarySubProjections(
				IteratorRange<const Float4x4*> worldToCamera,
				IteratorRange<const Float4x4*> cameraToProjection) override
			{
				assert(_projections._mode == ShadowProjectionMode::Arbitrary || _projections._mode == ShadowProjectionMode::ArbitraryCubeMap);
				assert(worldToCamera.size() <= Internal::MaxShadowTexturesPerLight);
				assert(!worldToCamera.empty());
				assert(worldToCamera.size() == cameraToProjection.size());
				auto projCount = std::min((size_t)Internal::MaxShadowTexturesPerLight, worldToCamera.size());
				assert(projCount <= _projections._operatorNormalProjCount);     // a mis-match here means it does not agree with the operator
				for (unsigned c=0; c<projCount; ++c) {
					_projections._fullProj[c]._worldToProjTransform = Combine(worldToCamera[c], cameraToProjection[c]);
					_projections._minimalProjection[c] = ExtractMinimalProjection(cameraToProjection[c]);
				}
				_projections._normalProjCount = projCount;
			}

			virtual void SetWorldToOrthoView(const Float4x4& worldToCamera) override
			{
				assert(_projections._mode == ShadowProjectionMode::Ortho);
				assert(IsOrthonormal(Truncate3x3(worldToCamera)));
				_projections._definitionViewMatrix = worldToCamera;
			}

			virtual void SetOrthoSubProjections(IteratorRange<const OrthoSubProjection*> projections) override
			{
				assert(_projections._mode == ShadowProjectionMode::Ortho);
				assert(projections.size() < Internal::MaxShadowTexturesPerLight);
				assert(!projections.empty());
				auto projCount = std::min((size_t)Internal::MaxShadowTexturesPerLight, projections.size());
				assert(projCount <= _projections._operatorNormalProjCount);     // a mis-match here means it does not agree with the operator
				for (unsigned c=0; c<projCount; ++c) {
					_projections._orthoSub[c]._leftTopFront = projections[c]._leftTopFront;
					_projections._orthoSub[c]._rightBottomBack = projections[c]._rightBottomBack;

					auto projTransform = OrthogonalProjection(
						projections[c]._leftTopFront[0], projections[c]._leftTopFront[1], 
						projections[c]._rightBottomBack[0], projections[c]._rightBottomBack[1], 
						projections[c]._leftTopFront[2], projections[c]._rightBottomBack[2],
						GeometricCoordinateSpace::RightHanded, Techniques::GetDefaultClipSpaceType());
					_projections._fullProj[c]._worldToProjTransform = Combine(_projections._definitionViewMatrix, projTransform);
					_projections._minimalProjection[c] = ExtractMinimalProjection(projTransform);
				}
				_projections._normalProjCount = projCount;
			}

			virtual Float4x4 GetWorldToOrthoView() const override
			{
				assert(_projections._mode == ShadowProjectionMode::Ortho);
				return _projections._definitionViewMatrix;
			}

			virtual std::vector<OrthoSubProjection> GetOrthoSubProjections() const override
			{
				assert(_projections._mode == ShadowProjectionMode::Ortho);
				std::vector<OrthoSubProjection> result;
				result.reserve(_projections._normalProjCount);
				for (unsigned c=0; c<_projections._normalProjCount; ++c)
					result.push_back(OrthoSubProjection{_projections._orthoSub[c]._leftTopFront, _projections._orthoSub[c]._rightBottomBack});
				return result;
			}

			virtual void SetProjection(const Float4x4& nearWorldToProjection) override
			{
				assert(_projections._useNearProj);
				_projections._specialNearProjection = nearWorldToProjection;
				_projections._specialNearMinimalProjection = ExtractMinimalProjection(nearWorldToProjection);
			}

			virtual void* QueryInterface(uint64_t interfaceTypeCode) override
			{
				if (interfaceTypeCode == typeid(IDepthTextureResolve).hash_code()) {
					return (IDepthTextureResolve*)this;
				} else if (interfaceTypeCode == typeid(IArbitraryShadowProjections).hash_code()) {
					if (_projections._mode == ShadowProjectionMode::Arbitrary || _projections._mode == ShadowProjectionMode::ArbitraryCubeMap)
						return (IArbitraryShadowProjections*)this;
				} else if (interfaceTypeCode == typeid(IOrthoShadowProjections).hash_code()) {
					if (_projections._mode == ShadowProjectionMode::Ortho)
						return (IOrthoShadowProjections*)this;
				} else if (interfaceTypeCode == typeid(INearShadowProjection).hash_code()) {
					if (_projections._useNearProj)
						return (INearShadowProjection*)this;
				} else if (interfaceTypeCode == typeid(StandardShadowProjection).hash_code()) {
					return this;
				}
				return nullptr;
			}
		};
	}

	TechniqueSequenceParseId CreateShadowParseInSequence(
		LightingTechniqueIterator& iterator,
		LightingTechniqueSequence& sequence,
		Internal::ILightBase& proj,
		std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> volumeTester)
	{
		auto& standardProj = *checked_cast<Internal::StandardShadowProjection*>(&proj);
		if (standardProj._multiViewInstancingPath) {
			std::vector<Techniques::ProjectionDesc> projDescs;
			projDescs.resize(standardProj._projections.Count());
			CalculateProjections(MakeIteratorRange(projDescs), standardProj._projections);
			return sequence.CreateMultiViewParseScene(Techniques::BatchFlags::Opaque, std::move(projDescs), std::move(volumeTester));
		} else {
			if (volumeTester) {
				return sequence.CreateParseScene(Techniques::BatchFlags::Opaque, std::move(volumeTester));
			} else
				return sequence.CreateParseScene(Techniques::BatchFlags::Opaque);
		}
	}

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
		PipelineType descSetPipelineType,
		IPreparedShadowResult& res)
	{
		assert(_descSetGood);

		DescriptorSetInitializer descSetInit;
		descSetInit._signature = &_descSetHeap.GetSignature();
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
		descSetInit._pipelineType = descSetPipelineType;

		// We can only use this descriptor set during this frame -- but there's no protections for this, we're on our own
		auto* descSet = _descSetHeap.Allocate();
		assert(descSet);
		Techniques::WriteWithSubframeImmediates(threadContext, *descSet, descSetInit);
		checked_cast<PreparedShadowResult*>(&res)->_descriptorSet = descSet;

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
	static const auto s_shadowOrthogonalClipToNearString = "SHADOW_ORTHOGONAL_CLIP_TO_NEAR";

	void DMShadowPreparer::SetDescriptorSetLayout(
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout,
		PipelineType pipelineType)
	{
		auto& commonResources = *Techniques::Services::GetCommonResources();
		_descSetHeap = Techniques::SubFrameDescriptorSetHeap {
			*_pipelineAccelerators->GetDevice(),
			descSetLayout->MakeDescriptorSetSignature(&commonResources._samplerPool),
			pipelineType };
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
		_descSetGood = true;
	}

	DMShadowPreparer::DMShadowPreparer(
		const ShadowOperatorDesc& desc,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox)
	: _pipelineAccelerators(pipelineAccelerators)
	{
		assert(desc._resolveType == ShadowResolveType::DepthTexture);

		unsigned arrayCount = 0u;
		if (desc._projectionMode != ShadowProjectionMode::ArbitraryCubeMap)
			arrayCount = desc._normalProjCount + (desc._enableNearCascade ? 1 : 0);

		auto shadowGenDelegate = delegatesBox->GetShadowGenTechniqueDelegate(
			desc._multiViewInstancingPath ? Techniques::ShadowGenType::VertexIdViewInstancing : Techniques::ShadowGenType::GSAmplify,
			desc._singleSidedBias, desc._doubleSidedBias, desc._cullMode);

		ParameterBox sequencerSelectors;
		if (desc._projectionMode == ShadowProjectionMode::Ortho) {
			sequencerSelectors.SetParameter(s_shadowCascadeModeString, 2);
			sequencerSelectors.SetParameter(s_shadowOrthogonalClipToNearString, 1u);		// cheap solution for geometry behind the shadow camera in orthogonal modes
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
			subpass.SetName("prepare-shadow");
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
				BindFlag::ShaderResource | BindFlag::DepthStencil,
				TextureDesc::PlainCube(desc._width, desc._height, desc._format),
				"shadow-map-cube");
		} else {
			pregAttach._desc = CreateDesc(
				BindFlag::ShaderResource | BindFlag::DepthStencil,
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

		_descSetGood = false;
		_shadowTextureSize = (float)std::min(desc._width, desc._height);
		_maxFrustumCount = desc._normalProjCount;
	}

	DMShadowPreparer::~DMShadowPreparer() {}

	std::pair<std::unique_ptr<Internal::ILightBase>, std::shared_ptr<ICompiledShadowPreparer>> DynamicShadowPreparers::CreateShadowProjection(unsigned operatorIdx)
	{
		assert(operatorIdx <= _preparers.size());
		auto result = std::make_unique<Internal::StandardShadowProjection>();
		auto& op = _preparers[operatorIdx];
		result->_projections._mode = op._desc._projectionMode;
		result->_projections._useNearProj = op._desc._enableNearCascade;
		result->_projections._operatorNormalProjCount = op._desc._normalProjCount;
		result->_multiViewInstancingPath = op._desc._multiViewInstancingPath;
		return { std::move(result), op._preparer };
	}

	std::future<std::shared_ptr<ICompiledShadowPreparer>> CreateCompiledShadowPreparer(
		const ShadowOperatorDesc& desc,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox)
	{
		std::promise<std::shared_ptr<ICompiledShadowPreparer>> promise;
		auto result = promise.get_future();
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[desc, pipelineAccelerators, delegatesBox, promise=std::move(promise)]() mutable {
				TRY {
					promise.set_value(std::make_shared<DMShadowPreparer>(desc, pipelineAccelerators, delegatesBox));
				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
		return result;
	}

	std::future<std::shared_ptr<DynamicShadowPreparers>> CreateDynamicShadowPreparers(
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators, 
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox)
	{
		std::promise<std::shared_ptr<DynamicShadowPreparers>> promise;
		auto result = promise.get_future();
		if (shadowGenerators.empty()) {
			promise.set_value(std::make_shared<DynamicShadowPreparers>());
			return result;
		}

		struct Helper
		{
			using PreparerFuture = std::future<std::shared_ptr<ICompiledShadowPreparer>>;
			std::vector<PreparerFuture> _futures;
			unsigned _completedUpTo = 0;
		};
		auto helper = std::make_shared<Helper>();
		helper->_futures.reserve(shadowGenerators.size());
		for (unsigned operatorIdx=0; operatorIdx<shadowGenerators.size(); ++operatorIdx) {
			assert(shadowGenerators[operatorIdx]._resolveType != ShadowResolveType::Probe);
			auto preparer = CreateCompiledShadowPreparer(shadowGenerators[operatorIdx], pipelineAccelerators, delegatesBox);
			helper->_futures.push_back(std::move(preparer));
		}

		std::vector<ShadowOperatorDesc> shadowGeneratorCopy { shadowGenerators.begin(), shadowGenerators.end() };
		::Assets::PollToPromise(
			std::move(promise),
			[helper](auto timeout) {
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				for (;helper->_completedUpTo<helper->_futures.size(); ++helper->_completedUpTo)
					if (helper->_futures[helper->_completedUpTo].wait_until(timeoutTime) == std::future_status::timeout)
						return ::Assets::PollStatus::Continue;
				return ::Assets::PollStatus::Finish;
			},
			[helper,shadowGeneratorCopy=std::move(shadowGeneratorCopy)]() {
				using namespace ::Assets;
				std::vector<std::shared_ptr<ICompiledShadowPreparer>> actualized;
				actualized.resize(helper->_futures.size());
				auto a=actualized.begin();
				for (auto& p:helper->_futures)
					*a++ = p.get();

				auto finalResult = std::make_shared<DynamicShadowPreparers>();
				finalResult->_preparers.reserve(actualized.size());
				assert(actualized.size() == shadowGeneratorCopy.size());
				auto i = shadowGeneratorCopy.begin();
				for (auto&a:actualized)
					finalResult->_preparers.push_back(DynamicShadowPreparers::Preparer{std::move(a), *i++});

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
			| (GetBits<1>(_multiViewInstancingPath)	<< 55ull)
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
				if (_shadowing != ShadowResolveParam::Shadowing::Probe) {
					if (_shadowing == ShadowResolveParam::Shadowing::OrthShadows || _shadowing == ShadowResolveParam::Shadowing::OrthShadowsNearCascade || _shadowing == ShadowResolveParam::Shadowing::OrthHybridShadows) {
						selectors.SetParameter(s_shadowCascadeModeString, 2u);
					} else if (_shadowing == ShadowResolveParam::Shadowing::CubeMapShadows) {
						selectors.SetParameter(s_shadowCascadeModeString, 3u);
					} else
						selectors.SetParameter(s_shadowCascadeModeString, 1u);
					selectors.SetParameter(s_shadowSubProjectionCountString, _normalProjCount);
					selectors.SetParameter(s_shadowEnableNearCascadeString, _shadowing == ShadowResolveParam::Shadowing::OrthShadowsNearCascade ? 1u : 0u);
					selectors.SetParameter("SHADOW_FILTER_MODEL", unsigned(_filterModel));
					selectors.SetParameter("SHADOW_FILTER_CONTACT_HARDENING", _enableContactHardening);
					selectors.SetParameter("SHADOW_RT_HYBRID", unsigned(_shadowing == ShadowResolveParam::Shadowing::OrthHybridShadows));
				} else {
					selectors.SetParameter("SHADOW_PROBE", 1);
				}
			}
		}

		ShadowResolveParam MakeShadowResolveParam(const ShadowOperatorDesc& shadowOp)
		{
			ShadowResolveParam param;
			param._filterModel = shadowOp._filterModel;
			if (shadowOp._resolveType != ShadowResolveType::Probe) {
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
			} else {
				param._shadowing = ShadowResolveParam::Shadowing::Probe;
			}
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

