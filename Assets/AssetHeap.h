#pragma once

#include "DepVal.h"
#if !defined(__CLR_VER)
#include "Marker.h"
#include "DeferredConstruction.h"
#include "../Utility/Threading/Mutex.h"
#endif
#include <memory>
#include <functional>

namespace Assets
{
	class DivergentAssetBase;

	class AssetHeapRecord
	{
	public:
		rstring		_initializer;
		AssetState	_state;
		DependencyValidation	_depVal;
		Blob		_actualizationLog; 
		uint64_t	_typeCode;
        unsigned    _initializationCount = 0;
	};

	class IAssetTracking
	{
	public:
		using SignalId = unsigned;
		using UpdateSignalSig = void(IteratorRange<const std::pair<uint64_t, AssetHeapRecord>*>);
		virtual SignalId BindUpdateSignal(std::function<UpdateSignalSig>&& fn) = 0;
		virtual void UnbindUpdateSignal(SignalId) = 0;
		virtual ~IAssetTracking();
	};

	class IDefaultAssetHeap : public IAssetTracking
	{
	public:
		virtual uint64_t		GetTypeCode() const = 0;
		virtual std::string		GetTypeName() const = 0;
		virtual void            Clear() = 0;

		virtual void UpdateMarkerStates() = 0;

		virtual ~IDefaultAssetHeap();
	};

///////////////////////////////////////////////////////////////////////////////////////////////////

#if !defined(__CLR_VER)
	template<typename AssetType>
		class DefaultAssetHeap : public IDefaultAssetHeap
	{
	public:
		using PtrToFuture = std::shared_ptr<Marker<AssetType>>;

		template<typename... Params>
			PtrToFuture Get(Params...);

		template<typename... Params>
			uint64_t SetShadowingAsset(AssetType&& newShadowingAsset, Params...);

		void            Clear() override;
		uint64_t		GetTypeCode() const override;
		std::string		GetTypeName() const override;

		SignalId BindUpdateSignal(std::function<UpdateSignalSig>&& fn) override;
		void UnbindUpdateSignal(SignalId) override;
		void UpdateMarkerStates() override;

		DefaultAssetHeap();
		~DefaultAssetHeap();
		DefaultAssetHeap(const DefaultAssetHeap&) = delete;
		DefaultAssetHeap& operator=(const DefaultAssetHeap&) = delete;
	private:
		mutable Threading::Mutex _lock;		
		std::vector<std::pair<uint64_t, PtrToFuture>> _assets;
		std::vector<std::pair<uint64_t, PtrToFuture>> _shadowingAssets;

		std::vector<AssetState> _lastKnownAssetStates;
		std::vector<std::pair<uint64_t, AssetHeapRecord>> LogRecordsAlreadyLocked() const;
		Signal<IteratorRange<const std::pair<uint64_t, AssetHeapRecord>*>> _updateSignal;
	};

	template<typename AssetType>
		static bool IsInvalidated(Marker<AssetType>& future)
	{
		// We must check the "background state" here. If it's invalidated in the
		// background, we can restart the compile; even if that invalidated state hasn't
		// reached the "foreground" yet.
		DependencyValidation depVal;
		Blob actualizationLog;
		auto state = future.CheckStatusBkgrnd(depVal, actualizationLog);
		if (state == AssetState::Pending)
			return false;
		if (!depVal)
			return false;
		return depVal.GetValidationIndex() > 0;
	}

	template<typename AssetType>
		template<typename... Params>
			auto DefaultAssetHeap<AssetType>::Get(Params... initialisers) -> PtrToFuture
	{
		auto hash = Internal::BuildParamHash(initialisers...);

		PtrToFuture newFuture;
		{
			ScopedLock(_lock);
			auto shadowing = LowerBound(_shadowingAssets, hash);
			if (shadowing != _shadowingAssets.end() && shadowing->first == hash)
				return shadowing->second;

			auto i = LowerBound(_assets, hash);
			if (i != _assets.end() && i->first == hash)
				if (!IsInvalidated(*i->second))
					return i->second;

			auto stringInitializer = Internal::AsString(initialisers...);	// (used for tracking/debugging purposes)
			newFuture = std::make_shared<Marker<AssetType>>(stringInitializer);
			if (i != _assets.end() && i->first == hash) {
				i->second = newFuture;
				_lastKnownAssetStates[std::distance(_assets.begin(), i)] = AssetState::Pending;
			} else {
				_lastKnownAssetStates.insert(_lastKnownAssetStates.begin() + std::distance(_assets.begin(), i), AssetState::Pending);
				_assets.insert(i, std::make_pair(hash, newFuture));
			}
		}

		// note -- call AutoConstructToPromise outside of the mutex lock, because this operation can be expensive
		// after the future has been constructed but before we complete AutoConstructToPromise, the asset is considered to be
		// in "pending" state, and Actualize() will through a PendingAsset exception, so this should be thread-safe, even if
		// another thread grabs the future before AutoConstructToPromise is done
		AutoConstructToPromise(newFuture->AdoptPromise(), std::forward<Params>(initialisers)...);
		return newFuture;
	}

