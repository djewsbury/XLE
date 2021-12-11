// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TestDLL.h"
#include "../../Assets/Assets.h"
#include "../../Utility/Streams/FileSystemMonitor.h"

namespace Samples
{
	static std::shared_ptr<Assets::DependencyValidation> s_depVal;

	TESTDLL_API void	Startup()
	{
		s_depVal = std::make_shared<Assets::DependencyValidation>();
		Assets::RegisterFileDependency(s_depVal, "colladaimport.dat");
	}

	TESTDLL_API void	Shutdown()
	{
		s_depVal.reset();
		TerminateFileSystemMonitoring();
	}

	TESTDLL_API void* AllocateBlock(size_t size)
	{
		return new char[size];
	}

	TESTDLL_API void  DeallocateBlock(void* blk)
	{
		delete[] (char*)blk;
	}

	extern char VersionString[];
	extern char BuildDateString[];

	TESTDLL_API std::pair<const char*, const char*>    GetVersionInformation()
	{
		return std::make_pair(VersionString, BuildDateString);
	}
}

