// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IntersectionTest.h"
#include "RayVsModel.h"
#include "PlacementsManager.h"

#include "../RenderCore/RenderUtils.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/Drawables.h"
#include "../RenderCore/Techniques/Apparatuses.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"

#include "../Math/Transformations.h"
#include "../Math/Vector.h"
#include "../Math/ProjectionMath.h"
#include "../Utility/BitUtils.h"

#pragma warning(disable:4505)

using namespace Utility::Literals;

namespace SceneEngine
{

    /// <summary>Resolves ray and box intersections for tools</summary>
    /// This object can calculate intersections of basic primitives against
    /// the scene. This is intended for tools to perform interactive operations
    /// (like selecting objects in the scene).
    /// Note that much of the intersection math is performed on the GPU. This means
    /// that any intersection operation will probably involve a GPU synchronisation.
    /// This isn't intended to be used at runtime in a game, because it may cause
    /// frame-rate hitches. But for tools, it should not be an issue.
    class IntersectionTestScene : public IIntersectionScene
    {
    public:
        IntersectionTestResult FirstRayIntersection(
            const IntersectionTestContext& context,
            std::pair<Float3, Float3> worldSpaceRay,
            IntersectionTestResult::Type::BitField filter = ~IntersectionTestResult::Type::BitField(0)) const override;

        void FrustumIntersection(
            std::vector<IntersectionTestResult>& result,
            const IntersectionTestContext& context,
            const Float4x4& worldToProjection,
            IntersectionTestResult::Type::BitField filter = ~IntersectionTestResult::Type::BitField(0)) const override;

        const std::shared_ptr<TerrainManager>& GetTerrain() const { return _terrainManager; }

        IntersectionTestScene(
            std::shared_ptr<TerrainManager> terrainManager = nullptr,
            std::shared_ptr<PlacementsEditor> placementsEditor = nullptr,
            IteratorRange<const std::shared_ptr<SceneEngine::IIntersectionScene>*> extraTesters = {});
        ~IntersectionTestScene();
    protected:
        std::shared_ptr<TerrainManager> _terrainManager;
        std::shared_ptr<PlacementsEditor> _placementsEditor;
        std::vector<std::shared_ptr<IIntersectionScene>> _extraTesters;
    };

////////////////////////////////////////////////////////////////////////////////////////////////////////

