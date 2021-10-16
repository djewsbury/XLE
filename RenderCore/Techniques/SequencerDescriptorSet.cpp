// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DrawableDelegates.h"
#include "PipelineAccelerator.h"
#include "DrawableDelegates.h"
#include "ParsingContext.h"
#include "DrawablesInternal.h"
#include "Techniques.h"
#include "CommonResources.h"
#include "../BufferView.h"
#include "../IThreadContext.h"
#include "../IDevice.h"
#include "../Metal/InputLayout.h"
#include "../Metal/DeviceContext.h"
#include "../../OSServices/Log.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../Utility/BitUtils.h"


#include "../../Utility/HeapUtils.h"
#include "../Metal/ObjectFactory.h"
#include "../Vulkan/IDeviceVulkan.h"

namespace RenderCore { namespace Techniques
{
	template<unsigned PageSize>
		class GPUTrackerHeap
	{
	public:
		unsigned GetNextFreeItem()
		{
			auto producerMarker = _tracker->GetProducerMarker();
			auto consumerMarker = _tracker->GetConsumerMarker();

			// pop any completed items first
			for (auto &page:_pages) {
				while (!page._allocatedItems.empty() && page._allocatedItems.front().first <= consumerMarker) {
					page._freeItems.try_emplace_back(page._allocatedItems.front().second);
					page._allocatedItems.pop_front();
				}
			}
			while (_pages.size() > 1 && _pages[_pages.size()-1]._allocatedItems.empty())
				_pages.erase(_pages.end()-1);

			for (auto page=_pages.begin(); page!=_pages.end(); ++page) {
				if (!page->_freeItems.empty()) {
					auto item = page->_freeItems.front();
					page->_freeItems.pop_front();
					page->_allocatedItems.try_emplace_back(producerMarker, item);
					return PageSize*std::distance(_pages.begin(), page) + item;
				}
			}

			_pages.push_back({});
			auto& page = *(_pages.end()-1);
			auto item = page._freeItems.front();
			page._freeItems.pop_front();
			page._allocatedItems.try_emplace_back(producerMarker, item);
			return PageSize*(_pages.size()-1) + item;
		}

		GPUTrackerHeap(IDevice& device)
		{
			auto* vulkanDevice = (IDeviceVulkan*)device.QueryInterface(typeid(IDeviceVulkan).hash_code());
			if (!vulkanDevice)
				Throw(std::runtime_error("Requires vulkan device for GPU tracking"));
			_tracker = vulkanDevice->GetAsyncTracker();
		}
	private:
		std::shared_ptr<Metal_Vulkan::IAsyncTracker> _tracker;

		struct Page
		{
			CircularBuffer<std::pair<Metal_Vulkan::IAsyncTracker::Marker, unsigned>, PageSize> _allocatedItems;
			CircularBuffer<unsigned, PageSize> _freeItems;

			Page()
			{
				for (unsigned c=0; c<PageSize; ++c)
					_freeItems.try_emplace_back(c);
			}
		};
		std::vector<Page> _pages;
	};

	constexpr unsigned s_poolPageSize = 16;

	class SemiConstantDescriptorSet : public ISemiConstantDescriptorSet
	{
	public:
		UniformsStreamInterface _finalUSI;
		uint64_t _slotsQueried_ResourceViews = 0ull;
		uint64_t _slotsQueried_Samplers = 0ull;
		uint64_t _slotsQueried_ImmediateDatas = 0ull;

		std::vector<IResourceView*> _queriedResources;
		std::vector<ISampler*> _queriedSamplers;
		std::vector<UniformsStream::ImmediateData> _queriedImmediateDatas;

		std::vector<uint8_t> _tempDataBuffer;

		size_t _workingTempBufferSize = 0;
		static constexpr unsigned s_immediateDataAlignment = 8;

		////////////////////////////////////////////////////////////////////////////////////
		struct ShaderResourceDelegateBinding
		{
			IShaderResourceDelegate* _delegate = nullptr;
			std::vector<std::pair<size_t, size_t>> _immediateDataBeginAndEnd;

