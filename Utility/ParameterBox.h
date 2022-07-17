// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ImpliedTyping.h"
#include "UTFUtils.h"
#include "StringUtils.h"
#include "IteratorUtils.h"
#include "Streams/SerializationUtils.h"
#include "../Core/Types.h"
#include <string>
#include <vector>

namespace Utility
{
    class OutputStreamFormatter;
    template<typename CharType> class InputStreamFormatter;
    static constexpr const unsigned NativeRepMaxSize = MaxPath * 4;

///////////////////////////////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////
            //      P A R A M E T E R   B O X                       //
        //////////////////////////////////////////////////////////////////

            //      a handy abstraction to represent a number of 
            //      parameters held together. We must be able to
            //      quickly merge and filter values in this table.

    #pragma pack(push)
    #pragma pack(1)

    class ParameterBox
    {
    public:
        using ParameterNameHash = uint64_t;

        class ParameterName
        {
        public:
            ParameterNameHash _hash;

            ParameterName(StringSection<> name);
            ParameterName(ParameterNameHash hash);
            ParameterName(const char name[]);
            ParameterName(const std::string& name);
        };

        static ParameterNameHash    MakeParameterNameHash(StringSection<> name);

        using TypeDesc = ImpliedTyping::TypeDesc;

        ////////////////////////////////////////////////////////////////////////////////////////
            //      S E T                                                   //
        ////////////////////////////////////////////////////////////////////////////////////////

        void            SetParameter(StringSection<utf8> name, IteratorRange<const void*> data, const TypeDesc& type);
        void            SetParameter(StringSection<utf8> name, StringSection<char> stringData);
        void            SetParameter(StringSection<utf8> name, const std::string& stringData);
        T1(Type) void   SetParameter(StringSection<utf8> name, Type value);

        void            SetParameter(ParameterNameHash nameHash, IteratorRange<const void*> data, const TypeDesc& type);
		void			RemoveParameter(ParameterName name);

        // avoid ambiguity
        void            SetParameter(StringSection<utf8> name, const char* stringData) { SetParameter(name, MakeStringSection(stringData)); }
        
        ////////////////////////////////////////////////////////////////////////////////////////
            //      G E T                                                   //
        ////////////////////////////////////////////////////////////////////////////////////////

        T1(Type) std::optional<Type>  GetParameter(ParameterName name) const;
        T1(Type) Type   GetParameter(ParameterName name, const Type& def) const;
        bool            GetParameter(ParameterName name, void* dest, const TypeDesc& destType) const;
        bool            HasParameter(ParameterName name) const;
        TypeDesc        GetParameterType(ParameterName name) const;
		IteratorRange<const void*>	GetParameterRawValue(ParameterName name) const;

        std::optional<std::string>      GetParameterAsString(ParameterName name) const;

        ////////////////////////////////////////////////////////////////////////////////////////
            //      H A S H   V A L U E S                                   //
        ////////////////////////////////////////////////////////////////////////////////////////

        uint64_t  GetHash() const;
        uint64_t  GetParameterNamesHash() const;
        uint64_t  CalculateFilteredHashValue(const ParameterBox& source) const;
        bool    AreParameterNamesEqual(const ParameterBox& other) const;
        IteratorRange<const void*> GetValueTable() const;

        ////////////////////////////////////////////////////////////////////////////////////////
            //      M E R G I N G   &   I T E R A T O R                     //
        ////////////////////////////////////////////////////////////////////////////////////////

        void    MergeIn(const ParameterBox& source);

        class Iterator
        {
        public:
			class Value
			{
			public:
				StringSection<utf8>			Name() const;
				IteratorRange<const void*>	RawValue() const;
				const TypeDesc&				Type() const;
				ParameterNameHash			HashName() const;
				std::string					ValueAsString(bool strongTyping = false) const;
			
				size_t                  _index;
				const ParameterBox*     _box;

			private:
				Value(const ParameterBox& box, size_t index);
				Value();
				friend class Iterator;
			};

            Iterator operator++();
			const Value& operator*() const;
			const Value* operator->() const;
			friend bool operator==(const Iterator&, const Iterator&);
			friend bool operator!=(const Iterator&, const Iterator&);

        private:
            Value _value;

            Iterator(const ParameterBox& box, size_t index);
            Iterator();
            friend class ParameterBox;
        };

