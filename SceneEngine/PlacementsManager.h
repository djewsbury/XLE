// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IScene.h"
#include "../Assets/AssetsCore.h"
#include "../Utility/UTFUtils.h"
#include "../Math/Vector.h"
#include "../Math/Matrix.h"
#include <string>
#include <functional>
#include <future>

namespace RenderCore { namespace Techniques { class ParsingContext; class ModelCache; class DrawablesPacket; class ICustomDrawDelegate; } }
namespace Utility { class OutputStream; template<typename CharType> class InputStreamFormatter; }
namespace Assets { class DirectorySearchRules; class IAsyncMarker; }
namespace RenderOverlays { namespace DebuggingDisplay { class IWidget; }}
namespace XLEMath { class ArbitraryConvexVolumeTester; }
namespace Assets { class OperationContext; }

namespace SceneEngine
{
	using PlacementsModelCache = RenderCore::Techniques::ModelCache;


    class PlacementsRenderer;
    class PlacementsIntersections;
    class PlacementsEditor;
    class GenericQuadTree;
    class DynamicImposters;

    /// <summary>A collection of cells</summary>
    /// 
    class PlacementCellSet
    {
    public:
        void Add(StringSection<> placementsInitializer, const Float3x4& cellToWorld, std::pair<Float3, Float3> localSpaceAABB);
        std::optional<Float3x4> GetCellToWorld(StringSection<> placementsInitializer) const;

        PlacementCellSet();
        ~PlacementCellSet();
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

    /// <summary>Manages stream and organization of object placements</summary>
    /// In this context, placements are static objects placed in the world. Most
    /// scenes will have a large number of essentially static objects. This object
    /// manages a large continuous world of these kinds of objects.
    class PlacementsManager : public std::enable_shared_from_this<PlacementsManager>
    {
    public:
        const std::shared_ptr<PlacementsRenderer>& GetRenderer();
        const std::shared_ptr<PlacementsIntersections>& GetIntersections();
        std::shared_ptr<PlacementsEditor> CreateEditor(const std::shared_ptr<PlacementCellSet>& cellSet);

        PlacementsManager(std::shared_ptr<PlacementsModelCache> modelCache, std::shared_ptr<::Assets::OperationContext> loadingContext);
        ~PlacementsManager();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        friend class PlacementsEditor;
    };
    
    class PlacementCell;
    class PlacementsCache;
    class PreparedScene;
    using PlacementGUID = std::pair<uint64_t, uint64_t>;
    
    class PlacementsRenderer
    {
    public:
            // -------------- Rendering --------------
        void BuildDrawables(
            ExecuteSceneContext& executeContext,
			const PlacementCellSet& cellSet);

            // -------------- Render filtered --------------
        void BuildDrawablesSingleView(
            ExecuteSceneContext& executeContext,
            const PlacementCellSet& cellSet,
            const PlacementGUID* begin, const PlacementGUID* end,
			const std::shared_ptr<RenderCore::Techniques::ICustomDrawDelegate>& preDrawDelegate = nullptr);

            // -------------- Utilities --------------
        auto GetVisibleQuadTrees(const PlacementCellSet& cellSet, const Float4x4& worldToClip) const
            -> std::vector<std::pair<Float3x4, std::shared_ptr<GenericQuadTree>>>;
        auto GetQuadTree(const PlacementCellSet& cellSet, StringSection<> cellName) const
            -> std::shared_ptr<GenericQuadTree>;

        struct ObjectBoundingBoxes { const std::pair<Float3, Float3> * _boundingBox = nullptr; unsigned _stride = 0; unsigned _count = 0; };
        auto GetObjectBoundingBoxes(const PlacementCellSet& cellSet, const Float4x4& worldToClip) const
            -> std::vector<std::pair<Float3x4, ObjectBoundingBoxes>>;
        auto GetObjectBoundingBoxes(const PlacementCellSet& cellSet, StringSection<> cellName) const -> ObjectBoundingBoxes;

        void SetImposters(std::shared_ptr<DynamicImposters> imposters);

        std::future<void> PrepareDrawables(IteratorRange<const Float4x4*> worldToCullingFrustums, const PlacementCellSet& cellSet);

        PlacementsRenderer(
            std::shared_ptr<PlacementsCache> placementsCache, 
            std::shared_ptr<PlacementsModelCache> modelCache);
        ~PlacementsRenderer();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        void BuildDrawablesComplex(
            ExecuteSceneContext& executeContext,
			const PlacementCellSet& cellSet);
    };

