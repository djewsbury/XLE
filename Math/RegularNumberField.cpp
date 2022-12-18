// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SILENCE_CXX17_NEGATORS_DEPRECATION_WARNING		// silence warning due to removed functions in C++17 (from Eigen library)

#include "RegularNumberField.h"
#include <cmath>

#pragma warning(disable:4714)
#pragma push_macro("new")
#undef new
#include <Eigen/Dense>
#pragma pop_macro("new")

namespace XLEMath
{

///////////////////////////////////////////////////////////////////////////////////////////////////
            //   B I L I N E A R   
///////////////////////////////////////////////////////////////////////////////////////////////////

    template<unsigned SamplingFlags, typename Store>
        static Float2 SampleBilinear(const VectorField2DSeparate<Store>& field, Float2 coord)
    {
        float fx = XlFloor(coord[0]);
        float fy = XlFloor(coord[1]);
        float a = coord[0] - fx, b = coord[1] - fy;
        float weights[] = 
        {
            (1.f - a) * (1.f - b),
            a * (1.f - b),
            (1.f - a) * b,
            a * b
        };
        unsigned x0, x1, y0, y1;
        
        const auto dims = field.Dimensions();
        if (constant_expression<(SamplingFlags & RNFSample::WrapX)!=0>::result()) {
            x0 = unsigned((int(fx) + int(dims[0]))%dims[0]);
            x1 = (x0+1u)%dims[0];
        } else if (constant_expression<(SamplingFlags & RNFSample::ClampX)!=0>::result()) {
            x0 = unsigned(Clamp(fx, 0.f, float(dims[0]-1)));
            x1 = std::min(x0+1u, dims[0]-1u);
        } else {
            x0 = unsigned(fx); x1 = x0+1;
        }

        if (constant_expression<(SamplingFlags & RNFSample::WrapY)!=0>::result()) {
            y0 = unsigned((int(fy) + int(dims[1]))%dims[1]);
            y1 = (y0+1u)%dims[1];
        } else if (constant_expression<(SamplingFlags & RNFSample::ClampY)!=0>::result()) {
            y0 = unsigned(Clamp(fy, 0.f, float(dims[1]-1)));
            y1 = std::min(y0+1u, dims[1]-1u);
        } else {
            y0 = unsigned(fy); y1 = y0+1;
        }
        assert(x1 < dims[0] && y1 < dims[1]);
        
        float u
            = weights[0] * (*field._u)[y0*dims[0]+x0]
            + weights[1] * (*field._u)[y0*dims[0]+x1]
            + weights[2] * (*field._u)[y1*dims[0]+x0]
            + weights[3] * (*field._u)[y1*dims[0]+x1]
            ;
        float v
            = weights[0] * (*field._v)[y0*dims[0]+x0]
            + weights[1] * (*field._v)[y0*dims[0]+x1]
            + weights[2] * (*field._v)[y1*dims[0]+x0]
            + weights[3] * (*field._v)[y1*dims[0]+x1]
            ;
        return Float2(u, v);
    }

    template<unsigned SamplingFlags, typename Store>
        static float SampleBilinear(const ScalarField2D<Store>& field, Float2 coord)
    {
        float fx = XlFloor(coord[0]);
        float fy = XlFloor(coord[1]);
        float a = coord[0] - fx, b = coord[1] - fy;
        float weights[] = 
        {
            (1.f - a) * (1.f - b),
            a * (1.f - b),
            (1.f - a) * b,
            a * b
        };
        unsigned x0, x1, y0, y1;
        
        const auto dims = field.Dimensions();
        if (constant_expression<(SamplingFlags & RNFSample::WrapX)!=0>::result()) {
            x0 = unsigned((int(fx) + int(dims[0]))%dims[0]);
            x1 = (x0+1u)%dims[0];
        } else if (constant_expression<(SamplingFlags & RNFSample::ClampX)!=0>::result()) {
            x0 = unsigned(Clamp(fx, 0.f, float(dims[0]-1)));
            x1 = std::min(x0+1u, dims[0]-1u);
        } else {
            x0 = unsigned(fx); x1 = x0+1;
        }

        if (constant_expression<(SamplingFlags & RNFSample::WrapY)!=0>::result()) {
            y0 = unsigned((int(fy) + int(dims[1]))%dims[1]);
            y1 = (y0+1u)%dims[1];
        } else if (constant_expression<(SamplingFlags & RNFSample::ClampY)!=0>::result()) {
            y0 = unsigned(Clamp(fy, 0.f, float(dims[1]-1)));
            y1 = std::min(y0+1u, dims[1]-1u);
        } else {
            y0 = unsigned(fy); y1 = y0+1;
        }
        assert(x1 < dims[0] && y1 < dims[1]);
        
        return 
              weights[0] * (*field._u)[y0*dims[0]+x0]
            + weights[1] * (*field._u)[y0*dims[0]+x1]
            + weights[2] * (*field._u)[y1*dims[0]+x0]
            + weights[3] * (*field._u)[y1*dims[0]+x1]
            ;
    }

