// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SILENCE_CXX17_NEGATORS_DEPRECATION_WARNING

#include "TestDisplays.h"
#include "../../RenderOverlays/Font.h"
#include "../../RenderCore/Format.h"
#include "../../Math/Geometry.h"
#include "../../Math/Vector.h"
#include "../../Math/Noise.h"
#include "../../Math/RectanglePacking.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/BitUtils.h"
#include <random>

#pragma warning(disable:4714)
#pragma push_macro("new")
#undef new
#include <Eigen/Dense>
#pragma pop_macro("new")

#pragma warning(disable:4127)       // conditional expression is constant

namespace PlatformRig { namespace Overlays
{
    class CollectIntersectionPts
    {
    public:
        std::vector<Float2> _intr;

        void operator()(XLEMath::Int2 s0, XLEMath::Int2 s1, float edgeAlpha)
        {
            _intr.push_back(LinearInterpolate(Float2(s0), Float2(s1), edgeAlpha));
        }

        void operator()(XLEMath::Float2 s0, XLEMath::Float2 s1, float edgeAlpha)
        {
            _intr.push_back(LinearInterpolate(s0, s1, edgeAlpha));
        }
    };

    void    GridIteratorDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
    {
        auto maxSize = layout.GetMaximumSize();
        Rect gridArea(maxSize._topLeft + Coord2(50, 50), maxSize._bottomRight - Coord2(50, 50));
        
        const unsigned elementDim = 32;
        unsigned segmentsX = gridArea.Width() / elementDim;
        unsigned segmentsY = gridArea.Height() / elementDim;

        auto segsX = std::make_unique<Coord2[]>(segmentsX*2);
        auto segsY = std::make_unique<Coord2[]>(segmentsY*2);
        auto colours = std::make_unique<ColorB[]>(std::max(segmentsX, segmentsY)*2);
        for (unsigned c=0; c<segmentsX; ++c) {
            segsX[c*2+0] = Coord2(gridArea._topLeft[0] + c * elementDim, gridArea._topLeft[1]);
            segsX[c*2+1] = Coord2(gridArea._topLeft[0] + c * elementDim, gridArea._bottomRight[1]);
            colours[c*2+0] = ColorB(0xffafafaf);
            colours[c*2+1] = ColorB(0xffafafaf);
        }
        for (unsigned c=0; c<segmentsY; ++c) {
            segsY[c*2+0] = Coord2(gridArea._topLeft[0], gridArea._topLeft[1] + c * elementDim);
            segsY[c*2+1] = Coord2(gridArea._bottomRight[0], gridArea._topLeft[1] + c * elementDim);
            colours[c*2+0] = ColorB(0xffafafaf);
            colours[c*2+1] = ColorB(0xffafafaf);
        }

        DrawLines(context, segsX.get(), colours.get(), segmentsX);
        DrawLines(context, segsY.get(), colours.get(), segmentsY);

        Float2 start = Float2(float(gridArea._bottomRight[0] - gridArea._topLeft[0]), float(gridArea._bottomRight[1] - gridArea._topLeft[1])) / float(elementDim) / 2.f;
        Float2 end = Float2(float(_currentMousePosition[0] - gridArea._topLeft[0]), float(_currentMousePosition[1] - gridArea._topLeft[1])) / float(elementDim);

        // start[0] = XlFloor(start[0]);
        // start[1] = XlFloor(start[1]);
        // end[0] = XlFloor(end[0]);
        // end[1] = XlFloor(end[1]);

        Coord2 testLine[] = 
        {
            Coord2(int(start[0]*elementDim), int(start[1]*elementDim)) + gridArea._topLeft,
            Coord2(int(end[0]*elementDim), int(end[1]*elementDim)) + gridArea._topLeft,
        };
        ColorB testLineCol[] = { ColorB(0xffffffff), ColorB(0xffffffff) };
        DrawLines(context, testLine, testLineCol, 1);

        CollectIntersectionPts intersections;
        // GridEdgeIterator(Int2(start), Int2(end), intersections);
        GridEdgeIterator2(start, end, intersections);

        for (auto i=intersections._intr.cbegin(); i!=intersections._intr.cend(); ++i) {
            Coord2 centre(int((*i)[0] * elementDim) + gridArea._topLeft[0], int((*i)[1] * elementDim) + gridArea._topLeft[1]);
            Rect rect(centre - Coord2(2,2), centre + Coord2(2,2));
            OutlineEllipse(&context, rect, ColorB(0xffff0000));
        }
    }

    bool    GridIteratorDisplay::ProcessInput(InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input)
    {
        if (input.IsHeld_LButton()) {
            _currentMousePosition = input._mousePosition;
        }
        return false;
    }

    GridIteratorDisplay::GridIteratorDisplay() : _currentMousePosition(0,0) {}
    GridIteratorDisplay::~GridIteratorDisplay() {}



    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class FieldIntersection
    {
    public:
        Float2 _position, _normal;
        bool _isGood;
        FieldIntersection(Float2 position, Float2 normal) : _position(position), _normal(normal), _isGood(true) {}
        FieldIntersection() : _position(0.f, 0.f), _normal(0.f, 0.f), _isGood(false) {}
    };

