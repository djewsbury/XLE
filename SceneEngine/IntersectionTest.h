// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/Techniques/TechniqueUtils.h"        // for CameraDesc
#include "../Assets/AssetsCore.h"   // for rstring
#include "../Core/Types.h"
#include "../Math/Vector.h"
#include <memory>

namespace RenderCore { namespace Techniques { class CameraDesc; class DrawingApparatus; }}
namespace RenderCore { class PresentationChainDesc; }

namespace SceneEngine
{
    class IntersectionTestContext;

    class IntersectionTestResult
    {
    public:
        struct Type 
        {
            enum Enum 
            {
                Terrain = 1<<0, 
                Placement = 1<<1,

                Extra = 1<<6
            };
            typedef unsigned BitField;
        };
        
        Type::Enum						_type = Type::Enum(0);
        Float3							_worldSpaceCollision;
        std::pair<uint64_t, uint64_t>   _objectGuid = {0ull, 0ull};
        float							_distance = std::numeric_limits<float>::max();
        unsigned						_drawCallIndex = ~0u;
        uint64_t						_materialGuid = 0;
        ::Assets::rstring				_materialName;
        ::Assets::rstring				_modelName;
    };

    class IIntersectionScene
    {
    public:
        virtual IntersectionTestResult FirstRayIntersection(
            const IntersectionTestContext& context,
            std::pair<Float3, Float3> worldSpaceRay,
            IntersectionTestResult::Type::BitField filter = ~IntersectionTestResult::Type::BitField(0)) const = 0;

        virtual void FrustumIntersection(
            std::vector<IntersectionTestResult>& results,
            const IntersectionTestContext& context,
            const Float4x4& worldToProjection,
            IntersectionTestResult::Type::BitField filter = ~IntersectionTestResult::Type::BitField(0)) const = 0;

        virtual ~IIntersectionScene() = default;
    };

    /// <summary>Context for doing ray & box intersection test<summary>
    /// This context is intended for performing ray intersections for tools.
    /// Frequently we need to do "hit tests" and various projection and 
    /// unprojection operations. This context contains the minimal references
    /// to do this.
    /// Note that we need some camera information for LOD calculations. We could
    /// assume everything is at top LOD; but we will get a better match with 
    /// the rendered result if we take into account LOD. We even need viewport
    /// size -- because this can effect LOD as well. It's frustrating, but all 
    /// this is required!
    /// <seealso cref="IntersectionResolver" />
	class IntersectionTestContext
    {
    public:
		static std::pair<Float3, Float3> CalculateWorldSpaceRay(
            const RenderCore::Techniques::CameraDesc& sceneCamera,
            Int2 screenCoord, UInt2 viewMins, UInt2 viewMaxs);

        std::pair<Float3, Float3> CalculateWorldSpaceRay(Int2 screenCoord) const;
        Float2 ProjectToScreenSpace(const Float3& worldSpaceCoord) const;

		RenderCore::Techniques::CameraDesc _cameraDesc;
		Int2 _viewportMins, _viewportMaxs;
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
    };

    class TerrainManager;
    class PlacementCellSet;
    class PlacementsEditor;
    std::shared_ptr<IIntersectionScene> CreateIntersectionTestScene(
        std::shared_ptr<TerrainManager> terrainManager,
        std::shared_ptr<PlacementCellSet> placements,
        std::shared_ptr<PlacementsEditor> placementsEditor,
        IteratorRange<const std::shared_ptr<SceneEngine::IIntersectionScene>*> extraTesters = {});

    namespace Stubs
    {
        std::optional<float> GetTerrainHeight(const IIntersectionScene& scene, Float2 pt);
    }
}
