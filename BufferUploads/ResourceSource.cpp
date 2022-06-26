// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ResourceSource.h"
#include "IBufferUploads.h"
#include "Metrics.h"
#include "../RenderCore/ResourceUtils.h"
#include "../RenderCore/ResourceDesc.h"
#include "../RenderCore/IDevice.h"
#include "../OSServices/Log.h"
#include "../Utility/BitUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/Threading/LockFree.h"
#include <algorithm>

namespace BufferUploads
{
	namespace BindFlag = RenderCore::BindFlag;
	namespace AllocationRules = RenderCore::AllocationRules;

    // ~~~~~~~~~~~~ // ~~~~~~<   >~~~~~~ // ~~~~~~~~~~~~ //

    static unsigned RoundUpBufferSize(unsigned input)
    {
        unsigned log2 = IntegerLog2(input);
        if ((1<<log2)==input) {
            return input;
        }
        unsigned nextBit = 1<<(log2-1);
        unsigned nextBit2 = 1<<(log2-2);
        if (log2 >= 14 && !(input & nextBit2)) {
            return (input&((1<<log2)|nextBit))|nextBit2;
        }
        if (log2 >= 12 && !(input & nextBit)) {
            return (1<<log2)|nextBit;
        }
        return 1<<(log2+1);
    }

    // ~~~~~~~~~~~~ // ~~~~~~<   >~~~~~~ // ~~~~~~~~~~~~ //

        /////   R E S O U R C E S   P O O L   /////

    using DescHash = uint64_t;

    template <typename Desc> class ReusableResourcesPool : public IResourcePool, public std::enable_shared_from_this<ReusableResourcesPool<Desc>>
    {
    public:
        ResourceLocator     CreateResource(const Desc&, unsigned realSize, bool allowDeviceCreation);

        virtual void AddRef(
            uint64_t resourceMarker, IResource& resource, 
            size_t offset, size_t size) override;

        virtual void Release(
            uint64_t resourceMarker, std::shared_ptr<IResource>&& resource, 
            size_t offset, size_t size) override;

        std::vector<PoolMetrics>    CalculateMetrics() const;
        void                        Update(unsigned newFrameID);

        ReusableResourcesPool(RenderCore::IDevice& device, unsigned retainFrames = ~unsigned(0x0));
        ~ReusableResourcesPool();
    protected:
        std::shared_ptr<IResource> MakeReturnToPoolPointer(std::shared_ptr<IResource>&& resource, uint64_t poolMarker);
        void ReturnToPool(std::shared_ptr<IResource>&& resource, uint64_t resourceMarker);

        class PoolOfLikeResources
        {
        public:
            auto        AllocateResource(size_t realSize, bool allowDeviceCreation) -> std::shared_ptr<IResource>;
            const Desc& GetDesc() const { return _desc; }
            PoolMetrics CalculateMetrics() const;
            void        Update(unsigned newFrameID);
            void        ReturnToPool(std::shared_ptr<IResource>&& resource);

            PoolOfLikeResources(RenderCore::IDevice& underlyingDevice, const Desc&, unsigned retainFrames = ~unsigned(0x0));
            ~PoolOfLikeResources();
        private:
            struct Entry
            {
                std::shared_ptr<IResource>  _underlying;
                unsigned            _returnFrameID;
            };
            LockFreeFixedSizeQueue<Entry, 512> _allocableResources;
            Desc                        _desc;
            mutable size_t              _peakSize;
            mutable std::atomic<unsigned>  _recentDeviceCreateCount, _recentPoolCreateCount, _recentReleaseCount;
            std::atomic<size_t>         _totalCreateSize, _totalCreateCount, _totalRealSize;
            unsigned                    _currentFrameID;
            unsigned                    _retainFrames;
			RenderCore::IDevice*        _underlyingDevice;
        };

            //
            //          >   Manual hash table; sorted vector with a     <
            //          >   pointer to the payload                      <
            //
        typedef std::pair<DescHash, std::shared_ptr<PoolOfLikeResources> > HashTableEntry;
        typedef std::vector<HashTableEntry> HashTable;
        HashTable                       _hashTables[2];
        volatile std::atomic<unsigned>  _readerCount[2];
        unsigned                        _hashTableIndex;
        mutable Threading::Mutex        _writerLock;
        unsigned                        _retainFrames;
		RenderCore::IDevice*            _underlyingDevice;

        struct CompareFirst
        {
            bool operator()(const HashTableEntry& lhs, const HashTableEntry& rhs) { return lhs.first < rhs.first; }
            bool operator()(const HashTableEntry& lhs, DescHash rhs) { return lhs.first < rhs; }
            bool operator()(DescHash lhs, const HashTableEntry& rhs) { return lhs < rhs.first; }
        };
    };

