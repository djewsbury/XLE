// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TerrainShortCircuit.h"
#include "TerrainRender.h"
#include "TextureTileSet.h"
#include "TerrainUberSurface.h"
#include "TerrainScaffold.h"
#include "GestaltResource.h"
#include "MetalStubs.h"

#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/BufferView.h"

#include "../BufferUploads/IBufferUploads.h"
#include "../BufferUploads/DataPacket.h"
#include "../BufferUploads/ResourceLocator.h"

#include "../ConsoleRig/IProgress.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Math/Transformations.h"
#include "../Assets/Assets.h"
#include "../Utility/StringFormat.h"
#include "../xleres/FileList.h"

#include "../RenderCore/DX11/Metal/IncludeDX11.h"       // for UpdateSubResource below

namespace SceneEngine
{
    using namespace RenderCore;

    class ShortCircuitResources
    {
    public:
        class Desc
        {
        public:
            Format      _valueFormat;
            unsigned    _filterType;
            unsigned    _gradientFlagsEnable;

            Desc(Format valueFormat, unsigned filterType, bool gradientFlagsEnable)
            : _valueFormat(valueFormat), _filterType(filterType), _gradientFlagsEnable(gradientFlagsEnable)
            {}
        };

        GestaltTypes::UAV _tileCoordsBuffer;

        const Metal::ComputeShader* _cs0;
        const Metal::ComputeShader* _cs1;
        const Metal::ComputeShader* _cs2;
        Metal::BoundUniforms _boundLayout;

        const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

        ShortCircuitResources(const Desc& desc);
    private:
        ::Assets::DependencyValidation _depVal;
    };

    ShortCircuitResources::ShortCircuitResources(const Desc& desc)
    {
        const ::Assets::ResChar firstPassShader[] = SCENE_ENGINE_RES "/Terrain/copyterraintile.hlsl:WriteToMidway:cs_*";
        const ::Assets::ResChar secondPassShader[] = SCENE_ENGINE_RES "/Terrain/copyterraintile.hlsl:CommitToFinal:cs_*";
        StringMeld<64, char> defines; 
        defines << "VALUE_FORMAT=" << unsigned(desc._valueFormat) << ";FILTER_TYPE=" << desc._filterType;
        auto encodedGradientFlags = desc._gradientFlagsEnable;
        if (encodedGradientFlags) defines << ";ENCODED_GRADIENT_FLAGS=1";

        _cs0 = &::Assets::Legacy::GetAssetDep<Metal::ComputeShader>(firstPassShader, defines.get());
        _cs1 = &::Assets::Legacy::GetAssetDep<Metal::ComputeShader>(secondPassShader, defines.get());
        _cs2 = &::Assets::Legacy::GetAssetDep<Metal::ComputeShader>(SCENE_ENGINE_RES "/Terrain/copyterraintile.hlsl:DirectToFinal:cs_*", defines.get());

		UniformsStreamInterface usi;
        usi.BindConstantBuffer(0, {Hash64("Parameters")});
        usi.BindShaderResource(0, Hash64("Input"));
		usi.BindShaderResource(1, Hash64("OldHeights"));
		_boundLayout = Metal::BoundUniforms(
			*_cs0,
			Metal::PipelineLayoutConfig{},
			UniformsStreamInterface{},
			usi);

        _tileCoordsBuffer = GestaltTypes::UAV(
            BufferUploads::LinearBufferDesc::Create(32, 32),
            "TileCoordsBuffer", nullptr, BufferUploads::BindFlag::UnorderedAccess);

        _depVal = ::Assets::GetDepValSys().Make();
        _depVal.RegisterDependency(_cs0->GetDependencyValidation());
        _depVal.RegisterDependency(_cs1->GetDependencyValidation());
        _depVal.RegisterDependency(_cs2->GetDependencyValidation());
    }

    class ShortCircuitMidwayBox
    {
    public:
        class Desc
        {
        public:
            UInt2 _dims;
            Desc(UInt2 dims) : _dims(dims) {}
        };

