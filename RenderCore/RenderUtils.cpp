// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RenderUtils.h"
#include "ResourceDesc.h"
#include "Types.h"
#include "StateDesc.h"
#include "Format.h"

#include "IDevice.h"
#include "IThreadContext.h"
#include "IAnnotator.h"

#include "../ConsoleRig/GlobalServices.h"
#include "../OSServices/Log.h"
#include "../ConsoleRig/AttachablePtr.h"
#include "../Utility/StringFormat.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/Threading/ThreadingUtils.h"
#include "../Utility/Threading/ThreadLocalPtr.h"
#include "../Utility/ArithmeticUtils.h"
#include "../Utility/BitUtils.h"
#include <cctype>

namespace RenderCore
{
    const TextureViewDesc::SubResourceRange TextureViewDesc::All = SubResourceRange{0, Unlimited};

    namespace Exceptions
    {
        GenericFailure::GenericFailure(const char what[]) : ::Exceptions::BasicLabel(what) {}

        AllocationFailure::AllocationFailure(const char what[]) 
        : GenericFailure(what) 
        {}
    }

    class SubFrameHeap_Heap
    {
    public:
        std::vector<uint8_t>    _data;
        uint8_t*                _writeMarker = nullptr;
        unsigned                _resetId = 0;

        SubFrameHeap_Heap() {}
        SubFrameHeap_Heap(SubFrameHeap_Heap&&moveFrom) = default;
        SubFrameHeap_Heap& operator=(SubFrameHeap_Heap&&moveFrom) = default;
    };

#if !FEATURE_THREAD_LOCAL_KEYWORD
    thread_local_ptr<SubFrameHeap_Heap>  s_producerHeap;
#else
    static thread_local std::shared_ptr<SubFrameHeap_Heap> s_producerHeap;
#endif

    class SubFrameHeap
    {
    public:
        using ResetId = unsigned;

        static ResetId InitialResetId()
        {
            std::hash<std::thread::id> hasher;
            return (ResetId)hasher(std::this_thread::get_id());
        }

#if !FEATURE_THREAD_LOCAL_KEYWORD
        SubFrameHeap_Heap* GetThreadLocalProducerHeap() const
        {
            return s_producerHeap.get();
        }

        SubFrameHeap_Heap& GetOrCreateThreadLocalProducerHeap()
        {
            auto* producerHeap = s_producerHeap.get();
            if (!producerHeap) {
                s_producerHeap.allocate();
                producerHeap = s_producerHeap.get();
                producerHeap->_data = std::vector<uint8_t>(256*1024, 0);
                producerHeap->_writeMarker = producerHeap->_data.data();
                producerHeap->_resetId = InitialResetId();

#if defined(_DEBUG)
                {
                    ScopedLock(_swapMutex);
                    _currentProducerHeapResetIds.push_back(producerHeap->_resetId);
                }
#endif
            }
            return *producerHeap;
        }
#else

        SubFrameHeap_Heap* GetThreadLocalProducerHeap() const
        {
            return s_producerHeap.get();
        }

        SubFrameHeap_Heap& GetOrCreateThreadLocalProducerHeap()
        {
            auto* producerHeap = s_producerHeap.get();
            if (!producerHeap) {
                s_producerHeap = std::make_shared<SubFrameHeap_Heap>();
                producerHeap = s_producerHeap.get();
                producerHeap->_data = std::vector<uint8_t>(256*1024, 0);
                producerHeap->_writeMarker = producerHeap->_data.data();
                producerHeap->_resetId = InitialResetId();
#if defined(_DEBUG)
                {
                    ScopedLock(_swapMutex);
                    _currentProducerHeapResetIds.push_back(producerHeap->_resetId);
                }
#endif
            }
            return *producerHeap;
        }
#endif

        void OnConsumerFrameBarrier(unsigned producerBarrierId)
        {
            ScopedLock(_swapMutex);
            while (!_pendingConsumerHeaps.empty() && _pendingConsumerHeaps.begin()->_resetId <= producerBarrierId) {
                if (_reusableHeaps.size() < 5) {
                    SubFrameHeap_Heap temp;
                    std::swap(temp, *_pendingConsumerHeaps.begin());
                    _reusableHeaps.emplace_back(std::move(temp));
                }
                _pendingConsumerHeaps.erase(_pendingConsumerHeaps.begin());
            }
        }

        ResetId OnProducerFrameBarrier()
        {
            #if defined(_DEBUG)
                // Only one thread can call this function, otherwise the "resetId"s from different
                // source producer threads cannot be scheduled relatively to each other
                assert(Threading::CurrentThreadId() == _mainProducerThread);
            #endif

            auto* producerHeap = GetThreadLocalProducerHeap();

            unsigned result = 0;
            if (producerHeap) {
                ScopedLock(_swapMutex);
                // Try to swap the main buffer into the secondary / waiting for consumer buffer
                result = producerHeap->_resetId;

                SubFrameHeap_Heap nextMainHeap;
                if (!_reusableHeaps.empty()) {
                    std::swap(nextMainHeap, *_reusableHeaps.begin());
                    _reusableHeaps.erase(_reusableHeaps.begin());
                } else {
                    nextMainHeap._data = std::vector<uint8_t>(256*1024, 0);
                }
                nextMainHeap._writeMarker = nextMainHeap._data.data();
                nextMainHeap._resetId = result+1;

                std::swap(*producerHeap, nextMainHeap);

#if defined(_DEBUG)
                auto it = std::find(_currentProducerHeapResetIds.begin(), _currentProducerHeapResetIds.end(), nextMainHeap._resetId);
                if (it != _currentProducerHeapResetIds.end())
                    _currentProducerHeapResetIds.erase(it);
                _currentProducerHeapResetIds.push_back(producerHeap->_resetId);
#endif

                _pendingConsumerHeaps.emplace_back(std::move(nextMainHeap));

                if (_pendingConsumerHeaps.size() >= 16) {
                    Log(Warning) << "Very high number of pending consumer heaps queued. This is an indication that the foreground thread is getting very far ahead, or that the consumer thread is not catching up correctly. This message is sometimes an indication of a serious bug, or at the very least a memory hog." << std::endl;
                }
            }

            _logMsg = true;
            return result;
        }

