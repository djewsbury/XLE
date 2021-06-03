#pragma once

#include "AssetsCore.h"
#include "DepVal.h"
#include "IAsyncMarker.h"
#include "../Utility/Threading/Mutex.h"
#include "../Utility/Threading/CompletionThreadPool.h"
#include "../Core/Exceptions.h"
#include <memory>
#include <string>
#include <functional>
#include <assert.h>
#include <utility>

namespace Assets
{

		////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename AssetType>
		using AssetPtr = std::shared_ptr<AssetType>;

	template<typename AssetType>
		class Future : public IAsyncMarker
	{
	public:
		const AssetType& Actualize() const;
		const AssetType& TryActualize() const;

		bool			IsOutOfDate() const;
		void			SimulateChange();
		
		AssetState		            GetAssetState() const;
        std::optional<AssetState>   StallWhilePending(std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) const;

		AssetState		CheckStatusBkgrnd(
			AssetType& actualized,
			DependencyValidation& depVal,
			Blob& actualizationLog);

		const std::string&		    Initializer() const { return _initializer; }
		const DependencyValidation&	GetDependencyValidation() const { return _actualizedDepVal; }
		const Blob&				    GetActualizationLog() const { return _actualizationLog; }

		explicit Future(const std::string& initializer = {});
		~Future();

		Future(Future&&);
		Future& operator=(Future&&);

		void SetAsset(AssetType&&, const Blob& log);
		void SetInvalidAsset(DependencyValidation depVal, const Blob& log);
		void SetAssetForeground(AssetType&& newAsset, const Blob& log);
		void SetPollingFunction(std::function<bool(Future<AssetType>&)>&&);
		
	private:
		mutable Threading::Mutex		_lock;
		mutable Threading::Conditional	_conditional;

		volatile AssetState 	_state;
		AssetType 				_actualized;
		Blob					_actualizationLog;
		DependencyValidation	_actualizedDepVal;

		AssetType 				_pending;
		AssetState				_pendingState;
		Blob					_pendingActualizationLog;
		DependencyValidation	_pendingDepVal;

		std::function<bool(Future<AssetType>&)> _pollingFunction;

		std::string			_initializer;	// stored for debugging purposes
		
		void				OnFrameBarrier();

		struct CallbackMarker { unsigned _markerId; Future<AssetType>* _parent; };
		mutable std::shared_ptr<CallbackMarker> _frameBarrierCallbackMarker;

		void RegisterFrameBarrierCallbackAlreadyLocked();
		void ClearFrameBarrierCallbackAlreadyLocked() const;
	};

	template<typename AssetType>
		using AssetFuture = Future<std::shared_ptr<AssetType>>;

	template<typename AssetType>
		using PtrToFuturePtr = std::shared_ptr<FuturePtr<AssetType>>;

	namespace Internal
	{
		template<typename AssetType> static auto HasGetDependencyValidation_Helper(int) -> decltype(std::declval<AssetType>().GetDependencyValidation(), std::true_type{});
		template<typename...> static auto HasGetDependencyValidation_Helper(...) -> std::false_type;
		template<typename AssetType> struct HasGetDependencyValidation : decltype(HasGetDependencyValidation_Helper<AssetType>(0)) {};

		template<typename AssetType> static auto HasDerefGetDependencyValidation_Helper(int) -> decltype((*std::declval<AssetType>()).GetDependencyValidation(), std::true_type{});
		template<typename...> static auto HasDerefGetDependencyValidation_Helper(...) -> std::false_type;
		template<typename AssetType> struct HasDerefGetDependencyValidation : decltype(HasDerefGetDependencyValidation_Helper<AssetType>(0)) {};

		template<typename AssetType, typename std::enable_if<HasGetDependencyValidation<AssetType>::value>::type* =nullptr>
			decltype(std::declval<AssetType>().GetDependencyValidation()) GetDependencyValidation(const AssetType& asset) { return asset.GetDependencyValidation(); }

		template<typename AssetType, typename std::enable_if<!HasGetDependencyValidation<AssetType>::value && HasDerefGetDependencyValidation<AssetType>::value>::type* =nullptr>
			decltype((*std::declval<AssetType>()).GetDependencyValidation()) GetDependencyValidation(const AssetType& asset) { return (*asset).GetDependencyValidation(); }

