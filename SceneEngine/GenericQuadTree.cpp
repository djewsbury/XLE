// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GenericQuadTree.h"
#include "../Assets/ChunkFileContainer.h"
#include "../Math/ProjectionMath.h"
#include "../Assets/BlockSerializer.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/Streams/SerializationUtils.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/ArithmeticUtils.h"
#include "../Utility/StreamUtils.h"
#include "../Core/Prefix.h"
#include <stack>
#include <cfloat>

namespace SceneEngine
{
#pragma pack(push)
#pragma pack(1)
    class GenericQuadTree::Pimpl
    {
    public:
        class Node
        {
        public:
            BoundingBox     _boundary;
            unsigned        _payloadID;
            unsigned        _treeDepth;
            unsigned        _children[4];

			static const bool SerializeRaw = true;
        };

        class Payload
        {
        public:
			SerializableVector<unsigned>	_objects;

			void SerializeMethod(::Assets::NascentBlockSerializer& serializer) const
			{
				SerializationOperator(serializer, _objects);
			}
        };

		SerializableVector<Node>		_nodes;
		SerializableVector<Payload>		_payloads;
        unsigned						_maxCullResults;

        class WorkingObject
        {
        public:
            BoundingBox     _boundary;
            int             _id;
        };

        void PushNode(  unsigned parentNode, unsigned childIndex,
                        const std::vector<WorkingObject>& workingObjects,
						unsigned leafThreshold, Orientation orientation);

        unsigned CalculateMaxResults()
        {
            unsigned result = 0;
            for (auto&i:_payloads)
                result += (unsigned)i._objects.size();
            return result;
        }

        static void InitPayload(Payload& p, const std::vector<WorkingObject>& workingObjects)
        {
            for (auto i=workingObjects.cbegin(); i!=workingObjects.cend(); ++i) {
                p._objects.push_back(i->_id);
            }
        }

        static BoundingBox CalculateBoundary(const std::vector<WorkingObject>& workingObjects)
        {
            BoundingBox result;
            result.first  = Float3( std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max());
            result.second = Float3(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
            for (auto i=workingObjects.cbegin(); i!=workingObjects.cend(); ++i) {
                assert(i->_boundary.first[0] <= i->_boundary.second[0]);
                assert(i->_boundary.first[1] <= i->_boundary.second[1]);
                assert(i->_boundary.first[2] <= i->_boundary.second[2]);
                result.first[0] = std::min(result.first[0], i->_boundary.first[0]);
                result.first[1] = std::min(result.first[1], i->_boundary.first[1]);
                result.first[2] = std::min(result.first[2], i->_boundary.first[2]);
                result.second[0] = std::max(result.second[0], i->_boundary.second[0]);
                result.second[1] = std::max(result.second[1], i->_boundary.second[1]);
                result.second[2] = std::max(result.second[2], i->_boundary.second[2]);
            }
            return result;
        }

        struct DivMetrics { unsigned _leftCount = 0; unsigned _straddleCount = 0; unsigned _rightCount = 0; };

        static DivMetrics DividingLineMetrics(float dividingLine, const std::vector<WorkingObject>& workingObjects, unsigned axis)
        {
            DivMetrics result;
            for (auto i=workingObjects.cbegin(); i!=workingObjects.cend(); ++i) {
                result._leftCount += (i->_boundary.second[axis] <= dividingLine);
                result._straddleCount += (i->_boundary.first[axis] < dividingLine) && (i->_boundary.second[axis] > dividingLine);
                result._rightCount += (i->_boundary.first[axis] >= dividingLine);
            }
            return result;
        }

        static float Volume(const BoundingBox& box)
        {
            return (box.second[2] - box.first[2]) * (box.second[1] - box.first[1]) * (box.second[0] - box.first[0]);
        }

		void SerializeMethod(::Assets::NascentBlockSerializer& serializer) const
		{
			SerializationOperator(serializer, _nodes);
			SerializationOperator(serializer, _payloads);
			SerializationOperator(serializer, _maxCullResults);
		}

        void SerializeNode(std::ostream&str, unsigned nodeIdx) const;
    };
#pragma pack(pop)