        void OnProducerAndConsumerFrameBarrier()
        {
            // Don't even need a lock for this (assuming OnProducerFrameBarrier will not be called
            // synchronously)
            auto* producerHeap = GetThreadLocalProducerHeap();
            if (producerHeap) {
                producerHeap->_writeMarker = producerHeap->_data.data();
                auto oldId = producerHeap->_resetId;
                ++producerHeap->_resetId;

#if defined(_DEBUG)
                {
                    ScopedLock(_swapMutex);
                    auto it = std::find(_currentProducerHeapResetIds.begin(), _currentProducerHeapResetIds.end(), oldId);
                    if (it != _currentProducerHeapResetIds.end())
                        _currentProducerHeapResetIds.erase(it);
                    _currentProducerHeapResetIds.push_back(producerHeap->_resetId);
                }
#endif
            }
            _logMsg = true;
        }

#if defined(_DEBUG)
        bool IsValidResetId(ResetId resetId) const
        {
            auto* producerHeap = GetThreadLocalProducerHeap();
            if (producerHeap && resetId == producerHeap->_resetId)
                return true;

            ScopedLock(_swapMutex);
            for (const auto&pendingConsumer:_pendingConsumerHeaps)
                if (pendingConsumer._resetId == resetId)
                    return true;

            for (const auto &currentProducerResetId : _currentProducerHeapResetIds) {
                if (currentProducerResetId == resetId) {
                    return true;
                }
            }
            return false;
        }
#endif
        
        std::pair<void*, ResetId> Allocate(size_t size)
        {
            auto& producerHeap = GetOrCreateThreadLocalProducerHeap();
            if (PtrAdd(producerHeap._writeMarker, size) > AsPointer(producerHeap._data.end())) {
                if (_logMsg) {
                    Log(Warning) << "Overran subframe heap with allocation of size (" << size << ")" << std::endl;
                    _logMsg = false;
                }
                return {nullptr, 0};
            }
            
            void* result = producerHeap._writeMarker;
            producerHeap._writeMarker += size;
            return {result, producerHeap._resetId};
        }

        std::pair<void*, ResetId> AllocateAligned(size_t size, size_t alignment)
        {
            auto& producerHeap = GetOrCreateThreadLocalProducerHeap();
            auto alignOffset = size_t(producerHeap._writeMarker) % alignment;
            if (alignOffset != 0) {
                if (PtrAdd(producerHeap._writeMarker, alignment-alignOffset+size) > AsPointer(producerHeap._data.end())) {
                    if (_logMsg) {
                        Log(Warning) << "Overran subframe heap with aligned allocation of size (" << size << ") alignment (" << alignment << ")" << std::endl;
                        _logMsg = false;
                    }
                    return {nullptr, 0};
                }

                producerHeap._writeMarker = PtrAdd(producerHeap._writeMarker, alignment-alignOffset);
            }

            // now that we've queued "_writeMarker" to be aligned, we can just go ahead and allocate
            // the next block
            auto result = Allocate(size);
            assert(!result.first || (size_t(result.first)%alignment)==0);
            return result;
        }
        
        SubFrameHeap()
        {
            _pendingConsumerHeaps.reserve(5);
            _reusableHeaps.reserve(5);

            _logMsg = true;

            #if defined(_DEBUG)
                _mainProducerThread = Threading::CurrentThreadId();
            #endif
        }
        
        ~SubFrameHeap() {}
    private:
        std::vector<SubFrameHeap_Heap>       _pendingConsumerHeaps;
        std::vector<SubFrameHeap_Heap>       _reusableHeaps;

        bool                    _logMsg;

        mutable Threading::Mutex    _swapMutex;
        #if defined(_DEBUG)
            Threading::ThreadId         _mainProducerThread;
            std::vector<unsigned>       _currentProducerHeapResetIds;
        #endif
    };
    
    static SubFrameHeap& GetSubFrameHeap();

    SharedPkt::SharedPkt(MiniHeap::Allocation alloc, size_t size, unsigned subframeHeapReset)
    : Allocation(alloc), _size(size), _calculatedHash(0)
    #if defined(_DEBUG)
        , _subframeHeapReset(subframeHeapReset)
    #endif
    {
            // Careful --   first initialization never addrefs!
            //              this is because allocations will return an 
            //              object with reference count of 1
    }

    SharedPkt::SharedPkt(const SharedPkt& cloneFrom)
    : Allocation(cloneFrom), _size(cloneFrom._size), _calculatedHash(cloneFrom._calculatedHash)
    #if defined(_DEBUG)
        , _subframeHeapReset(cloneFrom._subframeHeapReset)
    #endif
    {
        if (_allocation != nullptr && _marker != ~0u) {
            GetHeap().AddRef(*this);
        }
    }

