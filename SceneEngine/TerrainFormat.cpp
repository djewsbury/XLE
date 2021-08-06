// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TerrainFormat.h"
#include "TerrainUberSurface.h"
#include "TerrainScaffold.h"
#include "../RenderCore/Format.h"
#include "../Assets/ChunkFile.h"
#include "../Assets/Assets.h"
#include "../Assets/IFileSystem.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../OSServices/RawFS.h"
#include "../Utility/PtrUtils.h"
#include "../Core/Types.h"

#include <stack>
#include <assert.h>

#include "../OSServices/WinAPI/IncludeWindows.h"

namespace SceneEngine
{
    using namespace RenderCore;

    namespace MainTerrainFormat
    {
        class TerrainCell : public SceneEngine::TerrainCell
        {
        public:
            TerrainCell(const char filename[]);
        };

        class TerrainCellTexture : public SceneEngine::TerrainCellTexture
        {
        public:
            TerrainCellTexture(const char filename[]);
        };

            //////////////////////////////////////////////////////////////////////////////////////////
        namespace Compression
        {
            enum Enum 
            {
                None,
                QuantRange      ///< high precision min-max range, with low precision values in between
            };
            typedef unsigned Type;
        }

        namespace DownsampleMethod
        {
            enum Enum { Average, Corner };
        }

        class CellDesc
        {
        public:
            class Header
            {
            public:
                struct Flags
                {
                    enum { EncodedGradientFlags = 1<<0 };
                    typedef unsigned BitField;
                };
                unsigned            _treeDepth;
                unsigned            _overlapCount;
                Flags::BitField     _flags;
                unsigned            _dummy[1];
            };
            Header _hdr;
        };

        class NodeDesc
        {
        public:
            class Header
            {
            public:
                unsigned    _nodeHeaderVersion;
                unsigned    _dataOffset;
                unsigned    _dataSize;
                unsigned    _dimensionsInElements;

                    //  The following elements define the texture format and compression
                    //  method used.
                    //      . _format is just a NativeFormat::Enum that represents the texture format
                    //      . Some data needs to be decompressed at run-time. The most common form of
                    //          decompression is used by most height data. We store floating point
                    //          min and scale values per node, and each element contains a quantized
                    //          lower precision height value.
                    //      . For compressed texture, _compressionType determines the type of compression
                    //          applied. When there are compression parameters (like the min and scale 
                    //          height values) _compressionDataSize specifies the amount of extra data stored
                    //          per node.
                Format              _format;
                Compression::Type   _compressionType;
                unsigned            _compressionDataSize;

                unsigned    _dummy[1];
            };
            Header _hdr;
        };

        static NodeDesc LoadNodeStructure(::Assets::IFileInterface& file)
        {
            NodeDesc result;
            file.Read(&result._hdr, sizeof(result._hdr), 1);
            assert(result._hdr._nodeHeaderVersion == 0);
            return result;
        }

        static const uint64 ChunkType_CoverageScaffold = ConstHash64<'Cove','rage','Scaf','fold'>::Value;
        static const uint64 ChunkType_CoverageData = ConstHash64<'Cove','rage','Data'>::Value;

        //////////////////////////////////////////////////////////////////////////////////////////

