// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OperationContext.h"
#include "../Core/Exceptions.h"
#include <chrono>

namespace Assets
{
	class OperationContext::Pimpl
	{
	public:
		struct RegisteredOp
		{
			Internal::VariantFutureSet::Id _future = ~0u;
			std::string _description, _msg;
			std::chrono::steady_clock::time_point _beginTime;
			std::optional<std::pair<unsigned, unsigned>> _progress;
		};
		std::vector<std::pair<OperationId, RegisteredOp>> _ops;
		OperationId _nextOperationId = 1u;
	};

	void OperationContext::EndWithFutureAlreadyLocked(OperationId opId, Internal::VariantFutureSet::Id futureId)
	{
		for (auto i=_pimpl->_ops.begin(); i!=_pimpl->_ops.end(); ++i)
			if (i->first == opId) {
				i->second._future = futureId;
				return;
			}
		assert(0);
	}

	auto OperationContext::Begin(std::string desc) -> OperationContextHelper
	{
		ScopedLock(_mutex);
		auto id = _pimpl->_nextOperationId++;
		_pimpl->_ops.emplace_back(id, Pimpl::RegisteredOp{~0u, desc, {}, std::chrono::steady_clock::now()});
		assert(shared_from_this().get() == this);
		return {id, shared_from_this()};
	}

	void OperationContext::End(OperationId id)
	{
		ScopedLock(_mutex);
		for (auto i=_pimpl->_ops.begin(); i!=_pimpl->_ops.end(); ++i)
			if (i->first == id) {
				_pimpl->_ops.erase(i);
				return;
			}
		assert(0);		// didn't find it
	}

	void OperationContext::SetMessage(OperationId id, std::string str)
	{
		ScopedLock(_mutex);
		for (auto i=_pimpl->_ops.begin(); i!=_pimpl->_ops.end(); ++i)
			if (i->first == id) {
				i->second._msg = std::move(str);
				return;
			}
		assert(0);		// didn't find it
	}

	void OperationContext::SetDescription(OperationId id, std::string str)
	{
		ScopedLock(_mutex);
		for (auto i=_pimpl->_ops.begin(); i!=_pimpl->_ops.end(); ++i)
			if (i->first == id) {
				i->second._description = std::move(str);
				return;
			}
		assert(0);		// didn't find it
	}

	void OperationContext::SetProgress(OperationId id, unsigned completed, unsigned total)
	{
		assert(completed <= total);
		ScopedLock(_mutex);
		for (auto i=_pimpl->_ops.begin(); i!=_pimpl->_ops.end(); ++i)
			if (i->first == id) {
				i->second._progress = { completed, total };
				return;
			}
		assert(0);		// didn't find it
	}

	auto OperationContext::GetActiveOperations() -> std::vector<OperationDesc>
	{
		ScopedLock(_mutex);
		std::vector<OperationDesc> result;
		result.reserve(_pimpl->_ops.size());
		for (auto i=_pimpl->_ops.begin(); i!=_pimpl->_ops.end(); ++i) {
			if (i->second._future != ~0u) {
				auto status = _futures.WaitFor(i->second._future, std::chrono::steady_clock::duration{0});
				if (status == std::future_status::ready)
					continue;		// already completed, implicitly removed
			}
			OperationDesc desc;
			desc._description = i->second._description;
			desc._msg = i->second._msg;
			desc._progress = i->second._progress;
			desc._beginTime = i->second._beginTime;
			result.emplace_back(std::move(desc));
		}
		return result;
	}

	bool OperationContext::IsIdle()
	{
		ScopedLock(_mutex);
		for (auto i=_pimpl->_ops.begin(); i!=_pimpl->_ops.end(); ++i) {
			if (i->second._future != ~0u) {
				auto status = _futures.WaitFor(i->second._future, std::chrono::steady_clock::duration{0});
				if (status == std::future_status::ready)
					continue;		// already completed, implicitly removed
			}
			return false;
		}
		return true;
	}