    static std::pair<float, float> RayVsCircle(const Float2& rayStart, const Float2& rayDirection, const Float2& sphereCenter, float sphereRadius)
    {
            //  we're going to look for intersections between this ray and the sphere.
            //  there can be maximum of 2 intersections. 
            //  The return values are the length along the line to the intersection points.
            //  When no intersection is found (or only one) the unused return values will be negative).
            //  We solve this using algebra, looking for the points where the equations
            //  for the line and the ray agree.

        const float a = Dot(rayDirection, rayStart - sphereCenter);
        const float m = Magnitude(rayStart - sphereCenter);
        const float x = a*a - m*m + sphereRadius*sphereRadius;
        if (x < 0.f) {
            return std::make_pair(-1.f, -1.f);
        }

        const float A = -a;
        const float B = XlSqrt(x);
        return std::make_pair(A+B, A-B);
    }

    static float CalculateDensity(const Float2& pt)
    {
        const float circleRadius = 20.f;
        // const float theta = atan2(pt[1], pt[0]);
        const float r = Magnitude(pt);
        // static float s0 = 5.f;
        // static float s1 = 0.25f;
        // float noiseValue = SimplexNoise(Float2(s0 * theta, s1 * r));
        // 
        // static float noiseScale = 10.f;
        // return r - (circleRadius + noiseScale * noiseValue);
        return r - circleRadius;
    }

    static FieldIntersection GetIntersection(const Float2& e0, const Float2& e1)
    {
            //  For the given edge, find the intersection with the equation.
            //  We're just using a sphere, so it should be easy.
            //
            //  We're interested for the smallest positive value that come out of RayVsCircle

        const bool useDensitySample = true;

        if (useDensitySample) {

            float d0 = CalculateDensity(e0);
            float d1 = CalculateDensity(e1);
            if (d0 < 0.f == d1 < 0.f) {
                return FieldIntersection(); // no intersections here
            }

                //  -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

                //      When we get a change in sign, it means there
                //      is an intersection along this edge somewhere.
                //      We can't know exactly where. But we can take
                //      an estimate based on the density values.
                //
                //      It might be a good idea to further improve the
                //      result by taking a few steps to try to get to the
                //      smallest density value. note that a very strange
                //      density function might behave very non-linearly,
                //      and so produce strange results when trying to
                //      find the intersection (particularly if there are
                //      really multiple intersections).

                //  -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

            float x0 = 0.f, x1 = 1.f;
            float x = 1.f, d = std::numeric_limits<float>::max();
            unsigned improvementSteps = 3;
            for (unsigned c=0; ; ++c) {
                float prevD = d; float prevX = x;
                x = LinearInterpolate(x0, x1, -d0 / (d1 - d0));
                d = CalculateDensity(LinearInterpolate(e0, e1, x));

                    // along noisy edges we could end up getting a worse result after a step
                    //  In these cases, just give up at the last reasonable result
                if (XlAbs(d) > XlAbs(prevD)) {
                    x = prevX;
                    break;
                }

                if ((c+1)>=improvementSteps) break;

                    //  We're going to attempt another improvement.
                    //  Divide the search area again, depending on where
                    //  the origin falls
                if ((d < 0.f) != (d1 < 0.f)) {
                    x0 = x;
                    d0 = d;
                } else {
                    x1 = x;
                    d1 = d;
                }
            }

                //  -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
            
            Float2 bestIntersection = LinearInterpolate(e0, e1, x);

                //  We need to calculate the normal for this point. We can
                //  use finite different method for this. The normal is just
                //  in the same direction as the partial derivatives of the 
                //  density function.
            // static float normalCalcDistance = 1.f;  // changing this distance produces some interesting results in very noisy fields. Larger numbers will generally be smoother
            // float dx0 = CalculateDensity(bestIntersection + Float2(-normalCalcDistance, 0.f));
            // float dx1 = CalculateDensity(bestIntersection + Float2( normalCalcDistance, 0.f));
            // float dy0 = CalculateDensity(bestIntersection + Float2(0.f, -normalCalcDistance));
            // float dy1 = CalculateDensity(bestIntersection + Float2(0.f,  normalCalcDistance));
            //     // (actually the shouldn't matter if the normal is pointing positive or negative)
            // Float2 normal = -Normalize(Float2(dx1-dx0, dy1-dy0));
            Float2 normal = -Normalize(bestIntersection);

            return FieldIntersection(bestIntersection, normal);

        } else {
            const float radius = 28.5f;
            Float2 dir = e1-e0;
            const float length = Magnitude(e1-e0);
            dir *= XlRSqrt(length);
            auto intersection = RayVsCircle(e0, dir, Float2(0.f, 0.f), radius);
            if (intersection.first < 0.f) {
                return FieldIntersection();
            }

            float distance = intersection.first;
            if (intersection.second > 0.f && intersection.second < distance) {
                distance = intersection.second;
            }

            if (distance > length) {
                return FieldIntersection();
            }

            Float2 intersectionPt = e0 + dir * distance;
            return FieldIntersection(intersectionPt, Normalize(intersectionPt));
        }
    }

