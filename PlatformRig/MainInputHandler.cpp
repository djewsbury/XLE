// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MainInputHandler.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../Utility/StringUtils.h"
#include "../Utility/MemoryUtils.h"
#include <assert.h>

using namespace PlatformRig::Literals;

namespace PlatformRig
{
    ProcessInputResult    MainInputHandler::OnInputEvent(const InputContext& context, const OSServices::InputSnapshot& evnt)
    {
        for (auto i=_listeners.cbegin(); i!=_listeners.cend(); ++i) {
            auto c = (*i)->OnInputEvent(context, evnt);
            if (c != ProcessInputResult::Passthrough)
                return c;
        }
        return ProcessInputResult::Passthrough;
    }

    void    MainInputHandler::AddListener(std::shared_ptr<IInputListener> listener)
    {
        assert(listener);
        _listeners.push_back(std::move(listener));
    }

    void    MainInputHandler::RemoveListened(IInputListener& listener)
    {
        for (auto i=_listeners.begin(); i!=_listeners.end(); ++i)
            if (i->get() == &listener) {
                _listeners.erase(i);
                return;
            }
    }

    MainInputHandler::MainInputHandler()
    {}

    MainInputHandler::~MainInputHandler() {}


    void* InputContext::GetService(uint64_t id) const
    {
        auto i = LowerBound(_services, id);
		if (i != _services.end() && i->first == id)
			return i->second;
		return nullptr;
    }

    void InputContext::AttachService(uint64_t id, void* ptr)
    {
        auto i = LowerBound(_services, id);
		if (i != _services.end() && i->first == id) {
			i->second = ptr;
		} else {
			_services.emplace_back(id, ptr);
		}
    }

    InputContext::InputContext() = default;
    InputContext::~InputContext() = default;
}

