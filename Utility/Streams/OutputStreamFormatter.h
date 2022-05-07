// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Detail/API.h"
#include "../UTFUtils.h"
#include "../StringUtils.h"
#include <vector>
#include <assert.h>

namespace Utility
{
	class OutputStream;

	#define STREAM_FORMATTER_CHECK_ELEMENTS

	class XL_UTILITY_API OutputStreamFormatter
	{
	public:
		using ElementId = unsigned;

		ElementId BeginKeyedElement(StringSection<utf8> name);
		ElementId BeginSequencedElement();
		void EndElement(ElementId);

		void WriteKeyedValue(
			StringSection<utf8> name,
			StringSection<utf8> value);
		void WriteSequencedValue(
			StringSection<utf8> value);
		
		void NewLine();

		OutputStreamFormatter(OutputStream& stream);
		~OutputStreamFormatter();

	protected:
		OutputStream*   _stream;
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
			template<typename U, void (U::*)(OutputStreamFormatter&) const> struct FunctionSignature {};
			template<typename U> static std::true_type Test1(FunctionSignature<U, &U::SerializeMethod>*);
			template<typename U> static std::false_type Test1(...);
			static const bool Result = decltype(Test1<T>(0))::value;
		};
	}

	template<typename Type, typename std::enable_if<Internal::HasSerializeMethod<Type>::Result>::type* =nullptr>
		inline void SerializationOperator(OutputStreamFormatter& formatter, const Type& input)
	{
		input.SerializeMethod(formatter);
	}
}
