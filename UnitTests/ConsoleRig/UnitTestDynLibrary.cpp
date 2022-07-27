// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CrossModuleTestHelper.h"
#include "../../OSServices/AttachableLibrary.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../OSServices/Log.h"

// We can force the compiler to give us a simple name for a dll export with 
// some syntax trickery.
//
// In clang, we can use 'asm("NewName")' after a function declaration (but it has
// to be a signature declaration, it can't be the implementation itself)
// For MSVC, we can use `#pragma comment(linker, "/EXPORT:NewName=" __FUNCDNAME__)`
// but it must be within the function implementation.
//
// It's possible either compiler/linker might be able to emulate the other with
// specific flags... Otherwise the syntax is awkwardly different between the compilers
// This is also pretty critical (within falling back to extern "C" stuff) because
// the name mangling won't be guaranteed to be the same, anyway...
//
dll_export std::string ExampleFunctionReturnsString(std::string acrossInterface) asm("ExampleFunctionReturnsString");
dll_export std::string ExampleFunctionReturnsString(std::string acrossInterface)
{
	return "This is a string from ExampleFunctionReturnsString <<" + acrossInterface + ">>";
}

static ConsoleRig::AttachablePtr<UnitTests::SingletonSharedFromAttachedModule> s_singletonToPublish;
static ConsoleRig::AttachablePtr<UnitTests::SingletonSharedFromMainModule2> s_embuedByMainModule2;
static ConsoleRig::WeakAttachablePtr<UnitTests::SingletonSharedFromMainModule3> s_embuedByMainModule3;

dll_export std::string FunctionCheckingAttachablePtrs() asm("FunctionCheckingAttachablePtrs");
dll_export std::string FunctionCheckingAttachablePtrs()
{
	s_singletonToPublish = std::make_shared<UnitTests::SingletonSharedFromAttachedModule>();

	ConsoleRig::AttachablePtr<UnitTests::SingletonSharedFromMainModule1> embuedByMainModule1;
	return embuedByMainModule1->_identifyingString + " and " + s_embuedByMainModule2->_identifyingString + " and " + s_embuedByMainModule3.lock()->_identifyingString;
}

dll_export std::string CheckWeakAttachable() asm("CheckWeakAttachable");
dll_export std::string CheckWeakAttachable()
{
	if (s_embuedByMainModule3.lock() != nullptr)
		return "Still have value";
	return "No longer have value";
}

extern "C" 
{
	dll_export OSServices::LibVersionDesc GetVersionInformation()
	{
		return ConsoleRig::GetLibVersionDesc();
	}

	static ConsoleRig::AttachablePtr<ConsoleRig::GlobalServices> s_globalServicesAttachRef;

	dll_export void AttachLibrary(ConsoleRig::CrossModule& crossModule)
	{
		assert(s_globalServicesAttachRef != nullptr);
		auto versionDesc = ConsoleRig::GetLibVersionDesc();
		Log(Verbose) << "Attached unit test DLL: {" << versionDesc._versionString << "} -- {" << versionDesc._buildDateString << "}" << std::endl;
	}

	dll_export void DetachLibrary()
	{
	}
}