        GestaltTypes::UAV _midwayBuffer;
        GestaltTypes::UAV _midwayGradFlags;

        ShortCircuitMidwayBox(const Desc& desc);
        ~ShortCircuitMidwayBox();
    };

    ShortCircuitMidwayBox::ShortCircuitMidwayBox(const Desc& desc)
    {
        _midwayBuffer = GestaltTypes::UAV(
            BufferUploads::TextureDesc::Plain2D(desc._dims[0], desc._dims[1], Format::R32_FLOAT),
            "TerrainMidway");
        _midwayGradFlags = GestaltTypes::UAV(
            BufferUploads::TextureDesc::Plain2D(desc._dims[0], desc._dims[1], Format::R32_UINT),
            "TerrainMidway");
    }

    ShortCircuitMidwayBox::~ShortCircuitMidwayBox() {}

    void TerrainCellRenderer::ShortCircuitTileUpdate(
        RenderCore::Metal::DeviceContext& metalContext, const TextureTile& tile, 
        NodeCoverageInfo& coverageInfo, 
        TerrainCoverageId layerId, unsigned fieldIndex, 
        Float2 cellCoordMins, Float2 cellCoordMaxs,
        const ShortCircuitUpdate& upd)
    {
        TRY 
        {
            // downsampling required depends on which field we're in.
            unsigned downsample = unsigned(4-fieldIndex);

            Format format = Format::Unknown;
            TextureTileSet* tileSet = nullptr;
            if (layerId == CoverageId_Heights) {
                tileSet = _heightMapTileSet.get();
            } else {
                for (unsigned c=0; c<_coverageIds.size(); ++c)
                    if (_coverageIds[c] == layerId) { 
                        tileSet = _coverageTileSet[c].get();
                        format = tileSet->GetFormat();
                        break; 
                    }
            }
            if (!tileSet) return;

            const auto Filter_Bilinear = 1u;
            const auto Filter_Max = 2u;
            unsigned filterType = Filter_Bilinear;
            if (format == Format::R8_UINT) filterType = Filter_Max;      // (use "max" filter for integer types)

            auto& box = ConsoleRig::FindCachedBoxDep2<ShortCircuitResources>(format, filterType, _gradientFlagsSettings._enable);

            const float heightOffsetValue = 5000.f; // (height values are shifted by this constant in the shader to get around issues with negative heights
#if GFXAPI_TARGET == GFXAPI_DX11	// platformtemp
            float temp = std::numeric_limits<float>::max();
            struct TileCoords
            {
                float minHeight, heightScale;
                unsigned workingMinHeight, workingMaxHeight;
                float heightOffsetValue;
                unsigned dummy[3];
            } tileCoords = { 
                coverageInfo._heightOffset, coverageInfo._heightScale, 
                *reinterpret_cast<unsigned*>(&temp), 0x0u,
                heightOffsetValue, 0, 0, 0
            };

            metalContext.GetUnderlying()->UpdateSubresource(
                Metal::AsResource(*box._tileCoordsBuffer.Locator().GetUnderlying()).GetUnderlying().get(),
                0, nullptr, &tileCoords, sizeof(TileCoords), sizeof(TileCoords));
#endif

            const auto resSrc = Metal::ExtractDesc(*upd._srv);
            assert(resSrc._type == BufferUploads::BufferDesc::Type::Texture);

            struct Parameters
            {
                Int2 _sourceMin; unsigned _dummy0[2];
                UInt2 _updateMin, _updateMax;
                Int3 _dstTileAddress; int _sampleArea;
                UInt2 _tileSize; unsigned _dummy[2];
                float _gradFlagSpacing;
                float _gradFlagThresholds[3];
            } parameters = {
                upd._cellMinsInResource + 
                    Int2(   int((upd._cellMaxsInResource[0] - upd._cellMinsInResource[0]) * cellCoordMins[0]),
                            int((upd._cellMaxsInResource[1] - upd._cellMinsInResource[1]) * cellCoordMins[1])),
                {0,0},
                UInt2(0, 0), UInt2(resSrc._textureDesc._width, resSrc._textureDesc._height),

                Int3(tile._x, tile._y, tile._arrayIndex), 1<<downsample, 
                Int2(tile._width, tile._height), {0,0},
                _gradientFlagsSettings._elementSpacing,
                { _gradientFlagsSettings._slopeThresholds[0], _gradientFlagsSettings._slopeThresholds[1], _gradientFlagsSettings._slopeThresholds[2] }
            };
            ConstantBufferView pkts[] = { RenderCore::MakeSharedPkt(parameters) };
            const Metal::ShaderResourceView* srv[] = { upd._srv.get(), &tileSet->GetShaderResource() };

			box._boundLayout.Apply(metalContext, 1, 
				UniformsStream{
					MakeIteratorRange(pkts), 
					UniformsStream::MakeResources(MakeIteratorRange(srv))});

            const unsigned threadGroupWidth = 6;
            if (format == Format::Unknown) {
                    // go via a midway buffer and handle the min/max quantization
                auto& midwayBox = ConsoleRig::FindCachedBox2<ShortCircuitMidwayBox>(UInt2(tile._width, tile._height));
                metalContext.GetNumericUniforms(ShaderStage::Compute).Bind(
                    MakeResourceList(1, midwayBox._midwayBuffer.UAV(), midwayBox._midwayGradFlags.UAV(), box._tileCoordsBuffer.UAV()));

                metalContext.Bind(*box._cs0);
                metalContext.Dispatch( 
                    unsigned(XlCeil(tile._width /float(threadGroupWidth))), 
                    unsigned(XlCeil(tile._height/float(threadGroupWidth))));

                    //  if everything is ok up to this point, we can commit to the final
                    //  output --
				box._boundLayout.UnbindShaderResources(metalContext, 1);
                metalContext.GetNumericUniforms(ShaderStage::Compute).Bind(MakeResourceList(tileSet->GetUnorderedAccessView()));
                metalContext.Bind(*box._cs1);
                metalContext.Dispatch( 
                    unsigned(XlCeil(tile._width /float(threadGroupWidth))), 
                    unsigned(XlCeil(tile._height/float(threadGroupWidth))));

                    //  We need to read back the new min/max heights
                    //  we could write these back to the original terrain cell -- but it
                    //  would be better to keep them cached only in the NodeRenderInfo
                auto& uploads = tileSet->GetBufferUploads();
                auto readback = uploads.Resource_ReadBack(box._tileCoordsBuffer.Locator());
                float* readbackData = (float*)readback->GetData();
                if (readbackData) {
                    const auto compressedHeightMask = CompressedHeightMask(_gradientFlagsSettings._enable);
                    float newHeightOffset = readbackData[2] - heightOffsetValue;
                    float newHeightScale = (readbackData[3] - readbackData[2]) / float(compressedHeightMask);
                    coverageInfo._heightOffset = newHeightOffset;
                    coverageInfo._heightScale = newHeightScale;
                }
            } else {
                    // just write directly
                metalContext.GetNumericUniforms(ShaderStage::Compute).Bind(MakeResourceList(tileSet->GetUnorderedAccessView()));
                metalContext.Bind(*box._cs2);
                metalContext.Dispatch( 
                    unsigned(XlCeil(tile._width /float(threadGroupWidth))), 
                    unsigned(XlCeil(tile._height/float(threadGroupWidth))));
            }

            MetalStubs::UnbindCS<Metal::UnorderedAccessView>(metalContext, 0, 4);
        } CATCH (...) {
            // note, it's a real problem when we get a invalid resource get... 
            //  We should ideally stall until all the required resources are loaded
        } CATCH_END
    }

