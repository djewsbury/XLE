// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ParameterBox.h"
#include "MemoryUtils.h"
#include "PtrUtils.h"
#include "StringUtils.h"
#include "IteratorUtils.h"
#include "MemoryUtils.h"
#include "StringFormat.h"
#include "Conversion.h"
#include "Streams/StreamFormatter.h"
#include "Streams/OutputStreamFormatter.h"
#include <algorithm>
#include <utility>
#include <sstream>

namespace Utility
{
///////////////////////////////////////////////////////////////////////////////////////////////////

    ParameterBox::ParameterNameHash    ParameterBox::MakeParameterNameHash(StringSection<> name)
    {
		// If the variable name has array indexor syntax, we strip off that syntax and use
        // the indexor as a offset for the hash value. This makes it possible to store arrays,
		// and has a couple of interesting side effects.
		//		- array elements always get stored subsequentially
		//		- only single dimensional arrays are supported, because this syntax has no hints for how to arrange multi dimensional arrays in the hash space
		//		- "something[0]" and "something" evaluate to the same hash value
		//		- only positive integer array indexors are supported, but octal or hex numbers can be used
        if (name.size() >= 2 && *(name.end()-1) == ']') {
            auto i = &name[name.size()-2];
            while (i > name.begin() && ((*i >= '0' && *i <= '9') || *i == 'x' || *i == '+')) --i;
			if (*i == '[') {
				char* end = nullptr;
				auto indexor = strtoul(i+1, &end, 0);
				if (end == name.end()-1) {
					// successful parse, we can use the array syntax interpretation
					return MakeParameterNameHash(MakeStringSection(name.begin(), i)) + indexor;
				}
			}
        }

        return Hash64(name.begin(), name.end());
    }
    
    void ParameterBox::SetParameter(StringSection<utf8> name, StringSection<char> stringData)
    {
        using namespace ImpliedTyping;
        if (stringData.IsEmpty()) {
                // null values or empty strings become "void" type parameters
			SetParameter(name, {}, TypeDesc{TypeCat::Void, 0});
            return;
        }

        uint8_t buffer[NativeRepMaxSize];
		assert(stringData.size() < NativeRepMaxSize);
        auto typeDesc = ParseFullMatch(stringData, MakeIteratorRange(buffer));
        if (typeDesc._type != TypeCat::Void) {
			SetParameter(name, {buffer, PtrAdd(buffer, std::min(sizeof(buffer), (size_t)typeDesc.GetSize()))}, typeDesc);
        } else {
            // no conversion... just store a string
            auto hash = MakeParameterNameHash(name);
            SetParameterHint(
                std::lower_bound(_hashNames.cbegin(), _hashNames.cend(), hash),
				hash, name, MakeIteratorRange(stringData.begin(), stringData.end()),
                TypeDesc{TypeCat::UInt8, (uint16_t)(stringData.size()), TypeHint::String}, false);
        }
    }

	template<>
        void ParameterBox::SetParameter(StringSection<utf8> name, const utf8* value)
    {
        SetParameter(name, MakeStringSection(value));
    }

    uint8_t* ValueTableOffset(SerializableVector<uint8_t>& values, size_t offset)
    {
        return PtrAdd(AsPointer(values.begin()), offset);
    }

    const uint8_t* ValueTableOffset(const SerializableVector<uint8_t>& values, size_t offset)
    {
        return PtrAdd(AsPointer(values.begin()), offset);
    }

    void ParameterBox::SetParameter(
        StringSection<utf8> name, IteratorRange<const void*> value, 
        const ImpliedTyping::TypeDesc& insertType)
    {
        SetParameter(MakeParameterNameHash(name), name, value, insertType);
    }

    void ParameterBox::SetParameter(ParameterNameHash nameHash, IteratorRange<const void*> data, const TypeDesc& type)
    {
        SetParameter(nameHash, {}, data, type);
    }

