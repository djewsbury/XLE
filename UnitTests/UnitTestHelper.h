// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../ConsoleRig/GlobalServices.h"

#pragma warning(disable:4505)		// 'UnitTests::GetStartupConfig': unreferenced local function has been removed

namespace UnitTests
{
    inline ConsoleRig::StartupConfig GetStartupConfig()
    {
        ConsoleRig::StartupConfig cfg = "xle-unit-tests";
            // we can't set the working in this way when run from the 
            // visual studio test explorer
        cfg._setWorkingDir = false;
        cfg._registerTemporaryIntermediates = true;
        return cfg;
    }
}

