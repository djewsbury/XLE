// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../../../RenderCore/Assets/ModelScaffold.h"
#include "../../../RenderCore/Assets/Services.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/ICompileOperation.h"
#include "../../../Assets/CompileAndAsyncManager.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../ConsoleRig/Console.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "../../../ConsoleRig/GlobalServices.h"
#include "../../../OSServices/AttachableLibrary.h"
#include "../../../OSServices/Log.h"
#include "../../../OSServices/RawFS.h"
#include "../../../OSServices/TimeUtils.h"
#include "../../../Utility/StringFormat.h"
#include "../../../Utility/Threading/ThreadingUtils.h"
#include "../../../Utility/Conversion.h"
#include "../../../Utility/Streams/StreamDOM.h"
#include "../../../Utility/Streams/XmlStreamFormatter.h"
#include "../../../Core/SelectConfiguration.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

namespace UnitTests
{
    __declspec(noinline) static void StreamDomPerformanceTest(const utf8*start, const utf8*end, uint64 iterationCount)
    {
        for (uint64 c=0; c<iterationCount; ++c) {
            XmlInputStreamFormatter<utf8> formatter(MakeStringSection(start, end));
            StreamDOM<XmlInputStreamFormatter<utf8>> doc(formatter);
            (void) doc;
        }
    }

    size_t GlobalHack = 0;

    __declspec(noinline) static void StrLenPerformanceTest(const utf8*start, uint64 iterationCount)
    {
        for (uint64 c=0; c<iterationCount; ++c) {
            GlobalHack += std::strlen((const char*)start);
        }
    }

    __declspec(noinline) static void XlStrLenPerformanceTest(const utf8*start, uint64 iterationCount)
    {
        for (uint64 c=0; c<iterationCount; ++c) {
            GlobalHack += XlGlyphCount(start);
        }
    }
    
    TEST_CASE("RenderCoreCompilation-ColladaParsePerformance", "[rendercore_assets]")
    {
        auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());

        {
            OSServices::AttachableLibrary lib("ColladaConversion.dll");
            std::string attachError;
            bool attachResult = lib.TryAttach(attachError);
            INFO(attachError);
            REQUIRE(attachResult);
            auto createScaffold = lib.GetFunction<Assets::CreateCompileOperationFn*>("CreateCompileOperation");

            // Test parsing performance. Unfortunately we only have a small .dae file in the embedded resources to
            // try; this would produce more accurate results with very large .dae files
            const char sampleAsset[] = "xleres/DefaultResources/materialsphere.dae";
            auto fileDesc = ::Assets::MainFileSystem::TryGetDesc(sampleAsset);

            auto start = __rdtsc();
            const auto iterationCount = (uint64_t)std::max(100ull, 10000000000ull / uint64(fileDesc._size));
            for (uint64 c=0; c<iterationCount; ++c) {
                auto mdl = (*createScaffold)(sampleAsset);
                mdl.reset();    // (incur the cost of deletion here)
            }
            auto end = __rdtsc();

            Log(Warning) << "New path: " << (end-start) / iterationCount << " cycles per collada file." << std::endl;
            Log(Warning) << "That's about " << (end-start) / (uint64(fileDesc._size)*iterationCount) << " cycles per byte in the input file." << std::endl;
        }

        ::Assets::MainFileSystem::GetMountingTree()->Unmount(xlresmnt);
    }

    TEST_CASE("RenderCoreCompilation-StreamDOMParsePerformance", "[rendercore_assets]")
    {
        auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());

        {
            const char sampleAsset[] = "xleres/DefaultResources/materialsphere.dae";

            size_t size = 0;
            auto chars = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(sampleAsset, &size);
            std::vector<char> charsWithNullTerminator(size+1);
            std::memcpy(charsWithNullTerminator.data(), chars.get(), size);
            charsWithNullTerminator[size] = 0;

            const uint64 iterationCount = (uint64)std::max(100ull, 10000000000ull / uint64(size));
            
            auto startXML = __rdtsc();
            StreamDomPerformanceTest(charsWithNullTerminator.data(), PtrAdd(charsWithNullTerminator.data(), size), iterationCount);
            auto middleSample = __rdtsc();
            StrLenPerformanceTest(charsWithNullTerminator.data(), iterationCount);
            auto middleSample2 = __rdtsc();
            XlStrLenPerformanceTest(charsWithNullTerminator.data(), iterationCount);
            auto endStrLen = __rdtsc();

            Log(Warning) << "Ran " << iterationCount << " iterations." << std::endl;
            Log(Warning) << "XML parser: " << (middleSample-startXML) / (iterationCount*uint64(size)) << " cycles per byte. (" << (middleSample-startXML) << ") total" << std::endl;
            Log(Warning) << "std::strlen: " << (middleSample2-middleSample) / (iterationCount*uint64(size)) << " cycles per byte. (" << (middleSample2-middleSample) << ") total" << std::endl;
            Log(Warning) << "XlGlyphCount: " << (endStrLen-middleSample2) / (iterationCount*uint64(size)) << " cycles per byte. (" << (endStrLen-middleSample2) << ") total" << std::endl;
        }

        ::Assets::MainFileSystem::GetMountingTree()->Unmount(xlresmnt);
    }

#if 0
    TEST_METHOD(ColladaScaffold)
    {
        UnitTest_SetWorkingDirectory();
        ConsoleRig::GlobalServices services(GetStartupConfig());

        // Load all of the .dae files from a hierarchy of folders, look for errors
        // and exceptions from the scaffold parsing. This is intended to be used with
        // large test suites of .dae files (like the collada implementor's test kit)
        // auto inputFiles = OSServices::FindFilesHierarchical("../../work/ColladaImplementors/StandardDataSets", "*.dae", OSServices::FindFilesFilter::File);

        {
            #if defined(_DEBUG)
                OSServices::AttachableLibrary lib("../Finals_Debug32/ColladaConversion.dll");
            #else
                OSServices::AttachableLibrary lib("../Finals_Profile32/ColladaConversion.dll");
            #endif
            std::string attachError;
            lib.TryAttach(attachError);
            auto createScaffold = lib.GetFunction<Assets::CreateCompileOperationFn*>(
                "?CreateCompileOperation@ColladaConversion@RenderCore@@YA?AV?$shared_ptr@VICompileOperation@ColladaConversion@RenderCore@@@std@@QEBD@Z");
            
            std::vector<std::string> filesWithErrors;
            for (const auto&f:inputFiles) {
                TRY
                {
                    (*createScaffold)(f.c_str());
                }
                CATCH(...) {
                    filesWithErrors.push_back(f);
                } CATCH_END
            }

            for (const auto&f:filesWithErrors)
                Log(Error) << "Failure in file: " << f << std::endl;
        }
        
    }
#endif
}