    SharedPkt::~SharedPkt()
    {
        if (_marker == ~0u) {
            // subframe allocation
        } else if (_allocation != nullptr) {
            GetHeap().Release(*this);
        }
    }

    void SharedPkt::CalculateHash()
    {
        _calculatedHash = Hash64(begin(), end());
    }

    #if defined(_DEBUG)
        void SharedPkt::CheckSubframeHeapReset() const
        {
            auto& subframeHeap = GetSubFrameHeap();
            assert(_subframeHeapReset == 0 || subframeHeap.IsValidResetId(_subframeHeapReset));
        }
    #endif

    SharedPkt MakeSharedPktSize(size_t size)
    {
        auto& heap = SharedPkt::GetHeap();
        return SharedPkt(heap.Allocate((unsigned)size), size);
    }

    SharedPkt MakeSharedPkt(const void* begin, const void* end)
    {
        auto& heap = SharedPkt::GetHeap();
        auto size = size_t(ptrdiff_t(end) - ptrdiff_t(begin));
        SharedPkt pkt(heap.Allocate((unsigned)size), size);
        if (pkt.begin()) {
            XlCopyMemory(pkt.begin(), begin, size);
        }
        return pkt;
    }
    
    SharedPkt MakeSubFramePktSize(size_t size)
    {
        auto allocation = GetSubFrameHeap().Allocate(size);
        if (!allocation.first)
            return MakeSharedPktSize(size);   // fall back to (slower) shared pkt
        assert(allocation.second);
        return SharedPkt({allocation.first, ~0u}, size, allocation.second);
    }

    SharedPkt MakeSubFramePktSizeAligned(size_t size, size_t alignment)
    {
        auto allocation = GetSubFrameHeap().AllocateAligned(size, alignment);
        if (!allocation.first) {
            auto& heap = SharedPkt::GetHeap();
            return SharedPkt(heap.AllocateAligned((unsigned)size, (unsigned)alignment), size);
        };
        assert(allocation.second);
        return SharedPkt({allocation.first, ~0u}, size, allocation.second);
    }
    
    SharedPkt MakeSubFramePkt(const void* begin, const void* end)
    {
        auto size = size_t(ptrdiff_t(end) - ptrdiff_t(begin));
        auto allocation = GetSubFrameHeap().Allocate(size);
        if (!allocation.first)
            return MakeSharedPkt(begin, end);   // fall back to (slower) shared pkt
        SharedPkt pkt({allocation.first, ~0u}, size, allocation.second);
        if (pkt.begin()) {
            XlCopyMemory(pkt.begin(), begin, size);
        }
        return pkt;
    }
    
    void SubFrameHeap_ConsumerFrameBarrier(unsigned producerBarrierId)
    {
        GetSubFrameHeap().OnConsumerFrameBarrier(producerBarrierId);
    }

    unsigned SubFrameHeap_ProducerFrameBarrier()
    {
        return GetSubFrameHeap().OnProducerFrameBarrier();
    }

    void SubFrameHeap_ProducerAndConsumerFrameBarrier()
    {
        GetSubFrameHeap().OnProducerAndConsumerFrameBarrier();
    }

    void* SubFrameHeap_Allocate(size_t size)
    {
        return GetSubFrameHeap().Allocate(size).first;
    }

    static SubFrameHeap& GetSubFrameHeap()
    {
        static SubFrameHeap* MainSubFrameHeap = nullptr;
        if (!MainSubFrameHeap) {
                // initialize our global from the global services
                // this will ensure that the same object will be used across multiple DLLs
            static auto Fn_GetStorage = ConstHash64<'gets', 'ubfr', 'ameh', 'eap'>::Value;
            auto& services = ConsoleRig::CrossModule::GetInstance()._services;
            if (!services.Has<SubFrameHeap*()>(Fn_GetStorage)) {
                SubFrameHeap** storedPtr = &MainSubFrameHeap;
                auto newSubFrameHeap = std::shared_ptr<SubFrameHeap>(
                    new SubFrameHeap,
                    [storedPtr](SubFrameHeap* heap) { assert(heap == *storedPtr); delete heap; *storedPtr = nullptr; });
                services.Add(Fn_GetStorage,
                    [newSubFrameHeap]() { return newSubFrameHeap.get(); });
                MainSubFrameHeap = newSubFrameHeap.get();
            } else {
                MainSubFrameHeap = services.Call<SubFrameHeap*>(Fn_GetStorage);
            }
        }

        return *MainSubFrameHeap;
    }

    MiniHeap& SharedPkt::GetHeap()
    {
        static MiniHeap* MainHeap = nullptr;
        if (!MainHeap) {
                // initialize our global from the global services
                // this will ensure that the same object will be used across multiple DLLs
            static auto Fn_GetStorage = ConstHash64<'gets', 'hare', 'dpkt', 'heap'>::Value;
            auto& services = ConsoleRig::CrossModule::GetInstance()._services;
            if (!services.Has<MiniHeap*()>(Fn_GetStorage)) {
                MiniHeap** storedPtr = &MainHeap;
                auto newMiniHeap = std::shared_ptr<MiniHeap>(
                    new MiniHeap,
                    [storedPtr](MiniHeap* heap) { assert(heap == *storedPtr); delete heap; *storedPtr = nullptr; });
                services.Add(Fn_GetStorage,
                    [newMiniHeap]() { return newMiniHeap.get(); });
                MainHeap = newMiniHeap.get();
            } else {
                MainHeap = services.Call<MiniHeap*>(Fn_GetStorage);
            }
        }

        return *MainHeap;
    }

