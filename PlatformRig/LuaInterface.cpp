#include "LuaInterface.h"
#include "../Math/Vector.h"
#include "../Core/Exceptions.h"

#if XLE_CONSOLE_LUA_ENABLE
#define _SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS		// LuaBridge uses hash_map, which creates a compile error in Visual Studio 2015. We should use the standard unordered_map, instead
#undef new

	#include <lua.hpp>
	#include <LuaBridge.h>

#if defined(DEBUG_NEW)
	#define new DEBUG_NEW
#endif
#endif

namespace PlatformRig
{
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if XLE_CONSOLE_LUA_ENABLE

	class LuaState
	{
	public:
		lua_State* L;
		lua_State* GetUnderlying() { return L; }

		int PCall(int argumentCount, int returnValueCount);
	
		LuaState();
		LuaState(lua_State& existing);
		~LuaState();

	private:
		static void*    AllocationBridge(void *userData, void *ptr, size_t osize, size_t nsize);
		static int      PanicBridge(lua_State* L);
		static int      ErrorHandler(lua_State* L);
		static void*    GetTracebackKey();
		static int      Print(lua_State* L);
		bool _closeOnExit = true;
	};

	static const char* s_consoleVariableBridgeMetatable = "Meta-LuaConsoleVariableBridge";

	class LuaConsoleVariableBridge
	{
	public:
		struct ClosureParams { LuaConsoleVariableBridge* _bridge; ClosureParams(LuaConsoleVariableBridge* bridge) : _bridge(bridge) {} };

		LuaConsoleVariableBridge(std::shared_ptr<ConsoleRig::ConsoleVariableStorage> cvars, lua_State* iL)
		: _cvars(std::move(cvars)), L(iL)
		{
			DEBUG_ONLY(int stackSizeStart2 = lua_gettop(L));

			lua_pushglobaltable(L);
			assert (lua_istable (L, -1));

			new (lua_newuserdata (L, sizeof (ClosureParams))) ClosureParams (this);

			luaL_newmetatable(L, s_consoleVariableBridgeMetatable);
			luaL_setfuncs(L, s_container_meta, 0);		// register our funcs in the metatable
			lua_setmetatable (L, -2);

			lua_setfield(L, -2, "cv");
			lua_pop(L, 1);

			DEBUG_ONLY(int stackSizeEnd2 = lua_gettop(L));
			assert(stackSizeEnd2 == stackSizeStart2);
		}

		~LuaConsoleVariableBridge()
		{
			lua_pushglobaltable(L);
			lua_getfield(L, -1, "cv");
			lua_pop(L, 1);
			lua_pushnil(L);
			lua_setfield(L, -2, "cv");
			lua_pop(L, 1);
		}

	private:
		std::shared_ptr<ConsoleRig::ConsoleVariableStorage> _cvars;
		lua_State* L;

		static int indexMethod(lua_State* L)
		{
			auto* params = (ClosureParams*)luaL_checkudata(L, 1, s_consoleVariableBridgeMetatable);
			assert(params && params->_bridge);
			auto& bridge = *params->_bridge;

			assert(lua_isstring(L, 2));
			auto name = lua_tostring(L, 2);

			for (const auto& ints:bridge._cvars->GetTable<int>())
				if (XlEqString(ints->second.Name(), name)) {
					lua_pushinteger(L, ints->first);
					return 1;
				}

			for (const auto& floats:bridge._cvars->GetTable<float>())
				if (XlEqString(floats->second.Name(), name)) {
					lua_pushnumber(L, floats->first);
					return 1;
				}

			for (const auto& strings:bridge._cvars->GetTable<std::string>())
				if (XlEqString(strings->second.Name(), name)) {
					lua_pushstring(L, strings->first.c_str());
					return 1;		// number of values returned
				}

			for (const auto& bools:bridge._cvars->GetTable<bool>())
				if (XlEqString(bools->second.Name(), name)) {
					lua_pushboolean(L, bools->first);
					return 1;
				}

			for (const auto& v3ds:bridge._cvars->GetTable<Float3>())
				if (XlEqString(v3ds->second.Name(), name)) {
					lua_pushnil(L);	// todo -- complex type required
					return 1;
				}

			for (const auto& v4ds:bridge._cvars->GetTable<Float4>())
				if (XlEqString(v4ds->second.Name(), name)) {
					lua_pushnil(L);	// todo -- complex type required
					return 1;
				}

			lua_pushnil(L);
			return 1;
		}

