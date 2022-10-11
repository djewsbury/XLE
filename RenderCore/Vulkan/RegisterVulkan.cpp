// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

// This is a silly way to do it -- but we really only care about this in the top level executable/library that will be generated
// We push this into the cmake "interface" feature, because it will be stripped by the linker otherwise
// However, it will end up being added to many projects, creating duplicate linker symbols. We use the define XLE_REGISTER_METAL_VARIANTS to
// try to distinguish the particular project we want to actually instantiate this in
#if defined(XLE_REGISTER_METAL_VARIANTS)

#include "../Init.h"
#include "../../Utility/FunctionUtils.h"
#include "../../Core/Prefix.h"
#include <memory>

namespace RenderCore { namespace ImplVulkan
{
	std::shared_ptr<IDevice> CreateDevice();
	void RegisterCreation()
	{
		#if COMPILER_ACTIVE == COMPILER_TYPE_MSVC
			#pragma comment(linker, "/include:" __FUNCDNAME__)
		#endif

		// (we can also use __attribute__((constructor)) with clang/gcc)
		(void)static_constructor<&RegisterCreation>::c;
		RegisterDeviceCreationFunction(UnderlyingAPI::Vulkan, &CreateDevice);
	}
}}

#endif