    // static void RotateToUpperTriangle(cml::matrix<float, cml::fixed<5,4>, cml::col_basis>& matrix)
    // {
    //         // Convert this matrix into a upper triangle using the Householder method
    // }

    static void RotateToUpperTriangle(Eigen::Matrix<float,5,4>& A)
    {
        // Eigen::ColPivHouseholderQR<Eigen::Matrix<float,5,4>> qr(A);
        // A = qr.matrixR().triangularView<Eigen::Upper>();
        Eigen::HouseholderQR<Eigen::Matrix<float,5,4>> qr2(A);
        A = qr2.matrixQR().triangularView<Eigen::Upper>();
    }

    Float2 CalculateDualContouringPt(const FieldIntersection intersections[], unsigned intersectionCount, const Float2& massPoint)
    {
            //  Each intersection point defines a plane. We want to find the point within the grid cell that
            //  is closest to lying on all planes. There might not be an exact solution. However, the following
            //  math will find the solution that minimizes the least squares of the distances from the planes.
            //
            //  Also note the use of the "mass point". This is described in the original dual contouring paper.
            //  This can help avoid cases where the solution is outside of the grid cell.
            //      http://www.cs.rice.edu/~jwarren/papers/techreport02408.pdf

            //  The simplest version of this just involves creating a matrix from the planes
            //  defined by the intersection pts, and using SVD decomposition to calculate
            //  the pseudo-inverse (which will give us a solution).
            //  Let's do this using the "eigen" library. That means using the matrix tpyes from
            //  that library.

        static unsigned method = 2;
        if (method == 1) {

                //  Most basic method. We're just going to solve the linear equations
                //  directly. This doesn't involve any more optimisations or accuracy
                //  improvements
            Eigen::MatrixXf A(intersectionCount, 2);
            Eigen::MatrixXf B(intersectionCount, 1);
            for (unsigned c=0; c<intersectionCount; ++c) {
                A(c, 0) = intersections[c]._normal[0];
                A(c, 1) = intersections[c]._normal[1];
                B(c, 0) = Dot(intersections[c]._position, intersections[c]._normal);
            }

                //  Calculate the decomposition.
                //  
                //  The paper suggests that we might not have to do all of this. It uses A multiplied
                //  by it's transpose, which reduces some of the SVD math. It doesn't look like
                //  the eigen library supports this simplification, however.
            Eigen::JacobiSVD<Eigen::MatrixXf> svd(A, Eigen::ComputeFullU | Eigen::ComputeFullV);

            Eigen::MatrixXf x(2, 1);
            x = svd.solve(B).eval();

                // that's our basic solution...
            return Float2(x(0, 0), x(1, 0));

        } else if (method == 0) {

                //  This is the QR decomposition method. This transforms the matrices 
                //  beforehand into a form that can be more accurate for later math.

            typedef Eigen::Matrix<float,5,4> Float5x4;
            Float5x4 mat = Float5x4::Zero();

            for (unsigned c=0; c<intersectionCount; ++c) {
                mat(4, 0) = intersections[c]._normal[0];
                mat(4, 1) = intersections[c]._normal[1];
                mat(4, 2) = 0.f;
                mat(4, 3) = Dot(intersections[c]._position, intersections[c]._normal);
                RotateToUpperTriangle(mat);
            }

            Eigen::Matrix<float,3,3> Ahat;
            Eigen::Matrix<float,3,1> Bhat;
            for (unsigned c=0; c<3; ++c) {
                Ahat(c,0) = mat(c,0);
                Ahat(c,1) = mat(c,1);
                Ahat(c,2) = mat(c,2);
                Bhat(c,0) = mat(c,3);
            }

            Eigen::JacobiSVD<Eigen::MatrixXf> svd(Ahat, Eigen::ComputeFullU | Eigen::ComputeFullV);

            Eigen::MatrixXf x(2, 1);
            x = svd.solve(Bhat).eval();

            return Float2(x(0, 0), x(1, 0));

        } else if (method == 2) {

                // same as above, but using the mass point 

            typedef Eigen::Matrix<float,5,4> Float5x4;
            Float5x4 mat = Float5x4::Zero();

            for (unsigned c=0; c<intersectionCount; ++c) {
                mat(4, 0) = intersections[c]._normal[0];
                mat(4, 1) = intersections[c]._normal[1];
                mat(4, 2) = 0.f;
                mat(4, 3) = Dot(intersections[c]._position, intersections[c]._normal);
                RotateToUpperTriangle(mat);
            }

            Eigen::Matrix<float,3,3> Ahat;
            Eigen::Matrix<float,3,1> Bhat;
            for (unsigned c=0; c<3; ++c) {
                Ahat(c,0) = mat(c,0);
                Ahat(c,1) = mat(c,1);
                Ahat(c,2) = mat(c,2);
                Bhat(c,0) = mat(c,3);
            }

            // const float r = mat(3,3);        (not clear what I should do with "r". It always seem to be 0.f in tests so far)

            Eigen::Matrix<float,3,1> massPointVec;
            massPointVec(0,0) = massPoint[0];
            massPointVec(1,0) = massPoint[1];
            massPointVec(2,0) = 0.f;

            Eigen::MatrixXf x(2, 1);

                //  The original dual contour multiplies through with the transpose of A. This might
                //  reduce the work when calculating the SVD, but the Eigen library doesn't seem to
                //  support this optimisation, however. So I'm not sure if we need to multiply
                //  through with AHatTranspose here.
            static bool useTranspose = true;
            if (useTranspose) {
                auto AhatTranspose = Ahat.transpose();
                Eigen::JacobiSVD<Eigen::MatrixXf> svd(AhatTranspose * Ahat, Eigen::ComputeFullU | Eigen::ComputeFullV);
                x = svd.solve(AhatTranspose * Bhat - AhatTranspose * Ahat * massPointVec).eval();
            } else {
                    //  note that if we know the mass point when we're calculating Ahat, we can probably
                    //  just take into account the mass point then. However, if we're using a marching
                    //  cubes-like algorithm to move through the data field, and so visiting each cube
                    //  element multiple times, we might not be able to calculate the mass point until 
                    //  after AHat has been fully built. But we can compensate during the solution, as so ---
                Eigen::JacobiSVD<Eigen::MatrixXf> svd(Ahat, Eigen::ComputeFullU | Eigen::ComputeFullV);
                x = svd.solve(Bhat - Ahat * massPointVec).eval();
            }

            auto result = Float2(x(0, 0) + massPoint[0], x(1, 0) + massPoint[1]);

                //  We can still get some results where the solution is outside of the
                //  grid element we're building. This can happen when there is significant
                //  curvature inside a single grid element. It produces very strange results
                //  so we have to detect these cases, and use some other solution. In these
                //  cases, the mass point is probably the best estimate of what we need.
                //  The only other option would be to use higher order primitives than cubes
                //  (eg. bezier surfaces). But that would require lots of extra work and calculations.

            if (    result[0] < -.5f || result[0] > .5f
                ||  result[1] < -.5f || result[1] > .5f) {
                return massPoint;       // got a poor result from the QEF test -- too much curvature in a small area.
            }

            return result;

        } else if (method == 3) {
                //  When the normal method isn't working, this can give a rough estimate 
                //  of what it should look like
            return massPoint;       
        }

        return Float2(0.f, 0.f);
    }

