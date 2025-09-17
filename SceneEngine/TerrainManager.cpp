// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Terrain.h"
#include "TerrainRender.h"
#include "TerrainConfig.h"
#include "TerrainScaffold.h"
#include "TerrainUberSurface.h"
#include "TerrainMaterialTextures.h"
#include "TerrainMaterial.h"
#include "SurfaceHeightsProvider.h"
#include "TerrainFormat.h"
#include "PreparedScene.h"
#include "SceneEngineUtils.h"
#include "LightingParser.h"
#include "MetalStubs.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../BufferUploads/DataPacket.h"
#include "../BufferUploads/ResourceLocator.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Metal/TextureView.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/ResourceUtils.h"
#include "../Math/Transformations.h"
#include "../Math/Geometry.h"
#include "../Assets/Assets.h"
#include "../OSServices/Log.h"
#include "../ConsoleRig/Console.h"
#include "../ConsoleRig/IProgress.h"
#include "../Utility/StringFormat.h"
#include <stack>
#include <utility>

#include "LightDesc.h"              // for getting sun direction
#include "SceneParser.h"            // for getting sun direction

namespace SceneEngine
{
    using namespace RenderCore;

    //////////////////////////////////////////////////////////////////////////////////////////

    class TerrainSurfaceHeightsProvider : public ISurfaceHeightsProvider
    {
    public:
        virtual SRV         GetSRV();
        virtual Addressing  GetAddress(Float2 minCoord, Float2 maxCoord);
        virtual bool        IsFloatFormat() const;

        void SetCoords(
            const TerrainConfig& terrainConfig, 
            const TerrainCoordinateSystem& coordSystem);

        TerrainSurfaceHeightsProvider(  
            std::shared_ptr<TerrainCellRenderer> terrainRenderer, 
            const TerrainConfig& terrainConfig,
            const TerrainCoordinateSystem& coordSystem);

    private:
        std::shared_ptr<TerrainCellRenderer> _terrainRenderer;
        TerrainCoordinateSystem _coordSystem;
        TerrainConfig _terrainConfig;
    };

    auto TerrainSurfaceHeightsProvider::GetSRV() -> SRV
    {
        return _terrainRenderer->_heightMapTileSet->GetShaderResource();
    }

    bool TerrainSurfaceHeightsProvider::IsFloatFormat() const { return false; }

    static bool PointInside(const Float2& pt, const Float2& mins, const Float2& maxs)
    {
        return  pt[0] >= mins[0] && pt[1] >= mins[1]
            &&  pt[0] <= maxs[0] && pt[1] <= maxs[1];
    }

