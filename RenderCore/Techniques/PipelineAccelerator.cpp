// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineAccelerator.h"
#include "PipelineAcceleratorInternal.h"
#include "PipelineCollection.h"
#include "DescriptorSetAccelerator.h"
#include "CompiledShaderPatchCollection.h"
#include "CommonResources.h"
#include "ShaderVariationSet.h"
#include "PipelineOperators.h"		// for CompiledPipelineLayoutAsset & DescriptorSetLayoutAndBinding
#include "DeformAccelerator.h"		// for UniformDeformerToRendererBinding
#include "CompiledLayoutPool.h"
#include "Drawables.h"				// for IDrawablesPool & protected destroy
#include "ResourceConstructionContext.h"
#include "../FrameBufferDesc.h"
#include "../Format.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/ObjectFactory.h"
#include "../Assets/RawMaterial.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Assets/ScaffoldCmdStream.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetHeapLRU.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Streams/PathUtils.h"
#include <cctype>
#include <sstream>
#include <iomanip>

#define PA_SEPARATE_CONTINUATIONS 1		// Set to use a dedicated continuation executor for internal pipeline accelerator pool work. This can help by avoiding interfering with the continuation executor
#if defined(PA_SEPARATE_CONTINUATIONS)
	#include "../Assets/ContinuationExecutor.h"
	#include "thousandeyes/futures/then.h"
#endif

namespace RenderCore { namespace Techniques
{
	using SequencerConfigId = uint64_t;
	class PipelineAcceleratorPool;

	struct PipelineLayoutMarker
	{
		std::shared_ptr<CompiledPipelineLayoutAsset> _pipelineLayout;
		volatile VisibilityMarkerId _visibilityMarker = ~VisibilityMarkerId(0);
		::Assets::DependencyValidation _depVal;
		std::shared_future<std::shared_ptr<CompiledPipelineLayoutAsset>> _pending;
	};

	class SequencerConfig
	{
	public:
		SequencerConfigId _cfgId = ~0ull;

		std::shared_ptr<ITechniqueDelegate> _delegate;
		PipelineLayoutMarker _pipelineLayout;
		ParameterBox _sequencerSelectors;

		FrameBufferDesc _fbDesc;
		unsigned _subpassIdx = 0;
		uint64_t _fbRelevanceValue = 0;
		std::string _name;

		#if defined(_DEBUG)
			PipelineAcceleratorPool* _ownerPool = nullptr;
		#endif
	};

	class PipelineAccelerator : public std::enable_shared_from_this<PipelineAccelerator>
	{
	public:
		PipelineAccelerator(
			unsigned ownerPoolId,
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
			const ParameterBox& materialSelectors,
			IteratorRange<const InputElementDesc*> inputAssembly,
			Topology topology,
			const RenderCore::Assets::RenderStateSet& stateSet);
		PipelineAccelerator(
			unsigned ownerPoolId,
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
			const ParameterBox& materialSelectors,
			IteratorRange<const MiniInputElementDesc*> inputAssembly,
			Topology topology,
			const RenderCore::Assets::RenderStateSet& stateSet);
		~PipelineAccelerator();
	
		// "_completedPipelines" is protected by the _pipelineUsageLock lock in the pipeline accelerator pool
		using Pipeline = IPipelineAcceleratorPool::Pipeline;
		struct CompletedPipeline
		{
			volatile VisibilityMarkerId _visibilityMarker = ~VisibilityMarkerId(0);
			Pipeline _pipeline; 
			::Assets::DependencyValidation _depVal;
			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		};
		std::vector<CompletedPipeline> _completedPipelines;

		// "_pendingPipelines" is protected by the _constructionLock in the pipeline accelerator pool
		using FuturePipeline = std::shared_future<Pipeline>;
		std::vector<std::pair<uint32_t, FuturePipeline>> _pendingPipelines;		// sequencer index -> FuturePipeline

		std::shared_future<Pipeline> BeginPrepareForSequencerStateAlreadyLocked(
			std::shared_ptr<SequencerConfig> cfg,
			const ParameterBox& globalSelectors,
			const std::shared_ptr<PipelineCollection>& pipelineCollection,
			ICompiledLayoutPool& layoutPatcher);

		void BeginPrepareForSequencerStateInternal(
			std::promise<IPipelineAcceleratorPool::Pipeline>&& resultPromise,
			std::shared_ptr<CompiledShaderPatchCollection> compiledPatchCollection,
			PipelineCollection& pipelineCollection, const ParameterBox& globalSelectors, std::shared_ptr<SequencerConfig> cfg, PipelineLayoutMarker& pipelineLayoutMarker);

		bool HasCurrentOrFuturePipeline(const SequencerConfig& cfg) const;

		Pipeline* TryGetPipeline(const SequencerConfig& cfg, VisibilityMarkerId visibilityMarker);
	
		std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> _shaderPatches;
		ParameterBox _materialSelectors;
		ParameterBox _geoSelectors;
		std::vector<InputElementDesc> _inputAssembly;
		std::vector<MiniInputElementDesc> _miniInputAssembly;
		Topology _topology;
		RenderCore::Assets::RenderStateSet _stateSet;

		unsigned _ownerPoolId;
	};

	void PipelineAccelerator::BeginPrepareForSequencerStateInternal(
		std::promise<IPipelineAcceleratorPool::Pipeline>&& resultPromise,
		std::shared_ptr<CompiledShaderPatchCollection> compiledPatchCollection,
		PipelineCollection& pipelineCollection, const ParameterBox& globalSelectors, std::shared_ptr<SequencerConfig> cfg, PipelineLayoutMarker& pipelineLayoutMarker)
	{
		// Use our copy of PipelineLayoutMarker (rather than the one in cfg) to avoid and race conditions
		std::shared_ptr<CompiledPipelineLayoutAsset> actualPipelineLayoutAsset = pipelineLayoutMarker._pipelineLayout;
		if (!actualPipelineLayoutAsset) {
			assert(pipelineLayoutMarker._pending.valid());
			YieldToPool(pipelineLayoutMarker._pending);		// this case should be uncommon, and YieldToPool is a lot simplier than making pipelineLayoutMarker._pending an optional precondition
			actualPipelineLayoutAsset = pipelineLayoutMarker._pending.get();
		}

		const ParameterBox* paramBoxes[] = {
			&cfg->_sequencerSelectors,
			&_geoSelectors,
			&_materialSelectors,
			&globalSelectors
		};

		std::shared_future<std::shared_ptr<GraphicsPipelineDesc>> pipelineDescFuture = cfg->_delegate->GetPipelineDesc(compiledPatchCollection->GetInterface(), _stateSet);
		VertexInputStates vis { _inputAssembly, _miniInputAssembly, _topology };
		auto metalPipelineFuture = std::make_shared<::Assets::Marker<Techniques::GraphicsPipelineAndLayout>>();
		pipelineCollection.CreateGraphicsPipeline(
			metalPipelineFuture->AdoptPromise(),
			actualPipelineLayoutAsset->GetPipelineLayout(), pipelineDescFuture,
			MakeIteratorRange(paramBoxes), 
			vis, FrameBufferTarget{&cfg->_fbDesc, cfg->_subpassIdx}, compiledPatchCollection);

		std::weak_ptr<PipelineAccelerator> weakThis = shared_from_this();
		::Assets::WhenAll(metalPipelineFuture, std::move(pipelineDescFuture)).ThenConstructToPromise(
			std::move(resultPromise),
			[cfg=std::move(cfg), weakThis](
				GraphicsPipelineAndLayout metalPipeline, auto pipelineDesc) {

				auto containingPipelineAccelerator = weakThis.lock();
				if (!containingPipelineAccelerator)
					Throw(std::runtime_error("Containing GraphicsPipeline builder has been destroyed"));

				IPipelineAcceleratorPool::Pipeline result;
				result._metalPipeline = std::move(metalPipeline._pipeline);
				result._depVal = metalPipeline._depVal;
				#if defined(_DEBUG)
					result._vsDescription = metalPipeline._debugInfo._vsDescription;
					result._psDescription = metalPipeline._debugInfo._psDescription;
					result._gsDescription = metalPipeline._debugInfo._gsDescription;
				#endif
				return result;
			});
	}

