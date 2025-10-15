// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Console.h"
#include "Plugins.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/IteratorUtils.h"
#include "../Math/Vector.h"
#include "../Core/Exceptions.h"
#include <iterator>
#include <algorithm>

#pragma GCC diagnostic ignored "-Wundefined-bool-conversion"

namespace ConsoleRig
{

			//////   C O R E   C O N S O L E   B E H A V I O U R   //////

	class Console::Pimpl
	{
	public:
		std::vector<std::basic_string<ucs2>> _lines;
		bool _lastLineComplete;
		std::shared_ptr<ConsoleVariableStorage> _cvars;

		std::mutex _scriptingInterfaceMutex;
		std::vector<std::shared_ptr<IConsoleScriptingInterface>> _scriptingInterfaces;
	};

	Console*        Console::s_instance = nullptr;

	void            Console::Execute(const std::string& str)
	{
		if (!_pimpl->_lastLineComplete) Print("\n");
		Print(Concatenate("{Color:af3f7f}> {Color:7F7F7F}", str, "\n"));
		if (auto L = LockScriptingState(); L._interface) {
			TRY {
				L._interface->Execute(str);
			} CATCH(std::exception& e) {
				if (!_pimpl->_lastLineComplete) Print("\n");
				Print(Concatenate(e.what(), "\n"));
			} CATCH_END
		} else
			Print("No scripting interface available\n");
	}

	std::vector<std::string>    Console::AutoComplete(const std::string& input)
	{
		if (auto L = LockScriptingState(); L._interface)
			return L._interface->AutoComplete(input);
		return {};
	}

	static std::basic_string<ucs2>      AsUTF16(const std::string& input)
	{
		ucs2 buffer[1024];
		utf8_2_ucs2((utf8*)AsPointer(input.begin()), input.size(), buffer, dimof(buffer));
		return std::basic_string<ucs2>(buffer);
	}

	static std::basic_string<ucs2>      AsUTF16(const char input[], size_t len)
	{
		ucs2 buffer[1024];
		utf8_2_ucs2((utf8*)input, len, buffer, dimof(buffer));
		return std::basic_string<ucs2>(buffer);
	}

	void            Console::Print(StringSection<> message)
	{
		if (!this) return;  // hack!
		Print(AsUTF16(message.begin(), message.size()));
	}

	void            Console::Print(const std::basic_string<ucs2>& message)
	{
		if (!this) return;  // hack!
		std::basic_string<ucs2>::size_type currentOffset = 0;
		std::basic_string<ucs2>::size_type stringLength = message.size();
		bool lastLineComplete = _pimpl->_lastLineComplete;

		while (currentOffset < stringLength) {
			const std::basic_string<ucs2>::size_type start = currentOffset;
			const std::basic_string<ucs2>::size_type s = message.find_first_of((ucs2*)u"\r\n", currentOffset);
			std::basic_string<ucs2>::size_type end;
			bool completeLine = false;

			if (s != std::string::npos) {
				end = s;
				completeLine = true;
			} else {
				end = message.size();
			}

			if (end > start) {
				if (!lastLineComplete && !_pimpl->_lines.empty()) {
					_pimpl->_lines[_pimpl->_lines.size()-1] += message.substr(start, end-start);
				} else {
					_pimpl->_lines.push_back(message.substr(start, end-start));
				}
			}
			lastLineComplete = completeLine;

			currentOffset = end;
			while (currentOffset < stringLength && (message[currentOffset]=='\r'||message[currentOffset]=='\n')) {
				++currentOffset;
			}
		}

		_pimpl->_lastLineComplete = lastLineComplete;
	}

	std::vector<std::basic_string<ucs2>>    Console::GetLines(unsigned lineCount, unsigned scrollback)
	{
		std::vector<std::basic_string<ucs2>> result;
		signed linesToGet = std::max(0, std::min(signed(lineCount), signed(_pimpl->_lines.size())-signed(scrollback)));
		result.reserve(linesToGet);

		if (linesToGet <= 0) {
			return result;
		}

		std::copy(
			_pimpl->_lines.end() - scrollback - linesToGet, _pimpl->_lines.end() - scrollback,
			std::back_inserter(result));

		return result;
	}

	unsigned Console::GetLineCount() const
	{
		return unsigned(_pimpl->_lines.size());
	}

	LockedScriptingState  Console::LockScriptingState()
	{
		LockedScriptingState result;
		result._lock = std::unique_lock<Threading::Mutex>{_pimpl->_scriptingInterfaceMutex};
		if (!_pimpl->_scriptingInterfaces.empty()) {
			result._interface = _pimpl->_scriptingInterfaces.back().get();
		} else
			result._interface = nullptr;
		return result;
	}

	ConsoleVariableStorage& Console::GetCVars() { return *_pimpl->_cvars; }
	std::shared_ptr<ConsoleVariableStorage> Console::GetCVarsPtr() { return _pimpl->_cvars; }

	void Console::SetInstance(Console* newInstance)
	{
		assert(!s_instance || !newInstance);
		s_instance = newInstance;
	}

	void Console::PushScriptingInterface(std::shared_ptr<IConsoleScriptingInterface> interf)
	{
		ScopedLock(_pimpl->_scriptingInterfaceMutex);
		_pimpl->_scriptingInterfaces.push_back(std::move(interf));
	}