		template<typename AssetType, typename std::enable_if<!HasGetDependencyValidation<AssetType>::value && !HasDerefGetDependencyValidation<AssetType>::value>::type* =nullptr>
			inline const DependencyValidation& GetDependencyValidation(const AssetType&) { static DependencyValidation dummy; return dummy; }

		unsigned RegisterFrameBarrierCallback(std::function<void()>&& fn);
		void DeregisterFrameBarrierCallback(unsigned);

		void CheckMainThreadStall(std::chrono::steady_clock::time_point& stallStartTime);

		using FutureResolution_CheckStatusFn = AssetState(*)(void*);
		void FutureResolution_BeginMoment(void* future, FutureResolution_CheckStatusFn);
		void FutureResolution_EndMoment(void* future);
		bool FutureResolution_DeadlockDetection(void* future);

		/**
			FutureResolutionMoment is used to bracket a piece of code that is going to resolve
			the state of an Future. When FutureResolutionMoment begins, the future should
			be in Pending state, and when it ends, it should be in either Ready or Invalid state
			(or at least have that state change queued to happen at the next frame barrier)

			This will bracket resolution code fairly tightly (and only a single thread).
			It's used to detect deadlock scenarios. That is, we can't stall waiting for a future
			during it's own resolution moment.
		*/
		template<typename AssetType>
			class FutureResolutionMoment
		{
		public:
			FutureResolutionMoment(Future<std::shared_ptr<AssetType>>& future) : _future(&future)
			{
				assert(future.GetAssetState() == AssetState::Pending);
				FutureResolution_BeginMoment(
					&future,
					[](void* inputFuture) {
						std::shared_ptr<AssetType> actualized;
						DependencyValidation depVal;
						Blob actualizationLog;
						return ((Future<std::shared_ptr<AssetType>>*)inputFuture)->CheckStatusBkgrnd(actualized, depVal, actualizationLog);
					});
			}
			~FutureResolutionMoment()
			{
				FutureResolution_EndMoment(_future);
			}
		private:
			Future<std::shared_ptr<AssetType>>* _future;
		};
	}

		////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename AssetType>
		const AssetType& Future<AssetType>::Actualize() const
	{
		auto state = _state;
		if (state == AssetState::Ready)
			return _actualized;	// once the state is set to "Ready" neither it nor _actualized can change -- so we've safe to access it without a lock

		if (state == AssetState::Pending) {
			// Note that the asset have have completed loading here -- but it may still be in it's "pending" state,
			// waiting for a frame barrier. Let's include the pending state in the exception message to make it clearer. 
			Throw(Exceptions::PendingAsset(MakeStringSection(_initializer)));
		}
		
		assert(state == AssetState::Invalid);
		Throw(Exceptions::InvalidAsset(MakeStringSection(_initializer), _actualizedDepVal, _actualizationLog));
	}

	template<typename AssetType>
		const AssetType& Future<AssetType>::TryActualize() const
	{
		if (_state == AssetState::Ready)
			return _actualized;

		static AssetType dummy;
		return dummy;
	}

