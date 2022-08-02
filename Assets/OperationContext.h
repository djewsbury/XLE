// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/StringUtils.h"
#include "../Utility/Threading/Mutex.h"
#include <cstdint>
#include <future>
#include <chrono>

namespace Assets
{

	namespace Internal
	{
		class VariantFutureSet
		{
		public:
			using Id = uint32_t;
			using Duration = std::chrono::steady_clock::duration;
			using TimePoint = std::chrono::steady_clock::time_point;

			template<typename FutureObj>
				Id Add(std::shared_future<FutureObj>);
			void Remove(Id);

			std::future_status WaitFor(Id id, Duration);
			std::future_status WaitUntil(Id id, TimePoint);

			VariantFutureSet();
			~VariantFutureSet();
			VariantFutureSet(VariantFutureSet&&) = delete;
			VariantFutureSet& operator=(VariantFutureSet&&) = delete;
		private:
			struct Entry
			{
				Id _id = 0;
				size_t _begin = ~0ull, _end = ~0ull;
				using WaitForFn = std::future_status(void*, Duration);
				using WaitUntilFn = std::future_status(void*, TimePoint);
				using MoveFn = void(void*, void*);
				using DestructorFn = void(void*);
				WaitForFn* _waitForFn = nullptr;
				WaitUntilFn* _waitUntilFn = nullptr;
				MoveFn* _moveFn = nullptr;
				DestructorFn* _destructionFn = nullptr;
			};
			std::vector<Entry> _entries;
			std::vector<uint8_t> _storage;
			Id _nextId = 1;
		};
	}

	/// <summary>Track progress of long operations</summary>
	class OperationContext
	{
	public:
		using OperationId = uint32_t;
		struct OperationHelper;
		OperationHelper Begin(std::string description);
		struct OperationHelper
		{
			template<typename FutureObj>
				void EndWithFuture(std::shared_future<FutureObj>);
			operator bool() const { return _context != nullptr; }
			void SetMessage(std::string);

			OperationHelper();
			~OperationHelper();
			OperationHelper(OperationHelper&&);
			OperationHelper& operator=(OperationHelper&&);
		private:
			OperationHelper(OperationId, OperationContext&);
			OperationContext* _context = nullptr;
			OperationId _opId = ~0u;
			friend class OperationContext;
		};

		template<typename Service>
			std::shared_ptr<Service> GetService();

		std::vector<std::string> GetActiveOperations();

		void End(OperationId);
		template<typename FutureObj>
			void EndWithFuture(OperationId, std::shared_future<FutureObj>);
		void SetMessage(OperationId, std::string);

		OperationContext();
		~OperationContext();

		OperationContext(OperationContext&&) = delete;
		OperationContext& operator=(OperationContext&&) = delete;
		uint64_t GetGUID() const { return _guid; }
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
		Internal::VariantFutureSet _futures;
		Threading::Mutex _mutex;
		uint64_t _guid = 0;
		void EndWithFutureAlreadyLocked(OperationId opId, Internal::VariantFutureSet::Id futureId);
	};

	template<typename FutureObj>
		void OperationContext::OperationHelper::EndWithFuture(std::shared_future<FutureObj> future)
	{
		assert(_context);
		_context->EndWithFuture(_opId, std::move(future));
		// we now release our reference this to operation, because it's tracked alongside the future
		_context = nullptr;
		_opId = ~0u;
	}

	template<typename FutureObj>
		void OperationContext::EndWithFuture(OperationId opId, std::shared_future<FutureObj> future)
	{
		ScopedLock(_mutex);
		return EndWithFutureAlreadyLocked(opId, _futures.Add(future));
	}

	namespace Internal
	{
		template<typename FutureObj>
			auto VariantFutureSet::Add(std::shared_future<FutureObj> future) -> Id
		{
			using StoredType = std::shared_future<FutureObj>;
			auto offset = _storage.size();
			_storage.resize(_storage.size() + sizeof(StoredType));
			Entry e;
			e._id = _nextId++;
			e._begin = offset;
			e._end = _storage.size();
			e._waitForFn = [](void* future, Duration d) -> std::future_status {
				return ((StoredType*)future)->wait_for(d);
			};
			e._waitUntilFn = [](void* future, TimePoint tp) -> std::future_status {
				return ((StoredType*)future)->wait_until(tp);
			};
			e._moveFn = [](void* dst, void* src) {
				*((StoredType*)dst) = std::move(*((StoredType*)src));
			};
			e._destructionFn = [](void* future) {
				((StoredType*)future)->~StoredType();
			};
			_entries.push_back(e);
			#pragma push_macro("new")
			#undef new
				new (PtrAdd(_storage.data(), offset)) StoredType{std::move(future)};
			#pragma pop_macro("new")
			return e._id;
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

}

