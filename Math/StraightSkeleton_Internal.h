// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Vector.h"
#include "Matrix.h"
#include <optional>

// We can define the handiness of 2D space as such:
// If we wanted to rotate the X axis so that it lies on the Y axis, 
// which is the shortest direction to rotate in? Is it clockwise, or counterclockwise?
// "SPACE_HANDINESS_COUNTERCLOCKWISE" corresponds to a space in which +Y points up the page, and +X to the right
// "SPACE_HANDINESS_CLOCKWISE" corresponds to a space in which +Y points down the page, and +X to the right
#define SPACE_HANDINESS_CLOCKWISE 1
#define SPACE_HANDINESS_COUNTERCLOCKWISE 2
#define SPACE_HANDINESS SPACE_HANDINESS_COUNTERCLOCKWISE 

namespace XLEMath
{

	T1(Primitive) static constexpr Primitive GetEpsilon();
	T1(Primitive) static constexpr Primitive GetTimeEpsilon();
	template<> constexpr float GetEpsilon<float>() { return 1e-4f; }
	template<> constexpr double GetEpsilon<double>() { return 1e-8; }
	template<> constexpr float GetTimeEpsilon<float>() { return 1e-4f; }
	template<> constexpr double GetTimeEpsilon<double>() { return 1e-8; }

	template<> inline const Vector2T<int64_t>& Zero<Vector2T<int64_t>>()
    {
        static Vector2T<int64_t> result(0ll, 0ll);
        return result;
    }

	template<> inline const Vector3T<int64_t>& Zero<Vector3T<int64_t>>()
    {
        static Vector3T<int64_t> result(0ll, 0ll, 0ll);
        return result;
    }

    T1(T) auto IsFiniteNumber(T value) -> typename std::enable_if<std::is_floating_point<T>::value, bool>::type
    {
        auto type = std::fpclassify(value);
        return ((type == FP_NORMAL) || (type == FP_SUBNORMAL) || (type == FP_ZERO)) && (value == value);
    }

    T1(T) auto IsFiniteNumber(T) -> typename std::enable_if<!std::is_floating_point<T>::value, bool>::type { return true; }

    template<typename Primitive>
        std::ostream& operator<<(std::ostream& str, const Vector2T<Primitive>& vert)
        {
            return str << vert[0] << ", " << vert[1];
        }

    template<typename Primitive>
        std::ostream& operator<<(std::ostream& str, const Vector3T<Primitive>& vert)
        {
            return str << vert[0] << ", " << vert[1] << ", " << vert[2];
        }

	bool AdaptiveEquivalent(float A, float B, float epsilon)
	{
		// from https://floating-point-gui.de/errors/comparison/
		// More robust way of doing these comparisons; with better support through the whole number line
		// We can also consider just looking at the bit pattern and checking the difference in integer form
		auto absA = std::abs(A);
		auto absB = std::abs(B);
		auto diff = std::abs(A - B);

		if (A == B) {
			return true;
		} else if (A == 0 || B == 0 || (absA + absB < std::numeric_limits<decltype(A)>::min())) {
			return diff < (epsilon * std::numeric_limits<decltype(A)>::min());
		} else { // use relative error
			return diff / (absA + absB) < epsilon;
		}	
	}

	bool AdaptiveEquivalent(double A, double B, double epsilon)
	{
		auto absA = std::abs(A);
		auto absB = std::abs(B);
		auto diff = std::abs(A - B);

		if (A == B) {
			return true;
		} else if (A == 0 || B == 0 || (absA + absB < std::numeric_limits<decltype(A)>::min())) {
			return diff < (epsilon * std::numeric_limits<decltype(A)>::min());
		} else { // use relative error
			return diff / (absA + absB) < epsilon;
		}	
	}

	template<typename Primitive>
		bool AdaptiveEquivalent(Vector2T<Primitive> A, Vector2T<Primitive> B, Primitive epsilon)
	{
		return AdaptiveEquivalent(A[0], B[0], epsilon) && AdaptiveEquivalent(A[1], B[1], epsilon);
	}

	template<typename Primitive>
		bool AdaptiveEquivalent(Vector3T<Primitive> A, Vector3T<Primitive> B, Primitive epsilon)
	{
		return AdaptiveEquivalent(A[0], B[0], epsilon) && AdaptiveEquivalent(A[1], B[1], epsilon) && AdaptiveEquivalent(A[2], B[2], epsilon);
	}