    auto  TerrainSurfaceHeightsProvider::GetAddress(Float2 minCoord, Float2 maxCoord) -> Addressing
    {
        Addressing result;
        result._valid = false;
        result._heightScale = result._heightOffset = 0;

            //  We need to find the node that contains this position
            //  and return the coordinates of the height map texture (in the tile set)
            
        auto worldToCellBased = _coordSystem.WorldToCellBased();

            //  We're going to assume that both points are in the same cell.
            //  We can only return a contiguous texture if they both belong to the same cell
        Float2 cellCoordMin = Truncate(TransformPoint(worldToCellBased, Expand(minCoord, 0.f)));
        Float2 cellCoordMax = Truncate(TransformPoint(worldToCellBased, Expand(maxCoord, 0.f)));

        if (cellCoordMin[0] < 0.f || cellCoordMin[1] < 0.f) return result;  // can't deal with negative coords currently

        UInt2 cellIndex = UInt2(unsigned(XlFloor(cellCoordMin[0])), unsigned(XlFloor(cellCoordMin[1])));
        assert(unsigned(XlFloor(cellCoordMax[0] - 1e-5f)) == cellIndex[0]);
        assert(unsigned(XlFloor(cellCoordMax[1] - 1e-5f)) == cellIndex[1]);

            //  Currently we don't have a strong mapping between world space and rendered terrain
            //  we have to calculate the names of the height map and coverage files, and then look
            //  for those names in the TerrainCellRenderer's cache
            //      -- maybe there's a better way to go directly to a hash value?
        char heightMapFile[MaxPath], coverageFile[MaxPath];
        _terrainConfig.GetCellFilename(heightMapFile, dimof(heightMapFile), cellIndex, CoverageId_Heights);
        auto hash = Hash64(heightMapFile);

        for (unsigned c = 0; c<_terrainConfig.GetCoverageLayerCount(); ++c) {
            _terrainConfig.GetCellFilename(coverageFile, dimof(coverageFile), cellIndex, _terrainConfig.GetCoverageLayer(c)._id);
            hash = Hash64(coverageFile, hash);
        }
        
        auto i = std::lower_bound(
            _terrainRenderer->_renderInfos.begin(), _terrainRenderer->_renderInfos.end(), 
            hash, CompareFirst<uint64_t, std::unique_ptr<TerrainCellRenderer::CellRenderInfo>>());
        if (i!=_terrainRenderer->_renderInfos.end() && i->first == hash) {

                //  We found the correct cell...
                //
                //  Now we need to find the node within it at the maximum
                //  detail level (or maybe whatever is the highest detail level
                //  that we currently have loaded).
                //  We can't go directly to the node... we need to find it within
                //  in the list.
                //  
                //  We want to find the highest detail node that contains both 
                //  points (and is actually loaded). Often it should mean going
                //  down to the highest detail level

            auto& sourceCell = *i->second->_sourceCell;
            const unsigned startLod = 0;
            auto& field2 = sourceCell._nodeFields[startLod];
            Float2 cellSpaceSearchMin = (cellCoordMin - Float2(cellIndex));
            Float2 cellSpaceSearchMax = (cellCoordMax - Float2(cellIndex));

            std::stack<std::pair<unsigned, unsigned>> pendingNodes;
            for (unsigned n=0; n<field2._nodeEnd - field2._nodeBegin; ++n)
                pendingNodes.push(std::make_pair(startLod, n));

            const unsigned compressedHeightMask = 
                CompressedHeightMask(sourceCell.EncodedGradientFlags());

            while (!pendingNodes.empty()) {
                auto nodeRef = pendingNodes.top(); pendingNodes.pop();
                auto& field = sourceCell._nodeFields[nodeRef.first];
                unsigned n = field._nodeBegin + nodeRef.second;

                auto& sourceNode = sourceCell._nodes[n];

                    // (we can simplify this by making some assumptions about localToCell...)
                Float3 mins = TransformPoint(sourceNode->_localToCell, Float3(0.f, 0.f, 0.f));
                Float3 maxs = TransformPoint(sourceNode->_localToCell, Float3(1.f, 1.f, float(compressedHeightMask)));
                if (    PointInside(cellSpaceSearchMin, Truncate(mins), Truncate(maxs)) 
                    &&  PointInside(cellSpaceSearchMax, Truncate(mins), Truncate(maxs))) {

                    auto& node = i->second->_heightTiles[n];
                    if (node._tile._width > 0) {

                            //  This node is a valid result.
                            //  But it may not be the best result.
                            //      we can attempt to search deeper in the tree
                            //      to find a better result

                        result._baseCoordinate[0] = node._tile._x;
                        result._baseCoordinate[1] = node._tile._y;
                        result._baseCoordinate[2] = node._tile._arrayIndex;
                        result._heightScale = sourceNode->_localToCell(2,2);
                        result._heightOffset = sourceNode->_localToCell(2,3) + _coordSystem.TerrainOffset()[2];

                            //  what is the coordinate in our texture for the "minCoord" ? 
                            //  we want an actual pixel location

                            //  We can't use "InvertOrthonormalTransform" here, because there are
                            //  scales on the matrix. But we could simplify this by making some assumptions
                            //  (and only taking into account the X & Y transformations)
                        auto cellToLocal = Inverse(sourceNode->_localToCell);
                        Float2 nodeLocalMin = Truncate(TransformPoint(cellToLocal, Expand(cellSpaceSearchMin, 0.f)));
                        Float2 nodeLocalMax = Truncate(TransformPoint(cellToLocal, Expand(cellSpaceSearchMax, 0.f)));
                            
                        const unsigned textureDims = sourceNode->_widthInElements;
                        const unsigned overlapWidth = sourceNode->GetOverlapWidth();
                        result._minCoordOffset = UInt2(unsigned(nodeLocalMin[0] * float(textureDims-overlapWidth)), unsigned(nodeLocalMin[1] * float(textureDims-overlapWidth)));
                        result._maxCoordOffset = UInt2(unsigned(nodeLocalMax[0] * float(textureDims-overlapWidth)), unsigned(nodeLocalMax[1] * float(textureDims-overlapWidth)));

                        result._valid = true;

                    }

                        //  if we get a bounding box success on this node, they we know
                        //  that all siblings must not be valid. The only future node's
                        //  we're interested in are this node's children.
                        //      (why is there no clear for a stack?)
                    while (!pendingNodes.empty()) pendingNodes.pop();

                    if ((nodeRef.first+1) < sourceCell._nodeFields.size()) {

                            // push in the higher quality nodes
                            //      note that if we were stricter with how we store nodes
                            //      we could just select the particular node based on the quadrant
                            //      the points appear in
                        unsigned fieldx = nodeRef.second % field._widthInNodes;
                        unsigned fieldy = nodeRef.second / field._widthInNodes;
                        pendingNodes.push(std::make_pair(nodeRef.first+1, (fieldy*2+0) * (field._widthInNodes*2) + (fieldx*2+0)));
                        pendingNodes.push(std::make_pair(nodeRef.first+1, (fieldy*2+0) * (field._widthInNodes*2) + (fieldx*2+1)));
                        pendingNodes.push(std::make_pair(nodeRef.first+1, (fieldy*2+1) * (field._widthInNodes*2) + (fieldx*2+0)));
                        pendingNodes.push(std::make_pair(nodeRef.first+1, (fieldy*2+1) * (field._widthInNodes*2) + (fieldx*2+1)));

                    }

                }
            }

        }

        return result;
    }

    void TerrainSurfaceHeightsProvider::SetCoords(const TerrainConfig& terrainConfig, const TerrainCoordinateSystem& coordSystem)
    {
        _terrainConfig = terrainConfig;
        _coordSystem = coordSystem;
    }

    TerrainSurfaceHeightsProvider::TerrainSurfaceHeightsProvider(
        std::shared_ptr<TerrainCellRenderer> terrainRenderer,
        const TerrainConfig& terrainConfig,
        const TerrainCoordinateSystem& coordSystem)
    : _terrainRenderer(std::move(terrainRenderer))
    , _coordSystem(coordSystem)
    , _terrainConfig(terrainConfig)
    {}

    extern ISurfaceHeightsProvider* MainSurfaceHeightsProvider;

    //////////////////////////////////////////////////////////////////////////////////////////
    class TerrainManager::Pimpl
    {
    public:
        std::shared_ptr<TerrainCellRenderer> _renderer;
        std::shared_ptr<TerrainSurfaceHeightsProvider> _heightsProvider;
        std::shared_ptr<ITerrainFormat> _ioFormat;

