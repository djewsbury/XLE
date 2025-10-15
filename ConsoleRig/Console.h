// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/StringUtils.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/UTFUtils.h"
#if !defined(__CLR_VER)
    #include "../Utility/Threading/Mutex.h"
#endif
#include <string>
#include <vector>
#include <memory>

#if !defined(_DEBUG)

    #define Tweakable(name, defaultValue)   defaultValue

#else

    #define Tweakable(name, defaultValue)                                                       \
        ([&]() -> decltype(defaultValue)&                                                       \
            {                                                                                   \
                static auto& value = ::ConsoleRig::Internal::FindTweakable(name, defaultValue); \
                return value;                                                                   \
            })()                                                                                \
        /**/

#endif

namespace ConsoleRig
{
    class ConsoleVariableStorage;
    class IConsoleScriptingInterface;
    struct LockedScriptingState;

    class Console
    {
    public:
        void        Execute(const std::string& str);
        auto        AutoComplete(const std::string& input) -> std::vector<std::string>;

        void        Print(StringSection<> message);
        void        Print(const std::basic_string<ucs2>& message);

        auto        GetLines(unsigned lineCount, unsigned scrollback=0) -> std::vector<std::basic_string<ucs2>>;
        unsigned    GetLineCount() const;

        static Console&     GetInstance() { return *s_instance; }
        static bool         HasInstance() { return s_instance != nullptr; }
        static void         SetInstance(Console* newInstance);

        #if !defined(__CLR_VER)
            LockedScriptingState LockScriptingState();
        #endif

        void PushScriptingInterface(std::shared_ptr<IConsoleScriptingInterface> interf);
        void PopScriptingInterface(IConsoleScriptingInterface&);

        ConsoleVariableStorage& GetCVars();
        std::shared_ptr<ConsoleVariableStorage> GetCVarsPtr();

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

    class IConsoleScriptingInterface
    {
    public:
        virtual void Execute(StringSection<> str) = 0;
        virtual auto AutoComplete(StringSection<> str) -> std::vector<std::string> = 0;
        virtual ~IConsoleScriptingInterface();
    };

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    #if !defined(__CLR_VER)
        struct LockedScriptingState
        {
            std::unique_lock<Threading::Mutex> _lock;
            IConsoleScriptingInterface* _interface = nullptr;
        };
    #endif

    template <typename Type> class ConsoleVariable
    {
    public:
        ConsoleVariable(const std::string& name, Type& attachedValue);
        ConsoleVariable();
        ~ConsoleVariable();

        ConsoleVariable(ConsoleVariable&& moveFrom);
        ConsoleVariable& operator=(ConsoleVariable&& moveFrom);

        const std::string& Name() const { return _name; }

        Type*           _attachedValue;
    private:
        std::string     _name;
    };

    std::ostream& GetWarningStream();

    void        xleWarning(const char format[], ...);
    void        xleWarning(const char format[], va_list args);

    #if defined(_DEBUG)
        void            xleWarningDebugOnly(const char format[], ...);
    #else
        inline void     xleWarningDebugOnly(const char format[], ...) { (void)format; }
    #endif

    namespace Internal
    {
        template <typename Type>
            Type&       FindTweakable(const char name[], Type defaultValue);
        template <typename Type>
            Type*       FindTweakable(const char name[]);
    }

    class ConsoleVariableStorage
    {
    public:
        class ICVarTable
        {
        public:
            virtual ~ICVarTable() {}
        };

        template<typename Type>
            class CVarTable : public ICVarTable
        {
        public:
            using Table = std::vector<std::unique_ptr<std::pair<Type, ConsoleVariable<Type>>>>;
            Table _table;
        };

        template<typename Type>
            using Table = typename CVarTable<Type>::Table;

        template<typename Type>
            Table<Type>& GetTable()
        {
            constexpr auto hash = TypeHashCode<Type>;
            auto i = LowerBound(_tables, (uint64_t)hash);
            if (i == _tables.end() || i->first != hash) {
                auto newTable = std::make_unique<CVarTable<Type>>();
                i = _tables.insert(i, std::make_pair(hash, std::move(newTable)));
            }
            
            auto* rawTable = i->second.get();
            return ((CVarTable<Type>*)rawTable)->_table;    // (critical upcast here)
        }

    private:
        std::vector<std::pair<uint64_t, std::unique_ptr<ICVarTable>>> _tables;
    };
}

