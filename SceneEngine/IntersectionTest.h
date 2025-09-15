// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DrawableMetadataLookup.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"        // for CameraDesc
#include "../Math/Vector.h"
#include <memory>
#include <optional>

namespace RenderCore { namespace Techniques { class CameraDesc; class DrawingApparatus; class TechniqueContext; class ParsingContext; }}

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
                Brush = 1<<2,

                Extra = 1<<6
            };
            using BitField = unsigned;
        };
        
        Type::Enum			_type = Type::Enum(0);
        Float3				_worldSpaceIntersectionPt;
        Float3				_worldSpaceIntersectionNormal;
        float				_distance = std::numeric_limits<float>::max();
        MetadataProvider	_metadataQuery; 
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
            const AccurateFrustumTester&,
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
        std::pair<Float3, Float3> CalculateWorldSpaceRay(Int2 screenCoord) const;
        Float2 ProjectToScreenSpace(const Float3& worldSpaceCoord) const;

		RenderCore::Techniques::CameraDesc _cameraDesc;
		Int2 _viewportMins, _viewportMaxs;

		void* GetService(uint64_t) const;
        void AttachService(uint64_t, void*);

        template<typename Type>
            Type* GetService() const { return (Type*)GetService(typeid(std::decay_t<Type>).hash_code()); }
        template<typename Type>
            void AttachService2(Type& type) { AttachService(typeid(std::decay_t<Type>).hash_code(), &type); }

        IntersectionTestContext(const RenderCore::Techniques::CameraDesc&, Int2 viewportMins, Int2 viewportMaxs);
        ~IntersectionTestContext();
        IntersectionTestContext(IntersectionTestContext&&);
        IntersectionTestContext& operator=(IntersectionTestContext&&);
        IntersectionTestContext(const IntersectionTestContext&);
        IntersectionTestContext& operator=(const IntersectionTestContext&);
    private:
        std::vector<std::pair<uint64_t, void*>> _services;
    };

    class TerrainManager;
    class PlacementCellSet;
    class PlacementsEditor;
    std::shared_ptr<IIntersectionScene> CreateIntersectionTestScene(
        std::shared_ptr<TerrainManager> terrainManager,
        std::shared_ptr<PlacementsEditor> placementsEditor,
        IteratorRange<const std::shared_ptr<SceneEngine::IIntersectionScene>*> extraTesters = {});

    namespace Stubs
    {
        std::optional<float> GetTerrainHeight(const IIntersectionScene& scene, Float2 pt);
    }

    std::pair<Float3, Float3> CalculateWorldSpaceRay(
        const RenderCore::Techniques::CameraDesc& sceneCamera,
        Int2 screenCoord, UInt2 viewMins, UInt2 viewMaxs);

    RenderCore::Techniques::TechniqueContext MakeIntersectionsTechniqueContext(
        RenderCore::Techniques::DrawingApparatus& drawingApparatus);

    std::optional<IntersectionTestResult> FirstRayIntersection(
        RenderCore::Techniques::ParsingContext& parsingContext,
        PlacementsEditor& placementsEditor,
        std::pair<Float3, Float3> worldSpaceRay,
        const RenderCore::Techniques::CameraDesc* cameraForLOD);
}