    uint64_t LinearBufferDesc::CalculateHash() const
    {
        return (uint64_t(_structureByteSize) << 32ull) | uint64_t(_sizeInBytes);
    }

    uint64_t TextureDesc::CalculateHash() const
    {
        assert((unsigned(_dimensionality) & ((1<<4)-1)) == unsigned(_dimensionality));
        assert((_mipCount & ((1<<8)-1)) == _mipCount);
        assert((_arrayCount & ((1<<16)-1)) == _arrayCount);
        assert((_samples._sampleCount & ((1<<8)-1)) == _samples._sampleCount);
        assert((_samples._samplingQuality & ((1<<8)-1)) == _samples._samplingQuality);
        assert((unsigned(_format) & ((1<<8)-1)) == unsigned(_format));

        if (IsPowerOfTwo(_width) && IsPowerOfTwo(_height) && _depth == 1
            && _width >= 64 && _width <= 16384
            && _height >= 64 && _height <= 16384) {
            uint64_t result = 0x1;  // set the bottom "type" bit
            result |= (unsigned(_dimensionality) & ((1<<4)-1)) << 1;
            result |= _arrayCount << 5;
            uint64_t widthPower = IntegerLog2(_width)-6;
            uint64_t heightPower = IntegerLog2(_height)-6;
            result |= widthPower << 21ull;
            result |= heightPower << 29ull;
            result |= uint64_t(_format) << 37ull;
            result |= uint64_t(_mipCount) << 45ull;
            return result;
        } else {
            uint64_t h0 = (uint64_t(_width) << 32ull) | uint64_t(_height);
            uint64_t h1 = (uint64_t(_depth) << 32ull) | uint64_t(_format);
            uint64_t h2 = 
                uint64_t(_dimensionality)
                | (uint64_t(_mipCount) << 4ull)
                | (uint64_t(_arrayCount) << 12ull)
                | (uint64_t(_samples._sampleCount) << 28ull)
                | (uint64_t(_samples._samplingQuality) << 36ull)
                ;
            return HashCombine(h0, HashCombine(h1, h2));
        }
    }

    uint64_t ResourceDesc::CalculateHash() const
    {
        assert((unsigned(_type) & ((1<<2)-1)) == unsigned(_type));
        assert((_bindFlags & ((1<<16)-1)) == _bindFlags);
        assert((_cpuAccess & ((1<<3)-1)) == _cpuAccess);
        assert((_gpuAccess & ((1<<3)-1)) == _gpuAccess);
        assert((_allocationRules & ((1<<3)-1)) == _allocationRules);
        uint64_t h0 = 
            uint64_t(_type)
            | (uint64_t(_bindFlags) << 2ull)
            | (uint64_t(_cpuAccess) << 18ull)
            | (uint64_t(_gpuAccess) << 21ull)
            | (uint64_t(_allocationRules) << 24ull)
            ;
        if (_type == Type::Texture) h0 = HashCombine(_textureDesc.CalculateHash(), h0);
        else if (_type == Type::LinearBuffer) h0 = HashCombine(_linearBufferDesc.CalculateHash(), h0);
        return h0;
    }

	ResourceDesc::ResourceDesc()
	{
		_type = Type::Unknown;
		_bindFlags = _cpuAccess = _gpuAccess = _allocationRules = 0;
		_name[0] = '\0';
		XlZeroMemory(_textureDesc);
	}


	namespace GlobalInputLayouts
    {
        namespace Detail
        {
            InputElementDesc P2CT_Elements[] =
            {
                InputElementDesc( "PIXELPOSITION",   0, Format::R32G32_FLOAT   ),
                InputElementDesc( "COLOR",      0, Format::R8G8B8A8_UNORM ),
                InputElementDesc( "TEXCOORD",   0, Format::R32G32_FLOAT   )
            };

            InputElementDesc P2C_Elements[] = 
            {
                InputElementDesc( "PIXELPOSITION",   0, Format::R32G32_FLOAT   ),
                InputElementDesc( "COLOR",      0, Format::R8G8B8A8_UNORM )
            };

            InputElementDesc PCT_Elements[] = 
            {
                InputElementDesc( "POSITION",   0, Format::R32G32B32_FLOAT),
                InputElementDesc( "COLOR",      0, Format::R8G8B8A8_UNORM ),
                InputElementDesc( "TEXCOORD",   0, Format::R32G32_FLOAT   )
            };

            InputElementDesc P_Elements[] = 
            {
                InputElementDesc( "POSITION",   0, Format::R32G32B32_FLOAT)
            };

            InputElementDesc PC_Elements[] = 
            {
                InputElementDesc( "POSITION",   0, Format::R32G32B32_FLOAT),
                InputElementDesc( "COLOR",      0, Format::R8G8B8A8_UNORM )
            };

            InputElementDesc PT_Elements[] = 
            {
                InputElementDesc( "POSITION",   0, Format::R32G32B32_FLOAT),
                InputElementDesc( "TEXCOORD",   0, Format::R32G32_FLOAT   )
            };