			uint64_t _usiSlotsFilled_ResourceViews = 0ull;
			uint64_t _usiSlotsFilled_Samplers = 0ull;
			uint64_t _usiSlotsFilled_ImmediateDatas = 0ull;

			std::vector<unsigned> _resourceInterfaceToUSI;
			std::vector<unsigned> _immediateDataInterfaceToUSI;
			std::vector<unsigned> _samplerInterfaceToUSI;
		};
		std::vector<ShaderResourceDelegateBinding> _srBindings;
		void Prepare(IShaderResourceDelegate& del, ParsingContext& parsingContext);
		void QueryResources(ParsingContext& parsingContext, uint64_t resourcesToQuery, ShaderResourceDelegateBinding& del);
		void QuerySamplers(ParsingContext& parsingContext, uint64_t samplersToQuery, ShaderResourceDelegateBinding& del);
		void QueryImmediateDatas(ParsingContext& parsingContext, uint64_t immediateDatasToQuery, ShaderResourceDelegateBinding& del);

		////////////////////////////////////////////////////////////////////////////////////
		struct UniformBufferDelegateBinding
		{
			IUniformBufferDelegate* _delegate = nullptr;
			size_t _size = 0;

			unsigned _usiSlotFilled = 0;
			size_t _tempBufferOffset = 0;
		};
		std::vector<UniformBufferDelegateBinding> _uBindings;
		void Prepare(IUniformBufferDelegate& del, uint64_t delBinding);
		void QueryImmediateDatas(ParsingContext& parsingContext, uint64_t immediateDatasToQuery, UniformBufferDelegateBinding& del);

		////////////////////////////////////////////////////////////////////////////////////
		void QueryResources(ParsingContext& parsingContext, uint64_t resourcesToQuery);
		void QuerySamplers(ParsingContext& parsingContext, uint64_t samplersToQuery);
		void QueryImmediateDatas(ParsingContext& parsingContext, uint64_t immediateDatasToQuery);

		////////////////////////////////////////////////////////////////////////////////////
		std::vector<std::pair<uint64_t, std::shared_ptr<IUniformBufferDelegate>>> _uniformDelegates;
		std::vector<std::shared_ptr<IShaderResourceDelegate>> _shaderResourceDelegates;
		bool _pendingReconfigureDescriptorSet = true;
		bool _pendingRebuildDescriptorSet = true;

		DescriptorSetSignature _signature;

		GPUTrackerHeap<s_poolPageSize> _trackerHeap;
		std::vector<std::shared_ptr<IDescriptorSet>> _descriptorSetPool;
		unsigned _currentDescriptorSet = ~0u;

		void ReconfigureDescriptorSet(ParsingContext& parsingContext);
		void RebuildDescriptorSet(IThreadContext& threadContext, ParsingContext& parsingContext);

		////////////////////////////////////////////////////////////////////////////////////
		void AddShaderResourceDelegate(const std::shared_ptr<IShaderResourceDelegate>&) override;
		void RemoveShaderResourceDelegate(IShaderResourceDelegate&) override;
		
		void AddUniformDelegate(uint64_t binding, const std::shared_ptr<IUniformBufferDelegate>&) override;
		void RemoveUniformDelegate(IUniformBufferDelegate&) override;

		void InvalidateUniforms() override;
		const UniformsStreamInterface& GetInterface(IThreadContext&, ParsingContext& parsingContext) override;

		SemiConstantDescriptorSet(
			IDevice& device,
			uint64_t descriptorSetName,
			const RenderCore::Assets::PredefinedDescriptorSetLayout& layout,
			CommonResourceBox& res);
		~SemiConstantDescriptorSet();
	};

