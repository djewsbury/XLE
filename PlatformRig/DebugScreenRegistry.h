// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/Marker.h"
#include "../Utility/StringUtils.h"
#include "../Utility/FunctionUtils.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

namespace RenderOverlays { namespace DebuggingDisplay { class IWidget; }}

namespace PlatformRig
{
	class IDebugScreenRegistry
	{
	public:
		using RegisteredScreenId = uint64_t;
		virtual RegisteredScreenId Register(
            std::string name,
            std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget>) = 0;
		virtual void Deregister(RegisteredScreenId) = 0;

        virtual std::vector<std::pair<std::string, std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget>>> EnumerateRegistered() = 0;

        Signal<std::string, std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget>> OnRegister;
        Signal<RenderOverlays::DebuggingDisplay::IWidget&> OnDeregister;

		virtual ~IDebugScreenRegistry();
	};

	std::shared_ptr<IDebugScreenRegistry> CreateDebugScreenRegistry();
	IDebugScreenRegistry* GetDebugScreenRegistry();

    struct DebugScreenRegistration
    {
        DebugScreenRegistration(std::string name, std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> widget)
        : _registrationId(~0u)
        {
            auto* registry = GetDebugScreenRegistry();
            if (registry)
                _registrationId = registry->Register(name, std::move(widget));
        }
        ~DebugScreenRegistration()
        {
            auto* registry = GetDebugScreenRegistry();
            if (registry && _registrationId != ~0u)
                registry->Deregister(_registrationId);
        }
        DebugScreenRegistration() : _registrationId(~0u) {}
        DebugScreenRegistration(DebugScreenRegistration&& moveFrom)
        {
            _registrationId = moveFrom._registrationId;
            moveFrom._registrationId = ~0u;
        }
        DebugScreenRegistration& operator=(DebugScreenRegistration&& moveFrom)
        {
            if (_registrationId != ~0u) {
                auto* registry = GetDebugScreenRegistry();
                if (registry)
                    registry->Deregister(_registrationId);
            }
            _registrationId = moveFrom._registrationId;
            moveFrom._registrationId = ~0u;
            return *this;
        }
        IDebugScreenRegistry::RegisteredScreenId _registrationId = ~0u;
    };
}

