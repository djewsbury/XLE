// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../OSServices/Log.h"
#include "OutputStream.h"
#include "GlobalServices.h"
#include "AttachablePtr.h"
#include "../Utility/MemoryUtils.h"
#include "../OSServices/RawFS.h"
#include "../Utility/Streams/Stream.h"
#include "../Core/Exceptions.h"

#include <iostream>

#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS && !defined(__MINGW32__)
    #include "../OSServices/WinAPI/IncludeWindows.h"
    #include "../Foreign/StackWalker/StackWalker.h"
#endif

    // We can't use the default initialisation method for easylogging++
    // because is causes a "LoaderLock" exception when used with C++/CLI dlls.
    // It also doesn't work well when sharing a single log file across dlls.
    // Anyway, the default behaviour isn't great for our needs.
    // So, we need to use "INITIALIZE_NULL" here, and manually construct
    // a "GlobalStorage" object below...

#if defined(_DEBUG)
    #define REDIRECT_COUT
#endif

#pragma warning(disable:4592)

//////////////////////////////////

#if defined(REDIRECT_COUT)
    static auto Fn_CoutRedirectModule = ConstHash64Legacy<'cout', 'redi', 'rect'>::Value;
    static auto Fn_RedirectCout = ConstHash64Legacy<'redi', 'rect', 'cout'>::Value;
#endif

namespace ConsoleRig
{
    #if defined(REDIRECT_COUT)
        template <typename CharType>
            class StdCToXLEStreamAdapter : public std::basic_streambuf<CharType>
        {
        public:
            void Reset(std::shared_ptr<Utility::OutputStream> chain) { _chain = chain; }
            StdCToXLEStreamAdapter(std::shared_ptr<Utility::OutputStream> chain);
            ~StdCToXLEStreamAdapter();
        protected:
            std::shared_ptr<Utility::OutputStream> _chain;

            virtual std::streamsize xsputn(const CharType* s, std::streamsize count);
            virtual int sync();
			using StreamIntType = typename std::basic_streambuf<CharType>::int_type;
			virtual StreamIntType overflow(StreamIntType ch);
        };

        template <typename CharType>
            StdCToXLEStreamAdapter<CharType>::StdCToXLEStreamAdapter(std::shared_ptr<Utility::OutputStream> chain) : _chain(chain) {}
        template <typename CharType>
            StdCToXLEStreamAdapter<CharType>::~StdCToXLEStreamAdapter() {}

        template <typename CharType>
            std::streamsize StdCToXLEStreamAdapter<CharType>::xsputn(const CharType* s, std::streamsize count)
        {
            assert(_chain);
            _chain->Write(s, int(sizeof(CharType) * count));
            return count;
        }

		template <typename CharType>
			auto StdCToXLEStreamAdapter<CharType>::overflow(StreamIntType ch) -> StreamIntType
		{
			// For some reason, std::endl always invokes "overflow" instead of "xsputn"
			using Traits = std::char_traits<CharType>;
			if (!Traits::eq_int_type(ch, Traits::eof()))
				_chain->Write(&ch, sizeof(CharType));
			// any value other than Traits::eof() is considered a non-error return; but what
			// value can be guarantee is not Traits::eof()?
			return std::basic_streambuf<CharType>::overflow(ch);
		}

        template <typename CharType>
            int StdCToXLEStreamAdapter<CharType>::sync()
        {
            _chain->Flush();
            return 0;
        }

        std::shared_ptr<Utility::OutputStream>      GetSharedDebuggerWarningStream();

        static StdCToXLEStreamAdapter<char> s_coutAdapter(nullptr);
        static std::basic_streambuf<char>* s_oldCoutStreamBuf = nullptr;
    #endif

    static void SendExceptionToLogger(const ::Exceptions::CustomReportableException&);