	template<typename AssetType>
		template<typename... Params>
			uint64_t DefaultAssetHeap<AssetType>::SetShadowingAsset(AssetType&& newShadowingAsset, Params... initialisers)
	{
		auto hash = Internal::BuildParamHash(initialisers...);

		ScopedLock(_lock);
		auto shadowing = LowerBound(_shadowingAssets, hash);
		if (shadowing != _shadowingAssets.end() && shadowing->first == hash) {
			assert(0);
			// shadowing->second->SimulateChange(); 
			if (newShadowingAsset) {
				shadowing->second->SetAssetForeground(std::move(newShadowingAsset));
			} else {
				_shadowingAssets.erase(shadowing);
			}
			return hash;
		}

		if (newShadowingAsset) {
			auto stringInitializer = Internal::AsString(initialisers...);	// (used for tracking/debugging purposes)
			auto newShadowingFuture = std::make_shared<Marker<AssetType>>(stringInitializer);
			newShadowingFuture->SetAssetForeground(std::move(newShadowingAsset));
			_shadowingAssets.emplace(shadowing, std::make_pair(hash, std::move(newShadowingFuture)));
		}

		assert(0);
		/*auto i = LowerBound(_assets, hash);
		if (i != _assets.end() && i->first == hash)
			i->second->SimulateChange();*/

		return hash;
	}

	template<typename AssetType>
		DefaultAssetHeap<AssetType>::DefaultAssetHeap() {}

	template<typename AssetType>
		DefaultAssetHeap<AssetType>::~DefaultAssetHeap() {}
	
	template<typename AssetType>
		void DefaultAssetHeap<AssetType>::Clear()
    {
        ScopedLock(_lock);
        _assets.clear();
		_lastKnownAssetStates.clear();
        _shadowingAssets.clear();
    }

	template<typename AssetType>
		auto DefaultAssetHeap<AssetType>::BindUpdateSignal(std::function<UpdateSignalSig>&& fn) -> SignalId
	{
		ScopedLock(_lock);
		auto existingRecords = LogRecordsAlreadyLocked();
		if (!existingRecords.empty())
			fn(MakeIteratorRange(existingRecords));	// catchup with the current state of the heap
		auto res = _updateSignal.Bind(std::move(fn));
		return res;
	}

	template<typename AssetType>
		void DefaultAssetHeap<AssetType>::UnbindUpdateSignal(SignalId signalId)
	{
		ScopedLock(_lock);
		_updateSignal.Unbind(signalId);
	}

	template<typename AssetType>
		void DefaultAssetHeap<AssetType>::UpdateMarkerStates()
	{
		if (!_updateSignal.AtLeastOneBind()) return;
		ScopedLock(_lock);
		assert(_assets.size() == _lastKnownAssetStates.size());
		std::pair<uint64_t, AssetHeapRecord> updates[_assets.size()];
		unsigned updateCount = 0;
		for (unsigned c=0; c<_assets.size(); ++c) {
			auto& a = _assets[c];
			auto newState = a.second->GetAssetState();
			if (newState != _lastKnownAssetStates[c]) {
				updates[updateCount++] = {a.first, AssetHeapRecord{ a.second->Initializer(), a.second->GetAssetState(), a.second->GetDependencyValidation(), a.second->GetActualizationLog(), GetTypeCode() }};
				_lastKnownAssetStates[c] = newState;
			}
		}
		if (updateCount)
			_updateSignal.Invoke(MakeIteratorRange(updates, &updates[updateCount]));
	}

	template<typename AssetType>
		auto DefaultAssetHeap<AssetType>::LogRecordsAlreadyLocked() const -> std::vector<std::pair<uint64_t, AssetHeapRecord>>
	{
		std::vector<std::pair<uint64_t, AssetHeapRecord>> result;
		result.reserve(_assets.size() + _shadowingAssets.size());
		auto typeCode = GetTypeCode();
		for (const auto&a : _assets)
			result.emplace_back(a.first, AssetHeapRecord{ a.second->Initializer(), a.second->GetAssetState(), a.second->GetDependencyValidation(), a.second->GetActualizationLog(), typeCode });
		for (const auto&a : _shadowingAssets)
			result.emplace_back(a.first, AssetHeapRecord{ a.second->Initializer(), a.second->GetAssetState(), a.second->GetDependencyValidation(), a.second->GetActualizationLog(), typeCode });
		return result;
	}

	template<typename AssetType>
		uint64_t		DefaultAssetHeap<AssetType>::GetTypeCode() const
		{
			return typeid(AssetType).hash_code();
		}

	template<typename AssetType>
		std::string		DefaultAssetHeap<AssetType>::GetTypeName() const
		{
			return typeid(AssetType).name();
		}

#endif

}