    void ParameterBox::SetParameter(
        ParameterNameHash hash, StringSection<utf8> name, IteratorRange<const void*> value,
        const ImpliedTyping::TypeDesc& insertType)
    {
        SetParameterHint(
            std::lower_bound(_hashNames.cbegin(), _hashNames.cend(), hash),
            hash, name,
            value, insertType, true);
    }

    auto ParameterBox::SetParameterHint(
        SerializableVector<ParameterNameHash>::const_iterator i,
        ParameterNameHash hash, StringSection<utf8> name, IteratorRange<const void*> value,
        const ImpliedTyping::TypeDesc& insertType, bool checkStringConversion) -> SerializableVector<ParameterNameHash>::const_iterator
    {
		assert(value.size() == insertType.GetSize());

        // If the input is a string, and we can parse it into a simplier type, then let's store it as that simplier type instead
        if (insertType._typeHint == ImpliedTyping::TypeHint::String && checkStringConversion) {
            uint8_t parsedTypeBuffer[NativeRepMaxSize];
            if (insertType._type == ImpliedTyping::TypeCat::UInt8 || insertType._type == ImpliedTyping::TypeCat::Int8) {
                auto parsedType = ImpliedTyping::ParseFullMatch(MakeStringSection((const char*)value.begin(), (const char*)value.end()), MakeIteratorRange(parsedTypeBuffer));
                if (parsedType._type != ImpliedTyping::TypeCat::Void) {
                    assert(parsedType.GetSize() <= sizeof(parsedTypeBuffer));
                    return SetParameterHint(i, hash, name, MakeIteratorRange(parsedTypeBuffer, parsedTypeBuffer+parsedType.GetSize()), parsedType, false);
                }
            } else if (insertType._type == ImpliedTyping::TypeCat::UInt16 || insertType._type == ImpliedTyping::TypeCat::Int16) {
                assert(0);      // parsing for long char types not supported
            } else if (insertType._type == ImpliedTyping::TypeCat::UInt32 || insertType._type == ImpliedTyping::TypeCat::Int32) {
                assert(0);      // parsing for long char types not supported
            }
        }

        if (i==_hashNames.cend()) {
                // push new value onto the end (including name & type info)
            _hashNames.push_back(hash);

            auto valueOffset = _values.size();
            auto nameOffset = _names.size();
            
            _values.insert(_values.end(), (const uint8_t*)value.begin(), (const uint8_t*)value.end());

            if (!name.IsEmpty())
                _names.insert(_names.end(), name.begin(), name.end());
            _names.push_back(0);

			_offsets.push_back(OffsetsEntry{unsigned(nameOffset), unsigned(valueOffset), unsigned(name.size()), unsigned(value.size())});
            _types.push_back(insertType);

            _cachedHash = 0;
            _cachedParameterNameHash = 0;
            return _hashNames.end()-1;
        }

        size_t index = std::distance(_hashNames.cbegin(), i);
        if (*i!=hash) {
                // insert new value in the middle somewhere
            i = _hashNames.insert(i, hash);

            const auto nameLength = name.size()+1;
            auto dstOffsets = _offsets[index];
			dstOffsets._nameSize = (unsigned)name.size();
			dstOffsets._valueSize = (unsigned)value.size();

            _offsets.insert(_offsets.begin()+index, dstOffsets);
            for (auto i2=_offsets.begin()+index+1; i2<_offsets.end(); ++i2) {
                i2->_nameBegin += unsigned(nameLength);
                i2->_valueBegin += unsigned(value.size());
            }

            _values.insert(
                _values.cbegin()+dstOffsets._valueBegin, 
                (uint8_t*)value.begin(), (uint8_t*)value.end());
            if (!name.IsEmpty())
                _names.insert(_names.cbegin()+dstOffsets._nameBegin, name.begin(), name.end());
            _names.insert(_names.cbegin()+dstOffsets._nameBegin+name.size(), 0);
            _types.insert(_types.begin() + index, insertType);

            _cachedHash = 0;
            _cachedParameterNameHash = 0;
            return i;
        }

            // just update the value
        const auto offset = _offsets[index];

        assert(name.IsEmpty()|| !XlCompareString(&_names[offset._nameBegin], name));

        if (offset._valueSize == value.size()) {

                // same type, or type with the same size...
            XlCopyMemory(ValueTableOffset(_values, offset._valueBegin), (uint8_t*)value.begin(), value.size());
            _types[index] = insertType;

        } else {

                // if the size of the type changes, we need to adjust the values table a bit
                // hopefully this should be an uncommon case
			auto prevSize = _offsets[index]._valueSize;
            signed sizeChange = signed(value.size()) - signed(prevSize);
			_offsets[index]._valueSize = (unsigned)value.size();
            auto dstOffsets = _offsets[index];

            for (auto i2=_offsets.begin()+index+1; i2<_offsets.end(); ++i2) {
                i2->_valueBegin += sizeChange;
            }

			if (prevSize != 0) {
				_values.erase(
					_values.cbegin()+dstOffsets._valueBegin,
					_values.cbegin()+dstOffsets._valueBegin+prevSize);
			}
            _values.insert(
                _values.cbegin()+dstOffsets._valueBegin, 
                (uint8_t*)value.begin(), (uint8_t*)value.end());
            _types[index] = insertType;

        }

        _cachedHash = 0;
        return i;
    }

