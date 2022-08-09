// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetHeap.h"
#include "../Core/Types.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace Utility { template<typename T> class IteratorRange; }

namespace Assets
{
	class IDefaultAssetHeap;
	class AssetHeapRecord;
    template <typename AssetType> class DefaultAssetHeap;

    class AssetSetManager : public IAssetTracking
    {
    public:
        template<typename Type>
			DefaultAssetHeap<Type>& GetSetForType();

        void Clear();

        unsigned GetAssetSetCount();
        const IDefaultAssetHeap* GetAssetSet(unsigned index);

		void OnFrameBarrier();

        void Lock();
        void Unlock();

        SignalId BindUpdateSignal(std::function<UpdateSignalSig>&& fn) override;
		void UnbindUpdateSignal(SignalId) override;

		unsigned RegisterFrameBarrierCallback(std::function<void()>&& fn);
		void DeregisterFrameBarrierCallback(unsigned);
        void HintMarkerUpdated(uint64_t typeId);

        AssetSetManager();
        ~AssetSetManager();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

		IDefaultAssetHeap* GetSetForTypeCode(size_t typeCode);
        void Add(size_t typeCode, std::unique_ptr<IDefaultAssetHeap>&& set);
    };

    template<typename Type>
		DefaultAssetHeap<Type>& AssetSetManager::GetSetForType()
    {
            // The lock here is frustratingly redundant in 99% of cases. But 
            // we still need it for the rest of the cases. If we could force the
            // system to add all of the types we need during startup, we could
            // avoid this. Alternatively, this might be a good candidate for a spin
            // lock, instead of a mutex
        Lock();
        auto existing = GetSetForTypeCode(typeid(Type).hash_code());
        if (existing) {
            Unlock();
            return *static_cast<DefaultAssetHeap<Type>*>(existing);
        }

		DefaultAssetHeap<Type>* result = nullptr;
        try 
        {
            auto newPtr = std::make_unique<DefaultAssetHeap<Type>>();
            result = newPtr.get();
            Add(typeid(Type).hash_code(), std::move(newPtr));
        } catch (...) {
            Unlock();
            throw;
        }
        Unlock();
        return *result;
    }

}

