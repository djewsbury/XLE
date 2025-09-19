// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GlobalServices.h"
#include "AttachablePtr.h"
#include "../Utility/MemoryUtils.h"
#include "../OSServices/RawFS.h"
#include "../Core/Exceptions.h"

#include <iostream>

#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS && !defined(__MINGW32__) && XLE_STACK_WALKER_ENABLE
    #include "../OSServices/WinAPI/IncludeWindows.h"
    #include "../Foreign/StackWalker/StackWalker.h"
#endif

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
        std::ostream* GetSharedDebuggerWarningStream();
        static std::ostream* s_coutAdapter;
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
                s_coutAdapter = GetSharedDebuggerWarningStream();
                if (s_coutAdapter) {
                    s_oldCoutStreamBuf = std::cout.rdbuf();
                    std::cout.rdbuf(s_coutAdapter->rdbuf());
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
            // note -- still seeming unreliable
			// auto& onThrow = GlobalOnThrowCallback();
			// if (!onThrow)
			// 	onThrow = &SendExceptionToLogger;
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

    #if XLE_STACK_WALKER_ENABLE

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

    #else

        static void SendExceptionToLogger(const ::Exceptions::CustomReportableException& e) {}

    #endif
}

