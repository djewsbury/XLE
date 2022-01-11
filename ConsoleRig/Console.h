// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/UTFUtils.h"
#include "../Utility/Threading/Mutex.h"
#include <string>
#include <vector>
#include <memory>

typedef struct lua_State lua_State;

namespace ConsoleRig
{
    class LuaState;
    class ConsoleVariableStorage;

    struct LockedLuaState
    {
        std::unique_lock<Threading::Mutex> _lock;
        lua_State* _luaState;
        lua_State* GetLuaState() const { return _luaState; }
    };
    
    class Console
    {
    public:
        void        Execute(const std::string& str);
        auto        AutoComplete(const std::string& input) -> std::vector<std::string>;

        void        Print(const char messageStart[]);
        void        Print(const char* messageStart, const char* messageEnd);
        void        Print(const std::string& message);
        void        Print(const std::basic_string<ucs2>& message);

        auto        GetLines(unsigned lineCount, unsigned scrollback=0) -> std::vector<std::basic_string<ucs2>>;
        unsigned    GetLineCount() const;

        static Console&     GetInstance() { return *s_instance; }
        static bool         HasInstance() { return s_instance != nullptr; }
        static void         SetInstance(Console* newInstance);

        LockedLuaState      LockLuaState();
        ConsoleVariableStorage& GetCVars();

        Console();
        ~Console();

        Console(const Console&) = delete;
        Console& operator=(const Console&) = delete;
        Console(Console&&) = delete;
        Console& operator=(Console&&) = delete;
    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
        static Console* s_instance;
    };

    template <typename Type> class ConsoleVariable
    {
    public:
        ConsoleVariable(const std::string& name, Type& attachedValue, const char cvarNamespace[] = nullptr);
        ConsoleVariable();
        ~ConsoleVariable();

        ConsoleVariable(ConsoleVariable&& moveFrom);
        ConsoleVariable& operator=(ConsoleVariable&& moveFrom);

        const std::string& Name() const { return _name; }

        Type*           _attachedValue;
    private:
        std::string     _name;
        std::string     _cvarNamespace;

        void Deregister();
    };

    namespace Detail
    {
        template <typename Type>
            Type&       FindTweakable(const char name[], Type defaultValue);
        template <typename Type>
            Type*       FindTweakable(const char name[]);
    }
}



    //
    //      Get a generic "tweakable" console variable. 
    //
    //          Tweakable --    this can be called for the same variable from
    //                          multiple places. The same name always evaluates
    //                          to the same value
    //
    //          TweakableUnique --   this can only be used when the variable
    //                               is only referenced from a single place
    //                               in the code.
    //

#if 1

    #define Tweakable(name, defaultValue)                                                       \
        ([&]() -> decltype(defaultValue)&                                                        \
            {                                                                                   \
                static auto& value = ::ConsoleRig::Detail::FindTweakable(name, defaultValue);   \
                return value;                                                                   \
            })()                                                                                \
        /**/

    // not currently working! The static within the lamdba gets destroyed
    // #define TweakableUnique(name, defaultValue)                                     \
    //     ([&]() -> decltype(defaultValue)&                                            \
    //         {                                                                       \
    //             static auto value = defaultValue;                                   \
    //             static ::ConsoleRig::ConsoleVariable<decltype(value)>(name, value); \
    //             return value;                                                       \
    //         })()                                                                    \
    //     /**/

#else

    template <int Line, typename Type>
        Type    GetTweakableValue(const char name[], Type defaultValue)
        {
            static auto& value = ::ConsoleRig::Detail::FindTweakable(name, defaultValue);
            return value;
        }

    #define Tweakable(name, defaultValue)                   \
        GetTweakableValue<__LINE__>(name, defaultValue);    \
        /**/


#endif