	void ParameterBox::RemoveParameter(ParameterName name)
	{
		auto i = std::lower_bound(_hashNames.begin(), _hashNames.end(), name._hash);
        if (i==_hashNames.end() || *i != name._hash)
			return;

		auto index = std::distance(_hashNames.begin(), i);

		{
			auto prevSize = _offsets[index]._valueSize;
			if (prevSize != 0) {
				_values.erase(
					_values.cbegin() + _offsets[index]._valueBegin,
					_values.cbegin() + _offsets[index]._valueBegin + prevSize);
			}

            signed sizeChange = 0 - signed(prevSize);
            for (auto i2=_offsets.begin()+index+1; i2<_offsets.end(); ++i2)
                i2->_valueBegin += sizeChange;
        }

		{
			auto prevSize = _offsets[index]._nameSize;
			assert(prevSize != 0);
			_names.erase(
                _names.cbegin() + _offsets[index]._nameBegin,
                _names.cbegin() + _offsets[index]._nameBegin + prevSize);

            signed sizeChange = 0 - signed(prevSize);
			for (auto i2=_offsets.begin()+index+1; i2<_offsets.end(); ++i2)
                i2->_nameBegin += sizeChange;
        }

		_hashNames.erase(_hashNames.begin() + index);
		_offsets.erase(_offsets.begin() + index);
		_types.erase(_types.begin() + index);
	}

    bool ParameterBox::GetParameter(ParameterName name, void* dest, const ImpliedTyping::TypeDesc& destType) const
    {
        auto i = std::lower_bound(_hashNames.cbegin(), _hashNames.cend(), name._hash);
        if (i!=_hashNames.cend() && *i == name._hash) {
            size_t index = std::distance(_hashNames.cbegin(), i);
            auto offset = _offsets[index];

            if (_types[index] == destType) {
                XlCopyMemory(dest, ValueTableOffset(_values, offset._valueBegin), offset._valueSize);
                return true;
            }
            else {
                return ImpliedTyping::Cast(
                    { dest, PtrAdd(dest, destType.GetSize()) }, destType,
                    { ValueTableOffset(_values, offset._valueBegin), ValueTableOffset(_values, offset._valueBegin+offset._valueSize) },
                    _types[index]);
            }
        }
        return false;
    }

    bool ParameterBox::HasParameter(ParameterName name) const
    {
        auto i = std::lower_bound(_hashNames.cbegin(), _hashNames.cend(), name._hash);
        return i!=_hashNames.cend() && *i == name._hash;
    }