        std::unique_ptr<TerrainUberHeightsSurface> _uberSurface;
        std::shared_ptr<HeightsUberSurfaceInterface> _uberSurfaceInterface;
        std::shared_ptr<ShortCircuitBridge> _uberSurfaceBridge;

        std::shared_ptr<GradientFlagsSettings> _gradFlagSettings;

        class CoverageInterface
        {
        public:
            TerrainCoverageId _id;
            std::unique_ptr<TerrainUberSurfaceGeneric> _uberSurface;
            std::shared_ptr<CoverageUberSurfaceInterface> _interface;
            std::shared_ptr<ShortCircuitBridge> _bridge;

            CoverageInterface() {}
            CoverageInterface(CoverageInterface&& moveFrom) never_throws 
            : _id(moveFrom._id), _uberSurface(std::move(moveFrom._uberSurface))
            , _interface(std::move(moveFrom._interface)), _bridge(std::move(moveFrom._bridge)) {}
            CoverageInterface& operator=(CoverageInterface&& moveFrom) never_throws
            {
                _id = moveFrom._id;
                _uberSurface = std::move(moveFrom._uberSurface);
                _interface = std::move(moveFrom._interface);
                _bridge = std::move(moveFrom._bridge);
                return *this;
            }
        };
        std::vector<CoverageInterface> _coverageInterfaces;

        std::vector<TerrainCellId> _cells;
        TerrainCoordinateSystem _coords;
        TerrainConfig _cfg;
        TerrainMaterialConfig _matCfg;

        std::unique_ptr<TerrainMaterialTextures> _textures;

        void CullNodes(
            const RenderCore::Techniques::ProjectionDesc& parserContext, 
            TerrainRenderingContext& terrainContext);

        void AddCells(const TerrainConfig& cfg, UInt2 cellMin, UInt2 cellMax);
        void BuildUberSurface(const ::Assets::ResChar uberSurfaceDir[], const TerrainConfig& cfg);

        void FlushShortCircuitQueue(Metal::DeviceContext& context);
		void ShortCircuitFinishedUploads(Metal::DeviceContext& context, IteratorRange<std::pair<uint64_t, uint32>*> updated);
    };


    //////////////////////////////////////////////////////////////////////////////////////////
    static TerrainRendererConfig CreateRendererConfig(const TerrainConfig& cfg, unsigned overlap)
    {
        const Int2 heightMapElementSize = cfg.NodeDimensionsInElements() + Int2(overlap, overlap);
        const unsigned cachedTileCount = 1024;

        TerrainRendererConfig rendererCfg;
        rendererCfg._heights = TerrainRendererConfig::Layer { heightMapElementSize, cachedTileCount, Format::R16_UINT };

        for (unsigned c=0; c<cfg.GetCoverageLayerCount(); ++c) {
            const auto& l = cfg.GetCoverageLayer(c);
            ImpliedTyping::TypeDesc t(ImpliedTyping::TypeCat(l._typeCat), uint16(l._typeCount));

            auto fmt = RenderCore::AsFormat(t, (ShaderNormalizationMode)l._shaderNormalizationMode);
            if (l._typeCat == (unsigned)ImpliedTyping::TypeCat::UInt16 && l._typeCount == 2)
                fmt = RenderCore::Format::R16G16_UNORM;        // hack! ShadowSample must be "unorm"

            rendererCfg._coverageLayers.push_back(
                std::make_pair(
                    l._id, 
                    TerrainRendererConfig::Layer { (l._nodeDimensions+UInt2(l._overlap, l._overlap)), cachedTileCount,  fmt } ));
        }
        return std::move(rendererCfg);
    }

    static std::shared_ptr<TerrainCellRenderer> CreateRenderer(
        const TerrainRendererConfig& cfg, std::shared_ptr<ITerrainFormat> ioFormat,
        bool allowTerrainModification)
    {
        return std::make_shared<TerrainCellRenderer>(cfg, std::move(ioFormat), allowTerrainModification);
    }

    void TerrainManager::Pimpl::AddCells(const TerrainConfig& cfg, UInt2 cellMin, UInt2 cellMax)
    {
        auto cells = BuildPrimedCells(cfg);

        StringMeld<MaxPath, ::Assets::ResChar> cachedDataFile;
        cachedDataFile << cfg._cellsDirectory << "cached.dat";
        
        TerrainCachedData cachedData;
        TRY
        {
            cachedData = TerrainCachedData(cachedDataFile);
        }
        CATCH(...)
        {
            Log(Warning) << "Got error while loading cached terrain data from file: " << cachedDataFile << ". Regenerating data." << std::endl;
        }
        CATCH_END

        if (cachedData._cells.empty())
            cachedData = TerrainCachedData(cfg, *_ioFormat);
        
        auto cellBasedToWorld = _coords.CellBasedToWorld();
        for (auto c=cells.cbegin(); c<cells.cend(); ++c) {
                // reject cells that haven't been selected
            if (    c->_cellIndex[0] < cellMin[0] || c->_cellIndex[0] >= cellMax[0]
                ||  c->_cellIndex[1] < cellMin[1] || c->_cellIndex[1] >= cellMax[1])
                continue;

            TerrainCellId cell;
            cell._cellToWorld = Combine(AsFloat4x4(Float3(float(c->_cellIndex[0]), float(c->_cellIndex[1]), 0.f)), cellBasedToWorld);
            
            cfg.GetCellFilename(cell._heightMapFilename, dimof(cell._heightMapFilename), c->_cellIndex, CoverageId_Heights);
            cell._heightsToUber._mins = c->_heightUber.first;
            cell._heightsToUber._maxs = c->_heightUber.second;

            for (unsigned q=0; q<std::min(cfg.GetCoverageLayerCount(), TerrainCellId::MaxCoverageCount); ++q) {
                const auto& l = cfg.GetCoverageLayer(q);
                cfg.GetCellFilename(cell._coverageFilename[q], dimof(cell._coverageFilename[q]), c->_cellIndex, l._id);
                cell._coverageIds[q] = l._id;
                cell._coverageToUber[q]._mins = c->_coverageUber[q].first;
                cell._coverageToUber[q]._maxs = c->_coverageUber[q].second;
            }

            cell._aabbMin = Float3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
            cell._aabbMax = Float3(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());

                // attempt to load the bounding box from our cached data
            auto i = std::find_if(cachedData._cells.cbegin(), cachedData._cells.cend(),
                [c](const TerrainCachedData::Cell& cell) { return cell._cellIndex == c->_cellIndex; });
            if (i != cachedData._cells.cend()) {
                cell._aabbMin = TransformPoint(cellBasedToWorld, Float3(float(c->_cellIndex[0]), float(c->_cellIndex[1]), i->_heightRange.first));
                cell._aabbMax = TransformPoint(cellBasedToWorld, Float3(float(c->_cellIndex[0]+1), float(c->_cellIndex[1]+1), i->_heightRange.second));
            }

            _cells.push_back(cell);
        }
    }

