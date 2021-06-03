// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetHeap.h"
#include "../Utility/HeapUtils.h"

namespace Assets
{
	template<typename AssetType>
		class AssetLRUHeap : public IDefaultAssetHeap
	{
	public:
		template<typename... Params>
			PtrToFuturePtr<AssetType> Get(Params...);

		void            Clear();
		uint64_t		GetTypeCode() const;
		std::string		GetTypeName() const;

		std::vector<AssetHeapRecord>		LogRecords() const;

		AssetLRUHeap(unsigned heapSize);
		~AssetLRUHeap();
		AssetLRUHeap(const AssetLRUHeap&) = delete;
		AssetLRUHeap& operator=(const AssetLRUHeap&) = delete;
	private:
		mutable Threading::Mutex _lock;		
		LRUCache<FuturePtr<AssetType>> _assets;
	};

	template<typename AssetType>
		template<typename... Params>
			auto AssetLRUHeap<AssetType>::Get(Params... initialisers) -> PtrToFuturePtr<AssetType>
	{
		auto hash = Internal::BuildParamHash(initialisers...);

		PtrToFuturePtr<AssetType> newFuture;
		{
			ScopedLock(_lock);

			auto& existing = _assets.Get(hash);
			if (existing)
				if (!IsInvalidated(*existing))
					return existing;

			auto stringInitializer = Internal::AsString(initialisers...);	// (used for tracking/debugging purposes)
			newFuture = std::make_shared<FuturePtr<AssetType>>(stringInitializer);
			_assets.Insert(hash, newFuture);
		}

		// note -- call AutoConstructToFuture outside of the mutex lock, because this operation can be expensive
		// after the future has been constructed but before we complete AutoConstructToFuture, the asset is considered to be
		// in "pending" state, and Actualize() will through a PendingAsset exception, so this should be thread-safe, even if
		// another thread grabs the future before AutoConstructToFuture is done
		AutoConstructToFuture<AssetType>(*newFuture, std::forward<Params>(initialisers)...);
		return newFuture;
	}

	template<typename AssetType>
		AssetLRUHeap<AssetType>::AssetLRUHeap(unsigned heapSize) : _assets(heapSize) {}

	template<typename AssetType>
		AssetLRUHeap<AssetType>::~AssetLRUHeap() {}
	
	template<typename AssetType>
		void AssetLRUHeap<AssetType>::Clear()
    {
        ScopedLock(_lock);
		auto heapSize = _assets.GetCacheSize();
        _assets = LRUCache<FuturePtr<AssetType>> { heapSize };
    }

	template<typename AssetType>
		auto AssetLRUHeap<AssetType>::LogRecords() const -> std::vector<AssetHeapRecord>
	{
		ScopedLock(_lock);
		std::vector<AssetHeapRecord> result;
		result.reserve(_assets.GetObjects().size());
		auto typeCode = GetTypeCode();
		for (const auto&a : _assets.GetObjects()) {
			if (!a) continue;
			result.push_back({ a->Initializer(), a->GetAssetState(), a->GetDependencyValidation(), a->GetActualizationLog(), typeCode, ~0u });
		}
		return result;
	}

	template<typename AssetType>
		uint64_t		AssetLRUHeap<AssetType>::GetTypeCode() const
		{
			return typeid(AssetType).hash_code();
		}

	template<typename AssetType>
		std::string		AssetLRUHeap<AssetType>::GetTypeName() const
		{
			return typeid(AssetType).name();
		}
}

