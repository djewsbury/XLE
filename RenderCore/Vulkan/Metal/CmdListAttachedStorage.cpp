// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CmdListAttachedStorage.h"
#include "ObjectFactory.h"
#include "DeviceContext.h"
#include "TextureView.h"
#include "IncludeVulkan.h"
#include "../../BufferView.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/HeapUtils.h"
#include "../../../Utility/BitUtils.h"
#include "../../../Utility/Threading/Mutex.h"
#include <thread>

namespace RenderCore { namespace Metal_Vulkan
{
	static unsigned LeastCommonMultiple(unsigned a, unsigned b)
	{
		// The least common multiple is product/greatest-common-divisor
		// we can calculate greatest common divisor using Euclid's method
		auto rm2 = a, rm1 = b;		// doesn't matter if a or b is smaller (though will complete 1 iteration quicker if a is the larger)
		for (;;) {
			auto r0 = rm2%rm1;
			if (!r0) break;
			rm2=rm1;
			rm1=r0;
		}
		auto gcd = rm1;
		return a / gcd * b;
	}

	struct MarkedDestroys
	{
		IAsyncTracker::Marker	_marker;
		unsigned				_front;
	};

	class TemporaryStoragePage
	{
	public:
		BindFlag::BitField 	_type;
		bool 			_cpuMappable = false;
		std::shared_ptr<Resource> _resource;
		CircularHeap	_heap;
		unsigned		_pageId = ~0u;

		unsigned		_pendingNewFront = ~0u;
		ResizableCircularBuffer<MarkedDestroys, 32>	_markedDestroys;		// Note! we should only use _markedDestroys on the manager bound thread!

		unsigned		_lastBarrier;
		DeviceContext*	_lastBarrierContext;

		unsigned		_alignment;

		TemporaryStoragePage(ObjectFactory& factory, size_t size, BindFlag::BitField type, bool cpuMappable, unsigned pageId);
		~TemporaryStoragePage();
		TemporaryStoragePage(TemporaryStoragePage&&) = delete;
		TemporaryStoragePage& operator=(TemporaryStoragePage&&) = delete;
	};

	class TemporaryStorageManager::Pimpl
	{
	public:
		ObjectFactory* _factory;
		std::shared_ptr<IAsyncTracker> _gpuTracker;
		Threading::Mutex _lock;
		std::vector<std::unique_ptr<TemporaryStoragePage>> _pages;
		std::vector<std::unique_ptr<TemporaryStoragePage>> _namedPages;
		std::vector<std::unique_ptr<TemporaryStoragePage>> _oversizedAllocations;
		BitHeap _pageReservations, _namedPageReservations;
		unsigned _nextPageId = 1u;
		std::atomic<unsigned> _cmdListAttachedStorageAlive = 0;
		std::thread::id _boundThreadId;

		Pimpl(ObjectFactory& factory, std::shared_ptr<IAsyncTracker> gpuTracker);
		~Pimpl();

		std::pair<TemporaryStoragePage*, unsigned> ReserveNewPageForAllocation(size_t byteCode, size_t alignment, BindFlag::BitField bindFlags, bool cpuMapping, size_t pageSize);
		TemporaryStoragePage* ReserveNamedPage(NamedPage namedPage);
		void ReleaseReservationAlreadyLocked(TemporaryStoragePage& page);
		void FlushDestroys();
	};