	auto PipelineAccelerator::BeginPrepareForSequencerStateAlreadyLocked(
		std::shared_ptr<SequencerConfig> cfg,
		const ParameterBox& globalSelectors,
		const std::shared_ptr<PipelineCollection>& pipelineCollection,
		ICompiledLayoutPool& layoutPatcher) -> std::shared_future<Pipeline>
	{
		std::promise<Pipeline> pipelinePromise;
		std::shared_future<Pipeline> futurePipeline = pipelinePromise.get_future();
		ParameterBox copyGlobalSelectors = globalSelectors;
		std::weak_ptr<PipelineAccelerator> weakThis = shared_from_this();
		auto patchCollectionFuture = _shaderPatches ? layoutPatcher.GetPatchCollectionFuture(*_shaderPatches) : layoutPatcher.GetDefaultPatchCollectionFuture();

		// capture the "PipelineLayoutMarker" while inside of the lock
		auto pipelineLayoutMarker = cfg->_pipelineLayout;
		unsigned sequencerIdx = unsigned(cfg->_cfgId);

		// Queue massive chain of future continuation functions (it's not as scary as it looks)
		//
		//    CompiledShaderPatchCollection -> GraphicsPipelineDesc -> Metal::GraphicsPipeline
		//
		// Note there may be an issue here in that if the shader compile fails, the dep val for the 
		// final pipeline will only contain the dependencies for the shader. So if the root problem
		// is actually something about the configuration, we won't get the proper recompile functionality 
		std::shared_ptr<CompiledShaderPatchCollection> immediatePatchCollection; ::Assets::DependencyValidation patchCollectionDepVal; ::Assets::Blob patchCollectionLog;
		if (patchCollectionFuture->CheckStatusBkgrnd(immediatePatchCollection, patchCollectionDepVal, patchCollectionLog) == ::Assets::AssetState::Ready) {
			TRY {
				BeginPrepareForSequencerStateInternal(
					std::move(pipelinePromise),
					immediatePatchCollection, *pipelineCollection, copyGlobalSelectors, std::move(cfg), pipelineLayoutMarker);
			} CATCH(...) {
				pipelinePromise.set_exception(std::current_exception());
			} CATCH_END
		} else {
			::Assets::WhenAll(patchCollectionFuture).ThenConstructToPromise(
				std::move(pipelinePromise),
				[pipelineCollection, copyGlobalSelectors, cfg=std::move(cfg), pipelineLayoutMarker, weakThis](
					std::promise<IPipelineAcceleratorPool::Pipeline>&& resultPromise,
					std::shared_ptr<CompiledShaderPatchCollection> compiledPatchCollection) mutable {

					TRY {
						auto containingPipelineAccelerator = weakThis.lock();
						if (!containingPipelineAccelerator)
							Throw(std::runtime_error("Containing GraphicsPipeline builder has been destroyed"));
						containingPipelineAccelerator->BeginPrepareForSequencerStateInternal(
							std::move(resultPromise),
							compiledPatchCollection, *pipelineCollection, copyGlobalSelectors, std::move(cfg), pipelineLayoutMarker);
					} CATCH(...) {
						resultPromise.set_exception(std::current_exception());
					} CATCH_END
				});
		}

		auto i = std::find_if(_pendingPipelines.begin(), _pendingPipelines.end(), [sequencerIdx](const auto& p) { return p.first == sequencerIdx; });
		if (i != _pendingPipelines.end()) {
			i->second = futurePipeline;
		} else 
			_pendingPipelines.emplace_back(sequencerIdx, futurePipeline);

#if 0
		// When the future completes, add an note to tell the pool to update the completed pipelines list
		// this should work even if there end up begin multiple pipelines queued for the same sequencer/accelerator pair
		::Assets::WhenAll(futurePipeline).Then(
			[weakThis, sequencerIdx](const auto&) {
				auto containingPipelineAccelerator = weakThis.lock();
				if (!containingPipelineAccelerator)
					Throw(std::runtime_error("Containing GraphicsPipeline builder has been destroyed"));

				ScopedLock(containingPipelineAccelerator->_futuresToCheckLock);
				containingPipelineAccelerator->_futuresToCheck.push_back(sequencerIdx);
			});
#endif
		return futurePipeline;
	}

	bool PipelineAccelerator::HasCurrentOrFuturePipeline(const SequencerConfig& cfg) const
	{
		#if defined(_DEBUG)
			unsigned poolId = unsigned(cfg._cfgId >> 32ull);
			if (poolId != _ownerPoolId)
				Throw(std::runtime_error("Mixing a pipeline accelerator from an incorrect pool"));
		#endif

		// If we have something in _completedPipelines return true
		unsigned sequencerIdx = unsigned(cfg._cfgId);
		assert(sequencerIdx < _completedPipelines.size());
		if (_completedPipelines[sequencerIdx]._pipeline._metalPipeline)
			return true;

		// If we have a pipeline currently in pending state, also return true
		auto i = std::find_if(_pendingPipelines.begin(), _pendingPipelines.end(), [sequencerIdx](const auto& p) { return p.first == sequencerIdx; });
		if (i != _pendingPipelines.end())
			return true;

		return false;
	}

	auto PipelineAccelerator::TryGetPipeline(const SequencerConfig& cfg, VisibilityMarkerId visibilityMarker) -> Pipeline*
	{
		#if defined(_DEBUG)
			unsigned poolId = unsigned(cfg._cfgId >> 32ull);
			if (poolId != _ownerPoolId)
				Throw(std::runtime_error("Mixing a pipeline accelerator from an incorrect pool"));
		#endif

		unsigned sequencerIdx = unsigned(cfg._cfgId);
		assert(_completedPipelines.size() > sequencerIdx);
		if (_completedPipelines[sequencerIdx]._visibilityMarker > visibilityMarker || !_completedPipelines[sequencerIdx]._pipeline._metalPipeline)
			return nullptr;
		return &_completedPipelines[sequencerIdx]._pipeline;
	}

	PipelineAccelerator::PipelineAccelerator(
		unsigned ownerPoolId,
		const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
		const ParameterBox& materialSelectors,
		IteratorRange<const InputElementDesc*> inputAssembly,
		Topology topology,
		const RenderCore::Assets::RenderStateSet& stateSet)
	: _shaderPatches(shaderPatches)
	, _materialSelectors(materialSelectors)
	, _topology(topology)
	, _stateSet(stateSet)
	, _ownerPoolId(ownerPoolId)
	{
		_inputAssembly = {inputAssembly.begin(), inputAssembly.end()};
		std::vector<InputElementDesc> sortedIA = _inputAssembly;
		std::sort(
			sortedIA.begin(), sortedIA.end(),
			[](const InputElementDesc& lhs, const InputElementDesc& rhs) {
				if (lhs._semanticName < rhs._semanticName) return true;
				if (lhs._semanticName > rhs._semanticName) return false;
				return lhs._semanticIndex > rhs._semanticIndex;	// note -- reversing order
			});

		// Build up the geometry selectors. 
		for (auto i = sortedIA.begin(); i!=sortedIA.end(); ++i) {
			StringMeld<256> meld;
			meld << "GEO_HAS_" << i->_semanticName;
			if (i->_semanticIndex != 0)
				meld << i->_semanticIndex;
			_geoSelectors.SetParameter(meld.AsStringSection(), 1);
		}

		// If we have no IA elements at all, force on GEO_HAS_VERTEX_ID. Shaders will almost always
		// require it in this case, because there's no other way to distinquish one vertex from
		// the next.
		if (sortedIA.empty())
			_geoSelectors.SetParameter("GEO_HAS_VERTEX_ID", 1);
	}

	PipelineAccelerator::PipelineAccelerator(
		unsigned ownerPoolId,
		const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
		const ParameterBox& materialSelectors,
		IteratorRange<const MiniInputElementDesc*> miniInputAssembly,
		Topology topology,
		const RenderCore::Assets::RenderStateSet& stateSet)
	: _shaderPatches(shaderPatches)
	, _materialSelectors(materialSelectors)
	, _topology(topology)
	, _stateSet(stateSet)
	, _ownerPoolId(ownerPoolId)
	{
		_miniInputAssembly = {miniInputAssembly.begin(), miniInputAssembly.end()};
		std::vector<MiniInputElementDesc> sortedIA = _miniInputAssembly;
		std::sort(
			sortedIA.begin(), sortedIA.end(),
			[](const MiniInputElementDesc& lhs, const MiniInputElementDesc& rhs) {
				return lhs._semanticHash < rhs._semanticHash;
			});

		// Build up the geometry selectors. 
		for (auto i = sortedIA.begin(); i!=sortedIA.end(); ++i) {
			StringMeld<256> meld;
			auto basicSemantic = CommonSemantics::TryDehash(i->_semanticHash);
			if (basicSemantic.first) {
				meld << "GEO_HAS_" << basicSemantic.first;
				if (basicSemantic.second != 0)
					meld << basicSemantic.second;
				_geoSelectors.SetParameter(meld.AsStringSection(), 1);
			} else {
				// The MiniInputElementDesc is not all-knowing, unfortunately. We can only dehash the
				// "common" semantics
				meld << "GEO_HAS_" << std::hex << i->_semanticHash;
				_geoSelectors.SetParameter(meld.AsStringSection(), 1);
			}
		}

		// If we have no IA elements at all, force on GEO_HAS_VERTEX_ID. Shaders will almost always
		// require it in this case, because there's no other way to distinquish one vertex from
		// the next.
		if (sortedIA.empty())
			_geoSelectors.SetParameter("GEO_HAS_VERTEX_ID", 1);
	}