	static unsigned GetFieldIndex(const TerrainCell& cell, unsigned nodeIndex)
	{
		auto fi = std::find_if(
			cell._nodeFields.cbegin(), cell._nodeFields.cend(),
			[=](const TerrainCell::NodeField& field)
		{ return unsigned(nodeIndex) >= field._nodeBegin && unsigned(nodeIndex) < field._nodeEnd; });
		return (unsigned)std::distance(cell._nodeFields.cbegin(), fi);
	}

	static std::pair<Float2, Float2> GetNodeInCell(const TerrainCell& cell, unsigned nodeIndex)
	{
		const auto& sourceNode = *cell._nodes[nodeIndex];
		auto nodeMinInCell = Truncate(TransformPoint(sourceNode._localToCell, Float3(0.f, 0.f, 0.f)));
		auto nodeMaxInCell = Truncate(TransformPoint(sourceNode._localToCell, Float3(1.f, 1.f, 1.f)));
		return std::make_pair(nodeMinInCell, nodeMaxInCell);
	}

	class TileSetPtrs
	{
	public:
		TextureTileSet* _tileSet;
		std::vector<TerrainCellRenderer::NodeCoverageInfo>* _tiles;
		unsigned _coverageLayerIndex;

		TileSetPtrs(TerrainCellRenderer& renderer, TerrainCellRenderer::CellRenderInfo& cri, TerrainCoverageId layerId)
		{
			_tileSet = nullptr;
			_tiles = nullptr;
			_coverageLayerIndex = ~unsigned(0);

			if (layerId == CoverageId_Heights) {
				_tileSet = renderer._heightMapTileSet.get();
				_tiles = &cri._heightTiles;
			}
			else {
				for (unsigned c = 0; c<renderer._coverageIds.size(); ++c)
					if (renderer._coverageIds[c] == layerId) { _coverageLayerIndex = c; break; }
				if (_coverageLayerIndex >= unsigned(renderer._coverageTileSet.size())) return;
				_tileSet = renderer._coverageTileSet[_coverageLayerIndex].get();
				_tiles = &cri._coverage[_coverageLayerIndex]._tiles;
			}
		}
	};

