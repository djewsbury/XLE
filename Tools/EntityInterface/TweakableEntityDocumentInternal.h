// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../OSServices/Log.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/FastParseValue.h"
#include <set>
#include <stack>

namespace EntityInterface
{
	class ArbiterState
	{
	public:
		void ToggleEnable(uint64_t id)
		{
			auto i = _enabledTweakables.find(id);
			if (i == _enabledTweakables.end()) _enabledTweakables.insert(id);
			else _enabledTweakables.erase(i);
		}

		bool IsEnabled(uint64_t id) const { return _enabledTweakables.find(id) != _enabledTweakables.end(); }

		template<typename Type>
			void InitializeValue(uint64_t id, Type value)
		{
			if (!_workingValues.HasParameter(id))
				_workingValues.SetParameter(id, MakeOpaqueIteratorRange(value), ImpliedTyping::TypeOf<Type>());
		}

		template<typename Type>
			Type GetWorkingValue(uint64_t id)
		{
			return _workingValues.GetParameter<Type>(id).value();
		}

		template<typename Type>
			std::optional<Type> TryGetWorkingValue(uint64_t id)
		{
			return _workingValues.GetParameter<Type>(id);
		}

		template<typename Type>
			void SetWorkingValue(uint64_t id, Type newValue)
		{
			_workingValues.SetParameter(id, MakeOpaqueIteratorRange(newValue), ImpliedTyping::TypeOf<Type>());
		}

		std::string GetWorkingValueAsString(uint64_t id)
		{
			return _workingValues.GetParameterAsString(id).value();
		}

		template<typename Type>
			bool TryUpdateValueFromString(uint64_t id, StringSection<> editBoxResult)
		{
			if (editBoxResult.IsEmpty()) return false;
			Type newValue;
			const auto* parseEnd = FastParseValue(editBoxResult, newValue);
			if (parseEnd == editBoxResult.end()) {
				_workingValues.SetParameter(id, MakeOpaqueIteratorRange(newValue), ImpliedTyping::TypeOf<Type>());
				return true;
			} else {
				Log(Debug) << "Failed to parse (" << editBoxResult << ") to type (" << typeid(Type).name() << ")" << std::endl;
				return false;
			}
		}
		
	private:
		std::set<uint64_t> _enabledTweakables;
		ParameterBox _workingValues;
	};

    class GuidStackHelper
	{
	public:
		uint64_t MakeGuid(StringSection<> name) { return Hash64(name, _guidStack.top()); }
		uint64_t MakeGuid(StringSection<> name, StringSection<> concatenation) { return Hash64(name, Hash64(concatenation, _guidStack.top())); }

		void push(uint64_t guid) { return _guidStack.push(guid); }
		void pop() { return _guidStack.pop(); }
		uint64_t top() const { return _guidStack.top(); }
		bool empty() const { return _guidStack.empty(); }

		void Reset()
		{
			_guidStack = {};
			_guidStack.push(DefaultSeed64);
		}

		GuidStackHelper()
		{
			_guidStack.push(DefaultSeed64);
		}
	private:
		std::stack<uint64_t> _guidStack;
	};

    enum class HierarchicalEnabledState { NoImpact, DisableChildren, EnableChildren };
}