    ImpliedTyping::TypeDesc ParameterBox::GetParameterType(ParameterName name) const
    {
        auto i = std::lower_bound(_hashNames.cbegin(), _hashNames.cend(), name._hash);
        if (i!=_hashNames.cend() && *i == name._hash) {
            return _types[std::distance(_hashNames.cbegin(), i)];
        }
        return ImpliedTyping::TypeDesc{ImpliedTyping::TypeCat::Void, 0};
    }

	IteratorRange<const void*> ParameterBox::GetParameterRawValue(ParameterName name) const
	{
		auto i = std::lower_bound(_hashNames.cbegin(), _hashNames.cend(), name._hash);
		if (i != _hashNames.cend() && *i == name._hash) {
			size_t index = std::distance(_hashNames.cbegin(), i);
			auto offset = _offsets[index];
			return {ValueTableOffset(_values, offset._valueBegin), ValueTableOffset(_values, offset._valueBegin+offset._valueSize)};
		}
		return {};
	}

    std::optional<std::string> ParameterBox::GetParameterAsString(ParameterName name) const
    {
        auto i = std::lower_bound(_hashNames.cbegin(), _hashNames.cend(), name._hash);
        if (i!=_hashNames.cend() && *i == name._hash) {
            size_t index = std::distance(_hashNames.cbegin(), i);
            const auto& type = _types[index];
            
			auto offset = _offsets[index];
			IteratorRange<const void*> rawValue{ValueTableOffset(_values, offset._valueBegin), ValueTableOffset(_values, offset._valueBegin+offset._valueSize)};

            return ImpliedTyping::AsString(rawValue, type);
        }

        return {};
    }

    uint64_t      ParameterBox::CalculateParameterNamesHash() const
    {
            //  Note that the parameter names are always in the same order (unless 
            //  two different names resolve to the same 32 hash value). So we should be
			//	ok if the same parameter names are added in 2 different orders.
        return Hash64(AsPointer(_hashNames.cbegin()), AsPointer(_hashNames.cend()));
    }

    uint64_t      ParameterBox::CalculateHash() const
    {
        return Hash64(AsPointer(_values.cbegin()), AsPointer(_values.cend()));
    }

    size_t ParameterBox::GetCount() const
    {
        return (unsigned)_offsets.size();
    }

    uint64_t      ParameterBox::GetHash() const
    {
        if (!_cachedHash) {
            _cachedHash = CalculateHash();
        }
        return _cachedHash;
    }

    uint64_t      ParameterBox::GetParameterNamesHash() const
    {
        if (!_cachedParameterNameHash) {
            _cachedParameterNameHash = CalculateParameterNamesHash();
        }
        return _cachedParameterNameHash;
    }

    uint64_t      ParameterBox::CalculateFilteredHashValue(const ParameterBox& source) const
    {
        if (_values.size() > 1024) {
            assert(0);
            return 0;
        }

        uint8_t temporaryValues[1024];
        std::copy(_values.cbegin(), _values.cend(), temporaryValues);

        auto i  = _hashNames.cbegin();
        auto i2 = source._hashNames.cbegin();
        while (i < _hashNames.cend() && i2 < source._hashNames.cend()) {

            if (*i < *i2)       { ++i; } 
            else if (*i > *i2)  { ++i2; } 
            else if (*i == *i2) {
                auto offsetDest = _offsets[std::distance(_hashNames.cbegin(), i)]._valueBegin;
                auto typeDest   = _types[std::distance(_hashNames.cbegin(), i)];
                auto offsetSrc  = source._offsets[std::distance(source._hashNames.cbegin(), i2)];
                auto typeSrc    = source._types[std::distance(source._hashNames.cbegin(), i2)];
                
                if (typeDest == typeSrc) {
                    XlCopyMemory(
                        PtrAdd(temporaryValues, offsetDest), 
                        ValueTableOffset(source._values, offsetSrc._valueBegin),
                        offsetSrc._valueSize);
                } else {
                        // sometimes we get trival casting situations (like "unsigned int" to "int")
                        //  -- even in those cases, we execute the casting function, which will effect performance
                    bool castSuccess = ImpliedTyping::Cast(
                        { PtrAdd(temporaryValues, offsetDest), PtrAdd(temporaryValues, sizeof(temporaryValues)) }, typeDest,
                        { ValueTableOffset(source._values, offsetSrc._valueBegin), ValueTableOffset(source._values, offsetSrc._valueBegin+offsetSrc._valueSize) },
                        typeSrc);

                    assert(castSuccess);  // type mis-match when attempting to build filtered hash value
                    (void)castSuccess;
                }

                ++i; ++i2;
            }

        }

        return Hash64(temporaryValues, PtrAdd(temporaryValues, _values.size()));
    }