	void SemiConstantDescriptorSet::Prepare(IShaderResourceDelegate& del, ParsingContext& parsingContext)
	{
		ShaderResourceDelegateBinding newBinding;
		newBinding._delegate = &del;

		auto& usi = del._interface;
		newBinding._resourceInterfaceToUSI.reserve(usi._resourceViewBindings.size());
		for (auto b:usi._resourceViewBindings) {
			auto existing = std::find(_finalUSI._resourceViewBindings.begin(), _finalUSI._resourceViewBindings.end(), b);
			if (existing != _finalUSI._resourceViewBindings.end()) {
				newBinding._resourceInterfaceToUSI.push_back(~0u);
			} else {
				auto finalUSISlot = (unsigned)_finalUSI._resourceViewBindings.size();
				newBinding._resourceInterfaceToUSI.push_back(finalUSISlot);
				_finalUSI._resourceViewBindings.push_back(b);
				assert(finalUSISlot < 64);
				newBinding._usiSlotsFilled_ResourceViews |= 1ull << uint64_t(finalUSISlot);
			}
		}

		newBinding._samplerInterfaceToUSI.reserve(usi._samplerBindings.size());
		for (auto b:usi._samplerBindings) {
			auto existing = std::find(_finalUSI._samplerBindings.begin(), _finalUSI._samplerBindings.end(), b);
			if (existing != _finalUSI._samplerBindings.end()) {
				newBinding._samplerInterfaceToUSI.push_back(~0u);
			} else {
				auto finalUSISlot = (unsigned)_finalUSI._samplerBindings.size();
				newBinding._samplerInterfaceToUSI.push_back(finalUSISlot);
				_finalUSI._samplerBindings.push_back(b);
				assert(finalUSISlot < 64);
				newBinding._usiSlotsFilled_Samplers |= 1ull << uint64_t(finalUSISlot);
			}
		}

		newBinding._immediateDataInterfaceToUSI.reserve(usi._immediateDataBindings.size());
		unsigned idx=0;
		for (auto b:usi._immediateDataBindings) {
			auto existing = std::find(_finalUSI._immediateDataBindings.begin(), _finalUSI._immediateDataBindings.end(), b);
			if (existing != _finalUSI._immediateDataBindings.end()) {
				newBinding._immediateDataInterfaceToUSI.push_back(~0u);
				newBinding._immediateDataBeginAndEnd.push_back({});
			} else {
				auto finalUSISlot = (unsigned)_finalUSI._immediateDataBindings.size();
				newBinding._immediateDataInterfaceToUSI.push_back(finalUSISlot);
				_finalUSI._immediateDataBindings.push_back(b);
				assert(finalUSISlot < 64);
				newBinding._usiSlotsFilled_ImmediateDatas |= 1ull << uint64_t(finalUSISlot);

				// Note that we need to support GetImmediateDataSize() returning zero. Here we're querying the size of everything
				// from the delegate interface, not just the ones that are actually bound
				auto size = del.GetImmediateDataSize(parsingContext, nullptr, idx);	
				std::pair<size_t, size_t> beginAndEnd;
				beginAndEnd.first = _workingTempBufferSize;
				beginAndEnd.second = _workingTempBufferSize + size;
				newBinding._immediateDataBeginAndEnd.push_back(beginAndEnd);
				_workingTempBufferSize += CeilToMultiplePow2(size, s_immediateDataAlignment);
			}
			++idx;
		}

		_srBindings.push_back(std::move(newBinding));
	}