	std::pair<TemporaryStoragePage*, unsigned> TemporaryStorageManager::Pimpl::ReserveNewPageForAllocation(size_t byteCount, size_t alignment, BindFlag::BitField bindFlags, bool cpuMapping, size_t pageSize)
	{
		assert(byteCount != 0);
		assert(bindFlags != 0);
		assert(pageSize != 0);
		assert(alignment != 0);
		// Find a page with at least the given amount of free space (hopefully significantly more) and the 
		// given binding type
		ScopedLock(_lock);
		auto i = _pageReservations.FirstUnallocated();
		for (; i!=_pages.size(); ++i) {
			auto& page = *_pages[i];
			if (page._type != bindFlags) continue;
			if (_pageReservations.IsAllocated(i)) continue;

			alignment = LeastCommonMultiple((unsigned)alignment, page._alignment);
			auto space = page._heap.AllocateBack(byteCount, alignment);
			if (space != ~0u) {
				assert((space%alignment) == 0);
				_pageReservations.Allocate(i);
				page._pendingNewFront = space + byteCount;
				return std::make_pair(&page, space);
			}
		}

		if (byteCount <= pageSize) {
			// pageSize = std::max(1u<<(IntegerLog2(byteCount+byteCount/2)+1), (unsigned)pageSize);
			_pages.emplace_back(std::make_unique<TemporaryStoragePage>(*_factory, pageSize, bindFlags, cpuMapping, _nextPageId++));
			_pageReservations.Allocate(_pages.size()-1);

			auto& page = *_pages[_pages.size()-1];
			alignment = LeastCommonMultiple((unsigned)alignment, page._alignment);
			auto space = page._heap.AllocateBack(byteCount, alignment);
			assert(space != ~0u);
			assert((space%alignment) == 0);
			page._pendingNewFront = space + byteCount;
			return std::make_pair(&page, space);
		} else {
			// Oversized allocation.. we will allocate from the main heap and attempt to return it as soon as possible
			_oversizedAllocations.emplace_back(std::make_unique<TemporaryStoragePage>(*_factory, byteCount, bindFlags, cpuMapping, _nextPageId++));

			auto& page = *_oversizedAllocations[_oversizedAllocations.size()-1];
			auto space = page._heap.AllocateBack(byteCount);
			assert(space != ~0u);
			page._pendingNewFront = byteCount;
			return std::make_pair(&page, space);
		}
	}

	TemporaryStoragePage* TemporaryStorageManager::Pimpl::ReserveNamedPage(NamedPage namedPage)
	{
		ScopedLock(_lock);
		assert(namedPage < _namedPages.size());
		assert(!_namedPageReservations.IsAllocated(namedPage));
		_namedPageReservations.Allocate(namedPage);
		return _namedPages[namedPage].get();
	}

	void TemporaryStorageManager::Pimpl::ReleaseReservationAlreadyLocked(TemporaryStoragePage& page)
	{
		for (auto i = 0u; i!=_pages.size(); ++i) {
			if (_pages[i].get() != &page) continue;
			assert(_pageReservations.IsAllocated(i));
			_pageReservations.Deallocate(i);
			return;
		}

		for (auto i = 0u; i!=_namedPages.size(); ++i) {
			if (_namedPages[i].get() != &page) continue;
			assert(_namedPageReservations.IsAllocated(i));
			_namedPageReservations.Deallocate(i);
			return;
		}

		for (auto i = 0u; i!=_oversizedAllocations.size(); ++i)
			if (_oversizedAllocations[i].get() == &page)
				return;

		assert(0);	// not found in this manager
	}

	void TemporaryStorageManager::Pimpl::FlushDestroys()
	{
		ScopedLock(_lock);
		assert(std::this_thread::get_id() == _boundThreadId);
		for (auto i = 0u; i!=_pages.size(); ++i) {
			auto& page = *_pages[i];

			auto trackerMarker = _gpuTracker ? _gpuTracker->GetConsumerMarker() : ~0u;
			unsigned newFront = ~0u;
			while (!page._markedDestroys.empty() && page._markedDestroys.front()._marker <= trackerMarker) {
				newFront = page._markedDestroys.front()._front;
				page._markedDestroys.pop_front();
			}

			if (newFront != ~0u)
				page._heap.ResetFront(newFront);
		}

		for (auto i = 0u; i!=_namedPages.size(); ++i) {
			auto& page = *_namedPages[i];

			auto trackerMarker = _gpuTracker ? _gpuTracker->GetConsumerMarker() : ~0u;
			unsigned newFront = ~0u;
			while (!page._markedDestroys.empty() && page._markedDestroys.front()._marker <= trackerMarker) {
				newFront = page._markedDestroys.front()._front;
				page._markedDestroys.pop_front();
			}

			if (newFront != ~0u)
				page._heap.ResetFront(newFront);
		}

		for (auto i = _oversizedAllocations.begin(); i!=_oversizedAllocations.end();) {
			auto& page = **i;
			assert(!page._markedDestroys.empty());
			auto status = _gpuTracker->GetSpecificMarkerStatus(page._markedDestroys.front()._marker);
			if (status == IAsyncTracker::MarkerStatus::ConsumerCompleted || status == IAsyncTracker::MarkerStatus::Abandoned) {
				page._markedDestroys.pop_front();
				assert(page._markedDestroys.empty());
				// we use AllocationRules::DisableSafeDestruction, so the GPU memory should be immediately freed when we do this
				i = _oversizedAllocations.erase(i);
			} else 
				++i;
		}
	}

