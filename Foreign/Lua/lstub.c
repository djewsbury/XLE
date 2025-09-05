
#include "src/lua.h"
#include <stdlib.h>

// stubbing out unnecessary base libraries

LUAMOD_API int luaopen_package (lua_State *L)
{
	return 0;
}

LUAMOD_API int luaopen_coroutine (lua_State *L)
{
	return 0;
}

LUAMOD_API int luaopen_io (lua_State *L)
{
	return 0;
}

LUAMOD_API int luaopen_os (lua_State *L)
{
	return 0;
}

LUAMOD_API int luaopen_debug (lua_State *L)
{
	return 0;
}
