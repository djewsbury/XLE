// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TextFormatter.h"
#include "../Utility/ImpliedTyping.h"
#include "../Utility/StreamUtils.h"		// for StreamIndent
#include "../Utility/StringFormat.h"

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

		template<typename Type> static auto HasTryStringValue_Helper(int) -> decltype(std::declval<Type>().TryStringValue(std::declval<StringSection<typename Type::value_type>&>()), std::true_type{});
		template<typename...> static auto HasTryStringValue_Helper(...) -> std::false_type;
		template<typename Type> struct HasTryStringValue : decltype(HasTryStringValue_Helper<Type>(0)) {};

		template<typename Type> static auto HasTryRawValue_Helper(int) -> decltype(std::declval<Type>().TryRawValue(std::declval<IteratorRange<const void*>&>(), std::declval<Utility::ImpliedTyping::TypeDesc&>()), std::true_type{});
		template<typename...> static auto HasTryRawValue_Helper(...) -> std::false_type;
		template<typename Type> struct HasTryRawValue : decltype(HasTryRawValue_Helper<Type>(0)) {};

		template<typename Type> static auto HasTryCastValue_Helper(int) -> decltype(std::declval<Type>().TryCastValue(std::declval<IteratorRange<void*>>(), std::declval<const Utility::ImpliedTyping::TypeDesc&>()), std::true_type{});
		template<typename...> static auto HasTryCastValue_Helper(...) -> std::false_type;
		template<typename Type> struct HasTryCastValue : decltype(HasTryCastValue_Helper<Type>(0)) {};

		template<typename Type> static auto HasTryKeyedItemHash_Helper(int) -> decltype(std::declval<Type>().TryKeyedItem(std::declval<uint64_t&>()), std::true_type{});
		template<typename...> static auto HasTryKeyedItemHash_Helper(...) -> std::false_type;
		template<typename Type> struct HasTryKeyedItemHash : decltype(HasTryKeyedItemHash_Helper<Type>(0)) {};

		template<typename Type> static auto HasReversedEndian_Helper(int) -> decltype(std::declval<Type>().ReversedEndian(), std::true_type{});
		template<typename...> static auto HasReversedEndian_Helper(...) -> std::false_type;
		template<typename Type> struct HasReversedEndian : decltype(HasReversedEndian_Helper<Type>(0)) {};

		template<typename Type> static auto HasGetLocation_Helper(int) -> decltype(std::declval<Type>().GetLocation(), std::true_type{});
		template<typename...> static auto HasGetLocation_Helper(...) -> std::false_type;
		template<typename Type> struct HasGetLocation : decltype(HasGetLocation_Helper<Type>(0)) {};

		template<typename Type> static auto HasBeginBlock_Helper(int) -> decltype(std::declval<Type>().TryBeginBlock(std::declval<typename Type::EvaluatedTypeId&>()), std::true_type{});
		template<typename...> static auto HasBeginBlock_Helper(...) -> std::false_type;
		template<typename Type> struct HasBeginBlock : decltype(HasBeginBlock_Helper<Type>(0)) {};

		template<typename Type> static auto HasBeginElement_Helper(int) -> decltype(std::declval<Type>().TryBeginElement(), std::true_type{});
		template<typename...> static auto HasBeginElement_Helper(...) -> std::false_type;
		template<typename Type> struct HasBeginElement : decltype(HasBeginElement_Helper<Type>(0)) {};

		template<typename Type> static auto HasBeginArray_Helper(int) -> decltype(std::declval<Type>().TryBeginArray(std::declval<unsigned&>(), std::declval<typename Type::EvaluatedTypeId&>()), std::true_type{});
		template<typename...> static auto HasBeginArray_Helper(...) -> std::false_type;
		template<typename Type> struct HasBeginArray : decltype(HasBeginArray_Helper<Type>(0)) {};

		template<typename Type> static auto HasBeginDictionary_Helper(int) -> decltype(std::declval<Type>().TryBeginDictionary(std::declval<typename Type::EvaluatedTypeId&>(), std::declval<typename Type::EvaluatedTypeId&>()), std::true_type{});
		template<typename...> static auto HasBeginDictionary_Helper(...) -> std::false_type;
		template<typename Type> struct HasBeginDictionary : decltype(HasBeginDictionary_Helper<Type>(0)) {};

		template<typename Formatter>
			struct FormatterTraits
		{
			static constexpr auto HasCharacterData = Internal::HasTryCharacterData<Formatter>::value;
			static constexpr auto HasSkipValueOrElement = Internal::HasSkipValueOrElement<Formatter>::value;
			static constexpr auto HasTryStringValue = Internal::HasTryStringValue<Formatter>::value;
			static constexpr auto HasTryRawValue = Internal::HasTryRawValue<Formatter>::value;
			static constexpr auto HasTryCastValue = Internal::HasTryCastValue<Formatter>::value;
			static constexpr auto HasTryKeyedItemHash = Internal::HasTryKeyedItemHash<Formatter>::value;
			static constexpr auto HasReversedEndian = Internal::HasReversedEndian<Formatter>::value;
			static constexpr auto HasGetLocation = Internal::HasGetLocation<Formatter>::value;
			static constexpr auto HasBeginBlock = Internal::HasBeginBlock<Formatter>::value;
			static constexpr auto HasBeginElement = Internal::HasBeginElement<Formatter>::value;
			static constexpr auto HasBeginArray = Internal::HasBeginArray<Formatter>::value;
			static constexpr auto HasBeginDictionary = Internal::HasBeginDictionary<Formatter>::value;
		};
	}

	template<typename Formatter>
		NO_RETURN_PREFIX void ThrowFormatException(Formatter& formatter, StringSection<> msg) NO_RETURN_POSTFIX;

	template<typename Formatter>
		NO_RETURN_PREFIX void ThrowFormatException(Formatter& formatter, StringSection<> msg)
	{
		if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasGetLocation) {
			Throw(FormatException(msg, formatter.GetLocation()));
		} else {
			Throw(FormatException(msg, {}));
		}
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
					ThrowFormatException(formatter, "Malformed begin element while skipping forward");
				++subtreeEle;
				break;

			case FormatterBlob::EndElement:
				if (!subtreeEle) return;    // end now, while the EndElement is primed

				if (!formatter.TryEndElement())
					ThrowFormatException(formatter, "Malformed end element while skipping forward");
				--subtreeEle;
				break;

			case FormatterBlob::KeyedItem:
				if (!formatter.TryKeyedItem(dummy0))
					ThrowFormatException(formatter, "Malformed keyed item while skipping forward");
				break;

			case FormatterBlob::Value:
				// we must have either TryStringValue or TryRawValue, because we don't want to use some arbitrary type with TryCastValue
				if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue) {
					Utility::ImpliedTyping::TypeDesc type;
					IteratorRange<const void*> data;
					if (!formatter.TryRawValue(data, type))
						ThrowFormatException(formatter, "Malformed value while skipping forward");
				} else {
					if (!formatter.TryStringValue(dummy0))
						ThrowFormatException(formatter, "Malformed value while skipping forward");
				}
				break;

			case FormatterBlob::CharacterData:
				if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasCharacterData) {
					if (!formatter.TryCharacterData(dummy0))
						ThrowFormatException(formatter, "Malformed value while skipping forward");
				} else
					UNREACHABLE();
				break;

			default:
				ThrowFormatException(formatter, "Unexpected blob or end of stream hit while skipping forward");
			}
		}
	}

	template<typename Formatter, typename std::enable_if<!Formatters::Internal::FormatterTraits<Formatter>::HasSkipValueOrElement>::type* =nullptr>
		void SkipValueOrElement(Formatter& formatter)
	{
		typename Formatter::InteriorSection dummy0;
		if (formatter.PeekNext() == FormatterBlob::Value) {
			// we must have either TryStringValue or TryRawValue, because we don't want to use some arbitrary type with TryCastValue
			if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue) {
				Utility::ImpliedTyping::TypeDesc type;
				IteratorRange<const void*> data;
				if (!formatter.TryRawValue(data, type))
					ThrowFormatException(formatter, "Malformed value while skipping forward");
			} else {
				if (!formatter.TryStringValue(dummy0))
					ThrowFormatException(formatter, "Malformed value while skipping forward");
			}
		} else {
			if constexpr (Formatters::Internal::FormatterTraits<Formatter>::HasBeginElement) {
				if (!formatter.TryBeginElement())
					ThrowFormatException(formatter, "Expected begin element while skipping forward");
				SkipElement(formatter);
				if (!formatter.TryEndElement())
					ThrowFormatException(formatter, "Malformed end element while skipping forward");
			}
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
		if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasBeginBlock) {
			typename Formatter::EvaluatedTypeId evalTypeId;
			if (!formatter.TryBeginBlock(evalTypeId))
				ThrowFormatException(formatter, "Expecting begin block");
		} else {
			if (!formatter.TryBeginElement())
				ThrowFormatException(formatter, "Expecting begin element");
		}
	}

	template<typename Formatter>
		void RequireEndElement(Formatter& formatter)
	{
		if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasBeginBlock) {
			if (!formatter.TryEndBlock())
				ThrowFormatException(formatter, "Expecting end block");
		} else {
			if (!formatter.TryEndElement())
				ThrowFormatException(formatter, "Expecting end element");
		}
	}

	template<typename Formatter>
		void RequireBeginBlock(Formatter& formatter, typename Formatter::EvaluatedTypeId& evalTypeId)
	{
		if (!formatter.TryBeginBlock(evalTypeId))
			ThrowFormatException(formatter, "Expecting begin block");
	}

	template<typename Formatter>
		void RequireEndBlock(Formatter& formatter)
	{
		if (!formatter.TryEndBlock())
			ThrowFormatException(formatter, "Expecting end block");
	}

	template<typename Formatter>
		void RequireBeginArray(Formatter& formatter, unsigned& count, typename Formatter::EvaluatedTypeId& evalTypeId)
	{
		if (!formatter.TryBeginArray(count, evalTypeId))
			ThrowFormatException(formatter, "Expecting begin array");
	}

	template<typename Formatter>
		void RequireEndArray(Formatter& formatter)
	{
		if (!formatter.TryEndArray())
			ThrowFormatException(formatter, "Expecting end array");
	}

	template<typename Formatter>
		void RequireBeginDictionary(Formatter& formatter, unsigned& count, typename Formatter::EvaluatedTypeId& evalTypeId)
	{
		if (!formatter.TryBeginDictionary(count, evalTypeId))
			ThrowFormatException(formatter, "Expecting begin dictionary");
	}

	template<typename Formatter>
		void RequireEndDictionary(Formatter& formatter)
	{
		if (!formatter.TryEndDictionary())
			ThrowFormatException(formatter, "Expecting end dictionary");
	}

	template<typename Formatter>
		typename Formatter::InteriorSection RequireKeyedItem(Formatter& formatter)
	{
		typename Formatter::InteriorSection name;
		if (!formatter.TryKeyedItem(name))
			ThrowFormatException(formatter, "Expecting keyed item");
		return name;
	}

	template<typename Formatter>
		uint64_t RequireKeyedItemHash(Formatter& formatter)
	{
		uint64_t name;
		if (!formatter.TryKeyedItem(name))
			ThrowFormatException(formatter, "Expecting keyed item");
		return name;
	}

	template<typename Formatter>
		IteratorRange<const void*> RequireRawValue(Formatter& formatter, Utility::ImpliedTyping::TypeDesc& typeDesc)
	{
		if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue) {
			IteratorRange<const void*> value;
			if (!formatter.TryRawValue(value, typeDesc))
				ThrowFormatException(formatter, "Expecting value");
			return value;
		} else {
			typename Formatter::InteriorSection stringValue;
			if (!formatter.TryStringValue(stringValue))
				ThrowFormatException(formatter, "Expecting value");
			typeDesc = {Utility::ImpliedTyping::TypeOf<typename Formatter::InteriorSection::value_type>()._type, (uint32_t)stringValue.size(), Utility::ImpliedTyping::TypeHint::String};
			return {stringValue.begin(), stringValue.end()};
		}
	}

	template<typename Formatter>
		typename Formatter::InteriorSection RequireStringValue(Formatter& formatter)
	{
		if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasTryStringValue) {
			typename Formatter::InteriorSection value;
			if (!formatter.TryStringValue(value))
				ThrowFormatException(formatter, "Expecting string value");
			return value;
		} else {
			IteratorRange<const void*> value;
			Utility::ImpliedTyping::TypeDesc typeDesc;
			if (!formatter.TryRawValue(value, typeDesc))
				ThrowFormatException(formatter, "Expecting string value");
			if (typeDesc._typeHint != ImpliedTyping::TypeHint::String || typeDesc._type != ImpliedTyping::TypeOf<typename Formatter::InteriorSection::value_type>()._type)
				ThrowFormatException(formatter, "Expecting string value, but not some non-string type");
			return typename Formatter::InteriorSection {
				(const typename Formatter::InteriorSection::value_type*)value.begin(),
				(const typename Formatter::InteriorSection::value_type*)value.end() };
		}
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
				ThrowFormatException(formatter, StringMeld<256>() << "Expecting value of type " << typeid(Type).name());
			return result;
		} else if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue) {
			IteratorRange<const void*> value;
			Utility::ImpliedTyping::TypeDesc typeDesc;
			if (!formatter.TryRawValue(value, typeDesc))
				ThrowFormatException(formatter, StringMeld<256>() << "Expecting value of type " << typeid(Type).name());
			return Utility::ImpliedTyping::VariantNonRetained{typeDesc, value, ReversedEndian(formatter)}.RequireCastValue<Type>();
		} else {
			typename Formatter::InteriorSection value;
			Type result;
			if (	!formatter.TryStringValue(value)
				|| 	!Utility::ImpliedTyping::ConvertFullMatch(value, MakeOpaqueIteratorRange(result), Utility::ImpliedTyping::TypeOf<Type>()))
				ThrowFormatException(formatter, StringMeld<256>() << "Expecting value of type " << typeid(Type).name());
			return result;
		}
	}

	template<typename Formatter>
		bool TryRawValue(Formatter& formatter, IteratorRange<const void*>& data, ImpliedTyping::TypeDesc& typeDesc)
	{
		if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue) {
			return formatter.TryRawValue(data, typeDesc);
		} else {
			static_assert(Formatters::Internal::FormatterTraits<Formatter>::HasTryStringValue);
			StringSection<> str;
			if (!formatter.TryStringValue(str))
				return false;
			data = { str.begin(), str.end() };
			typeDesc = ImpliedTyping::TypeOf<char>();
			typeDesc._arrayCount = str.size();
			typeDesc._typeHint = ImpliedTyping::TypeHint::String;
			return true;
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
			ThrowFormatException(formatter, "Expecting character data");
		return value;
	}

	template<typename EnumType, std::optional<EnumType> StringToEnum(StringSection<>), typename Formatter>
		EnumType RequireEnum(Formatter& formatter)
	{
		if (typename Formatter::InteriorSection value; formatter.TryStringValue(value)) {
			auto result = StringToEnum(value);
			if (!result.has_value())
				ThrowFormatException(formatter, StringMeld<256>() << "Could not interpret (" << value << ") as (" << typeid(EnumType).name() << ")");
			return result.value();
		} else if constexpr (Formatters::Internal::FormatterTraits<Formatter>::HasTryCastValue) {
			std::underlying_type_t<EnumType> result = 0;
			if (!formatter.TryCastValue(MakeOpaqueIteratorRange(result), ImpliedTyping::TypeOf<decltype(result)>()))
				ThrowFormatException(formatter, StringMeld<256>() << "Expecting value of type " << typeid(EnumType).name());
			return (EnumType)result;
		} else if constexpr (Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue) {
			IteratorRange<const void*> value;
			Utility::ImpliedTyping::TypeDesc typeDesc;
			if (!formatter.TryRawValue(value, typeDesc))
				ThrowFormatException(formatter, StringMeld<256>() << "Expecting value of type " << typeid(EnumType).name());
			return Utility::ImpliedTyping::VariantNonRetained{typeDesc, value, ReversedEndian(formatter)}.RequireCastValue<std::underlying_type_t<EnumType>>();
		} else
			ThrowFormatException(formatter, "Expecting value");
	}

	template<typename Formatter>
		void LogFormatter(std::ostream& str, Formatter& formatter, unsigned indent=0)
	{
		bool pendingIndent = true;
		for (;;) {
			switch (formatter.PeekNext()) {
			case FormatterBlob::KeyedItem:
				if (pendingIndent) { str << StreamIndent(indent); pendingIndent = false; }
				str << "[" << RequireKeyedItem(formatter) << "]: ";
				break;
			case FormatterBlob::Value:
				if constexpr (Formatters::Internal::FormatterTraits<Formatter>::HasTryStringValue) {
					typename Formatter::InteriorSection value;
					if (formatter.TryStringValue(value)) {
						str << value << std::endl;
						pendingIndent = true;
						break;
					}
				}
				if constexpr (Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue) {
					IteratorRange<const void*> value;
					Utility::ImpliedTyping::TypeDesc typeDesc;
					if (formatter.TryRawValue(value, typeDesc)) {
						str << ImpliedTyping::AsString(value, typeDesc, false) << std::endl;
						pendingIndent = true;
						break;
					}
				}
				str << "<<unserializable value>>" << std::endl;
				pendingIndent = true;
				break;
			case FormatterBlob::BeginElement:
				RequireBeginElement(formatter);
				if (pendingIndent) { str << StreamIndent(indent); pendingIndent = false; }
				str << "~" << std::endl;
				pendingIndent = true;
				indent += 4;
				break;
			case FormatterBlob::EndElement:
				RequireEndElement(formatter);
				indent -= 4;
				break;
			case FormatterBlob::BeginArray:
				if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasBeginArray) {
					typename Formatter::EvaluatedTypeId typeId;
					unsigned count;
					RequireBeginArray(formatter, count, typeId);
					str << "~[" << count << "]" << std::endl;
					pendingIndent = true;
					indent += 4;
				} else
					UNREACHABLE();
				break;
			case FormatterBlob::EndArray:
				if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasBeginArray) {
					RequireEndArray(formatter);
					indent -= 4;
				} else
					UNREACHABLE();
				break;
			case FormatterBlob::BeginDictionary:
				if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasBeginDictionary) {
					typename Formatter::EvaluatedTypeId keyTypeId, valueTypeId;
					RequireBeginDictionary(formatter, keyTypeId, valueTypeId);
					str << "~[:]" << std::endl;
					pendingIndent = true;
					indent += 4;
				} else
					UNREACHABLE();
				break;
			case FormatterBlob::EndDictionary:
				if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasBeginDictionary) {
					RequireEndDictionary(formatter);
					indent -= 4;
				} else
					UNREACHABLE();
				break;
			case FormatterBlob::CharacterData:
				if constexpr(Formatters::Internal::FormatterTraits<Formatter>::HasCharacterData) {
					if (pendingIndent) { str << StreamIndent(indent); pendingIndent = false; }
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