            InputElementDesc PN_Elements[] = 
            {
                InputElementDesc( "POSITION",   0, Format::R32G32B32_FLOAT),
                InputElementDesc( "NORMAL",   0, Format::R32G32B32_FLOAT )
            };

            InputElementDesc PNT_Elements[] = 
            {
                InputElementDesc( "POSITION",   0, Format::R32G32B32_FLOAT),
                InputElementDesc( "NORMAL",   0, Format::R32G32B32_FLOAT ),
                InputElementDesc( "TEXCOORD",   0, Format::R32G32_FLOAT )
            };

            InputElementDesc PNTT_Elements[] = 
            {
                InputElementDesc( "POSITION",   0, Format::R32G32B32_FLOAT),
                InputElementDesc( "NORMAL",   0, Format::R32G32B32_FLOAT ),
                InputElementDesc( "TEXCOORD",   0, Format::R32G32_FLOAT ),
                InputElementDesc( "TEXTANGENT",   0, Format::R32G32B32_FLOAT ),
                InputElementDesc( "TEXBITANGENT",   0, Format::R32G32B32_FLOAT )
            };
        }

        InputLayout P2CT = MakeIteratorRange(Detail::P2CT_Elements);
        InputLayout P2C = MakeIteratorRange(Detail::P2C_Elements);
        InputLayout PCT = MakeIteratorRange(Detail::PCT_Elements);
        InputLayout P = MakeIteratorRange(Detail::P_Elements);
        InputLayout PC = MakeIteratorRange(Detail::PC_Elements);
        InputLayout PT = MakeIteratorRange(Detail::PT_Elements);
        InputLayout PN = MakeIteratorRange(Detail::PN_Elements);
        InputLayout PNT = MakeIteratorRange(Detail::PNT_Elements);
        InputLayout PNTT = MakeIteratorRange(Detail::PNTT_Elements);
    }

    namespace GlobalMiniInputLayouts
    {
        namespace Detail
        {
            MiniInputElementDesc P2CT_Elements[] =
            {
                MiniInputElementDesc{ Hash64("PIXELPOSITION"), Format::R32G32_FLOAT },
                MiniInputElementDesc{ Hash64("COLOR"), Format::R8G8B8A8_UNORM },
                MiniInputElementDesc{ Hash64("TEXCOORD"), Format::R32G32_FLOAT }
            };

            MiniInputElementDesc P2C_Elements[] = 
            {
                MiniInputElementDesc{ Hash64("PIXELPOSITION"), Format::R32G32_FLOAT },
                MiniInputElementDesc{ Hash64("COLOR"), Format::R8G8B8A8_UNORM }
            };

            MiniInputElementDesc PCT_Elements[] = 
            {
                MiniInputElementDesc{ Hash64("POSITION"), Format::R32G32B32_FLOAT },
                MiniInputElementDesc{ Hash64("COLOR"), Format::R8G8B8A8_UNORM },
                MiniInputElementDesc{ Hash64("TEXCOORD"), Format::R32G32_FLOAT }
            };

            MiniInputElementDesc P_Elements[] = 
            {
                MiniInputElementDesc{ Hash64("POSITION"), Format::R32G32B32_FLOAT }
            };

            MiniInputElementDesc PC_Elements[] = 
            {
                MiniInputElementDesc{ Hash64("POSITION"), Format::R32G32B32_FLOAT },
                MiniInputElementDesc{ Hash64("COLOR"), Format::R8G8B8A8_UNORM }
            };

            MiniInputElementDesc PT_Elements[] = 
            {
                MiniInputElementDesc{ Hash64("POSITION"), Format::R32G32B32_FLOAT },
                MiniInputElementDesc{ Hash64("TEXCOORD"), Format::R32G32_FLOAT }
            };

            MiniInputElementDesc PN_Elements[] = 
            {
                MiniInputElementDesc{ Hash64("POSITION"), Format::R32G32B32_FLOAT },
                MiniInputElementDesc{ Hash64("NORMAL"), Format::R32G32B32_FLOAT }
            };

            MiniInputElementDesc PNT_Elements[] = 
            {
                MiniInputElementDesc{ Hash64("POSITION"), Format::R32G32B32_FLOAT },
                MiniInputElementDesc{ Hash64("NORMAL"), Format::R32G32B32_FLOAT },
                MiniInputElementDesc{ Hash64("TEXCOORD"), Format::R32G32_FLOAT }
            };

            MiniInputElementDesc PNTT_Elements[] = 
            {
                MiniInputElementDesc{ Hash64("POSITION"), Format::R32G32B32_FLOAT },
                MiniInputElementDesc{ Hash64("NORMAL"), Format::R32G32B32_FLOAT },
                MiniInputElementDesc{ Hash64("TEXCOORD"), Format::R32G32_FLOAT },
                MiniInputElementDesc{ Hash64("TEXTANGENT"), Format::R32G32B32_FLOAT },
                MiniInputElementDesc{ Hash64("TEXBITANGENT"), Format::R32G32B32_FLOAT }
            };
        }

