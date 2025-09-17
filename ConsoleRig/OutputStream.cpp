// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OutputStream.h"
#include "Console.h"
#include "../Utility/Streams/Stream.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/StringFormat.h"
#include "../Utility/Conversion.h"
#include "../Core/Prefix.h"
#include <assert.h>
#include <algorithm>

#if PLATFORMOS_ACTIVE == PLATFORMOS_WINDOWS && defined(_DEBUG)
    extern "C" dll_import void __stdcall OutputDebugStringA(const char lpOutputString[]);
    extern "C" dll_import void __stdcall OutputDebugStringW(const wchar_t lpOutputString[]);
#endif

namespace ConsoleRig
{

        ////    B U F F E R E D   O U T P U T   S T R E A M   ////

    class BufferedOutputStream : public Utility::OutputStream
    {
    public:
        virtual size_type   Tell();
        virtual void        Write(const void* p, size_type len);
        virtual void        WriteChar(char ch);
        virtual void        Write(StringSection<utf8>);

        virtual void        Flush();

        BufferedOutputStream(std::shared_ptr<Utility::OutputStream> nextStream);
        virtual ~BufferedOutputStream();

    private:
        uint8_t     _buffer[4096];
        size_type   _bufferPosition;
        std::shared_ptr<Utility::OutputStream>      _nextStream;
    };

    auto   BufferedOutputStream::Tell() -> size_type                   { return ~size_type(0x0); }

    void   BufferedOutputStream::Write(const void* p, size_type len)
    {
        if ((_bufferPosition + len) <= dimof(_buffer)) {

            XlCopyMemory(PtrAdd(_buffer, _bufferPosition), p, len);
            _bufferPosition += len;

        } else if (len >= dimof(_buffer)) {

                //      Very long input. Just flush the buffer and send this down to
                //      the next stream
            Flush();
            if (_nextStream) {
                _nextStream->Write(p, len);
            }

        } else {

                //      Copy as much as possible, flush and then 
                //      restart the buffer
            auto firstCopyLength = dimof(_buffer) - _bufferPosition;
            if (firstCopyLength) {
                XlCopyMemory(PtrAdd(_buffer, _bufferPosition), p, firstCopyLength);
                _bufferPosition += firstCopyLength;
            }
            Flush();

            auto secondCopyLength = len-firstCopyLength;
            if (secondCopyLength) {
                XlCopyMemory(_buffer, PtrAdd(p, firstCopyLength), secondCopyLength);
            }
            _bufferPosition = len-firstCopyLength;

        }
    }

    void    BufferedOutputStream::WriteChar(char ch)             { Write(&ch, sizeof(ch)); }
    void    BufferedOutputStream::Write(StringSection<utf8> str)     { Write(str.begin(), str.Length()); }

    void    BufferedOutputStream::Flush()
    {
        if (_bufferPosition && _nextStream) {
            _nextStream->Write(_buffer, _bufferPosition);
        }
        _bufferPosition = 0;
    }

    BufferedOutputStream::BufferedOutputStream(std::shared_ptr<Utility::OutputStream> nextStream)
    :   _bufferPosition(0)
    ,   _nextStream(std::move(nextStream))
    {
    }

    BufferedOutputStream::~BufferedOutputStream()
    {
        Flush();
    }

        ////    F O R K   O U T P U T   S T R E A M   ////

    class ForkOutputStream : public Utility::OutputStream
    {
    public:
        virtual size_type   Tell();
        virtual void        Write(const void* p, size_type len);
        virtual void        WriteChar(char ch);
        virtual void        Write(StringSection<utf8>);

        virtual void        Flush();

        ForkOutputStream(std::shared_ptr<Utility::OutputStream> first, std::shared_ptr<Utility::OutputStream> second);
        virtual ~ForkOutputStream();

    private:
        std::shared_ptr<Utility::OutputStream>      _first;
        std::shared_ptr<Utility::OutputStream>      _second;
    };

    auto    ForkOutputStream::Tell() -> size_type       { return _first->Tell(); }

    void    ForkOutputStream::Write(const void* p, size_type len)
    {
        _first->Write(p, len);
        _second->Write(p, len);
    }

    void    ForkOutputStream::WriteChar(char ch)             { _first->Write(&ch, sizeof(ch)); _second->Write(&ch, sizeof(ch)); }
    void    ForkOutputStream::Write(StringSection<utf8> s)   { _first->Write(s.begin(), s.Length()); _second->Write(s.begin(), s.Length()); }

    void    ForkOutputStream::Flush()
    {
        _first->Flush();
        _second->Flush();
    }

    ForkOutputStream::ForkOutputStream(std::shared_ptr<Utility::OutputStream> first, std::shared_ptr<Utility::OutputStream> second)
    :       _first(first)
    ,       _second(second)
    {}

    ForkOutputStream::~ForkOutputStream()
    {
    }

            ////    C O N S O L E   O U T P U T   S T R E A M   ////