    #define tdesc template<typename Desc>

    tdesc auto ReusableResourcesPool<Desc>::PoolOfLikeResources::AllocateResource(size_t realSize, bool allowDeviceCreation) -> std::shared_ptr<IResource>
        {
            Entry* front = NULL;
            if (_allocableResources.try_front(front)) {
                ++_recentPoolCreateCount;
                std::shared_ptr<IResource> result = std::move(front->_underlying);
                _allocableResources.pop();
                return result;
            } else if (allowDeviceCreation) {
                auto result = _underlyingDevice->CreateResource(_desc);
                if (result) {
                    _totalRealSize += realSize;
                    _totalCreateSize += RenderCore::ByteCount(_desc);
                    ++_recentDeviceCreateCount;
                    ++_totalCreateCount;
                }
                return result;
            }
            return nullptr;
        }

    tdesc void ReusableResourcesPool<Desc>::PoolOfLikeResources::Update(unsigned newFrameID)
        {
            _currentFrameID = newFrameID;
                // pop off any resources that have lived here for too long
            if (_retainFrames != ~unsigned(0x0)) {
                const unsigned minToKeep = 4;
                while (_allocableResources.size() > minToKeep) {
                    Entry* front = NULL;
                    if (!_allocableResources.try_front(front) || (newFrameID - front->_returnFrameID) < _retainFrames) {
                        break;
                    }
                    _allocableResources.pop();
                }
            }
        }

    tdesc void ReusableResourcesPool<Desc>::PoolOfLikeResources::ReturnToPool(std::shared_ptr<IResource>&& resource)
        {
            Entry newEntry;
            newEntry._underlying = std::move(resource);
            newEntry._returnFrameID = _currentFrameID;
            _allocableResources.push(newEntry);
            ++_recentReleaseCount;
        }

    tdesc ReusableResourcesPool<Desc>::PoolOfLikeResources::PoolOfLikeResources(
			RenderCore::IDevice& underlyingDevice, const Desc& desc, unsigned retainFrames) : _desc(desc)
        {
            _peakSize = 0;
            _recentDeviceCreateCount = _recentPoolCreateCount = _recentReleaseCount = 0;
            _totalCreateSize = _totalCreateCount = _totalRealSize = 0;
            _currentFrameID = 0;
            _retainFrames = retainFrames;
            _underlyingDevice = &underlyingDevice;
        }

    tdesc ReusableResourcesPool<Desc>::PoolOfLikeResources::~PoolOfLikeResources()
        {
        }

    tdesc PoolMetrics    ReusableResourcesPool<Desc>::PoolOfLikeResources::CalculateMetrics() const
    {
        PoolMetrics result;
        result._desc = _desc;
        {
            //ScopedLock(_lock);
            result._currentSize = (unsigned)_allocableResources.size();
        }
        result._peakSize = _peakSize = std::max(_peakSize, result._currentSize);
        result._topMostAge               = 0;
        result._recentDeviceCreateCount  = _recentDeviceCreateCount.exchange(0);
        result._recentPoolCreateCount    = _recentPoolCreateCount.exchange(0);
        result._recentReleaseCount       = _recentReleaseCount.exchange(0);
        result._totalRealSize            = _totalRealSize;
        result._totalCreateSize          = _totalCreateSize;
        result._totalCreateCount         = _totalCreateCount;
        return result;
    }

