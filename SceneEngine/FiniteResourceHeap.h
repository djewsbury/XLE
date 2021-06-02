#pragma once

#include "../RenderCore/Format.h"
#include "../Math/Vector.h"
#include "../Utility/HeapUtils.h"
#include <assert.h>

namespace SceneEngine
{
    template<typename Key>
        class FiniteResourceHeap
    {
    public:
        class RequestResult
        {
        public:
            enum class Result { Available, InitiateTransfer, CurrentlyTransferring, Overflow };
            Result _result;
            unsigned _index;
            Key _previousKey;
        };
        
        void ProcessRequests(IteratorRange<RequestResult*> results,
                             IteratorRange<const Key*> requests);
        void CancelTransfer(unsigned idx);
        void CompleteTransfer(unsigned idx);
        void Evict(unsigned idx);
        
        FiniteResourceHeap();
        FiniteResourceHeap(unsigned count, const Key& dummyKey);
        ~FiniteResourceHeap();
        
        FiniteResourceHeap(FiniteResourceHeap&&) = default;
        FiniteResourceHeap& operator=(FiniteResourceHeap&&) = default;
        
    private:
        mutable LRUQueue    _lruQueue;
        
        class Block
        {
        public:
            enum class State { Empty, Occupied, Transfer };
            State   _state;
            Key     _key;
        };
        std::vector<Block> _blockStates;
        Key _dummyKey;
    };
}
