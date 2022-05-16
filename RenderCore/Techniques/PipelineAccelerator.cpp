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
#include "../FrameBufferDesc.h"
#include "../Format.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/ObjectFactory.h"
#include "../Assets/RawMaterial.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Assets/ScaffoldCmdStream.h"
#include "../../Assets/Marker.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetHeapLRU.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Streams/PathUtils.h"
#include <cctype>
#include <sstream>
#include <iomanip>

namespace RenderCore { namespace Techniques
{
	using SequencerConfigId = uint64_t;

	class SequencerConfig
	{
	public:
		SequencerConfigId _cfgId = ~0ull;

		std::shared_ptr<ITechniqueDelegate> _delegate;
		::Assets::PtrToMarkerPtr<CompiledPipelineLayoutAsset> _pipelineLayout;
		ParameterBox _sequencerSelectors;

		FrameBufferDesc _fbDesc;
		unsigned _subpassIdx = 0;
		uint64_t _fbRelevanceValue = 0;
		std::string _name;
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
	
		// "_completedGraphicsPipelines" is protected by the _pipelineUsageLock lock in the pipeline accelerator pool
		std::vector<IPipelineAcceleratorPool::Pipeline> _completedGraphicsPipelines;

		// "_pendingGraphicsPipelines" is protected by the _constructionLock in the pipeline accelerator pool
		using Pipeline = IPipelineAcceleratorPool::Pipeline;
		using PtrToPipelineFuture = std::shared_ptr<::Assets::Marker<Pipeline>>;
		std::vector<std::pair<SequencerConfigId, PtrToPipelineFuture>> _pendingGraphicsPipelines;

		void BeginPrepareForSequencerStateAlreadyLocked(
			std::shared_ptr<SequencerConfig> cfg,
			const ParameterBox& globalSelectors,
			const std::shared_ptr<PipelineCollection>& pipelineCollection,
			ICompiledLayoutPool& layoutPatcher);
		bool PipelineValidPipelineOrFuture(const SequencerConfig& cfg) const;

		Pipeline* TryGetPipeline(const SequencerConfig& cfg);
		PtrToPipelineFuture FindPipelineFutureAlreadyLocked(const SequencerConfig& cfg);
	
		std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> _shaderPatches;
		ParameterBox _materialSelectors;
		ParameterBox _geoSelectors;
		std::vector<InputElementDesc> _inputAssembly;
		std::vector<MiniInputElementDesc> _miniInputAssembly;
		Topology _topology;
		RenderCore::Assets::RenderStateSet _stateSet;

		unsigned _ownerPoolId;
	};