	static uint64_t s_nextOperationContextGuid = 0;
	OperationContext::OperationContext()
	: _guid(++s_nextOperationContextGuid)
	{
		_pimpl = std::make_unique<Pimpl>();
	}

	OperationContext::~OperationContext()
	{}

	std::shared_ptr<OperationContext> CreateOperationContext()
	{
		return std::make_shared<OperationContext>();
	}

	void OperationContextHelper::SetMessage(std::string str)
	{
		if (_context)
			_context->SetMessage(_opId, std::move(str));
	}

	void OperationContextHelper::SetDescription(std::string str)
	{
		if (_context)
			_context->SetDescription(_opId, std::move(str));
	}

	void OperationContextHelper::SetProgress(unsigned completed, unsigned total)
	{
		if (_context)
			_context->SetProgress(_opId, completed, total);
	}

	OperationContextHelper::OperationContextHelper() = default;
	OperationContextHelper::~OperationContextHelper()
	{
		if (_context && !_endFunctionInvoked) _context->End(_opId);
	}
	OperationContextHelper::OperationContextHelper(OperationContextHelper&& moveFrom)
	{
		_context = moveFrom._context;
		_opId = moveFrom._opId;
		_endFunctionInvoked = moveFrom._endFunctionInvoked;
		moveFrom._context = nullptr;
		moveFrom._opId = ~0u;
		moveFrom._endFunctionInvoked = false;
	}
	OperationContextHelper& OperationContextHelper::operator=(OperationContextHelper&& moveFrom)
	{
		if (_context && !_endFunctionInvoked) _context->End(_opId);
		_context = moveFrom._context;
		_opId = moveFrom._opId;
		_endFunctionInvoked = moveFrom._endFunctionInvoked;
		moveFrom._context = nullptr;
		moveFrom._opId = ~0u;
		moveFrom._endFunctionInvoked = false;
		return *this;
	}
	OperationContextHelper::OperationContextHelper(OperationContext::OperationId id, std::shared_ptr<OperationContext> context)
	: _context(std::move(context)), _opId(id), _endFunctionInvoked(false) {}


	namespace Internal
	{
		void VariantFutureSet::Remove(Id id)
		{
			auto i = std::find_if(_entries.begin(), _entries.end(), [id](const auto& q) { return q._id == id; });
			if (i == _entries.end()) return;

			auto begin = i->_begin;
			i->_begin = i->_end = ~0ull;
			i->_destructionFn(PtrAdd(_storage.data(), begin));		// not safe to modify this VariantFutureSet from within this destructor

			auto size = i->_end-i->_begin;
			for (auto i2=i+1; i2!=_entries.end(); ++i2) {
				assert((i2->_end-size) <= i2->_begin);		// if you hit this, src and destination overlap -- which will break the move fn
				i2->_moveFn(
					PtrAdd(_storage.data(), i2->_begin-size),
					PtrAdd(_storage.data(), i2->_begin));
				i2->_begin -= size; i2->_end -= size;
			}
			_entries.erase(i);
		}

		std::future_status VariantFutureSet::WaitFor(Id id, Duration duration)
		{
			auto i = std::find_if(_entries.begin(), _entries.end(), [id](const auto& q) { return q._id == id; });
			if (i == _entries.end()) Throw(std::runtime_error("Bad future id"));
			assert(i->_end <= _storage.size());
			return i->_waitForFn(PtrAdd(_storage.data(), i->_begin), duration);
		}

		std::future_status VariantFutureSet::WaitUntil(Id id, TimePoint timePoint)
		{
			auto i = std::find_if(_entries.begin(), _entries.end(), [id](const auto& q) { return q._id == id; });
			if (i == _entries.end()) Throw(std::runtime_error("Bad future id"));
			assert(i->_end <= _storage.size());
			return i->_waitUntilFn(PtrAdd(_storage.data(), i->_begin), timePoint);
		}

		VariantFutureSet::VariantFutureSet() = default;
		VariantFutureSet::~VariantFutureSet()
		{
			for (const auto& e:_entries)
				(*e._destructionFn)(PtrAdd(_storage.data(), e._begin));
		}
	}
}