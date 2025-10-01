
#pragma once

#include "AssetTraits.h"
#include "AssetsCore.h"
#include "DepVal.h"
#include "ContinuationInternal.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/Threading/CompletionThreadPool.h"
#include "../Utility/Threading/Mutex.h"
#include "../Core/Aliases.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <future>
#include <shared_mutex>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <typeindex>
#include <memory>
#include "thousandeyes/futures/Executor.h"

namespace AssetsNew
{
	using RWM = Threading::ReadWriteMutex;

	class AssetHeap
	{
	public:
		using IdentifierCode = uint64_t;
		using VisibilityMarkerId = uint64_t;
		T1(Type) class Table;
		T1(Type) class Iterator;

		T1(Type) void Insert(IdentifierCode, std::string initializer, std::shared_future<Type>&&);
		T1(Type) void Insert(IdentifierCode, std::string initializer, std::future<Type>&&);
		T1(Type) void Insert(IdentifierCode, std::string initializer, Type&&);
		T1(Type) void Erase(IdentifierCode);
		T1(Type) void Erase(Iterator<Type>&);

		T1(Type) IteratorRange<Iterator<Type>> MakeRange();
		T1(Type) IteratorRange<Iterator<Type>> MakeRange() const;
		T1(Type) Iterator<Type> Begin();
		T1(Type) Iterator<Type> End();
		T1(Type) Iterator<Type> Lookup(IdentifierCode);

		T1(Type) std::unique_lock<RWM> WriteLock();
		T1(Type) void InsertAlreadyLocked(IdentifierCode, std::string, std::shared_future<Type>&&);
		T1(Type) Iterator<Type> LookupAlreadyLocked(IdentifierCode);		// Use with caution! The iterator normally keeps a read lock for it's entire lifetime; but will not in this case!

		VisibilityMarkerId VisibilityBarrier();

		explicit AssetHeap(std::shared_ptr<thousandeyes::futures::Executor> =nullptr);
		~AssetHeap();

	protected:
		struct InternalTable
		{
			sp<void> _table;
			void (*_checkCompletionFn)(void*, IteratorRange<const p<IdentifierCode, unsigned>*>, VisibilityMarkerId);
		};
		std::mutex _tableLock;
		vp<uint64_t, InternalTable> _tables;

		std::mutex _checkList;
		struct FutureToCheck { uint64_t _collection, _asset; };
		std::vector<FutureToCheck> _futuresToCheck;

		VisibilityMarkerId _lastVisibilityMarker = 0;

		T1(Type) Table<Type>* FindTableForType();
		constexpr static unsigned s_tableSpacing = 8;

		std::shared_ptr<thousandeyes::futures::Executor> _continuationExecutor;
		struct CheckFuturesHelper;
		std::shared_ptr<CheckFuturesHelper> _checkFuturesHelper;

		T1(FutureType) void WatchCompletion(FutureType&& future, std::type_index type, IdentifierCode code, unsigned valIdx);
	};


	T1(Type) class AssetHeap::Table
	{
	public:
		using ValidationIndex = unsigned;
		v<IdentifierCode> _idLookup;
		v<Type> _completed;
		v<std::shared_future<Type>> _completedFutures;
		v<std::shared_future<Type>> _pendingFutures;
		
		// We could alternatively store a AssetHeapRecord, rather than have so many parallel arrays
		v<::Assets::Blob> _completedActualizationLogs;
		v<::Assets::DependencyValidation> _completedDepVals;
		v<::Assets::AssetState> _states;
		v<std::string> _initializers;
		v<VisibilityMarkerId> _visibilityBarriers;
		v<ValidationIndex> _valIndices;

		RWM _mutex;

		IteratorRange<AssetHeap::Iterator<Type>> MakeRange();
		AssetHeap::Iterator<Type> begin();
		AssetHeap::Iterator<Type> end();
		AssetHeap::Iterator<Type> Lookup(IdentifierCode);

		std::unique_lock<RWM> WriteLock();
		AssetHeap::Iterator<Type> LookupAlreadyLocked(IdentifierCode);		// Use with caution! The iterator normally keeps a read lock for it's entire lifetime; but will not in this case!

		Table();
		~Table();

	protected:
		void CheckCompletion(IteratorRange<const p<IdentifierCode, ValidationIndex>*>, VisibilityMarkerId);		// expecting sorted
		void CompleteIndividualAlreadyLocked(size_t idx, VisibilityMarkerId barrier);
		void StallWhilePending(IdentifierCode id, std::shared_lock<RWM>&&);
		::Assets::AssetState StallWhilePendingFor(IdentifierCode id, std::chrono::microseconds timeout, std::shared_lock<RWM>&&);
		::Assets::AssetState StallWhilePendingUntil(IdentifierCode id, std::chrono::steady_clock::time_point timeout, std::shared_lock<RWM>&&);

		ValidationIndex Insert(IdentifierCode, std::string initializer, std::shared_future<Type>&&);
		ValidationIndex Insert(IdentifierCode, std::string initializer, VisibilityMarkerId, Type&&);
		ValidationIndex InsertAlreadyLocked(IdentifierCode, std::string initializer, std::shared_future<Type>&&);
		void Erase(IdentifierCode);
		friend class AssetHeap;
		friend class AssetHeap::Iterator<Type>;

		std::atomic<unsigned> _stallWhilePendingCounter;
		std::atomic<bool> _shuttingDown;
	};

