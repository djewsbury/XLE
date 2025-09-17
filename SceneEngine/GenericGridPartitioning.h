#pragma once

#include "../Math/Vector.h"
#include "../Math/Matrix.h"
#include "../Assets/AssetsCore.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/MemoryUtils.h"
#include <utility>
#include <memory>

namespace Assets { class ArtifactChunkContainer; }
namespace XLEMath { enum class ClipSpaceType; }

namespace SceneEngine
{
    class GenericGridPartitioning
    {
    public:
        typedef std::pair<Float3, Float3> BoundingBox;

        struct Metrics
        {
            unsigned _cellAabbTestCount = 0u;
            unsigned _payloadAabbTestCount = 0u;
        };

        bool CalculateVisibleObjects(
            const Float4x4& cellToClipAligned, ClipSpaceType clipSpaceType,
            const BoundingBox objCellSpaceBoundingBoxes[], size_t objStride,
            unsigned visObjs[], unsigned& visObjsCount, unsigned visObjMaxCount,
            Metrics* metrics = nullptr) const;
        unsigned GetMaxResults() const;

		enum class Orientation { YUp, ZUp };

		static std::vector<uint8> Build(
            const BoundingBox objCellSpaceBoundingBoxes[], size_t objStride,
            size_t objCount, float cellSize,
			Orientation orientation = Orientation::ZUp);

		GenericGridPartitioning(const ::Assets::ArtifactChunkContainer& chunkFile);
		GenericGridPartitioning(std::unique_ptr<uint8[], PODAlignedDeletor>&& dataBlock);
		GenericGridPartitioning();
        ~GenericGridPartitioning();

		GenericGridPartitioning(GenericGridPartitioning&& moveFrom);
		GenericGridPartitioning& operator=(GenericGridPartitioning&& moveFrom) never_throws;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
        std::vector<std::pair<Float3, Float3>> GetCellBoundingBoxes() const;

    protected:
        class Pimpl;
		std::unique_ptr<uint8[], PODAlignedDeletor> _dataBlock;
		::Assets::DependencyValidation _depVal;

		const Pimpl& GetPimpl() const;
    };
}