	PipelineAccelerator::~PipelineAccelerator()
	{}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//		D E S C R I P T O R S E T
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class DescriptorSetAccelerator
	{
	public:
		volatile VisibilityMarkerId _visibilityMarker = ~VisibilityMarkerId(0);
		ActualizedDescriptorSet _completed;

		std::shared_future<std::vector<ActualizedDescriptorSet>> _pending;
		::Assets::DependencyValidation _depVal;		// filled in after _pending has completed
		DescriptorSetBindingInfo _bindingInfo;

		#if defined(_DEBUG)
			PipelineAcceleratorPool* _ownerPool = nullptr;
		#endif
		uint64_t _constructionContextGuid = 0;
	};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//		P   O   O   L
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class PipelineAcceleratorPool : public IPipelineAcceleratorPool
	{
	public:
		std::shared_ptr<PipelineAccelerator> CreatePipelineAccelerator(
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
			const ParameterBox& materialSelectors,
			IteratorRange<const InputElementDesc*> inputAssembly,
			Topology topology,
			const RenderCore::Assets::RenderStateSet& stateSet) override;

		std::shared_ptr<PipelineAccelerator> CreatePipelineAccelerator(
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
			const ParameterBox& materialSelectors,
			IteratorRange<const MiniInputElementDesc*> inputAssembly,
			Topology topology,
			const RenderCore::Assets::RenderStateSet& stateSet) override;

		virtual std::shared_ptr<DescriptorSetAccelerator> CreateDescriptorSetAccelerator(
			const std::shared_ptr<ResourceConstructionContext>& constructionContext,
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
			IteratorRange<Assets::ScaffoldCmdIterator> materialMachine,
			std::shared_ptr<void> memoryHolder,
			const std::shared_ptr<DeformerToDescriptorSetBinding>& deformBinding) override;

		std::shared_ptr<SequencerConfig> CreateSequencerConfig(
			const std::string& name,
			std::shared_ptr<ITechniqueDelegate> delegate,
			const ParameterBox& sequencerSelectors,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex = 0) override;

		std::future<VisibilityMarkerId> GetPipelineMarker(PipelineAccelerator& pipelineAccelerator, const SequencerConfig& sequencerConfig) const override;
		std::future<std::pair<VisibilityMarkerId, BufferUploads::CommandListID>> GetDescriptorSetMarker(DescriptorSetAccelerator& accelerator) const override;
		std::future<VisibilityMarkerId> GetCompiledPipelineLayoutMarker(const SequencerConfig& sequencerConfig) const override;

		void			SetGlobalSelector(StringSection<> name, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type) override;
		T1(Type) void   SetGlobalSelector(StringSection<> name, Type value);
		void			RemoveGlobalSelector(StringSection<> name) override;

		VisibilityMarkerId VisibilityBarrier(VisibilityMarkerId expectedVisibility) override;

		void 			LockForReading() const override;
		void 			UnlockForReading() const override;

		Records LogRecords() const override;

		const std::shared_ptr<IDevice>& GetDevice() const override;
		const std::shared_ptr<ICompiledLayoutPool>& GetCompiledLayoutPool() const override;

		PipelineAcceleratorPool(
			std::shared_ptr<IDevice> device,
			std::shared_ptr<IDrawablesPool> drawablesPool,
			std::shared_ptr<ICompiledLayoutPool> patchCollectionPool,
			PipelineAcceleratorPoolFlags::BitField flags);
		~PipelineAcceleratorPool();
		PipelineAcceleratorPool(const PipelineAcceleratorPool&) = delete;
		PipelineAcceleratorPool& operator=(const PipelineAcceleratorPool&) = delete;

		#if defined(_DEBUG)
			mutable std::optional<std::thread::id> _lockForThreadingThread;
		#endif

	protected:
		//
		// Two main locks:
		//		1. _constructionLock
		//		2. _pipelineUsageLock
		//
		// _constructionLock is used for all construction operations; CreatePipelineAccelerator, CreateSequencerConfig, etc
		// _pipelineUsageLock is used for actually retrieving the pipeline/descriptor set with TryGetPipeline, etc
		// Construction operations can happen in parallel with pipeline usage operations, so different kinds of clients won't
		// interfer with each other. 
		// However, there is an overlap in RebuildAllOutOfDatePipelines() where both locks are taken. This also exposes
		// the changes that were made by construction operations
		//
		mutable Threading::Mutex _constructionLock;
		mutable std::shared_mutex _pipelineUsageLock;
		ParameterBox _globalSelectors;
		std::vector<std::pair<uint64_t, std::weak_ptr<SequencerConfig>>> _sequencerConfigById;
		std::vector<std::pair<uint64_t, std::weak_ptr<PipelineAccelerator>>> _pipelineAccelerators;
		std::vector<std::pair<uint64_t, std::weak_ptr<DescriptorSetAccelerator>>> _descriptorSetAccelerators;

		SequencerConfig MakeSequencerConfig(
			/*out*/ uint64_t& hash,
			std::shared_ptr<ITechniqueDelegate> delegate,
			const ParameterBox& sequencerSelectors,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex);

		void RebuildAllPipelinesAlreadyLocked(unsigned poolGuid);
		void RebuildAllPipelinesAlreadyLocked(unsigned poolGuid, PipelineAccelerator& pipeline, uint64_t pipelineHash);

		std::shared_ptr<SamplerPool> _samplerPool;
		std::shared_ptr<PipelineCollection> _pipelineCollection;
		std::shared_ptr<IDrawablesPool> _drawablesPool;
		std::shared_ptr<ICompiledLayoutPool> _layoutPatcher;
		PipelineAcceleratorPoolFlags::BitField _flags;

		struct FuturesToCheckHelper
		{
			std::atomic<VisibilityMarkerId> _lastPublishedVisibilityMarker;
			Threading::Mutex _lock;
			std::vector<std::pair<uint64_t, SequencerConfigId>> _pipelineFuturesToCheck;
			std::vector<uint64_t> _descSetFuturesToCheck;
		};
		std::shared_ptr<FuturesToCheckHelper> _futuresToCheckHelper;

		std::vector<bool> _lastFrameSequencerConfigExpired;
		unsigned _lastSequencerCfgHotReloadCheck = 0;
		unsigned _lastPipelineAcceleratorHotReloadCheck = 0;

		struct NewlyQueued
		{
			std::shared_future<PipelineAccelerator::Pipeline> _future; uint64_t _acceleratorHash; SequencerConfigId _cfgId; 
			NewlyQueued() = default;
			NewlyQueued(std::shared_future<PipelineAccelerator::Pipeline> future, uint64_t acceleratorHash, SequencerConfigId cfgId) : _future(std::move(future)), _acceleratorHash(acceleratorHash), _cfgId(cfgId) {}
		};
		void SetupNewlyQueuedAlreadyLocked(IteratorRange<const NewlyQueued*>);

		#if defined(PA_SEPARATE_CONTINUATIONS)
			std::shared_ptr<thousandeyes::futures::Executor> _continuationExecutor;
		#endif
	};

	void PipelineAcceleratorPool::LockForReading() const
	{
		_pipelineUsageLock.lock_shared();
		#if defined(_DEBUG)
			assert(!_lockForThreadingThread.has_value());
			_lockForThreadingThread = std::this_thread::get_id();
		#endif
	}

	void PipelineAcceleratorPool::UnlockForReading() const
	{
		#if defined(_DEBUG)
			assert(_lockForThreadingThread.has_value() && _lockForThreadingThread.value() == std::this_thread::get_id());
			_lockForThreadingThread = {};
		#endif
		_pipelineUsageLock.unlock_shared();	
	}

	template<typename ContinuationFn, typename PromisedType, typename... FutureTypes>
		static std::unique_ptr<::Assets::Internal::FlexTimedWaitableWithContinuation<ContinuationFn, PromisedType, std::decay_t<FutureTypes>...>> MakeTimedWaitable(
			std::promise<PromisedType>&& p,
			ContinuationFn&& continuation,
			FutureTypes... futures)
	{
		return std::make_unique<::Assets::Internal::FlexTimedWaitableWithContinuation<ContinuationFn, PromisedType, std::decay_t<FutureTypes>...>>(
			std::chrono::hours(1), std::tuple<std::decay_t<FutureTypes>...>{std::forward<FutureTypes>(futures)...}, std::move(continuation), std::move(p));
	}

	template<typename ContinuationFn, typename... FutureTypes>
		static std::unique_ptr<::Assets::Internal::FlexTimedWaitableJustContinuation<ContinuationFn, std::decay_t<FutureTypes>...>> MakeTimedWaitableJustContinuation(
			ContinuationFn&& continuation,
			FutureTypes... futures)
	{
		return std::make_unique<::Assets::Internal::FlexTimedWaitableJustContinuation<ContinuationFn, std::decay_t<FutureTypes>...>>(
			std::chrono::hours(1), std::tuple<std::decay_t<FutureTypes>...>{std::forward<FutureTypes>(futures)...}, std::move(continuation));
	}