        Iterator    begin() const;
		Iterator	end() const;
        Iterator    at(size_t index) const;
        size_t      GetCount() const;

        ////////////////////////////////////////////////////////////////////////////////////////
            //      S E R I A L I S A T I O N                               //
        ////////////////////////////////////////////////////////////////////////////////////////

        template<typename CharType>
            void    SerializeWithCharType(OutputStreamFormatter& stream) const;

        ParameterBox();
        ParameterBox(std::initializer_list<std::pair<const utf8*, const char*>>);
        template<typename CharType>
            ParameterBox(InputStreamFormatter<CharType>& stream, 
				IteratorRange<const void*> defaultValue = {}, 
                const ImpliedTyping::TypeDesc& defaultValueType = ImpliedTyping::TypeDesc{ImpliedTyping::TypeCat::Void, 0u});
        ParameterBox(ParameterBox&& moveFrom) never_throws;
        ParameterBox& operator=(ParameterBox&& moveFrom) never_throws;
		
		#if defined(COMPILER_DEFAULT_IMPLICIT_OPERATORS)
			ParameterBox(const ParameterBox&) = default;
			ParameterBox& operator=(const ParameterBox&) = default;
		#endif

        ~ParameterBox();
    private:
        mutable uint64_t      _cachedHash;
        mutable uint64_t      _cachedParameterNameHash;

		class OffsetsEntry
		{
		public:
			uint32_t _nameBegin, _valueBegin;
			uint32_t _nameSize, _valueSize;
		};

        SerializableVector<ParameterNameHash>	_hashNames;
        SerializableVector<OffsetsEntry>		_offsets;
        SerializableVector<utf8>				_names;
        SerializableVector<uint8_t>				_values;
        SerializableVector<TypeDesc>			_types;

        uint64_t              CalculateHash() const;
        uint64_t              CalculateParameterNamesHash() const;

        void SetParameter(
            ParameterNameHash hash, StringSection<utf8> name, IteratorRange<const void*> value,
            const ImpliedTyping::TypeDesc& insertType);

        SerializableVector<ParameterNameHash>::const_iterator SetParameterHint(
            SerializableVector<ParameterNameHash>::const_iterator paramNameHash,
            ParameterNameHash hash, StringSection<utf8> name, IteratorRange<const void*> value,
            const ImpliedTyping::TypeDesc& insertType, bool checkStringConversion);

		template<typename Stream>
			friend void SerializationOperator(Stream& serializer, const ParameterBox& box);
        friend void SerializationOperator(std::ostream& serializer, const ParameterBox& box);
    };

    #pragma pack(pop)

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename Type> 
        Type ParameterBox::GetParameter(ParameterName name, const Type& def) const
    {
        auto q = GetParameter<Type>(name);
        if (q.has_value()) return q.value();
        return def;
    }

    inline ParameterBox::ParameterName::ParameterName(StringSection<> name)
    {
        _hash = ParameterBox::MakeParameterNameHash(name);
    }

    inline ParameterBox::ParameterName::ParameterName(const char name[])
    {
        _hash = ParameterBox::MakeParameterNameHash(name);
    }

    inline ParameterBox::ParameterName::ParameterName(const std::string& name)
    {
        _hash = ParameterBox::MakeParameterNameHash(MakeStringSection(name));
    }

    inline ParameterBox::ParameterName::ParameterName(ParameterNameHash hash)
    {
        _hash = hash;
    }

    template<typename Type>
        void ParameterBox::SetParameter(StringSection<utf8> name, Type value)
    {
        const auto insertType = ImpliedTyping::TypeOf<Type>();
        SetParameter(name, MakeOpaqueIteratorRange(value), insertType);
    }

    inline void ParameterBox::SetParameter(StringSection<utf8> name, const std::string& stringData) { SetParameter(name, MakeStringSection(stringData)); }
    
    uint8_t* ValueTableOffset(SerializableVector<uint8_t>& values, size_t offset);
    const uint8_t* ValueTableOffset(const SerializableVector<uint8_t>& values, size_t offset);

