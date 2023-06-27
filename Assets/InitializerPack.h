// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/Prefix.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/StringFormat.h"
#include <string>
#include <sstream>
#include <any>
#include <type_traits>

// don't allow raw pointers to be stored in an std::any InitializerPack directly, since there's
// no explicit lifetime management... It also resolves ambiguity with the char pointer overrides
// above
template<typename Type>
	inline auto MakeStoreableInAny(const Type& type)
{
	static_assert(!std::is_pointer_v<std::decay_t<Type>>, "Avoid using raw pointer types with MakeStoreableInAny, since there is no lifetime management");
	return type;
}

inline auto MakeStoreableInAny(const char* type) { return std::string(type); }
inline auto MakeStoreableInAny(const char16_t* type) { return std::u16string(type); }
inline auto MakeStoreableInAny(const char32_t* type) { return std::u32string(type); }
#if __cplusplus >= 202002L
	inline auto MakeStoreableInAny(const char8_t* type) { return std::basic_string<char8_t>(type); }
#endif
inline auto MakeStoreableInAny(const wchar_t* type) { return std::wstring(type); }

inline auto MakeStoreableInAny(char* type) { return std::string(type); }
inline auto MakeStoreableInAny(char16_t* type) { return std::u16string(type); }
inline auto MakeStoreableInAny(char32_t* type) { return std::u32string(type); }
#if __cplusplus >= 202002L
	inline auto MakeStoreableInAny(char8_t* type) { return std::basic_string<char8_t>(type); }
#endif
inline auto MakeStoreableInAny(wchar_t* type) { return std::wstring(type); }

template<int Count> inline auto MakeStoreableInAny(char (&type)[Count]) { return std::string(type); }
template<int Count> inline auto MakeStoreableInAny(char16_t (&type)[Count]) { return std::u16string(type); }
template<int Count> inline auto MakeStoreableInAny(char32_t (&type)[Count]) { return std::u32string(type); }
#if __cplusplus >= 202002L
	template<int Count>  inline auto MakeStoreableInAny(char8_t (&type)[Count]) { return std::basic_string<char8_t>(type); }
#endif
template<int Count> inline auto MakeStoreableInAny(wchar_t (&type)[Count]) { return std::wstring(type); }

template<typename CharType>
	inline auto MakeStoreableInAny(StringSection<CharType> type) { return type.AsString(); }

template<typename IteratorType>
	inline auto MakeStoreableInAny(IteratorRange<IteratorType> type) { static_assert("IteratorRange<>s cannot be stored in an std::any. Try explicitly creating a std::vector instead"); }

template<typename Type>
	std::ostream& MakeArchivableName(std::ostream& str, const Type& t) { return str << t; }
	
namespace Assets 
{

	/// <summary>String initializer with constexpr hash</summary>
	/// Though assets initializers can be any type, strings are one of the most useful.
	/// This utility class imbues a StringSection<> with a hash value. The hash value will
	/// be generated at compile time for literal strings
	template<typename CharType=char>
		class Initializer : public StringSection<CharType>
	{
	public:
		constexpr uint64_t GetHash() const { return _hash; }

		constexpr Initializer(const CharType* start, const CharType* end) : StringSection<CharType>{start, end}, _hash(ConstHash64(start, end-start)) {}
        constexpr Initializer() : _hash(0) {}
        CLANG_ONLY(constexpr) Initializer(const CharType* nullTerm) : StringSection<CharType>{nullTerm}, _hash(ConstHash64(StringSection<CharType>::AsStringView())) {}
        Initializer(std::nullptr_t) = delete;
		constexpr explicit Initializer(std::basic_string_view<CharType> view) : StringSection<CharType>{view}, _hash(ConstHash64(view)) {}
		CLANG_ONLY(constexpr) Initializer(const CharType* start, size_t len) : StringSection<CharType>{start, len}, _hash(ConstHash64(start, len)) {}
        
		template<typename CT, typename A>
			Initializer(const std::basic_string<CharType, CT, A>& str) : StringSection<CharType>{str}, _hash(ConstHash64(str.data(), str.size())) {}

		// the following constructor is for the _initializer literal suffix only -- it avoids an issue on MSVC in which some Initializer constructors
		// can't be made constexpr safely
		CLANG_ONLY(constexpr) Initializer(const CharType* start, size_t len, uint64_t hash) : StringSection<CharType>{start, len}, _hash(hash) {}
	private:
		uint64_t _hash;
	};

