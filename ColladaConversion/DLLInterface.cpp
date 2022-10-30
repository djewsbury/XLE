// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ColladaConversion.h"
#include "../Assets/ICompileOperation.h"
#include "../Assets/CompilerLibrary.h"
#include "../OSServices/AttachableLibrary.h"
#include "../ConsoleRig/AttachablePtr.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../OSServices/Log.h"
#include "../Utility/IteratorUtils.h"
#include "../Core/SelectConfiguration.h"

namespace ColladaConversion
{

	static uint64_t s_knownAssetTypes[] = { Type_Model, Type_RawMat, Type_Skeleton, Type_AnimationSet };

	class CompilerDesc : public ::Assets::ICompilerDesc
	{
	public:
		std::string			Description() const override { return "Compiler and converter for Collada asset files"; }

		unsigned	FileKindCount() const override { return 1; }
		FileKind	GetFileKind(unsigned index) const override
		{
			return FileKind { MakeIteratorRange(s_knownAssetTypes), "*.[dD][aA][eE]", "Collada XML asset", "dae", "dae" };
		}

		CompilerDesc() {}
		~CompilerDesc() {}
	};

#if COMPILER_ACTIVE == COMPILER_TYPE_MSVC
	std::shared_ptr<::Assets::ICompilerDesc> GetCompilerDesc() 
	{
#pragma comment(linker, "/EXPORT:GetCompilerDesc=" __FUNCDNAME__)
		return std::make_shared<CompilerDesc>();
	}
#else
	dll_export std::shared_ptr<::Assets::ICompilerDesc> GetCompilerDesc() asm("GetCompilerDesc");
	std::shared_ptr<::Assets::ICompilerDesc> GetCompilerDesc() { return std::make_shared<CompilerDesc>(); }
#endif

}

extern "C" 
{

	dll_export OSServices::LibVersionDesc GetVersionInformation()
	{
		return ConsoleRig::GetLibVersionDesc();
	}

	static ConsoleRig::WeakAttachablePtr<ConsoleRig::GlobalServices> s_attachRef;

	dll_export void AttachLibrary(ConsoleRig::CrossModule& crossModule)
	{
		auto versionDesc = ConsoleRig::GetLibVersionDesc();
		Log(Verbose) << "Attached Collada Compiler DLL: {" << versionDesc._versionString << "} -- {" << versionDesc._buildDateString << "}" << std::endl;
	}

	dll_export void DetachLibrary()
	{
	}

}