	template<typename Primitive>
		bool AdaptiveEquivalent(Vector4T<Primitive> A, Vector4T<Primitive> B, Primitive epsilon)
	{
		return AdaptiveEquivalent(A[0], B[0], epsilon) && AdaptiveEquivalent(A[1], B[1], epsilon) && AdaptiveEquivalent(A[2], B[2], epsilon) && AdaptiveEquivalent(A[3], B[3], epsilon);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	T1(Primitive) using PointAndTime = Vector3T<Primitive>;

	enum WindingType { Left, Right, Straight, FlatV };
	T1(Primitive) Primitive WindingDeterminant(Vector2T<Primitive> zero, Vector2T<Primitive> one, Vector2T<Primitive> two)
	{
		// This is the 2d dot product of (one - zero) and a vector orthogonal to two - zero
		return (one[0] - zero[0]) * (two[1] - zero[1]) - (one[1] - zero[1]) * (two[0] - zero[0]);
	}

	T1(Primitive) std::pair<WindingType, float> CalculateWindingType(Vector2T<Primitive> zero, Vector2T<Primitive> one, Vector2T<Primitive> two, Primitive threshold)
	{
		auto sign = WindingDeterminant(zero, one, two);
		#if SPACE_HANDINESS == SPACE_HANDINESS_CLOCKWISE
			if (sign > threshold) return Right;
			if (sign < -threshold) return Left;
		#else
			if (sign > threshold) return {Left, sign};
			if (sign < -threshold) return {Right, sign};
		#endif
		float d = (zero[0] - one[0]) * (two[0] - one[0]) + (zero[1] - one[1]) * (two[1] - one[1]);
		return {(d > 0) ? FlatV : Straight, sign};
	}

	T1(Primitive) Vector2T<Primitive> EdgeTangentToMovementDir(Vector2T<Primitive> tangent)
	{
		#if SPACE_HANDINESS == SPACE_HANDINESS_CLOCKWISE
			return Vector2T<Primitive>(tangent[1], -tangent[0]);
		#else
			return Vector2T<Primitive>(-tangent[1], tangent[0]);
		#endif
	}

#if 0
	T1(Primitive) Vector2T<Primitive> CalculateVertexVelocity_FirstMethod(Vector2T<Primitive> vex0, Vector2T<Primitive> vex1, Vector2T<Primitive> vex2)
	{
		// Calculate the velocity of vertex v1, assuming segments vex0->vex1 && vex1->vex2
		// are moving at a constant velocity inwards.
		// Note that the winding order is important. We're assuming these are polygon edge vertices
		// arranged in a clockwise order. This means that v1 will move towards the left side of the
		// segments.

		// let segment 1 be v0->v1
		// let segment 2 be v1->v2
		// let m1,m2 = gradient of segments
		// let u1,u2 = speed in X axis of points on segments
		// let v1,v1 = speed in Y axis of points on segments
		//
		// We're going to center our coordinate system on the initial intersection point, v0
		// We want to know where the intersection point of the 2 segments will be after time 't'
		// (since the intersection point will move in a straight line, we only need to calculate
		// it for t=1)
		//
		// I've calculated this out using basic algebra -- but we may be able to get a more efficient
		// method using vector math.

		if (AdaptiveEquivalent(vex0, vex2, GetEpsilon<Primitive>())) return Zero<Vector2T<Primitive>>();

		auto t0 = Vector2T<Primitive>(vex1-vex0);
		auto t1 = Vector2T<Primitive>(vex2-vex1);

		if (Equivalent(t0, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>())) return Zero<Vector2T<Primitive>>();
		if (Equivalent(t1, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>())) return Zero<Vector2T<Primitive>>();

		// create normal pointing in direction of movement
		auto N0 = Normalize(EdgeTangentToMovementDir(t0));
		auto N1 = Normalize(EdgeTangentToMovementDir(t1));
		auto a = N0[0], b = N0[1];
		auto c = N1[0], d = N1[1];
		const auto t = Primitive(1);		// time = 1.0f, because we're calculating the velocity

		// Now, line1 is 0 = xa + yb - t and line2 is 0 = xc + yd - t

		// we can calculate the intersection of the lines using this formula...
		auto B0 = Primitive(0), B1 = Primitive(0);
		if (d<-GetEpsilon<Primitive>() || d>GetEpsilon<Primitive>()) B0 = a - b*c/d;
		if (c<-GetEpsilon<Primitive>() || c>GetEpsilon<Primitive>()) B1 = b - a*d/c;

		Primitive x, y;
		if (std::abs(B0) > std::abs(B1)) {
			if (B0 > -GetEpsilon<Primitive>() && B0 < GetEpsilon<Primitive>()) return Zero<Vector2T<Primitive>>();
			auto A = Primitive(1) - b/d;
			x = t * A / B0;
			y = (t - x*c) / d;
		} else {
			if (B1 > -GetEpsilon<Primitive>() && B1 < GetEpsilon<Primitive>()) return Zero<Vector2T<Primitive>>();
			auto A = Primitive(1) - a/c;
			y = t * A / B1;
			x = (t - y*d) / c;
		}

		assert(Dot(Vector2T<Primitive>(x, y), N0+N1) > Primitive(0));

		assert(IsFiniteNumber(x) && IsFiniteNumber(y));
		return Vector2T<Primitive>(x, y);
	}
#endif

	T1(Primitive) auto SetMagnitude(Vector2T<Primitive> input, Primitive mag)
		-> typename std::enable_if<!std::is_integral<Primitive>::value, Vector2T<Primitive>>::type
	{
		auto scale = std::hypot(input[0], input[1]);		// (note "scale" becomes promoted to double)
		using Promoted = decltype(scale);
		Vector2T<Primitive> result;
		for (unsigned c=0; c<2; ++c)
			result[c] = input[c] * mag / scale;
		return result;
	}
	
	T1(Primitive) auto SetMagnitude(Vector2T<Primitive> input, Primitive mag)
		-> typename std::enable_if<std::is_integral<Primitive>::value, Vector2T<Primitive>>::type
	{
		auto scale = std::hypot(input[0], input[1]);		// (note "scale" becomes promoted to double)
		using Promoted = decltype(scale);
		Vector2T<Primitive> result;
		for (unsigned c=0; c<2; ++c)
			result[c] = (Primitive)std::round(Promoted(input[c]) * Promoted(mag) / scale);
		return result;
	}

	T1(Primitive) struct PromoteIntegral { using Value = Primitive; };
	template<> struct PromoteIntegral<int> { using Value = int64_t; };
	template<> struct PromoteIntegral<int64_t> { using Value = double; };

	T1(Primitive) std::optional<Vector2T<Primitive>> LineIntersection(
		std::pair<Vector2T<Primitive>, Vector2T<Primitive>> zero,
		std::pair<Vector2T<Primitive>, Vector2T<Primitive>> one)
	{
		// Look for an intersection between infinite lines "zero" and "one".
		// Only parallel lines won't collide.
		// Try to do this so that it's still precise with integer coords

		// We can define the line A->B as: (here sign of result is arbitrary)
		//		x(By-Ay) + y(Ax-Bx) + AyBx - AxBy = 0
		//
		// If we also have line C->D
		//		x(Dy-Cy) + y(Cx-Dx) + CyDx - CxDy = 0
		//
		// Let's simplify:
		//	xu + yv + i = 0
		//  xs + yt + j = 0
		//
		// Solving for simultaneous equations.... If tu != sv, then:
		// x = (it - jv) / (sv - tu)
		// y = (ju - is) / (sv - tu)
		
		// For some primitive types we should promote to higher precision
		//			types here (eg, we will get int32_t overflows if we don't promote here)

		using WorkingPrim = typename PromoteIntegral<Primitive>::Value;
		auto Ax = (WorkingPrim)zero.first[0], Ay = (WorkingPrim)zero.first[1];
		auto Bx = (WorkingPrim)zero.second[0], By = (WorkingPrim)zero.second[1];
		auto Cx = (WorkingPrim)one.first[0], Cy = (WorkingPrim)one.first[1];
		auto Dx = (WorkingPrim)one.second[0], Dy = (WorkingPrim)one.second[1];
		
		auto u = By-Ay, v = Ax-Bx, i = Ay*Bx-Ax*By;
		auto s = Dy-Cy, t = Cx-Dx, j = Cy*Dx-Cx*Dy;

		auto d = s*v - t*u;
		if (!d) return {};
		return Vector2T<Primitive>{ Primitive((i*t - j*v) / d), Primitive((j*u - i*s) / d) };
		// return { Primitive(i*t/d - j*v/d), Primitive(j*u/d - i*s/d) };
	}

	T1(Primitive) std::optional<Vector2T<Primitive>> CalculateVertexVelocity_LineIntersection(Vector2T<Primitive> vex0, Vector2T<Primitive> vex1, Vector2T<Primitive> vex2, Primitive movementTime)
	{
		// For integers, let's simplify the math to try to get the high precision result.
		// We'll simply calculate the two edges at 2 points in time, and find the intersection
		// points at both times (actually vex1 is already an intersection point). Since the intersection always moves in a straight path, we
		// can just use the difference between those intersections to calculate the velocity

		// if (AdaptiveEquivalent(vex0, vex2, GetEpsilon<Primitive>())) return Zero<Vector2T<Primitive>>();

		auto t0 = Vector2T<Primitive>(vex1-vex0);
		auto t1 = Vector2T<Primitive>(vex2-vex1);

		// if (Equivalent(t0, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>())) return Zero<Vector2T<Primitive>>();
		// if (Equivalent(t1, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>())) return Zero<Vector2T<Primitive>>();

		auto N0 = SetMagnitude(EdgeTangentToMovementDir(t0), movementTime);
		auto N1 = SetMagnitude(EdgeTangentToMovementDir(t1), movementTime);
		// if (AdaptiveEquivalent(N0, N1, GetEpsilon<Primitive>()) || AdaptiveEquivalent(N0, Vector2T<Primitive>(-N1), GetEpsilon<Primitive>())) return Zero<Vector2T<Primitive>>();
    
		auto A = vex0 - vex1 + N0;
		auto B = N0;
		auto C = N1;
		auto D = vex2 - vex1 + N1;

		// where do A->B and C->D intersect?
		// result is is the distance travelled in "movementTime"
		return LineIntersection<Primitive>({A, B}, {C, D});
	}

	static const int static_velocityVectorScale = INT32_MAX; // 0x7fff;
	T1(Primitive) struct VelocityVectorScale { static const Primitive Value; };
	template<> struct VelocityVectorScale<int> { static const int Value = static_velocityVectorScale; };
	template<> struct VelocityVectorScale<int64_t> { static const int64_t Value = static_velocityVectorScale; };
	T1(Primitive) const Primitive VelocityVectorScale<Primitive>::Value = Primitive(1);

#if 0
	T1(Primitive) auto CalculateVertexVelocity(Vector2T<Primitive> vex0, Vector2T<Primitive> vex1, Vector2T<Primitive> vex2)
		-> typename std::enable_if<!std::is_integral<Primitive>::value, Vector2T<Primitive>>::type
	{
		Vector2T<Primitive> firstMethod = CalculateVertexVelocity_FirstMethod(vex0, vex1, vex2);
		Vector2T<Primitive> lineIntersection = CalculateVertexVelocity_LineIntersection(vex0, vex1, vex2, Primitive(1));
		(void)firstMethod;
		// assert(AdaptiveEquivalent(firstMethod, lineIntersection, GetEpsilon<Primitive>()));
		// assert(!AdaptiveEquivalent(lineIntersection, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>()));
		return lineIntersection;
	}

	T1(Primitive) auto CalculateVertexVelocity(Vector2T<Primitive> vex0, Vector2T<Primitive> vex1, Vector2T<Primitive> vex2)
		-> typename std::enable_if<std::is_integral<Primitive>::value, Vector2T<Primitive>>::type
	{
		// "CalculateVertexVelocity_FirstMethod" is not accurate when using integer & fixed point.
		// We need to use the line intersection method. This also allow us to scale up the length of the 
		// velocity vector so we represent it using integers.
		return CalculateVertexVelocity_LineIntersection(vex0, vex1, vex2, Primitive(static_velocityVectorScale));
	}

	T1(Primitive) static Primitive CalculateCollapseTime_FirstMethod(Vector2T<Primitive> p0, Vector2T<Primitive> v0, Vector2T<Primitive> p1, Vector2T<Primitive> v1)
	{
		auto d0x = v0[0] - v1[0];
		auto d0y = v0[1] - v1[1];
		if (std::abs(d0x) > std::abs(d0y)) {
			if (std::abs(d0x) < GetEpsilon<Primitive>()) return std::numeric_limits<Primitive>::max();
			auto t = (p1[0] - p0[0]) / d0x;

			auto ySep = p0[1] + t * v0[1] - p1[1] - t * v1[1];
			if (t > 0.f && std::abs(ySep) < (10 * GetEpsilon<Primitive>())) {
				// assert(std::abs(p0[0] + t * v0[0] - p1[0] - t * v1[0]) < GetEpsilon<Primitive>());
				return t;	// (todo -- we could refine with the x results?
			}
		} else {
			if (std::abs(d0y) < GetEpsilon<Primitive>()) return std::numeric_limits<Primitive>::max();
			auto t = (p1[1] - p0[1]) / d0y;

			auto xSep = p0[0] + t * v0[0] - p1[0] - t * v1[0];
			if (t > 0.0f && std::abs(xSep) < (10 * GetEpsilon<Primitive>())) {
				// sassert(std::abs(p0[1] + t * v0[1] - p1[1] - t * v1[1]) < GetEpsilon<Primitive>());
				return t;	// (todo -- we could refine with the y results?
			}
		}

		return std::numeric_limits<Primitive>::max();
	}

	T1(Primitive) static Primitive CalculateCollapseTime_LineIntersection(Vector2T<Primitive> p0, Vector2T<Primitive> v0, Vector2T<Primitive> p1, Vector2T<Primitive> v1)
	{
		// Attempt to find the collapse time for these 2 vertices
		// Since we're doing this with integer coordinates, we should try to pick a method that will work well at limited
		// precision
		// There another way to do this... Effectively we want to find where 3 moving edges intersect in time.
		// We can do that algebraically
		auto intr = LineIntersection<Primitive>({Zero<Vector2T<Primitive>>(), v0}, {p1-p0, p1-p0+v1});
		using PromotedType = Primitive; // typename PromoteIntegral<Primitive>::Value;
		auto t0 = std::numeric_limits<PromotedType>::max();
		auto scale = VelocityVectorScale<Primitive>::Value;
		if (std::abs(v0[0]) > std::abs(v0[1]))	t0 = PromotedType(intr[0]) * PromotedType(scale) / PromotedType(v0[0]);
		else									t0 = PromotedType(intr[1]) * PromotedType(scale) / PromotedType(v0[1]);

		auto t1 = std::numeric_limits<PromotedType>::max();
		if (std::abs(v1[0]) > std::abs(v1[1]))	t1 = PromotedType(intr[0] - p1[0] + p0[0]) * PromotedType(scale) / PromotedType(v1[0]);
		else									t1 = PromotedType(intr[1] - p1[1] + p0[1]) * PromotedType(scale) / PromotedType(v1[1]);

		if (std::abs(t0 - t1) < (50 * GetEpsilon<Primitive>())) {
			auto result = Primitive(t0+t1)/Primitive(2);
			auto test0 = Vector2T<Primitive>(	Primitive(PromotedType(p0[0]) + PromotedType(v0[0]) * PromotedType(result) / PromotedType(scale)),
												Primitive(PromotedType(p0[1]) + PromotedType(v0[1]) * PromotedType(result) / PromotedType(scale)));
			auto test1 = Vector2T<Primitive>(	Primitive(PromotedType(p1[0]) + PromotedType(v1[0]) * PromotedType(result) / PromotedType(scale)),
												Primitive(PromotedType(p1[1]) + PromotedType(v1[1]) * PromotedType(result) / PromotedType(scale)));
			assert(AdaptiveEquivalent(test0, Vector2T<Primitive>(intr + p0), 50 * GetEpsilon<Primitive>()));
			assert(AdaptiveEquivalent(test1, Vector2T<Primitive>(intr + p0), 50 * GetEpsilon<Primitive>()));
			(void)test0; (void)test1;
			return result;
		}
		return std::numeric_limits<Primitive>::max();
	}

	T1(Primitive) auto CalculateCollapseTime(Vector2T<Primitive> p0, Vector2T<Primitive> v0, Vector2T<Primitive> p1, Vector2T<Primitive> v1)
		-> typename std::enable_if<!std::is_integral<Primitive>::value, Primitive>::type
	{
		auto firstMethod = CalculateCollapseTime_FirstMethod(p0, v0, p1, v1);
		assert(firstMethod > 0.f);
		return firstMethod;
		/*auto lineIntersection = CalculateCollapseTime_LineIntersection(p0, v0, p1, v1);
		(void)firstMethod;
		//assert(AdaptiveEquivalent(firstMethod, lineIntersection, GetEpsilon<Primitive>()));
		return lineIntersection;*/
	}

	T1(Primitive) auto CalculateCollapseTime(Vector2T<Primitive> p0, Vector2T<Primitive> v0, Vector2T<Primitive> p1, Vector2T<Primitive> v1)
		-> typename std::enable_if<std::is_integral<Primitive>::value, Primitive>::type
	{
		return CalculateCollapseTime_LineIntersection(p0, v0, p1, v1);
	}

	T1(Primitive) static Vector3T<Primitive> CalculateTriangleCollapse(Vector2T<Primitive> p0, Vector2T<Primitive> p1, Vector2T<Primitive> p2)
	{
		Matrix3x3T<Primitive> M;
		Vector3T<Primitive> res;
		Vector2T<Primitive> As[] = { p0, p1, p2 };
		Vector2T<Primitive> Bs[] = { p1, p2, p0 };
		for (unsigned c=0; c<3; ++c) {
			auto mag = (Primitive)std::hypot(Bs[c][0] - As[c][0], Bs[c][1] - As[c][1]);
			auto Nx = Primitive((As[c][1] - Bs[c][1]) * VelocityVectorScale<Primitive>::Value / mag);
			auto Ny = Primitive((Bs[c][0] - As[c][0]) * VelocityVectorScale<Primitive>::Value / mag);
			M(c, 0) = Nx;
			M(c, 1) = Ny;
			M(c, 2) = -Nx*Nx-Ny*Ny;
			res[c]  = As[c][0] * Nx + As[c][1] * Ny;
		}
		return cml::inverse(M) * res;
	}

	T1(Primitive) static Vector3T<Primitive> CalculateTriangleCollapse_Offset(Vector2T<Primitive> p0, Vector2T<Primitive> p1, Vector2T<Primitive> p2)
	{
		Matrix3x3T<Primitive> M;
		Vector3T<Primitive> res;
		Vector2T<Primitive> As[] = { Zero<Vector2T<Primitive>>(), p1 - p0, p2 - p0 };
		Vector2T<Primitive> Bs[] = { p1 - p0, p2 - p0, Zero<Vector2T<Primitive>>() };
		for (unsigned c=0; c<3; ++c) {
			auto mag = (Primitive)std::hypot(Bs[c][0] - As[c][0], Bs[c][1] - As[c][1]);
			auto Nx = Primitive((As[c][1] - Bs[c][1]) * VelocityVectorScale<Primitive>::Value / mag);
			auto Ny = Primitive((Bs[c][0] - As[c][0]) * VelocityVectorScale<Primitive>::Value / mag);
			M(c, 0) = Nx;
			M(c, 1) = Ny;
			M(c, 2) = -Nx*Nx-Ny*Ny;
			res[c]  = As[c][0] * Nx + As[c][1] * Ny;
		}
		auto result = cml::inverse(M) * res;
		result[0] += p0[0];
		result[1] += p0[1];
		return result;
	}
#endif

	T1(Primitive) static bool InvertInplaceSafe(Matrix3x3T<Primitive>& M)
	{
		// note -- derived from "inverse.h" in CML.
		// This version will return false if the determinant of the matrix is zero (which means
		// there is no inverse)

        /* Compute cofactors for each entry: */
        auto m_00 = M(1,1)*M(2,2) - M(1,2)*M(2,1);
        auto m_01 = M(1,2)*M(2,0) - M(1,0)*M(2,2);
        auto m_02 = M(1,0)*M(2,1) - M(1,1)*M(2,0);

        auto m_10 = M(0,2)*M(2,1) - M(0,1)*M(2,2);
        auto m_11 = M(0,0)*M(2,2) - M(0,2)*M(2,0);
        auto m_12 = M(0,1)*M(2,0) - M(0,0)*M(2,1);

        auto m_20 = M(0,1)*M(1,2) - M(0,2)*M(1,1);
        auto m_21 = M(0,2)*M(1,0) - M(0,0)*M(1,2);
        auto m_22 = M(0,0)*M(1,1) - M(0,1)*M(1,0);

        /* Compute determinant from the minors: */
        auto Ddenom = (M(0,0)*m_00 + M(0,1)*m_01 + M(0,2)*m_02);

		auto type = std::fpclassify(Ddenom);
        if ((type != FP_NORMAL) && (type != FP_SUBNORMAL))		// zeroes, infinites and nans rejected
			return false;

        /* Assign the inverse as (1/D) * (cofactor matrix)^T: */
        M(0,0) = m_00/Ddenom;  M(0,1) = m_10/Ddenom;  M(0,2) = m_20/Ddenom;
        M(1,0) = m_01/Ddenom;  M(1,1) = m_11/Ddenom;  M(1,2) = m_21/Ddenom;
        M(2,0) = m_02/Ddenom;  M(2,1) = m_12/Ddenom;  M(2,2) = m_22/Ddenom;
		return true;
	}

	T1(Primitive) static std::optional<Vector3T<Primitive>> CalculateEdgeCollapse_Offset(Vector2T<Primitive> pm1, Vector2T<Primitive> p0, Vector2T<Primitive> p1, Vector2T<Primitive> p2)
	{
		// If the points are already too close together, the math will not be accurate enough
		// We must just use the current time as a close-enough approximation of the collapse time
		//if (Equivalent(p0, p1, GetEpsilon<Primitive>())) {
		if (p0 == p1) {		// bitwise comparison intended
			// Vector2T<Primitive> pt = (p0 + p1) / Primitive(2);
			// return Expand(pt, Primitive(0));
			return Expand(p0, Primitive(0));
		}

		Matrix3x3T<Primitive> M;
		Vector3T<Primitive> res;
		Vector2T<Primitive> As[] = { pm1 - p0, Zero<Vector2T<Primitive>>(), p1 - p0 };
		Vector2T<Primitive> Bs[] = { Zero<Vector2T<Primitive>>(), p1 - p0, p2 - p0 };
		for (unsigned c=0; c<3; ++c) {
			auto mag = (Primitive)std::hypot(Bs[c][0] - As[c][0], Bs[c][1] - As[c][1]);
			assert(IsFiniteNumber(mag));
			// If pm1->p0 or p1->p2 are too small, we can't accurately calculate the collapse time. This can 
			// happen if there's an earlier collapse event on the left or right of this edge. In these cases,
			// we should process those collapse events first.
			// if (Equivalent(mag, Primitive(0), GetEpsilon<Primitive>()))
				// return {};

			auto Nx = Primitive((As[c][1] - Bs[c][1]) * VelocityVectorScale<Primitive>::Value / mag);
			auto Ny = Primitive((Bs[c][0] - As[c][0]) * VelocityVectorScale<Primitive>::Value / mag);
			assert(Nx != 0 || Ny != 0);
			M(c, 0) = Nx;
			M(c, 1) = Ny;
			M(c, 2) = -Nx*Nx-Ny*Ny;
			res[c]  = As[c][0] * Nx + As[c][1] * Ny;
		}
		if (!InvertInplaceSafe(M))
			return {};

		auto result = M * res;
		assert(IsFiniteNumber(result[0]) && IsFiniteNumber(result[1]) && IsFiniteNumber(result[2]));
		result[0] += p0[0];
		result[1] += p0[1];
		return result;
	}

	T1(Primitive) static std::optional<Vector3T<Primitive>> CalculateColinearEdgeCollapse(Vector2T<Primitive> pm1, Vector2T<Primitive> p0, Vector2T<Primitive> p1, Vector2T<Primitive> p2)
	{
		auto epsilon = GetEpsilon<Primitive>();
		auto magFactor0 = Primitive(4) / MagnitudeSquared(p1 - pm1), magFactor1 = Primitive(4) / MagnitudeSquared(p2 - p0);
		// Primitive magFactor0 = 1, magFactor1 = 1;
		auto winding0 = CalculateWindingType(pm1, p0, p1, epsilon*magFactor0);
		auto winding1 = CalculateWindingType(p0, p1, p2, epsilon*magFactor1);
		if (winding0.first == WindingType::Straight && std::abs(winding0.second) < std::abs(winding1.second)) {
			// See comments below for working
			if (winding1.first == WindingType::Straight) return {};		// everything colinear

			auto movement0 = EdgeTangentToMovementDir<Primitive>(p1 - pm1);
			movement0 /= std::hypot(movement0[0], movement0[1]);
			auto movement1Opt = CalculateVertexVelocity_LineIntersection<Primitive>(p0, p1, p2, 1);
			if (!movement1Opt) return {};
			auto movement1 = movement1Opt.value();

			auto A = movement0[1] - movement1[1], B = p0[1] - p1[1];
			auto C = movement0[0] - movement1[0], D = p0[0] - p1[0];
			auto a = A*A+C*C, b = 2*A*B + 2*C*D, c = B*B+D*D;

			auto q = b*b-4*a*c;
			if (q > 0 && a != 0) {
				auto root0 = (-b + std::sqrt(q)) / (2 * a);
				auto root1 = (-b - std::sqrt(q)) / (2 * a);
				if (root0 >= 0 && root1 >= 0) {
					if (root0 < root1) {
						return Vector3T<Primitive>(p0 + movement0 * root0, root0);
					} else {
						return Vector3T<Primitive>(p0 + movement0 * root1, root1);
					}
				} else if (root0 >= 0) {
					return Vector3T<Primitive>(p0 + movement0 * root0, root0);
				} else if (root1 >= 0) {
					return Vector3T<Primitive>(p0 + movement0 * root1, root1);
				} else {
					if (root0 > root1) {
						return Vector3T<Primitive>(p0 + movement0 * root0, root0);
					} else {
						return Vector3T<Primitive>(p0 + movement0 * root1, root1);
					}
				}
			} else if (a > 0) {
				auto minimum = -b / (2*a);
				auto W = (minimum*A+B), U = (minimum*C+D);
				auto minDistSq = W*W+U*U;
				if (minDistSq < epsilon*epsilon)
					return Vector3T<Primitive>(p0 + movement0 * minimum, minimum);
			}

			return {};

		} else if (winding1.first == WindingType::Straight) {
			if (winding0.first == WindingType::Straight) return {};		// everything colinear

			// p0 -> p1 -> p2 may be colinear
			// assume pm1 -> p1 > p2 is not colinear and try to find a collision point
			//
			// Because 2 edges are colinear, there are an infinite number of valid movement directions for p1
			// (ie, it needn't actually move normal to the edge). But the speed it moves relative to the
			// edge is contrained.
			//
			// So, we could find a collapse solution in almost every case... However this can cause problems
			// in the algorithm because we can end up assuming that a vertex is moving in multiple ways at
			// once. To simplify, we'll constrain p1 to moving only in direction movement1. This will reduce
			// the number of collapses we make, but it's more consistent

			auto movement0Opt = CalculateVertexVelocity_LineIntersection<Primitive>(pm1, p0, p1, 1);
			if (!movement0Opt) return {};
			auto movement0 = movement0Opt.value();
			auto movement1 = EdgeTangentToMovementDir<Primitive>(p2-p0);
			movement1 /= std::hypot(movement1[0], movement1[1]);

			// path 0 = p0 + t * movement0
			// path 1 = p1 + t * movement1
			//
			// x0 = p0x + t * movement0x
			// x1 = p1x + t * movement1x
			// (x0 - x1) = t * (movement0x-movement1x) + p0x - p1x
			//
			// y0 = p0y + t * movement0y
			// y1 = p1y + t * movement1y
			// (y0 - y1) = t * (movement0y-movement1y) + p0y - p1y
			//
			// (y0 - y1)^2 + (x0 - x1)^2 = (t * (movement0y-movement1y) + p0y - p1y)^2 + (t * (movement0x-movement1x) + p0x - p1x)^2
			// A = movement0y-movement1y, B = p0y-p1y
			// C = movement0x-movement1x, D = p0x-p1x
			// = (tA + B)^2 + (tC+D)^2
			// = (A^2+C^2)t^2 + (2AB+2CD)t + B^2+D^2
			// a = A+C, b = 2AB+2CD, c = B^2+D^2
			//
			// either find the intersections with zero at 
			// t = (-b +/- sqrt(b^2 - 4ac)) / 2a
			// or minimum with -b/2a

			auto A = movement0[1] - movement1[1], B = p0[1] - p1[1];
			auto C = movement0[0] - movement1[0], D = p0[0] - p1[0];
			auto a = A*A+C*C, b = 2*A*B + 2*C*D, c = B*B+D*D;

			auto q = b*b-4*a*c;
			if (q > 0 && a != 0) {
				auto root0 = (-b + std::sqrt(q)) / (2 * a);
				auto root1 = (-b - std::sqrt(q)) / (2 * a);
				// Return a positive root if possible, otherwise return the root closest to zero
				if (root0 >= 0 && root1 >= 0) {
					if (root0 < root1) {
						return Vector3T<Primitive>(p1 + movement1 * root0, root0);
					} else {
						return Vector3T<Primitive>(p1 + movement1 * root1, root1);
					}
				} else if (root0 >= 0) {
					return Vector3T<Primitive>(p1 + movement1 * root0, root0);
				} else if (root1 >= 0) {
					return Vector3T<Primitive>(p1 + movement1 * root1, root1);
				} else {
					if (root0 > root1) {
						return Vector3T<Primitive>(p0 + movement0 * root0, root0);
					} else {
						return Vector3T<Primitive>(p0 + movement0 * root1, root1);
					}
				}
			} else if (a > 0) {
				auto minimum = -b / (2*a);
				auto W = (minimum*A+B), U = (minimum*C+D);
				auto minDistSq = W*W+U*U;
				if (minDistSq < epsilon*epsilon)
					return Vector3T<Primitive>(p1 + movement1 * minimum, minimum);
			}

			return {};
		} else
			return {};
	}

	T1(Primitive) static std::optional<Vector3T<Primitive>> CalculateEdgeCollapse_Offset_ColinearTest(Vector2T<Primitive> pm1, Vector2T<Primitive> p0, Vector2T<Primitive> p1, Vector2T<Primitive> p2)
	{
		auto result = CalculateColinearEdgeCollapse(pm1, p0, p1, p2);
		if (result) return result;
		return CalculateEdgeCollapse_Offset(pm1, p0, p1, p2);
	}

	T1(Primitive) static std::optional<Vector3T<Primitive>> CalculateEdgeCollapse_Offset_ColinearTest_LargeTimeProtection(Vector2T<Primitive> pm1, Vector2T<Primitive> p0, Vector2T<Primitive> p1, Vector2T<Primitive> p2, Vector2T<Primitive> anchor)
	{
		auto resultOpt = CalculateEdgeCollapse_Offset_ColinearTest<Primitive>(pm1-anchor, p0-anchor, p1-anchor, p2-anchor);
		if (resultOpt) {
			auto result = resultOpt.value();
			const Primitive largeTimeOffsetProtection = 512;
			if (result[2] > largeTimeOffsetProtection) {
				return Vector3T<Primitive>(anchor[0] + result[0]/result[2], anchor[1] + result[1]/result[2], 1);
			} else if (result[2] < -largeTimeOffsetProtection) {
				return Vector3T<Primitive>(anchor[0] - result[0]/result[2], anchor[1] - result[1]/result[2], -1);
			} else {
				return Vector3T<Primitive>(anchor[0] + result[0], anchor[1] + result[1], result[2]);
			}
		}
		return {};
	}

#if 0
	T1(Primitive) static Primitive CalculateTriangleCollapse_Area_Internal(
		Vector2T<Primitive> p0, Vector2T<Primitive> p1, Vector2T<Primitive> p2,
		Vector2T<Primitive> v0, Vector2T<Primitive> v1, Vector2T<Primitive> v2)
	{
		auto a = (v1[0]-v0[0])*(v2[1]-v0[1]) - (v2[0]-v0[0])*(v1[1]-v0[1]);
		if (Equivalent(a, Primitive(0), GetEpsilon<Primitive>())) return std::numeric_limits<Primitive>::max();
			
		auto c = (p1[0]-p0[0])*(p2[1]-p0[1]) - (p2[0]-p0[0])*(p1[1]-p0[1]);
		auto b = (p1[0]-p0[0])*(v2[1]-v0[1]) + (v1[0]-v0[0])*(p2[1]-p0[1]) - (p2[0]-p0[0])*(v1[1]-v0[1]) - (v2[0]-v0[0])*(p1[1]-p0[1]);
			
		// x = (-b +/- sqrt(b*b - 4*a*c)) / 2*a
		auto K = b*b - Primitive(4)*a*c;
		if (K < Primitive(0)) return std::numeric_limits<Primitive>::max();

		auto Q = std::sqrt(K);
		Primitive ts[] = {
			Primitive((-b + Q) / (decltype(Q)(2)*a)),
			Primitive((-b - Q) / (decltype(Q)(2)*a))
		};
		if (ts[0] > 0.0f && ts[0] < ts[1]) return ts[0];
		return ts[1];
	}

	T1(Primitive) static Primitive CalculateTriangleCollapse_Area(
		Vector2T<Primitive> p0, Vector2T<Primitive> p1, Vector2T<Primitive> p2,
		Vector2T<Primitive> v0, Vector2T<Primitive> v1, Vector2T<Primitive> v2)
	{
		auto test = CalculateTriangleCollapse_Area_Internal(p0, p1, p2, v0, v1, v2);
		if (test != std::numeric_limits<Primitive>::max())
			return test;

		test = CalculateTriangleCollapse_Area_Internal(p1, p2, p0, v1, v2, v0);
		if (test != std::numeric_limits<Primitive>::max())
			return test;

		test = CalculateTriangleCollapse_Area_Internal(p2, p0, p1, v2, v0, v1);
		if (test != std::numeric_limits<Primitive>::max())
			return test;

		return std::numeric_limits<Primitive>::max();
	}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
	T1(Primitive) static bool BuildCrashEvent_OldMethod(
		Vector3T<Primitive>& pointAndTime,
		Vertex<Primitive> edgeHead, Vertex<Primitive> edgeTail,
		Vertex<Primitive> motorcycle)
	{
		// Attempt to find a crash event between the given motor cycle and the given edge.
		// Since the edge segments are moving, the solution is a little complex
		// We can create a triangle between head, tail & the motorcycle head
		// If there is a collision, the triangle area will be zero at that point.
		// So we can search for a time when the triangle area is zero, and check to see
		// if a collision has actually occurred at that time.
		const auto calcTime = std::max(std::max(edgeHead._initialTime, edgeTail._initialTime), motorcycle._initialTime);
		auto p0 = edgeHead.PositionAtTime(calcTime);
		auto p1 = edgeTail.PositionAtTime(calcTime);
		auto v0 = edgeHead._velocity;
		auto v1 = edgeTail._velocity;

		auto p2 = motorcycle.PositionAtTime(calcTime);
		auto v2 = motorcycle._velocity;

		// 2 * signed triangle area = 
		//		(p1[0]-p0[0]) * (p2[1]-p0[1]) - (p2[0]-p0[0]) * (p1[1]-p0[1])
		//
		// A =	(p1[0]+t*v1[0]-p0[0]-t*v0[0]) * (p2[1]+t*v2[1]-p0[1]-t*v0[1])
		// B =  (p2[0]+t*v2[0]-p0[0]-t*v0[0]) * (p1[1]+t*v1[1]-p0[1]-t*v0[1]);
		//
		// A =   (p1[0]-p0[0]) * (p2[1]+t*v2[1]-p0[1]-t*v0[1])
		//	 + t*(v1[0]-v0[0]) * (p2[1]+t*v2[1]-p0[1]-t*v0[1])
		//
		// A =   (p1[0]-p0[0]) * (p2[1]-p0[1]+t*(v2[1]-v0[1]))
		//	 + t*(v1[0]-v0[0]) * (p2[1]-p0[1]+t*(v2[1]-v0[1]))
		//
		// A =   (p1[0]-p0[0])*(p2[1]-p0[1]) + t*(p1[0]-p0[0])*(v2[1]-v0[1])
		//	 + t*(v1[0]-v0[0])*(p2[1]-p0[1]) + t*t*(v1[0]-v0[0])*(v2[1]-v0[1])
		//
		// B =   (p2[0]-p0[0])*(p1[1]-p0[1]) + t*(p2[0]-p0[0])*(v1[1]-v0[1])
		//	 + t*(v2[0]-v0[0])*(p1[1]-p0[1]) + t*t*(v2[0]-v0[0])*(v1[1]-v0[1])
		//
		// 0 = t*t*a + t*b + c
		// c = (p1[0]-p0[0])*(p2[1]-p0[1]) - (p2[0]-p0[0])*(p1[1]-p0[1])
		// b = (p1[0]-p0[0])*(v2[1]-v0[1]) + (v1[0]-v0[0])*(p2[1]-p0[1]) - (p2[0]-p0[0])*(v1[1]-v0[1]) - (v2[0]-v0[0])*(p1[1]-p0[1])
		// a = (v1[0]-v0[0])*(v2[1]-v0[1]) - (v2[0]-v0[0])*(v1[1]-v0[1])

		auto a = (v1[0]-v0[0])*(v2[1]-v0[1]) - (v2[0]-v0[0])*(v1[1]-v0[1]);
		if (Equivalent(a, Primitive(0), GetEpsilon<Primitive>())) return false;
			
		auto c = (p1[0]-p0[0])*(p2[1]-p0[1]) - (p2[0]-p0[0])*(p1[1]-p0[1]);
		auto b = (p1[0]-p0[0])*(v2[1]-v0[1]) + (v1[0]-v0[0])*(p2[1]-p0[1]) - (p2[0]-p0[0])*(v1[1]-v0[1]) - (v2[0]-v0[0])*(p1[1]-p0[1]);
			
		// x = (-b +/- sqrt(b*b - 4*a*c)) / 2*a
		auto K = b*b - Primitive(4)*a*c;
		if (K < Primitive(0)) return false;

		auto Q = std::sqrt(K);
		Primitive ts[] = {
			calcTime + Primitive((-b + Q) / (decltype(Q)(2)*a)),
			calcTime + Primitive((-b - Q) / (decltype(Q)(2)*a))
		};

		// Is there is a viable collision at either t0 or t1?
		// All 3 points should be on the same line at this point -- so we just need to check if
		// the motorcycle is between them (or intersecting a vertex)
		for (auto t:ts) {
			if (t <= std::max(edgeHead._initialTime, edgeTail._initialTime)) continue;	// don't need to check collisions that happen too late
			auto P0 = edgeHead.PositionAtTime(t);
			auto P1 = edgeTail.PositionAtTime(t);
			auto P2 = motorcycle.PositionAtTime(t);
			if ((Dot(P1-P0, P2-P0) > Primitive(0)) && (Dot(P0-P1, P2-P1) > Primitive(0))) {
				// good collision
				pointAndTime = Expand(P2, t);
				return true;
			} else if (AdaptiveEquivalent(P0, P2, GetEpsilon<Primitive>()) || AdaptiveEquivalent(P1, P2, GetEpsilon<Primitive>())) {
				// collided with vertex (or close enough)
				pointAndTime = Expand(P2, t);
				return true;
			}
		}

		return false;
	}
#endif

	T1(Primitive) static std::optional<PointAndTime<Primitive>> FindCrashEvent(
		Vector2T<Primitive> edgeHead, Vector2T<Primitive> edgeTail, Vector2T<Primitive> motorVelocity)
	{
		// Look for a crash event involving this motorcycle & the given segment
		// Here, we assume that the motorcycle starts at the origin (ie, caller should
		// redefine the coordinate system to suit that requirement)

		auto mag = (Primitive)std::hypot(edgeHead[0] - edgeTail[0], edgeHead[1] - edgeTail[1]);
		assert(IsFiniteNumber(mag));
		//if (Equivalent(mag, Primitive(0), GetEpsilon<Primitive>()))
			//return false;

		Matrix3x3T<Primitive> M;
		Vector3T<Primitive> res;

		// first row tests for intersection with the edge segment (as it's moving along its normal)
		auto Nx = Primitive((edgeTail[1] - edgeHead[1]) * VelocityVectorScale<Primitive>::Value / mag);
		auto Ny = Primitive((edgeHead[0] - edgeTail[0]) * VelocityVectorScale<Primitive>::Value / mag);
		M(0, 0) = Nx;
		M(0, 1) = Ny;
		M(0, 2) = -Nx*Nx-Ny*Ny;
		res[0]  = edgeTail[0] * Nx + edgeTail[1] * Ny;

		// second row tests x component of motorcycle
		// x - t * motorVelocity[0] = 0
		M(1, 0) = Primitive(1);
		M(1, 1) = Primitive(0);
		M(1, 2) = -motorVelocity[0];
		res[1]  = Primitive(0);

		// third row tests y component of motorcycle
		// y - t * motorVelocity[1] = 0
		M(2, 0) = Primitive(0);
		M(2, 1) = Primitive(1);
		M(2, 2) = -motorVelocity[1];
		res[2]  = Primitive(0);

		if (!InvertInplaceSafe(M))
			return {};

		auto crash = M * res;
		assert(IsFiniteNumber(crash[0]) && IsFiniteNumber(crash[1]) && IsFiniteNumber(crash[2]));
		return crash;
	}

	T1(Primitive) static std::optional<PointAndTime<Primitive>> FindCrashEvent(
		Vector2T<Primitive> edgeHead, Vector2T<Primitive> edgeTail, 
		Vector2T<Primitive> motorPrev, Vector2T<Primitive> motorNext)
	{
		Matrix3x3T<Primitive> M;
		Vector3T<Primitive> res;
		Vector2T<Primitive> As[] = { edgeTail, motorPrev, Zero<Vector2T<Primitive>>() };
		Vector2T<Primitive> Bs[] = { edgeHead, Zero<Vector2T<Primitive>>(), motorNext };
		for (unsigned c=0; c<3; ++c) {
			auto mag = (Primitive)std::hypot(Bs[c][0] - As[c][0], Bs[c][1] - As[c][1]);
			assert(IsFiniteNumber(mag));
			// If pm1->p0 or p1->p2 are too small, we can't accurately calculate the collapse time. This can 
			// happen if there's an earlier collapse event on the left or right of this edge. In these cases,
			// we should process those collapse events first.
			//if (Equivalent(mag, Primitive(0), GetEpsilon<Primitive>()))
				//return {};

			auto Nx = Primitive((As[c][1] - Bs[c][1]) * VelocityVectorScale<Primitive>::Value / mag);
			auto Ny = Primitive((Bs[c][0] - As[c][0]) * VelocityVectorScale<Primitive>::Value / mag);
			M(c, 0) = Nx;
			M(c, 1) = Ny;
			M(c, 2) = -Nx*Nx-Ny*Ny;
			res[c]  = As[c][0] * Nx + As[c][1] * Ny;
		}
		if (!InvertInplaceSafe(M))
			return {};

		PointAndTime<Primitive> crash = M * res;
		assert(IsFiniteNumber(crash[0]) && IsFiniteNumber(crash[1]) && IsFiniteNumber(crash[2]));
		return crash;
	}

}