	T1(Type) class AssetHeap::Iterator
	{
	public:
		std::shared_future<Type> GetFuture() const;
		::Assets::AssetState GetState() const;
		::Assets::Blob GetActualizationLog() const;
		std::string GetInitializer() const;
		IdentifierCode Id() const;
		const ::Assets::DependencyValidation& GetDependencyValidation() const;

		// Note that TryActualize / Actualize return pointers into the heap. These are only
		// valid during the lifetime of the Iterator, because we need the read lock
		Type* TryActualize() const;
		Type& Actualize() const;

		// StallWhilePending variants will invalidate this (ie, you must search again for the result in order to use it)
		void StallWhilePending();
		::Assets::AssetState StallWhilePendingFor(std::chrono::microseconds);
		::Assets::AssetState StallWhilePendingUntil(std::chrono::steady_clock::time_point);

		Iterator& operator++();
		Iterator& operator+=(ptrdiff_t);
		operator bool() const;

		friend bool operator==(const Iterator& lhs, const Iterator& rhs) { assert(lhs._table == rhs._table); return lhs._index == rhs._index; }
		friend bool operator!=(const Iterator& lhs, const Iterator& rhs) { return !operator==(lhs, rhs); }
		friend size_t operator-(const Iterator& lhs, const Iterator& rhs) { assert(lhs._table == rhs._table); return lhs._index - rhs._index; }		// should this be the difference in the sparse sequence, or the dense sequence?

		// deref invokes Actualize() (as opposed to TryActualize)
		using value_type = Type;
		Type& get() const { return Actualize(); }
		Type& operator*() const { return Actualize(); }
		Type* operator->() const { return &Actualize(); }

		Iterator() = default;
		~Iterator() = default;
		Iterator(Iterator&&) = default;
		Iterator& operator=(Iterator&&) = default;

	protected:
		std::shared_lock<RWM> _readLock;
		AssetHeap::Table<Type>* _table = nullptr;
		size_t _index = ~size_t(0);

		friend class AssetHeap::Table<Type>;
		Iterator(std::shared_lock<RWM> readLock, AssetHeap::Table<Type>* table, size_t index)
		: _readLock(std::move(readLock)), _table(table), _index(index) {}
	};


////////////////////////////////////////////////////////////////////////////////////

	T1(Type) IteratorRange<AssetHeap::Iterator<Type>> AssetHeap::Table<Type>::MakeRange()
	{
		if (_idLookup.empty()) return {};
		return IteratorRange<AssetHeap::Iterator<Type>> {
			AssetHeap::Iterator<Type> {
				std::shared_lock<RWM> { _mutex }, this, 0
			},
			AssetHeap::Iterator<Type> {
				std::shared_lock<RWM> { _mutex }, this, _idLookup.size()
			}
		};
	}

	T1(Type) AssetHeap::Iterator<Type> AssetHeap::Table<Type>::begin()
	{
		if (_idLookup.empty()) return {};
		return AssetHeap::Iterator<Type> { std::shared_lock<RWM> { _mutex }, this, 0 };
	}

	T1(Type) AssetHeap::Iterator<Type> AssetHeap::Table<Type>::end()
	{
		if (_idLookup.empty()) return {};
		return AssetHeap::Iterator<Type> { std::shared_lock<RWM> { _mutex }, this, _idLookup.size() };
	}

	T1(Type) AssetHeap::Iterator<Type> AssetHeap::Table<Type>::Lookup(IdentifierCode id)
	{
		std::shared_lock<RWM> readLock { _mutex };
		auto i = std::lower_bound(b2e(_idLookup), id);
		if (i == _idLookup.end() || *i != id)
			return {};		// didn't find it
		AssetHeap::Iterator<Type> result;
		result._readLock = std::move(readLock);
		result._table = this;
		result._index = i-_idLookup.begin();
		return result;
	}

	T1(Type) AssetHeap::Iterator<Type> AssetHeap::Table<Type>::LookupAlreadyLocked(IdentifierCode id)
	{
		auto i = std::lower_bound(b2e(_idLookup), id);
		if (i == _idLookup.end() || *i != id)
			return {};		// didn't find it
		AssetHeap::Iterator<Type> result;
		result._readLock = {};		// no lock!
		result._table = this;
		result._index = i-_idLookup.begin();
		return result;
	}