    auto TerrainCellRenderer::FindIntersectingNodes(
		uint64_t filenameHash, TerrainCoverageId layerId,
		Float2 cellCoordMin, Float2 cellCoordMax) -> std::vector<FoundNode>
	{
		std::vector<FoundNode> result;

        auto i = LowerBound(_renderInfos, filenameHash);
        if (i == _renderInfos.end() || i->first != filenameHash) return result;

        auto& cri = *i->second;
        auto& sourceCell = *i->second->_sourceCell;
		TileSetPtrs p(*this, cri, layerId);
        if (!p._tileSet || !p._tiles) return result;

		for (unsigned nodeIndex=0; nodeIndex<unsigned(sourceCell._nodes.size()); ++nodeIndex) {
			auto& ni = (*p._tiles)[nodeIndex];
            if (!p._tileSet->IsValid(ni._tile)) continue;
			auto nodeInCell = GetNodeInCell(sourceCell, nodeIndex);

			//  We need to transform the coordinates for this node into
			//  the uber-surface coordinate system. If there's an overlap
			//  between the node coords and the update box, we need to do
			//  a copy.
            // todo -- this overlap should be data driven! (and dependent on the field index)
            const float overlap = 2.0f / 512.0f;
            if (    nodeInCell.first[0] <= cellCoordMax[0] && (nodeInCell.second[0]+overlap) >= cellCoordMin[0]
                &&  nodeInCell.first[1] <= cellCoordMax[1] && (nodeInCell.second[1]+overlap) >= cellCoordMin[1])
				result.push_back(FoundNode { &ni, GetFieldIndex(sourceCell, nodeIndex), nodeInCell.first, nodeInCell.second });
		}

		return result;
	}

    void    TerrainCellRenderer::ShortCircuit(
        RenderCore::Metal::DeviceContext& metalContext,
		ShortCircuitBridge& bridge,
		uint64_t cellHash, TerrainCoverageId layerId, 
		Float2 cellCoordMins, Float2 cellCoordMaxs)
    {
    	auto nodes = FindIntersectingNodes(cellHash, layerId, cellCoordMins, cellCoordMaxs);
		if (nodes.empty()) return;

		auto upd = bridge.GetShortCircuit(cellHash, Float2(0.f, 0.f), Float2(1.f, 1.f));
		for (const auto& n:nodes)
			ShortCircuitTileUpdate(
				metalContext, n._node->_tile, *n._node,
				layerId, n._fieldIndex, n._cellCoordMin, n._cellCoordMax,
				upd);
    }