	void Console::PopScriptingInterface(IConsoleScriptingInterface& interf)
	{
		ScopedLock(_pimpl->_scriptingInterfaceMutex);
		assert(_pimpl->_scriptingInterfaces.back().get() == &interf);
		_pimpl->_scriptingInterfaces.pop_back();
	}

	Console::Console()  
	{
		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_lastLineComplete = false;
		_pimpl->_lines.push_back(std::basic_string<ucs2>());
		_pimpl->_cvars = std::make_shared<ConsoleVariableStorage>();

		assert(!s_instance);
		s_instance = this;
	}

	Console::~Console() 
	{
		_pimpl->_cvars.reset();
		_pimpl.reset();
		assert(s_instance==this);
		s_instance = nullptr;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		template <typename Type>
			ConsoleVariableStorage::Table<Type>& GetConsoleVariableTable()
		{
			return Console::GetInstance().GetCVars().GetTable<Type>();
		}

		template <typename Type>
			class CompareConsoleVariable 
		{
		public:
			typedef std::pair<Type, ConsoleVariable<Type>> Pair;
			bool operator()(const char lhs[], const std::unique_ptr<Pair>& rhs) const      { return XlCompareString(lhs, rhs->second.Name().c_str()) < 0; }
			bool operator()(const std::unique_ptr<Pair>& lhs, const char rhs[]) const      { return XlCompareString(lhs->second.Name().c_str(), rhs) < 0; }
		};

		#undef new

			template <typename Type>
				Type&       FindTweakable(const char name[], Type defaultValue)
			{
				auto& table  = GetConsoleVariableTable<Type>();
				auto i       = std::lower_bound(table.cbegin(), table.cend(), name, CompareConsoleVariable<Type>());
				if (i!=table.cend() && XlEqString((*i)->second.Name(), name))
					return (*i)->first;

				using Pair = std::pair<Type, ConsoleVariable<Type>>;
				// This bit of funkiness is because we want the ConsoleVariable object to contain
				// a pointer to the value object (which is contained in the same heap block)
				// It's awkward here, but it's convenient otherwise
				auto p = std::make_unique<Pair>(defaultValue, ConsoleVariable<Type>());
				ConsoleVariable<Type>& var = std::get<1>(*p);
				var.~ConsoleVariable<Type>();
				new(&var) ConsoleVariable<Type>(name, std::get<0>(*p));

				i = table.insert(i, std::move(p));
				return (*i)->first;
			}

		#if defined(DEBUG_NEW)
			#define new DEBUG_NEW
		#endif

		template <typename Type>
			Type*       FindTweakable(const char name[])
		{
					// this version only find an existing tweakable, and returns null if it can't be found
			auto& table  = GetConsoleVariableTable<Type>();
			auto i       = std::lower_bound(table.cbegin(), table.cend(), name, CompareConsoleVariable<Type>());
			if (i!=table.cend() && !XlCompareString((*i)->second.Name().c_str(), name)) {
				return &(*i)->first;
			}
			return nullptr;
		}

		template int&           FindTweakable<int>(const char name[], int defaultValue);
		template float&         FindTweakable<float>(const char name[], float defaultValue);
		template std::string&   FindTweakable<std::string>(const char name[], std::string defaultValue);
		template bool&          FindTweakable<bool>(const char name[], bool defaultValue);
		template Float3&        FindTweakable<Float3>(const char name[], Float3 defaultValue);
		template Float4&        FindTweakable<Float4>(const char name[], Float4 defaultValue);

		template int*           FindTweakable<int>(const char name[]);
		template float*         FindTweakable<float>(const char name[]);
		template std::string*   FindTweakable<std::string>(const char name[]);
		template bool*          FindTweakable<bool>(const char name[]);
		template Float3*        FindTweakable<Float3>(const char name[]);
		template Float4*        FindTweakable<Float4>(const char name[]);
	}

			//////   C O N S O L E   V A R I A B L E   H E L P E R   //////

	template <typename Type>
		ConsoleVariable<Type>::ConsoleVariable(const std::string& name, Type& attachedValue)
	:   _name(name)
	,   _attachedValue(&attachedValue)
	{}

	template <typename Type>
		ConsoleVariable<Type>::ConsoleVariable() {}

	template <typename Type>
		ConsoleVariable<Type>::~ConsoleVariable() {}

	template <typename Type>
		ConsoleVariable<Type>::ConsoleVariable(ConsoleVariable&& moveFrom)
	:       _name(std::move(moveFrom._name))
	,       _attachedValue(std::move(moveFrom._attachedValue))
	{}

	template <typename Type>
		ConsoleVariable<Type>& ConsoleVariable<Type>::operator=(ConsoleVariable<Type>&& moveFrom)
	{
		_name           = std::move(moveFrom._name);
		_attachedValue  = std::move(moveFrom._attachedValue);
		return *this;
	}

	template class ConsoleVariable<int>;
	template class ConsoleVariable<float>;
	template class ConsoleVariable<std::string>;
	template class ConsoleVariable<bool>;
	template class ConsoleVariable<Float3>;
	template class ConsoleVariable<Float4>;

	IConsoleScriptingInterface::~IConsoleScriptingInterface() = default;
	IStartupShutdownPlugin::~IStartupShutdownPlugin() {}

}