    static Float2 MulAcross(const Float2& zero, const Float2& one)
    {
        return Float2(zero[0]*one[0], zero[1]*one[1]);
    }

    void    DualContouringTest::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
    {
        int gridSize = 64;
        std::vector<std::pair<Float2, Float2>> lines;
        for (int y=-gridSize/2; y<gridSize; ++y) {
            for (int x=-gridSize/2; x<gridSize; ++x) {

                    //  Here we're going to test one grid element at a time (in 2d)
                    //  Let's look at the 4 edges of this grid element and find
                    //  the signs and intersection points. We also need the positions
                    //  of the intersections and the normals at those coordinates.
                    //
                    //  Let's start with a simple equation where we can calculate those
                    //  things easily

                Float2 p0 = Float2(  float(x),     float(y));
                Float2 p1 = Float2(float(x+1),     float(y));
                Float2 p2 = Float2(  float(x),   float(y+1));
                Float2 p3 = Float2(float(x+1),   float(y+1));

                auto i0 = GetIntersection(p0, p1);
                auto i1 = GetIntersection(p1, p3);
                auto i2 = GetIntersection(p3, p2);
                auto i3 = GetIntersection(p2, p0);

                FieldIntersection intrs[4];
                Float2 massPoint(0.f, 0.f);
                unsigned intersectionCount = 0;
                if (i0._isGood) { intrs[intersectionCount++] = i0; massPoint += i0._position; }
                if (i1._isGood) { intrs[intersectionCount++] = i1; massPoint += i1._position; }
                if (i2._isGood) { intrs[intersectionCount++] = i2; massPoint += i2._position; }
                if (i3._isGood) { intrs[intersectionCount++] = i3; massPoint += i3._position; }
                if (!intersectionCount) { continue; }

                massPoint /= float(intersectionCount);

                //  We offset the whole system to bring it near
                    //  the origin.
                Float2 offset(float(x) + .5f, float(y) + .5f);
                for (unsigned c=0; c<intersectionCount; ++c) {
                    intrs[c]._position -= offset;
                }
                massPoint -= offset;

                    //  Now we have all our intersection. We need to solve a system
                    //  of linear equations to calculate the point that is closest to
                    //  meeting the planes defined.
                auto cellPt = CalculateDualContouringPt(intrs, intersectionCount, massPoint);

                    //  now we must add lines from each of the intersection points to the 
                    //  cell pt.
                for (unsigned c=0; c<intersectionCount; ++c) {
                    lines.push_back(std::make_pair(intrs[c]._position + offset, cellPt + offset));
                }
            }
        }

        auto maxSize = layout.GetMaximumSize();
        Rect gridArea(maxSize._topLeft + Coord2(50, 50), maxSize._bottomRight - Coord2(50, 50));

        Float2 o = LinearInterpolate(AsFloat2(gridArea._topLeft), AsFloat2(gridArea._bottomRight), 0.5f);
        Float2 s = Float2(gridArea.Width() / float(gridSize), gridArea.Height() / float(gridSize));

            //////////////////////////////////////////////////////////////
                    //  draw a background grid          //

        {
            const unsigned segmentsX = gridSize+1;
            const unsigned segmentsY = gridSize+1;
            auto segsX = std::make_unique<Coord2[]>(segmentsX*2);
            auto segsY = std::make_unique<Coord2[]>(segmentsY*2);
            auto colours = std::make_unique<ColorB[]>(std::max(segmentsX, segmentsY)*2);
            unsigned c=0;
            for (int x=-gridSize/2; x<=gridSize/2; ++x, ++c) {
                segsX[c*2+0] = AsCoord2(MulAcross(Float2(float(x), float(-gridSize/2)), s) + o);
                segsX[c*2+1] = AsCoord2(MulAcross(Float2(float(x), float( gridSize/2)), s) + o);
                colours[c*2+0] = ColorB(0xff5f5f5f);
                colours[c*2+1] = ColorB(0xff5f5f5f);
            }
            assert(c==segmentsX);
            c=0;
            for (int y=-gridSize/2; y<=gridSize/2; ++y, ++c) {
                segsY[c*2+0] = AsCoord2(MulAcross(Float2(float(-gridSize/2), float(y)), s) + o);
                segsY[c*2+1] = AsCoord2(MulAcross(Float2(float( gridSize/2), float(y)), s) + o);
                colours[c*2+0] = ColorB(0xff5f5f5f);
                colours[c*2+1] = ColorB(0xff5f5f5f);
            }
            assert(c==segmentsY);

            DrawLines(context, segsX.get(), colours.get(), segmentsX);
            DrawLines(context, segsY.get(), colours.get(), segmentsY);
        }

            //////////////////////////////////////////////////////////////

        {
            std::vector<Coord2> transformedLines;
            std::vector<ColorB> lineColours;
            transformedLines.reserve(lines.size()*2);
            lineColours.reserve(lines.size()*2);
            for (auto i=lines.cbegin(); i!=lines.cend(); ++i) {
                transformedLines.push_back(AsCoord2(MulAcross(i->first, s) + o));
                transformedLines.push_back(AsCoord2(MulAcross(i->second, s) + o));
                lineColours.push_back(ColorB(0xffffffff));
                lineColours.push_back(ColorB(0xffffffff));
            }

            DrawLines(context,
                AsPointer(transformedLines.cbegin()),
                AsPointer(lineColours.cbegin()),
                unsigned(transformedLines.size()/2));
        }

    }

