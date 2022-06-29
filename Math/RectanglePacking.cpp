// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RectanglePacking.h"
#include "../Utility/IteratorUtils.h"
#include <limits>

namespace XLEMath
{
    static unsigned Width(std::pair<UInt2, UInt2> rect)   { return rect.second[0] - rect.first[0]; }
    static unsigned Height(std::pair<UInt2, UInt2> rect)  { return rect.second[1] - rect.first[1]; }

    const auto s_emptyRect = std::make_pair(UInt2(0,0), UInt2(0,0));

    auto RectanglePacker::Allocate(UInt2 dims) -> Rectangle
    {
        auto i = SearchNodes(0, dims);
        if (i != s_invalidNode) {
            auto& n = _nodes[i];
            assert(!n.IsAllocated());
            n._children = _nodes.size();

            auto result = std::make_pair(n._space.first, UInt2(n._space.first + dims));

                // note that the order we push the children in here will determine
                // how they are searched. We will push in the "right" space first,
                // and then the "down" space.
            const bool alternateDivision = false;
            if ((n._depth & 1) || !constant_expression<alternateDivision>::result()) {
                auto rightSpace = std::make_pair(
                    UInt2(n._space.first[0] + dims[0], n._space.first[1]),
                    UInt2(n._space.second[0], n._space.first[1] + dims[1]));
                auto downSpace = std::make_pair(
                    UInt2(n._space.first[0], n._space.first[1] + dims[1]),
                    UInt2(n._space.second[0], n._space.second[1]));
                _nodes.push_back(Node { rightSpace, s_invalidNode, n._depth+1 });
                _nodes.push_back(Node { downSpace, s_invalidNode, n._depth+1 });
            } else {
                auto rightSpace = std::make_pair(
                    UInt2(n._space.first[0] + dims[0], n._space.first[1]),
                    UInt2(n._space.second[0], n._space.second[1]));
                auto downSpace = std::make_pair(
                    UInt2(n._space.first[0], n._space.first[1] + dims[1]),
                    UInt2(n._space.first[0] + dims[0], n._space.second[1]));
                _nodes.push_back(Node { downSpace, s_invalidNode, n._depth+1 });
                _nodes.push_back(Node { rightSpace, s_invalidNode, n._depth+1 });
            }

            return result;
        }

        return s_emptyRect;
    }

    size_t RectanglePacker::SearchNodes(size_t startingNode, UInt2 dims) const
    {
        assert(startingNode < _nodes.size());
        const auto& n = _nodes[startingNode];
        if (n.IsAllocated()) {
            auto result = SearchNodes(n._children, dims);
            if (result == s_invalidNode)
                result = SearchNodes(n._children+1, dims);
            return result;
        } else if (dims[0] <= Width(n._space) && dims[1] <= Height(n._space)) {
            return startingNode;
        } else 
            return s_invalidNode;
    }

    static std::pair<UInt2, UInt2> LargestArea(
        std::pair<UInt2, UInt2>& lhs, std::pair<UInt2, UInt2>& rhs)
    {
        auto a0 = (lhs.second[0] - lhs.first[0]) * (lhs.second[1] - lhs.first[1]);
        auto a1 = (rhs.second[0] - rhs.first[0]) * (rhs.second[1] - rhs.first[1]);
        if (a0 >= a1) return lhs;
        return rhs;
    }

    static std::pair<UInt2, UInt2> LargestSide(
        std::pair<UInt2, UInt2>& lhs, std::pair<UInt2, UInt2>& rhs)
    {
        auto a0 = std::max(lhs.second[0] - lhs.first[0], lhs.second[1] - lhs.first[1]);
        auto a1 = std::max(rhs.second[0] - rhs.first[0], rhs.second[1] - rhs.first[1]);
        if (a0 >= a1) return lhs;
        return rhs;
    }

    auto RectanglePacker::SearchLargestFree(size_t startingNode) const 
        -> std::pair<Rectangle, Rectangle>
    {
        assert(startingNode < _nodes.size());
        const auto& n = _nodes[startingNode];
        if (!n.IsAllocated()) {
            auto area = (n._space.second[0] - n._space.first[0]) * (n._space.second[1] - n._space.first[1]);
            if (area > 0)   return std::make_pair(n._space, n._space);
            else            return std::make_pair(s_emptyRect, s_emptyRect);
        }

        auto c0 = SearchLargestFree(n._children);
        auto c1 = SearchLargestFree(n._children+1);
        return std::make_pair(
            LargestArea(c0.first, c1.first),
            LargestSide(c0.second, c1.second));
    }

    std::pair<UInt2, UInt2> RectanglePacker::LargestFreeBlock() const
    {
        if (_nodes.empty()) { return std::make_pair(UInt2(0,0), UInt2(0,0)); }

        auto search = SearchLargestFree(0);
        return std::make_pair(
            search.first.second - search.first.first,
            search.second.second - search.second.first);
    }