	T1(Type) std::unique_lock<RWM> AssetHeap::Table<Type>::WriteLock()
	{
		return std::unique_lock<RWM> { _mutex };
	}

	T1(Type) void AssetHeap::Table<Type>::CheckCompletion(IteratorRange<const p<IdentifierCode, ValidationIndex>*> codes, VisibilityMarkerId barrier)
	{
		// Check the status of the identifiers we've been passed, and move them into the
		// completed list if we can
		// This is should be called from the VisibilityBarrier function of the AssetHeap
		assert(std::is_sorted(codes.begin(), codes.end()));
		assert(!codes.empty());

		std::unique_lock<RWM> writeLock { _mutex };
		auto i = _idLookup.begin();
		for (auto c:codes) {
			i = std::lower_bound(i, _idLookup.end(), c.first);
			if (i == _idLookup.end() || *i != c.first) {
				assert(0);		// checking an entry that doesn't exist
				continue;
			}

			// has this future just completed?
			auto idx = i - _idLookup.begin();
			if (_valIndices[idx] != c.second) {
				assert(c.second < _valIndices[idx]);
				continue;		// we've overwritten this slot while the future was completing
			}

			CompleteIndividualAlreadyLocked(idx, barrier);
		}
	}

	template<typename Cleanup> static auto OnExitScope(Cleanup&&c) { return std::unique_ptr<void, Cleanup>{(void*)1, std::move(c)}; } 

	T1(Type) void AssetHeap::Table<Type>::StallWhilePending(IdentifierCode id, std::shared_lock<RWM>&& consumedLock)
	{
		assert(consumedLock.owns_lock());
		if (_shuttingDown) { consumedLock.unlock(); Throw(std::runtime_error("StallWhilePending called on AssetHeap::Table that is shutting down")); }
		++_stallWhilePendingCounter;
		auto onExitScope = OnExitScope([counter=&this->_stallWhilePendingCounter](void*) { --(*counter); });
		consumedLock.unlock();

		std::shared_future<Type> future;
		{
			std::shared_lock<RWM> readLock { _mutex };
			auto i = std::lower_bound(b2e(_idLookup), id);
			if (i == _idLookup.end() || *i != id)
				return;		// didn't find it

			auto idx = i-_idLookup.begin(); 
			if (_states[idx] != ::Assets::AssetState::Pending)
				return;
			assert(_pendingFutures[idx].valid());
			future = _pendingFutures[idx];
		}

		// We take no locks during the yield (no read, no write)
		Utility::YieldToPool(future);

		// Just like CheckCompletion, update the asset with the new state
		{
			std::unique_lock<RWM> writeLock { _mutex };
			auto i = std::lower_bound(b2e(_idLookup), id);
			if (i == _idLookup.end() || *i != id)
				return;		// erased during this operation
			CompleteIndividualAlreadyLocked(i-_idLookup.begin(), ~VisibilityMarkerId(0));
		}
	}

	T1(Type) ::Assets::AssetState AssetHeap::Table<Type>::StallWhilePendingFor(IdentifierCode id, std::chrono::microseconds timeout, std::shared_lock<RWM>&& consumedLock)
	{
		assert(consumedLock.owns_lock());
		if (_shuttingDown) { consumedLock.unlock(); Throw(std::runtime_error("StallWhilePending called on AssetHeap::Table that is shutting down")); }
		++_stallWhilePendingCounter;
		auto onExitScope = OnExitScope([counter=&this->_stallWhilePendingCounter](void*) { --(*counter); });
		consumedLock.unlock();

		// If the asset is in pending state, we must wait on the future
		// afterwards, we move the asset into ready state (even though we're outside the VisibilityBarrier)
		//
		// Note that we will still get a CheckCompletion() for the future
		std::shared_future<Type> future;
		{
			std::shared_lock<RWM> readLock { _mutex };
			auto i = std::lower_bound(b2e(_idLookup), id);
			if (i == _idLookup.end() || *i != id)
				return ::Assets::AssetState::Invalid;		// didn't find it

			auto idx = i-_idLookup.begin(); 
			if (_states[idx] != ::Assets::AssetState::Pending)
				return _states[idx];
			assert(_pendingFutures[idx].valid());
			future = _pendingFutures[idx];
		}

		// We take no locks during the yield (no read, no write)
		auto status = Utility::YieldToPoolFor(future, timeout);
		if (status == std::future_status::timeout)
			return ::Assets::AssetState::Pending;

		// Just like CheckCompletion, update the asset with the new state
		{
			std::unique_lock<RWM> writeLock { _mutex };
			auto i = std::lower_bound(b2e(_idLookup), id);
			if (i == _idLookup.end() || *i != id)
				return ::Assets::AssetState::Invalid;		// erased during this operation
			CompleteIndividualAlreadyLocked(i-_idLookup.begin(), ~VisibilityMarkerId(0));
			return _states[i-_idLookup.begin()];
		}
	}