	TemporaryStorageManager::Pimpl::Pimpl(ObjectFactory& factory, std::shared_ptr<IAsyncTracker> gpuTracker)
	: _factory(&factory), _gpuTracker(gpuTracker)
	{
		_boundThreadId = std::this_thread::get_id();
	}

	TemporaryStorageManager::Pimpl::~Pimpl()
	{
		assert(_cmdListAttachedStorageAlive.load() == 0);
	}

	CmdListAttachedStorage TemporaryStorageManager::BeginCmdListReservation()
	{
		return CmdListAttachedStorage { _pimpl.get() };
	}

	NamedPage TemporaryStorageManager::CreateNamedPage(size_t byteCount, BindFlag::BitField bindFlags)
	{
		auto result = (NamedPage)_pimpl->_namedPages.size();
		_pimpl->_namedPages.emplace_back(std::make_unique<TemporaryStoragePage>(*_pimpl->_factory, byteCount, bindFlags, true, _pimpl->_nextPageId++));
		return result;
	}

	std::shared_ptr<IResource> TemporaryStorageManager::GetResourceForNamedPage(NamedPage namedPage)
	{
		assert(namedPage < _pimpl->_namedPages.size());
		return _pimpl->_namedPages[namedPage]->_resource;
	}

	void TemporaryStorageManager::FlushDestroys()
	{
		_pimpl->FlushDestroys();
	}

	TemporaryStorageManager::TemporaryStorageManager(
		ObjectFactory& factory,
		const std::shared_ptr<IAsyncTracker>& asyncTracker) 
	{
		_pimpl = std::make_unique<Pimpl>(factory, asyncTracker);
	}
	
	TemporaryStorageManager::~TemporaryStorageManager() {}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static ResourceDesc BuildBufferDesc(BindFlag::BitField bindingFlags, size_t byteCount, bool cpuMappable)
	{
		return CreateDesc(
			bindingFlags,
			AllocationRules::DedicatedPage | AllocationRules::DisableSafeDestruction | (cpuMappable ? AllocationRules::HostVisibleSequentialWrite|AllocationRules::PermanentlyMapped|AllocationRules::DisableAutoCacheCoherency : 0),
			LinearBufferDesc::Create(unsigned(byteCount)));
	}

	TemporaryStoragePage::TemporaryStoragePage(ObjectFactory& factory, size_t byteCount, BindFlag::BitField type, bool cpuMappable, unsigned pageId)
	: _resource(std::make_shared<Resource>(factory, BuildBufferDesc(type, byteCount, cpuMappable), "RollingTempBuf"))
	, _heap((unsigned)byteCount), _type(type), _cpuMappable(cpuMappable), _pageId(pageId)
	{
		_lastBarrier = 0u;
		_lastBarrierContext = nullptr;

		_alignment = 1;
		if (type & BindFlag::ConstantBuffer)
			_alignment = LeastCommonMultiple(_alignment, (unsigned)factory.GetPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment);
		
		if (type & BindFlag::UnorderedAccess)
			_alignment = LeastCommonMultiple(_alignment, (unsigned)factory.GetPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment);
		
		if (type & BindFlag::ShaderResource)
			_alignment = LeastCommonMultiple(_alignment, (unsigned)factory.GetPhysicalDeviceProperties().limits.minTexelBufferOffsetAlignment);
	}