	static void WriteCell(
		::Assets::rstring filename, UInt2 uberMins, UInt2 uberMaxs,
		std::weak_ptr<GenericUberSurfaceInterface> uber,
        std::shared_ptr<GradientFlagsSettings> gradFlagSettings,
		unsigned cellTreeDepth, unsigned nodeOverlap)
	{
        auto l = uber.lock();
        if (!l) return;

        TerrainFormat fmt(*gradFlagSettings);
		fmt.WriteCell(
			filename.c_str(), l->GetSurface(),
			uberMins, uberMaxs,
			cellTreeDepth, nodeOverlap);
	}

    void TerrainManager::Pimpl::BuildUberSurface(const ::Assets::ResChar uberSurfaceDir[], const TerrainConfig& cfg)
    {
        const bool registerShortCircuit = true;

        {
            ::Assets::ResChar uberSurfaceFile[MaxPath];
            TerrainConfig::GetUberSurfaceFilename(uberSurfaceFile, dimof(uberSurfaceFile), uberSurfaceDir, CoverageId_Heights);

            _uberSurface = std::make_unique<TerrainUberHeightsSurface>(uberSurfaceFile);
            _uberSurfaceInterface = std::make_shared<HeightsUberSurfaceInterface>(std::ref(*_uberSurface));

            if (constant_expression<registerShortCircuit>::result()) {
                _uberSurfaceBridge = std::make_shared<ShortCircuitBridge>(_uberSurfaceInterface);
                _uberSurfaceInterface->SetShortCircuitBridge(_uberSurfaceBridge);

                    //  Register cells for short-circuit update... Do we need to do this for every single cell
                    //  or just those that are within the limited area we're going to load?
                for (auto c=_cells.cbegin(); c!=_cells.cend(); ++c) {
                    _uberSurfaceBridge->RegisterCell(
                        c->BuildHash(),
                        c->_heightsToUber._mins, c->_heightsToUber._maxs,
						std::bind(WriteCell, 
							::Assets::rstring(c->_heightMapFilename), 
							c->_heightsToUber._mins, c->_heightsToUber._maxs,
							_uberSurfaceInterface, _gradFlagSettings,
							_cfg.CellTreeDepth(), _cfg.NodeOverlap()));
                }
            }
        }

        for (unsigned c=0; c<cfg.GetCoverageLayerCount(); ++c) {
            const auto& l = cfg.GetCoverageLayer(c);
            if (l._id == CoverageId_AngleBasedShadows) continue;

            ::Assets::ResChar uberSurfaceFile[MaxPath];
            TerrainConfig::GetUberSurfaceFilename(uberSurfaceFile, dimof(uberSurfaceFile), uberSurfaceDir, l._id);

            Pimpl::CoverageInterface ci;
            ci._id = l._id;
                
            ci._uberSurface = std::make_unique<TerrainUberSurfaceGeneric>(uberSurfaceFile);
            ci._interface = std::make_shared<CoverageUberSurfaceInterface>(std::ref(*ci._uberSurface));

            if (constant_expression<registerShortCircuit>::result()) {
                ci._bridge = std::make_shared<ShortCircuitBridge>(ci._interface);
                ci._interface->SetShortCircuitBridge(ci._bridge);

                    //  Register cells for short-circuit update... Do we need to do this for every single cell
                    //  or just those that are within the limited area we're going to load?
                for (auto cell=_cells.cbegin(); cell!=_cells.cend(); ++cell) {
                    ci._bridge->RegisterCell(
                        cell->BuildHash(),
                        cell->_heightsToUber._mins, cell->_heightsToUber._maxs, 
						std::bind(WriteCell,
							::Assets::rstring(cell->_coverageFilename[c]),
							cell->_coverageToUber[c]._mins, cell->_coverageToUber[c]._maxs,
							ci._interface, _gradFlagSettings,
							_cfg.CellTreeDepth(), l._overlap));
                }
            }

            _coverageInterfaces.push_back(std::move(ci));
        }
    }