	T1(Type) ::Assets::AssetState AssetHeap::Table<Type>::StallWhilePendingUntil(IdentifierCode id, std::chrono::steady_clock::time_point timeout, std::shared_lock<RWM>&& consumedLock)
	{
		assert(consumedLock.owns_lock());
		if (_shuttingDown) { consumedLock.unlock(); Throw(std::runtime_error("StallWhilePending called on AssetHeap::Table that is shutting down")); }
		++_stallWhilePendingCounter;
		auto onExitScope = OnExitScope([counter=&this->_stallWhilePendingCounter](void*) { --(*counter); });
		consumedLock.unlock();

		// If the asset is in pending state, we must wait on the future
		// afterwards, we move the asset into ready state (even though we're outside the VisibilityBarrier)
		//
		// Note that we will still get a CheckCompletion() for the future
		std::shared_future<Type> future;
		{
			std::shared_lock<RWM> readLock { _mutex };
			auto i = std::lower_bound(b2e(_idLookup), id);
			if (i == _idLookup.end() || *i != id)
				return ::Assets::AssetState::Invalid;		// didn't find it

			auto idx = i-_idLookup.begin(); 
			if (_states[idx] != ::Assets::AssetState::Pending)
				return _states[idx];
			assert(_pendingFutures[idx].valid());
			future = _pendingFutures[idx];
		}

		// We take no locks during the yield (no read, no write)
		auto status = Utility::YieldToPoolUntil(future, timeout);
		if (status == std::future_status::timeout)
			return ::Assets::AssetState::Pending;

		// Just like CheckCompletion, update the asset with the new state
		{
			std::unique_lock<RWM> writeLock { _mutex };
			auto i = std::lower_bound(b2e(_idLookup), id);
			if (i == _idLookup.end() || *i != id)
				return ::Assets::AssetState::Invalid;		// erased during this operation
			CompleteIndividualAlreadyLocked(i-_idLookup.begin(), ~VisibilityMarkerId(0));
			return _states[i-_idLookup.begin()];
		}
	}

	T1(Type) void AssetHeap::Table<Type>::CompleteIndividualAlreadyLocked(size_t idx, VisibilityMarkerId barrier)
	{
		TRY
		{
			if (_pendingFutures[idx].valid()) {
				_completedFutures[idx] = std::move(_pendingFutures[idx]);
				_visibilityBarriers[idx] = barrier;
				_completed[idx] = _completedFutures[idx].get();
				_completedActualizationLogs[idx] = ::Assets::Internal::GetActualizationLog(_completed[idx]);
				_completedDepVals[idx] = ::Assets::Internal::GetDependencyValidation(_completed[idx]);
				_states[idx] = ::Assets::AssetState::Ready;
			} else {
				// we can end up checking the same future multiple times after calling CompleteIndividualAlreadyLocked from StallWhilePending (etc)
			}
		} CATCH(const ::Assets::Exceptions::ConstructionError& e) {
			_completed[idx] = {};
			_completedActualizationLogs[idx] = e.GetActualizationLog();
			_completedDepVals[idx] = e.GetDependencyValidation();
			_states[idx] = ::Assets::AssetState::Invalid;
		} CATCH(const ::Assets::Exceptions::InvalidAsset& e) {
			_completed[idx] = {};
			_completedActualizationLogs[idx] = e.GetActualizationLog();
			_completedDepVals[idx] = e.GetDependencyValidation();
			_states[idx] = ::Assets::AssetState::Invalid;
		} CATCH(const ::Assets::Exceptions::ExceptionWithDepVal& e) {
			_completed[idx] = {};
			_completedActualizationLogs[idx] = ::Assets::AsBlob(e.what());
			_completedDepVals[idx] = e.GetDependencyValidation();
			_states[idx] = ::Assets::AssetState::Invalid;
		} CATCH(const std::exception& e) {
			// we've gone invalid (no dep val)
			_completed[idx] = {};
			_completedActualizationLogs[idx] = ::Assets::AsBlob(e.what());
			_completedDepVals[idx] = {};
			_states[idx] = ::Assets::AssetState::Invalid;
		} CATCH_END
	}

