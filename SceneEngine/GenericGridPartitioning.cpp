#include "GenericGridPartitioning.h"
#include "../Assets/ChunkFileContainer.h"
#include "../Assets/BlockSerializer.h"
#include "../Math/ProjectionMath.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/Streams/SerializationUtils.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Core/Prefix.h"
#include <stack>
#include <cfloat>

namespace SceneEngine
{
    const float s_maxCellCreep = 0.5f;

#pragma pack(push)
#pragma pack(1)
    class GenericGridPartitioning::Pimpl
    {
    public:
        class Payload
        {
        public:
            BoundingBox                     _boundary;
			SerializableVector<unsigned>	_objects;

			void SerializeMethod(::Assets::NascentBlockSerializer& serializer) const 
			{ 
				SerializationOperator(serializer, _boundary);
				SerializationOperator(serializer, _objects);
			}

            Payload() : _boundary(Float3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), Float3(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max())) {}
        };

        struct Desc
        {
            Int2        _minCell;
            Int2        _maxCell;
            float       _minHeight, _maxHeight;
            float       _cellSize;
            unsigned	_maxCullResults;
            static const bool SerializeRaw = true;
        };

		SerializableVector<Payload>		_payloads;
        Payload                         _oversized;
        Desc                            _desc;

        static Int2 CalculateGridCell(const BoundingBox& box, float cellSize, Orientation orientation);

        unsigned CalculateMaxResults()
        {
            unsigned result = 0;
            for (auto&i:_payloads)
                result += (unsigned)i._objects.size();
            result += (unsigned)_oversized._objects.size();
            return result;
        }

        std::pair<Int2, Int2> CalculatePotentiallyVisibleRange(const Float4x4& cellToClipAligned, ClipSpaceType clipSpaceType, Orientation orientation) const
        {
            Float4 plane0, plane1;
            if (orientation == Orientation::YUp) {
                plane0 = Float4(0.f, 1.f, 0.f, -_desc._minHeight);
                plane1 = Float4(0.f, 1.f, 0.f, -_desc._maxHeight);
            } else {
                plane0 = Float4(0.f, 0.f, 1.f, -_desc._minHeight);
                plane1 = Float4(0.f, 0.f, 1.f, -_desc._maxHeight);
            }

            auto planarMinMax0 = GetPlanarMinMax(cellToClipAligned, plane0, clipSpaceType);
            auto planarMinMax1 = GetPlanarMinMax(cellToClipAligned, plane1, clipSpaceType);
            std::pair<Float2, Float2> planarMinMax;
            planarMinMax.first[0] = std::min(planarMinMax0.first[0], planarMinMax1.first[0]);
            planarMinMax.first[1] = std::min(planarMinMax0.first[1], planarMinMax1.first[1]);
            planarMinMax.second[0] = std::max(planarMinMax0.second[0], planarMinMax1.second[0]);
            planarMinMax.second[1] = std::max(planarMinMax0.second[1], planarMinMax1.second[1]);

            // We must allow for a leeway of s_maxCellCreep in all directions
            // (We could optimize this by calculating the max cell creep in each direction)
            planarMinMax.first[0] -= s_maxCellCreep * _desc._cellSize;
            planarMinMax.first[1] -= s_maxCellCreep * _desc._cellSize;
            planarMinMax.second[0] += s_maxCellCreep * _desc._cellSize;
            planarMinMax.second[1] += s_maxCellCreep * _desc._cellSize;

            auto resultMin = Int2(int(std::floor(planarMinMax.first[0]) / _desc._cellSize), int(std::floor(planarMinMax.first[1]) / _desc._cellSize));
            auto resultMax = Int2(int(std::ceil(planarMinMax.second[0]) / _desc._cellSize), int(std::ceil(planarMinMax.second[1]) / _desc._cellSize));
            return std::make_pair(
                Int2(std::max(resultMin[0], _desc._minCell[0]), std::max(resultMin[1], _desc._minCell[1])),
                Int2(std::min(resultMax[0], _desc._maxCell[0]), std::min(resultMax[1], _desc._maxCell[1])));
        }

		void SerializeMethod(::Assets::NascentBlockSerializer& serializer) const
		{
			SerializationOperator(serializer, _payloads);
            SerializationOperator(serializer, _oversized);
            SerializationOperator(serializer, _desc);
		}
    };