	std::future<VisibilityMarkerId> PipelineAcceleratorPool::GetPipelineMarker(PipelineAccelerator& pipelineAccelerator, const SequencerConfig& sequencerConfig) const
	{
		// We must lock the "_constructionLock" for this -- so it's less advisable to call this often
		// TryGetPipeline doesn't take a lock and is more efficient to call frequently
		// This will also return nullptr if the pipeline has already been completed and is accessable via TryGetPipeline
		ScopedLock(_constructionLock);
		#if defined(_DEBUG)
			unsigned poolId = unsigned(sequencerConfig._cfgId >> 32ull);
			if (poolId != _guid || pipelineAccelerator._ownerPoolId != _guid)
				Throw(std::runtime_error("Mixing a pipeline accelerator from an incorrect pool"));
		#endif

		auto pending = std::find_if(
			pipelineAccelerator._pendingPipelines.begin(), pipelineAccelerator._pendingPipelines.end(),
			[cfgId=uint32_t(sequencerConfig._cfgId)](const auto& p) { return p.first == cfgId; });
		if (pending != pipelineAccelerator._pendingPipelines.end()) {
			std::promise<VisibilityMarkerId> newPromise;
			auto result = newPromise.get_future();
			#if !defined(PA_SEPARATE_CONTINUATIONS)
				::Assets::WhenAll(pending->second).ThenConstructToPromise(
					std::move(newPromise),
					[helper=_futuresToCheckHelper](const auto&) {
						// the visibility marker should always be the next one
						return helper->_lastPublishedVisibilityMarker.load()+1;
					});
			#else
				_continuationExecutor->watch(
					MakeTimedWaitable(
						std::move(newPromise),
						[helper=_futuresToCheckHelper](auto&& promise, auto&&) {
							// the visibility marker should always be the next one
							promise.set_value(helper->_lastPublishedVisibilityMarker.load()+1);
						},
						pending->second));
			#endif
			return result;
		}
		
		auto seqIndex = unsigned(sequencerConfig._cfgId);
		if (seqIndex >= pipelineAccelerator._completedPipelines.size())
			return {};

		auto& completed = pipelineAccelerator._completedPipelines[seqIndex];
		std::promise<VisibilityMarkerId> immediatePromise;
		immediatePromise.set_value((VisibilityMarkerId)completed._visibilityMarker); // includes invalid, & not pending case
		return immediatePromise.get_future();
	}

	std::future<std::pair<VisibilityMarkerId, BufferUploads::CommandListID>> PipelineAcceleratorPool::GetDescriptorSetMarker(DescriptorSetAccelerator& accelerator) const
	{
		ScopedLock(_constructionLock);

		if (accelerator._pending.valid()) {
			std::promise<std::pair<VisibilityMarkerId, BufferUploads::CommandListID>> newPromise;
			auto result = newPromise.get_future();
			#if !defined(PA_SEPARATE_CONTINUATIONS)
				::Assets::WhenAll(accelerator._pending).ThenConstructToPromise(
					std::move(newPromise),
					[helper=_futuresToCheckHelper](const auto& descSetActualQ) {
						// the visibility marker should always be the next one
						auto& descSetActual = *descSetActualQ.begin();
						assert(descSetActual._completionCommandList != ~0u);		// use zero when not required
						return std::make_pair(helper->_lastPublishedVisibilityMarker.load()+1, descSetActual._completionCommandList);
					});
			#else
				_continuationExecutor->watch(
					MakeTimedWaitable(
						std::move(newPromise),
						[helper=_futuresToCheckHelper](auto&& promise, auto&& descSetActualQ) {
								// the visibility marker should always be the next one
							TRY {
								auto& descSetActual = *std::get<0>(descSetActualQ).get().begin();
								assert(descSetActual._completionCommandList != ~0u);		// use zero when not required
								promise.set_value(std::make_pair(helper->_lastPublishedVisibilityMarker.load()+1, descSetActual._completionCommandList));
							} CATCH(...) {
								promise.set_exception(std::current_exception());
							} CATCH_END
						},
						accelerator._pending));
			#endif
			return result;
		}

		std::promise<std::pair<VisibilityMarkerId, BufferUploads::CommandListID>> immediatePromise;
		assert(accelerator._completed._completionCommandList != ~0u);		// use zero when not required
		immediatePromise.set_value(std::make_pair((VisibilityMarkerId)accelerator._visibilityMarker, accelerator._completed._completionCommandList));
		return immediatePromise.get_future();
	}

	std::future<VisibilityMarkerId> PipelineAcceleratorPool::GetCompiledPipelineLayoutMarker(const SequencerConfig& sequencerConfig) const
	{
		ScopedLock(_constructionLock);
		#if defined(_DEBUG)
			unsigned poolId = unsigned(sequencerConfig._cfgId >> 32ull);
			if (poolId != _guid)
				Throw(std::runtime_error("Mixing a pipeline accelerator from an incorrect pool"));
		#endif

		auto& seqConfig = _sequencerConfigById[unsigned(sequencerConfig._cfgId)];
		auto cfg = seqConfig.second.lock();
		if (!cfg)
			return {};
	
		if (cfg->_pipelineLayout._pending.valid()) {
			std::promise<VisibilityMarkerId> newPromise;
			auto result = newPromise.get_future();
			::Assets::WhenAll(cfg->_pipelineLayout._pending).ThenConstructToPromise(
				std::move(newPromise),
				[helper=_futuresToCheckHelper](const auto&) {
					// the visibility marker should always be the next one
					return helper->_lastPublishedVisibilityMarker.load()+1;
				});
			return result;
		}

		std::promise<VisibilityMarkerId> immediatePromise;
		immediatePromise.set_value((VisibilityMarkerId)cfg->_pipelineLayout._visibilityMarker);
		return immediatePromise.get_future();
	}

	const IPipelineAcceleratorPool::Pipeline* TryGetPipeline(
		PipelineAccelerator& pipelineAccelerator,
		const SequencerConfig& sequencerConfig,
		VisibilityMarkerId visibilityMarker)
	{
		#if defined(_DEBUG)
			assert(sequencerConfig._ownerPool);
			assert(sequencerConfig._ownerPool->_lockForThreadingThread.has_value() && sequencerConfig._ownerPool->_lockForThreadingThread.value() == std::this_thread::get_id());
			unsigned poolId = unsigned(sequencerConfig._cfgId >> 32ull);
			if (poolId != pipelineAccelerator._ownerPoolId)
				Throw(std::runtime_error("Mixing a pipeline accelerator from an incorrect pool"));
		#endif
		
		return pipelineAccelerator.TryGetPipeline(sequencerConfig, visibilityMarker);
	}

	const ActualizedDescriptorSet* TryGetDescriptorSet(DescriptorSetAccelerator& accelerator, VisibilityMarkerId visibilityMarker)
	{
		#if defined(_DEBUG)
			assert(accelerator._ownerPool);
			assert(accelerator._ownerPool->_lockForThreadingThread.has_value() && accelerator._ownerPool->_lockForThreadingThread.value() == std::this_thread::get_id());
		#endif
		if (accelerator._visibilityMarker > visibilityMarker) return nullptr;
		return &accelerator._completed;
	}

	std::shared_ptr<ICompiledPipelineLayout> TryGetCompiledPipelineLayout(const SequencerConfig& sequencerConfig, VisibilityMarkerId visibilityMarker)
	{
		#if defined(_DEBUG)
			assert(sequencerConfig._ownerPool);
			assert(sequencerConfig._ownerPool->_lockForThreadingThread.has_value() && sequencerConfig._ownerPool->_lockForThreadingThread.value() == std::this_thread::get_id());
		#endif
		if (sequencerConfig._pipelineLayout._visibilityMarker > visibilityMarker) return nullptr;
		if (!sequencerConfig._pipelineLayout._pipelineLayout) return nullptr;
		return sequencerConfig._pipelineLayout._pipelineLayout->GetPipelineLayout();
	}

	SequencerConfig PipelineAcceleratorPool::MakeSequencerConfig(
		/*out*/ uint64_t& hash,
		std::shared_ptr<ITechniqueDelegate> delegate,
		const ParameterBox& sequencerSelectors,
		const FrameBufferDesc& fbDesc,
		unsigned subpassIndex)
	{
		// Search for an identical sequencer config already registered, and return it
		// if it's here already. Other create it and return the result
		assert(!fbDesc.GetSubpasses().empty());

		SequencerConfig cfg;
		cfg._delegate = std::move(delegate);
		cfg._sequencerSelectors = sequencerSelectors;

		cfg._fbDesc = fbDesc;
		cfg._subpassIdx = subpassIndex;

		// In Vulkan, the "subpass index" value (and subpass count of the containing pipeline layout) is
		// important when building a pipeline. That if there are 2 render passes that contain identical 
		// subpasses, just at different subpass indices, then we can't use the same pipelines in both
		// those subpasses (ie, pipelines created for one aren't compatible with the other)
		//
		// However, we can get away with this on other APIs... When we can, we can use this trick to
		// take advantage of it
		const bool apiSupportsSeparatingSubpasses = false;
		if ((subpassIndex != 0 || fbDesc.GetSubpasses().size() > 1) && apiSupportsSeparatingSubpasses) {
			cfg._fbDesc = SeparateSingleSubpass(fbDesc, subpassIndex);
			cfg._subpassIdx = 0;
		}

		cfg._fbRelevanceValue = Metal::GraphicsPipelineBuilder::CalculateFrameBufferRelevance(cfg._fbDesc, cfg._subpassIdx);

		hash = HashCombine(sequencerSelectors.GetHash(), sequencerSelectors.GetParameterNamesHash());
		hash = HashCombine(cfg._fbRelevanceValue, hash);

		// todo -- we must take into account the delegate itself; it must impact the hash
		hash = HashCombine(uint64_t(cfg._delegate.get()), hash);

		return cfg;
	}

