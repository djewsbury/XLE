// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StreamFormatter.h"
#include "../ImpliedTyping.h"

namespace Utility
{
	namespace ImpliedTyping { class TypeDesc; }

	namespace Internal
	{
		template<typename Type> static auto HasTryCharacterData_Helper(int) -> decltype(std::declval<Type>().TryCharacterData(std::declval<StringSection<typename Type::value_type>>()), std::true_type{});
		template<typename...> static auto HasTryCharacterData_Helper(...) -> std::false_type;
		template<typename Type> struct HasTryCharacterData : decltype(HasTryCharacterData_Helper<Type>(0)) {};

		template<typename Type> static auto HasSkipValueOrElement_Helper(int) -> decltype(std::declval<Type>().SkipValueOrElement(), std::true_type{});
		template<typename...> static auto HasSkipValueOrElement_Helper(...) -> std::false_type;
		template<typename Type> struct HasSkipValueOrElement : decltype(HasSkipValueOrElement_Helper<Type>(0)) {};

		template<typename Type> static auto HasTryStringValue_Helper(int) -> decltype(std::declval<Type>().TryStringValue(std::declval<StringSection<typename Type::value_type>>()), std::true_type{});
		template<typename...> static auto HasTryStringValue_Helper(...) -> std::false_type;
		template<typename Type> struct HasTryStringValue : decltype(HasTryStringValue_Helper<Type>(0)) {};

		template<typename Type> static auto HasTryRawValue_Helper(int) -> decltype(std::declval<Type>().TryRawValue(std::declval<IteratorRange<const void*>&>(), std::declval<ImpliedTyping::TypeDesc&>()), std::true_type{});
		template<typename...> static auto HasTryRawValue_Helper(...) -> std::false_type;
		template<typename Type> struct HasTryRawValue : decltype(HasTryRawValue_Helper<Type>(0)) {};

		template<typename Type> static auto HasTryCastValue_Helper(int) -> decltype(std::declval<Type>().TryCastValue(std::declval<IteratorRange<const void*>>(), std::declval<const ImpliedTyping::TypeDesc&>()), std::true_type{});
		template<typename...> static auto HasTryCastValue_Helper(...) -> std::false_type;
		template<typename Type> struct HasTryCastValue : decltype(HasTryCastValue_Helper<Type>(0)) {};

		template<typename Formatter>
			struct FormatterTraits
		{
			static constexpr auto HasCharacterData = Internal::HasTryCharacterData<Formatter>::value;
			static constexpr auto HasSkipValueOrElement = Internal::HasSkipValueOrElement<Formatter>::value;
			static constexpr auto HasTryStringValue = Internal::HasTryStringValue<Formatter>::value;
			static constexpr auto HasTryRawValue = Internal::HasTryRawValue<Formatter>::value;
			static constexpr auto HasTryCastValue = Internal::HasTryRawValue<Formatter>::value;
		};
	}

	template<typename Formatter>
		void SkipElement(Formatter& formatter)
	{
		unsigned subtreeEle = 0;
		typename Formatter::InteriorSection dummy0;
		for (;;) {
			switch(formatter.PeekNext()) {
			case FormatterBlob::BeginElement:
				if (!formatter.TryBeginElement())
					Throw(FormatException(
						"Malformed begin element while skipping forward", formatter.GetLocation()));
				++subtreeEle;
				break;

			case FormatterBlob::EndElement:
				if (!subtreeEle) return;    // end now, while the EndElement is primed

				if (!formatter.TryEndElement())
					Throw(FormatException(
						"Malformed end element while skipping forward", formatter.GetLocation()));
				--subtreeEle;
				break;

			case FormatterBlob::KeyedItem:
				if (!formatter.TryKeyedItem(dummy0))
					Throw(FormatException(
						"Malformed keyed item while skipping forward", formatter.GetLocation()));
				break;

			case FormatterBlob::Value:
				if constexpr(Internal::FormatterTraits<Formatter>::HasTryRawValue) {
					ImpliedTyping::TypeDesc type;
					IteratorRange<const void*> data;
					if (!formatter.TryRawValue(data, type))
						Throw(FormatException(
							"Malformed value while skipping forward", formatter.GetLocation()));
				} else {
					if (!formatter.TryStringValue(dummy0))
						Throw(FormatException(
							"Malformed value while skipping forward", formatter.GetLocation()));
				}
				break;

			case FormatterBlob::CharacterData:
				if constexpr(Internal::FormatterTraits<Formatter>::HasCharacterData) {
					if (!formatter.TryCharacterData(dummy0))
						Throw(FormatException(
							"Malformed value while skipping forward", formatter.GetLocation()));
				} else
					assert(0);
				break;

			default:
				Throw(FormatException(
					"Unexpected blob or end of stream hit while skipping forward", formatter.GetLocation()));
			}
		}
	}

