// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TweakableEntityDocument.h"
#include "MinimalBindingEngine.h"
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
	class FormatterRecording
	{
	public:
		struct Blob
		{
			Formatters::FormatterBlob _type;
			size_t _valueBegin = 0, _valueEnd = 0;
			ImpliedTyping::TypeDesc _valueType;
			uint64_t _bindingEngineId = 0ull;
			bool _useBindingEngineId = false;
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

		void PushBindingEngineKeyedItem(uint64_t id, StringSection<> name)
		{
			Blob b { Formatters::FormatterBlob::KeyedItem };
			b._valueBegin = _data.size();
			b._valueEnd = b._valueBegin + name.size();
			b._bindingEngineId = id;
			b._useBindingEngineId = true;
			_data.insert(_data.end(), name.begin(), name.end());
			_blobs.emplace_back(std::move(b));
		}

		void PushBindingEngineValue(uint64_t id)
		{
			Blob b { Formatters::FormatterBlob::Value };
			b._bindingEngineId = id;
			b._useBindingEngineId = true;
			_blobs.emplace_back(std::move(b));
		}
	};

	class PlaybackFormatter : public Formatters::IDynamicInputFormatter
	{
	public:
		Formatters::FormatterBlob PeekNext() override
		{
			while (_iterator != _recording->_blobs.end() && _iterator->_useBindingEngineId && !_bindingEngine->IsEnabled(_iterator->_bindingEngineId))
				++_iterator;
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

		void GetNextValueBlob(IteratorRange<const void*>& value, ImpliedTyping::TypeDesc& type)
		{
			assert(_iterator != _recording->_blobs.end() && _iterator->_type == Formatters::FormatterBlob::Value);
			if (!_iterator->_useBindingEngineId) {
				value = MakeIteratorRange(_recording->_data.begin() + _iterator->_valueBegin, _recording->_data.begin() + _iterator->_valueEnd);
				type = _iterator->_valueType;
			} else {
				assert(_bindingEngine->IsEnabled(_iterator->_bindingEngineId));
				auto v = _bindingEngine->TryGetModelValue(_iterator->_bindingEngineId);
				assert(v.has_value());
				value = v->_data;
				type = v->_type;
			}
		}

		bool TryStringValue(StringSection<>& value) override
		{
			if (PeekNext() != Formatters::FormatterBlob::Value)
				return false;

			IteratorRange<const void*> rawValue; ImpliedTyping::TypeDesc rawType;
			GetNextValueBlob(rawValue, rawType);

			if (rawType._typeHint != ImpliedTyping::TypeHint::String
				|| rawType._type != ImpliedTyping::TypeOf<char>()._type)
				return false;

			value = MakeStringSection((const char*)rawValue.begin(), (const char*)rawValue.end());
			++_iterator;
			return true;
		}

		bool TryRawValue(IteratorRange<const void*>& value, ImpliedTyping::TypeDesc& type) override
		{
			if (PeekNext() != Formatters::FormatterBlob::Value)
				return false;

			GetNextValueBlob(value, type);
			++_iterator;
			return true;
		}

		bool TryCastValue(IteratorRange<void*> destinationBuffer, const ImpliedTyping::TypeDesc& type) override
		{
			if (PeekNext() != Formatters::FormatterBlob::Value)
				return false;

			IteratorRange<const void*> rawValue; ImpliedTyping::TypeDesc rawType;
			GetNextValueBlob(rawValue, rawType);
			if (!ImpliedTyping::Cast(destinationBuffer, type, rawValue, rawType))
				return false;

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

		PlaybackFormatter(
			std::shared_ptr<FormatterRecording> recording,
			std::shared_ptr<MinimalBindingEngine> bindingEngine,
			::Assets::DependencyValidation depVal)
		: _recording(std::move(recording))
		, _bindingEngine(std::move(bindingEngine))
		, _depVal(std::move(depVal))
		{
			_iterator = _recording->_blobs.begin();
		}

		std::shared_ptr<FormatterRecording> _recording;
		std::vector<FormatterRecording::Blob>::iterator _iterator;
		std::shared_ptr<MinimalBindingEngine> _bindingEngine;
		::Assets::DependencyValidation _depVal;
	};

	class FormatToMinimalBindingEngine : public IOutputFormatterWithDataBinding
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
			_recording->PushKeyedItem(label);
			_recording->PushStringValue(value);
		}

		void WriteSequencedValue(StringSection<> value) override
		{
			_recording->PushStringValue(value);
		}

		void WriteKeyedValue(StringSection<> label, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type) override
		{
			_recording->PushKeyedItem(label);
			_recording->PushRawValue(data, type);
		}

		void WriteSequencedValue(IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type) override
		{
			_recording->PushRawValue(data, type);
		}

		void WriteKeyedModelValue(StringSection<> label) override
		{
			auto id = _stackFrames.empty() ? DefaultSeed64 : _stackFrames.back()._id;
			id = Hash64(label, id);

			_recording->PushBindingEngineKeyedItem(id, label);
			_recording->PushBindingEngineValue(id);
		}

		void WriteSequencedModelValue() override
		{
			uint64_t id;
			if (!_stackFrames.empty()) {
				auto sequenceElementIdx = _stackFrames.back()._sequencedElementCounter++;
				id = HashCombine(sequenceElementIdx, _stackFrames.back()._id);
			} else {
				id = _rootSequencedElementCounter++;
			}

			_recording->PushBindingEngineValue(id);
		}

		MinimalBindingEngine& GetBindingEngine() override { return *_bindingEngine; }

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

	class EntityDocumentWithDataBinding : public IEntityDocumentWithDataBinding
	{
	public:
		std::shared_ptr<MinimalBindingEngine> _bindingEngine;
		Threading::Mutex _readMutex;
		std::unique_lock<Threading::Mutex> _lock;
		::Assets::DependencyValidation _depVal;
		unsigned _lastUpstreamModelValidationIndex = 0;

		void TestUpstreamValidationIndex() override 
		{ 
			if (_bindingEngine->GetModelValidationIndex() != _lastUpstreamModelValidationIndex) {
				_lastUpstreamModelValidationIndex = _bindingEngine->GetModelValidationIndex();
				_depVal.IncreaseValidationIndex();
			}
		}

		virtual std::future<std::shared_ptr<Formatters::IDynamicInputFormatter>> BeginFormatter(StringSection<> internalPoint) override
		{
			FormatToMinimalBindingEngine fmttr{*_bindingEngine};
			_modelFunction(fmttr);

			std::promise<std::shared_ptr<Formatters::IDynamicInputFormatter>> promise;
			auto result = promise.get_future();
			promise.set_value(std::make_shared<PlaybackFormatter>(fmttr._recording, _bindingEngine, _depVal));
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

		EntityDocumentWithDataBinding(std::shared_ptr<MinimalBindingEngine> bindingEngine, WriteToDataBindingFormatter&& modelFn)
		: _bindingEngine(std::move(bindingEngine))
		, _modelFunction(std::move(modelFn))
		{
			_depVal = ::Assets::GetDepValSys().Make();
		}

		WriteToDataBindingFormatter _modelFunction;
	};

	std::shared_ptr<IEntityDocumentWithDataBinding> CreateEntityDocumentWithDataBinding(
		std::shared_ptr<MinimalBindingEngine> bindingEngine,
		WriteToDataBindingFormatter&& modelFn)
	{
		return std::make_shared<EntityDocumentWithDataBinding>(std::move(bindingEngine), std::move(modelFn));
	}

	IDynamicOutputFormatter::~IDynamicOutputFormatter() = default;

}


