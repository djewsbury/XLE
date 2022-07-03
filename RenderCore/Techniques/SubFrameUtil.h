// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../IDevice.h"
#include "../Vulkan/IDeviceVulkan.h"
#include "../Vulkan/Metal/ObjectFactory.h"		// for Metal_Vulkan::IAsyncTracker
#include "../../Utility/HeapUtils.h"
#include <vector>
#include <memory>

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
					assert(item<PageSize);
					page->_freeItems.pop_front();
					page->_allocatedItems.try_emplace_back(producerMarker, item);
					return PageSize*std::distance(_pages.begin(), page) + item;
				}
			}

			_pages.push_back({});
			auto& page = *(_pages.end()-1);
			auto item = page._freeItems.front();
			assert(item<PageSize);
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

		GPUTrackerHeap() = default;
		GPUTrackerHeap(GPUTrackerHeap&&) = default;
		GPUTrackerHeap& operator=(GPUTrackerHeap&&) = default;
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

	/// <summary>Maintains a small heap of descriptor sets with the same layout, each of which will be used for no more than one frame</summary>
	/// Don't attempt to use the returned descriptor set after the current cmd list has been completed. There are no protections for this,
	/// but the descriptor set may be rewritten
	class SubFrameDescriptorSetHeap
	{
	public:
		IDescriptorSet* Allocate();
		const DescriptorSetSignature& GetSignature() const { return _signature; }
		PipelineType GetPipelineType() const { return _pipelineType; }

		SubFrameDescriptorSetHeap(
			IDevice& device,
			const DescriptorSetSignature& signature,
			PipelineType pipelineType);
		SubFrameDescriptorSetHeap();
		~SubFrameDescriptorSetHeap();
		SubFrameDescriptorSetHeap(SubFrameDescriptorSetHeap&&);
		SubFrameDescriptorSetHeap& operator=(SubFrameDescriptorSetHeap&&);

	private:
		static constexpr unsigned s_poolPageSize = 8;
		GPUTrackerHeap<s_poolPageSize> _trackerHeap;
		std::vector<std::shared_ptr<IDescriptorSet>> _descriptorSetPool;

		DescriptorSetSignature _signature;
		PipelineType _pipelineType;
		IDevice* _device = nullptr;
	};

	// Writes new values to a descriptor set, but uses cmd list bound storage for all "immediate" initializers
	// This means that the descriptor set will only be valid for use with the current cmd list, and shouldn't be used on
	// other command lists. But there's no explicit validation for this.
	// Also note that updating descriptor sets happens immediately -- not synchronized with the commands in the cmd list
	// Think of it as being like a ResourceMap
	void WriteWithSubframeImmediates(IThreadContext&, IDescriptorSet&, const DescriptorSetInitializer&);
}}

