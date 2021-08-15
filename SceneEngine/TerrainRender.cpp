// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TerrainRender.h"
#include "Terrain.h"
#include "TextureTileSet.h"
#include "TerrainMaterialTextures.h"
#include "TerrainScaffold.h"

#include "SimplePatchBox.h"
#include "Noise.h"
#include "SceneEngineUtils.h"
#include "MetalStubs.h"

#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Format.h"

#include "../Assets/Assets.h"
#include "../ConsoleRig/Console.h"
#include "../OSServices/Log.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Utility/StringFormat.h"
#include "../Utility/MemoryUtils.h"
#include "../Math/ProjectionMath.h"
#include "../Math/Transformations.h"
#include "../xleres/FileList.h"

#include <stack>

#include "../OSServices/WinAPI/IncludeWindows.h"

namespace SceneEngine
{
    using namespace RenderCore;
    using namespace RenderCore::Metal;

    //////////////////////////////////////////////////////////////////////////////////////////

    uint64      TerrainCellId::BuildHash() const
    {
        uint64 result = Hash64(_heightMapFilename);
        for (unsigned c=0; c<dimof(_coverageFilename); ++c)
            if (_coverageFilename[c] && _coverageFilename[c][0])
                result = HashCombine(Hash64(_coverageFilename[c], XlStringEnd(_coverageFilename[c])), result);
        return result;
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    static float CalculateScreenSpaceEdgeLength(
        const Float4x4& localToProjection,
        float viewportWidth, float viewportHeight)
    {
        const Float3 corners[] = {
            Float3(0.f, 0.f, 0.f), Float3(1.f, 0.f, 0.f),
            Float3(1.f, 1.f, 0.f), Float3(0.f, 1.f, 0.f)
        };

        Float3 transformedCorners[4];
        for (unsigned c=0; c<dimof(transformedCorners); ++c) {
            auto t = localToProjection * Expand(corners[c], 1.f);
            transformedCorners[c] = Float3(t[0]/t[3] * .5f * viewportWidth, t[1]/t[3] * .5f * viewportHeight, t[2]/t[3]);
        }

        float screenSpaceLengths[4] = {
            Magnitude(transformedCorners[0] - transformedCorners[1]),
            Magnitude(transformedCorners[1] - transformedCorners[2]),
            Magnitude(transformedCorners[2] - transformedCorners[3]),
            Magnitude(transformedCorners[3] - transformedCorners[0])
        };

        return std::max(    std::max(screenSpaceLengths[0], screenSpaceLengths[1]),
                            std::max(screenSpaceLengths[2], screenSpaceLengths[3]));
    }

    struct TileConstants
    {
    public:
        Float4x4 _localToCell;
        Int3 _heightMapOrigin;
        int _tileDimensionsInVertices;
        Int4 _neighbourLodDiffs;

        Float4  _coverageCoordMins[TerrainCellId::MaxCoverageCount], _coverageCoordMaxs[TerrainCellId::MaxCoverageCount];
        Int4    _coverageOrigin[TerrainCellId::MaxCoverageCount];
    };

    std::vector<TerrainRenderingContext::QueuedNode> TerrainRenderingContext::_queuedNodes;        // HACK -- static to avoid allocation!

    TerrainRenderingContext::TerrainRenderingContext(
        const TerrainCoverageId* coverageLayers, 
        const CoverageFormat* coverageFmts, 
        unsigned coverageLayerCount,
        bool encodedGradientFlags,
        PriorityMode priorityMode)
    : _currentViewport(0.f, 0.f, 0.f, 0.f, 0.f, 0.f)
    {
        _indexDrawCount = 0;
        // _isTextured = isTextured;
        // _elementSize = elementSize;
        _dynamicTessellation = false;
        _encodedGradientFlags = encodedGradientFlags;
        _priorityMode = priorityMode;

        _coverageLayerCount = std::min(coverageLayerCount, TerrainCellId::MaxCoverageCount);
        for (unsigned c=0; c<_coverageLayerCount; ++c) {
            _coverageLayerIds[c] = coverageLayers[c];
            _coverageFmts[c] = coverageFmts[c];
        }

        _tileConstantsBuffer = MakeMetalCB(nullptr, sizeof(TileConstants));
        _localTransformConstantsBuffer = MakeMetalCB(nullptr, sizeof(Techniques::LocalTransformConstants));
    }

    class TerrainRenderingResources
    {
    public:
        class Desc
        {
        public:
            TerrainRenderingContext::Mode _mode;
            bool _doExtraSmoothing, _noisyTerrain;
            bool _drawWireframe;
            bool _encodedGradientFlags;
            bool _vegetationAlignToTerrainUp;
            unsigned _strataCount;
            TerrainCoverageId _visLayer;

            TerrainCoverageId _coverageIds[TerrainCellId::MaxCoverageCount];
            CoverageFormat _coverageFmts[TerrainCellId::MaxCoverageCount];

            Desc(   TerrainRenderingContext::Mode mode,
                    const TerrainCoverageId* coverageIdsBegin, const TerrainCoverageId* coverageIdsEnd,
                    const CoverageFormat* coverageFmtsBegin, const CoverageFormat* coverageFmtsEnd,
                    bool doExtraSmoothing, bool noisyTerrain, bool encodedGradientFlags,
                    bool vegetationAlignToTerrainUp,
                    bool drawWireframe, unsigned strataCount,
                    TerrainCoverageId visLayer)
            {
                std::fill((uint8*)this, (uint8*)PtrAdd(this, sizeof(*this)), 0ui8);
                _mode = mode;
                _doExtraSmoothing = doExtraSmoothing;
                _noisyTerrain = noisyTerrain;
                _encodedGradientFlags = encodedGradientFlags;
                _vegetationAlignToTerrainUp = vegetationAlignToTerrainUp;
                _drawWireframe = drawWireframe;
                _strataCount = strataCount;
                _visLayer = visLayer;

                for (unsigned c=0; c<std::min(unsigned(coverageIdsEnd-coverageIdsBegin), TerrainCellId::MaxCoverageCount); ++c)
                    _coverageIds[c] = coverageIdsBegin[c];

                for (unsigned c=0; c<std::min(unsigned(coverageFmtsEnd-coverageFmtsBegin), TerrainCellId::MaxCoverageCount); ++c)
                    _coverageFmts[c] = coverageFmtsBegin[c];
            }
        };

        const DeepShaderProgram* _shaderProgram;
        RenderCore::Metal::BoundUniforms _boundUniforms;
        BoundClassInterfaces _dynLinkage;

        TerrainRenderingResources(const Desc& desc);

        const ::Assets::DependencyValidation& GetDependencyValidation() const   { return _validationCallback; }
    private:
        ::Assets::DependencyValidation  _validationCallback
    };

    static const char* AsShaderFormat(CoverageFormat fmt)
    {
        const char* floatNames[]    = { "float", "float2", "float3", "float4" };
        const char* uintNames[]     = { "uint", "uint2", "uint3", "uint4" };
        const char* intNames[]      = { "int", "int2", "int3", "int4" };

        using namespace RenderCore;
        auto compCount = (size_t)GetComponentCount(GetComponents(fmt));
        switch (GetComponentType(fmt)) {
        case FormatComponentType::UNorm:
        case FormatComponentType::SNorm:
        case FormatComponentType::UNorm_SRGB:
        case FormatComponentType::UnsignedFloat16:
        case FormatComponentType::SignedFloat16:
        case FormatComponentType::Float: return floatNames[std::min(compCount-1, dimof(floatNames))];
        case FormatComponentType::UInt: return uintNames[std::min(compCount-1, dimof(uintNames))];
        case FormatComponentType::SInt: return intNames[std::min(compCount-1, dimof(intNames))];
        default: return "uint";
        }
    }

	static UniformsStreamInterface MakeUniformsStreamInterface()
	{
		UniformsStreamInterface usi;
		usi.BindConstantBuffer(0, {Hash64("LocalTransform")});
		usi.BindConstantBuffer(1, {Hash64("TileBuffer")});
		usi.BindShaderResource(0, Hash64("GradTexture"));	// for noise calculation
		usi.BindShaderResource(0, Hash64("PermTexture"));	// for noise calculation
		return usi;
	}

    TerrainRenderingResources::TerrainRenderingResources(const Desc& desc)
    {
        const bool isTextured = true;

        StringMeld<1024, char> definesBuffer;
        definesBuffer << "DO_EXTRA_SMOOTHING=" << int(desc._doExtraSmoothing);
        definesBuffer << ";SOLIDWIREFRAME_TEXCOORD=" << int(isTextured);
        definesBuffer << ";DO_ADD_NOISE=" << int(desc._noisyTerrain);
        definesBuffer << ";VSOUT_HAS_WORLD_POSITION=1;SOLIDWIREFRAME_WORLDPOSITION=1";
        definesBuffer << ";DRAW_WIREFRAME=" << int(desc._drawWireframe);
        definesBuffer << ";STRATA_COUNT=" << desc._strataCount;
        if (desc._encodedGradientFlags)
            definesBuffer << ";ENCODED_GRADIENT_FLAGS=1";

        for (unsigned c=0; c<dimof(desc._coverageIds); ++c)
            if (desc._coverageIds[c]) {
                definesBuffer << ";COVERAGE_" << desc._coverageIds[c] << "=" << c;
                if (desc._coverageIds[c] == desc._visLayer)
                    definesBuffer << ";VISUALIZE_COVERAGE=" << c;
                definesBuffer << ";COVERAGE_FMT_" << c << "=" << AsShaderFormat(desc._coverageFmts[c]);
            }

        const char* ps = isTextured 
            ? "xleres/objects/terrain/TerrainBasePS.hlsl:ps_main:!ps_*" 
            : SOLID_WIREFRAME_PIXEL_HLSL ":main:ps_*";

        if (Tweakable("LightingModel", 0) == 1 && isTextured) {
                // manually switch to the forward shading pixel shader depending on the lighting model
            ps = "xleres/objects/terrain/TerrainTexturing.hlsl:ps_main_forward:!ps_*";
        }

        InputElementDesc eles[] = {
            InputElementDesc("INTERSECTION", 0, Format::R32G32B32A32_FLOAT)
        };

        const char* gs = "null";
        if (desc._mode == TerrainRenderingContext::Mode_RayTest) {
            ps = "null";
            unsigned strides = sizeof(float)*4;
            MetalStubs::SetDefaultStreamOutputInitializers(
				StreamOutputInitializers{MakeIteratorRange(eles), MakeIteratorRange(&strides, &strides+1)});
            gs = "xleres/objects/terrain/TerrainIntersection.hlsl:gs_intersectiontest:gs_*";
        } else if (desc._mode == TerrainRenderingContext::Mode_VegetationPrepare) {
            definesBuffer << ";TERRAIN_NORMAL=" << unsigned(desc._vegetationAlignToTerrainUp);
            ps = "null";
            gs = SCENE_ENGINE_RES "/Vegetation/InstanceSpawn.geo.hlsl:main:gs_*";
        } else if (desc._drawWireframe) {
            gs = SOLID_WIREFRAME_GEO_HLSL ":main:gs_*";
        }

        const DeepShaderProgram* shaderProgram;
        TRY {
            shaderProgram = &::Assets::Legacy::GetAssetDep<DeepShaderProgram>(
                "xleres/objects/terrain/GeoGenerator.hlsl:vs_dyntess_main:vs_*", 
                gs, ps, 
                "xleres/objects/terrain/GeoGenerator.hlsl:hs_main:hs_*",
                "xleres/objects/terrain/GeoGenerator.hlsl:ds_main:ds_*",
                definesBuffer.get());
        } CATCH (...) {
			MetalStubs::SetDefaultStreamOutputInitializers(StreamOutputInitializers{});
            throw;
        } CATCH_END

        if (desc._mode == TerrainRenderingContext::Mode_RayTest) {
			MetalStubs::SetDefaultStreamOutputInitializers(StreamOutputInitializers{});
        }

        BoundUniforms boundUniforms(
			*shaderProgram,
			Metal::PipelineLayoutConfig{},
			Techniques::TechniqueContext::GetGlobalUniformsStreamInterface(),
			MakeUniformsStreamInterface());

        if (shaderProgram->DynamicLinkingEnabled()) {
            _dynLinkage = BoundClassInterfaces(*shaderProgram);
            _dynLinkage.Bind(Hash64("MainTexturing"), 0, "GradFlagTexturing");
        }

        auto validationCallback = ::Assets::GetDepValSys().Make();
        validationCallback.RegisterDependency(shaderProgram->GetDependencyValidation());

        _shaderProgram = shaderProgram;
        _boundUniforms = std::move(boundUniforms);
        _validationCallback = std::move(validationCallback);
    }

    bool g_TerrainVegetationSpawn_AlignToTerrainUp = false;

    void        TerrainRenderingContext::EnterState(DeviceContext& context, Techniques::ParsingContext& parserContext, const TerrainMaterialTextures& texturing, UInt2 elementSize, Mode mode)
    {
		auto& perlinNoiseRes = ConsoleRig::FindCachedBox<PerlinNoiseResources>(PerlinNoiseResources::Desc());
		ConstantBufferView cbvs[] = { &_localTransformConstantsBuffer, &_tileConstantsBuffer };
		const ShaderResourceView* srvs[] = { &perlinNoiseRes._gradShaderResource, &perlinNoiseRes._permShaderResource };

        _dynamicTessellation = Tweakable("TerrainDynamicTessellation", true);
        if (_dynamicTessellation) {
            const auto doExtraSmoothing = Tweakable("TerrainExtraSmoothing", false);
            const auto noisyTerrain = Tweakable("TerrainNoise", false);
            const auto drawWireframe = Tweakable("TerrainWireframe", false);
            const auto visLayer = Tweakable("TerrainVisCoverage", 0);

            auto& box = ConsoleRig::FindCachedBoxDep2<TerrainRenderingResources>(
                mode, _coverageLayerIds, &_coverageLayerIds[_coverageLayerCount],
                _coverageFmts, &_coverageFmts[_coverageLayerCount],
                doExtraSmoothing, noisyTerrain, _encodedGradientFlags, 
                g_TerrainVegetationSpawn_AlignToTerrainUp,
                drawWireframe, texturing._strataCount,
                visLayer);

            if (box._shaderProgram->DynamicLinkingEnabled()) {
                context.Bind(*box._shaderProgram, box._dynLinkage);
            } else {
                context.Bind(*box._shaderProgram);
            }
            context.Bind(Topology::PatchList4);
            box._boundUniforms.Apply(context, 0, parserContext.GetGlobalUniformsStream());
			box._boundUniforms.Apply(context, 1, 
				UniformsStream { MakeIteratorRange(cbvs), UniformsStream::MakeResources(MakeIteratorRange(srvs)) });

                //  when using dynamic tessellation, the basic geometry should just be
                //  a quad. We'll use a vertex generator shader.
        } else {
            const ShaderProgram* shaderProgram;
            if (mode == Mode_Normal) {
                shaderProgram = &::Assets::Legacy::GetAssetDep<ShaderProgram>(
                    "xleres/objects/terrain/Basic.hlsl:vs_basic:vs_*", 
                    SOLID_WIREFRAME_GEO_HLSL ":main:gs_*", 
                    SOLID_WIREFRAME_PIXEL_HLSL ":main:ps_*", "");
            } else if (mode == Mode_VegetationPrepare) {
                shaderProgram = &::Assets::Legacy::GetAssetDep<ShaderProgram>(
                    "xleres/objects/terrain/Basic.hlsl:vs_basic:vs_*", 
                    SCENE_ENGINE_RES "/Vegetation/InstanceSpawn.geo.hlsl:main:gs_*", 
                    "", "VSOUT_HAS_WORLD_POSITION=1");
            } else {
                shaderProgram = &::Assets::Legacy::GetAssetDep<ShaderProgram>(
                    "xleres/objects/terrain/Basic.hlsl:vs_basic:vs_*", 
                    "xleres/objects/terrain/TerrainIntersection.hlsl:gs_intersectiontest:gs_*", 
                    "", "VSOUT_HAS_WORLD_POSITION=1");
            }

            context.Bind(*shaderProgram);

            BoundUniforms uniforms(
				*shaderProgram,
				Metal::PipelineLayoutConfig{},
				Techniques::TechniqueContext::GetGlobalUniformsStreamInterface(),
				MakeUniformsStreamInterface());
            uniforms.Apply(context, 0, parserContext.GetGlobalUniformsStream());
			uniforms.Apply(context, 1, 
				UniformsStream { MakeIteratorRange(cbvs), UniformsStream::MakeResources(MakeIteratorRange(srvs)) });

            auto& simplePatchBox = ConsoleRig::FindCachedBox<SimplePatchBox>(
                SimplePatchBox::Desc(elementSize[0], elementSize[1], true));
            context.Bind(*(Metal::Resource*)simplePatchBox._simplePatchIndexBuffer->QueryInterface(typeid(Metal::Resource).hash_code()), Format::R32_UINT);
            context.Bind(Topology::TriangleList);
            _indexDrawCount = simplePatchBox._simplePatchIndexCount;
        }

            ////-////-/====/-////-////
        context.UnbindInputLayout();
            ////-////-/====/-////-////

        context.Bind(Techniques::CommonResources()._dssReadWrite);
    }

    void    TerrainRenderingContext::ExitState(DeviceContext& context, RenderCore::Techniques::ParsingContext& )
    {
        MetalStubs::UnbindTessellationShaders(context);
        MetalStubs::UnbindGS<ShaderResourceView>(context, 0, 5);
        MetalStubs::UnbindPS<ShaderResourceView>(context, 0, 5);
        context.Bind(Topology::TriangleList);
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    static std::vector<TerrainCellTexture const*> BuildMappedCoverageTextures(
        const TerrainCellId& cell, 
        const std::vector<TerrainCoverageId>& ids, const ITerrainFormat& ioFormat)
    {
        std::vector<TerrainCellTexture const*> tex;
        tex.resize(ids.size(), nullptr);
        for (unsigned c=0; c<unsigned(ids.size()); ++c) {
            auto end = &cell._coverageIds[dimof(cell._coverageIds)];
            auto i = std::find(cell._coverageIds, end, ids[c]);
            if (i != end) {
                tex[c] = &ioFormat.LoadCoverage(cell._coverageFilename[i-cell._coverageIds]);
            }
        }
        return std::move(tex);
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    void    TerrainCellRenderer::CullNodes( 
        const RenderCore::Techniques::ProjectionDesc& projDesc, 
        TerrainRenderingContext& terrainContext, TerrainCollapseContext& collapseContext,
        const TerrainCellId& cell)
    {
            // Cull on a cell level (prevent loading of distance cell resources)
            //      todo -- if we knew the cell min/max height, we could do this more accurately
        if (CullAABB_Aligned(projDesc._worldToProjection, cell._aabbMin, cell._aabbMax, RenderCore::Techniques::GetDefaultClipSpaceType()))
            return;

            // look for a valid "CellRenderInfo" already in our cache
            //  Note that we have a very flexible method for how cells are addressed
            //  see the comments in PlacementsRenderer::Render for more information on 
            //  this. It means we can overlapping cells, or switch cells in and our as
            //  time or situation changes.
        auto hash = cell.BuildHash();
        auto i = LowerBound(_renderInfos, hash);

        CellRenderInfo* renderInfo = nullptr;
        if (i != _renderInfos.end() && i->first == hash) {

                // if it's been invalidated on disk, reload
            bool invalidation = false;
            if (i->second->_sourceCell) {
                const auto& cell2 = *i->second;
                invalidation |= (cell2._sourceCell->GetDependencyValidation().GetValidationIndex()!=0);
                for (auto q=cell2._coverage.cbegin(); q!=cell2._coverage.cend(); ++q)
                    invalidation |= (q->_source->GetDependencyValidation().GetValidationIndex()!=0);
            }
            if (invalidation) {
                    // before we delete it, we need to erase it from the pending uploads
                _pendingUploads.erase(
                    std::remove_if(
                        _pendingUploads.begin(), _pendingUploads.end(), 
                        [=](const UploadPair& p) { return p.first == i->second.get(); }),
                    _pendingUploads.end());
                i->second.reset();

                auto tex = BuildMappedCoverageTextures(cell, _coverageIds, *_ioFormat);
                i->second = std::make_unique<CellRenderInfo>(
                    std::ref(_ioFormat->LoadHeights(cell._heightMapFilename)), 
                    AsPointer(tex.cbegin()), AsPointer(tex.cend()));
            }

            renderInfo = i->second.get();

        } else {

            auto tex = BuildMappedCoverageTextures(cell, _coverageIds, *_ioFormat);
            auto newRenderInfo = std::make_unique<CellRenderInfo>(
                std::ref(_ioFormat->LoadHeights(cell._heightMapFilename)), 
                AsPointer(tex.cbegin()), AsPointer(tex.cend()));
            auto newIterator = _renderInfos.insert(i, CRIPair(hash, std::move(newRenderInfo)));
            assert(newIterator->first == hash);
            renderInfo = newIterator->second.get();

        }

            // find all of the nodes that we're go to render this frame
        static bool useNewCulling = true;
        if (!useNewCulling) {
            CullNodes(
                projDesc, terrainContext, 
                *renderInfo, cell._cellToWorld);
        } else {
            CullNodes(
                terrainContext, collapseContext,
                projDesc._worldToProjection,
                ExtractTranslation(projDesc._cameraToWorld),
                *renderInfo, cell._cellToWorld);
        }
    }

	void        TerrainCellRenderer::CompletePendingUploads()
	{
		// note; ideally we want an erase/remove that will reorder the vector here... That is on erase,
		// just do a std::swap between the erased item and the last item.
		// that should be most efficient when we complete multiple operations per frame (which will be normal)
		auto i = std::remove_if(
			_pendingUploads.begin(), _pendingUploads.end(),
			[=](const UploadPair& p) { return p.first->CompleteUpload(p.second, _heightMapTileSet->GetBufferUploads()); });
		_pendingUploads.erase(i, _pendingUploads.end());
	}

	template<typename Iterator, typename Predicate>
		Iterator MoveToBack(Iterator begin, Iterator end, Predicate predicate) 
	{
		// "remove_if" type algorithm that will not retain the same ordering, but
		// is generally efficient. Also, the "removed" items can be accessed in the 
		// range between newEnd and end
		Iterator newEnd = end;
		for (auto i = begin; i != newEnd;) {
			if (predicate(*i)) {
				if ((newEnd-1) != i)
					std::swap(*(newEnd-1), *i);
				--newEnd;
			} else ++i;
		}
		return newEnd;
	}

	std::vector<std::pair<uint64, uint32>> TerrainCellRenderer::CompletePendingUploads_Bridge()
    {
            // note; ideally we want an erase/remove that will reorder the vector here... That is on erase,
            // just do a std::swap between the erased item and the last item.
            // that should be most efficient when we complete multiple operations per frame (which will be normal)
        auto i = MoveToBack(
            _pendingUploads.begin(), _pendingUploads.end(),
            [=](const UploadPair& p) { return p.first->CompleteUpload(p.second, _heightMapTileSet->GetBufferUploads()); });

            // Each of the uploads between i and _pendingUploads.end() are completed this frame. 
            // We can search through them to see if any need to be touched by a short-circuit 
            // from the uber surface.
		std::vector<std::pair<uint64, uint32>> result;
		result.reserve(std::distance(i, _pendingUploads.end()));
        for (auto i2=i; i2!=_pendingUploads.end(); ++i2) {
			auto ri = std::find_if(_renderInfos.begin(), _renderInfos.end(), [i2](const CRIPair& p) { return p.second.get() == i2->first; });
			assert(ri != _renderInfos.end());
			result.push_back(std::make_pair(ri->first, i2->second));
        }

        _pendingUploads.erase(i, _pendingUploads.end());
		return result;
    }

    void        TerrainCellRenderer::QueueUploads(TerrainRenderingContext& terrainContext)
    {
        bool refreshPriorityMode = (terrainContext._priorityMode != TerrainRenderingContext::PriorityMode::None) != _heightMapTileSet->GetPriorityMode();
        refreshPriorityMode |= !_coverageTileSet.empty() && (terrainContext._priorityMode == TerrainRenderingContext::PriorityMode::All) != _coverageTileSet[0]->GetPriorityMode();
        if (refreshPriorityMode) {
            UnloadCachedData();
            terrainContext._queuedNodes.clear();
            _heightMapTileSet->SetPriorityMode(terrainContext._priorityMode != TerrainRenderingContext::PriorityMode::None);
            //      Perhaps some coverage layers are never set to frame priority mode...?
            //      We can render distant terrain geometry without all of the coverage layers
            //      -- but it might look a little strange flickering in.
            for (auto& t:_coverageTileSet)
                t->SetPriorityMode(terrainContext._priorityMode == TerrainRenderingContext::PriorityMode::All);
        }

            //  After we've culled the list of nodes we need for this frame, let's queue all of the uploads that
            //  need to occur. Sort the list by priority so that if we run out of upload slots, the most important
            //  nodes will be processed first.
        std::sort(
            terrainContext._queuedNodes.begin(), terrainContext._queuedNodes.end(),
            [](const TerrainRenderingContext::QueuedNode& lhs, const TerrainRenderingContext::QueuedNode& rhs)
                { return lhs._priority < rhs._priority; });

        // queue new uploads
        static unsigned frameUploadLimit = 500;
        static unsigned totalActiveUploadLimit = 1000;

        unsigned uploadsThisFrame = 0;
        typedef TerrainRenderingContext::QueuedNode::Flags Flags;
        for (auto i = terrainContext._queuedNodes.begin(); i!=terrainContext._queuedNodes.end(); ++i) {
            if (uploadsThisFrame >= frameUploadLimit) break;
            if (_pendingUploads.size() >= totalActiveUploadLimit) break;

            auto& cellRenderInfo = *i->_cell;
            unsigned n2 = i->_absNodeIndex;
            if (i->_flags & Flags::NeedsHeightMapUpload) {
                auto& sourceCell = *cellRenderInfo._sourceCell;
                auto& sourceNode = *sourceCell._nodes[n2];

                auto& heightTile = cellRenderInfo._heightTiles[n2];
				#if defined(TERRAIN_ENABLE_EDITING)
					heightTile._heightScale = sourceNode._localToCell(2,2);
					heightTile._heightOffset = sourceNode._localToCell(2,3);
				#endif
                heightTile.Queue(
                    *_heightMapTileSet, cellRenderInfo._heightMapStreamingFilePtr,
                    unsigned(sourceNode._heightMapFileOffset), unsigned(sourceNode._heightMapFileSize));
                ++uploadsThisFrame;

                _pendingUploads.push_back(UploadPair(&cellRenderInfo, n2));
            }
            
            if (i->_flags & Flags::NeedsCoverageUploadMask) {
				bool anyCoverageUploads = false;
                for (unsigned covIndex=0; covIndex<unsigned(cellRenderInfo._coverage.size()); ++covIndex) {
                    auto n = i->_absNodeIndex;
                    if (i->_flags & (Flags::NeedsCoverageUpload0<<covIndex)) {
                        auto& c = cellRenderInfo._coverage[covIndex];
                        c._tiles[n].Queue(
                            *_coverageTileSet[covIndex], c._streamingFilePtr, 
                            c._source->_nodeFileOffsets[n], c._source->_nodeTextureByteCount);

                        ++uploadsThisFrame;
                        anyCoverageUploads = true;
                    }
                }
				if (anyCoverageUploads)
					_pendingUploads.push_back(UploadPair(&cellRenderInfo, n2 | (1u << 31u)));
            }
        }
    }

    static Int2 ToFieldXY(const TerrainCell::NodeField& field, unsigned nodeIndex)
    {
        signed fieldx = signed(nodeIndex) % field._widthInNodes;
        signed fieldy = signed(nodeIndex) / field._widthInNodes;
        return Int2(fieldx, fieldy);
    }

    static unsigned ToNodeIndex(const TerrainCell::NodeField& field, Int2 xy)
    {
        if (xy[0] < 0 || xy[1] < 0 || xy[0] >= signed(field._widthInNodes) || xy[1] >= signed(field._heightInNodes)) {
            return ~unsigned(0x0);
        }
        return xy[1] * field._widthInNodes + xy[0];
    }

    auto TerrainCollapseContext::FindNeighbour(const NodeID& id, std::vector<Node>& workingField, unsigned workingFieldLOD) -> Node*
    {
        if (id._nodeId == ~unsigned(0x0) || id._lodField == ~unsigned(0x0)) { return nullptr; }

        if (id._lodField == workingFieldLOD) {
            auto i = std::find_if(workingField.begin(), workingField.end(),
                [&](const Node& n) { return n._id.Equivalent(id); });
            if (i != workingField.end()) {
                return AsPointer(i);
            } else {
                return nullptr;
            }
        } else {
            auto& field = _activeNodes[id._lodField];
            auto i = std::find_if(field.begin(), field.end(),
                [&](const Node& n) { return n._id.Equivalent(id); });
            if (i != field.end()) {
                return AsPointer(i);
            } else {
                return nullptr;
            }
        }
    }

    void TerrainCollapseContext::AttemptLODPromote(unsigned startLod, TerrainRenderingContext& renderingContext)
    {
            //  Attempt to collapse the nodes in the given start LOD
            //  Some of the nodes will be shifted to the next highest LOD
            //  (but only if they meet the restrictions & requirements)
            //
            //  Every time we shift nodes, we have to update the neighbour
            //  references.

        const auto compressedHeightMask = CompressedHeightMask(renderingContext._encodedGradientFlags);

        std::vector<Node> collapsedField;
        for (auto n=_activeNodes[startLod].begin(); n!=_activeNodes[startLod].end(); ++n) {

            auto& sourceCell = *_cells[n->_id._cellId]->_sourceCell;
            if (sourceCell._nodeFields.size() <= (n->_id._lodField+1)) { continue; }        // not collapsible, because no high levels of detail
            auto& field = sourceCell._nodeFields[n->_id._lodField];

            bool doCollapse = false;
            if (n->_screenSpaceEdgeLength > _screenSpaceEdgeThreshold) {
                    //  Try to drop to a lower lod.
                    //  Currently, we can only do this if all of our neighbours are at
                    //  a equal or lower LOD (otherwise that would mean having more than 2
                    //  neighbours on an edge)
                    //  When there is no neighbour, we want this test to succeeded...
                    //      -- note; there is a problem if the neighbour has just been culled
                    //              this frame. The neighbour may have been prevent a LOD
                    //              promote; but as soon as it is no visible, the promote will
                    //              occur. It may appear too sudden and weird.
                doCollapse = 
                        n->_neighbours[ Neighbours::TopEdgeLeft     ]._lodField >= startLod
                    &&  n->_neighbours[ Neighbours::RightEdgeTop    ]._lodField >= startLod
                    &&  n->_neighbours[ Neighbours::BottomEdgeRight ]._lodField >= startLod
                    &&  n->_neighbours[ Neighbours::LeftEdgeBottom  ]._lodField >= startLod;
            }

            if (doCollapse) {
                    //  mark this node as collapsed, and then 
                    //  create 4 new nodes, at high levels of detail
                n->_lodPromoted = true;

                auto baseXY = ToFieldXY(field, n->_id._nodeId-field._nodeBegin);
                auto& newField = sourceCell._nodeFields[n->_id._lodField+1];
                auto ni0 = ToNodeIndex(newField, baseXY*2 + Int2(0,0)) + newField._nodeBegin;
                auto ni1 = ToNodeIndex(newField, baseXY*2 + Int2(1,0)) + newField._nodeBegin;
                auto ni2 = ToNodeIndex(newField, baseXY*2 + Int2(0,1)) + newField._nodeBegin;
                auto ni3 = ToNodeIndex(newField, baseXY*2 + Int2(1,1)) + newField._nodeBegin;

                auto f = n->_id._lodField+1;
                Node newNodes[4] = {
                    Node(NodeID(f, ni0, n->_id._cellId)),
                    Node(NodeID(f, ni1, n->_id._cellId)),
                    Node(NodeID(f, ni2, n->_id._cellId)),
                    Node(NodeID(f, ni3, n->_id._cellId))
                };

                {
                    auto& cellToProjection = _cellToProjection[n->_id._cellId];
                        //  We have to do another culling test to see if any of these smaller
                        //  nodes are off-screen
                    for (unsigned c=0; c<dimof(newNodes); ++c) {
                        assert(newNodes[c]._id._nodeId != ~unsigned(0x0));
						auto localToCell = _cells[n->_id._cellId]->NodeToCell(newNodes[c]._id._nodeId);
                        __declspec(align(16)) auto localToProjection = Combine(localToCell, cellToProjection);

                            // once a parent node is entirely within the frustum, so to must be all children
                        if (n->_entirelyWithinFrustum) {
                            auto aabbTest = TestAABB_Aligned(localToProjection, Float3(0.f, 0.f, 0.f), Float3(1.f, 1.f, float(compressedHeightMask)), RenderCore::Techniques::GetDefaultClipSpaceType());
                            if (aabbTest == CullTestResult::Culled) { 
                                newNodes[c]._id._nodeId = ~unsigned(0x0);
                                continue; 
                            }

                            newNodes[c]._entirelyWithinFrustum = aabbTest == CullTestResult::Within;
                        } else {
                            newNodes[c]._entirelyWithinFrustum = true;
                        }

                        if ((f+1) < sourceCell._nodeFields.size()) {
                            newNodes[c]._screenSpaceEdgeLength = CalculateScreenSpaceEdgeLength(
                                localToProjection, renderingContext._currentViewport.Width, renderingContext._currentViewport.Height);
                        } else {
                            newNodes[c]._screenSpaceEdgeLength = std::numeric_limits<float>::max();
                        }
                    }

                        //  Setup the neighbours for the new nodes
                        //  Do this after culling, so we don't have neighbour references to
                        //  nodes that were culled out.
                        // (internal)
                    newNodes[0]._neighbours[Neighbours::RightEdgeTop] = newNodes[0]._neighbours[Neighbours::RightEdgeBottom] = newNodes[1]._id;
                    newNodes[0]._neighbours[Neighbours::BottomEdgeLeft] = newNodes[0]._neighbours[Neighbours::BottomEdgeRight] = newNodes[2]._id;

                    newNodes[1]._neighbours[Neighbours::LeftEdgeTop] = newNodes[1]._neighbours[Neighbours::LeftEdgeBottom] = newNodes[0]._id;
                    newNodes[1]._neighbours[Neighbours::BottomEdgeLeft] = newNodes[1]._neighbours[Neighbours::BottomEdgeRight] = newNodes[3]._id;

                    newNodes[2]._neighbours[Neighbours::RightEdgeTop] = newNodes[2]._neighbours[Neighbours::RightEdgeBottom] = newNodes[3]._id;
                    newNodes[2]._neighbours[Neighbours::TopEdgeLeft] = newNodes[2]._neighbours[Neighbours::TopEdgeRight] = newNodes[0]._id;

                    newNodes[3]._neighbours[Neighbours::LeftEdgeTop] = newNodes[3]._neighbours[Neighbours::LeftEdgeBottom] = newNodes[2]._id;
                    newNodes[3]._neighbours[Neighbours::TopEdgeLeft] = newNodes[3]._neighbours[Neighbours::TopEdgeRight] = newNodes[1]._id;

                        // (external)
                    newNodes[0]._neighbours[Neighbours::TopEdgeLeft] = newNodes[0]._neighbours[Neighbours::TopEdgeRight] = n->_neighbours[Neighbours::TopEdgeLeft];
                    newNodes[0]._neighbours[Neighbours::LeftEdgeTop] = newNodes[0]._neighbours[Neighbours::LeftEdgeBottom] = n->_neighbours[Neighbours::LeftEdgeTop];

                    newNodes[1]._neighbours[Neighbours::TopEdgeLeft] = newNodes[1]._neighbours[Neighbours::TopEdgeRight] = n->_neighbours[Neighbours::TopEdgeRight];
                    newNodes[1]._neighbours[Neighbours::RightEdgeTop] = newNodes[1]._neighbours[Neighbours::RightEdgeBottom] = n->_neighbours[Neighbours::RightEdgeTop];

                    newNodes[2]._neighbours[Neighbours::BottomEdgeLeft] = newNodes[2]._neighbours[Neighbours::BottomEdgeRight] = n->_neighbours[Neighbours::BottomEdgeLeft];
                    newNodes[2]._neighbours[Neighbours::LeftEdgeTop] = newNodes[2]._neighbours[Neighbours::LeftEdgeBottom] = n->_neighbours[Neighbours::LeftEdgeBottom];

                    newNodes[3]._neighbours[Neighbours::RightEdgeTop] = newNodes[3]._neighbours[Neighbours::RightEdgeBottom] = n->_neighbours[Neighbours::RightEdgeBottom];
                    newNodes[3]._neighbours[Neighbours::BottomEdgeLeft] = newNodes[3]._neighbours[Neighbours::BottomEdgeRight] = n->_neighbours[Neighbours::BottomEdgeRight];

                        // commit...
                    for (unsigned c=0; c<dimof(newNodes); ++c) {
                        if (newNodes[c]._id._nodeId != ~unsigned(0x0)) {
                            collapsedField.push_back(newNodes[c]);
                        }
                    }
                }

                    //  Update the neighbour details of attached nodes.
                    //  There's only two possibilities currently 
                    //      -- our neighbour is at the same LOD level we started at
                    //      -- our neighbour is at the same LOD we ended at
                    //  it should be make it simplier.
                Neighbours::Enum mirrorNeighbours[] = 
                {
                    Neighbours::BottomEdgeLeft,         // <--> TopEdgeLeft
                    Neighbours::BottomEdgeRight,        // <--> TopEdgeRight
                    Neighbours::LeftEdgeTop,            // <--> RightEdgeTop
                    Neighbours::LeftEdgeBottom,         // <--> RightEdgeBottom
                    Neighbours::TopEdgeRight,           // <--> BottomEdgeRight
                    Neighbours::TopEdgeLeft,            // <--> BottomEdgeLeft
                    Neighbours::RightEdgeBottom,        // <--> LeftEdgeBottom
                    Neighbours::RightEdgeTop            // <--> LeftEdgeTop
                };
                unsigned attachSubNode[] =  { 0, 1, 1, 3, 3, 2, 2, 0 };
                for (unsigned c=0; c<Neighbours::Count; ++c) {
                    auto* node = FindNeighbour(n->_neighbours[c], collapsedField, startLod+1);
                    if (!node) continue;

                    assert(node->_neighbours[mirrorNeighbours[c]].Equivalent(n->_id));
                    if (node->_id._lodField == startLod) {
                        // neighbour is so far uncollapsed. So we just update as needed
                        node->_neighbours[mirrorNeighbours[c]] = newNodes[attachSubNode[c]]._id;
                    } else {
                        // neighbour is already collapsed. So we need to update 2 neighbour references
                        // there.
                        unsigned n0 = mirrorNeighbours[c] & ~0x1;
                        unsigned n1 = n0 + 1;
                        node->_neighbours[n0] = newNodes[attachSubNode[c]]._id;
                        node->_neighbours[n1] = newNodes[attachSubNode[c]]._id;
                    }
                }
            }

        }

        assert(_activeNodes[startLod+1].empty());
        _activeNodes[startLod+1] = std::move(collapsedField);
    }

    auto TerrainCellRenderer::BuildQueuedNodeFlags(const CellRenderInfo& cellRenderInfo, unsigned nodeIndex, unsigned lodField) const -> unsigned
    {
        typedef TerrainRenderingContext::QueuedNode::Flags Flags;
        Flags::BitField flags = 0;
        auto& heightTile = cellRenderInfo._heightTiles[nodeIndex];
        bool validHeightMap = _heightMapTileSet->IsValid(heightTile._tile);
        flags |= Flags::HasValidData * unsigned(validHeightMap);    // (we can render without valid coverage)

            //  if there's no valid data, and no currently pending data, we need to queue a 
            //  new upload
        if (!validHeightMap && !_heightMapTileSet->IsValid(heightTile._pendingTile))
            flags |= Flags::NeedsHeightMapUpload;

        for (unsigned covIndex=0; covIndex < unsigned(cellRenderInfo._coverage.size()); ++covIndex) {
            auto& sourceCoverage = *cellRenderInfo._coverage[covIndex]._source;
            auto& covTile = cellRenderInfo._coverage[covIndex]._tiles[nodeIndex];

            bool validCoverage = _coverageTileSet[covIndex]->IsValid(covTile._tile);
            if (!validCoverage) {
                    // some tiles don't have any coverage information. 
                flags |= (Flags::NeedsCoverageUpload0<<covIndex) * 
                    unsigned( 
                            lodField < sourceCoverage._fieldCount 
                        &&  sourceCoverage._nodeFileOffsets[nodeIndex] != ~unsigned(0x0)
                        && !_coverageTileSet[covIndex]->IsValid(covTile._pendingTile));
            }
        }

        return flags;
    }

    void TerrainCellRenderer::WriteQueuedNodes(
        TerrainRenderingContext& renderingContext, TerrainCollapseContext& collapseContext)
    {
        // After calculating the correct LOD level and neighbours for each cell, we need to do 2 final things
        //      * queue texture updates
        //      * queue the node for actual rendering

        for (unsigned l=0; l<TerrainCollapseContext::MaxLODLevels; ++l) {
            for (auto n=collapseContext._activeNodes[l].cbegin(); n!=collapseContext._activeNodes[l].cend(); ++n) {

                if (n->_lodPromoted) { continue; }        // collapsed into larger LOD

                auto& cellRenderInfo = *collapseContext._cells[n->_id._cellId];

                auto flags = BuildQueuedNodeFlags(cellRenderInfo, n->_id._nodeId, n->_id._lodField);

                auto& minusViewPosition = collapseContext._cellPositionMinusViewPosition[n->_id._cellId];
                auto& cellToWorld = collapseContext._cellToWorlds[n->_id._cellId];
				auto nodeToCell = cellRenderInfo.NodeToCell(n->_id._nodeId);

                TerrainRenderingContext::QueuedNode queuedNode;
                queuedNode._cell = &cellRenderInfo;
                queuedNode._fieldIndex = n->_id._lodField;
                queuedNode._absNodeIndex = n->_id._nodeId;
                auto worldSpaceMean = Truncate(cellToWorld * (nodeToCell * Float4(0.5f, 0.5f, 0.f, 1.f)));
                queuedNode._priority = MagnitudeSquared(worldSpaceMean + minusViewPosition);
                queuedNode._flags = flags;
                queuedNode._cellToWorld = cellToWorld;    // note -- it's a pity we have to store this for every node (it's a per-cell property)

                    // calculate LOD differentials for neighbouring nodes
                for (unsigned c=0; c<4; ++c) {
                    auto& n0 = n->_neighbours[c*2 + 0];
                    auto& n1 = n->_neighbours[c*2 + 1];
                    signed diff = 0;
                    if (n0._nodeId != ~unsigned(0x0)) {
                        diff = signed(n0._lodField) - signed(n->_id._lodField);
                        if (n1._nodeId != ~unsigned(0x0) && XlAbs(signed(n1._lodField) - signed(n->_id._lodField)) > XlAbs(diff)) {
                            diff = signed(n1._lodField) - signed(n->_id._lodField);
                        }
                    } else if (n1._nodeId != ~unsigned(0x0)) {
                        diff = signed(n1._lodField) - signed(n->_id._lodField);
                    }
                    queuedNode._neighbourLODDiff[c] = (int8)diff;
                }

                renderingContext._queuedNodes.push_back(queuedNode);

            }
        }
    }

    void TerrainCellRenderer::CullNodes(
        TerrainRenderingContext& terrainContext, TerrainCollapseContext& collapseContext,
        const Float4x4& worldToProjection, const Float3& viewPositionWorld, CellRenderInfo& cellRenderInfo, const Float4x4& cellToWorld)
    {
        if (cellRenderInfo._heightTiles.empty()) { return; }
        if (cellRenderInfo._heightMapStreamingFilePtr == INVALID_HANDLE_VALUE)
            return;

        auto& sourceCell = *cellRenderInfo._sourceCell;
        if (collapseContext._startLod >= sourceCell._nodeFields.size())
            return;

        auto cellToProjection = Combine(cellToWorld, worldToProjection);
        auto& field = sourceCell._nodeFields[collapseContext._startLod];

        unsigned cellId = unsigned(collapseContext._cells.size());
        collapseContext._cellToWorlds.push_back(cellToWorld);
        collapseContext._cellToProjection.push_back(cellToProjection);
        collapseContext._cellPositionMinusViewPosition.push_back(-viewPositionWorld);
        collapseContext._cells.push_back(&cellRenderInfo);
        auto f = collapseContext._startLod;

            //              We need to initialize the collapse context with all of the nodes in this LOD            //
            //      Note that we're just going to add in all of the non-culled nodes, first. We'll calculate the
            //      appropriate LOD levels later.

        std::vector<CullTestResult> cullResults;
        std::vector<float> screenSpaceEdgeLengths;
        cullResults.resize(field._nodeEnd - field._nodeBegin);
        screenSpaceEdgeLengths.resize(field._nodeEnd - field._nodeBegin, std::numeric_limits<float>::max());

        const unsigned compressedHeightMask = CompressedHeightMask(terrainContext._encodedGradientFlags);

        for (unsigned n=field._nodeBegin; n<field._nodeEnd; ++n) {
			auto& sourceNode = sourceCell._nodes[n];
			auto nodeToCell = cellRenderInfo.NodeToCell(n);

            const unsigned expectedDataSize = sourceNode->_widthInElements*sourceNode->_widthInElements*2;
            if (sourceNode->_heightMapFileSize < expectedDataSize) {
                    // some nodes have "holes". We have to ignore them.
                cullResults[n - field._nodeBegin] = CullTestResult::Culled;
            } else {
                __declspec(align(16)) auto localToProjection = Combine(nodeToCell, cellToProjection);
                cullResults[n - field._nodeBegin] = TestAABB_Aligned(localToProjection, Float3(0.f, 0.f, 0.f), Float3(1.f, 1.f, float(compressedHeightMask)), RenderCore::Techniques::GetDefaultClipSpaceType());
                if (cullResults[n - field._nodeBegin] != CullTestResult::Culled) {
                    screenSpaceEdgeLengths[n - field._nodeBegin] = CalculateScreenSpaceEdgeLength(
                        localToProjection, terrainContext._currentViewport.Width, terrainContext._currentViewport.Height);
                }
            }
        }

        for (unsigned n=field._nodeBegin; n<field._nodeEnd; ++n) {
            auto cullTest = cullResults[n - field._nodeBegin];
            if (cullTest == CullTestResult::Culled) { continue; }

            typedef TerrainCollapseContext::NodeID NodeID;
            typedef TerrainCollapseContext::Node Node;
            typedef TerrainCollapseContext::Neighbours Neighbours;

            NodeID nid(f, n, cellId);
            Node node(nid);
            node._entirelyWithinFrustum = cullTest == CullTestResult::Within;
            node._screenSpaceEdgeLength = screenSpaceEdgeLengths[n - field._nodeBegin];

                //  We can calculate the neighbours within this cell. But neighbours
                //  in other cells become difficult. At this point, all nodes in the
                //  collapse context are at the same LOD (and so should be the same size).
            
            auto baseXY     = ToFieldXY(field, n-field._nodeBegin);
            auto topEdge    = ToNodeIndex(field, baseXY + Int2( 0, -1));
            auto rightEdge  = ToNodeIndex(field, baseXY + Int2( 1,  0));
            auto bottomEdge = ToNodeIndex(field, baseXY + Int2( 0,  1));
            auto leftEdge   = ToNodeIndex(field, baseXY + Int2(-1,  0));
            if ((topEdge != ~unsigned(0x0)) && (cullResults[topEdge] != CullTestResult::Culled)) {
                node._neighbours[Neighbours::TopEdgeLeft] = NodeID(f, topEdge+field._nodeBegin, cellId);
                node._neighbours[Neighbours::TopEdgeRight] = NodeID(f, topEdge+field._nodeBegin, cellId);
            }
            if ((rightEdge != ~unsigned(0x0)) && (cullResults[rightEdge] != CullTestResult::Culled)) {
                node._neighbours[Neighbours::RightEdgeTop] = NodeID(f, rightEdge+field._nodeBegin, cellId);
                node._neighbours[Neighbours::RightEdgeBottom] = NodeID(f, rightEdge+field._nodeBegin, cellId);
            }
            if ((bottomEdge != ~unsigned(0x0)) && (cullResults[bottomEdge] != CullTestResult::Culled)) {
                node._neighbours[Neighbours::BottomEdgeRight] = NodeID(f, bottomEdge+field._nodeBegin, cellId);
                node._neighbours[Neighbours::BottomEdgeLeft] = NodeID(f, bottomEdge+field._nodeBegin, cellId);
            }
            if ((leftEdge != ~unsigned(0x0)) && (cullResults[leftEdge] != CullTestResult::Culled)) {
                node._neighbours[Neighbours::LeftEdgeBottom] = NodeID(f, leftEdge+field._nodeBegin, cellId);
                node._neighbours[Neighbours::LeftEdgeTop] = NodeID(f, leftEdge+field._nodeBegin, cellId);
            }

            collapseContext.AddNode(node);
        }
    }

    void TerrainCellRenderer::CullNodes( 
        const RenderCore::Techniques::ProjectionDesc& projDesc, TerrainRenderingContext& terrainContext,
        CellRenderInfo& cellRenderInfo, const Float4x4& localToWorld)
    {
        if (cellRenderInfo._heightTiles.empty())
            return;

            // any cells that are missing either the height map or coverage map should just be excluded
        if (cellRenderInfo._heightMapStreamingFilePtr == INVALID_HANDLE_VALUE)
            return;

        auto cellToProjection = Combine(localToWorld, projDesc._worldToProjection);
        Float3 cellPositionMinusViewPosition = ExtractTranslation(localToWorld) - ExtractTranslation(projDesc._cameraToWorld);

        auto& sourceCell = *cellRenderInfo._sourceCell;
        const unsigned startLod = Tweakable("TerrainMinLOD", 1);
        const unsigned maxLod = unsigned(sourceCell._nodeFields.size()-1);      // sometimes the coverage doesn't have all of the LODs. In these cases, we have to clamp the LOD number (for both heights and coverage...!)
        const float screenSpaceEdgeThreshold = Tweakable("TerrainEdgeThreshold", 384.f);
        auto& field2 = sourceCell._nodeFields[startLod];

            // DavidJ -- HACK -- making this "static" to try to avoid extra memory allocations
        static std::stack<std::pair<unsigned, unsigned>> pendingNodes;
        for (unsigned n=0; n<field2._nodeEnd - field2._nodeBegin; ++n)
            pendingNodes.push(std::make_pair(startLod, n));

        const auto compressedHeightMask = CompressedHeightMask(terrainContext._encodedGradientFlags);

        while (!pendingNodes.empty()) {
            auto nodeRef = pendingNodes.top(); pendingNodes.pop();
            auto& field = sourceCell._nodeFields[nodeRef.first];
            unsigned n = field._nodeBegin + nodeRef.second;

			auto& sourceNode = sourceCell._nodes[n];
			auto nodeToCell = cellRenderInfo.NodeToCell(n);

                //  do a culling step first... If the node is completely outside
                //  of the frustum, let's cull it quickly
            const __declspec(align(16)) auto localToProjection = Combine(nodeToCell, cellToProjection);
            if (CullAABB_Aligned(localToProjection, Float3(0.f, 0.f, 0.f), Float3(1.f, 1.f, float(compressedHeightMask)), RenderCore::Techniques::GetDefaultClipSpaceType())) {
                continue;
            }

                //   We can choose to render this node at this lod level, or drop down to
                //   a higher quality lod.
                //   Here is the easiest method for deciding lod:
                //        *   If we assume that every node is 33x33, we can try to balance the
                //            size of the quads in each node in screen-space. Let's project
                //            the corners into screen space and find the screen space edge lengths.
                //   Ideally we should take into account curvature of the surface, as well
                //    -- eg: by looking at average screen space error.

            if (nodeRef.first < maxLod) {
                float screenSpaceEdgeLength = CalculateScreenSpaceEdgeLength(
                    localToProjection, terrainContext._currentViewport.Width, terrainContext._currentViewport.Height);
                if (screenSpaceEdgeLength >= screenSpaceEdgeThreshold) {
                        // drop to lower lod... push in the nodes from the lower lod
                    unsigned fieldx = nodeRef.second % field._widthInNodes;
                    unsigned fieldy = nodeRef.second / field._widthInNodes;
                    pendingNodes.push(std::make_pair(nodeRef.first+1, (fieldy*2+0) * (field._widthInNodes*2) + (fieldx*2+0)));
                    pendingNodes.push(std::make_pair(nodeRef.first+1, (fieldy*2+0) * (field._widthInNodes*2) + (fieldx*2+1)));
                    pendingNodes.push(std::make_pair(nodeRef.first+1, (fieldy*2+1) * (field._widthInNodes*2) + (fieldx*2+0)));
                    pendingNodes.push(std::make_pair(nodeRef.first+1, (fieldy*2+1) * (field._widthInNodes*2) + (fieldx*2+1)));
                    continue;
                }
            }
            
            const unsigned expectedDataSize = sourceNode->_widthInElements*sourceNode->_widthInElements*2;
            if (sourceNode->_heightMapFileSize < expectedDataSize)
                continue;   // some nodes have "holes". We have to ignore them.

                //  we should check for valid data & required uploads. Mark the flags now, and we'll 
                //  do the processing later.
            auto flags = BuildQueuedNodeFlags(cellRenderInfo, n, nodeRef.first);

            TerrainRenderingContext::QueuedNode queuedNode;
            queuedNode._cell = &cellRenderInfo;
            queuedNode._fieldIndex = nodeRef.first;
            queuedNode._absNodeIndex = n;
            queuedNode._priority = MagnitudeSquared(ExtractTranslation(nodeToCell) + cellPositionMinusViewPosition);
            queuedNode._flags = flags;
            queuedNode._cellToWorld = localToWorld;    // note -- it's a pity we have to store this for every node (it's a per-cell property)
            queuedNode._neighbourLODDiff[0] = queuedNode._neighbourLODDiff[1] = 
                queuedNode._neighbourLODDiff[2] = queuedNode._neighbourLODDiff[3] = 0;
            terrainContext._queuedNodes.push_back(queuedNode);
        }
    }

    void TerrainCellRenderer::Render(    
        DeviceContext& context, RenderCore::Techniques::ParsingContext& parserContext, 
        TerrainRenderingContext& terrainContext)
    {
        context.GetNumericUniforms(ShaderStage::Vertex).Bind(MakeResourceList(_heightMapTileSet->GetShaderResource()));
        context.GetNumericUniforms(ShaderStage::Domain).Bind(MakeResourceList(_heightMapTileSet->GetShaderResource()));
        for (unsigned c=0; c<unsigned(_coverageTileSet.size()); ++c) {
            context.GetNumericUniforms(ShaderStage::Pixel).Bind(MakeResourceList(c+1, _coverageTileSet[c]->GetShaderResource()));
                //  for instance spawn mode, we also need the coverage resources in the geometry shader. Perhaps
                //  we could use techniques to make this a little more reliable...?
            context.GetNumericUniforms(ShaderStage::Geometry).Bind(MakeResourceList(c+1, _coverageTileSet[c]->GetShaderResource()));
        }

            // heights required on the pixel shader only for prototype texturing...
        context.GetNumericUniforms(ShaderStage::Pixel).Bind(MakeResourceList(_heightMapTileSet->GetShaderResource()));

            // go through all nodes that were previously queued (with CullNodes) and render them
        CATCH_ASSETS_BEGIN
            CellRenderInfo* currentCell = nullptr;
            for (auto i=terrainContext._queuedNodes.begin(); i!=terrainContext._queuedNodes.end(); ++i) {
                bool hasData = _heightMapTileSet->IsValid(i->_cell->_heightTiles[i->_absNodeIndex]._tile);
                if (hasData) { // i->_flags & TerrainRenderingContext::QueuedNode::Flags::HasValidData) {

                        // we only have to upload the local transform when the current cell changes
                        // note that the current culling process is reordering queued nodes by
                        // lod level. So, when a cell is split across multiple LOD levels, we may
                        // switch to a given cell more than once (resulting in multiple uploads of
                        // it's local transform)
                    if (i->_cell != currentCell) {
                        auto localTransform = Techniques::MakeLocalTransform(i->_cellToWorld, Float3(0,0,0));
                        terrainContext._localTransformConstantsBuffer.Update(context, &localTransform, sizeof(localTransform));
                        currentCell = i->_cell;
                    }

                    #if defined(_DEBUG)
                            // if the cell source data format doesn't match the format expected by the current
                            // rendering context, then we can't render this cell. We use the same shaders for
                            // all cells, so all cells must agree on those settings that affect shader selection.
                        if (i->_cell->_sourceCell->EncodedGradientFlags() != terrainContext._encodedGradientFlags) {
                            Log(Warning) << "Encoded gradient flags setting in cell doesn't match renderer. Rebuild cells so that the renderer settings match the cell data." << std::endl;
                            continue;
                        }
                    #endif

                    RenderNode(context, parserContext, terrainContext, *i->_cell, i->_absNodeIndex, i->_neighbourLODDiff);
                }
            }
        CATCH_ASSETS_END(parserContext)

		MetalStubs::UnbindVS<Metal::ShaderResourceView>(context, 0, 1);
		MetalStubs::UnbindDS<Metal::ShaderResourceView>(context, 0, 1);
    }

    void TerrainCellRenderer::RenderNode(    
        DeviceContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        TerrainRenderingContext& terrainContext,
        CellRenderInfo& cellRenderInfo, unsigned absNodeIndex,
        int8 neighbourLodDiffs[4])
    {
        auto& heightTile = cellRenderInfo._heightTiles[absNodeIndex]._tile;

        /////////////////////////////////////////////////////////////////////////////
            //  if we've got some texture data, we can go ahead and
            //  render this object
        assert(heightTile._width && heightTile._height && heightTile._width != ~0x0u && heightTile._height != ~0x0u);

        TileConstants tileConstants;
        XlSetMemory(&tileConstants, 0, sizeof(tileConstants));
        tileConstants._localToCell = cellRenderInfo.NodeToCell(absNodeIndex);
        tileConstants._heightMapOrigin = Int3(heightTile._x, heightTile._y, heightTile._arrayIndex);

        for (unsigned covIndex=0; covIndex<cellRenderInfo._coverage.size(); ++covIndex) {
            const auto& covTile = cellRenderInfo._coverage[covIndex]._tiles[absNodeIndex]._tile;
            if (covTile._width == ~unsigned(0x0) || covTile._height == ~unsigned(0x0)) {
                // LogWarning << "Can't queue coverage tile (seems like upload didn't complete)";
                continue;
            }
            
            const unsigned overlap = 1;
            tileConstants._coverageCoordMins[covIndex][1]  = (float)covTile._y;
            tileConstants._coverageCoordMins[covIndex][0]  = (float)covTile._x;
            tileConstants._coverageCoordMaxs[covIndex][0]  = (float)(covTile._x + (covTile._width-overlap));
            tileConstants._coverageCoordMaxs[covIndex][1]  = (float)(covTile._y + (covTile._height-overlap));
            tileConstants._coverageOrigin[covIndex] = Int4(covTile._x, covTile._y, covTile._arrayIndex, 0);
        }

        tileConstants._tileDimensionsInVertices = GetHeightsElementSize()[1];
        tileConstants._neighbourLodDiffs = Int4(neighbourLodDiffs[0], neighbourLodDiffs[1], neighbourLodDiffs[2], neighbourLodDiffs[3]);
        terrainContext._tileConstantsBuffer.Update(context, &tileConstants, sizeof(tileConstants));

        /////////////////////////////////////////////////////////////////////////////
            //  If we're using dynamic tessellation, then we have a vertex generator shader with no index buffer,
            //  otherwise we're still using a vertex generator shader, but we are using an index buffer to 
            //  order the triangles.
        if (terrainContext._dynamicTessellation) {
            context.Draw(4);
        } else {
            context.DrawIndexed(terrainContext._indexDrawCount);
        }
    }

    //////////////////////////////////////////////////////////////////////////////////////////

    TerrainCellRenderer::TerrainCellRenderer(
        const TerrainRendererConfig& cfg,
        std::shared_ptr<ITerrainFormat> ioFormat,
        bool allowShortCircuitModification)
    : _cfg(cfg)
    , _shortCircuitAllowed(allowShortCircuitModification)
    {
        auto& bufferUploads = GetBufferUploads();
        _heightMapTileSet = std::make_unique<TextureTileSet>(
            bufferUploads, cfg._heights._tileSize, cfg._heights._cachedTileCount, cfg._heights._format, allowShortCircuitModification);

        _coverageTileSet.reserve(unsigned(cfg._coverageLayers.size()));
        _coverageIds.reserve(unsigned(cfg._coverageLayers.size()));

        for (unsigned c=0; c<unsigned(cfg._coverageLayers.size()); ++c) {
            const auto& layer = cfg._coverageLayers[c];
            _coverageTileSet.push_back(std::make_unique<TextureTileSet>(
                bufferUploads, layer.second._tileSize, layer.second._cachedTileCount, layer.second._format, allowShortCircuitModification));
            _coverageIds.push_back(layer.first);
            _coverageFmts.push_back(layer.second._format);
        }

        _renderInfos.reserve(64);
        _ioFormat = std::move(ioFormat);
    }

    TerrainCellRenderer::~TerrainCellRenderer()
    {
        CompletePendingUploads();
            // note;    there's no protection to make sure these get completed. If we want to release
            //          the upload transaction, we should complete them all
        assert(_pendingUploads.empty());
    }

    void TerrainCellRenderer::UnloadCachedData()
    {
        // we want to clear out the "_renderInfos" array
        //  --  so we can release handles to source data files
        //      and avoid dangling pointers to cells that are about
        //      to change.
        // First we have to complete any pending uploads!
        CompletePendingUploads();
        _renderInfos.clear();
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    bool TerrainCellRenderer::CellRenderInfo::CompleteUpload(uint32 uploadId, BufferUploads::IManager& bufferUploads)
    {
        if (uploadId & (1u<<31u)) {
            bool result = true;
            for (auto i=_coverage.begin(); i!=_coverage.end(); ++i)
                result &= i->_tiles[uploadId & ~(1u<<31u)].CompleteUpload(bufferUploads);
            return result;
        } else {
            return _heightTiles[uploadId].CompleteUpload(bufferUploads);
        }
    }

    TerrainCellRenderer::CellRenderInfo::CellRenderInfo(
        const TerrainCell& cell, 
        const TerrainCellTexture* const* cellCoverageBegin, const TerrainCellTexture* const* cellCoverageEnd)
    {
            //  we need to create a "NodeRenderInfo" for each node.
            //  this will keep track of texture uploads, etc
        size_t nodeCount = cell._nodes.size();
        _sourceCell = nullptr;
        _heightMapStreamingFilePtr = INVALID_HANDLE_VALUE;

        if (nodeCount && !cell.SourceFile().empty()) {
            std::vector<NodeCoverageInfo> heightTiles;
            heightTiles.resize(nodeCount);

			#if defined(TERRAIN_ENABLE_EDITING)
				for (unsigned c=0; c<nodeCount; ++c) {
					heightTiles[c]._heightScale = cell._nodes[c]->_localToCell(2,2);
					heightTiles[c]._heightOffset = cell._nodes[c]->_localToCell(2,3);
				}
			#endif

                //  we also need to open a file for streaming access. 
                //  we should open this file here, and keep it open permanently.
                //  we don't want to have to open and close it for each streaming
                //  operation.
                //  No wrapped asynchronous file io yet. We have to use the win32 api
                //  directly. Using overlapped io is best for this task. Mapped files
                //  might be better in 64 bit.. But it depends on the size of the terrain
                //  files.
                //
                //      Maybe set "FILE_FLAG_NO_BUFFERING" ?
            auto heightMapFileHandle = ::CreateFile(
                cell.SourceFile().c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED|FILE_FLAG_RANDOM_ACCESS, nullptr);
            if (heightMapFileHandle == INVALID_HANDLE_VALUE)
                Throw(::Exceptions::BasicLabel("Failed opening terrain height-map file for streaming"));

            std::vector<CoverageLayer> coverage;
            for (auto q=cellCoverageBegin; q<cellCoverageEnd; ++q) {
                if (!*q) continue;

                CoverageLayer layer;
                layer._source = *q;
                layer._streamingFilePtr = INVALID_HANDLE_VALUE;
                layer._tiles.resize(layer._source->_nodeFileOffsets.size());

                layer._streamingFilePtr = ::CreateFile(
                    (*q)->SourceFile().c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED|FILE_FLAG_RANDOM_ACCESS, nullptr);
                assert(layer._streamingFilePtr != INVALID_HANDLE_VALUE);

                coverage.emplace_back(std::move(layer));
            }

            _heightTiles = std::move(heightTiles);
            _sourceCell = &cell;
            _heightMapStreamingFilePtr = heightMapFileHandle;
            _coverage = std::move(coverage);
        }
    }

	#if defined(TERRAIN_ENABLE_EDITING)
		Float4x4 TerrainCellRenderer::CellRenderInfo::NodeToCell(unsigned nodeId) const
		{
			// note --	When terrain editing is enabled, we sometimes override the some
			//			elements of the node-to-cell transform (because the height range can change).
			//			This should only happen in the editor, however
			assert(_sourceCell && nodeId < _sourceCell->_nodes.size() && nodeId < _heightTiles.size());
			auto base = _sourceCell->_nodes[nodeId]->_localToCell;
			const auto& h = _heightTiles[nodeId];
			base(2,2) = h._heightScale;
			base(2,3) = h._heightOffset;
			return base;
		}
	#else
		const Float4x4& TerrainCellRenderer::CellRenderInfo::NodeToCell(unsigned nodeId) const
		{
			assert(_sourceCell && nodeId < _sourceCell->_nodes.size() && nodeId < _heightTiles.size());
			return _sourceCell->_nodes[nodeId]->_localToCell;
		}
	#endif

    TerrainCellRenderer::CellRenderInfo::CellRenderInfo(CellRenderInfo&& moveFrom) never_throws
	:	_heightTiles(std::move(moveFrom._heightTiles))
	,	_sourceCell(std::move(moveFrom._sourceCell))
	,	_heightMapStreamingFilePtr(std::move(moveFrom._heightMapStreamingFilePtr))
	,	_coverage(std::move(moveFrom._coverage))
    {
        moveFrom._heightMapStreamingFilePtr = INVALID_HANDLE_VALUE;
    }

    auto TerrainCellRenderer::CellRenderInfo::operator=(CellRenderInfo&& moveFrom) never_throws -> CellRenderInfo&
    {
        _heightTiles = std::move(moveFrom._heightTiles);
        _sourceCell = std::move(moveFrom._sourceCell);
        _heightMapStreamingFilePtr = std::move(moveFrom._heightMapStreamingFilePtr);
        _coverage = std::move(moveFrom._coverage);
        moveFrom._heightMapStreamingFilePtr = INVALID_HANDLE_VALUE;
        return *this;
    }


    TerrainCellRenderer::CellRenderInfo::~CellRenderInfo()
    {
            //  we need to cancel any buffer uploads transactions that are still active
            //      -- note they may still complete in a background thread
        auto& bufferUploads = GetBufferUploads();
        for (auto i = _heightTiles.begin(); i!=_heightTiles.end(); ++i)
            i->EndTransactions(bufferUploads);

        for (auto i = _coverage.begin(); i!=_coverage.end(); ++i)
            for (auto t = i->_tiles.begin(); t!=i->_tiles.end(); ++t)
                t->EndTransactions(bufferUploads);

        if (_heightMapStreamingFilePtr && _heightMapStreamingFilePtr!=INVALID_HANDLE_VALUE)
            CloseHandle((HANDLE)_heightMapStreamingFilePtr);
        
        for (auto i = _coverage.begin(); i!=_coverage.end(); ++i)
            if (i->_streamingFilePtr && i->_streamingFilePtr!=INVALID_HANDLE_VALUE)
                CloseHandle((HANDLE)i->_streamingFilePtr);
    }

	TerrainCellRenderer::CellRenderInfo::CoverageLayer::CoverageLayer() { _streamingFilePtr = nullptr; _source = nullptr; }
	TerrainCellRenderer::CellRenderInfo::CoverageLayer::~CoverageLayer() {}
	TerrainCellRenderer::CellRenderInfo::CoverageLayer::CoverageLayer(CoverageLayer&& moveFrom) never_throws
	: _source(moveFrom._source)
	, _streamingFilePtr(moveFrom._streamingFilePtr)
	, _tiles(std::move(moveFrom._tiles))
	{}

	auto TerrainCellRenderer::CellRenderInfo::CoverageLayer::operator=(CoverageLayer&& moveFrom) never_throws -> CoverageLayer&
	{
		_source = moveFrom._source; moveFrom._source = nullptr;
		_streamingFilePtr = moveFrom._streamingFilePtr; moveFrom._streamingFilePtr = nullptr;
		_tiles = std::move(moveFrom._tiles);
		return *this;
	}

    static bool IsCompatible(const TerrainRendererConfig::Layer& lhs, const TerrainRendererConfig::Layer& rhs)
    {
        return (lhs._tileSize == rhs._tileSize) 
            && (lhs._cachedTileCount == rhs._cachedTileCount) 
            && (lhs._format == rhs._format);
    }

    bool IsCompatible(const TerrainRendererConfig& lhs, const TerrainRendererConfig& rhs)
    {
        if (lhs._coverageLayers.size() != rhs._coverageLayers.size()) return false;
        if (!IsCompatible(lhs._heights, rhs._heights)) return false;
        for (unsigned c=0; c<lhs._coverageLayers.size(); ++c)
            if (    lhs._coverageLayers[c].first != rhs._coverageLayers[c].first
                || !IsCompatible(lhs._coverageLayers[c].second, rhs._coverageLayers[c].second)) return false;
        return true;
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    bool TerrainCellRenderer::NodeCoverageInfo::CompleteUpload(BufferUploads::IManager& bufferUploads)
    {
        if (_pendingTile._transaction != ~BufferUploads::TransactionID(0x0)) {
            if (!bufferUploads.IsCompleted(_pendingTile._transaction))
                return false;

            bufferUploads.Transaction_End(_pendingTile._transaction);
            _pendingTile._transaction = ~BufferUploads::TransactionID(0x0);
            _tile = std::move(_pendingTile);
        }
        return true;
    }

    void TerrainCellRenderer::NodeCoverageInfo::Queue(
        TextureTileSet& coverageTileSet,
        const void* filePtr, unsigned fileOffset, unsigned fileSize)
    {
            // the caller should check to see if we need an upload before calling this
        assert(!coverageTileSet.IsValid(_tile));
        assert(!coverageTileSet.IsValid(_pendingTile));
        coverageTileSet.Transaction_Begin(_pendingTile, filePtr, fileOffset, fileSize);
    }

    void TerrainCellRenderer::NodeCoverageInfo::EndTransactions(BufferUploads::IManager& bufferUploads)
    {
            //  note that when we complete the transaction like this, we might
            //  leave the tile in an invalid state, because it may still point
            //  to some allocated space in the tile set. In other words, the 
            //  destination area in the tile set is not deallocated during when
            //  the transaction ends.
        assert(_tile._transaction == ~BufferUploads::TransactionID(0x0));		// (there should never be a transaction on the front buffer tile)
        if (_pendingTile._transaction != ~BufferUploads::TransactionID(0x0)) {
            bufferUploads.Transaction_End(_pendingTile._transaction);
            _pendingTile._transaction = ~BufferUploads::TransactionID(0x0);
        }
    }

    TerrainCellRenderer::NodeCoverageInfo::NodeCoverageInfo()
    {
		#if defined(TERRAIN_ENABLE_EDITING)
			_heightScale = _heightOffset = 0.f;
		#endif
	}

    TerrainCellRenderer::NodeCoverageInfo::NodeCoverageInfo(NodeCoverageInfo&& moveFrom) never_throws
	: _tile(std::move(moveFrom._tile))
	, _pendingTile(std::move(moveFrom._pendingTile))
    {
		#if defined(TERRAIN_ENABLE_EDITING)
			_heightScale = moveFrom._heightScale;
			_heightOffset = moveFrom._heightOffset;
		#endif
	}

    auto TerrainCellRenderer::NodeCoverageInfo::operator=(NodeCoverageInfo&& moveFrom) never_throws -> NodeCoverageInfo&
    {
        _tile = std::move(moveFrom._tile);
        _pendingTile = std::move(moveFrom._pendingTile);
		#if defined(TERRAIN_ENABLE_EDITING)
			_heightScale = moveFrom._heightScale;
			_heightOffset = moveFrom._heightOffset;
		#endif
        return *this;
    }

}


