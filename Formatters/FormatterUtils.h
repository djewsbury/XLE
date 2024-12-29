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
		#define TEST_SUBST_MEMBER(Name, ...)																		\
			template<typename T> static constexpr auto Name##_(int) -> decltype(__VA_ARGS__, std::true_type{});		\
			template<typename...> static constexpr auto Name##_(...) -> std::false_type;							\
			static constexpr bool Name = decltype(Name##_<Type>(0))::value;											\
			/**/

		template<typename Type>
			struct FormatterTraits
		{
			TEST_SUBST_MEMBER(HasSkipValueOrElement, 	std::declval<T>().SkipValueOrElement());
			TEST_SUBST_MEMBER(HasSkipElement, 			std::declval<T>().SkipElement());

			TEST_SUBST_MEMBER(HasCharacterData, 		std::declval<T>().TryCharacterData(std::declval<StringSection<typename T::value_type>&>()))
			TEST_SUBST_MEMBER(HasTryStringValue, 		std::declval<T>().TryStringValue(std::declval<StringSection<typename T::value_type>&>()))
			TEST_SUBST_MEMBER(HasTryRawValue, 			std::declval<T>().TryRawValue(std::declval<IteratorRange<const void*>&>(), std::declval<Utility::ImpliedTyping::TypeDesc&>()))
			TEST_SUBST_MEMBER(HasTryCastValue,			std::declval<T>().TryCastValue(std::declval<IteratorRange<void*>>(), std::declval<const Utility::ImpliedTyping::TypeDesc&>()))
			TEST_SUBST_MEMBER(HasTryKeyedItemHash,		std::declval<T>().TryKeyedItem(std::declval<uint64_t&>()))

			TEST_SUBST_MEMBER(HasGetLocation,			std::declval<T>().GetLocation())
			
			TEST_SUBST_MEMBER(HasReversedEndian,		std::declval<T>().ReversedEndian())
			TEST_SUBST_MEMBER(HasBeginBlock,			std::declval<T>().TryBeginBlock(std::declval<typename T::EvaluatedTypeId&>()))
			TEST_SUBST_MEMBER(HasBeginElement,			std::declval<T>().TryBeginElement())
			TEST_SUBST_MEMBER(HasBeginArray,			std::declval<T>().TryBeginArray(std::declval<unsigned&>()))
			TEST_SUBST_MEMBER(HasBeginDictionary,		std::declval<T>().TryBeginDictionary(std::declval<typename T::EvaluatedTypeId&>(), std::declval<typename T::EvaluatedTypeId&>()))
		};

		#undef TEST_SUBST_MEMBER
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
		if constexpr (Formatters::Internal::FormatterTraits<Formatter>::HasSkipElement) {
			formatter.SkipElement();
		} else {
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
	}

	template<typename Formatter>
		void SkipValueOrElement(Formatter& formatter)
	{
		if constexpr (Formatters::Internal::FormatterTraits<Formatter>::HasSkipValueOrElement) {
			formatter.SkipValueOrElement();
		} else {
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
			typeDesc._arrayCount = (uint32_t)str.size();
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