#pragma pack(pop)

    Int2 GenericGridPartitioning::Pimpl::CalculateGridCell(const BoundingBox& box, float cellSize, Orientation orientation)
    {
        // Find the grid cell for the given bounding box (or return Int2(INT_MAX, INT_MAX) for an oversized
        // bounding box)
        Float2 min, max;
        if (orientation == Orientation::YUp) {
            min = Float2(box.first[0] / cellSize, box.first[2] / cellSize);
            max = Float2(box.second[0] / cellSize, box.second[2] / cellSize);
        } else {
            min = Float2(box.first[0] / cellSize, box.first[1] / cellSize);
            max = Float2(box.second[0] / cellSize, box.second[1] / cellSize);
        }

        Int2 result;

        // Some bounding boxes can exist on the boundary of multiple cells.
        // But bounding boxes should go no more than this fraction into their neighbouring cells
        // (If they do, or if they full cover more than one cell, then then are put in the "oversized" list)
        for (unsigned d=0; d<2; ++d) {
            int xmin = (int)std::floor(min[d]);
            int xmax = (int)std::ceil(max[d]);
            assert(xmax - xmin >= 1);
            float fractLeft = std::ceil(min[d]) - min[d];
            float fractRight = max[d] - std::floor(max[d]);
            assert(fractLeft >= 0.f && fractLeft <= 1.f && fractRight >= 0.f && fractRight <= 1.f);

            switch (xmax - xmin) {
            case 1:
                // simple case, entirely within
                result[d] = xmin;
                break;

            case 2:
                // In this case, straddling a cell edge. Pick left or right, depending on which is larger
                if (fractLeft > fractRight) {
                    if (fractRight >= s_maxCellCreep) return Int2(INT_MAX, INT_MAX);
                    result[d] = xmin;
                } else {
                    if (fractLeft >= s_maxCellCreep) return Int2(INT_MAX, INT_MAX);
                    result[d] = xmin+1;
                }
                break;

            case 3:
                // In this case, entirely covering a middle cell, but creeping into left and right
                if (fractLeft >= s_maxCellCreep || fractRight >= s_maxCellCreep)
                    return Int2(INT_MAX, INT_MAX);  // creeping too far left or right; it must fall into the oversized bucket

                result[d] = xmin+1;
                break;

            default:
                return Int2(INT_MAX, INT_MAX);  // covering multiple cells; must be oversized
            }
        }

        return result;
    }

	const GenericGridPartitioning::Pimpl& GenericGridPartitioning::GetPimpl() const
	{
		return *(const GenericGridPartitioning::Pimpl*)::Assets::Block_GetFirstObject(_dataBlock.get());
	}

    bool GenericGridPartitioning::CalculateVisibleObjects(
        const Float4x4& cellToClipAligned, ClipSpaceType clipSpaceType,
        const BoundingBox objCellSpaceBoundingBoxes[], size_t objStride,
        unsigned visObjs[], unsigned& visObjsCount, unsigned visObjMaxCount,
        Metrics* metrics) const
    {
        visObjsCount = 0;
        assert((size_t(AsFloatArray(cellToClipAligned)) & 0xf) == 0);

        unsigned cellAabbTestCount = 0, payloadAabbTestCount = 0;
		const auto& pimpl = GetPimpl();

        // Find the intersection of the frustum with our grid space
        // This will tell us the max and minimum possible grid cells
        const auto orientation = Orientation::YUp;
        auto potentiallyVisible = pimpl.CalculatePotentiallyVisibleRange(cellToClipAligned, clipSpaceType, orientation);
        for (auto y=potentiallyVisible.first[1]; y<=potentiallyVisible.second[1]; ++y)
            for (auto x=potentiallyVisible.first[0]; x<=potentiallyVisible.second[0]; ++x) {
                const auto& payload = pimpl._payloads[(y - pimpl._desc._minCell[1]) * (pimpl._desc._maxCell[0] - pimpl._desc._minCell[0] + 1) + x - pimpl._desc._minCell[0]];
                if (payload._objects.empty()) continue;

                ++cellAabbTestCount;
                auto test = TestAABB_Aligned(cellToClipAligned, payload._boundary.first, payload._boundary.second, clipSpaceType);
                if (test != CullTestResult::Culled) {
                    if (test == CullTestResult::Boundary && (objCellSpaceBoundingBoxes && objStride)) {
                        for (auto i:payload._objects) {
                            const auto& boundary = *PtrAdd(objCellSpaceBoundingBoxes, i*objStride);
                            ++payloadAabbTestCount;
                            if (!CullAABB_Aligned(cellToClipAligned, boundary.first, boundary.second, clipSpaceType)) {
                                if ((visObjsCount+1) > visObjMaxCount) {
                                    return false;
                                }
                                visObjs[visObjsCount++] = i;
                            }
                        }
                    } else {
                        if ((visObjsCount + payload._objects.size()) > visObjMaxCount) {
							return false;
						}

						for (auto i:payload._objects) visObjs[visObjsCount++] = i;
                    }
                }
            }

        assert(visObjsCount <= visObjMaxCount);
        if (metrics) {
            metrics->_cellAabbTestCount = cellAabbTestCount;
            metrics->_payloadAabbTestCount = payloadAabbTestCount;
        }

        return true;
    }

	unsigned GenericGridPartitioning::GetMaxResults() const
    {
        return GetPimpl()._desc._maxCullResults;
    }

	std::vector<uint8_t> GenericGridPartitioning::Build(
        const BoundingBox objCellSpaceBoundingBoxes[], size_t objStride,
        size_t objCount, float cellSize,
		Orientation orientation)
    {
        // First; find the maximum extents we'll require
        Int2 mins(INT_MAX, INT_MAX), maxs(INT_MIN, INT_MIN);
        float minHeight = std::numeric_limits<float>::max(), maxHeight = -std::numeric_limits<float>::max();
        unsigned heightIndex = (orientation == Orientation::YUp)?1:2;
        for (unsigned c=0; c<objCount; ++c) {
            const auto& objBoundary = *PtrAdd(objCellSpaceBoundingBoxes, c * objStride);
            auto gridCell = Pimpl::CalculateGridCell(objBoundary, cellSize, orientation);
            if (gridCell != Int2(INT_MAX, INT_MAX)) {
                mins[0] = std::min(gridCell[0], mins[0]);
                mins[1] = std::min(gridCell[1], mins[1]);
                maxs[0] = std::max(gridCell[0], maxs[0]);
                maxs[1] = std::max(gridCell[1], maxs[1]);
            }

            minHeight = std::min(minHeight, objBoundary.first[heightIndex]);
            maxHeight = std::max(maxHeight, objBoundary.second[heightIndex]);
        }

        auto pimpl = std::make_unique<Pimpl>();

        pimpl->_payloads.resize((maxs[1]-mins[1]+1) * (maxs[0]-mins[0]+1));
        pimpl->_desc._minCell = mins;
        pimpl->_desc._maxCell = maxs;
        pimpl->_desc._minHeight = minHeight;
        pimpl->_desc._maxHeight = maxHeight;
        pimpl->_desc._cellSize = cellSize;

        for (unsigned c=0; c<objCount; ++c) {
            const auto& objBoundary = *PtrAdd(objCellSpaceBoundingBoxes, c * objStride);
            auto gridCell = Pimpl::CalculateGridCell(objBoundary, cellSize, orientation);
            Pimpl::Payload* payload = nullptr;
            if (gridCell != Int2(INT_MAX, INT_MAX)) {
                assert(gridCell[0] >= mins[0] && gridCell[1] >= mins[1] && gridCell[0] <= maxs[0] && gridCell[1] <= maxs[1]);
                payload = &pimpl->_payloads[(gridCell[1] - mins[1]) * (maxs[0] - mins[0] + 1) + gridCell[0] - mins[0]];
            } else {
                payload = &pimpl->_oversized;
            }

            payload->_objects.push_back(c);
            payload->_boundary.first[0] = std::min(payload->_boundary.first[0], objBoundary.first[0]);
            payload->_boundary.first[1] = std::min(payload->_boundary.first[1], objBoundary.first[1]);
            payload->_boundary.first[2] = std::min(payload->_boundary.first[2], objBoundary.first[2]);
            payload->_boundary.second[0] = std::max(payload->_boundary.second[0], objBoundary.second[0]);
            payload->_boundary.second[1] = std::max(payload->_boundary.second[1], objBoundary.second[1]);
            payload->_boundary.second[2] = std::max(payload->_boundary.second[2], objBoundary.second[2]);
        }

        pimpl->_desc._maxCullResults = pimpl->CalculateMaxResults();

        ::Assets::NascentBlockSerializer serializer;
		SerializationOperator(serializer, *pimpl);
		return std::vector<uint8_t>(
			serializer.AsMemoryBlock(),
			serializer.Size());
    }

    std::vector<std::pair<Float3, Float3>> GenericGridPartitioning::GetCellBoundingBoxes() const
    {
        const auto& pimpl = GetPimpl();
        std::vector<std::pair<Float3, Float3>> result;
        result.reserve(pimpl._payloads.size());
        for (const auto&p:pimpl._payloads)
            if (!p._objects.empty())
                result.push_back(p._boundary);
        return result;
    }

	static const uint64 ChunkType_GridPartitioning = ConstHash64<'Grid', 'Part'>::Value;
	static const unsigned GridPartitioningDataVersion = 0;

	static const ::Assets::ArtifactRequest GridPartitioningChunkRequests[]
    {
        ::Assets::ArtifactRequest { "GridPartitioning", ChunkType_GridPartitioning, GridPartitioningDataVersion, ::Assets::ArtifactRequest::DataType::BlockSerializer },
    };

	GenericGridPartitioning::GenericGridPartitioning(GenericGridPartitioning&& moveFrom)
	: _dataBlock(std::move(moveFrom._dataBlock))
	, _depVal(std::move(moveFrom._depVal))
	{}

	GenericGridPartitioning& GenericGridPartitioning::operator=(GenericGridPartitioning&& moveFrom) never_throws
	{
		_dataBlock = std::move(moveFrom._dataBlock);
		_depVal = std::move(moveFrom._depVal);
		return *this;
	}

	GenericGridPartitioning::GenericGridPartitioning(const ::Assets::ChunkFileContainer& chunkFile)
	: _depVal(chunkFile.GetDependencyValidation())
    {
        auto chunks = chunkFile.ResolveRequests(MakeIteratorRange(GridPartitioningChunkRequests));
		assert(chunks.size() == 1);
		_dataBlock = std::move(chunks[0]._buffer);
	}

	GenericGridPartitioning::GenericGridPartitioning(std::unique_ptr<uint8[], PODAlignedDeletor>&& dataBlock)
	: _dataBlock(std::move(dataBlock))
	{
	}

	GenericGridPartitioning::GenericGridPartitioning() {}
	GenericGridPartitioning::~GenericGridPartitioning() {}
}
