// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CrossModuleTestHelper.h"
#include "../UnitTestHelper.h"
#include "../../OSServices/AttachableLibrary.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../OSServices/Log.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../OSServices/RawFS.h"
#include "../../Utility/StringFormat.h"
#include <stdexcept>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <iostream>

using namespace Catch::literals;
namespace UnitTests
{
	static std::string GetUnitTestLibraryName()
	{
		#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
			return "UnitTestDynLibrary.dll";
		#elif (PLATFORMOS_TARGET == PLATFORMOS_OSX) || (PLATFORMOS_TARGET == PLATFORMOS_IOS)
			return "libUnitTestDynLibrary.dylib";
		#else
			return "libUnitTestDynLibrary.so";
		#endif
	}

	TEST_CASE("DynLibraryBinding-StartupShutdown", "[consoleRig]")
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		Verbose.SetConfiguration(OSServices::MessageTargetConfiguration{"<<configured-template>>"});

		OSServices::AttachableLibrary testLibrary(GetUnitTestLibraryName());
		std::string attachErrorMsg;
		auto tryAttachResult = testLibrary.TryAttach(attachErrorMsg);
		REQUIRE(tryAttachResult == true);

		using FnSig = std::string(*)(std::string);
		auto fn = testLibrary.GetFunction<FnSig>("ExampleFunctionReturnsString");
		REQUIRE(fn);
		auto interfaceTest = (*fn)("Passed Over Interface");
		REQUIRE(interfaceTest == "This is a string from ExampleFunctionReturnsString <<Passed Over Interface>>");
	}

	signed SingletonSharedFromMainModule1::s_aliveCount = 0;
	signed SingletonSharedFromMainModule2::s_aliveCount = 0;

	TEST_CASE("DynLibraryBinding-AttachablePtr", "[consoleRig]")
	{
		// (we don't use globalServices here, but the attachable library checks to ensure it's initialized
		// with something -- so just ensure we have some value for it)
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());

		// We can use attachable ptrs to share references to singleton objects
		// between modules (ie, shared libraries).
		//
		// This isn't as trivial as it may seem at first; particularly when you
		// consider differences in linker behaviour between GNU derived linkers 
		// and microsoft ecosystem linkers.
		//
		// Also, if shared libraries can be explicitly attached and detached, we
		// want to intelligently handle the lifecycles for the singletons involved

		auto attachablePtrBeforeLibraryAttach = ConsoleRig::MakeAttachablePtr<SingletonSharedFromMainModule1>();
		attachablePtrBeforeLibraryAttach->_identifyingString = "ConfiguredBeforeLibraryAttach";

		auto attachablePropagatedAsWeak = ConsoleRig::MakeAttachablePtr<SingletonSharedFromMainModule3>();
		attachablePropagatedAsWeak->_identifyingString = "PropagatedAsWeak";

		ConsoleRig::AttachablePtr<SingletonSharedFromAttachedModule> singletonFromAttachedModule;

		{
			OSServices::AttachableLibrary testLibrary(GetUnitTestLibraryName());
			std::string attachErrorMsg;
			auto tryAttachResult = testLibrary.TryAttach(attachErrorMsg);
			REQUIRE(tryAttachResult == true);

			auto attachablePtrAfterLibraryAttach = ConsoleRig::MakeAttachablePtr<SingletonSharedFromMainModule2>();
			attachablePtrAfterLibraryAttach->_identifyingString = "ConfiguredAfterLibraryAttach";

			using FnSig = std::string(*)();
			auto fn = testLibrary.GetFunction<FnSig>("FunctionCheckingAttachablePtrs");
			auto fnResult = fn();

			//
			// Here's what happened when we called FunctionCheckingAttachablePtrs:
			//   1. singletons SingletonSharedFromMainModule1 and SingletonSharedFromMainModule2 where
			//		published by the main module and captured by the attached module. The function uses
			//		values from those singletons to return to us an identifying value
			//   2. the attached module did not hold a reference on SingletonSharedFromMainModule1, but it
			//		did hold a reference on SingletonSharedFromMainModule2
			//	 3. a new singleton, SingletonSharedFromAttachedModule, was created, and we've can now
			//		use that singleton with the pointer "singletonFromAttachedModule"
			//

			REQUIRE(fnResult == "ConfiguredBeforeLibraryAttach and ConfiguredAfterLibraryAttach and PropagatedAsWeak");
			REQUIRE(SingletonSharedFromMainModule1::s_aliveCount == 1);
			REQUIRE(SingletonSharedFromMainModule2::s_aliveCount == 1);
			REQUIRE(attachablePtrBeforeLibraryAttach->_attachedModuleCount == 2);
			REQUIRE(attachablePtrAfterLibraryAttach->_attachedModuleCount == 2);
			REQUIRE(singletonFromAttachedModule != nullptr);		// should be embued with a value from the attached module

			attachablePropagatedAsWeak = nullptr;
			{
				auto checkWkFn = testLibrary.GetFunction<FnSig>("CheckWeakAttachable");
				auto checkWkFnResult = checkWkFn();
				REQUIRE(checkWkFnResult == "No longer have value");
			}
		}

		//
		// The attached library has been implicitly been detached, and as so all references that it was keeping
		// alive are released.
		// In particular, singletonFromAttachedModule is now automatically cleared out -- since it was published
		// by the attached module, it can't continue to exist safely (eg, if there are any methods on that class,
		// the code for those methods will now have been unloaded)
		//

		REQUIRE(SingletonSharedFromMainModule1::s_aliveCount == 1);
		REQUIRE(SingletonSharedFromMainModule2::s_aliveCount == 0);
		REQUIRE(singletonFromAttachedModule == nullptr);			// automatically reset to null when the attached module was detached
	}

}

