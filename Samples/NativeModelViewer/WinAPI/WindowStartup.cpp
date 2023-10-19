// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../NativeModelViewer.h"
#include "../../Shared/SampleRig.h"
#include "../../../PlatformRig/AllocationProfiler.h"
#include "../../../OSServices/Log.h"
#include "../../../Formatters/CommandLineFormatter.h"

    // Note --  when you need to include <windows.h>, generally
    //          prefer to to use the following header ---
    //          This helps prevent name conflicts with 
    //          windows #defines and so forth...
    //  (this is only actually required for the "WinMain" signature)
#include "../../../OSServices/WinAPI/IncludeWindows.h"


int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    #if CLIBRARIES_ACTIVE == CLIBRARIES_MSVC && defined(_DEBUG)
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | /*_CRTDBG_CHECK_CRT_DF |*/ /*_CRTDBG_CHECK_EVERY_16_DF |*/ _CRTDBG_LEAK_CHECK_DF /*| _CRTDBG_CHECK_ALWAYS_DF*/);
    #endif

        //  Initialize the "AccumulatedAllocations" profiler as soon as possible, to catch
        //  startup allocation counts.
    PlatformRig::AccumulatedAllocations accumulatedAllocations;

    TRY {
        std::shared_ptr<void> workingSpace;
        auto cmdLine = Formatters::MakeCommandLineFormatterFromWin32String(MakeStringSection(lpCmdLine), workingSpace);
        Sample::ExecuteSample(std::make_shared<Sample::NativeModelViewerOverlay>(), cmdLine);
    } CATCH (const std::exception& e) {
        Log(Error) << "Hit top level exception. Aborting program!" << std::endl;
        Log(Error) << e.what() << std::endl;
    } CATCH_END

    return 0;
}
