// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../ConsoleRig/GlobalServices.h"
#include "../Utility/Streams/PathUtils.h"
#include "../OSServices/RawFS.h"

#pragma warning(disable:4505)		// 'UnitTests::GetStartupConfig': unreferenced local function has been removed

namespace UnitTests
{
    inline ConsoleRig::StartupConfig GetStartupConfig()
    {
        ConsoleRig::StartupConfig cfg = "unittest";
            // we can't set the working in this way when run from the 
            // visual studio test explorer
        cfg._setWorkingDir = false;
        return cfg;
    }

    inline void UnitTest_SetWorkingDirectory()
    {
    	    //
    	    //  We need a special way to set the working dir
            //  for units tests...
            //      The executable being run is actually the
            //      visual studio test rig. The normal process
            //      is to get the directory of the executable
            //      and find the working directory relative to
            //      that...
            //      But in this case, we need to assume that as
            //      we start, the current working directory is
            //      one of the "Finals_" output directories, and
            //      we'll find the working directory relative to
            //      that.
    	    //
    	/*char appDir[MaxPath];
    	OSServices::GetCurrentDirectory(dimof(appDir), appDir);
    	OSServices::ChDir((const utf8*)(std::basic_string<char>(appDir) + "\\..\\Working").c_str());*/
    }
    
}