    void TerrainManager::FlushToDisk(ConsoleRig::IProgress* progress)
    {
        unsigned stepCount = 0;
        if (_pimpl->_uberSurfaceInterface) stepCount++;
        stepCount += (unsigned)_pimpl->_coverageInterfaces.size();

        {
            auto step = progress ? progress->BeginStep("Commit terrain to disk", stepCount, true) : nullptr;

                //  Unload the renderer (because we'll want to reload 
                //  everything from disk after this)
            _pimpl->_renderer.reset();

            if (_pimpl->_uberSurfaceInterface) {
                _pimpl->_uberSurfaceInterface->FlushLockToDisk();
                if (step) step->Advance();
            }

            for (auto& i:_pimpl->_coverageInterfaces) {
                if (step && step->IsCancelled()) break;
                i._interface->FlushLockToDisk();
                if (step) step->Advance();
            }

            // make sure we save the cfg file, as well
            // _pimpl->_cfg.Save();
        }

        auto step = progress ? progress->BeginStep("Reloading terrain", 1, false) : nullptr;
        Load(_pimpl->_cfg, UInt2(0,0), _pimpl->_cfg._cellCount + UInt2(1,1), true);
    }

    void TerrainManager::Reset()
    {
        _pimpl->_cells.clear();
        _pimpl->_uberSurfaceInterface.reset();
        _pimpl->_uberSurface.reset();
        _pimpl->_uberSurfaceBridge.reset();
        _pimpl->_coverageInterfaces.clear();

        if (_pimpl->_renderer)
            _pimpl->_renderer->UnloadCachedData();

        _pimpl->_gradFlagSettings = std::make_shared<GradientFlagsSettings>();
    }

    static float CellSizeWorldSpace(const TerrainConfig& cfg)
    {
        return cfg.CellDimensionsInNodes()[0] * cfg.NodeDimensionsInElements()[0] * cfg.ElementSpacing();
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    void TerrainManager::Load(
        const TerrainConfig& cfg, 
        UInt2 cellMin, UInt2 cellMax, bool allowModification)
    {
        Reset();

            // These are the determining parameters for the terrain:
            //      cellSize: 
            //          Size of the a cell (in meters)
            //          This determines the scaling that is applied to convert to world coords
            //      cellElementCount: 
            //          Number of height samples per cell, for width and height (ie, 512 means cells are 512x512)
            //      cellTreeDepth: 
            //          The depth of tree of a single cell. Higher numbers allow for more aggressive lodding on distant LODs
            //      overlap: 
            //          Overlap elements for each node (eg, 2 means an extra 2 rows and an extra 2 columns is added to each node)
            //          Overlap rows and columns store the same value as adjacent nodes -- they are required to make sure the edges match
            //          Eg, if nodes are 32x32 and overlap=2, each node will actually store 34x34 height samples
            //
            //      The number of samples per node is determined by "cellElementCount" and "cellTreeDepth"
            //      In general, we want to balance the number of height samples per node so that is it convenient for 
            //      the shader (since the shaders always work on a node-by-node basis). The number of samples per
            //      node should be small enough that the height texture can fit in the GPU cache, and the GPU
            //      tessellation can apply enough tessellation. But it should also be large enough that the "overlap"
            //      parts are not excessive.

            //      \todo -- these terrain configuration values should come from the data image
            //      The "uber-surface" is effectively the "master" data for the terrain. These variables determine
            //      how we break the uber surface down into separate cells. As a result, the cell data is almost
            //      just a cached version of the uber surface, with these configuration values.
        _pimpl->_coords = TerrainCoordinateSystem(_pimpl->_coords.TerrainOffset(), CellSizeWorldSpace(cfg));
        _pimpl->_cfg = cfg;

        if (cellMax == UInt2(~0u, ~0u))
            cellMax = cfg._cellCount + UInt2(1,1);

        _pimpl->AddCells(cfg, cellMin, cellMax);

        ////////////////////////////////////////////////////////////////////////////

        auto rendererCfg = CreateRendererConfig(cfg, cfg.NodeOverlap());
        bool reuseRenderer = 
                _pimpl->_renderer 
            &&  IsCompatible(rendererCfg, _pimpl->_renderer->GetConfig())
            && _pimpl->_renderer->IsShortCircuitAllowed() == allowModification;
        
        if (reuseRenderer) {
            _pimpl->_heightsProvider->SetCoords(_pimpl->_cfg, _pimpl->_coords);
        } else {
            MainSurfaceHeightsProvider = nullptr;
            _pimpl->_heightsProvider.reset();
            _pimpl->_renderer.reset();
            _pimpl->_textures.reset();

            _pimpl->_renderer = CreateRenderer(rendererCfg, _pimpl->_ioFormat, allowModification);
            _pimpl->_heightsProvider = std::make_shared<TerrainSurfaceHeightsProvider>(
                _pimpl->_renderer, _pimpl->_cfg, _pimpl->_coords);
            MainSurfaceHeightsProvider = _pimpl->_heightsProvider.get();
        }
    }

    void TerrainManager::LoadMaterial(const TerrainMaterialConfig& matCfg)
    {
        _pimpl->_matCfg = matCfg;
        _pimpl->_textures.reset();
    }

    void TerrainManager::LoadUberSurface(const ::Assets::ResChar uberSurfaceDir[])
    {
        _pimpl->BuildUberSurface(uberSurfaceDir, _pimpl->_cfg);
    }

    TerrainManager::TerrainManager(std::shared_ptr<ITerrainFormat> ioFormat)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_ioFormat = std::move(ioFormat);
        _pimpl->_gradFlagSettings = std::make_shared<GradientFlagsSettings>();
    }