    tdesc ReusableResourcesPool<Desc>::ReusableResourcesPool(RenderCore::IDevice& device, unsigned retainFrames) 
	: _hashTableIndex(0), _retainFrames(retainFrames), _underlyingDevice(&device)
    {
        _readerCount[0] = _readerCount[1] = 0;
    }
    
    tdesc ReusableResourcesPool<Desc>::~ReusableResourcesPool() {}

    tdesc ResourceLocator   ReusableResourcesPool<Desc>::CreateResource(
            const Desc& desc, unsigned realSize, bool allowDeviceCreation)
        {
            DescHash hashValue = desc.CalculateHash();
            {
                unsigned hashTableIndex = _hashTableIndex;
                ++_readerCount[hashTableIndex];
                HashTable& hashTable = _hashTables[hashTableIndex];
                auto entry = std::lower_bound(hashTable.begin(), hashTable.end(), hashValue, CompareFirst());
                if (entry != hashTable.end() && entry->first == hashValue) {
                    if (desc._type == ResourceDesc::Type::Texture) {
                        assert(desc._textureDesc._width == entry->second->GetDesc()._textureDesc._width);
                        assert(desc._textureDesc._height == entry->second->GetDesc()._textureDesc._height);
                        assert(desc._textureDesc._mipCount == entry->second->GetDesc()._textureDesc._mipCount);
                        assert(desc._textureDesc._format == entry->second->GetDesc()._textureDesc._format);
                    }
                    auto newResource = entry->second.get()->AllocateResource(realSize, allowDeviceCreation);
                    --_readerCount[hashTableIndex];
                    return MakeReturnToPoolPointer(std::move(newResource), hashValue);
                }
                --_readerCount[hashTableIndex];
            }

            if (!allowDeviceCreation)
                return {};

                //
                //              -=*=- Insert a new hash table entry for this type of resource -=*=-
                //
            {
                ScopedLock(_writerLock);

                    //
                    //      Doubled buffered writing scheme... We don't change the hash table very often, so let's optimise
                    //      for when the hash table isn't modified. 
                    //      Note there might be a problem here if we get a reader that uses a hash table while another thread
                    //      has enough table to modify the hash tables twice.
                    //
                unsigned oldHashTableIndex = _hashTableIndex;
                unsigned nextHashTableIndex = (oldHashTableIndex+1)%dimof(_hashTables);
                HashTable& newHashTable = _hashTables[nextHashTableIndex];
                newHashTable = _hashTables[oldHashTableIndex];
                auto entry = std::lower_bound(newHashTable.begin(), newHashTable.end(), hashValue, CompareFirst());

                HashTableEntry newEntry;
                newEntry.first = hashValue;
                newEntry.second = std::make_shared<PoolOfLikeResources>(
                    std::ref(*_underlyingDevice), desc, _retainFrames);
                auto newIterator = newHashTable.insert(entry, newEntry);
                _hashTableIndex = nextHashTableIndex;

                auto newResource = newIterator->second->AllocateResource(realSize, true);

                    //  We should wait until there are no more readers on the old hash table before we give up the "_writerLock" mutex. This is because
                    //  Another thread could create a new entry immediately after, and start writing over the old hash table while readers are still
                    //  using it (even though this is very rare, it still does happen). But if we wait until that hash table is no longer being used, we're safe 
                    //  to release the _writerLock. Unfortunately it requires an extra interlocked Increment/Decrement in every read operation...
                while (_readerCount[oldHashTableIndex]) {}

                return MakeReturnToPoolPointer(std::move(newResource), hashValue);
            }
        }

