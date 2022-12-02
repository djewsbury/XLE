// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Pools.h"
#include "ObjectFactory.h"
#include "DeviceContext.h"
#include "FrameBuffer.h"		// for CreateVulkanRenderPass
#include "ExtensionFunctions.h"
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

    VulkanSharedPtr<VkCommandBuffer> CommandBufferPool::Allocate(CommandBufferType type)
	{
        ScopedLock(_lock);

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

	void CommandBufferPool::QueueDestroy(VkCommandBuffer buffer)
	{
        ScopedLock(_lock);
		auto currentMarker = _gpuTracker ? _gpuTracker->GetProducerMarker() : ~0u;
        if (!_markedDestroys.empty() && _markedDestroys.back()._marker == currentMarker) {
            ++_markedDestroys.back()._pendingCount;
        } else {
            _markedDestroys.emplace_back(MarkedDestroys{currentMarker, 1u});
        }

        #if defined(_DEBUG)
            if (_markedDestroys.page_count() > 2) {
                static auto lastMsg = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if ((now-lastMsg) > std::chrono::seconds(1)) {
                    Log(Warning) << "High number of _markedDestroys pages in CommandPool." << std::endl;
                    lastMsg = now;
                }
            }
        #endif

		_pendingDestroys.push_back(buffer);
	}

	void CommandBufferPool::FlushDestroys()
	{
        ScopedLock(_lock);

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

    CommandBufferPool::CommandBufferPool(CommandBufferPool&& moveFrom) never_throws
    {
        std::unique_lock<std::mutex> guard(moveFrom._lock, std::try_to_lock);
        if (!guard.owns_lock())
            Throw(::Exceptions::BasicLabel("Bad lock attempt in CommandPool::Allocate. Multiple threads attempting to use the same object."));

        _pool = std::move(moveFrom._pool);
        _device = std::move(moveFrom._device);
        _pendingDestroys = std::move(moveFrom._pendingDestroys);
		_markedDestroys = std::move(moveFrom._markedDestroys);
		_gpuTracker = std::move(moveFrom._gpuTracker);
    }
    
    CommandBufferPool& CommandBufferPool::operator=(CommandBufferPool&& moveFrom) never_throws
    {
        std::unique_lock<std::mutex> guard(moveFrom._lock, std::defer_lock);
        std::unique_lock<std::mutex> guard2(_lock, std::defer_lock);
        if (!std::try_lock(guard, guard2))
            Throw(::Exceptions::BasicLabel("Bad lock attempt in CommandPool::Allocate. Multiple threads attempting to use the same object."));
        
		if (!_pendingDestroys.empty()) {
            // potentially dangerous early destruction (can happen in exception cases)
			vkFreeCommandBuffers(
				_device.get(), _pool.get(),
				(uint32_t)_pendingDestroys.size(), AsPointer(_pendingDestroys.begin()));
			_pendingDestroys.clear();
		}

        _pool = std::move(moveFrom._pool);
        _device = std::move(moveFrom._device);
        _pendingDestroys = std::move(moveFrom._pendingDestroys);
		_markedDestroys = std::move(moveFrom._markedDestroys); 
		_gpuTracker = std::move(moveFrom._gpuTracker);
        return *this;
    }

	CommandBufferPool::CommandBufferPool(ObjectFactory& factory, unsigned queueFamilyIndex, bool resetable, const std::shared_ptr<IAsyncTracker>& tracker)
	: _device(factory.GetDevice())
	, _gpuTracker(tracker)
	{
		_pool = factory.CreateCommandPool(
            queueFamilyIndex, 
			resetable ? VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT : 0);
	}

	CommandBufferPool::CommandBufferPool() {}
	CommandBufferPool::~CommandBufferPool() 
	{
		if (!_pendingDestroys.empty()) {
            // potentially dangerous early destruction (can happen in exception cases)
			vkFreeCommandBuffers(
				_device.get(), _pool.get(),
				(uint32_t)_pendingDestroys.size(), AsPointer(_pendingDestroys.begin()));
			_pendingDestroys.clear();
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    void DescriptorPool::AllocateAlreadyLocked(
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

        VLA(VkDescriptorSet, rawDescriptorSets, desc_alloc_info.descriptorSetCount);
        for (unsigned c=0; c<desc_alloc_info.descriptorSetCount; ++c)
            rawDescriptorSets[c] = 0;

        VkResult res;
        res = vkAllocateDescriptorSets(_device.get(), &desc_alloc_info, rawDescriptorSets);
        for (unsigned c=0; c<desc_alloc_info.descriptorSetCount; ++c)
            dst[c] = VulkanUniquePtr<VkDescriptorSet>(
                rawDescriptorSets[c],
                [this](VkDescriptorSet set) { this->QueueDestroy(set); });
        
        #if defined(VULKAN_ENABLE_DEBUG_EXTENSIONS)
            auto& extFn = GetObjectFactory().GetExtensionFunctions();
			if (extFn._setObjectName && !_poolName.empty()) {
                for (unsigned c=0; c<desc_alloc_info.descriptorSetCount; ++c) {
                    VkDebugUtilsObjectNameInfoEXT nameInfo {
                        VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, nullptr,
                        VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)dst[c].get(),
                        _poolName.c_str()
                    };
				    extFn._setObjectName(_device.get(), &nameInfo);
                }
			}
		#endif

        if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while allocating descriptor set")); 
    }

    void DescriptorPool::Allocate(
        IteratorRange<VulkanUniquePtr<VkDescriptorSet>*> dst,
        IteratorRange<const VkDescriptorSetLayout*> layouts)
    {
        ScopedLock(_lock);
        AllocateAlreadyLocked(dst, layouts);
    }

	VulkanUniquePtr<VkDescriptorSet> DescriptorPool::Allocate(VkDescriptorSetLayout layout)
	{
		VulkanUniquePtr<VkDescriptorSet> result[1];
		Allocate(MakeIteratorRange(result), MakeIteratorRange(&layout, &layout+1));
		return std::move(result[0]);
	}

    VkDescriptorSet DescriptorPoolReusableGroup::AllocateSingleImmediateUse()
    {
        assert(_parent->_gpuTracker);
        auto producerMarker = _parent->_gpuTracker->GetProducerMarker();

        ScopedLock(_parent->_lock); // unfortunately global pool lock required
        Page* page = nullptr;
        unsigned item;
        for (auto& p:_pages) {
            item = p._allocationStates.AllocateBack(1);
            if (item != ~0u) {
                page = &p;
                break;
            }
        }

        if (!page) {
            // create a new page & allocate the first item
            Page newPage;
            newPage._allocationStates = CircularHeap{PageSize};
            newPage._descriptorSets.resize(PageSize);
            VkDescriptorSetLayout layouts[PageSize];
            for (unsigned c=0; c<PageSize; ++c) layouts[c] = _layout->GetUnderlying();
            _parent->AllocateAlreadyLocked(
                MakeIteratorRange(newPage._descriptorSets),
                MakeIteratorRange(layouts));

            item = newPage._allocationStates.AllocateBack(1);
            _pages.emplace_back(std::move(newPage));
            page = &_pages.back();
        }

        if (!page->_frontResets.empty() && page->_frontResets.back().first == producerMarker) {
            page->_frontResets.back().second = item;
        } else {
            bool success = page->_frontResets.try_emplace_back(producerMarker, item);
            assert(success);    // should always succeed, because we're limited by the size of _allocationStates
            (void)success;
        }

        // if not already first in the list in allocation order, move there
        if (_parent->_reusableGroupsInAllocationOrder != this) {
            if (_prevInAllocationOrder) {
                assert(_prevInAllocationOrder->_nextInAllocationOrder == this);
                _prevInAllocationOrder->_nextInAllocationOrder = _nextInAllocationOrder;
                if (_nextInAllocationOrder) {
                    assert(_nextInAllocationOrder->_prevInAllocationOrder == this);
                    _nextInAllocationOrder->_prevInAllocationOrder = _prevInAllocationOrder;
                }
                _prevInAllocationOrder = nullptr;
            } else {
                assert(!_nextInAllocationOrder);
            }
            _nextInAllocationOrder = _parent->_reusableGroupsInAllocationOrder;
            if (_nextInAllocationOrder) {
                assert(!_nextInAllocationOrder->_prevInAllocationOrder);
                _nextInAllocationOrder->_prevInAllocationOrder = this;
            }
            _parent->_reusableGroupsInAllocationOrder = this;
        }

        _empty = false;
        return page->_descriptorSets[item].get();
    }

    DescriptorPoolReusableGroup::DescriptorPoolReusableGroup(DescriptorPool& parent, std::shared_ptr<CompiledDescriptorSetLayout> layout)
    : _parent(&parent), _layout(std::move(layout))
    {}

    DescriptorPoolReusableGroup::~DescriptorPoolReusableGroup()
    {}

    auto DescriptorPool::GetReusableGroup(const std::shared_ptr<CompiledDescriptorSetLayout>& layout) -> const std::shared_ptr<DescriptorPoolReusableGroup>&
    {
        // find a reusable group that matches the layout hash
        auto hash = layout->GetHashCode();
        ScopedLock(_lock);
        auto i = LowerBound(_reusableGroups, hash);
        if (i != _reusableGroups.end() && i->first == hash)
            return i->second;

        std::shared_ptr<DescriptorPoolReusableGroup> newGroup { new DescriptorPoolReusableGroup{*this, layout} };
        i = _reusableGroups.insert(i, {hash, std::move(newGroup)});
        return i->second;
    }

    void DescriptorPool::FlushDestroys()
    {
		if (!_device || !_pool) return;

        ScopedLock(_lock);
        
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

        // check reusable groups
        auto* reusableGroup = _reusableGroupsInAllocationOrder;
        while (reusableGroup && !reusableGroup->_empty) {
            bool anythingLeft = false;
            for (auto &p:reusableGroup->_pages) {
                while (!p._frontResets.empty() && p._frontResets.front().first <= trackerMarker) {
                    p._allocationStates.ResetFront(p._frontResets.front().second);
                    p._frontResets.pop_front();
                }
                anythingLeft |= p._frontResets.empty();
            }
            reusableGroup->_empty = !anythingLeft;
            reusableGroup = reusableGroup->_nextInAllocationOrder;
        }
    }

	void DescriptorPool::QueueDestroy(VkDescriptorSet set)
	{
        ScopedLock(_lock);
		auto currentMarker = _gpuTracker ? _gpuTracker->GetProducerMarker() : ~0u;
		if (_markedDestroys.empty() || _markedDestroys.back()._marker != currentMarker) {
			_markedDestroys.emplace_back(MarkedDestroys{ currentMarker, 1u });
		} else {
			++_markedDestroys.back()._pendingCount;
		}
		_pendingDestroys.push_back(set);
	}

    DescriptorPool::DescriptorPool(ObjectFactory& factory, const std::shared_ptr<IAsyncTracker>& tracker, StringSection<> poolName)
    : _device(factory.GetDevice())
	, _gpuTracker(tracker)
    , _poolName(poolName.AsString())
    {
        VkDescriptorPoolSize type_count[] = 
        {
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16*1024},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16*128},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16*1024},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 16*1024},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16*128},
            {VK_DESCRIPTOR_TYPE_SAMPLER, 16*256},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 16*128}
        };

        VkDescriptorPoolCreateInfo descriptor_pool = {};
        descriptor_pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptor_pool.pNext = nullptr;
        descriptor_pool.maxSets = 4096;
        descriptor_pool.poolSizeCount = dimof(type_count);
        descriptor_pool.pPoolSizes = type_count;
        descriptor_pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        _pool = factory.CreateDescriptorPool(descriptor_pool);
    }
    DescriptorPool::DescriptorPool() {}
    DescriptorPool::~DescriptorPool() 
    {
		DestroyEverythingImmediately();
    }

    void DescriptorPool::DestroyEverythingImmediately()
    {
        // clear reusable groups first, because they will release into our _pendingDestroys list
        _reusableGroups.clear();
        if (!_pendingDestroys.empty()) {
            // potentially dangerous early destruction (can happen in exception cases)
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
    , _pendingDestroys(std::move(moveFrom._pendingDestroys))
    , _reusableGroups(std::move(moveFrom._reusableGroups))
    , _reusableGroupsInAllocationOrder(std::move(moveFrom._reusableGroupsInAllocationOrder))
    , _poolName(std::move(moveFrom._poolName))
    {
    }

    DescriptorPool& DescriptorPool::operator=(DescriptorPool&& moveFrom) never_throws
    {
		DestroyEverythingImmediately();
        _pool = std::move(moveFrom._pool);
        _device = std::move(moveFrom._device);
		_gpuTracker = std::move(moveFrom._gpuTracker);
		_markedDestroys = std::move(moveFrom._markedDestroys);
		_pendingDestroys = std::move(moveFrom._pendingDestroys);
        _reusableGroups = std::move(moveFrom._reusableGroups);
        _reusableGroupsInAllocationOrder = std::move(moveFrom._reusableGroupsInAllocationOrder);
        _poolName = std::move(moveFrom._poolName);
        return *this;
    }

	VulkanSharedPtr<VkRenderPass> VulkanRenderPassPool::CreateVulkanRenderPass(const FrameBufferDesc& layout)
	{
        ScopedLock(_lock);
		auto hash = layout.GetHashExcludingDimensions();
		auto i = LowerBound(_cachedRenderPasses, hash);
		if (i != _cachedRenderPasses.end() && i->first == hash) {
			return i->second;
		} else {
			VulkanSharedPtr<VkRenderPass> newRenderPass = Metal_Vulkan::CreateVulkanRenderPass(*_factory, layout);
			_cachedRenderPasses.insert(i, std::make_pair(hash, newRenderPass));
			return newRenderPass;
		}
	}

	VulkanRenderPassPool::VulkanRenderPassPool(ObjectFactory& factory)
	: _factory(&factory) {}
	VulkanRenderPassPool::VulkanRenderPassPool() {}
	VulkanRenderPassPool::~VulkanRenderPassPool() {}
    VulkanRenderPassPool::VulkanRenderPassPool(VulkanRenderPassPool&& moveFrom) never_throws
    {
        _cachedRenderPasses = std::move(moveFrom._cachedRenderPasses);
        _factory = moveFrom._factory;
        moveFrom._factory = nullptr;
    }
    VulkanRenderPassPool& VulkanRenderPassPool::operator=(VulkanRenderPassPool&& moveFrom) never_throws
    {
        _cachedRenderPasses = std::move(moveFrom._cachedRenderPasses);
        _factory = moveFrom._factory;
        moveFrom._factory = nullptr;
        return *this;
    }

    static void CopyHelper(
        BlitEncoder& encoder,
        IResource& dst, CopyPartial_Src src)
    {
        auto size = ByteCount(dst.GetDesc());
        assert((src._linearBufferRange.second - src._linearBufferRange.first) >= size);
        src._linearBufferRange.second = src._linearBufferRange.first + size;
        encoder.Copy(CopyPartial_Dest{dst}, src);
    }

	void DummyResources::CompleteInitialization(DeviceContext& devContext)
	{
		IResource* res[] = {
            _blankImage1DSrv.GetResource().get(), _blankImage2DSrv.GetResource().get(), _blankImage3DSrv.GetResource().get(), _blankImageCubeSrv.GetResource().get(),
            _blankImage1DArraySrv.GetResource().get(), _blankImage2DArraySrv.GetResource().get(),

            _blankImage1DUav.GetResource().get(), _blankImage2DUav.GetResource().get(), _blankImage3DUav.GetResource().get(), _blankImageCubeUav.GetResource().get(),
            _blankImage1DArrayUav.GetResource().get(), _blankImage2DArrayUav.GetResource().get(),

            _blankBufferUav.GetResource().get() }; 
		Metal_Vulkan::CompleteInitialization(devContext, MakeIteratorRange(res));

        if (_blankImageCubeArraySrv.GetResource()) {
            // not all drivers support cubemap arrays; can be disabled with feature flags
            IResource* res[] = {
                _blankImageCubeArraySrv.GetResource().get(),
                _blankImageCubeArrayUav.GetResource().get()
            };
            Metal_Vulkan::CompleteInitialization(devContext, MakeIteratorRange(res));
        }

        const unsigned maxDummySizeBytes = 6144;        // (odd size because of cubemaps)
        auto stagingSource = devContext.MapTemporaryStorage(maxDummySizeBytes, BindFlag::TransferSrc);
        std::memset(stagingSource.GetData().begin(), 0, stagingSource.GetData().size());
        auto encoder = devContext.BeginBlitEncoder();

        CopyHelper(encoder, *_blankImage1DSrv.GetResource(), stagingSource.AsCopySource());
        CopyHelper(encoder, *_blankImage2DSrv.GetResource(), stagingSource.AsCopySource());
        CopyHelper(encoder, *_blankImage3DSrv.GetResource(), stagingSource.AsCopySource());
        CopyHelper(encoder, *_blankImageCubeSrv.GetResource(), stagingSource.AsCopySource());
        CopyHelper(encoder, *_blankImage1DArraySrv.GetResource(), stagingSource.AsCopySource());
        CopyHelper(encoder, *_blankImage2DArraySrv.GetResource(), stagingSource.AsCopySource());

        CopyHelper(encoder, *_blankImage1DUav.GetResource(), stagingSource.AsCopySource());
        CopyHelper(encoder, *_blankImage2DUav.GetResource(), stagingSource.AsCopySource());
        CopyHelper(encoder, *_blankImage3DUav.GetResource(), stagingSource.AsCopySource());
        CopyHelper(encoder, *_blankImageCubeUav.GetResource(), stagingSource.AsCopySource());
        CopyHelper(encoder, *_blankImage1DArrayUav.GetResource(), stagingSource.AsCopySource());
        CopyHelper(encoder, *_blankImage2DArrayUav.GetResource(), stagingSource.AsCopySource());

        CopyHelper(encoder, *_blankBufferUav.GetResource(), stagingSource.AsCopySource());

        if (_blankImageCubeArraySrv.GetResource()) {
            CopyHelper(encoder, *_blankImageCubeArraySrv.GetResource(), stagingSource.AsCopySource());
            CopyHelper(encoder, *_blankImageCubeArrayUav.GetResource(), stagingSource.AsCopySource());
        }
	}

    DummyResources::DummyResources(ObjectFactory& factory)
    : _blankSampler(std::make_unique<SamplerState>(factory, SamplerDesc{FilterMode::Point, AddressMode::Clamp, AddressMode::Clamp}))
    {
        auto blankImage1D = Internal::CreateResource(factory, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::Plain1D(64, Format::R8G8B8A8_UNORM)), "DummyTexture1D");
        auto blankImage2D = Internal::CreateResource(factory, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::Plain2D(16, 16, Format::R8G8B8A8_UNORM)), "DummyTexture2D");
        auto blankImage3D = Internal::CreateResource(factory, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::Plain3D(4, 4, 4, Format::R8G8B8A8_UNORM)), "DummyTexture3D");
        auto blankCube = Internal::CreateResource(factory, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::PlainCube(16, 16, Format::R8G8B8A8_UNORM)), "DummyTextureCube");
        auto blankImage1DArray = Internal::CreateResource(factory, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::Plain1D(64, Format::R8G8B8A8_UNORM, 1, 1)), "DummyTexture1DArray");
        auto blankImage2DArray = Internal::CreateResource(factory, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::Plain2D(16, 16, Format::R8G8B8A8_UNORM, 1, 1)), "DummyTexture2DArray");

        auto blankImage1DUav = Internal::CreateResource(factory, CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, TextureDesc::Plain1D(64, Format::R8G8B8A8_UNORM)), "DummyTexture1DUAV");
        auto blankImage2DUav = Internal::CreateResource(factory, CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, TextureDesc::Plain2D(16, 16, Format::R8G8B8A8_UNORM)), "DummyTexture2DUAV");
        auto blankImage3DUav = Internal::CreateResource(factory, CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, TextureDesc::Plain3D(4, 4, 4, Format::R8G8B8A8_UNORM)), "DummyTexture3DUAV");
        auto blankCubeUav = Internal::CreateResource(factory, CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, TextureDesc::PlainCube(16, 16, Format::R8G8B8A8_UNORM)), "DummyTextureCubeUAV");
        auto blankImage1DArrayUav = Internal::CreateResource(factory, CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, TextureDesc::Plain1D(64, Format::R8G8B8A8_UNORM, 1, 1)), "DummyTexture1DArrayUAV");
        auto blankImage2DArrayUav = Internal::CreateResource(factory, CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, TextureDesc::Plain2D(16, 16, Format::R8G8B8A8_UNORM, 1, 1)), "DummyTexture2DArrayUAV");

        auto blankUAVBufferRes = Internal::CreateResource(factory, CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, LinearBufferDesc::Create(4096)), "DummyBufferUAV");
        _blankBuffer = Internal::CreateResource(factory, CreateDesc(BindFlag::ConstantBuffer|BindFlag::TransferDst, LinearBufferDesc::Create(4096)), "DummyUniformBuffer");

        _blankImage1DSrv = ResourceView{factory, blankImage1D};
        _blankImage2DSrv = ResourceView{factory, blankImage2D};
        _blankImage3DSrv = ResourceView{factory, blankImage3D};
        _blankImageCubeSrv = ResourceView{factory, blankCube};

        _blankImage1DArraySrv = ResourceView{factory, blankImage1DArray};
        _blankImage2DArraySrv = ResourceView{factory, blankImage2DArray};

        _blankImage1DUav = ResourceView{factory, blankImage1DUav};
        _blankImage2DUav = ResourceView{factory, blankImage2DUav};
        _blankImage3DUav = ResourceView{factory, blankImage3DUav};
        _blankImageCubeUav = ResourceView{factory, blankCubeUav};

        _blankImage1DArrayUav = ResourceView{factory, blankImage1DArrayUav};
        _blankImage2DArrayUav = ResourceView{factory, blankImage2DArrayUav};

        _blankBufferUav = ResourceView{factory, blankUAVBufferRes};

        if (factory.GetXLEFeatures()._cubemapArrays) {
            auto blankCubeArray = Internal::CreateResource(factory, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::PlainCube(16, 16, Format::R8G8B8A8_UNORM, 1, 6)), "DummyTextureCubeArray");
            auto blankCubeArrayUav = Internal::CreateResource(factory, CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, TextureDesc::PlainCube(16, 16, Format::R8G8B8A8_UNORM, 1, 6)), "DummyTextureCubeArrayUAV");
            _blankImageCubeArraySrv = ResourceView{factory, blankCubeArray};
            _blankImageCubeArrayUav = ResourceView{factory, blankCubeArrayUav};
        }
    }

    DummyResources::DummyResources() = default;
    DummyResources::~DummyResources() = default;
    DummyResources::DummyResources(DummyResources&& moveFrom) never_throws = default;
    DummyResources& DummyResources::operator=(DummyResources&& moveFrom) never_throws = default;

    GlobalPools::GlobalPools() {}
    GlobalPools::~GlobalPools() {}

}}