    TerrainManager::~TerrainManager()
    {
        MainSurfaceHeightsProvider = nullptr;
    }

    void TerrainManager::Pimpl::CullNodes(
        const RenderCore::Techniques::ProjectionDesc& projDesc, TerrainRenderingContext& terrainContext)
    {
        TerrainCollapseContext collapseContext;
        collapseContext._startLod = Tweakable("TerrainMinLOD", 1);
        collapseContext._screenSpaceEdgeThreshold = Tweakable("TerrainEdgeThreshold", 256.f);
        for (const auto& c:_cells)
            _renderer->CullNodes(projDesc, terrainContext, collapseContext, c);

        for (unsigned c=collapseContext._startLod; c<(TerrainCollapseContext::MaxLODLevels-1); ++c)
            collapseContext.AttemptLODPromote(c, terrainContext);

        _renderer->WriteQueuedNodes(terrainContext, collapseContext);
    }

    static void FlushShortCircuit(Metal::DeviceContext& context, TerrainCellRenderer& renderer, ShortCircuitBridge& bridge, TerrainCoverageId coverageId)
    {
        // Do the abandons first, then update every cell that's changed
        auto pendingAbandons = bridge.GetPendingAbandons();
        for (const auto&a:pendingAbandons)
            renderer.AbandonShortCircuitData(a._cellHash, coverageId, a._cellMins, a._cellMaxs);

        auto pendingUpdates = bridge.GetPendingUpdates();
        for (const auto&u:pendingUpdates)
            renderer.ShortCircuit(context, bridge, u._cellHash, coverageId, u._cellMins, u._cellMaxs);
    }

    void TerrainManager::Pimpl::FlushShortCircuitQueue(Metal::DeviceContext& context)
    {
        if (!_uberSurfaceBridge) return;
        FlushShortCircuit(context, *_renderer, *_uberSurfaceBridge, CoverageId_Heights);
        for (const auto&l:_coverageInterfaces)
            FlushShortCircuit(context, *_renderer, *l._bridge, l._id);
    }

	void TerrainManager::Pimpl::ShortCircuitFinishedUploads(
		Metal::DeviceContext& context, 
		IteratorRange<std::pair<uint64_t, uint32>*> updated)
	{
        if (!_uberSurfaceBridge) return;
		for (const auto& u:updated) {
			if (u.second & (1u << 31u)) {
				// Unfortunately, due to the way this works we don't know which layer has completed
				// So we just have to try them all. This will result in redundant extra uploads in some
				// cases.
				for (const auto&l : _coverageInterfaces)
					_renderer->ShortCircuit(context, *l._bridge, u.first, l._id, u.second & ~(1u << 31u));
			} else {
				_renderer->ShortCircuit(context, *_uberSurfaceBridge, u.first, CoverageId_Heights, u.second);
			}
		}
	}

    static TerrainRenderingContext::PriorityMode GetPriorityMode() 
    { 
        return (TerrainRenderingContext::PriorityMode)Tweakable("TerrainPriorityMode", 1); 
    }

    void TerrainManager::Prepare(
        RenderCore::IThreadContext& context,
        Techniques::ParsingContext& parserContext,
        PreparedScene& preparedPackets)
    {
        assert(_pimpl);
        auto* renderer = _pimpl->_renderer.get();
        if (!renderer) return;

        // auto& reload = Tweakable("TerrainReload", false);
        // if (reload) {
        //  _pimpl->_renderer->UnloadCachedData();
        //  reload = false;
        // }

            //  We need to enable the rendering state once, for all cells. The state should be
            //  more or less the same for every cell, so we don't need to do it every time
            //
            //  We can setup multi-pass rendering for the terrain by reorganising this function
            //  a little bit. The Cull step only needs to be done once per frame. So it can be
            //  prepared early in the frame calculations, and then reused in multiple 
            //  subsequent render steps.
        auto* state = preparedPackets.Allocate<TerrainRenderingContext>(
            0,
            renderer->GetCoverageIds(), 
            renderer->GetCoverageFmts(), renderer->GetCoverageLayersCount(), 
            _pimpl->_cfg.EncodedGradientFlags(),
            GetPriorityMode());
        if (!state) return;

        state->_queuedNodes.erase(state->_queuedNodes.begin(), state->_queuedNodes.end());
        state->_queuedNodes.reserve(2048);
        state->_currentViewport = Metal::DeviceContext::Get(context)->GetBoundViewport();
        _pimpl->CullNodes(parserContext.GetProjectionDesc(), *state);

        renderer->QueueUploads(*state);

        if (!_pimpl->_textures || _pimpl->_textures->GetDependencyValidation().GetValidationIndex() > 0) {
            _pimpl->_textures.reset();
            _pimpl->_textures = std::make_unique<TerrainMaterialTextures>(
                context, _pimpl->_matCfg, _pimpl->_cfg.EncodedGradientFlags());
        }
    }

