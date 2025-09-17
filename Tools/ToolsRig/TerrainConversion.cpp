// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SCL_SECURE_NO_WARNINGS

#include "TerrainConversion.h"
#include "TerrainOp.h"
#include "TerrainShadowOp.h"
#include "../../SceneEngine/Terrain.h"
#include "../../SceneEngine/TerrainFormat.h"
#include "../../SceneEngine/TerrainConfig.h"
#include "../../SceneEngine/TerrainUberSurface.h"
#include "../../SceneEngine/TerrainScaffold.h"
#include "../../RenderCore/Format.h"      // (for BitsPerPixel)
#include "../../Math/Vector.h"
#include "../../ConsoleRig/IProgress.h"
#include "../../OSServices/Log.h"
#include "../../Assets/IFileSystem.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../OSServices/RawFS.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/Threading/ThreadingUtils.h"
#include <vector>
#include <regex>

#include "../../Foreign/LibTiff/tiff.h"
#include "../../Foreign/LibTiff/tiffio.h"
#include "../../Foreign/half-1.9.2/include/half.hpp"

namespace ToolsRig
{
    using namespace SceneEngine;

	static bool DoesFileExist(const char fn[])
	{
		return ::Assets::MainFileSystem::TryGetDesc(fn)._state == ::Assets::FileDesc::State::Normal;
	}

    //////////////////////////////////////////////////////////////////////////////////////////
    static void WriteCellCoverageData(
        const TerrainConfig& cfg, ITerrainFormat& ioFormat, 
        const ::Assets::ResChar uberSurfaceName[], unsigned layerIndex,
        bool overwriteExisting,
        ConsoleRig::IProgress* progress)
    {
        ::Assets::ResChar path[MaxPath];

        auto cells = BuildPrimedCells(cfg);
        auto& layer = cfg.GetCoverageLayer(layerIndex);

        auto step = progress ? progress->BeginStep("Write coverage cells", (unsigned)cells.size(), true) : nullptr;

        TerrainUberSurfaceGeneric uberSurface(uberSurfaceName);
        for (auto c=cells.cbegin(); c!=cells.cend(); ++c) {

            char cellFile[MaxPath];
            cfg.GetCellFilename(cellFile, dimof(cellFile), c->_cellIndex, layer._id);
            if (!DoesFileExist(cellFile) || overwriteExisting) {
                XlDirname(path, dimof(path), cellFile);
                OSServices::CreateDirectoryRecursive(path);

                TRY {
                    ioFormat.WriteCell(
                        cellFile, uberSurface, 
                        c->_coverageUber[layerIndex].first, c->_coverageUber[layerIndex].second, 
                        cfg.CellTreeDepth(), layer._overlap);
                } CATCH(...) {
                    Log(Error) << "Error while writing cell coverage file to: " << cellFile << std::endl;
                } CATCH_END
            }

            if (step) {
                if (step->IsCancelled()) break;
                step->Advance();
            }

        }
    }