    bool    DualContouringTest::ProcessInput(InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input)
    {
        return false;
    }

    DualContouringTest::DualContouringTest()
    {
    }

    DualContouringTest::~DualContouringTest()
    {}


///////////////////////////////////////////////////////////////////////////////////////////////////

    static UInt2 RectPackerSize(512, 512);

    class RectanglePackerTest::Pimpl
    {
    public:
        RectanglePacker_MaxRects _packer;

        using Rectangle = std::pair<UInt2, UInt2>;
        std::vector<Rectangle> _rects;

        Pimpl() {}

        UInt2 BuildRandomSize()
        {
            static std::mt19937 generator(std::random_device().operator()());
            // const UInt2 minSize( 32,  32);
            // const UInt2 maxSize(128, 128);
            // return UInt2(
            //     std::uniform_int_distribution<>(minSize[0], maxSize[0])(generator),
            //     std::uniform_int_distribution<>(minSize[1], maxSize[1])(generator));
            return UInt2(
                unsigned(std::normal_distribution<>(64, 16)(generator)),
                unsigned(std::normal_distribution<>(64, 16)(generator)));
            // return UInt2(
            //     1<<IntegerLog2(unsigned(std::normal_distribution<>(64, 16)(generator))),
            //     1<<IntegerLog2(unsigned(std::normal_distribution<>(64, 16)(generator))));
        }

