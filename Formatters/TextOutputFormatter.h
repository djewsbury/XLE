// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/StringUtils.h"
#include <vector>

namespace Utility { class OutputStream; }

namespace Formatters
{
	#define STREAM_FORMATTER_CHECK_ELEMENTS

	class TextOutputFormatter
	{
	public:
		using ElementId = unsigned;

		ElementId BeginKeyedElement(StringSection<> name);
		ElementId BeginSequencedElement();
		ElementId BeginElement();
		void EndElement(ElementId);

		void WriteKeyedValue(
			StringSection<> name,
			StringSection<> value);
		void WriteSequencedValue(
			StringSection<> value);
		void WriteValue(StringSection<> value);
		
		void NewLine();

		TextOutputFormatter(Utility::OutputStream& stream);
		~TextOutputFormatter();

	protected:
		Utility::OutputStream*   _stream;
		unsigned        _currentIndentLevel;
		unsigned		_indentLevelAtStartOfLine;
		bool            _hotLine;
		unsigned        _currentLineLength;
		bool            _pendingHeader;

		#if defined(STREAM_FORMATTER_CHECK_ELEMENTS)
			std::vector<ElementId> _elementStack;
			unsigned _nextElementId;
		#endif

		void DoNewLine();
	};

	namespace Internal
	{
		template<typename T> struct HasSerializeMethod
		{
			template<typename U, void (U::*)(TextOutputFormatter&) const> struct FunctionSignature {};
			template<typename U> static std::true_type Test1(FunctionSignature<U, &U::SerializeMethod>*);
			template<typename U> static std::false_type Test1(...);
			static const bool Result = decltype(Test1<T>(0))::value;
		};
	}

	template<typename Type, typename std::enable_if<Internal::HasSerializeMethod<Type>::Result>::type* =nullptr>
		inline void SerializationOperator(TextOutputFormatter& formatter, const Type& input)
	{
		input.SerializeMethod(formatter);
	}
}