	void GenericQuadTree::Pimpl::PushNode(   
        unsigned parentNodeIndex, unsigned childIndex,
        const std::vector<WorkingObject>& workingObjects,
		unsigned leafThreshold, Orientation orientation)
    {
        Node newNode;
        newNode._boundary = CalculateBoundary(workingObjects);
        newNode._children[0] = newNode._children[1] = newNode._children[2] = newNode._children[3] = ~unsigned(0x0);

		const unsigned axis0 = 0, axis1 = 1, axis2 = 2;

        Node* parent = nullptr;
        if (parentNodeIndex < _nodes.size())
            parent = &_nodes[parentNodeIndex];

        newNode._treeDepth = parent ? (parent->_treeDepth+1) : 0;
        for (unsigned c=0; c<4; ++c) newNode._children[c] = ~unsigned(0x0);
        newNode._payloadID = ~unsigned(0x0);

            //  if the quantity of objects in this node is less than a threshold 
            //  amount, then we can consider it a leaf node
        if (workingObjects.size() <= leafThreshold) {
            Payload payload;
            InitPayload(payload, workingObjects);
            _payloads.push_back(std::move(payload));
            newNode._payloadID = unsigned(_payloads.size()-1);

            if (parent) {
                parent->_children[childIndex] = unsigned(_nodes.size());
            }
            _nodes.push_back(newNode);
            return;
        }

            //  if it's not a leaf, then we must divide the boundary into sub nodes
            //  Let's try to do this in a way that will adapt to the placements of
            //  objects, and create a balanced tree. However, there is always a
            //  chance that objects will not be able to fit into the division 
            //  perfectly... These "straddling" objects need to be placed into the 
            //  smallest node that contains them completely. Ideally we want to find
            //  dividing lines that separate the objects into 2 roughly even groups, 
            //  but minimize the number of straddling objects. We can just do a brute
            //  force test of various potential dividing lines near the median points

        float bestDividingLine[3];
        unsigned straddleCount[3];

        {
            std::vector<WorkingObject> sortedObjects = workingObjects;
            auto objCount =  unsigned(sortedObjects.size());
            std::sort(
                sortedObjects.begin(), sortedObjects.end(),
                [axis0](const WorkingObject& lhs, const WorkingObject&rhs)
                { return (lhs._boundary.first[axis0] + lhs._boundary.second[axis0]) < (rhs._boundary.first[axis0] + rhs._boundary.second[axis0]); });

            bestDividingLine[axis0] = .5f * (sortedObjects[objCount/2]._boundary.first[axis0] + sortedObjects[objCount/2]._boundary.second[axis0]);

                //  Attempt to improve the subdivision by looking for dividing lines
                //  that minimize the number of objects straddling this line. This will
                //  help try to avoid overlap between child nodes.
            {
                auto testCount = std::max(size_t(1), objCount/size_t(4));

                    //  Attempt to optimise X dividing line
                    //  Our optimised dividing line should always be on one
                    //  of the edges of the bounding box of one of the objects.

                unsigned minStradingCount = DividingLineMetrics(bestDividingLine[axis0], sortedObjects, axis0)._straddleCount;
                float minDivLineX = LinearInterpolate(newNode._boundary.first[axis0], newNode._boundary.second[axis0], 0.f); // 0.25f);
                float maxDivLineX = LinearInterpolate(newNode._boundary.first[axis0], newNode._boundary.second[axis0], 1.f); // 0.75f);

                    // todo -- rather than starting at the object that has an equal number
                    //  of objects on each side, we could consider starting on the object that
                    //  is closest to the center of the bounding box
                for (unsigned c=0; c<testCount && minStradingCount; ++c) {
                    unsigned o;
                    if (c & 1)  { o = objCount/2 - ((c+1)>>1); }
                    else        { o = objCount/2 + ((c+1)>>1); }

                        //  We need to test both the left and right edges of the
                        //  this object's bounding box.
                    float testLine = sortedObjects[o]._boundary.first[axis0];
                    if (testLine >= minDivLineX && testLine <= maxDivLineX) {
                        auto metrics = DividingLineMetrics(testLine, sortedObjects, axis0);
                        if (metrics._straddleCount < minStradingCount && metrics._leftCount && metrics._rightCount) {
                            bestDividingLine[axis0] = testLine;
                            minStradingCount = metrics._straddleCount;
                        }
                    }

                    testLine = sortedObjects[o]._boundary.second[axis0];
                    if (testLine >= minDivLineX && testLine <= maxDivLineX) {
                        auto metrics = DividingLineMetrics(testLine, sortedObjects, axis0);
                        if (metrics._straddleCount < minStradingCount && metrics._leftCount && metrics._rightCount) {
                            bestDividingLine[axis0] = testLine;
                            minStradingCount = metrics._straddleCount;
                        }
                    }
                }

                straddleCount[axis0] = minStradingCount;
            }

            std::sort(
                sortedObjects.begin(), sortedObjects.end(),
                [axis1](const WorkingObject& lhs, const WorkingObject&rhs)
                { return (lhs._boundary.first[axis1] + lhs._boundary.second[axis1]) < (rhs._boundary.first[axis1] + rhs._boundary.second[axis1]); });

            bestDividingLine[axis1] = .5f * (sortedObjects[objCount/2]._boundary.first[axis1] + sortedObjects[objCount/2]._boundary.second[axis1]);

            {
                auto testCount = std::max(size_t(1), objCount/size_t(4));

                    //  Attempt to optimise Y dividing line

                unsigned minStradingCount = DividingLineMetrics(bestDividingLine[axis1], sortedObjects, axis1)._straddleCount;
                float minDivLineY = LinearInterpolate(newNode._boundary.first[axis1], newNode._boundary.second[axis1], 0.f); // 0.25f);
                float maxDivLineY = LinearInterpolate(newNode._boundary.first[axis1], newNode._boundary.second[axis1], 1.f); // 0.75f);

                for (unsigned c=0; c<testCount && minStradingCount; ++c) {
                    unsigned o;
                    if (c & 1)  { o = objCount/2 - ((c+1)>>1); }
                    else        { o = objCount/2 + ((c+1)>>1); }

                    float testLine = sortedObjects[o]._boundary.first[axis1];
                    if (testLine >= minDivLineY && testLine <= maxDivLineY) {
                        auto metrics = DividingLineMetrics(testLine, sortedObjects, axis1);
                        if (metrics._straddleCount < minStradingCount && metrics._leftCount && metrics._rightCount) {
                            bestDividingLine[axis1] = testLine;
                            minStradingCount = metrics._straddleCount;
                        }
                    }

                    testLine = sortedObjects[o]._boundary.second[axis1];
                    if (testLine >= minDivLineY && testLine <= maxDivLineY) {
                        auto metrics = DividingLineMetrics(testLine, sortedObjects, axis1);
                        if (metrics._straddleCount < minStradingCount && metrics._leftCount && metrics._rightCount) {
                            bestDividingLine[axis1] = testLine;
                            minStradingCount = metrics._straddleCount;
                        }
                    }
                }

                straddleCount[axis1] = minStradingCount;
            }

            std::sort(
                sortedObjects.begin(), sortedObjects.end(),
                [axis2](const WorkingObject& lhs, const WorkingObject&rhs)
                { return (lhs._boundary.first[axis2] + lhs._boundary.second[axis2]) < (rhs._boundary.first[axis2] + rhs._boundary.second[axis2]); });

            bestDividingLine[axis2] = .5f * (sortedObjects[objCount/2]._boundary.first[axis2] + sortedObjects[objCount/2]._boundary.second[axis2]);

            {
                auto testCount = std::max(size_t(1), objCount/size_t(4));

                    //  Attempt to optimise Y dividing line

                unsigned minStradingCount = DividingLineMetrics(bestDividingLine[axis2], sortedObjects, axis2)._straddleCount;
                float minDivLineZ = LinearInterpolate(newNode._boundary.first[axis2], newNode._boundary.second[axis2], 0.f); // 0.25f);
                float maxDivLineZ = LinearInterpolate(newNode._boundary.first[axis2], newNode._boundary.second[axis2], 1.f); // 0.75f);

                for (unsigned c=0; c<testCount && minStradingCount; ++c) {
                    unsigned o;
                    if (c & 1)  { o = objCount/2 - ((c+1)>>1); }
                    else        { o = objCount/2 + ((c+1)>>1); }

                    float testLine = sortedObjects[o]._boundary.first[axis2];
                    if (testLine >= minDivLineZ && testLine <= maxDivLineZ) {
                        auto metrics = DividingLineMetrics(testLine, sortedObjects, axis2);
                        if (metrics._straddleCount < minStradingCount && metrics._leftCount && metrics._rightCount) {
                            bestDividingLine[axis2] = testLine;
                            minStradingCount = metrics._straddleCount;
                        }
                    }

                    testLine = sortedObjects[o]._boundary.second[axis2];
                    if (testLine >= minDivLineZ && testLine <= maxDivLineZ) {
                        auto metrics = DividingLineMetrics(testLine, sortedObjects, axis2);
                        if (metrics._straddleCount < minStradingCount && metrics._leftCount && metrics._rightCount) {
                            bestDividingLine[axis2] = testLine;
                            minStradingCount = metrics._straddleCount;
                        }
                    }
                }

                straddleCount[axis2] = minStradingCount;
            }
        }

        
        unsigned splitAxis0 = axis0, splitAxis1 = axis1;
        if (straddleCount[axis0] > straddleCount[axis1]) {
            if (straddleCount[axis0] > straddleCount[axis2]) {
                splitAxis0 = axis2;
            } else {
                // Z rejected
            }
        } else if (straddleCount[axis1] > straddleCount[axis2]) {
            splitAxis1 = axis2;
        } else {
            // Z rejected
        }

            //  ok, now we have our dividing line. We an divide our objects up into 5 parts:
            //  4 children nodes, and the straddling nodes

        std::vector<WorkingObject> dividedObjects[5];
        for (auto i=workingObjects.cbegin(); i!=workingObjects.cend(); ++i) {

                /// \todo - if an object is large compared to the size of this
                ///         node's bounding box, then we should put it into
                ///         a forth "straddling" payload (rather than expanding one
                ///         of our children to almost the size of the parent node)

            unsigned index = 0;
            if (i->_boundary.first[splitAxis0] > bestDividingLine[splitAxis0])          { index |= 0x1; } 
            else if (i->_boundary.second[splitAxis0] < bestDividingLine[splitAxis0])    { index |= 0x0; } 
            else { 
                    //  try to put this object on the left or the right (based on where
                    //  the larger volume of this object is)
                if ((bestDividingLine[splitAxis0] - i->_boundary.first[splitAxis0]) > (i->_boundary.second[splitAxis0] - bestDividingLine[splitAxis0])) {
                    index |= 0x0;
                } else {
                    index |= 0x1;
                }
            }

            if (i->_boundary.first[splitAxis1] > bestDividingLine[splitAxis1])          { index |= 0x2; } 
            else if (i->_boundary.second[splitAxis1] < bestDividingLine[splitAxis1])    { index |= 0x0; } 
            else { 
                if ((bestDividingLine[splitAxis1] - i->_boundary.first[splitAxis1]) > (i->_boundary.second[splitAxis1] - bestDividingLine[splitAxis1])) {
                    index |= 0x0;
                } else {
                    index |= 0x2;
                }
            }

            dividedObjects[std::min(index, 4u)].push_back(*i);
        }

        // When there is a lot of overlap (or too few objects), we can choose to
        // merge children together.
        //   2+0 -> merged into 0
        //   3+1 -> merged into 1
        //   1+0 -> merged into 0
        //   3+2 -> merged into 2
        if ((dividedObjects[2].size() + dividedObjects[0].size()) <= leafThreshold) {
            if ((dividedObjects[1].size() + dividedObjects[0].size()) <= leafThreshold) {
                //  we can merge 2+0 or 1+0. Let's just do whichever ends up with a bounding
                //  box that has a smaller volume.
                auto merge20 = dividedObjects[2]; merge20.insert(merge20.begin(), dividedObjects[0].begin(), dividedObjects[0].end());
                auto merge10 = dividedObjects[1]; merge10.insert(merge10.begin(), dividedObjects[0].begin(), dividedObjects[0].end());
                if (Volume(CalculateBoundary(merge20)) < Volume(CalculateBoundary(merge10))) {
                    dividedObjects[0] = merge20;
                    dividedObjects[2].clear();
                } else {
                    dividedObjects[0] = merge10;
                    dividedObjects[1].clear();
                }
            } else {
                dividedObjects[0].insert(dividedObjects[0].begin(), dividedObjects[2].begin(), dividedObjects[2].end());
                dividedObjects[2].clear();
            }
        }

        if ((dividedObjects[3].size() + dividedObjects[1].size()) <= leafThreshold) {
            dividedObjects[1].insert(dividedObjects[1].begin(), dividedObjects[3].begin(), dividedObjects[3].end());
            dividedObjects[3].clear();
        }

        if ((dividedObjects[1].size() + dividedObjects[0].size()) <= leafThreshold) {
            dividedObjects[0].insert(dividedObjects[0].begin(), dividedObjects[1].begin(), dividedObjects[1].end());
            dividedObjects[1].clear();
        }

        if ((dividedObjects[3].size() + dividedObjects[2].size()) <= leafThreshold) {
            dividedObjects[2].insert(dividedObjects[2].begin(), dividedObjects[3].begin(), dividedObjects[3].end());
            dividedObjects[3].clear();
        }

		int emptyCount = 0;
		for (unsigned c=0; c<dimof(dividedObjects); ++c)
			emptyCount += int(dividedObjects[c].empty());

		// if all objects are going to the same child, then we have to make this a leaf, after all
		// (otherwise we would just end up with an infinite loop where all nodes attempt to push all
		// their children into the same child)
		if (emptyCount == 4 && dividedObjects[4].empty()) {
			for (unsigned c=0; c<4; ++c)
				if (!dividedObjects[c].empty()) {
					dividedObjects[4] = std::move(dividedObjects[c]);
					break;
				}
		}

        if (!dividedObjects[4].empty()) {
            Payload payload;
            InitPayload(payload, dividedObjects[4]);
            _payloads.push_back(std::move(payload));
            newNode._payloadID = unsigned(_payloads.size()-1);
        }

        assert(dividedObjects[0].size() + dividedObjects[1].size() + dividedObjects[2].size() + dividedObjects[3].size() + dividedObjects[4].size() == workingObjects.size());

        auto newNodeId = unsigned(_nodes.size());
        if (parent) {
            parent->_children[childIndex] = newNodeId;
        }
        _nodes.push_back(newNode);

            // now just push in the children
        for (unsigned c=0; c<4; ++c) {
            if (!dividedObjects[c].empty()) {
                PushNode(newNodeId, c, dividedObjects[c], leafThreshold, orientation);
            }
        }
    }

