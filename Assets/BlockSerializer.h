// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/Exceptions.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/Streams/SerializationUtils.h"
#include <vector>
#include <iterator>
#include <type_traits>

namespace Assets
{

	namespace Internal
	{
		template<typename Type> static auto IsPodIterator_Helper(int) -> std::is_pod<std::decay_t<decltype(*std::declval<Type>())>>;
		template<typename...> static auto IsPodIterator_Helper(...) -> std::false_type;
		template<typename Type> constexpr bool IsPodIterator = decltype(IsPodIterator_Helper<Type>(0))::value;
	}

		////////////////////////////////////////////////////

	class BlockSerializer
	{
	public:
		enum class SpecialBuffer { Unknown, String, Vector, UniquePtr, IteratorRange, StringSection };
		
		template<typename Type, typename std::enable_if_t<Internal::IsPodIterator<Type>>* = nullptr>
			void    SerializeSubBlock(IteratorRange<Type> range, SpecialBuffer specialBuffer = SpecialBuffer::Unknown);

		template<typename Type, typename std::enable_if_t<!Internal::IsPodIterator<Type>>* = nullptr>
			void    SerializeSubBlock(IteratorRange<Type> range, SpecialBuffer specialBuffer = SpecialBuffer::Unknown);

		void    SerializeSubBlock(const BlockSerializer& subBlock, SpecialBuffer specialBuffer = SpecialBuffer::Unknown);
		void    SerializeRawSubBlock(IteratorRange<const void*> range, SpecialBuffer specialBuffer = SpecialBuffer::Unknown);

		void    SerializeSpecialBuffer(SpecialBuffer specialBuffer, IteratorRange<const void*> range);
		
		void    SerializeValue  ( uint8_t value );
		void    SerializeValue  ( uint16_t value );
		void    SerializeValue  ( uint32_t value );
		void    SerializeValue  ( uint64_t value );
		void    SerializeValue  ( float value );
		void    SerializeValue  ( const std::string& value );
		void    AddPadding      ( unsigned sizeInBytes );
			
		void    SerializeRawRange	( IteratorRange<const void*> data );
		template<typename Type>
			void    SerializeRaw    ( const Type& type );

		unsigned	CreateRecall(unsigned size);
		void		PushAtRecall(unsigned recallId, IteratorRange<const void*> value);
		void		PushSizeValueAtRecall(unsigned recallId);

		auto	AsMemoryBlock() const -> std::unique_ptr<uint8_t[], PODAlignedDeletor>;
		size_t	Size() const;
		size_t	SizePrimaryBlock() const;

		BlockSerializer();
		~BlockSerializer();
		BlockSerializer(BlockSerializer&&) never_throws;
		BlockSerializer& operator=(BlockSerializer&&) never_throws;

		static const size_t PtrFlagBit  = size_t(1)<<(size_t(sizeof(size_t)*8-1));
		static const size_t PtrMask     = ~PtrFlagBit;
		struct InternalPointer;

	protected:
		std::vector<uint8_t>			_memory;
		std::vector<uint8_t>			_trailingSubBlocks;
		std::vector<InternalPointer>	_internalPointers;
		struct Recall;
		std::vector<Recall>				_pendingRecalls;
		unsigned _nextRecallId = 0;

		void PushBackPointer(size_t value);
		void PushBackRaw(const void* data, size_t size);
		void PushBackRaw_SubBlock(const void* data, size_t size);
		void RegisterInternalPointer(const InternalPointer& ptr);
		void PushBackPlaceholder(SpecialBuffer specialBuffer);
	};

	void            Block_Initialize(void* block, const void* base=nullptr);
	const void*     Block_GetFirstObject(const void* blockStart);
	size_t          Block_GetSize(const void* block);
	std::unique_ptr<uint8_t[]>     Block_Duplicate(const void* block);

		////////////////////////////////////////////////////