	void SemiConstantDescriptorSet::QueryResources(ParsingContext& parsingContext, uint64_t resourcesToQuery, ShaderResourceDelegateBinding& del)
	{
		auto toLoad = resourcesToQuery & del._usiSlotsFilled_ResourceViews;
		if (!toLoad) return;

		uint64_t toLoadDelegate = 0;
		for (unsigned c=0; c<del._resourceInterfaceToUSI.size(); ++c)
			if (del._resourceInterfaceToUSI[c] != ~0u && (resourcesToQuery & (1 << uint64_t(del._resourceInterfaceToUSI[c]))))
				toLoadDelegate |= 1ull << uint64_t(c);

		assert(toLoadDelegate);
		auto minToCheck = xl_ctz8(toLoad);
		auto maxPlusOneToCheck = 64 - xl_clz8(toLoad);
		IResourceView* rvDst[maxPlusOneToCheck];

		del._delegate->WriteResourceViews(parsingContext, nullptr, toLoadDelegate, MakeIteratorRange(rvDst, &rvDst[maxPlusOneToCheck]));
		
		for (unsigned c=minToCheck; c<maxPlusOneToCheck; ++c)
			if (del._resourceInterfaceToUSI[c] != ~0u && (resourcesToQuery & (1 << uint64_t(del._resourceInterfaceToUSI[c])))) {
				assert(rvDst[c]);
				_queriedResources[del._resourceInterfaceToUSI[c]] = rvDst[c];
			}

		_slotsQueried_ResourceViews |= toLoad;
	}

	void SemiConstantDescriptorSet::QuerySamplers(ParsingContext& parsingContext, uint64_t samplersToQuery, ShaderResourceDelegateBinding& del)
	{
		auto toLoad = samplersToQuery & del._usiSlotsFilled_Samplers;
		if (!toLoad) return;

		uint64_t toLoadDelegate = 0;
		for (unsigned c=0; c<del._samplerInterfaceToUSI.size(); ++c)
			if (del._samplerInterfaceToUSI[c] != ~0u && (samplersToQuery & (1 << uint64_t(del._samplerInterfaceToUSI[c]))))
				toLoadDelegate |= 1ull << uint64_t(c);

		assert(toLoadDelegate);
		auto minToCheck = xl_ctz8(toLoad);
		auto maxPlusOneToCheck = 64 - xl_clz8(toLoad);
		ISampler* samplerDst[maxPlusOneToCheck];

		del._delegate->WriteSamplers(parsingContext, nullptr, toLoadDelegate, MakeIteratorRange(samplerDst, &samplerDst[maxPlusOneToCheck]));
		
		for (unsigned c=minToCheck; c<maxPlusOneToCheck; ++c)
			if (del._samplerInterfaceToUSI[c] != ~0u && (samplersToQuery & (1 << uint64_t(del._samplerInterfaceToUSI[c])))) {
				assert(samplerDst[c]);
				_queriedSamplers[del._samplerInterfaceToUSI[c]] = samplerDst[c];
			}

		_slotsQueried_Samplers |= toLoad;
	}

	void SemiConstantDescriptorSet::QueryImmediateDatas(ParsingContext& parsingContext, uint64_t immediateDatasToQuery, ShaderResourceDelegateBinding& del)
	{
		auto toLoad = immediateDatasToQuery & del._usiSlotsFilled_ImmediateDatas;
		if (!toLoad) return;

		uint64_t toLoadDelegate = 0;
		for (unsigned c=0; c<del._immediateDataInterfaceToUSI.size(); ++c)
			if (del._immediateDataInterfaceToUSI[c] != ~0u && (toLoad & (1 << uint64_t(del._immediateDataInterfaceToUSI[c]))))
				toLoadDelegate |= 1ull << uint64_t(c);

		assert(toLoadDelegate);
		auto minToCheck = xl_ctz8(toLoadDelegate);
		auto maxPlusOneToCheck = 64 - xl_clz8(toLoadDelegate);

		for (unsigned c=minToCheck; c<maxPlusOneToCheck; ++c) {
			if (!(toLoadDelegate & (1ull << uint64_t(c)))) continue;
			auto beginAndEnd = del._immediateDataBeginAndEnd[c];
			auto dstRange = MakeIteratorRange(_tempDataBuffer.begin() + beginAndEnd.first, _tempDataBuffer.begin() + beginAndEnd.second); 
			del._delegate->WriteImmediateData(parsingContext, nullptr, c, dstRange);
			_queriedImmediateDatas[del._immediateDataInterfaceToUSI[c]] = dstRange;
		}

		_slotsQueried_ImmediateDatas |= toLoad;
	}

