// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Log.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/AssetUtils.h"
#include "../Assets/DepVal.h"
#include "../Utility/StringFormat.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/Streams/FormatterUtils.h"
#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/Streams/StreamDOM.h"
#include "../Utility/Threading/Mutex.h"
#include "../Foreign/fmt/format.h"
#include <iostream>

namespace OSServices
{
    template<typename CharType, typename CharTraits>
        std::streamsize MessageTarget<CharType, CharTraits>::FormatAndOutput(
            StringSection<char> msg,
            const std::string& fmtTemplate,
            const SourceLocation& sourceLocation)
    {
        auto outputFn = _externalMessageHandler;
        if (outputFn == nullptr) {
            outputFn = [](const CharType* s, std::streamsize count) -> std::streamsize {
                return std::cout.rdbuf()->sputn(s, count);
            };
        }

		if (!fmtTemplate.empty()) {
            auto fmt = fmt::format(
                fmtTemplate,
                fmt::arg("file", sourceLocation._file),
                fmt::arg("line", sourceLocation._line));
            outputFn(fmt.data(), fmt.size());
            outputFn(" ", 1); // always append one extra space since the format string can't
        }
        return outputFn(msg.begin(), msg.size());       // (note; don't include the length of the formatted section; because it will confuse the caller when it is a basic_ostream
    }

    template<typename CharType, typename CharTraits>
        std::streamsize MessageTarget<CharType, CharTraits>::xsputn(const CharType* s, std::streamsize count)
    {
        if (_cfg._enabledSinks & MessageTargetConfiguration::Sink::Console) {
            auto result = FormatAndOutput(
                MakeStringSection(s, s + count),
                _sourceLocationPrimed ? _cfg._template : std::string(),
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
        if (std::basic_streambuf<CharType, CharTraits>::traits_type::not_eof(ch)) {
            if (_cfg._enabledSinks & MessageTargetConfiguration::Sink::Console) {
                std::cout.rdbuf()->sputc((CharType)ch);
                _sourceLocationPrimed |= std::basic_streambuf<CharType, CharTraits>::traits_type::eq_int_type(ch, (int_type)'\n');
                static_assert(0!=std::basic_streambuf<CharType, CharTraits>::traits_type::eof(), "Expecting char traits EOF character to be something other than 0");
                return 0;   // (anything other than traits_type::eof() signifies success)
            }
        }

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

    MessageTargetConfiguration LogConfigurationSet::ResolveConfig(StringSection<> name) const
    {
        MessageTargetConfiguration result;
        auto i = std::find_if(_configs.begin(), _configs.end(),
            [name](const std::pair<std::string, Config>&p) { return XlEqString(MakeStringSection(p.first), name); });
        if (i == _configs.end()) return result;

        const auto& src = i->second;
        for (const auto&inherit:src._inherit)
            MergeIn(result, ResolveConfig(inherit));
        MergeIn(result, src._cfg);
        return result;
    }

    void LogConfigurationSet::Set(StringSection<> id, MessageTargetConfiguration& cfg)
    {
        auto str = id.AsString();
        auto existing = std::find_if(
            _configs.begin(), _configs.end(),
            [str](const std::pair<std::string, Config>& p) { return p.first == str; });
        if (existing != _configs.end()) {
            existing->second = {{}, cfg};
        } else {
            _configs.emplace_back(std::make_pair(str, Config{{}, cfg}));
        }
    }

    LogConfigurationSet::LogConfigurationSet() {}
    LogConfigurationSet::LogConfigurationSet(InputStreamFormatter<char>& formatter)
    {
        while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
            StringSection<char> eleName;
            formatter.TryKeyedItem(eleName);
            RequireBeginElement(formatter);

            auto cfg = LoadConfig(formatter);
            _configs.emplace_back(std::make_pair(eleName.AsString(), std::move(cfg)));

            RequireEndElement(formatter);
        }
    }

    auto LogConfigurationSet::LoadConfig(InputStreamFormatter<char>& formatter) -> Config
    {
        StreamDOM<InputStreamFormatter<char>> doc(formatter);
        Config cfg;
        if (doc.RootElement().Attribute("OutputToConsole")) {
            bool outputToConsole = doc.RootElement().Attribute("OutputToConsole", false);
            cfg._cfg._enabledSinks = (outputToConsole ? MessageTargetConfiguration::Sink::Console : 0u);
            cfg._cfg._disabledSinks = ((!outputToConsole) ? MessageTargetConfiguration::Sink::Console : 0u);
        }
        cfg._cfg._template = doc.RootElement().Attribute("Template").Value().AsString();

        for (const auto& inherentElement:doc.RootElement().Element("Inherit").children())
            cfg._inherit.push_back(inherentElement.Name().AsString());

        return cfg;
    }

    LogConfigurationSet::~LogConfigurationSet() {}

#endif

////////////////////////////////////////////////////////////////////////////////////////////////////

    class LogCentral::Pimpl
    {
    public:
        struct Target
        {
            std::string _id;
            MessageTarget<>* _target;
        };
        std::vector<std::pair<uint64_t, Target>> _activeTargets;

        #if defined(OSSERVICES_ENABLE_LOG)
            std::shared_ptr<LogConfigurationSet> _activeCfgSet;
        #endif
    };

    static std::shared_ptr<LogCentral> s_instance;
    static Threading::Mutex s_logCentralInstanceLock;
    
    const std::shared_ptr<LogCentral>& LogCentral::GetInstance()
    {
        if (!s_instance) {
            ScopedLock(s_logCentralInstanceLock);
            if (!s_instance) {
                s_instance = std::make_shared<LogCentral>();
            }
        }
        return s_instance;
    }

    void LogCentral::DestroyInstance()
    {
        s_instance.reset();
    }

    void LogCentral::Register(MessageTarget<>& target, StringSection<> id)
    {
        assert(!id.IsEmpty());      // empty id's are not supported -- without the id, there's no way to assign a configuration

        auto hash = Hash64(id);
        auto i = LowerBound(_pimpl->_activeTargets, hash);

        // We allow multiple targets with the same name. Despite that fact that they are different
        // objects, they should contain the same data
        _pimpl->_activeTargets.insert(i, std::make_pair(hash, Pimpl::Target{id.AsString(), &target}));

        #if defined(OSSERVICES_ENABLE_LOG)
            // Set the initial configuration
            if (_pimpl->_activeCfgSet)
                target.SetConfiguration(_pimpl->_activeCfgSet->ResolveConfig(id));
        #endif
    }

    void LogCentral::Deregister(MessageTarget<>& target)
    {
        auto i = std::find_if(_pimpl->_activeTargets.begin(), _pimpl->_activeTargets.end(),
            [&target](const std::pair<uint64_t, Pimpl::Target>& p) { return p.second._target == &target; });
        assert(i!=_pimpl->_activeTargets.end());
        _pimpl->_activeTargets.erase(i);
    }

    void LogCentral::SetConfiguration(const std::shared_ptr<LogConfigurationSet>& cfgs)
    {
        #if defined(OSSERVICES_ENABLE_LOG)
            _pimpl->_activeCfgSet = cfgs;
            if (cfgs) {
                for (const auto&t:_pimpl->_activeTargets)
                    t.second._target->SetConfiguration(cfgs->ResolveConfig(t.second._id));
            } else {
                Error.SetConfiguration(MessageTargetConfiguration{});
                Warning.SetConfiguration(MessageTargetConfiguration{});
                Debug.SetConfiguration(MessageTargetConfiguration{});
                Verbose.SetConfiguration(MessageTargetConfiguration{ std::string(), 0, MessageTargetConfiguration::Sink::Console });
            }
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