	const GenericQuadTree::Pimpl& GenericQuadTree::GetPimpl() const
	{
		return *(const GenericQuadTree::Pimpl*)::Assets::Block_GetFirstObject(_dataBlock.get());
	}

    bool GenericQuadTree::CalculateVisibleObjects(
        const Float4x4& cellToClipAligned, ClipSpaceType clipSpaceType,
        const BoundingBox objCellSpaceBoundingBoxes[], size_t objStride,
        unsigned visObjs[], unsigned& visObjsCount, unsigned visObjMaxCount,
        Metrics* metrics) const
    {
        visObjsCount = 0;
        assert((size_t(AsFloatArray(cellToClipAligned)) & 0xf) == 0);

        unsigned nodeAabbTestCount = 0, payloadAabbTestCount = 0;

		const auto& pimpl = GetPimpl();

            //  Traverse through the quad tree, and find do bounding box level 
            //  culling on each object
        static std::stack<unsigned> workingStack;
        static std::vector<unsigned> entirelyVisibleStack;
        assert(workingStack.empty() && entirelyVisibleStack.empty());
        workingStack.push(0);
        while (!workingStack.empty()) {
            auto nodeIndex = workingStack.top();
            workingStack.pop();
            
            auto& node = pimpl._nodes[nodeIndex];
            auto test = TestAABB_Aligned(cellToClipAligned, node._boundary.first, node._boundary.second, clipSpaceType);
            ++nodeAabbTestCount;
            if (test == CullTestResult::Culled) {
                continue;
            }

            if (test == CullTestResult::Within) {

                    //  this node and all children are "visible" without
                    //  any further culling tests
                entirelyVisibleStack.push_back(nodeIndex);

            } else {

                for (unsigned c=0; c<4; ++c)
                    if (node._children[c] < pimpl._nodes.size())
                        workingStack.push(node._children[c]);

                if (node._payloadID < pimpl._payloads.size()) {
                    auto& payload = pimpl._payloads[node._payloadID];
					if (objCellSpaceBoundingBoxes && payload._objects.size() > 1) {     // if only one object in the payload, assume that the node bounding test is a tight test for that object
						for (auto i=payload._objects.cbegin(); i!=payload._objects.cend(); ++i) {

								//  Test the "cell" space bounding box of the object itself
								//  This must be done inside of this function, we can't
								//  drop the responsibility to the caller. Because:
								//      * sometimes we can skip it entirely, when quad tree
								//          node bounding boxes are considered entirely within the frustum
								//      * it's best to reduce the result arrays to as small as
								//          possible (because the caller may need to sort them)

							const auto& boundary = *PtrAdd(objCellSpaceBoundingBoxes, (*i) * objStride);
							++payloadAabbTestCount;
							if (!CullAABB_Aligned(cellToClipAligned, boundary.first, boundary.second, clipSpaceType)) {
								if ((visObjsCount+1) > visObjMaxCount) {
									return false;
								}
								visObjs[visObjsCount++] = *i; 
							}
						}
					} else {
						if ((visObjsCount + payload._objects.size()) > visObjMaxCount) {
							return false;
						}

						for (auto i=payload._objects.cbegin(); i!=payload._objects.cend(); ++i) {
							visObjs[visObjsCount++] = *i; 
						}
					}
                }

            }
        }

            //  some nodes might be "entirely visible" -- ie, the bounding box is completely
            //  within the culling frustum. In these cases, we can skip the rest of the culling
            //  checks and just add these objects as visible
        for (unsigned c=0; c<entirelyVisibleStack.size(); ++c) {
            auto& node = pimpl._nodes[entirelyVisibleStack[c]];
            for (unsigned c=0; c<4; ++c)
                if (node._children[c] < pimpl._nodes.size())
                    entirelyVisibleStack.push_back(node._children[c]);

            if (node._payloadID < pimpl._payloads.size()) {
                auto& payload = pimpl._payloads[node._payloadID];

                if ((visObjsCount + payload._objects.size()) > visObjMaxCount) {
                    return false;
                }

                for (auto i=payload._objects.cbegin(); i!=payload._objects.cend(); ++i) {
                    visObjs[visObjsCount++] = *i; 
                }
            }
        }
        entirelyVisibleStack.clear();

        assert(visObjsCount <= visObjMaxCount);
        if (metrics) {
            metrics->_nodeAabbTestCount += nodeAabbTestCount; 
            metrics->_payloadAabbTestCount += payloadAabbTestCount;
        }

        return true;
    }

