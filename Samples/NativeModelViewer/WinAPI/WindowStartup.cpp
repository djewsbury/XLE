// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../NativeModelViewer.h"
#include "../../Shared/SampleRig.h"
#include "../../../PlatformRig/AllocationProfiler.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/XPak.h"
#include "../../../OSServices/Log.h"
#include "../../../Formatters/CommandLineFormatter.h"
#include "../../../Formatters/FormatterUtils.h"
#include "../../../Utility/Streams/PathUtils.h"
#include "../../../ConsoleRig/GlobalServices.h"
#include "../../../ConsoleRig/AttachablePtr.h"

    // Note --  when you need to include <windows.h>, generally
    //          prefer to to use the following header ---
    //          This helps prevent name conflicts with 
    //          windows #defines and so forth...
    //  (this is only actually required for the "WinMain" signature)
#include "../../../OSServices/WinAPI/IncludeWindows.h"

struct CommandLineArgsDigest
{
    StringSection<> _xleres = "xleres.pak";
    std::shared_ptr<void> _workingSpace;
    CommandLineArgsDigest(StringSection<> cmdLine)
    {
        auto fmttr = Formatters::MakeCommandLineFormatterFromWin32String(cmdLine, _workingSpace);
        StringSection<> keyname;
        for (;;) {
            if (fmttr.TryKeyedItem(keyname)) {
                if (XlEqStringI(keyname, "xleres"))
                    _xleres = Formatters::RequireStringValue(fmttr);
            } else if (fmttr.PeekNext() == Formatters::FormatterBlob::None) {
                break;
            } else
                Formatters::SkipValueOrElement(fmttr);
        }
    }
};

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    #if CLIBRARIES_ACTIVE == CLIBRARIES_MSVC && defined(_DEBUG)
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | /*_CRTDBG_CHECK_CRT_DF |*/ /*_CRTDBG_CHECK_EVERY_16_DF |*/ _CRTDBG_LEAK_CHECK_DF /*| _CRTDBG_CHECK_ALWAYS_DF*/);
        // _CrtSetBreakAlloc(18160);
    #endif

        //  There maybe a few basic platform-specific initialisation steps we might need to
        //  perform. We can do these here, before calling into platform-specific code.

            // ...

        //  Initialize the "AccumulatedAllocations" profiler as soon as possible, to catch
        //  startup allocation counts.
    PlatformRig::AccumulatedAllocations accumulatedAllocations;

    auto services = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>();
    Log(Verbose) << "------------------------------------------------------------------------------------------" << std::endl;

    TRY {
        CommandLineArgsDigest cmdLineDigest { lpCmdLine };

        std::vector<::Assets::MountingTree::MountID> fsMounts;
		std::shared_ptr<::Assets::ArchiveUtility::FileCache> fileCache;
        if (XlEqStringI(MakeFileNameSplitter(cmdLineDigest._xleres).Extension(), "pak")) {
            fileCache = ::Assets::CreateFileCache(4 * 1024 * 1024);
            std::string finalXleRes = cmdLineDigest._xleres.AsString();
            // by default, search next to the executable if we don't have a fully qualified name
            if (::Assets::MainFileSystem::TryGetDesc(finalXleRes)._snapshot._state == ::Assets::FileSnapshot::State::DoesNotExist) {
                char buffer[MaxPath];
                OSServices::GetProcessPath(buffer, dimof(buffer));
                finalXleRes = Concatenate(MakeFileNameSplitter(buffer).DriveAndPath(), "/", finalXleRes);
            }
            fsMounts.push_back(::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateXPakFileSystem(finalXleRes, fileCache)));
        } else
            fsMounts.push_back(::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateFileSystem_OS(cmdLineDigest._xleres, services->GetPollingThread())));

        Sample::SampleConfiguration cfg;
        cfg._presentationChainBindFlags = RenderCore::BindFlag::UnorderedAccess;
        Sample::ExecuteSample(std::make_shared<Sample::NativeModelViewerOverlay>(), cfg);

        for (auto mnt:fsMounts)
            ::Assets::MainFileSystem::GetMountingTree()->Unmount(mnt);
    } CATCH (const std::exception& e) {
        Log(Error) << "Hit top level exception. Aborting program!" << std::endl;
        Log(Error) << e.what() << std::endl;
    } CATCH_END

    services->PrepareForDestruction();
    services = nullptr;

    return 0;
}