	T1(Type) auto AssetHeap::Table<Type>::InsertAlreadyLocked(IdentifierCode id, std::string initializer, std::shared_future<Type>&& future) -> ValidationIndex
	{
		// we assume that the future isn't completed -- and we'll wait for the AssetHeap to call CheckCompletion in all cases

		auto i = std::lower_bound(_idLookup.begin(), _idLookup.end(), id);
		if (i == _idLookup.end() || *i != id) {
			i = _idLookup.insert(i, id);

			auto idx = i - _idLookup.begin();
			_completed.emplace(_completed.begin() + idx);
			_completedFutures.emplace(_completedFutures.begin() + idx);
			_completedActualizationLogs.emplace(_completedActualizationLogs.begin() + idx);
			_completedDepVals.emplace(_completedDepVals.begin() + idx);
			_visibilityBarriers.emplace(_visibilityBarriers.begin() + idx, ~VisibilityMarkerId(0));
			_states.emplace(_states.begin() + idx, ::Assets::AssetState::Pending);
			_initializers.emplace(_initializers.begin() + idx, std::move(initializer));
			_valIndices.emplace(_valIndices.begin() + idx, 1);
			_pendingFutures.emplace(_pendingFutures.begin() + idx, std::move(future));

			return 1;

		} else {

			auto idx = i - _idLookup.begin();
			_completed[idx] = {};
			_completedFutures[idx] = {};
			_completedActualizationLogs[idx] = {};
			_completedDepVals[idx] = {};
			_visibilityBarriers[idx] = ~VisibilityMarkerId(0);
			_states[idx] = ::Assets::AssetState::Pending;
			_initializers[idx] = std::move(initializer);
			auto valIdx = ++_valIndices[idx];
			_pendingFutures[idx] = std::move(future);

			return valIdx;

		}
	}

	T1(Type) auto AssetHeap::Table<Type>::Insert(IdentifierCode id, std::string initializer, std::shared_future<Type>&& future) -> ValidationIndex
	{
		std::unique_lock<RWM> writeLock { _mutex };
		return InsertAlreadyLocked(id, std::move(initializer), std::move(future));
	}

	T1(Type) auto AssetHeap::Table<Type>::Insert(IdentifierCode id, std::string initializer, VisibilityMarkerId barrier, Type&& asset) -> ValidationIndex
	{
		// we always need a future, event when we're receiving a completed object
		std::promise<Type> p;
		std::shared_future<Type> f = p.get_future();
		p.set_value(asset);

		std::unique_lock<RWM> writeLock { _mutex };
		auto i = std::lower_bound(_idLookup.begin(), _idLookup.end(), id);
		if (i == _idLookup.end() || *i != id) {
			i = _idLookup.insert(i, id);

			auto idx = i - _idLookup.begin();
			_completed.emplace(_completed.begin() + idx, std::move(asset));
			_completedFutures.emplace(_completedFutures.begin() + idx, std::move(f));
			_completedActualizationLogs.emplace(_completedActualizationLogs.begin() + idx, ::Assets::Internal::GetActualizationLog(_completed[idx]));
			_completedDepVals.emplace(_completedDepVals.begin() + idx, ::Assets::Internal::GetDependencyValidation(_completed[idx]));
			_visibilityBarriers.emplace(_visibilityBarriers.begin() + idx, barrier);
			_states.emplace(_states.begin() + idx, ::Assets::AssetState::Ready);
			_initializers.emplace(_initializers.begin() + idx, std::move(initializer));
			_valIndices.emplace(_valIndices.begin() + idx, 1);
			_pendingFutures.emplace(_pendingFutures.begin() + idx);

			return 1;

		} else {

			auto idx = i - _idLookup.begin();
			_completed[idx] = std::move(asset);
			_completedFutures[idx] = std::move(f);
			_completedActualizationLogs[idx] = ::Assets::Internal::GetActualizationLog(_completed[idx]);
			_completedDepVals[idx] = ::Assets::Internal::GetDependencyValidation(_completed[idx]);
			_visibilityBarriers[idx] = barrier;
			_states[idx] = ::Assets::AssetState::Ready;
			_initializers[idx] = std::move(initializer);
			auto valIdx = ++_valIndices[idx];
			_pendingFutures[idx] = {};

			return valIdx;

		}
	}

	T1(Type) void AssetHeap::Table<Type>::Erase(IdentifierCode id)
	{
		std::unique_lock<RWM> writeLock { _mutex };
		auto i = std::lower_bound(_idLookup.begin(), _idLookup.end(), id);
		if (i != _idLookup.end() && *i == id) {
			auto idx = i - _idLookup.begin();
			_idLookup.erase(i);
			_completed.erase(_completed.begin()+idx);
			_completedFutures.erase(_completedFutures.begin()+idx);
			_completedActualizationLogs.erase(_completedActualizationLogs.begin()+idx);
			_completedDepVals.erase(_completedDepVals.begin()+idx);
			_visibilityBarriers.erase(_visibilityBarriers.begin()+idx);
			_states.erase(_states.begin()+idx);
			_initializers.erase(_initializers.begin()+idx);
			_valIndices.erase(_valIndices.begin()+idx);
			_pendingFutures.erase(_pendingFutures.begin()+idx);
		}
	}