    static unsigned FindLayer(const TerrainConfig& cfg, TerrainCoverageId coverageId)
    {
        for (unsigned l=0; l<cfg.GetCoverageLayerCount(); ++l)
            if (cfg.GetCoverageLayer(l)._id == coverageId)
                return l;
        return ~0u;
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    void GenerateCellFiles(
        const TerrainConfig& outputConfig, 
        const ::Assets::ResChar uberSurfaceDir[],
        bool overwriteExisting,
        const GradientFlagsSettings& gradFlagSettings,
        ConsoleRig::IProgress* progress)
    {
        auto outputIOFormat = std::make_shared<TerrainFormat>(gradFlagSettings);
        assert(outputIOFormat);

        //////////////////////////////////////////////////////////////////////////////////////
            // for each coverage layer, we must write all of the component parts
        for (unsigned l=0; l<outputConfig.GetCoverageLayerCount(); ++l) {
            const auto& layer = outputConfig.GetCoverageLayer(l);

            ::Assets::ResChar layerUberSurface[MaxPath];
            TerrainConfig::GetUberSurfaceFilename(layerUberSurface, dimof(layerUberSurface), uberSurfaceDir, layer._id);
            if (DoesFileExist(layerUberSurface)) {
                    //  open and destroy these coverage uber shadowing surface before we open the uber heights surface
                    //  (opening them both at the same time requires too much memory)
                WriteCellCoverageData(outputConfig, *outputIOFormat, layerUberSurface, l, overwriteExisting, progress);
            }
        }

        //////////////////////////////////////////////////////////////////////////////////////
            //  load the uber height surface, and uber surface interface (but only temporarily
            //  while we initialise the data)
        ::Assets::ResChar uberSurfaceFile[MaxPath];
        TerrainConfig::GetUberSurfaceFilename(uberSurfaceFile, dimof(uberSurfaceFile), uberSurfaceDir, CoverageId_Heights);
        TerrainUberHeightsSurface heightsData(uberSurfaceFile);
        HeightsUberSurfaceInterface uberSurfaceInterface(heightsData);

        //////////////////////////////////////////////////////////////////////////////////////
        auto cells = BuildPrimedCells(outputConfig);
        Interlocked::Value queueLoc = 0;
        auto step = progress ? progress->BeginStep("Generate Cell Files", (unsigned)cells.size(), true) : nullptr;

        auto threadFunction = 
            [&queueLoc, &cells, &outputConfig, overwriteExisting, &outputIOFormat, &step, &uberSurfaceInterface]()
            {
                for (;;) {
                    auto i = Interlocked::Increment(&queueLoc);
                    if (i >= (int)cells.size()) return;

                    const auto& c = cells[i];
                    char heightMapFile[MaxPath];
                    outputConfig.GetCellFilename(heightMapFile, dimof(heightMapFile), c._cellIndex, CoverageId_Heights);
                    if (overwriteExisting || !DoesFileExist(heightMapFile)) {
                        char path[MaxPath];
                        XlDirname(path, dimof(path), heightMapFile);
                        OSServices::CreateDirectoryRecursive(path);
                        TRY {
                            outputIOFormat->WriteCell(
                                heightMapFile, *uberSurfaceInterface.GetUberSurface(), 
                                c._heightUber.first, c._heightUber.second, outputConfig.CellTreeDepth(), outputConfig.NodeOverlap());
                        } CATCH(...) { // sometimes throws (eg, if the directory doesn't exist)
                        } CATCH_END
                    }

                    if (step) {
                        if (step->IsCancelled()) break;
                        step->Advance();
                    }
                }
            };

        auto hardwareConc = std::thread::hardware_concurrency();

        std::vector<std::thread> threads;
        for (unsigned c=0; c<std::max(1u, hardwareConc); ++c)
            threads.emplace_back(std::thread(threadFunction));

        for (auto&t : threads) t.join();
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    void GenerateCellFiles(
        const TerrainConfig& outputConfig, 
        const ::Assets::ResChar uberSurfaceDir[],
        bool overwriteExisting,
        SceneEngine::TerrainCoverageId coverageId,
        ConsoleRig::IProgress* progress)
    {
        auto layerIndex = FindLayer(outputConfig, coverageId);
        if (layerIndex == ~0u) return;

        auto outputIOFormat = std::make_shared<TerrainFormat>();
        assert(outputIOFormat);

        ::Assets::ResChar layerUberSurface[MaxPath];
        TerrainConfig::GetUberSurfaceFilename(layerUberSurface, dimof(layerUberSurface), uberSurfaceDir, coverageId);
        if (DoesFileExist(layerUberSurface)) {
                //  open and destroy these coverage uber shadowing surface before we open the uber heights surface
                //  (opening them both at the same time requires too much memory)
            WriteCellCoverageData(outputConfig, *outputIOFormat, layerUberSurface, layerIndex, overwriteExisting, progress);
        }
    }
    
    static void GenerateSurface(
        ITerrainOp& op, TerrainCoverageId coverageId,
        const TerrainConfig& cfg, 
        const ::Assets::ResChar uberSurfaceDir[],
        bool overwriteExisting,
        ConsoleRig::IProgress* progress)
    {
        auto layerIndex = FindLayer(cfg, coverageId);
        if (layerIndex == ~0u) return;

        ::Assets::ResChar shadowUberFn[MaxPath];
        TerrainConfig::GetUberSurfaceFilename(shadowUberFn, dimof(shadowUberFn), uberSurfaceDir, coverageId);

        if (overwriteExisting || !DoesFileExist(shadowUberFn)) {

            //////////////////////////////////////////////////////////////////////////////////////
                // this is the shadows layer... We need to build the shadows procedurally
            ::Assets::ResChar uberHeightsFile[MaxPath];
            TerrainConfig::GetUberSurfaceFilename(uberHeightsFile, dimof(uberHeightsFile), uberSurfaceDir, CoverageId_Heights);
            TerrainUberHeightsSurface heightsData(uberHeightsFile);
            HeightsUberSurfaceInterface uberSurfaceInterface(heightsData);

            //////////////////////////////////////////////////////////////////////////////////////
                // build the uber shadowing file, and then write out the shadowing textures for each node
            // Int2 interestingMins((9-1) * 16 * 32, (19-1) * 16 * 32), interestingMaxs((9+4) * 16 * 32, (19+4) * 16 * 32);
            float shadowToHeightsScale = 
                cfg.NodeDimensionsInElements()[0] 
                / float(cfg.GetCoverageLayer(layerIndex)._nodeDimensions[0]);
            
            UInt2 interestingMins(0, 0);
            UInt2 interestingMaxs = UInt2(
                unsigned((cfg._cellCount[0] * cfg.CellDimensionsInNodes()[0] * cfg.NodeDimensionsInElements()[0]) / shadowToHeightsScale),
                unsigned((cfg._cellCount[1] * cfg.CellDimensionsInNodes()[1] * cfg.NodeDimensionsInElements()[1]) / shadowToHeightsScale));

            //////////////////////////////////////////////////////////////////////////////////////
                
            BuildUberSurface(
                shadowUberFn, op, 
                *uberSurfaceInterface.GetUberSurface(), interestingMins, interestingMaxs, 
                cfg.ElementSpacing(), shadowToHeightsScale, 
                TerrainOpConfig(),
                progress);

        }

        //////////////////////////////////////////////////////////////////////////////////////
            // write cell files
        auto fmt = std::make_shared<TerrainFormat>();
        WriteCellCoverageData(
            cfg, *fmt, shadowUberFn, layerIndex, overwriteExisting, progress);
    }

    void GenerateShadowsSurface(
        const TerrainConfig& cfg,
        const ::Assets::ResChar uberSurfaceDir[],
        bool overwriteExisting,
        ConsoleRig::IProgress* progress)
    {
        Float2 sunAxisOfMovement(XlCos(cfg.SunPathAngle()), XlSin(cfg.SunPathAngle()));
        const float shadowSearchDistance = 1000.f;
        AngleBasedShadowsOperator op(sunAxisOfMovement, shadowSearchDistance);
        
        GenerateSurface(
            op, CoverageId_AngleBasedShadows,
            cfg, uberSurfaceDir, overwriteExisting, progress);
    }

    void GenerateAmbientOcclusionSurface(
        const TerrainConfig& cfg, 
        const ::Assets::ResChar uberSurfaceDir[],
        bool overwriteExisting,
        ConsoleRig::IProgress* progress)
    {
        static auto testRadius = 24u;
        static auto power = 4.f;
        AOOperator op(testRadius, power);

        GenerateSurface(
            op, CoverageId_AmbientOcclusion,
            cfg, uberSurfaceDir, overwriteExisting, progress);
    }

    void GenerateMissingUberSurfaceFiles(
        const TerrainConfig& cfg, 
        const ::Assets::ResChar uberSurfaceDir[],
        ConsoleRig::IProgress* progress)
    {
        OSServices::CreateDirectoryRecursive(uberSurfaceDir);

        auto step = progress ? progress->BeginStep("Generate UberSurface Files", (unsigned)cfg.GetCoverageLayerCount(), true) : nullptr;
        for (unsigned l=0; l<cfg.GetCoverageLayerCount(); ++l) {
            const auto& layer = cfg.GetCoverageLayer(l);

            ::Assets::ResChar uberSurfaceFile[MaxPath];
            TerrainConfig::GetUberSurfaceFilename(uberSurfaceFile, dimof(uberSurfaceFile), uberSurfaceDir, layer._id);
            if (DoesFileExist(uberSurfaceFile)) continue;

            if (layer._id == CoverageId_AngleBasedShadows || layer._id == CoverageId_AmbientOcclusion) {

                continue;      // (skip, we'll get this later in GenerateShadowsSurface)

            } else {

                HeightsUberSurfaceInterface::BuildEmptyFile(
                    uberSurfaceFile, 
                    cfg._cellCount[0] * cfg.CellDimensionsInNodes()[0] * layer._nodeDimensions[0],
                    cfg._cellCount[1] * cfg.CellDimensionsInNodes()[1] * layer._nodeDimensions[1],
                    ImpliedTyping::TypeDesc{ImpliedTyping::TypeCat(layer._typeCat), uint16(layer._typeCount)});

            }

            if (step) {
                if (step->IsCancelled()) break;
                step->Advance();
            }
        }

        GenerateShadowsSurface(cfg, uberSurfaceDir, false, progress);
        GenerateAmbientOcclusionSurface(cfg, uberSurfaceDir, false, progress);
    }
    
///////////////////////////////////////////////////////////////////////////////////////////////////
    std::vector<std::string>* s_tiffWarningVector = nullptr;
    static void TIFFWarningHandler(const char* module, const char* fmt, va_list args)
    {
        // suppress warnings
        char buffer[1024];
        _vsnprintf_s(buffer, dimof(buffer), _TRUNCATE, fmt, args);
        Log(Warning) << "Tiff reader warning: " << buffer << std::endl;

        if (s_tiffWarningVector) {
            s_tiffWarningVector->push_back(buffer);
        }
    }

    static void TIFFErrorHandler(const char* module, const char* fmt, va_list args)
    {
        // suppress warnings
        char buffer[1024];
        _vsnprintf_s(buffer, dimof(buffer), _TRUNCATE, fmt, args);
        Log(Warning) << "Tiff reader error: " << buffer << std::endl;

        if (s_tiffWarningVector) {
            s_tiffWarningVector->push_back(buffer);
        }
    }

    template<typename Fn>
        class AutoClose
    {
    public:
        AutoClose(Fn&& fn) : _fn(std::move(fn)) {}
        ~AutoClose() { _fn(); }
        
    protected:
        Fn _fn;
    };

    template<typename Fn>
        AutoClose<Fn> MakeAutoClose(Fn&& fn) { return AutoClose<Fn>(std::move(fn)); }

    static UInt2 ClampImportDims(UInt2 input, unsigned destNodeDims, unsigned destCellTreeDepth)
    {
            //  we have to make sure the width and height are multiples of the
            //  dimensions of a cell (in elements). We'll pad out the edges if
            //  they don't match
        const unsigned cellWidthInNodes = 1<<(destCellTreeDepth-1);
        const unsigned clampingDim = destNodeDims * cellWidthInNodes;
        
        if ((input[0] % clampingDim) != 0) { input[0] += clampingDim - (input[0] % clampingDim); }
        if ((input[1] % clampingDim) != 0) { input[1] += clampingDim - (input[1] % clampingDim); }
        return input;
    }

    TerrainImportOp PrepareTerrainImport(
        const ::Assets::ResChar input[], 
        unsigned destNodeDims, unsigned destCellTreeDepth)
    {
        TerrainImportOp result;
        result._sourceDims = UInt2(0, 0);
        result._sourceFile = input;
        result._sourceIsGood = false;
        result._importCoverageFormat = (unsigned)ImpliedTyping::TypeCat::Float;

        auto ext = XlExtension(input);
        if (ext && (!XlCompareStringI(ext, "hdr") || !XlCompareStringI(ext, "flt"))) {

            result._sourceFormat = TerrainImportOp::SourceFormat::AbsoluteFloats;
            result._sourceHeightRange = Float2(std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());

            ::Assets::ResChar inputFile[MaxPath];
            XlCopyString(inputFile, input);
            XlChopExtension(inputFile);
            XlCatString(inputFile, dimof(inputFile), ".hdr");

            size_t fileSize = 0;
            auto block = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(inputFile, &fileSize);
            if (block.get() && fileSize) {
                std::string configAsString(block.get(), &block[fileSize]);
                std::regex parse("^(\\S+)\\s+(.*)");

                std::vector<int> captureGroups;
                captureGroups.push_back(1);
                captureGroups.push_back(2);
    
                const std::sregex_token_iterator end;
                std::sregex_token_iterator iter(configAsString.begin(), configAsString.end(), parse, captureGroups);
                for (;iter != end;) {
                    auto paramName = *iter++;
                    auto paramValue = *iter++;

                        //  we ignore many parameters. But we at least need to get ncols & nrows
                        //  These tell us the dimensions of the input data
                    if (!XlCompareStringI(paramName.str().c_str(), "ncols")) { result._sourceDims[0] = XlAtoI32(paramValue.str().c_str()); }
                    if (!XlCompareStringI(paramName.str().c_str(), "nrows")) { result._sourceDims[1] = XlAtoI32(paramValue.str().c_str()); }
                }

                result._sourceIsGood = true;
            } else {
                result._warnings.push_back("Could not open input file");
            }

        } else if (ext && (!XlCompareStringI(ext, "tif") || !XlCompareStringI(ext, "tiff"))) {

            auto oldWarningHandler = TIFFSetWarningHandler(&TIFFWarningHandler);
            auto oldErrorHandler = TIFFSetErrorHandler(&TIFFErrorHandler);
            s_tiffWarningVector = &result._warnings;
            auto autoClose2 = MakeAutoClose([oldWarningHandler, oldErrorHandler]() 
                {
                    TIFFSetWarningHandler(oldWarningHandler);
                    TIFFSetErrorHandler(oldErrorHandler);
                    s_tiffWarningVector = nullptr;
                });
            
            auto* tif = TIFFOpen(input, "r");
            if (tif) {
                auto autoClose = MakeAutoClose([tif]() { TIFFClose(tif); });

                TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &result._sourceDims[0]);
                TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &result._sourceDims[1]);

                uint32 bitsPerPixel = 32;
                uint32 sampleFormat = SAMPLEFORMAT_IEEEFP;
                TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerPixel);
                TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat);

                switch (sampleFormat) {
                case SAMPLEFORMAT_UINT:
					result._sourceFormat = TerrainImportOp::SourceFormat::Quantized;
                    if (bitsPerPixel == 8)          { result._sourceHeightRange = Float2(0.f, float(0xff)); result._importCoverageFormat = (unsigned)ImpliedTyping::TypeCat::UInt8; }
                    else if (bitsPerPixel == 16)    { result._sourceHeightRange = Float2(0.f, float(0xffff)); result._importCoverageFormat = (unsigned)ImpliedTyping::TypeCat::UInt16; }
                    else if (bitsPerPixel == 32)    { result._sourceHeightRange = Float2(0.f, float(0xffffffff)); result._importCoverageFormat = (unsigned)ImpliedTyping::TypeCat::UInt32; }
                    else                            { result._warnings.push_back("Bad bits per pixel"); return result; }
                    break;

                case SAMPLEFORMAT_INT:
                    result._sourceFormat = TerrainImportOp::SourceFormat::Quantized;
                    if (bitsPerPixel == 8)          { result._sourceHeightRange = Float2(float( INT8_MIN), float( INT8_MAX)); result._importCoverageFormat = (unsigned)ImpliedTyping::TypeCat::UInt8; }
                    else if (bitsPerPixel == 16)    { result._sourceHeightRange = Float2(float(INT16_MIN), float(INT16_MAX)); result._importCoverageFormat = (unsigned)ImpliedTyping::TypeCat::UInt16; }
                    else if (bitsPerPixel == 32)    { result._sourceHeightRange = Float2(float(INT32_MIN), float(INT32_MAX)); result._importCoverageFormat = (unsigned)ImpliedTyping::TypeCat::UInt32; }
                    else                            { result._warnings.push_back("Bad bits per pixel"); return result; }
                    break;

                case SAMPLEFORMAT_IEEEFP:
                    result._sourceFormat = TerrainImportOp::SourceFormat::AbsoluteFloats;
                    if (bitsPerPixel != 16 && bitsPerPixel != 32)
                        { result._warnings.push_back("Bad bits per pixel"); return result; }

                    result._importCoverageFormat = (unsigned)ImpliedTyping::TypeCat::Float;     // (todo -- float16 support?)
                    break;

                default:
                    result._warnings.push_back("Unsupported sample format");
                    return result;
                }

                result._sourceIsGood = true;
            } else {
                result._warnings.push_back("Could not open tiff file");
            }

        } else {
            result._warnings.push_back("Unknown input file format");
        }

        result._importMins = UInt2(0, 0);
        result._importMaxs = ClampImportDims(result._sourceDims, destNodeDims, destCellTreeDepth);
        result._importHeightRange = result._sourceHeightRange;
        return result;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename InputType>
        static void SimpleConvert(float dest[], const InputType* input, size_t count, float offset, float scale)
    {
        for (unsigned c=0; c<count; ++c)
            dest[c] = float(input[c]) * scale + offset;
    }

    template<typename InputType>
        static void SimpleConvertGen(void* dest, const ImpliedTyping::TypeDesc& dstType, const InputType* input, size_t count, double offset, double scale)
    {
            // note --  the implied typing cast here is quite expensive! But it's 
            //          reliable
        auto dstSize = dstType.GetSize();
        for (unsigned c=0; c<count; ++c) {
			// good idea to use the increased precision of "doubles" here, particularly when
			// loading from 32 bit integer formats
            auto midway = float(double(input[c]) * scale + offset);
            ImpliedTyping::Cast(
                { PtrAdd(dest, c*dstSize), PtrAdd(dest, c*dstSize+dstSize) }, dstType,
                MakeOpaqueIteratorRange(midway), ImpliedTyping::TypeOf<decltype(midway)>());
        }
    }

    static float Float16AsFloat32(unsigned short input)
    {
        return half_float::detail::half2float(input);
    }

    static void ConvertFloat16Gen(void* dest, const ImpliedTyping::TypeDesc& dstType, const uint16* input, size_t count, double offset, double scale)
    {
            // note --  the implied typing cast here is quite expensive! But it's 
            //          reliable
        auto dstSize = dstType.GetSize();
        for (unsigned c=0; c<count; ++c) {
            auto midway = float(double(Float16AsFloat32(input[c])) * scale + offset);
            ImpliedTyping::Cast(
                { PtrAdd(dest, c*dstSize), PtrAdd(dest, c*dstSize+dstSize) }, dstType,
                MakeOpaqueIteratorRange(midway), ImpliedTyping::TypeOf<decltype(midway)>());
        }
    }
    
    void ExecuteTerrainImport(
        const TerrainImportOp& op,
        const ::Assets::ResChar outputDir[],
        unsigned destNodeDims, unsigned destCellTreeDepth,
        SceneEngine::TerrainCoverageId coverageId,
        ImpliedTyping::TypeCat dstType,
        ConsoleRig::IProgress* progress)
    {
        auto initStep = progress ? progress->BeginStep("Load source data", 1, false) : nullptr;

        auto oldWarningHandler = TIFFSetWarningHandler(&TIFFWarningHandler);
        auto oldErrorHandler = TIFFSetErrorHandler(&TIFFErrorHandler);
        s_tiffWarningVector = nullptr;
        auto autoClose2 = MakeAutoClose([oldWarningHandler, oldErrorHandler]() 
            {
                TIFFSetWarningHandler(oldWarningHandler);
                TIFFSetErrorHandler(oldErrorHandler);
                s_tiffWarningVector = nullptr;
            });

        if (!op._sourceIsGood || ((op._importMaxs[0] <= op._importMins[0]) && (op._importMaxs[1] <= op._importMins[1]))) {
            Throw(
                ::Exceptions::BasicLabel("Bad or missing input terrain config file (%s)", op._sourceFile.c_str()));
        }

        UInt2 importOffset =  op._importMins;
        UInt2 finalDims = ClampImportDims(op._importMaxs - op._importMins, destNodeDims, destCellTreeDepth);

        OSServices::CreateDirectoryRecursive(outputDir);

        auto dstSampleSize = ImpliedTyping::TypeDesc{dstType}.GetSize();
        uint64_t resultSize = 
            sizeof(TerrainUberHeader)
            + finalDims[0] * finalDims[1] * dstSampleSize
            ;

        ::Assets::ResChar outputUberFileName[MaxPath]; 
        SceneEngine::TerrainConfig::GetUberSurfaceFilename(
            outputUberFileName, dimof(outputUberFileName),
            outputDir, coverageId);

        auto outputUberFile = ::Assets::MainFileSystem::OpenMemoryMappedFile(outputUberFileName, resultSize, "w");

        auto& hdr   = *(TerrainUberHeader*)outputUberFile.GetData().begin();
        hdr._magic  = TerrainUberHeader::Magic;
        hdr._width  = finalDims[0];
        hdr._height = finalDims[1];
        hdr._typeCat = (unsigned)dstType;
        hdr._typeArrayCount = 1;
        hdr._dummy[0] = hdr._dummy[1] = hdr._dummy[2]  = 0;

        void* outputArray = PtrAdd(outputUberFile.GetData().begin(), sizeof(TerrainUberHeader));

        auto ext = XlExtension(op._sourceFile.c_str());
        if (ext && (!XlCompareStringI(ext, "hdr") || !XlCompareStringI(ext, "flt"))) {
            if (dstType != ImpliedTyping::TypeCat::Float)
                Throw(::Exceptions::BasicLabel("Attempting to load float format input into non-float destination (%s)", op._sourceFile.c_str()));

            auto inputFileData = ::Assets::MainFileSystem::OpenMemoryMappedFile(op._sourceFile, 0, "r");

            if (op._sourceFormat!=TerrainImportOp::SourceFormat::AbsoluteFloats)
                Throw(::Exceptions::BasicLabel("Expecting absolute floats when loading from raw float array"));

            if (initStep) {
                initStep->Advance();
                initStep.reset();
            }

            auto copyRows = std::min(finalDims[1], op._importMaxs[1]) - op._importMins[1];
            const unsigned progressStep = 16;
            auto copyStep = progress ? progress->BeginStep("Create uber surface data", copyRows / progressStep, true) : nullptr;

            auto inputArray = (const float*)inputFileData.GetData().begin();

            unsigned yoff = op._importMins[1];
            unsigned y2=0;
            for (; (y2+progressStep)<=copyRows; y2+=progressStep) {
                for (unsigned y=0; y<progressStep; ++y) {
                    std::copy(
                        &inputArray[(y2+y+yoff) * op._sourceDims[0] + op._importMins[0]],
                        &inputArray[(y2+y+yoff) * op._sourceDims[0] + std::min(op._sourceDims[0], op._importMaxs[0])],
                        (float*)PtrAdd(outputArray, ((y2+y) * finalDims[0]) * dstSampleSize));
                }

                if (copyStep) {
                    if (copyStep->IsCancelled())
                        Throw(::Exceptions::BasicLabel("User cancelled"));
                    copyStep->Advance();
                }
            }

                // remainder rows left over after dividing by progressStep
            for (; y2<copyRows; ++y2) {
                std::copy(
                    &inputArray[(y2+yoff) * op._sourceDims[0] + op._importMins[0]],
                    &inputArray[(y2+yoff) * op._sourceDims[0] + std::min(op._sourceDims[0], op._importMaxs[0])],
                    (float*)PtrAdd(outputArray, (y2 * finalDims[0]) * dstSampleSize));
            }
        } else if (ext && (!XlCompareStringI(ext, "tif") || !XlCompareStringI(ext, "tiff"))) {
                // attempt to read geotiff file
            auto* tif = TIFFOpen(op._sourceFile.c_str(), "r");
            if (!tif)
                Throw(::Exceptions::BasicLabel("Couldn't open input file (%s)", op._sourceFile.c_str()));

            auto autoClose = MakeAutoClose([tif]() { TIFFClose(tif); });
            auto stripCount = TIFFNumberOfStrips(tif);

            auto copyStep = 
                  progress 
                ? progress->BeginStep("Create uber surface data", stripCount, true)
                : nullptr;

            uint32 rowsperstrip = 1;
            TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rowsperstrip);

            uint32 bitsPerPixel = 32;
            uint32 sampleFormat = SAMPLEFORMAT_IEEEFP;
            TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerPixel);
            TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat);

                //  We only support loading a few input formats.
                //  TIFF supports a wide variety of formats. If we get something
                //  unexpected, just throw;
            if (sampleFormat != SAMPLEFORMAT_UINT && sampleFormat != SAMPLEFORMAT_INT && sampleFormat != SAMPLEFORMAT_IEEEFP)
                Throw(::Exceptions::BasicLabel("Unexpected sample format in input file (%s). Only supporting integer or floating point inputs", op._sourceFile.c_str()));

            if (bitsPerPixel != 8 && bitsPerPixel != 16 && bitsPerPixel != 32)
                Throw(::Exceptions::BasicLabel("Unexpected bits per sample in input file (%s). Only supporting 8, 16 or 32 bit formats. Try floating a 32 bit float format.", op._sourceFile.c_str()));

            auto stripSize = TIFFStripSize(tif);
            if (!stripSize)
                Throw(::Exceptions::BasicLabel("Could not get strip byte codes from tiff file (%s). Input file may be corrupted.", op._sourceFile.c_str()));
            
            auto stripBuffer = std::make_unique<char[]>(stripSize);
            XlSetMemory(stripBuffer.get(), 0, stripSize);

            typedef void ConversionFn(void*, const ImpliedTyping::TypeDesc&, const void*, size_t, double, double);
            ConversionFn* convFn;
            switch (sampleFormat) {
            case SAMPLEFORMAT_UINT:
				if (bitsPerPixel == 8)          convFn = (ConversionFn*)&SimpleConvertGen<uint8>;
                else if (bitsPerPixel == 16)    convFn = (ConversionFn*)&SimpleConvertGen<uint16>;
                else if (bitsPerPixel == 32)    convFn = (ConversionFn*)&SimpleConvertGen<uint32>;
                else Throw(::Exceptions::BasicLabel("Unknown input format.", op._sourceFile.c_str()));
                break;

            case SAMPLEFORMAT_INT:
                if (bitsPerPixel == 8)          convFn = (ConversionFn*)&SimpleConvertGen<int8>;
                else if (bitsPerPixel == 16)    convFn = (ConversionFn*)&SimpleConvertGen<int16>;
                else if (bitsPerPixel == 32)    convFn = (ConversionFn*)&SimpleConvertGen<int32>;
                else Throw(::Exceptions::BasicLabel("Unknown input format.", op._sourceFile.c_str()));
                break;

            case SAMPLEFORMAT_IEEEFP:
                if (bitsPerPixel == 16)         convFn = (ConversionFn*)&ConvertFloat16Gen;
                else if (bitsPerPixel == 32)    convFn = (ConversionFn*)&SimpleConvertGen<float>;
                else Throw(::Exceptions::BasicLabel("8 bit floats not supported in input file (%s). Use 16 or 32 bit floats instead.", op._sourceFile.c_str()));
                break;

            default:
                Throw(::Exceptions::BasicLabel("Unknown input format.", op._sourceFile.c_str()));
            }

            double valueScale = (double(op._importHeightRange[1]) - double(op._importHeightRange[0])) / (double(op._sourceHeightRange[1]) - double(op._sourceHeightRange[0]));
            double valueOffset = double(op._importHeightRange[0]) - double(op._sourceHeightRange[0]) * valueScale;
                
            for (tstrip_t strip = 0; strip < stripCount; strip++) {
                auto readResult = TIFFReadEncodedStrip(tif, strip, stripBuffer.get(), stripSize);

                if (readResult != stripSize) {
					// Sometimes the very last strip is truncated. This occurs if the height
					// is not an even multiple of the strip size
					// In this case, we just blank out the remaining part
					if (readResult > 0 && readResult < stripSize && (strip+1 == stripCount)) {
						std::memset(PtrAdd(stripBuffer.get(), readResult), 0x0, stripSize - readResult);
					} else {
						Throw(::Exceptions::BasicLabel(
							"Error while reading from tiff file. File may be truncated or otherwise corrupted.", 
							op._sourceFile.c_str()));
					}
				}

                for (unsigned r=0; r<rowsperstrip; ++r) {
                    auto y = strip * rowsperstrip + r;
                    if (y >= op._importMins[1] && y < op._importMaxs[1]) {
                        (*convFn)(
                            PtrAdd(outputArray, ((y - op._importMins[1]) * finalDims[0]) * dstSampleSize),
                            ImpliedTyping::TypeDesc{dstType},
                            PtrAdd(stripBuffer.get(), op._importMins[0]*bitsPerPixel/8),
                            std::min(op._sourceDims[0], op._importMaxs[0]) - op._importMins[0],
                            valueOffset, valueScale);
                    }
                }

                if (copyStep) {
                    if (copyStep->IsCancelled())
                        Throw(::Exceptions::BasicLabel("User cancelled"));
                    copyStep->Advance();
                }
            }

            // TIFFClose called by AutoClose
        }

            // fill in the extra space caused by rounding up
        float blank = 0.f;
        if (finalDims[0] > op._sourceDims[0]) {
            for (unsigned y=0; y<(op._importMaxs[1] - op._importMins[1]); ++y) {
                for (unsigned x=op._sourceDims[0]; x<finalDims[0]; ++x)
                    ImpliedTyping::Cast(
                        { PtrAdd(outputArray, (y * finalDims[0] + x)*dstSampleSize), PtrAdd(outputArray, (y * finalDims[0] + x)*dstSampleSize+dstSampleSize) },
                        ImpliedTyping::TypeDesc{dstType},
                        MakeOpaqueIteratorRange(blank), ImpliedTyping::TypeOf<decltype(blank)>());
                    
            }
        }

        for (unsigned y=op._importMaxs[1] - op._importMins[1]; y < finalDims[1]; ++y) {
            for (unsigned x=0; x<finalDims[0]; ++x)
                ImpliedTyping::Cast(
                    { PtrAdd(outputArray, (y * finalDims[0] + x)*dstSampleSize), PtrAdd(outputArray, (y * finalDims[0] + x)*dstSampleSize+dstSampleSize) },
                    ImpliedTyping::TypeDesc{dstType},
                    MakeOpaqueIteratorRange(blank), ImpliedTyping::TypeOf<decltype(blank)>());
        }
    }

    void ExecuteTerrainExport(
        const ::Assets::ResChar dstFile[],
        const SceneEngine::TerrainConfig& srcCfg, 
        const ::Assets::ResChar srcDir[],
        SceneEngine::TerrainCoverageId coverageId,
        ConsoleRig::IProgress* progress)
    {
            // Export a uber surface file to tiff format.
        ::Assets::ResChar dirName[MaxPath];
        XlDirname(dirName, dimof(dirName), dstFile);
        OSServices::CreateDirectoryRecursive(dirName);

        ::Assets::ResChar srcFN[MaxPath];
        srcCfg.GetUberSurfaceFilename(srcFN, dimof(srcFN), srcDir, coverageId);
        if (!DoesFileExist(srcFN))
            Throw(::Exceptions::BasicLabel("Could not find input file (%s)", srcFN));

        TerrainUberSurfaceGeneric uberSurface(srcFN);
        auto step = 
              progress 
            ? progress->BeginStep("Create uber surface data", uberSurface.GetHeight(), true)
            : nullptr;

        auto* tif = TIFFOpen(dstFile, "w");
        if (!tif)
            Throw(::Exceptions::BasicLabel("Error openning output file (%s)", dstFile));
        auto autoClose = MakeAutoClose([tif]() { TIFFClose(tif); });

        TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, uberSurface.GetWidth());  // set the width of the image
        TIFFSetField(tif, TIFFTAG_IMAGELENGTH, uberSurface.GetHeight());    // set the height of the image
        TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);    // set the origin of the image.

        TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);

        auto fmt = uberSurface.Format();
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, fmt._arrayCount);
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, fmt.GetSize() * 8 / fmt._arrayCount);
        using TC = ImpliedTyping::TypeCat;
        switch (fmt._type) {
        case TC::Bool:
        case TC::Int8:
        case TC::Int16:
        case TC::Int32:
            TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_INT);
            break;

        case TC::UInt8:
        case TC::UInt16:
        case TC::UInt32:
            TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
            break;

        case TC::Float:
            TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);
            break;

        default:
            Throw(::Exceptions::BasicLabel("Unknown uber surface format, can't export"));
        }

        TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);
            // I think this will only work correctly with a single sample per pixel
        for (unsigned row = 0; row < uberSurface.GetHeight(); row++) {
            TIFFWriteScanline(tif, uberSurface.GetData(UInt2(0, row)), row, 0);

            if (step) {
                if (step->IsCancelled())
                    break;
                step->Advance();
            }
        }
    }

    void GenerateBlankUberSurface(
        const ::Assets::ResChar outputDir[], 
        unsigned cellCountX, unsigned cellCountY,
        unsigned destNodeDims, unsigned destCellTreeDepth,
        ConsoleRig::IProgress* progress)
    {
        OSServices::CreateDirectoryRecursive(outputDir);

        UInt2 finalDims(
            cellCountX * destNodeDims * (1<<(destCellTreeDepth-1)),
            cellCountY * destNodeDims * (1<<(destCellTreeDepth-1)));

        uint64_t resultSize = 
            sizeof(TerrainUberHeader)
            + finalDims[0] * finalDims[1] * sizeof(float)
            ;

        ::Assets::ResChar outputUberFileName[MaxPath]; 
        SceneEngine::TerrainConfig::GetUberSurfaceFilename(
            outputUberFileName, dimof(outputUberFileName),
            outputDir, SceneEngine::CoverageId_Heights);

        auto outputUberFile = ::Assets::MainFileSystem::OpenMemoryMappedFile(outputUberFileName, resultSize, "w");

        auto& hdr   = *(TerrainUberHeader*)outputUberFile.GetData().begin();
        hdr._magic  = TerrainUberHeader::Magic;
        hdr._width  = finalDims[0];
        hdr._height = finalDims[1];
        hdr._typeCat = (unsigned)ImpliedTyping::TypeCat::Float;
        hdr._typeArrayCount = 1;
        hdr._dummy[0] = hdr._dummy[1] = hdr._dummy[2]  = 0;

        float* outputArray = (float*)PtrAdd(outputUberFile.GetData().begin(), sizeof(TerrainUberHeader));
        std::fill(
            outputArray,
            &outputArray[finalDims[0] * finalDims[1]],
            0.f);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////
    static UInt2 GetUberSurfaceDimensions(const ::Assets::ResChar fn[])
    {
        auto file = ::Assets::MainFileSystem::OpenBasicFile(fn, "rb", OSServices::FileShareMode::Read|OSServices::FileShareMode::Write);
        TerrainUberHeader hdr;
        if ((file.Read(&hdr, sizeof(hdr), 1) != 1) || (hdr._magic != TerrainUberHeader::Magic))
            Throw(::Exceptions::BasicLabel("Error while reading from: (%s)", fn));
        return UInt2(hdr._width, hdr._height);
    }

    UInt2 GetCellCountFromUberSurface(
        const ::Assets::ResChar inputUberSurfaceDirectory[],
        UInt2 destNodeDims, unsigned destCellTreeDepth)
    {
        using namespace SceneEngine;

        ::Assets::ResChar uberSurfaceHeights[MaxPath]; 
        TerrainConfig::GetUberSurfaceFilename(
            uberSurfaceHeights, dimof(uberSurfaceHeights),
            inputUberSurfaceDirectory, SceneEngine::CoverageId_Heights);
        auto eleCount = GetUberSurfaceDimensions(uberSurfaceHeights);

        auto cellDimsInEles = (1 << (destCellTreeDepth - 1)) * destNodeDims;
        if ((eleCount[0] % cellDimsInEles[0])!=0 || (eleCount[1] % cellDimsInEles[1])!=0)
            Throw(::Exceptions::BasicLabel("Uber surface size is not divisable by cell size (uber surface size:(%ix%i), cell size:(%i))", 
            eleCount[0], eleCount[1], cellDimsInEles[0]));

        return UInt2(eleCount[0] / cellDimsInEles[0], eleCount[1] / cellDimsInEles[1]);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
    static void WriteNode(  
        float destination[], TerrainCell::Node& node, 
        const char sourceFileName[], const char secondaryCacheName[], 
        size_t stride, signed downsample)
    {
            // load the raw data either from the source file, or the cache file
        std::unique_ptr<uint16[]> rawData;
        
        const unsigned expectedCount = node._widthInElements*node._widthInElements;

            // todo -- check for incomplete nodes (ie, with holes)
        if (node._heightMapFileSize) {
            auto count = node._heightMapFileSize/sizeof(uint16);
            if (count == expectedCount) {
                OSServices::BasicFile file(sourceFileName, "rb");
                rawData = std::make_unique<uint16[]>(count);
                file.Seek(node._heightMapFileOffset, SEEK_SET);
                file.Read(rawData.get(), sizeof(uint16), count);
            }
        } else if (node._secondaryCacheSize) {
            auto count = node._secondaryCacheSize/sizeof(uint16);
            if (count == expectedCount) {
                OSServices::BasicFile file(secondaryCacheName, "rb");
                rawData = std::make_unique<uint16[]>(count);
                file.Seek(node._secondaryCacheOffset, SEEK_SET);
                file.Read(rawData.get(), sizeof(uint16), count);
            }
        }

        const unsigned dimsNoOverlay = node._widthInElements - node.GetOverlapWidth();

        for (unsigned y=0; y<dimsNoOverlay; ++y)
            for (unsigned x=0; x<dimsNoOverlay; ++x) {
                assert(((y*node._widthInElements)+x) < expectedCount);
                auto inputValue = rawData ? rawData[(y*node._widthInElements)+x] : uint16(0);

                float cellSpaceHeight = node._localToCell(2,2) * float(inputValue) + node._localToCell(2,3);
                
                assert(downsample==0);
                *PtrAdd(destination, y*stride + x*sizeof(float)) = cellSpaceHeight;
            }
    }

    static void WriteBlankNode(float destination[], size_t stride, signed downsample, const UInt2 elementDims)
    {
        assert(downsample==0);
        for (unsigned y=0; y<elementDims[1]; ++y)
            for (unsigned x=0; x<elementDims[0]; ++x) {
                *PtrAdd(destination, y*stride + x*sizeof(float)) = 0.f;
            }
    }

    static bool BuildUberSurfaceFile(
        const char filename[], const TerrainConfig& config, 
        ITerrainFormat* ioFormat,
        unsigned xStart, unsigned yStart, unsigned xDims, unsigned yDims)
    {
            //
            //  Read in the existing terrain data, and generate a uber surface file
            //        --  this uber surface file should contain the height values for the
            //            entire "world", at the highest resolution
            //  The "uber" surface file is initialized from the source crytek terrain files, 
            //  but becomes our new authoritative source for terrain data.
            //
        
        const unsigned cellCount        = xDims * yDims;
        const auto cellDimsInNodes      = config.CellDimensionsInNodes();
        const auto nodeDimsInElements   = config.NodeDimensionsInElements();
        const unsigned nodesPerCell     = cellDimsInNodes[0] * cellDimsInNodes[1];
        const unsigned heightsPerNode   = nodeDimsInElements[0] * nodeDimsInElements[1];

        size_t stride = nodeDimsInElements[0] * cellDimsInNodes[0] * xDims * sizeof(float);

        uint64_t resultSize = 
            sizeof(TerrainUberHeader)
            + cellCount * nodesPerCell * heightsPerNode * sizeof(float)
            ;
        MemoryMappedFile mappedFile(filename, resultSize, MemoryMappedFile::Access::Write, BasicFile::ShareMode::Read);
        if (!mappedFile.IsValid())
            return false;

        auto& hdr   = *(TerrainUberHeader*)mappedFile.GetData();
        hdr._magic  = TerrainUberHeader::Magic;
        hdr._width  = nodeDimsInElements[0] * cellDimsInNodes[0] * xDims;
        hdr._height = nodeDimsInElements[1] * cellDimsInNodes[1] * yDims;
        hdr._dummy  = 0;

        void* heightArrayStart = PtrAdd(mappedFile.GetData(), sizeof(TerrainUberHeader));

        TRY
        {
                // fill in the "uber" surface file with all of the terrain information
            for (unsigned cy=0; cy<yDims; ++cy)
                for (unsigned cx=0; cx<xDims; ++cx) {

                    char buffer[MaxPath];
                    config.GetCellFilename(buffer, dimof(buffer), UInt2(cx, cy),
                        CoverageId_Heights);
                    auto& cell = ioFormat->LoadHeights(buffer);

                        //  the last "field" in the input data should be the resolution that we want
                        //  however, if we don't have enough fields, we may have to upsample from
                        //  the lower resolution ones

                    if (cell._nodeFields.size() >= 5) {

                        auto& field = cell._nodeFields[4];
                        assert(field._widthInNodes == cellDimsInNodes[0]);
                        assert(field._heightInNodes == cellDimsInNodes[1]);
                        for (auto n=field._nodeBegin; n!=field._nodeEnd; ++n) {
                            auto& node = *cell._nodes[n];
                            unsigned nx = (unsigned)std::floor(node._localToCell(0, 3) / 64.f + 0.5f);
                            unsigned ny = (unsigned)std::floor(node._localToCell(1, 3) / 64.f + 0.5f);

                            auto* nodeDataStart = (float*)PtrAdd(
                                heightArrayStart, 
                                    (cy * cellDimsInNodes[1] + ny) * nodeDimsInElements[1] * stride 
                                +   (cx * cellDimsInNodes[0] + nx) * nodeDimsInElements[0] * sizeof(float));

                            WriteNode(
                                nodeDataStart, node, 
                                cell.SourceFile().c_str(), cell.SecondaryCacheFile().c_str(), 
                                stride, 0);
                        }

                    } else {

                        for (unsigned y=0; y<cellDimsInNodes[1]; ++y)
                            for (unsigned x=0; x<cellDimsInNodes[0]; ++x) {
                                auto* nodeDataStart = (float*)PtrAdd(
                                    heightArrayStart, 
                                        (cy * cellDimsInNodes[1] + y) * nodeDimsInElements[1] * stride 
                                    +   (cx * cellDimsInNodes[0] + x) * nodeDimsInElements[0] * sizeof(float));
                                WriteBlankNode(nodeDataStart, stride, 0, nodeDimsInElements);
                            }

                    }

                }
        }
        CATCH(...) { return false; } 
        CATCH_END

        return true;
    }
#endif

}