    void DebugUtil_Startup()
    {
            // It can be handy to redirect std::cout to the debugger output
            // window in Visual Studio (etc)
            // We can do this with an adapter to connect out DebufferWarningStream
            // object to a c++ std::stream_buf
        #if defined(REDIRECT_COUT)

            auto currentModule = OSServices::GetCurrentModuleId();
            auto& serv = CrossModule::GetInstance()._services;
            
            bool doRedirect = serv.Call<bool>(Fn_RedirectCout);
            if (doRedirect && !serv.Has<OSServices::ModuleId()>(Fn_CoutRedirectModule)) {
                auto redirect = GetSharedDebuggerWarningStream();
                if (redirect) {
                    s_coutAdapter.Reset(GetSharedDebuggerWarningStream());
                    s_oldCoutStreamBuf = std::cout.rdbuf();
                    std::cout.rdbuf(&s_coutAdapter);

                    serv.Add(Fn_CoutRedirectModule, [=](){ return currentModule; });
                }
            }

        #endif

            //
            //  Check to see if there is an existing logging object in the
            //  global services. If there is, it will have been created by
            //  another module.
            //  If it's there, we can just re-use it. Otherwise we need to
            //  create a new one and set it up...
            //
		#if FEATURE_EXCEPTIONS
			auto& onThrow = GlobalOnThrowCallback();
			if (!onThrow)
				onThrow = &SendExceptionToLogger;
		#endif
    }

    void DebugUtil_Shutdown()
    {
        #if defined(REDIRECT_COUT)
            auto& serv = CrossModule::GetInstance()._services;
            auto currentModule = OSServices::GetCurrentModuleId();

            OSServices::ModuleId testModule = 0;
            if (serv.TryCall<OSServices::ModuleId>(testModule, Fn_CoutRedirectModule) && (testModule == currentModule)) {
                if (s_oldCoutStreamBuf)
                    std::cout.rdbuf(s_oldCoutStreamBuf);
                serv.Remove(Fn_CoutRedirectModule);
            }
        #endif
    }

    #if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS && !defined(__MINGW32__)
        class StackWalkerToLog : public StackWalker
        {
        protected:
            virtual void OnOutput(LPCSTR) {}

            void OnCallstackEntry(CallstackEntryType eType, int frameNumber, CallstackEntry &entry)
            {
                    // We should normally have 3 entries on the callstack ahead of what we want:
                    //  StackWalker::ShowCallstack
                    //  ConsoleRig::SendExceptionToLogger
                    //  Utility::Throw
                if ((frameNumber >= 3) && (eType != lastEntry) && (entry.offset != 0)) {
                    if (entry.lineFileName[0] == 0) {
                        Log(Error) 
                            << std::hex << entry.offset << std::dec
                            << " (" << entry.moduleName << "): "
                            << entry.name
							<< std::endl;
                    } else {
                        Log(Error)
                            << entry.lineFileName << " (" << entry.lineNumber << "): "
                            << ((entry.undFullName[0] != 0) ? entry.undFullName : ((entry.undName[0] != 0) ? entry.undName : entry.name))
							<< std::endl;
                    }
                }
            }
        };
    #endif

    static void SendExceptionToLogger(const ::Exceptions::CustomReportableException& e)
    {
        TRY
        {
            if (!e.CustomReport()) {
                #if FEATURE_RTTI
                    Log(Error) << "Throwing Exception -- " << typeid(e).name() << ". Extra information follows:" << std::endl;
                #else
                    Log(Error) << "Throwing Exception. Extra information follows:" << std::endl;
                #endif
                Log(Error) << e.what() << std::endl;

                    // report this exception to the logger (including callstack information)
                #if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS && !defined(__MINGW32__)
                    static StackWalkerToLog walker;
                    walker.ShowCallstack(7);
                #endif
            }
        } CATCH (...) {
            // Encountering another exception at this point would be trouble.
            // We have to suppress any exception that happen during reporting,
            // and allow the exception, 'e' to be handled
        } CATCH_END
    }
}