	namespace Literals
	{
        CLANG_ONLY(XLE_CONSTEVAL_OR_CONSTEXPR) inline Initializer<char> operator"" _initializer(const char* str, const size_t len) never_throws { return Initializer{str, len, ConstHash64(str, len)}; }
	}

	template<typename CharType>
		constexpr Initializer<CharType> MakeInitializer(const CharType* start, const CharType* end) { return Initializer<CharType>{start, end}; }

	template<typename CharType>
		constexpr Initializer<CharType> MakeInitializer(const CharType* start, size_t len) { return Initializer<CharType>{start, len}; }

	template<typename CharType>
		constexpr Initializer<CharType> MakeInitializer(std::basic_string_view<CharType> view) { return Initializer<CharType>{view}; }

	template<typename CharType>
		constexpr Initializer<CharType> MakeInitializer(const CharType* nullTerm) { auto len = XlStringSize(nullTerm); return Initializer<CharType>{nullTerm, len, ConstHash64(nullTerm, len)}; }

///////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{

		#define DOES_SUBST_MEMBER(Name, ...)																		\
			template<typename T> static constexpr auto Name##_(int) -> decltype(__VA_ARGS__, std::true_type{});		\
			template<typename...> static constexpr auto Name##_(...) -> std::false_type;							\
			static constexpr bool Name = decltype(Name##_<Type>(0))::value;											\
			/**/

		template<typename Type>
			struct AssetHashTraits
		{
			DOES_SUBST_MEMBER(HasHash64Override, Hash64(std::declval<const T&>(), std::declval<uint64_t>()));

			DOES_SUBST_MEMBER(HasGetHash, std::declval<const T&>().GetHash());
			DOES_SUBST_MEMBER(HasGetGUID, std::declval<const T&>().GetGUID());
			DOES_SUBST_MEMBER(HasCalculateHash, std::declval<const T&>().CalculateHash(uint64_t(0)));

			DOES_SUBST_MEMBER(IsDereferenceable, *std::declval<const T&>());
			DOES_SUBST_MEMBER(HasBeginAndEnd, std::declval<const T&>().begin() != std::declval<const T&>().end());
			DOES_SUBST_MEMBER(IsStreamable, std::declval<std::ostream&>() << std::declval<const T&>());

			static constexpr bool IsNonIntegralHashable = HasHash64Override || HasGetHash || HasGetGUID || HasCalculateHash || IsDereferenceable || HasBeginAndEnd;
			static constexpr bool IsHashable = IsNonIntegralHashable || std::is_integral_v<Type> || std::is_enum_v<Type> || std::is_same_v<nullptr_t, Type>;
		};

		#undef DOES_SUBST_PATTERN

		template<typename T>
			uint64_t HashParam_Chain(const T& p, uint64_t seed)
		{
			using Traits = AssetHashTraits<std::decay_t<T>>;
			static_assert(Traits::IsHashable, "Parameter used in InitializerPack does not have a valid method to extract a hash");

			if constexpr (std::is_same_v<nullptr_t, std::decay_t<T>>) {		return seed+1;
			} else if constexpr (Traits::HasHash64Override) {				return Hash64(p, seed);
			} else if constexpr (Traits::IsDereferenceable) {				return p ? HashParam_Chain(*p, seed) : (seed+1);
			} else if constexpr (Traits::HasGetHash) {						return HashCombine(p.GetHash(), seed);
			} else if constexpr (Traits::HasGetGUID) {						return HashCombine(p.GetGUID(), seed);
			} else if constexpr (Traits::HasCalculateHash) {				return p.CalculateHash(seed);
			} else if constexpr (Traits::HasBeginAndEnd) {
				auto i = p.begin(), end=p.end();
				if (i == end) return seed;
				auto res = seed;
				for (;i!=end; ++i)
					res = HashParam_Chain(*i, res);
				return res;
			} else if constexpr (std::is_integral_v<T>) {
				return HashCombine(p, seed);
			} else if constexpr (std::is_enum_v<T>) {
				return HashCombine((uint64_t)p, seed);
			} else {
				UNREACHABLE();
			}
		}

		template<typename T>
			uint64_t HashParam_Single(const T& p)
		{
			using Traits = AssetHashTraits<std::decay_t<T>>;
			static_assert(Traits::IsHashable, "Parameter used in InitializerPack does not have a valid method to extract a hash");

			if constexpr (std::is_same_v<nullptr_t, std::decay_t<T>>) {		return DefaultSeed64;
			} else if constexpr (Traits::HasHash64Override) {				return Hash64(p);
			} else if constexpr (Traits::IsDereferenceable) {				return p ? HashParam_Single(*p) : DefaultSeed64;
			} else if constexpr (Traits::HasGetHash) {						return p.GetHash();
			} else if constexpr (Traits::HasGetGUID) {						return p.GetGUID();
			} else if constexpr (Traits::HasCalculateHash) {				return p.CalculateHash(DefaultSeed64);
			} else if constexpr (Traits::HasBeginAndEnd) {
				auto i = p.begin(), end=p.end();
				if (i == end) return 0;
				auto res = HashParam_Single(*i++);
				for (;i!=end; ++i)
					res = HashParam_Chain(*i, res);
				return res;
			} else if constexpr (std::is_integral_v<T>) {
				return IntegerHash64(p);
			} else if constexpr (std::is_enum_v<T>) {
				return IntegerHash64((uint64_t)p);
			} else {
				UNREACHABLE();
			}
		}

		template <typename FirstParam, typename... Params>
			uint64_t BuildParamHash(const FirstParam& firstInitializer, const Params&... initialisers)
		{
				//  Note Hash64 is a relatively expensive hash function
				//      ... we might get away with using a simpler/quicker hash function
			uint64_t result = HashParam_Single(firstInitializer);
			int dummy[] = { 0, (result = HashParam_Chain(initialisers, result), 0)... };
			(void)dummy;
			return result;
		}

		inline uint64_t BuildParamHash() { return 0; }

		template<typename Type>
			constexpr bool IsStringPointerType()
		{
			using Traits = AssetHashTraits<std::decay_t<Type>>;
			if constexpr (Traits::IsDereferenceable) {
				return std::is_same_v<char, decltype(*std::declval<Type>())> || std::is_same_v<char16_t, decltype(*std::declval<Type>())> || std::is_same_v<char32_t, decltype(*std::declval<Type>())>
					#if __cplusplus >= 202002L
						|| std::is_same_v<char8_t, decltype(*std::declval<Type>())> 
					#endif
					|| std::is_same_v<uint32_t, decltype(*std::declval<Type>())> || std::is_same_v<wchar_t, decltype(*std::declval<Type>())>;
			} else {
				return false;
			}
		}

		template<typename Type>
			constexpr bool IsHashablePointerType()
		{
			using Traits = AssetHashTraits<std::decay_t<Type>>;
			if constexpr (Traits::IsDereferenceable) {
				return AssetHashTraits<std::decay_t<decltype(*std::declval<Type>())>>::IsNonIntegralHashable;
			} else {
				return false;
			}
		}

		template<typename Type>
			constexpr bool IsStreamablePointerType()
		{
			using Traits = AssetHashTraits<std::decay_t<Type>>;
			if constexpr (Traits::IsDereferenceable) {
				return AssetHashTraits<std::decay_t<decltype(*std::declval<Type>())>>::IsStreamable;
			} else {
				return false;
			}
		}

		template<typename Type>
			std::ostream& StreamWithHashFallback(std::ostream& str, const Type& value, bool allowFilesystemCharacters)
		{
			// We need some special handling for string pointer types here.
			// 	For an input that is dereferenceable, we can either stream the value directly, or dereference first and then stream it
			// 	"const char*" style strings must be streamed directly
			// 	but most other pointers (and smart pointers) should be dereferenced first, and then streamed (in order to catch specializations for the object that is being pointed to)
			// We can't handle this with just plain overriding, we have to explicitly check for cases where dereferencing and taking the hash is preferable
			using Traits = AssetHashTraits<std::decay_t<Type>>;
			constexpr auto hashablePointerType = IsHashablePointerType<Type>();
			static_assert(hashablePointerType || Traits::IsStreamable || Traits::IsHashable, "Parameter used in InitializerPack does not have a valid method to convert to a string");
			static_assert(!Traits::IsDereferenceable || hashablePointerType || IsStringPointerType<Type>() || IsStreamablePointerType<Type>(), "Parameter used in InitializerPack looks like a pointer to a type that is not hashable and not streamable");

			if constexpr (hashablePointerType) {
				return StreamWithHashFallback(str, *value, allowFilesystemCharacters);
			} else if constexpr (Traits::IsStreamable) {
				if (allowFilesystemCharacters) {
					return str << value;
				} else {
					// Unfortunately we can't filter the text passed through a stream
					// easily without using some temporary buffer. Boost seems to have some
					// classes to do this, but that seems like it's unlikely to reach the 
					// standard library
					StringMeld<256> temp;
					temp.AsOStream() << value;
					for (auto& chr:temp.AsIteratorRange())
						if (chr == '/' || chr == '\\') chr = '-';
					return str << temp.AsStringSection();
				}
			} else if constexpr (Traits::IsHashable) {
				return str << std::hex << HashParam_Single(value) << std::dec;
			} else {
				UNREACHABLE();
			}
		}

		template <typename Object>
			inline void StreamDashSeparated(std::basic_stringstream<char>& result, const Object& obj, bool allowFilesystemCharacters)
		{
			result << "-";
			StreamWithHashFallback(result, obj, allowFilesystemCharacters);
		}

		template <typename P0, typename... Params>
			std::basic_string<char> AsString(const P0& p0, const Params&... initialisers)
		{
			std::basic_stringstream<char> result;
			StreamWithHashFallback(result, p0, true);
			int dummy[] = { 0, (StreamDashSeparated(result, initialisers, false), 0)... };
			(void)dummy;
			return result.str();
		}

		inline std::basic_string<char> AsString() { return {}; }

		template<std::size_t idx=0, typename... Args>
			std::ostream& MakeArchivableName_Pack(std::ostream& str, const std::vector<std::any>& variantPack)
		{
			if constexpr (sizeof...(Args) == 0) return str;

			if (idx != 0) str << "-";
			using TT = std::tuple<Args...>;
			const auto& value = std::any_cast<const std::tuple_element_t<idx, TT>&>(variantPack[idx]);
			StreamWithHashFallback(str, value, idx == 0);		// filesystem characters (ie directory separators) only allowed on the first initializer
			if constexpr ((idx+1) != sizeof...(Args)) {
				return MakeArchivableName_Pack<idx+1, Args...>(str, variantPack);
			} else
				return str;
		}

		template<typename FirstArg, typename... Args>
			uint64_t MakeArchivableHash_Pack(const std::vector<std::any>& variantPack, uint64_t seed)
		{
			auto iterator = variantPack.begin();
			uint64_t result = HashParam_Single(std::any_cast<const FirstArg&>(*iterator++));			
			int dummy[] = { 0, (result = HashParam_Chain(std::any_cast<const Args&>(*iterator++), result), 0)... };
			(void)dummy; (void)iterator;
			assert(iterator == variantPack.end());
            return result;
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	class InitializerPack
	{
	public:
		std::string ArchivableName() const
		{ 
			std::stringstream str;
			_nameFn(str, _variantPack);
			return str.str();
		}

		uint64_t ArchivableHash(uint64_t seed = DefaultSeed64) const
		{ 
			return _hashFn(_variantPack, seed);
		}

		template<typename Type>
			const Type& GetInitializer(unsigned idx) const
			{
				return std::any_cast<const Type&>(_variantPack[idx]);
			}

		const std::type_info& GetInitializerType(unsigned idx) const
		{
			return _variantPack[idx].type();
		}

		std::size_t GetCount() const { return _variantPack.size(); }
		bool IsEmpty() const { return _variantPack.empty(); }

		template<typename... Args>
			InitializerPack(Args&&... args)
		: _variantPack { MakeStoreableInAny(std::forward<Args>(args))... }
		{
			_nameFn = &Internal::MakeArchivableName_Pack<0, decltype(MakeStoreableInAny(std::declval<Args>()))...>;
			_hashFn = &Internal::MakeArchivableHash_Pack<decltype(MakeStoreableInAny(std::declval<Args>()))...>;
		}

		InitializerPack() = default;
		InitializerPack(InitializerPack&& moveFrom) = default;
		InitializerPack& operator=(InitializerPack&& moveFrom) = default;
		InitializerPack(const InitializerPack& copyFrom) = default;
		InitializerPack& operator=(const InitializerPack& copyFrom) = default;

		#if COMPILER_ACTIVE != COMPILER_TYPE_MSVC
			InitializerPack(InitializerPack& copyFrom) = default;
			InitializerPack& operator=(InitializerPack& copyFrom) = default;
		#endif

	private:
		std::vector<std::any> _variantPack;

		using MakeArchivableNameFn = std::ostream& (*)(std::ostream&, const std::vector<std::any>&);
		using MakeArchivableHashFn = uint64_t (*)(const std::vector<std::any>&, uint64_t);
		MakeArchivableNameFn _nameFn = nullptr;
		MakeArchivableHashFn _hashFn = nullptr;
	};
}