	void SemiConstantDescriptorSet::Prepare(IUniformBufferDelegate& del, uint64_t delBinding)
	{
		auto existing = std::find(_finalUSI._immediateDataBindings.begin(), _finalUSI._immediateDataBindings.end(), delBinding);
		if (existing != _finalUSI._immediateDataBindings.end())
			return;
			
		UniformBufferDelegateBinding newBinding;
		newBinding._delegate = &del;
		newBinding._usiSlotFilled = (unsigned)_finalUSI._immediateDataBindings.size();
		_finalUSI._immediateDataBindings.push_back(delBinding);
		newBinding._size = del.GetSize();
		newBinding._tempBufferOffset = _workingTempBufferSize;
		_workingTempBufferSize += CeilToMultiplePow2(newBinding._size, s_immediateDataAlignment);

		_uBindings.push_back(newBinding);
	}

	void SemiConstantDescriptorSet::QueryImmediateDatas(ParsingContext& parsingContext, uint64_t immediateDatasToQuery, UniformBufferDelegateBinding& del)
	{
		auto mask = 1ull << uint64_t(del._usiSlotFilled);
		if (!(immediateDatasToQuery & mask)) return;

		auto dstRange = MakeIteratorRange(_tempDataBuffer.begin() + del._tempBufferOffset, _tempDataBuffer.begin() + del._tempBufferOffset + del._size); 
		del._delegate->WriteImmediateData(
			parsingContext, nullptr,
			dstRange);
		
		_queriedImmediateDatas[del._usiSlotFilled] = dstRange;
		_slotsQueried_ImmediateDatas |= mask;
	}

	void SemiConstantDescriptorSet::QueryResources(ParsingContext& parsingContext, uint64_t resourcesToQuery)
	{
		resourcesToQuery &= ~_slotsQueried_ResourceViews;
		if (!resourcesToQuery) return;
		for (auto& del:_srBindings) QueryResources(parsingContext, resourcesToQuery, del);
	}

	void SemiConstantDescriptorSet::QuerySamplers(ParsingContext& parsingContext, uint64_t samplersToQuery)
	{
		samplersToQuery &= ~_slotsQueried_Samplers;
		if (!samplersToQuery) return;
		for (auto& del:_srBindings) QuerySamplers(parsingContext, samplersToQuery, del);
	}

	void SemiConstantDescriptorSet::QueryImmediateDatas(ParsingContext& parsingContext, uint64_t immediateDatasToQuery)
	{
		immediateDatasToQuery &= ~_slotsQueried_ImmediateDatas;
		if (!immediateDatasToQuery) return;
		for (auto& del:_srBindings) QueryImmediateDatas(parsingContext, immediateDatasToQuery, del);
		for (auto& del:_uBindings) QueryImmediateDatas(parsingContext, immediateDatasToQuery, del);
	}
	