	template<typename AssetType>
		AssetState		Future<AssetType>::CheckStatusBkgrnd(AssetType& actualized, DependencyValidation& depVal, Blob& actualizationLog)
	{
		if (_state == AssetState::Ready) {
			actualized = _actualized;
			depVal = _actualizedDepVal;
			actualizationLog = _actualizationLog;
			return AssetState::Ready;
		}

		{
			std::unique_lock<decltype(_lock)> lock(_lock);
			if (_state != AssetState::Pending) {
				actualized = _actualized;
				depVal = _actualizedDepVal;
				actualizationLog = _actualizationLog;
				if (_pendingState == AssetState::Invalid) { assert(depVal); }
				return _state;
			}
			if (_pendingState != AssetState::Pending) {
				actualized = _pending;
				depVal = _pendingDepVal;
				actualizationLog = _pendingActualizationLog;
				if (_pendingState == AssetState::Invalid) { assert(depVal); }
				return _pendingState;
			}
			if (_pollingFunction) {
				std::function<bool(Future<AssetType>&)> pollingFunction;
				std::swap(pollingFunction, _pollingFunction);
				lock = {};
				bool pollingResult = false;
				TRY {
					pollingResult = pollingFunction(*this);
				} CATCH (const Exceptions::ConstructionError& e) {
					lock = std::unique_lock<decltype(_lock)>(_lock);
					_pendingState = AssetState::Invalid;
					_pendingActualizationLog = AsBlob(e);
					_pendingDepVal = e.GetDependencyValidation();
					actualized = _pending;
					depVal = _pendingDepVal;
					assert(depVal);
					actualizationLog = _pendingActualizationLog;
					return _pendingState;
				} CATCH (const std::exception& e) {
					lock = std::unique_lock<decltype(_lock)>(_lock);
					_pendingState = AssetState::Invalid;
					_pendingActualizationLog = AsBlob(e);
					actualized = _pending;
					depVal = _pendingDepVal;
					assert(depVal);
					actualizationLog = _pendingActualizationLog;
					return _pendingState;
				} CATCH_END

				lock = std::unique_lock<decltype(_lock)>(_lock);
				if (pollingResult) {
					assert(!_pollingFunction);
					std::swap(pollingFunction, _pollingFunction);
				}

				if (_pendingState != AssetState::Pending) {
					actualized = _pending;
					depVal = _pendingDepVal;
					actualizationLog = _pendingActualizationLog;
					if (_pendingState == AssetState::Invalid) { assert(depVal); }
					return _pendingState;
				}
			}
		}

		return AssetState::Pending;
	}

	template<typename AssetType>
		void Future<AssetType>::OnFrameBarrier() 
	{
		auto state = _state;
		if (state != AssetState::Pending) return;

			// lock & swap the asset into the front buffer. We only do this during the "frame barrier" phase, to
			// prevent assets from changing in the middle of a single frame.
		std::unique_lock<decltype(_lock)> lock(_lock);
		if (_pollingFunction) {
			std::function<bool(Future<AssetType>&)> pollingFunction;
            std::swap(pollingFunction, _pollingFunction);
			lock = {};
			TRY {
				bool pollingResult = pollingFunction(*this);
				lock = std::unique_lock<decltype(_lock)>(_lock);
				if (pollingResult) {
					assert(!_pollingFunction);
					std::swap(pollingFunction, _pollingFunction);
				}
			} CATCH (const Exceptions::ConstructionError& e) {
				lock = std::unique_lock<decltype(_lock)>(_lock);
				_pendingState = AssetState::Invalid;
				_pendingActualizationLog = AsBlob(e);
				_pendingDepVal = e.GetDependencyValidation();
			} CATCH (const std::exception& e) {
				lock = std::unique_lock<decltype(_lock)>(_lock);
				_pendingState = AssetState::Invalid;
				_pendingActualizationLog = AsBlob(e);
				_pendingDepVal = {};
			} CATCH_END
		}

		if (!_pollingFunction)
			ClearFrameBarrierCallbackAlreadyLocked();
		if (_state == AssetState::Pending && _pendingState != AssetState::Pending) {
			_actualized = std::move(_pending);
			_actualizationLog = std::move(_pendingActualizationLog);
			_actualizedDepVal = std::move(_pendingDepVal);
			// Note that we must change "_state" last -- because another thread can access _actualized without a mutex lock
			// when _state is set to AssetState::Ready
			// we should also consider a cache flush here to ensure the CPU commits in the correct order
			_state = _pendingState;
		}
	}

	template<typename AssetType>
		bool			Future<AssetType>::IsOutOfDate() const
	{
		auto state = _state;
		if (state == AssetState::Pending) return false;
		return _actualizedDepVal.GetValidationIndex() > 0;
	}

	template<typename AssetType>
		void			Future<AssetType>::SimulateChange()
	{
		assert(0);
		/*auto state = _state;
		if (state == AssetState::Ready || state == AssetState::Invalid) {
			if (_actualizedDepVal)
				_actualizedDepVal->OnChange();
			return;
		}*/

		// else, still pending -- can't do anything right now
	}

	template<typename AssetType>
		AssetState		Future<AssetType>::GetAssetState() const
	{
		return _state;
	}