	template<typename Formatter, typename std::enable_if<!Internal::FormatterTraits<Formatter>::HasSkipValueOrElement>::type* =nullptr>
		void SkipValueOrElement(Formatter& formatter)
	{
		typename Formatter::InteriorSection dummy0;
		if (formatter.PeekNext() == FormatterBlob::Value) {
			if (!formatter.TryStringValue(dummy0))
				Throw(FormatException(
					"Malformed value while skipping forward", formatter.GetLocation()));
		} else {
			if (!formatter.TryBeginElement())
				Throw(FormatException(
					"Expected begin element while skipping forward", formatter.GetLocation()));
			SkipElement(formatter);
			if (!formatter.TryEndElement())
				Throw(FormatException(
					"Malformed end element while skipping forward", formatter.GetLocation()));
		}
	}

	template<typename Formatter, typename std::enable_if<Internal::FormatterTraits<Formatter>::HasSkipValueOrElement>::type* =nullptr>
		void SkipValueOrElement(Formatter& formatter)
	{
		formatter.SkipValueOrElement();
	}

	template<typename Formatter>
		void RequireBeginElement(Formatter& formatter)
	{
		if (!formatter.TryBeginElement())
			Throw(Utility::FormatException("Expecting begin element", formatter.GetLocation()));
	}

	template<typename Formatter>
		void RequireEndElement(Formatter& formatter)
	{
		if (!formatter.TryEndElement())
			Throw(Utility::FormatException("Expecting end element", formatter.GetLocation()));
	}

	template<typename Formatter>
		typename Formatter::InteriorSection RequireKeyedItem(Formatter& formatter)
	{
		typename Formatter::InteriorSection name;
		if (!formatter.TryKeyedItem(name))
			Throw(Utility::FormatException("Expecting keyed item", formatter.GetLocation()));
		return name;
	}

	template<typename Formatter>
		IteratorRange<const void*> RequireRawValue(Formatter& formatter, ImpliedTyping::TypeDesc& typeDesc)
	{
		IteratorRange<const void*> value;
		if (!formatter.TryRawValue(value, typeDesc))
			Throw(Utility::FormatException("Expecting value", formatter.GetLocation()));
		return value;
	}

	template<typename Formatter>
		typename Formatter::InteriorSection RequireStringValue(Formatter& formatter)
	{
		typename Formatter::InteriorSection value;
		if (!formatter.TryStringValue(value))
			Throw(Utility::FormatException("Expecting value", formatter.GetLocation()));
		return value;
	}

	template<typename Type, typename Formatter>
		Type RequireCastValue(Formatter& formatter)
	{
		if constexpr(Internal::FormatterTraits<Formatter>::HasTryCastValue) {
			Type result;
			if (!formatter.TryCastValue(MakeOpaqueIteratorRange(result), ImpliedTyping::TypeOf<Type>()))
				Throw(Utility::FormatException(StringMeld<256>() << "Expecting value of type " << typeid(Type).name(), formatter.GetLocation()));
			return result;
		} else if constexpr(Internal::FormatterTraits<Formatter>::HasTryRawValue) {
			IteratorRange<const void*> value;
			ImpliedTyping::TypeDesc typeDesc;
			if (!formatter.TryRawValue(value, typeDesc))
				Throw(Utility::FormatException(StringMeld<256>() << "Expecting value of type " << typeid(Type).name(), formatter.GetLocation()));
			Type result;
			ImpliedTyping::Cast(MakeOpaqueIteratorRange(result), ImpliedTyping::TypeOf<Type>(), value, typeDesc);
			return result;
		} else {
			typename Formatter::InteriorSection value;
			Type result;
			if (	!formatter.TryStringValue(value)
				|| 	!ImpliedTyping::ConvertFullMatch(value, MakeOpaqueIteratorRange(result), ImpliedTyping::TypeOf<Type>()))
				Throw(Utility::FormatException(StringMeld<256>() << "Expecting value of type " << typeid(Type).name(), formatter.GetLocation()));
			return result;
		}
	}

	template<typename Formatter>
		typename Formatter::InteriorSection RequireCharacterData(Formatter& formatter)
	{
		typename Formatter::InteriorSection value;
		if (!formatter.TryCharacterData(value))
			Throw(Utility::FormatException("Expecting character data", formatter.GetLocation()));
		return value;
	}

	template<typename Formatter>
		void LogFormatter(std::ostream& str, Formatter& formatter)
	{
		unsigned indent = 0;
		for (;;) {
			switch (formatter.PeekNext()) {
			case FormatterBlob::KeyedItem:
				str << StreamIndent(indent) << "[" << RequireKeyedItem(formatter) << "]: ";
				break;
			case FormatterBlob::Value:
				str << RequireStringValue(formatter) << std::endl;
				break;
			case FormatterBlob::BeginElement:
				RequireBeginElement(formatter);
				str << "~" << std::endl;
				indent += 4;
				break;
			case FormatterBlob::EndElement:
				RequireEndElement(formatter);
				indent -= 4;
				break;
			case FormatterBlob::CharacterData:
				if constexpr(Internal::FormatterTraits<Formatter>::HasCharacterData) {
					str << "<<" << RequireCharacterData(formatter) << ">>";
				} else
					assert(0);
				break;
			case FormatterBlob::None:
				return;
			}
		}
	}

}