    bool GenericQuadTree::CalculateVisibleObjects(
        IteratorRange<const Float4x4*> cellToClipAligned, uint32_t viewMask, ClipSpaceType clipSpaceType,
        const BoundingBox objCellSpaceBoundingBoxes[], size_t objStride,
        std::pair<unsigned, uint32_t> visObjs[], unsigned& visObjsCount, unsigned visObjMaxCount,
        Metrics* metrics) const
    {
        visObjsCount = 0;
        assert((size_t(AsFloatArray(*cellToClipAligned.begin())) & 0xf) == 0);
        assert(cellToClipAligned.size() <= 32);

        unsigned nodeAabbTestCount = 0, payloadAabbTestCount = 0;

		const auto& pimpl = GetPimpl();

            //  Traverse through the quad tree, and find do bounding box level 
            //  culling on each object
        struct NodeEntry { unsigned _nodeIndex; uint32_t _partialInsideMask; uint32_t _entirelyInsideMask; };
        static std::stack<NodeEntry> workingStack;
        static std::vector<NodeEntry> payloadsToProcess;
        assert(workingStack.empty() && payloadsToProcess.empty());
        workingStack.push({0, viewMask, 0});
        while (!workingStack.empty()) {
            auto nodeIndex = workingStack.top();
            workingStack.pop();
            
            auto& node = pimpl._nodes[nodeIndex._nodeIndex];
            uint32_t partialInside = nodeIndex._partialInsideMask, entirelyInsideMask = nodeIndex._entirelyInsideMask;
            uint32_t partialIterator = partialInside;
            while (partialIterator) {
                unsigned viewIdx = xl_ctz4(partialIterator);
                partialIterator ^= 1u<<viewIdx;

                auto test = TestAABB_Aligned(cellToClipAligned[viewIdx], node._boundary.first, node._boundary.second, clipSpaceType);
                if (test == CullTestResult::Culled) {
                    partialInside ^= 1u<<viewIdx;
                } else if (test == CullTestResult::Within) {
                    partialInside ^= 1u<<viewIdx;
                    entirelyInsideMask |= 1u<<viewIdx;
                }
                ++nodeAabbTestCount;
            }

            if ((entirelyInsideMask|partialInside) != 0) {
                for (unsigned c=0; c<4; ++c)
                    if (node._children[c] < pimpl._nodes.size())
                        workingStack.push({node._children[c], partialInside, entirelyInsideMask});

                if (node._payloadID < pimpl._payloads.size())
                    payloadsToProcess.push_back({node._payloadID, partialInside, entirelyInsideMask});
            }
        }

        for (auto payloadIdx:payloadsToProcess) {
            auto& payload = pimpl._payloads[payloadIdx._nodeIndex];
            assert(payloadIdx._partialInsideMask | payloadIdx._entirelyInsideMask);
            if ((visObjsCount + payload._objects.size()) > visObjMaxCount)
                return false;

            if (objCellSpaceBoundingBoxes) {
                unsigned objectsVisible = 0;
                for (auto i=payload._objects.cbegin(); i!=payload._objects.cend(); ++i) {
                    const auto& boundary = *PtrAdd(objCellSpaceBoundingBoxes, (*i) * objStride);
                    uint32_t partialIterator = payloadIdx._partialInsideMask;
                    uint32_t partialInside = payloadIdx._partialInsideMask;
                    while (partialIterator) {
                        unsigned viewIdx = xl_ctz4(partialIterator);
                        partialIterator ^= 1u<<viewIdx;

                        // we might be able to get better performance with a single optimized function that does either multiple views
                        // or multiple bounding boxes all in one go 
                        auto test = TestAABB_Aligned(cellToClipAligned[viewIdx], boundary.first, boundary.second, clipSpaceType);
                        if (test == CullTestResult::Culled)
                            partialInside ^= 1u<<viewIdx;
                        ++payloadAabbTestCount;
                    }

                    if ((visObjsCount+1) > visObjMaxCount)
                        return false;
                    if (partialInside|payloadIdx._entirelyInsideMask) {
                        visObjs[visObjsCount++] = {*i, partialInside|payloadIdx._entirelyInsideMask};
                        ++objectsVisible;
                    }
                }
            } else {
                if ((visObjsCount + payload._objects.size()) > visObjMaxCount)
                    return false;
                for (auto i=payload._objects.cbegin(); i!=payload._objects.cend(); ++i)
                    visObjs[visObjsCount++] = {*i, payloadIdx._partialInsideMask|payloadIdx._entirelyInsideMask};
            }
        }
        payloadsToProcess.clear();

        assert(visObjsCount <= visObjMaxCount);
        if (metrics) {
            metrics->_nodeAabbTestCount += nodeAabbTestCount; 
            metrics->_payloadAabbTestCount += payloadAabbTestCount;
        }

        return true;
    }