	TemporaryStoragePage::~TemporaryStoragePage() {}

	static unsigned GetPageSize(BindFlag::BitField type)
	{
		if (type & BindFlag::ConstantBuffer) return 256 * 1024;
		if (type & BindFlag::VertexBuffer) return 8 * 1024 * 1024;		// need a fair amount of room for deform accelerators
		if (type & BindFlag::IndexBuffer) return 256 * 1024;
		if (type & BindFlag::ShaderResource) return 256 * 1024;
		if (type & BindFlag::TransferSrc) return 8 * 1024 * 1024;
		return 256 * 1024;
	}

	static unsigned AllocateSpaceFromPage(TemporaryStoragePage& page, size_t byteCount, size_t alignment)
	{
		alignment = LeastCommonMultiple((unsigned)alignment, page._alignment);
		auto space = page._heap.AllocateBack((unsigned)byteCount, alignment);
		if (space != ~0u) {
			assert((space % alignment) == 0);
			page._pendingNewFront = space + (unsigned)byteCount;

			// Check if we've crossed over the "last barrier" point (no special
			// handling for wrap around case required)
			if (space <= page._lastBarrier && space > page._lastBarrier)
				page._lastBarrierContext = nullptr;	// reset tracking
		}
		return space;
	}

	TemporaryStorageResourceMap CmdListAttachedStorage::MapStorage(size_t byteCount, BindFlag::BitField bindFlags, size_t alignment)
	{
		assert(byteCount != 0);
		assert(alignment != 0);
		const bool cpuMappable = true;
		for (auto page=_reservedPages.rbegin(); page!=_reservedPages.rend(); ++page) {
			if ((*page)->_type != bindFlags || (*page)->_cpuMappable != cpuMappable) continue;

			auto space = AllocateSpaceFromPage(**page, byteCount, alignment);
			if (space != ~0u)
				return TemporaryStorageResourceMap {
					*_manager->_factory,
					(*page)->_resource, space, byteCount, (*page)->_pageId };
		}

		auto newPageAndAllocation = _manager->ReserveNewPageForAllocation(byteCount, alignment, bindFlags, cpuMappable, GetPageSize(bindFlags));
		_reservedPages.push_back(newPageAndAllocation.first);
		return TemporaryStorageResourceMap {
			*_manager->_factory, 
			newPageAndAllocation.first->_resource, newPageAndAllocation.second, byteCount, newPageAndAllocation.first->_pageId };
	}

	BufferAndRange CmdListAttachedStorage::AllocateDeviceOnlyRange(size_t byteCount, BindFlag::BitField bindFlags, size_t alignment)
	{
		assert(byteCount != 0);
		assert(alignment != 0);
		const bool cpuMappable = false;
		for (auto page=_reservedPages.rbegin(); page!=_reservedPages.rend(); ++page) {
			if ((*page)->_type != bindFlags || (*page)->_cpuMappable != cpuMappable) continue;

			auto space = AllocateSpaceFromPage(**page, byteCount, alignment);
			if (space != ~0u)
				return BufferAndRange { (*page)->_resource, space, (unsigned)byteCount };
		}

		auto newPageAndAllocation = _manager->ReserveNewPageForAllocation(byteCount, alignment, bindFlags, cpuMappable, GetPageSize(bindFlags));
		_reservedPages.push_back(newPageAndAllocation.first);
		return BufferAndRange { newPageAndAllocation.first->_resource, newPageAndAllocation.second, (unsigned)byteCount };
	}

	TemporaryStorageResourceMap	CmdListAttachedStorage::MapStorageFromNamedPage(size_t byteCount, NamedPage namedPage, size_t alignment)
	{
		if (namedPage >= _namedPageReservations.size())
			_namedPageReservations.resize(namedPage+1, nullptr);

		if (!_namedPageReservations[namedPage])
			_namedPageReservations[namedPage] = _manager->ReserveNamedPage(namedPage);

		auto& page = *_namedPageReservations[namedPage];
		assert(page._cpuMappable);

		auto space = AllocateSpaceFromPage(page, byteCount, alignment);
		if (space != ~0u)
			return TemporaryStorageResourceMap {
				*_manager->_factory,
				page._resource, space, byteCount, page._pageId };

		assert(0);
		return {};
	}