        IteratorRange<const MiniInputElementDesc*> P2CT = MakeIteratorRange(Detail::P2CT_Elements);
        IteratorRange<const MiniInputElementDesc*> P2C = MakeIteratorRange(Detail::P2C_Elements);
        IteratorRange<const MiniInputElementDesc*> PCT = MakeIteratorRange(Detail::PCT_Elements);
        IteratorRange<const MiniInputElementDesc*> P = MakeIteratorRange(Detail::P_Elements);
        IteratorRange<const MiniInputElementDesc*> PC = MakeIteratorRange(Detail::PC_Elements);
        IteratorRange<const MiniInputElementDesc*> PT = MakeIteratorRange(Detail::PT_Elements);
        IteratorRange<const MiniInputElementDesc*> PN = MakeIteratorRange(Detail::PN_Elements);
        IteratorRange<const MiniInputElementDesc*> PNT = MakeIteratorRange(Detail::PNT_Elements);
        IteratorRange<const MiniInputElementDesc*> PNTT = MakeIteratorRange(Detail::PNTT_Elements);
    }

    unsigned CalculateVertexStrideForSlot(IteratorRange<const InputElementDesc*> range, unsigned slot)
    {
            // note --  Assuming vertex elements are densely packed (which
            //          they usually are).
            //          We could also use the "_alignedByteOffset" member
            //          to find out where the element begins and ends)
        unsigned result = 0;
        unsigned largestAlignmentRequirement = 1;
        for (auto i=range.begin(); i<range.end(); ++i) {
            if (i->_inputSlot == slot) {
                assert(i->_alignedByteOffset == (result/8) || i->_alignedByteOffset == ~unsigned(0x0));
                auto alignmentRequirement = VertexAttributeRequiredAlignment(i->_nativeFormat);
                largestAlignmentRequirement = std::max(largestAlignmentRequirement, alignmentRequirement);
                assert(((result/8)%alignmentRequirement)==0);
                result += BitsPerPixel(i->_nativeFormat);
            }
        }
        assert(((result/8)%largestAlignmentRequirement)==0);
        return result / 8;
    }

	std::vector<unsigned> CalculateVertexStrides(IteratorRange<const InputElementDesc*> layout)
	{
		std::vector<unsigned> result;
        #if defined(_DEBUG)
            std::vector<unsigned> largestAlignmentRequirement;
        #endif
		for (auto& a:layout) {
			if (result.size() <= a._inputSlot) {
				result.resize(a._inputSlot + 1, 0);
                #if defined(_DEBUG)
                    largestAlignmentRequirement.resize(a._inputSlot + 1, 1);
                #endif
            }
			unsigned& stride = result[a._inputSlot];
            #if defined(_DEBUG)
                auto alignmentRequirement = VertexAttributeRequiredAlignment(a._nativeFormat);
                largestAlignmentRequirement[a._inputSlot] = std::max(largestAlignmentRequirement[a._inputSlot], alignmentRequirement);
            #endif
			auto bytes = BitsPerPixel(a._nativeFormat) / 8;
			if (a._alignedByteOffset == ~0u) {
                assert((stride%alignmentRequirement) == 0);
				stride = stride + bytes;
			} else {
                assert((a._alignedByteOffset%alignmentRequirement) == 0);
				stride = std::max(stride, a._alignedByteOffset + bytes);
			}
		}
        #if defined(_DEBUG)
            for (unsigned s=0; s<result.size(); ++s)
                assert((result[s]%largestAlignmentRequirement[s])==0);
        #endif
		return result;
	}

    bool RequiresAlignmentSpacing(IteratorRange<const InputElementDesc*> layout)
	{
		std::vector<unsigned> result;
        std::vector<unsigned> largestAlignmentRequirement;
		for (auto& a:layout) {
			if (result.size() <= a._inputSlot) {
				result.resize(a._inputSlot + 1, 0);
                largestAlignmentRequirement.resize(a._inputSlot + 1, 1);
            }
			unsigned& stride = result[a._inputSlot];
            auto alignmentRequirement = VertexAttributeRequiredAlignment(a._nativeFormat);
            largestAlignmentRequirement[a._inputSlot] = std::max(largestAlignmentRequirement[a._inputSlot], alignmentRequirement);
			auto bytes = BitsPerPixel(a._nativeFormat) / 8;
			if (a._alignedByteOffset == ~0u) {
                if ((stride%alignmentRequirement) != 0) return true;
				stride = stride + bytes;
			} else {
                if ((a._alignedByteOffset%alignmentRequirement) != 0) return true;
				stride = std::max(stride, a._alignedByteOffset + bytes);
			}
		}
        for (unsigned s=0; s<result.size(); ++s)
            if ((result[s]%largestAlignmentRequirement[s])!=0) return true;
		return false;
	}

	std::vector<InputElementDesc> NormalizeInputAssembly(IteratorRange<const InputElementDesc*> layout)
	{
		// Transform the given InputElementDesc into a "normalized" form
		//   1) convert any cases where the _alignedByteOffset is ~0u to the true offset
		//   2) make all semantics uppercase
		//	 3) sort by input slot & data offset

		std::vector<InputElementDesc> result(layout.begin(), layout.end());

		std::vector<unsigned> runningSizes;
        std::vector<unsigned> largestAlignmentRequirement;
		for (auto& a:result) {
			if (runningSizes.size() <= a._inputSlot) {
				runningSizes.resize(a._inputSlot + 1, 0);
                largestAlignmentRequirement.resize(a._inputSlot + 1, 1);
            }
			unsigned& runningSize = runningSizes[a._inputSlot];
			auto bytes = BitsPerPixel(a._nativeFormat) / 8;
            auto alignmentRequirement = VertexAttributeRequiredAlignment(a._nativeFormat);
			if (a._alignedByteOffset == ~0u) {
                if ((runningSize%alignmentRequirement) != 0) {
                    runningSize += alignmentRequirement-(runningSize%alignmentRequirement);
                    Log(Warning) << "Adding spacer in vertex buffer due to attribute alignment rules" << std::endl;
                }
				a._alignedByteOffset = runningSize;
            } else {
                assert((a._alignedByteOffset%alignmentRequirement) == 0);
            }

			runningSize = std::max(runningSize, a._alignedByteOffset + bytes);

			std::transform(
				a._semanticName.begin(), a._semanticName.end(), a._semanticName.begin(),
				[](char c) { return (char)std::toupper(c); });
		}

		std::sort(
			result.begin(), result.end(),
			[](const InputElementDesc& lhs, const InputElementDesc& rhs) {
				if (lhs._inputSlot < rhs._inputSlot) return true;
				if (lhs._inputSlot > rhs._inputSlot) return false;
				return lhs._alignedByteOffset < rhs._alignedByteOffset;
			});

		return result;
	}