    tdesc void        ReusableResourcesPool<Desc>::AddRef(
            uint64_t resourceMarker, IResource& resource, 
            size_t offset, size_t size)
        {
            // we don't have to do anything in this case
        }

    tdesc void        ReusableResourcesPool<Desc>::Release(
            uint64_t resourceMarker, std::shared_ptr<IResource>&& resource, 
            size_t offset, size_t size)
        {}

    tdesc std::shared_ptr<IResource> ReusableResourcesPool<Desc>::MakeReturnToPoolPointer(std::shared_ptr<IResource>&& resource, uint64_t poolMarker)
    {
        // We're going to create a second std::shared_ptr<> that points to the same resource,
        // but it's destruction routine will return it to the pool.
        // The destruction routine also captures the original shared pointer!
        auto weakThisI = std::enable_shared_from_this<ReusableResourcesPool<Desc>>::weak_from_this();
        auto* res = resource.get();
        return std::shared_ptr<IResource>(
            res,
            [originalPtr = std::move(resource), poolMarker, weakThis = std::move(weakThisI)](IResource*) mutable {
                auto strongThis = weakThis.lock();
                if (strongThis)
                    strongThis->ReturnToPool(std::move(originalPtr), poolMarker);
            });
    }
    
    tdesc void        ReusableResourcesPool<Desc>::ReturnToPool(std::shared_ptr<IResource>&& resource, uint64_t resourceMarker)
        {
            unsigned hashTableIndex = _hashTableIndex;
            ++_readerCount[hashTableIndex];
            HashTable& hashTable = _hashTables[hashTableIndex];
            auto entry = std::lower_bound(hashTable.begin(), hashTable.end(), resourceMarker, CompareFirst());
            if (entry != hashTable.end() && entry->first == resourceMarker) {
                entry->second->ReturnToPool(std::move(resource));
                --_readerCount[hashTableIndex];
            } else {
                --_readerCount[hashTableIndex];
            }
        }

    tdesc void        ReusableResourcesPool<Desc>::Update(unsigned newFrameID)
        {
            unsigned hashTableIndex = _hashTableIndex;
            ++_readerCount[hashTableIndex];
            HashTable& hashTable = _hashTables[hashTableIndex];
            for (auto i=hashTable.begin(); i!=hashTable.end(); ++i) {
                i->second->Update(newFrameID);
            }
            --_readerCount[hashTableIndex];
        }

    tdesc std::vector<PoolMetrics>        ReusableResourcesPool<Desc>::CalculateMetrics() const
    {
        ScopedLock(_writerLock);
        const HashTable& hashTable = _hashTables[_hashTableIndex];
        std::vector<PoolMetrics> result;
        result.reserve(hashTable.size());
        for (auto i = hashTable.begin(); i!= hashTable.end(); ++i) {
            result.push_back(i->second->CalculateMetrics());
        }
        return result;
    }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    std::shared_ptr<IResource> ResourceLocator::AsIndependentResource() const
    {
        return (!_managedByPool && IsWholeResource()) ? _resource : nullptr;
    }

    RenderCore::VertexBufferView ResourceLocator::CreateVertexBufferView() const
    {
        return RenderCore::VertexBufferView {
            _resource,
            (_interiorOffset != ~size_t(0)) ? unsigned(_interiorOffset) : 0u
        };
    }

    RenderCore::IndexBufferView ResourceLocator::CreateIndexBufferView(RenderCore::Format indexFormat) const
    {
        return RenderCore::IndexBufferView { _resource, indexFormat, (_interiorOffset != ~size_t(0)) ? unsigned(_interiorOffset) : 0u };
    }

    RenderCore::ConstantBufferView ResourceLocator::CreateConstantBufferView() const
    {
        if (_interiorOffset != ~size_t(0)) {
            return RenderCore::ConstantBufferView { _resource, unsigned(_interiorOffset), unsigned(_interiorOffset + _interiorSize) };
        } else {
            return RenderCore::ConstantBufferView { _resource };
        }
    }

