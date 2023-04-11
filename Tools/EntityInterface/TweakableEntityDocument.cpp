// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TweakableEntityDocument.h"
#include "TweakableEntityDocumentInternal.h"
#include "FormatterAdapters.h"
#include "../../RenderOverlays/LayoutEngine.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/Marker.h"
#include "../../Formatters/TextOutputFormatter.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../Utility/Streams/StreamTypes.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/MemoryUtils.h"

namespace EntityInterface
{
#if 0
	class OutputStreamFormatterWithStubs : public IWidgetsLayoutFormatter
	{
	public:
		void WriteHalfDoubleInt(StringSection<> name, int64_t initialValue, int64_t minValue, int64_t maxValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable))
				if (auto str = _arbiterState->TryGetWorkingValueAsString(interactable))
					_fmttr.WriteKeyedValue(AutoFormatName(name), *str);
		}

		void WriteHalfDoubleFloat(StringSection<> name, float initialValue, float minValue, float maxValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable))
				if (auto str = _arbiterState->TryGetWorkingValueAsString(interactable))
					_fmttr.WriteKeyedValue(AutoFormatName(name), *str);
		}

		void WriteDecrementIncrementInt(StringSection<> name, int64_t initialValue, int64_t minValue, int64_t maxValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable))
				if (auto str = _arbiterState->TryGetWorkingValueAsString(interactable))
					_fmttr.WriteKeyedValue(AutoFormatName(name), *str);
		}

		void WriteDecrementIncrementFloat(StringSection<> name, float initialValue, float minValue, float maxValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable))
				if (auto str = _arbiterState->TryGetWorkingValueAsString(interactable))
					_fmttr.WriteKeyedValue(AutoFormatName(name), *str);
		}

		void WriteBoundedInt(StringSection<> name, int64_t initialValue, int64_t leftSideValue, int64_t rightSideValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable))
				if (auto str = _arbiterState->TryGetWorkingValueAsString(interactable))
					_fmttr.WriteKeyedValue(AutoFormatName(name), *str);
		}

		void WriteBoundedFloat(StringSection<> name, float initialValue, float leftSideValue, float rightSideValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable))
				if (auto str = _arbiterState->TryGetWorkingValueAsString(interactable))
					_fmttr.WriteKeyedValue(AutoFormatName(name), *str);
		}

		void WriteHorizontalCombo(StringSection<> name, int64_t initialValue, IteratorRange<const std::pair<int64_t, const char*>*> options) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable)) {
				if (auto v = _arbiterState->TryGetWorkingValue<int64_t>(interactable)) {
					auto i = std::find_if(options.begin(), options.end(), [v=*v](const auto& c) { return c.first == v; });
					if (i != options.end())
						_fmttr.WriteKeyedValue(AutoFormatName(name), i->second);
				}
			}
		}

		void WriteCheckbox(StringSection<> name, bool initialValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			if (EnabledByHierarchy() == HierarchicalEnabledState::EnableChildren || _arbiterState->IsEnabled(interactable)) {
				if (auto b = _arbiterState->TryGetWorkingValue<bool>(interactable))
					_fmttr.WriteKeyedValue(AutoFormatName(name), *b ? "true" : "false");
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

		OutputStreamFormatterWithStubs(OutputStream& str, MinimalBindingEngine& bindingEngine) : _fmttr(str), _bindingEngine(&bindingEngine) {}
	private:
		RenderOverlays::GuidStackHelper _guidStack;
		MinimalBindingEngine* _bindingEngine;
		Formatters::TextOutputFormatter _fmttr;
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
#endif

	class FormatterRecording
	{
	public:
		struct Blob
		{
			Formatters::FormatterBlob _type;
			size_t _valueBegin = 0, _valueEnd = 0;
			ImpliedTyping::TypeDesc _valueType;
		};
		std::vector<Blob> _blobs;
		std::vector<uint8_t> _data;

		void PushBeginElement()
		{
			_blobs.emplace_back(Blob{Formatters::FormatterBlob::BeginElement});
		}

		void PushEndElement()
		{
			_blobs.emplace_back(Blob{Formatters::FormatterBlob::EndElement});
		}

		void PushKeyedItem(StringSection<> name)
		{
			Blob b { Formatters::FormatterBlob::KeyedItem };
			b._valueBegin = _data.size();
			b._valueEnd = b._valueBegin + name.size();
			_data.insert(_data.end(), name.begin(), name.end());
			_blobs.emplace_back(std::move(b));
		}

		void PushStringValue(StringSection<> value)
		{
			Blob b { Formatters::FormatterBlob::Value };
			b._valueBegin = _data.size();
			b._valueEnd = b._valueBegin + value.size();
			_data.insert(_data.end(), value.begin(), value.end());
			b._valueType = ImpliedTyping::TypeOf<char>();
			b._valueType._arrayCount = (uint32_t)value.size();
			b._valueType._typeHint = ImpliedTyping::TypeHint::String;
			_blobs.emplace_back(std::move(b));
		}

		void PushRawValue(IteratorRange<const void*> value, const ImpliedTyping::TypeDesc& type)
		{
			Blob b { Formatters::FormatterBlob::Value };
			b._valueBegin = _data.size();
			b._valueEnd = b._valueBegin + value.size();
			_data.insert(_data.end(), (const uint8_t*)value.begin(), (const uint8_t*)value.end());
			b._valueType = type;
			_blobs.emplace_back(std::move(b));
		}
	};

	class PlaybackFormatter : public Formatters::IDynamicInputFormatter
	{
	public:
		Formatters::FormatterBlob PeekNext() override
		{
			if (_iterator == _recording->_blobs.end())
				return Formatters::FormatterBlob::None;
			return _iterator->_type;
		}

		bool TryBeginElement() override
		{
			if (PeekNext() != Formatters::FormatterBlob::BeginElement)
				return false;

			++_iterator;
			return true;
		}

		bool TryEndElement() override
		{
			if (PeekNext() != Formatters::FormatterBlob::EndElement)
				return false;

			++_iterator;
			return true;
		}

		bool TryKeyedItem(StringSection<>& name) override
		{
			if (PeekNext() != Formatters::FormatterBlob::KeyedItem)
				return false;

			name = MakeStringSection(_recording->_data.begin() + _iterator->_valueBegin, _recording->_data.begin() + _iterator->_valueEnd).Cast<char>();
			++_iterator;
			return true;
		}

		bool TryKeyedItem(uint64_t& name) override
		{
			StringSection<> str;
			if (!TryKeyedItem(str))
				return false;
			name = Hash64(str);
			return true;
		}

		bool TryStringValue(StringSection<>& value) override
		{
			if (PeekNext() != Formatters::FormatterBlob::Value)
				return false;

			if (_iterator->_valueType._typeHint != ImpliedTyping::TypeHint::String
				|| _iterator->_valueType._type != ImpliedTyping::TypeOf<char>()._type)
				return false;

			value = MakeStringSection(_recording->_data.begin() + _iterator->_valueBegin, _recording->_data.begin() + _iterator->_valueEnd).Cast<char>();
			++_iterator;
			return true;
		}

		bool TryRawValue(IteratorRange<const void*>& value, ImpliedTyping::TypeDesc& type) override
		{
			if (PeekNext() != Formatters::FormatterBlob::Value)
				return false;

			value = MakeIteratorRange(_recording->_data.begin() + _iterator->_valueBegin, _recording->_data.begin() + _iterator->_valueEnd);
			type = _iterator->_valueType;
			++_iterator;
			return true;
		}

		bool TryCastValue(IteratorRange<void*> destinationBuffer, const ImpliedTyping::TypeDesc& type) override
		{
			if (PeekNext() != Formatters::FormatterBlob::Value)
				return false;

			auto srcValue = MakeIteratorRange(_recording->_data.begin() + _iterator->_valueBegin, _recording->_data.begin() + _iterator->_valueEnd);
			ImpliedTyping::Cast(destinationBuffer, type, srcValue, _iterator->_valueType);
			++_iterator;
			return true;
		}

		void SkipValueOrElement() override
		{
			auto next = PeekNext();
			if (next == Formatters::FormatterBlob::Value) {
				++_iterator;
			} else {
				if (next != Formatters::FormatterBlob::BeginElement)
					Throw(std::runtime_error("Expected begin element while skipping forward"));
				++_iterator;

				Formatters::SkipElement(*this);

				if (PeekNext() != Formatters::FormatterBlob::EndElement)
					Throw(std::runtime_error("Malformed end element while skipping forward"));
				++_iterator;
			}
		}

        Formatters::StreamLocation GetLocation() const override { return {}; }
        ::Assets::DependencyValidation GetDependencyValidation() const override { return _depVal; }

		PlaybackFormatter(std::shared_ptr<FormatterRecording> recording, ::Assets::DependencyValidation depVal)
		: _recording(std::move(recording))
		, _depVal(std::move(depVal))
		{
			_iterator = _recording->_blobs.begin();
		}

		std::shared_ptr<FormatterRecording> _recording;
		std::vector<FormatterRecording::Blob>::iterator _iterator;
		::Assets::DependencyValidation _depVal;
	};

	class FormatToMinimalBindingEngine : public IDynamicOutputFormatter
	{
	public:
		ElementId BeginKeyedElement(StringSection<> label) override
		{
			_recording->PushKeyedItem(label);
			_recording->PushBeginElement();

			auto eleId = _nextEleId++;
			auto id = _stackFrames.empty() ? DefaultSeed64 : _stackFrames.back()._id;
			id = Hash64(label, id);
			_stackFrames.push_back(Frame{eleId, id});
			return eleId;
		}

		ElementId BeginSequencedElement() override
		{
			_recording->PushBeginElement();

			if (!_stackFrames.empty()) {
				auto eleId = _nextEleId++;
				auto sequenceElementIdx = _stackFrames.back()._sequencedElementCounter++;
				_stackFrames.push_back(Frame{eleId, HashCombine(sequenceElementIdx, _stackFrames.back()._id)});
				return eleId;
			} else {
				auto eleId = _nextEleId++;
				auto sequenceElementIdx = _rootSequencedElementCounter++;
				_stackFrames.push_back(Frame{eleId, sequenceElementIdx});		// eleId reused as hash id
				return eleId;
			}
		}

		void EndElement(ElementId ele) override
		{
			_recording->PushEndElement();

			assert(!_stackFrames.empty());
			assert(_stackFrames.back()._eleId == ele);
			_stackFrames.pop_back();
		}

		void WriteKeyedValue(StringSection<> label, StringSection<> value) override
		{
			auto id = _stackFrames.empty() ? DefaultSeed64 : _stackFrames.back()._id;
			id = Hash64(label, id);

			_recording->PushKeyedItem(label);
			if (auto mirrorValue = _bindingEngine->TryGetModelValue(id)) {
				_recording->PushRawValue(mirrorValue->_data, mirrorValue->_type);
			} else {
				_bindingEngine->SetModelValue(id, value);
				_recording->PushStringValue(value);
			}
		}

		void WriteSequencedValue(StringSection<> value) override
		{
			uint64_t id;
			if (!_stackFrames.empty()) {
				auto sequenceElementIdx = _stackFrames.back()._sequencedElementCounter++;
				id = HashCombine(sequenceElementIdx, _stackFrames.back()._id);
			} else {
				id = _rootSequencedElementCounter++;
			}

			if (auto mirrorValue = _bindingEngine->TryGetModelValue(id)) {
				_recording->PushRawValue(mirrorValue->_data, mirrorValue->_type);
			} else {
				_bindingEngine->SetModelValue(id, value);
				_recording->PushStringValue(value);
			}
		}

		void WriteKeyedValue(StringSection<> label, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type) override
		{
			auto id = _stackFrames.empty() ? DefaultSeed64 : _stackFrames.back()._id;
			id = Hash64(label, id);

			_recording->PushKeyedItem(label);
			if (auto mirrorValue = _bindingEngine->TryGetModelValue(id)) {
				_recording->PushRawValue(mirrorValue->_data, mirrorValue->_type);
			} else {
				_bindingEngine->SetModelValue(id, data, type);
				_recording->PushRawValue(data, type);
			}
		}

		void WriteSequencedValue(IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type) override
		{
			uint64_t id;
			if (!_stackFrames.empty()) {
				auto sequenceElementIdx = _stackFrames.back()._sequencedElementCounter++;
				id = HashCombine(sequenceElementIdx, _stackFrames.back()._id);
			} else {
				id = _rootSequencedElementCounter++;
			}

			if (auto mirrorValue = _bindingEngine->TryGetModelValue(id)) {
				_recording->PushRawValue(mirrorValue->_data, mirrorValue->_type);
			} else {
				_bindingEngine->SetModelValue(id, data, type);
				_recording->PushRawValue(data, type);
			}
		}

		FormatToMinimalBindingEngine(MinimalBindingEngine& bindingEngine)
		: _bindingEngine(&bindingEngine)
		{
			_recording = std::make_shared<FormatterRecording>();
		}

		~FormatToMinimalBindingEngine() = default;

		std::shared_ptr<FormatterRecording> _recording;

	private:
		MinimalBindingEngine* _bindingEngine = nullptr;
		struct Frame
		{
			ElementId _eleId;
			uint64_t _id;
			unsigned _sequencedElementCounter = 0;
		};
		std::vector<Frame> _stackFrames;
		ElementId _nextEleId = 1u;
		unsigned _rootSequencedElementCounter = 0;
	};

	class TweakableDocumentInterface : public ITweakableDocumentInterface
	{
	public:
		std::shared_ptr<MinimalBindingEngine> _bindingEngine = std::make_shared<MinimalBindingEngine>();
		Threading::Mutex _readMutex;
		std::unique_lock<Threading::Mutex> _lock;
		::Assets::DependencyValidation _depVal;

		void IncreaseValidationIndex() override { _depVal.IncreaseValidationIndex(); }
		std::shared_ptr<MinimalBindingEngine> GetBindingEngine() override { return _bindingEngine; }

		virtual std::future<std::shared_ptr<Formatters::IDynamicInputFormatter>> BeginFormatter(StringSection<> internalPoint) override
		{
			FormatToMinimalBindingEngine fmttr{*_bindingEngine};
			_modelFunction(fmttr);

			std::promise<std::shared_ptr<Formatters::IDynamicInputFormatter>> promise;
			auto result = promise.get_future();
			promise.set_value(std::make_shared<PlaybackFormatter>(fmttr._recording, _depVal));
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

		TweakableDocumentInterface(WriteToModelFormatter&& modelFn)
		: _modelFunction(std::move(modelFn))
		{
			_depVal = ::Assets::GetDepValSys().Make();
		}

		WriteToModelFormatter _modelFunction;
	};

	std::shared_ptr<ITweakableDocumentInterface> CreateTweakableDocumentInterface(WriteToModelFormatter&& modelFn)
	{
		return std::make_shared<TweakableDocumentInterface>(std::move(modelFn));
	}

	IDynamicOutputFormatter::~IDynamicOutputFormatter() = default;
	IWidgetsLayoutFormatter::~IWidgetsLayoutFormatter() = default;

}