		static int newIndexMethod(lua_State* L)
		{
			auto* params = (ClosureParams*)luaL_checkudata(L, 1, s_consoleVariableBridgeMetatable);
			assert(params && params->_bridge);
			auto& bridge = *params->_bridge;

			assert(lua_isstring(L, 2));
			auto name = lua_tostring(L, 2);

			for (const auto& ints:bridge._cvars->GetTable<int>())
				if (XlEqString(ints->second.Name(), name)) {
					int isnum;
					auto result = lua_tointegerx(L, 3, &isnum);
					if (!isnum) Throw(std::runtime_error(Concatenate("Attempting to assign non-integer value (", lua_tostring(L, 3), ") to integer cvar (", name, ")")));

					ints->first = (int)result;
					return 0;
				}

			for (const auto& floats:bridge._cvars->GetTable<float>())
				if (XlEqString(floats->second.Name(), name)) {
					int isnum;
					auto result = lua_tonumberx(L, 3, &isnum);
					if (!isnum) Throw(std::runtime_error(Concatenate("Attempting to assign non-number value (", lua_tostring(L, 3), ") to float cvar (", name, ")")));

					floats->first = (float)result;
					return 0;
				}

			for (const auto& strings:bridge._cvars->GetTable<std::string>())
				if (XlEqString(strings->second.Name(), name)) {
					strings->first = lua_tostring(L, 3);
					return 0;
				}

			for (const auto& bools:bridge._cvars->GetTable<bool>())
				if (XlEqString(bools->second.Name(), name)) {
					bools->first = lua_toboolean(L, 3);
					return 0;
				}

			for (const auto& v3ds:bridge._cvars->GetTable<Float3>())
				if (XlEqString(v3ds->second.Name(), name)) {
					// todo -- complex type required
					assert(0);
					return 0;
				}

			for (const auto& v4ds:bridge._cvars->GetTable<Float4>())
				if (XlEqString(v4ds->second.Name(), name)) {
					// todo -- complex type required
					assert(0);
					return 0;
				}

			return 0;
		}

		static int ContainerIterator(lua_State* L)
		{
			auto* params = (ClosureParams*)luaL_checkudata(L, 1, s_consoleVariableBridgeMetatable);
			assert(params && params->_bridge);
			auto& bridge = *params->_bridge;

			const char* search = nullptr;

			if (!lua_isnil(L, 2))
				search = lua_tostring(L, 2);

			for (const auto& ints:bridge._cvars->GetTable<int>())
				if (!search) {
					lua_pushstring(L, ints->second.Name().c_str());
					lua_pushinteger(L, ints->first);
					return 2;		// number of values returned
				} else if (XlEqString(ints->second.Name(), search)) {
					search = nullptr;
				}

			for (const auto& floats:bridge._cvars->GetTable<float>())
				if (!search) {
					lua_pushstring(L, floats->second.Name().c_str());
					lua_pushnumber(L, floats->first);
					return 2;		// number of values returned
				} else if (XlEqString(floats->second.Name(), search)) {
					search = nullptr;
				}

			for (const auto& strings:bridge._cvars->GetTable<std::string>())
				if (!search) {
					lua_pushstring(L, strings->second.Name().c_str());
					lua_pushstring(L, strings->first.c_str());
					return 2;		// number of values returned
				} else if (XlEqString(strings->second.Name(), search)) {
					search = nullptr;
				}

			for (const auto& bools:bridge._cvars->GetTable<bool>())
				if (!search) {
					lua_pushstring(L, bools->second.Name().c_str());
					lua_pushboolean(L, bools->first);
					return 2;		// number of values returned
				} else if (XlEqString(bools->second.Name(), search)) {
					search = nullptr;
				}

			for (const auto& v3ds:bridge._cvars->GetTable<Float3>())
				if (!search) {
					lua_pushstring(L, v3ds->second.Name().c_str());
					lua_pushnil(L);	// todo -- complex type required
					return 2;		// number of values returned
				} else if (XlEqString(v3ds->second.Name(), search)) {
					search = nullptr;
				}

			for (const auto& v4ds:bridge._cvars->GetTable<Float4>())
				if (!search) {
					lua_pushstring(L, v4ds->second.Name().c_str());
					lua_pushnil(L);	// todo -- complex type required
					return 2;		// number of values returned
				} else if (XlEqString(v4ds->second.Name(), search)) {
					search = nullptr;
				}

			lua_pushnil(L);
			return 1;
		}