	void TerrainCellRenderer::ShortCircuit(
		RenderCore::Metal::DeviceContext& metalContext,
		ShortCircuitBridge& bridge,
		uint64_t cellHash, TerrainCoverageId layerId,
		uint32 nodeIndex)
	{
		auto i = LowerBound(_renderInfos, cellHash);
		if (i == _renderInfos.end() || i->first != cellHash) return;

		TileSetPtrs p(*this, *i->second, layerId);
		auto& sourceCell = *i->second->_sourceCell;
		auto nodeInCell = GetNodeInCell(sourceCell, nodeIndex);

		if (!p._tileSet || !p._tiles || nodeIndex >= p._tiles->size()) return;
		const auto& tile = (*p._tiles)[nodeIndex]._tile;
		if (!p._tileSet->IsValid(tile)) return;
		
		auto upd = bridge.GetShortCircuit(cellHash, nodeInCell.first, nodeInCell.second);
		if (upd._srv && upd._cellMinsInResource[0] < upd._cellMaxsInResource[0] && upd._cellMinsInResource[1] < upd._cellMaxsInResource[1]) {
			// LogInfo 
			// 	<< "ShortCircuit " << nodeIndex << " field:" << GetFieldIndex(sourceCell, nodeIndex) 
			// 	<< " (" << nodeInCell.first[0] << "," << nodeInCell.first[1] << ") ("
			// 	<< nodeInCell.second[0] << "," << nodeInCell.second[1] << ") -- update";
			ShortCircuitTileUpdate(
				metalContext, tile,
				(*p._tiles)[nodeIndex], layerId, GetFieldIndex(sourceCell, nodeIndex),
				Float2(0.f, 0.f), Float2(1.f, 1.f), upd);
		}
	}

	void    TerrainCellRenderer::AbandonShortCircuitData(
		uint64_t cellHash, TerrainCoverageId layerId, 
		Float2 cellAbandonMins, Float2 cellAbandonMaxs)
    {
		// Find the tiles that would have been effected by short circuit operations in this area.
		// We will dump their data, so that they get reloaded from disk.
		auto nodes = FindIntersectingNodes(
			cellHash, layerId, cellAbandonMins, cellAbandonMaxs);
		for (const auto& n:nodes) {
			// We only need to blank out the tile data to force a reload
			// Any pending operations can be allowed to complete as is.
			// The old tile is now invalid, so we could explicitly remove it
			// from the TextureTileSet... However, this isn't really necessarily
			// because the LRU scheme will eventually overwrite it.
			n._node->_tile = TextureTile();
		}
	}

