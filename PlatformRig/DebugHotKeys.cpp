// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DebugHotKeys.h"
#include "InputContext.h"
#include "../Tools/EntityInterface/MountedData.h"
#include "../Formatters/FormatterUtils.h"
#include "../Assets/Assets.h"
#include "../ConsoleRig/Console.h"

using namespace OSServices::Literals;

namespace PlatformRig
{
	struct TableOfKeys
	{
		std::vector<std::pair<uint32_t, std::string>> _table;

		template<typename Formatter>
			TableOfKeys(Formatter& formatter)
		{
			StringSection kn;
			while (formatter.TryKeyedItem(kn))
				_table.emplace_back(OSServices::KeyId_Make(kn), Formatters::RequireStringValue(formatter).AsString());
		}

		TableOfKeys() = default;
	};

	class HotKeyInputHandler : public IInputListener
	{
	public:
		ProcessInputResult    OnInputEvent(const InputContext& context, const OSServices::InputSnapshot& evnt)
		{
			constexpr auto ctrlKey = "control"_key;
			if (evnt.IsHeld(ctrlKey))
				for (auto& key:EntityInterface::MountedData<TableOfKeys>::LoadOrDefault(_filename)._table)
					if (evnt.IsPress(key.first)) {
						ConsoleRig::Console::GetInstance().Execute(key.second);
						return ProcessInputResult::Consumed;
					}

			return ProcessInputResult::Passthrough;
		}

		HotKeyInputHandler(StringSection<> filename) : _filename(filename.AsString()) {}
	protected:
		::Assets::rstring _filename;
	};

	std::unique_ptr<IInputListener> MakeHotKeysHandler(StringSection<> filename) { return std::make_unique<HotKeyInputHandler>(filename); }
}