	void CmdListAttachedStorage::OnSubmitToQueue(unsigned trackerMarker)
	{
		if (!_manager || (_reservedPages.empty() && _namedPageReservations.empty())) return;

		// lock the manager, because any pages' _markedDestroys can be synchronously
		// accessed in TemporaryStorageManager::Pimpl::FlushDestroys
		ScopedLock(_manager->_lock);

		// There's no actual thread protection for "_reservedPages" and "_pendingNewFront" here
		// We're assuming that since this happens when the command list is being submitted, that there
		// will be no further writers for those
		auto releasePage = [](auto& page, unsigned trackerMarker, auto& manager) {
			assert(page->_pendingNewFront != ~0u);		// this would mean we never actually allocated anything from this page

			if (page->_markedDestroys.empty() || page->_markedDestroys.back()._marker != trackerMarker) {
				page->_markedDestroys.emplace_back(MarkedDestroys {trackerMarker, ~0u});
			}
			page->_markedDestroys.back()._front = page->_pendingNewFront;
			page->_pendingNewFront = ~0u;
			manager.ReleaseReservationAlreadyLocked(*page);
		};

		for (const auto& page:_reservedPages) releasePage(page, trackerMarker, *_manager);
		_reservedPages.clear();
		for (const auto& page:_namedPageReservations) releasePage(page, trackerMarker, *_manager);
		_namedPageReservations.clear();
	}

	void CmdListAttachedStorage::AbandonAllocations()
	{
		if (!_manager || (_reservedPages.empty() && _namedPageReservations.empty())) return;

		ScopedLock(_manager->_lock);

		// We don't reset "_pendingNewFront" when releasing the page here
		// That will mean the allocations we made will effectively be cleaned up
		// along with the next user of the page
		for (const auto& page:_reservedPages)
			_manager->ReleaseReservationAlreadyLocked(*page);
		_reservedPages.clear();
		for (const auto& page:_namedPageReservations)
			_manager->ReleaseReservationAlreadyLocked(*page);
		_namedPageReservations.clear();
	}

	CmdListAttachedStorage::operator bool() const { return _manager != nullptr; }

	void CmdListAttachedStorage::MergeIn(CmdListAttachedStorage&& src)
	{
		assert(&src != this);
		if (!_manager) {
			*this = std::move(src);
			return;
		}

		_reservedPages.insert(
			_reservedPages.begin(),
			src._reservedPages.begin(), src._reservedPages.end());
		src._reservedPages.clear();
		_namedPageReservations.resize(std::max(_namedPageReservations.size(), src._namedPageReservations.size()));
		for (unsigned c=0; c<src._namedPageReservations.size(); ++c) {
			assert(!src._namedPageReservations[c] && _namedPageReservations[c]);		// samed named page can't be reserved by both
			if (src._namedPageReservations[c])
				_namedPageReservations[c] = src._namedPageReservations[c];
		}
		src = CmdListAttachedStorage{};
	}

	CmdListAttachedStorage::CmdListAttachedStorage(TemporaryStorageManager::Pimpl* manager) : _manager(manager)
	{
		assert(_manager);
		++_manager->_cmdListAttachedStorageAlive;
	}

	CmdListAttachedStorage::~CmdListAttachedStorage()
	{
		assert(_reservedPages.empty());
		if (_manager) {
			assert(_manager);
			--_manager->_cmdListAttachedStorageAlive;
		}
	}

	CmdListAttachedStorage::CmdListAttachedStorage()
	{
		_manager = nullptr;
	}
	
