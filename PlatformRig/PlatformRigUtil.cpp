// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PlatformRigUtil.h"
#include "FrameRig.h"
#include "InputContext.h"
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
            void SetInteger(const char name[], uint32_t value)
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
        auto luaState = ConsoleRig::Console::GetInstance().LockLuaState();
        setGlobal(luaState.GetLuaState(), binder.get(), name.c_str());
        _pimpl->_techniqueBinders.insert(std::make_pair(name, std::move(binder)));
    }

    void ScriptInterface::BindFrameRig(const std::string& name, std::shared_ptr<FrameRig> frameRig)
    {
        auto binder = std::make_unique<Pimpl::FrameRigBinder>(frameRig);

        using namespace luabridge;
        auto luaState = ConsoleRig::Console::GetInstance().LockLuaState();
        setGlobal(luaState.GetLuaState(), binder.get(), name.c_str());
        _pimpl->_frameRigs.insert(std::make_pair(name, std::move(binder)));
    }

    ScriptInterface::ScriptInterface() 
    {
        _pimpl = std::make_unique<Pimpl>();

        using namespace luabridge;
        auto luaState = ConsoleRig::Console::GetInstance().LockLuaState();
        getGlobalNamespace(luaState.GetLuaState())
            .beginClass<Pimpl::FrameRigBinder>("FrameRig")
            .endClass();

        getGlobalNamespace(luaState.GetLuaState())
            .beginClass<Pimpl::TechniqueContextBinder>("TechniqueContext")
                .addFunction("SetI", &Pimpl::TechniqueContextBinder::SetInteger)
            .endClass();
    }

    ScriptInterface::~ScriptInterface() 
    {
        auto luaState = ConsoleRig::Console::GetInstance().LockLuaState();
        for (const auto& a:_pimpl->_techniqueBinders) {
            lua_pushnil(luaState.GetLuaState());
            lua_setglobal(luaState.GetLuaState(), a.first.c_str());
        }

        for (const auto& a:_pimpl->_frameRigs) {
            lua_pushnil(luaState.GetLuaState());
            lua_setglobal(luaState.GetLuaState(), a.first.c_str());
        }
    }


    InputContext InputContextForSubView(
        const InputContext& superViewContext,
        Coord2 subViewMins, Coord2 subViewMaxs)
    {
        PlatformRig::InputContext subContext = superViewContext;
        auto v = superViewContext._view;
        subContext._view = {
            { std::max(v._viewMins[0], subViewMins[0]), std::max(v._viewMins[1], subViewMins[1]) },
            { std::min(v._viewMaxs[0], subViewMaxs[0]), std::min(v._viewMaxs[1], subViewMaxs[1]) },
        };
        return subContext;
    }

}