	void SemiConstantDescriptorSet::RebuildDescriptorSet(IThreadContext& threadContext, ParsingContext& parsingContext)
	{
		// Create a temporary descriptor set, with per-sequencer bindings
		// We need to look for something providing data for this:
		// * parsingContext uniform buffer delegate
		// * sequencer technique uniform buffer delegate
		// * sequencer technique shader resource delegate
		// Unfortunately we have to do a make a lot of small temporary allocations in order to
		// calculate how the various delegates map onto the descriptor set layout. It might be
		// worth considering caching this result, because there should actually only be a finite
		// number of different configurations in most use cases

		assert(parsingContext.GetTechniqueContext()._sequencerDescSetLayout);
		const auto& descSetLayout = *parsingContext.GetTechniqueContext()._sequencerDescSetLayout;
		
		std::vector<DescriptorSetInitializer::BindTypeAndIdx> bindTypesAndIdx;
		bindTypesAndIdx.reserve(descSetLayout._slots.size());
		uint64_t resourcesWeNeed = 0ull;
		uint64_t samplersWeNeed = 0ull;
		uint64_t immediateDatasWeNeed = 0ull;

		for (unsigned slotIdx=0; slotIdx<descSetLayout._slots.size(); ++slotIdx) {
			auto hashName = Hash64(descSetLayout._slots[slotIdx]._name);

			if (descSetLayout._slots[slotIdx]._type == DescriptorType::Sampler) {
				auto samplerBinding = std::find(_finalUSI._samplerBindings.begin(), _finalUSI._samplerBindings.end(), hashName);
				if (samplerBinding != _finalUSI._samplerBindings.end()) {
					auto samplerIdx = (unsigned)std::distance(_finalUSI._samplerBindings.begin(), samplerBinding);
					bindTypesAndIdx.push_back({DescriptorSetInitializer::BindType::Sampler, samplerIdx});
					samplersWeNeed |= 1ull << uint64_t(samplerIdx);
					continue;
				}
				#if defined(_DEBUG)		// just check to make sure we're not attempting to bind some incorrect type here 
					auto resourceBinding = std::find(_finalUSI._resourceViewBindings.begin(), _finalUSI._resourceViewBindings.end(), hashName);
					if (resourceBinding != _finalUSI._resourceViewBindings.end())
						Log(Warning) << "Resource view provided for descriptor set slot (" << descSetLayout._slots[slotIdx]._name << "), however, this lot is 'sampler' type in the descriptor set layout." << std::endl;
					auto immediateDataBinding = std::find(_finalUSI._immediateDataBindings.begin(), _finalUSI._immediateDataBindings.end(), hashName);
					if (immediateDataBinding != _finalUSI._immediateDataBindings.end())
						Log(Warning) << "Immediate data provided for descriptor set slot (" << descSetLayout._slots[slotIdx]._name << "), however, this lot is 'sampler' type in the descriptor set layout." << std::endl;
				#endif
			} else {
				auto resourceBinding = std::find(_finalUSI._resourceViewBindings.begin(), _finalUSI._resourceViewBindings.end(), hashName);
				if (resourceBinding != _finalUSI._resourceViewBindings.end()) {
					auto resourceIdx = (unsigned)std::distance(_finalUSI._resourceViewBindings.begin(), resourceBinding);
					bindTypesAndIdx.push_back({DescriptorSetInitializer::BindType::ResourceView, resourceIdx});
					resourcesWeNeed |= 1ull << uint64_t(resourceIdx);
					continue;
				}

				auto immediateDataBinding = std::find(_finalUSI._immediateDataBindings.begin(), _finalUSI._immediateDataBindings.end(), hashName);
				if (immediateDataBinding != _finalUSI._immediateDataBindings.end()) {
					auto resourceIdx = (unsigned)std::distance(_finalUSI._immediateDataBindings.begin(), immediateDataBinding);
					bindTypesAndIdx.push_back({DescriptorSetInitializer::BindType::ImmediateData, resourceIdx});
					immediateDatasWeNeed |= 1ull << uint64_t(resourceIdx);
					continue;
				}

				#if defined(_DEBUG)		// just check to make sure we're not attempting to bind some incorrect type here 
					auto samplerBinding = std::find(_finalUSI._samplerBindings.begin(), _finalUSI._samplerBindings.end(), hashName);
					if (samplerBinding != _finalUSI._samplerBindings.end())
						Log(Warning) << "Sampler provided for descriptor set slot (" << descSetLayout._slots[slotIdx]._name << "), however, this lot is not a sampler type in the descriptor set layout." << std::endl;
				#endif
			}

			bindTypesAndIdx.push_back({});		// didn't find any binding
		}

		// Now that we know what we need, we should query the delegates to get the associated data
		QueryResources(parsingContext, resourcesWeNeed);
		QuerySamplers(parsingContext, samplersWeNeed);
		QueryImmediateDatas(parsingContext, immediateDatasWeNeed);

		DescriptorSetInitializer initializer;
		initializer._slotBindings = MakeIteratorRange(bindTypesAndIdx);
		initializer._bindItems._resourceViews = MakeIteratorRange(_queriedResources);
		initializer._bindItems._samplers = MakeIteratorRange(_queriedSamplers);
		initializer._bindItems._immediateData = MakeIteratorRange(_queriedImmediateDatas);

		// If useCmdListAttachedStorage is true, move the "ImmediateData" items into cmd list attached storage. The
		// alternative is attaching storage to the descriptor set itself; but this isn't ideal because it requires
		// allocating new resources 
		const bool useCmdListAttachedStorage = true;
		const IResourceView* newResourceViews[_queriedResources.size() + _queriedImmediateDatas.size()];
		std::shared_ptr<IResourceView> tempResViews[_queriedResources.size() + _queriedImmediateDatas.size()];
		if (useCmdListAttachedStorage) {
			//....
			size_t immDataStart = -1, immDataEnd = 0;
			for (auto& immData:initializer._bindItems._immediateData) {
				immDataStart = std::min(immDataStart, size_t(immData.first));
				immDataEnd = std::min(immDataStart, size_t(immData.second));
			}
			auto dataSize = immDataEnd-immDataStart;
			if (dataSize) {
				auto storage = Metal::DeviceContext::Get(threadContext)->MapTemporaryStorage(dataSize, BindFlag::ConstantBuffer);
				std::memcpy(storage.GetData().begin(), (const void*)immDataStart, dataSize);

				unsigned newResourceViewCount = 0;
				for (auto& src:initializer._bindItems._resourceViews)
					newResourceViews[newResourceViewCount++] = src;

				auto resource = storage.GetResource();					
				auto beginAndEndInRes = storage.GetBeginAndEndInResource(); 
				for (auto& immData:initializer._bindItems._immediateData) {
					tempResViews[newResourceViewCount] = resource->CreateBufferView(
						BindFlag::ConstantBuffer, 
						size_t(immData.first) - immDataStart + beginAndEndInRes.first,
						size_t(immData.second) - immDataStart + beginAndEndInRes.first);
					newResourceViews[newResourceViewCount] = tempResViews[newResourceViewCount].get();
					++newResourceViewCount;
				}

				initializer._bindItems._resourceViews = MakeIteratorRange(newResourceViews, &newResourceViews[newResourceViewCount]);
			}
		}

		_currentDescriptorSet = _trackerHeap.GetNextFreeItem();
		if (_currentDescriptorSet >= _descriptorSetPool.size()) {
			// _trackerHeap allocated a new page -- we need to resize the pool of descriptor sets
			auto initialSize = _descriptorSetPool.size();
			auto newPageCount = (_currentDescriptorSet+s_poolPageSize-1)/s_poolPageSize;
			_descriptorSetPool.resize(newPageCount*s_poolPageSize);
			DescriptorSetInitializer creationInitializer;
			creationInitializer._signature = &_signature;
			for (auto c=initialSize; c<_descriptorSetPool.size(); ++c)
				_descriptorSetPool[c] = threadContext.GetDevice()->CreateDescriptorSet(creationInitializer);
		}

		_descriptorSetPool[_currentDescriptorSet]->Write(initializer);
	}