    class StringTableComparison
    {
    public:
        bool operator()(const utf8* lhs, const std::pair<const utf8*, std::string>& rhs) const 
        {
            return XlCompareString(lhs, rhs.first) < 0;
        }

        bool operator()(const std::pair<const utf8*, std::string>& lhs, const std::pair<const utf8*, std::string>& rhs) const 
        {
            return XlCompareString(lhs.first, rhs.first) < 0;
        }

        bool operator()(const std::pair<const utf8*, std::string>& lhs, const utf8* rhs) const 
        {
            return XlCompareString(lhs.first, rhs) < 0;
        }
    };

    bool ParameterBox::AreParameterNamesEqual(const ParameterBox& other) const
    {
            // return true iff both boxes have exactly the same parameter names, in the same order
        if (_names.size() != other._names.size()) {
            return false;
        }
        return GetParameterNamesHash() == other.GetParameterNamesHash();
    }

    void ParameterBox::MergeIn(const ParameterBox& source)
    {
        auto srcHashNameI = source._hashNames.cbegin();
        auto hashNameI = _hashNames.cbegin();

        for (;;) {
            if (srcHashNameI == source._hashNames.cend()) return;

            // Skip over any parameters in "this" that should come before the
            // parameter we need to merge in
            while (hashNameI < _hashNames.cend() && *hashNameI < *srcHashNameI) ++hashNameI;

            auto srcIdx = std::distance(source._hashNames.cbegin(), srcHashNameI);
            auto srcOffsets = source._offsets[srcIdx];
            hashNameI = SetParameterHint(
                hashNameI,
                *srcHashNameI,
                { PtrAdd(source._names.begin(), srcOffsets._nameBegin), PtrAdd(source._names.begin(), srcOffsets._nameBegin+srcOffsets._nameSize) },
                { ValueTableOffset(source._values, srcOffsets._valueBegin), ValueTableOffset(source._values, srcOffsets._valueBegin+srcOffsets._valueSize) },
                source._types[srcIdx], false);
            ++srcHashNameI;
            ++hashNameI;
        }
    }

    template<typename CharType>
        std::string AsString(const std::vector<CharType>& buffer, size_t len)
    {
        return Conversion::Convert<std::string>(
            std::basic_string<CharType>(AsPointer(buffer.cbegin()), AsPointer(buffer.cbegin()) + len));
    }