    template<unsigned SamplingFlags, typename Store>
        static Float3 SampleBilinear(const VectorField3DSeparate<Store>& field, Float3 coord)
    {
        float fx = XlFloor(coord[0]);
        float fy = XlFloor(coord[1]);
        float fz = XlFloor(coord[2]);
        float a = coord[0] - fx, b = coord[1] - fy, c = coord[2] - fz;
        float weights[] = 
        {
            (1.f - a) * (1.f - b) * (1.f - c),
            a * (1.f - b) * (1.f - c),
            (1.f - a) * b * (1.f - c),
            a * b * (1.f - c),

            (1.f - a) * (1.f - b) * c,
            a * (1.f - b) * c,
            (1.f - a) * b * c,
            a * b * c,
        };
        unsigned x0, x1, y0, y1, z0, z1;
        
        const auto dims = field.Dimensions();
        if (constant_expression<(SamplingFlags & RNFSample::WrapX)!=0>::result()) {
            x0 = unsigned((int(fx) + int(dims[0]))%dims[0]);
            x1 = (x0+1u)%dims[0];
        } else if (constant_expression<(SamplingFlags & RNFSample::ClampX)!=0>::result()) {
            x0 = unsigned(Clamp(fx, 0.f, float(dims[0]-1)));
            x1 = std::min(x0+1u, dims[0]-1u);
        } else {
            x0 = unsigned(fx); x1 = x0+1;
        }

        if (constant_expression<(SamplingFlags & RNFSample::WrapY)!=0>::result()) {
            y0 = unsigned((int(fy) + int(dims[1]))%dims[1]);
            y1 = (y0+1u)%dims[1];
        } else if (constant_expression<(SamplingFlags & RNFSample::ClampY)!=0>::result()) {
            y0 = unsigned(Clamp(fy, 0.f, float(dims[1]-1)));
            y1 = std::min(y0+1u, dims[1]-1u);
        } else {
            y0 = unsigned(fy); y1 = y0+1;
        }

        if (constant_expression<(SamplingFlags & RNFSample::WrapZ)!=0>::result()) {
            z0 = unsigned((int(fz) + int(dims[2]))%dims[2]);
            z1 = (z0+1u)%dims[2];
        } else if (constant_expression<(SamplingFlags & RNFSample::ClampZ)!=0>::result()) {
            z0 = unsigned(Clamp(fz, 0.f, float(dims[2]-1)));
            z1 = std::min(z0+1u, dims[2]-1u);
        } else {
            z0 = unsigned(fz); z1 = z0+1;
        }
        assert(x1 < dims[0] && y1 < dims[1] && z1 < dims[2]);
        
        float u
            = weights[0] * (*field._u)[(z0*dims[1]+y0)*dims[0]+x0]
            + weights[1] * (*field._u)[(z0*dims[1]+y0)*dims[0]+x1]
            + weights[2] * (*field._u)[(z0*dims[1]+y1)*dims[0]+x0]
            + weights[3] * (*field._u)[(z0*dims[1]+y1)*dims[0]+x1]
            + weights[4] * (*field._u)[(z1*dims[1]+y0)*dims[0]+x0]
            + weights[5] * (*field._u)[(z1*dims[1]+y0)*dims[0]+x1]
            + weights[6] * (*field._u)[(z1*dims[1]+y1)*dims[0]+x0]
            + weights[7] * (*field._u)[(z1*dims[1]+y1)*dims[0]+x1]
            ;
        float v
            = weights[0] * (*field._v)[(z0*dims[1]+y0)*dims[0]+x0]
            + weights[1] * (*field._v)[(z0*dims[1]+y0)*dims[0]+x1]
            + weights[2] * (*field._v)[(z0*dims[1]+y1)*dims[0]+x0]
            + weights[3] * (*field._v)[(z0*dims[1]+y1)*dims[0]+x1]
            + weights[4] * (*field._v)[(z1*dims[1]+y0)*dims[0]+x0]
            + weights[5] * (*field._v)[(z1*dims[1]+y0)*dims[0]+x1]
            + weights[6] * (*field._v)[(z1*dims[1]+y1)*dims[0]+x0]
            + weights[7] * (*field._v)[(z1*dims[1]+y1)*dims[0]+x1]
            ;
        float w
            = weights[0] * (*field._w)[(z0*dims[1]+y0)*dims[0]+x0]
            + weights[1] * (*field._w)[(z0*dims[1]+y0)*dims[0]+x1]
            + weights[2] * (*field._w)[(z0*dims[1]+y1)*dims[0]+x0]
            + weights[3] * (*field._w)[(z0*dims[1]+y1)*dims[0]+x1]
            + weights[4] * (*field._w)[(z1*dims[1]+y0)*dims[0]+x0]
            + weights[5] * (*field._w)[(z1*dims[1]+y0)*dims[0]+x1]
            + weights[6] * (*field._w)[(z1*dims[1]+y1)*dims[0]+x0]
            + weights[7] * (*field._w)[(z1*dims[1]+y1)*dims[0]+x1]
            ;
        return Float3(u, v, w);
    }