    void TerrainManager::Render(
        IThreadContext& context, 
        Techniques::ParsingContext& parserContext, 
		const ILightingStateDelegate* lightingParserDelegate,
        PreparedScene& preparedPackets,
        unsigned techniqueIndex)
    {
        assert(_pimpl);
        auto* renderer = _pimpl->_renderer.get();
        if (!renderer) return;

        auto* state = preparedPackets.Get<TerrainRenderingContext>(0);
        if (!state) return;
		
		auto& metalContext = *Metal::DeviceContext::Get(context);

        // Check for short-circuit events.
        if (renderer->IsShortCircuitAllowed()) {
            auto completed = renderer->CompletePendingUploads_Bridge();
            _pimpl->ShortCircuitFinishedUploads(metalContext, MakeIteratorRange(completed));
            _pimpl->FlushShortCircuitQueue(metalContext);
        } else {
            renderer->CompletePendingUploads();
        }

        metalContext.GetNumericUniforms(ShaderStage::Pixel).Bind(MakeResourceList(8, 
            _pimpl->_textures->_srv[TerrainMaterialTextures::Diffuse], 
            _pimpl->_textures->_srv[TerrainMaterialTextures::Normal], 
            _pimpl->_textures->_srv[TerrainMaterialTextures::Roughness]));

        auto mode = 
            (techniqueIndex==5)
            ? TerrainRenderingContext::Mode_VegetationPrepare
            : TerrainRenderingContext::Mode_Normal;

        Float3 sunDirection(0.f, 0.f, 1.f);
        if (lightingParserDelegate && lightingParserDelegate->GetLightCount() > 0)
            sunDirection = lightingParserDelegate->GetLightDesc(0)._position;

            // We want to project the sun direction onto the plane for the precalculated sun movement.
            // Then find the appropriate angle for on that plane.
        float sunDirectionAngle;
        {
            auto trans = Identity<Float4x4>();
            Combine_IntoLHS(trans, RotationZ(-_pimpl->_cfg.SunPathAngle()));
            auto transDirection = TransformDirectionVector(trans, sunDirection);
            sunDirectionAngle = XlATan2(transDirection[0], transDirection[2]);
        }

        const float expansionConstant = 1.5f;
        float terrainLightingConstants[] = 
        {
            sunDirectionAngle / float(.5f * expansionConstant * M_PI), 
            _pimpl->_matCfg._shadowSoftness,
            _pimpl->_matCfg._specularParameter,
            _pimpl->_matCfg._roughnessMin,
            _pimpl->_matCfg._roughnessMax,
            0.f, 0.f, 0.f
        };
        auto lightingConstantsBuffer = MakeMetalCB(terrainLightingConstants, sizeof(terrainLightingConstants));
        metalContext.GetNumericUniforms(ShaderStage::Pixel).Bind(MakeResourceList(5, _pimpl->_textures->_texturingConstants, lightingConstantsBuffer, _pimpl->_textures->_procTexContsBuffer));
        if (mode == TerrainRenderingContext::Mode_VegetationPrepare) {
                // this cb required in the geometry shader for vegetation prepare mode!
            metalContext.GetNumericUniforms(ShaderStage::Geometry).Bind(MakeResourceList(6, lightingConstantsBuffer));  
        }

        state->EnterState(metalContext, parserContext, *_pimpl->_textures, renderer->GetHeightsElementSize(), mode);
        renderer->Render(metalContext, parserContext, *state);
        state->ExitState(metalContext, parserContext);

        // if (_pimpl->_coverageInterfaces.size() > 0)
        //     parserContext._pendingOverlays.push_back(
        //         std::bind(
        //             &GenericUberSurfaceInterface::RenderDebugging, _pimpl->_coverageInterfaces[0]._interface.get(), 
        //             std::placeholders::_1, std::placeholders::_2));
    }