    template<typename CharType>
        void    ParameterBox::SerializeWithCharType(OutputStreamFormatter& stream) const
    {
        std::vector<CharType> tmpBuffer;
        std::vector<CharType> nameBuffer;

        for (auto i=_offsets.cbegin(); i!=_offsets.cend(); ++i) {
            const auto* name = &_names[i->_nameBegin];
            const void* value = ValueTableOffset(_values, i->_valueBegin);
            const auto& type = _types[std::distance(_offsets.begin(), i)];

            auto nameLen = i->_nameSize;
            nameBuffer.resize((nameLen*2)+1);     // (note; we're assuming this stl implementation won't reallocate when resizing to smaller size)
            auto finalNameLen = Conversion::Convert(
                AsPointer(nameBuffer.begin()), nameBuffer.size(),
                name, &name[nameLen]);
               
                // attributes with empty name strings will throw an exception here
            if (finalNameLen <= 0) {
                // Throw(::Exceptions::BasicLabel("Empty name string or error during name conversion"));
                nameBuffer.resize(64);
                XlUI64toA(_hashNames[std::distance(_offsets.cbegin(), i)], (char*)nameBuffer.data(), nameBuffer.size(), 16);
                finalNameLen = (unsigned)(std::find(nameBuffer.begin(), nameBuffer.end(), (utf8)'\0') - nameBuffer.begin());
            }

                // We need special cases for string types. In these cases we might have to
                // do some conversion to get the value in the format we want.
                // single char will be written as it's decimal value (unless specifically hinted as a string)
            if ((type._type == ImpliedTyping::TypeCat::Int8 || type._type == ImpliedTyping::TypeCat::UInt8) && (type._arrayCount != 1 || type._typeHint == ImpliedTyping::TypeHint::String)) {
                auto start = (const utf8*)value;
                tmpBuffer.resize((type._arrayCount*2)+1);
                auto valueLen = Conversion::Convert(
                    AsPointer(tmpBuffer.begin()), tmpBuffer.size(),
                    start, &start[type._arrayCount]);
                
                if (valueLen < 0)
                    Throw(::Exceptions::BasicLabel("Error during string conversion for member: %s", AsString(nameBuffer, finalNameLen).c_str()));

                stream.WriteKeyedValue(
                    MakeStringSection(AsPointer(nameBuffer.begin()), AsPointer(nameBuffer.begin()) + finalNameLen),
                    MakeStringSection(AsPointer(tmpBuffer.begin()), AsPointer(tmpBuffer.begin()) + valueLen));
                continue;
            }

            if ((type._type == ImpliedTyping::TypeCat::Int16 || type._type == ImpliedTyping::TypeCat::UInt16) && (type._arrayCount != 1 || type._typeHint == ImpliedTyping::TypeHint::String)) {
                auto start = (const utf16*)value;
                tmpBuffer.resize((type._arrayCount*2)+1);
                auto valueLen = Conversion::Convert(
                    AsPointer(tmpBuffer.begin()), tmpBuffer.size(),
                    start, &start[type._arrayCount]);
                
                if (valueLen < 0)
                    Throw(::Exceptions::BasicLabel("Error during string conversion for member: %s", AsString(nameBuffer, finalNameLen).c_str()));

                stream.WriteKeyedValue(
                    MakeStringSection(AsPointer(nameBuffer.begin()), AsPointer(nameBuffer.begin()) + finalNameLen),
                    MakeStringSection(AsPointer(tmpBuffer.begin()), AsPointer(tmpBuffer.begin()) + valueLen));
                continue;
            }

            auto stringFormat = ImpliedTyping::AsString(MakeStringSection((const CharType*)value, (const CharType*)AsPointer(_values.end())), type, true);
            auto convertedString = Conversion::Convert<std::basic_string<CharType>>(stringFormat);
            stream.WriteKeyedValue(
                MakeStringSection(AsPointer(nameBuffer.begin()), AsPointer(nameBuffer.begin()) + finalNameLen),
                convertedString);
        }
    }

    ParameterBox::ParameterBox()
    {
        _cachedHash = _cachedParameterNameHash = 0;
    }

    ParameterBox::ParameterBox(
        std::initializer_list<std::pair<const utf8*, const char*>> init)
    {
        for (auto i=init.begin(); i!=init.end(); ++i) {
            SetParameter(i->first, i->second);
        }
    }

