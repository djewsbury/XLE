// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/MemoryUtils.h"
#include "../Utility/UTFUtils.h"
#include "../Utility/StringFormat.h"
#include <string>
#include <sstream>
#include <any>
#include <type_traits>

inline auto MakeStoreableInAny(const char* type) { return std::string(type); }
inline auto MakeStoreableInAny(const utf16* type) { return std::u16string(type); }
inline auto MakeStoreableInAny(const utf32* type) { return std::u32string(type); }

template<int Count> inline auto MakeStoreableInAny(char (&type)[Count]) { return std::string(type); }
template<int Count> inline auto MakeStoreableInAny(utf16 (&type)[Count]) { return std::u16string(type); }
template<int Count> inline auto MakeStoreableInAny(utf32 (&type)[Count]) { return std::u32string(type); }

// don't allow raw pointers to be stored in an std::any InitializerPack directly, since there's
// no explicit lifetime management... It also resolves ambiguity with the char pointer overrides
// above
template<typename Type, typename std::enable_if_t<!std::is_pointer_v<Type>>* =nullptr>
	inline auto MakeStoreableInAny(const Type& type) { return type; }

template<typename CharType>
	inline auto MakeStoreableInAny(StringSection<CharType> type) { return type.AsString(); }

template<typename IteratorType>
	inline auto MakeStoreableInAny(IteratorRange<IteratorType> type) { static_assert("IteratorRange<>s cannot be stored in an std::any. Try explicitly creating a std::vector instead"); }

template<typename Type>
	std::ostream& MakeArchivableName(std::ostream& str, const Type& t) { return str << t; }
	
namespace Assets 
{
	namespace Internal
	{

		template<typename T> static auto HasSimpleHashing_(int) -> decltype(std::declval<T>().GetHash(), std::true_type{});
		template<typename T> static auto HasSimpleHashing_(int) -> decltype(std::declval<T>().GetGUID(), std::true_type{});
		template<typename T> static auto HasSimpleHashing_(int) -> decltype(Hash64(std::declval<const T&>(), 0ull), std::true_type{});
		template<typename T>  static auto HasSimpleHashing_(int) -> typename std::enable_if<std::is_integral<T>::value, std::true_type>::type;
		template<typename...> static auto HasSimpleHashing_(...) -> std::false_type;
		template<typename T> struct HasSimpleHashing : decltype(HasSimpleHashing_<T>(0)) {};


		template<typename T, typename std::enable_if<std::is_integral<T>::value>::type* = nullptr>
			uint64_t HashParam_Chain(const T& p, uint64_t seed) { return HashCombine(p, seed); }

		template<typename T, decltype(std::declval<T>().GetHash())* = nullptr>
			uint64_t HashParam_Chain(const T& p, uint64_t seed) { return HashCombine(p.GetHash(), seed); }

		template<typename T, decltype(std::declval<T>().GetGUID())* = nullptr>
			uint64_t HashParam_Chain(const T& p, uint64_t seed) { return HashCombine(p.GetGUID(), seed); }

		template<typename T, decltype(Hash64(std::declval<const T&>(), 0ull))* = nullptr>
			uint64_t HashParam_Chain(const T& p, uint64_t seed) { return Hash64(p, seed); }

		template<
			typename T, 
			decltype(std::declval<const T&>().begin() != std::declval<const T&>().end())* = nullptr, 
			std::enable_if_t<!HasSimpleHashing<T>::value>* =nullptr>
			uint64_t HashParam_Chain(const T& p, uint64_t seed) 
			{
				auto i = p.begin(), end=p.end();
				if (i == end) return seed;
				auto res = seed;
				for (;i!=end; ++i)
					res = HashParam_Chain(*i, res);
				return res;
			}

		template<
			typename T, 
			decltype(HashParam_Chain(*std::declval<const T&>(), 0ull))* = nullptr, 
			std::enable_if_t<!HasSimpleHashing<T>::value>* =nullptr>
			uint64_t HashParam_Chain(const T& p, uint64_t seed) { return HashParam_Chain(*p, seed); }



		template<typename T, typename std::enable_if<std::is_integral<T>::value>::type* = nullptr>
			uint64_t HashParam_Single(const T& p) { return IntegerHash64(p); }

		template<typename T, decltype(std::declval<T>().GetHash())* = nullptr>
			uint64_t HashParam_Single(const T& p) { return p.GetHash(); }