	static void DestroyPipelineAccelerator(void* obj) { delete (PipelineAccelerator*)obj; }

	std::shared_ptr<PipelineAccelerator> PipelineAcceleratorPool::CreatePipelineAccelerator(
		const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
		const ParameterBox& materialSelectors,
		IteratorRange<const InputElementDesc*> inputAssembly,
		Topology topology,
		const RenderCore::Assets::RenderStateSet& stateSet)
	{
		ScopedLock(_constructionLock);

		uint64_t hash = HashCombine(materialSelectors.GetHash(), materialSelectors.GetParameterNamesHash());
		hash = HashInputAssembly(inputAssembly, hash);
		hash = HashCombine((unsigned)topology, hash);
		hash = HashCombine(stateSet.GetHash(), hash);
		if (shaderPatches)
			hash = HashCombine(shaderPatches->GetHash(), hash);

		// If it already exists in the cache, just return it now
		auto i = LowerBound(_pipelineAccelerators, hash);
		if (i != _pipelineAccelerators.end() && i->first == hash) {
			auto l = i->second.lock();
			if (l)
				return l;
		}

		std::shared_ptr<PipelineAccelerator> newAccelerator;
		if (_drawablesPool) {
			newAccelerator = _drawablesPool->MakeProtectedPtr<PipelineAccelerator>(
				_guid,
				shaderPatches, materialSelectors,
				inputAssembly, topology,
				stateSet);
		} else {
			newAccelerator = std::make_shared<PipelineAccelerator>(
				_guid,
				shaderPatches, materialSelectors,
				inputAssembly, topology,
				stateSet);
		}

		if (i != _pipelineAccelerators.end() && i->first == hash) {
			i->second = newAccelerator;		// (we replaced one that expired)
		} else {
			_pipelineAccelerators.insert(i, std::make_pair(hash, newAccelerator));
		}

		RebuildAllPipelinesAlreadyLocked(_guid, *newAccelerator, hash);

		return newAccelerator;
	}

	std::shared_ptr<PipelineAccelerator> PipelineAcceleratorPool::CreatePipelineAccelerator(
		const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
		const ParameterBox& materialSelectors,
		IteratorRange<const MiniInputElementDesc*> inputAssembly,
		Topology topology,
		const RenderCore::Assets::RenderStateSet& stateSet)
	{
		ScopedLock(_constructionLock);

		uint64_t hash = HashCombine(materialSelectors.GetHash(), materialSelectors.GetParameterNamesHash());
		hash = HashInputAssembly(inputAssembly, hash);
		hash = HashCombine((unsigned)topology, hash);
		hash = HashCombine(stateSet.GetHash(), hash);
		if (shaderPatches)
			hash = HashCombine(shaderPatches->GetHash(), hash);

		// If it already exists in the cache, just return it now
		auto i = LowerBound(_pipelineAccelerators, hash);
		if (i != _pipelineAccelerators.end() && i->first == hash) {
			auto l = i->second.lock();
			if (l)
				return l;
		}

		std::shared_ptr<PipelineAccelerator> newAccelerator;
		if (_drawablesPool) {
			newAccelerator = _drawablesPool->MakeProtectedPtr<PipelineAccelerator>(
				_guid,
				shaderPatches, materialSelectors,
				inputAssembly, topology,
				stateSet);
		} else {
			newAccelerator = std::make_shared<PipelineAccelerator>(
				_guid,
				shaderPatches, materialSelectors,
				inputAssembly, topology,
				stateSet);
		}

		if (i != _pipelineAccelerators.end() && i->first == hash) {
			i->second = newAccelerator;		// (we replaced one that expired)
		} else {
			_pipelineAccelerators.insert(i, std::make_pair(hash, newAccelerator));
		}

		RebuildAllPipelinesAlreadyLocked(_guid, *newAccelerator, hash);

		return newAccelerator;
	}

	std::shared_ptr<DescriptorSetAccelerator> PipelineAcceleratorPool::CreateDescriptorSetAccelerator(
		const std::shared_ptr<ResourceConstructionContext>& constructionContext,
		const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
		IteratorRange<Assets::ScaffoldCmdIterator> materialMachine,
		std::shared_ptr<void> memoryHolder,
		const std::shared_ptr<DeformerToDescriptorSetBinding>& deformBinding)
	{
		auto constructionContextGuid = constructionContext ? constructionContext->GetGUID() : 0;
		std::shared_ptr<DescriptorSetAccelerator> result;
		std::promise<std::vector<ActualizedDescriptorSet>> promise;
		{
			ScopedLock(_constructionLock);

			uint64_t hash = HashMaterialMachine(materialMachine);
			if (shaderPatches)
				hash = HashCombine(shaderPatches->GetHash(), hash);
			if (deformBinding)
				hash = HashCombine(deformBinding->GetHash(), hash);

			// If it already exists in the cache, just return it now
			auto cachei = LowerBound(_descriptorSetAccelerators, hash);
			if (cachei != _descriptorSetAccelerators.end() && cachei->first == hash)
				if (auto l = cachei->second.lock()) {
					if (l->_constructionContextGuid != constructionContextGuid)
						Throw(std::runtime_error("Identical DescriptorSetAccelerator initialized for 2 different ConstructionContexts. This is unsafe, because either context could cancel the load"));
					return l;
				}

			if (_drawablesPool) {
				result = _drawablesPool->MakeProtectedPtr<DescriptorSetAccelerator>();
			} else
				result = std::make_shared<DescriptorSetAccelerator>();

			result->_pending = promise.get_future();
			if (cachei != _descriptorSetAccelerators.end() && cachei->first == hash) {
				cachei->second = result;		// (we replaced one that expired)
			} else {
				_descriptorSetAccelerators.insert(cachei, std::make_pair(hash, result));
			}

			result->_constructionContextGuid = constructionContextGuid;
			#if defined(_DEBUG)
				result->_ownerPool = this;
			#endif

			#if !defined(PA_SEPARATE_CONTINUATIONS)
				::Assets::WhenAll(result->_pending).Then(
					[helper=_futuresToCheckHelper, hash](const auto&) {
						ScopedLock(helper->_lock);
						helper->_descSetFuturesToCheck.emplace_back(hash);
					});
			#else
				_continuationExecutor->watch(
					MakeTimedWaitableJustContinuation(
						[helper=_futuresToCheckHelper, hash](auto&&) {
							ScopedLock(helper->_lock);
							helper->_descSetFuturesToCheck.emplace_back(hash);
						},
						result->_pending));
			#endif
		}

		// We don't need to have "_constructionLock" after we've added the Marker to _descriptorSetAccelerators, so let's do the
		// rest outside of the lock

		bool generateBindingInfo = !!(_flags & PipelineAcceleratorPoolFlags::RecordDescriptorSetBindingInfo);
		if (shaderPatches) {
			auto patchCollectionFuture = _layoutPatcher->GetPatchCollectionFuture(*shaderPatches);

			// Most of the time, it will be ready immediately, and we can avoid some of the overhead of the
			// future continuation functions
			std::shared_ptr<CompiledShaderPatchCollection> patchCollection; ::Assets::DependencyValidation patchCollectionDepVal; ::Assets::Blob patchCollectionLog;
			if (patchCollectionFuture->CheckStatusBkgrnd(patchCollection, patchCollectionDepVal, patchCollectionLog) == ::Assets::AssetState::Ready) {
				ConstructDescriptorSetHelper helper{_device, _samplerPool.get(), PipelineType::Graphics, generateBindingInfo};
				helper.Construct(
					constructionContext.get(),
					patchCollection->GetInterface().GetMaterialDescriptorSet(),
					materialMachine, deformBinding.get());
				helper.CompleteToPromise(std::move(promise));
			} else {
				std::weak_ptr<IDevice> weakDevice = _device;
				::Assets::WhenAll(patchCollectionFuture).ThenConstructToPromise(
					std::move(promise),
					[materialMachine, memoryHolder, weakDevice, generateBindingInfo, samplerPool=std::weak_ptr<SamplerPool>(_samplerPool), deformBinding, result, constructionContext](
						std::promise<std::vector<ActualizedDescriptorSet>>&& promise,
						std::shared_ptr<CompiledShaderPatchCollection> patchCollection) {

						auto d = weakDevice.lock();
						if (!d)
							Throw(std::runtime_error("Device has been destroyed"));
						
						ConstructDescriptorSetHelper helper{d, samplerPool.lock().get(), PipelineType::Graphics, generateBindingInfo};
						helper.Construct(
							constructionContext.get(),
							patchCollection->GetInterface().GetMaterialDescriptorSet(),
							materialMachine, deformBinding.get());
						helper.CompleteToPromise(std::move(promise));
					});
			}
		} else {
			ConstructDescriptorSetHelper helper{_device, _samplerPool.get(), PipelineType::Graphics, generateBindingInfo};
			helper.Construct(
				constructionContext.get(),
				_layoutPatcher->GetBaseMaterialDescriptorSetLayout(),
				materialMachine, deformBinding.get());
			helper.CompleteToPromise(std::move(promise));
		}

		return result;
	}