	template<typename AssetType>
        std::optional<AssetState>   Future<AssetType>::StallWhilePending(std::chrono::milliseconds timeout) const
	{
		if (Internal::FutureResolution_DeadlockDetection((void*)this)) {
			// This future is currently in a "resolution moment"
			// This means that the code that will assign this future to either ready or invalid is
			// higher up in the callstack on this same thread. If we attempt to stall for it here, 
			// the stall will be infinite -- because we need to pass execution back to that resolution
			// moment in order for the future to be resolved
			Throw(std::runtime_error("Detected asset future deadlock scenario in StallWhilePending. Future initializer: " + _initializer));
		}

		using namespace std::chrono_literals;
		auto startTime = std::chrono::steady_clock::now();
        auto timeToCancel = startTime + timeout;

		auto* that = const_cast<Future<AssetType>*>(this);	// hack to defeat the "const" on this method
		std::unique_lock<decltype(that->_lock)> lock(that->_lock);

		// If we have polling function assigned, we have to poll waiting for
		// it to be completed. Threading is a little complicated here, because
		// the pollingFunction is expected to lock our mutex, and it is not
		// recursive.
		// We also don't particularly want the polling function to be called 
		// from multiple threads at the same time. So, let's take ownership of
		// the polling function, and unlock the future while the polling function
		// is working. This will often result in 3 locks on the same mutex in
		// quick succession from this same thread sometimes.
		if (that->_pollingFunction) {
			auto pollingFunction = std::move(that->_pollingFunction);
			lock = {};
			bool isInLock = false;

			for (;;) {
				bool pollingResult = false;

				TRY {
					pollingResult = pollingFunction(*that);
				} CATCH (const Exceptions::ConstructionError& e) {
					lock = std::unique_lock<decltype(_lock)>(_lock);
					that->_pendingState = AssetState::Invalid;
					that->_pendingActualizationLog = AsBlob(e);
					that->_pendingDepVal = e.GetDependencyValidation();
					isInLock = true;		// already locked "that->_lock"
					break;
				} CATCH (const std::exception& e) {
					lock = std::unique_lock<decltype(that->_lock)>(that->_lock);
					that->_pendingState = AssetState::Invalid;
					that->_pendingActualizationLog = AsBlob(e);
					that->_pendingDepVal = {};
					isInLock = true;		// already locked "that->_lock"
					break;
				} CATCH_END

				if (!pollingResult) {
					lock = std::unique_lock<decltype(that->_lock)>(that->_lock);
					// If pollingResult was false, and no replacement polling function 
					// has been set, then we are done, and we break out of the loop
					// If we replacement polling function was set, we now capture that
					// function and continue on as before
					if (!that->_pollingFunction) {
						isInLock = true;
						break;
					}
					pollingFunction = std::move(that->_pollingFunction);
					lock = {};
				}

                if (timeout.count() != 0 && std::chrono::steady_clock::now() >= timeToCancel) {
                    // return the polling function to the future
                    lock = std::unique_lock<decltype(that->_lock)>(that->_lock);
                    assert(!that->_pollingFunction);
                    that->_pollingFunction = std::move(pollingFunction);
					DEBUG_ONLY(Internal::CheckMainThreadStall(startTime));
                    return {};
                }

				// Note that we often get here during thread pool operations. We should always
				// yield to the pool, rather that just sleeping this thread, because otherwise we
				// can easily get into a deadlock situation where all threadpool worker threads 
				// can end up here, waiting on some other operation to execute on the same pool,
				// but it can never happen because all workers are stuck yielding.
				using namespace std::chrono_literals;
				if (timeout.count() != 0) {
					YieldToPoolFor(std::min(std::chrono::duration_cast<std::chrono::microseconds>(timeout), 50us));
				} else {
					YieldToPoolFor(50us);
				}
				DEBUG_ONLY(Internal::CheckMainThreadStall(startTime));
			}
			
			if (!isInLock)
				lock = std::unique_lock<decltype(that->_lock)>(that->_lock);
		}

		for (;;) {
			if (that->_state != AssetState::Pending) {
				DEBUG_ONLY(Internal::CheckMainThreadStall(startTime));
				return (AssetState)that->_state;
			}
			if (that->_pendingState != AssetState::Pending) {
				// Force the background version into the foreground (see OnFrameBarrier)
				// This is required because we can be woken up by SetAsset, which only set the
				// background asset. But the caller most likely needs the asset right now, so
				// we've got to swap it into the foreground.
				// There is a problem if the caller is using bothActualize() and StallWhilePending() on the
				// same asset in the same frame -- in this case, the order can have side effects.
				assert(that->_state == AssetState::Pending);
				ClearFrameBarrierCallbackAlreadyLocked();
				that->_actualized = std::move(that->_pending);
				that->_actualizationLog = std::move(that->_pendingActualizationLog);
				that->_actualizedDepVal = std::move(that->_pendingDepVal);
				that->_state = that->_pendingState;
				DEBUG_ONLY(Internal::CheckMainThreadStall(startTime));
				return (AssetState)that->_state;
			}
            if (timeout.count() != 0) {
                auto waitResult = that->_conditional.wait_until(lock, timeToCancel);
                // If we timed out during wait, we should cancel and return
                if (waitResult == std::cv_status::timeout) {
					DEBUG_ONLY(Internal::CheckMainThreadStall(startTime));
					return {};
				}
            } else {
                that->_conditional.wait(lock);
            }
		}
	}

