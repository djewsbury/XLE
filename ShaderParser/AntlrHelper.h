// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Exceptions.h"
#include "../Utility/StringUtils.h"
#include "../Core/SelectConfiguration.h"
#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
    // Antlr includes windows.h; so get in before it can
    #include "../OSServices/WinAPI/IncludeWindows.h"
#endif

#if XLE_ANTLR_ENABLE

#define	ANTLR3_STRDUP(instr)		(pANTLR3_UINT8)(_strdup  ((const char *)(instr)))
#include <antlr3defs.h>
#include <antlr3interfaces.h>
#include <antlr3basetree.h>
#include <antlr3exception.h>
#include <memory>
#include <string>

typedef void ExceptionHandler(const ANTLR3_EXCEPTION* exception, const ANTLR3_UINT8**);
typedef struct { ExceptionHandler* _callback; void* _userData; } ExceptionHandlerAndUserData;
extern "C" ExceptionHandlerAndUserData SetShaderParserExceptionHandler(ExceptionHandlerAndUserData handler);

namespace ShaderSourceParser { namespace AntlrHelper
{
    namespace Internal
    {
        #pragma push_macro("free")
        #undef free
        template <typename Type> 
            void DestroyAntlrObject(Type* object) 
            {
                if (object) object->free(object);
            }
        #pragma pop_macro("free")

        template<> void DestroyAntlrObject<>(struct ::ANTLR3_INPUT_STREAM_struct*);
    }

    template <typename Type>
        class AntlrPtr
    {
    public:
        operator Type*()                { return _antlrObj; }
        operator const Type*() const    { return _antlrObj; }
        operator bool() const           { return !!_antlrObj; }
        Type* operator->()              { return _antlrObj; }
        const Type* operator->() const  { return _antlrObj; }
        Type* get()                     { return _antlrObj; }
        const Type* get() const         { return _antlrObj; }

        AntlrPtr(Type* antlrObj) : _antlrObj(antlrObj) {}
        ~AntlrPtr()     { if(_antlrObj) Internal::DestroyAntlrObject(_antlrObj); }
        AntlrPtr()      { _antlrObj = NULL; }

        AntlrPtr(const AntlrPtr&) = delete;
        AntlrPtr& operator=(const AntlrPtr&) = delete;

        AntlrPtr(AntlrPtr&& moveFrom)
        {
            _antlrObj = moveFrom._antlrObj;
            moveFrom._antlrObj = nullptr;
        }
        AntlrPtr& operator=(AntlrPtr&& moveFrom)
        {
            _antlrObj = moveFrom._antlrObj;
            moveFrom._antlrObj = nullptr;
            return *this;
        }
    private:
        Type*        _antlrObj;
    };

    pANTLR3_BASE_TREE       GetChild(pANTLR3_BASE_TREE node, ANTLR3_UINT32 childIndex);
	unsigned				GetChildCount(pANTLR3_BASE_TREE node);
    pANTLR3_COMMON_TOKEN    GetToken(pANTLR3_BASE_TREE node);
    ANTLR3_UINT32           GetType(pANTLR3_COMMON_TOKEN token);

    template<typename CharType=char>
        std::basic_string<CharType> AsString(ANTLR3_STRING* antlrString);

    template<typename CharType=char>
        std::basic_string<CharType> AsString(pANTLR3_COMMON_TOKEN token);

	void Description(std::ostream& str, pANTLR3_COMMON_TOKEN token);
	void StructureDescription(std::ostream& str, pANTLR3_BASE_TREE node, unsigned indent=0);

	class ExceptionSet
	{
	public:
		std::vector<Error> _errors;

		static void __cdecl HandleException(
			ExceptionSet* pimpl,
			const ANTLR3_EXCEPTION* exc,
			const ANTLR3_UINT8 ** tokenNames);
	};

	class ExceptionContext
	{
	public:
		ExceptionSet _exceptions;

		ExceptionContext();
		~ExceptionContext();
	private:
		ExceptionHandlerAndUserData _previousExceptionHandler;
	};
}}

#endif

