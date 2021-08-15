// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Resource.h"
#include "VulkanCore.h"
#include "../../ResourceDesc.h"

namespace RenderCore
{
	class VertexBufferView;
	class IndexBufferView;
	class ConstantBufferView;
}

namespace RenderCore { namespace Metal_Vulkan
{
	///
	/// \page Command list attached storage
	///
	/// This is used for temporary allocations associated with a command list. The allocations lifetime is
	/// directly controlled by the command list. That is they are visible to the CPU while preparing the command
	/// list and visible by the GPU after the command list has been queued until the command list finishes
	/// processing.
	///
	/// Generally this is used for things like dynamic geometry and temporary uniform buffers.
	///
	/// There is a single global manager that has within it a number of pages (TemporaryStorageManager). 
	/// When a command list needs some storage space it will reserve one or more pages. The pages contain device 
	/// resources, which can then be mapped and written to. This command list specific reservation is the 
	/// CmdListAttachedStorage object
	///
	/// A command list has exclusive access to any pages it's working with; so we can build multiple command
	/// lists as the same time (either on different threads or the same thread).
	///

	class TemporaryStorageResourceMap : public ResourceMap
	{
	public:
		const std::shared_ptr<IResource>& GetResource() const { return _resource; }
		std::pair<size_t, size_t> GetBeginAndEndInResource() const { return _beginAndEndInResource; }
		unsigned GetPageId() const { return _pageId; }

		VertexBufferView AsVertexBufferView();
		IndexBufferView AsIndexBufferView(Format indexFormat);
		ConstantBufferView AsConstantBufferView();
		std::shared_ptr<IResourceView> AsResourceView();

		TemporaryStorageResourceMap(TemporaryStorageResourceMap&&) = delete;
		TemporaryStorageResourceMap& operator=(TemporaryStorageResourceMap&&) = delete;
		TemporaryStorageResourceMap(ResourceMap&&) = delete;
		TemporaryStorageResourceMap& operator=(ResourceMap&&) = delete;
	private:
		TemporaryStorageResourceMap(
			VkDevice dev, std::shared_ptr<IResource> resource,
			VkDeviceSize offset, VkDeviceSize size,
			unsigned pageId);
		std::shared_ptr<IResource> _resource;
		unsigned _pageId = ~0u;
		std::pair<size_t, size_t> _beginAndEndInResource;
		friend class CmdListAttachedStorage;
	};

	class IAsyncTracker;
	class CmdListAttachedStorage;

	class TemporaryStorageManager
	{
	public:
		CmdListAttachedStorage BeginCmdListReservation();
		void FlushDestroys();

		TemporaryStorageManager(
			ObjectFactory& factory,
			const std::shared_ptr<IAsyncTracker>& asyncTracker);
		~TemporaryStorageManager();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
		friend class CmdListAttachedStorage;
	};

	class TemporaryStoragePage;

	class CmdListAttachedStorage
	{
	public:
		TemporaryStorageResourceMap	MapStorage(size_t byteCount, BindFlag::Enum type);
		void OnSubmitToQueue(unsigned trackerMarker);		// IAsyncTracker::Marker
		void AbandonAllocations();
		void WriteBarrier(DeviceContext& context, unsigned pageId);
		void MergeIn(CmdListAttachedStorage&& src);
		operator bool() const;
		~CmdListAttachedStorage();

		CmdListAttachedStorage();
		CmdListAttachedStorage(CmdListAttachedStorage&&);
		CmdListAttachedStorage& operator=(CmdListAttachedStorage&&);

	private:
		std::vector<TemporaryStoragePage*> _reservedPages;
		TemporaryStorageManager::Pimpl* _manager = nullptr;
		CmdListAttachedStorage(TemporaryStorageManager::Pimpl*);
		friend class TemporaryStorageManager;
	}; 
}}