    RectanglePacker::RectanglePacker(const UInt2 dimensions)
    {
        _nodes.reserve(128);
        _nodes.push_back(
            Node { std::make_pair(UInt2(0,0), dimensions), s_invalidNode, 0 });
        _totalSize = dimensions;
    }

    RectanglePacker::~RectanglePacker() {}

    RectanglePacker::RectanglePacker(RectanglePacker&& moveFrom) never_throws
    : _nodes(std::move(moveFrom._nodes))
    , _totalSize(moveFrom._totalSize)
    {}
     
    RectanglePacker& RectanglePacker::operator=(RectanglePacker&& moveFrom) never_throws
    {
        _nodes = std::move(moveFrom._nodes);
        _totalSize = moveFrom._totalSize;
        return *this;
    }

    RectanglePacker::RectanglePacker(const RectanglePacker& cloneFrom)
    : _nodes(cloneFrom._nodes)
    , _totalSize(cloneFrom._totalSize)
    {}

    RectanglePacker& RectanglePacker::operator=(const RectanglePacker& cloneFrom)
    {
        _nodes = cloneFrom._nodes;
        _totalSize = cloneFrom._totalSize;
        return *this;
    }

    RectanglePacker::RectanglePacker() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    static int Score(const std::pair<UInt2, UInt2>& rect, UInt2 dims)
    {
        auto width = rect.second[0] - rect.first[0];
        auto height = rect.second[1] - rect.first[1];
        // if (dims[0] > width || dims[1] > height) return -1; (similar result is achieved by the below equation)
        return std::min(int(width) - int(dims[0]), int(height) - int(dims[1]));
    }

    static bool Intersects(std::pair<UInt2, UInt2>& lhs, std::pair<UInt2, UInt2>& rhs)
    {
        return 
            !(  lhs.second[0] <= rhs.first[0]
            ||  lhs.second[1] <= rhs.first[1]
            ||  lhs.first[0] >= rhs.second[0]
            ||  lhs.first[1] >= rhs.second[1]);
    }

    static bool Contains(
        const std::pair<UInt2, UInt2>& bigger, 
        const std::pair<UInt2, UInt2>& smaller)
    {
        return
            (   smaller.first[0]  >= bigger.first[0]
            &&  smaller.first[1]  >= bigger.first[1]
            &&  smaller.second[0] <= bigger.second[0]
            &&  smaller.second[1] <= bigger.second[1]);
    }

    static bool IsGood(const std::pair<UInt2, UInt2>& rect)
    {
        return (rect.second[0] > rect.first[0]) && (rect.second[1] > rect.first[1]);
    }

    auto    RectanglePacker_MaxRects::PreviewAllocation(UInt2 dims) -> PreviewedAllocation
    {
        assert(dims[0] != 0 && dims[1] != 0);

            // Search through to find the best free rectangle that can contain
            // this dimension.
            // As described here -- http://clb.demon.fi/files/RectangleBinPack.pdf 
            //      -- there are a number of different predicates we can used to 
            //      determine which free rectangle is ideal.

        auto best0 = _freeRectangles.end();
        auto best0Score = std::numeric_limits<int>::max();
        auto best1 = _freeRectangles.end();
        auto best1Score = std::numeric_limits<int>::max();

        /*UInt2 flippedDims(dims[1], dims[0]);
        for (auto i=_freeRectangles.begin(); i!=_freeRectangles.end(); ++i) {
            auto score0 = Score(*i, dims);
            if (score0 >= 0 && score0 < best0Score) {
                best0Score = score0;
                best0 = i;
            }

            auto score1 = Score(*i, flippedDims);
            if (score1 >= 0 && score1 < best1Score) {
                best1Score = score1;
                best1 = i;
            }
        }*/
        auto wi = std::lower_bound(_freeRectsByWidth.begin(), _freeRectsByWidth.end(), dims[0], CompareFirst<unsigned, unsigned>());
        for (; wi != _freeRectsByWidth.end(); ++wi) {
            auto score0 = Score(_freeRectangles[wi->second], dims);
            if (score0 >= 0 && score0 < best0Score) {
                best0Score = score0;
                best0 = _freeRectangles.begin() + wi->second;
            }
        }

        auto hi = std::lower_bound(_freeRectsByHeight.begin(), _freeRectsByHeight.end(), dims[1], CompareFirst<unsigned, unsigned>());
        for (; hi != _freeRectsByHeight.end(); ++hi) {
            auto score0 = Score(_freeRectangles[hi->second], dims);
            if (score0 >= 0 && score0 < best0Score) {
                best0Score = score0;
                best0 = _freeRectangles.begin() + hi->second;
            }
        }

        const bool allowFlipped = false;
        if (constant_expression<!allowFlipped>::result()) {
            best1 = _freeRectangles.end();
            best1Score = std::numeric_limits<int>::max();
        }

        if (best0 == _freeRectangles.end() && best1 == _freeRectangles.end())
            return {s_emptyRect, std::numeric_limits<int>::max()}; // couldn't fit it in!

        PreviewedAllocation result;
        if (best0Score <= best1Score) {
                // fit it within the top-left of the given space
            result._rectangle.first = best0->first;
            result._rectangle.second = result._rectangle.first + dims;
            result._score = best0Score;
            assert(Contains(*best0, result._rectangle));
        } /*else {
            result.first = best1->first;
            result.second = result.first + flippedDims;
            assert(Contains(*best1, result));
        }*/
        return result;
    }