	CmdListAttachedStorage::CmdListAttachedStorage(CmdListAttachedStorage&& moveFrom)
	: _reservedPages(std::move(moveFrom._reservedPages))
	, _namedPageReservations(std::move(moveFrom._namedPageReservations))
	{
		_manager = moveFrom._manager;
		moveFrom._manager = nullptr;
	}

	CmdListAttachedStorage& CmdListAttachedStorage::operator=(CmdListAttachedStorage&& moveFrom)
	{
		if (this != &moveFrom) {
			assert(_reservedPages.empty());
			if (_manager)
				_manager->_cmdListAttachedStorageAlive--;
			_reservedPages = std::move(moveFrom._reservedPages);
			_namedPageReservations = std::move(moveFrom._namedPageReservations);
			_manager = moveFrom._manager;
			moveFrom._manager = nullptr;
		}
		return *this;
	}

	VertexBufferView TemporaryStorageResourceMap::AsVertexBufferView()
	{
		return VertexBufferView { _resource.get(), (unsigned)_beginAndEndInResource.first };
	}
	
	IndexBufferView TemporaryStorageResourceMap::AsIndexBufferView(Format indexFormat)
	{
		return IndexBufferView { _resource.get(), indexFormat, (unsigned)_beginAndEndInResource.first };
	}

	ConstantBufferView TemporaryStorageResourceMap::AsConstantBufferView()
	{
		return ConstantBufferView { _resource.get(), (unsigned)_beginAndEndInResource.first, (unsigned)_beginAndEndInResource.second };
	}

	std::shared_ptr<IResourceView> TemporaryStorageResourceMap::AsResourceView()
	{
		return std::make_shared<ResourceView>(
			GetObjectFactory(),
			_resource,
			unsigned(_beginAndEndInResource.first), unsigned(_beginAndEndInResource.second-_beginAndEndInResource.first));
	}

	std::shared_ptr<IResourceView> TemporaryStorageResourceMap::AsResourceView(size_t subRangeBegin, size_t subRangeEnd)
	{
		assert(subRangeBegin < _beginAndEndInResource.second);
		assert(subRangeEnd <= _beginAndEndInResource.second);
		assert(subRangeBegin < subRangeEnd);
		return std::make_shared<ResourceView>(
			GetObjectFactory(),
			_resource,
			unsigned(_beginAndEndInResource.first+subRangeBegin), unsigned(subRangeEnd - subRangeBegin));
	}

	CopyPartial_Src TemporaryStorageResourceMap::AsCopySource()
	{
		// assuming linear buffer
		return CopyPartial_Src { *_resource, (unsigned)_beginAndEndInResource.first, (unsigned)_beginAndEndInResource.second };
	}

	TemporaryStorageResourceMap::TemporaryStorageResourceMap(
		ObjectFactory& factory, std::shared_ptr<IResource> resource,
		VkDeviceSize offset, VkDeviceSize size,
		unsigned pageId)
	: ResourceMap(
		factory, *resource,
		Mode::WriteDiscardPrevious,
		offset, size)
	, _resource(std::move(resource))
	, _pageId(pageId)
	, _beginAndEndInResource(offset, offset+size)
	{
	}

	TemporaryStorageResourceMap::TemporaryStorageResourceMap(TemporaryStorageResourceMap&& moveFrom)
	: ResourceMap(std::move(moveFrom))
	{
		_resource = std::move(moveFrom._resource);
		_pageId = moveFrom._pageId;
		moveFrom._pageId = ~0u;
		_beginAndEndInResource = moveFrom._beginAndEndInResource;
		moveFrom._beginAndEndInResource = {0,0};
	}

	TemporaryStorageResourceMap& TemporaryStorageResourceMap::operator=(TemporaryStorageResourceMap&& moveFrom)
	{
		FlushCache();
		ResourceMap::operator=(std::move(moveFrom));
		_resource = std::move(moveFrom._resource);
		_pageId = moveFrom._pageId;
		moveFrom._pageId = ~0u;
		_beginAndEndInResource = moveFrom._beginAndEndInResource;
		moveFrom._beginAndEndInResource = {0,0};
		return *this;
	}

