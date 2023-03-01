// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../OSServices/Log.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/MountingTree.h"
#include "../../Assets/OSFileSystem.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Assets/IntermediateAssets.h"
#include "../../Assets/IArtifact.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Formatters/StreamDOM.h"
#include "../../OSServices/FileSystemMonitor.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../OSServices/RawFS.h"

#include "../../OSServices/WinAPI/IncludeWindows.h"

namespace Converter
{
    int Execute(StringSection<char> cmdLine)
    {
        // We're going to run a simple process that loads a texture file, runs some shader
        // process, and then writes out an output file.
        // This is a command line app; so our instructions should be on the command line.
        // We're going to use a stream formatter & our "StreamDOM" asbtraction to interpret
        // the command line. 
        // We could replace the formatter with a version specialized for
        // command lines if we wanted a unix style command line syntax (and, actually, some
        // syntax elements of this formatter [like ';'] might conflict on some OSs.

        // Sometimes the system will add quotes around our cmdLine (happens when called from groovy)... We need to remove that here
        if (cmdLine.Length() > 2 && *cmdLine.begin() == '"' && *(cmdLine.end()-1) == '"') { ++cmdLine._start; --cmdLine._end; }

        MemoryMappedInputStream stream(cmdLine.begin(), cmdLine.end());
        InputStreamFormatter<char> formatter(stream);
        StreamDOM<InputStreamFormatter<char>> doc(formatter);

        auto outputDirectory = doc.Attribute("o").Value().AsString();
        auto inputFile = doc.Attribute("i").Value();

        if (inputFile.IsEmpty()) {
            Log(Error) << "At least an input file is required on the command line (with the syntax i=<filename>)" << std::endl;
            Log(Error) << "Cmdline: " << cmdLine.AsString().c_str() << std::endl;
            return -1;
        }

		if (outputDirectory.empty())
			outputDirectory = MakeFileNameSplitter(inputFile).File().AsString() + ".rgns";

		ConsoleRig::CrossModule::GetInstance()._services.Add(
			ConstHash64<'comp', 'iler', 'cfg'>::Value,
			[&doc]() -> StreamDOM<InputStreamFormatter<char>>& { return doc; });

		const utf8* xleResDir = "game/xleres";
		::Assets::MainFileSystem::GetMountingTree()->Mount("xleres"), ::Assets::CreateFileSystem_OS(MakeStringSection(xleResDir)));

            // we can now construct basic services
        auto cleanup = MakeAutoCleanup([]() { TerminateFileSystemMonitoring(); });
       
		auto assetServices = ConsoleRig::MakeAttachablePtr<::Assets::Services>(0);
		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		auto discoveredOperations = ::Assets::DiscoverCompileOperations("*Conversion.dll");
		auto generalCompiler = ::Assets::CreateIntermediateCompilers(
			MakeIteratorRange(discoveredOperations),
			nullptr);
		compilers.AddCompiler(generalCompiler);

		const StringSection<char> inits[] = { inputFile };
		auto marker = compilers.Prepare(ConstHash64<'Worl', 'dmap', 'Geo'>::Value, inits, dimof(inits));

		auto pendingCompile = marker->InvokeCompile();
		auto finalState = pendingCompile->StallWhilePending();

		if (finalState == ::Assets::AssetState::Ready) {
			// write out artifacts to output directory... But first, find all of the existing files.
			auto filesToDelete = OSServices::FindFiles(outputDirectory + "/" + MakeFileNameSplitter(outputDirectory).File().AsString() + "[*]", OSServices::FindFilesFilter::File);
			
			OSServices::CreateDirectoryRecursive(outputDirectory);
			for (const auto&a:pendingCompile->GetArtifacts()) {
				auto outputName = outputDirectory + "/" + MakeFileNameSplitter(outputDirectory).File().AsString() + "[" + a.first + "]";
				auto file = ::Assets::MainFileSystem::OpenFileInterface(MakeStringSection(outputName), "wb");
				file->Write(AsPointer(a.second->GetBlob()->begin()), a.second->GetBlob()->size());

				auto i = std::find_if(filesToDelete.begin(), filesToDelete.end(),
					[outputName](const std::string& compare) { 
						return XlEqStringI(MakeFileNameSplitter(MakeStringSection(compare)).FileAndExtension(), MakeFileNameSplitter(MakeStringSection(outputName)).FileAndExtension()); 
					});
				if (i!=filesToDelete.end())
					filesToDelete.erase(i);
			}

			// Delete any files that were there originally, but haven't been overwritten
			for (const auto&f:filesToDelete)
				DeleteFile((const utf8*)f.c_str());
		}
		
        return 0;
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
    ConsoleRig::StartupConfig cfg("converter");
    cfg._setWorkingDir = false;
	auto services = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(cfg);

    TRY {
        return Converter::Execute(lpCmdLine);
    } CATCH (const std::exception& e) {
        Log(Error) << "Hit top level exception. Aborting program!" << std::endl;
        Log(Error) << e.what() << std::endl;
        return -1;
    } CATCH_END
}

int main(int argc, char* argv[])
{
    ConsoleRig::StartupConfig cfg("converter");
    cfg._setWorkingDir = false;
	auto services = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(cfg);

	std::string cmdLine;
	for (int c=1; c<argc; ++c) {
		if (c != 1) cmdLine += " ";
		cmdLine += argv[c];
	}

    TRY {
        return Converter::Execute(MakeStringSection(cmdLine));
    } CATCH (const std::exception& e) {
        Log(Error) << "Hit top level exception. Aborting program!" << std::endl;
        Log(Error) << e.what() << std::endl;
        return -1;
    } CATCH_END
}

