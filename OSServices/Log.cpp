// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Log.h"
#include "../Utility/IteratorUtils.h"
#include "../Formatters/FormatterUtils.h"
#include "../Formatters/TextFormatter.h"
#include "../Formatters/StreamDOM.h"
#include "../Utility/Threading/Mutex.h"
#include <iostream>

namespace OSServices
{
    template<typename CharType, typename CharTraits>
        std::streamsize MessageTarget<CharType, CharTraits>::FormatAndOutput(
            StringSection<char> msg,
            const std::string& fmtTemplate,
            const SourceLocation& sourceLocation)
    {
#if defined(OSSERVICES_ENABLE_LOG)
        auto outputFn = _externalMessageHandler;
        if (outputFn == nullptr) {
            outputFn = [](const CharType* s, std::streamsize count) -> std::streamsize {
                return std::cout.rdbuf()->sputn(s, count);
            };
        }

		// if (!fmtTemplate.empty()) {
        //     auto fmt = fmt::format(
        //         fmtTemplate,
        //         fmt::arg("file", sourceLocation._file),
        //         fmt::arg("line", sourceLocation._line));
        //     outputFn(fmt.data(), fmt.size());
        //     outputFn(" ", 1); // always append one extra space since the format string can't
        // }
        return outputFn(msg.begin(), msg.size());       // (note; don't include the length of the formatted section; because it will confuse the caller when it is a basic_ostream
#else
    return 0;
#endif
    }

    template<typename CharType, typename CharTraits>
        std::streamsize MessageTarget<CharType, CharTraits>::xsputn(const CharType* s, std::streamsize count)
    {
        if (_cfg->_enabledSinks & MessageTargetConfiguration::Sink::Console) {
            auto result = FormatAndOutput(
                MakeStringSection(s, s + count),
                _sourceLocationPrimed ? _cfg->_template : std::string(),
                _pendingSourceLocation);
            _sourceLocationPrimed = false;
            return result;
        } else {
            return 0;
        }
    }

    template<typename CharType, typename CharTraits>
        auto MessageTarget<CharType, CharTraits>::overflow(int_type ch) -> int_type
    {
#if defined(OSSERVICES_ENABLE_LOG)
        if (std::basic_streambuf<CharType, CharTraits>::traits_type::not_eof(ch)) {
            if (_cfg->_enabledSinks & MessageTargetConfiguration::Sink::Console) {
                std::cout.rdbuf()->sputc((CharType)ch);
                _sourceLocationPrimed |= std::basic_streambuf<CharType, CharTraits>::traits_type::eq_int_type(ch, (int_type)'\n');
                static_assert(0!=std::basic_streambuf<CharType, CharTraits>::traits_type::eof(), "Expecting char traits EOF character to be something other than 0");
                return 0;   // (anything other than traits_type::eof() signifies success)
            }
        }
#endif

        return std::basic_streambuf<CharType, CharTraits>::overflow(ch);
    }

    template<typename CharType, typename CharTraits>
        int MessageTarget<CharType, CharTraits>::sync() 
		{ 
			return std::cout.rdbuf()->pubsync(); 
		}

    template<>
        std::basic_streambuf<char>& MessageTarget<char>::DefaultChain()
    {
        return *std::cout.rdbuf();
    }

    class LocalModuleRegistry
    {
    public:
        Threading::Mutex _lock;
        std::vector<std::pair<uint64_t, std::shared_ptr<MessageTargetConfiguration>>> _activeTargets;

        std::shared_ptr<MessageTargetConfiguration> Register(StringSection<> id)
        {
            assert(!id.IsEmpty());      // empty id's are not supported -- without the id, there's no way to assign a configuration
            ScopedLock(_lock);

            auto hash = Hash64(id);
            auto i = LowerBound(_activeTargets, hash);
            if (i != _activeTargets.end() && i->first == hash)
                return i->second;
            
            auto cfg = std::make_shared<MessageTargetConfiguration>();
            _activeTargets.emplace(i, hash, cfg);
            return cfg;
        }

    };

    static LocalModuleRegistry& GetLocalModuleRegistry()
    {
        static LocalModuleRegistry s_instance;
        return s_instance;
    }

    template<typename CharType, typename CharTraits>
        MessageTarget<CharType, CharTraits>::MessageTarget(StringSection<> id, std::basic_streambuf<CharType, CharTraits>& chain)
    : _chain(&chain)
    {
        #if defined(OSSERVICES_ENABLE_LOG)
            _cfg = GetLocalModuleRegistry().Register(id);
        #endif
    }

    template<typename CharType, typename CharTraits>
        MessageTarget<CharType, CharTraits>::~MessageTarget()
    {
        _chain->pubsync();
    }

    template class MessageTarget<>;

////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(OSSERVICES_ENABLE_LOG)

    static void MergeIn(MessageTargetConfiguration& dst, const MessageTargetConfiguration& src)
    {
        if (!src._template.empty())
            dst._template = src._template;
        dst._enabledSinks |= src._enabledSinks;
        dst._enabledSinks &= ~src._disabledSinks;
    }

