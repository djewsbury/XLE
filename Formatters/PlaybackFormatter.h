#pragma once

#include "IDynamicFormatter.h"
#include "FormatterUtils.h"
#include "../Utility/ImpliedTyping.h"

namespace Formatters
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
			b._valueEnd = b._valueBegin + name.size() + sizeof(uint64_t);
			auto hash = Hash64(name);
			_data.insert(_data.end(), (const uint8_t*)&hash, (const uint8_t*)(&hash+1));
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
			b._valueEnd = b._valueBegin + name.size() + sizeof(uint64_t);
			b._bindingEngineId = id;
			b._useBindingEngineId = true;
			auto hash = Hash64(name);
			_data.insert(_data.end(), (const uint8_t*)&hash, (const uint8_t*)(&hash+1));
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

			assert((_iterator->_valueEnd - _iterator->_valueBegin) >= sizeof(uint64_t));
			name = MakeStringSection(
				_recording->_data.begin() + _iterator->_valueBegin + sizeof(uint64_t),
				_recording->_data.begin() + _iterator->_valueEnd).Cast<char>();
			++_iterator;
			return true;
		}

		bool TryKeyedItem(uint64_t& name) override
		{
			if (PeekNext() != Formatters::FormatterBlob::KeyedItem)
				return false;

			assert((_iterator->_valueEnd - _iterator->_valueBegin) >= sizeof(uint64_t));
			name = *(const uint64_t*)(AsPointer(_recording->_data.begin()) + _iterator->_valueBegin);
			++_iterator;
			return true;
		}

		void GetNextValueBlob(IteratorRange<const void*>& value, ImpliedTyping::TypeDesc& type)
		{
			assert(_iterator != _recording->_blobs.end() && _iterator->_type == Formatters::FormatterBlob::Value);
			assert(!_iterator->_useBindingEngineId);
			value = MakeIteratorRange(_recording->_data.begin() + _iterator->_valueBegin, _recording->_data.begin() + _iterator->_valueEnd);
			type = _iterator->_valueType;
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
			if (rawType._typeHint == ImpliedTyping::TypeHint::String && (rawType._type == ImpliedTyping::TypeCat::UInt8 || rawType._type == ImpliedTyping::TypeCat::Int8)) {
				if (!ImpliedTyping::ConvertFullMatch(MakeStringSection((const char*)rawValue.begin(), (const char*)rawValue.end()), destinationBuffer, type))
					return false;
			} else {
				if (!ImpliedTyping::Cast(destinationBuffer, type, rawValue, rawType))
					return false;
			}

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
			::Assets::DependencyValidation depVal)
		: _recording(std::move(recording))
		, _depVal(std::move(depVal))
		{
			_iterator = _recording->_blobs.begin();
		}

		std::shared_ptr<FormatterRecording> _recording;
		std::vector<FormatterRecording::Blob>::iterator _iterator;
		::Assets::DependencyValidation _depVal;
	};

	std::shared_ptr<Formatters::FormatterRecording> CopyToRecording(Formatters::TextInputFormatter<>& fmttr);

	std::shared_ptr<Formatters::IDynamicInputFormatter> PlaybackRecording(std::shared_ptr<Formatters::FormatterRecording> recording, ::Assets::DependencyValidation depVal);
}