    template<typename Type>
        std::optional<Type> ParameterBox::GetParameter(ParameterName name) const
    {
        auto i = std::lower_bound(_hashNames.cbegin(), _hashNames.cend(), name._hash);
        if (i!=_hashNames.cend() && *i == name._hash) {
            size_t index = std::distance(_hashNames.cbegin(), i);
            auto offset = _offsets[index];

            if (_types[index] == ImpliedTyping::TypeOf<Type>()) {
                return *(Type*)ValueTableOffset(_values, offset._valueBegin);
            } else {
                Type result;
                if (ImpliedTyping::Cast(
                    MakeOpaqueIteratorRange(result), ImpliedTyping::TypeOf<Type>(),
                    { ValueTableOffset(_values, offset._valueBegin), ValueTableOffset(_values, offset._valueBegin+offset._valueSize) },
                    _types[index])) {
					return result;
                }
            }
        }
		return {};
    }

    void SerializationOperator(std::ostream& serializer, const ParameterBox& box);

    template<typename Stream>
        void SerializationOperator(Stream& serializer, const ParameterBox& box)
    {
        SerializationOperator(serializer, box._cachedHash);
        SerializationOperator(serializer, box._cachedParameterNameHash);
        SerializationOperator(serializer, box._hashNames);
        SerializationOperator(serializer, box._offsets);
        SerializationOperator(serializer, box._names);
        SerializationOperator(serializer, box._values);
        SerializationOperator(serializer, box._types);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    inline auto ParameterBox::begin() const -> Iterator
    {
        return Iterator(*this, 0);
    }

	inline auto ParameterBox::end() const -> Iterator
    {
        return Iterator(*this, _offsets.size());
    }

    inline auto ParameterBox::at(size_t index) const -> Iterator
    {
        if (index >= _offsets.size()) return Iterator();
        return Iterator(*this, index);
    }

    inline StringSection<utf8> ParameterBox::Iterator::Value::Name() const
    {
        return MakeStringSection(
			&_box->_names[_box->_offsets[_index]._nameBegin], 
			&_box->_names[_box->_offsets[_index]._nameBegin + _box->_offsets[_index]._nameSize]);
    }

    inline auto        ParameterBox::Iterator::Value::Type() const -> const TypeDesc&
    {
        return _box->_types[_index];
    }

    inline auto   ParameterBox::Iterator::Value::HashName() const -> ParameterNameHash
    {
        return _box->_hashNames[_index];
    }

    inline std::string   ParameterBox::Iterator::Value::ValueAsString(bool strongTyping) const
    {
        return ImpliedTyping::AsString(RawValue(), Type(), strongTyping);
    }

	inline ParameterBox::Iterator::Value::Value(const ParameterBox& box, size_t index)
    : _box(&box), _index(index)
    {}

    inline ParameterBox::Iterator::Value::Value() : _index(0), _box(nullptr) {}

    inline auto ParameterBox::Iterator::operator++() -> Iterator
    {
        ++_value._index;
        return *this;
    }

	inline auto ParameterBox::Iterator::operator*() const -> const Value& { return _value; }
	inline auto ParameterBox::Iterator::operator->() const -> const Value* { return &_value; }

	inline bool operator==(const ParameterBox::Iterator& lhs, const ParameterBox::Iterator& rhs)
	{
		return lhs._value._box == rhs._value._box && lhs._value._index == rhs._value._index;
	}

	inline bool operator!=(const ParameterBox::Iterator& lhs, const ParameterBox::Iterator& rhs)
	{
		return lhs._value._box != rhs._value._box || lhs._value._index != rhs._value._index;
	}

    inline ParameterBox::Iterator::Iterator(const ParameterBox& box, size_t index)
    : _value(box, index)
    {}

    inline ParameterBox::Iterator::Iterator() {}

    inline IteratorRange<const void*> ParameterBox::GetValueTable() const
    {
        return MakeIteratorRange(_values);
    }

    using StringTable = std::vector<std::pair<const utf8*, std::string>>;
    XLE_DEPRECATED_ATTRIBUTE void    BuildStringTable(StringTable& defines, const ParameterBox& box);
    XLE_DEPRECATED_ATTRIBUTE void    OverrideStringTable(StringTable& defines, const ParameterBox& box);
    XLE_DEPRECATED_ATTRIBUTE std::string FlattenStringTable(const StringTable& stringTable);
	std::string BuildFlatStringTable(const ParameterBox& box);
}

using namespace Utility;
