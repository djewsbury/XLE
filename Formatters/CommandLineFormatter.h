// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextFormatter.h"
#include <memory>
#include <vector>

namespace Formatters
{
	template<typename CharType = char>
		class CommandLineFormatter
	{
	public:
		FormatterBlob PeekNext();

		bool TryKeyedItem(StringSection<CharType>& name);
		bool TryStringValue(StringSection<CharType>& value);

		using value_type = CharType;
		using InteriorSection = StringSection<CharType>;
		using Blob = FormatterBlob;

		CommandLineFormatter(StringSection<CharType>);
		CommandLineFormatter(int argc, CharType const* const* argv);
		CommandLineFormatter(std::vector<StringSection<CharType>>&& data);
		CommandLineFormatter();
		~CommandLineFormatter();

	private:
		std::vector<StringSection<CharType>> _data;
		void SkipWhitespace();
	};

	template<typename CharType>
		CommandLineFormatter<CharType> MakeCommandLineFormatterFromWin32String(StringSection<CharType>, std::shared_ptr<void>& workingSpace);

	template<typename CharType>
		CommandLineFormatter<CharType> MakeCommandLineFormatter(StringSection<CharType>);

	template<typename CharType>
		CommandLineFormatter<CharType> MakeCommandLineFormatter(int argc, CharType const* const* argv);
}