    template<typename CharType>
        ParameterBox::ParameterBox(
            InputStreamFormatter<CharType>& stream, 
            IteratorRange<const void*> defaultValue, const ImpliedTyping::TypeDesc& defaultValueType)
    {
        using namespace ImpliedTyping;

            // note -- fixed size buffer here bottlenecks max size for native representations
            // of these values
        uint8_t nativeTypeBuffer[NativeRepMaxSize];
        std::vector<utf8> nameBuffer;
        std::vector<char> valueBuffer;

            // attempt to read attributes from a serialized text file
            // as soon as we hit something that is not another attribute
            // (it could be a sub-element, or the end of this element)
            // then we will stop reading and return
        while (stream.PeekNext() == InputStreamFormatter<CharType>::Blob::KeyedItem) {
            typename InputStreamFormatter<CharType>::InteriorSection name, value;
            bool success = stream.TryKeyedItem(name) && stream.TryStringValue(value);
            if (!success)
                Throw(::Exceptions::BasicLabel("Parsing exception while reading attribute in parameter box deserialization"));

            auto nameLen = (size_t(name._end) - size_t(name._start)) / sizeof(CharType);
			{
                nameBuffer.resize(nameLen*2+1);
                
                auto nameConvResult = Conversion::Convert(
                    AsPointer(nameBuffer.begin()), nameBuffer.size(),
                    name._start, name._end);

                if (nameConvResult <= 0)
                    Throw(::Exceptions::BasicLabel("Empty name or error converting string name in parameter box deserialization"));

				nameLen = std::min(nameBuffer.size()-1, (size_t)nameConvResult);
                nameBuffer[nameLen] = '\0';
            }

            if (!value._start || !value._end) {
                    // if there is no value attached, we default to the value given us
                    // (usually jsut void)
                SetParameter(AsPointer(nameBuffer.cbegin()), defaultValue, defaultValueType);
                continue;
            }

            TypeDesc nativeType{TypeCat::Void};
            if (constant_expression<sizeof(CharType) == sizeof(utf8)>::result()) {

                nativeType = ParseFullMatch(
                    MakeStringSection((const char*)value._start, (const char*)value._end),
                    MakeIteratorRange(nativeTypeBuffer));

            } else {

                valueBuffer.resize((value._end - value._start)*2+1);
                auto valueLen = Conversion::Convert(
                    AsPointer(valueBuffer.begin()), valueBuffer.size(),
                    value._start, value._end);

                // a failed conversion here is valid, but it means we must treat the value as a string
                if (valueLen>=0) {
                    nativeType = ParseFullMatch(
                        MakeStringSection(AsPointer(valueBuffer.begin()), AsPointer(valueBuffer.begin()) + valueLen),
                        MakeIteratorRange(nativeTypeBuffer));
                }

            }

            if (nativeType._type != TypeCat::Void) {
                SetParameter(
					MakeStringSection(AsPointer(nameBuffer.cbegin()), PtrAdd(AsPointer(nameBuffer.cbegin()), nameLen)), 
					MakeIteratorRange(nativeTypeBuffer, PtrAdd(nativeTypeBuffer, nativeType.GetSize())), 
					nativeType);
            } else {
                    // this is just a string. We should store it as a string, in whatever character set it came in
                SetParameter(
                    MakeStringSection(AsPointer(nameBuffer.cbegin()), PtrAdd(AsPointer(nameBuffer.cbegin()), nameLen)),
                    MakeIteratorRange(value.begin(), value.end()), 
                    TypeDesc{TypeOf<CharType>()._type, uint16_t(value._end - value._start), TypeHint::String});
            }
        }
    }

    ParameterBox::ParameterBox(ParameterBox&& moveFrom) never_throws
    : _hashNames(std::move(moveFrom._hashNames))
    , _offsets(std::move(moveFrom._offsets))
    , _names(std::move(moveFrom._names))
    , _values(std::move(moveFrom._values))
    , _types(std::move(moveFrom._types))
    {
        _cachedHash = moveFrom._cachedHash;
        _cachedParameterNameHash = moveFrom._cachedParameterNameHash;
    }
        