        TerrainCell::TerrainCell(const char filename[])
        {
            auto validationCallback = ::Assets::GetDepValSys().Make();

            std::vector<NodeField> nodeFields;
            std::vector<std::unique_ptr<Node>> nodes;

            auto file = ::Assets::MainFileSystem::OpenFileInterface(filename, "rb");

                // Load the chunks. We should have 2 chunks:
                //  . cell scaffold
                //  . height map data
            auto chunks = ::Assets::ChunkFile::LoadChunkTable(*file);

            ::Assets::ChunkFile::ChunkHeader scaffoldChunk;
            ::Assets::ChunkFile::ChunkHeader heightDataChunk;
            for (auto i=chunks.begin(); i!=chunks.end(); ++i) {
                if (i->_type == ChunkType_CoverageScaffold && !scaffoldChunk._fileOffset)   { scaffoldChunk = *i; }
                if (i->_type == ChunkType_CoverageData && !heightDataChunk._fileOffset)     { heightDataChunk = *i; }
            }

            if (!scaffoldChunk._fileOffset || !heightDataChunk._fileOffset) {
                Throw(::Exceptions::BasicLabel("Missing correct terrain chunks: %s", filename));
            }

            CellDesc cellDesc;
            file->Seek(scaffoldChunk._fileOffset);
            file->Read(&cellDesc._hdr, sizeof(cellDesc._hdr), 1);

            _encodedGradientFlags = !!(cellDesc._hdr._flags & CellDesc::Header::Flags::EncodedGradientFlags);

            {
                //  nodes are stored as a breadth-first quad tree, starting with
                //  a single node. The amount of data stored in each node may not
                //  be constant, so we can't predict where every node will be stored
                //  in the file.

                nodeFields.reserve(cellDesc._hdr._treeDepth);
                size_t nodeCount = 0;
                for (unsigned l=0; l<cellDesc._hdr._treeDepth; ++l) {
                    size_t fieldNodeCount = (1<<l) * (1<<l);
                    nodeFields.push_back(
                        NodeField(1<<l, 1<<l, unsigned(nodeCount), unsigned(nodeCount + fieldNodeCount)));
                    nodeCount += fieldNodeCount;
                }

                nodes.reserve(nodeCount);
                for (unsigned l=0; l<cellDesc._hdr._treeDepth; ++l) {
                    float xyDim = std::pow(2.f, -float(l));

                    for (unsigned y=0; y<(1u<<l); ++y) {
                        for (unsigned x=0; x<(1u<<l); ++x) {
                            auto loadInfo = LoadNodeStructure(*file);
                            if (loadInfo._hdr._nodeHeaderVersion != 0) {
                                Throw(::Exceptions::BasicLabel(
                                    "Unexpected version number in terrain node file: %s (node header version: %i)", 
                                    filename, loadInfo._hdr._nodeHeaderVersion));
                            }

                            float compressionData[2] = { 0.f, 1.f };
                            if (loadInfo._hdr._compressionDataSize) {
                                if (loadInfo._hdr._compressionType == Compression::QuantRange && loadInfo._hdr._compressionDataSize >= (sizeof(float)*2)) {
                                    file->Read(compressionData, sizeof(float), 2);
                                    file->Seek(loadInfo._hdr._compressionDataSize - sizeof(float)*2, OSServices::FileSeekAnchor::Current);
                                } else {
                                    file->Seek(loadInfo._hdr._compressionDataSize, OSServices::FileSeekAnchor::Current);
                                }
                            }

                            Float4x4 localToCell(
                                xyDim, 0.f, 0.f, float(x) * xyDim,
                                0.f, xyDim, 0.f, float(y) * xyDim,
                                0.f, 0.f, compressionData[1], compressionData[0],
                                0.f, 0.f, 0.f, 1.f);

                            auto node = std::make_unique<Node>(
                                localToCell, loadInfo._hdr._dataOffset + heightDataChunk._fileOffset, 
                                loadInfo._hdr._dataSize, loadInfo._hdr._dimensionsInElements);

                            nodes.push_back(std::move(node));
                        }
                    }
                }
            }

            _sourceFileName = filename;
            _nodeFields = std::move(nodeFields);
            _nodes = std::move(nodes);

            validationCallback.RegisterDependency(filename);
            _validationCallback = std::move(validationCallback);
        }

        static unsigned NodeCountFromTreeDepth(unsigned treeDepth)
        {
            unsigned nodeCount = 0;
            for (unsigned l=0; l<treeDepth; ++l) {
                unsigned fieldNodeCount = (1<<l) * (1<<l);
                nodeCount += fieldNodeCount;
            }
            return nodeCount;
        }

