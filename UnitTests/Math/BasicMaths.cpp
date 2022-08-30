// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Math/Transformations.h"
#include "../../Math/ProjectionMath.h"
#include "../../Math/Geometry.h"
#include <random>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
    static Float3 RandomUnitVector(std::mt19937& rng)
    {
        return SphericalToCartesian(Float3(
            Deg2Rad(std::uniform_real_distribution<float>(-180.f, 180.f)(rng)),
            Deg2Rad(std::uniform_real_distribution<float>(-180.f, 180.f)(rng)),
            1.f));
    }

    static float RandomSign(std::mt19937& rng) { return ((std::uniform_real_distribution<>(-1.f, 1.f)(rng) < 0.f) ? -1.f : 1.f); }
    static Float3 RandomScaleVector(std::mt19937& rng)
    {
        return Float3(
            RandomSign(rng) * std::uniform_real_distribution<float>(0.1f, 10.f)(rng),
            RandomSign(rng) * std::uniform_real_distribution<float>(0.1f, 10.f)(rng),
            RandomSign(rng) * std::uniform_real_distribution<float>(0.1f, 10.f)(rng));
    }

    static Float3 RandomTranslationVector(std::mt19937& rng)
    {
        return Float3(
            std::uniform_real_distribution<float>(-10000.f, 10000.f)(rng),
            std::uniform_real_distribution<float>(-10000.f, 10000.f)(rng),
            std::uniform_real_distribution<float>(-10000.f, 10000.f)(rng));
    }

    TEST_CASE( "BasicMath-TestMethod1", "[math]" )
    {
        // Test some fundamental 3d geometry maths stuff
        // Using Combine_IntoLHS/Combine_IntoRHS, produce 2 complex rotation
        // matrixes. Each should be the inverse of the other.

        const float tolerance = 1.e-5f; 
        
        SECTION("Combine and transform")
        {
            Float4x4 rotA = Identity<Float4x4>();
            Combine_IntoRHS(RotationX(.85f * gPI), rotA);
            Combine_IntoRHS(RotationY(-.35f * gPI), rotA);
            Combine_IntoRHS(RotationZ(.5f  * gPI), rotA);

            Float4x4 rotB = Identity<Float4x4>();
            Combine_IntoRHS(RotationZ(-.5f * gPI), rotB);
            Combine_IntoRHS(RotationY(.35f * gPI), rotB);
            Combine_IntoRHS(RotationX(-.85f * gPI), rotB);

            auto shouldBeIdentity = Combine(rotA, rotB);
            REQUIRE(Equivalent(Identity<Float4x4>(), shouldBeIdentity, tolerance));
            
            auto invRotA = Inverse(rotA);
            auto invRotA2 = InvertOrthonormalTransform(rotA);
            REQUIRE(Equivalent(rotB, invRotA, tolerance));
            REQUIRE(Equivalent(rotB, invRotA2, tolerance));
            
            Float3 starterVector(1.f, 2.f, 3.f);
            auto trans1 = TransformDirectionVector(rotA, starterVector);
            auto trans2 = TransformDirectionVector(rotB, trans1);
            REQUIRE(Equivalent(trans2, starterVector, tolerance));

            auto trans1a = TransformPoint(rotA, starterVector);
            auto trans2a = TransformPointByOrthonormalInverse(rotA, trans1a);
            auto trans3a = TransformPoint(InvertOrthonormalTransform(rotA), trans1a);
            REQUIRE(Equivalent(trans2a, starterVector, tolerance));
            REQUIRE(Equivalent(trans3a, starterVector, tolerance));
        }

            // test different types of rotation construction
        SECTION("Construct rotation matrix")
        {
            auto quat = MakeRotationQuaternion(Normalize(Float3(1.f, 2.f, 3.f)), .6f * gPI);
            auto rotMat = MakeRotationMatrix(Normalize(Float3(1.f, 2.f, 3.f)), .6f * gPI);

            REQUIRE(Equivalent(AsFloat4x4(quat), AsFloat4x4(rotMat), tolerance));
        }
    }

    TEST_CASE( "BasicMath-MatrixAccumulationAndDecomposition", "[math]" )
    {
            // Compare 2 method of building scale/rotation/translation matrices
            // Also check the decomposition is accurate
        std::mt19937 rng(1638462987);
        const unsigned tests = 50000;
        const float tolerance = 1e-4f;
        for (unsigned c2=0; c2<tests; ++c2) {
            auto rotationAxis = RandomUnitVector(rng);
            auto rotationAngle = Deg2Rad(std::uniform_real_distribution<float>(-180.f, 180.f)(rng));
            auto scale = RandomScaleVector(rng);
            auto translation = RandomTranslationVector(rng);

            ScaleRotationTranslationQ srt(
                scale, 
                MakeRotationQuaternion(rotationAxis, rotationAngle),
                translation);

            Float4x4 accumulativeMatrix = Identity<Float4x4>();
            Combine_IntoLHS(accumulativeMatrix, ArbitraryScale(scale));
            auto rotMat = MakeRotationMatrix(rotationAxis, rotationAngle);
            accumulativeMatrix = Combine(accumulativeMatrix, AsFloat4x4(rotMat));
            Combine_IntoLHS(accumulativeMatrix, translation);
            
            auto srtMatrix = AsFloat4x4(srt);
            REQUIRE(Equivalent(srtMatrix, accumulativeMatrix, tolerance)); // "Acculumated matrix does not match ScaleRotationTranslationQ version"

                // note that sometimes the decomposition will be different from the 
                // original scale/rotation values... But the final result will be the same.
                // We can compenstate for this by pushing sign differences in the scale
                // values into the rotation matrix
            ScaleRotationTranslationM decomposed(accumulativeMatrix);
            auto signCompScale = decomposed._scale;
            auto signCompRot = decomposed._rotation;
            for (unsigned c=0; c<3; ++c)
                if (signCompScale[c]<0.f != scale[c]<0.f) {
                    signCompScale[c] *= -1.f;
                    signCompRot(0, c) *= -1.f; signCompRot(1, c) *= -1.f; signCompRot(2, c) *= -1.f;
                }

            REQUIRE(Equivalent(signCompScale, scale, tolerance)); // "Scale in decomposed matrix doesn't match");
            REQUIRE(Equivalent(decomposed._translation, translation, tolerance)); // "Translation in decomposed matrix doesn't match");
            REQUIRE(Equivalent(signCompRot, rotMat, tolerance)); // "Rotation in decomposed matrix doesn't match");

            auto rebuilt = AsFloat4x4(decomposed);
            REQUIRE(Equivalent(srtMatrix, rebuilt, tolerance)); // "Rebuilt matrix doesn't match ScaleRotationTranslationQ matrix");

            // ensure that we can also decompose the rotation matrix part into axis/angle correctly
            // this will only work if we reflection is begin moved from the "rotation" part into the "scale" part correctly
            ArbitraryRotation rot(decomposed._rotation);
            auto recomposedFromArbitraryRotation = AsFloat4x4(
                ScaleRotationTranslationM{
                    decomposed._scale,
                    Truncate3x3(AsFloat4x4(rot)),
                    decomposed._translation});
            auto rebuilt2 = AsFloat4x4(decomposed);
            REQUIRE(Equivalent(srtMatrix, rebuilt2, tolerance));
        }

        // ensure that HasReflection() correctly identifies matrices with reflections, 
        for (unsigned c=0; c<100000; ++c) {
            auto rotationAxis = RandomUnitVector(rng);
            auto rotationAngle = Deg2Rad(std::uniform_real_distribution<float>(-180.f, 180.f)(rng));
            auto scale = RandomScaleVector(rng);
            auto composed4x4 = AsFloat4x4(
                ScaleRotationTranslationM{
                    scale, 
                    MakeRotationMatrix(rotationAxis, rotationAngle),
                    Zero<Float3>()});

            bool hasFlip0 = HasReflection(Truncate3x3(composed4x4));
            bool hasFlip1 = (scale[0] < 0) ^ (scale[1] < 0) ^ (scale[2] < 0);
            REQUIRE(hasFlip0 == hasFlip1);
        }

        // also test matrix with reflection but no rotation
        Float3x3 matrixWithReflection{
            0.f, 1.f, 0.f,
            1.f, 0.f, 0.f,
            0.f, 0.f, 1.f};
        REQUIRE(HasReflection(matrixWithReflection));

        Float3x3 matrixWithoutReflection{
            0.f, 1.f, 0.f,
            1.f, 0.f, 0.f,
            0.f, 0.f, -1.f};
        REQUIRE(!HasReflection(matrixWithoutReflection));
    }

    static bool IsReverseZType(ClipSpaceType clipSpaceType) { return (clipSpaceType == ClipSpaceType::Positive_ReverseZ) || (clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ); }

    TEST_CASE( "BasicMath-ProjectionMath", "[math]" )    
    {
        const ClipSpaceType clipSpaceTypesToTest[] { ClipSpaceType::Positive, ClipSpaceType::Positive_ReverseZ };

        SECTION("ExtractPerspectiveProperties")
        {
            std::mt19937 rng(2634725489);
            const unsigned tests = 10000;
            const float tolerance = 1e-4f;
            for (const auto clipSpaceType:clipSpaceTypesToTest) {
                float farWorstQuality = 0.f;
                for (unsigned c=0; c<tests; ++c) {
                    float fov = Deg2Rad(std::uniform_real_distribution<float>(15.f, 80.f)(rng));
                    float aspect = std::uniform_real_distribution<float>(.5f, 3.f)(rng);
                    float near = std::uniform_real_distribution<float>(0.02f, 1.f)(rng);
                    float far = std::uniform_real_distribution<float>(100.f, 1000.f)(rng);

                    auto proj = PerspectiveProjection(
                        fov, aspect, near, far,
                        GeometricCoordinateSpace::RightHanded,
                        clipSpaceType);

                    float outNear, outFar;
                    float outFOV, outAspect;
                    std::tie(outNear, outFar) = CalculateNearAndFarPlane(
                        ExtractMinimalProjection(proj), clipSpaceType);
                    std::tie(outFOV, outAspect) = CalculateFov(
                        ExtractMinimalProjection(proj), clipSpaceType);

                    REQUIRE(Equivalent(fov, outFOV, fov * tolerance));
                    REQUIRE(Equivalent(aspect, outAspect, aspect * tolerance));
                    REQUIRE(Equivalent(near, outNear, near * tolerance));
                        // Calculations for the far clip are much less accurate. We're
                        // essentially dividing by (-1 + 1/far + 1), so the larger far
                        // is, the more inaccurate it gets (and the variance in the
                        // inaccuracy grows even faster, making random measurements
                        // more fun). We then double the precision loss in our reverse
                        // math test.
                        // Raising the minimum near clip is the best way to reduce this error value 
                    const float farTolerance = IsReverseZType(clipSpaceType) ? 1e-3f : 4.f;
                    REQUIRE(Equivalent(far, outFar, farTolerance));
                    farWorstQuality = std::max(std::abs(far-outFar), farWorstQuality);
                }
            }
        }

        SECTION("ChangeFarClipPlane")
        {
            // Test ChangeFarClipPlane by comparing clip space z/w to view space depth values
            std::mt19937 rng(68639673);
            const unsigned tests = 10000;
            for (const auto clipSpaceType:clipSpaceTypesToTest)
                for (unsigned c=0; c<tests; ++c) {

                    float fov = Deg2Rad(std::uniform_real_distribution<float>(45.f, 80.f)(rng));
                    float aspect = std::uniform_real_distribution<float>(.5f, 3.f)(rng);
                    float near = std::uniform_real_distribution<float>(0.1f, 2.f)(rng);
                    float far = std::uniform_real_distribution<float>(100.f, 1000.f)(rng);

                    auto proj = PerspectiveProjection(
                        fov, aspect, near, far,
                        GeometricCoordinateSpace::RightHanded,
                        clipSpaceType);

                    float adjustedFar = std::uniform_real_distribution<float>(near, 2.f*far)(rng);
                    auto adjustedProj = proj;
                    ChangeFarClipPlane(adjustedProj, adjustedFar, clipSpaceType);

                    for (unsigned ptIdx=0; ptIdx<3; ptIdx++) {
                        Float3 pt = Float3{
                            std::uniform_real_distribution<float>(-.5f,  .5f)(rng),
                            std::uniform_real_distribution<float>(-.5f,  .5f)(rng),
                            std::uniform_real_distribution<float>(-1.5f, -1.f)(rng)}
                            * std::uniform_real_distribution<float>(near, adjustedFar)(rng);
                        
                        auto transformed = proj * Expand(pt, 1.f);
                        auto transformed2 = adjustedProj * Expand(pt, 1.f);
                        // Ensure that clip space far plane determination agrees with view space
                        // We still need some tolerance here, because the perspective math involves
                        // so much creep
                        const float tolerance = IsReverseZType(clipSpaceType) ? (1e-4f * far) : (1e-3f * far);
                        if ((transformed[2] >= 0.f && transformed[2] < transformed[3])) {
                            REQUIRE(pt[2] > -(far+tolerance));
                        } else {
                            REQUIRE(pt[2] < -(far-tolerance));
                        }

                        if ((transformed2[2] >= 0.f && transformed2[2] < transformed2[3])) {
                            REQUIRE(pt[2] > -(adjustedFar+tolerance));
                        } else {
                            REQUIRE(pt[2] < -(adjustedFar-tolerance));
                        }
                        
                        // XY unchanged from ChangeFarClipPlane call
                        REQUIRE(Equivalent(transformed[0], transformed2[0], 1e-5f));
                        REQUIRE(Equivalent(transformed[1], transformed2[1], 1e-5f));
                    }
                }
        }
    }
}
