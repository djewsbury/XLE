// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/StringUtils.h"
#include <string>
#include <ostream>
#include <memory>
#include <functional>

#if defined(_DEBUG)
    #define OSSERVICES_ENABLE_LOG
#endif

namespace Utility
{
    template<typename CharType>
        class InputStreamFormatter;
}

namespace OSServices
{
    class SourceLocation
    {
    public:
        const char*     _file = nullptr;
        unsigned        _line = ~0u;
        const char*     _function = nullptr;
    };

    class MessageTargetConfiguration
    {
    public:
        std::string _template;
        struct Sink
        {
            enum Enum { Console = 1<<0 };
            using BitField = unsigned;
        };
        Sink::BitField _enabledSinks = Sink::Console;
        Sink::BitField _disabledSinks = 0;
    };

    class LogCentral;

    template<typename CharType = char, typename CharTraits = std::char_traits<CharType>>
        class MessageTarget : public std::basic_streambuf<CharType, CharTraits>
    {
    public:
        void SetNextSourceLocation(const SourceLocation& sourceLocation) { _pendingSourceLocation = sourceLocation; _sourceLocationPrimed = true; }
        void SetConfiguration(const MessageTargetConfiguration& cfg) { _cfg = cfg; }
        void SetExternalMessageHandler(std::function<std::streamsize(const CharType*, std::streamsize)> externalMessageHandler) { _externalMessageHandler = externalMessageHandler; }
		bool IsEnabled() const { return _cfg._enabledSinks != 0; }
        void Disable() { _cfg._enabledSinks = 0; }

        MessageTarget(StringSection<> id, std::basic_streambuf<CharType, CharTraits>& chain = DefaultChain());
        ~MessageTarget();

        MessageTarget(const MessageTarget&) = delete;
        MessageTarget& operator=(const MessageTarget&) = delete;

        static std::basic_streambuf<CharType, CharTraits>& DefaultChain();
    private:
        std::basic_streambuf<CharType, CharTraits>* _chain;
        SourceLocation              _pendingSourceLocation;
        bool                        _sourceLocationPrimed;
        MessageTargetConfiguration _cfg;
        std::function<std::streamsize(const CharType* s, std::streamsize count)> _externalMessageHandler;
        std::weak_ptr<LogCentral>   _registeredLogCentral;

        using int_type = typename std::basic_streambuf<CharType>::int_type;
        virtual std::streamsize xsputn(const CharType* s, std::streamsize count) override;
        virtual int_type overflow(int_type ch) override;
        virtual int sync() override;

        std::streamsize FormatAndOutput(
            StringSection<char> msg,
            const std::string& fmtTemplate,
            const SourceLocation& sourceLocation);
    };

    class LogConfigurationSet;

    /// <summary>Manages all message targets for a module</summary>
    /// LogCentral holds a list of all active logging message targets for a given current module.
    /// This list is used when we want to apply a configuration set.
    /// We separate the management of the message targets from the management of the configuration
    /// because we want to be able to use the message targets before the configuration has been
    /// loaded (ie, during early stages of initialization).
    /// Furthermore LogCentral is bound to a single module, but the logging configuration can
    /// be shared over multiple modules.
    class LogCentral
    {
    public:
        // We want to try to ensure that there's only a single instance of LogCentral
        // across the entire process, regardless of the number of modules. We can do
        // that with visibility attributes when using clang. This ensures that shared
        // libraries that are loaded by the main executable will use the implementation
        // from that main executable
        #if COMPILER_ACTIVE == COMPILER_TYPE_CLANG
            static const std::shared_ptr<LogCentral>& GetInstance() __attribute__((visibility("default")));
            static void DestroyInstance() __attribute__((visibility("default")));
        #else
            static const std::shared_ptr<LogCentral>& GetInstance();
            static void DestroyInstance();
        #endif

        void Register(MessageTarget<>& target, StringSection<> id);
        void Deregister(MessageTarget<>& target);

        void SetConfiguration(const std::shared_ptr<LogConfigurationSet>& cfgs);

        LogCentral();
        ~LogCentral();
        LogCentral& operator=(const LogCentral&) = delete;
        LogCentral(const LogCentral&) = delete;
    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