		static int pairsMethod (lua_State* L)
		{
			luaL_checkudata(L, 1, s_consoleVariableBridgeMetatable);
			lua_pushcfunction(L, ContainerIterator);
			lua_pushvalue(L, 1);
			lua_pushnil(L);
			return 3;		// We are returning 3 values.
		}

		static int gcMethod(lua_State* L)
		{
			auto* container = (ClosureParams*)luaL_checkudata(L, 1, s_consoleVariableBridgeMetatable);
			container->~ClosureParams();
			return 0;
		}

		static const struct luaL_Reg s_container_meta[];
	};

	const struct luaL_Reg LuaConsoleVariableBridge::s_container_meta[] = {
		{"__pairs", LuaConsoleVariableBridge::pairsMethod},
		{"__index", LuaConsoleVariableBridge::indexMethod},
		{"__newindex", LuaConsoleVariableBridge::newIndexMethod},
		{"__gc", LuaConsoleVariableBridge::gcMethod},
		{nullptr, nullptr}
	};

#if 0
	namespace Internal
	{
		template <typename MemFn, typename D=MemFn> struct ImmMemberFunction {};

		template <typename R, typename E, typename D>
			struct ImmMemberFunction <R (*) (E), D>
		{
			typedef luabridge::None Params;
			static R call (D fp, E e, luabridge::TypeListValues<Params>)            { return fp(e); }
		};

		template <typename R, typename P1, typename E, typename D>
			struct ImmMemberFunction <R (*) (E, P1), D>
		{
			typedef luabridge::TypeList <P1> Params;
			static R call (D fp, E e, luabridge::TypeListValues<Params> tvl)       { return fp(e, tvl.hd); }
		};

		template <typename R, typename P1, typename P2, typename E, typename D>
			struct ImmMemberFunction <R (*) (E, P1, P2), D>
		{
			typedef luabridge::TypeList <P1, P2> Params;
			static R call (D fp, E e, luabridge::TypeListValues<Params> tvl)       { return fp(e, tvl.hd, tvl.tl.hd); }
		};

		template <typename R, typename P1, typename P2, typename P3, typename E, typename D>
			struct ImmMemberFunction <R (*) (E, P1, P2, P3), D>
		{
			typedef luabridge::TypeList <P1, luabridge::TypeList <P2, luabridge::TypeList <P3> > > Params;
			static R call (D fp, E e, luabridge::TypeListValues<Params> tvl)            { return fp(e, tvl.hd, tvl.tl.hd, tvl.tl.tl.hd); }
		};

		template <typename Type>
			static Type ConsoleVariable_Getter(ConsoleRig::ConsoleVariable<Type>* attachedValue)
		{
			return (*attachedValue->_attachedValue);
		}

		template <typename Type>
			static Type ConsoleVariable_Setter(ConsoleRig::ConsoleVariable<Type>* attachedValue, Type newValue)
		{
			(*attachedValue->_attachedValue) = newValue;
			return *attachedValue->_attachedValue;
		}