        TerrainCellTexture::TerrainCellTexture(const char filename[])
        {
            _nodeTextureByteCount = 0;
            _fieldCount = 0;
            auto validationCallback = ::Assets::GetDepValSys().Make();
			validationCallback.RegisterDependency(filename);

            TRY {
                auto file = ::Assets::MainFileSystem::OpenFileInterface(filename, "rb");

                    // Load the chunks. We should have 2 chunks:
                    //  . cell scaffold
                    //  . coverage data
                auto chunks = ::Assets::ChunkFile::LoadChunkTable(*file);

                ::Assets::ChunkFile::ChunkHeader scaffoldChunk;
                ::Assets::ChunkFile::ChunkHeader coverageDataChunk;
                for (auto i=chunks.begin(); i!=chunks.end(); ++i) {
                    if (i->_type == ChunkType_CoverageScaffold && !scaffoldChunk._fileOffset) { scaffoldChunk = *i; }
                    if (i->_type == ChunkType_CoverageData && !coverageDataChunk._fileOffset) { coverageDataChunk = *i; }
                }

                if (!scaffoldChunk._fileOffset || !coverageDataChunk._fileOffset) {
                    Throw(::Exceptions::BasicLabel("Missing correct terrain chunks: %s", filename));
                }

                CellDesc cellDesc;
                file->Seek(scaffoldChunk._fileOffset);
                file->Read(&cellDesc._hdr, sizeof(cellDesc._hdr), 1);

                std::vector<unsigned> fileOffsetsBreadthFirst;
        
                {
                    //  nodes are stored as a breadth-first quad tree, starting with
                    //  a single node. The amount of data stored in each node may not
                    //  be constant, so we can't predict where every node will be stored
                    //  in the file.

                    auto nodeCount = NodeCountFromTreeDepth(cellDesc._hdr._treeDepth);
                    fileOffsetsBreadthFirst.reserve(nodeCount);
                    for (unsigned l=0; l<cellDesc._hdr._treeDepth; ++l) {
                        for (unsigned y=0; y<(1u<<l); ++y) {
                            for (unsigned x=0; x<(1u<<l); ++x) {
                                auto loadInfo = LoadNodeStructure(*file);
                                fileOffsetsBreadthFirst.push_back(loadInfo._hdr._dataOffset + coverageDataChunk._fileOffset);
                                if (!_nodeTextureByteCount) {
                                    _nodeTextureByteCount = loadInfo._hdr._dataSize;
                                } else {
                                        // assert all nodes have the same size data
                                    assert(loadInfo._hdr._dataSize == _nodeTextureByteCount);
                                }
                            }
                        }
                    }
                }

                _fieldCount = (unsigned)cellDesc._hdr._treeDepth;
                _sourceFileName = filename;
                _nodeFileOffsets = std::move(fileOffsetsBreadthFirst);
                _validationCallback = std::move(validationCallback);
            } CATCH(const ::Assets::Exceptions::ConstructionError& e) {
				Throw(::Assets::Exceptions::ConstructionError(e, validationCallback));
			} CATCH (const std::exception& e) {
				Throw(::Assets::Exceptions::ConstructionError(e, validationCallback));
			}
            CATCH_END
        }


        ////////////////////////////////////////////////////////////////////////////////////////////////

        template<typename Element> Element Add(Element lhs, Element rhs)            { return lhs + rhs; }
        template<typename Element> Element Divide(Element lhs, unsigned rhs)        { return Element(lhs / rhs); }
        template<typename Element> float AsScalar(Element in)                       { return float(in); }
        template<typename Element> void Zero(Element& dst)                          { dst = Element(0); }

        template<typename A, typename B> std::pair<A, B> Add(std::pair<A, B> lhs, std::pair<A, B> rhs)      { return std::make_pair(A(lhs.first + rhs.first), B(lhs.second + rhs.second)); }
        template<typename A, typename B> std::pair<A, B> Divide(std::pair<A, B> lhs, unsigned rhs)          { return std::make_pair(A(lhs.first / rhs), B(lhs.second / rhs)); }
        template<typename A, typename B> float AsScalar(std::pair<A, B> in)         { return float(in.first); }
        template<typename A, typename B> void Zero(std::pair<A, B>& dst)            { dst = std::pair<A, B>(A(0), B(0)); }