	auto PipelineAcceleratorPool::CreateSequencerConfig(
		const std::string& name,
		std::shared_ptr<ITechniqueDelegate> delegate,
		const ParameterBox& sequencerSelectors,
		const FrameBufferDesc& fbDesc,
		unsigned subpassIndex) -> std::shared_ptr<SequencerConfig>
	{
		ScopedLock(_constructionLock);

		std::vector<NewlyQueued> newlyQueued;

		uint64_t hash = 0;
		auto cfg = MakeSequencerConfig(hash, delegate, sequencerSelectors, fbDesc, subpassIndex);

		// Look for an existing configuration with the same settings
		//	-- todo, not checking the delegate here!
		for (auto i=_sequencerConfigById.begin(); i!=_sequencerConfigById.end(); ++i) {
			if (i->first == hash) {
				auto cfgId = SequencerConfigId(i - _sequencerConfigById.begin()) | (SequencerConfigId(_guid) << 32ull);
				
				auto result = i->second.lock();

				// The configuration may have expired. In this case, we should just create it again, and reset
				// our pointer. Note that we only even hold a weak pointer, so if the caller doesn't hold
				// onto the result, it's just going to expire once more
				if (!result) {
					result = std::make_shared<SequencerConfig>(std::move(cfg));
					result->_pipelineLayout._pending = _layoutPatcher->GetPatchedPipelineLayout(result->_delegate->GetPipelineLayout())->ShareFuture();
					result->_cfgId = cfgId;
					result->_name = name;
					#if defined(_DEBUG)
						result->_ownerPool = this;
					#endif
					i->second = result;

					// If a pipeline accelerator was added while this sequencer config was expired, the pipeline
					// accelerator would not have been configured. We have to check for this case and construct
					// as necessary -- 
					for (auto& accelerator:_pipelineAccelerators) {
						auto a = accelerator.second.lock();
						if (a && !a->HasCurrentOrFuturePipeline(*result)) {
							auto future = a->BeginPrepareForSequencerStateAlreadyLocked(result, _globalSelectors, _pipelineCollection, *_layoutPatcher);
							newlyQueued.emplace_back(std::move(future), accelerator.first, cfgId);
						}
					}
				} else {
					if (!name.empty() && !XlFindString(result->_name, name))
						result->_name += "|" + name;		// we're repurposing the same cfg for something else
				}

				SetupNewlyQueuedAlreadyLocked(newlyQueued);
				return result;
			}
		}

		auto cfgId = SequencerConfigId(_sequencerConfigById.size()) | (SequencerConfigId(_guid) << 32ull);
		auto result = std::make_shared<SequencerConfig>(std::move(cfg));
		result->_pipelineLayout._pending = _layoutPatcher->GetPatchedPipelineLayout(result->_delegate->GetPipelineLayout())->ShareFuture();
		result->_cfgId = cfgId;
		result->_name = name;
		#if defined(_DEBUG)
			result->_ownerPool = this;
		#endif

		_sequencerConfigById.emplace_back(std::make_pair(hash, result));		// (note; only holding onto a weak pointer here)

		// trigger creation of pipeline states for all accelerators
		for (auto& accelerator:_pipelineAccelerators) {
			auto a = accelerator.second.lock();
			if (a) {
				if (a->_completedPipelines.size() < _sequencerConfigById.size())
					a->_completedPipelines.resize(_sequencerConfigById.size());
				auto future = a->BeginPrepareForSequencerStateAlreadyLocked(result, _globalSelectors, _pipelineCollection, *_layoutPatcher);
				newlyQueued.emplace_back(std::move(future), accelerator.first, cfgId);
			}
		}

		SetupNewlyQueuedAlreadyLocked(newlyQueued);
		return result;
	}

	void PipelineAcceleratorPool::RebuildAllPipelinesAlreadyLocked(unsigned poolGuid, PipelineAccelerator& pipeline, uint64_t acceleratorHash)
	{
		std::vector<NewlyQueued> newlyQueued;

		if (pipeline._completedPipelines.size() < _sequencerConfigById.size())
			pipeline._completedPipelines.resize(_sequencerConfigById.size());

		for (unsigned c=0; c<_sequencerConfigById.size(); ++c) {
			auto cfgId = SequencerConfigId(c) | (SequencerConfigId(poolGuid) << 32ull);
			auto l = _sequencerConfigById[c].second.lock();
			if (l) {
				auto future = pipeline.BeginPrepareForSequencerStateAlreadyLocked(l, _globalSelectors, _pipelineCollection, *_layoutPatcher);
				newlyQueued.emplace_back(std::move(future), acceleratorHash, cfgId);
			}
		}

		SetupNewlyQueuedAlreadyLocked(newlyQueued);
	}

	void PipelineAcceleratorPool::RebuildAllPipelinesAlreadyLocked(unsigned poolGuid)
	{
		for (auto& accelerator:_pipelineAccelerators) {
			auto a = accelerator.second.lock();
			if (a)
				RebuildAllPipelinesAlreadyLocked(poolGuid, *a, accelerator.first);
		}
	}