        void FillRandomly()
        {
            const unsigned rects = 128;
            _packer = RectanglePacker_MaxRects();
            _packer.Deallocate(std::make_pair(UInt2(0,0), RectPackerSize));
            _rects.clear();
            
            for (unsigned c=0; c<rects; ++c) {
                auto rect = _packer.Allocate(BuildRandomSize());
                if (rect.second[0] > rect.first[0] && rect.second[1] > rect.first[1])
                    _rects.push_back(rect);
            }
        }

        void FillEfficiently()
        {
            const unsigned rects = 128;
            _packer = RectanglePacker_MaxRects();
            _packer.Deallocate(std::make_pair(UInt2(0,0), RectPackerSize));
            _rects.clear();

            std::vector<UInt2> rectsToAdd; rectsToAdd.reserve(rects);
            for (unsigned c=0; c<rects; ++c)
                rectsToAdd.push_back(BuildRandomSize());

            std::sort(rectsToAdd.begin(), rectsToAdd.end(),
                [](const UInt2& lhs, const UInt2& rhs)
                {
                    return std::max(lhs[0], lhs[1]) > std::max(rhs[0], rhs[1]);
                });

            for (const auto& size:rectsToAdd) {
                auto rect = _packer.Allocate(size);
                if (rect.second[0] > rect.first[0] && rect.second[1] > rect.first[1])
                    _rects.push_back(rect);
            }
        }
    };

    class ButtonFormatting
    {
    public:
        ColorB  _background;
        ColorB  _foreground;
        ButtonFormatting(ColorB background, ColorB foreground) : _background(background), _foreground(foreground) {}
    };

    static void DrawButtonBasic(IOverlayContext& context, const Rect& rect, const char label[], ButtonFormatting formatting)
    {
        FillRectangle(context, rect, formatting._background);
        OutlineRectangle(context, rect, 0.f, formatting._foreground);
        context.DrawText(
            std::make_tuple(Float3(float(rect._topLeft[0]), float(rect._topLeft[1]), 0.f), Float3(float(rect._bottomRight[0]), float(rect._bottomRight[1]), 0.f)),
			GetDefaultFont(), TextStyle{}, formatting._foreground, TextAlignment::Center, label);
    }

    template<typename T> inline const T& FormatButton(InterfaceState& interfaceState, InteractableId id, const T& normalState, const T& mouseOverState, const T& pressedState)
    {
        if (interfaceState.HasMouseOver(id))
            return interfaceState.IsMouseButtonHeld(0)?pressedState:mouseOverState;
        return normalState;
    }

    void RectanglePackerTest::Render(
        IOverlayContext& context, Layout& layout, 
        Interactables&interactables, InterfaceState& interfaceState)
    {
        static ButtonFormatting buttonNormalState(ColorB(127, 192, 127, 64), ColorB(164, 192, 164, 255));
        static ButtonFormatting buttonMouseOverState(ColorB(127, 192, 127, 64), ColorB(255, 255, 255, 160));
        static ButtonFormatting buttonPressedState(ColorB(127, 192, 127, 64), ColorB(255, 255, 255, 96));

        Layout toolbar = layout.AllocateFullWidth(32);
        {
            auto buttonRect = toolbar.Allocate(UInt2(256, 32));
            DrawButtonBasic(
                &context, buttonRect,
                "Randomize", 
                FormatButton(interfaceState, Hash64("Randomize"), buttonNormalState, buttonMouseOverState, buttonPressedState));
            interactables.Register(Interactables::Widget(buttonRect, Hash64("Randomize")));
        }
        {
            auto buttonRect = toolbar.Allocate(UInt2(256, 32));
            DrawButtonBasic(
                &context, buttonRect,
                "FillEfficiently", 
                FormatButton(interfaceState, Hash64("FillEfficiently"), buttonNormalState, buttonMouseOverState, buttonPressedState));
            interactables.Register(Interactables::Widget(buttonRect, Hash64("FillEfficiently")));
        }

        auto remainingSpace = layout.AllocateFullHeightFraction(1.f);

        unsigned colors[] = 
        {
            0xffFDFD96, 0xff03C03C, 0xff836953, 0xffB19CD9, 0xffFFB347, 0xffFF6961, 
            0xffCB99C9, 0xffFFD1DC, 0xff779ECB, 0xffAEC6CF
        };
        unsigned c=0;
        
            // draw each rectangle within the remaining space
        for (const auto&r:_pimpl->_rects) {
            UInt2 mins = remainingSpace._topLeft + UInt2(
                 unsigned(r.first[0] * float(remainingSpace.Width()) / float(RectPackerSize[0])),
                 unsigned(r.first[1] * float(remainingSpace.Height()) / float(RectPackerSize[1])));
            UInt2 maxs = remainingSpace._topLeft + UInt2(
                 unsigned(r.second[0] * float(remainingSpace.Width()) / float(RectPackerSize[0])),
                 unsigned(r.second[1] * float(remainingSpace.Height()) / float(RectPackerSize[1])));
            FillRectangle(&context, Rect(mins, maxs), ColorB(colors[(++c)%dimof(colors)]));
        }
    }

