// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../ShaderParser/ShaderInstantiation.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../OSServices/AttachableLibrary.h"
#include "../../OSServices/Log.h"
#include <regex>

#if COMPILER_ACTIVE == COMPILER_TYPE_CLANG
	dll_export std::string AntiStrippingReferences() asm("AntiStrippingReferences");
#endif
dll_export void AntiStrippingReferences()
{
	#if COMPILER_ACTIVE == COMPILER_TYPE_MSVC
		#pragma comment(linker, "/EXPORT:AntiStrippingReferences=" __FUNCDNAME__)
	#endif

	GraphLanguage::INodeGraphProvider::NodeGraph graph;
	auto result = ShaderSourceParser::InstantiateShader(graph, false, ShaderSourceParser::InstantiationRequest{}, ShaderSourceParser::GenerateFunctionOptions{});
	(void)result;

	std::regex r{".*\.dll"};
	(void)r;
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