	VisibilityMarkerId PipelineAcceleratorPool::VisibilityBarrier(VisibilityMarkerId expectedVisibility)
	{
		// We're locking 2 lock here, so we have to be a little careful of deadlocks here
		// _constructionLock will be locked for short durations of arbitrary threads -- including the main thread and threadpool threads
		//   This lock isn't exposed to the user, and fully controlled by this system
		// _pipelineUsageLock is locked at least once every frame, and will typically only be used by a smaller number of threads. However,
		//   since the client can control this lock with LockForReading() and UnlockForReading(), there are a wider range of different things
		//   that can happen while a thread holds this lock
		// 
		// Let's lock _pipelineUsageLock first and then _constructionLock. This means:
		// * If a thread has the _constructionLock, it should never attempt to lock _pipelineUsageLock, or wait on a thread that has _pipelineUsageLock
		// * On the flip side, if you have _pipelineUsageLock, you can, technically, wait on _constructionLock -- even if this isn't really advisable
		// This way around should be more easily controllable for us
		ScopedLock(_pipelineUsageLock);		// (exclusive lock here)
		ScopedLock(_constructionLock);

		// We can make the barrier optional by setting "expectedVisibility" to something other than the default (which is ~0u)
		// This can allow us to early out if we find that the required visibility is already achieved
		auto current = _futuresToCheckHelper->_lastPublishedVisibilityMarker.load();
		if (current >= expectedVisibility)
			return current;

		auto newVisibilityMarker = ++_futuresToCheckHelper->_lastPublishedVisibilityMarker;
		std::vector<NewlyQueued> newlyQueued;

		// Look through every pipeline registered in this pool, and 
		// trigger a rebuild of any that appear to be out of date.
		// This allows us to support hotreloading when files change, etc
		_lastFrameSequencerConfigExpired.resize(_sequencerConfigById.size(), true);
		VLA(unsigned, invalidSequencerIndices, _sequencerConfigById.size());
		unsigned invalidSequencerCount = 0;
		VLA(unsigned, newlyExpiredSequencerIndices, _sequencerConfigById.size());
		unsigned newlyExpiredSequencerCount = 0;

		for (unsigned c=0; c<_sequencerConfigById.size(); ++c) {
			auto cfg = _sequencerConfigById[c].second.lock();
			if (!cfg) {
				if (!_lastFrameSequencerConfigExpired[c]) {
					newlyExpiredSequencerIndices[newlyExpiredSequencerCount++] = c;
					_lastFrameSequencerConfigExpired[c] = true;
				}
				continue;
			}

			_lastFrameSequencerConfigExpired[c] = false;
			if (cfg->_pipelineLayout._pending.valid()) {
				auto state = cfg->_pipelineLayout._pending.wait_for(std::chrono::milliseconds(0));
				if (state != std::future_status::timeout) {
					TRY
					{
						auto pipelineLayout = cfg->_pipelineLayout._pending.get();
						cfg->_pipelineLayout._depVal = pipelineLayout->GetDependencyValidation();
						cfg->_pipelineLayout._pipelineLayout = std::move(pipelineLayout);
						cfg->_pipelineLayout._visibilityMarker = newVisibilityMarker;
						cfg->_pipelineLayout._pending = {};
					} CATCH(const ::Assets::Exceptions::ConstructionError& e) {
						// we've gone invalid
						cfg->_pipelineLayout._depVal = e.GetDependencyValidation();
						cfg->_pipelineLayout._pipelineLayout = {};
						cfg->_pipelineLayout._visibilityMarker = ~VisibilityMarkerId(0);
						cfg->_pipelineLayout._pending = {};
					} CATCH(const std::exception& e) {
						// we've gone invalid (no dep val)
						cfg->_pipelineLayout._depVal = {};
						cfg->_pipelineLayout._pipelineLayout = {};
						cfg->_pipelineLayout._visibilityMarker = ~VisibilityMarkerId(0);
						cfg->_pipelineLayout._pending = {};
					} CATCH_END
				}
			} else if (cfg->_pipelineLayout._depVal.GetValidationIndex() != 0) {
				assert(c == unsigned(cfg->_cfgId));
				// rebuild pipeline layout asset
				cfg->_pipelineLayout._pipelineLayout = nullptr;
				cfg->_pipelineLayout._depVal = {};
				cfg->_pipelineLayout._visibilityMarker = ~VisibilityMarkerId(0);
				cfg->_pipelineLayout._pending = _layoutPatcher->GetPatchedPipelineLayout(cfg->_delegate->GetPipelineLayout())->ShareFuture();
				invalidSequencerIndices[invalidSequencerCount++] = c;
			}
		}

		// Requeue pipelines for invalidated sequencers
		if (invalidSequencerCount) {
			std::vector<std::shared_ptr<SequencerConfig>> lockedSequencers;
			lockedSequencers.resize(invalidSequencerCount);
			for (unsigned c=0; c<invalidSequencerCount; ++c)
				lockedSequencers[c] = _sequencerConfigById[invalidSequencerIndices[c]].second.lock();

			for (auto& accelerator:_pipelineAccelerators) {
				auto a = accelerator.second.lock();
				if (a) {
					for (unsigned c=0; c<invalidSequencerCount; ++c) {
						// It's out of date -- let's rebuild and reassign it
						if (!lockedSequencers[c]) continue;
						auto future = a->BeginPrepareForSequencerStateAlreadyLocked(lockedSequencers[c], _globalSelectors, _pipelineCollection, *_layoutPatcher);
						newlyQueued.emplace_back(std::move(future), accelerator.first, lockedSequencers[c]->_cfgId);
					}
				}
			}
		} else {
			// pipeline invalidation behaviour & releasing unneeded pipelines
			// check only 1 sequencer cfg per frame, and just a few pipeline accelerators, but rotate through
			// also don't do it if we've also invalidated a full sequencer cfg this frame
			const bool checkForInvalidatedAccelerators = true;
			if (checkForInvalidatedAccelerators && !_sequencerConfigById.empty()) {
				std::shared_ptr<SequencerConfig> seqCfg;
				for (unsigned c=0; c<_sequencerConfigById.size(); ++c) {
					auto idx = (c+_lastSequencerCfgHotReloadCheck)%_sequencerConfigById.size();
					seqCfg = _sequencerConfigById[idx].second.lock();
					if (seqCfg) {
						_lastSequencerCfgHotReloadCheck = idx;
						break;
					}
				}

				if (seqCfg) {
					unsigned acceleratorsToCheckCountDown = 32;	// only check a few accelerators; doesn't matter if it takes a few frames to get through all of them
					for (; _lastPipelineAcceleratorHotReloadCheck<_pipelineAccelerators.size(); ++_lastPipelineAcceleratorHotReloadCheck) {
						if (!acceleratorsToCheckCountDown--) break;
						auto a = _pipelineAccelerators[_lastPipelineAcceleratorHotReloadCheck].second.lock();
						if (!a) continue;
						if (a->_completedPipelines[_lastSequencerCfgHotReloadCheck].GetDependencyValidation().GetValidationIndex() != 0) {
							// Don't attempt hotreload if we already have a pending pipeline here (if the hotreload is already out of date it will still get rebuilt after finishing)
							auto existing = std::find_if(a->_pendingPipelines.begin(), a->_pendingPipelines.end(), [c=_lastSequencerCfgHotReloadCheck](const auto& p) { return p.first == c; });
							if (existing != a->_pendingPipelines.end()) continue;
							auto future = a->BeginPrepareForSequencerStateAlreadyLocked(seqCfg, _globalSelectors, _pipelineCollection, *_layoutPatcher);
							newlyQueued.emplace_back(std::move(future), _pipelineAccelerators[_lastPipelineAcceleratorHotReloadCheck].first, seqCfg->_cfgId);
						}
					}
					if (_lastPipelineAcceleratorHotReloadCheck >= _pipelineAccelerators.size()) {
						_lastPipelineAcceleratorHotReloadCheck = 0;
						++_lastSequencerCfgHotReloadCheck;
					}
				}

				// We can't automatically reload descriptor sets quite so easily -- because we don't hold onto
				// the construction information. In theory we could do that; but it would mean duplicating a
				// fair bit of data...
			}
		}

		// Release pipeline accelerators for any sequencers that have just been dropped
		if (newlyExpiredSequencerCount) {
			for (auto& accelerator:_pipelineAccelerators) {
				auto a = accelerator.second.lock();
				if (a) {
					for (auto newlyExpired:MakeIteratorRange(newlyExpiredSequencerIndices, &newlyExpiredSequencerIndices[newlyExpiredSequencerCount]))
						a->_completedPipelines[newlyExpired] = {};
				}
			}
		}

		// check for completed futures
		std::vector<std::pair<uint64_t, SequencerConfigId>> thisTimePipelinesToCheck;
		std::vector<uint64_t> thisTimeDescSetsToCheck;
		{
			ScopedLock(_futuresToCheckHelper->_lock);
			std::swap(_futuresToCheckHelper->_pipelineFuturesToCheck, thisTimePipelinesToCheck);
			std::swap(_futuresToCheckHelper->_descSetFuturesToCheck, thisTimeDescSetsToCheck);
		}

		for (auto& futureToCheck:thisTimePipelinesToCheck) {
			auto a = LowerBound(_pipelineAccelerators, futureToCheck.first);
			if (a == _pipelineAccelerators.end() || a->first != futureToCheck.first) continue;	// pipeline accelerator removed?

			auto accelerator = a->second.lock();
			if (!accelerator) continue;

			assert(accelerator->_completedPipelines.size() >= _sequencerConfigById.size());
			auto seqIdx = uint32_t(futureToCheck.second);
			assert(seqIdx < _sequencerConfigById.size());

			auto p = std::find_if(
				accelerator->_pendingPipelines.begin(), accelerator->_pendingPipelines.end(),
				[w=seqIdx](const auto& q) { return q.first == w; });
			if (p == accelerator->_pendingPipelines.end()) continue;

			auto state = p->second.wait_for(std::chrono::milliseconds(0));
			if (state == std::future_status::timeout) continue;
			assert(state == std::future_status::ready);

			auto pendingPipeline = std::move(*p);
			accelerator->_pendingPipelines.erase(p);

			if (_sequencerConfigById[seqIdx].second.expired()) continue;		// don't keep a pipeline for an expired sequencer config

			TRY
			{
				auto pipeline = pendingPipeline.second.get();
				accelerator->_completedPipelines[seqIdx]._depVal = pipeline.GetDependencyValidation();
				accelerator->_completedPipelines[seqIdx]._pipeline = std::move(pipeline);
				accelerator->_completedPipelines[seqIdx]._visibilityMarker = newVisibilityMarker;
			} CATCH(const ::Assets::Exceptions::ConstructionError& e) {
				// we've gone invalid
				accelerator->_completedPipelines[seqIdx]._depVal = e.GetDependencyValidation();
				accelerator->_completedPipelines[seqIdx]._pipeline = {};
				accelerator->_completedPipelines[seqIdx]._visibilityMarker = ~VisibilityMarkerId(0);
			} CATCH(const ::Assets::Exceptions::InvalidAsset& e) {
				// we've gone invalid
				accelerator->_completedPipelines[seqIdx]._depVal = e.GetDependencyValidation();
				accelerator->_completedPipelines[seqIdx]._pipeline = {};
				accelerator->_completedPipelines[seqIdx]._visibilityMarker = ~VisibilityMarkerId(0);
			} CATCH(const std::exception& e) {
				// we've gone invalid (no dep val)
				accelerator->_completedPipelines[seqIdx]._depVal = {};
				accelerator->_completedPipelines[seqIdx]._pipeline = {};
				accelerator->_completedPipelines[seqIdx]._visibilityMarker = ~VisibilityMarkerId(0);
			} CATCH_END
		}

		for (auto futureToCheck:thisTimeDescSetsToCheck) {
			auto a = LowerBound(_descriptorSetAccelerators, futureToCheck);
			if (a == _descriptorSetAccelerators.end() || a->first != futureToCheck) continue;

			auto descSet = a->second.lock();
			if (!descSet) continue;

			if (!descSet->_pending.valid()) continue;

			auto state = descSet->_pending.wait_for(std::chrono::milliseconds(0));
			if (state == std::future_status::timeout) continue;
			assert(state == std::future_status::ready);

			TRY
			{
				auto completed = descSet->_pending.get();
				descSet->_depVal = completed.begin()->GetDependencyValidation();
				descSet->_completed = std::move(*completed.begin());
				descSet->_visibilityMarker = newVisibilityMarker;
				descSet->_pending = {};
			} CATCH(const ::Assets::Exceptions::ConstructionError& e) {
				// we've gone invalid
				descSet->_depVal = e.GetDependencyValidation();
				descSet->_completed = {};
				descSet->_visibilityMarker = ~VisibilityMarkerId(0);
				descSet->_pending = {};
			} CATCH(const ::Assets::Exceptions::InvalidAsset& e) {
				// we've gone invalid
				descSet->_depVal = e.GetDependencyValidation();
				descSet->_completed = {};
				descSet->_visibilityMarker = ~VisibilityMarkerId(0);
				descSet->_pending = {};
			} CATCH(const std::exception& e) {
				// we've gone invalid (no dep val)
				descSet->_depVal = {};
				descSet->_completed = {};
				descSet->_visibilityMarker = ~VisibilityMarkerId(0);
				descSet->_pending = {};
			} CATCH_END
		}

		SetupNewlyQueuedAlreadyLocked(newlyQueued);

		return newVisibilityMarker;
	}