	template<typename AssetType>
		void Future<AssetType>::SetAsset(AssetType&& newAsset, const Blob& log)
	{
		{
			ScopedLock(_lock);
			_pending = std::move(newAsset);
			_pendingState = AssetState::Ready;
			_pendingActualizationLog = log;
			if (_pending) {
				_pendingDepVal = Internal::GetDependencyValidation(_pending);
			} else
				_pendingDepVal = {};
			RegisterFrameBarrierCallbackAlreadyLocked();

			// If we are already in invalid / ready state, we will never move the pending
			// asset into the foreground. We also cannot change from those states to pending, 
			// because of some other assumptions.
			assert(_state == ::Assets::AssetState::Pending);
		}
		_conditional.notify_all();
	}

	template<typename AssetType>
		void Future<AssetType>::SetAssetForeground(AssetType&& newAsset, const Blob& log)
	{
		// this is intended for "shadowing" assets only; it sets the asset directly into the foreground
		// asset and goes immediately into ready state
		{
			ScopedLock(_lock);
			ClearFrameBarrierCallbackAlreadyLocked();
			_actualized = std::move(newAsset);
			_actualizationLog = log;
			if (_actualized) {
				_actualizedDepVal = Internal::GetDependencyValidation(_actualized);
			} else
				_actualizedDepVal = {};
			_state = _actualized ? AssetState::Ready : AssetState::Invalid;
		}
		_conditional.notify_all();
	}

	template<typename AssetType>
		void Future<AssetType>::SetInvalidAsset(DependencyValidation depVal, const Blob& log)
	{
		assert(depVal);
		{
			ScopedLock(_lock);
			_pending = nullptr;
			_pendingState = AssetState::Invalid;
			_pendingActualizationLog = log;
			_pendingDepVal = std::move(depVal);
			RegisterFrameBarrierCallbackAlreadyLocked();
		}
		_conditional.notify_all();
	}

	template<typename AssetType>
		void Future<AssetType>::SetPollingFunction(std::function<bool(Future<AssetType>&)>&& newFunction)
	{
		// We can often just resolve the polling operation immediately. So go ahead and
		// execute it now to see if we can resolve the polling operation straight out of the block
		if (!newFunction(*this)) {
			ScopedLock(_lock);
			// Note -- in one edge condition, _state can be something other than Pending here. A polling function
			// function can set another polling function on the future while it's processing -- so long as the
			// original polling function returns false. However, in this case, the original polling function may
			// have completely immediately as well, and actually hit this same codeblock and moved the asset into 
			// ready/invalid state already.
			// assert(_state == AssetState::Pending);
			if (!_pollingFunction)
				ClearFrameBarrierCallbackAlreadyLocked();
			if (_state == AssetState::Pending && _pendingState != AssetState::Pending) {
				_actualized = std::move(_pending);
				_actualizationLog = std::move(_pendingActualizationLog);
				_actualizedDepVal = std::move(_pendingDepVal);
				// Note that we must change "_state" last -- because another thread can access _actualized without a mutex lock
				// when _state is set to AssetState::Ready
				// we should also consider a cache flush here to ensure the CPU commits in the correct order
				_state = _pendingState;
			}
			return;
		}

		ScopedLock(_lock);
		assert(!_pollingFunction);
		assert(_state == AssetState::Pending);
		assert(_pendingState == AssetState::Pending && !_pending);
		_pollingFunction = std::move(newFunction);
		RegisterFrameBarrierCallbackAlreadyLocked();
	}

