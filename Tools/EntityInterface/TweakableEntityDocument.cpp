// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TweakableEntityDocument.h"
#include "TweakableEntityDocumentInternal.h"
#include "FormatterAdapters.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/Marker.h"
#include "../../Utility/Streams/OutputStreamFormatter.h"
#include "../../Utility/Streams/StreamTypes.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/MemoryUtils.h"

namespace EntityInterface
{
	class OutputStreamFormatterWithStubs : public IWidgetsLayoutFormatter
	{
	public:
		void WriteHalfDoubleInt(StringSection<> name, int64_t initialValue, int64_t minValue, int64_t maxValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable))
				_fmttr.WriteKeyedValue(AutoFormatName(name), _arbiterState->GetWorkingValueAsString(interactable));
		}

		void WriteHalfDoubleFloat(StringSection<> name, float initialValue, float minValue, float maxValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable))
				_fmttr.WriteKeyedValue(AutoFormatName(name), _arbiterState->GetWorkingValueAsString(interactable));
		}

		void WriteDecrementIncrementInt(StringSection<> name, int64_t initialValue, int64_t minValue, int64_t maxValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable))
				_fmttr.WriteKeyedValue(AutoFormatName(name), _arbiterState->GetWorkingValueAsString(interactable));
		}

		void WriteDecrementIncrementFloat(StringSection<> name, float initialValue, float minValue, float maxValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable))
				_fmttr.WriteKeyedValue(AutoFormatName(name), _arbiterState->GetWorkingValueAsString(interactable));
		}

		void WriteBoundedInt(StringSection<> name, int64_t initialValue, int64_t leftSideValue, int64_t rightSideValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable))
				_fmttr.WriteKeyedValue(AutoFormatName(name), _arbiterState->GetWorkingValueAsString(interactable));
		}

		void WriteBoundedFloat(StringSection<> name, float initialValue, float leftSideValue, float rightSideValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable))
				_fmttr.WriteKeyedValue(AutoFormatName(name), _arbiterState->GetWorkingValueAsString(interactable));
		}

		void WriteHorizontalCombo(StringSection<> name, int64_t initialValue, IteratorRange<const std::pair<int64_t, const char*>*> options) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable)) {
				auto v = _arbiterState->GetWorkingValue<int64_t>(interactable);
				auto i = std::find_if(options.begin(), options.end(), [v](const auto& c) { return c.first == v; });
				if (i != options.end())
					_fmttr.WriteKeyedValue(AutoFormatName(name), i->second);
			}
		}

		void WriteCheckbox(StringSection<> name, bool initialValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable)) {
				_fmttr.WriteKeyedValue(AutoFormatName(name), _arbiterState->GetWorkingValue<bool>(interactable) ? "true" : "false");
			}
		}

		virtual bool GetCheckbox(StringSection<> name, bool initialValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			auto override = _arbiterState->TryGetWorkingValue<bool>(interactable);
			return override ? override.value() : initialValue;
		}

		bool BeginCollapsingContainer(StringSection<> name) override
		{
			_guidStack.push(MakeGuid(name, "##collapsingcontainer"));
			_hierarchicalEnabledStates.push_back(0);
			return true;
		}

		void BeginContainer() override
		{
			auto containerGuid = MakeGuid("##container");
			_guidStack.push(containerGuid);
			_hierarchicalEnabledStates.push_back(containerGuid);
		}

		void EndContainer() override
		{
			_guidStack.pop();
			_hierarchicalEnabledStates.pop_back();
		}

		HierarchicalEnabledState EnabledByHierarchy()
		{
			for (auto i=_hierarchicalEnabledStates.rbegin(); i!=_hierarchicalEnabledStates.rend(); ++i) {
				if (*i != 0) {
					auto state = _arbiterState->IsEnabled(*i);
					return state ? HierarchicalEnabledState::EnableChildren : HierarchicalEnabledState::DisableChildren;
				}
			}
			return HierarchicalEnabledState::NoImpact;
		}

		ElementId BeginKeyedElement(StringSection<> name) override { return _fmttr.BeginKeyedElement(name); }
		ElementId BeginSequencedElement() override { return _fmttr.BeginSequencedElement(); }
		void EndElement(ElementId id) override { _fmttr.EndElement(id); }

		void WriteKeyedValue(StringSection<> name, StringSection<> value) override { _fmttr.WriteKeyedValue(name, value); }
		void WriteSequencedValue(StringSection<> value) override { _fmttr.WriteSequencedValue(value); }

		OutputStreamFormatterWithStubs(OutputStream& str, ArbiterState& arbiterState) : _fmttr(str), _arbiterState(&arbiterState) {}
	private:
		GuidStackHelper _guidStack;
		ArbiterState* _arbiterState;
		OutputStreamFormatter _fmttr;
		std::vector<uint64_t> _hierarchicalEnabledStates;

		uint64_t MakeGuid(StringSection<> name) { return Hash64(name, _guidStack.top()); }
		uint64_t MakeGuid(StringSection<> name, StringSection<> concatenation) { return Hash64(name, Hash64(concatenation, _guidStack.top())); }

		static std::string AutoFormatName(StringSection<> input)
		{
			// remove spaces, and ensure that the first character and each character after a space is a capital
			if (input.IsEmpty()) return {};
			std::string result;
			result.reserve(input.size());
			result.push_back(std::toupper(*input.begin()));
			for (auto i=input.begin()+1; i!=input.end(); ++i) {
				if (*i == ' ') {
					while (*i == ' ' && i!=input.end()) ++i;
					if (i==input.end()) break;
					result.push_back(std::toupper(*i));
				} else
					result.push_back(*i);
			}
			return result;
		}
	};

	class TweakableDocumentInterface : public ITweakableDocumentInterface
	{
	public:
		std::shared_ptr<ArbiterState> _arbiterState = std::make_shared<ArbiterState>();
		Threading::Mutex _readMutex;
		std::unique_lock<Threading::Mutex> _lock;
		::Assets::DependencyValidation _depVal;

		void ExecuteOnFormatter(IWidgetsLayoutFormatter& fmttr) override
		{
			_writeFunction(fmttr);
		}

		void IncreaseValidationIndex() override { _depVal.IncreaseValidationIndex(); }
		std::shared_ptr<ArbiterState> GetArbiterState() override { return _arbiterState; }

		virtual std::future<std::shared_ptr<Formatters::IDynamicFormatter>> BeginFormatter(StringSection<> internalPoint) override
		{
			MemoryOutputStream<> outputStream;
			{
				OutputStreamFormatterWithStubs fmttr{outputStream, *_arbiterState};
				ExecuteOnFormatter(fmttr);
			}

			std::promise<std::shared_ptr<Formatters::IDynamicFormatter>> promise;
			auto result = promise.get_future();
			promise.set_value(CreateDynamicFormatter(std::move(outputStream), ::Assets::DependencyValidation{_depVal}));
			return result;
		}

		::Assets::DirectorySearchRules _searchRules;
		virtual const ::Assets::DependencyValidation& GetDependencyValidation() const override { return _depVal; }
		virtual const ::Assets::DirectorySearchRules& GetDirectorySearchRules() const override { return _searchRules; }

		virtual void Lock() override { _lock = std::unique_lock<Threading::Mutex>{_readMutex}; }
		virtual bool TryLock() override
		{
			_lock = std::unique_lock<Threading::Mutex>{_readMutex, std::defer_lock};
			return _lock.try_lock();
		}
		virtual void Unlock() override { _lock = {}; }

		TweakableDocumentInterface(WriteToLayoutFormatter&& fn)
		: _writeFunction(std::move(fn))
		{
			_depVal = ::Assets::GetDepValSys().Make();
		}

		WriteToLayoutFormatter _writeFunction;
	};

	std::shared_ptr<ITweakableDocumentInterface> CreateTweakableDocumentInterface(WriteToLayoutFormatter&& fn)
	{
		return std::make_shared<TweakableDocumentInterface>(std::move(fn));
	}

}


