// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AssetSetManager.h"
#include "AssetHeap.h"
#include "AssetServices.h"
#include "../Utility/Threading/ThreadingUtils.h"
#include "../Utility/Threading/Mutex.h"
#include "../Utility/IteratorUtils.h"
#include <vector>
#include <memory>
#include <assert.h>

namespace Assets
{
	class AssetSetManager::Pimpl
	{
	public:
		std::vector<std::pair<size_t, std::unique_ptr<IDefaultAssetHeap>>> _sets;
		std::vector<std::pair<size_t, std::unique_ptr<IDefaultAssetHeap>>> _setsPendingIteration;
		std::vector<std::pair<unsigned, std::function<void()>>> _frameBarrierFunctions;
		std::vector<std::pair<unsigned, std::function<void()>>> _pendingFrameBarrierFunctions;
		std::vector<uint64_t> _hintMarkerUpdates;
		std::vector<unsigned> _pendingRemoveFrameBarrierFunctions;
		unsigned _nextFrameBufferMarkerId = 1;
		Threading::RecursiveMutex _lock;
		Threading::ThreadId _boundThreadId;

		bool _inIterationOperation = false;

		struct UpdatePropagation
		{
			Signal<IteratorRange<const std::pair<uint64_t, AssetHeapRecord>*>> _signal;
		};
		std::shared_ptr<UpdatePropagation> _updatePropagation;
		std::vector<std::pair<size_t, SignalId>> _registeredSignals;
	};

	IDefaultAssetHeap * AssetSetManager::GetSetForTypeCode(size_t typeCode)
	{
		auto i = LowerBound(_pimpl->_sets, typeCode);
		if (i != _pimpl->_sets.end() && i->first == typeCode)
			return i->second.get();

		i = LowerBound(_pimpl->_setsPendingIteration, typeCode);
		if (i != _pimpl->_setsPendingIteration.end() && i->first == typeCode)
			return i->second.get();

		return nullptr;
	}

	void AssetSetManager::Add(size_t typeCode, std::unique_ptr<IDefaultAssetHeap>&& set)
	{
		auto* target = &_pimpl->_sets;
		// We can't modify "_sets" while we're iterating through it.
		// As a simple solution, just keep the these new sets in a pending list
		if (_pimpl->_inIterationOperation)
			target = &_pimpl->_setsPendingIteration;

		auto signalId = set->BindUpdateSignal(
			[updatePropagation=_pimpl->_updatePropagation](IteratorRange<const std::pair<uint64_t, AssetHeapRecord>*> updates) {
				// propagate to our signals
				updatePropagation->_signal.Invoke(updates);
			});
		_pimpl->_registeredSignals.emplace_back(typeCode, signalId);

		auto i = LowerBound(*target, typeCode);
		assert(i == target->end() || i->first != typeCode);
		target->insert(i, std::make_pair(typeCode, std::move(set)));
	}

    void AssetSetManager::Clear()
    {
        ScopedLock(_pimpl->_lock);
		assert(!_pimpl->_inIterationOperation);
		for (auto signal:_pimpl->_registeredSignals) {
			auto i = LowerBound(_pimpl->_sets, signal.first);
			if (i != _pimpl->_sets.end() && i->first == signal.first)
				i->second->UnbindUpdateSignal(signal.second);
		}
		_pimpl->_registeredSignals.clear();
        _pimpl->_sets.clear();
		_pimpl->_setsPendingIteration.clear();
		_pimpl->_frameBarrierFunctions.clear();
		_pimpl->_pendingFrameBarrierFunctions.clear();
		_pimpl->_pendingRemoveFrameBarrierFunctions.clear();
    }

    unsigned AssetSetManager::GetAssetSetCount()
    {
        return unsigned(_pimpl->_sets.size());
    }

    const IDefaultAssetHeap* AssetSetManager::GetAssetSet(unsigned index)
    {
		// -- note -- pending sets can't be returned
        return _pimpl->_sets[index].second.get();
    }

    void AssetSetManager::Lock()
    {
        _pimpl->_lock.lock();
    }

    void AssetSetManager::Unlock()
    {
        _pimpl->_lock.unlock();
    }