	void SemiConstantDescriptorSet::InvalidateUniforms()
	{
		_slotsQueried_ResourceViews = _slotsQueried_Samplers = _slotsQueried_ImmediateDatas = 0ull;
	}

	const UniformsStreamInterface& SemiConstantDescriptorSet::GetInterface(IThreadContext& threadContext, ParsingContext& parsingContext)
	{
		if (_pendingReconfigureDescriptorSet)
			ReconfigureDescriptorSet(parsingContext);
		if (_pendingRebuildDescriptorSet)
			RebuildDescriptorSet(threadContext, parsingContext);
		return _finalUSI;
	}

	void SemiConstantDescriptorSet::ReconfigureDescriptorSet(ParsingContext& parsingContext)
	{
		// reset everything and rebuild all bindings
		_finalUSI._resourceViewBindings.clear();
		_finalUSI._resourceViewBindings.reserve(64);
		_finalUSI._immediateDataBindings.clear();
		_finalUSI._immediateDataBindings.reserve(64);
		_finalUSI._samplerBindings.clear();
		_finalUSI._samplerBindings.reserve(64);
		_slotsQueried_ResourceViews = _slotsQueried_Samplers = _slotsQueried_ImmediateDatas = 0ull;
		_workingTempBufferSize = 0;
		_srBindings.clear();
		_uBindings.clear();

		// Delegates we visit first will be preferred over subsequent delegates (if they bind the same thing)
		// So, we should go through in reverse order
		if (!_shaderResourceDelegates.empty())
			for (signed c=_shaderResourceDelegates.size()-1; c>=0; c--)
				Prepare(*_shaderResourceDelegates[c], parsingContext);

		if (!_uniformDelegates.empty())
			for (signed c=_uniformDelegates.size()-1; c>=0; c--)
				Prepare(*_uniformDelegates[c].second, _uniformDelegates[c].first);

		auto parserDelegates = parsingContext.GetShaderResourceDelegates();
		for (signed c=parserDelegates.size()-1; c>=0; c--)
			Prepare(*parserDelegates[c], parsingContext);

		auto parserUDelegates = parsingContext.GetUniformDelegates();
		for (signed c=parserUDelegates.size()-1; c>=0; c--)
			Prepare(*parserUDelegates[c].second, parserUDelegates[c].first);

		_queriedResources.resize(_finalUSI._resourceViewBindings.size(), nullptr);
		std::fill(_queriedResources.begin(), _queriedResources.end(), nullptr);
		_queriedSamplers.resize(_finalUSI._samplerBindings.size(), nullptr);
		std::fill(_queriedSamplers.begin(), _queriedSamplers.end(), nullptr);
		_queriedImmediateDatas.resize(_finalUSI._immediateDataBindings.size(), {});
		std::fill(_queriedImmediateDatas.begin(), _queriedImmediateDatas.end(), UniformsStream::ImmediateData{});
		_tempDataBuffer.resize(_workingTempBufferSize, 0);
		_pendingReconfigureDescriptorSet = false;
	}

