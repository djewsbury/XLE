// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Console.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/StringFormat.h"
#include <iostream>
#include <streambuf>

#if PLATFORMOS_ACTIVE == PLATFORMOS_WINDOWS && defined(_DEBUG)
	extern "C" dll_import void __stdcall OutputDebugStringA(const char lpOutputString[]);
	extern "C" dll_import void __stdcall OutputDebugStringW(const wchar_t lpOutputString[]);
#endif

namespace ConsoleRig
{

		////    B U F F E R E D   O U T P U T   S T R E A M   ////

	template<void Sync(StringSection<>)>
		class BufferedStreamBuf : public std::basic_streambuf<char>
	{
	public:
		char_type _buffer[4096];

		BufferedStreamBuf()
		{
			setp(_buffer, ArrayEnd(_buffer)-1);		// note negative one to guarantee there's always space for a null terminator
		}

	protected:
		virtual int_type overflow(int_type ch) override
		{
			if (ch == traits_type::eof()) {
				return sync() == 0 ? traits_type::not_eof(ch) : traits_type::eof();
			}

			if (pptr() == epptr()) {
				if (sync() != 0) return traits_type::eof();
			}

			*pptr() = traits_type::to_char_type(ch);
			pbump(1); // Advance put pointer
			return traits_type::not_eof(ch);
		}

		std::streamsize xsputn(const char* s, std::streamsize count) override
		{
			std::streamsize finalCount = 0;
			while (finalCount < count) {
				auto remaining = epptr() - pptr();
				if (!remaining) { 
					if (sync() != 0) return finalCount;
					remaining = epptr() - pptr();
				}

				auto copy = std::min(remaining, count-finalCount);
				std::memcpy(pptr(), s+finalCount, copy);
				pbump(copy);
				finalCount += copy;
			}
			return count;
		}

		int sync() override
		{
			Sync(MakeStringSection(pbase(), pptr()));
			setp(pbase(), epptr()); 
			return 0; // Or -1 on failure
		}
	};

			////    C O N S O L E   O U T P U T   S T R E A M   ////

	static void BufferedStreamBuf_ToConsole(StringSection<> str)
	{
		Console::GetInstance().Print(str);
	}

	#if PLATFORMOS_ACTIVE == PLATFORMOS_WINDOWS && defined(_DEBUG)

			////    D E B U G   C O N S O L E   O U T P U T   ////

		static void BufferedStreamBuf_ToDebugger(StringSection<> str)
		{
			if (str.IsEmpty()) return;

			// NOTE -- assuming we're coming from BufferedStreamBuf, where we will always have space to write a null terminator
			auto t = *str._end; *(char*)str._end = 0;
			OutputDebugStringA(str._start);
			*(char*)str._end = t;
		}

		std::ostream* GetSharedDebuggerWarningStream()
		{
			static BufferedStreamBuf<BufferedStreamBuf_ToDebugger> buffer;
			static std::ostream result(&buffer);
			return &result;
		}

		std::ostream& GetWarningStream()
		{
			static BufferedStreamBuf<BufferedStreamBuf_ToConsole> buffer;
			static std::ostream result(&buffer);
			return result;
		}

		void xleWarning(const char format[], va_list args)
		{
			auto& strm = GetWarningStream();
			strm << "{Color:ff7f7f}";
			PrintFormatV(&strm, format, args);
			strm.flush();
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

	#else
		std::ostream* GetWarningStream() { return nullptr; }
		void xleWarning(const char format[], va_list args) {}
		void xleWarning(const char format[], ...) {}
		void DebuggerOnlyWarning(const char format[], ...) {}
	#endif

	

}