	template<typename AssetType>
		void Future<AssetType>::RegisterFrameBarrierCallbackAlreadyLocked()
	{
		if (_frameBarrierCallbackMarker)
			return;

		_frameBarrierCallbackMarker = std::make_shared<CallbackMarker>();
		_frameBarrierCallbackMarker->_parent = this;
		std::weak_ptr<CallbackMarker> weakMarker = _frameBarrierCallbackMarker;
		_frameBarrierCallbackMarker->_markerId = Internal::RegisterFrameBarrierCallback(
			[weakMarker]() {
				auto l = weakMarker.lock();
				if (!l) return;
				l->_parent->OnFrameBarrier();
			});
	}

	template<typename AssetType>
		void Future<AssetType>::ClearFrameBarrierCallbackAlreadyLocked() const
	{
		if (!_frameBarrierCallbackMarker)
			return;

		Internal::DeregisterFrameBarrierCallback(_frameBarrierCallbackMarker->_markerId);
		_frameBarrierCallbackMarker.reset();
	}

	template<typename AssetType>
		Future<AssetType>::Future(Future&& moveFrom)
	{
		ScopedLock(moveFrom._lock);

		bool needsFrameBufferCallback = moveFrom._frameBarrierCallbackMarker != nullptr;
		moveFrom.ClearFrameBarrierCallbackAlreadyLocked();
		_state = moveFrom._state;
		moveFrom._state = AssetState::Pending;
		_actualized = std::move(moveFrom._actualized);
		_actualizationLog = std::move(moveFrom._actualizationLog);
		_actualizedDepVal = std::move(moveFrom._actualizedDepVal);

		_pendingState = moveFrom._pendingState;
		moveFrom._pendingState = AssetState::Pending;
		_pending = std::move(moveFrom._pending);
		_pendingActualizationLog = std::move(moveFrom._pendingActualizationLog);
		_pendingDepVal = std::move(moveFrom._pendingDepVal);

		_pollingFunction = std::move(moveFrom._pollingFunction);
		_initializer = std::move(moveFrom._initializer);
		if (needsFrameBufferCallback)
			RegisterFrameBarrierCallbackAlreadyLocked();
	}

	template<typename AssetType>
		Future<AssetType>& Future<AssetType>::operator=(Future&& moveFrom)
	{
		std::lock(_lock, moveFrom._lock);
        std::lock_guard<Threading::Mutex> lk1(_lock, std::adopt_lock);
        std::lock_guard<Threading::Mutex> lk2(moveFrom._lock, std::adopt_lock);

		ClearFrameBarrierCallbackAlreadyLocked();

		bool needsFrameBufferCallback = moveFrom._frameBarrierCallbackMarker != nullptr;
		moveFrom.ClearFrameBarrierCallbackAlreadyLocked();
		_state = moveFrom.state;
		moveFrom.state = AssetState::Pending;
		_actualized = std::move(moveFrom._actualized);
		_actualizationLog = std::move(moveFrom._actualizationLog);
		_actualizedDepVal = std::move(moveFrom._actualizedDepVal);

		_pendingState = moveFrom._pendingState;
		moveFrom._pendingState = AssetState::Pending;
		_pending = std::move(moveFrom._pending);
		_pendingActualizationLog = std::move(moveFrom._pendingActualizationLog);
		_pendingDepVal = std::move(moveFrom._pendingDepVal);

		_pollingFunction = std::move(moveFrom._pollingFunction);
		_initializer = std::move(moveFrom._initializer);
		if (needsFrameBufferCallback)
			RegisterFrameBarrierCallbackAlreadyLocked();

		return *this;
	}

	template<typename AssetType>
		Future<AssetType>::Future(const std::string& initializer)
	: _initializer(initializer)
	{
		// Technically, we're not actually "pending" yet, because no background operation has begun.
		// If this future is not bound to a specific operation, we'll be stuck in pending state
		// forever.
		_state = AssetState::Pending;
		_pendingState = AssetState::Pending;
	}

	template<typename AssetType>
		Future<AssetType>::~Future() 
	{
		if (_frameBarrierCallbackMarker)
			Internal::DeregisterFrameBarrierCallback(_frameBarrierCallbackMarker->_markerId);
	}

}