		template<typename T, decltype(std::declval<T>().GetGUID())* = nullptr>
			uint64_t HashParam_Single(const T& p) { return p.GetGUID(); }

		template<typename T, decltype(Hash64(std::declval<const T&>(), 0ull))* = nullptr>
			uint64_t HashParam_Single(const T& p) { return Hash64(p); }

		template<
			typename T, 
			decltype(std::declval<const T&>().begin() != std::declval<const T&>().end())* = nullptr, 
			std::enable_if_t<!HasSimpleHashing<T>::value>* =nullptr>
			uint64_t HashParam_Single(const T& p) 
			{
				auto i = p.begin(), end=p.end();
				if (i == end) return 0;
				auto res = HashParam_Single(*i++);
				for (;i!=end; ++i)
					res = HashParam_Chain(*i, res);
				return res;
			}

		template<
			typename T, 
			decltype(HashParam_Single(*std::declval<const T&>()))* = nullptr, 
			std::enable_if_t<!HasSimpleHashing<T>::value>* =nullptr>
			uint64_t HashParam_Single(const T& p) { return HashParam_Single(*p); }

		

		template <typename FirstParam, typename... Params>
			uint64_t BuildParamHash(FirstParam firstInitializer, Params... initialisers)
		{
				//  Note Hash64 is a relatively expensive hash function
				//      ... we might get away with using a simpler/quicker hash function
			uint64_t result = HashParam_Single(firstInitializer);
			int dummy[] = { 0, (result = HashParam_Chain(initialisers, result), 0)... };
			(void)dummy;
			return result;
		}

		inline uint64_t BuildParamHash() { return 0; }

		T1(Type) static auto IsStreamable(int) -> decltype(std::declval<std::ostream&>() << std::declval<const Type&>(), std::true_type{});
		T1(Type)  static auto IsStreamable(...) -> std::false_type;

		template<typename Type>
			static decltype(std::declval<std::ostream&>() << std::declval<const Type&>())
				StreamWithHashFallback(std::ostream& str, const Type& value, bool allowFilesystemCharacters)
			{
				if (allowFilesystemCharacters) {
					return str << value; 
				} else {
					// Unfortunately we can't filter the text passed through a stream
					// easily without using some temporary buffer. Boost seems to have some
					// classes to do this, but that seems like it's unlikely to reach the 
					// standard library
					StringMeld<256> temp;
					temp << value;
					for (auto& chr:temp.AsIteratorRange())
						if (chr == '/' || chr == '\\') chr = '-';
					return str << temp.AsStringSection();
				}
			}

		template<typename Type>
			std::enable_if_t<!decltype(IsStreamable<Type>(0))::value, std::ostream>& StreamWithHashFallback(std::ostream& str, const Type& value, bool allowFilesystemCharacters)
			{
				return str << HashParam_Single(value);
			}

		template<typename Type>
			std::ostream& StreamWithHashFallback(std::ostream& str, const std::shared_ptr<Type>& value, bool allowFilesystemCharacters)
			{
				return str << HashParam_Single(value);
			}

		template <typename Object>
			inline void StreamDashSeparated(std::basic_stringstream<char>& result, const Object& obj, bool allowFilesystemCharacters)
		{
			result << "-";
			StreamWithHashFallback(result, obj, allowFilesystemCharacters);
		}

		template <typename P0, typename... Params>
			std::basic_string<char> AsString(P0 p0, Params... initialisers)
		{
			std::basic_stringstream<char> result;
			result << p0;
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

		std::size_t GetCount() const { return _variantPack.size(); }
		bool IsEmpty() const { return _variantPack.empty(); }

		template<typename... Args>
			InitializerPack(Args... args)
		: _variantPack { MakeStoreableInAny(std::forward<Args>(args))... }
		{
			_nameFn = &Internal::MakeArchivableName_Pack<0, decltype(MakeStoreableInAny(std::declval<Args>()))...>;
			_hashFn = &Internal::MakeArchivableHash_Pack<decltype(MakeStoreableInAny(std::declval<Args>()))...>;
		}

	private:
		std::vector<std::any> _variantPack;

		using MakeArchivableNameFn = std::ostream& (*)(std::ostream&, const std::vector<std::any>&);
		using MakeArchivableHashFn = uint64_t (*)(const std::vector<std::any>&, uint64_t);
		MakeArchivableNameFn _nameFn;
		MakeArchivableHashFn _hashFn;
	};
}