    class LogConfigurationSet
    {
    public:
        MessageTargetConfiguration ResolveConfig(StringSection<> name) const;
        void Set(StringSection<> id, MessageTargetConfiguration& cfg);

        LogConfigurationSet();
        LogConfigurationSet(InputStreamFormatter<char>& formatter);
        ~LogConfigurationSet();
    private:
        class Config
        {
        public:
            std::vector<std::string> _inherit;
            MessageTargetConfiguration _cfg;
        };

        std::vector<std::pair<std::string, Config>> _configs;

        Config LoadConfig(InputStreamFormatter<char>& formatter);
    };

    inline void DeserializationOperator(InputStreamFormatter<char>& str, LogConfigurationSet& cls)
    {
        cls = LogConfigurationSet(str);
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename CharType, typename CharTraits>
        MessageTarget<CharType, CharTraits>::MessageTarget(StringSection<> id, std::basic_streambuf<CharType, CharTraits>& chain)
    : _chain(&chain)
    {
        #if defined(OSSERVICES_ENABLE_LOG)
            auto logCentral = LogCentral::GetInstance();
            if (logCentral) {
                logCentral->Register(*this, id);
                _registeredLogCentral = logCentral;
            }
        #endif
    }

    template<typename CharType, typename CharTraits>
        MessageTarget<CharType, CharTraits>::~MessageTarget()
    {
        _chain->pubsync();
        #if defined(OSSERVICES_ENABLE_LOG)
            // DavidJ --
            //      For global MessageTargets objects, the LogCentral instance can be destroyed
            //      first. So we can't access the singleton GetInstance() method from here. We
            //      need to keep a weak pointer to the LogCentral instance we registered with,
            //      and check it to make sure it still exists.
            auto logCentral = _registeredLogCentral.lock();
            if (logCentral)
                logCentral->Deregister(*this);
        #endif
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename CharType>
        std::basic_ostream<CharType>& operator<<(std::basic_ostream<CharType>& ostream, const SourceLocation& sourceLocation)
    {
        auto* rdbuf = ostream.rdbuf();
        ((MessageTarget<CharType>*)rdbuf)->SetNextSourceLocation(sourceLocation);
        return ostream;
    }

    namespace Internal
    {
        constexpr const char* JustFilename(const char filePath[])
        {
            const char* pastLastSlash = filePath;
            for (const auto* i=filePath; *i; ++i)
                if (*i == '\\' || *i == '/') pastLastSlash = i+1;
            return pastLastSlash;
        }

        template<typename T> struct LogItemContainer
        {
            ::std::basic_ostream<typename T::char_type, typename T::traits_type> _strm;
            template<typename X> LogItemContainer(X* x) : _strm(x) {}
        };
    }

#if defined(OSSERVICES_ENABLE_LOG)
	#if defined(__PRETTY_FUNCTION__)
		#define MakeSourceLocation (::OSServices::SourceLocation {::OSServices::Internal::JustFilename(__FILE__), __LINE__, __PRETTY_FUNCTION__})
	#else
		#define MakeSourceLocation (::OSServices::SourceLocation {::OSServices::Internal::JustFilename(__FILE__), __LINE__, __FUNCTION__})
	#endif
    // Use of std::make_unique<> here removes the "rvalue" flag from the temporary basic_ostream (at the cost of an extra allocation)
    #define Log(X) (std::make_unique<OSServices::Internal::LogItemContainer<std::remove_reference_t<decltype(X)>>>(&X)->_strm << MakeSourceLocation)
#else
    // DavidJ -- HACK -- we need to disable the warning "dangling-else" for this construct
    //      unfortunately, it has to be done globally because this evaluates to a macro
    #pragma GCC diagnostic ignored "-Wdangling-else"
    extern std::ostream* g_fakeOStream;
    #define Log(X) if (true) {} else (*::OSServices::g_fakeOStream)
#endif

}

extern OSServices::MessageTarget<> Error;
extern OSServices::MessageTarget<> Warning;
extern OSServices::MessageTarget<> Debug;
extern OSServices::MessageTarget<> Verbose;