	unsigned GenericQuadTree::GetMaxResults() const
    {
        return GetPimpl()._maxCullResults;
    }
    
    unsigned GenericQuadTree::GetNodeCount() const
    {
        return GetPimpl()._nodes.size();
    }

	std::vector<std::pair<Float3, Float3>> GenericQuadTree::GetNodeBoundingBoxes() const
	{
		std::vector<std::pair<Float3, Float3>> result;
		auto& nodes = GetPimpl()._nodes;
        for (const auto&n:nodes) result.push_back(n._boundary);
		return result;
	}

	auto GenericQuadTree::BuildQuadTree(
        const BoundingBox objCellSpaceBoundingBoxes[], size_t objStride,
        size_t objCount, unsigned leafThreshold,
		Orientation orientation) -> std::pair<DataBlock, size_t>
    {
            //  Find the minimum and maximum XY of the placements in "placements", and
            //  divide this space up into a quad tree (ignoring height)
            //
            //  Perhaps there are some cases where we might need to use an oct tree
            //  instead of a quad tree? What about buildings with multiple floors?
            //  can we intelligently detect where an oct-tree is required, and where it 
            //  should just be a quad tree?
            //
            //  Ideally we want to support input data that can have either a world space
            //  bounding box or a local space bounding box (or perhaps even other bounding
            //  primitives?)

        std::vector<Pimpl::WorkingObject> workingObjects;
        workingObjects.reserve(objCount);

        for (unsigned c=0; c<objCount; ++c) {
            auto& objBoundary = *PtrAdd(objCellSpaceBoundingBoxes, c * objStride);
            Pimpl::WorkingObject o;
            o._boundary = objBoundary;
            o._id = c;
            workingObjects.push_back(o);
        }

            //  we need to filter each object into nodes as we iterate through the tree
            //  once we have a fixed number of objects a given node, we can make that node
            //  a leaf. Objects should be placed into the smallest node that contains them
            //  completely. We want to avoid cases where objects end up in the dividing line
            //  between nodes. So we'll use a system that adjusts the bounding box of each
            //  node based on the objects assigned to it.

        auto pimpl = std::make_unique<Pimpl>();
        pimpl->PushNode(~unsigned(0x0), 0, workingObjects, leafThreshold, orientation);
        pimpl->_maxCullResults = pimpl->CalculateMaxResults();

        ::Assets::NascentBlockSerializer serializer;
		SerializationOperator(serializer, *pimpl);
		return std::make_pair(std::move(serializer.AsMemoryBlock()), serializer.Size());
    }

