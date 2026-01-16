// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingDelegateUtil.h"
#include "ILightScene.h"
#include "Sequence.h"
#include "SequenceIterator.h"
#include "ShadowPreparer.h"
#include "ShadowProbes.h"
#include "ShadowProjectionDriver.h"
#include "LightTiler.h"
#include "LightUniforms.h"
#include "SkyOperator.h"
#include "SHCoefficients.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/PipelineAccelerator.h"
#include "../Techniques/TechniqueUtils.h"
#include "../Techniques/Services.h"
#include "../Techniques/CommonResources.h"
#include "../Assets/TextureCompiler.h"
#include "../Metal/Resource.h"
#include "../Metal/DeviceContext.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Math/ProjectionMath.h"
#include "../../Math/Transformations.h"
#include "../../xleres/FileList.h"
#include <limits>
#include <numeric>
#include <utility>

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	struct SequencerAddendums;

	struct PriorityShadowProjectionScheduler::SceneSet
	{
		using ShadowProjectionBasePtr = std::unique_ptr<ILightBase>;
		std::vector<ShadowProjectionBasePtr> _projections;
		std::vector<std::shared_ptr<IPreparedShadowResult>> _preparedResult;
		std::vector<SequencerAddendums> _addendums;
		BitHeap _activeProjections;
		std::shared_ptr<PriorityShadowSchedulerUtil> _preparers;
		unsigned _preparerId = ~0u;
		ShadowProjectionMode _projectionMode = ShadowProjectionMode::Arbitrary;
		QueryInterfaceFunction _qi;

		void RegisterLight(unsigned index);
		void DeregisterLight(unsigned index);
		void SetQI(QueryInterfaceFunction&& qi);
		SceneSet();
		SceneSet(SceneSet&&);
		SceneSet& operator=(SceneSet&&);
	};

	struct SequencerAddendums : public IAttachDriver
	{
		std::shared_ptr<Internal::ILightBase> _driver;
		std::shared_ptr<IShadowPreparer> _preparer;
		IPositionalLightSource* _positionalChain = nullptr;
		IFiniteLightSource* _finiteChain = nullptr;
		IConeSource* _coneChain = nullptr;

		void AttachDriver(std::shared_ptr<ILightBase> driver) override { _driver = std::move(driver); }
	};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class PriorityShadowSchedulerUtil
	{
	public:
		struct Preparer
		{
			std::shared_ptr<IShadowPreparer> _preparer;
			ShadowOperatorDesc _desc;
			std::pair<std::unique_ptr<Internal::ILightBase>, std::shared_ptr<IShadowPreparer>> CreateShadowProjection();
		};
		std::vector<Preparer> _preparers;
		std::shared_ptr<IDevice> _device;
	};

	static std::shared_ptr<IPreparedShadowResult> SetupShadowPrepare(
		SequenceIterator& iterator,
		Sequence& sequence,
		ILightBase& proj,
		const SequencerAddendums& addenums,
		ShadowProjectionMode shadowProjectionMode,
		Techniques::IFrameBufferPool& shadowGenFrameBufferPool,
		Techniques::IAttachmentPool& shadowGenAttachmentPool,
		ViewPool& shadowGenViewPool);

	void PriorityShadowProjectionScheduler::SceneSet::RegisterLight(unsigned index)
	{
		if (_projections.size() <= index) {
			_projections.resize(index+1);
			_preparedResult.resize(index+1);
			_addendums.resize(index+1);
		}
		assert(!_activeProjections.IsAllocated(index));
		assert(!_projections[index]);
		std::tie(_projections[index], _addendums[index]._preparer) = _preparers->_preparers[_preparerId].CreateShadowProjection();
		_addendums[index]._positionalChain = (IPositionalLightSource*)_qi(index, TypeHashCode<IPositionalLightSource>);
		_addendums[index]._finiteChain = (IFiniteLightSource*)_qi(index, TypeHashCode<IFiniteLightSource>);
		_addendums[index]._coneChain = (IConeSource*)_qi(index, TypeHashCode<IConeSource>);
		_activeProjections.Allocate(index);
	}
	void PriorityShadowProjectionScheduler::SceneSet::DeregisterLight(unsigned index)
	{
		_activeProjections.Deallocate(index);
		_projections[index] = {};
		_addendums[index] = {};
	}
	void PriorityShadowProjectionScheduler::SceneSet::SetQI(QueryInterfaceFunction&& qi)
	{
		_qi = std::move(qi);
		for (auto& l:_addendums) {
			l._positionalChain = (IPositionalLightSource*)_qi(&l-_addendums.data(), TypeHashCode<IPositionalLightSource>);
			l._finiteChain = (IFiniteLightSource*)_qi(&l-_addendums.data(), TypeHashCode<IFiniteLightSource>);
			l._coneChain = (IConeSource*)_qi(&l-_addendums.data(), TypeHashCode<IConeSource>);
		}
	}

	PriorityShadowProjectionScheduler::SceneSet::SceneSet() = default;
	PriorityShadowProjectionScheduler::SceneSet::SceneSet(SceneSet&&) = default;
	auto PriorityShadowProjectionScheduler::SceneSet::operator=(SceneSet&&) -> SceneSet& = default;

	void PriorityShadowProjectionScheduler::DoShadowPrepare(
		SequenceIterator& iterator,
		Sequence& sequence)
	{
		if (_shadowPreparers->_preparers.empty()) return;

		for (auto& comp:_sceneSets) {
			if (!comp) continue;
			unsigned offset = 0;
			for (auto q:comp->_activeProjections.InternalArray()) {
				q = ~q;		// bit heap inverts allocations
				while (q) {
					auto idx = xl_ctz8(q);
					q ^= 1ull << uint64_t(idx);
					idx += offset;

					comp->_preparedResult[idx] = SetupShadowPrepare(
						iterator, sequence, *comp->_projections[idx], comp->_addendums[idx], comp->_projectionMode,
						*_shadowGenFrameBufferPool, *_shadowGenAttachmentPool, _shadowGenViewPool);
				}
				offset += 64;
			}
		}
	}

	void PriorityShadowProjectionScheduler::ClearPreparedShadows()
	{
		for (auto& comp:_sceneSets) {
			if (!comp) continue;
			for (auto& p:comp->_preparedResult)
				p = {};
		}
	}

	std::vector<std::shared_ptr<Techniques::SequencerConfig>> PriorityShadowProjectionScheduler::GetSequencerCfgsForPrepareSteps()
	{
		// return a unique list of sequencer cfgs, which we can use to ensure resources are prepared
		std::vector<std::shared_ptr<Techniques::SequencerConfig>> result;
		for(auto& prep:_shadowPreparers->_preparers)
			result.emplace_back(prep._preparer->GetSequencerConfig().first);
		std::sort(b2e(result));
		result.erase(std::unique(b2e(result)), result.end());
		return result;
	}

	void PriorityShadowProjectionScheduler::RegisterLight(unsigned setIdx, unsigned lightIdx)
	{
		assert(setIdx < _sceneSets.size() && _sceneSets[setIdx]);
		_sceneSets[setIdx]->RegisterLight(lightIdx);
		++_totalProjectionCount;
	}

	void PriorityShadowProjectionScheduler::DeregisterLight(unsigned setIdx, unsigned lightIdx)
	{
		assert(setIdx < _sceneSets.size());
		_sceneSets[setIdx]->DeregisterLight(lightIdx);
		assert(_totalProjectionCount > 0);
		--_totalProjectionCount;
	}

	bool PriorityShadowProjectionScheduler::BindToSet(ILightScene::LightOperatorId opId, unsigned setIdx, QueryInterfaceFunction&& queryChain)
	{
		if (opId >= _operatorToPreparerIdMapping.size() || _operatorToPreparerIdMapping[opId] == ~0u) return false;
		if (_sceneSets.size() <= setIdx)
			_sceneSets.resize(setIdx+1);
		if (!_sceneSets[setIdx]) _sceneSets[setIdx] = std::make_unique<SceneSet>();
		_sceneSets[setIdx]->_preparers = _shadowPreparers;
		_sceneSets[setIdx]->_preparerId = _operatorToPreparerIdMapping[opId];
		_sceneSets[setIdx]->_projectionMode = _shadowPreparers->_preparers[_sceneSets[setIdx]->_preparerId]._desc._projectionMode;
		_sceneSets[setIdx]->SetQI(std::move(queryChain));
		return true;
	}

	void* PriorityShadowProjectionScheduler::QueryInterface(unsigned setIdx, ILightScene::LightSourceId lightIdx, uint64_t interfaceTypeCode)
	{
		if (setIdx >= _sceneSets.size() || !_sceneSets[setIdx]) return nullptr;

		switch (interfaceTypeCode) {
		case TypeHashCode<IAttachDriver>:
			return (IAttachDriver*)&_sceneSets[setIdx]->_addendums[lightIdx];
		default:
			if (_sceneSets[setIdx]->_addendums[lightIdx]._driver)
				if (auto* res = _sceneSets[setIdx]->_addendums[lightIdx]._driver->QueryInterface(interfaceTypeCode))
					return res;
			return _sceneSets[setIdx]->_projections[lightIdx]->QueryInterface(interfaceTypeCode);
		}
	}

	auto PriorityShadowProjectionScheduler::GetPreparedShadow(unsigned setIdx, unsigned lightIdx) -> const IPreparedShadowResult*
	{
		if (setIdx >= _sceneSets.size() || !_sceneSets[setIdx]) return nullptr;
		assert(_sceneSets[setIdx]->_activeProjections.IsAllocated(lightIdx));
		return _sceneSets[setIdx]->_preparedResult[lightIdx].get();
	}

	const ShadowOperatorDesc& PriorityShadowProjectionScheduler::GetShadowOperatorDesc(unsigned setIdx, unsigned lightIdx)
	{
		if (setIdx >= _sceneSets.size() || !_sceneSets[setIdx]) {
			static ShadowOperatorDesc s_dummy;
			return s_dummy;
		}
		assert(_sceneSets[setIdx]->_activeProjections.IsAllocated(lightIdx));
		return _shadowPreparers->_preparers[_sceneSets[setIdx]->_preparerId]._desc;
	}

	auto PriorityShadowProjectionScheduler::GetAllPreparedShadows() -> std::vector<PreparedShadow>
	{
		std::vector<PreparedShadow> result;
		result.reserve(_totalProjectionCount);
		for (const auto& sceneSet:_sceneSets) {
			if (!sceneSet) continue;
			for (auto& p:sceneSet->_preparedResult)
				if (p)
					result.push_back({sceneSet->_preparerId, p.get(), _shadowPreparers->_preparers[sceneSet->_preparerId]._desc});
		}
		return result;
	}

	void PriorityShadowProjectionScheduler::SetDescriptorSetLayout(
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout,
		PipelineType pipelineType)
	{
		for (const auto& preparer:_shadowPreparers->_preparers)
			preparer._preparer->SetDescriptorSetLayout(descSetLayout, pipelineType);
	}

	PriorityShadowProjectionScheduler::PriorityShadowProjectionScheduler(std::shared_ptr<PriorityShadowSchedulerUtil> shadowPreparers, IteratorRange<const unsigned*> operatorToPreparer)
	: _shadowPreparers(std::move(shadowPreparers)), _totalProjectionCount(0)
	{
		_shadowGenAttachmentPool = Techniques::CreateAttachmentPool(_shadowPreparers->_device);
		_shadowGenFrameBufferPool = Techniques::CreateFrameBufferPool();
		_operatorToPreparerIdMapping = {operatorToPreparer.begin(), operatorToPreparer.end()};
	}
	PriorityShadowProjectionScheduler::~PriorityShadowProjectionScheduler() {}

	static SequenceParseId SetupShadowParse(
		SequenceIterator& iterator,
		Sequence& sequence,
		Internal::ILightBase& proj,
		const SequencerAddendums& addendums, ShadowProjectionMode shadowProjectionMode)
	{
		std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> volumeTester;

		assert(addendums._positionalChain);
		auto localToWorld = addendums._positionalChain->GetLocalToWorld();

		// Call the driver if one exists
		if (addendums._driver) {
			// Note the TryGetLightSourceInterface is expensive particular, and scales poorly with the number of
			// lights in the scene
			auto* orthoShadowProjections = (IOrthoShadowProjections*)proj.QueryInterface(TypeHashCode<IOrthoShadowProjections>);
			assert(orthoShadowProjections);
			volumeTester = ((Internal::IShadowProjectionDriver*)addendums._driver->QueryInterface(TypeHashCode<Internal::IShadowProjectionDriver>))->UpdateProjections(
				*iterator._parsingContext, localToWorld, *orthoShadowProjections);
		} else if (addendums._coneChain) {
			// we'll attempt to attach the projections to the list
			// only implemented for specific cases of sphere light & cubemap shadows, or cone light
			assert(shadowProjectionMode == ShadowProjectionMode::Arbitrary);
			auto* shadowProjections = (IArbitraryShadowProjections*)proj.QueryInterface(TypeHashCode<IArbitraryShadowProjections>);
			assert(shadowProjections);
			auto lightPosition = ExtractTranslation(localToWorld); float lightNearRadius = ExtractUniformScaleFast(AsFloat3x4(localToWorld));
			auto lightForward = ExtractForward(localToWorld);
			float lightFarRadius = addendums._finiteChain ? addendums._finiteChain->GetCutoffRange() : 8.f;
			Techniques::CameraDesc cameraDesc;
			cameraDesc._cameraToWorld = MakeCameraToWorld(Normalize(lightForward), Float3{0,0,1}, lightPosition);
			cameraDesc._verticalFieldOfView = std::min(gPI, 2.f * addendums._coneChain->GetConeAngle());
			assert(lightNearRadius<lightFarRadius);
			cameraDesc._nearClip = lightNearRadius; cameraDesc._farClip = lightFarRadius;
			cameraDesc._projection = Techniques::CameraDesc::Projection::Perspective;
			auto projDesc = Techniques::BuildProjectionDesc(cameraDesc, 1.f);
			auto worldToCamera = InvertOrthonormalTransform(projDesc._cameraToWorld);
			auto cameraToProjection = projDesc._cameraToProjection;
			shadowProjections->SetArbitrarySubProjections(MakeIteratorRange(&worldToCamera, &worldToCamera+1), MakeIteratorRange(&cameraToProjection, &cameraToProjection+1));
		} else {
			assert(shadowProjectionMode == ShadowProjectionMode::ArbitraryCubeMap);
			auto* shadowProjections = (IArbitraryShadowProjections*)proj.QueryInterface(TypeHashCode<IArbitraryShadowProjections>);
			assert(shadowProjections);
			Float4x4 worldToCamera[6];
			Float4x4 cameraToProjection[6];
			auto lightPosition = ExtractTranslation(localToWorld); float lightNearRadius = ExtractUniformScaleFast(AsFloat3x4(localToWorld));
			float lightFarRadius = addendums._finiteChain ? addendums._finiteChain->GetCutoffRange() : 8.f;
			assert(lightNearRadius<lightFarRadius);
			for (unsigned c=0; c<6; ++c) {
				auto projDesc = Techniques::BuildCubemapProjectionDesc(c, lightPosition, lightNearRadius, lightFarRadius);
				worldToCamera[c] = InvertOrthonormalTransform(projDesc._cameraToWorld);
				cameraToProjection[c] = projDesc._cameraToProjection;
			}
			shadowProjections->SetArbitrarySubProjections(worldToCamera, cameraToProjection);
		}

		// todo - cull out any offscreen projections
		return CreateShadowParseInSequence(iterator, sequence, proj, std::move(volumeTester));
	}

	static std::shared_ptr<IPreparedShadowResult> SetupShadowPrepare(
		SequenceIterator& iterator,
		Sequence& sequence,
		ILightBase& proj,
		const SequencerAddendums& addenums,
		ShadowProjectionMode shadowProjectionMode,
		Techniques::IFrameBufferPool& shadowGenFrameBufferPool,
		Techniques::IAttachmentPool& shadowGenAttachmentPool,
		ViewPool& shadowGenViewPool)
	{
		auto parseId = SetupShadowParse(iterator, sequence, proj, addenums, shadowProjectionMode);

		auto& preparer = *addenums._preparer;
		auto res = preparer.CreatePreparedShadowResult();
		sequence.CreateStep_CallFunction(
			[&preparer, &proj, &shadowGenFrameBufferPool, &shadowGenAttachmentPool, &shadowGenViewPool, parseId, res](SequenceIterator& iterator) {
				auto rpi = preparer.Begin(
					*iterator._parsingContext,
					proj,
					shadowGenFrameBufferPool,
					shadowGenAttachmentPool,
					shadowGenViewPool);
				iterator.ExecuteDrawables(parseId, *preparer.GetSequencerConfig().first, preparer.GetSequencerConfig().second);
				rpi.End();
				preparer.End(*iterator._parsingContext, rpi, *res);
			});
		return res;
	}

	std::pair<std::unique_ptr<Internal::ILightBase>, std::shared_ptr<IShadowPreparer>> PriorityShadowSchedulerUtil::Preparer::CreateShadowProjection()
	{
		return { CreateStandardShadowProjectionInterface(_desc), _preparer };
	}

	std::future<std::shared_ptr<PriorityShadowSchedulerUtil>> CreatePriorityShadowSchedulerUtil(
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox)
	{
		std::promise<std::shared_ptr<PriorityShadowSchedulerUtil>> promise;
		auto result = promise.get_future();
		if (shadowGenerators.empty()) {
			promise.set_value(std::make_shared<PriorityShadowSchedulerUtil>());
			return result;
		}

		struct Helper
		{
			using PreparerFuture = std::future<std::shared_ptr<IShadowPreparer>>;
			std::vector<PreparerFuture> _futures;
			unsigned _completedUpTo = 0;
		};
		auto helper = std::make_shared<Helper>();
		helper->_futures.reserve(shadowGenerators.size());
		for (unsigned operatorIdx=0; operatorIdx<shadowGenerators.size(); ++operatorIdx) {
			assert(shadowGenerators[operatorIdx]._resolveType != ShadowResolveType::SemiStaticProbe && shadowGenerators[operatorIdx]._resolveType != ShadowResolveType::DynamicProbe && shadowGenerators[operatorIdx]._resolveType != ShadowResolveType::SemiStaticAndDynamicProbe);
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
			[helper,shadowGeneratorCopy=std::move(shadowGeneratorCopy),device=pipelineAccelerators->GetDevice()]() mutable {
				using namespace ::Assets;
				std::vector<std::shared_ptr<IShadowPreparer>> actualized;
				actualized.resize(helper->_futures.size());
				auto a=actualized.begin();
				for (auto& p:helper->_futures)
					*a++ = p.get();

				auto finalResult = std::make_shared<PriorityShadowSchedulerUtil>();
				finalResult->_preparers.reserve(actualized.size());
				assert(actualized.size() == shadowGeneratorCopy.size());
				auto i = shadowGeneratorCopy.begin();
				for (auto&a:actualized) {
					finalResult->_preparers.push_back(PriorityShadowSchedulerUtil::Preparer{std::move(a), *i});
					++i;
				}

				finalResult->_device = std::move(device);
				return finalResult;
			});
		return result;
	}

	class DefaultSequencerResourcesDelegate : public Techniques::IShaderResourceDelegate
	{
	public:
		virtual void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst)
		{
			dst[0] = _normalsFitting.get();
			dst[1] = _ggxTable.get();
			dst[2] = _balancedNoise.get();
			context.RequireCommandList(_completionCmdList);
		}

		DefaultSequencerResourcesDelegate(
			const std::shared_ptr<Techniques::DeferredShaderResource>& normalsFittingResource,
			const std::shared_ptr<Techniques::DeferredShaderResource>& ggxTableResource,
			const std::shared_ptr<Techniques::DeferredShaderResource>& balancedNoise)
		{
			BindResourceView(0, "NormalsFittingTexture"_h);
			BindResourceView(1, "GGXTable"_h);
			BindResourceView(2, "NoiseTexture"_h);
			_normalsFitting = normalsFittingResource->GetShaderResource();
			_ggxTable = ggxTableResource->GetShaderResource();
			_balancedNoise = balancedNoise->GetShaderResource();
			_completionCmdList = std::max(normalsFittingResource->GetCompletionCommandList(), ggxTableResource->GetCompletionCommandList());
			_completionCmdList = std::max(_completionCmdList, balancedNoise->GetCompletionCommandList());
		}
		std::shared_ptr<IResourceView> _normalsFitting, _ggxTable, _balancedNoise;
		BufferUploads::CommandListID _completionCmdList;
	};

	std::future<std::shared_ptr<Techniques::IShaderResourceDelegate>> CreateDefaultSequencerResourceDelegate()
	{
		auto normalsFittingTexture = ::Assets::GetAssetFuturePtr<Techniques::DeferredShaderResource>(NORMALS_FITTING_TEXTURE);
		auto ggxTableTexture = ::Assets::GetAssetFuturePtr<Techniques::DeferredShaderResource>(GGX_TABLE_TEXTURE);
		auto compileRequest = Assets::MakeTextureCompilationRequest(Assets::TextureCompiler_BalancedNoise(256, 256), Format::R8_UNORM);
		auto balancedNoise = ::Assets::GetAssetFuturePtr<Techniques::DeferredShaderResource>(compileRequest);
		std::promise<std::shared_ptr<Techniques::IShaderResourceDelegate>> promise;
		auto result = promise.get_future();
		::Assets::WhenAll(std::move(normalsFittingTexture), std::move(ggxTableTexture), std::move(balancedNoise)).ThenConstructToPromise(
			std::move(promise),
			[](const auto& zero, const auto& one, const auto& two) {
				return std::make_shared<DefaultSequencerResourcesDelegate>(zero, one, two);
			});
		return result;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////

	struct AttachedShadowProbes
	{
		unsigned _staticProbeDatabaseEntry = ~0u;
		unsigned _dynamicCubeDatabaseEntry = ~0u;
		int _probeFading = 0;
	};

	struct SharedProbeSceneSet
	{
		// we just maintain a parallel list of the light probes we're interested in
		struct ProbeEntry : public IPositionalLightSource, public IFiniteLightSource
		{
			ShadowProbes::Probe _probeDesc;
			unsigned _attachedProbeTableIndex = ~0u;
			int _fading = 0;
			IPositionalLightSource* _positionalChain = nullptr;
			IFiniteLightSource* _finiteChain = nullptr;
			IUniformEmittance* _uniformEmittanceChain = nullptr;
			AttachedShadowProbes* _attachedShadowProbe = nullptr;

			void SetStaticProbeTableIndex(unsigned newValue)
			{
				_attachedProbeTableIndex = newValue;
				assert(_attachedShadowProbe); _attachedShadowProbe->_staticProbeDatabaseEntry = newValue;
			}

			void SetDynamicProbeTableIndex(unsigned newValue)
			{
				_attachedProbeTableIndex = newValue;
				assert(_attachedShadowProbe); _attachedShadowProbe->_dynamicCubeDatabaseEntry = newValue;
			}

			void ResetStaticProbeTable()
			{
				_attachedProbeTableIndex = ~0u;
				assert(_attachedShadowProbe); _attachedShadowProbe->_staticProbeDatabaseEntry = ~0u;
			}

			void ResetDynamicProbeTable()
			{
				_attachedProbeTableIndex = ~0u;
				assert(_attachedShadowProbe); _attachedShadowProbe->_dynamicCubeDatabaseEntry = ~0u;
			}

			void SetStaticFading(int newValue)
			{
				_fading = newValue;
				assert(_attachedShadowProbe); _attachedShadowProbe->_probeFading = newValue;
			}

			void SetDynamicFading(int newValue)
			{
				_fading = newValue;
				assert(_attachedShadowProbe); _attachedShadowProbe->_probeFading = newValue;
			}

			void SetLocalToWorld(const Float4x4& newLocalToWorld) override
			{
				_probeDesc._objectToWorld = AsFloat3x4(newLocalToWorld);
				_probeDesc._nearRadius = ExtractUniformScaleFast(AsFloat3x4(newLocalToWorld));
				if (_positionalChain)
					_positionalChain->SetLocalToWorld(newLocalToWorld);
			}
			Float4x4 GetLocalToWorld() const override { assert(_positionalChain); return _positionalChain->GetLocalToWorld(); }

			void SetCutoffBrightness(float cutoffBrightness) override
			{
				// Note that since _srcFinite may be null, we have to duplicate the calculations here from IFiniteLightSource
				Float3 brightnessRGB{1.f, 1.f, 1.f};
				if (_uniformEmittanceChain)
					brightnessRGB = _uniformEmittanceChain->GetBrightness();

				auto brightness = std::max(std::max(brightnessRGB[0], brightnessRGB[1]), brightnessRGB[2]);
				if (cutoffBrightness < brightness) {
					SetCutoffRange(std::sqrt(brightness / cutoffBrightness - 1.0f));
				} else {
					// The light can't actually get as bright as the cutoff brightness.. just set to a small value
					SetCutoffRange(1e-3f);
				}
			}
			void SetCutoffRange(float cutoff) override
			{
				_probeDesc._farRadius = cutoff;
				if (_finiteChain) _finiteChain->SetCutoffRange(cutoff);
			}
			float GetCutoffRange() const override
			{
				return _probeDesc._farRadius;
			}
		};
		std::deque<ProbeEntry> _probes;
		BitHeap _activeProbes;
		ILightSceneComponent::QueryInterfaceFunction _qi;

		void RegisterLight(unsigned index)
		{
			if (_probes.size() <= index)
				_probes.resize(index+1);

			ProbeEntry newProbe;
			newProbe._probeDesc._objectToWorld = Identity<Float3x4>();
			newProbe._probeDesc._nearRadius = 1.f;
			newProbe._probeDesc._farRadius = 1024.f;
			newProbe._probeDesc._dimensionality = TextureDesc::Dimensionality::CubeMap;
			if ((newProbe._positionalChain = (IPositionalLightSource*)_qi(index, TypeHashCode<IPositionalLightSource>))) {
				auto localToWorld = newProbe._positionalChain->GetLocalToWorld();
				newProbe._probeDesc._objectToWorld = AsFloat3x4(localToWorld);
				newProbe._probeDesc._nearRadius = ExtractUniformScaleFast(newProbe._probeDesc._objectToWorld);
			}
			if ((newProbe._finiteChain = (IFiniteLightSource*)_qi(index, TypeHashCode<IFiniteLightSource>)))
				newProbe._probeDesc._farRadius = newProbe._finiteChain->GetCutoffRange();
			newProbe._uniformEmittanceChain = (IUniformEmittance*)_qi(index, TypeHashCode<IUniformEmittance>);
			newProbe._attachedShadowProbe = (AttachedShadowProbes*)_qi(index, TypeHashCode<AttachedShadowProbes>);

			_probes[index] = std::move(newProbe);
			_activeProbes.Allocate(index);
		}

		void DeregisterLight(unsigned index)
		{
			_activeProbes.Deallocate(index);
			_probes[index] = {};
		}

		void SetQI(ILightSceneComponent::QueryInterfaceFunction&& qi)
		{
			_qi = std::move(qi);
			unsigned idx = 0;
			for (auto& probe:_probes) {
				probe._positionalChain = (IPositionalLightSource*)_qi(idx, TypeHashCode<IPositionalLightSource>);
				probe._finiteChain = (IFiniteLightSource*)_qi(idx, TypeHashCode<IFiniteLightSource>);
				probe._uniformEmittanceChain = (IUniformEmittance*)_qi(idx, TypeHashCode<IUniformEmittance>);
				probe._attachedShadowProbe = (AttachedShadowProbes*)_qi(idx, TypeHashCode<AttachedShadowProbes>);
				++idx;
			}
		}

		SharedProbeSceneSet() = default;
		SharedProbeSceneSet(SharedProbeSceneSet&&) = default;
		SharedProbeSceneSet& operator=(SharedProbeSceneSet&&) = default;
	};

	static uint32_t GetSetIndex(uint64_t lightIndex) { return lightIndex >> 32; }
	static uint32_t GetLightIndex(uint64_t lightIndex) { return uint32_t(lightIndex); }

	std::shared_ptr<IProbeRenderingInstance> SemiStaticShadowProbeScheduler::BeginPrepare(IThreadContext& threadContext, unsigned maxProbeCount)
	{
		// Can be called in a background thread -- begins prepare for the most important queued probes, as 
		// calculated in the last OnFrameBarrier

		std::vector<std::pair<unsigned, ShadowProbes::Probe>> probesToPrepare;
		{
			ScopedLock(_lock);
			if (_lastEvalBestRenders.empty()) return nullptr;

			probesToPrepare.reserve(_lastEvalBestRenders.size());
			uint64_t probeSlotsToUse = _lastEvalAvailableProbeSlots;
			assert(_probeSlotsReservedInBackground == 0);
			assert(_probeSlotsPreparedInBackground.empty());
			_probeSlotsReservedInBackground = 0;
			_probeSlotsPreparedInBackground.clear();
			_readyToCommitBackgroundChanges = false;

			for (auto q:_lastEvalBestRenders) {
				if (probesToPrepare.size() >= maxProbeCount) break;

				const auto& comp = _sceneSets[GetSetIndex(q)];
				if (!comp->_activeProbes.IsAllocated(GetLightIndex(q)))
					continue;	// deregistered at some point

				auto instanceProbeSlot = xl_ctz8(probeSlotsToUse);
				assert(instanceProbeSlot < 64);
				probeSlotsToUse &= ~(1ull << uint64_t(instanceProbeSlot));

				auto probeDesc = comp->_probes[GetLightIndex(q)]._probeDesc;
				probeDesc._nearRadius = std::max(probeDesc._nearRadius, _defaultNearRadius);
				probesToPrepare.emplace_back(instanceProbeSlot, probeDesc);

				_probeSlotsReservedInBackground |= 1ull << uint64_t(instanceProbeSlot);
				_probeSlotsPreparedInBackground.emplace_back(q, instanceProbeSlot);
			}

			// note -- eviction based on _probeSlotsReservedInBackground will be performed in the foreground on the next 
			// OnFrameBarrier
		}

		if (probesToPrepare.empty()) return nullptr;
		return _shadowProbes->PrepareStaticProbes(threadContext, MakeIteratorRange(probesToPrepare));
	}

	void SemiStaticShadowProbeScheduler::EndPrepare(IThreadContext& threadContext)
	{
		ScopedLock(_lock);
		_readyToCommitBackgroundChanges = true;
	}

	void SemiStaticShadowProbeScheduler::CommitBackgroundChangesAlreadyLocked()
	{
		assert(!_probeSlotsPreparedInBackground.empty());
		assert(_probeSlotsReservedInBackground != 0);
		assert(_readyToCommitBackgroundChanges);

		// Assign the probes we just completed into the main list
		// note -- scheduling is complicated here, since we've just completed and queued the GPU commands

		auto i = _allocatedDatabaseEntries.begin();
		for (auto q:_probeSlotsPreparedInBackground) {
			auto& comp = _sceneSets[GetSetIndex(q.first)];
			if (!comp->_activeProbes.IsAllocated(GetLightIndex(q.first))) {
				// Light was deregistered while begin prepared. The probe slot should just become unassociated
				_unassociatedProbeSlots |= 1ull << uint64_t(q.second);
				continue;
			}

			i = LowerBound2(MakeIteratorRange(i, _allocatedDatabaseEntries.end()), q.first);
			assert(i == _allocatedDatabaseEntries.end() || i->first != q.first);		// attempting to assign a light that is already assigned to a slot

			AllocatedDatabaseEntry p;
			p._databaseIndex = q.second;
			p._fading = 1;		// begins at minimum fade in
			i = _allocatedDatabaseEntries.insert(i, {q.first, p});
			_unassociatedProbeSlots &= ~(1ull << uint64_t(q.second));

			comp->_probes[GetLightIndex(q.first)].SetStaticProbeTableIndex(p._databaseIndex);
			comp->_probes[GetLightIndex(q.first)].SetStaticFading(p._fading);
		}

		_probeSlotsReservedInBackground = 0;
		_doneInitialBackgroundPrepare |= !_probeSlotsPreparedInBackground.empty();
		_probeSlotsPreparedInBackground.clear();
		_readyToCommitBackgroundChanges = false;
	}

	void SemiStaticShadowProbeScheduler::SetNearRadius(float nearRadius) { _defaultNearRadius = nearRadius; }
	float SemiStaticShadowProbeScheduler::GetNearRadius(float) { return _defaultNearRadius; }
	void SemiStaticShadowProbeScheduler::SetFadeTransition(unsigned newValue) { _fadeTransitionInFrames = newValue; }

	auto SemiStaticShadowProbeScheduler::OnFrameBarrier(const Float3& newViewPosition, float drawDistance) -> OnFrameBarrierResult
	{
		ScopedLock(_lock);

		if (_probeSlotsReservedInBackground) {

			// Ensure that none of the current lights are using any of the probes we're going to rewrite now
			// Scheduling here is a little complicated, since we're going to rewrite this probe instance pretty very
			// soon, we don't want it to be read from
			// this is actually the "evict" step
			for (auto l=_allocatedDatabaseEntries.begin(); l!=_allocatedDatabaseEntries.end();) {
				auto bit = 1ull << uint64_t(l->second._databaseIndex);
				if (_probeSlotsReservedInBackground & bit) {
					auto& inComponent = _sceneSets[GetSetIndex(l->first)]->_probes[GetLightIndex(l->first)];
					inComponent.ResetStaticProbeTable();
					inComponent.SetStaticFading(0);

					_unassociatedProbeSlots |= (1ull << uint64_t(l->second._databaseIndex));
					l=_allocatedDatabaseEntries.erase(l);
				} else
					++l;
			}

			if (_readyToCommitBackgroundChanges) {
				CommitBackgroundChangesAlreadyLocked();
			} else {
				// just have to advance fading state
				for (auto& l:_allocatedDatabaseEntries) {
					l.second._fading = std::min(l.second._fading+1, int(_fadeTransitionInFrames));
					_sceneSets[GetSetIndex(l.first)]->_probes[GetLightIndex(l.first)].SetStaticFading(l.second._fading);
				}
				return OnFrameBarrierResult::BackgroundOperationOngoing;
			}
		}

		// Given the current set of lights, calculate the optimal use of a finite number of shadow probe database entries
		// The easiest way to do this is to just the sort the list of lights we have by distance
		// but ideally this should really be tied into some visibility solution -- and perhaps avoid updating every frame
		std::vector<std::pair<LightIndex, float>> lightsAndDistance;
		lightsAndDistance.reserve(256);
		for (unsigned compIdx=0; compIdx<_sceneSets.size(); ++compIdx) {
			auto& comp = _sceneSets[compIdx];
			if (!comp) continue;
			unsigned offset = 0;
			for (auto q:comp->_activeProbes.InternalArray()) {
				q = ~q;		// bit heap inverts allocations
				while (q) {
					auto idx = xl_ctz8(q);
					q ^= 1ull << uint64_t(idx);
					idx += offset;
					auto& probe = comp->_probes[idx]._probeDesc;
					lightsAndDistance.emplace_back((uint64_t(compIdx) << 32ull) | idx, Magnitude(ExtractTranslation(probe._objectToWorld)-newViewPosition) - probe._farRadius);
				}
				offset += 64;
			}
		}

		if (lightsAndDistance.size() > _probeSlotsCount) {
			// find the smallest N items and then restore sort order
			std::nth_element(lightsAndDistance.begin(), lightsAndDistance.begin()+_probeSlotsCount, lightsAndDistance.end(), CompareSecond2{});
			lightsAndDistance.erase(lightsAndDistance.begin()+_probeSlotsCount, lightsAndDistance.end());
			std::sort(lightsAndDistance.begin(), lightsAndDistance.end(), CompareFirst2{});
		}
		
		// compare to the list lights currently in the database and figure out
		// evictions and new renderings
		using LightIndexAndDistance = std::pair<LightIndex, float>;
		VLA_UNSAFE_FORCE(LightIndexAndDistance, potentialNewRenders, lightsAndDistance.size());
		unsigned potentialRenderCount = 0;

		auto currentStateIterator = _allocatedDatabaseEntries.begin();
		auto newStateIterator = lightsAndDistance.begin();
		assert(_probeSlotsCount <= 64u);	// has to be small, because we're going to use a bitfield in a uint64_t
		while (newStateIterator != lightsAndDistance.end()) {
			while (currentStateIterator != _allocatedDatabaseEntries.end() && currentStateIterator->first < newStateIterator->first) {
				// This light fell out of the close lights list
				currentStateIterator->second._fading = std::max(currentStateIterator->second._fading-1, 0);
				if (!currentStateIterator->second._fading) {
					_unassociatedProbeSlots |= 1ull << uint64_t(currentStateIterator->second._databaseIndex);
					auto& inComponent = _sceneSets[GetSetIndex(currentStateIterator->first)]->_probes[GetLightIndex(currentStateIterator->first)];
					inComponent.ResetStaticProbeTable();
					inComponent.SetStaticFading(0);
					currentStateIterator = _allocatedDatabaseEntries.erase(currentStateIterator);
				} else
					++currentStateIterator;
			}
			while (newStateIterator != lightsAndDistance.end() && (currentStateIterator == _allocatedDatabaseEntries.end() || newStateIterator->first < currentStateIterator->first)) {
				// This light is new to the close lights list. Note that newStateIterator->second is distance - cutoff range
				if (newStateIterator->second < drawDistance)
					potentialNewRenders[potentialRenderCount++] = *newStateIterator;
				++newStateIterator;
			}

			if (currentStateIterator != _allocatedDatabaseEntries.end() && newStateIterator != lightsAndDistance.end() && currentStateIterator->first == newStateIterator->first) {
				currentStateIterator->second._fading = std::min(currentStateIterator->second._fading+1, int(_fadeTransitionInFrames));
				auto& inComponent = _sceneSets[GetSetIndex(currentStateIterator->first)]->_probes[GetLightIndex(currentStateIterator->first)];
				inComponent.SetStaticFading(currentStateIterator->second._fading);
				assert(inComponent._attachedProbeTableIndex == currentStateIterator->second._databaseIndex);
				++currentStateIterator;
				++newStateIterator;
			}
		}

		// all remaining lights fell off the close lights list
		while (currentStateIterator!=_allocatedDatabaseEntries.end()) {
			currentStateIterator->second._fading = std::max(currentStateIterator->second._fading-1, 0);
			if (!currentStateIterator->second._fading) {
				_unassociatedProbeSlots |= 1ull << uint64_t(currentStateIterator->second._databaseIndex);
				auto& inComponent = _sceneSets[GetSetIndex(currentStateIterator->first)]->_probes[GetLightIndex(currentStateIterator->first)];
				inComponent.ResetStaticProbeTable();
				inComponent.SetStaticFading(0);
				currentStateIterator = _allocatedDatabaseEntries.erase(currentStateIterator);
			} else
				++currentStateIterator;
		}

		uint64_t availableProbeSlots = _unassociatedProbeSlots;
		// avoid stealing something begin written to in the background right now
		availableProbeSlots &= ~_probeSlotsReservedInBackground;

		// If we have some lights to render, we need to prioritize them and record
		auto freeSlotCount =  countbits(availableProbeSlots);
		if (potentialRenderCount && freeSlotCount) {
			if (freeSlotCount < potentialRenderCount) {
				std::partial_sort(potentialNewRenders, potentialNewRenders+freeSlotCount, potentialNewRenders+potentialRenderCount, CompareSecond2{});
				potentialRenderCount = freeSlotCount;
			} else {
				std::sort(potentialNewRenders, potentialNewRenders+potentialRenderCount, CompareSecond2{});
			}
			_lastEvalBestRenders.clear();
			_lastEvalBestRenders.reserve(potentialRenderCount);
			for (unsigned c=0; c<potentialRenderCount; ++c) _lastEvalBestRenders.push_back(potentialNewRenders[c].first);
		} else {
			_lastEvalBestRenders.clear();
		}
		_lastEvalAvailableProbeSlots = availableProbeSlots;

		return _lastEvalBestRenders.empty() ? OnFrameBarrierResult::NoChange : OnFrameBarrierResult::QueuedRenders;
	}

	void SemiStaticShadowProbeScheduler::RegisterLight(unsigned setIdx, unsigned lightIdx)
	{
		_sceneSets[setIdx]->RegisterLight(lightIdx);
	}

	void SemiStaticShadowProbeScheduler::DeregisterLight(unsigned setIdx, unsigned lightIdx)
	{
		_sceneSets[setIdx]->DeregisterLight(lightIdx);

		// remove it from our allocated list, if it's there
		for (auto i=_allocatedDatabaseEntries.begin(); i!=_allocatedDatabaseEntries.end(); ++i)
			if (GetSetIndex(i->first) == setIdx && GetLightIndex(i->first) == lightIdx) {
				_unassociatedProbeSlots |= 1ull << uint64_t(i->second._databaseIndex);
				_allocatedDatabaseEntries.erase(i);
				break;
			}
	}

	bool SemiStaticShadowProbeScheduler::BindToSet(ILightScene::LightOperatorId op, unsigned setIdx, QueryInterfaceFunction&& qi)
	{
		if (op >= _maskedLightOperators.size() || !_maskedLightOperators[op]) return false;
		if (_sceneSets.size() <= setIdx)
			_sceneSets.resize(setIdx+1);
		if (!_sceneSets[setIdx]) _sceneSets[setIdx] = std::make_unique<SharedProbeSceneSet>();
		_sceneSets[setIdx]->SetQI(std::move(qi));
		return true;
	}

	void* SemiStaticShadowProbeScheduler::QueryInterface(unsigned setIdx, unsigned lightIdx, uint64_t interfaceTypeCode)
	{
		if (!_sceneSets[setIdx]) return nullptr;
		switch(interfaceTypeCode) {
		case TypeHashCode<IPositionalLightSource>:
			return (IPositionalLightSource*)&_sceneSets[setIdx]->_probes[lightIdx];
		case TypeHashCode<IFiniteLightSource>:
			return (IFiniteLightSource*)&_sceneSets[setIdx]->_probes[lightIdx];
		case TypeHashCode<ISemiStaticShadowProbeScheduler>:
			return (ISemiStaticShadowProbeScheduler*)this;
		default:
			return nullptr;
		}
	}

	SemiStaticShadowProbeScheduler::SemiStaticShadowProbeScheduler(std::shared_ptr<ShadowProbes> shadowProbes, const std::vector<bool>& maskedLightOperators)
	: _shadowProbes(std::move(shadowProbes)), _maskedLightOperators(maskedLightOperators)
	{
		_probeSlotsCount = _shadowProbes->GetReservedProbeCount();
		assert(_probeSlotsCount <= 64);
		_unassociatedProbeSlots = (_probeSlotsCount == 64u) ? ~0ull : ((1ull << uint64_t(_probeSlotsCount)) - 1ull);
		_lastEvalBestRenders.reserve(_probeSlotsCount);
		_allocatedDatabaseEntries.reserve(_probeSlotsCount);
		_probeSlotsPreparedInBackground.reserve(_probeSlotsCount);
		_lastEvalBestRenders.reserve(_probeSlotsCount);
	}

	SemiStaticShadowProbeScheduler::~SemiStaticShadowProbeScheduler() {}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void DynamicShadowProbeScheduler::SetNearRadius(float nearRadius) { _defaultNearRadius = nearRadius; }
	float DynamicShadowProbeScheduler::GetNearRadius(float) { return _defaultNearRadius; }
	void DynamicShadowProbeScheduler::SetFadeTransition(unsigned newValue) { _fadeTransitionInFrames = newValue; }

	void DynamicShadowProbeScheduler::UpdateActiveLights(const Float3& newViewPosition, float drawDistance, const Float4x4& worldToClipSpace)
	{
		AccurateFrustumTester frustumTester { worldToClipSpace, Techniques::GetDefaultClipSpaceType() };

		// Given the current set of lights, calculate the optimal use of a finite number of shadow probe database entries
		// The easiest way to do this is to just the sort the list of lights we have by distance
		// but ideally this should really be tied into some visibility solution -- and perhaps avoid updating every frame
		std::vector<std::pair<LightIndex, float>> lightsAndDistance;
		lightsAndDistance.reserve(256);
		for (unsigned compIdx=0; compIdx<_sceneSets.size(); ++compIdx) {
			auto& comp = _sceneSets[compIdx];
			if (!comp) continue;
			unsigned offset = 0;
			for (auto q:comp->_activeProbes.InternalArray()) {
				q = ~q;		// bit heap inverts allocations
				while (q) {
					auto idx = xl_ctz8(q);
					q ^= 1ull << uint64_t(idx);
					idx += offset;
					auto& probe = comp->_probes[idx]._probeDesc;
					auto probePosition = ExtractTranslation(probe._objectToWorld);
					float dist = Magnitude(probePosition-newViewPosition) - probe._farRadius;
					if (dist < drawDistance) {
						if (frustumTester.TestSphere(probePosition, probe._farRadius) != CullTestResult::Culled)
							lightsAndDistance.emplace_back((uint64_t(compIdx) << 32ull) | idx, dist);
					}
				}
				offset += 64;
			}
		}

		auto maxLights = _probeTableFaceReserved / 6;
		if (lightsAndDistance.size() > maxLights) {
			// find the smallest N items and then restore sort order
			std::nth_element(lightsAndDistance.begin(), lightsAndDistance.begin()+maxLights, lightsAndDistance.end(), CompareSecond2{});
			lightsAndDistance.erase(lightsAndDistance.begin()+maxLights, lightsAndDistance.end());
			std::sort(lightsAndDistance.begin(), lightsAndDistance.end(), CompareFirst2{});
		}
		
		// compare to the list lights currently in the database and figure out
		// evictions and new renderings
		using LightIndexAndDistance = std::pair<LightIndex, float>;
		VLA_UNSAFE_FORCE(LightIndexAndDistance, potentialNewRenders, lightsAndDistance.size());
		unsigned potentialRenderCount = 0;

		auto currentStateIterator = _activeLights[0].begin();
		auto& updatedState = _activeLights[1];
		updatedState.clear();

		auto LookupProbeEntry = [this](LightIndex lightIndex) -> SharedProbeSceneSet::ProbeEntry& { return this->_sceneSets[GetSetIndex(lightIndex)]->_probes[GetLightIndex(lightIndex)]; };

		{
			auto newDistancesIterator = lightsAndDistance.begin();
			assert(_probeTableFaceReserved <= 64u*6);	// has to be small, because we're going to use a bitfield in a uint64_t
			while (newDistancesIterator != lightsAndDistance.end()) {

				if (currentStateIterator != _activeLights[0].end() && currentStateIterator->first < newDistancesIterator->first) {

					// This light fell out of the close lights list
					auto& inComponent = LookupProbeEntry(currentStateIterator->first);
					inComponent.SetDynamicFading(currentStateIterator->second._fading = std::max(currentStateIterator->second._fading-1, 0));
					currentStateIterator->second._active = frustumTester.TestSphere(ExtractTranslation(inComponent._probeDesc._objectToWorld), inComponent._probeDesc._farRadius) != CullTestResult::Culled;
					if (currentStateIterator->second._fading!=0)
						updatedState.emplace_back(*currentStateIterator);
					++currentStateIterator;

				} else if (currentStateIterator != _activeLights[0].end() && currentStateIterator->first == newDistancesIterator->first) {

					// no change
					currentStateIterator->second._fading = std::min(currentStateIterator->second._fading+1, int(_fadeTransitionInFrames));
					currentStateIterator->second._active = true;
					auto& inComponent = LookupProbeEntry(currentStateIterator->first);
					inComponent.SetDynamicFading(currentStateIterator->second._fading);
					updatedState.emplace_back(*currentStateIterator++);
					++newDistancesIterator;

				} else {

					// This light is new to the close lights list
					updatedState.emplace_back(newDistancesIterator->first, ActiveLight{ ~0u, 1, true });
					++newDistancesIterator;

				}
			}

			// all remaining lights fell off the close lights list
			while (currentStateIterator!=_activeLights[0].end()) {
				auto& inComponent = LookupProbeEntry(currentStateIterator->first);
				inComponent.SetDynamicFading(currentStateIterator->second._fading = std::max(currentStateIterator->second._fading-1, 0));
				currentStateIterator->second._active = frustumTester.TestSphere(ExtractTranslation(inComponent._probeDesc._objectToWorld), inComponent._probeDesc._farRadius) != CullTestResult::Culled;
				if (currentStateIterator->second._fading!=0)
					updatedState.emplace_back(*currentStateIterator);
				++currentStateIterator;
			}
		}

		// we can have too many due to the fading process slowing down evictions. In this case we must prioritize removals based some heuristic
		// that considers distance, fading and new light vs old light
		if (updatedState.size() > maxLights) {
			using SlotAndScore = std::pair<unsigned, float>;
			VLA_UNSAFE_FORCE(SlotAndScore, scores, updatedState.size());
			auto* s = scores;
			for (auto& u:updatedState) {
				float fadeFactor = u.second._fading / float(_fadeTransitionInFrames);
				auto& inComponent = LookupProbeEntry(u.first);
				auto probePosition = ExtractTranslation(inComponent._probeDesc._objectToWorld);
				float dist = Magnitude(probePosition-newViewPosition) - inComponent._probeDesc._farRadius;
				if (u.second._clusterIndex == ~0u && u.second._active) fadeFactor = 0.33f;		// just added this frame, so let's bias the "fading" factor up a bit -- otherwise it would be very low
				float offScreenFactor = u.second._active ? 1.f : 0.25f;
				float score = fadeFactor*fadeFactor * (drawDistance-dist)*(drawDistance-dist) * offScreenFactor * offScreenFactor;
				*s++ = {unsigned(&u-updatedState.data()), score};
			}
			auto countToRemove = updatedState.size() - maxLights;
			std::nth_element(scores, scores+countToRemove, scores+updatedState.size(), CompareSecond2{});		// smallest scores to the front
			std::sort(scores, scores+countToRemove, CompareFirst2{});
			for (unsigned c=0; c<countToRemove; ++c)
				updatedState.erase(updatedState.begin()+scores[countToRemove-c-1].first);
			assert(updatedState.size() == maxLights);
		}

		// Update clustering (this also assign database slot indices)
		UpdateClustering();

		// Ensure lights that have become inactive are cleared of their database allocation
		{
			auto i2 = _activeLights[1].begin();
			for (auto& i:_activeLights[0]) {
				if (!i.second._active) continue;
				while (i2!=_activeLights[1].end() && i2->first < i.first) ++i2;
				if (i2==_activeLights[1].end() || i2->first != i.first || !i2->second._active)
					LookupProbeEntry(i.first).ResetDynamicProbeTable();		// received shadowing last frame, but will not this frame
			}
		}

		// swap in the new state
		std::swap(_activeLights[0], _activeLights[1]);
	}

	static float Sq(float x) { return x*x; }

	static constexpr unsigned s_maxProbesPerCluster = 5;				// 5 * 6 = 30, just under the limit of 32 projections
	static constexpr float s_maxSeparationWithinCluster = 10.f;		// lights with 10m or more between them will never trigger a cluster

	void DynamicShadowProbeScheduler::UpdateClustering()
	{
		//
		// We can cluster the rendering two combine two separate things:
		//	1) merging scene parse steps
		//	2) merging draw calls with multi-view instancing
		//
		// In theory, we may be able to call the results of multiple scene parse steps during a single preparer
		// execute. That would allow us to disconnect the granularity of these two things to some extent.
		// However, this may not work perfectly, because we may be recording the active views on a per object level
		// during the parse step.
		// (see, eg, PlacementsRenderer::Pimpl::BuildDrawablesViewMasks)
		//
		// Meaning there's an expectation of a one-to-one mapping between the views on the parse step and the views
		// that the shader sees when finally rendering.
		//
		// At the shader level, there's a limit on the number of views that can be active. If we go the simple route
		// of one scene parse per render step, can use this to constrain our clustering. Ideally we want to cluster to
		// maximize the effectiveness of the ArbitraryConvexVolumeTester in the parse step.
		//

		auto& activeLights = _activeLights[1];

		const auto maxLights = _probeTableFaceReserved / 6;
		const auto baseClusterCount = (maxLights + s_maxProbesPerCluster - 1) / s_maxProbesPerCluster;
		auto activeProbeCount = std::accumulate(b2e(activeLights), 0u, [](unsigned lhs, const auto& q) { return lhs+(unsigned)q.second._active; });

		using ClusterIndex = unsigned;
		std::vector<ClusterIndex> clusterAssignments;
		{
			auto LookupProbeEntry = [this](LightIndex lightIndex) -> SharedProbeSceneSet::ProbeEntry& { return this->_sceneSets[GetSetIndex(lightIndex)]->_probes[GetLightIndex(lightIndex)]; };

			struct ClusterHelper
			{
				Float3 _position; float _radius;
				unsigned _activeLightIndex = 0;
			};
			unsigned clusterHelperCount = 0;

			VLA_UNSAFE_FORCE(ClusterHelper, clusterHelpers, activeLights.size());
			VLA_UNSAFE_FORCE(Float3, radii, activeLights.size());
			for (unsigned c=0; c<activeLights.size(); ++c) {
				if (!activeLights[c].second._active) continue;
				const auto& desc = LookupProbeEntry(activeLights[c].first)._probeDesc;
				clusterHelpers[clusterHelperCount]._position = ExtractTranslation(desc._objectToWorld);
				clusterHelpers[clusterHelperCount]._radius = desc._farRadius;
				clusterHelpers[clusterHelperCount]._activeLightIndex = c;
				++clusterHelperCount;
			}

			// start with every probe in it's own cluster
			VLA(unsigned, clusterAssignments, clusterHelperCount);
			VLA(unsigned, clusterCounts, clusterHelperCount);
			VLA(unsigned, starts, clusterHelperCount);		// (indexed by cluster)
			VLA(unsigned, nexts, clusterHelperCount);
			VLA_UNSAFE_FORCE(Float4, clusterBestAxis, clusterHelperCount);
			for (unsigned c=0; c<clusterHelperCount; ++c) { clusterAssignments[c] = c; clusterCounts[c] = 1; starts[c] = c; nexts[c] = ~0u; clusterBestAxis[c] = Zero<Float4>(); }

			// Generate distances between probes, and then sort -- this is the expensive part of this algorithm
			std::vector<std::tuple<float, unsigned, unsigned>> probeDistances;
			probeDistances.reserve(clusterHelperCount*clusterHelperCount/2);
			for (unsigned i=0; i<clusterHelperCount; ++i)
				for (unsigned j=i+1; j<clusterHelperCount; ++j)
					if (float dist = MagnitudeSquared(clusterHelpers[i]._position-clusterHelpers[j]._position)-Sq(clusterHelpers[i]._radius+clusterHelpers[j]._radius); dist < s_maxSeparationWithinCluster)
						probeDistances.emplace_back(dist, i, j);
			std::sort(b2e(probeDistances), [](const auto& lhs, const auto& rhs) { return g<0>(lhs) < g<0>(rhs); });

			// greedily merge together clusters were we can
			for (auto& d:probeDistances) {
				auto c0 = clusterAssignments[std::get<1>(d)], c1 = clusterAssignments[std::get<2>(d)];
				if (c0 == c1 || (clusterCounts[c0]+clusterCounts[c1] > s_maxProbesPerCluster)) continue;
				
				auto c0Start = starts[c0], c1Start = starts[c1];
				for (unsigned c=c1Start; c!=~0u; c=nexts[c]) clusterAssignments[c] = c0;

				auto c0End = std::get<1>(d); while (nexts[c0End] != ~0u) c0End = nexts[c0End];
				nexts[c0End] = c1Start; starts[c1] = c0Start;

				clusterCounts[c0] += clusterCounts[c1]; clusterCounts[c1] = 0;

				if (clusterBestAxis[c1][3] > clusterBestAxis[c0][3]) clusterBestAxis[c0] = clusterBestAxis[c1];
				if (std::get<0>(d) > clusterBestAxis[c0][3])
					clusterBestAxis[c0] = Float4(clusterHelpers[std::get<1>(d)]._position-clusterHelpers[std::get<2>(d)]._position, std::get<0>(d));
			}

			// remap cluster indices to ensure they are dense
			// also assign database indices at this stage (these must be in cluster order)
			for (auto& a:activeLights) { a.second._clusterIndex = ~0u; }
			_clusterBestAxes.clear();
			_clusterBestAxes.reserve(clusterHelperCount);
			unsigned denseClusterIdx = 0;
			for (unsigned c=0; c<clusterHelperCount; ++c) {
				if (!clusterCounts[c]) continue;

				for (auto h=starts[c]; h!=~0u; h=nexts[h])
					activeLights[clusterHelpers[h]._activeLightIndex].second._clusterIndex = denseClusterIdx;
				auto axis = Truncate(clusterBestAxis[c]);
				if (!Normalize_Checked(&axis, axis)) axis = Float3{1,0,0};		// will trigger when the cluster only has a single element
				_clusterBestAxes.push_back(axis);
				++denseClusterIdx;
			}
			_clusterCount = denseClusterIdx;
		}
	}

	void DynamicShadowProbeScheduler::DoShadowPrepare(
		SequenceIterator& iterator,
		Sequence& sequence)
	{
		auto viewPosition = ExtractTranslation(iterator._parsingContext->GetProjectionDesc()._cameraToWorld);
		auto farClip = iterator._parsingContext->GetProjectionDesc()._farClip;
		UpdateActiveLights(viewPosition, farClip, iterator._parsingContext->GetProjectionDesc()._worldToProjection);
		if (!_clusterCount) return;

		struct WorkingCluster { SequenceParseId _parseId; };

		// generate scene parse operations & prepare steps
		auto LookupProbeEntry = [this](LightIndex lightIndex) -> SharedProbeSceneSet::ProbeEntry& { return this->_sceneSets[GetSetIndex(lightIndex)]->_probes[GetLightIndex(lightIndex)]; };
		unsigned nextProbeTableFaceIdx = 0;
		std::vector<ShadowProbes::Probe> updatedProbesInDatabaseOrder;		// subframe heap candidate
		updatedProbesInDatabaseOrder.reserve(_probeTableFaceReserved/6);

		for (unsigned c=0; c<_clusterCount; ++c) {

			// scene parse
			WorkingCluster workingCluster;
			std::vector<Techniques::ProjectionDesc> projDescs;		// subframe heap candidate (we're actually only going to use a few members from this)
			projDescs.reserve(_probeTableFaceReserved);
			unsigned firstFaceIndex = nextProbeTableFaceIdx;
			{
				// Orient the cluster bounding box such that the axis between the two most distant lights is one of the cardinal axes
				Float3 clusterMins { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
				Float3 clusterMaxs { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
				auto clusterToWorld = MakeObjectToWorld(_clusterBestAxes[c], Float3{0,0,1}, Float3{0,0,0});

				for (auto& a:_activeLights[0])
					if (a.second._clusterIndex == c) {
						auto& probe = LookupProbeEntry(a.first)._probeDesc;
						LookupProbeEntry(a.first).SetDynamicProbeTableIndex(nextProbeTableFaceIdx + unsigned(projDescs.size()));
						WriteProjectionDescs(projDescs, {&probe, &probe+1});
						updatedProbesInDatabaseOrder.push_back(probe);

						// sphere rules
						auto p = ExtractTranslation(probe._objectToWorld);
						p = TransformPointByOrthonormalInverse(clusterToWorld, p);
						clusterMins[0] = std::min(clusterMins[0], p[0] - probe._farRadius);
						clusterMins[1] = std::min(clusterMins[1], p[1] - probe._farRadius);
						clusterMins[2] = std::min(clusterMins[2], p[2] - probe._farRadius);
						clusterMaxs[0] = std::max(clusterMaxs[0], p[0] + probe._farRadius);
						clusterMaxs[1] = std::max(clusterMaxs[1], p[1] + probe._farRadius);
						clusterMaxs[2] = std::max(clusterMaxs[2], p[2] + probe._farRadius);
					}

				auto volumeTester = std::make_shared<ArbitraryConvexVolumeTester>(ArbitraryConvexVolumeTesterFromAABB(clusterToWorld, clusterMins, clusterMaxs));
				workingCluster._parseId = sequence.CreateMultiViewParseScene(Techniques::BatchFlags::Opaque, std::vector<Techniques::ProjectionDesc>{projDescs}, std::move(volumeTester));
			}

			assert(!projDescs.empty());
			nextProbeTableFaceIdx += unsigned(projDescs.size());

			// preparation step
			sequence.CreateStep_CallFunction(
				[this, workingCluster, projDescs=std::move(projDescs), firstFaceIndex, firstCluster=c==0](SequenceIterator& iterator) {

					if (firstCluster)
						this->_shadowProbes->Bind(*iterator._parsingContext);

					auto rpi = this->_shadowProbes->Begin(*iterator._parsingContext, projDescs, firstFaceIndex);
					if (auto cfg = this->_shadowProbes->GetSequencerConfig())		// returns null if still pending
						iterator.ExecuteDrawables(workingCluster._parseId, *cfg);
					rpi.End();
				});
		}

		sequence.CreateStep_CallFunction(
			[this, updatedProbesInDatabaseOrder=std::move(updatedProbesInDatabaseOrder)](SequenceIterator& iterator) {
				this->_shadowProbes->Unbind(*iterator._parsingContext, updatedProbesInDatabaseOrder);
			});

		_probeTableFaceUsed = nextProbeTableFaceIdx;
		assert(_probeTableFaceUsed <= _probeTableFaceReserved);
	}

	void DynamicShadowProbeScheduler::ClearPreparedShadows()
	{
	}

	void DynamicShadowProbeScheduler::BarrierToReadingLayout(Techniques::ParsingContext& parsingContext)
	{
		_shadowProbes->BarrierToReadingLayout(parsingContext);
	}

	void DynamicShadowProbeScheduler::RegisterLight(unsigned setIdx, unsigned lightIdx)
	{
		_sceneSets[setIdx]->RegisterLight(lightIdx);
	}

	void DynamicShadowProbeScheduler::DeregisterLight(unsigned setIdx, unsigned lightIdx)
	{
		_sceneSets[setIdx]->DeregisterLight(lightIdx);

		// remove it from our allocated list, if it's there
		auto id = (uint64_t(setIdx) << 32ull) | lightIdx;
		for (auto i=_activeLights[0].begin(); i!=_activeLights[0].end(); ++i)
			if (i->first == id) { _activeLights[0].erase(i); break; }
	}

	bool DynamicShadowProbeScheduler::BindToSet(ILightScene::LightOperatorId op, unsigned setIdx, QueryInterfaceFunction&& qi)
	{
		if (op >= _maskedLightOperators.size() || !_maskedLightOperators[op]) return false;

		if (_sceneSets.size() <= setIdx) _sceneSets.resize(setIdx+1);
		if (!_sceneSets[setIdx]) _sceneSets[setIdx] = std::make_unique<SharedProbeSceneSet>();
		_sceneSets[setIdx]->SetQI(std::move(qi));
		return true;
	}

	void* DynamicShadowProbeScheduler::QueryInterface(unsigned setIdx, unsigned lightIdx, uint64_t interfaceTypeCode)
	{
		if (!_sceneSets[setIdx]) return nullptr;

		switch(interfaceTypeCode) {
		case TypeHashCode<IPositionalLightSource>:
			return (IPositionalLightSource*)&_sceneSets[setIdx]->_probes[lightIdx];
		case TypeHashCode<IFiniteLightSource>:
			return (IFiniteLightSource*)&_sceneSets[setIdx]->_probes[lightIdx];
		default:
			return nullptr;
		}
	}

	DynamicShadowProbeScheduler::DynamicShadowProbeScheduler(
		std::shared_ptr<DynamicShadowProbes> shadowProbes,
		const std::vector<bool>& maskedLightOperators)
	: _shadowProbes(std::move(shadowProbes)), _maskedLightOperators(maskedLightOperators)
	{
		_probeTableFaceReserved = _shadowProbes->GetReservedFaceCount();
		_probeTableFaceUsed = 0;
		assert(_probeTableFaceReserved <= 64*6);
		_activeLights[0].reserve(_probeTableFaceReserved*2);
		_activeLights[1].reserve(_probeTableFaceReserved*2);		// allow some overfill during UpdateActiveLights
	}

	DynamicShadowProbeScheduler::~DynamicShadowProbeScheduler() {}

	auto DynamicShadowProbeScheduler::GetMetrics() const -> Metrics
	{
		Metrics metrics;
		metrics._probeTableFaceUsed = _probeTableFaceUsed;
		metrics._probeTableFaceReserved = _probeTableFaceReserved;
		const auto& cfg = _shadowProbes->GetConfiguration(); 
		metrics._probeTableSizeBytes = (cfg._faceDims * cfg._faceDims * BitsPerPixel(cfg._format) / 8) * cfg._maxProbes * 6;

		auto LookupProbeEntry = [this](LightIndex lightIndex) -> const SharedProbeSceneSet::ProbeEntry& { return this->_sceneSets[GetSetIndex(lightIndex)]->_probes[GetLightIndex(lightIndex)]; };

		metrics._activeLights.reserve(_activeLights[0].size());
		for (auto& a:_activeLights[0]) {
			if (!a.second._active) continue;
			assert(a.second._clusterIndex < _clusterCount);
			Metrics::Light l;
			auto& probeEntry = LookupProbeEntry(a.first);
			l._position = ExtractTranslation(probeEntry._probeDesc._objectToWorld);
			l._radius = probeEntry._probeDesc._farRadius;
			l._dimensionality = probeEntry._probeDesc._dimensionality;
			l._attachedProbeTableIndex = probeEntry._attachedProbeTableIndex;
			l._fading = a.second._fading;
			l._clusterIndex = a.second._clusterIndex;
			metrics._activeLights.push_back(l);
		}

		for (auto& c:_clusterBestAxes)
			metrics._clusters.push_back(Metrics::Cluster{c});

		return metrics;
	}

	std::shared_ptr<IResourceView> DynamicShadowProbeScheduler::GetCubeMapSRV(unsigned activeLightIdx)
	{
		if (activeLightIdx >= _activeLights[0].size()) return nullptr;
		auto LookupProbeEntry = [this](LightIndex lightIndex) -> const SharedProbeSceneSet::ProbeEntry& { return this->_sceneSets[GetSetIndex(lightIndex)]->_probes[GetLightIndex(lightIndex)]; };
		TextureViewDesc viewDesc;
		viewDesc._format._aspect = TextureViewDesc::Aspect::Depth;
		viewDesc._arrayLayerRange = { LookupProbeEntry(_activeLights[0][activeLightIdx].first)._attachedProbeTableIndex, 6 };
		viewDesc._dimensionality = TextureDesc::Dimensionality::T2D;
		return _shadowProbes->GetDynamicProbeTable().GetResource()->CreateTextureView(BindFlag::ShaderResource, viewDesc);
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void DominantLightSet::WriteEnvProps(CB_EnvironmentProps& dst)
	{
		if (_hasLight) {
			assert(_standardPositionalLight);
			dst._dominantLight = Internal::MakeLightUniforms(*_standardPositionalLight, _uniformShapeCode);
		}
	}

	DominantLightSet::DominantLightSet(ILightScene::LightOperatorId lightOpId, unsigned uniformShapeCode)
	: _lightOpId(lightOpId), _uniformShapeCode(uniformShapeCode)
	{}
	DominantLightSet::~DominantLightSet() {}

	void DominantLightSet::RegisterLight(unsigned setIdx, unsigned lightIdx)
	{
		assert(setIdx == _setIdx);
		if (_hasLight) Throw(std::runtime_error("Attempting to add multiple dominant lights. Only one is supported."));
		assert(lightIdx == 0);
		_hasLight = true;
		_standardPositionalLight = (StandardPositionalLight*)_qi(lightIdx, TypeHashCode<StandardPositionalLight>);
	}

	void DominantLightSet::DeregisterLight(unsigned setIdx, unsigned lightIdx)
	{
		assert(setIdx == _setIdx);
		assert(_hasLight);
		assert(lightIdx == 0);
		_hasLight = false;
	}

	bool DominantLightSet::BindToSet(ILightScene::LightOperatorId opId, unsigned setIdx, QueryInterfaceFunction&& qi)
	{
		if (opId != _lightOpId)
			return false;
		assert(_setIdx == ~0u);
		_setIdx = setIdx;
		_qi = std::move(qi);
		if (_hasLight)
			_standardPositionalLight = (StandardPositionalLight*)_qi(0, TypeHashCode<StandardPositionalLight>);
		return true;
	}

	void* DominantLightSet::QueryInterface(unsigned setIdx, unsigned lightIdx, uint64_t interfaceTypeCode)
	{
		return nullptr;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct TiledLightScheduler::SceneSet
	{
		struct LightEntry : public IPositionalLightSource, public IFiniteLightSource, public AttachedShadowProbes
		{
			StandardPositionalLight* _standardLight = nullptr;
			IPositionalLightSource* _positionalChain = nullptr;
			IFiniteLightSource* _finiteChain = nullptr;
			unsigned _idForTiler = ~0u;
			Float3 _position { 0.f, 0.f, 0.f };
			float _cutoffRange = 1.f;
			SceneSet* _parent = nullptr;
		
			void SetLocalToWorld(const Float4x4& newLocalToWorld) override
			{
				_position = ExtractTranslation(newLocalToWorld);
				_parent->_tiler->UpdateLight(_position, _cutoffRange, _idForTiler);
				if (_positionalChain) _positionalChain->SetLocalToWorld(newLocalToWorld);
			}
			Float4x4 GetLocalToWorld() const override { assert(_positionalChain); return _positionalChain->GetLocalToWorld(); }

			void SetCutoffBrightness(float cutoffBrightness) override
			{
				if (_finiteChain) {
					_finiteChain->SetCutoffBrightness(cutoffBrightness);
					_cutoffRange = _finiteChain->GetCutoffRange();
					_parent->_tiler->UpdateLight(_position, _cutoffRange, _idForTiler);
				} else
					assert(0);		// not going to do much without a _finiteChain
			}
			void SetCutoffRange(float cutoff) override
			{
				_cutoffRange = cutoff;
				if (_finiteChain) _finiteChain->SetCutoffRange(cutoff);
				_parent->_tiler->UpdateLight(_position, _cutoffRange, _idForTiler);
			}
			float GetCutoffRange() const override { return _cutoffRange; }
		};
		std::deque<LightEntry> _lights;
		std::shared_ptr<RasterizationLightTileOperator> _tiler;
		ILightSceneComponent::QueryInterfaceFunction _qi;
		LightOperatorInfo _operatorInfo;
		unsigned _setIdForTiler = 0;

		void RegisterLight(unsigned index)
		{
			if (_lights.size() <= index)
				_lights.resize(index+1);

			LightEntry newLight;
			newLight._idForTiler = _setIdForTiler|index;
			newLight._parent = this;
			if ((newLight._positionalChain = (IPositionalLightSource*)_qi(index, TypeHashCode<IPositionalLightSource>))) {
				auto localToWorld = newLight._positionalChain->GetLocalToWorld();
				newLight._position = ExtractTranslation(localToWorld);
			}
			if ((newLight._finiteChain = (IFiniteLightSource*)_qi(index, TypeHashCode<IFiniteLightSource>)))
				newLight._cutoffRange = newLight._finiteChain->GetCutoffRange();
			newLight._standardLight = (StandardPositionalLight*)_qi(index, TypeHashCode<StandardPositionalLight>);

			assert(_lights[index]._idForTiler == ~0u);		// if you hit this, it means we're about the register the same light multiple times with the tiler
			_tiler->AddLight(newLight._position, newLight._cutoffRange, newLight._idForTiler);
			_lights[index] = std::move(newLight);
		}

		void DeregisterLight(unsigned index)
		{
			_tiler->RemoveLight(_lights[index]._idForTiler);
			_lights[index] = {};
		}

		void SetQI(ILightSceneComponent::QueryInterfaceFunction&& qi)
		{
			_qi = std::move(qi);
			unsigned idx = 0;
			for (auto& light:_lights) {
				light._positionalChain = (IPositionalLightSource*)_qi(idx, TypeHashCode<IPositionalLightSource>);
				light._finiteChain = (IFiniteLightSource*)_qi(idx, TypeHashCode<IFiniteLightSource>);
				light._standardLight = (StandardPositionalLight*)_qi(idx, TypeHashCode<StandardPositionalLight>);
				++idx;
			}
		}
	};

	static CB_Light MakeLightUniforms(const StandardPositionalLight& light, const AttachedShadowProbes& shadowProbes, unsigned shapeCode)
	{
		return CB_Light 
			{
				(shapeCode == 0) ? light._unitLengthPosition : light._position, light._cutoffRange,
				light._brightness, light._radii[0],
				ExtractRight(light._orientation), light._radii[1],
				ExtractForward(light._orientation), shapeCode,
				ExtractUp(light._orientation), light._coneCosTheta,
				shadowProbes._staticProbeDatabaseEntry+1, shadowProbes._dynamicCubeDatabaseEntry+1,			// add 1 for shader indexing (note that ~0u will become 0, meaning no probe database entry)
				{0,0}
			};
	}

	void TiledLightScheduler::DoPrepareUniforms(Techniques::ParsingContext& parsingContext)
	{
		++_pingPongCounter;

		auto& uniforms = _uniforms[_pingPongCounter%dimof(_uniforms)];
		auto& tilerOutputs = _lightTiler->_outputs;
		auto& device = *parsingContext.GetThreadContext().GetDevice();
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
				auto setIdx = *idx >> 24, lightIdx = (*idx)&0xffffff;
				auto& lightDesc = _sceneSets[setIdx]->_lights[lightIdx];	// expensive array lookup
				*i = MakeLightUniforms(*lightDesc._standardLight, lightDesc, _sceneSets[setIdx]->_operatorInfo._uniformShapeCode);
			}

			map.FlushCache();
		}

		// additional copy from our shared memory mapping into gpu-only memory
		if (tilerOutputs._lightCount && _unmapLightList) {
			auto& metalContext = *Metal::DeviceContext::Get(parsingContext.GetThreadContext());
			auto bltEncoder = metalContext.BeginBlitEncoder();
			bltEncoder.Copy(
				*_unmapLightList,
				RenderCore::CopyPartial_Src{*uniforms._lightList, 0, unsigned(sizeof(Internal::CB_Light)*tilerOutputs._lightCount)});
			bltEncoder.Copy(
				*_unmapDepthTable,
				RenderCore::CopyPartial_Src{*uniforms._lightDepthTable, 0, unsigned(sizeof(unsigned)*tilerOutputs._lightDepthTable.size())});
		}
	}

	void TiledLightScheduler::WriteEnvProps(CB_EnvironmentProps& dst)
	{
		dst._lightCount = _lightTiler->_outputs._lightCount;
	}

	void TiledLightScheduler::RegisterLight(unsigned setIdx, unsigned lightIdx)
	{
		_sceneSets[setIdx]->RegisterLight(lightIdx);
	}

	void TiledLightScheduler::DeregisterLight(unsigned setIdx, unsigned lightIdx)
	{
		_sceneSets[setIdx]->DeregisterLight(lightIdx);
	}

	bool TiledLightScheduler::BindToSet(ILightScene::LightOperatorId op, unsigned setIdx, QueryInterfaceFunction&& qi)
	{
		if (op >= _operatorInfos.size() || !_operatorInfos[op]._tileable) return false;
		if (_sceneSets.size() <= setIdx)
			_sceneSets.resize(setIdx+1);
		if (!_sceneSets[setIdx]) _sceneSets[setIdx] = std::make_unique<SceneSet>();
		_sceneSets[setIdx]->_setIdForTiler = setIdx << 24u;
		_sceneSets[setIdx]->_tiler = _lightTiler;
		_sceneSets[setIdx]->SetQI(std::move(qi));
		_sceneSets[setIdx]->_operatorInfo = _operatorInfos[op];
		return true;
	}

	void* TiledLightScheduler::QueryInterface(unsigned setIdx, ILightScene::LightSourceId lightIdx, uint64_t interfaceTypeCode)
	{
		if (!_sceneSets[setIdx]) return nullptr;
		switch(interfaceTypeCode) {
		case TypeHashCode<IPositionalLightSource>:
			return (IPositionalLightSource*)&_sceneSets[setIdx]->_lights[lightIdx];
		case TypeHashCode<IFiniteLightSource>:
			return (IFiniteLightSource*)&_sceneSets[setIdx]->_lights[lightIdx];
		case TypeHashCode<AttachedShadowProbes>:
			return (AttachedShadowProbes*)&_sceneSets[setIdx]->_lights[lightIdx];
		default:
			return nullptr;
		}
	}

	TiledLightScheduler::TiledLightScheduler(
		std::shared_ptr<RasterizationLightTileOperator> lightTiler,
		IteratorRange<const LightOperatorInfo*> operatorInfo)
	: _lightTiler(std::move(lightTiler))
	, _operatorInfos(operatorInfo.begin(), operatorInfo.end())
	{
		auto& device = *_lightTiler->GetDevice();
		auto tilerConfig = _lightTiler->GetConfiguration();
		AllocationRules::BitField allocationRulesForDynamicCBs = AllocationRules::HostVisibleSequentialWrite|AllocationRules::DisableAutoCacheCoherency|AllocationRules::PermanentlyMapped;

		auto lightListDesc = CreateDesc(BindFlag::UnorderedAccess, LinearBufferDesc::Create(sizeof(Internal::CB_Light)*tilerConfig._maxLightsPerView, sizeof(Internal::CB_Light)));
		auto lightDepthTableDesc = CreateDesc(BindFlag::UnorderedAccess, LinearBufferDesc::Create(sizeof(unsigned)*tilerConfig._depthLookupGradiations, sizeof(unsigned)));

		if (tilerConfig._copyOutOfSharedMemory) {
			_unmapLightList = device.CreateResource(lightListDesc, "light-list");
			_unmapDepthTable = device.CreateResource(lightDepthTableDesc, "light-depth-table");
		}

		lightListDesc._allocationRules = lightDepthTableDesc._allocationRules = allocationRulesForDynamicCBs;
		for (unsigned c=0; c<dimof(_uniforms); c++) {
			_uniforms[c]._lightList = device.CreateResource(lightListDesc, "light-list");
			_uniforms[c]._lightDepthTable = device.CreateResource(lightDepthTableDesc, "light-depth-table");
		}

		if (_unmapLightList) {
			_uniforms[0]._lightListUAV = _unmapLightList->CreateBufferView(BindFlag::UnorderedAccess);
			_uniforms[0]._lightDepthTableUAV = _unmapDepthTable->CreateBufferView(BindFlag::UnorderedAccess);
			for (unsigned c=1; c<dimof(_uniforms); c++) {
				_uniforms[c]._lightListUAV = _uniforms[0]._lightListUAV;
				_uniforms[c]._lightDepthTableUAV = _uniforms[0]._lightDepthTableUAV;
			}
		} else {
			for (unsigned c=0; c<dimof(_uniforms); c++) {
				_uniforms[c]._lightListUAV = _uniforms[c]._lightList->CreateBufferView(BindFlag::UnorderedAccess);
				_uniforms[c]._lightDepthTableUAV = _uniforms[c]._lightDepthTable->CreateBufferView(BindFlag::UnorderedAccess);
			}
		}
	}

	TiledLightScheduler::~TiledLightScheduler() {}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void AmbientResourcesScheduler::RegisterLight(LightSetId setIdx, ILightScene::LightSourceId lightIdx)
	{
		assert(setIdx == _boundSet);
		if (_boundLight != ~0u) Throw(std::runtime_error("Multiple lights bound to the AmbientResourcesScheduler"));
		_boundLight = lightIdx;
	}

	void AmbientResourcesScheduler::DeregisterLight(LightSetId setIdx, ILightScene::LightSourceId lightIdx)
	{
		assert(setIdx == _boundSet);
		assert(_boundLight == lightIdx);
		_boundLight = ~0u;
	}

	bool AmbientResourcesScheduler::BindToSet(ILightScene::LightOperatorId opId, LightSetId setIdx, QueryInterfaceFunction&&)
	{
		if (opId != _operatorId) return false;
		if (_boundSet != ~0u) Throw(std::runtime_error("Multiple sets bound to the AmbientResourcesScheduler"));
		_boundSet = setIdx;
		return true;
	}

	void* AmbientResourcesScheduler::QueryInterface(LightSetId setIdx, ILightScene::LightSourceId lightIdx, uint64_t interfaceTypeCode)
	{
		assert(setIdx == _boundSet);
		if (lightIdx != _boundLight) return nullptr;
		if (interfaceTypeCode == TypeHashCode<ISkyTextureProcessor>)
			return _processor.get();
		return nullptr;
	}

	void AmbientResourcesScheduler::SetDiffuseSHCoefficients(const SHCoefficients& coeffients)
	{
		std::memset(_diffuseSHCoefficients, 0, sizeof(_diffuseSHCoefficients));
		std::memcpy(_diffuseSHCoefficients, coeffients.GetCoefficients().begin(), sizeof(Float4)*std::min(coeffients.GetCoefficients().size(), dimof(_diffuseSHCoefficients)));
	}

	void AmbientResourcesScheduler::SetDistantSpecularIBL(std::shared_ptr<IResourceView> resource, BufferUploads::CommandListID completion)
	{
		// When distant specular IBL is disabled, _glossLut will be nullptr
		_distantSpecularIBL = std::move(resource);
		if (!_distantSpecularIBL) _distantSpecularIBL = Techniques::Services::GetCommonResources()->_blackCubeSRV;
		_distantSpecularIBLCompletion = std::max(_distantSpecularIBLCompletion, completion);
	}

	void AmbientResourcesScheduler::BindSkyTextureProcessor(std::shared_ptr<ISkyTextureProcessor> processor)
	{
		assert(!_processor);
		assert(_processorBinding == ~0u);
		_processor = processor;

		_processorBinding = _processor->BindOnChangeIBL(
			[this](std::shared_ptr<IResourceView> specularResource, BufferUploads::CommandListID completionCmdList, SHCoefficients& shCoefficients) {
				// Pass the updated SH coefficients into the light scene
				this->SetDiffuseSHCoefficients(shCoefficients);
				this->SetDistantSpecularIBL(std::move(specularResource), completionCmdList);
			});
	}

	void AmbientResourcesScheduler::Prerender()
	{
		if (_processor) SkyTextureProcessorPrerender(*_processor);
	}

	AmbientResourcesScheduler::AmbientResourcesScheduler(unsigned operatorId)
	: _operatorId(operatorId)
	{
		std::memset(_diffuseSHCoefficients, 0, sizeof(_diffuseSHCoefficients));
		_distantSpecularIBL = Techniques::Services::GetCommonResources()->_blackCubeSRV;
	}

	AmbientResourcesScheduler::~AmbientResourcesScheduler()
	{
		if (_processor && _processorBinding != ~0u) _processor->UnbindOnChangeIBL(_processorBinding);
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	UInt2 ExtractOutputResolution(IteratorRange<const Techniques::PreregisteredAttachment*> preregs)
	{
		auto i = std::find_if(preregs.begin(), preregs.end(), [](const auto& q) { return q._semantic == Techniques::AttachmentSemantics::ColorLDR; });
		if (i == preregs.end())
			Throw(std::runtime_error("Missing output attachment in input interface"));
		return { i->_desc._textureDesc._width, i->_desc._textureDesc._height };
	}

	UInt2 ExtractOutputResolution(IteratorRange<const Techniques::PreregisteredAttachment*> preregs, uint64_t outputSemantic)
	{
		auto i = std::find_if(preregs.begin(), preregs.end(), [outputSemantic](const auto& q) { return q._semantic == outputSemantic; });
		if (i == preregs.end())
			Throw(std::runtime_error("Missing output attachment in input interface"));
		return { i->_desc._textureDesc._width, i->_desc._textureDesc._height };
	}

	void ShaderResourceSplitter::WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst)
	{
		for (unsigned c=0; c<dimof(_subDelegates); ++c) {
			if (!_subDelegates[c]) break;
			_subDelegates[c]->WriteResourceViews(context, objectContext, bindingFlags, {dst.first+_srvOffsets[c], dst.second});
		}
	}

	void ShaderResourceSplitter::WriteSamplers(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<ISampler**> dst)
	{
		for (unsigned c=0; c<dimof(_subDelegates); ++c) {
			if (!_subDelegates[c]) break;
			_subDelegates[c]->WriteSamplers(context, objectContext, bindingFlags, {dst.first+_samplerOffsets[c], dst.second});
		}
	}

	void ShaderResourceSplitter::WriteImmediateData(Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst)
	{
		for (unsigned c=0; c<dimof(_subDelegates); ++c) {
			if (!_subDelegates[c]) break;
			if (idx >= _immDataOffsets[c] && idx < _immDataOffsets[c+1]) {
				_subDelegates[c]->WriteImmediateData(context, objectContext, idx-_immDataOffsets[c], dst);
				break;
			}
		}
	}

	size_t ShaderResourceSplitter::GetImmediateDataSize(Techniques::ParsingContext& context, const void* objectContext, unsigned idx)
	{
		for (unsigned c=0; c<dimof(_subDelegates); ++c) {
			if (!_subDelegates[c]) break;
			if (idx >= _immDataOffsets[c] && idx < _immDataOffsets[c+1])
				return _subDelegates[c]->GetImmediateDataSize(context, objectContext, idx-_immDataOffsets[c]);
		}
		return 0;
	}

	void ShaderResourceSplitter::Configure()
	{
		for (auto& o:_srvOffsets) o = 0;
		for (auto& o:_samplerOffsets) o = 0;
		for (auto& o:_immDataOffsets) o = 0;
		_completionCmdList = 0;

		unsigned nextSrvOffset = 0, nextSamplerOffset = 0, nextImmDataOffset = 0;
		unsigned c=0;
		for (auto& d:_subDelegates) {
			if (!d) break;
			_srvOffsets[c] = nextSrvOffset;
			_samplerOffsets[c] = nextSamplerOffset;
			_immDataOffsets[c] = nextImmDataOffset;
			for (auto& srv:d->_interface.GetResourceViewBindings()) _interface.BindResourceView(nextSrvOffset++, srv);
			for (auto& sampler:d->_interface.GetSamplerBindings()) _interface.BindSampler(nextSamplerOffset++, sampler);
			for (auto& imm:d->_interface.GetImmediateDataBindings()) _interface.BindImmediateData(nextImmDataOffset++, imm);
			_completionCmdList = std::max(_completionCmdList, d->_completionCmdList);
			++c;
		}
		_srvOffsets[c] = nextSrvOffset;
		_samplerOffsets[c] = nextSamplerOffset;
		_immDataOffsets[c] = nextImmDataOffset;
	}

	ShaderResourceSplitter::ShaderResourceSplitter(std::shared_ptr<Techniques::IShaderResourceDelegate> zero, std::shared_ptr<Techniques::IShaderResourceDelegate> one)
	{
		_subDelegates[0] = std::move(zero);
		_subDelegates[1] = std::move(one);
		Configure();
	}

	ShaderResourceSplitter::ShaderResourceSplitter(std::shared_ptr<Techniques::IShaderResourceDelegate> zero, std::shared_ptr<Techniques::IShaderResourceDelegate> one, std::shared_ptr<Techniques::IShaderResourceDelegate> two)
	{
		_subDelegates[0] = std::move(zero);
		_subDelegates[1] = std::move(one);
		_subDelegates[2] = std::move(two);
		Configure();
	}

	ShaderResourceSplitter::ShaderResourceSplitter(std::shared_ptr<Techniques::IShaderResourceDelegate> zero, std::shared_ptr<Techniques::IShaderResourceDelegate> one, std::shared_ptr<Techniques::IShaderResourceDelegate> two, std::shared_ptr<Techniques::IShaderResourceDelegate> three)
	{
		_subDelegates[0] = std::move(zero);
		_subDelegates[1] = std::move(one);
		_subDelegates[2] = std::move(two);
		_subDelegates[3] = std::move(three);
		Configure();
	}

}}}