		template<   class Type,
					class MemFn,
					class ReturnType = typename luabridge::FuncTraits<MemFn>::ReturnType>
		struct ConsoleVariable_CallFunction
		{
			using T = ConsoleVariable<Type>;
			using Params = typename ImmMemberFunction<MemFn>::Params;
			static int Call(lua_State* L)
			{
				using namespace luabridge;
				assert (lua_isuserdata (L, lua_upvalueindex(1)));
				T*t = (T*)lua_touserdata(L, lua_upvalueindex(1));

				assert (lua_isuserdata (L, lua_upvalueindex (2)));
				MemFn fp = reinterpret_cast<MemFn>(lua_touserdata(L, lua_upvalueindex (2)));

				assert (fp != 0);
				ArgList<Params> args (L);
				Stack<ReturnType>::push(L, ImmMemberFunction<MemFn>::call(fp, t, args));
				return 1;
			}
		};
	}
#endif

	struct MetamethodAwareIteration
	{
		int Next()
		{
			lua_pushvalue(L, _iterFuncIdx);
			lua_pushvalue(L, _stateIdx);
			lua_pushvalue(L, _controlVarIdx);

			if (lua_pcall(L, 2, LUA_MULTRET, 0) != LUA_OK) {
				std::string errMsg = lua_tostring(L, -1);
				lua_settop(L, _controlVarIdx - 1); // Clean up stack
				Throw(std::runtime_error("Error during iteration: " + errMsg));
			}

			int nresults = lua_gettop(L) - _controlVarIdx;
			if (nresults < 2) {
				lua_settop(L, _controlVarIdx);
				return 0;
			}
			
			_lastNResults = nresults;
			return nresults;
		}

		int _lastNResults = 0;

		void Cleanup()
		{
			// call this every loop iteration, after using the results from Next()
			lua_pop(L, _lastNResults-1);
			lua_replace(L, _controlVarIdx);
		}

		MetamethodAwareIteration(lua_State* iL, int tableIdx) : L(iL)
		{
			_initialStackSize = lua_gettop(L);
			_tableIdx = lua_absindex(L, tableIdx);

			if (luaL_getmetafield(L, _tableIdx, "__pairs") != LUA_TNIL) {
				lua_pushvalue(L, _tableIdx); // Push the object itself as the argument
				if (lua_pcall(L, 1, 3, 0) != LUA_OK) {
					std::string errMsg = lua_tostring(L, -2);
					lua_pop(L, 1); // Pop error message
					Throw(std::runtime_error("Error calling __pairs metamethod: " + errMsg));
				}
				// Stack now has: [iterator_func, state, initial_control_var]
			} else {
				// No __pairs metamethod. Use the default behavior.
				lua_getglobal(L, "next");    // Push the standard `next` function
				lua_pushvalue(L, _tableIdx); // Push the table itself as the state
				lua_pushnil(L);              // Push nil as the initial control variable
			}

			_iterFuncIdx = lua_absindex(L, -3);
			_stateIdx = lua_absindex(L, -2);
			_controlVarIdx = lua_absindex(L, -1);
		}

		~MetamethodAwareIteration()
		{
			lua_pop(L, 3);
			int finalStackSize = lua_gettop(L);
			assert(finalStackSize == _initialStackSize);
		}

		lua_State* L;
		int _tableIdx, _iterFuncIdx, _stateIdx, _controlVarIdx;
		int _initialStackSize;
	};

	static std::vector<std::string> CollectAutoCompleteList(lua_State* L, StringSection<> input, size_t iterateStart)
	{
		std::vector<std::string> result;
		size_t compareLength = input.size() - iterateStart;
		if (compareLength) {
			DEBUG_ONLY(int stackSizeStart2 = lua_gettop(L));

			{
				MetamethodAwareIteration iteration{L, -1};
				while (iteration.Next() != 0) {

					size_t length = 0;
					const char* name = lua_tolstring(L, -2, &length);
					if (name && length >= compareLength && !XlComparePrefixI(name, &input[iterateStart], compareLength))
						result.push_back(Concatenate(MakeStringSection(input.begin(), input.begin()+iterateStart), name));

					iteration.Cleanup();
				}
			}

			DEBUG_ONLY(int stackSizeEnd2 = lua_gettop(L));
			assert(stackSizeEnd2 == stackSizeStart2);
		}
		return result;
	}