    void GenericQuadTree::Pimpl::SerializeNode(std::ostream&str, unsigned nodeIdx) const
    {
        const auto& node = _nodes[nodeIdx];
        if (node._payloadID != ~0u) {
            str << StreamIndent(node._treeDepth*4) << "Node " << nodeIdx << ". Payload: [";
            for (auto p=_payloads[node._payloadID]._objects.begin(); p!=_payloads[node._payloadID]._objects.end(); ++p) {
                if (p != _payloads[node._payloadID]._objects.begin())
                    str << ", ";
                str << *p;
            }
            str << "]" << std::endl;
        } else {
            str << StreamIndent(node._treeDepth*4) << "Node " << nodeIdx << ". No payload" << std::endl;
        }
        for (unsigned c=0; c<4; ++c)
            if (node._children[c] != ~0u)
                SerializeNode(str, node._children[c]);
    }

    std::ostream& GenericQuadTree::SerializeMethod(std::ostream& str) const
    {
        const auto& pimpl = GetPimpl();
        if (pimpl._nodes.empty()) return str;

        pimpl.SerializeNode(str, 0);
        return str;
    }

	static const uint64 ChunkType_QuadTree = ConstHash64<'Quad', 'Tree'>::Value;
	static const unsigned QuadTreeDataVersion = 0;