	void PipelineAcceleratorPool::SetupNewlyQueuedAlreadyLocked(IteratorRange<const NewlyQueued*> newlyQueued)
	{
		// after the newly queued futures are completed, make a record so we know to come and check them
		for (const auto& q:newlyQueued) {
			#if !defined(PA_SEPARATE_CONTINUATIONS)
				::Assets::WhenAll(std::move(q._future)).Then(
					[helper=_futuresToCheckHelper, acceleratorHash=q._acceleratorHash, cfgId=q._cfgId](const auto&) {
						ScopedLock(helper->_lock);
						helper->_pipelineFuturesToCheck.emplace_back(acceleratorHash, cfgId);
					});
			#else
				_continuationExecutor->watch(
					MakeTimedWaitableJustContinuation(
						[helper=_futuresToCheckHelper, acceleratorHash=q._acceleratorHash, cfgId=q._cfgId](auto&&) {
							ScopedLock(helper->_lock);
							helper->_pipelineFuturesToCheck.emplace_back(acceleratorHash, cfgId);
						},
						std::move(q._future)));
			#endif
		}
	}

	void PipelineAcceleratorPool::SetGlobalSelector(StringSection<> name, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
	{
		ScopedLock(_constructionLock);
		_globalSelectors.SetParameter(name, data, type);
		RebuildAllPipelinesAlreadyLocked(_guid);
	}

	void PipelineAcceleratorPool::RemoveGlobalSelector(StringSection<> name)
	{
		ScopedLock(_constructionLock);
		_globalSelectors.RemoveParameter(name);
		RebuildAllPipelinesAlreadyLocked(_guid);
	}

	static std::string AsString(const ParameterBox& selectors, unsigned countPerLine)
	{
		std::stringstream str;
		str.fill('0');
		unsigned counter = 0;
		for (auto e:selectors) {
			if ((counter%countPerLine) == (countPerLine-1)) str << "\n";
			else if (counter != 0) str << ", ";
			++counter;
			str << "{color:" << std::hex << std::setw(6) << (e.HashName() & 0xffffff) << std::setw(0) << "}";
			str << e.Name() << ":" << e.ValueAsString();
		};
		return str.str();
	}

	auto PipelineAcceleratorPool::LogRecords() const -> Records
	{
		ScopedLock(_constructionLock);
		Records result;
		result._pipelineAccelerators.reserve(_pipelineAccelerators.size());
		for (const auto&pa:_pipelineAccelerators) {
			auto l = pa.second.lock();
			if (!l) continue;

			PipelineAcceleratorRecord record;
			record._shaderPatchesHash = l->_shaderPatches ? l->_shaderPatches->GetHash() : 0ull;
			record._materialSelectors = AsString(l->_materialSelectors, 4);
			record._geoSelectors = AsString(l->_geoSelectors, 2);
			record._stateSetHash = l->_stateSet.GetHash();
			if (!l->_miniInputAssembly.empty())
				record._inputAssemblyHash = HashInputAssembly(l->_miniInputAssembly, DefaultSeed64);
			else
				record._inputAssemblyHash = HashInputAssembly(l->_inputAssembly, DefaultSeed64);
			result._pipelineAccelerators.push_back(std::move(record));
		}

		result._sequencerConfigs.reserve(_sequencerConfigById.size());
		for (const auto&cfg:_sequencerConfigById) {
			auto l = cfg.second.lock();
			if (!l) continue;

			SequencerConfigRecord record;
			record._name = l->_name;
			record._sequencerSelectors = AsString(l->_sequencerSelectors, 2);
			record._fbRelevanceValue = l->_fbRelevanceValue;
			result._sequencerConfigs.push_back(record);
		}
		result._descriptorSetAcceleratorCount = _descriptorSetAccelerators.size();

		auto collectionMetrics = _pipelineCollection->GetMetrics();
		result._metalPipelineCount = collectionMetrics._graphicsPipelineCount;

		return result;
	}

	const std::shared_ptr<IDevice>& PipelineAcceleratorPool::GetDevice() const { return _device; }
	const std::shared_ptr<ICompiledLayoutPool>& PipelineAcceleratorPool::GetCompiledLayoutPool() const { return _layoutPatcher; }

	static unsigned s_nextPipelineAcceleratorPoolGUID = 1;

	static void CheckDescSetLayout(
		const DescriptorSetLayoutAndBinding& matDescSetLayout,
		const PipelineLayoutInitializer& pipelineLayoutDesc,
		const char descSetName[])
	{
		if (matDescSetLayout.GetSlotIndex() >= pipelineLayoutDesc.GetDescriptorSets().size())
			Throw(std::runtime_error("Invalid slot index (" + std::to_string(matDescSetLayout.GetSlotIndex()) + " for " + descSetName + " during pipeline accelerator pool construction"));

		const auto& matchingDesc = pipelineLayoutDesc.GetDescriptorSets()[matDescSetLayout.GetSlotIndex()]._signature;
		const auto& layout = *matDescSetLayout.GetLayout();
		for (unsigned s=0; s<layout._slots.size(); ++s) {
			auto expectedCount = layout._slots[s]._arrayElementCount ?: 1;
			auto idx = layout._slots[s]._slotIdx;

			// It's ok if the pipeline layout has more slots than the _matDescSetLayout version; just not the other way around
			// we just have the verify that the types match up for the slots that are there
			if (idx >= matchingDesc._slots.size())
				Throw(std::runtime_error(std::string{"Pipeline layout does not match the provided "} + descSetName + " layout. There are too few slots in the pipeline layout"));

			if (matchingDesc._slots[idx]._type != layout._slots[s]._type || matchingDesc._slots[idx]._count != expectedCount)
				Throw(std::runtime_error(std::string{"Pipeline layout does not match the provided "} + descSetName + " layout. Slot type does not match for slot (" + std::to_string(s) + ")"));
		}
	}

	namespace Internal
	{
		struct InvokerImmediate
		{
			template<typename Fn>
				void operator()(Fn&& f) { f(); }
		};
	}
	
	PipelineAcceleratorPool::PipelineAcceleratorPool(
		std::shared_ptr<IDevice> device,
		std::shared_ptr<IDrawablesPool> drawablesPool,
		std::shared_ptr<ICompiledLayoutPool> patchCollectionPool,
		PipelineAcceleratorPoolFlags::BitField flags)
	: _samplerPool(std::make_shared<SamplerPool>(*device))
	, _layoutPatcher(std::move(patchCollectionPool))
	, _drawablesPool(std::move(drawablesPool))
	{
		_guid = s_nextPipelineAcceleratorPoolGUID++;
		_device = std::move(device);
		_flags = flags;
		_pipelineCollection = std::make_shared<PipelineCollection>(_device);
		_futuresToCheckHelper = std::make_shared<FuturesToCheckHelper>();
		_futuresToCheckHelper->_lastPublishedVisibilityMarker.store(0);

		#if defined(PA_SEPARATE_CONTINUATIONS)
			using SimpleExecutor = thousandeyes::futures::PollingExecutor<
				thousandeyes::futures::detail::InvokerWithNewThread,
				Internal::InvokerImmediate>;
			_continuationExecutor = std::make_shared<SimpleExecutor>(
				std::chrono::microseconds(2000),
				thousandeyes::futures::detail::InvokerWithNewThread{},
				Internal::InvokerImmediate{});
		#endif
	}

	PipelineAcceleratorPool::~PipelineAcceleratorPool() {}
	IPipelineAcceleratorPool::~IPipelineAcceleratorPool() {}

	std::shared_ptr<IPipelineAcceleratorPool> CreatePipelineAcceleratorPool(
		const std::shared_ptr<IDevice>& device, 
		const std::shared_ptr<IDrawablesPool>& drawablesPool,
		const std::shared_ptr<ICompiledLayoutPool>& patchCollectionPool,
		PipelineAcceleratorPoolFlags::BitField flags)
	{
		return std::make_shared<PipelineAcceleratorPool>(device, drawablesPool, patchCollectionPool, flags);
	}

	namespace Internal
	{
		const DescriptorSetLayoutAndBinding& GetDefaultDescriptorSetLayoutAndBinding()
		{
			static DescriptorSetLayoutAndBinding s_result;
			return s_result;
		}
	}

}}