    class ConsoleOutputStream : public Utility::OutputStream
    {
    public:
        virtual size_type   Tell();
        virtual void        Write(const void* p, size_type len);
        virtual void    WriteChar(char ch);
        virtual void    Write(StringSection<utf8>);

        virtual void    Flush();

        ConsoleOutputStream();
        virtual ~ConsoleOutputStream();
    };

    auto ConsoleOutputStream::Tell() -> size_type   { return ~size_type(0x0); }

    void   ConsoleOutputStream::Write(const void* p, size_type len)
    {
        Console::GetInstance().Print(std::string((char*)p, len));
    }

        //      Character type conversions not handled correctly!
    void    ConsoleOutputStream::WriteChar(char ch)             { Console::GetInstance().Print(std::string((char*)&ch, 1)); }
    void    ConsoleOutputStream::Write(StringSection<utf8> str) { Console::GetInstance().Print((const char*)str.begin(), (const char*)str.end()); }

    void    ConsoleOutputStream::Flush(){}

    ConsoleOutputStream::ConsoleOutputStream()
    {}

    ConsoleOutputStream::~ConsoleOutputStream()
    {}


    #if PLATFORMOS_ACTIVE == PLATFORMOS_WINDOWS && defined(_DEBUG)

            ////    D E B U G   C O N S O L E   O U T P U T   ////

        class DebuggerConsoleOutput : public Utility::OutputStream
        {
        public:
            virtual size_type   Tell();
            virtual void        Write(const void* p, size_type len);
            virtual void    WriteChar(char ch);
            virtual void    Write(StringSection<utf8>);

            virtual void    Flush();

            DebuggerConsoleOutput();
            virtual ~DebuggerConsoleOutput();
        };

        auto DebuggerConsoleOutput::Tell() -> size_type { return ~size_type(0x0); }

        void   DebuggerConsoleOutput::Write(const void* p, size_type len)
        {
                //
                //              Input is treated as UTF-8. If we're using BufferedOutputStream, we may
                //              have forgotten the input type on the way through...
                //
            uint8_t buffer[4096];
            while (len) {

                    //
                    //          Have to append the null terminator!
                    //
                size_t copyAmount = std::min(dimof(buffer) - 1, size_t(len));
                XlCopyMemory(buffer, p, copyAmount);
                buffer[copyAmount]      = '\0';

                OutputDebugStringA((const char*)buffer);

                p = PtrAdd(p, copyAmount);
                len -= (int)copyAmount;
            }
        }

        void    DebuggerConsoleOutput::WriteChar(char ch)
        {
            char buffer[2];
            buffer[0] = ch;
            buffer[1] = '\0';
            OutputDebugStringA(buffer);
        }
        void    DebuggerConsoleOutput::Write(StringSection<utf8> str)
        {
                // some extra overhead required to add the null terminator (because some input strings won't have it!)
            OutputDebugStringA(
                Conversion::Convert<std::string>(str.AsString()).c_str());
        }

        void    DebuggerConsoleOutput::Flush()          {}
        DebuggerConsoleOutput::DebuggerConsoleOutput()  {}
        DebuggerConsoleOutput::~DebuggerConsoleOutput() {}

        std::shared_ptr<Utility::OutputStream>      GetSharedDebuggerWarningStream()
        {
            #if PLATFORMOS_ACTIVE == PLATFORMOS_WINDOWS && defined(_DEBUG)
                static auto result = std::make_shared<BufferedOutputStream>(std::make_shared<DebuggerConsoleOutput>());
                return result;
            #else
                return nullptr;
            #endif
        }

        Utility::OutputStream&      GetWarningStream()
        {
            static ForkOutputStream stream(
                GetSharedDebuggerWarningStream(),
                std::make_shared<ConsoleOutputStream>());
            return stream;
        }

        static Utility::OutputStream&      GetDebuggerWarningStream()
        {
            return *GetSharedDebuggerWarningStream();
        }

        void xleWarning(const char format[], va_list args)
        {
            ConsoleRig::GetWarningStream().Write((utf8*)"{Color:ff7f7f}");
            PrintFormatV(&ConsoleRig::GetWarningStream(), format, args);
            ConsoleRig::GetDebuggerWarningStream().Flush();
        }

        void xleWarning(const char format[], ...)
        {
            va_list args;
            va_start(args, format);
            xleWarning(format, args);
            va_end(args);
        }

        #if defined(_DEBUG)
            void xleWarningDebugOnly(const char format[], ...)
            {
                va_list args;
                va_start(args, format);
                xleWarning(format, args);
                va_end(args);
            }
        #endif


        void DebuggerOnlyWarning(const char format[], ...)
        {
            va_list args;
            va_start(args, format);
            PrintFormatV(&ConsoleRig::GetDebuggerWarningStream(), format, args);
            va_end(args);
        }

    #else
        Utility::OutputStream&      GetWarningStream()
        {
            static auto stream = std::make_shared<ConsoleOutputStream>();
            return *stream;
        }

        void xleWarning(const char format[], va_list args) {}
        void xleWarning(const char format[], ...) {}
        void DebuggerOnlyWarning(const char format[], ...) {}
    #endif

    

}