    bool    RectanglePackerTest::ProcessInput(InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input)
    {
        if (input.IsRelease_LButton() && interfaceState.TopMostId() == Hash64("Randomize")) {
            _pimpl->FillRandomly();
            return true;
        } else if (input.IsRelease_LButton() && interfaceState.TopMostId() == Hash64("FillEfficiently")) {
            _pimpl->FillEfficiently();
            return true;
        }
        return false;
    }

    RectanglePackerTest::RectanglePackerTest() 
    {
        _pimpl = std::make_unique<Pimpl>();
        // _pimpl->FillRandomly();
    }

    RectanglePackerTest::~RectanglePackerTest() {}

}}

#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Metal/Shader.h"
#include "../../RenderCore/Metal/TextureView.h"
#include "../../RenderCore/Metal/InputLayout.h"
#include "../../RenderCore/Metal/ObjectFactory.h"
#include "../../RenderCore/Assets/Services.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../Assets/Assets.h"

namespace PlatformRig { namespace Overlays
{
    using namespace RenderCore;

    class CRTBox
    {
    public:
        class Desc {};

        using SRV = Metal::ShaderResourceView;
        using RTV = Metal::RenderTargetView;

        RTV _bufferRTV;
        SRV _bufferSRV;
        ViewportDesc _viewport;

        CRTBox(const Desc&);
        ~CRTBox();
    };

    const unsigned width = 64;
    const unsigned height = 64;

    CRTBox::CRTBox(const Desc&)
    {
        auto& factory = Metal::GetObjectFactory();
        auto buffer = Metal::CreateResource(
            factory, 
            CreateDesc(
                BindFlag::RenderTarget|BindFlag::ShaderResource, 0, GPUAccess::Read|GPUAccess::Write,
                TextureDesc::Plain2D(64, 64, Format::R8_UNORM), "ConsRasterTest"));
        _bufferRTV = RTV(buffer);
        _bufferSRV = SRV(buffer);
        _viewport = ViewportDesc(0.f, 0.f, float(width), float(height), 0.f, 1.f);
    }

    CRTBox::~CRTBox() {}

