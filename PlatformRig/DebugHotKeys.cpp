// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DebugHotKeys.h"
#include "InputContext.h"
#include "../Assets/Assets.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/ConfigFileContainer.h"
#include "../Assets/AssetUtils.h"
#include "../ConsoleRig/Console.h"
#include "../OSServices/RawFS.h"
#include "../Formatters/StreamDOM.h"
#include "../Formatters/TextFormatter.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/UTFUtils.h"
#include "../Utility/Conversion.h"

using namespace OSServices::Literals;

namespace PlatformRig
{

    class HotKeyInputHandler : public IInputListener
    {
    public:
        ProcessInputResult    OnInputEvent(const InputContext& context, const OSServices::InputSnapshot& evnt);

        HotKeyInputHandler(StringSection<> filename) : _filename(filename.AsString()) {}
    protected:
        ::Assets::rstring _filename;
    };

    class TableOfKeys
    {
    public:
        const std::vector<std::pair<uint32_t, std::string>>& GetTable() const { return _table; }
        const ::Assets::DependencyValidation& GetDependencyValidation() const { return _validationCallback; }

        TableOfKeys(
			Formatters::TextInputFormatter<utf8>& formatter,
			const ::Assets::DirectorySearchRules&,
			const ::Assets::DependencyValidation& depVal);
        TableOfKeys() = default;
        ~TableOfKeys();
    private:
        ::Assets::DependencyValidation                             _validationCallback;
        std::vector<std::pair<uint32_t, std::string>>     _table;
    };

    TableOfKeys::TableOfKeys(
		Formatters::TextInputFormatter<utf8>& formatter,
		const ::Assets::DirectorySearchRules&,
		const ::Assets::DependencyValidation& depVal)
	: _validationCallback(depVal)
    {
        Formatters::StreamDOM<Formatters::TextInputFormatter<utf8>> doc(formatter);

        for (auto attrib:doc.RootElement().attributes()) {
            auto executeString = attrib.Value();
            if (!executeString.IsEmpty()) {
                auto keyName = attrib.Name();
                auto p = std::make_pair(
                    OSServices::KeyId_Make(keyName),
                    executeString.AsString());
                _table.push_back(p);
            }
        }
    }
    TableOfKeys::~TableOfKeys() {}

    auto HotKeyInputHandler::OnInputEvent(const PlatformRig::InputContext& context, const OSServices::InputSnapshot& evnt) -> ProcessInputResult
    {
        constexpr auto ctrlKey = "control"_key;
        if (evnt.IsHeld(ctrlKey)) {
            auto* t = Assets::GetAssetMarker<TableOfKeys>(MakeStringSection(_filename))->TryActualize();
            if (t) {
                for (auto i=t->GetTable().cbegin(); i!=t->GetTable().cend(); ++i) {
                    if (evnt.IsPress(i->first)) {
                        ConsoleRig::Console::GetInstance().Execute(i->second);
                        return ProcessInputResult::Consumed;
                    }
                }
            }
        }

        return ProcessInputResult::Passthrough;
    }

    std::unique_ptr<IInputListener> MakeHotKeysHandler(StringSection<> filename)
    {
        return std::make_unique<HotKeyInputHandler>(filename);
    }

}