    std::shared_ptr<RenderCore::IResourceView> ResourceLocator::CreateTextureView(BindFlag::Enum usage, const RenderCore::TextureViewDesc& window) const
    {
        if (!IsWholeResource() || _managedByPool)
            Throw(std::runtime_error("Cannot create a texture view from a partial resource locator"));
        return _resource->CreateTextureView(usage, window);
    }

    std::shared_ptr<RenderCore::IResourceView> ResourceLocator::CreateBufferView(BindFlag::Enum usage, unsigned rangeOffset, unsigned rangeSize) const
    {
        return _resource->CreateBufferView(usage, rangeOffset + ((_interiorOffset != ~size_t(0)) ? unsigned(_interiorOffset) : 0u), rangeSize);
    }

    bool ResourceLocator::IsWholeResource() const
    {
        return _interiorOffset == ~size_t(0) && _interiorSize == ~size_t(0);
    }

    ResourceLocator::ResourceLocator(
        std::shared_ptr<IResource> independentResource)
    : _resource(std::move(independentResource))
    {
        _interiorOffset = _interiorSize = ~size_t(0);
        _poolMarker = ~0ull;
        _managedByPool = false;
        _completionCommandList = CommandListID_Invalid;
    }
    ResourceLocator::ResourceLocator(
        std::shared_ptr<IResource> containingResource,
        size_t interiorOffset, size_t interiorSize,
        std::weak_ptr<IResourcePool> pool, uint64_t poolMarker,
        bool initialReferenceAlreadyTaken,
        CommandListID completionCommandList)
    : _resource(std::move(containingResource))
    , _pool(std::move(pool))
    {
        _interiorOffset = interiorOffset;
        _interiorSize = interiorSize;
        _poolMarker = poolMarker;
        _managedByPool = true;
        _completionCommandList = completionCommandList;

        if (!initialReferenceAlreadyTaken) {
            auto strongPool = _pool.lock();
            if (strongPool && _resource)
                strongPool->AddRef(_poolMarker, *_resource, _interiorOffset, _interiorSize);
        }
    }

    ResourceLocator::ResourceLocator(
        std::shared_ptr<IResource> containingResource,
        size_t interiorOffset, size_t interiorSize,
        CommandListID completionCommandList)
    : _resource(std::move(containingResource))
    {
        _interiorOffset = interiorOffset;
        _interiorSize = interiorSize;
        _poolMarker = ~0ull;
        _managedByPool = false;
        _completionCommandList = completionCommandList;
    }

    ResourceLocator::ResourceLocator() {}
    ResourceLocator::~ResourceLocator() 
    {
        auto pool = _pool.lock();
        if (pool)
            pool->Release(_poolMarker, std::move(_resource), _interiorOffset, _interiorSize);
    }

    ResourceLocator::ResourceLocator(
        ResourceLocator&& moveFrom,
        CommandListID completionCommandList)
    : _resource(std::move(moveFrom._resource))
    , _interiorOffset(moveFrom._interiorOffset)
    , _interiorSize(moveFrom._interiorSize)
    , _pool(std::move(moveFrom._pool))
    , _poolMarker(moveFrom._poolMarker)
    , _managedByPool(moveFrom._managedByPool)
    , _completionCommandList(completionCommandList)
    {
        moveFrom._interiorOffset = moveFrom._interiorSize = ~size_t(0);
        moveFrom._poolMarker = ~0ull;
        moveFrom._managedByPool = false;
        moveFrom._completionCommandList = CommandListID_Invalid;
    }

