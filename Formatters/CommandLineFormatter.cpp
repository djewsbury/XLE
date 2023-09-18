// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CommandLineFormatter.h"
#include "../Utility/UTFUtils.h"

namespace Formatters
{
	
	template<typename CharType>
		FormatterBlob CommandLineFormatter<CharType>::PeekNext()
	{
		if (_data.empty())
			return FormatterBlob::None;

		assert(!_data.front().IsEmpty());
		auto next = _data.front()[0];
		if (next == '-' || next == '/')
			return FormatterBlob::KeyedItem;
		return FormatterBlob::Value;
	}

	template<typename CharType>
		bool CommandLineFormatter<CharType>::TryKeyedItem(StringSection<CharType>& name)
	{
		if (_data.empty())
			return false;

		assert(!_data.front().IsEmpty());
		auto next = _data.front()[0];
		if (next != '-' && next != '/')
			return false;

		++_data.front()._start;
		if (next == '-' && !_data.front().IsEmpty() && _data.front()[0] == '-')
			++_data.front()._start;		// double "--"
		
		auto start = _data.front()._start;
		while (!_data.front().IsEmpty() && (_data.front()[0] != ' ' && _data.front()[0] != '\t' && _data.front()[0] != '-' && _data.front()[0] != '/' && _data.front()[0] != '='))
			++_data.front()._start;

		name = MakeStringSection(start, _data.front()._start);

		SkipWhitespace();

		return true;
	}

	template<typename CharType>
		bool CommandLineFormatter<CharType>::TryStringValue(StringSection<CharType>& value)
	{
		if (_data.empty())
			return false;

		if (_data.front()[0] == '-')
			return false;

		assert(!_data.front().IsEmpty());
		if (_data.front()[0] == '=')
			++_data.front()._start;

		if (!_data.front().IsEmpty() && (_data.front()[0] == '"' || _data.front()[0] == '\'')) {

			auto term = _data.front()[0];
			++_data.front()._start;
			auto start = _data.front()._start;

			while (!_data.front().IsEmpty() && _data.front()[0] != term)
				++_data.front()._start;

			value = MakeStringSection(start, _data.front()._start);
			if (!_data.front().IsEmpty())
				++_data.front()._start;

		} else {

			auto start = _data.front()._start;
			while (!_data.front().IsEmpty() && (_data.front()[0] != ' ' && _data.front()[0] != '\t'))
				++_data.front()._start;

			value = MakeStringSection(start, _data.front()._start);

		}

		SkipWhitespace();

		return true;
	}

	template<typename CharType>
		void CommandLineFormatter<CharType>::SkipWhitespace()
	{
		if (_data.empty()) return;

		while (!_data.front().IsEmpty() && (_data.front()[0] == ' ' || _data.front()[0] == '\t'))
			++_data.front()._start;

		if (_data.front().IsEmpty())
			_data.erase(_data.begin());

		assert(_data.empty() || (_data.front()[0] != ' ' && _data.front()[0] != '\t'));
	}

	template<typename CharType>
		CommandLineFormatter<CharType>::CommandLineFormatter(StringSection<CharType> cmdLine)
	{
		if (!cmdLine.IsEmpty())
			_data.push_back(cmdLine);
		SkipWhitespace();
	}

	template<typename CharType>
		CommandLineFormatter<CharType>::CommandLineFormatter(int argc, CharType const* const* argv)
	{
		_data.reserve(argc);
		for (int c=1; c<argc; ++c) {
			StringSection<CharType> s = argv[c];
			while (!s.IsEmpty() && (s[0] == ' ' || s[0] == '\t'))
				++s._start;
			if (!s.IsEmpty())
				_data.push_back(s);
		}
		SkipWhitespace();
	}

	template<typename CharType>
		CommandLineFormatter<CharType>::CommandLineFormatter(std::vector<StringSection<CharType>>&& data)
	: _data(std::move(data)) {}

	template<typename CharType>
		CommandLineFormatter<CharType>::CommandLineFormatter() {}
	template<typename CharType>
		CommandLineFormatter<CharType>::~CommandLineFormatter() {}

	template<typename CharType>
		CommandLineFormatter<CharType> MakeCommandLineFormatterFromWin32String(StringSection<CharType> str, std::shared_ptr<void>& workingSpaceRes)
	{
		// Windows does a weird way of combining a argv / argc set into a single string.. but it makes things
		// super complex for quotation marks
		auto workingSpace = std::make_shared<std::vector<CharType>>();
		workingSpace->resize(str.size());
		CharType* workingSpaceIterator = AsPointer(workingSpace->begin());

		auto initialStr = str;
		std::vector<StringSection<CharType>> data;
		for (;;) {
			while (!str.IsEmpty() && (*str.begin() == ' ' || *str.begin() == '\t')) ++str._start;
			if (str.IsEmpty()) break;

			if (*str.begin() != '"') break;		// oddly formatted, let's just bail
			++str._start;

			auto start = str._start;
			bool requiresProcessing = false;
			while (!str.IsEmpty() && *str._start != '"') {
				// convert \" -> " (not sure what happens if \" is actually in the original string)
				if (str.size() >= 2 && *str._start == '\\' && *(str._start+1) == '"') {
					requiresProcessing = true; str._start += 2; 
				} else ++str._start;
			}
			if (str._start != start) {
				if (requiresProcessing) {
					auto start2 = workingSpaceIterator;
					for (auto i=start; i!=str._start;) {
						if (*i == 92 && (i+1)!=str._start && *(i+1) == '"') {
							*workingSpaceIterator++ = '"';
							i += 2;
						} else {
							*workingSpaceIterator++ = *i++;
						}
					}
					data.emplace_back(start2, workingSpaceIterator);
				} else
					data.emplace_back(start, str._start);
			}
			if (!str.IsEmpty()) ++str._start;
		}

		workingSpaceRes = workingSpace;
		return CommandLineFormatter<CharType>(std::move(data));
	}

	template<typename CharType>
		CommandLineFormatter<CharType> MakeCommandLineFormatter(StringSection<CharType> str)
	{
		return CommandLineFormatter<CharType> { str };
	}

	template<typename CharType>
		CommandLineFormatter<CharType> MakeCommandLineFormatter(int argc, CharType const* const* argv)
	{
		return CommandLineFormatter<CharType> { argc, argv };
	}

	template CommandLineFormatter<char>;
	template CommandLineFormatter<utf16>;
	template CommandLineFormatter<wchar_t>;

	template CommandLineFormatter<char> MakeCommandLineFormatterFromWin32String(StringSection<char>, std::shared_ptr<void>&);
	template CommandLineFormatter<char> MakeCommandLineFormatter(StringSection<char>);
	template CommandLineFormatter<char> MakeCommandLineFormatter(int argc, char const* const* argv);
	template CommandLineFormatter<utf16> MakeCommandLineFormatterFromWin32String(StringSection<utf16>, std::shared_ptr<void>&);
	template CommandLineFormatter<utf16> MakeCommandLineFormatter(StringSection<utf16>);
	template CommandLineFormatter<utf16> MakeCommandLineFormatter(int argc, utf16 const* const* argv);
	template CommandLineFormatter<wchar_t> MakeCommandLineFormatterFromWin32String(StringSection<wchar_t>, std::shared_ptr<void>&);
	template CommandLineFormatter<wchar_t> MakeCommandLineFormatter(StringSection<wchar_t>);
	template CommandLineFormatter<wchar_t> MakeCommandLineFormatter(int argc, wchar_t const* const* argv);

}
