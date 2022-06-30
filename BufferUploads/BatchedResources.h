// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once
#include "IBufferUploads.h"
#include "ResourceSource.h"
#include "../RenderCore/ResourceDesc.h"

namespace BufferUploads
{
	class ThreadContext;
	struct BatchingSystemMetrics;

	struct Event_ResourceReposition
	{
		RenderCore::IResource* _originalResource;
		std::shared_ptr<RenderCore::IResource> _newResource;
		std::vector<Utility::RepositionStep> _defragSteps;
	};
	using EventListID = uint32_t;

	class IBatchedResources : public IResourcePool
	{
	public:
		virtual void TickDefrag() = 0;
		virtual IteratorRange<const Event_ResourceReposition*> EventList_Get(EventListID id) = 0;
		virtual void EventList_Release(EventListID id) = 0;
		virtual EventListID EventList_GetPublishedID() const = 0;

		virtual BatchingSystemMetrics CalculateMetrics() const = 0;
	};

	std::shared_ptr<IBatchedResources> CreateBatchedResources(
		RenderCore::IDevice&, const std::shared_ptr<IManager>&, 
		RenderCore::BindFlag::BitField bindFlags,
		unsigned pageSizeInBytes);

	struct BatchedHeapMetrics
	{
		std::vector<unsigned> _markers;
		uint64_t _guid;
		size_t _allocatedSpace, _unallocatedSpace;
		size_t _heapSize;
		size_t _largestFreeBlock;
		unsigned _spaceInReferencedCountedBlocks;
		unsigned _referencedCountedBlockCount;
	};

	struct BatchingSystemMetrics
	{
		std::vector<BatchedHeapMetrics> _heaps;
		unsigned _recentDeviceCreateCount;
		unsigned _totalDeviceCreateCount;
		unsigned _recentAllocateBytes;
		unsigned _totalAllocateBytes;
		unsigned _recentRepositionBytes;
		unsigned _totalRepositionBytes;
	};
}