    static std::pair<Float3, bool> FindTerrainIntersection(
        RenderCore::IThreadContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        TerrainManager& terrainManager,
        std::pair<Float3, Float3> worldSpaceRay)
    {
#if 0
        CATCH_ASSETS_BEGIN
            TerrainManager::IntersectionResult intersections[8];
            unsigned intersectionCount = terrainManager.CalculateIntersections(
                intersections, dimof(intersections), worldSpaceRay, context, parserContext);

            if (intersectionCount > 0)
                return std::make_pair(intersections[0]._intersectionPoint, true);
        CATCH_ASSETS_END(parserContext)
#endif
        return std::make_pair(Float3(0,0,0), false);
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////

    static std::pair<Float3, bool> FindTerrainIntersection(
        const IntersectionTestContext& intersectionContext,
		RenderCore::Techniques::ParsingContext& parsingContext,
        TerrainManager& terrainManager,
        std::pair<Float3, Float3> worldSpaceRay)
    {
#if 0
            //  create a new device context and lighting parser context, and use
            //  this to find an accurate terrain collision.
        RenderCore::ViewportDesc newViewport {
            float(intersectionContext._viewportMins[0]), float(intersectionContext._viewportMins[1]),
			float(intersectionContext._viewportMaxs[0]-intersectionContext._viewportMins[0]),
			float(intersectionContext._viewportMaxs[1]-intersectionContext._viewportMins[1]),
			0.f, 1.f };
		auto& threadContext = *RenderCore::Techniques::GetThreadContext();
        RenderCore::Metal::DeviceContext::Get(threadContext)->Bind(newViewport);
		// We need to set the camera to get correct culling and lodding of the terrain during intersection tests
		parsingContext.GetProjectionDesc() = RenderCore::Techniques::BuildProjectionDesc(
			intersectionContext._cameraDesc,
			UInt2{unsigned(intersectionContext._viewportMaxs[0]-intersectionContext._viewportMins[0]), unsigned(intersectionContext._viewportMaxs[1]-intersectionContext._viewportMins[1])});
		return FindTerrainIntersection(threadContext, parsingContext, terrainManager, worldSpaceRay);
#endif
        return std::make_pair(Float3(0,0,0), false);
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////

    RenderCore::Techniques::TechniqueContext MakeIntersectionsTechniqueContext(RenderCore::Techniques::DrawingApparatus& drawingApparatus)
    {
        RenderCore::Techniques::TechniqueContext techniqueContext;
        RenderCore::Techniques::InitializeTechniqueContext(techniqueContext, drawingApparatus);
        return techniqueContext;
    }

    std::optional<IntersectionTestResult> FirstRayIntersection(
        RenderCore::Techniques::ParsingContext& parsingContext,
        PlacementsEditor& placementsEditor,
        std::pair<Float3, Float3> worldSpaceRay,
        const RenderCore::Techniques::CameraDesc* cameraForLOD)
    {
        std::optional<IntersectionTestResult> result;

        auto intersections = placementsEditor.GetManager()->GetIntersections();
        auto roughIntersection = 
            intersections->Find_RayIntersection(placementsEditor.GetCellSet(), worldSpaceRay.first, worldSpaceRay.second, nullptr);

        if (!roughIntersection.empty()) {

                // we can improve the intersection by doing ray-vs-triangle tests
                // on the roughIntersection geometry

                //  we need to create a temporary transaction to get
                //  at the information for these objects.
            auto trans = placementsEditor.Transaction_Begin(
                AsPointer(roughIntersection.cbegin()), AsPointer(roughIntersection.cend()));

            TRY
            {
                using namespace RenderCore;
                Techniques::DrawablesPacket pkt[(unsigned)Techniques::Batch::Max];
                Techniques::DrawablesPacket* pktPtr[(unsigned)Techniques::Batch::Max];
                for (unsigned c=0; c<(unsigned)Techniques::Batch::Max; ++c) pktPtr[c] = &pkt[c];
                ExecuteSceneContext sceneExeContext;
                sceneExeContext._views = {&parsingContext.GetProjectionDesc(), &parsingContext.GetProjectionDesc()+1};
                sceneExeContext._destinationPkts = {pktPtr, &pktPtr[(unsigned)Techniques::Batch::Max]};

                auto& renderer = *placementsEditor.GetManager()->GetRenderer();
                auto count = trans->GetObjectCount();
                VLA_UNSAFE_FORCE(PlacementGUID, guids, count);
                for (unsigned c=0; c<count; ++c)
                    guids[c] = trans->GetGuid(c);

                renderer.BuildDrawablesSingleView(
                    sceneExeContext,
                    placementsEditor.GetCellSet(), guids, &guids[count]);

                std::vector<ModelIntersectionStateContext::ResultEntry> modelIntersectionResults;
                {
                    ModelIntersectionStateContext intersectionContext {
                        ModelIntersectionStateContext::RayTest,
                        parsingContext.GetThreadContext(), parsingContext.GetTechniqueContext()._pipelineAccelerators,
                        parsingContext.GetPipelineAcceleratorsVisibility() };
                    intersectionContext.SetRay(worldSpaceRay);
                    parsingContext.RequireCommandList(sceneExeContext._completionCmdList);
                    for (unsigned c=0; c<(unsigned)Techniques::Batch::Max; ++c)
                        intersectionContext.ExecuteDrawables(parsingContext, pkt[c], c, cameraForLOD);
                    modelIntersectionResults = intersectionContext.GetResults();
                }

                // we only select the first intersection result (which is the closest)
                if (!modelIntersectionResults.empty()) {
                    result = IntersectionTestResult {};
                    result->_type = IntersectionTestResult::Type::Placement;
                    const float rayLength = Magnitude(worldSpaceRay.second - worldSpaceRay.first);
                    result->_worldSpaceIntersectionPt = LinearInterpolate(
                        worldSpaceRay.first, worldSpaceRay.second, 
                        modelIntersectionResults[0]._intersectionDepth / rayLength);
                    result->_worldSpaceIntersectionNormal = modelIntersectionResults[0]._normal;
                    result->_distance = modelIntersectionResults[0]._intersectionDepth;

                    DrawableMetadataLookupContext lookupContext {
                        MakeIteratorRange(&modelIntersectionResults[0]._drawableIndex, &modelIntersectionResults[0]._drawableIndex+1),
                        modelIntersectionResults[0]._packetIndex };
                    renderer.LookupDrawableMetadata(
                        lookupContext, sceneExeContext,
                        placementsEditor.GetCellSet(), guids, &guids[count]);

                    assert(!lookupContext.GetProviders().empty());
                    result->_metadataQuery = std::move(lookupContext.GetProviders()[0]);
                }
            }
            CATCH(const ::Assets::Exceptions::RetrievalError&) {}
            CATCH(const std::exception&) {}        // can sometimes through runtime_errors on pending assets
            CATCH_END

            trans->Cancel();
        }
        return result;
    }

    auto IntersectionTestScene::FirstRayIntersection(
        const IntersectionTestContext& context,
        std::pair<Float3, Float3> worldSpaceRay,
        IntersectionTestResult::Type::BitField filter) const -> IntersectionTestResult
    {
        IntersectionTestResult result;
        using Type = IntersectionTestResult::Type;

        auto* drawingApparatus = context.GetService<RenderCore::Techniques::DrawingApparatus>();
        if (!drawingApparatus) return {};

		auto threadContext = RenderCore::Techniques::GetThreadContext();
        auto techniqueContext = MakeIntersectionsTechniqueContext(*drawingApparatus);
		RenderCore::Techniques::ParsingContext parsingContext{techniqueContext, *threadContext};
        parsingContext.SetPipelineAcceleratorsVisibility(techniqueContext._pipelineAccelerators->VisibilityBarrier());
        auto viewportDims = context._viewportMaxs - context._viewportMins;
        parsingContext.GetProjectionDesc() = RenderCore::Techniques::BuildProjectionDesc(context._cameraDesc, viewportDims[0] / float(viewportDims[1]));

        if ((filter & Type::Terrain) && _terrainManager) {
            auto intersection = FindTerrainIntersection(
                context, parsingContext, *_terrainManager.get(), worldSpaceRay);
            if (intersection.second) {
                float distance = Magnitude(intersection.first - worldSpaceRay.first);
                if (distance < result._distance) {
                    result = IntersectionTestResult{};
                    result._type = Type::Terrain;
                    result._worldSpaceIntersectionPt = intersection.first;
                    result._worldSpaceIntersectionNormal = {0,0,0};
                    result._distance = distance;
                }
            }
        }

        if ((filter & Type::Placement) && _placementsEditor) {
            auto placementsIntersection = SceneEngine::FirstRayIntersection(
                parsingContext, *_placementsEditor, worldSpaceRay, &context._cameraDesc);
            if (placementsIntersection.has_value() && placementsIntersection->_distance < result._distance)
                result = *placementsIntersection;
        }

        unsigned firstExtraBit = IntegerLog2(uint32(Type::Extra));
        for (size_t c=0; c<_extraTesters.size(); ++c) {
            if (!(filter & 1<<uint32(c+firstExtraBit))) continue;
            TRY
            {
                auto res = _extraTesters[c]->FirstRayIntersection(context, worldSpaceRay);
                if (res._distance >= 0.f && res._distance <result._distance) {
                    result = res;
                    result._type = (Type::Enum)(1<<uint32(c+firstExtraBit));
                }
            } 
            CATCH(const ::Assets::Exceptions::RetrievalError&) {}
            CATCH(const std::exception&) {}        // can sometimes through runtime_errors on pending assets
            CATCH_END
        }

        return result;
    }

    void IntersectionTestScene::FrustumIntersection(
        std::vector<IntersectionTestResult>& result,
        const IntersectionTestContext& context,
        const Float4x4& worldToProjection,
        IntersectionTestResult::Type::BitField filter) const
    {
        using Type = IntersectionTestResult::Type;

        auto threadContext = RenderCore::Techniques::GetThreadContext();

        if ((filter & Type::Placement) && _placementsEditor) {
            auto intersections = _placementsEditor->GetManager()->GetIntersections();
            auto roughIntersection = 
                intersections->Find_FrustumIntersection(_placementsEditor->GetCellSet(), worldToProjection, nullptr);

                // we can improve the intersection by doing ray-vs-triangle tests
                // on the roughIntersection geometry

            auto drawingApparatus = context.GetService<RenderCore::Techniques::DrawingApparatus>();
            if (!roughIntersection.empty() && drawingApparatus) {
                    //  we need to create a temporary transaction to get
                    //  at the information for these objects.
                auto trans = _placementsEditor->Transaction_Begin(
                    AsPointer(roughIntersection.cbegin()), AsPointer(roughIntersection.cend()));

                TRY
                {
                    auto techniqueContext = MakeIntersectionsTechniqueContext(*drawingApparatus);
					RenderCore::Techniques::ParsingContext parsingContext{techniqueContext, *threadContext};
                    parsingContext.SetPipelineAcceleratorsVisibility(techniqueContext._pipelineAccelerators->VisibilityBarrier());

                    using namespace RenderCore;
                    Techniques::DrawablesPacket pkt[(unsigned)Techniques::Batch::Max];
                    Techniques::DrawablesPacket* pktPtr[(unsigned)Techniques::Batch::Max];
                    for (unsigned c=0; c<(unsigned)Techniques::Batch::Max; ++c) pktPtr[c] = &pkt[c];
                    ExecuteSceneContext sceneExeContext;
                    sceneExeContext._views = {&parsingContext.GetProjectionDesc(), &parsingContext.GetProjectionDesc()+1};
                    sceneExeContext._destinationPkts = {pktPtr, &pktPtr[(unsigned)Techniques::Batch::Max]};

                    auto& renderer = *_placementsEditor->GetManager()->GetRenderer();
                    auto count = trans->GetObjectCount();
                    VLA_UNSAFE_FORCE(PlacementGUID, triangleBaseTestGuids, count);
                    unsigned triangleBasedTestCount = 0;
                    for (unsigned c=0; c<count; ++c) {
                            //  We only need to test the triangles if the bounding box is 
                            //  intersecting the edge of the frustum... If the entire bounding
                            //  box is within the frustum, then we must have a hit
                        auto boundary = trans->GetLocalBoundingBox(c);
                        auto boundaryTest = TestAABB(
                            Combine(trans->GetObject(c)._localToWorld, worldToProjection),
                            boundary.first, boundary.second, RenderCore::Techniques::GetDefaultClipSpaceType());
                        if (boundaryTest == CullTestResult::Culled) {
                            continue; // (could happen because earlier tests were on the world space bounding box)
                        } else if (boundaryTest == CullTestResult::Within) {
                            IntersectionTestResult r;
                            r._type = Type::Placement;
                            r._worldSpaceIntersectionPt = r._worldSpaceIntersectionNormal = {0,0,0};
                            r._distance = 0.f;
                            r._metadataQuery = [guid=trans->GetGuid(c)](uint64_t semantic) -> std::any {
                                switch (semantic) {
                                case "PlacementGUID"_h: return guid;
                                default: return {};
                                }
                            };
                            result.push_back(r);
                        } else {
                            triangleBaseTestGuids[triangleBasedTestCount++] = trans->GetGuid(c);
                        }
                    }

                    if (triangleBasedTestCount) {
                        renderer.BuildDrawablesSingleView(
                            sceneExeContext,
                            _placementsEditor->GetCellSet(), triangleBaseTestGuids, &triangleBaseTestGuids[triangleBasedTestCount]);

                        std::vector<ModelIntersectionStateContext::ResultEntry> modelIntersectionResults;

                        {
                            ModelIntersectionStateContext intersectionContext{
                                ModelIntersectionStateContext::FrustumTest,
                                *threadContext, drawingApparatus->_pipelineAccelerators,
                                parsingContext.GetPipelineAcceleratorsVisibility()};
                            intersectionContext.SetFrustum(worldToProjection);
                            parsingContext.RequireCommandList(sceneExeContext._completionCmdList);
                            for (unsigned c=0; c<(unsigned)Techniques::Batch::Max; ++c)
                                intersectionContext.ExecuteDrawables(parsingContext, pkt[c], c, &context._cameraDesc);
                            modelIntersectionResults = intersectionContext.GetResults();
                        }

                        VLA(unsigned, drawableIndicesToLookup, modelIntersectionResults.size());
                        for (unsigned c=0; c<modelIntersectionResults.size(); ++c) {
                            drawableIndicesToLookup[c] = modelIntersectionResults[c]._drawableIndex;
                            assert(modelIntersectionResults[c]._packetIndex == 0);
                        }

                        DrawableMetadataLookupContext lookupContext { MakeIteratorRange(&modelIntersectionResults[0]._drawableIndex, &modelIntersectionResults[0]._drawableIndex+1) };
                        renderer.LookupDrawableMetadata(
                            lookupContext, sceneExeContext,
                            _placementsEditor->GetCellSet(), triangleBaseTestGuids, &triangleBaseTestGuids[triangleBasedTestCount]);

                        assert(lookupContext.GetProviders().size() == modelIntersectionResults.size());

                        for (unsigned c=0; c<modelIntersectionResults.size(); ++c) {
                            IntersectionTestResult r;
                            r._type = IntersectionTestResult::Type::Placement;
                            r._worldSpaceIntersectionPt = r._worldSpaceIntersectionNormal = {0,0,0};
                            r._distance = 0;
                            r._metadataQuery = std::move(lookupContext.GetProviders()[c]);
                            result.emplace_back(std::move(r));
                        }
                    }
                } 
                CATCH(const ::Assets::Exceptions::RetrievalError&) {}
                CATCH(const std::exception&) {}        // can sometimes through runtime_errors on pending assets
                CATCH_END

                trans->Cancel();
            }
        }

        unsigned firstExtraBit = IntegerLog2(uint32(Type::Extra));
        for (size_t c=0; c<_extraTesters.size(); ++c) {
            if (!(filter & 1<<uint32(c+firstExtraBit))) continue;
            _extraTesters[c]->FrustumIntersection(result, context, worldToProjection);
        }
    }

    IntersectionTestScene::IntersectionTestScene(
        std::shared_ptr<TerrainManager> terrainManager,
        std::shared_ptr<PlacementsEditor> placementsEditor,
        IteratorRange<const std::shared_ptr<SceneEngine::IIntersectionScene>*> extraTesters)
    : _terrainManager(std::move(terrainManager))
    , _placementsEditor(std::move(placementsEditor))
    {
        for (size_t c=0; c<extraTesters.size(); ++c) 
            _extraTesters.push_back(std::move(extraTesters.begin()[c]));
    }

    IntersectionTestScene::~IntersectionTestScene()
    {}

    std::shared_ptr<IIntersectionScene> CreateIntersectionTestScene(
        std::shared_ptr<TerrainManager> terrainManager,
        std::shared_ptr<PlacementsEditor> placementsEditor,
        IteratorRange<const std::shared_ptr<SceneEngine::IIntersectionScene>*> extraTesters)
    {
        return std::make_shared<IntersectionTestScene>(
            std::move(terrainManager), std::move(placementsEditor), extraTesters);
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////
        
    static Float4x4 CalculateWorldToProjection(const RenderCore::Techniques::CameraDesc& sceneCamera, float viewportAspect)
    {
        auto projectionMatrix = RenderCore::Techniques::Projection(sceneCamera, viewportAspect);
        return Combine(InvertOrthonormalTransform(sceneCamera._cameraToWorld), projectionMatrix);
    }

    std::pair<Float3, Float3> CalculateWorldSpaceRay(
        const RenderCore::Techniques::CameraDesc& sceneCamera,
        Int2 screenCoord, UInt2 viewMins, UInt2 viewMaxs)
    {
		UInt2 viewportDims = viewMaxs - viewMins;
		assert(viewportDims[0] > 0 && viewportDims[1] > 0);	// expecting a non-empty viewport here, otherwise we'll get a divide by zero below
        auto worldToProjection = CalculateWorldToProjection(sceneCamera, viewportDims[0] / float(viewportDims[1]));

        Float3 frustumCorners[8];
        CalculateAbsFrustumCorners(frustumCorners, worldToProjection, RenderCore::Techniques::GetDefaultClipSpaceType());
        Float3 cameraPosition = ExtractTranslation(sceneCamera._cameraToWorld);

        return BuildRayUnderCursor(screenCoord, frustumCorners, std::make_pair(viewMins, viewMaxs));
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    std::pair<Float3, Float3> IntersectionTestContext::CalculateWorldSpaceRay(Int2 screenCoord) const
    {
		return SceneEngine::CalculateWorldSpaceRay(_cameraDesc, screenCoord, _viewportMins, _viewportMaxs);
    }

    Float2 IntersectionTestContext::ProjectToScreenSpace(const Float3& worldSpaceCoord) const
    {
        Int2 viewport = _viewportMaxs - _viewportMins;
        auto worldToProjection = CalculateWorldToProjection(_cameraDesc, viewport[0] / float(viewport[1]));
        auto projCoords = worldToProjection * Expand(worldSpaceCoord, 1.f);

        return Float2(
            _viewportMins[0] + (projCoords[0] / projCoords[3] * 0.5f + 0.5f) * float(viewport[0]),
            _viewportMins[1] + (projCoords[1] / projCoords[3] * -0.5f + 0.5f) * float(viewport[1]));
    }

    void* IntersectionTestContext::GetService(uint64_t id) const
    {
        auto i = LowerBound(_services, id);
		if (i != _services.end() && i->first == id)
			return i->second;
		return nullptr;
    }

    void IntersectionTestContext::AttachService(uint64_t id, void* ptr)
    {
        auto i = LowerBound(_services, id);
		if (i != _services.end() && i->first == id) {
			i->second = ptr;
		} else {
			_services.insert(i, std::make_pair(id, ptr));
		}
    }

    IntersectionTestContext::IntersectionTestContext(const RenderCore::Techniques::CameraDesc& cameraDesc, Int2 viewportMins, Int2 viewportMaxs)
    : _cameraDesc(cameraDesc), _viewportMins(viewportMins), _viewportMaxs(viewportMaxs) {}
    IntersectionTestContext::~IntersectionTestContext() = default;
    IntersectionTestContext::IntersectionTestContext(IntersectionTestContext&&) = default;
    IntersectionTestContext& IntersectionTestContext::operator=(IntersectionTestContext&&) = default;
    IntersectionTestContext::IntersectionTestContext(const IntersectionTestContext&) = default;
    IntersectionTestContext& IntersectionTestContext::operator=(const IntersectionTestContext&) = default;

    namespace Stubs
    {
        std::optional<float> GetTerrainHeight(const IIntersectionScene& scene, Float2 pt)
        {
            return {};
        }
    }

}