    void RectanglePacker_MaxRects::Allocate(PreviewedAllocation allocation)
    {
            // Go through every free rectangle and split every rectangle that
            // intersects with the one we just cut out. We will build the
            // "maximal rectangle" created by the split
        for (auto i=_freeRectangles.begin(); i!=_freeRectangles.end();) {
            if (Intersects(allocation._rectangle, *i)) {
                Rectangle splits[4];
                unsigned splitsCount = 0;

                Rectangle left(i->first, UInt2(allocation._rectangle.first[0], i->second[1]));
                Rectangle right(UInt2(allocation._rectangle.second[0], i->first[1]), i->second);
                Rectangle top(i->first, UInt2(i->second[0], allocation._rectangle.first[1]));
                Rectangle bottom(UInt2(i->first[0], allocation._rectangle.second[1]), i->second);

                if (IsGood(left))   splits[splitsCount++] = left;
                if (IsGood(right))  splits[splitsCount++] = right;
                if (IsGood(top))    splits[splitsCount++] = top;
                if (IsGood(bottom)) splits[splitsCount++] = bottom;

                #if defined(_DEBUG)
                    for (unsigned c=0; c<splitsCount; ++c)
                        assert(!Intersects(allocation._rectangle, splits[c]));
                #endif

                if (splitsCount > 0) {
                    auto originalIndex = std::distance(_freeRectangles.begin(), i);
                    *i = splits[0];
                    _freeRectangles.insert(_freeRectangles.end(), &splits[1], &splits[splitsCount]);
                    i = _freeRectangles.begin() + (originalIndex+1);
                } else {
                    i = _freeRectangles.erase(i);
                }
            } else 
                ++i;
        }

            // We need to remove rectangles that are completely contained within other
            // rectangles. Go through and search for any cases like this. Note that it
            // might be more efficient to sort the list before doing this.
        for (auto i=_freeRectangles.begin(); i!=_freeRectangles.end(); ++i) {
            const auto r = *i;
            (void)r;
            auto newEnd = _freeRectangles.end();
            for (auto i2=i+1; i2<newEnd; ++i2) {
                assert(i!=i2);
                if (Contains(*i, *i2)) {
                    std::swap(*i2, *(newEnd-1));
                    --newEnd;
                } else if (Contains(*i2, *i)) {
                        // i2 is bigger; therefore keep i2 and erase i.
                    std::swap(*i2, *i);
                    std::swap(*i2, *(newEnd-1));
                    --newEnd;
                        // But we need to compare i2 against all of the
                        // rectangles that preceed it... That means reset
                        // i2 to the start of the list of rectangles
                    i2 = i;
                }
            }

                // do an erase to remove the rectangles that were
                // shifted to the end. This should not invalidate 'i'
            _freeRectangles.erase(newEnd, _freeRectangles.end());
        }

        #if defined(_DEBUG)
            for (auto i=_freeRectangles.begin(); i!=_freeRectangles.end();++i)
                assert(!Intersects(allocation._rectangle, *i));
        #endif

        RebuildFreeRects();
    }

    auto RectanglePacker_MaxRects::Allocate(UInt2 dims) -> Rectangle
    {
        assert(dims[0] != 0 && dims[1] != 0);
        auto preview = PreviewAllocation(dims);
        if (preview._rectangle.first[0] != preview._rectangle.second[0])
            Allocate(preview);
        return preview._rectangle;
    }