    class PlacementsIntersections
    {
    public:
                // -------------- Intersections --------------
        class IntersectionDef
        {
        public:
            Float3x4    _localToWorld;
            std::pair<Float3, Float3> _localSpaceBoundingBox;
            uint64_t      _model;
            uint64_t      _material;
        };

        std::vector<PlacementGUID> Find_BoxIntersection(
            const PlacementCellSet& cellSet,
            const Float3& worldSpaceMins, const Float3& worldSpaceMaxs,
            const std::function<bool(const IntersectionDef&)>& predicate);

        std::vector<PlacementGUID> Find_RayIntersection(
            const PlacementCellSet& cellSet,
            const Float3& rayStart, const Float3& rayEnd,
            const std::function<bool(const IntersectionDef&)>& predicate);

        std::vector<PlacementGUID> Find_FrustumIntersection(
            const PlacementCellSet& cellSet,
            const Float4x4& worldToProjection,
            const std::function<bool(const IntersectionDef&)>& predicate);

        PlacementsIntersections(
            std::shared_ptr<PlacementsCache> placementsCache, 
            std::shared_ptr<PlacementsModelCache> modelCache);
        ~PlacementsIntersections();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

    class PlacementsEditor
    {
    public:
        typedef Float3x4 PlacementsTransform;

        class ObjTransDef
        {
        public:
            Float3x4        _localToWorld;
            std::string     _model;
            std::string     _material;
            std::string     _supplements;

            ObjTransDef() {}
            ObjTransDef(const Float3x4& localToWorld, const std::string& model, const std::string& material, const std::string& supplements)
                : _localToWorld(localToWorld), _model(model), _material(material), _supplements(supplements), _transaction(Error) {}

            enum TransactionType { Unchanged, Created, Deleted, Modified, Error };
            TransactionType _transaction;
        };

            // -------------- transactions --------------
        class ITransaction
        {
        public:
            virtual const ObjTransDef&  GetObject(unsigned index) const = 0;
            virtual const ObjTransDef&  GetObjectOriginalState(unsigned index) const = 0;
            virtual PlacementGUID       GetGuid(unsigned index) const = 0;
            virtual PlacementGUID       GetOriginalGuid(unsigned index) const = 0;
            virtual unsigned            GetObjectCount() const = 0;
            virtual auto                GetLocalBoundingBox(unsigned index) const -> std::pair<Float3, Float3> = 0;
            virtual auto                GetWorldBoundingBox(unsigned index) const -> std::pair<Float3, Float3> = 0;
            virtual std::string         GetMaterialName(unsigned objectIndex, uint64_t materialGuid) const = 0;

            virtual void    SetObject(unsigned index, const ObjTransDef& newState) = 0;
            virtual bool    Create(const ObjTransDef& newState) = 0;
            virtual bool    Create(PlacementGUID guid, const ObjTransDef& newState) = 0;
            virtual void    Delete(unsigned index) = 0;

            virtual void    Commit() = 0;
            virtual void    Cancel() = 0;
            virtual void    UndoAndRestart() = 0;
        };

        struct TransactionFlags { 
            enum Flags { IgnoreIdTop32Bits = 1<<1 };
            typedef unsigned BitField;
        };
        std::shared_ptr<ITransaction> Transaction_Begin(
            const PlacementGUID* placementsBegin, const PlacementGUID* placementsEnd,
            TransactionFlags::BitField transactionFlags = 0);

        uint64_t  CreateCell(const ::Assets::ResChar name[], const Float2& mins, const Float2& maxs);
        bool    RemoveCell(uint64_t id);
        static uint64_t GenerateObjectGUID();
        void    PerformGUIDFixup(PlacementGUID* begin, PlacementGUID* end) const;

        std::pair<Float3, Float3> CalculateCellBoundary(uint64_t cellId) const;

        std::string GetMetricsString(uint64_t cellId) const;
        void WriteAllCells();
        void WriteCell(uint64_t cellId, const Assets::ResChar destinationFile[]) const;

        std::pair<Float3, Float3> GetModelBoundingBox(const Assets::ResChar modelName[]) const;

        std::shared_ptr<PlacementsManager> GetManager();
        const PlacementCellSet& GetCellSet() const;

        PlacementsEditor(
            std::shared_ptr<PlacementCellSet> cellSet,
            std::shared_ptr<PlacementsManager> manager,
            std::shared_ptr<PlacementsCache> placementsCache,
            std::shared_ptr<PlacementsModelCache> modelCache);
        ~PlacementsEditor();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        friend class Transaction;
    };
}