	static const ::Assets::ArtifactRequest QuadTreeChunkRequests[]
    {
        ::Assets::ArtifactRequest { "QuadTree", ChunkType_QuadTree, QuadTreeDataVersion, ::Assets::ArtifactRequest::DataType::BlockSerializer },
    };

	GenericQuadTree::GenericQuadTree(GenericQuadTree&& moveFrom)
	: _dataBlock(std::move(moveFrom._dataBlock))
	, _depVal(std::move(moveFrom._depVal))
	{}

	GenericQuadTree& GenericQuadTree::operator=(GenericQuadTree&& moveFrom) never_throws
	{
		_dataBlock = std::move(moveFrom._dataBlock);
		_depVal = std::move(moveFrom._depVal);
		return *this;
	}

	GenericQuadTree::GenericQuadTree(const ::Assets::ChunkFileContainer& chunkFile) 
	: _depVal(chunkFile.GetDependencyValidation())
    {
        auto chunks = chunkFile.ResolveRequests(MakeIteratorRange(QuadTreeChunkRequests));
		assert(chunks.size() == 1);
		_dataBlock = std::move(chunks[0]._buffer);
	}

	GenericQuadTree::GenericQuadTree(std::unique_ptr<uint8[], PODAlignedDeletor>&& dataBlock)
	: _dataBlock(std::move(dataBlock))
	{
	}

	GenericQuadTree::GenericQuadTree() {}
	GenericQuadTree::~GenericQuadTree() {}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

#include "../RenderOverlays/IOverlayContext.h"
#include "../RenderOverlays/DebuggingDisplay.h"

namespace SceneEngine
{
    class QuadTreeDisplay : public RenderOverlays::DebuggingDisplay::IWidget
    {
    public:
        static void DrawQuadTree(
            RenderOverlays::IOverlayContext& context,
            const GenericQuadTree& qt,
            const Float3x4& localToWorld,
            int treeDepthFilter = -1)
        {
            using namespace RenderOverlays;
            using namespace RenderOverlays::DebuggingDisplay;
            ColorB cols[]= {
                ColorB(196, 230, 230),
                ColorB(255, 128, 128),
                ColorB(128, 255, 128),
                ColorB(128, 128, 255),
                ColorB(255, 255, 128)
            };

            auto& nodes = qt.GetPimpl()._nodes;
            for (auto n=nodes.cbegin(); n!=nodes.cend(); ++n) {
                if (treeDepthFilter < 0 || signed(n->_treeDepth) == treeDepthFilter) {
                    DrawBoundingBox(
                        &context, n->_boundary, localToWorld,
                        cols[std::min((unsigned)dimof(cols), n->_treeDepth)], 0x1);
                }
            }
            for (auto n=nodes.cbegin(); n!=nodes.cend(); ++n) {
                if (treeDepthFilter < 0 || signed(n->_treeDepth) == treeDepthFilter) {
                    DrawBoundingBox(
                        &context, n->_boundary, localToWorld,
                        cols[std::min((unsigned)dimof(cols), n->_treeDepth)], 0x2);
                }
            }
        }

        void    Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState)
        {
            DrawQuadTree(context, *_quadTree, _localToWorld);
        }

