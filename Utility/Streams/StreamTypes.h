// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Core/Types.h"
#include "Stream.h"
#include <streambuf>
#include <sstream>

namespace Utility
{

    // --------------------------------------------------------------------------
    // Memory Output
    // --------------------------------------------------------------------------

    template<typename BufferType>
        class XL_UTILITY_API StreamBuf : public OutputStream 
    {
    public:
        virtual size_type Tell();
        virtual void Write(const void* p, size_type len);
        virtual void WriteChar(char ch);
        virtual void Write(StringSection<utf8>);

        virtual void Flush();

    private:
        template <typename C> static unsigned IsFullTest(decltype(&C::IsFull)*);
        template <typename C> static char IsFullTest(...);

    public:

            //  If the "BufferType" type has a method called str(), then we
            //  should have an AsString() method that calls str(). 
            //  if str() is missing, then AsString() is also missing.
            //  (likewise for IsFull)
        
        template<
            typename Buffer = BufferType,
            typename Result = decltype(((Buffer*)nullptr)->str())>
        auto AsString() const -> Result { return _buffer.str(); }

        bool IsFull() const
        {
            if constexpr (sizeof(decltype(IsFullTest<BufferType>(0))) > 1) {
                return _buffer.IsFull();
            } else {
                return false;
            }
        }

        const BufferType& GetBuffer() const { return _buffer; }

        using CharType = typename BufferType::char_type;

        StreamBuf();
        ~StreamBuf();

        template<typename std::enable_if<std::is_constructible<BufferType, CharType*, size_t>::value>::type* = nullptr>
            StreamBuf(CharType* buffer, size_t bufferCharCount)
            : _buffer(buffer, bufferCharCount) {}

		StreamBuf(StreamBuf&& moveFrom) : _buffer(std::move(moveFrom._buffer)) {}
		StreamBuf& operator=(StreamBuf&& moveFrom) { _buffer = std::move(moveFrom._buffer); return *this; }

    protected:
        BufferType _buffer;
    };

    namespace Internal
    {
        template<typename CharType>
            struct FixedMemoryBuffer2 : public std::basic_streambuf<CharType>
        {
            typedef typename std::basic_streambuf<CharType>::char_type char_type;

            bool IsFull() const { return this->pptr() >= this->epptr(); }
            unsigned Length() const { return unsigned(this->pptr() - this->pbase()); }

            FixedMemoryBuffer2(CharType* buffer, size_t bufferCharCount)
            {
                this->setp(buffer, &buffer[bufferCharCount-1]);
                for (unsigned c=0; c<bufferCharCount; ++c) buffer[c] = 0;
            }
            FixedMemoryBuffer2() {}
            ~FixedMemoryBuffer2() {}

			FixedMemoryBuffer2(FixedMemoryBuffer2&& moveFrom) : std::basic_streambuf<CharType>(std::move(moveFrom)) {}
			FixedMemoryBuffer2& operator=(FixedMemoryBuffer2&& moveFrom) { std::basic_streambuf<CharType>::operator=(std::move(moveFrom)); return *this; }
        };

        template<typename CharType>
            struct ResizeableMemoryBuffer : public std::basic_stringbuf<CharType>
        {
            typedef typename std::basic_stringbuf<CharType>::char_type char_type;

            CharType* Begin() const { return this->pbase(); }
            CharType* End() const   { return this->pptr(); }

            ResizeableMemoryBuffer() {}
            ~ResizeableMemoryBuffer() {}
            
            ResizeableMemoryBuffer(CharType* buffer, size_t bufferCharCount)
            {
                assert(0);
            }

			ResizeableMemoryBuffer(ResizeableMemoryBuffer&& moveFrom) : std::basic_stringbuf<CharType>(std::move(moveFrom)) {}
			ResizeableMemoryBuffer& operator=(ResizeableMemoryBuffer&& moveFrom) { std::basic_stringbuf<CharType>::operator=(std::move(moveFrom)); return *this; }
        };
    }

    template<typename CharType = char>
        using MemoryOutputStream = StreamBuf<Internal::ResizeableMemoryBuffer<CharType>>;

    template<typename CharType = char>
        using FixedMemoryOutputStream = StreamBuf<Internal::FixedMemoryBuffer2<CharType>>;
}

using namespace Utility;
