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
#include "Services.h"
#include "SubFrameUtil.h"
#include "../BufferView.h"
#include "../IDevice.h"
#include "../Metal/InputLayout.h"
#include "../Metal/DeviceContext.h"
#include "../../OSServices/Log.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../Utility/BitUtils.h"
#include <stack>

namespace RenderCore { namespace Techniques
{
	class SemiConstantDescriptorSet;
	class UniformDelegateGroup
	{
	public:
		using ChangeIndex = unsigned;
		std::vector<std::pair<uint64_t, std::shared_ptr<IUniformBufferDelegate>>> _uniformDelegates;
		std::vector<std::shared_ptr<IShaderResourceDelegate>> _shaderResourceDelegates;
		std::vector<std::pair<ChangeIndex, std::shared_ptr<UniformDelegateGroup>>> _baseGroups;

		ChangeIndex _currentChangeIndex = 0;

		void AddShaderResourceDelegate(const std::shared_ptr<IShaderResourceDelegate>& dele)
		{
			#if defined(_DEBUG)
				auto i = std::find_if(
					_shaderResourceDelegates.begin(), _shaderResourceDelegates.end(),
					[&dele](const std::shared_ptr<IShaderResourceDelegate>& p) { return p.get() == dele.get(); });
				assert(i == _shaderResourceDelegates.end());
			#endif
			_shaderResourceDelegates.push_back(dele);
			++_currentChangeIndex;
		}

		void RemoveShaderResourceDelegate(IShaderResourceDelegate& dele)
		{
			_shaderResourceDelegates.erase(
				std::remove_if(
					_shaderResourceDelegates.begin(), _shaderResourceDelegates.end(),
					[&dele](const std::shared_ptr<IShaderResourceDelegate>& p) { return p.get() == &dele; }),
				_shaderResourceDelegates.end());
			++_currentChangeIndex;
		}
		
		void AddUniformDelegate(uint64_t binding, const std::shared_ptr<IUniformBufferDelegate>& dele)
		{
			for (auto&d:_uniformDelegates)
				if (d.first == binding) {
					d.second = dele;
					return;
				}
			_uniformDelegates.push_back(std::make_pair(binding, dele));
			++_currentChangeIndex;
		}

		void RemoveUniformDelegate(IUniformBufferDelegate& dele)
		{
			_uniformDelegates.erase(
				std::remove_if(
					_uniformDelegates.begin(), _uniformDelegates.end(),
					[&dele](const std::pair<uint64_t, std::shared_ptr<IUniformBufferDelegate>>& p) { return p.second.get() == &dele; }),
				_uniformDelegates.end());
			++_currentChangeIndex;
		}

		void AddBase(std::shared_ptr<UniformDelegateGroup> base)
		{
			#if defined(_DEBUG)
				auto i = std::find_if(
					_baseGroups.begin(), _baseGroups.end(),
					[&base](const auto& p) { return p.second.get() == base.get(); });
				assert(i == _baseGroups.end());
			#endif
			_baseGroups.emplace_back(-1, std::move(base));
			++_currentChangeIndex;
		}

		void RemoveBase(UniformDelegateGroup& base)
		{
			_baseGroups.erase(
				std::remove_if(
					_baseGroups.begin(), _baseGroups.end(),
					[&base](const auto& p) { return p.second.get() == &base; }),
				_baseGroups.end());
			++_currentChangeIndex;
		}
	};

	class DelegateQueryHelper
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

		void Prepare(
			ParsingContext& parsingContext,
			const UniformDelegateGroup& group);
		void InvalidateUniforms();
	};