	T1(Type) AssetHeap::Table<Type>::Table() : _stallWhilePendingCounter(0), _shuttingDown(false) {}
	T1(Type) AssetHeap::Table<Type>::~Table()
	{
		// Some protections to ensure we don't shutdown while any other threads are inside of StallWhilePending
		_shuttingDown = true;
		while (_stallWhilePendingCounter) std::this_thread::yield();

		// Grab the right lock to ensure all iterators are destroyed
		_mutex.lock();
		_mutex.unlock();
	}

///////////////////////////////////////////////////////////////////////////

	T1(Type) std::shared_future<Type> AssetHeap::Iterator<Type>::GetFuture() const
	{
		assert(_index < _table->_completedFutures.size());
		if (_table->_completedFutures[_index].valid())
			return _table->_completedFutures[_index];
		assert(_table->_pendingFutures[_index].valid());
		return _table->_pendingFutures[_index];
	}

	T1(Type) ::Assets::AssetState AssetHeap::Iterator<Type>::GetState() const
	{
		assert(_index < _table->_states.size());
		return _table->_states[_index];
	}

	T1(Type) ::Assets::Blob AssetHeap::Iterator<Type>::GetActualizationLog() const
	{
		assert(_index < _table->_completedActualizationLogs.size());
		return _table->_completedActualizationLogs[_index];
	}

	T1(Type) std::string AssetHeap::Iterator<Type>::GetInitializer() const
	{
		assert(_index < _table->_initializers.size());
		return _table->_initializers[_index]; 
	}

	T1(Type) const ::Assets::DependencyValidation& AssetHeap::Iterator<Type>::GetDependencyValidation() const
	{
		assert(_index < _table->_completedDepVals.size());
		return _table->_completedDepVals[_index]; 
	}

	T1(Type) auto AssetHeap::Iterator<Type>::Id() const -> IdentifierCode
	{
		assert(_index < _table->_idLookup.size());
		return _table->_idLookup[_index]; 
	}

	T1(Type) Type* AssetHeap::Iterator<Type>::TryActualize() const
	{
		assert(_index < _table->_completed.size());
		auto state = _table->_states[_index];
		if (state != ::Assets::AssetState::Ready)
			return nullptr;
		if (state == ::Assets::AssetState::Invalid)
			Throw(::Assets::Exceptions::InvalidAsset(_table->_initializers[_index], _table->_completedDepVals[_index], _table->_completedActualizationLogs[_index]));
		return &_table->_completed[_index];
	}

	T1(Type) Type& AssetHeap::Iterator<Type>::Actualize() const
	{
		assert(_index < _table->_completed.size());
		auto state = _table->_states[_index];
		if (state == ::Assets::AssetState::Pending)
			Throw(::Assets::Exceptions::PendingAsset(_table->_initializers[_index]));
		if (state == ::Assets::AssetState::Invalid)
			Throw(::Assets::Exceptions::InvalidAsset(_table->_initializers[_index], _table->_completedDepVals[_index], _table->_completedActualizationLogs[_index]));
		return _table->_completed[_index];
	}

	T1(Type) void AssetHeap::Iterator<Type>::StallWhilePending()
	{
		auto id = Id();
		auto* table = _table;
		auto lock = std::move(_readLock);
		*this = {};		// invalidate iterator
		table->StallWhilePending(id, std::move(lock));
	}
	
	T1(Type) ::Assets::AssetState AssetHeap::Iterator<Type>::StallWhilePendingFor(std::chrono::microseconds timeout)
	{
		auto id = Id();
		auto* table = _table;
		auto lock = std::move(_readLock);
		*this = {};		// invalidate iterator
		return table->StallWhilePendingFor(id, timeout, std::move(lock));
	}

	T1(Type) ::Assets::AssetState AssetHeap::Iterator<Type>::StallWhilePendingUntil(std::chrono::steady_clock::time_point timeout)
	{
		auto id = Id();
		auto* table = _table;
		auto lock = std::move(_readLock);
		*this = {};		// invalidate iterator
		return table->StallWhilePendingUntil(id, timeout, std::move(lock));
	}

	T1(Type) auto AssetHeap::Iterator<Type>::operator++() -> Iterator&
	{
		assert(_table);
		assert((_index+1) <= _table->_idLookup.size());
		++_index;
		return *this;
	}

