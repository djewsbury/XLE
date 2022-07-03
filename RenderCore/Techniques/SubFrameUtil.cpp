// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SubFrameUtil.h"
#include "../Metal/DeviceContext.h"
#include "../../Utility/BitUtils.h"

namespace RenderCore { namespace Techniques
{

	IDescriptorSet* SubFrameDescriptorSetHeap::Allocate()
	{
		auto nextItem = _trackerHeap.GetNextFreeItem();
		if (nextItem >= _descriptorSetPool.size()) {
			// _trackerHeap allocated a new page -- we need to resize the pool of descriptor sets
			auto initialSize = _descriptorSetPool.size();
			auto newPageCount = 1+nextItem/s_poolPageSize;
			_descriptorSetPool.resize(newPageCount*s_poolPageSize);
			DescriptorSetInitializer creationInitializer;
			creationInitializer._signature = &_signature;
			creationInitializer._pipelineType = _pipelineType;
			for (auto c=initialSize; c<_descriptorSetPool.size(); ++c)
				_descriptorSetPool[c] = _device->CreateDescriptorSet(creationInitializer);
		}
		return _descriptorSetPool[nextItem].get();
	}

	SubFrameDescriptorSetHeap::SubFrameDescriptorSetHeap(
		IDevice& device,
		const DescriptorSetSignature& signature,
		PipelineType pipelineType)
	: _trackerHeap(device)
	, _pipelineType(pipelineType)
	, _signature(signature)
	, _device(&device)
	{
		DescriptorSetInitializer initializer;
		initializer._signature = &_signature;
		initializer._pipelineType = _pipelineType;
		_descriptorSetPool.reserve(s_poolPageSize);
		for (unsigned c=0; c<s_poolPageSize; ++c)
			_descriptorSetPool.push_back(device.CreateDescriptorSet(initializer));
	}

	SubFrameDescriptorSetHeap::SubFrameDescriptorSetHeap() = default;
	SubFrameDescriptorSetHeap::~SubFrameDescriptorSetHeap() = default;
	SubFrameDescriptorSetHeap::SubFrameDescriptorSetHeap(SubFrameDescriptorSetHeap&&) = default;
	SubFrameDescriptorSetHeap& SubFrameDescriptorSetHeap::operator=(SubFrameDescriptorSetHeap&&) = default;


	void WriteWithSubframeImmediates(IThreadContext& threadContext, IDescriptorSet& descriptorSet, const DescriptorSetInitializer& initializer)
	{
		if (initializer._bindItems._immediateData.empty()) {
			descriptorSet.Write(initializer);
			return;
		}

		DescriptorSetInitializer newInitializer = initializer;
		const IResourceView* newResourceViews[initializer._bindItems._immediateData.size() + initializer._bindItems._resourceViews.size()];
		std::shared_ptr<IResourceView> temporaryResourceViews[initializer._bindItems._immediateData.size() + initializer._bindItems._resourceViews.size()];
		unsigned rv = 0;
		for (auto v:newInitializer._bindItems._resourceViews) newResourceViews[rv++] = v;

		size_t immDataTotal = 0;
		const unsigned cbAlignmentRules = 64;		// todo -- get from device
		for (auto imm:newInitializer._bindItems._immediateData) immDataTotal += CeilToMultiple(imm.size(), cbAlignmentRules);
		assert(immDataTotal);

		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		// we don't actually know the correct bind flags here... We'll have to assume it's uniforms for now
		// though, we could lookup which slot it's bound to and try to use the DescriptorType to figure it out
		auto mappedData = metalContext.MapTemporaryStorage(immDataTotal, BindFlag::ConstantBuffer);
		immDataTotal = 0;
		for (auto imm:newInitializer._bindItems._immediateData) {
			std::memcpy(PtrAdd(mappedData.GetData().begin(), immDataTotal), imm.begin(), imm.size());
			temporaryResourceViews[rv] = mappedData.AsResourceView(immDataTotal, immDataTotal+imm.size());
			newResourceViews[rv] = temporaryResourceViews[rv].get();
			++rv;
			immDataTotal += CeilToMultiple(imm.size(), cbAlignmentRules);
		}

		// update _slotBindings to change from immediate data reference to resource view reference
		DescriptorSetInitializer::BindTypeAndIdx newBindings[initializer._slotBindings.size()];
		for (unsigned c=0; c<initializer._slotBindings.size(); ++c) {
			newBindings[c] = initializer._slotBindings[c];
			if (newBindings[c]._type == DescriptorSetInitializer::BindType::ImmediateData) {
				newBindings[c]._type = DescriptorSetInitializer::BindType::ResourceView;
				newBindings[c]._uniformsStreamIdx += initializer._bindItems._resourceViews.size();
			}
		}

		newInitializer._bindItems._resourceViews = MakeIteratorRange(newResourceViews, &newResourceViews[initializer._bindItems._immediateData.size() + initializer._bindItems._resourceViews.size()]);
		newInitializer._bindItems._immediateData = {};
		newInitializer._slotBindings = MakeIteratorRange(newBindings, &newBindings[initializer._slotBindings.size()]);

		descriptorSet.Write(newInitializer);
	}

}}