	SemiConstantDescriptorSet::SemiConstantDescriptorSet(
		IDevice& device,
		uint64_t descriptorSetName,
		const RenderCore::Assets::PredefinedDescriptorSetLayout& layout,
		CommonResourceBox& res)
	: _trackerHeap(device)
	{
		_signature = layout.MakeDescriptorSetSignature(&res._samplerPool);

		DescriptorSetInitializer initializer;
		initializer._signature = &_signature;
		_descriptorSetPool.reserve(s_poolPageSize);
		for (unsigned c=0; c<s_poolPageSize; ++c)
			_descriptorSetPool.push_back(device.CreateDescriptorSet(initializer));
	}

	SemiConstantDescriptorSet::~SemiConstantDescriptorSet() {}

	void ApplyLooseUniforms(
		ISemiConstantDescriptorSet& i,
		Metal::DeviceContext& metalContext,
		Metal::SharedEncoder& encoder,
		ParsingContext& parsingContext,
		Metal::BoundUniforms& boundUniforms,
		unsigned groupIdx)
	{
		auto& uniformsHelper = *checked_cast<SemiConstantDescriptorSet*>(&i);
		assert(!uniformsHelper._pendingReconfigureDescriptorSet);
		uniformsHelper.QueryResources(parsingContext, boundUniforms.GetBoundLooseResources(groupIdx));
		uniformsHelper.QuerySamplers(parsingContext, boundUniforms.GetBoundLooseSamplers(groupIdx));
		uniformsHelper.QueryImmediateDatas(parsingContext, boundUniforms.GetBoundLooseImmediateDatas(groupIdx));
		UniformsStream us {
			uniformsHelper._queriedResources,
			uniformsHelper._queriedImmediateDatas,
			uniformsHelper._queriedSamplers };
		boundUniforms.ApplyLooseUniforms(metalContext, encoder, us, groupIdx);
	}

}}