    template<unsigned SamplingFlags, typename Store>
        static float SampleBilinear(const ScalarField3D<Store>& field, Float3 coord)
    {
        float fx = XlFloor(coord[0]);
        float fy = XlFloor(coord[1]);
        float fz = XlFloor(coord[2]);
        float a = coord[0] - fx, b = coord[1] - fy, c = coord[2] - fz;
        float weights[] = 
        {
            (1.f - a) * (1.f - b) * (1.f - c),
            a * (1.f - b) * (1.f - c),
            (1.f - a) * b * (1.f - c),
            a * b * (1.f - c),

            (1.f - a) * (1.f - b) * c,
            a * (1.f - b) * c,
            (1.f - a) * b * c,
            a * b * c,
        };
        unsigned x0, x1, y0, y1, z0, z1;
        
        const auto dims = field.Dimensions();
        if (constant_expression<(SamplingFlags & RNFSample::WrapX)!=0>::result()) {
            x0 = unsigned((int(fx) + int(dims[0]))%dims[0]);
            x1 = (x0+1u)%dims[0];
        } else if (constant_expression<(SamplingFlags & RNFSample::ClampX)!=0>::result()) {
            x0 = unsigned(Clamp(fx, 0.f, float(dims[0]-1)));
            x1 = std::min(x0+1u, dims[0]-1u);
        } else {
            x0 = unsigned(fx); x1 = x0+1;
        }

        if (constant_expression<(SamplingFlags & RNFSample::WrapY)!=0>::result()) {
            y0 = unsigned((int(fy) + int(dims[1]))%dims[1]);
            y1 = (y0+1u)%dims[1];
        } else if (constant_expression<(SamplingFlags & RNFSample::ClampY)!=0>::result()) {
            y0 = unsigned(Clamp(fy, 0.f, float(dims[1]-1)));
            y1 = std::min(y0+1u, dims[1]-1u);
        } else {
            y0 = unsigned(fy); y1 = y0+1;
        }

        if (constant_expression<(SamplingFlags & RNFSample::WrapZ)!=0>::result()) {
            z0 = unsigned((int(fz) + int(dims[2]))%dims[2]);
            z1 = (z0+1u)%dims[2];
        } else if (constant_expression<(SamplingFlags & RNFSample::ClampZ)!=0>::result()) {
            z0 = unsigned(Clamp(fz, 0.f, float(dims[2]-1)));
            z1 = std::min(z0+1u, dims[2]-1u);
        } else {
            z0 = unsigned(fz); z1 = z0+1;
        }
        assert(x1 < dims[0] && y1 < dims[1] && z1 < dims[2]);
        
        return
              weights[0] * (*field._u)[(z0*dims[1]+y0)*dims[0]+x0]
            + weights[1] * (*field._u)[(z0*dims[1]+y0)*dims[0]+x1]
            + weights[2] * (*field._u)[(z0*dims[1]+y1)*dims[0]+x0]
            + weights[3] * (*field._u)[(z0*dims[1]+y1)*dims[0]+x1]
            + weights[4] * (*field._u)[(z1*dims[1]+y0)*dims[0]+x0]
            + weights[5] * (*field._u)[(z1*dims[1]+y0)*dims[0]+x1]
            + weights[6] * (*field._u)[(z1*dims[1]+y1)*dims[0]+x0]
            + weights[7] * (*field._u)[(z1*dims[1]+y1)*dims[0]+x1]
            ;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////
            //   M O N O T O N I C   C U B I C
///////////////////////////////////////////////////////////////////////////////////////////////////

    static float Sign(float x) { return (x < 0.f) ? -1.f : (x==0.f) ? 0.f : 1.f; }
    static float MonotonicCubic(float xn1, float x0, float x1, float x2, float alpha)
    {
            // See also http://grmanet.sogang.ac.kr/ihm/webpapers/CLMCI.pdf
            // (Controllable Local Monotonic Cubic Interpolation in Fluid Animations)
            // for a much more expensive, but more flexible, monotonic interpolation
            // method.
            // This method reverts to linear in non-monotonic cases. The above paper
            // attempts to find a good curve even on non-monotonic cases
      
        float dk        = (x1 - xn1) / 2.f;
        float dk1       = (x2 - x0) / 2.f;
        float deltak    = x1 - x0;
        if (Sign(dk) != Sign(deltak) || Sign(dk1) != Sign(deltak))
            dk = dk1 = 0.f;

        float a2 = alpha * alpha;
        float a3 = alpha * a2;

            //      original math from Fedkiw has a small error in the first term
            //      (should be -2*deltak, not -delta)
        return 
              (dk + dk1 - 2.f * deltak) * a3
            + (3.f * deltak - 2.f * dk - dk1) * a2
            + dk * alpha
            + x0;
    }

    template<unsigned SamplingFlags, typename Store>
        float SampleMonotonicCubic(const ScalarField2D<Store>& field, Float2 coord)
    {
        float fx = XlFloor(coord[0]);
        float fy = XlFloor(coord[1]);
        float a = coord[0] - fx, b = coord[1] - fy;

        const auto dims = field.Dimensions();
        unsigned xn1, x0, x1, x2;
        unsigned yn1, y0, y1, y2;
        if (constant_expression<(SamplingFlags & RNFSample::WrapX)!=0>::result()) {
            x0 = unsigned((int(fx) + int(dims[0]))%dims[0]);
            x1 = (x0+1u)%dims[0];
            x2 = (x1+1u)%dims[0];
            xn1 = (x0+dims[0]-1)%dims[0];
        } else {
            x0 = unsigned(Clamp(fx, 0.f, float(dims[0]-1)));
            x1 = std::min(x0+1u, dims[0]-1u);
            x2 = std::min(x1+1u, dims[0]-1u);
            xn1 = std::max(x0, 1u) - 1u;
        }

        if (constant_expression<(SamplingFlags & RNFSample::WrapY)!=0>::result()) {
            y0 = unsigned((int(fy) + int(dims[1]))%dims[1]);
            y1 = (y0+1u)%dims[1];
            y2 = (y1+1u)%dims[1];
            yn1 = (y0+dims[1]-1)%dims[1];
        } else {
            y0 = unsigned(Clamp(fy, 0.f, float(dims[1]-1)));
            y1 = std::min(y0+1u, dims[1]-1u);
            y2 = std::min(y1+1u, dims[1]-1u);
            yn1 = std::max(y0, 1u) - 1u;
        }
        
        float u[] = 
        {
            (*field._u)[yn1*dims[0]+xn1],
            (*field._u)[yn1*dims[0]+ x0],
            (*field._u)[yn1*dims[0]+ x1],
            (*field._u)[yn1*dims[0]+ x2],

            (*field._u)[ y0*dims[0]+xn1],
            (*field._u)[ y0*dims[0]+ x0],
            (*field._u)[ y0*dims[0]+ x1],
            (*field._u)[ y0*dims[0]+ x2],

            (*field._u)[ y1*dims[0]+xn1],
            (*field._u)[ y1*dims[0]+ x0],
            (*field._u)[ y1*dims[0]+ x1],
            (*field._u)[ y1*dims[0]+ x2],

            (*field._u)[ y2*dims[0]+xn1],
            (*field._u)[ y2*dims[0]+ x0],
            (*field._u)[ y2*dims[0]+ x1],
            (*field._u)[ y2*dims[0]+ x2]
        };

        float un1 = MonotonicCubic(u[ 0], u[ 1], u[ 2], u[ 3], a);
        float u0  = MonotonicCubic(u[ 4], u[ 5], u[ 6], u[ 7], a);
        float u1  = MonotonicCubic(u[ 8], u[ 9], u[10], u[11], a);
        float u2  = MonotonicCubic(u[12], u[13], u[14], u[15], a);
        return MonotonicCubic(un1, u0, u1, u2, b);
    }

    template<unsigned SamplingFlags, typename Store>
        Float2 SampleMonotonicCubic(const VectorField2DSeparate<Store>& field, Float2 coord)
    {
        float fx = XlFloor(coord[0]);
        float fy = XlFloor(coord[1]);
        float a = coord[0] - fx, b = coord[1] - fy;

        const auto dims = field.Dimensions();
        unsigned xn1, x0, x1, x2;
        unsigned yn1, y0, y1, y2;
        if (constant_expression<(SamplingFlags & RNFSample::WrapX)!=0>::result()) {
            x0  = unsigned((int(fx) + int(dims[0]))%dims[0]);
            x1  = (x0+1u)%dims[0];
            x2  = (x1+1u)%dims[0];
            xn1 = (x0+dims[0]-1)%dims[0];
        } else {
            x0  = unsigned(Clamp(fx, 0.f, float(dims[0]-1)));
            x1  = std::min(x0+1u, dims[0]-1u);
            x2  = std::min(x1+1u, dims[0]-1u);
            xn1 = std::max(x0, 1u) - 1u;
        }

        if (constant_expression<(SamplingFlags & RNFSample::WrapY)!=0>::result()) {
            y0  = unsigned((int(fy) + int(dims[1]))%dims[1]);
            y1  = (y0+1u)%dims[1];
            y2  = (y1+1u)%dims[1];
            yn1 = (y0+dims[1]-1)%dims[1];
        } else {
            y0  = unsigned(Clamp(fy, 0.f, float(dims[1]-1)));
            y1  = std::min(y0+1u, dims[1]-1u);
            y2  = std::min(y1+1u, dims[1]-1u);
            yn1 = std::max(y0, 1u) - 1u;
        }
        
        float u[] = 
        {
            (*field._u)[yn1*dims[0]+xn1],
            (*field._u)[yn1*dims[0]+ x0],
            (*field._u)[yn1*dims[0]+ x1],
            (*field._u)[yn1*dims[0]+ x2],

            (*field._u)[ y0*dims[0]+xn1],
            (*field._u)[ y0*dims[0]+ x0],
            (*field._u)[ y0*dims[0]+ x1],
            (*field._u)[ y0*dims[0]+ x2],

            (*field._u)[ y1*dims[0]+xn1],
            (*field._u)[ y1*dims[0]+ x0],
            (*field._u)[ y1*dims[0]+ x1],
            (*field._u)[ y1*dims[0]+ x2],

            (*field._u)[ y2*dims[0]+xn1],
            (*field._u)[ y2*dims[0]+ x0],
            (*field._u)[ y2*dims[0]+ x1],
            (*field._u)[ y2*dims[0]+ x2]
        };

        float v[] = 
        {
            (*field._v)[yn1*dims[0]+xn1],
            (*field._v)[yn1*dims[0]+ x0],
            (*field._v)[yn1*dims[0]+ x1],
            (*field._v)[yn1*dims[0]+ x2],

            (*field._v)[ y0*dims[0]+xn1],
            (*field._v)[ y0*dims[0]+ x0],
            (*field._v)[ y0*dims[0]+ x1],
            (*field._v)[ y0*dims[0]+ x2],

            (*field._v)[ y1*dims[0]+xn1],
            (*field._v)[ y1*dims[0]+ x0],
            (*field._v)[ y1*dims[0]+ x1],
            (*field._v)[ y1*dims[0]+ x2],

            (*field._v)[ y2*dims[0]+xn1],
            (*field._v)[ y2*dims[0]+ x0],
            (*field._v)[ y2*dims[0]+ x1],
            (*field._v)[ y2*dims[0]+ x2]
        };

            // Unfortunately, to get correct cubic interpolation in 2D, we
            // have to do a lot of interpolations (similar to the math used
            // in refining cubic surfaces)
            //
            // We can optimise this by only doing the monotonic interpolation
            // of the U parameter in the U direction (and the V parameter in
            // the V direction). Maybe we could fall back to linear in the 
            // cross-wise direction.
            //
            // It's hard to know what effect that optimisation would have on
            // the advection operation.
        float un1 = MonotonicCubic(u[ 0], u[ 1], u[ 2], u[ 3], a);
        float u0  = MonotonicCubic(u[ 4], u[ 5], u[ 6], u[ 7], a);
        float u1  = MonotonicCubic(u[ 8], u[ 9], u[10], u[11], a);
        float u2  = MonotonicCubic(u[12], u[13], u[14], u[15], a);
        float fu =  MonotonicCubic(un1, u0, u1, u2, b);

        float vn1 = MonotonicCubic(v[ 0], v[ 1], v[ 2], v[ 3], a);
        float v0  = MonotonicCubic(v[ 4], v[ 5], v[ 6], v[ 7], a);
        float v1  = MonotonicCubic(v[ 8], v[ 9], v[10], v[11], a);
        float v2  = MonotonicCubic(v[12], v[13], v[14], v[15], a);
        float fv =  MonotonicCubic(vn1, v0, v1, v2, b);
        return Float2(fu, fv);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////
            //   G A T H E R   N E I G H B O R S
///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename Store>
        void GatherNeighbors(
            Float2 neighbours[9], float weights[4], 
            const VectorField2DSeparate<Store>& field, Float2 coord, unsigned samplingFlags)
    {
        float fx = XlFloor(coord[0]);
        float fy = XlFloor(coord[1]);
        float a = coord[0] - fx, b = coord[1] - fy;
        weights[0] = (1.f - a) * (1.f - b);
        weights[1] = a * (1.f - b);
        weights[2] = (1.f - a) * b;
        weights[3] = a * b;

        const auto dims = field.Dimensions();
        unsigned x0, x1, y0, y1;
        unsigned xx, yx;
        if (samplingFlags & RNFSample::WrapX) {
            x0 = unsigned((int(fx) + int(dims[0]))%dims[0]);
            x1 = (x0+1u)%dims[0];
            xx = (x0+dims[0]-1)%dims[0];
        } else {
            x0 = unsigned(Clamp(fx, 0.f, float(dims[0]-1)));
            x1 = std::min(x0+1u, dims[0]-1u);
            xx = std::max(x0, 1u) - 1u;
        }

        if (samplingFlags & RNFSample::WrapY) {
            y0 = unsigned((int(fy) + int(dims[1]))%dims[1]);
            y1 = (y0+1u)%dims[1];
            yx = (y0+dims[1]-1)%dims[1];
        } else {
            y0 = unsigned(Clamp(fy, 0.f, float(dims[1]-1)));
            y1 = std::min(y0+1u, dims[1]-1u);
            yx = std::max(y0, 1u) - 1u;
        }

        neighbours[0][0] = (*field._u)[y0*dims[0]+x0];
        neighbours[1][0] = (*field._u)[y0*dims[0]+x1];
        neighbours[2][0] = (*field._u)[y1*dims[0]+x0];
        neighbours[3][0] = (*field._u)[y1*dims[0]+x1];

        neighbours[4][0] = (*field._u)[yx*dims[0]+xx];
        neighbours[5][0] = (*field._u)[yx*dims[0]+x0];
        neighbours[6][0] = (*field._u)[yx*dims[0]+x1];
        neighbours[7][0] = (*field._u)[y0*dims[0]+xx];
        neighbours[8][0] = (*field._u)[y1*dims[0]+xx];

        neighbours[0][1] = (*field._v)[y0*dims[0]+x0];
        neighbours[1][1] = (*field._v)[y0*dims[0]+x1];
        neighbours[2][1] = (*field._v)[y1*dims[0]+x0];
        neighbours[3][1] = (*field._v)[y1*dims[0]+x1];

        neighbours[4][1] = (*field._v)[yx*dims[0]+xx];
        neighbours[5][1] = (*field._v)[yx*dims[0]+x0];
        neighbours[6][1] = (*field._v)[yx*dims[0]+x1];
        neighbours[7][1] = (*field._v)[y0*dims[0]+xx];
        neighbours[8][1] = (*field._v)[y1*dims[0]+xx];
    }

    template<typename Store>
        void GatherNeighbors(
            float neighbours[8], float weights[4], 
            const ScalarField2D<Store>& field, Float2 coord, unsigned samplingFlags)
    {
        float fx = XlFloor(coord[0]);
        float fy = XlFloor(coord[1]);
        float a = coord[0] - fx, b = coord[1] - fy;
        weights[0] = (1.f - a) * (1.f - b);
        weights[1] = a * (1.f - b);
        weights[2] = (1.f - a) * b;
        weights[3] = a * b;

        const auto dims = field.Dimensions();
        unsigned x0, x1, y0, y1;
        unsigned xx, yx;
        if (samplingFlags & RNFSample::WrapX) {
            x0 = unsigned((int(fx) + int(dims[0]))%dims[0]);
            x1 = (x0+1u)%dims[0];
            xx = (x0+dims[0]-1)%dims[0];
        } else {
            x0 = unsigned(Clamp(fx, 0.f, float(dims[0]-1)));
            x1 = std::min(x0+1u, dims[0]-1u);
            xx = std::max(x0, 1u) - 1u;
        }

        if (samplingFlags & RNFSample::WrapY) {
            y0 = unsigned((int(fy) + int(dims[1]))%dims[1]);
            y1 = (y0+1u)%dims[1];
            yx = (y0+dims[1]-1)%dims[1];
        } else {
            y0 = unsigned(Clamp(fy, 0.f, float(dims[1]-1)));
            y1 = std::min(y0+1u, dims[1]-1u);
            yx = std::max(y0, 1u) - 1u;
        }

        neighbours[0] = (*field._u)[y0*dims[0]+x0];
        neighbours[1] = (*field._u)[y0*dims[0]+x1];
        neighbours[2] = (*field._u)[y1*dims[0]+x0];
        neighbours[3] = (*field._u)[y1*dims[0]+x1];

        neighbours[4] = (*field._u)[yx*dims[0]+xx];
        neighbours[5] = (*field._u)[yx*dims[0]+x0];
        neighbours[6] = (*field._u)[yx*dims[0]+x1];
        neighbours[7] = (*field._u)[y0*dims[0]+xx];
        neighbours[8] = (*field._u)[y1*dims[0]+xx];
    }

    static void GatherNeighbors(float* result, size_t stride, const float* source, UInt3 base, UInt3 dims, unsigned samplingFlags)
    {
        auto x0 = base[0], y0 = base[1], z0 = base[2];
        unsigned x1, y1, z1, xx, yx, zx;
        if (samplingFlags & RNFSample::WrapX) {
            x1 = (x0+1u)%dims[0];
            xx = (x0+dims[0]-1u)%dims[0];
        } else {
            x1 = std::min(x0+1u, dims[0]-1u);
            xx = std::max(x0, 1u)-1u;
        }

        if (samplingFlags & RNFSample::WrapY) {
            y1 = (y0+1u)%dims[1];
            yx = (y0+dims[1]-1u)%dims[1];
        } else {
            y1 = std::min(y0+1u, dims[1]-1u);
            yx = std::max(y0, 1u)-1u;
        }

        if (samplingFlags & RNFSample::WrapY) {
            z1 = (z0+1u)%dims[2];
            zx = (z0+dims[2]-1u)%dims[2];
        } else {
            z1 = std::min(z0+1u, dims[2]-1u);
            zx = std::max(z0, 1u)-1u;
        }

        #define V(x,y,z) source[(z*dims[1]+y)*dims[0]+x]
        #define R(x) result[x*stride]

            // results are arranged so the first 8 are the ones used for
            // bilinear interpolation
        R( 0) = V(x0, y0, z0);
        R( 1) = V(x1, y0, z0);
        R( 2) = V(x0, y1, z0);
        R( 3) = V(x1, y1, z0);
        R( 4) = V(x0, y0, z1);
        R( 5) = V(x1, y0, z1);
        R( 6) = V(x0, y1, z1);
        R( 7) = V(x1, y1, z1);

        R( 8) = V(xx, yx, z0);
        R( 9) = V(x0, yx, z0);
        R(10) = V(x1, yx, z0);
        R(11) = V(xx, y0, z0);
        R(12) = V(xx, y1, z0);

        R(13) = V(xx, yx, z1);
        R(14) = V(x0, yx, z1);
        R(15) = V(x1, yx, z1);
        R(16) = V(xx, y0, z1);
        R(17) = V(xx, y1, z1);

        R(18) = V(xx, yx, zx);
        R(19) = V(x0, yx, zx);
        R(20) = V(x1, yx, zx);
        R(21) = V(xx, y0, zx);
        R(22) = V(x0, y0, zx);
        R(23) = V(x1, y0, zx);
        R(24) = V(xx, y1, zx);
        R(25) = V(x0, y1, zx);
        R(26) = V(x1, y1, zx);
        
        #undef V
        #undef R
    }

    template<typename Store>
        void GatherNeighbors(
            Float3 neighbours[27], float weights[8], 
            const VectorField3DSeparate<Store>& field, Float3 coord, unsigned samplingFlags)
    {
        float fx = XlFloor(coord[0]);
        float fy = XlFloor(coord[1]);
        float fz = XlFloor(coord[2]);
        float a = coord[0] - fx, b = coord[1] - fy, c = coord[2] - fz;
        weights[0] = (1.f - a) * (1.f - b) * (1.f - c);
        weights[1] = a * (1.f - b) * (1.f - c);
        weights[2] = (1.f - a) * b * (1.f - c);
        weights[3] = a * b * (1.f - c);
        weights[4] = (1.f - a) * (1.f - b) * c;
        weights[5] = a * (1.f - b) * c;
        weights[6] = (1.f - a) * b * c;
        weights[7] = a * b * c;

        const auto dims = field.Dimensions();
        unsigned x0, y0, z0;
        if (samplingFlags & RNFSample::WrapX) {
            x0 = unsigned((int(fx) + int(dims[0]))%dims[0]);
        } else {
            x0 = unsigned(Clamp(fx, 0.f, float(dims[0]-1)));
        }

        if (samplingFlags & RNFSample::WrapY) {
            y0 = unsigned((int(fy) + int(dims[1]))%dims[1]);
        } else {
            y0 = unsigned(Clamp(fy, 0.f, float(dims[1]-1)));
        }

        if (samplingFlags & RNFSample::WrapY) {
            z0 = unsigned((int(fz) + int(dims[2]))%dims[2]);
        } else {
            z0 = unsigned(Clamp(fz, 0.f, float(dims[2]-1)));
        }

        GatherNeighbors(&neighbours[0][0], 3, &(*field._u)[0], UInt3(x0, y0, z0), dims, samplingFlags);
        GatherNeighbors(&neighbours[0][1], 3, &(*field._v)[0], UInt3(x0, y0, z0), dims, samplingFlags);
        GatherNeighbors(&neighbours[0][2], 3, &(*field._w)[0], UInt3(x0, y0, z0), dims, samplingFlags);
    }

    template<typename Store>
        void GatherNeighbors(
            float neighbours[27], float weights[8], 
            const ScalarField3D<Store>& field, Float3 coord, unsigned samplingFlags)
    {
        float fx = XlFloor(coord[0]);
        float fy = XlFloor(coord[1]);
        float fz = XlFloor(coord[2]);
        float a = coord[0] - fx, b = coord[1] - fy, c = coord[2] - fz;
        weights[0] = (1.f - a) * (1.f - b) * (1.f - c);
        weights[1] = a * (1.f - b) * (1.f - c);
        weights[2] = (1.f - a) * b * (1.f - c);
        weights[3] = a * b * (1.f - c);
        weights[4] = (1.f - a) * (1.f - b) * c;
        weights[5] = a * (1.f - b) * c;
        weights[6] = (1.f - a) * b * c;
        weights[7] = a * b * c;

        const auto dims = field.Dimensions();
        unsigned x0, y0, z0;
        if (samplingFlags & RNFSample::WrapX) {
            x0 = unsigned((int(fx) + int(dims[0]))%dims[0]);
        } else {
            x0 = unsigned(Clamp(fx, 0.f, float(dims[0]-1)));
        }

        if (samplingFlags & RNFSample::WrapY) {
            y0 = unsigned((int(fy) + int(dims[1]))%dims[1]);
        } else {
            y0 = unsigned(Clamp(fy, 0.f, float(dims[1]-1)));
        }

        if (samplingFlags & RNFSample::WrapY) {
            z0 = unsigned((int(fz) + int(dims[2]))%dims[2]);
        } else {
            z0 = unsigned(Clamp(fz, 0.f, float(dims[2]-1)));
        }

        GatherNeighbors(neighbours, 1, &(*field._u)[0], UInt3(x0, y0, z0), dims, samplingFlags);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename Store>
        float ScalarField2D<Store>::Load(Coord coord) const
    {
        assert(coord[0] < _dims[0] && coord[1] < _dims[1]);
        return (*_u)[coord[1] * _dims[0] + coord[0]];
    }

    template<typename Store>
        void ScalarField2D<Store>::Write(Coord coord, ValueType value)
    {
        assert(coord[0] < _dims[0] && coord[1] < _dims[1]);
        (*_u)[coord[1] * _dims[0] + coord[0]] = value;
        assert(std::isfinite(value) && !std::isnan(value));
    }

    template<typename Store>
    template<unsigned SamplingFlags>
        auto ScalarField2D<Store>::Sample(FloatCoord c) const -> ValueType
    {
        if (constant_expression<(SamplingFlags & RNFSample::Cubic)!=0>::result())
            return SampleMonotonicCubic<SamplingFlags>(*this, c);
        return SampleBilinear<SamplingFlags>(*this, c);
    }

    template<typename Store>
        void ScalarField2D<Store>::GatherNeighbors(ValueType neighbours[9], float weights[4], FloatCoord coord, unsigned samplingFlags) const
    {
        XLEMath::GatherNeighbors(neighbours, weights, *this, coord, samplingFlags);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename Store>
        Float2 VectorField2DSeparate<Store>::Load(Coord coord) const
    {
        assert(coord[0] < _dims[0] && coord[1] < _dims[1]);
        return Float2(
            (*_u)[coord[1] * _dims[0] + coord[0]],
            (*_v)[coord[1] * _dims[0] + coord[0]]);
    }

    template<typename Store>
        void VectorField2DSeparate<Store>::Write(Coord coord, ValueType value)
    {
        assert(coord[0] < _dims[0] && coord[1] < _dims[1]);
        (*_u)[coord[1] * _dims[0] + coord[0]] = value[0];
        (*_v)[coord[1] * _dims[0] + coord[0]] = value[1];
        assert(std::isfinite(value[0]) && !std::isnan(value[0]));
        assert(std::isfinite(value[1]) && !std::isnan(value[1]));
    }

    template<typename Store>
    template<unsigned SamplingFlags>
        auto VectorField2DSeparate<Store>::Sample(FloatCoord c) const -> ValueType
    {
        if (constant_expression<(SamplingFlags & RNFSample::Cubic)!=0>::result())
            return SampleMonotonicCubic<SamplingFlags>(*this, c);
        return SampleBilinear<SamplingFlags>(*this, c);
    }

    template<typename Store>
        void VectorField2DSeparate<Store>::GatherNeighbors(ValueType neighbours[9], float weights[4], FloatCoord coord, unsigned samplingFlags) const
    {
        XLEMath::GatherNeighbors(neighbours, weights, *this, coord, samplingFlags);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename Store>
        float ScalarField3D<Store>::Load(Coord coord) const
    {
        assert(coord[0] < _dims[0] && coord[1] < _dims[1]);
        return (*_u)[(coord[2] * _dims[1] + coord[1]) * _dims[0] + coord[0]];
    }

    template<typename Store>
        void ScalarField3D<Store>::Write(Coord coord, ValueType value)
    {
        assert(coord[0] < _dims[0] && coord[1] < _dims[1]);
        (*_u)[(coord[2] * _dims[1] + coord[1]) * _dims[0] + coord[0]] = value;
        assert(std::isfinite(value) && !std::isnan(value));
    }

    template<typename Store>
    template<unsigned SamplingFlags>
        auto ScalarField3D<Store>::Sample(FloatCoord c) const -> ValueType
    {
        // if (constant_expression<(SamplingFlags & RNFSample::Cubic)!=0>::result())
        //     return SampleMonotonicCubic<SamplingFlags>(*this, c);
        return SampleBilinear<SamplingFlags>(*this, c);
    }

    template<typename Store>
        void ScalarField3D<Store>::GatherNeighbors(ValueType neighbours[27], float weights[8], FloatCoord coord, unsigned samplingFlags) const
    {
        XLEMath::GatherNeighbors(neighbours, weights, *this, coord, samplingFlags);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename Store>
        Float3 VectorField3DSeparate<Store>::Load(Coord coord) const
    {
        assert(coord[0] < _dims[0] && coord[1] < _dims[1]);
        return Float3(
            (*_u)[(coord[2] * _dims[1] + coord[1]) * _dims[0] + coord[0]],
            (*_v)[(coord[2] * _dims[1] + coord[1]) * _dims[0] + coord[0]],
            (*_w)[(coord[2] * _dims[1] + coord[1]) * _dims[0] + coord[0]]);
    }

    template<typename Store>
        void VectorField3DSeparate<Store>::Write(Coord coord, ValueType value)
    {
        assert(coord[0] < _dims[0] && coord[1] < _dims[1] && coord[2] < _dims[2]);
        (*_u)[(coord[2] * _dims[1] + coord[1]) * _dims[0] + coord[0]] = value[0];
        (*_v)[(coord[2] * _dims[1] + coord[1]) * _dims[0] + coord[0]] = value[1];
        (*_w)[(coord[2] * _dims[1] + coord[1]) * _dims[0] + coord[0]] = value[2];
        assert(std::isfinite(value[0]) && !std::isnan(value[0]));
        assert(std::isfinite(value[1]) && !std::isnan(value[1]));
    }

    template<typename Store>
    template<unsigned SamplingFlags>
        auto VectorField3DSeparate<Store>::Sample(FloatCoord c) const -> ValueType
    {
        // if (constant_expression<(SamplingFlags & RNFSample::Cubic)!=0>::result())
        //     return SampleMonotonicCubic<SamplingFlags>(*this, c);
        return SampleBilinear<SamplingFlags>(*this, c);
    }

    template<typename Store>
        void VectorField3DSeparate<Store>::GatherNeighbors(ValueType neighbours[27], float weights[8], FloatCoord coord, unsigned samplingFlags) const
    {
        XLEMath::GatherNeighbors(neighbours, weights, *this, coord, samplingFlags);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

#if COMPILER_ACTIVE == COMPILER_TYPE_MSVC
    template<typename Field>
        unsigned InstantiateField()
        {
            using SampleFn = typename Field::ValueType(Field::*)(typename Field::FloatCoord)const;
            SampleFn fns[14] = {
                &Field::Sample<0>,
                &Field::Sample<RNFSample::ClampX|RNFSample::ClampY|RNFSample::ClampZ>,
                &Field::Sample<RNFSample::WrapX |RNFSample::ClampY|RNFSample::ClampZ>,
                &Field::Sample<RNFSample::ClampX|RNFSample::WrapY |RNFSample::ClampZ>,
                &Field::Sample<RNFSample::ClampX|RNFSample::ClampY|RNFSample::WrapZ >,
                &Field::Sample<RNFSample::WrapX |RNFSample::WrapY |RNFSample::WrapZ >,
                &Field::Sample<RNFSample::WrapX |RNFSample::WrapY |RNFSample::ClampZ >,
                &Field::Sample<RNFSample::Cubic>,
                &Field::Sample<RNFSample::Cubic|RNFSample::ClampX|RNFSample::ClampY|RNFSample::ClampZ>,
                &Field::Sample<RNFSample::Cubic|RNFSample::WrapX |RNFSample::ClampY|RNFSample::ClampZ>,
                &Field::Sample<RNFSample::Cubic|RNFSample::ClampX|RNFSample::WrapY |RNFSample::ClampZ>,
                &Field::Sample<RNFSample::Cubic|RNFSample::ClampX|RNFSample::ClampY|RNFSample::WrapZ >,
                &Field::Sample<RNFSample::Cubic|RNFSample::WrapX |RNFSample::WrapY |RNFSample::WrapZ >,
                &Field::Sample<RNFSample::Cubic|RNFSample::WrapX |RNFSample::WrapY |RNFSample::ClampZ >
            };
            (void)fns;
            return 0;
        }

    static const unsigned s_i[] = 
    {
        InstantiateField<ScalarField2D<Eigen::VectorXf>>(),
        InstantiateField<VectorField2DSeparate<Eigen::VectorXf>>(),
        InstantiateField<ScalarField3D<Eigen::VectorXf>>(),
        InstantiateField<VectorField3DSeparate<Eigen::VectorXf>>()
    };
#endif

    template class ScalarField2D<Eigen::VectorXf>;
    template class VectorField2DSeparate<Eigen::VectorXf>;
    template class ScalarField3D<Eigen::VectorXf>;
    template class VectorField3DSeparate<Eigen::VectorXf>;

}