        template<typename Element> Format AsFormat() { return Format::Unknown; }

        template<> Format AsFormat<float>()                      { return Format::R32_FLOAT; }
        template<> Format AsFormat<uint16>()                     { return Format::R16_UINT; }
        template<> Format AsFormat<uint8>()                      { return Format::R8_UINT; }
        template<> Format AsFormat<int16>()                      { return Format::R16_SINT; }
        template<> Format AsFormat<int8>()                       { return Format::R8_SINT; }
        template<> Format AsFormat<std::pair<float, float>>()    { return Format::R32G32_FLOAT; }
        template<> Format AsFormat<std::pair<uint16, uint16>>()  { return Format::R16G16_UNORM; }       // note -- UNORM (not UINT). Required for shadow samples to work right

        class CoverageDataResult
        {
        public:
            std::vector<uint8> _compressionData;
            Format _nativeFormat;
            unsigned _rawDataSize;

            CoverageDataResult(
                std::vector<uint8>&& compressionData, Format nativeFormat, unsigned rawDataSize)
            : _compressionData(std::forward<std::vector<uint8>>(compressionData))
            , _nativeFormat(nativeFormat)
            , _rawDataSize(rawDataSize) {}
        };

        template<typename Element>
            Element GetValue(TerrainUberSurfaceGeneric& surf, UInt2 coord)
            {
                void* d = surf.GetData(coord);
                if (!d) return SceneEngine::Internal::DummyValue<Element>();
                return *(Element*)d;
            }

        namespace Internal
        {
            static const float ScharrConstant3x3 = 1.f/32.f;
            static const float ScharrHoriz3x3[3][3] =
            {
                {  -3.f * ScharrConstant3x3, 0.f,  3.f * ScharrConstant3x3 },
                { -10.f * ScharrConstant3x3, 0.f, 10.f * ScharrConstant3x3 },
                {  -3.f * ScharrConstant3x3, 0.f,  3.f * ScharrConstant3x3 },
            };
            static const float ScharrVert3x3[3][3] =
            {
                {  -3.f * ScharrConstant3x3, -10.f * ScharrConstant3x3,  -3.f * ScharrConstant3x3 },
                { 0.f, 0.f, 0.f },
                {   3.f * ScharrConstant3x3,  10.f * ScharrConstant3x3,   3.f * ScharrConstant3x3 },
            };
        }

        template<typename Element>
            static Float2 CalculateDHDXY(TerrainUberSurfaceGeneric& surface, UInt2 coord)
        {
            auto centerHeight = GetValue<Element>(surface, coord);
            Float2 dhdxy(0.f, 0.f);
            for (int y=0; y<3; ++y) {
                for (int x=0; x<3; ++x) {
                    Int2 c = Int2(coord) + Int2(x-1, y-1);
                    if (c[0] >= 0 && c[1] >= 0 && c[0] < (int)surface.GetWidth() && c[1] < (int)surface.GetHeight()) {
                        float heightDiff = *(Element*)surface.GetDataFast(c) - centerHeight;
                        dhdxy[0] += Internal::ScharrHoriz3x3[x][y] * heightDiff;
                        dhdxy[1] +=  Internal::ScharrVert3x3[x][y] * heightDiff;
                    }
                }
            }
            return dhdxy;
        }

        template<typename Element>
            static unsigned CalculateGradientFlag(TerrainUberSurfaceGeneric&, UInt2 , const GradientFlagsSettings&)
            {
                return 0;
            }

