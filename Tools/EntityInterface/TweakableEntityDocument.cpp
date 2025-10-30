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
#include "../../Formatters/PlaybackFormatter.h"
#include "../../Utility/Streams/StreamTypes.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/MemoryUtils.h"

namespace EntityInterface
{
	class PlaybackFormatterWithBindingEngine : public Formatters::PlaybackFormatter
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

		PlaybackFormatterWithBindingEngine(
			std::shared_ptr<Formatters::FormatterRecording> recording,
			std::shared_ptr<MinimalBindingEngine> bindingEngine,
			::Assets::DependencyValidation depVal)
		: PlaybackFormatter(std::move(recording), std::move(depVal))
		, _bindingEngine(std::move(bindingEngine))
		{}

		std::shared_ptr<MinimalBindingEngine> _bindingEngine;
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
			_recording = std::make_shared<Formatters::FormatterRecording>();
		}

		~FormatToMinimalBindingEngine() = default;

		std::shared_ptr<Formatters::FormatterRecording> _recording;

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
			if (_bindingEngine->GetModelDependencyValidation().GetValidationIndex() != _lastUpstreamModelValidationIndex) {
				_lastUpstreamModelValidationIndex = _bindingEngine->GetModelDependencyValidation().GetValidationIndex();
				_depVal.IncreaseValidationIndex();
			}
		}

		virtual std::future<std::shared_ptr<Formatters::IDynamicInputFormatter>> BeginFormatter(StringSection<> internalPoint) override
		{
			if (!internalPoint.IsEmpty())
				Throw(std::runtime_error("BeginFormatter with a internal starting point is not supported for EntityDocumentWithDataBinding"));

			FormatToMinimalBindingEngine fmttr{*_bindingEngine};
			_modelFunction(fmttr);

			std::promise<std::shared_ptr<Formatters::IDynamicInputFormatter>> promise;
			auto result = promise.get_future();
			promise.set_value(std::make_shared<PlaybackFormatterWithBindingEngine>(fmttr._recording, _bindingEngine, _depVal));
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