    uint64_t HashInputAssembly(IteratorRange<const InputElementDesc*> inputAssembly, uint64_t seed)
	{
        // We want ideal to create a hashing algorithm such that Hash(A) == Hash(NormalizeInputAssembly(A))
        // and also the MiniInputElementDesc version will produce the same hash for equivalent results
        // That makes this a little more complicated, unfortunately
        // Note -- this won't produce the correct result if the input is so scrambled that there are multiple
        // elements that overlap each other

        #if defined(_DEBUG)
            // no support for alignment spacing yet
            assert(!RequiresAlignmentSpacing(inputAssembly));
        #endif

        uint64_t result = seed;

        unsigned elementsHashed = 0;
        for (unsigned inputSlot = 0;; ++inputSlot) {
            assert(inputSlot < 16); // if this gets too high, it signals that something has gone off the rails (maybe overlapping elements in the input?)
            if (elementsHashed == inputAssembly.size())
                break;

            // rotate the hash as a way of marking the changing input slot
            if (inputSlot != 0)
                result = rotl64(result, 1);

            const InputElementDesc* earliestElement = nullptr;
            unsigned earliestElementOffset = ~0u;
            for (const auto& e:inputAssembly) {
                if (e._inputSlot != inputSlot) continue;
                if (e._alignedByteOffset < earliestElementOffset || !earliestElement) {
                    earliestElement = &e;
                    earliestElementOffset = e._alignedByteOffset;
                }
            }

            if (!earliestElement) continue;     // no elements on this slot at all

            auto e = earliestElement;
            unsigned offsetIterator = earliestElementOffset;
            if (offsetIterator == ~0u) offsetIterator = 0;

            for (;;) {
                auto semanticHash = Utility::Hash64(e->_semanticName) + e->_semanticIndex;
                result = HashCombine(semanticHash ^ uint64_t(e->_nativeFormat), result);
                if (e->_inputSlotClass != InputDataRate::PerVertex)
                    result = HashCombine((uint64_t(e->_instanceDataStepRate) << 32ull) | uint64_t(e->_inputSlotClass), result);
                ++elementsHashed;
                if (elementsHashed == inputAssembly.size())
                    break;

                offsetIterator += BitsPerPixel(e->_nativeFormat) / 8;

                auto nexte = e+1;
                if (nexte != inputAssembly.end() && nexte->_alignedByteOffset == ~0u) {
                    e = nexte;
                    continue;
                }            

                nexte = nullptr;
                earliestElementOffset = ~0u;
                for (e=inputAssembly.begin(); e!=inputAssembly.end(); ++e) {
                    if (e->_inputSlot != inputSlot) continue;
                    if (e->_alignedByteOffset >= offsetIterator && (e->_alignedByteOffset < earliestElementOffset)) {
                        nexte = e;
                        earliestElementOffset = e->_alignedByteOffset;
                    }
                }

                if (!nexte)
                    break;

                auto gap = nexte->_alignedByteOffset - offsetIterator;
                if (gap != 0)
                    result = HashCombine(gap, result);
                offsetIterator = nexte->_alignedByteOffset;
                e = nexte;
            }
        }

        return result;
	}

    uint64_t HashInputAssembly(IteratorRange<const MiniInputElementDesc*> inputAssembly, uint64_t seed)
    {
        auto result = seed;
        for (const auto&e:inputAssembly)
            result = HashCombine(e._semanticHash ^ uint64_t(e._nativeFormat), result);
        return result;
    }

    unsigned HasElement(IteratorRange<const InputElementDesc*> range, const char elementSemantic[])
    {
        unsigned result = 0;
        for (auto i = range.begin(); i != range.end(); ++i) {
            if (!XlCompareStringI(i->_semanticName.c_str(), elementSemantic)) {
                assert((result & (1 << i->_semanticIndex)) == 0);
                result |= (1 << i->_semanticIndex);
            }
        }
        return result;
    }

    unsigned FindElement(IteratorRange<const InputElementDesc*> range, const char elementSemantic[], unsigned semanticIndex)
    {
        for (auto i = range.begin(); i != range.end(); ++i)
            if (i->_semanticIndex == semanticIndex && !XlCompareStringI(i->_semanticName.c_str(), elementSemantic))
                return unsigned(i - range.begin());
        return ~0u;
    }

	bool HasElement(IteratorRange<const MiniInputElementDesc*> elements, uint64 semanticHash)
	{
		for (const auto&e:elements)
			if (e._semanticHash == semanticHash)
				return true;
		return false;
	}