	T1(Type) auto AssetHeap::Iterator<Type>::operator+=(ptrdiff_t adv) -> Iterator&
	{
		assert(_table);
		assert(ptrdiff_t(_index)+adv >= 0);
		assert((_index+adv) <= _table->_idLookup.size());
		_index += adv;
		return *this;
	}

	T1(Type) AssetHeap::Iterator<Type>::operator bool() const
	{
		return _table != nullptr;
	}

///////////////////////////////////////////////////////////////////////////

	T1(Type) auto AssetHeap::FindTableForType() -> Table<Type>*
	{
		ScopedLock(_tableLock);
		auto code = typeid(Type).hash_code();
		auto i = _tables.begin() + (code >> 56ull) * s_tableSpacing;
		auto end = i + s_tableSpacing;
		for (;;) {
			if (expect_evaluation(i->first == code, true))
				return static_cast<Table<Type>*>(i->second._table.get());

			if (i->first < code) {
				++i;
				if (expect_evaluation(i == end, false)) Throw(std::runtime_error("AssetHeap table did not reserve enough space"));
				continue;
			}

			// i->first > code found insert point. Move things back
			if ((end-1)->second._table) Throw(std::runtime_error("AssetHeap table did not reserve enough space"));
			std::move_backward(i, end-1, end);
			i->first = code;
			i->second = InternalTable { std::make_shared<Table<Type>>() };
			i->second._checkCompletionFn = [](void* table, IteratorRange<const p<IdentifierCode, unsigned>*> r, VisibilityMarkerId m) {
				static_cast<Table<Type>*>(table)->CheckCompletion(r, m);
			};
			return static_cast<Table<Type>*>(i->second._table.get());
		}
	}

	struct AssetHeap::CheckFuturesHelper
	{
		std::mutex _lock;
		struct Entry { std::type_index _type; IdentifierCode _code; unsigned _valIdx; };
		std::vector<Entry> _pendingState, _completedState;
	};

	template<typename ContinuationFn, typename... FutureTypes>
		static std::unique_ptr<::Assets::Internal::FlexTimedWaitableJustContinuation<ContinuationFn, std::decay_t<FutureTypes>...>> MakeTimedWaitableJustContinuation(
			ContinuationFn&& continuation,
			FutureTypes... futures)
	{
		return std::make_unique<::Assets::Internal::FlexTimedWaitableJustContinuation<ContinuationFn, std::decay_t<FutureTypes>...>>(
			std::chrono::hours(1), std::tuple<std::decay_t<FutureTypes>...>{std::forward<FutureTypes>(futures)...}, std::move(continuation));
	}

	T1(FutureType) void AssetHeap::WatchCompletion(FutureType&& future, std::type_index type, IdentifierCode code, unsigned valIdx)
    {
        // Watch the future to move it into the completed list when it's ready
		ScopedLock(_checkFuturesHelper->_lock);
		
		AssetHeap::CheckFuturesHelper::Entry e { type, code, valIdx };
		_checkFuturesHelper->_pendingState.emplace_back(e);

		_continuationExecutor->watch(
            MakeTimedWaitableJustContinuation(
				[helper=_checkFuturesHelper, e](auto&&) {
					// record this future as ready to check
					ScopedLock(helper->_lock);
					auto i = std::find_if(helper->_pendingState.begin(), helper->_pendingState.end(),
						[&e](const auto& q) {
							return q._type == e._type && q._code == e._code && q._valIdx == e._valIdx; 
						});
					assert(i != helper->_pendingState.end());
					helper->_pendingState.erase(i);
					helper->_completedState.push_back(e);
				},
				std::move(future)));
    }

	T1(Type) void AssetHeap::Insert(IdentifierCode id, std::string initializer, std::shared_future<Type>&& f)
	{
		auto valIdx = FindTableForType<Type>()->Insert(id, std::move(initializer), std::shared_future<Type>{f});
		WatchCompletion(std::move(f), std::type_index{typeid(Type)}, id, valIdx);
	}

	T1(Type) void AssetHeap::Insert(IdentifierCode id, std::string initializer, std::future<Type>&& f)
	{
		Insert(id, std::move(initializer), std::shared_future<Type>{std::move(f)});
	}

	T1(Type) void AssetHeap::Insert(IdentifierCode id, std::string initializer, Type&& o)
	{
		FindTableForType<Type>()->Insert(id, std::move(initializer), _lastVisibilityMarker+1, std::move(o));
	}

	T1(Type) void AssetHeap::InsertAlreadyLocked(IdentifierCode id, std::string initializer, std::shared_future<Type>&& f)
	{
		auto valIdx = FindTableForType<Type>()->InsertAlreadyLocked(id, std::move(initializer), std::shared_future<Type>{f});
		WatchCompletion(std::move(f), std::type_index{typeid(Type)}, id, valIdx);
	}

