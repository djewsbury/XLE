// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../ConsoleRig/Console.h"

typedef struct lua_State lua_State;

namespace PlatformRig
{
	std::shared_ptr<ConsoleRig::IConsoleScriptingInterface> CreateLuaScripting(std::shared_ptr<ConsoleRig::ConsoleVariableStorage>);

	class ILuaScriptInterface
	{
	public:
		virtual lua_State* GetLuaState() = 0;
	};
}