	void PipelineAccelerator::BeginPrepareForSequencerStateAlreadyLocked(
		std::shared_ptr<SequencerConfig> cfg,
		const ParameterBox& globalSelectors,
		const std::shared_ptr<PipelineCollection>& pipelineCollection,
		ICompiledLayoutPool& layoutPatcher)
	{
		PtrToPipelineFuture pipelineFuture = std::make_shared<::Assets::Marker<IPipelineAcceleratorPool::Pipeline>>("PipelineAccelerator Pipeline");
		ParameterBox copyGlobalSelectors = globalSelectors;
		std::weak_ptr<PipelineAccelerator> weakThis = shared_from_this();
		auto patchCollectionFuture = _shaderPatches ? layoutPatcher.GetPatchCollectionFuture(*_shaderPatches) : layoutPatcher.GetDefaultPatchCollectionFuture();

		// Queue massive chain of future continuation functions (it's not as scary as it looks)
		//
		//    CompiledShaderPatchCollection -> GraphicsPipelineDesc -> Metal::GraphicsPipeline
		//
		// Note there may be an issue here in that if the shader compile fails, the dep val for the 
		// final pipeline will only contain the dependencies for the shader. So if the root problem
		// is actually something about the configuration, we won't get the proper recompile functionality 
		::Assets::WhenAll(patchCollectionFuture, cfg->_pipelineLayout).ThenConstructToPromise(
			pipelineFuture->AdoptPromise(),
			[pipelineCollection, copyGlobalSelectors, cfg, weakThis](
				std::promise<IPipelineAcceleratorPool::Pipeline>&& resultPromise,
				std::shared_ptr<CompiledShaderPatchCollection> compiledPatchCollection,
				std::shared_ptr<CompiledPipelineLayoutAsset> pipelineLayoutAsset) {

				auto containingPipelineAccelerator = weakThis.lock();
				if (!containingPipelineAccelerator)
					Throw(std::runtime_error("Containing GraphicsPipeline builder has been destroyed"));

				const ParameterBox* paramBoxes[] = {
					&cfg->_sequencerSelectors,
					&containingPipelineAccelerator->_geoSelectors,
					&containingPipelineAccelerator->_materialSelectors,
					&copyGlobalSelectors
				};

				auto pipelineDescFuture = cfg->_delegate->GetPipelineDesc(compiledPatchCollection->GetInterface(), containingPipelineAccelerator->_stateSet);
				VertexInputStates vis { containingPipelineAccelerator->_inputAssembly, containingPipelineAccelerator->_miniInputAssembly, containingPipelineAccelerator->_topology };
				auto metalPipelineFuture = std::make_shared<::Assets::Marker<Techniques::GraphicsPipelineAndLayout>>();
				pipelineCollection->CreateGraphicsPipeline(
					metalPipelineFuture->AdoptPromise(),
					pipelineLayoutAsset->GetPipelineLayout(), pipelineDescFuture,
					MakeIteratorRange(paramBoxes), 
					vis, FrameBufferTarget{&cfg->_fbDesc, cfg->_subpassIdx}, compiledPatchCollection);

				::Assets::WhenAll(metalPipelineFuture, pipelineDescFuture).ThenConstructToPromise(
					std::move(resultPromise),
					[cfg, weakThis](
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
			});

		unsigned sequencerIdx = unsigned(cfg->_cfgId);
		auto i = std::find_if(_pendingGraphicsPipelines.begin(), _pendingGraphicsPipelines.end(), [sequencerIdx](const auto& p) { return p.first == sequencerIdx; });
		if (i != _pendingGraphicsPipelines.end()) {
			i->second = pipelineFuture;
		} else 
			_pendingGraphicsPipelines.emplace_back(sequencerIdx, std::move(pipelineFuture));
	}

	bool PipelineAccelerator::PipelineValidPipelineOrFuture(const SequencerConfig& cfg) const
	{
		#if defined(_DEBUG)
			unsigned poolId = unsigned(cfg._cfgId >> 32ull);
			if (poolId != _ownerPoolId)
				Throw(std::runtime_error("Mixing a pipeline accelerator from an incorrect pool"));
		#endif

		// If we have something in _completedGraphicsPipelines with a current validation index, return true
		unsigned sequencerIdx = unsigned(cfg._cfgId);
		if (sequencerIdx < _completedGraphicsPipelines.size())
			if (_completedGraphicsPipelines[sequencerIdx]._metalPipeline && _completedGraphicsPipelines[sequencerIdx].GetDependencyValidation().GetValidationIndex() == 0)
				return true;

		// If we have a pipeline currently in pending state, or ready/invalid with a current validation index, then return true
		auto i = std::find_if(_pendingGraphicsPipelines.begin(), _pendingGraphicsPipelines.end(), [sequencerIdx](const auto& p) { return p.first == sequencerIdx; });
		if (i != _pendingGraphicsPipelines.end()) {
			::Assets::DependencyValidation depVal;
			::Assets::Blob b;
			auto state = i->second->CheckStatusBkgrnd(depVal, b);
			if (state == ::Assets::AssetState::Pending)
				return true;
			return depVal.GetValidationIndex() == 0;
		}
		return false;
	}

	auto PipelineAccelerator::TryGetPipeline(const SequencerConfig& cfg) -> Pipeline*
	{
		#if defined(_DEBUG)
			unsigned poolId = unsigned(cfg._cfgId >> 32ull);
			if (poolId != _ownerPoolId)
				Throw(std::runtime_error("Mixing a pipeline accelerator from an incorrect pool"));
		#endif

		unsigned sequencerIdx = unsigned(cfg._cfgId);
		if (sequencerIdx >= _completedGraphicsPipelines.size() || !_completedGraphicsPipelines[sequencerIdx]._metalPipeline)
			return nullptr;
		return &_completedGraphicsPipelines[sequencerIdx];
	}

	auto PipelineAccelerator::FindPipelineFutureAlreadyLocked(const SequencerConfig& cfg) -> PtrToPipelineFuture
	{
		#if defined(_DEBUG)
			unsigned poolId = unsigned(cfg._cfgId >> 32ull);
			if (poolId != _ownerPoolId)
				Throw(std::runtime_error("Mixing a pipeline accelerator from an incorrect pool"));
		#endif

		// we should be in the pool's _constructionLock for this
		unsigned sequencerIdx = unsigned(cfg._cfgId);
		auto i = std::find_if(_pendingGraphicsPipelines.begin(), _pendingGraphicsPipelines.end(), [sequencerIdx](const auto& p) { return p.first == sequencerIdx; });
		if (i != _pendingGraphicsPipelines.end())
			return i->second;
		return nullptr;
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
		std::shared_ptr<::Assets::Marker<ActualizedDescriptorSet>> _descriptorSet;
		DescriptorSetBindingInfo _bindingInfo;
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

		std::shared_ptr<::Assets::Marker<Pipeline>> GetPipelineMarker(PipelineAccelerator& pipelineAccelerator, const SequencerConfig& sequencerConfig) const override;
		const Pipeline* TryGetPipeline(PipelineAccelerator& pipelineAccelerator, const SequencerConfig& sequencerConfig) const override;

		std::shared_ptr<::Assets::Marker<ActualizedDescriptorSet>> GetDescriptorSetMarker(DescriptorSetAccelerator& accelerator) const override;
		const ActualizedDescriptorSet* TryGetDescriptorSet(DescriptorSetAccelerator& accelerator) const override;

		::Assets::PtrToMarkerPtr<CompiledPipelineLayoutAsset> GetCompiledPipelineLayoutMarker(const SequencerConfig& sequencerConfig) const override;
		std::shared_ptr<ICompiledPipelineLayout> TryGetCompiledPipelineLayout(const SequencerConfig& sequencerConfig) const override;

		void			SetGlobalSelector(StringSection<> name, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type) override;
		T1(Type) void   SetGlobalSelector(StringSection<> name, Type value);
		void			RemoveGlobalSelector(StringSection<> name) override;

		void			RebuildAllOutOfDatePipelines() override;

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
		void RebuildAllPipelinesAlreadyLocked(unsigned poolGuid, PipelineAccelerator& pipeline);

		std::shared_ptr<SamplerPool> _samplerPool;
		std::shared_ptr<PipelineCollection> _pipelineCollection;
		std::shared_ptr<IDrawablesPool> _drawablesPool;
		std::shared_ptr<ICompiledLayoutPool> _layoutPatcher;
		PipelineAcceleratorPoolFlags::BitField _flags;

		#if defined(_DEBUG)
			mutable std::optional<std::thread::id> _lockForThreadingThread;
			std::thread::id _boundThreadId;
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

	auto PipelineAcceleratorPool::GetPipelineMarker(
		PipelineAccelerator& pipelineAccelerator, 
		const SequencerConfig& sequencerConfig) const -> std::shared_ptr<::Assets::Marker<Pipeline>>
	{
		// We must lock the "_constructionLock" for this -- so it's less advisable to call this often
		// TryGetPipeline doesn't take a lock and is more efficient to call frequently
		// This will also return nullptr if the pipeline has already been completed and is accessable via TryGetPipeline
		ScopedLock(_constructionLock);
		#if defined(_DEBUG)
			unsigned poolId = unsigned(sequencerConfig._cfgId >> 32ull);
			if (poolId != pipelineAccelerator._ownerPoolId)
				Throw(std::runtime_error("Mixing a pipeline accelerator from an incorrect pool"));
		#endif
		
		return pipelineAccelerator.FindPipelineFutureAlreadyLocked(sequencerConfig);
	}

	auto PipelineAcceleratorPool::TryGetPipeline(
		PipelineAccelerator& pipelineAccelerator, 
		const SequencerConfig& sequencerConfig) const -> const Pipeline*
	{
		#if defined(_DEBUG)
			assert(_lockForThreadingThread.has_value() && _lockForThreadingThread.value() == std::this_thread::get_id());
			unsigned poolId = unsigned(sequencerConfig._cfgId >> 32ull);
			if (poolId != pipelineAccelerator._ownerPoolId)
				Throw(std::runtime_error("Mixing a pipeline accelerator from an incorrect pool"));
		#endif
		
		return pipelineAccelerator.TryGetPipeline(sequencerConfig);
	}

	std::shared_ptr<::Assets::Marker<ActualizedDescriptorSet>> PipelineAcceleratorPool::GetDescriptorSetMarker(DescriptorSetAccelerator& accelerator) const
	{
		ScopedLock(_constructionLock);
		return accelerator._descriptorSet;
	}

	const ActualizedDescriptorSet* PipelineAcceleratorPool::TryGetDescriptorSet(DescriptorSetAccelerator& accelerator) const
	{
		#if defined(_DEBUG)
			assert(_lockForThreadingThread.has_value() && _lockForThreadingThread.value() == std::this_thread::get_id());
		#endif
		return accelerator._descriptorSet->TryActualize();
	}

	::Assets::PtrToMarkerPtr<CompiledPipelineLayoutAsset> PipelineAcceleratorPool::GetCompiledPipelineLayoutMarker(const SequencerConfig& sequencerConfig) const
	{
		ScopedLock(_constructionLock);
		return sequencerConfig._pipelineLayout;
	}

	std::shared_ptr<ICompiledPipelineLayout> PipelineAcceleratorPool::TryGetCompiledPipelineLayout(const SequencerConfig& sequencerConfig) const
	{
		#if defined(_DEBUG)
			assert(_lockForThreadingThread.has_value() && _lockForThreadingThread.value() == std::this_thread::get_id());
		#endif
		auto actual = sequencerConfig._pipelineLayout->TryActualize();
		if (actual)
			return (*actual)->GetPipelineLayout();
		return nullptr;
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

		RebuildAllPipelinesAlreadyLocked(_guid, *newAccelerator);

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

		RebuildAllPipelinesAlreadyLocked(_guid, *newAccelerator);

		return newAccelerator;
	}

	std::shared_ptr<DescriptorSetAccelerator> PipelineAcceleratorPool::CreateDescriptorSetAccelerator(
		const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
		IteratorRange<Assets::ScaffoldCmdIterator> materialMachine,
		std::shared_ptr<void> memoryHolder,
		const std::shared_ptr<DeformerToDescriptorSetBinding>& deformBinding)
	{
		std::shared_ptr<DescriptorSetAccelerator> result;
		{
			ScopedLock(_constructionLock);

			uint64_t hash = HashMaterialMachine(materialMachine);
			if (shaderPatches)
				hash = HashCombine(shaderPatches->GetHash(), hash);
			if (deformBinding)
				hash = HashCombine(deformBinding->GetHash(), hash);

			// If it already exists in the cache, just return it now
			auto cachei = LowerBound(_descriptorSetAccelerators, hash);
			if (cachei != _descriptorSetAccelerators.end() && cachei->first == hash) {
				auto l = cachei->second.lock();
				if (l)
					return l;
			}

			if (_drawablesPool) {
				result = _drawablesPool->MakeProtectedPtr<DescriptorSetAccelerator>();
			} else
				result = std::make_shared<DescriptorSetAccelerator>();
			result->_descriptorSet = std::make_shared<::Assets::Marker<ActualizedDescriptorSet>>("descriptorset-accelerator");

			if (cachei != _descriptorSetAccelerators.end() && cachei->first == hash) {
				cachei->second = result;		// (we replaced one that expired)
			} else {
				_descriptorSetAccelerators.insert(cachei, std::make_pair(hash, result));
			}
		}

		// We don't need to have "_constructionLock" after we've added the Marker to _descriptorSetAccelerators, so let's do the
		// rest outside of the lock

		bool generateBindingInfo = !!(_flags & PipelineAcceleratorPoolFlags::RecordDescriptorSetBindingInfo);
		if (shaderPatches) {
			auto patchCollectionFuture = _layoutPatcher->GetPatchCollectionFuture(*shaderPatches);

			// Most of the time, it will be ready immediately, and we can avoid some of the overhead of the
			// future continuation functions
			if (auto* patchCollection = patchCollectionFuture->TryActualize()) {
				ConstructDescriptorSetHelper{_device, _samplerPool.get(), PipelineType::Graphics, generateBindingInfo}
					.Construct(
						result->_descriptorSet->AdoptPromise(),
						(*patchCollection)->GetInterface().GetMaterialDescriptorSet(),
						materialMachine, deformBinding.get());
			} else {
				std::weak_ptr<IDevice> weakDevice = _device;
				::Assets::WhenAll(patchCollectionFuture).ThenConstructToPromise(
					result->_descriptorSet->AdoptPromise(),
					[materialMachine, memoryHolder, weakDevice, generateBindingInfo, samplerPool=std::weak_ptr<SamplerPool>(_samplerPool), deformBinding](
						std::promise<ActualizedDescriptorSet>&& promise,
						std::shared_ptr<CompiledShaderPatchCollection> patchCollection) {

						auto d = weakDevice.lock();
						if (!d)
							Throw(std::runtime_error("Device has been destroyed"));
						
						ConstructDescriptorSetHelper{d, samplerPool.lock().get(), PipelineType::Graphics, generateBindingInfo}
							.Construct(
								std::move(promise),
								patchCollection->GetInterface().GetMaterialDescriptorSet(),
								materialMachine, deformBinding.get());
					});
			}
		} else {
			ConstructDescriptorSetHelper{_device, _samplerPool.get(), PipelineType::Graphics, generateBindingInfo}
				.Construct(
					result->_descriptorSet->AdoptPromise(),
					_layoutPatcher->GetBaseMaterialDescriptorSetLayout(),
					materialMachine, deformBinding.get());
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
					result->_pipelineLayout = _layoutPatcher->GetPatchedPipelineLayout(result->_delegate->GetPipelineLayout());
					result->_cfgId = cfgId;
					result->_name = name;
					i->second = result;

					// If a pipeline accelerator was added while this sequencer config was expired, the pipeline
					// accelerator would not have been configured. We have to check for this case and construct
					// as necessary -- 
					for (auto& accelerator:_pipelineAccelerators) {
						auto a = accelerator.second.lock();
						if (!a || !(*a).PipelineValidPipelineOrFuture(*result))
							a->BeginPrepareForSequencerStateAlreadyLocked(result, _globalSelectors, _pipelineCollection, *_layoutPatcher);
					}
				} else {
					if (!name.empty() && !XlFindString(result->_name, name))
						result->_name += "|" + name;		// we're repurposing the same cfg for something else
				}

				return result;
			}
		}

		auto cfgId = SequencerConfigId(_sequencerConfigById.size()) | (SequencerConfigId(_guid) << 32ull);
		auto result = std::make_shared<SequencerConfig>(std::move(cfg));
		result->_pipelineLayout = _layoutPatcher->GetPatchedPipelineLayout(result->_delegate->GetPipelineLayout());
		result->_cfgId = cfgId;
		result->_name = name;

		_sequencerConfigById.emplace_back(std::make_pair(hash, result));		// (note; only holding onto a weak pointer here)

		// trigger creation of pipeline states for all accelerators
		for (auto& accelerator:_pipelineAccelerators) {
			auto a = accelerator.second.lock();
			if (a)
				a->BeginPrepareForSequencerStateAlreadyLocked(result, _globalSelectors, _pipelineCollection, *_layoutPatcher);
		}

		return result;
	}

	void PipelineAcceleratorPool::RebuildAllPipelinesAlreadyLocked(unsigned poolGuid, PipelineAccelerator& pipeline)
	{
		for (unsigned c=0; c<_sequencerConfigById.size(); ++c) {
			auto cfgId = SequencerConfigId(c) | (SequencerConfigId(poolGuid) << 32ull);
			auto l = _sequencerConfigById[c].second.lock();
			if (l) 
				pipeline.BeginPrepareForSequencerStateAlreadyLocked(l, _globalSelectors, _pipelineCollection, *_layoutPatcher);
		}
	}

	void PipelineAcceleratorPool::RebuildAllPipelinesAlreadyLocked(unsigned poolGuid)
	{
		for (auto& accelerator:_pipelineAccelerators) {
			auto a = accelerator.second.lock();
			if (a)
				RebuildAllPipelinesAlreadyLocked(poolGuid, *a);
		}
	}

	void PipelineAcceleratorPool::RebuildAllOutOfDatePipelines()
	{
		#if defined(_DEBUG)
			assert(std::this_thread::get_id() == _boundThreadId);
		#endif
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

		// Look through every pipeline registered in this pool, and 
		// trigger a rebuild of any that appear to be out of date.
		// This allows us to support hotreloading when files change, etc
		std::vector<std::shared_ptr<SequencerConfig>> lockedSequencerConfigs;
		lockedSequencerConfigs.resize(_sequencerConfigById.size());
		unsigned invalidSequencerIndices[_sequencerConfigById.size()];
		unsigned invalidSequencerCount = 0;

		for (unsigned c=0; c<_sequencerConfigById.size(); ++c) {
			auto cfg = _sequencerConfigById[c].second.lock();
			if (cfg && cfg->_pipelineLayout->GetDependencyValidation().GetValidationIndex() != 0) {
				assert(c == unsigned(cfg->_cfgId));
				// rebuild pipeline layout asset
				cfg->_pipelineLayout = _layoutPatcher->GetPatchedPipelineLayout(cfg->_delegate->GetPipelineLayout());
				invalidSequencerIndices[invalidSequencerCount++] = c;
			}
			lockedSequencerConfigs[c] = std::move(cfg);
		}
					
		for (auto& accelerator:_pipelineAccelerators) {
			auto a = accelerator.second.lock();
			if (a) {
				for (auto invalidSequencer:MakeIteratorRange(invalidSequencerIndices, &invalidSequencerIndices[invalidSequencerCount])) {
					// It's out of date -- let's rebuild and reassign it
					a->BeginPrepareForSequencerStateAlreadyLocked(lockedSequencerConfigs[invalidSequencer], _globalSelectors, _pipelineCollection, *_layoutPatcher);
				}

				// check for completed/invalidated pipelines
				if (a->_completedGraphicsPipelines.size() < _sequencerConfigById.size())
					a->_completedGraphicsPipelines.resize(_sequencerConfigById.size());

				for (auto i=a->_pendingGraphicsPipelines.begin(); i!=a->_pendingGraphicsPipelines.end();) {
					PipelineAccelerator::Pipeline pipeline;
					::Assets::DependencyValidation depVal;
					::Assets::Blob b;
					auto state = i->second->CheckStatusBkgrnd(pipeline, depVal, b);
					if (state == ::Assets::AssetState::Pending) {
						++i;
						continue;
					} else if (state == ::Assets::AssetState::Ready) {
						a->_completedGraphicsPipelines[i->first] = std::move(pipeline);
						i=a->_pendingGraphicsPipelines.erase(i);
					} else {
						// "invalid" state. Attempt to rebuild on changes
						a->_completedGraphicsPipelines[i->first] = {};
						if (depVal.GetValidationIndex() != 0 && lockedSequencerConfigs[i->first]) {
							// should not change _pendingGraphicsPipelines, just overwrite existing entry
							a->BeginPrepareForSequencerStateAlreadyLocked(lockedSequencerConfigs[i->first], _globalSelectors, _pipelineCollection, *_layoutPatcher);
						}
						++i;
					}
				}

				for (unsigned c=0; c<_sequencerConfigById.size(); ++c) {
					if (lockedSequencerConfigs[c]) {
						if (a->_completedGraphicsPipelines[c].GetDependencyValidation().GetValidationIndex() != 0) {
							auto existing = std::find_if(a->_pendingGraphicsPipelines.begin(), a->_pendingGraphicsPipelines.end(), [c](const auto& p) { return p.first == c; });
							if (existing != a->_pendingGraphicsPipelines.end()) continue;	// already scheduled this rebuild
							a->BeginPrepareForSequencerStateAlreadyLocked(lockedSequencerConfigs[c], _globalSelectors, _pipelineCollection, *_layoutPatcher);
						}
					} else {
						// sequencer destroyed, release related pipelines
						a->_completedGraphicsPipelines[c] = {};
					}
				}
			}
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
		#if defined(_DEBUG)
			_boundThreadId = std::this_thread::get_id();
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
