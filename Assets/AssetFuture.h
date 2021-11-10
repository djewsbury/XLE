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

	namespace Internal
	{
		class FutureShared : public IAsyncMarker
		{
		public:
			const std::string& 			Initializer() const { return _initializer; }

			explicit FutureShared(const std::string& initializer = {});
			~FutureShared();
		protected:
			mutable Threading::Mutex		_lock;
			struct CallbackMarker
			{
				std::atomic<FutureShared*> _parent;
				unsigned _markerId;
				Threading::Mutex _callbackActive;
			};
			mutable std::shared_ptr<CallbackMarker> _frameBarrierCallbackMarker;

			void RegisterFrameBarrierCallbackAlreadyLocked();
			void DisableFrameBarrierCallbackAlreadyLocked() const;
			void EnsureFrameBarrierCallbackStopped() const;
			virtual void OnFrameBarrier(std::unique_lock<Threading::Mutex>& lock) = 0;

			std::string _initializer;	// stored for debugging purposes
		};
	}

	namespace Internal
	{
		template<typename Type> static auto SharedFutureFallback_Helper(int) -> typename std::enable_if<std::is_copy_constructible_v<Type>, std::shared_future<Type>>::type;
		template<typename Type> static auto SharedFutureFallback_Helper(...) -> std::future<Type>;
		template<typename Type> using SharedFutureFallback = decltype(SharedFutureFallback_Helper<Type>(0));
	}

	template<typename Type>
		class Future : public Internal::FutureShared
	{
	public:
		const Type& Actualize() const;
		const Type* TryActualize() const;
		
        std::optional<AssetState>   StallWhilePending(std::chrono::microseconds timeout = std::chrono::microseconds(0)) const override;

		AssetState		            GetAssetState() const override { return _state; }
		const DependencyValidation&	GetDependencyValidation() const { return _actualizedDepVal; }
		const Blob&				    GetActualizationLog() const { return _actualizationLog; }

		AssetState		CheckStatusBkgrnd(Type& actualized, DependencyValidation& depVal, Blob& actualizationLog);
		AssetState		CheckStatusBkgrnd(DependencyValidation& depVal, Blob& actualizationLog);
		const Type& 	ActualizeBkgrnd();

		std::shared_future<Type> ShareFuture() const;
		std::promise<Type> AdoptPromise();

		using PromisedType = Type;

		explicit Future(const std::string& initializer = {});
		~Future();

		Future(Future&&);
		Future& operator=(Future&&);

		void SetAsset(Type&&);
		void SetInvalidAsset(DependencyValidation depVal, const Blob& log);
		void SetAssetForeground(Type&& newAsset);
		void SetPollingFunction(std::function<bool(Future<Type>&)>&&);
		
	private:
		volatile AssetState 	_state;
		Type 					_actualized;
		Blob					_actualizationLog;
		DependencyValidation	_actualizedDepVal;

		std::promise<Type> _pendingPromise;
		Internal::SharedFutureFallback<Type> _pendingFuture;

		std::function<bool(Future<Type>&)> _pollingFunction;

		bool TryRunPollingFunction(std::unique_lock<Threading::Mutex>&);
		void CheckFrameBarrierCallbackAlreadyLocked();

		virtual void			OnFrameBarrier(std::unique_lock<Threading::Mutex>& lock) override;
	};

	template<typename Type>
		using PtrToFuturePtr = std::shared_ptr<FuturePtr<Type>>;

	namespace Internal
	{
		template<typename Type> static auto HasGetDependencyValidation_Helper(int) -> decltype(std::declval<Type>().GetDependencyValidation(), std::true_type{});
		template<typename...> static auto HasGetDependencyValidation_Helper(...) -> std::false_type;
		template<typename Type> struct HasGetDependencyValidation : decltype(HasGetDependencyValidation_Helper<Type>(0)) {};

		template<typename Type> static auto HasDerefGetDependencyValidation_Helper(int) -> decltype((*std::declval<Type>()).GetDependencyValidation(), std::true_type{});
		template<typename...> static auto HasDerefGetDependencyValidation_Helper(...) -> std::false_type;
		template<typename Type> struct HasDerefGetDependencyValidation : decltype(HasDerefGetDependencyValidation_Helper<Type>(0)) {};

		template<typename Type, typename std::enable_if<HasGetDependencyValidation<Type>::value>::type* =nullptr>
			decltype(std::declval<Type>().GetDependencyValidation()) GetDependencyValidation(const Type& asset) { return asset.GetDependencyValidation(); }

		template<typename Type, typename std::enable_if<!HasGetDependencyValidation<Type>::value && HasDerefGetDependencyValidation<Type>::value>::type* =nullptr>
			DependencyValidation GetDependencyValidation(const Type& asset) { return asset ? (*asset).GetDependencyValidation() : DependencyValidation{}; }

		template<typename Type, typename std::enable_if<std::is_same_v<std::decay_t<Type>, DependencyValidation>>::type* =nullptr>
			DependencyValidation GetDependencyValidation(const Type& asset) { return asset; }

		template<typename Type, typename std::enable_if<!HasGetDependencyValidation<Type>::value && !HasDerefGetDependencyValidation<Type>::value && !std::is_same_v<std::decay_t<Type>, DependencyValidation>>::type* =nullptr>
			inline const DependencyValidation& GetDependencyValidation(const Type&) { static DependencyValidation dummy; return dummy; }

		unsigned RegisterFrameBarrierCallback(std::function<void()>&& fn);
		void DeregisterFrameBarrierCallback(unsigned);

		void CheckMainThreadStall(std::chrono::steady_clock::time_point& stallStartTime);

		using PromiseFulFillment_CheckStatusFn = AssetState(*)(void*);
		void PromiseFulFillment_BeginMoment(void* future, PromiseFulFillment_CheckStatusFn);
		void PromiseFulFillment_EndMoment(void* future);
		bool PromiseFulFillment_DeadlockDetection(void* future);

		/**
			PromiseFulfillmentMoment is used to bracket a piece of code that is going to resolve
			the state of an Future. When PromiseFulfillmentMoment begins, the future should
			be in Pending state, and when it ends, it should be in either Ready or Invalid state
			(or at least have that state change queued to happen at the next frame barrier)

			This will bracket resolution code fairly tightly (and only a single thread).
			It's used to detect deadlock scenarios. That is, we can't stall waiting for a future
			during it's own resolution moment.
		*/
		class PromiseFulfillmentMoment
		{
		public:
			template<typename Type>
				PromiseFulfillmentMoment(Future<Type>& future) : _promise(&future)
			{
				assert(future.GetAssetState() == AssetState::Pending);
				PromiseFulFillment_BeginMoment(
					&future,
					[](void* inputFuture) {
						DependencyValidation depVal;
						Blob actualizationLog;
						return ((Future<Type>*)inputFuture)->CheckStatusBkgrnd(depVal, actualizationLog);
					});
			}
			~PromiseFulfillmentMoment()
			{
				PromiseFulFillment_EndMoment(_promise);
			}
		private:
			void* _promise;
		};

		template<typename Future, typename AssetType>
			void TryGetAssetFromFuture(
				Future& future,
				AssetState& state,
				AssetType& actualized,
				Blob& actualizationLog,
				DependencyValidation& actualizedDepVal)
		{
			TRY {
				auto pendingResult = future.get();
				actualized = std::move(pendingResult);
				actualizedDepVal = Internal::GetDependencyValidation(actualized);
				actualizationLog = {};
				state = AssetState::Ready;
			} CATCH (const Exceptions::ConstructionError& e) {
				actualizedDepVal = e.GetDependencyValidation();
				actualizationLog = e.GetActualizationLog();
				state = AssetState::Invalid;
			} CATCH (const Exceptions::InvalidAsset& e) {
				actualizedDepVal = e.GetDependencyValidation();
				actualizationLog = e.GetActualizationLog();
				state = AssetState::Invalid;
			} CATCH (const std::exception& e) {
				actualizedDepVal = {};
				actualizationLog = AsBlob(e);
				state = AssetState::Invalid;
			} CATCH_END
		}

		template<typename Type>
			void SetPromiseInvalidAsset(std::promise<Type>& promise, DependencyValidation depVal, const Blob& log)
		{
			promise.set_exception(std::make_exception_ptr(Exceptions::InvalidAsset({}, depVal, log)));
		}
	}

		////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename Type>
		const Type& Future<Type>::Actualize() const
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

	template<typename Type>
		const Type* Future<Type>::TryActualize() const
	{
		return (_state == AssetState::Ready) ? &_actualized : nullptr;
	}

	template<typename Type>
		bool Future<Type>::TryRunPollingFunction(std::unique_lock<Threading::Mutex>& lock)
	{
		if (!_pollingFunction)
			return false;

		std::function<bool(Future<Type>&)> pollingFunction;
		std::swap(pollingFunction, _pollingFunction);
		lock = {};
		TRY {
			auto pollingResult = pollingFunction(*this);
			lock = std::unique_lock<Threading::Mutex>(_lock);
			if (pollingResult) {
				assert(!_pollingFunction);
				std::swap(pollingFunction, _pollingFunction);
			}
		} CATCH (...) {
			_pendingPromise.set_exception(std::current_exception());
			lock = std::unique_lock<Threading::Mutex>(_lock);
		} CATCH_END

		CheckFrameBarrierCallbackAlreadyLocked();
		return true;
	}

	template<typename Type>
		AssetState		Future<Type>::CheckStatusBkgrnd(Type& actualized, DependencyValidation& depVal, Blob& actualizationLog)
	{
		if (_state == AssetState::Ready) {
			actualized = _actualized;
			depVal = _actualizedDepVal;
			actualizationLog = _actualizationLog;
			return AssetState::Ready;
		}

		{
			std::unique_lock<Threading::Mutex> lock(_lock);
			TryRunPollingFunction(lock);
		}

		auto futureState = _pendingFuture.wait_for(std::chrono::seconds(0));
		if (futureState == std::future_status::ready) {
			AssetState newState;
			Internal::TryGetAssetFromFuture(_pendingFuture, newState, actualized, actualizationLog, depVal);
			return newState;
		} else {
			return AssetState::Pending;
		}
	}

	template<typename Type>
		AssetState Future<Type>::CheckStatusBkgrnd(DependencyValidation& depVal, Blob& actualizationLog)
	{
		if (_state == AssetState::Ready) {
			depVal = _actualizedDepVal;
			actualizationLog = _actualizationLog;
			return AssetState::Ready;
		}

		{
			std::unique_lock<Threading::Mutex> lock(_lock);
			TryRunPollingFunction(lock);
		}

		auto futureState = _pendingFuture.wait_for(std::chrono::seconds(0));
		if (futureState == std::future_status::ready) {
			AssetState newState;
			Type actualized;
			Internal::TryGetAssetFromFuture(_pendingFuture, newState, actualized, actualizationLog, depVal);
			return newState;
		} else {
			return AssetState::Pending;
		}
	}

	template<typename Type>
		const Type& Future<Type>::ActualizeBkgrnd()
	{
		static_assert(std::is_copy_constructible_v<Type>, "ActualizeBkgrnd() and future continuations require an asset type that is copy constructable. This functionality cannot be used on this type.");
		if (_state == AssetState::Ready)
			return _actualized;

		{
			std::unique_lock<Threading::Mutex> lock(_lock);
			TryRunPollingFunction(lock);
		}

		return _pendingFuture.get();
	}

	template<typename Type>
		std::shared_future<Type> Future<Type>::ShareFuture() const
	{
		static_assert(std::is_copy_constructible_v<Type>, "ShareFuture() and future continuations require an asset type that is copy constructable. This functionality cannot be used on this type.");
		return _pendingFuture;
	}

	template<typename Type>
		std::promise<Type> Future<Type>::AdoptPromise()
	{
		// We won't be able to track when the promise is fulfilled, so we'll need to start polling
		// immediately. The polling function will move the state into the foreground after the promise
		// is fulfilled
		ScopedLock(_lock);
		RegisterFrameBarrierCallbackAlreadyLocked();
		return std::move(_pendingPromise);
	}

	template<typename Type>
		void Future<Type>::OnFrameBarrier(std::unique_lock<Threading::Mutex>& lock) 
	{
		auto state = _state;
		if (state != AssetState::Pending) {
			// Log(Warning) << "OnFrameBarrier for non-pending future" << std::endl;
			return;
		}		

			// lock & swap the asset into the front buffer. We only do this during the "frame barrier" phase, to
			// prevent assets from changing in the middle of a single frame.
		TryRunPollingFunction(lock);

		auto futureState = _pendingFuture.wait_for(std::chrono::seconds(0));
		if (futureState == std::future_status::ready) {
			AssetState newState;
			Internal::TryGetAssetFromFuture(_pendingFuture, newState, _actualized, _actualizationLog, _actualizedDepVal);
			// Note that we must change "_state" last -- because another thread can access _actualized without a mutex lock
			// when _state is set to AssetState::Ready
			// we should also consider a cache flush here to ensure the CPU commits in the correct order
			_state = newState;

			assert(!_pollingFunction);
			DisableFrameBarrierCallbackAlreadyLocked();
		}
	}

	template<typename Type>
		void Future<Type>::CheckFrameBarrierCallbackAlreadyLocked()
	{
		// Two reasons to run the frame barrier callback
		//		1. run polling function
		//		2. move background state into foreground state
		// If neither of these are relevant now, we can go ahead and clear it
		if (_state != ::Assets::AssetState::Pending && !_pollingFunction)
			DisableFrameBarrierCallbackAlreadyLocked();
	}

	template<typename Type>
        std::optional<AssetState>   Future<Type>::StallWhilePending(std::chrono::microseconds timeout) const
	{
		if (Internal::PromiseFulFillment_DeadlockDetection((void*)this)) {
			// This future is currently in a "resolution moment"
			// This means that the code that will assign this future to either ready or invalid is
			// higher up in the callstack on this same thread. If we attempt to stall for it here, 
			// the stall will be infinite -- because we need to pass execution back to that resolution
			// moment in order for the future to be resolved
			Throw(std::runtime_error("Detected asset future deadlock scenario in StallWhilePending. Future initializer: " + _initializer));
		}

		auto startTime = std::chrono::steady_clock::now();
        auto timeToCancel = startTime + timeout;

		auto* that = const_cast<Future<Type>*>(this);	// hack to defeat the "const" on this method
		std::unique_lock<Threading::Mutex> lock(that->_lock);

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
				} CATCH (...) {
					that->_pendingPromise.set_exception(std::current_exception());
				} CATCH_END

				if (!pollingResult) {
					lock = std::unique_lock<Threading::Mutex>(that->_lock);
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
                    lock = std::unique_lock<Threading::Mutex>(that->_lock);
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
				lock = std::unique_lock<Threading::Mutex>(that->_lock);

			that->CheckFrameBarrierCallbackAlreadyLocked();
		}

		lock = {};

		for (;;) {
			if (that->_state != AssetState::Pending) {
				DEBUG_ONLY(Internal::CheckMainThreadStall(startTime));
				return (AssetState)that->_state;
			}
			std::future_status waitResult;
			if (timeout.count() != 0) {
				waitResult = that->_pendingFuture.wait_until(timeToCancel);
				if (waitResult == std::future_status::timeout) {
					DEBUG_ONLY(Internal::CheckMainThreadStall(startTime));
					return {};
				}
			} else
				that->_pendingFuture.wait();

			// Force the background version into the foreground (see OnFrameBarrier)
			// This is required because we can be woken up by SetAsset, which only set the
			// background asset. But the caller most likely needs the asset right now, so
			// we've got to swap it into the foreground.
			// There is a problem if the caller is using bothActualize() and StallWhilePending() on the
			// same asset in the same frame -- in this case, the order can have side effects.
			AssetState newState;
			Type newActualized;
			Blob newActualizationLog;
			DependencyValidation newDepVal;
			Internal::TryGetAssetFromFuture(that->_pendingFuture, newState, newActualized, newActualizationLog, newDepVal);
			
			lock = std::unique_lock<Threading::Mutex>(that->_lock);
			assert(that->_state == AssetState::Pending);
			that->_actualized = std::move(newActualized);
			that->_actualizationLog = std::move(newActualizationLog);
			that->_actualizedDepVal = std::move(newDepVal);
			that->_state = newState;

			that->DisableFrameBarrierCallbackAlreadyLocked();
			DEBUG_ONLY(Internal::CheckMainThreadStall(startTime));
			return (AssetState)that->_state;
		}
	}

	template<typename Type>
		void Future<Type>::SetAsset(Type&& newAsset)
	{
		// If we are already in invalid / ready state, we will never move the pending
		// asset into the foreground. We also cannot change from those states to pending, 
		// because of some other assumptions.
		assert(_state == ::Assets::AssetState::Pending);

		_pendingPromise.set_value(std::move(newAsset));
		ScopedLock(_lock);
		RegisterFrameBarrierCallbackAlreadyLocked();		// register single callback event to move into foreground state
	}

	template<typename Type>
		void Future<Type>::SetAssetForeground(Type&& newAsset)
	{
		// this is intended for "shadowing" assets only; it sets the asset directly into the foreground
		// asset and goes immediately into ready state
		_pendingPromise.set_value(newAsset);
		ScopedLock(_lock);
		DisableFrameBarrierCallbackAlreadyLocked();
		_actualized = std::move(newAsset);
		_actualizationLog = {};
		_actualizedDepVal = Internal::GetDependencyValidation(newAsset);
		_state = _actualized ? AssetState::Ready : AssetState::Invalid;
	}

	template<typename Type>
		void Future<Type>::SetInvalidAsset(DependencyValidation depVal, const Blob& log)
	{
		Internal::SetPromiseInvalidAsset(_pendingPromise, depVal, log);
		ScopedLock(_lock);
		RegisterFrameBarrierCallbackAlreadyLocked();		// register single callback event to move into foreground state
	}

	template<typename Type>
		void Future<Type>::SetPollingFunction(std::function<bool(Future<Type>&)>&& newFunction)
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
			if (!_pollingFunction)		// "newFunction" might actually set a new polling function on the future
				DisableFrameBarrierCallbackAlreadyLocked();
			if (_state == AssetState::Pending && _pendingFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
				AssetState newState;
				Internal::TryGetAssetFromFuture(_pendingFuture, newState, _actualized, _actualizationLog, _actualizedDepVal);
				// Note that we must change "_state" last -- because another thread can access _actualized without a mutex lock
				// when _state is set to AssetState::Ready
				// we should also consider a cache flush here to ensure the CPU commits in the correct order
				_state = newState;
			}
			return;
		}

		ScopedLock(_lock);
		assert(!_pollingFunction);
		assert(_state == AssetState::Pending);
		assert(_pendingFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
		_pollingFunction = std::move(newFunction);
		if (_pollingFunction)
			RegisterFrameBarrierCallbackAlreadyLocked();
	}

	template<typename Type>
		Future<Type>::Future(Future&& moveFrom)
	{
		// Note calls to EnsureFrameBarrierCallbackStopped outside of the main lock
		moveFrom.EnsureFrameBarrierCallbackStopped();

		ScopedLock(moveFrom._lock);

		// It's not safe if the moveFrom._frameBarrierCallbackMarker is reinstated by another thread
		// after we called EnsureFrameBarrierCallbackStopped()
		assert(!moveFrom._frameBarrierCallbackMarker);

		_state = moveFrom._state;
		moveFrom._state = AssetState::Pending;
		_actualized = std::move(moveFrom._actualized);
		_actualizationLog = std::move(moveFrom._actualizationLog);
		_actualizedDepVal = std::move(moveFrom._actualizedDepVal);

		_pendingPromise = std::move(moveFrom._pendingPromise);
		_pendingFuture = std::move(moveFrom._pendingFuture);

		_pollingFunction = std::move(moveFrom._pollingFunction);
		_initializer = std::move(moveFrom._initializer);
		if ((_state == AssetState::Pending && _pendingFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) || _pollingFunction)
			RegisterFrameBarrierCallbackAlreadyLocked();
	}

	template<typename Type>
		Future<Type>& Future<Type>::operator=(Future&& moveFrom)
	{
		// Note calls to EnsureFrameBarrierCallbackStopped outside of the main lock
		EnsureFrameBarrierCallbackStopped();
		moveFrom.EnsureFrameBarrierCallbackStopped();

		std::lock(_lock, moveFrom._lock);
        std::lock_guard<Threading::Mutex> lk1(_lock, std::adopt_lock);
        std::lock_guard<Threading::Mutex> lk2(moveFrom._lock, std::adopt_lock);

		// It's not safe if the moveFrom._frameBarrierCallbackMarker is reinstated by another thread
		// after we called EnsureFrameBarrierCallbackStopped()
		assert(!_frameBarrierCallbackMarker);
		assert(!moveFrom._frameBarrierCallbackMarker);

		_state = moveFrom._state;
		moveFrom._state = AssetState::Pending;
		_actualized = std::move(moveFrom._actualized);
		_actualizationLog = std::move(moveFrom._actualizationLog);
		_actualizedDepVal = std::move(moveFrom._actualizedDepVal);

		_pendingPromise = std::move(moveFrom._pendingPromise);
		_pendingFuture = std::move(moveFrom._pendingFuture);

		_pollingFunction = std::move(moveFrom._pollingFunction);
		_initializer = std::move(moveFrom._initializer);
		if ((_state == AssetState::Pending && _pendingFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) || _pollingFunction)
			RegisterFrameBarrierCallbackAlreadyLocked();

		return *this;
	}

	template<typename Type>
		Future<Type>::Future(const std::string& initializer)
	: FutureShared(initializer)
	{
		// Technically, we're not actually "pending" yet, because no background operation has begun.
		// If this future is not bound to a specific operation, we'll be stuck in pending state
		// forever.
		_state = AssetState::Pending;
		if constexpr (std::is_copy_constructible_v<Type>) {
			_pendingFuture = _pendingPromise.get_future().share();
		} else 
			_pendingFuture = _pendingPromise.get_future();
	}

	template<typename Type>
		Future<Type>::~Future() 
	{
	}

		////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		inline void FutureShared::RegisterFrameBarrierCallbackAlreadyLocked()
		{
			if (_frameBarrierCallbackMarker && _frameBarrierCallbackMarker->_parent.load())
				return;

			_frameBarrierCallbackMarker = std::make_shared<CallbackMarker>();
			_frameBarrierCallbackMarker->_parent = this;
			std::weak_ptr<CallbackMarker> weakMarker = _frameBarrierCallbackMarker;
			// Note that if we're in a background thread, then the callback can be called before we
			// even assign "_frameBarrierCallbackMarker->_markerId". That's why we need to be inside
			// of the "_lock" mutex lock here
			_frameBarrierCallbackMarker->_markerId = Internal::RegisterFrameBarrierCallback(
				[weakMarker]() {
					auto l = weakMarker.lock();
					if (!l) {
						// Log(Warning) << "Frame barrier callback function was not cleaned up before asset was destroyed" << std::endl; 
						return;
					}
					ScopedLock(l->_callbackActive);
					// If we don't get a lock straight away, just skip. There's no point in stalling here, anyway, since 
					// we can just wait until the next frame
					auto* parent = l->_parent.load();
					if (!parent) return;
					std::unique_lock<Threading::Mutex> lk{parent->_lock, std::defer_lock};
					if (lk.try_lock()) {
						// We must check "_parent" again after we've taken the lock above
						parent = l->_parent.load();
						if (!parent) return;
						parent->OnFrameBarrier(lk);
					}
				});
		}

		inline void FutureShared::DisableFrameBarrierCallbackAlreadyLocked() const
		{
			// Deregistered the callback, but don't stall waiting if we're currently within a callback. That callback
			// might be waiting on _lock right now, and so could complete when _lock is released
			if (!_frameBarrierCallbackMarker)
				return;

			auto* oldParent = _frameBarrierCallbackMarker->_parent.exchange(nullptr);
			if (oldParent)
				Internal::DeregisterFrameBarrierCallback(_frameBarrierCallbackMarker->_markerId);
		}

		inline void FutureShared::EnsureFrameBarrierCallbackStopped() const
		{
			// This is similar to DisableFrameBarrierCallbackAlreadyLocked(), except it also stalls waiting
			// for the callback if it is currently running in a different thread.
			// Use this in scenarios where ee need to ensure that any OnFrameBarrier callbacks have finished, 
			// and will never be
			// started beyond this point. This is partially critical when destroying the future
			// 
			// Note that the OnFrameBarrier() callback doesn't take a ref count on this during it's callback
			// (it only ref the _frameBarrierCallbackMarker). So the future can be destroyed in a background
			// thread while the OnFrameBarrier() callback is being run -- we need a mutex to prevent that.
			//
			// Never call this function while  "_lock" is locked. That can cause deadlocks because of the stall
			// on callbackMarker->_callbackActive
			std::shared_ptr<CallbackMarker> callbackMarker;
			{
				ScopedLock(_lock);
				DisableFrameBarrierCallbackAlreadyLocked();
				callbackMarker = std::move(_frameBarrierCallbackMarker);
			}
			if (callbackMarker) {
				// Lock the mutex inside of the callback marker to ensure that if the callback is currently active, 
				// we stall waiting for it to finish.
				// We can't lock this at the same time as _lock, in order to avoid deadlock scenarios
				ScopedLock(callbackMarker->_callbackActive);
				// We actually need to repeat the check again, because the callback we just completed may register
				// another callback as it completes.
				// It should be unlikely for this to chain more than once more, because that would require the new
				// callback to also be running at the same time we're processing on this thread
				EnsureFrameBarrierCallbackStopped();
			}
		}

		inline FutureShared::FutureShared(const std::string& initializer)
		: _initializer(initializer)
		{}

		inline FutureShared::~FutureShared() 
		{
			EnsureFrameBarrierCallbackStopped();
		}
	}
}
