// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Pools.h"
#include "ObjectFactory.h"
#include "DeviceContext.h"
#include "FrameBuffer.h"		// for CreateVulkanRenderPass
#include "../../Format.h"
#include "../../OSServices/Log.h"
#include "../../Utility/BitUtils.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Metal_Vulkan
{
	static VkCommandBufferLevel AsBufferLevel(CommandBufferType type)
	{
		switch (type) {
		default:
		case CommandBufferType::Primary: return VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		case CommandBufferType::Secondary: return VK_COMMAND_BUFFER_LEVEL_SECONDARY;
		}
	}

    VulkanSharedPtr<VkCommandBuffer> CommandPool::Allocate(CommandBufferType type)
	{
        #if defined(CHECK_COMMAND_POOL)
            std::unique_lock<std::mutex> guard(_lock, std::try_to_lock);
            if (!guard.owns_lock())
                Throw(::Exceptions::BasicLabel("Bad lock attempt in CommandPool::Allocate. Multiple threads attempting to use the same object."));
        #endif

		VkCommandBufferAllocateInfo cmd = {};
		cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmd.pNext = nullptr;
		cmd.commandPool = _pool.get();
		cmd.level = AsBufferLevel(type);
		cmd.commandBufferCount = 1;

		VkCommandBuffer rawBuffer = nullptr;
		auto res = vkAllocateCommandBuffers(_device.get(), &cmd, &rawBuffer);
		VulkanSharedPtr<VkCommandBuffer> result(
			rawBuffer,
			[this](VkCommandBuffer buffer) { this->QueueDestroy(buffer); });
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while creating command buffer"));
		return result;
	}

	void CommandPool::QueueDestroy(VkCommandBuffer buffer)
	{
		auto currentMarker = _gpuTracker ? _gpuTracker->GetProducerMarker() : ~0u;
		if (_markedDestroys.empty() || _markedDestroys.back()._marker != currentMarker) {
			bool success = _markedDestroys.try_emplace_back(MarkedDestroys{currentMarker, 1u});
			assert(success);	// failure means eating our tail
			if (!success)
				Throw(::Exceptions::BasicLabel("Ran out of buffers in command pool"));
		} else {
			++_markedDestroys.back()._pendingCount;
		}
		_pendingDestroys.push_back(buffer);
	}

	void CommandPool::FlushDestroys()
	{
        #if defined(CHECK_COMMAND_POOL)
            std::unique_lock<std::mutex> guard(_lock, std::try_to_lock);
            if (!guard.owns_lock())
                Throw(::Exceptions::BasicLabel("Bad lock attempt in CommandPool::FlushDestroys. Multiple threads attempting to use the same object."));
        #endif

		auto trackerMarker = _gpuTracker ? _gpuTracker->GetConsumerMarker() : ~0u;
		size_t countToDestroy = 0;
		while (!_markedDestroys.empty() && _markedDestroys.front()._marker <= trackerMarker) {
			countToDestroy += _markedDestroys.front()._pendingCount;
			_markedDestroys.pop_front();
		}

		assert(countToDestroy <= _pendingDestroys.size());
		countToDestroy = std::min(countToDestroy, _pendingDestroys.size());

		if (countToDestroy) {
			vkFreeCommandBuffers(
				_device.get(), _pool.get(),
				(uint32_t)countToDestroy, AsPointer(_pendingDestroys.begin()));
			_pendingDestroys.erase(_pendingDestroys.begin(), _pendingDestroys.begin() + countToDestroy);
		}
	}

    CommandPool::CommandPool(CommandPool&& moveFrom) never_throws
    {
        #if defined(CHECK_COMMAND_POOL)
            std::unique_lock<std::mutex> guard(moveFrom._lock, std::try_to_lock);
            if (!guard.owns_lock())
                Throw(::Exceptions::BasicLabel("Bad lock attempt in CommandPool::Allocate. Multiple threads attempting to use the same object."));
        #endif

        _pool = std::move(moveFrom._pool);
        _device = std::move(moveFrom._device);
        _pendingDestroys = std::move(moveFrom._pendingDestroys);
		_markedDestroys = std::move(_markedDestroys);
		_gpuTracker = std::move(moveFrom._gpuTracker);
    }
    
    CommandPool& CommandPool::operator=(CommandPool&& moveFrom) never_throws
    {
        #if defined(CHECK_COMMAND_POOL)
            // note -- locking both mutexes here.
            //  because we're using try_lock(), it should prevent deadlocks
            std::unique_lock<std::mutex> guard(moveFrom._lock, std::try_to_lock);
            if (!guard.owns_lock())
                Throw(::Exceptions::BasicLabel("Bad lock attempt in CommandPool::Allocate. Multiple threads attempting to use the same object."));

            std::unique_lock<std::mutex> guard2(_lock, std::try_to_lock);
            if (!guard2.owns_lock())
                Throw(::Exceptions::BasicLabel("Bad lock attempt in CommandPool::Allocate. Multiple threads attempting to use the same object."));
        #endif

		if (!_pendingDestroys.empty()) {
			vkFreeCommandBuffers(
				_device.get(), _pool.get(),
				(uint32_t)_pendingDestroys.size(), AsPointer(_pendingDestroys.begin()));
			_pendingDestroys.clear();
		}

        _pool = std::move(moveFrom._pool);
        _device = std::move(moveFrom._device);
        _pendingDestroys = std::move(moveFrom._pendingDestroys);
		_markedDestroys = std::move(_markedDestroys); 
		_gpuTracker = std::move(moveFrom._gpuTracker);
        return *this;
    }

	CommandPool::CommandPool(ObjectFactory& factory, unsigned queueFamilyIndex, bool resetable, const std::shared_ptr<IAsyncTracker>& tracker)
	: _device(factory.GetDevice())
	, _gpuTracker(tracker)
	{
		_pool = factory.CreateCommandPool(
            queueFamilyIndex, 
			resetable ? VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT : 0);
	}

	CommandPool::CommandPool() {}
	CommandPool::~CommandPool() 
	{
		if (!_pendingDestroys.empty()) {
			vkFreeCommandBuffers(
				_device.get(), _pool.get(),
				(uint32_t)_pendingDestroys.size(), AsPointer(_pendingDestroys.begin()));
			_pendingDestroys.clear();
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	class CircularHeap
	{
	public:
		unsigned	AllocateBack(unsigned size);
		void		ResetFront(unsigned newFront);
		unsigned	Back() const { return _end; }
		unsigned	Front() const { return _start; }
		unsigned	HeapSize() const { return _heapSize; }

		CircularHeap(unsigned heapSize);
		~CircularHeap();
	private:
		unsigned	_start;
		unsigned	_end;
		unsigned	_heapSize;
	};

	unsigned	CircularHeap::AllocateBack(unsigned size)
	{
		if (_start == _end) return ~0u;
		if (_start > _end) {
			if ((_start - _end) >= size) {
				auto result = _end;
				_end += size;
				return result;
			}
		} else if ((_end + size) <= _heapSize) {
			auto result = _end;
			_end = _end + size;
			return result;
		} else if (_start >= size) { // this is the wrap around case
			_end = size;
			return 0u;
		}

		return ~0u;
	}

	void		CircularHeap::ResetFront(unsigned newFront)
	{
		_start = newFront;
		if (_start == _end) {
			_start = _heapSize;
			_end = 0;
		}
	}

	CircularHeap::CircularHeap(unsigned heapSize)
	{
		_start = heapSize;
		_end = 0;
		_heapSize = heapSize;
	}

	CircularHeap::~CircularHeap() {}

	class TemporaryBufferSpace::Pimpl
	{
	public:
		ObjectFactory*			_factory;
		std::shared_ptr<IAsyncTracker>	_gpuTracker;

		struct MarkedDestroys
		{
			IAsyncTracker::Marker	_marker;
			unsigned				_front;
		};

		class ReservedSpace
		{
		public:
			Resource		_buffer;
			CircularHeap	_heap;
			CircularBuffer<MarkedDestroys, 8>	_markedDestroys;

			unsigned		_lastBarrier;
			DeviceContext*	_lastBarrierContext;

			unsigned		_alignment;

			ReservedSpace(ObjectFactory& factory, size_t size);
			~ReservedSpace();
		};
		
		ReservedSpace _cb;

		Pimpl(ObjectFactory& factory, std::shared_ptr<IAsyncTracker> gpuTracker);
	};

	static ResourceDesc BuildBufferDesc(
		BindFlag::BitField bindingFlags, size_t byteCount)
	{
		return CreateDesc(
			bindingFlags,
			CPUAccess::Write, GPUAccess::Read,
			LinearBufferDesc::Create(unsigned(byteCount)),
			"RollingTempBuf");
	}

	TemporaryBufferSpace::Pimpl::ReservedSpace::ReservedSpace(ObjectFactory& factory, size_t byteCount)
	: _buffer(factory, BuildBufferDesc(BindFlag::ConstantBuffer, byteCount))
	, _heap((unsigned)byteCount)
	{
		_lastBarrier = 0u;
		_lastBarrierContext = nullptr;
		_alignment = 1;
	}

	TemporaryBufferSpace::Pimpl::ReservedSpace::~ReservedSpace() {}

	TemporaryBufferSpace::Pimpl::Pimpl(ObjectFactory& factory, std::shared_ptr<IAsyncTracker> gpuTracker)
	: _factory(&factory), _gpuTracker(gpuTracker)
	, _cb(factory, 256*1024)
	{
	}

	static void PushData(VkDevice device, Resource& buffer, size_t startPt, IteratorRange<const void*> data)
	{
		// Write to the buffer using a map and CPU assisted copy
		// Note -- we could also consider using "non-coherent" memory access here, and manually doing
		// flushes and invalidates.
		ResourceMap map(
			device, buffer.GetMemory(),
			startPt, data.size());
		std::memcpy(map.GetData().begin(), data.begin(), data.size());
	}

	VkDescriptorBufferInfo	TemporaryBufferSpace::AllocateBuffer(IteratorRange<const void*> data)
	{
		auto& b = _pimpl->_cb;

		bool fitsInHeap = true;
		auto currentMarker = _pimpl->_gpuTracker->GetProducerMarker();
		if (b._markedDestroys.empty()
			|| b._markedDestroys.back()._marker != currentMarker) {

			fitsInHeap = b._markedDestroys.try_emplace_back(
				Pimpl::MarkedDestroys {currentMarker, ~0u});
		}

		if (fitsInHeap) {
			auto byteCount = data.size();
			auto alignedByteCount = CeilToMultiple((unsigned)byteCount, b._alignment); // (probably pow2, but we don't know for sure)
			auto space = b._heap.AllocateBack(alignedByteCount);
			if (space != ~0u) {
				assert((space % b._alignment) == 0);
				PushData(_pimpl->_factory->GetDevice().get(), b._buffer, space, data);
				b._markedDestroys.back()._front = space + alignedByteCount;

				// Check if we've crossed over the "last barrier" point (no special
				// handling for wrap around case required)
				if (space <= b._lastBarrier && space > b._lastBarrier)
					b._lastBarrierContext = nullptr;	// reset tracking

				// Some devices have strict alignnment rules for the "offset" value for constant buffers
				// We must respect the alignment restriction given to use from vkGetPhysicalDeviceProperties!
				assert((space % b._alignment) == 0);
				return VkDescriptorBufferInfo { b._buffer.GetBuffer(), space, byteCount };
			}
		}

		return VkDescriptorBufferInfo{ nullptr, 0, 0 };
	}

	void TemporaryBufferSpace::FlushDestroys()
	{
		auto& b = _pimpl->_cb;

		auto trackerMarker = _pimpl->_gpuTracker ? _pimpl->_gpuTracker->GetConsumerMarker() : ~0u;
		unsigned newFront = ~0u;
		while (!b._markedDestroys.empty() && b._markedDestroys.front()._marker <= trackerMarker) {
			newFront = b._markedDestroys.front()._front;
			b._markedDestroys.pop_front();
		}

		if (newFront == ~0u) return;
		b._heap.ResetFront(newFront);
	}

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

	void TemporaryBufferSpace::WriteBarrier(DeviceContext& context)
	{
		return;

		// We want to create a barrier that covers all data written to the buffer
		// since the last barrier on this context.
		// We could assume that we're always using the same context -- in which case
		// the tracking becomes easier.

		VkDeviceSize startRegion, endRegion;
		auto& b = _pimpl->_cb;
		if (b._lastBarrierContext != &context) {
			if (b._lastBarrierContext != nullptr)
				Log(Warning) << "Temporary buffer used with multiple device contexts. This is an inefficient case, we need improved interface to handle this case better" << std::endl;

			// full barrier
			startRegion = 0;
			endRegion = VK_WHOLE_SIZE;
			b._lastBarrierContext = &context;
			b._lastBarrier = b._heap.Back();
		} else {
			startRegion = b._lastBarrier;
			endRegion = b._heap.Back();
			b._lastBarrier = (unsigned)endRegion;
		}
		if (endRegion == startRegion) return;		// this case should mean no changes

		if (context.IsInRenderPass()) {
			// Inside a render pass, we can't have a buffer barrier. Our only
			// option is a global memory barrier (but this tied into a
			// a subpass dependency in the render pass). This will probably create
			// unnecessary synchronization in some cases.
			VkMemoryBarrier globalBarrier =
			{
				VK_STRUCTURE_TYPE_MEMORY_BARRIER,
				nullptr,
				VK_ACCESS_HOST_WRITE_BIT,
				VK_ACCESS_INDIRECT_COMMAND_READ_BIT
				| VK_ACCESS_INDEX_READ_BIT
				| VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
				| VK_ACCESS_UNIFORM_READ_BIT
				| VK_ACCESS_INPUT_ATTACHMENT_READ_BIT
				| VK_ACCESS_SHADER_READ_BIT
			};
			context.GetActiveCommandList().PipelineBarrier(
				VK_PIPELINE_STAGE_HOST_BIT,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, // could be more precise about this?
				0,
				1, &globalBarrier,
				0, nullptr,
				0, nullptr);
		} else {
			VkBufferMemoryBarrier bufferBarrier[2];
			unsigned barrierCount;
			if (endRegion > startRegion) {
				bufferBarrier[0] = CreateBufferMemoryBarrier(
					_pimpl->_cb._buffer.GetBuffer(), 
					startRegion, endRegion - startRegion);
				barrierCount = 1;
			} else {
				bufferBarrier[0] = CreateBufferMemoryBarrier(
					_pimpl->_cb._buffer.GetBuffer(),
					startRegion, b._heap.HeapSize() - startRegion);
				bufferBarrier[1] = CreateBufferMemoryBarrier(
					_pimpl->_cb._buffer.GetBuffer(),
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
	}

	TemporaryBufferSpace::TemporaryBufferSpace(
		ObjectFactory& factory,
		const std::shared_ptr<IAsyncTracker>& asyncTracker) 
	{
		_pimpl = std::make_unique<Pimpl>(factory, asyncTracker);
		_pimpl->_cb._alignment = std::max(1u, (unsigned)factory.GetPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment);
	}
	
	TemporaryBufferSpace::~TemporaryBufferSpace() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    void DescriptorPool::Allocate(
        IteratorRange<VulkanUniquePtr<VkDescriptorSet>*> dst,
        IteratorRange<const VkDescriptorSetLayout*> layouts)
    {
        assert(dst.size() == layouts.size());
        assert(dst.size() > 0);

        VkDescriptorSetAllocateInfo desc_alloc_info;
        desc_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        desc_alloc_info.pNext = nullptr;
        desc_alloc_info.descriptorPool = _pool.get();
        desc_alloc_info.descriptorSetCount = (uint32_t)std::min(dst.size(), layouts.size());
        desc_alloc_info.pSetLayouts = layouts.begin();

        VkDescriptorSet rawDescriptorSets[4] = { nullptr, nullptr, nullptr, nullptr };

        VkResult res;
        if (desc_alloc_info.descriptorSetCount <= dimof(rawDescriptorSets)) {
            res = vkAllocateDescriptorSets(_device.get(), &desc_alloc_info, rawDescriptorSets);
            for (unsigned c=0; c<desc_alloc_info.descriptorSetCount; ++c)
                dst[c] = VulkanUniquePtr<VkDescriptorSet>(
                    rawDescriptorSets[c],
                    [this](VkDescriptorSet set) { this->QueueDestroy(set); });
        } else {
            std::vector<VkDescriptorSet> rawDescriptorsOverflow;
            rawDescriptorsOverflow.resize(desc_alloc_info.descriptorSetCount, nullptr);
            res = vkAllocateDescriptorSets(_device.get(), &desc_alloc_info, AsPointer(rawDescriptorsOverflow.begin()));
            for (unsigned c=0; c<desc_alloc_info.descriptorSetCount; ++c)
                dst[c] = VulkanUniquePtr<VkDescriptorSet>(
                    rawDescriptorsOverflow[c],
                    [this](VkDescriptorSet set) { this->QueueDestroy(set); });
        }

        if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while allocating descriptor set")); 
    }

	VulkanUniquePtr<VkDescriptorSet> DescriptorPool::Allocate(VkDescriptorSetLayout layout)
	{
		VulkanUniquePtr<VkDescriptorSet> result[1];
		Allocate(MakeIteratorRange(result), MakeIteratorRange(&layout, &layout+1));
		return std::move(result[0]);
	}

    void DescriptorPool::FlushDestroys()
    {
		if (!_device || !_pool) return;
        
		auto trackerMarker = _gpuTracker ? _gpuTracker->GetConsumerMarker() : ~0u;
		size_t countToDestroy = 0;
		while (!_markedDestroys.empty() && _markedDestroys.front()._marker <= trackerMarker) {
			countToDestroy += _markedDestroys.front()._pendingCount;
			_markedDestroys.pop_front();
		}

		assert(countToDestroy <= _pendingDestroys.size());
		countToDestroy = std::min(countToDestroy, _pendingDestroys.size());

		if (countToDestroy) {
			vkFreeDescriptorSets(
				_device.get(), _pool.get(),
				(uint32_t)countToDestroy, AsPointer(_pendingDestroys.begin()));
			_pendingDestroys.erase(_pendingDestroys.begin(), _pendingDestroys.begin() + countToDestroy);
		}
    }

	void DescriptorPool::QueueDestroy(VkDescriptorSet set)
	{
		auto currentMarker = _gpuTracker ? _gpuTracker->GetProducerMarker() : ~0u;
		if (_markedDestroys.empty() || _markedDestroys.back()._marker != currentMarker) {
			bool success = _markedDestroys.try_emplace_back(MarkedDestroys{ currentMarker, 1u });
			assert(success);	// failure means eating our tail
			if (!success)
				Throw(::Exceptions::BasicLabel("Ran out of buffers in command pool"));
		} else {
			++_markedDestroys.back()._pendingCount;
		}
		_pendingDestroys.push_back(set);
	}

    DescriptorPool::DescriptorPool(ObjectFactory& factory, const std::shared_ptr<IAsyncTracker>& tracker)
    : _device(factory.GetDevice())
	, _gpuTracker(tracker)
    {
        VkDescriptorPoolSize type_count[] = 
        {
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 256},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256},
            {VK_DESCRIPTOR_TYPE_SAMPLER, 256}
        };

        VkDescriptorPoolCreateInfo descriptor_pool = {};
        descriptor_pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptor_pool.pNext = nullptr;
        descriptor_pool.maxSets = 1024;
        descriptor_pool.poolSizeCount = dimof(type_count);
        descriptor_pool.pPoolSizes = type_count;
        descriptor_pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        _pool = factory.CreateDescriptorPool(descriptor_pool);
    }
    DescriptorPool::DescriptorPool() {}
    DescriptorPool::~DescriptorPool() 
    {
		if (!_pendingDestroys.empty()) {
			vkFreeDescriptorSets(
				_device.get(), _pool.get(),
				(uint32_t)_pendingDestroys.size(), AsPointer(_pendingDestroys.begin()));
			_pendingDestroys.clear();
		}
    }

    DescriptorPool::DescriptorPool(DescriptorPool&& moveFrom) never_throws
    : _pool(std::move(moveFrom._pool))
    , _device(std::move(moveFrom._device))
	, _gpuTracker(std::move(moveFrom._gpuTracker))
	, _markedDestroys(std::move(moveFrom._markedDestroys))
    , _pendingDestroys(moveFrom._pendingDestroys)
    {
    }

    DescriptorPool& DescriptorPool::operator=(DescriptorPool&& moveFrom) never_throws
    {
		if (!_pendingDestroys.empty()) {
			vkFreeDescriptorSets(
				_device.get(), _pool.get(),
				(uint32_t)_pendingDestroys.size(), AsPointer(_pendingDestroys.begin()));
			_pendingDestroys.clear();
		}
        _pool = std::move(moveFrom._pool);
        _device = std::move(moveFrom._device);
		_gpuTracker = std::move(moveFrom._gpuTracker);
		_markedDestroys = std::move(moveFrom._markedDestroys);
		_pendingDestroys = std::move(moveFrom._pendingDestroys);
        return *this;
    }

	VulkanSharedPtr<VkRenderPass> VulkanRenderPassPool::CreateVulkanRenderPass(
		const FrameBufferDesc& layout,
		TextureSamples samples)
	{
		auto hash = HashCombine(layout.GetHash(), samples._sampleCount << 8 | samples._samplingQuality);
		auto i = LowerBound(_cachedRenderPasses, hash);
		if (i != _cachedRenderPasses.end() && i->first == hash) {
			return i->second;
		} else {
			VulkanSharedPtr<VkRenderPass> newRenderPass = Metal_Vulkan::CreateVulkanRenderPass(*_factory, layout, samples);
			_cachedRenderPasses.insert(i, std::make_pair(hash, newRenderPass));
			return newRenderPass;
		}
	}

	VulkanRenderPassPool::VulkanRenderPassPool(ObjectFactory& factory)
	: _factory(&factory) {}
	VulkanRenderPassPool::VulkanRenderPassPool() {}
	VulkanRenderPassPool::~VulkanRenderPassPool() {}

    static std::shared_ptr<Resource> CreateDummyTexture(ObjectFactory& factory)
    {
        auto desc = CreateDesc(
            BindFlag::ShaderResource, 
            CPUAccess::Write, GPUAccess::Read, 
            TextureDesc::Plain2D(32, 32, Format::R8G8B8A8_UNORM), "DummyTexture");
        uint32 dummyData[32*32];
        std::memset(dummyData, 0, sizeof(dummyData));
        return Internal::CreateResource(
            factory, desc, 
            [&dummyData](SubResourceId)
            {
                return SubResourceInitData{MakeIteratorRange(dummyData), TexturePitches{32*4, 32*32*4}};
            });
    }

    static std::shared_ptr<Resource> CreateDummyUAVImage(ObjectFactory& factory)
    {
        auto desc = CreateDesc(
            BindFlag::UnorderedAccess, 
            0, GPUAccess::Read|GPUAccess::Write, 
            TextureDesc::Plain2D(32, 32, Format::R8G8B8A8_UNORM), "DummyTexture");
        return Internal::CreateResource(factory, desc);
    }

    static std::shared_ptr<Resource> CreateDummyUAVBuffer(ObjectFactory& factory)
    {
        auto desc = CreateDesc(
            BindFlag::UnorderedAccess, 
            0, GPUAccess::Read|GPUAccess::Write, 
            LinearBufferDesc::Create(256, 256), "DummyBuffer");
        return Internal::CreateResource(factory, desc);
    }

	void DummyResources::CompleteInitialization(DeviceContext& devContext)
	{
		IResource* res[] = { _blankTexture.get(), _blankUAVImageRes.get(), _blankUAVBufferRes.get() }; 
		Metal_Vulkan::CompleteInitialization(devContext, MakeIteratorRange(res));
	}

    DummyResources::DummyResources(ObjectFactory& factory)
    : _blankTexture(CreateDummyTexture(factory))
    , _blankUAVImageRes(CreateDummyUAVImage(factory))
    , _blankUAVBufferRes(CreateDummyUAVBuffer(factory))
    , _blankSrv(factory, _blankTexture)
    , _blankUavImage(factory, _blankUAVImageRes)
    , _blankUavBuffer(factory, _blankUAVBufferRes)
    , _blankSampler(std::make_unique<SamplerState>(factory, SamplerDesc{FilterMode::Point, AddressMode::Clamp, AddressMode::Clamp}))
    {
        uint8 blankData[4096];
        std::memset(blankData, 0, sizeof(blankData));
		auto initData = MakeIteratorRange(blankData);
        _blankBuffer = Internal::CreateResource(
            factory, 
            CreateDesc(BindFlag::ConstantBuffer, 0, GPUAccess::Read, LinearBufferDesc::Create(sizeof(blankData)), "DummyBuffer"),
			[initData](SubResourceId subResId) -> SubResourceInitData {
                assert(subResId._mip == 0 && subResId._arrayLayer == 0);
            	return SubResourceInitData{initData};
			});
    }

    DummyResources::DummyResources() {}
    DummyResources::~DummyResources() {}

    DummyResources::DummyResources(DummyResources&& moveFrom) never_throws
    : _blankTexture(std::move(moveFrom._blankTexture))
    , _blankUAVImageRes(std::move(moveFrom._blankUAVImageRes))
    , _blankUAVBufferRes(std::move(moveFrom._blankUAVBufferRes))
    , _blankSrv(std::move(moveFrom._blankSrv))
    , _blankUavImage(std::move(moveFrom._blankUavImage))
    , _blankUavBuffer(std::move(moveFrom._blankUavBuffer))
    , _blankBuffer(std::move(moveFrom._blankBuffer))
    , _blankSampler(std::move(moveFrom._blankSampler))
    {}

    DummyResources& DummyResources::operator=(DummyResources&& moveFrom) never_throws
    {
        _blankTexture = std::move(moveFrom._blankTexture);
        _blankUAVImageRes = std::move(moveFrom._blankUAVImageRes);
        _blankUAVBufferRes = std::move(moveFrom._blankUAVBufferRes);
        _blankSrv = std::move(moveFrom._blankSrv);
        _blankUavImage = std::move(moveFrom._blankUavImage);
        _blankUavBuffer = std::move(moveFrom._blankUavBuffer);
        _blankBuffer = std::move(moveFrom._blankBuffer);
        _blankSampler = std::move(moveFrom._blankSampler);
        return *this;
    }

    GlobalPools::GlobalPools() {}
    GlobalPools::~GlobalPools() {}

}}