	void AssetSetManager::OnFrameBarrier()
	{
		assert(_pimpl->_boundThreadId == Threading::CurrentThreadId());
		std::unique_lock<decltype(_pimpl->_lock)> lock(_pimpl->_lock);
		_pimpl->_inIterationOperation = true;

		TRY {
			lock = {};
			for (auto&fn:_pimpl->_frameBarrierFunctions)
				fn.second();
			lock = std::unique_lock<decltype(_pimpl->_lock)>(_pimpl->_lock);

			// If we queued up any new sets to add to the main list, handle them now
			for (auto&set:_pimpl->_setsPendingIteration)
				_pimpl->_sets.insert(LowerBound(_pimpl->_sets, set.first), std::move(set));
			_pimpl->_setsPendingIteration.clear();

			_pimpl->_frameBarrierFunctions.insert(_pimpl->_frameBarrierFunctions.end(), _pimpl->_pendingFrameBarrierFunctions.begin(), _pimpl->_pendingFrameBarrierFunctions.end());
			_pimpl->_pendingFrameBarrierFunctions.clear();

			for (auto hintMarkerUpdate:_pimpl->_hintMarkerUpdates) {
				auto i = LowerBound(_pimpl->_sets, hintMarkerUpdate);
				if (i != _pimpl->_sets.end() && i->first == hintMarkerUpdate)
					i->second->UpdateMarkerStates();
			}
			_pimpl->_hintMarkerUpdates.clear();

			// we have to be a little bit careful here, because destroying _frameBarrierFunctions can result
			// in calling an arbitrary destructor which could end up modifying _pendingRemoveFrameBarrierFunctions during this loop
			for (size_t c=0; c<_pimpl->_pendingRemoveFrameBarrierFunctions.size(); ++c) {
				auto r = _pimpl->_pendingRemoveFrameBarrierFunctions[c];
				auto i = LowerBound(_pimpl->_frameBarrierFunctions, r);
				if (i!=_pimpl->_frameBarrierFunctions.end() && i->first == r)
					_pimpl->_frameBarrierFunctions.erase(i);
			}
			_pimpl->_pendingRemoveFrameBarrierFunctions.clear();
		} CATCH (...) {
			_pimpl->_inIterationOperation = false;
			throw;
		} CATCH_END
		_pimpl->_inIterationOperation = false;
	}

	unsigned AssetSetManager::RegisterFrameBarrierCallback(std::function<void()>&& fn)
	{
		ScopedLock(_pimpl->_lock);
		auto result = _pimpl->_nextFrameBufferMarkerId++;
		if (!_pimpl->_inIterationOperation) {
			_pimpl->_frameBarrierFunctions.emplace_back(std::make_pair(result, std::move(fn)));
		} else {
			_pimpl->_pendingFrameBarrierFunctions.emplace_back(std::make_pair(result, std::move(fn)));
		}
		return result;
	}

	void AssetSetManager::DeregisterFrameBarrierCallback(unsigned markerId)
	{
		ScopedLock(_pimpl->_lock);
		if (!_pimpl->_inIterationOperation) {
			auto i = LowerBound(_pimpl->_frameBarrierFunctions, markerId);
			if (i!=_pimpl->_frameBarrierFunctions.end() && i->first == markerId)
				_pimpl->_frameBarrierFunctions.erase(i);
		} else {
			_pimpl->_pendingRemoveFrameBarrierFunctions.push_back(markerId);
		}
	}

	void AssetSetManager::HintMarkerUpdated(uint64_t typeId)
	{
		ScopedLock(_pimpl->_lock);
		auto i = std::lower_bound(_pimpl->_hintMarkerUpdates.begin(), _pimpl->_hintMarkerUpdates.end(), typeId);
		if (i == _pimpl->_hintMarkerUpdates.end() || *i != typeId)
			_pimpl->_hintMarkerUpdates.insert(i, typeId);
	}

	auto AssetSetManager::BindUpdateSignal(std::function<UpdateSignalSig>&& fn) -> SignalId
	{
		// We can't easily send through the existing state when we first bind. This would require
		// either keeping a record of every asset state in the manager, or iterating through each
		// asset heap to get it's current state -- but there are race conditions if we do it that way
		// (or, alternatively, binding this signal to every heap)
		return _pimpl->_updatePropagation->_signal.Bind(std::move(fn));
	}

	void AssetSetManager::UnbindUpdateSignal(SignalId signalId)
	{
		_pimpl->_updatePropagation->_signal.Unbind(signalId);
	}

	static Threading::ThreadId s_mainThreadId;

    AssetSetManager::AssetSetManager()
    {
        _pimpl = std::make_unique<Pimpl>();
		_pimpl->_boundThreadId = s_mainThreadId = Threading::CurrentThreadId();
		_pimpl->_updatePropagation = std::make_shared<Pimpl::UpdatePropagation>();
    }

    AssetSetManager::~AssetSetManager()
    {}


	namespace Internal
	{
		unsigned RegisterFrameBarrierCallback(std::function<void()>&& fn)
		{
			return Services::GetAssetSets().RegisterFrameBarrierCallback(std::move(fn));
		}

		void DeregisterFrameBarrierCallback(unsigned markerId, uint64_t typeHint)
		{
			// This can be called while the asset set manager is being shutdown; at that time it is
			// not available as a singleton
			if (Services::HasAssetSets()) {
				Services::GetAssetSets().DeregisterFrameBarrierCallback(markerId);
				Services::GetAssetSets().HintMarkerUpdated(typeHint);
			}
		}

		void CheckMainThreadStall(std::chrono::steady_clock::time_point& stallStartTime)
		{
			if (Threading::CurrentThreadId() == s_mainThreadId) {
				auto now = std::chrono::steady_clock::now();
				auto timeDiff = now - stallStartTime;
				if (timeDiff > std::chrono::milliseconds(100)) {
					Log(Warning) << "Long stall on main thread while waiting for asset (" << std::chrono::duration_cast<std::chrono::milliseconds>(timeDiff).count() << ") milliseconds" << std::endl;
					stallStartTime = now;
				}
			}
		}
	}

	IAssetTracking::~IAssetTracking() {}
	IDefaultAssetHeap::~IDefaultAssetHeap() {}

}