	void DelegateQueryHelper::Prepare(IShaderResourceDelegate& del, ParsingContext& parsingContext)
	{
		ShaderResourceDelegateBinding newBinding;
		newBinding._delegate = &del;

		auto& usi = del._interface;
		newBinding._resourceInterfaceToUSI.reserve(usi.GetResourceViewBindings().size());
		for (auto b:usi.GetResourceViewBindings()) {
			auto existing = std::find(_finalUSI.GetResourceViewBindings().begin(), _finalUSI.GetResourceViewBindings().end(), b);
			if (existing != _finalUSI.GetResourceViewBindings().end()) {
				newBinding._resourceInterfaceToUSI.push_back(~0u);
			} else {
				auto finalUSISlot = (unsigned)_finalUSI.GetResourceViewBindings().size();
				newBinding._resourceInterfaceToUSI.push_back(finalUSISlot);
				_finalUSI.BindResourceView(finalUSISlot, b);
				assert(finalUSISlot < 64);
				newBinding._usiSlotsFilled_ResourceViews |= 1ull << uint64_t(finalUSISlot);
			}
		}

		newBinding._samplerInterfaceToUSI.reserve(usi.GetSamplerBindings().size());
		for (auto b:usi.GetSamplerBindings()) {
			auto existing = std::find(_finalUSI.GetSamplerBindings().begin(), _finalUSI.GetSamplerBindings().end(), b);
			if (existing != _finalUSI.GetSamplerBindings().end()) {
				newBinding._samplerInterfaceToUSI.push_back(~0u);
			} else {
				auto finalUSISlot = (unsigned)_finalUSI.GetSamplerBindings().size();
				newBinding._samplerInterfaceToUSI.push_back(finalUSISlot);
				_finalUSI.BindSampler(finalUSISlot, b);
				assert(finalUSISlot < 64);
				newBinding._usiSlotsFilled_Samplers |= 1ull << uint64_t(finalUSISlot);
			}
		}

		newBinding._immediateDataInterfaceToUSI.reserve(usi.GetImmediateDataBindings().size());
		unsigned idx=0;
		for (auto b:usi.GetImmediateDataBindings()) {
			auto existing = std::find(_finalUSI.GetImmediateDataBindings().begin(), _finalUSI.GetImmediateDataBindings().end(), b);
			if (existing != _finalUSI.GetImmediateDataBindings().end()) {
				newBinding._immediateDataInterfaceToUSI.push_back(~0u);
				newBinding._immediateDataBeginAndEnd.push_back({});
			} else {
				auto finalUSISlot = (unsigned)_finalUSI.GetImmediateDataBindings().size();
				newBinding._immediateDataInterfaceToUSI.push_back(finalUSISlot);
				_finalUSI.BindImmediateData(finalUSISlot, b);
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

	void DelegateQueryHelper::QueryResources(ParsingContext& parsingContext, uint64_t resourcesToQuery, ShaderResourceDelegateBinding& del)
	{
		auto toLoad = resourcesToQuery & del._usiSlotsFilled_ResourceViews;
		if (!toLoad) return;

		uint64_t toLoadDelegate = 0;
		for (unsigned c=0; c<del._resourceInterfaceToUSI.size(); ++c)
			if (del._resourceInterfaceToUSI[c] != ~0u && (resourcesToQuery & (1 << uint64_t(del._resourceInterfaceToUSI[c]))))
				toLoadDelegate |= 1ull << uint64_t(c);

		assert(toLoadDelegate);
		auto minToCheck = xl_ctz8(toLoadDelegate);
		auto maxPlusOneToCheck = 64 - xl_clz8(toLoadDelegate);
		VLA(IResourceView*, rvDst, maxPlusOneToCheck);

		del._delegate->WriteResourceViews(parsingContext, nullptr, toLoadDelegate, MakeIteratorRange(rvDst, &rvDst[maxPlusOneToCheck]));
		parsingContext.RequireCommandList(del._delegate->_completionCmdList);
		
		for (unsigned c=minToCheck; c<maxPlusOneToCheck; ++c)
			if (del._resourceInterfaceToUSI[c] != ~0u && (resourcesToQuery & (1 << uint64_t(del._resourceInterfaceToUSI[c])))) {
				assert(rvDst[c]);
				_queriedResources[del._resourceInterfaceToUSI[c]] = rvDst[c];
			}

		_slotsQueried_ResourceViews |= toLoad;
	}

	void DelegateQueryHelper::QuerySamplers(ParsingContext& parsingContext, uint64_t samplersToQuery, ShaderResourceDelegateBinding& del)
	{
		auto toLoad = samplersToQuery & del._usiSlotsFilled_Samplers;
		if (!toLoad) return;

		uint64_t toLoadDelegate = 0;
		for (unsigned c=0; c<del._samplerInterfaceToUSI.size(); ++c)
			if (del._samplerInterfaceToUSI[c] != ~0u && (samplersToQuery & (1 << uint64_t(del._samplerInterfaceToUSI[c]))))
				toLoadDelegate |= 1ull << uint64_t(c);

		assert(toLoadDelegate);
		auto minToCheck = xl_ctz8(toLoadDelegate);
		auto maxPlusOneToCheck = 64 - xl_clz8(toLoadDelegate);
		VLA(ISampler*, samplerDst, maxPlusOneToCheck);

		del._delegate->WriteSamplers(parsingContext, nullptr, toLoadDelegate, MakeIteratorRange(&samplerDst[minToCheck], &samplerDst[maxPlusOneToCheck]));
		
		for (unsigned c=minToCheck; c<maxPlusOneToCheck; ++c)
			if (del._samplerInterfaceToUSI[c] != ~0u && (samplersToQuery & (1 << uint64_t(del._samplerInterfaceToUSI[c])))) {
				assert(samplerDst[c]);
				_queriedSamplers[del._samplerInterfaceToUSI[c]] = samplerDst[c];
			}

		_slotsQueried_Samplers |= toLoad;
	}

	void DelegateQueryHelper::QueryImmediateDatas(ParsingContext& parsingContext, uint64_t immediateDatasToQuery, ShaderResourceDelegateBinding& del)
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

	void DelegateQueryHelper::Prepare(IUniformBufferDelegate& del, uint64_t delBinding)
	{
		auto existing = std::find(_finalUSI.GetImmediateDataBindings().begin(), _finalUSI.GetImmediateDataBindings().end(), delBinding);
		if (existing != _finalUSI.GetImmediateDataBindings().end())
			return;
			
		UniformBufferDelegateBinding newBinding;
		newBinding._delegate = &del;
		newBinding._usiSlotFilled = (unsigned)_finalUSI.GetImmediateDataBindings().size();
		_finalUSI.BindImmediateData(newBinding._usiSlotFilled, delBinding);
		newBinding._size = del.GetSize();
		newBinding._tempBufferOffset = _workingTempBufferSize;
		_workingTempBufferSize += CeilToMultiplePow2(newBinding._size, s_immediateDataAlignment);

		_uBindings.push_back(newBinding);
	}

	void DelegateQueryHelper::QueryImmediateDatas(ParsingContext& parsingContext, uint64_t immediateDatasToQuery, UniformBufferDelegateBinding& del)
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

	void DelegateQueryHelper::QueryResources(ParsingContext& parsingContext, uint64_t resourcesToQuery)
	{
		resourcesToQuery &= ~_slotsQueried_ResourceViews;
		if (!resourcesToQuery) return;
		for (auto& del:_srBindings) QueryResources(parsingContext, resourcesToQuery, del);
	}

	void DelegateQueryHelper::QuerySamplers(ParsingContext& parsingContext, uint64_t samplersToQuery)
	{
		samplersToQuery &= ~_slotsQueried_Samplers;
		if (!samplersToQuery) return;
		for (auto& del:_srBindings) QuerySamplers(parsingContext, samplersToQuery, del);
	}

	void DelegateQueryHelper::QueryImmediateDatas(ParsingContext& parsingContext, uint64_t immediateDatasToQuery)
	{
		immediateDatasToQuery &= ~_slotsQueried_ImmediateDatas;
		if (!immediateDatasToQuery) return;
		for (auto& del:_srBindings) QueryImmediateDatas(parsingContext, immediateDatasToQuery, del);
		for (auto& del:_uBindings) QueryImmediateDatas(parsingContext, immediateDatasToQuery, del);
	}

	void DelegateQueryHelper::Prepare(
		ParsingContext& parsingContext,
		const UniformDelegateGroup& group)
	{
		// reset everything and rebuild all bindings
		_finalUSI.Reset();
		_slotsQueried_ResourceViews = _slotsQueried_Samplers = _slotsQueried_ImmediateDatas = 0ull;
		_workingTempBufferSize = 0;
		_srBindings.clear();
		_uBindings.clear();

		std::stack<const UniformDelegateGroup*> groupsToVisit;
		groupsToVisit.push(&group);
		while (!groupsToVisit.empty()) {
			auto& thisGroup = *groupsToVisit.top();
			groupsToVisit.pop();

			// Delegates we visit first will be preferred over subsequent delegates (if they bind the same thing)
			// So, we should go through in reverse order
			if (!thisGroup._shaderResourceDelegates.empty())
				for (signed c=thisGroup._shaderResourceDelegates.size()-1; c>=0; c--)
					Prepare(*thisGroup._shaderResourceDelegates[c], parsingContext);

			if (!thisGroup._uniformDelegates.empty())
				for (signed c=thisGroup._uniformDelegates.size()-1; c>=0; c--)
					Prepare(*thisGroup._uniformDelegates[c].second, thisGroup._uniformDelegates[c].first);

			// add "base" groups. The most overriding is the last group in the list. Since we're using a stack,
			// those will be pushed in last
			for (const auto&baseGroup:thisGroup._baseGroups)
				groupsToVisit.push(baseGroup.second.get());
		}

		_queriedResources.resize(_finalUSI.GetResourceViewBindings().size(), nullptr);
		std::fill(_queriedResources.begin(), _queriedResources.end(), nullptr);
		_queriedSamplers.resize(_finalUSI.GetSamplerBindings().size(), nullptr);
		std::fill(_queriedSamplers.begin(), _queriedSamplers.end(), nullptr);
		_queriedImmediateDatas.resize(_finalUSI.GetImmediateDataBindings().size(), {});
		std::fill(_queriedImmediateDatas.begin(), _queriedImmediateDatas.end(), UniformsStream::ImmediateData{});
		_tempDataBuffer.resize(_workingTempBufferSize, 0);
	}

	void DelegateQueryHelper::InvalidateUniforms()
	{
		_slotsQueried_ResourceViews = _slotsQueried_Samplers = _slotsQueried_ImmediateDatas = 0ull;
	}

	class SemiConstantDescriptorSet
	{
	public:
		IDescriptorSet* _currentDescriptorSet = nullptr;
		RenderCore::Assets::PredefinedDescriptorSetLayout _descSetLayout;
		SubFrameDescriptorSetHeap _heap;

		void RebuildDescriptorSet(
			ParsingContext& parsingContext,
			DelegateQueryHelper& delegateHelper);

		const IDescriptorSet* GetDescSet() const;

		SemiConstantDescriptorSet(
			IDevice& device,
			const RenderCore::Assets::PredefinedDescriptorSetLayout& layout,
			PipelineType pipelineType,
			CommonResourceBox& res);
		~SemiConstantDescriptorSet();
	};

	void SemiConstantDescriptorSet::RebuildDescriptorSet(
		ParsingContext& parsingContext,
		DelegateQueryHelper& delegateHelper)
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
		
		std::vector<DescriptorSetInitializer::BindTypeAndIdx> bindTypesAndIdx;
		bindTypesAndIdx.reserve(_descSetLayout._slots.size());
		uint64_t resourcesWeNeed = 0ull;
		uint64_t samplersWeNeed = 0ull;
		uint64_t immediateDatasWeNeed = 0ull;

		for (unsigned slotIdx=0; slotIdx<_descSetLayout._slots.size(); ++slotIdx) {
			auto hashName = Hash64(_descSetLayout._slots[slotIdx]._name);

			if (_descSetLayout._slots[slotIdx]._type == DescriptorType::Sampler) {
				auto samplerBinding = std::find(delegateHelper._finalUSI.GetSamplerBindings().begin(), delegateHelper._finalUSI.GetSamplerBindings().end(), hashName);
				if (samplerBinding != delegateHelper._finalUSI.GetSamplerBindings().end()) {
					auto samplerIdx = (unsigned)std::distance(delegateHelper._finalUSI.GetSamplerBindings().begin(), samplerBinding);
					bindTypesAndIdx.push_back({DescriptorSetInitializer::BindType::Sampler, samplerIdx});
					samplersWeNeed |= 1ull << uint64_t(samplerIdx);
					continue;
				}
				#if defined(_DEBUG)		// just check to make sure we're not attempting to bind some incorrect type here 
					auto resourceBinding = std::find(delegateHelper._finalUSI.GetResourceViewBindings().begin(), delegateHelper._finalUSI.GetResourceViewBindings().end(), hashName);
					if (resourceBinding != delegateHelper._finalUSI.GetResourceViewBindings().end())
						Log(Warning) << "Resource view provided for descriptor set slot (" << _descSetLayout._slots[slotIdx]._name << "), however, this lot is 'sampler' type in the descriptor set layout." << std::endl;
					auto immediateDataBinding = std::find(delegateHelper._finalUSI.GetImmediateDataBindings().begin(), delegateHelper._finalUSI.GetImmediateDataBindings().end(), hashName);
					if (immediateDataBinding != delegateHelper._finalUSI.GetImmediateDataBindings().end())
						Log(Warning) << "Immediate data provided for descriptor set slot (" << _descSetLayout._slots[slotIdx]._name << "), however, this lot is 'sampler' type in the descriptor set layout." << std::endl;
				#endif
			} else {
				auto resourceBinding = std::find(delegateHelper._finalUSI.GetResourceViewBindings().begin(), delegateHelper._finalUSI.GetResourceViewBindings().end(), hashName);
				if (resourceBinding != delegateHelper._finalUSI.GetResourceViewBindings().end()) {
					auto resourceIdx = (unsigned)std::distance(delegateHelper._finalUSI.GetResourceViewBindings().begin(), resourceBinding);
					bindTypesAndIdx.push_back({DescriptorSetInitializer::BindType::ResourceView, resourceIdx});
					resourcesWeNeed |= 1ull << uint64_t(resourceIdx);
					continue;
				}

				auto immediateDataBinding = std::find(delegateHelper._finalUSI.GetImmediateDataBindings().begin(), delegateHelper._finalUSI.GetImmediateDataBindings().end(), hashName);
				if (immediateDataBinding != delegateHelper._finalUSI.GetImmediateDataBindings().end()) {
					auto resourceIdx = (unsigned)std::distance(delegateHelper._finalUSI.GetImmediateDataBindings().begin(), immediateDataBinding);
					bindTypesAndIdx.push_back({DescriptorSetInitializer::BindType::ImmediateData, resourceIdx});
					immediateDatasWeNeed |= 1ull << uint64_t(resourceIdx);
					continue;
				}

				#if defined(_DEBUG)		// just check to make sure we're not attempting to bind some incorrect type here 
					auto samplerBinding = std::find(delegateHelper._finalUSI.GetSamplerBindings().begin(), delegateHelper._finalUSI.GetSamplerBindings().end(), hashName);
					if (samplerBinding != delegateHelper._finalUSI.GetSamplerBindings().end())
						Log(Warning) << "Sampler provided for descriptor set slot (" << _descSetLayout._slots[slotIdx]._name << "), however, this lot is not a sampler type in the descriptor set layout." << std::endl;
				#endif
			}

			bindTypesAndIdx.push_back({});		// didn't find any binding
		}

		// Now that we know what we need, we should query the delegates to get the associated data
		delegateHelper.QueryResources(parsingContext, resourcesWeNeed);
		delegateHelper.QuerySamplers(parsingContext, samplersWeNeed);
		delegateHelper.QueryImmediateDatas(parsingContext, immediateDatasWeNeed);

		DescriptorSetInitializer initializer;
		initializer._slotBindings = MakeIteratorRange(bindTypesAndIdx);
		initializer._bindItems._resourceViews = MakeIteratorRange(delegateHelper._queriedResources);
		initializer._bindItems._samplers = MakeIteratorRange(delegateHelper._queriedSamplers);
		initializer._bindItems._immediateData = MakeIteratorRange(delegateHelper._queriedImmediateDatas);

		// If useCmdListAttachedStorage is true, move the "ImmediateData" items into cmd list attached storage. The
		// alternative is attaching storage to the descriptor set itself; but this isn't ideal because it requires
		// allocating new resources
		const bool useCmdListAttachedStorage = true;
		VLA(const IResourceView*, newResourceViews, delegateHelper._queriedResources.size() + delegateHelper._queriedImmediateDatas.size());
		std::vector<std::shared_ptr<IResourceView>> tempResViews;		// subframe heap candidate
		tempResViews.resize(delegateHelper._queriedResources.size() + delegateHelper._queriedImmediateDatas.size());
		if (useCmdListAttachedStorage) {
			size_t immDataIterator = 0;
			const unsigned alignment = 0x100;
			for (auto& slot:bindTypesAndIdx) {
				if (slot._type != DescriptorSetInitializer::BindType::ImmediateData) continue;
				auto immData = initializer._bindItems._immediateData[slot._uniformsStreamIdx];
				immDataIterator = CeilToMultiplePow2(immDataIterator, alignment);
				immDataIterator += size_t(immData.end()) - size_t(immData.begin());
			}

			if (immDataIterator) {
				auto storage = Metal::DeviceContext::Get(parsingContext.GetThreadContext())->MapTemporaryStorage(immDataIterator, BindFlag::ConstantBuffer);

				unsigned newResourceViewCount = 0;
				for (auto& src:initializer._bindItems._resourceViews)
					newResourceViews[newResourceViewCount++] = src;

				auto resource = storage.GetResource();					
				auto beginAndEndInRes = storage.GetBeginAndEndInResource(); 
				immDataIterator = 0;
				for (auto& slot:bindTypesAndIdx) {
					if (slot._type != DescriptorSetInitializer::BindType::ImmediateData) continue;
					auto immData = initializer._bindItems._immediateData[slot._uniformsStreamIdx];

					immDataIterator = CeilToMultiplePow2(immDataIterator, alignment);
					auto size = size_t(immData.end()) - size_t(immData.begin());
					std::memcpy(PtrAdd(storage.GetData().begin(), immDataIterator), immData.begin(), size);
					
					// Creating a IResourceView here is a bit unfortunate -- on most APIs we should be fine with a resource pointer and size/offset
					tempResViews[newResourceViewCount] = resource->CreateBufferView(BindFlag::ConstantBuffer, immDataIterator + beginAndEndInRes.first, size);
					newResourceViews[newResourceViewCount] = tempResViews[newResourceViewCount].get();
					slot = { DescriptorSetInitializer::BindType::ResourceView, newResourceViewCount };
					++newResourceViewCount;
					immDataIterator += size;
				}

				initializer._bindItems._resourceViews = MakeIteratorRange(newResourceViews, &newResourceViews[newResourceViewCount]);
				initializer._bindItems._immediateData = {};
			}
		}

		_currentDescriptorSet = _heap.Allocate();
		_currentDescriptorSet->Write(initializer);
	}

	const IDescriptorSet* SemiConstantDescriptorSet::GetDescSet() const
	{
		assert(_currentDescriptorSet);
		return _currentDescriptorSet;
	}

	SemiConstantDescriptorSet::SemiConstantDescriptorSet(
		IDevice& device,
		const RenderCore::Assets::PredefinedDescriptorSetLayout& layout,
		PipelineType pipelineType,
		CommonResourceBox& res)
	: _descSetLayout(layout)
	, _heap(device, layout.MakeDescriptorSetSignature(&res._samplerPool), pipelineType)
	{}

	SemiConstantDescriptorSet::~SemiConstantDescriptorSet() {}

	class UniformDelegateManager : public IUniformDelegateManager
	{
	public:
		DelegateQueryHelper _delegateHelper;
		std::shared_ptr<UniformDelegateGroup> _delegateGroup;
		UniformDelegateGroup::ChangeIndex _lastPreparedChangeIndex = -1;

		struct PipelineBindings
		{
			std::vector<std::pair<uint64_t, std::shared_ptr<SemiConstantDescriptorSet>>> _semiConstantDescSets;
			std::vector<const IDescriptorSet*> _descSetsForBinding;
			bool _pendingRebuildDescSets = true;
		};
		PipelineBindings _graphics;
		PipelineBindings _compute;
		
		UniformsStreamInterface _interface;

		////////////////////////////////////////////////////////////////////////////////////
		void AddShaderResourceDelegate(const std::shared_ptr<IShaderResourceDelegate>&) override;
		void RemoveShaderResourceDelegate(IShaderResourceDelegate&) override;
		
		void AddUniformDelegate(uint64_t binding, const std::shared_ptr<IUniformBufferDelegate>&) override;
		void RemoveUniformDelegate(IUniformBufferDelegate&) override;

		void AddSemiConstantDescriptorSet(
			uint64_t binding, const RenderCore::Assets::PredefinedDescriptorSetLayout&,
			IDevice& device) override;
		void RemoveSemiConstantDescriptorSet(uint64_t binding) override;

		void AddBase(const std::shared_ptr<IUniformDelegateManager>&) override;
		void RemoveBase(IUniformDelegateManager&) override;

		void BringUpToDateGraphics(ParsingContext& parsingContext) override;
		void BringUpToDateCompute(ParsingContext& parsingContext) override;
		const UniformsStreamInterface& GetInterface() override;
		void InvalidateUniforms() override;

		UniformDelegateManager();
	};

	void UniformDelegateManager::BringUpToDateGraphics(ParsingContext& parsingContext)
	{
		bool pendingReprepare = _delegateGroup->_currentChangeIndex != _lastPreparedChangeIndex;
		for (auto&base:_delegateGroup->_baseGroups)
			pendingReprepare |= base.first != base.second->_currentChangeIndex;

		if (pendingReprepare) {
			_delegateHelper.Prepare(parsingContext, *_delegateGroup);

			_lastPreparedChangeIndex = _delegateGroup->_currentChangeIndex;
			for (auto&base:_delegateGroup->_baseGroups)
				base.first = base.second->_currentChangeIndex;

			_interface = _delegateHelper._finalUSI;
			for (unsigned c=0; c<_graphics._semiConstantDescSets.size(); ++c)
				_interface.BindFixedDescriptorSet(c, _graphics._semiConstantDescSets[c].first);

			_graphics._pendingRebuildDescSets = true;
			_compute._pendingRebuildDescSets = true;
		}

		PipelineBindings* bindings[] { &_graphics, &_compute };
		for (auto&b:bindings) {
			if (b->_pendingRebuildDescSets) {
				for (auto& descSet:b->_semiConstantDescSets)
					descSet.second->RebuildDescriptorSet(parsingContext, _delegateHelper);
				b->_pendingRebuildDescSets = false;
			}

			b->_descSetsForBinding.resize(b->_semiConstantDescSets.size());
			for (unsigned c=0; c<b->_semiConstantDescSets.size(); ++c)
				b->_descSetsForBinding[c] = b->_semiConstantDescSets[c].second->GetDescSet();
		}
	}

	void UniformDelegateManager::BringUpToDateCompute(ParsingContext& parsingContext)
	{
	}

	const UniformsStreamInterface& UniformDelegateManager::GetInterface()
	{
		return _interface;
	}

	void UniformDelegateManager::InvalidateUniforms()
	{
		_delegateHelper.InvalidateUniforms();
		_graphics._pendingRebuildDescSets = true;
		_compute._pendingRebuildDescSets = true;
	}

	void UniformDelegateManager::AddShaderResourceDelegate(const std::shared_ptr<IShaderResourceDelegate>& delegate)
	{
		_delegateGroup->AddShaderResourceDelegate(delegate);
	}

	void UniformDelegateManager::RemoveShaderResourceDelegate(IShaderResourceDelegate& delegate)
	{
		_delegateGroup->RemoveShaderResourceDelegate(delegate);
	}
	
	void UniformDelegateManager::AddUniformDelegate(uint64_t binding, const std::shared_ptr<IUniformBufferDelegate>& delegate)
	{
		_delegateGroup->AddUniformDelegate(binding, delegate);
	}

	void UniformDelegateManager::RemoveUniformDelegate(IUniformBufferDelegate& delegate)
	{
		_delegateGroup->RemoveUniformDelegate(delegate);
	}

	void UniformDelegateManager::AddBase(const std::shared_ptr<IUniformDelegateManager>& iman)
	{
		auto& man = *checked_cast<UniformDelegateManager*>(iman.get());
		_delegateGroup->AddBase(man._delegateGroup);
	}

	void UniformDelegateManager::RemoveBase(IUniformDelegateManager& iman)
	{
		auto& man = *checked_cast<UniformDelegateManager*>(&iman);
		_delegateGroup->RemoveBase(*man._delegateGroup);
	}

	void UniformDelegateManager::AddSemiConstantDescriptorSet(
		uint64_t binding, const RenderCore::Assets::PredefinedDescriptorSetLayout& layout,
		IDevice& device)
	{
		auto& resBox = *Services::GetCommonResources();
		#if defined(_DEBUG)
			auto i = std::find_if(_graphics._semiConstantDescSets.begin(), _graphics._semiConstantDescSets.end(),
				[binding](const auto& c) { return c.first == binding; });
			assert(i == _graphics._semiConstantDescSets.end());
			i = std::find_if(_compute._semiConstantDescSets.begin(), _compute._semiConstantDescSets.end(),
				[binding](const auto& c) { return c.first == binding; });
			assert(i == _compute._semiConstantDescSets.end());
		#endif

		_graphics._semiConstantDescSets.emplace_back(binding, std::make_shared<SemiConstantDescriptorSet>(device, layout, PipelineType::Graphics, resBox));
		_compute._semiConstantDescSets.emplace_back(binding, std::make_shared<SemiConstantDescriptorSet>(device, layout, PipelineType::Compute, resBox));
	}

	void UniformDelegateManager::RemoveSemiConstantDescriptorSet(uint64_t binding)
	{
		auto i = std::find_if(_graphics._semiConstantDescSets.begin(), _graphics._semiConstantDescSets.end(),
			[binding](const auto& c) { return c.first == binding; });
		if (i != _graphics._semiConstantDescSets.end())
			_graphics._semiConstantDescSets.erase(i);

		i = std::find_if(_compute._semiConstantDescSets.begin(), _compute._semiConstantDescSets.end(),
			[binding](const auto& c) { return c.first == binding; });
		if (i != _compute._semiConstantDescSets.end())
			_compute._semiConstantDescSets.erase(i);
	}

	UniformDelegateManager::UniformDelegateManager()
	{
		_delegateGroup = std::make_shared<UniformDelegateGroup>();
	}

	std::shared_ptr<IUniformDelegateManager> CreateUniformDelegateManager()
	{
		return std::make_shared<UniformDelegateManager>();
	}

	void ApplyUniformsGraphics(
		IUniformDelegateManager& delManager,
		Metal::DeviceContext& metalContext,
		Metal::SharedEncoder& encoder,
		ParsingContext& parsingContext,
		Metal::BoundUniforms& boundUniforms,
		unsigned groupIdx)
	{
		auto& man = *checked_cast<UniformDelegateManager*>(&delManager);
		assert(man._lastPreparedChangeIndex == man._delegateGroup->_currentChangeIndex);
		assert(!man._graphics._pendingRebuildDescSets);
		if (__builtin_expect(!man._graphics._descSetsForBinding.empty(), true))
			boundUniforms.ApplyDescriptorSets(metalContext, encoder, man._graphics._descSetsForBinding, groupIdx);

		if (__builtin_expect(boundUniforms.GetBoundLooseImmediateDatas(0) | boundUniforms.GetBoundLooseResources(0) | boundUniforms.GetBoundLooseResources(0), 0ull)) {
			man._delegateHelper.QueryResources(parsingContext, boundUniforms.GetBoundLooseResources(groupIdx));
			man._delegateHelper.QuerySamplers(parsingContext, boundUniforms.GetBoundLooseSamplers(groupIdx));
			man._delegateHelper.QueryImmediateDatas(parsingContext, boundUniforms.GetBoundLooseImmediateDatas(groupIdx));
			UniformsStream us {
				man._delegateHelper._queriedResources,
				man._delegateHelper._queriedImmediateDatas,
				man._delegateHelper._queriedSamplers };
			boundUniforms.ApplyLooseUniforms(metalContext, encoder, us, groupIdx);
		}		
	}

	void ApplyUniformsCompute(
		IUniformDelegateManager& delManager,
		Metal::DeviceContext& metalContext,
		Metal::SharedEncoder& encoder,
		ParsingContext& parsingContext,
		Metal::BoundUniforms& boundUniforms,
		unsigned groupIdx)
	{
		auto& man = *checked_cast<UniformDelegateManager*>(&delManager);
		assert(man._lastPreparedChangeIndex == man._delegateGroup->_currentChangeIndex);
		assert(!man._compute._pendingRebuildDescSets);
		if (__builtin_expect(!man._compute._descSetsForBinding.empty(), true))
			boundUniforms.ApplyDescriptorSets(metalContext, encoder, man._compute._descSetsForBinding, groupIdx);

		if (__builtin_expect(boundUniforms.GetBoundLooseImmediateDatas(0) | boundUniforms.GetBoundLooseResources(0) | boundUniforms.GetBoundLooseResources(0), 0ull)) {
			man._delegateHelper.QueryResources(parsingContext, boundUniforms.GetBoundLooseResources(groupIdx));
			man._delegateHelper.QuerySamplers(parsingContext, boundUniforms.GetBoundLooseSamplers(groupIdx));
			man._delegateHelper.QueryImmediateDatas(parsingContext, boundUniforms.GetBoundLooseImmediateDatas(groupIdx));
			UniformsStream us {
				man._delegateHelper._queriedResources,
				man._delegateHelper._queriedImmediateDatas,
				man._delegateHelper._queriedSamplers };
			boundUniforms.ApplyLooseUniforms(metalContext, encoder, us, groupIdx);
		}		
	}

}}