	T1(Type) void AssetHeap::Erase(IdentifierCode id)
	{
		FindTableForType<Type>()->Erase(id);
	}

	T1(Type) void AssetHeap::Erase(Iterator<Type>& iterator)
	{
		FindTableForType<Type>()->Erase(iterator.Id());
	}

	T1(Type) auto AssetHeap::MakeRange() -> IteratorRange<Iterator<Type>>
	{
		return FindTableForType<Type>()->MakeRange();
	}

	T1(Type) auto AssetHeap::MakeRange() const -> IteratorRange<Iterator<Type>>
	{
		return FindTableForType<Type>()->MakeRange();
	}

	T1(Type) auto AssetHeap::Begin() -> Iterator<Type>
	{
		return FindTableForType<Type>()->begin();
	}

	T1(Type) auto AssetHeap::End() -> Iterator<Type>
	{
		return FindTableForType<Type>()->end();
	}

	T1(Type) auto AssetHeap::Lookup(IdentifierCode id) -> Iterator<Type>
	{
		return FindTableForType<Type>()->Lookup(id);
	}

	T1(Type) auto AssetHeap::LookupAlreadyLocked(IdentifierCode id) -> Iterator<Type>
	{
		return FindTableForType<Type>()->LookupAlreadyLocked(id);
	}

	T1(Type) std::unique_lock<RWM> AssetHeap::WriteLock()
	{
		return FindTableForType<Type>()->WriteLock();
	}

///////////////////////////////////////////////////////////////////////////

	template<typename Type, typename... Params> ::AssetsNew::AssetHeap::Iterator<Type> StallWhilePending(AssetHeap& heap, Params&&... initialisers)
	{
		auto cacheKey = ::Assets::Internal::BuildParamHash(initialisers...);

		if (auto l = heap.Lookup<Type>(cacheKey)) {
			l.StallWhilePending();		// lookup again because StallWhilePending invalidates the iterator
			l = heap.Lookup<Type>(cacheKey);
			if (!l) Throw(std::runtime_error("Unexpected asset erasure in StallAndActualize"));
			if (l.GetDependencyValidation().GetValidationIndex() <= 0)
				return l;
		}

		// No existing asset, or asset is invalidated. Fall through

		auto lock = heap.WriteLock<Type>();

		// We have to check again for a valid object, incase another thread modified the heap
		// before we took our write lock
		if (auto l = heap.LookupAlreadyLocked<Type>(cacheKey)) {
			if (l.GetState() == ::Assets::AssetState::Pending) {
				// There's been an issue. Another thread has changed this entry unexpectedly. We need to restart from the top
				lock = {};
				return StallWhilePending<Type>(heap, std::forward<Params>(initialisers)...);
			}

			if (l.GetDependencyValidation().GetValidationIndex() <= 0)
				return l;		// another thread changed the entry, and it completed quickly
		}

		std::promise<Type> promise;
		heap.InsertAlreadyLocked<Type>(cacheKey, ::Assets::Internal::AsString(initialisers...), promise.get_future());

		lock = {};
		::Assets::AutoConstructToPromise(std::move(promise), std::forward<Params>(initialisers)...);

		auto l = heap.Lookup<Type>(cacheKey);
		assert(l);
		l.StallWhilePending();
		l = heap.Lookup<Type>(cacheKey);		// lookup again because StallWhilePending invalidates the iterator
		assert(l);

		// note that we return this even if it became invalidated during the construction
		// Also note that we can't return the result of l.Actualize(), because that gives a reference into the table, which is only valid while the iterator lock is held
		return l;
	}

	template<typename Type, typename... Params> ::AssetsNew::AssetHeap::Iterator<Type> Get(AssetHeap& heap, Params&&... initialisers)
	{
		auto cacheKey = ::Assets::Internal::BuildParamHash(initialisers...);

		if (auto l = heap.Lookup<Type>(cacheKey)) {
			if (l.GetDependencyValidation().GetValidationIndex() <= 0)
				return l;
		}

		// No existing asset, or asset is invalidated. Fall through

		auto lock = heap.WriteLock<Type>();

		// We have to check again for a valid object, incase another thread modified the heap
		// before we took our write lock
		if (auto l = heap.LookupAlreadyLocked<Type>(cacheKey)) {
			if (l.GetDependencyValidation().GetValidationIndex() <= 0)
				return l;		// another thread changed the entry, and it completed quickly
		}

		std::promise<Type> promise;
		heap.InsertAlreadyLocked<Type>(cacheKey, ::Assets::Internal::AsString(initialisers...), promise.get_future());

		lock = {}; // after unlocking here, other threads will return the future we've just placed in the heap

		::Assets::AutoConstructToPromise(std::move(promise), std::forward<Params>(initialisers)...);
		return heap.Lookup<Type>(cacheKey);
	}

}