    ParameterBox& ParameterBox::operator=(ParameterBox&& moveFrom) never_throws
    {
        _hashNames = std::move(moveFrom._hashNames);
        _offsets = std::move(moveFrom._offsets);
        _names = std::move(moveFrom._names);
        _values = std::move(moveFrom._values);
        _types = std::move(moveFrom._types);
        _cachedHash = moveFrom._cachedHash;
        _cachedParameterNameHash = moveFrom._cachedParameterNameHash;
        return *this;
    }

    ParameterBox::~ParameterBox()
    {
    }

    template void ParameterBox::SerializeWithCharType<utf8>(OutputStreamFormatter& stream) const;
    template ParameterBox::ParameterBox(InputStreamFormatter<utf8>& stream, IteratorRange<const void*>, const ImpliedTyping::TypeDesc&);

///////////////////////////////////////////////////////////////////////////////////////////////////

    void BuildStringTable(StringTable& defines, const ParameterBox& box)
    {
        for (const auto&i:box) {
            const auto name = i.Name();
            auto value = i.RawValue();
            const auto& type = i.Type();
            auto stringFormat = ImpliedTyping::AsString(value, type);

            auto insertPosition = std::lower_bound(
                defines.begin(), defines.end(), name.begin(), StringTableComparison());
            if (insertPosition!=defines.cend() && !XlCompareString(insertPosition->first, name)) {
                insertPosition->second = stringFormat;
            } else {
                defines.insert(insertPosition, std::make_pair(name.begin(), stringFormat));
            }
        }
    }

    void OverrideStringTable(StringTable& defines, const ParameterBox& box)
    {
        for (const auto&i:box) {
            const auto name = i.Name();
            auto value = i.RawValue();
            const auto& type = i.Type();

            auto insertPosition = std::lower_bound(
                defines.begin(), defines.end(), name.begin(), StringTableComparison());

            if (insertPosition!=defines.cend() && !XlCompareString(insertPosition->first, name)) {
                insertPosition->second = ImpliedTyping::AsString(value, type);
            }
        }
    }

    std::string FlattenStringTable(const StringTable& stringTable)
    {
        std::string combinedStrings;
        
            // Calculate size of the concatenated string first, so we can avoid allocations during the 
            // concatenation process.
        size_t size = 0;
        std::for_each(stringTable.cbegin(), stringTable.cend(), 
            [&size](const std::pair<const utf8*, std::string>& object) { size += 2 + XlStringSize(object.first) + object.second.size(); });
        combinedStrings.reserve(size+1);

        std::for_each(stringTable.cbegin(), stringTable.cend(), 
            [&combinedStrings](const std::pair<const utf8*, std::string>& object) 
            {
                combinedStrings.insert(combinedStrings.end(), (const char*)object.first, (const char*)XlStringEnd(object.first));
                combinedStrings.push_back('=');
                combinedStrings.insert(combinedStrings.end(), object.second.cbegin(), object.second.cend()); 
                combinedStrings.push_back(';');
            });
        return combinedStrings;
    }

	std::string BuildFlatStringTable(const ParameterBox& box)
	{
		std::stringstream str;
		bool first = true;
		for (const auto&i:box) {
			if (!first) str << ";"; first = false;
			str << i.Name() << '=' << i.ValueAsString();
		}
		return str.str();
	}

    IteratorRange<const void*> ParameterBox::Iterator::Value::RawValue() const
    {
		const auto& offsets = _box->_offsets[_index];
		return {ValueTableOffset(_box->_values, offsets._valueBegin), ValueTableOffset(_box->_values, offsets._valueBegin + offsets._valueSize)};
    }

    void SerializationOperator(std::ostream& serializer, const ParameterBox& box)
    {
        serializer << BuildFlatStringTable(box) << std::endl;
    }
}