    std::optional<MessageTargetConfiguration> LogConfigurationSet::ResolveConfig(uint64_t h) const
    {
        auto i = std::find_if(b2e(_configs), [h](const auto&p) { return p.first == h; });
        if (i == _configs.end()) return {};

        MessageTargetConfiguration result;
        const auto& src = i->second;
        for (const auto&inherit:src._inherit)
            if (auto r = ResolveConfig(inherit))
                MergeIn(result, *r);
        MergeIn(result, src._cfg);
        return result;
    }

    void LogConfigurationSet::Set(StringSection<> id, MessageTargetConfiguration& cfg)
    {
        uint64_t h = Hash64(id);
        auto existing = std::find_if(b2e(_configs), [h](const auto& p) { return p.first == h; });
        if (existing != _configs.end()) {
            existing->second = {id.AsString(), {}, cfg};
        } else {
            _configs.emplace_back(std::make_pair(h, Config{id.AsString(), {}, cfg}));
        }
    }

    LogConfigurationSet::LogConfigurationSet() {}
    LogConfigurationSet::LogConfigurationSet(Formatters::TextInputFormatter<char>& formatter)
    {
        while (formatter.PeekNext() == Formatters::FormatterBlob::KeyedItem) {
            StringSection<char> eleName;
            formatter.TryKeyedItem(eleName);
            RequireBeginElement(formatter);

            auto cfg = LoadConfig(formatter);
            cfg._name = eleName.AsString();
            _configs.emplace_back(Hash64(eleName), std::move(cfg));

            RequireEndElement(formatter);
        }
    }

    auto LogConfigurationSet::LoadConfig(Formatters::TextInputFormatter<char>& formatter) -> Config
    {
        Formatters::StreamDOM<Formatters::TextInputFormatter<char>> doc(formatter);
        Config cfg;
        if (doc.RootElement().Attribute("OutputToConsole")) {
            bool outputToConsole = doc.RootElement().Attribute("OutputToConsole", false);
            cfg._cfg._enabledSinks = (outputToConsole ? MessageTargetConfiguration::Sink::Console : 0u);
            cfg._cfg._disabledSinks = ((!outputToConsole) ? MessageTargetConfiguration::Sink::Console : 0u);
        }
        cfg._cfg._template = doc.RootElement().Attribute("Template").Value().AsString();

        for (const auto& inherentElement:doc.RootElement().Element("Inherit").children())
            cfg._inherit.push_back(Hash64(inherentElement.Name()));

        return cfg;
    }

    LogConfigurationSet::~LogConfigurationSet() {}

#endif

////////////////////////////////////////////////////////////////////////////////////////////////////

    class LogCentral::Pimpl
    {
    public:
        #if defined(OSSERVICES_ENABLE_LOG)
            Threading::Mutex _lock;
            std::vector<std::pair<uint64_t, std::shared_ptr<MessageTargetConfiguration>>> _cfgs;
        #endif
    };

    void LogCentral::AttachCurrentModule()
    {
        #if defined(OSSERVICES_ENABLE_LOG)
            auto& localModule = GetLocalModuleRegistry();
            std::unique_lock<Threading::Mutex> lk0 { _pimpl->_lock, std::defer_lock };
            std::unique_lock<Threading::Mutex> lk1 { localModule._lock, std::defer_lock };
            std::lock(lk0, lk1);
            auto i = localModule._activeTargets.begin();
            auto i2 = _pimpl->_cfgs.begin();
            while (i!=localModule._activeTargets.end() || i2!=_pimpl->_cfgs.end()) {
                while (i2!= _pimpl->_cfgs.end() && (i==localModule._activeTargets.end() || i2->first < i->first)) {
                    i = localModule._activeTargets.emplace(i, i2->first, i2->second);
                    ++i; ++i2;
                }

                while (i!= localModule._activeTargets.end() && (i2==_pimpl->_cfgs.end() || i->first < i2->first)) {
                    i2 = _pimpl->_cfgs.emplace(i2, i->first, i->second);
                    ++i; ++i2;
                }

                if (i!=localModule._activeTargets.end() && i2!=_pimpl->_cfgs.end() && i->first == i2->first) {
                    *i->second = *i2->second;       // copy values
                    ++i; ++i2;
                }
            }
        #endif
    }

    void LogCentral::DetachCurrentModule()
    {}

    void LogCentral::SetConfiguration(const LogConfigurationSet& cfgs)
    {
        #if defined(OSSERVICES_ENABLE_LOG)
            for (const auto&t:_pimpl->_cfgs)
                if (auto update = cfgs.ResolveConfig(t.first))
                    *t.second = *update;
        #endif
    }

    LogCentral::LogCentral()
    {
        _pimpl = std::make_unique<Pimpl>();
        // We can't load the config set here, because we will frequently get here during static initialization
        // before the file system has been properly initialized
    }

    LogCentral::~LogCentral() 
    {
    }

    std::ostream* g_fakeOStream = nullptr;
}

OSServices::MessageTarget<> Error("Error");
OSServices::MessageTarget<> Warning("Warning");
OSServices::MessageTarget<> Debug("Debug");
OSServices::MessageTarget<> Verbose("Verbose");