        template<>
            unsigned CalculateGradientFlag<float>(
                TerrainUberSurfaceGeneric& surface, UInt2 coord, 
                const GradientFlagsSettings& settings)
            {
                    // Calculate the gradient flags for the element at the given coordinate

                const float spacing = settings._elementSpacing;

                #if 0
                    const Int2 offsets[] =
                    {
                        Int2( 0, -1), Int2( 0,  1), 
                        Int2(-1,  0), Int2( 1,  0)
                    };
                    float heightDiff[dimof(offsets)];

                    float centerHeight = surface.GetValue(coord[0], coord[1]);
                    for (uint c2=0; c2<dimof(offsets); c2++) {
                        Int2 c = Int2(coord) + offsets[c2];
                        if (c[0] >= 0 && c[1] >= 0 && c[0] < (int)surface.GetWidth() && c[1] < (int)surface.GetHeight()) {
                            heightDiff[c2] = surface.GetValueFast(c[0], c[1]) - centerHeight;
                        } else {
                            heightDiff[c2] = 0.f;
                        }
                    }

                    Float3 b( 0.f, -spacing, heightDiff[0]);
                    Float3 t( 0.f,  spacing, heightDiff[1]);
                    Float3 l(-spacing,  0.f, heightDiff[2]);
                    Float3 r( spacing,  0.f, heightDiff[3]);

                    bool centerIsSlope = std::max(XlAbs(dhdxy[0]), XlAbs(dhdxy[1])) > slopeThreshold;
                    auto result = int(centerIsSlope);

                    bool topBottomTrans = dot(normalize(b), -normalize(t)) < transThreshold;
                    bool leftRightTrans = dot(normalize(l), -normalize(r)) < transThreshold;
                    if (leftRightTrans || topBottomTrans) result |= 2;
                    return result;
                #endif

                Float2 dhdxy = CalculateDHDXY<float>(surface, coord) / spacing;
                float slope = std::max(XlAbs(dhdxy[0]), XlAbs(dhdxy[1]));
                if (slope < settings._slopeThresholds[0]) return 0;
                if (slope < settings._slopeThresholds[1]) return 1;
                if (slope < settings._slopeThresholds[2]) return 2;
                return 3;
            }