    void TerrainCellRenderer::SetShortCircuitSettings(const GradientFlagsSettings& gradientFlagsSettings)
    {
        _gradientFlagsSettings = gradientFlagsSettings;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    static bool CompareCellHash(const ShortCircuitBridge::CellRegion& lhs, const ShortCircuitBridge::CellRegion& rhs)
    {
        return lhs._cellHash < rhs._cellHash;
    }

    ShortCircuitUpdate ShortCircuitBridge::GetShortCircuit(uint64_t cellHash, Float2 cellMins, Float2 cellMaxs)
    {
        auto l = _source.lock();
        if (!l) return ShortCircuitUpdate {};

        auto i = LowerBound(_cells, cellHash);
        if (i != _cells.end() && i->first == cellHash) {
            UInt2 uberMins(
                i->second._uberMins[0] + unsigned((i->second._uberMaxs[0] - i->second._uberMins[0]) * cellMins[0]),
                i->second._uberMins[1] + unsigned((i->second._uberMaxs[1] - i->second._uberMins[1]) * cellMins[1]));
            UInt2 uberMaxs(
                i->second._uberMins[0] + unsigned((i->second._uberMaxs[0] - i->second._uberMins[0]) * cellMaxs[0]),
                i->second._uberMins[1] + unsigned((i->second._uberMaxs[1] - i->second._uberMins[1]) * cellMaxs[1]));

            return l->GetShortCircuit(uberMins, uberMaxs);
        }
        return ShortCircuitUpdate {};
    }

    void ShortCircuitBridge::QueueShortCircuit(UInt2 uberMins, UInt2 uberMaxs)
    {
        for (const auto&i:_cells) {
            const auto& r = i.second;
            if (    r._uberMins[0] >= uberMaxs[0] || r._uberMaxs[0] < uberMins[0]
                ||  r._uberMins[1] >= uberMaxs[1] || r._uberMaxs[1] < uberMins[1])
                continue;

            // queue a new update event
            Float2 cellMins(
                (int(uberMins[0]) - int(r._uberMins[0])) / float(r._uberMaxs[0] - r._uberMins[0]),
                (int(uberMins[1]) - int(r._uberMins[1])) / float(r._uberMaxs[1] - r._uberMins[1]));
            Float2 cellMaxs(
                (int(uberMaxs[0]) - int(r._uberMins[0])) / float(r._uberMaxs[0] - r._uberMins[0]),
                (int(uberMaxs[1]) - int(r._uberMins[1])) / float(r._uberMaxs[1] - r._uberMins[1]));
            cellMins[0] = std::max(cellMins[0], 0.f);
            cellMins[1] = std::max(cellMins[1], 0.f);
            cellMaxs[0] = std::min(cellMaxs[0], 1.f);
            cellMaxs[1] = std::min(cellMaxs[1], 1.f);

            auto compare = CellRegion { i.first, Float2(0.f, 0.f), Float2(0.f, 0.f) };
            auto q = std::lower_bound(
                _pendingUpdates.begin(), _pendingUpdates.end(), 
                compare, CompareCellHash);

            // we can choose to insert this as a separate event, or just combine it with
            // what is already there.
            if (q != _pendingUpdates.end() && q->_cellHash == i.first) {
                q->_cellMins[0] = std::min(q->_cellMins[0], cellMins[0]);
                q->_cellMins[1] = std::min(q->_cellMins[1], cellMins[1]);
                q->_cellMaxs[0] = std::max(q->_cellMaxs[0], cellMaxs[0]);
                q->_cellMaxs[1] = std::max(q->_cellMaxs[1], cellMaxs[1]);
            } else {
                CellRegion region { i.first, cellMins, cellMaxs };
                _pendingUpdates.insert(q, region);
            }
        }
    }

    void ShortCircuitBridge::QueueAbandon(UInt2 uberMins, UInt2 uberMaxs)
    {
        // look for overlapping cells
        for (const auto&i:_cells) {
            const auto& r = i.second;
            if (    r._uberMins[0] >= uberMaxs[0] || r._uberMaxs[0] < uberMins[0]
                ||  r._uberMins[1] >= uberMaxs[1] || r._uberMaxs[1] < uberMins[1])
                continue;

            // remove any pending updates -- because they've all be abandoned now.
            auto compare = CellRegion { i.first, Float2(0.f, 0.f), Float2(0.f, 0.f) };
            auto range = std::equal_range(
                _pendingUpdates.begin(), _pendingUpdates.end(), 
                compare, CompareCellHash);
            _pendingUpdates.erase(range.first, range.second);

            // queue a new abandon event
            Float2 cellMins(
                (int(uberMins[0]) - int(r._uberMins[0])) / float(r._uberMaxs[0] - r._uberMins[0]),
                (int(uberMins[1]) - int(r._uberMins[1])) / float(r._uberMaxs[1] - r._uberMins[1]));
            Float2 cellMaxs(
                (int(uberMaxs[0]) - int(r._uberMins[0])) / float(r._uberMaxs[0] - r._uberMins[0]),
                (int(uberMaxs[1]) - int(r._uberMins[1])) / float(r._uberMaxs[1] - r._uberMins[1]));
            cellMins[0] = std::max(cellMins[0], 0.f);
            cellMins[1] = std::max(cellMins[1], 0.f);
            cellMaxs[0] = std::min(cellMaxs[0], 1.f);
            cellMaxs[1] = std::min(cellMaxs[1], 1.f);

            auto q = std::lower_bound(
                _pendingAbandons.begin(), _pendingAbandons.end(), 
                compare, CompareCellHash);

            // we can choose to insert this as a separate event, or just combine it with
            // what is already there.
            if (q != _pendingAbandons.end() && q->_cellHash == i.first) {
                q->_cellMins[0] = std::min(q->_cellMins[0], cellMins[0]);
                q->_cellMins[1] = std::min(q->_cellMins[1], cellMins[1]);
                q->_cellMaxs[0] = std::max(q->_cellMaxs[0], cellMaxs[0]);
                q->_cellMaxs[1] = std::max(q->_cellMaxs[1], cellMaxs[1]);
            } else {
                CellRegion region { i.first, cellMins, cellMaxs };
                _pendingAbandons.insert(q, region);
            }
        }
    }

    void ShortCircuitBridge::WriteCells(UInt2 uberMins, UInt2 uberMaxs, ConsoleRig::IProgress* progress)
    {
        auto l = _source.lock();
        if (!l) return;

        // look for overlapping cells
        std::vector<const RegisteredCell*> overlappingCells;
        for (const auto&i:_cells) {
            const auto& r = i.second;
            if (    r._uberMins[0] >= uberMaxs[0] || r._uberMaxs[0] < uberMins[0]
                ||  r._uberMins[1] >= uberMaxs[1] || r._uberMaxs[1] < uberMins[1])
                continue;
            if (r._writeCells)
                overlappingCells.push_back(&r);
        }

        std::shared_ptr<ConsoleRig::IStep> step;
        if (progress)
            step = progress->BeginStep("Write cells", unsigned(overlappingCells.size()), false);

        for (auto c:overlappingCells) {
            assert(c && c->_writeCells);
            // we need to call the write function to commit these cells to disk
            // todo -- how do we handle exceptions here?
            (c->_writeCells)();

            if (step)
                step->Advance();
        }
    }

    void ShortCircuitBridge::RegisterCell(uint64_t cellHash, UInt2 uberMins, UInt2 uberMaxs, WriteCellsFn&& writeCells)
    {
        auto i = LowerBound(_cells, cellHash);
        if (i != _cells.end() && i->first == cellHash)
            Throw(std::logic_error("Duplicate cell registered to ShortCircuitBridge. Check for hash conflicts or overlapping cells."));

        _cells.insert(i, std::make_pair(cellHash, RegisteredCell { uberMins, uberMaxs, std::move(writeCells) }));
    }

    auto ShortCircuitBridge::GetPendingUpdates() -> std::vector<CellRegion>
    {
		// This will clear "_pendingUpdates" as a side effect.
		return std::move(_pendingUpdates);
    }

    auto ShortCircuitBridge::GetPendingAbandons() -> std::vector<CellRegion>
    {
        // This will clear "_pendingAbandons" as a side effect.
        return std::move(_pendingAbandons);
    }

    ShortCircuitBridge::ShortCircuitBridge(const std::shared_ptr<IShortCircuitSource>& source)
    : _source(std::move(source))
    {}

    ShortCircuitBridge::~ShortCircuitBridge() {}

    IShortCircuitSource::~IShortCircuitSource() {}


    ShortCircuitUpdate::ShortCircuitUpdate() {}
    ShortCircuitUpdate::~ShortCircuitUpdate() {}
    ShortCircuitUpdate::ShortCircuitUpdate(ShortCircuitUpdate&& moveFrom) never_throws
    : _srv(std::move(moveFrom._srv))
    , _cellMinsInResource(moveFrom._cellMinsInResource), _cellMaxsInResource(moveFrom._cellMaxsInResource)
    {}

    ShortCircuitUpdate& ShortCircuitUpdate::operator=(ShortCircuitUpdate&& moveFrom) never_throws
    {
        _srv = std::move(moveFrom._srv);
        _cellMinsInResource = moveFrom._cellMinsInResource;
        _cellMaxsInResource = moveFrom._cellMaxsInResource;
        return *this;
    }

}