	class LuaScriptingInterface : public ConsoleRig::IConsoleScriptingInterface, public ILuaScriptInterface
	{
	public:
		void Execute(StringSection<> str) override
		{
			luaL_loadbuffer(L, str.begin(), str.size(), {});

			int errorCode;
			if (_customState) {
				errorCode = _customState->PCall(0, 0);
			} else
				errorCode = lua_pcall(L,0,0,0);

			if (errorCode != LUA_OK) {
				if (const char* msg = lua_tostring(L, -1)) {
					std::string msgCopy = msg;
					lua_pop(L, 1);
					Throw(std::runtime_error(std::move(msg)));
				} else {
					Throw(std::runtime_error("lua error code: " + std::to_string(errorCode)));
				}
			}
		}

		auto AutoComplete(StringSection<> input) -> std::vector<std::string> override
		{
				//
				//      Separate the input string into parts with "." or ":"
				//      these are tables, etc, we should look up
				//
			DEBUG_ONLY(int stackSizeStart2 = lua_gettop(L));
			lua_pushglobaltable(L);
			int tablesPushed = 1;
			auto iterateStart = input.begin();
			for (;;) {
				char search[] = ".:";
				auto nextPart = std::find_first_of(iterateStart, input.end(), search, ArrayEnd(search));
				if (nextPart == input.end()) {
					break;
				}

				auto iterateEnd = nextPart;
				std::string table(iterateStart, iterateEnd);

				lua_getfield(L, -1, table.c_str());
				++tablesPushed;

				if (lua_isnil(L, -1)) {
						// pushed a bad table name. Nothing here.
					lua_pop(L, tablesPushed);
					assert(lua_gettop(L) == stackSizeStart2);
					return std::vector<std::string>();
				}

				iterateStart = nextPart+1;
			}

			auto result = CollectAutoCompleteList(L, input, iterateStart-input.begin());
			lua_pop(L, tablesPushed);
			assert(lua_gettop(L) == stackSizeStart2);
			return result;
		}

		lua_State* GetLuaState() override
		{
			return L;
		}

		LuaScriptingInterface(std::shared_ptr<ConsoleRig::ConsoleVariableStorage> cvars)
		{
			_customState = std::make_shared<LuaState>();
			L = _customState->L;
			_consoleVariableBridge = std::make_shared<LuaConsoleVariableBridge>(cvars, L);

			// HACK --  getting some memory allocation problems across DLL boundaries sometimes
			//          It seems to be resolved if we allocate the first console variable in the
			//          main module.
			// _dummyValue = 1;
			// _dummyVar = ConsoleVariable<int>("dummy", _dummyValue);
		}

		LuaScriptingInterface(lua_State* l, std::shared_ptr<ConsoleRig::ConsoleVariableStorage> cvars)
		: L(l)
		{
			_consoleVariableBridge = std::make_shared<LuaConsoleVariableBridge>(cvars, L);
		}

		~LuaScriptingInterface()
		{
			// _dummyVar = ConsoleVariable<int>(std::string(), _dummyValue);		// force deregister
		}

		lua_State* L;
		std::shared_ptr<LuaState> _customState;
		std::shared_ptr<LuaConsoleVariableBridge> _consoleVariableBridge;

		// int _dummyValue;
		// ConsoleVariable<int> _dummyVar;
	};

	std::shared_ptr<ConsoleRig::IConsoleScriptingInterface> CreateLuaScripting(std::shared_ptr<ConsoleRig::ConsoleVariableStorage> cvars)
	{
		return std::make_shared<LuaScriptingInterface>(std::move(cvars));
	}

	std::shared_ptr<ConsoleRig::IConsoleScriptingInterface> CreateLuaScripting(lua_State* L, std::shared_ptr<ConsoleRig::ConsoleVariableStorage> cvars)
	{
		return std::make_shared<LuaScriptingInterface>(L, std::move(cvars));
	}

			//////   B A S I C   L U A   B E H A V I O U R   //////

	void * LuaState::AllocationBridge(void *userData, void *ptr, size_t osize, size_t nsize)
	{
		(void)userData;  (void)osize;  /* not used */
		if (nsize == 0) {
			free(ptr);
			return NULL;
		} else {
			return realloc(ptr, nsize);
		}
	}