        template<typename Element, typename File>
            static CoverageDataResult WriteCoverageData(
				File& destinationFile, TerrainUberSurfaceGeneric& surface,
                unsigned startx, unsigned starty, signed downsample, unsigned dimensionsInElements,
                const GradientFlagsSettings& gradFlagsSettings, Compression::Enum compression)
        {
            float minValue =  std::numeric_limits<float>::max();
            float maxValue = -std::numeric_limits<float>::max();
            auto sampledValues = std::make_unique<Element[]>(dimensionsInElements*dimensionsInElements);
            XlSetMemory(sampledValues.get(), 0, dimensionsInElements*dimensionsInElements*sizeof(Element));

            unsigned kw2 = 1<<downsample;
            for (unsigned y=0; y<dimensionsInElements; ++y)
                for (unsigned x=0; x<dimensionsInElements; ++x) {

                        //  "Corner" method is required for the LOD to work correctly on node
                        //  boundaries. We need adjacent tiles to match,
                        //  even if they are at different LOD levels. When a high-LOD tile needs
                        //  to match a low-LOD neighbour, we just skip every second sample.
                        //  So, we have to do the same here, when we downsample.
                    const DownsampleMethod::Enum downsampleMethod = DownsampleMethod::Corner;

                        //  first, we need to downsample the source data to get the 
                        //  correct values. Simple box filter currently. I'm not sure
                        //  what the best filter for height data is -- but maybe we
                        //  want to try something that will preserve large details in the 
                        //  distance 
                        //      (ie, so that mountains, etc, don't collapse into nothing)
                    Element k; 
                    Zero(k);
                    if (constant_expression<downsampleMethod == DownsampleMethod::Average>::result()) {
                        for (unsigned ky=0; ky<kw2; ++ky)
                            for (unsigned kx=0; kx<kw2; ++kx)
                                k = Add(k, GetValue<Element>(surface, UInt2(startx + kw2*x + kx, starty + kw2*y + ky)));
                        k = Divide(k, kw2*kw2);
                    } else if (constant_expression<downsampleMethod == DownsampleMethod::Corner>::result()) {
                        k = GetValue<Element>(surface, UInt2(startx + kw2*x, starty + kw2*y));
                    }

                    minValue = std::min(minValue, AsScalar(k));
                    maxValue = std::max(maxValue, AsScalar(k));
                    sampledValues[y*dimensionsInElements+x] = k;

                }

            if (compression == Compression::QuantRange) {

                const bool encodedGradientFlags = gradFlagsSettings._enable;
                const auto compressedHeightMask = encodedGradientFlags ? 0x3fffu : 0xffffu;
                auto sampledGradientFlags = std::make_unique<uint16[]>(dimensionsInElements*dimensionsInElements);

                if (encodedGradientFlags) {

                    const unsigned kw = 1<<downsample;
                    for (unsigned y=0; y<dimensionsInElements; ++y)
                        for (unsigned x=0; x<dimensionsInElements; ++x) {
                            unsigned counts[4] = { 0u, 0u, 0u, 0u };
                            
                            for (unsigned ky=0; ky<kw; ++ky)
                                for (unsigned kx=0; kx<kw; ++kx) {
                                    unsigned flag = CalculateGradientFlag<Element>(surface, UInt2(startx + kw*x + kx, starty + kw*y + ky), gradFlagsSettings);
                                    if (flag < dimof(counts)) ++counts[flag];
                                }
                            
                                // We choose the value that is most common
                                // average isn't actually right, because it runs
                                // the risk of producing a result that doesn't exist
                                // in the top-LOD data at all!
                            unsigned result = 0;
                            for (unsigned c=1; c<4; ++c)
                                if (counts[c] > counts[result]) result = c;
                            sampledGradientFlags[y*dimensionsInElements+x] = (uint16)(result<<14);
                        }

                } else {
                    XlSetMemory(sampledGradientFlags.get(), 0, sizeof(uint16)*dimensionsInElements*dimensionsInElements);
                }

                auto compressedHeightData = std::make_unique<uint16[]>(dimensionsInElements*dimensionsInElements);
                for (unsigned y=0; y<dimensionsInElements; ++y)
                    for (unsigned x=0; x<dimensionsInElements; ++x) {
                        auto s = AsScalar(sampledValues[y*dimensionsInElements+x]);
                        float ch = (s - minValue) * float(compressedHeightMask) / (maxValue - minValue);

                        uint16 compressedValue = (uint16)std::min(float(compressedHeightMask), std::max(0.f, ch));
                        compressedValue |= sampledGradientFlags[y*dimensionsInElements+x];
                        compressedHeightData[y*dimensionsInElements+x] = compressedValue;
                    }

                    // write all these results to the file...
                auto rawDataSize = sizeof(uint16)*dimensionsInElements*dimensionsInElements;
                destinationFile.Write(compressedHeightData.get(), rawDataSize, 1);

                std::vector<uint8> compressionData;
                compressionData.resize(sizeof(float)*2);
                *(std::pair<float, float>*)AsPointer(compressionData.begin()) = std::make_pair(minValue, (maxValue - minValue) / float(compressedHeightMask));
                return CoverageDataResult(std::move(compressionData), Format::R16_UINT, (unsigned)rawDataSize);

            } else if (compression == Compression::None) {

                auto rawDataSize = sizeof(Element)*dimensionsInElements*dimensionsInElements;
                destinationFile.Write(sampledValues.get(), rawDataSize, 1);
                return CoverageDataResult(std::vector<uint8>(), AsFormat<Element>(), (unsigned)rawDataSize);

            } else {
                return CoverageDataResult(std::vector<uint8>(), Format::Unknown, 0);
            }
        }