    unsigned TerrainManager::CalculateIntersections(
        IntersectionResult intersections[], unsigned maxIntersections,
        std::pair<Float3, Float3> ray,
        RenderCore::IThreadContext& context,
        Techniques::ParsingContext& parserContext)
    {
        assert(_pimpl);
        if (!_pimpl->_renderer) return 0;

		auto& metalContext = *Metal::DeviceContext::Get(context);

            // we can only do this on the immediate context (because we need to execute
            // and readback GPU data)
        assert(metalContext.IsImmediate());

            //  we can use the same culling as the rendering part. But ideally we want to cull nodes
            //  that are outside of the camera frustum, or that don't intersect the ray
            //      first pass -- normal culling
        TerrainRenderingContext state(
            _pimpl->_renderer->GetCoverageIds(), 
            _pimpl->_renderer->GetCoverageFmts(),
            _pimpl->_renderer->GetCoverageLayersCount(),
            _pimpl->_cfg.EncodedGradientFlags(),
            GetPriorityMode());
        state._queuedNodes.erase(state._queuedNodes.begin(), state._queuedNodes.end());
        state._queuedNodes.reserve(2048);
        state._currentViewport = metalContext.GetBoundViewport();        // (accurate viewport is required to get the lodding right)
        const auto& projDesc = parserContext.GetProjectionDesc();
		_pimpl->CullNodes(projDesc, state);

        const auto compressedHeightMask = CompressedHeightMask(state._encodedGradientFlags);

            //  second pass -- remove nodes that don't intersect the ray
        for (auto i=state._queuedNodes.begin(); i!=state._queuedNodes.end();) {
            auto localToWorld = Combine(i->_cell->NodeToCell(i->_absNodeIndex), i->_cellToWorld);
            auto result = true;
            if (Equivalent(localToWorld(2,2), 0.f, 1e-7f)) {
                    // If all of the heights in the terrain are equal, we end up with a zero-volume
                    // bounding box. We need a special case RayVsAABB for this... But let's just assume
                    // it is an intersection
                result = true; 
            } else {
                result = RayVsAABB(ray, localToWorld, Float3(0.f, 0.f, 0.f), Float3(1.f, 1.f, float(compressedHeightMask)));
            }

            if (!result) {
                i = state._queuedNodes.erase(i);
            } else {
                ++i;
            }
        }

            // \todo -- all this rendering code would be better placed in another class

            //  we should have a set of nodes that are visible on screen, and that might intersect the ray
            //  execute the intersections test shader to look for intersections with the terrain triangles
            //  we need to create a structured buffer for the result, and bind it to the geometry shader
		if (_pimpl->_renderer->IsShortCircuitAllowed()) {
			auto completed = _pimpl->_renderer->CompletePendingUploads_Bridge();
			_pimpl->ShortCircuitFinishedUploads(metalContext, MakeIteratorRange(completed));
		} else {
			_pimpl->_renderer->CompletePendingUploads();
		}
        _pimpl->_renderer->QueueUploads(state);

        const unsigned resultsBufferSize = 4 * 1024;

        IResourcePtr gpuOutput;
        intrusive_ptr<BufferUploads::ResourceLocator> outputRes;
        {
            auto desc = CreateDesc(
                BindFlag::StreamOutput, 0, GPUAccess::Write,
                LinearBufferDesc::Create(resultsBufferSize),
                "TriangleResult");

            auto pkt = BufferUploads::CreateEmptyPacket(desc);
            XlSetMemory(pkt->GetData(), 0x0, pkt->GetDataSize());

            auto& uploads = GetBufferUploads();
            outputRes = uploads.Transaction_Immediate(desc, pkt.get());

            gpuOutput = outputRes->GetUnderlying();
            if (gpuOutput)
                MetalStubs::BindSO(metalContext, *gpuOutput);
        }

        struct RayTestBuffer
        {
            Float3 _rayStart; float _dummy0;
            Float3 _rayEnd; float _dummy1;
        } rayTestBuffer = { ray.first, 0.f, ray.second, 0.f };
        metalContext.GetNumericUniforms(ShaderStage::Geometry).Bind(MakeResourceList(2, MakeMetalCB(&rayTestBuffer, sizeof(rayTestBuffer))));

        state.EnterState(metalContext, parserContext, 
            TerrainMaterialTextures(), _pimpl->_renderer->GetHeightsElementSize(), TerrainRenderingContext::Mode_RayTest);
        _pimpl->_renderer->Render(metalContext, parserContext, state);
        state.ExitState(metalContext, parserContext);

        MetalStubs::UnbindSO(metalContext);

        unsigned resultCount = 0;
        if (outputRes && gpuOutput) {
            auto readback = GetBufferUploads().Resource_ReadBack(*outputRes);
            if (readback->GetDataSize()) {
                    //  results are in the buffer we mapped... But how do we know how may
                    //  results there are? There is a counter associated with the buffer, but it's
                    //  inaccessible to us. Just read along until we get a zero.
                auto* resultArray = (Float4*)readback->GetData();
                auto* resultEnd = (Float4*)PtrAdd(readback->GetData(), readback->GetDataSize());
                auto* res = resultArray;
                while (res < resultEnd) {
                    if ((*res)[0] == 0.f)
                        break;
                    ++res;
                }
                resultCount = unsigned(res - resultArray);
                std::sort(resultArray, &resultArray[resultCount], 
                    [](const Float4& lhs, const Float4& rhs) { return lhs[0] < rhs[0]; });

                for (unsigned c=0; c<std::min(resultCount, maxIntersections); ++c) {
                    intersections[c]._intersectionPoint = 
                        LinearInterpolate(ray.first, ray.second, resultArray[c][0]);
                    intersections[c]._cellCoordinates = Float2(resultArray[c][1], resultArray[c][2]);
                    intersections[c]._fullTerrainCoordinates = Float2(0.f, 0.f);    // (we don't actually know which node it hit yet)
                }
            }
        }

        return resultCount;
    }

    void TerrainManager::SetWorldSpaceOrigin(const Float3& origin)
    {
        auto change = Float3(origin - _pimpl->_coords.TerrainOffset());
        _pimpl->_coords.SetTerrainOffset(origin);

            //  We have to update the _cellToWorld transforms
            //  Note that we could get some floating point creep if we
            //  do this very frequently! This method is fine for tools, but
            //  could be a problem if attempting to move the terrain origin
            //  in-game.
        for (auto& i:_pimpl->_cells) {
            Combine_IntoLHS(i._cellToWorld, change);
            i._aabbMin += change;
            i._aabbMax += change;
        }
    }

    void TerrainManager::SetShortCircuitSettings(const GradientFlagsSettings& gradientFlagsSettings)
    {
        if (_pimpl->_renderer)
            _pimpl->_renderer->SetShortCircuitSettings(gradientFlagsSettings);
        *_pimpl->_gradFlagSettings = gradientFlagsSettings;
    }

    const TerrainCoordinateSystem&  TerrainManager::GetCoords() const               { return _pimpl->_coords; }
    HeightsUberSurfaceInterface* TerrainManager::GetHeightsInterface()              { return _pimpl->_uberSurfaceInterface.get(); }
    std::shared_ptr<ISurfaceHeightsProvider> TerrainManager::GetHeightsProvider()   { return _pimpl->_heightsProvider; }

    CoverageUberSurfaceInterface*   TerrainManager::GetCoverageInterface(TerrainCoverageId id)
    {
        for (auto i = _pimpl->_coverageInterfaces.begin(); i != _pimpl->_coverageInterfaces.end(); ++i)
            if (i->_id == id) return i->_interface.get();
        return nullptr;
    }

    const TerrainConfig& TerrainManager::GetConfig() const                      { return _pimpl->_cfg; }
    const std::shared_ptr<ITerrainFormat>& TerrainManager::GetFormat() const    { return _pimpl->_ioFormat; }
    const TerrainMaterialConfig& TerrainManager::GetMaterialConfig() const      { return _pimpl->_matCfg; }
}

