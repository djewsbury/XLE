// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../IDevice.h"
#include "../Vulkan/IDeviceVulkan.h"
#include "../Metal/ObjectFactory.h"
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
}}

