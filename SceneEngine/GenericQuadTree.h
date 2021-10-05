#pragma once

#include "../Math/Vector.h"
#include "../Math/Matrix.h"
#include "../Assets/AssetsCore.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Core/Types.h"
#include <utility>
#include <memory>
#include <iosfwd>

namespace Assets { class ChunkFileContainer; }
namespace XLEMath { enum class ClipSpaceType; }
namespace RenderOverlays { namespace DebuggingDisplay { class IWidget; }}
namespace RenderOverlays { class IOverlayContext; }
namespace XLEMath { class ArbitraryConvexVolumeTester; }

namespace SceneEngine
{
    /// <summary>Quad tree arrangement for static object</summary>
    /// Given a set of objects (identified by cell-space bounding boxes)
    /// calculate a balanced quad tree. This can be used to optimise camera
    /// frustum.
    ///
    /// Use "CalculateVisibleObjects" to perform camera frustum tests
    /// using the quad tree information.
    ///
    /// Note that all object culling is done using bounding boxes axially
    /// aligned in cell-space (not object local space). This can be a little
    /// less accurate than object space. But it avoids an expensive matrix
    /// multiply. If the world space bounding box straddles the edge of the
    /// frustum, the caller may wish to perform a local space bounding
    /// box test to further improve the result.
    class GenericQuadTree
    {
    public:
        typedef std::pair<Float3, Float3> BoundingBox;

        struct Metrics
        {
            unsigned _nodeAabbTestCount = 0;
            unsigned _payloadAabbTestCount = 0;

            Metrics& operator+=(const Metrics& other)
            {
                _nodeAabbTestCount += other._nodeAabbTestCount;
                _payloadAabbTestCount += other._payloadAabbTestCount;
                return *this;
            }
        };

        bool CalculateVisibleObjects(
            const Float4x4& cellToClipAligned, ClipSpaceType clipSpaceType,
            const BoundingBox objCellSpaceBoundingBoxes[], size_t objStride,
            unsigned visObjs[], unsigned& visObjsCount, unsigned visObjMaxCount,
            Metrics* metrics = nullptr) const;

        bool CalculateVisibleObjects(
            IteratorRange<const Float4x4*> cellToClipAligned, uint32_t viewMask, ClipSpaceType clipSpaceType,
            const BoundingBox objCellSpaceBoundingBoxes[], size_t objStride,
            std::pair<unsigned, uint32_t> visObjs[], unsigned& visObjsCount, unsigned visObjMaxCount,
            Metrics* metrics = nullptr) const;

        bool CalculateVisibleObjects(
            const XLEMath::ArbitraryConvexVolumeTester& volumeTester,
            const Float3x4& cellToClip,
            const BoundingBox objCellSpaceBoundingBoxes[], size_t objStride,
            unsigned visObjs[], unsigned& visObjsCount, unsigned visObjMaxCount,
            Metrics* metrics = nullptr) const;

        unsigned GetMaxResults() const;
        unsigned GetNodeCount() const;

		enum class Orientation { YUp, ZUp };

        using DataBlock = std::unique_ptr<uint8[], PODAlignedDeletor>;

		static std::pair<DataBlock, size_t> BuildQuadTree(
            const BoundingBox objCellSpaceBoundingBoxes[], size_t objStride,
            size_t objCount, unsigned leafThreshold,
			Orientation orientation = Orientation::ZUp);

        std::ostream& SerializeMethod(std::ostream&) const;

		GenericQuadTree(const ::Assets::ChunkFileContainer& chunkFile);
		GenericQuadTree(DataBlock&& dataBlock);
		GenericQuadTree();
        ~GenericQuadTree();

		GenericQuadTree(GenericQuadTree&& moveFrom);
		GenericQuadTree& operator=(GenericQuadTree&& moveFrom) never_throws;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		std::vector<std::pair<Float3, Float3>> GetNodeBoundingBoxes() const;

    protected:
        class Pimpl;
		DataBlock _dataBlock;
		::Assets::DependencyValidation _depVal;

		const Pimpl& GetPimpl() const;        
        friend class QuadTreeDisplay;
    };

    std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateQuadTreeDisplay(
        std::shared_ptr<GenericQuadTree>,
        const Float3x4& localToWorld);

    std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateBoundingBoxDisplay(
        const GenericQuadTree::BoundingBox objCellSpaceBoundingBoxes[], size_t objStride, size_t objCount,
        const Float3x4& localToWorld);
}