        template<typename Element>
            static void WriteCellFromUberSurface(
                const char destinationFile[], TerrainUberSurfaceGeneric& surface, 
                UInt2 cellMins, UInt2 cellMaxs, unsigned treeDepth, unsigned overlapElements,
                const GradientFlagsSettings& gradFlagsSettings,
                Compression::Enum compression, std::pair<const char*, const char*> versionInfo)
        {
            using namespace Assets::ChunkFile;

            const unsigned chunkCount = 2;
            SimpleChunkFileWriter outputFile(
				::Assets::MainFileSystem::OpenBasicFile(destinationFile, "wb", OSServices::FileShareMode::Read),
                chunkCount, versionInfo.first, versionInfo.second);

                //  write an area of the uber surface to our native terrain format
            auto nodeCount = NodeCountFromTreeDepth(treeDepth);
            unsigned compressionDataPerNode = 0;
            if (compression == Compression::QuantRange)
                compressionDataPerNode = sizeof(float)*2;
            std::vector<uint8> nodeHeaders;
            nodeHeaders.resize(nodeCount*(sizeof(NodeDesc::Header) + compressionDataPerNode), 0);

            unsigned uniqueElementsDimension = 
                std::min(cellMaxs[0] - cellMins[0], cellMaxs[1] - cellMins[1]) / (1u<<(treeDepth-1));

            outputFile.BeginChunk(ChunkType_CoverageScaffold, 0, "Scaffold");

            CellDesc::Header cellDescHeader;
            cellDescHeader._treeDepth = treeDepth;
            cellDescHeader._overlapCount = overlapElements;
            cellDescHeader._flags = 0;
            if (gradFlagsSettings._enable) cellDescHeader._flags |= CellDesc::Header::Flags::EncodedGradientFlags;
            XlZeroMemory(cellDescHeader._dummy);
            outputFile.Write(&cellDescHeader, sizeof(cellDescHeader), 1);

                // Skip over the node header array for now
            auto nodeHeaderArray = outputFile.TellP();
            outputFile.Seek(nodeHeaders.size(), OSServices::FileSeekAnchor::Current);

                //  Now write the header for the height data part
                //  At the moment each node has the same amount of height data...
                //  If it varies, we would need a separate pass to calculate the 
                //  total size of this chunk before we write it.
            outputFile.BeginChunk(ChunkType_CoverageData, 0, "Data");

            unsigned heightDataOffsetIterator = 0;
            unsigned nodeIndex = 0;
            for (unsigned l=0; l<treeDepth; ++l) {
                for (unsigned y=0; y<(1u<<l); ++y) {
                    for (unsigned x=0; x<(1u<<l); ++x, ++nodeIndex) {
                        NodeDesc::Header nodeHdr;
                        nodeHdr._nodeHeaderVersion = 0;
                        nodeHdr._dimensionsInElements = uniqueElementsDimension + overlapElements;
                        std::fill(nodeHdr._dummy, &nodeHdr._dummy[dimof(nodeHdr._dummy)], 0);

                        signed downsample = treeDepth-1-l;
                        unsigned skip = 1 << downsample;
                        unsigned rawCoordX = cellMins[0] + x * uniqueElementsDimension * skip;
                        unsigned rawCoordY = cellMins[1] + y * uniqueElementsDimension * skip;

                        auto p = WriteCoverageData<Element>(
                            outputFile, surface, rawCoordX, rawCoordY,
                            downsample, uniqueElementsDimension + overlapElements, 
                            gradFlagsSettings, compression);
                        assert(p._compressionData.size() == compressionDataPerNode);

                        nodeHdr._dataOffset = heightDataOffsetIterator;
                        nodeHdr._dataSize = p._rawDataSize;
                        heightDataOffsetIterator += nodeHdr._dataSize;

                        nodeHdr._compressionType = compression;
                        nodeHdr._compressionDataSize = compressionDataPerNode;
                        nodeHdr._format = p._nativeFormat;

                        auto* hdr = (NodeDesc::Header*)PtrAdd(AsPointer(nodeHeaders.begin()), nodeIndex * (sizeof(NodeDesc::Header) + compressionDataPerNode));
                        *hdr = nodeHdr;
                        XlCopyMemory(PtrAdd(hdr, sizeof(NodeDesc::Header)), AsPointer(p._compressionData.begin()), p._compressionData.size());
                    }
                }
            }
            assert(nodeIndex == nodeCount);
            outputFile.FinishCurrentChunk();

                // go back and write the node headers in the node header chunk
            outputFile.Seek(nodeHeaderArray);
            outputFile.Write(AsPointer(nodeHeaders.begin()), nodeHeaders.size(), 1);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    const TerrainCell& TerrainFormat::LoadHeights(const char filename[], bool skipDependsCheck) const
    {
        if (skipDependsCheck) {
            return ::Assets::Legacy::GetAsset<MainTerrainFormat::TerrainCell>(filename);
        }
        return ::Assets::Legacy::GetAssetDep<MainTerrainFormat::TerrainCell>(filename);
    }

    const TerrainCellTexture& TerrainFormat::LoadCoverage(const char filename[]) const
    {
        return ::Assets::Legacy::GetAssetDep<MainTerrainFormat::TerrainCellTexture>(filename);
    }

    void TerrainFormat::WriteCell(
        const char destinationFile[], TerrainUberSurfaceGeneric& surface, 
        UInt2 cellMins, UInt2 cellMaxs, unsigned treeDepth, unsigned overlapElements) const
    {
		auto libVersion = ConsoleRig::GetLibVersionDesc();
		auto ver = std::make_pair(libVersion._versionString, libVersion._buildDateString);
        if (surface.Format() == ImpliedTyping::TypeOf<float>()) {
            MainTerrainFormat::WriteCellFromUberSurface<float>(
                destinationFile, surface, 
                cellMins, cellMaxs, treeDepth, overlapElements, _gradFlagsSettings,
                MainTerrainFormat::Compression::QuantRange, 
                ver);
        } else if (surface.Format() == ImpliedTyping::TypeOf<ShadowSample>()) {
            MainTerrainFormat::WriteCellFromUberSurface<ShadowSample>(
                destinationFile, surface, 
                cellMins, cellMaxs, treeDepth, overlapElements, _gradFlagsSettings,
                MainTerrainFormat::Compression::None, 
                ver);
        } else if (surface.Format() == ImpliedTyping::TypeOf<uint8>()) {
            MainTerrainFormat::WriteCellFromUberSurface<uint8>(
                destinationFile, surface, 
                cellMins, cellMaxs, treeDepth, overlapElements, _gradFlagsSettings,
                MainTerrainFormat::Compression::None, 
                ver);
        } else if (surface.Format() == ImpliedTyping::TypeOf<uint16>()) {
            MainTerrainFormat::WriteCellFromUberSurface<uint16>(
                destinationFile, surface, 
                cellMins, cellMaxs, treeDepth, overlapElements, _gradFlagsSettings,
                MainTerrainFormat::Compression::None, 
                ver);
        } else if (surface.Format() == ImpliedTyping::TypeOf<int8>()) {
            MainTerrainFormat::WriteCellFromUberSurface<int8>(
                destinationFile, surface, 
                cellMins, cellMaxs, treeDepth, overlapElements, _gradFlagsSettings,
                MainTerrainFormat::Compression::None, 
                ver);
        } else if (surface.Format() == ImpliedTyping::TypeOf<int16>()) {
            MainTerrainFormat::WriteCellFromUberSurface<int16>(
                destinationFile, surface, 
                cellMins, cellMaxs, treeDepth, overlapElements, _gradFlagsSettings,
                MainTerrainFormat::Compression::None, 
                ver);
        }
    }

    TerrainFormat::TerrainFormat(const GradientFlagsSettings& gradFlagsSettings)
    : _gradFlagsSettings(gradFlagsSettings) {}

    TerrainFormat::~TerrainFormat() {}


    GradientFlagsSettings::GradientFlagsSettings(
        bool enable, float elementSpacing,
        float slope0Threshold, float slope1Threshold, float slope2Threshold)
    {
        _enable = enable;
        _elementSpacing = elementSpacing;
        _slopeThresholds[0] = slope0Threshold;
        _slopeThresholds[1] = slope1Threshold;
        _slopeThresholds[2] = slope2Threshold;
    }

}

