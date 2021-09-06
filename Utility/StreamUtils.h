// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IteratorUtils.h"
#include "../Core/Exceptions.h"
#include <iomanip>
#include <ostream>

namespace Utility
{
    class ByteCount
    {
    public:
        explicit ByteCount(size_t size) : _size(size) {}
        size_t _size;

        friend inline std::ostream& operator<<(std::ostream& stream, const ByteCount& byteCount)
        {
            auto originalFlags = stream.flags();
            auto originalPrecision = stream.precision();
            auto s = byteCount._size;
            if (s > 512 * 1024 * 1024)  stream << std::setprecision(2) << std::fixed << s / float(1024 * 1024 * 1024) << " GiB";
            else if (s > 512 * 1024)    stream << std::setprecision(2) << std::fixed << s / float(1024 * 1024) << " MiB";
            else if (s > 512)           stream << std::setprecision(2) << std::fixed << s / float(1024) << " KiB";
            else                        stream << s << " B";
            stream.flags(originalFlags);
            stream.precision(originalPrecision);
            return stream;
        }
    };

    class ByteData
    {
    public:
        explicit ByteData(IteratorRange<const void*> data) : _data(data) {}
        IteratorRange<const void*> _data;

        friend inline std::ostream& operator<<(std::ostream& stream, const ByteData& byteData)
        {
            stream << "Binary data (" << ByteCount(byteData._data.size()) << ") follows" << std::endl;
            unsigned count = 0;
            for (auto byte:byteData._data.Cast<const uint8_t*>()) {
                if ((count % 32) == 0 && count != 0) stream << std::endl;
                else if (count != 0) stream << ' ';
                ++count;
                stream << std::hex << std::setw(2) << std::setfill('0') << (unsigned)byte;
            }
            stream << std::dec;
            return stream;
        }
    };

    class StreamIndent
    {
    public:
        explicit StreamIndent(unsigned spaceCount, char filler = ' ') : _spaceCount(spaceCount), _filler(filler) {}
        unsigned _spaceCount;
		char _filler;

        friend inline std::ostream& operator<<(std::ostream& stream, const StreamIndent& indent)
        {
            char buffer[128];
			unsigned total = indent._spaceCount;
			while (total) {
				unsigned cnt = std::min((unsigned)dimof(buffer), total);
				for (unsigned c=0; c<cnt; ++c) buffer[c] = indent._filler;
				stream.write(buffer, cnt);
				total -= cnt;
			}
            return stream;
        }
    };

    template<typename T>
        std::ostream& operator<<(std::ostream& oss, IteratorRange<const T*> v)
    {
        oss << "[";
        bool first = true;
        for (auto& item : v) {
            if (!first) {
                oss << ", ";
            }
            oss << item;
            first = false;
        }
        oss << "]";
        return oss;
    }
}

// Use namespace Exceptions and std instead of Utility here,
// because we should use the namespace which the rhs of the stream operator is in.
namespace Exceptions
{
    template<typename CharType, typename CharTraits>
    std::basic_ostream<CharType, CharTraits> &operator<<(
            std::basic_ostream<CharType, CharTraits> &stream,
            const ::Exceptions::BasicLabel &exception)
    {
        stream << exception.what();
        return stream;
    }
}

namespace std
{
    template<typename CharType, typename CharTraits>
    std::basic_ostream<CharType, CharTraits> &operator<<(
            std::basic_ostream<CharType, CharTraits> &stream,
            const std::exception &exception)
    {
        stream << exception.what();
        return stream;
    }

    template<typename CharType, typename CharTraits>
    std::basic_ostream<CharType, CharTraits> &operator<<(
            std::basic_ostream<CharType, CharTraits> &stream,
            const std::exception_ptr &exception_ptr)
    {
        TRY {
            std::rethrow_exception(exception_ptr);
        } CATCH (const std::exception &e) {
            stream << e.what();
        } CATCH (const std::string &e) {
            stream << e;
        } CATCH (const char *e) {
            stream << e;
        } CATCH (...) {
            stream << "Unknown Exception";
        } CATCH_END
        return stream;
    }
}

using namespace Utility;
