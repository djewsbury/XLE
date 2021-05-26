// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PlatformRigUtil.h"
#include "FrameRig.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Format.h"
#include "../ConsoleRig/Console.h"
#include "../ConsoleRig/IncludeLUA.h"
#include "../Utility/ParameterBox.h"
#include "../Utility/BitUtils.h"
#include "../Math/Transformations.h"
#include "../Math/ProjectionMath.h"
#include "../ConsoleRig/IncludeLUA.h"
#include <cfloat>
#include <unordered_map>

namespace PlatformRig
{
    class ScriptInterface::Pimpl
    {
    public:
        struct TechniqueContextBinder
        {
            void SetInteger(const char name[], uint32 value)
            {
                auto l = _real.lock();
                if (!l)
                    Throw(std::runtime_error("C++ object has expired"));
                l->_globalEnvironmentState.SetParameter((const utf8*)name, value);
            }
            std::weak_ptr<RenderCore::Techniques::TechniqueContext> _real;
            TechniqueContextBinder(std::weak_ptr<RenderCore::Techniques::TechniqueContext> real) : _real(std::move(real)) {}
        };

        struct FrameRigBinder
        {
            void SetFrameLimiter(unsigned maxFPS)
            {
                auto l = _real.lock();
                if (!l)
                    Throw(std::runtime_error("C++ object has expired"));
                l->SetFrameLimiter(maxFPS);
            }
            std::weak_ptr<FrameRig> _real;
            FrameRigBinder(std::weak_ptr<FrameRig> real) : _real(std::move(real)) {}
        };

        std::unordered_map<std::string, std::unique_ptr<TechniqueContextBinder>> _techniqueBinders;
        std::unordered_map<std::string, std::unique_ptr<FrameRigBinder>> _frameRigs;
    };

    void ScriptInterface::BindTechniqueContext(
        const std::string& name,
        std::shared_ptr<RenderCore::Techniques::TechniqueContext> techContext)
    {
        auto binder = std::make_unique<Pimpl::TechniqueContextBinder>(techContext);

        using namespace luabridge;
        auto* luaState = ConsoleRig::Console::GetInstance().GetLuaState();
        setGlobal(luaState, binder.get(), name.c_str());
        _pimpl->_techniqueBinders.insert(std::make_pair(name, std::move(binder)));
    }

    void ScriptInterface::BindFrameRig(const std::string& name, std::shared_ptr<FrameRig> frameRig)
    {
        auto binder = std::make_unique<Pimpl::FrameRigBinder>(frameRig);

        using namespace luabridge;
        auto* luaState = ConsoleRig::Console::GetInstance().GetLuaState();
        setGlobal(luaState, binder.get(), name.c_str());
        _pimpl->_frameRigs.insert(std::make_pair(name, std::move(binder)));
    }

    ScriptInterface::ScriptInterface() 
    {
        _pimpl = std::make_unique<Pimpl>();

        using namespace luabridge;
        auto* luaState = ConsoleRig::Console::GetInstance().GetLuaState();
        getGlobalNamespace(luaState)
            .beginClass<Pimpl::FrameRigBinder>("FrameRig")
                .addFunction("SetFrameLimiter", &Pimpl::FrameRigBinder::SetFrameLimiter)
            .endClass();

        getGlobalNamespace(luaState)
            .beginClass<Pimpl::TechniqueContextBinder>("TechniqueContext")
                .addFunction("SetI", &Pimpl::TechniqueContextBinder::SetInteger)
            .endClass();
    }

    ScriptInterface::~ScriptInterface() 
    {
        auto* luaState = ConsoleRig::Console::GetInstance().GetLuaState();
        for (const auto& a:_pimpl->_techniqueBinders) {
            lua_pushnil(luaState);
            lua_setglobal(luaState, a.first.c_str());
        }

        for (const auto& a:_pimpl->_frameRigs) {
            lua_pushnil(luaState);
            lua_setglobal(luaState, a.first.c_str());
        }
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    void ResizePresentationChain::OnResize(unsigned newWidth, unsigned newHeight)
    {
		auto chain = _presentationChain.lock();
        if (chain) {
                //  When we become an icon, we'll end up with zero width and height.
                //  We can't actually resize the presentation to zero. And we can't
                //  delete the presentation chain from here. So maybe just do nothing.
            if (newWidth && newHeight) {
				chain->Resize(newWidth, newHeight);
            }
        }

        _onResize.Invoke(newWidth, newHeight);
    }

    ResizePresentationChain::ResizePresentationChain(
		const std::shared_ptr<RenderCore::IPresentationChain>& presentationChain)
    : _presentationChain(presentationChain)
    {}
}