    void    ConservativeRasterTest::Render(
        IOverlayContext& context, Layout& layout, 
        Interactables&interactables, InterfaceState& interfaceState)
    {
		assert(0);		// can no longer get the device context from IOverlayContext
#if 0
        auto& box = ConsoleRig::FindCachedBox2<CRTBox>();
        auto metalContext = Metal::DeviceContext::Get(*context.GetDeviceContext());

        //
        //  We're going to test the conversative rasterization geometry shader
        //  to do this, we need to render to a low-res offscreen texture. Then
        //  we blow that up to fill the screen.
        //
        //  Let's do this in a forked command list, so we don't have to save 
        //  and restore state.
        //

        auto& commonResources = Techniques::CommonResources();
        ViewportDesc mainViewport(*metalContext);
        const float scale = XlFloor((mainViewport.Height - 150.f) / box._viewport.Height);
        const Float2 base(5.f, 100.f);
        Float2 triPoints[] = 
        {
            Float2(3.f, 3.f),
            Float2(40.f, 58.f),
            Float2(55.f, 5.f)
        };

        if (interfaceState.IsMouseButtonHeld(0)) {
            auto mp = interfaceState.MousePosition();
            triPoints[0] = (mp - base) / scale;
            triPoints[0][0] = XlFloor(triPoints[0][0]*10.f) / 10.f;
            triPoints[0][1] = XlFloor(triPoints[0][1]*10.f) / 10.f;
        }

        if (interfaceState.IsMouseButtonHeld(1)) {
            auto mp = interfaceState.MousePosition();
            triPoints[1] = (mp - base) / scale;
            triPoints[1][0] = XlFloor(triPoints[1][0]*10.f) / 10.f;
            triPoints[1][1] = XlFloor(triPoints[1][1]*10.f) / 10.f;
        }

        {
            auto forkedContext = metalContext->Fork();

            forkedContext->BeginCommandList();
            forkedContext->Bind(commonResources._blendOpaque);
            forkedContext->Bind(commonResources._dssDisable);
            forkedContext->Bind(commonResources._defaultRasterizer);
            forkedContext->Bind(Topology::TriangleList);

            forkedContext->Bind(mainViewport);
            
            TRY
            {
                forkedContext->Clear(box._bufferRTV, {0.f, 0.f, 0.f, 0.f});
                forkedContext->Bind(MakeResourceList(box._bufferRTV), nullptr);
                forkedContext->Bind(box._viewport);

                auto& shader = ::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(
                    BASIC2D_VERTEX_HLSL ":P2C:vs_*",
                    SCENE_ENGINE_RES "/ShadowGen/ConsRaster.hlsl:gs_conservativeRasterization:gs_*",
                    BASIC_PIXEL_HLSL ":P:ps_*",
                    "");
            
                class Vertex
                {
                public:
                    Float2 p; unsigned col;
                } 
                vertices[3] = 
                {
                    { triPoints[0], 0xffff0000 },
                    { triPoints[1], 0xff0000ff },
                    { triPoints[2], 0xff00ff00 }
                };
                Metal::VertexBuffer vb(vertices, sizeof(vertices));
                forkedContext->Bind(MakeResourceList(vb), sizeof(Vertex), 0);
                Metal::BoundInputLayout inputLayout(GlobalInputLayouts::P2C, shader);
                forkedContext->Bind(inputLayout);

                Float4 recipViewport(1.f / box._viewport.Width, 1.f / box._viewport.Height, 0.f, 0.f);
                SharedPkt constants[] = { MakeSharedPkt(recipViewport) };

                Metal::BoundUniforms uniforms(shader);
                uniforms.BindConstantBuffers(1, {"ReciprocalViewportDimensionsCB"});
                uniforms.Apply(
                    *forkedContext, context.GetGlobalUniformsStream(),
                    Metal::UniformsStream(constants));
            
                forkedContext->Bind(shader);
                forkedContext->Draw(3);

            } CATCH (...) {
            } CATCH_END

            // note --  We actually want to execute the forked command list and some point
            //          earlier than main context is executable -- and insert some synchronization
            //          tests to ensure that it is ready by the time we get to this point.
            //      we can keep a list of "tributary" command lists in the main device context...?
            metalContext->ExecuteCommandList(
                *forkedContext->ResolveCommandList(),
                true);
        }

        // now render this texture onto the main render target
        TRY
        {
            auto& shader = ::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(
                BASIC2D_VERTEX_HLSL ":P2CT:vs_*",
                BASIC_PIXEL_HLSL ":PCT:ps_*",
                "");

            class Vertex
            {
            public:
                Float2 p; unsigned col; Float2 tc;
            } 
            vertices[6] = 
            {
                { Float2(5.f, 100.f), ~0u, Float2(0.f, 0.f) },
                { Float2(5.f, 100.f + scale * box._viewport.Height), ~0u, Float2(0.f, 1.f) },
                { Float2(5.f + scale * box._viewport.Width, 100.f), ~0u, Float2(1.f, 0.f) },
                { Float2(5.f + scale * box._viewport.Width, 100.f), ~0u, Float2(1.f, 0.f) },
                { Float2(5.f, 100.f + scale * box._viewport.Height), ~0u, Float2(0.f, 1.f) },
                { Float2(5.f + scale * box._viewport.Width, 100.f + scale * box._viewport.Height), ~0u, Float2(1.f, 1.f) }
            };
            Metal::VertexBuffer vb(vertices, sizeof(vertices));
            metalContext->Bind(MakeResourceList(vb), sizeof(Vertex), 0);
            Metal::BoundInputLayout inputLayout(GlobalInputLayouts::P2CT, shader);
            metalContext->Bind(inputLayout);

            Float4 recipViewport(1.f / mainViewport.Width, 1.f / mainViewport.Height, 0.f, 0.f);
            SharedPkt constants[] = { MakeSharedPkt(recipViewport) };
            const Metal::ShaderResourceView* srvs[] = {&box._bufferSRV};

            Metal::BoundUniforms uniforms(shader);
            uniforms.BindShaderResources(1, {"DiffuseTexture"});
            uniforms.BindConstantBuffers(1, {"ReciprocalViewportDimensionsCB"});
            uniforms.Apply(
                *metalContext, context.GetGlobalUniformsStream(),
                Metal::UniformsStream(constants, srvs));
            
            metalContext->BindPS(MakeResourceList(commonResources._pointClampSampler));
            metalContext->Bind(shader);
            metalContext->Draw(6);

        } CATCH (...) {
        } CATCH_END

                // draw the triangle in wireframe
        context.DrawLine(
            ProjectionMode::P2D, 
            Expand(Float2(base + scale * triPoints[0]), 0.f), ColorB(0xffffffff),
            Expand(Float2(base + scale * triPoints[1]), 0.f), ColorB(0xffffffff),
            1.f);

        context.DrawLine(
            ProjectionMode::P2D, 
            Expand(Float2(base + scale * triPoints[1]), 0.f), ColorB(0xffffffff),
            Expand(Float2(base + scale * triPoints[2]), 0.f), ColorB(0xffffffff),
            1.f);

        context.DrawLine(
            ProjectionMode::P2D, 
            Expand(Float2(base + scale * triPoints[2]), 0.f), ColorB(0xffffffff),
            Expand(Float2(base + scale * triPoints[0]), 0.f), ColorB(0xffffffff),
            1.f);

        metalContext->BindPS(MakeResourceList(commonResources._linearClampSampler));
        metalContext->Bind(commonResources._blendStraightAlpha);
#endif
    }

    bool    ConservativeRasterTest::ProcessInput(InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input)
    {
        return false;
    }

    ConservativeRasterTest::ConservativeRasterTest() {}
    ConservativeRasterTest::~ConservativeRasterTest() {}

}}


