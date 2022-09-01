// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OperationContext.h"
#include "../Core/Exceptions.h"

namespace Assets
{
	class OperationContext::Pimpl
	{
	public:
		struct RegisteredOp
		{
			Internal::VariantFutureSet::Id _future = ~0u;
			std::string _description, _msg;
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

	auto OperationContext::Begin(std::string desc) -> OperationHelper
	{
		ScopedLock(_mutex);
		auto id = _pimpl->_nextOperationId++;
		_pimpl->_ops.emplace_back(id, Pimpl::RegisteredOp{~0u, desc});
		return {id, *this};
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

	std::vector<std::string> OperationContext::GetActiveOperations()
	{
		ScopedLock(_mutex);
		std::vector<std::string> result;
		result.reserve(_pimpl->_ops.size());
		for (auto i=_pimpl->_ops.begin(); i!=_pimpl->_ops.end(); ++i) {
			if (i->second._future != ~0u) {
				auto status = _futures.WaitFor(i->second._future, std::chrono::steady_clock::duration{0});
				if (status == std::future_status::ready)
					continue;		// already completed, implicitly removed
			}
			if (!i->second._msg.empty()) result.push_back(i->second._description + "; " + i->second._msg);
			else result.push_back(i->second._description);
		}
		return result;
	}

	static uint64_t s_nextOperationContextGuid = 0;
	OperationContext::OperationContext()
	: _guid(++s_nextOperationContextGuid)
	{
		_pimpl = std::make_unique<Pimpl>();
	}

	OperationContext::~OperationContext()
	{}

	void OperationContext::OperationHelper::SetMessage(std::string str)
	{
		assert(_context);
		_context->SetMessage(_opId, std::move(str));
	}

	OperationContext::OperationHelper::OperationHelper() = default;
	OperationContext::OperationHelper::~OperationHelper()
	{
		if (_context) _context->End(_opId);
	}
	OperationContext::OperationHelper::OperationHelper(OperationHelper&& moveFrom)
	{
		_context = moveFrom._context;
		_opId = moveFrom._opId;
		moveFrom._context = nullptr;
		moveFrom._opId = ~0u;
	}
	OperationContext::OperationHelper& OperationContext::OperationHelper::operator=(OperationHelper&& moveFrom)
	{
		if (_context) _context->End(_opId);
		_context = moveFrom._context;
		_opId = moveFrom._opId;
		moveFrom._context = nullptr;
		moveFrom._opId = ~0u;
		return *this;
	}
	OperationContext::OperationHelper::OperationHelper(OperationId id, OperationContext& context)
	: _context(&context), _opId(id) {}


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