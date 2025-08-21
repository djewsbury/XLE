// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)


#pragma once

#include "Prefix.h"
#include <exception>
#include <cstdarg>
#include <cstdio>		// note -- could be avoided if we moved the constructor for BasicLabel into .cpp file
#include <cstring>		// (for memcpy)

#if FEATURE_EXCEPTIONS
    #include <type_traits>  // for std::is_base_of, used by exceptions
#else
	#include <stdlib.h>     // (for exit)
#endif

/*!
    \page ExceptionsPage Exceptions in XLE

    XLE provides some macros to make it convenient to switch on and off language level exceptions.
    Exceptions are enabled if the corresponding flag is set on the compiler. In other words, to
    disable use of exceptions in XLE, switch off the corresponding flag on the compiler command
    line.

    When exceptions are disabled in the compiler, Throw() results in a call to exit(), and catch
    code is compiled out.

    When writing exception code to work with XLE, consider using the following template. This will
    allow the code to compile & work in both the exceptions-enabled and exceptions-disabled cases.

    \code{.cpp}

    TRY {
        if (SomeCondition)
            Throw(std::runtime_error("Example error"));

        return std::make_unique<SomeObject>();
    } CATCH (const SomeSpecificException& e) {
        HandleException(e);
    } CATCH (const std::exception& e) {
        Log(Error) << "Got unknown generic exception: " << e.what() << std::endl;
    } CATCH (...) {
        RETHROW
    } CATCH_END

    \endcode
*/

#if FEATURE_EXCEPTIONS

    #define TRY { try 
    #define CATCH(x) catch(x) 
    #define RETHROW throw 
    #define CATCH_END } 

#else 

    #define TRY { if (true) 
    #define CATCH(x) else if (false) 
    #define RETHROW 
    #define CATCH_END } 

    #pragma warning(disable:4127)       // conditional expression is constant (must be disabled when using these macros)

#endif 

namespace Exceptions
{
	class CustomReportableException : public std::exception
	{
	public:
		virtual bool CustomReport() const { return false; }
	};

    class BasicLabel : public CustomReportableException
    {
    public:
        virtual const char* what() const never_throws /*override*/ { return _buffer; }
        
        BasicLabel(const char format[], ...) never_throws;
        BasicLabel(const char format[], va_list args) never_throws;
        BasicLabel(const BasicLabel& copyFrom) never_throws;
        BasicLabel& operator=(const BasicLabel& copyFrom) never_throws;
        virtual ~BasicLabel();
    protected:
        BasicLabel() never_throws;
        char _buffer[512];
    };

    inline BasicLabel::BasicLabel() never_throws { _buffer[0] = '\0'; }

#pragma warning(push)
#pragma warning(disable:4793)       // 'Exceptions::BasicLabel::BasicLabel' : function compiled as native :
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    inline BasicLabel::BasicLabel(const char format[], ...) never_throws
    {
        va_list args;
        va_start(args, format);
        std::vsnprintf(_buffer, dimof(_buffer), format, args);
        va_end(args);
    }

    inline BasicLabel::BasicLabel(const char format[], va_list args) never_throws
    {
        std::vsnprintf(_buffer, dimof(_buffer), format, args);
    }
#pragma warning(pop)

    inline BasicLabel::BasicLabel(const BasicLabel& copyFrom) never_throws
    {
        std::memcpy(_buffer, copyFrom._buffer, sizeof(_buffer));
    }

    inline BasicLabel& BasicLabel::operator=(const BasicLabel& copyFrom) never_throws
    {
		std::memcpy(_buffer, copyFrom._buffer, sizeof(_buffer));
        return *this;
    }

    inline BasicLabel::~BasicLabel() {}
}

#if COMPILER_ACTIVE == COMPILER_TYPE_MSVC
    #define NO_RETURN_PREFIX __declspec(noreturn)
    #define NO_RETURN_POSTFIX
#else
    #define NO_RETURN_PREFIX
    #define NO_RETURN_POSTFIX __attribute((noreturn))
#endif

namespace Utility
{
    #if defined(__INTELLISENSE__)
        inline void Throw(...) {}
    #else
        #if FEATURE_EXCEPTIONS
            typedef void (*OnThrowCallback)(const ::Exceptions::CustomReportableException&);
            inline OnThrowCallback& GlobalOnThrowCallback()
            {
                static OnThrowCallback s_result = nullptr;
                return s_result;
            }

            template <class E, typename std::enable_if<std::is_base_of<::Exceptions::CustomReportableException, E>::value>::type* = nullptr>
                inline NO_RETURN_PREFIX void Throw(const E& e) NO_RETURN_POSTFIX;

            template <class E, typename std::enable_if<std::is_base_of<::Exceptions::CustomReportableException, E>::value>::type*>
                inline NO_RETURN_PREFIX void Throw(const E& e)
            {
                // auto* callback = GlobalOnThrowCallback();
                // if (callback) (*callback)(e);
                throw e;
            }

            template <class E, typename std::enable_if<!std::is_base_of<::Exceptions::CustomReportableException, E>::value>::type* = nullptr>
                NO_RETURN_PREFIX void Throw(const E& e) NO_RETURN_POSTFIX;

            template <class E, typename std::enable_if<!std::is_base_of<::Exceptions::CustomReportableException, E>::value>::type*>
                inline NO_RETURN_PREFIX void Throw(const E& e)
            {
                throw e;
            }
        #else
            template <class E> inline void Throw(const E& e)
            {
                // OutputDebugString("Suppressed thrown exception");
                exit(-1);
            }
        #endif
    #endif
}


using namespace Utility;