	#pragma warning(disable:4702)   //  warning C4702: unreachable code

	int LuaState::PanicBridge(lua_State* L)
	{
		Throw(std::exception());
		return 0;
	}

	int LuaState::ErrorHandler(lua_State* L)
	{
		lua_Debug ar;
		XlZeroMemory(ar);

		const char* sErr = lua_tostring(L, 1);
		if (sErr) {
			ConsoleRig::Console::GetInstance().Print(std::string("Lua Error: ") + sErr + "\n");
		}

		/*
			DavidJ -- this is causing stack corruption...?
		int stackIndex = 0;
		while (lua_getstack(L, stackIndex++, &ar)) {
			lua_getinfo(L, "lnS", &ar);
			Console::GetInstance().Print(XlDynFormatString("  [%i] %s, (%s: %d)\n", stackIndex-1, ar.name?ar.name:"<null>", ar.source?ar.source:ar.short_src, ar.currentline));
		}
		*/

		return 0;
	}

	int LuaState::Print(lua_State *L)
	{
		// LuaState* const luaState = static_cast <LuaState*> (
		//     lua_touserdata (L, lua_upvalueindex (1)));

		std::string text;
		int n = lua_gettop(L);  /* number of arguments */
		lua_getglobal(L, "tostring");
		for (int i=1; i<=n; i++) {
			const char *s;
			size_t l;
			lua_pushvalue(L, -1);  /* function to be called */
			lua_pushvalue(L, i);   /* value to print */
			lua_call(L, 1, 1);
			s = lua_tolstring(L, -1, &l);  /* get result */
			if (s == NULL) {
				return luaL_error(L,
					LUA_QL("tostring") " must return a string to " LUA_QL("print"));
			}

			if (i>1) {
				text += " ";
			}

			text += std::string(s, l);
			lua_pop(L, 1);  /* pop result */
		}

		ConsoleRig::Console::GetInstance().Print(text);
		return 0;
	}

	static char addressPlacementHolder;
	void* LuaState::GetTracebackKey() { return &addressPlacementHolder; }

	int LuaState::PCall(int argumentCount, int returnValueCount)
	{
			//
			//      Get the error handler function
			//          push LUA_REGISTRYINDEX[getTracebackKey()]
		lua_pushlightuserdata(L, GetTracebackKey());
		lua_rawget(L, LUA_REGISTRYINDEX);

			//  
			//      Move the error handle function to before the parameters 
			//      on the stack
			//
		int errorHandlerStackIndex = -(argumentCount+2);
		lua_insert(L, errorHandlerStackIndex);

		int result = lua_pcall(L, argumentCount, returnValueCount, errorHandlerStackIndex);
		if (result == 0) {
			lua_remove (L, -(returnValueCount+1));
		} else {
			lua_remove (L, -2);
		}
		return result;
	}

	LuaState::LuaState()
	{
		_closeOnExit = true;
		L = lua_newstate(&AllocationBridge, nullptr);
		luaL_openlibs(L);       // (maybe crash now that some standard libraries are disabled)
		lua_atpanic(L, &PanicBridge);

		lua_pushlightuserdata(L, this);
		lua_pushcclosure(L, &LuaState::Print, 1);
		lua_setglobal(L, "print");

			//
			//      Store a pointer to the error handler function
			//      in the lua registry at the key "getTracebackKey()"
			//          LUA_REGISTRYINDEX[getTracebackKey()] = &ErrorHandler
			//
		lua_pushlightuserdata(L, GetTracebackKey());
		lua_pushcclosure(L, &ErrorHandler, 0);
		lua_rawset(L, LUA_REGISTRYINDEX);
	}

	LuaState::LuaState(lua_State& existing)
	{
		_closeOnExit = false;
		L = &existing;
	}

	LuaState::~LuaState()
	{
		if (_closeOnExit)
			lua_close(L);
	}

#else

	std::shared_ptr<ConsoleRig::IConsoleScriptingInterface> CreateLuaScripting(std::shared_ptr<ConsoleRig::ConsoleVariableStorage>) { return nullptr; }

#endif

}