	TemporaryStorageResourceMap::TemporaryStorageResourceMap() {}
	TemporaryStorageResourceMap::~TemporaryStorageResourceMap()
	{
		// ensure that any cached changes get flushed
		FlushCache();
	}

	VertexBufferView BufferAndRange::AsVertexBufferView() { return { _resource.get(), _offset}; }
	IndexBufferView BufferAndRange::AsIndexBufferView(Format indexFormat) { return { _resource.get(), indexFormat, _offset}; }

	static VkBufferMemoryBarrier CreateBufferMemoryBarrier(
		VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)
	{
		return VkBufferMemoryBarrier {
			VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			nullptr,
			VK_ACCESS_HOST_WRITE_BIT,
			VK_ACCESS_INDIRECT_COMMAND_READ_BIT
			| VK_ACCESS_INDEX_READ_BIT
			| VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
			| VK_ACCESS_UNIFORM_READ_BIT
			| VK_ACCESS_INPUT_ATTACHMENT_READ_BIT
			| VK_ACCESS_SHADER_READ_BIT,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			buffer, offset, size };
	}

	void CmdListAttachedStorage::WriteBarrier(DeviceContext& context, unsigned pageId)
	{
		// In most cases, temporary buffer barriers are not required. The API automatically defines a memory barrier between
		// "host operation" (ie, in this case mapping and writing to the temporary buffer from the CPU) and any command list
		// operations when the command list is submitted to the queue.
		// See "7.9. Host Write Ordering Guarantees"
		// This effectively means any memory writes performed before the cmd list is submitted to the queue will be visible
		// We should only need to explicitly add a barrier if intend to write to the buffer from the CPU sometime in the future
		// (ie, after the barrier is written to the cmdlist, and after the cmdlist is submitted to the queue). That doesn't
		// seem like a particularly likely scenario

		TemporaryStoragePage* page = nullptr;
		for (auto p:_reservedPages)
			if (p->_pageId == pageId) {
				page = p;
				break;
			}
		if (!page)
			Throw(std::runtime_error("Attempting to insert a barrier for temporary storage that is not associated with this cmd list"));

		VkDeviceSize startRegion, endRegion;
		if (page->_lastBarrierContext != &context) {
			if (page->_lastBarrierContext != nullptr)
				Log(Warning) << "Temporary buffer used with multiple device contexts. This is an inefficient case, we need improved interface to handle this case better" << std::endl;

			// full barrier
			startRegion = 0;
			endRegion = VK_WHOLE_SIZE;
			page->_lastBarrierContext = &context;
			page->_lastBarrier = page->_heap.Back();
		} else {
			startRegion = page->_lastBarrier;
			endRegion = page->_heap.Back();
			page->_lastBarrier = (unsigned)endRegion;
		}
		if (endRegion == startRegion) return;		// this case should mean no changes

		// With render passes, we're expected to pre-specify all of the memory access and usage rules before hand
		// This is incompatible with dynamically adding in barriers as needed -- so it's not supported and not
		// advisable 
		if (context.IsInRenderPass())
			Throw(std::runtime_error("Attempting to add a memory buffer barrier while inside of a render pass. This isn't supported"));

		VkBufferMemoryBarrier bufferBarrier[2];
		unsigned barrierCount;
		if (endRegion > startRegion) {
			bufferBarrier[0] = CreateBufferMemoryBarrier(
				page->_resource->GetBuffer(), 
				startRegion, endRegion - startRegion);
			barrierCount = 1;
		} else {
			bufferBarrier[0] = CreateBufferMemoryBarrier(
				page->_resource->GetBuffer(),
				startRegion, page->_heap.HeapSize() - startRegion);
			bufferBarrier[1] = CreateBufferMemoryBarrier(
				page->_resource->GetBuffer(),
				0, endRegion);
			barrierCount = 2;
		}

		context.GetActiveCommandList().PipelineBarrier(
			VK_PIPELINE_STAGE_HOST_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, // could be more precise about this?
			0, // by-region flag?
			0, nullptr,
			barrierCount, bufferBarrier,
			0, nullptr);
	}
}}