	namespace Internal
	{
		template<typename T> struct HasSerializeMethod
		{
			template<typename U, void (U::*)(BlockSerializer&) const> struct FunctionSignature {};
			template<typename U> static std::true_type Test1(FunctionSignature<U, &U::SerializeMethod>*);
			template<typename U> static std::false_type Test1(...);
			static const bool Result = decltype(Test1<T>(0))::value;
		};

		template<typename Type> static auto SerializeAsValue_Helper(int) -> decltype(std::declval<BlockSerializer>().SerializeValue(std::declval<Type>()), std::true_type{});
		template<typename...> static auto SerializeAsValue_Helper(...) -> std::false_type;
		template<typename Type> struct SerializeAsValue : decltype(SerializeAsValue_Helper<Type>(0)) {};
	}

	template<typename Type, typename std::enable_if_t<Internal::HasSerializeMethod<Type>::Result>* = nullptr>
		void SerializationOperator(BlockSerializer& serializer, const Type& value)
			{ value.SerializeMethod(serializer); }

	template <typename Type, typename std::enable_if_t<Internal::SerializeAsValue<Type>::value>* = nullptr>
		void SerializationOperator(BlockSerializer& serializer, Type value)
			{ serializer.SerializeValue(value); }

	template <typename Type, typename std::enable_if_t<std::is_same_v<const bool, decltype(Type::SerializeRaw)> && Type::SerializeRaw>* = nullptr>
		void SerializationOperator(BlockSerializer& serializer, const Type& value)
			{ serializer.SerializeRaw(value); }

	template<typename Type, typename Allocator>
		void    SerializationOperator  ( BlockSerializer& serializer, const std::vector<Type, Allocator>& value )
	{
		serializer.SerializeSubBlock(MakeIteratorRange(value), BlockSerializer::SpecialBuffer::Vector);
	}

	template<typename Type>
		void    SerializationOperator  ( BlockSerializer& serializer, const SerializableVector<Type>& value )
	{
		serializer.SerializeSubBlock(MakeIteratorRange(value), BlockSerializer::SpecialBuffer::Vector);
	}

	template<typename Type, typename Deletor>
		void    SerializationOperator  ( BlockSerializer& serializer, const std::unique_ptr<Type, Deletor>& value, size_t count )
	{
		serializer.SerializeSubBlock(MakeIteratorRange(value.get(), &value[count]), BlockSerializer::SpecialBuffer::UniquePtr);
	}

	template<typename TypeLHS, typename TypeRHS>
		void SerializationOperator(BlockSerializer& serializer, const std::pair<TypeLHS, TypeRHS>& value)
			{ 
				SerializationOperator(serializer, value.first);
				SerializationOperator(serializer, value.second);
			}

		// the following has no implementation. Objects that don't match will attempt to use this implementation
	void SerializationOperator(BlockSerializer& serializer, ...) = delete;

		////////////////////////////////////////////////////

	template<typename Type, typename std::enable_if_t<!Internal::IsPodIterator<Type>>*>
		void    BlockSerializer::SerializeSubBlock(IteratorRange<Type> range, SpecialBuffer specialBuffer)
	{
		BlockSerializer temporaryBlock;
		for (const auto& i:range) SerializationOperator(temporaryBlock, i);
		SerializeSubBlock(temporaryBlock, specialBuffer);
	}

	template<typename Type, typename std::enable_if_t<Internal::IsPodIterator<Type>>*>
		void    BlockSerializer::SerializeSubBlock(IteratorRange<Type> range, SpecialBuffer specialBuffer)
	{
		SerializeRawSubBlock(range.template Cast<const void*>(), specialBuffer);
	}
		
	template<typename Type>
		void    BlockSerializer::SerializeRaw(const Type& type)
	{
		PushBackRaw(&type, sizeof(Type));
	}

	inline void    BlockSerializer::SerializeRawRange( IteratorRange<const void*> data ) { PushBackRaw(data.begin(), data.size()); }
}