    ResourceLocator::ResourceLocator(ResourceLocator&& moveFrom) never_throws
    : _resource(std::move(moveFrom._resource))
    , _interiorOffset(moveFrom._interiorOffset)
    , _interiorSize(moveFrom._interiorSize)
    , _pool(std::move(moveFrom._pool))
    , _poolMarker(moveFrom._poolMarker)
    , _managedByPool(moveFrom._managedByPool)
    , _completionCommandList(moveFrom._completionCommandList)
    {
        moveFrom._interiorOffset = moveFrom._interiorSize = ~size_t(0);
        moveFrom._poolMarker = ~0ull;
        moveFrom._managedByPool = false;
        moveFrom._completionCommandList = CommandListID_Invalid;
    }

    ResourceLocator& ResourceLocator::operator=(ResourceLocator&& moveFrom) never_throws
    {
        if (&moveFrom == this) return *this;

        if (_managedByPool) {
            auto pool = _pool.lock();
            if (pool && _resource)
                pool->Release(_poolMarker, std::move(_resource), _interiorOffset, _interiorSize);
        }

        _resource = std::move(moveFrom._resource);
        _interiorOffset = moveFrom._interiorOffset;
        _interiorSize = moveFrom._interiorSize;
        _pool = std::move(moveFrom._pool);
        _poolMarker = moveFrom._poolMarker;
        _managedByPool = moveFrom._managedByPool;
        _completionCommandList = moveFrom._completionCommandList;
        moveFrom._interiorOffset = moveFrom._interiorSize = ~size_t(0);
        moveFrom._poolMarker = ~0ull;
        moveFrom._managedByPool = false;
        moveFrom._completionCommandList = CommandListID_Invalid;
        return *this;
    }

    ResourceLocator::ResourceLocator(const ResourceLocator& copyFrom)
    : _resource(copyFrom._resource)
    , _interiorOffset(copyFrom._interiorOffset)
    , _interiorSize(copyFrom._interiorSize)
    , _pool(copyFrom._pool)
    , _poolMarker(copyFrom._poolMarker)
    , _managedByPool(copyFrom._managedByPool)
    , _completionCommandList(copyFrom._completionCommandList)
    {
        if (_managedByPool) {
            auto pool = _pool.lock();
            if (pool && _resource)
                pool->AddRef(_poolMarker, *_resource, _interiorOffset, _interiorSize);
        }
    }

    ResourceLocator& ResourceLocator::operator=(const ResourceLocator& copyFrom)
    {
        if (&copyFrom == this) return *this;

        if (_managedByPool) {
            auto pool = _pool.lock();
            if (pool && _resource)
                pool->Release(_poolMarker, std::move(_resource), _interiorOffset, _interiorSize);
        }

        _resource = copyFrom._resource;
        _interiorOffset = copyFrom._interiorOffset;
        _interiorSize = copyFrom._interiorSize;
        _pool = copyFrom._pool;
        _poolMarker = copyFrom._poolMarker;
        _managedByPool = copyFrom._managedByPool;
        _completionCommandList = copyFrom._completionCommandList;

        if (_managedByPool) {
            auto pool = _pool.lock();
            if (pool && _resource)
                pool->AddRef(_poolMarker, *_resource, _interiorOffset, _interiorSize);
        }
        return *this;
    }

    ResourceLocator ResourceLocator::MakeSubLocator(size_t offset, size_t size) const
    {
        if (_managedByPool) {
            if (IsWholeResource()) {
                return ResourceLocator {
                    _resource,
                    offset, size,
                    _pool, _poolMarker,
                    false, _completionCommandList };
            } else {
                return ResourceLocator {
                    _resource,
                    _interiorOffset + offset, size,
                    _pool, _poolMarker,
                    false, _completionCommandList };
            }
        } else {
            if (IsWholeResource()) {
                return ResourceLocator {
                    _resource,
                    offset, size,
                    _completionCommandList };
            } else {
                return ResourceLocator {
                    _resource,
                    _interiorOffset + offset, size,
                    _completionCommandList };
            }
        }
    }

    IResourcePool::~IResourcePool() {}

}
