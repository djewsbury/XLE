// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DebugScreenRegistry.h"
#include "ConsoleRig/AttachablePtr.h"

namespace PlatformRig
{

	class MainDebugScreenRegistry : public IDebugScreenRegistry
	{
	public:
		RegisteredScreenId Register(
            std::string name,
            std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> widget) override
		{
			auto result = _nextRegistrySetId+1;
			_registrySet.push_back(RegisteredScreen{result, name, widget});
            OnRegister.Invoke(name, widget);
			return result;
		}

		void Deregister(RegisteredScreenId setId) override
		{
			for (auto i=_registrySet.begin(); i!=_registrySet.end(); ++i)
				if (i->_id == setId) {
                    auto widget = i->_widget;
					_registrySet.erase(i);
                    OnDeregister.Invoke(*widget);
					break;
				}
		}

        std::vector<std::pair<std::string, std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget>>> EnumerateRegistered() override
        {
            std::vector<std::pair<std::string, std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget>>> result;
            for (auto i=_registrySet.begin(); i!=_registrySet.end(); ++i)
                result.push_back({i->_name, i->_widget});
            return result;
        }

		MainDebugScreenRegistry() {}
		~MainDebugScreenRegistry() {}

        struct RegisteredScreen
        {
            RegisteredScreenId _id;
            std::string _name;
            std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> _widget;
        };

		std::vector<RegisteredScreen> _registrySet;
		RegisteredScreenId _nextRegistrySetId = 1;
	};

	std::shared_ptr<IDebugScreenRegistry> CreateDebugScreenRegistry()
	{
		return std::make_shared<MainDebugScreenRegistry>();
	}

	static ConsoleRig::WeakAttachablePtr<IDebugScreenRegistry> s_debugScreenRegistry;
	IDebugScreenRegistry* GetDebugScreenRegistry()
	{
		return s_debugScreenRegistry.lock().get();
	}

	IDebugScreenRegistry::~IDebugScreenRegistry() {}

}