        QuadTreeDisplay(std::shared_ptr<GenericQuadTree> quadTree, const Float3x4& localToWorld) : _quadTree(quadTree), _localToWorld(localToWorld) {}
        std::shared_ptr<GenericQuadTree> _quadTree;
        Float3x4 _localToWorld;
    };

    std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateQuadTreeDisplay(
        std::shared_ptr<GenericQuadTree> qt,
        const Float3x4& localToWorld)
    {
        return std::make_shared<QuadTreeDisplay>(qt, localToWorld);
    }

    class BoundingBoxDisplay : public RenderOverlays::DebuggingDisplay::IWidget
    {
    public:
        void    Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState)
        {
            using namespace RenderOverlays;
            using namespace RenderOverlays::DebuggingDisplay;
            ColorB cols[]= {
                ColorB(196, 230, 230),
                ColorB(255, 128, 128),
                ColorB(128, 255, 128),
                ColorB(128, 128, 255),
                ColorB(255, 255, 128)
            };

            for (unsigned c=0; c<_boundingBoxes.size(); ++c)
                DrawBoundingBox(
                    &context, _boundingBoxes[c], _localToWorld,
                    cols[c%dimof(cols)], 0x1);

            for (unsigned c=0; c<_boundingBoxes.size(); ++c)
                DrawBoundingBox(
                    &context, _boundingBoxes[c], _localToWorld,
                    cols[c%dimof(cols)], 0x2);
        }

        BoundingBoxDisplay(const GenericQuadTree::BoundingBox objCellSpaceBoundingBoxes[], size_t objStride, size_t objCount, const Float3x4& localToWorld)
        : _localToWorld(localToWorld)
        {
            _boundingBoxes.reserve(objCount);
            for (unsigned c=0; c<objCount; ++c)
                _boundingBoxes.push_back(*PtrAdd(objCellSpaceBoundingBoxes, c*objStride));
        }
        std::vector<GenericQuadTree::BoundingBox> _boundingBoxes;
        Float3x4 _localToWorld;
    };

    std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateBoundingBoxDisplay(
        const GenericQuadTree::BoundingBox objCellSpaceBoundingBoxes[], size_t objStride, size_t objCount,
        const Float3x4& cellToWorld)
    {
        return std::make_shared<BoundingBoxDisplay>(objCellSpaceBoundingBoxes, objStride, objCount, cellToWorld);
    }
}

#if 0
    void    PlacementsQuadTreeDebugger::Render(
        RenderOverlays::IOverlayContext& context, Layout& layout, 
        Interactables& interactables, InterfaceState& interfaceState)
    {
        RenderCore::Metal::DeviceContext::Get(*context.GetDeviceContext())->Bind(Techniques::CommonResources()._dssDisable);
        static signed treeDepthFilter = -1;
        static bool drawObjects = false;

        using namespace RenderOverlays;
        using namespace RenderOverlays::DebuggingDisplay;

        ColorB cols[]= {
            ColorB(196, 230, 230),
            ColorB(255, 128, 128),
            ColorB(128, 255, 128),
            ColorB(128, 128, 255),
            ColorB(255, 255, 128)
        };

        if (!drawObjects) {
                // Find all of the quad trees that are used by the manager for
                // the current camera position. For each quad-tree we find, let's
                // render some debugging information (including bounding boxes for
                // the nodes in the quad tree).
            auto quadTrees = _placementsManager->GetRenderer()->GetVisibleQuadTrees(
                *_cells, context.GetProjectionDesc()._worldToProjection);
            for (auto i=quadTrees.cbegin(); i!=quadTrees.cend(); ++i) {
                auto cellToWorld = i->first;
                auto quadTree = i->second;
                if (!quadTree) continue;
                QuadTreeDisplay::DrawQuadTree(context, *quadTree, cellToWorld, treeDepthFilter);
            }
        } else {
            auto cells = _placementsManager->GetRenderer()->GetObjectBoundingBoxes(*_cells, context.GetProjectionDesc()._worldToProjection);
            for (auto c=cells.cbegin(); c!=cells.cend(); ++c) {
                auto cellToWorld = c->first;
                auto objs = c->second;

                for (unsigned c=0; c<objs._count; ++c) {
                    auto& boundary = *PtrAdd(objs._boundingBox, c*objs._stride);
                    DrawBoundingBox(&context, boundary, cellToWorld, cols[0], 0x1);
                }
                for (unsigned c=0; c<objs._count; ++c) {
                    auto& boundary = *PtrAdd(objs._boundingBox, c*objs._stride);
                    DrawBoundingBox(&context, boundary, cellToWorld, cols[0], 0x2);
                }
            }
        }
    }

    bool    PlacementsQuadTreeDebugger::ProcessInput(
        InterfaceState& interfaceState, const InputSnapshot& input)
    {
        return false;
    }

    PlacementsQuadTreeDebugger::PlacementsQuadTreeDebugger(
        std::shared_ptr<PlacementsManager> placementsManager, 
        std::shared_ptr<PlacementCellSet> cells)
    : _placementsManager(placementsManager)
    , _cells(cells)
    {}

    PlacementsQuadTreeDebugger::~PlacementsQuadTreeDebugger()
    {}

}
#endif
