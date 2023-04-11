// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TextFormatter.h"
#include "../Utility/ImpliedTyping.h"
#include "../Utility/StreamUtils.h"		// for StreamIndent

namespace Utility { namespace ImpliedTyping { class TypeDesc; } }

namespace Formatters
{
	namespace Internal
	{
		template<typename Type> static auto HasTryCharacterData_Helper(int) -> decltype(std::declval<Type>().TryCharacterData(std::declval<StringSection<typename Type::value_type>&>()), std::true_type{});
		template<typename...> static auto HasTryCharacterData_Helper(...) -> std::false_type;
		template<typename Type> struct HasTryCharacterData : decltype(HasTryCharacterData_Helper<Type>(0)) {};

		template<typename Type> static auto HasSkipValueOrElement_Helper(int) -> decltype(std::declval<Type>().SkipValueOrElement(), std::true_type{});
		template<typename...> static auto HasSkipValueOrElement_Helper(...) -> std::false_type;
		template<typename Type> struct HasSkipValueOrElement : decltype(HasSkipValueOrElement_Helper<Type>(0)) {};

		template<typename Type> static auto HasTryStringValue_Helper(int) -> decltype(std::declval<Type>().TryStringValue(std::declval<StringSection<typename Type::value_type>>()), std::true_type{});
		template<typename...> static auto HasTryStringValue_Helper(...) -> std::false_type;
		template<typename Type> struct HasTryStringValue : decltype(HasTryStringValue_Helper<Type>(0)) {};

		template<typename Type> static auto HasTryRawValue_Helper(int) -> decltype(std::declval<Type>().TryRawValue(std::declval<IteratorRange<const void*>&>(), std::declval<Utility::ImpliedTyping::TypeDesc&>()), std::true_type{});
		template<typename...> static auto HasTryRawValue_Helper(...) -> std::false_type;
		template<typename Type> struct HasTryRawValue : decltype(HasTryRawValue_Helper<Type>(0)) {};

		template<typename Type> static auto HasTryCastValue_Helper(int) -> decltype(std::declval<Type>().TryCastValue(std::declval<IteratorRange<const void*>>(), std::declval<const Utility::ImpliedTyping::TypeDesc&>()), std::true_type{});
		template<typename...> static auto HasTryCastValue_Helper(...) -> std::false_type;
		template<typename Type> struct HasTryCastValue : decltype(HasTryCastValue_Helper<Type>(0)) {};

		template<typename Type> static auto HasTryKeyedItemHash_Helper(int) -> decltype(std::declval<Type>().TryKeyedItem(std::declval<uint64_t&>()), std::true_type{});
		template<typename...> static auto HasTryKeyedItemHash_Helper(...) -> std::false_type;
		template<typename Type> struct HasTryKeyedItemHash : decltype(HasTryKeyedItemHash_Helper<Type>(0)) {};

		template<typename Type> static auto HasReversedEndian_Helper(int) -> decltype(std::declval<Type>().ReversedEndian(), std::true_type{});
		template<typename...> static auto HasReversedEndian_Helper(...) -> std::false_type;
		template<typename Type> struct HasReversedEndian : decltype(HasReversedEndian_Helper<Type>(0)) {};

		template<typename Formatter>
			struct FormatterTraits
		{
			static constexpr auto HasCharacterData = Internal::HasTryCharacterData<Formatter>::value;
			static constexpr auto HasSkipValueOrElement = Internal::HasSkipValueOrElement<Formatter>::value;
			static constexpr auto HasTryStringValue = Internal::HasTryStringValue<Formatter>::value;
			static constexpr auto HasTryRawValue = Internal::HasTryRawValue<Formatter>::value;
			static constexpr auto HasTryCastValue = Internal::HasTryRawValue<Formatter>::value;
			static constexpr auto HasTryKeyedItemHash = Internal::HasTryKeyedItemHash<Formatter>::value;
			static constexpr auto HasReversedEndian = Internal::HasReversedEndian<Formatter>::value;
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
				if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue) {
					Utility::ImpliedTyping::TypeDesc type;
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
				if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasCharacterData) {
					if (!formatter.TryCharacterData(dummy0))
						Throw(FormatException(
							"Malformed value while skipping forward", formatter.GetLocation()));
				} else
					UNREACHABLE();
				break;

