// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DebugHotKeys.h"
#include "InputListener.h"
#include "../Assets/Assets.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/ConfigFileContainer.h"
#include "../Assets/AssetUtils.h"
#include "../ConsoleRig/Console.h"
#include "../OSServices/RawFS.h"
#include "../Utility/Streams/StreamDOM.h"
#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/UTFUtils.h"
#include "../Utility/Conversion.h"

namespace PlatformRig
{

    class HotKeyInputHandler : public IInputListener
    {
    public:
        bool    OnInputEvent(const InputContext& context, const InputSnapshot& evnt);

        HotKeyInputHandler(StringSection<> filename) : _filename(filename.AsString()) {}
    protected:
        ::Assets::rstring _filename;
    };

    class TableOfKeys
    {
    public:
        const std::vector<std::pair<uint32, std::string>>& GetTable() const { return _table; }
        const ::Assets::DependencyValidation& GetDependencyValidation() const { return _validationCallback; }

        TableOfKeys(
			InputStreamFormatter<utf8>& formatter,
			const ::Assets::DirectorySearchRules&,
			const ::Assets::DependencyValidation& depVal);
        ~TableOfKeys();
    private:
        ::Assets::DependencyValidation                             _validationCallback;
        std::vector<std::pair<uint32, std::string>>     _table;
    };

    TableOfKeys::TableOfKeys(
		InputStreamFormatter<utf8>& formatter,
		const ::Assets::DirectorySearchRules&,
		const ::Assets::DependencyValidation& depVal)
	: _validationCallback(depVal)
    {
        StreamDOM<InputStreamFormatter<utf8>> doc(formatter);

        for (auto attrib:doc.RootElement().attributes()) {
            auto executeString = attrib.Value();
            if (!executeString.IsEmpty()) {
                auto keyName = attrib.Name();
                auto p = std::make_pair(
                    PlatformRig::KeyId_Make(
                        StringSection<char>((const char*)keyName.begin(), (const char*)keyName.end())),
                    executeString.AsString());
                _table.push_back(p);
            }
        }
    }
    TableOfKeys::~TableOfKeys() {}

    bool    HotKeyInputHandler::OnInputEvent(const PlatformRig::InputContext& context, const PlatformRig::InputSnapshot& evnt)
    {
        static KeyId ctrlKey = KeyId_Make("control");
        if (evnt.IsHeld(ctrlKey)) {
            auto* t = Assets::MakeAsset<TableOfKeys>(MakeStringSection(_filename))->TryActualize();
            if (t) {
                for (auto i=(*t)->GetTable().cbegin(); i!=(*t)->GetTable().cend(); ++i) {
                    if (evnt.IsPress(i->first)) {
                        ConsoleRig::Console::GetInstance().Execute(i->second);
                        return true;
                    }
                }
            }
        }

        return false;
    }

    std::unique_ptr<IInputListener> MakeHotKeysHandler(StringSection<> filename)
    {
        return std::make_unique<HotKeyInputHandler>(filename);
    }

}