	unsigned CalculateVertexStride(IteratorRange<const MiniInputElementDesc*> elements, bool enforceAlignment)
	{
        // note -- following alignment rules suggested by Apple in OpenGL ES guide
        //          each element should be aligned to a multiple of 4 bytes (or a multiple of
        //          it's component size, whichever is larger).
        //
        if (elements.empty()) return 0;
		unsigned result = 0;
        unsigned largestRequiredAlignment = 1;
        for (auto i=elements.begin(); i<elements.end(); ++i) {
            auto alignment = VertexAttributeRequiredAlignment(i->_nativeFormat);
            largestRequiredAlignment = std::max(largestRequiredAlignment, alignment);
            if ((result%alignment) != 0) {
                result += alignment-(result%alignment);
                Log(Warning) << "Adding spacer in vertex buffer due to attribute alignment rules" << std::endl;
            }
            auto size = BitsPerPixel(i->_nativeFormat) / 8;
            result += size;
        }
        if (enforceAlignment) {
            if ((result%largestRequiredAlignment) != 0) {
                result += largestRequiredAlignment-(result%largestRequiredAlignment);
                Log(Warning) << "Adding spacer in vertex buffer due to attribute alignment rules" << std::endl;
            }
        }
        return result;
	}

    unsigned VertexAttributeRequiredAlignment(Format fmt)
    {
        // The Vulkan space is clearest about the rules here:
        //      if fmt is a "packed format" (ie, multi-component types that are treated as a single larger component type), then the alignment has special rules
        //      for other formats, the alignment is the size of the component type
        auto componentPrecision = GetComponentPrecision(fmt);
        componentPrecision = std::max(componentPrecision, 8u);
        assert(componentPrecision != 10 && componentPrecision != 11);       // these are the 10/10/10/2, 11/11/10 type formats
        return componentPrecision/8;
    }

    unsigned CalculatePrimitiveCount(Topology topology, unsigned vertexCount, unsigned drawCallCount)
    {
        switch (topology) {
		case Topology::TriangleList:
			return vertexCount / 3;
		case Topology::TriangleStrip:
			return vertexCount - 2 * drawCallCount;
		case Topology::LineList:
			return vertexCount / 2;
		case Topology::LineStrip:
			return vertexCount - 1 * drawCallCount;
		case Topology::PointList:
			return vertexCount;
		default:
			return 0;
	    }
    }

    const char* AsString(ShaderStage stage)
	{
		switch (stage) {
		case ShaderStage::Vertex: return "Vertex";
		case ShaderStage::Pixel: return "Pixel";
		case ShaderStage::Geometry: return "Geometry";
		case ShaderStage::Hull: return "Hull";
		case ShaderStage::Domain: return "Domain";
		case ShaderStage::Compute: return "Compute";
		case ShaderStage::Null: return "Null";
		case ShaderStage::Max: return "Max";
		default: return "<<unknown>>";
		}
	}

    std::string BindFlagsAsString(BindFlag::BitField bindFlags)
    {
        static std::string s_zero {"0"};
        if (!bindFlags) return s_zero;
        std::stringstream str;
        bool first = true;
        while (bindFlags) {
            auto bit = xl_ctz4(bindFlags);
            bindFlags ^= 1u<<bit;
            if (!first) str << "|";
            first = false;
            str << AsString((BindFlag::Enum)(1u<<bit));
        }
        return str.str();
    }

    const char* AsString(BindFlag::Enum flag)
    {
        switch (flag) {
        case BindFlag::VertexBuffer: return "VertexBuffer";
		case BindFlag::IndexBuffer: return "IndexBuffer";
		case BindFlag::ShaderResource: return "ShaderResource";
		case BindFlag::RenderTarget: return "RenderTarget";
		case BindFlag::DepthStencil: return "DepthStencil";
		case BindFlag::UnorderedAccess: return "UnorderedAccess";
		case BindFlag::ConstantBuffer: return "ConstantBuffer";
		case BindFlag::StreamOutput: return "StreamOutput";
        case BindFlag::DrawIndirectArgs: return "DrawIndirectArgs";
        case BindFlag::RawViews: return "RawViews";
        case BindFlag::InputAttachment: return "InputAttachment";
        case BindFlag::TransferSrc: return "TransferSrc";
        case BindFlag::TransferDst: return "TransferDst";
        case BindFlag::PresentationSrc: return "PresentationSrc";
        case BindFlag::TexelBuffer: return "TexelBuffer";
		default: return "<<unknown>>";
        }
    }

    IResourcePtr IDevice::CreateResource(const ResourceDesc& desc, const SubResourceInitData& initData)
    {
        // Utility function to make creating single-subresource resources a little easier
        if (initData._data.size()) {
            return CreateResource(
                desc,
                [&initData](SubResourceId subResId) -> SubResourceInitData {
                    assert(subResId._mip == 0 && subResId._arrayLayer == 0);
                    return initData;
                });
        } else {
            return CreateResource(desc, ResourceInitializer{});
        }
    }

    IDevice::~IDevice() {}
    IThreadContext::~IThreadContext() {}
    IPresentationChain::~IPresentationChain() {}
    IResource::~IResource() {}
    IAnnotator::~IAnnotator() {}
    ICompiledPipelineLayout::~ICompiledPipelineLayout() {}
    IResourceView::~IResourceView() {}
    ISampler::~ISampler() {}

}