			default:
				Throw(FormatException(
					"Unexpected blob or end of stream hit while skipping forward", formatter.GetLocation()));
			}
		}
	}

	template<typename Formatter, typename std::enable_if<!Formatters::Internal::FormatterTraits<Formatter>::HasSkipValueOrElement>::type* =nullptr>
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

	template<typename Formatter, typename std::enable_if<Formatters::Internal::FormatterTraits<Formatter>::HasSkipValueOrElement>::type* =nullptr>
		void SkipValueOrElement(Formatter& formatter)
	{
		formatter.SkipValueOrElement();
	}

	template<typename Formatter>
		void RequireBeginElement(Formatter& formatter)
	{
		if (!formatter.TryBeginElement())
			Throw(FormatException("Expecting begin element", formatter.GetLocation()));
	}

	template<typename Formatter>
		void RequireEndElement(Formatter& formatter)
	{
		if (!formatter.TryEndElement())
			Throw(FormatException("Expecting end element", formatter.GetLocation()));
	}

	template<typename Formatter>
		typename Formatter::InteriorSection RequireKeyedItem(Formatter& formatter)
	{
		typename Formatter::InteriorSection name;
		if (!formatter.TryKeyedItem(name))
			Throw(FormatException("Expecting keyed item", formatter.GetLocation()));
		return name;
	}

	template<typename Formatter>
		IteratorRange<const void*> RequireRawValue(Formatter& formatter, Utility::ImpliedTyping::TypeDesc& typeDesc)
	{
		if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue) {
			IteratorRange<const void*> value;
			if (!formatter.TryRawValue(value, typeDesc))
				Throw(FormatException("Expecting value", formatter.GetLocation()));
			return value;
		} else {
			typename Formatter::InteriorSection stringValue;
			if (!formatter.TryStringValue(stringValue))
				Throw(FormatException("Expecting value", formatter.GetLocation()));
			typeDesc = {Utility::ImpliedTyping::TypeOf<typename Formatter::InteriorSection::value_type>()._type, (uint32_t)stringValue.size(), Utility::ImpliedTyping::TypeHint::String};
			return {stringValue.begin(), stringValue.end()};
		}
	}

	template<typename Formatter>
		typename Formatter::InteriorSection RequireStringValue(Formatter& formatter)
	{
		typename Formatter::InteriorSection value;
		if (!formatter.TryStringValue(value))
			Throw(FormatException("Expecting value", formatter.GetLocation()));
		return value;
	}

	template<typename Formatter>
		bool ReversedEndian(Formatter& formatter)
	{
		if constexpr (Formatters::Internal::FormatterTraits<Formatter>::HasReversedEndian) {
			return formatter.ReversedEndian();
		} else {
			return false;
		}
	}

	template<typename Type, typename Formatter>
		Type RequireCastValue(Formatter& formatter)
	{
		if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasTryCastValue) {
			Type result;
			if (!formatter.TryCastValue(MakeOpaqueIteratorRange(result), Utility::ImpliedTyping::TypeOf<Type>()))
				Throw(FormatException(StringMeld<256>() << "Expecting value of type " << typeid(Type).name(), formatter.GetLocation()));
			return result;
		} else if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue) {
			IteratorRange<const void*> value;
			Utility::ImpliedTyping::TypeDesc typeDesc;
			if (!formatter.TryRawValue(value, typeDesc))
				Throw(FormatException(StringMeld<256>() << "Expecting value of type " << typeid(Type).name(), formatter.GetLocation()));
			return Utility::ImpliedTyping::VariantNonRetained{typeDesc, value, ReversedEndian(formatter)}.RequireCastValue<Type>();
		} else {
			typename Formatter::InteriorSection value;
			Type result;
			if (	!formatter.TryStringValue(value)
				|| 	!Utility::ImpliedTyping::ConvertFullMatch(value, MakeOpaqueIteratorRange(result), Utility::ImpliedTyping::TypeOf<Type>()))
				Throw(FormatException(StringMeld<256>() << "Expecting value of type " << typeid(Type).name(), formatter.GetLocation()));
			return result;
		}
	}

	template<typename Formatter>
		bool TryKeyedItem(Formatter& fmttr, uint64_t& keyname)
	{
		if constexpr (Formatters::Internal::FormatterTraits<Formatter>::HasTryKeyedItemHash) {
			return fmttr.TryKeyedItem(keyname);
		} else {
			StringSection<> stringKeyName;
			auto result = fmttr.TryKeyedItem(stringKeyName);
			if (result) keyname = Hash64(stringKeyName);
			return result;
		}
	}

	template<typename Formatter>
		bool TryKeyedItem(Formatter& fmttr, StringSection<>& keyname)
	{
		return fmttr.TryKeyedItem(keyname);
	}

	template<typename Formatter>
		std::vector<typename Formatter::InteriorSection> RequireListOfStrings(Formatter& formatter)
	{
		std::vector<typename Formatter::InteriorSection> result;
		RequireBeginElement(formatter);
		typename Formatter::InteriorSection next;
		while (formatter.TryStringValue(next))
			result.push_back(next);
		RequireEndElement(formatter);
		return result;
	}

	template<typename Formatter>
		typename Formatter::InteriorSection RequireCharacterData(Formatter& formatter)
	{
		typename Formatter::InteriorSection value;
		if (!formatter.TryCharacterData(value))
			Throw(FormatException("Expecting character data", formatter.GetLocation()));
		return value;
	}

	template<typename EnumType, std::optional<EnumType> StringToEnum(StringSection<>), typename Formatter>
		EnumType RequireEnum(Formatter& formatter)
	{
		if (typename Formatter::InteriorSection value; formatter.TryStringValue(value)) {
			auto result = StringToEnum(value);
			if (!result.has_value())
				Throw(FormatException(StringMeld<256>() << "Could not interpret (" << value << ") as (" << typeid(EnumType).name() << ")", formatter.GetLocation()));
			return result.value();
		} else if constexpr (Formatters::Internal::FormatterTraits<Formatter>::HasTryCastValue) {
			std::underlying_type_t<EnumType> result = 0;
			if (!formatter.TryCastValue(MakeOpaqueIteratorRange(result), ImpliedTyping::TypeOf<decltype(result)>()))
				Throw(FormatException(StringMeld<256>() << "Expecting value of type " << typeid(EnumType).name(), formatter.GetLocation()));
			return (EnumType)result;
		} else if constexpr (Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue) {
			IteratorRange<const void*> value;
			Utility::ImpliedTyping::TypeDesc typeDesc;
			if (!formatter.TryRawValue(value, typeDesc))
				Throw(FormatException(StringMeld<256>() << "Expecting value of type " << typeid(EnumType).name(), formatter.GetLocation()));
			return Utility::ImpliedTyping::VariantNonRetained{typeDesc, value, ReversedEndian(formatter)}.RequireCastValue<std::underlying_type_t<EnumType>>();
		} else
			Throw(FormatException("Expecting value", formatter.GetLocation()));
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
				if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasCharacterData) {
					str << "<<" << RequireCharacterData(formatter) << ">>";
				} else
					UNREACHABLE();
				break;
			case FormatterBlob::None:
				return;
			}
		}
	}

	template<typename Formatter>
		void LogFormatter2(std::ostream& str, Formatter& formatter)
	{
		bool first = true;
		for (;;) {
			if (!first) str << ", ";
			first = false;
			switch (formatter.PeekNext()) {
			case FormatterBlob::KeyedItem:
				str << "KeyedItem[" << RequireKeyedItem(formatter) << "]";
				break;
			case FormatterBlob::Value:
				str << "Value[" << RequireStringValue(formatter) << "]";
				break;
			case FormatterBlob::BeginElement:
				RequireBeginElement(formatter);
				str << "BeginElement";
				break;
			case FormatterBlob::EndElement:
				RequireEndElement(formatter);
				str << "EndElement" << std::endl;
				first = true;
				break;
			case FormatterBlob::CharacterData:
				if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasCharacterData) {
					str << "CharacterData[" << RequireCharacterData(formatter) << "]";
				} else
					UNREACHABLE();
				break;
			case FormatterBlob::None:
				return;
			}
		}
	}

}
