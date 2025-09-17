// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/PtrUtils.h"
#include "../Utility/MiniHeap.h"
#include "../Utility/IteratorUtils.h"
#include "../Core/Exceptions.h"
#include <type_traits>      // (for is_integral)

namespace RenderCore
{
    namespace Exceptions
    {
        /// <summary>Some unspecific failure during rendering</summary>
        /// This exception should be used in cases that relate to an underlying
        /// graphics API. It could mean there is a compatibility issue, or a
        /// failure of the hardware.
        class GenericFailure : public ::Exceptions::BasicLabel
        {
        public: 
            GenericFailure(const char what[]);
        };

        /// <summary>Failure creating object</summary>
        /// Failure while attempting to create an object. This can sometimes be
        /// due to insufficient resources (ie, out of memory).
        /// But it could also happen if the IDevice is in an invalid state.
        /// This could potentially happen if (for example):
        ///     <list>
        ///         <item>Hardware fault or driver crash</item>
        ///         <item>Plug-and-play graphics card disconnected</item>
        ///         <item>Driver is unavailable (because during a driver install or similar operation)</item>
        ///     </list>
        /// These cases result in a "lost device" type state in DirectX11. While
        /// this is much more rare in DirectX11 than DirectX9, it can still happen.
        class AllocationFailure : public GenericFailure
        {
        public: 
            AllocationFailure(const char what[]);
        };
    }

    /// <summary>A reference counted packet for small allocations</summary>
    /// This object manage a small packet of reference counted memory in
    /// a way that reduces fragmentation and contention for the main heap.
    ///
    /// Often we need to create small packets for constant buffer uploads.
    /// We want those packets to be variable sized (but typically multiples
    /// of 8), and have reference counting attached. Most of the time the
    /// allocations might be created and destroyed during a single frame 
    /// (but sometimes the allocations can last longer).
    ///
    /// This object uses a dedicated heap that is designed to deal with many
    /// small constant-buffer sized packets. It performs thread safe reference
    /// counting, so packets can be passed between threads safely.
    ///
    /// By using a separate heap, we avoid polluting the main heap with these
    /// smaller (often short-lived) allocations.
    ///
    /// SharedPkt has proper value semantics (it works like a smart pointer, 
    /// but with a size attached). Prefer to use the "move" type methods to
    /// avoid unnecessary AddRef/Releases.
    class SharedPkt : public MiniHeap::Allocation
    {
    public:
        void* get() const never_throws          { CheckSubframeHeapReset(); return _allocation; }
        operator bool() const never_throws      { CheckSubframeHeapReset(); return _allocation != nullptr; }

        void* begin()               { CheckSubframeHeapReset(); return _allocation; }
        void* end()                 { CheckSubframeHeapReset(); return PtrAdd(_allocation, _size); }
        const void* begin() const   { CheckSubframeHeapReset(); return _allocation; }
        const void* end() const     { CheckSubframeHeapReset(); return PtrAdd(_allocation, _size); }
        size_t size() const         { CheckSubframeHeapReset(); return _size; }

		IteratorRange<const void*> AsIteratorRange() const { CheckSubframeHeapReset(); return MakeIteratorRange(begin(), end()); }

        SharedPkt() never_throws;
        SharedPkt(const SharedPkt& cloneFrom);
        SharedPkt& operator=(const SharedPkt& cloneFrom);
        SharedPkt(SharedPkt&& moveFrom) never_throws;
        SharedPkt& operator=(SharedPkt&& moveFrom) never_throws;
        ~SharedPkt();

        void CalculateHash();
        uint64_t GetHash() const { return _calculatedHash; }     // unless CalculateHash is explicitly called, the hash doesn't get calculated and is just zero here

        friend SharedPkt MakeSharedPktSize(size_t size);
        friend SharedPkt MakeSharedPkt(const void* begin, const void* end);
        friend SharedPkt MakeSubFramePktSize(size_t size);
        friend SharedPkt MakeSubFramePktSizeAligned(size_t size, size_t alignment);
        friend SharedPkt MakeSubFramePkt(const void* begin, const void* end);

        void swap(SharedPkt& other) never_throws;
    private:
        SharedPkt(MiniHeap::Allocation alloc, size_t size, unsigned subframeHeapReset=0);
        size_t _size;
        uint64_t _calculatedHash;

        #if defined(_DEBUG)
            unsigned _subframeHeapReset;
            void CheckSubframeHeapReset() const;
        #else
            void CheckSubframeHeapReset() const {}
        #endif
        static MiniHeap& GetHeap();
    };

    SharedPkt MakeSharedPktSize(size_t size);
    SharedPkt MakeSharedPkt(const void* begin, const void* end);
    
    template<typename T,
        typename std::enable_if<!std::is_integral<T>::value>::type* = nullptr>
        SharedPkt MakeSharedPkt(const T& input)
    {
        return MakeSharedPkt(&input, PtrAdd(&input, sizeof(T)));
    }
    
    SharedPkt MakeSubFramePktSize(size_t size);
    SharedPkt MakeSubFramePktSizeAligned(size_t size, size_t alignment);
    SharedPkt MakeSubFramePkt(const void* begin, const void* end);
    
    template<typename T,
        typename std::enable_if<!std::is_integral<T>::value>::type* = nullptr>
        SharedPkt MakeSubFramePkt(const T& input)
    {
        return MakeSubFramePkt(&input, PtrAdd(&input, sizeof(T)));
    }

    void* SubFrameHeap_Allocate(size_t size);
    unsigned SubFrameHeap_ProducerFrameBarrier();
    void SubFrameHeap_ConsumerFrameBarrier(unsigned producerBarrierId);
    void SubFrameHeap_ProducerAndConsumerFrameBarrier();

    inline SharedPkt::SharedPkt(SharedPkt&& moveFrom) never_throws
    {
        _allocation = nullptr;
        _marker = ~uint32_t(0x0);
        _size = 0;
        _calculatedHash = 0;
        #if defined(_DEBUG)
            _subframeHeapReset = 0;
        #endif
        swap(moveFrom);
    }

    inline SharedPkt::SharedPkt() never_throws
    {
        _allocation = nullptr;
        _marker = ~uint32_t(0x0);
        _size = 0;
        _calculatedHash = 0;
        #if defined(_DEBUG)
            _subframeHeapReset = 0;
        #endif
    }

    inline SharedPkt& SharedPkt::operator=(const SharedPkt& cloneFrom)
    {
        SharedPkt(cloneFrom).swap(*this);
        return *this;
    }

    inline SharedPkt& SharedPkt::operator=(SharedPkt&& moveFrom) never_throws
    {
        SharedPkt(std::forward<SharedPkt>(moveFrom)).swap(*this);
        return *this;
    }

    inline void SharedPkt::swap(SharedPkt& other) never_throws
    {
        std::swap(other._allocation, _allocation);
        std::swap(other._marker, _marker);
        std::swap(other._size, _size);
        std::swap(other._calculatedHash, _calculatedHash);
        #if defined(_DEBUG)
            std::swap(other._subframeHeapReset, _subframeHeapReset);
        #endif
    }

    inline unsigned ARGBtoABGR(unsigned input)
    {
        return   (input & 0xff00ff00)
            |   ((input & 0x00ff0000) >> 16)
            |   ((input & 0x000000ff) << 16)
            ;
    }
}