    void    RectanglePacker_MaxRects::Deallocate(const Rectangle& iRect)
    {
        if (IsGood(iRect)) {

                // We should expand this rectangle when it shares a boundary with any
                // other free rectangles.
                // Note that we can expand in 2 ways -- top & bottom, or left & right.
                // So in some cases, we need to add 2 new rects (one for expansion in
                // each direction)
            auto xRect = iRect; // (expand in +/- x direction)
            auto yRect = iRect; // (expand in +/- y direction)
            bool xExpand = false, yExpand = false;

            for (auto i=_freeRectangles.begin(); i!=_freeRectangles.end(); ++i) {
                if (i->second[0] == xRect.first[0]) {
                        // i is on the left of xRect. If the full edge is shared, then expand...
                    if (i->first[1] <= xRect.first[1] && i->second[1] >= xRect.second[1]) {
                        xRect.first[0] = i->first[0];
                        xExpand = true;
                    }
                }

                if (i->first[0] == xRect.second[0]) {
                        // i is on the right of xRect. If the full edge is shared, then expand...
                    if (i->first[1] <= xRect.first[1] && i->second[1] >= xRect.second[1]) {
                        xRect.second[0] = i->second[0];
                        xExpand = true;
                    }
                }

                if (i->second[1] == yRect.first[1]) {
                        // i is on the top of yRect. If the full edge is shared, then expand...
                    if (i->first[0] <= yRect.first[0] && i->second[0] >= yRect.second[0]) {
                        yRect.first[1] = i->first[1];
                        yExpand = true;
                    }
                }

                if (i->first[1] == yRect.second[1]) {
                        // i is on the bottom of yRect. If the full edge is shared, then expand...
                    if (i->first[0] <= yRect.first[0] && i->second[0] >= yRect.second[0]) {
                        yRect.second[1] = i->second[1];
                        yExpand = true;
                    }
                }
            }

            if (xExpand) {
                    // remove any rectangles that are fully contained within xRect first
                _freeRectangles.erase(
                    std::remove_if(
                        _freeRectangles.begin(), _freeRectangles.end(),
                        [&xRect](const Rectangle& r) { return Contains(xRect, r); }),
                    _freeRectangles.end());
                _freeRectangles.push_back(xRect);
            }
            if (yExpand) {
                   // remove any rectangles that are fully contained within yRect first
                _freeRectangles.erase(
                    std::remove_if(
                        _freeRectangles.begin(), _freeRectangles.end(),
                        [&yRect](const Rectangle& r) { return Contains(yRect, r); }),
                    _freeRectangles.end());
                _freeRectangles.push_back(yRect);
            }
            if (!xExpand && !yExpand) {
                    // unexpanded rects should not contain any other rects
                _freeRectangles.push_back(iRect);
            }

            RebuildFreeRects();
        }
    }

    void RectanglePacker_MaxRects::RebuildFreeRects()
    {
        _freeRectsByWidth.clear();
        _freeRectsByHeight.clear();

        _freeRectsByWidth.reserve(_freeRectangles.size());
        for (unsigned c=0; c<_freeRectangles.size(); ++c)
            _freeRectsByWidth.push_back({_freeRectangles[c].second[0] - _freeRectangles[c].first[0], c});

        _freeRectsByHeight.reserve(_freeRectangles.size());
        for (unsigned c=0; c<_freeRectangles.size(); ++c)
            _freeRectsByHeight.push_back({_freeRectangles[c].second[1] - _freeRectangles[c].first[1], c});

        std::sort(_freeRectsByWidth.begin(), _freeRectsByWidth.end(), CompareFirst<unsigned, unsigned>());
        std::sort(_freeRectsByHeight.begin(), _freeRectsByHeight.end(), CompareFirst<unsigned, unsigned>());
    }

    std::pair<UInt2, UInt2> RectanglePacker_MaxRects::LargestFreeBlock() const
    {
        UInt2 bestForArea(0, 0);
        UInt2 bestForSide(0, 0);
        unsigned bestArea = 0, bestSide = 0;
        for (auto i=_freeRectangles.begin(); i!=_freeRectangles.end();++i) {
            auto area = (i->second[0] - i->first[0]) * (i->second[1] - i->first[1]);
            if (area > bestArea) {
                bestForArea = i->second - i->first;
                bestArea = area;
            }
            auto side = std::max(i->second[0] - i->first[0], i->second[1] - i->first[1]);
            if (side > bestSide) {
                bestForSide = i->second - i->first;
                bestSide = side;
            }
        }
        return std::make_pair(bestForArea, bestForSide);
    }

    RectanglePacker_MaxRects::RectanglePacker_MaxRects() {}
    RectanglePacker_MaxRects::RectanglePacker_MaxRects(UInt2 initialSpace)
    {
        Deallocate(std::make_pair(UInt2(0,0), initialSpace));
    }
    RectanglePacker_MaxRects::RectanglePacker_MaxRects(RectanglePacker_MaxRects&& moveFrom) never_throws = default;
    RectanglePacker_MaxRects& RectanglePacker_MaxRects::operator=(RectanglePacker_MaxRects&& moveFrom) never_throws = default;
    RectanglePacker_MaxRects::~RectanglePacker_MaxRects() = default;
    RectanglePacker_MaxRects::RectanglePacker_MaxRects(const RectanglePacker_MaxRects& copyFrom) = default;
    RectanglePacker_MaxRects& RectanglePacker_MaxRects::operator=(const RectanglePacker_MaxRects& copyFrom) = default;

}

