// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SunSourceConfiguration.h"
#include "ShadowPreparer.h"
#include "ShadowUniforms.h"     // for the attach driver infrastructure
#include "../Techniques/TechniqueUtils.h"
#include "../Techniques/ParsingContext.h"
#include "../Format.h"
#include "../StateDesc.h"
#include "../../ConsoleRig/Console.h"
#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"
#include "../../Math/Transformations.h"
#include "../../Math/ProjectionMath.h"
#include "../../Math/MathSerialization.h"
#include "../../OSServices/Log.h"
#include "../../Utility/BitUtils.h"

#include <sstream>

namespace RenderCore { namespace LightingEngine
{
    static Float4x4 MakeWorldToLight(
        const Float3& negativeLightDirection,
        const Float3& position)
    {
        return InvertOrthonormalTransform(
            MakeCameraToWorld(-negativeLightDirection, Float3(1.f, 0.f, 0.f), position));
    }

    static const unsigned s_staticMaxSubProjections = 6;

    struct OrthoProjections
    {
        Float4x4 _worldToView;
        unsigned _normalProjCount = 0;
        IOrthoShadowProjections::OrthoSubProjection _orthSubProjections[s_staticMaxSubProjections];
    };

    struct ArbitraryProjections
    {
        unsigned _normalProjCount = 0;
        Float4x4 _worldToCamera[s_staticMaxSubProjections];
        Float4x4 _cameraToProjection[s_staticMaxSubProjections];
    };

    static ArbitraryProjections BuildBasicShadowProjections(
        const Float3& negativeLightDirection,
        const RenderCore::Techniques::ProjectionDesc& mainSceneProjectionDesc,
        const SunSourceFrustumSettings& settings)
    {
        using namespace RenderCore::LightingEngine;
        ArbitraryProjections result;

        const float shadowNearPlane = 1.f;
        const float shadowFarPlane = settings._maxDistanceFromCamera;
        static float shadowWidthScale = 3.f;
        static float projectionSizePower = 3.75f;
        float shadowProjectionDist = shadowFarPlane - shadowNearPlane;

        auto cameraPos = ExtractTranslation(mainSceneProjectionDesc._cameraToWorld);
        auto cameraForward = ExtractForward_Cam(mainSceneProjectionDesc._cameraToWorld);

            //  Calculate a simple set of shadow frustums.
            //  This method is non-ideal, but it's just a place holder for now
        result._normalProjCount = 5;
        for (unsigned c=0; c<result._normalProjCount; ++c) {
            const float projectionWidth = shadowWidthScale * std::pow(projectionSizePower, float(c));

            Float3 shiftDirection = cameraForward - negativeLightDirection * Dot(cameraForward, negativeLightDirection);

            Float3 focusPoint = cameraPos + (projectionWidth * 0.45f) * shiftDirection;
            auto lightViewMatrix = MakeWorldToLight(
                negativeLightDirection, focusPoint + (.5f * shadowProjectionDist) * negativeLightDirection);
            result._cameraToProjection[c] = OrthogonalProjection(
                -.5f * projectionWidth, -.5f * projectionWidth,
                 .5f * projectionWidth,  .5f * projectionWidth,
                shadowNearPlane, shadowFarPlane,
                GeometricCoordinateSpace::RightHanded,
                RenderCore::Techniques::GetDefaultClipSpaceType());
            result._worldToCamera[c] = lightViewMatrix;
        }
        
        return result;
    }

    static void CalculateCameraFrustumCornersDirection(
        Float3 result[4],
        const RenderCore::Techniques::ProjectionDesc& projDesc,
        ClipSpaceType clipSpaceType)
    {
        // For the given camera, calculate 4 vectors that represent the
        // the direction from the camera position to the frustum corners
        // (there are 8 frustum corners, but the directions to the far plane corners
        // are the same as the near plane corners)
        Float4x4 projection = projDesc._cameraToProjection;
        Float4x4 noTransCameraToWorld = projDesc._cameraToWorld;
        SetTranslation(noTransCameraToWorld, Float3(0.f, 0.f, 0.f));
        auto trans = Combine(InvertOrthonormalTransform(noTransCameraToWorld), projection);
        Float3 corners[8];
        CalculateAbsFrustumCorners(corners, trans, clipSpaceType);
        for (unsigned c=0; c<4; ++c) {
            result[c] = Normalize(corners[4+c]);    // use the more distance corners, on the far clip plane
        }
    }

    static std::pair<Float4x4, Float4> BuildCameraAlignedOrthogonalShadowProjection(
        const Float3& negativeLightDirection,
        const RenderCore::Techniques::ProjectionDesc& mainSceneProjectionDesc,
        float depth, float maxDistanceFromCamera)
    {
            // Build a special "camera aligned" shadow projection.
            // This can be used to for especially high resolution shadows very close to the
            // near clip plane.
            // First, we build a rough projection-to-world based on the camera right direction...

        auto projRight = ExtractRight_Cam(mainSceneProjectionDesc._cameraToWorld);
        auto projForward = -negativeLightDirection;
        auto projUp = Cross(projRight, projForward);
        auto adjRight = Cross(projForward, projUp);

        auto camPos = ExtractTranslation(mainSceneProjectionDesc._cameraToWorld);
        auto projToWorld = MakeCameraToWorld(projForward, Normalize(projUp), Normalize(adjRight), camPos);
        auto worldToLightProj = InvertOrthonormalTransform(projToWorld);

            // Now we just have to fit the finsl projection around the frustum corners

        auto clipSpaceType = RenderCore::Techniques::GetDefaultClipSpaceType();
        auto reducedDepthProjection = PerspectiveProjection(
            mainSceneProjectionDesc._verticalFov, mainSceneProjectionDesc._aspectRatio,
            mainSceneProjectionDesc._nearClip, depth,
            GeometricCoordinateSpace::RightHanded, clipSpaceType);

        auto worldToReducedDepthProj = Combine(
            InvertOrthonormalTransform(mainSceneProjectionDesc._cameraToWorld), reducedDepthProjection);

        Float3 frustumCorners[8];
        CalculateAbsFrustumCorners(frustumCorners, worldToReducedDepthProj, clipSpaceType);

        Float3 shadowViewSpace[8];
		Float3 shadowViewMins( std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max());
		Float3 shadowViewMaxs(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
		for (unsigned c = 0; c < 8; c++) {
			shadowViewSpace[c] = TransformPoint(worldToLightProj, frustumCorners[c]);

				//	In our right handed coordinate space, the z coordinate in view space should
				//	be negative. But we always specify near & far in positive values. So
				//	we have to swap the sign of z here

			shadowViewSpace[c][2] = -shadowViewSpace[c][2];

			shadowViewMins[0] = std::min(shadowViewMins[0], shadowViewSpace[c][0]);
			shadowViewMins[1] = std::min(shadowViewMins[1], shadowViewSpace[c][1]);
			shadowViewMins[2] = std::min(shadowViewMins[2], shadowViewSpace[c][2]);
			shadowViewMaxs[0] = std::max(shadowViewMaxs[0], shadowViewSpace[c][0]);
			shadowViewMaxs[1] = std::max(shadowViewMaxs[1], shadowViewSpace[c][1]);
			shadowViewMaxs[2] = std::max(shadowViewMaxs[2], shadowViewSpace[c][2]);
		}

        const float shadowNearPlane = -maxDistanceFromCamera;
        const float shadowFarPlane  =  maxDistanceFromCamera;

        Float4x4 projMatrix = OrthogonalProjection(
            shadowViewMins[0], shadowViewMaxs[1], shadowViewMaxs[0], shadowViewMins[1], 
            shadowNearPlane, shadowFarPlane,
            GeometricCoordinateSpace::RightHanded, clipSpaceType);

        auto result = Combine(worldToLightProj, projMatrix);
        return std::make_pair(result, ExtractMinimalProjection(projMatrix));
    }

    static OrthoProjections BuildSimpleOrthogonalShadowProjections(
        const Float3& negativeLightDirection,
        const RenderCore::Techniques::ProjectionDesc& mainSceneProjectionDesc,
        const SunSourceFrustumSettings& settings)
    {
        // We're going to build some basic adaptive shadow frustums. These frustums
        // all fit within the same "definition" orthogonal space. This means that
        // cascades can't be rotated or skewed relative to each other. Usually this 
        // should be fine, (and perhaps might reduce some flickering around the 
        // cascade edges) but it means that the cascades might not be as tightly
        // bound as they might be.

        using namespace RenderCore::LightingEngine;
        using namespace RenderCore;

        OrthoProjections result;
        result._normalProjCount = settings._maxFrustumCount;

        const float shadowNearPlane = -settings._maxDistanceFromCamera;
        const float shadowFarPlane = settings._maxDistanceFromCamera;
        auto clipSpaceType = Techniques::GetDefaultClipSpaceType();

        float t = 0;
        for (unsigned c=0; c<result._normalProjCount; ++c) { t += std::pow(settings._frustumSizeFactor, float(c)); }

        Float3 cameraPos = ExtractTranslation(mainSceneProjectionDesc._cameraToWorld);
        Float3 focusPoint = cameraPos + settings._focusDistance * ExtractForward_Cam(mainSceneProjectionDesc._cameraToWorld);
        auto lightToWorld = MakeCameraToWorld(-negativeLightDirection, ExtractRight_Cam(mainSceneProjectionDesc._cameraToWorld), focusPoint);
        Float4x4 worldToView = InvertOrthonormalTransform(lightToWorld);
        assert(std::isfinite(worldToView(0,3)) && !std::isnan(worldToView(0,3)));
        result._worldToView = worldToView;

            //  Calculate 4 vectors for the directions of the frustum corners, 
            //  relative to the camera position.
        Float3 frustumCornerDir[4];
        CalculateCameraFrustumCornersDirection(frustumCornerDir, mainSceneProjectionDesc, clipSpaceType);

        // Float3 allCascadesMins( std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max());
		// Float3 allCascadesMaxs(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());

		float distanceFromCamera = 0.f;
		for (unsigned f=0; f<result._normalProjCount; ++f) {

			float camNearPlane = distanceFromCamera;
			distanceFromCamera += std::pow(settings._frustumSizeFactor, float(f)) * settings._maxDistanceFromCamera / t;
			float camFarPlane = distanceFromCamera;

                //  Find the frustum corners for this part of the camera frustum,
				//  and then build a shadow frustum that will contain those corners.
				//  Potentially not all of the camera frustum is full of geometry --
				//  if we knew which parts were full, and which were empty, we could
				//  optimise the shadow frustum further.

			Float3 absFrustumCorners[8];
			for (unsigned c = 0; c < 4; ++c) {
				absFrustumCorners[c] = cameraPos + camNearPlane * frustumCornerDir[c];
				absFrustumCorners[4 + c] = cameraPos + camFarPlane * frustumCornerDir[c];
            }

				//	Let's assume that we're not going to rotate the shadow frustum
				//	during this fitting. Then, this is easy... The shadow projection
				//	is orthogonal, so we just need to find the AABB in shadow-view space
				//	for these corners, and the projection parameters will match those very
				//	closely.
                //
                //  Note that we could potentially get a better result if we rotate the
                //  shadow frustum projection to better fit around the projected camera.
                //  It might make shadow texels creep and flicker as the projection changes,
                //  but perhaps a better implementation of this function could try that out.

			Float3 shadowViewSpace[8];
			Float3 shadowViewMins( std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max());
			Float3 shadowViewMaxs(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
			for (unsigned c = 0; c < 8; c++) {
				shadowViewSpace[c] = TransformPoint(worldToView, absFrustumCorners[c]);

					//	In our right handed coordinate space, the z coordinate in view space should
					//	be negative. But we always specify near & far in positive values. So
					//	we have to swap the sign of z here

				shadowViewSpace[c][2] = -shadowViewSpace[c][2];

				shadowViewMins[0] = std::min(shadowViewMins[0], shadowViewSpace[c][0]);
				shadowViewMins[1] = std::min(shadowViewMins[1], shadowViewSpace[c][1]);
				shadowViewMins[2] = std::min(shadowViewMins[2], shadowViewSpace[c][2]);
				shadowViewMaxs[0] = std::max(shadowViewMaxs[0], shadowViewSpace[c][0]);
				shadowViewMaxs[1] = std::max(shadowViewMaxs[1], shadowViewSpace[c][1]);
				shadowViewMaxs[2] = std::max(shadowViewMaxs[2], shadowViewSpace[c][2]);
			}

                //	We have to pull the min depth distance back towards the light
				//	This is so we can capture geometry that is between the light
				//	and the frustum

            shadowViewMins[2] = shadowNearPlane;
            // shadowViewMaxs[2] = shadowFarPlane;

            result._orthSubProjections[f]._leftTopFront = shadowViewMins;
            result._orthSubProjections[f]._rightBottomBack = shadowViewMaxs;

            // allCascadesMins[0] = std::min(allCascadesMins[0], shadowViewMins[0]);
            // allCascadesMins[1] = std::min(allCascadesMins[1], shadowViewMins[1]);
            // allCascadesMins[2] = std::min(allCascadesMins[2], shadowViewMins[2]);
            // allCascadesMaxs[0] = std::max(allCascadesMaxs[0], shadowViewMaxs[0]);
            // allCascadesMaxs[1] = std::max(allCascadesMaxs[1], shadowViewMaxs[1]);
            // allCascadesMaxs[2] = std::max(allCascadesMaxs[2], shadowViewMaxs[2]);
        }

            //  When building the world to clip matrix, we want some to use some projection
            //  that projection that will contain all of the shadow frustums.
            //  We can use allCascadesMins and allCascadesMaxs to find the area of the 
            //  orthogonal space that is actually used. We just have to incorporate these
            //  mins and maxs into the projection matrix

        /*
        Float4x4 clippingProjMatrix = OrthogonalProjection(
            allCascadesMins[0], allCascadesMaxs[1], allCascadesMaxs[0], allCascadesMins[1], 
            shadowNearPlane, shadowFarPlane,
            GeometricCoordinateSpace::RightHanded, clipSpaceType);
        Float4x4 worldToClip = Combine(definitionViewMatrix, clippingProjMatrix);

        std::tie(result._specialNearProjection, result._specialNearMinimalProjection) = 
            BuildCameraAlignedOrthogonalShadowProjection(lightDesc, mainSceneProjectionDesc, 2.5, 30.f);
        result._useNearProj = true;
        */

        return result;
    }

    // We can use either Z or W for tests related to depth in the view frustu,
    // Z can work for either projection or orthogonal, but W is a lot simplier for perspective
    // projections. Using W also isolates us from the inpact of the ReverseZ modes
    #define USE_W_FOR_DEPTH_RANGE_COVERED

    static bool ClipSpaceFurther(const Float4& lhs, float rhs, ClipSpaceType clipSpaceType)
    {
        #if !defined(USE_W_FOR_DEPTH_RANGE_COVERED)
            // In non-reverseZ modes, lhs is further than rhs if it larger
            // In reverseZ mods, lhs is further than rhs if it is smaller
            return (clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ || clipSpaceType == ClipSpaceType::Positive_ReverseZ) ^ (lhs[2] > rhs);
        #else
            return lhs[3] > rhs;
        #endif
    }

    static std::pair<std::vector<Float3>, float> NearestPointNotInsideOrthoProjection(
        const Float4x4& cameraWorldToClip,
        const Float3* absFrustumCorners,
        const Float4x4& orthoViewToWorld,
        const IOrthoShadowProjections::OrthoSubProjection& projection,
        float depthRangeCovered,
        ClipSpaceType clipSpaceType)
    {
        // We need to test the edges of ortho box against the camera frustum
        // and the edges of the ortho box against the frustum
        //
        // Note that the points in "absFrustumCorners" are actually arranged so first 4 and last 4
        // make Z-patterns
        const unsigned edges_zpattern[] = {
            0, 1,
            1, 3,
            3, 2,
            2, 0,

            4, 6,
            6, 7,
            7, 5,
            5, 4,

            0, 4,
            1, 5,
            3, 7,
            2, 6
        };

        auto orthoToClip = Combine(orthoViewToWorld, cameraWorldToClip);
        auto orthoWorldToView = InvertOrthonormalTransform(orthoViewToWorld);

        std::vector<Float4> intersectionPts;

        {
            const Float4 clipSpaceCorners[] = {
                orthoToClip * Expand(Float3{projection._leftTopFront[0], projection._leftTopFront[1], projection._leftTopFront[2]}, 1.0f),
                orthoToClip * Expand(Float3{projection._leftTopFront[0], projection._rightBottomBack[1], projection._leftTopFront[2]}, 1.0f),
                orthoToClip * Expand(Float3{projection._rightBottomBack[0], projection._leftTopFront[1], projection._leftTopFront[2]}, 1.0f),
                orthoToClip * Expand(Float3{projection._rightBottomBack[0], projection._rightBottomBack[1], projection._leftTopFront[2]}, 1.0f),

                orthoToClip * Expand(Float3{projection._leftTopFront[0], projection._leftTopFront[1], projection._rightBottomBack[2]}, 1.0f),
                orthoToClip * Expand(Float3{projection._leftTopFront[0], projection._rightBottomBack[1], projection._rightBottomBack[2]}, 1.0f),
                orthoToClip * Expand(Float3{projection._rightBottomBack[0], projection._leftTopFront[1], projection._rightBottomBack[2]}, 1.0f),
                orthoToClip * Expand(Float3{projection._rightBottomBack[0], projection._rightBottomBack[1], projection._rightBottomBack[2]}, 1.0f)
            };

            for (unsigned e=0; e<dimof(edges_zpattern); e+=2) {
                auto start = clipSpaceCorners[edges_zpattern[e+0]];
                auto end = clipSpaceCorners[edges_zpattern[e+1]];

                for (unsigned ele=0; ele<2; ++ele) {
                    if ((start[ele] < -start[3]) != (end[ele] < -end[3])) {
                        // clip to the [ele] == -w plane
                        // start[ele] + alpha * (end[ele] - start[ele]) = -start[3] + alpha * (start[3] - end[3])
                        // alpha * (end[ele] - start[ele]) - alpha * (start[3] - end[3]) = -start[3] - start[ele]
                        // alpha * ((end[ele] - start[ele]) - (start[3] - end[3])) = -start[3] - start[ele]
                        // alpha = (-start[3] - start[ele]) / ((end[ele] - start[ele]) - (start[3] - end[3]))
                        // alpha = (start[3] + start[ele]) / (start[ele] + start[3] - end[ele] - end[3])
                        auto alpha = (start[3] + start[ele]) / (start[ele] + start[3] - end[ele] - end[3]);
                        assert(alpha >= 0.f && alpha <= 1.0f);
                        Float4 intersection = start + (end-start)*alpha;
                        assert(Equivalent(intersection[ele], -intersection[3], 1e-1f));
                        if (std::abs(intersection[ele^1]) <= intersection[3] && std::abs(intersection[2]) <= intersection[3] && intersection[2] >= 0) {
                            if (ClipSpaceFurther(intersection, depthRangeCovered, clipSpaceType)) {
                                intersectionPts.push_back(intersection);
                            }
                        }
                    }

                    if ((start[ele] > start[3]) != (end[ele] > end[3])) {
                        // clip to the [ele] == w plane
                        // start[ele] + alpha * (end[ele] - start[ele]) = start[3] + alpha * (end[3] - start[3])
                        // alpha * (end[ele] - start[ele]) - alpha * (end[3] - start[3]) = start[3] - start[ele]
                        // alpha = (start[3] - start[ele]) / (end[ele] - end[3] - start[ele] + start[3])
                        auto alpha = (start[3] - start[ele]) / (end[ele] - end[3] - start[ele] + start[3]);
                        assert(alpha >= 0.f && alpha <= 1.0f);
                        Float4 intersection = start + (end-start)*alpha;
                        assert(Equivalent(intersection[ele], intersection[3], 1e-1f));
                        if (std::abs(intersection[ele^1]) <= intersection[3] && std::abs(intersection[2]) <= intersection[3] && intersection[2] >= 0) {
                            if (ClipSpaceFurther(intersection, depthRangeCovered, clipSpaceType)) {
                                intersectionPts.push_back(intersection);
                            }
                        }
                    }
                }
            }
        }

        {
            assert(projection._leftTopFront[0] < projection._rightBottomBack[0]);
            assert(projection._leftTopFront[1] < projection._rightBottomBack[1]);
            assert(projection._leftTopFront[2] < projection._rightBottomBack[2]);

            for (unsigned e=0; e<dimof(edges_zpattern); e+=2) {
                auto start = TransformPoint(orthoWorldToView, absFrustumCorners[edges_zpattern[e+0]]);
                auto end = TransformPoint(orthoWorldToView, absFrustumCorners[edges_zpattern[e+1]]);

                for (unsigned ele=0; ele<3; ++ele) {
                    if ((start[ele] < projection._leftTopFront[ele]) != (end[ele] < projection._leftTopFront[ele])) {
                        float alpha = (projection._leftTopFront[ele] - start[ele]) / (end[ele] - start[ele]);
                        Float3 intersection = start + (end-start)*alpha;
                        if (intersection[(ele+1)%3] >= projection._leftTopFront[(ele+1)%3] && intersection[(ele+1)%3] <= projection._rightBottomBack[(ele+1)%3]
                            && intersection[(ele+2)%3] >= projection._leftTopFront[(ele+2)%3] && intersection[(ele+2)%3] <= projection._rightBottomBack[(ele+2)%3]) {

                            Float4 clipSpace = orthoToClip * Expand(intersection, 1.0f);
                            if (ClipSpaceFurther(clipSpace, depthRangeCovered, clipSpaceType)) {
                                intersectionPts.push_back(clipSpace);
                            }
                        }
                    } 
                    
                    if ((start[ele] > projection._rightBottomBack[ele]) != (end[ele] > projection._rightBottomBack[ele])) {
                        float alpha = (projection._rightBottomBack[ele] - start[ele]) / (end[ele] - start[ele]);
                        Float3 intersection = start + (end-start)*alpha;
                        if (intersection[(ele+1)%3] >= projection._leftTopFront[(ele+1)%3] && intersection[(ele+1)%3] <= projection._rightBottomBack[(ele+1)%3]
                            && intersection[(ele+2)%3] >= projection._leftTopFront[(ele+2)%3] && intersection[(ele+2)%3] <= projection._rightBottomBack[(ele+2)%3]) {

                            Float4 clipSpace = orthoToClip * Expand(intersection, 1.0f);
                            if (ClipSpaceFurther(clipSpace, depthRangeCovered, clipSpaceType)) {
                                intersectionPts.push_back(clipSpace);
                            }
                        }
                    }
                }
            }
        }

        if (!intersectionPts.empty()) {
            auto clipToWorld = Inverse(cameraWorldToClip);
            std::vector<Float3> result;
            // sort closest to camera first
            #if !defined(USE_W_FOR_DEPTH_RANGE_COVERED)
                if (clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ || clipSpaceType == ClipSpaceType::Positive_ReverseZ) {
                    std::sort(intersectionPts.begin(), intersectionPts.end(), [](const auto& lhs, const auto& rhs) { return lhs[2] > rhs[2]; });
                } else
                    std::sort(intersectionPts.begin(), intersectionPts.end(), [](const auto& lhs, const auto& rhs) { return lhs[2] < rhs[2]; });
                const unsigned depthRangeConveredEle = 2;
            #else
                std::sort(intersectionPts.begin(), intersectionPts.end(), [](const auto& lhs, const auto& rhs) { return lhs[3] < rhs[3]; });
                const unsigned depthRangeConveredEle = 3;
            #endif
            result.reserve(intersectionPts.size());
            for (auto& pt:intersectionPts)
                result.push_back(Truncate(clipToWorld * pt));
            return {std::move(result), intersectionPts[0][depthRangeConveredEle]};
        }

        return {{}, depthRangeCovered};
    }

    static Float2 MinAndMaxOrthoSpaceZ(
        const Float4x4& cameraWorldToClip,
        const Float3* absFrustumCorners,
        const Float4x4& orthoViewToWorld,
        const Float2& leftTop2D, const Float2& rightBottom2D,
        const Float4& cameraMiniProj,
        float depthRangeCovered,
        ClipSpaceType clipSpaceType)
    {
        const unsigned edges_zpattern[] = {
            0, 1,
            1, 3,
            3, 2,
            2, 0,

            4, 6,
            6, 7,
            7, 5,
            5, 4,

            0, 4,
            1, 5,
            3, 7,
            2, 6
        };

        auto orthoToClip = Combine(orthoViewToWorld, cameraWorldToClip);
        auto orthoWorldToView = InvertOrthonormalTransform(orthoViewToWorld);
        auto clipToWorld = Inverse(cameraWorldToClip);
        auto clipToOrthoView = Combine(clipToWorld, orthoWorldToView);

        Float2 orthZMinAndMax { FLT_MAX, -FLT_MAX };
        for (unsigned c=0; c<8; ++c) {
            float z = (orthoWorldToView * Float4{absFrustumCorners[c], 1.0f})[2];
            orthZMinAndMax[0] = std::min(orthZMinAndMax[0], z);
            orthZMinAndMax[1] = std::max(orthZMinAndMax[1], z);
        }

        Float2 result { FLT_MAX, -FLT_MAX };
        Float3 leftTopFront{leftTop2D[0], leftTop2D[1], orthZMinAndMax[0] - 0.1f};
        Float3 rightBottomBack{rightBottom2D[0], rightBottom2D[1], orthZMinAndMax[1] + 0.1f};

        {
            const Float4 clipSpaceCorners[] = {
                orthoToClip * Expand(Float3{leftTopFront[0], leftTopFront[1], leftTopFront[2]}, 1.0f),
                orthoToClip * Expand(Float3{leftTopFront[0], rightBottomBack[1], leftTopFront[2]}, 1.0f),
                orthoToClip * Expand(Float3{rightBottomBack[0], leftTopFront[1], leftTopFront[2]}, 1.0f),
                orthoToClip * Expand(Float3{rightBottomBack[0], rightBottomBack[1], leftTopFront[2]}, 1.0f),

                orthoToClip * Expand(Float3{leftTopFront[0], leftTopFront[1], rightBottomBack[2]}, 1.0f),
                orthoToClip * Expand(Float3{leftTopFront[0], rightBottomBack[1], rightBottomBack[2]}, 1.0f),
                orthoToClip * Expand(Float3{rightBottomBack[0], leftTopFront[1], rightBottomBack[2]}, 1.0f),
                orthoToClip * Expand(Float3{rightBottomBack[0], rightBottomBack[1], rightBottomBack[2]}, 1.0f)
            };

            for (unsigned e=0; e<dimof(edges_zpattern); e+=2) {
                auto start = clipSpaceCorners[edges_zpattern[e+0]];
                auto end = clipSpaceCorners[edges_zpattern[e+1]];

                for (unsigned ele=0; ele<3; ++ele) {
                    if ((start[ele] < -start[3]) != (end[ele] < -end[3])) {
                        auto alpha = (start[3] + start[ele]) / (start[ele] + start[3] - end[ele] - end[3]);
                        assert(alpha >= 0.f && alpha <= 1.0f);
                        Float4 intersection = start + (end-start)*alpha;
                        assert(Equivalent(intersection[ele], -intersection[3], 1e-1f));
                        if (std::abs(intersection[(ele+1)%3]) <= intersection[3] && std::abs(intersection[(ele+2)%3]) <= intersection[3]) {
                            auto orthoView = clipToOrthoView * intersection;
                            float z = orthoView[2];
                            result[0] = std::min(result[0], z);
                            result[1] = std::max(result[1], z);
                        }
                    }

                    if ((start[ele] > start[3]) != (end[ele] > end[3])) {
                        auto alpha = (start[3] - start[ele]) / (end[ele] - end[3] - start[ele] + start[3]);
                        assert(alpha >= 0.f && alpha <= 1.0f);
                        Float4 intersection = start + (end-start)*alpha;
                        assert(Equivalent(intersection[ele], intersection[3], 1e-1f));
                        if (std::abs(intersection[(ele+1)%3]) <= intersection[3] && std::abs(intersection[(ele+2)%3]) <= intersection[3]) {
                            auto orthoView = clipToOrthoView * intersection;
                            float z = orthoView[2];
                            result[0] = std::min(result[0], z);
                            result[1] = std::max(result[1], z);
                        }
                    }
                }

                // also check against the Z=0 plane (we've already done Z=W above) 
                if (((start[2] < 0) != (end[2] < 0))) {
                    auto alpha = (start[2]) / (start[2] - end[2]);
                    assert(alpha >= 0.f && alpha <= 1.0f);
                    Float4 intersection = start + (end-start)*alpha;
                    assert(Equivalent(intersection[2], 0.f, 1e-1f));
                    if (std::abs(intersection[0]) <= intersection[3] && std::abs(intersection[1]) <= intersection[3]) {
                        auto orthoView = clipToOrthoView * intersection;
                        float z = orthoView[2];
                        result[0] = std::min(result[0], z);
                        result[1] = std::max(result[1], z);
                    }
                } else {
                    assert((start[2]<0) == (end[2]<0));
                }
            }
        }

        {
            assert(leftTopFront[0] < rightBottomBack[0]);
            assert(leftTopFront[1] < rightBottomBack[1]);

            float f, n;
            std::tie(n,f) = CalculateNearAndFarPlane(cameraMiniProj, clipSpaceType);
            #if !defined(USE_W_FOR_DEPTH_RANGE_COVERED)
                float depthAlphaValue = (depthRangeCovered - cameraMiniProj[3]) / cameraMiniProj[2];            
                depthAlphaValue = (-depthAlphaValue - n) / f;
            #else
                float depthAlphaValue = (depthRangeCovered - n) / f;
            #endif

            for (unsigned e=0; e<dimof(edges_zpattern); e+=2) {
                unsigned startPt = edges_zpattern[e+0], endPt = edges_zpattern[e+1];
                Float3 start, end;
                if (startPt < 4) start = TransformPoint(orthoWorldToView, LinearInterpolate(absFrustumCorners[startPt], absFrustumCorners[startPt+4], depthAlphaValue));
                else start = TransformPoint(orthoWorldToView, absFrustumCorners[startPt]);
                if (endPt < 4) end = TransformPoint(orthoWorldToView, LinearInterpolate(absFrustumCorners[endPt], absFrustumCorners[endPt+4], depthAlphaValue));
                else end = TransformPoint(orthoWorldToView, absFrustumCorners[endPt]);

                // points inside of the projection area count
                if (start[0] >= leftTopFront[0] && start[1] >= leftTopFront[1] && start[0] <= rightBottomBack[0] && start[1] <= rightBottomBack[1]) {
                    result[0] = std::min(result[0], start[2]);
                    result[1] = std::max(result[1], start[2]);
                }

                if (end[0] >= leftTopFront[0] && end[1] >= leftTopFront[1] && end[0] <= rightBottomBack[0] && end[1] <= rightBottomBack[1]) {
                    result[0] = std::min(result[0], end[2]);
                    result[1] = std::max(result[1], end[2]);
                }

                for (unsigned ele=0; ele<3; ++ele) {
                    if ((start[ele] < leftTopFront[ele]) != (end[ele] < leftTopFront[ele])) {
                        float alpha = (leftTopFront[ele] - start[ele]) / (end[ele] - start[ele]);
                        Float3 intersection = start + (end-start)*alpha;
                        if (    intersection[(ele+1)%3] >= leftTopFront[(ele+1)%3] && intersection[(ele+1)%3] <= rightBottomBack[(ele+1)%3]
                            &&  intersection[(ele+2)%3] >= leftTopFront[(ele+2)%3] && intersection[(ele+2)%3] <= rightBottomBack[(ele+2)%3]) {
                            float z = intersection[2];
                            result[0] = std::min(result[0], z);
                            result[1] = std::max(result[1], z);
                        }
                    } 
                    
                    if ((start[ele] > rightBottomBack[ele]) != (end[ele] > rightBottomBack[ele])) {
                        float alpha = (rightBottomBack[ele] - start[ele]) / (end[ele] - start[ele]);
                        Float3 intersection = start + (end-start)*alpha;
                        if (    intersection[(ele+1)%3] >= leftTopFront[(ele+1)%3] && intersection[(ele+1)%3] <= rightBottomBack[(ele+1)%3]
                            &&  intersection[(ele+2)%3] >= leftTopFront[(ele+2)%3] && intersection[(ele+2)%3] <= rightBottomBack[(ele+2)%3]) {
                            float z = intersection[2];
                            result[0] = std::min(result[0], z);
                            result[1] = std::max(result[1], z);
                        }
                    }
                }
            }
        }

        if (result[0] < result[1]) {
            const float precisionGraceDistance = 1e-3f;
            result[0] -= std::abs(result[0]) * precisionGraceDistance;
            result[1] += std::abs(result[1]) * precisionGraceDistance;
        }

        return result;
    }

    static Float4x4 MakeOrientedShadowViewToWorld(const Float3& lightDirection, const Float3& positiveX, const Float3& focusPoint)
    {
        Float3 up = Normalize(Cross(positiveX, lightDirection));
        Float3 adjustedRight = Normalize(cross(lightDirection, up));
        return MakeCameraToWorld(lightDirection, up, adjustedRight, focusPoint);
    }

    static std::pair<std::optional<IOrthoShadowProjections::OrthoSubProjection>, float> CalculateNextFrustum_UnfilledSpace(
        const RenderCore::Techniques::ProjectionDesc& mainSceneProjectionDesc,
        const Float3* absFrustumCorners,
        const Float4x4& lightViewToWorld,
        const IOrthoShadowProjections::OrthoSubProjection& prev,
        const Float4& cameraMiniProj,
        const float maxProjectionDimsZ,
        float depthRangeCovered,
        ClipSpaceType clipSpaceType)
    {
        // Calculate the next frustum for a set of cascades, based on unfilled space
        // Find the nearest part of the view frustum that is not included in the previous ortho projection
        // & use that to position the new projection starting from that point

        auto closestUncoveredPart = NearestPointNotInsideOrthoProjection(
            mainSceneProjectionDesc._worldToProjection, absFrustumCorners,
            lightViewToWorld,
            prev, depthRangeCovered, clipSpaceType);

        if (!closestUncoveredPart.first.empty()) {

            // We want to position the new projection so that the center point is exactly on the camera forward
            // ray, and so that "closestUncoveredPart" is (most likely) exactly on one of the planes of the
            // ortho box
            //
            // This will mean that the new projection begins exactly where the old projection ended. However,
            // floating point creep here can add up to more than a pixel in screen space, so we need a little
            // bit of tolerance added.
            //
            // So while the first projection can be configured to be off the center ray of the camera, subsequent
            // projections always will be.
            //
            // We have some flexibility over the size of this new frustum -- in theory we could calculate size
            // that would attempt to maintain the same screen space pixel to shadowmap texel ratio -- however,
            // for more distant parts of the view frustum, visual importance also drops off; so we 
            //
            // let's do this in ortho space, where it's going to be a lot easier

            auto newProjectionDimsXY = 3.f * (prev._rightBottomBack[0] - prev._leftTopFront[0]);
            auto newProjectionDimsZ = 3.f * (prev._rightBottomBack[2] - prev._leftTopFront[2]);
            newProjectionDimsZ = std::min(newProjectionDimsZ, maxProjectionDimsZ);

            auto worldToLightView = InvertOrthonormalTransform(lightViewToWorld);
            auto camForwardInOrtho = TransformDirectionVector(worldToLightView, ExtractForward_Cam(mainSceneProjectionDesc._cameraToWorld));
            auto camPositionInOrtho = TransformPoint(worldToLightView, ExtractTranslation(mainSceneProjectionDesc._cameraToWorld));

            Float3 closestUncoveredPartInOrtho = TransformPoint(worldToLightView, closestUncoveredPart.first[0]);
            // Allow for a tiny bit of overlap, both to cover for floating point creep errors, and allow the shader
            // to cross-fade. 2% of the distance to the start of the projection, up to quarter unit max
            float overlap = std::min(Dot(closestUncoveredPartInOrtho, camForwardInOrtho) * 0.02f, 0.25f);
            Float3 focusPositionInOrtho = camPositionInOrtho + (Dot(closestUncoveredPartInOrtho, camForwardInOrtho) - overlap + 0.5f * newProjectionDimsXY) * camForwardInOrtho;

            IOrthoShadowProjections::OrthoSubProjection result;
            result._leftTopFront = Float3 { focusPositionInOrtho[0] - 0.5f * newProjectionDimsXY, focusPositionInOrtho[1] - 0.5f * newProjectionDimsXY, focusPositionInOrtho[2] - 0.5f * newProjectionDimsZ };
            result._rightBottomBack = Float3 { focusPositionInOrtho[0] + 0.5f * newProjectionDimsXY, focusPositionInOrtho[1] + 0.5f * newProjectionDimsXY, focusPositionInOrtho[2] + 0.5f * newProjectionDimsZ };

            auto minAndMaxDepth = MinAndMaxOrthoSpaceZ(mainSceneProjectionDesc._worldToProjection, absFrustumCorners, lightViewToWorld, Truncate(result._leftTopFront), Truncate(result._rightBottomBack), cameraMiniProj, closestUncoveredPart.second, clipSpaceType);
            if (minAndMaxDepth[0] > minAndMaxDepth[1])
                return {{}, closestUncoveredPart.second};

            bool entireViewFrustumCovered = (minAndMaxDepth[1] - newProjectionDimsZ) < minAndMaxDepth[0];
            if (!entireViewFrustumCovered) {
                if (TransformDirectionVector(worldToLightView, ExtractForward_Cam(mainSceneProjectionDesc._cameraToWorld))[2] > 0) {
                    // This is a little awkward because of the way -Z in camera space is forward; we have to be very careful of polarity in all these equations
                    result._leftTopFront[2] = minAndMaxDepth[0];
                    result._rightBottomBack[2] = minAndMaxDepth[0] + newProjectionDimsZ;
                } else {
                    result._leftTopFront[2] = minAndMaxDepth[1] - newProjectionDimsZ;
                    result._rightBottomBack[2] = minAndMaxDepth[1];
                }
            } else {
                result._leftTopFront[2] = minAndMaxDepth[1] - newProjectionDimsZ;
                result._rightBottomBack[2] = minAndMaxDepth[1];
            }
            assert(result._leftTopFront[2] < result._rightBottomBack[2]);

            return {result, closestUncoveredPart.second};
        }

        return {{}, depthRangeCovered};
    }

    static OrthoProjections BuildResolutionNormalizedOrthogonalShadowProjections(
        const Float3& negativeLightDirection,
        const RenderCore::Techniques::ProjectionDesc& mainSceneProjectionDescInit,
        const SunSourceFrustumSettings& settings,
        ClipSpaceType clipSpaceType)
    {
        // Don't adjust normalizedScreenResolution with viewport to avoid moving the shadow distance back and forth with render resolution changes
        const UInt2 normalizedScreenResolution(1920, 1080);
        const unsigned shadowMapResolution = settings._textureSize;
        unsigned shadowMapDepthResolution;
        if (settings._flags & SunSourceFrustumSettings::Flags::HighPrecisionDepths) {
            shadowMapDepthResolution = (1 << 23) - 1;       // high precision depths are a little awkward because it's floating point. But let's just use the size of the mantissa as an underestimate here
        } else
            shadowMapDepthResolution = (1 << 16) - 1;

        // We remove the camera position from the projection desc, because it's not actually important for
        // the calculations, and would just add floating point precision problems. Instead, let's do the
        // calculations as if the camera is at the origin, and then translate the results back to the
        // camera position at the end
        auto mainSceneProjectionDesc = mainSceneProjectionDescInit;
        Float3 extractedCameraPos = ExtractTranslation(mainSceneProjectionDesc._cameraToWorld);
        SetTranslation(mainSceneProjectionDesc._cameraToWorld, Float3{0.f, 0.f, 0.f});
        mainSceneProjectionDesc._worldToProjection = 
            Combine(InvertOrthonormalTransform(mainSceneProjectionDesc._cameraToWorld), mainSceneProjectionDesc._cameraToProjection);

        Float3 cameraForward = ExtractForward_Cam(mainSceneProjectionDesc._cameraToWorld);
        Float3 focusPoint = settings._focusDistance * ExtractForward_Cam(mainSceneProjectionDesc._cameraToWorld);

        assert(!IsOrthogonalProjection(mainSceneProjectionDesc._cameraToProjection));
        auto cameraMiniProj = ExtractMinimalProjection(mainSceneProjectionDesc._cameraToProjection);

        // Limit the depth of the shadow projection to some reasonable fixed value. If we allow it to 
        // get too large, we will get end up with a lot of floating point precision issues when building
        // the frustum
        // This will have an impact on the correct shadow bias values, etc, though
        const float maxProjectionDimsZ = 1.5f * CalculateNearAndFarPlane(cameraMiniProj, clipSpaceType).second;

        // find the dimensions in view space for the focus point
        auto worldToMainCamera = InvertOrthonormalTransform(mainSceneProjectionDesc._cameraToWorld);
        auto viewSpaceFocusPoint = TransformPoint(worldToMainCamera, focusPoint);
        auto clipSpaceFocus = mainSceneProjectionDesc._cameraToProjection * Expand(viewSpaceFocusPoint, 1.0f);
        float w = clipSpaceFocus[3];
        float viewSpaceDimsX = 1.0f / float(normalizedScreenResolution[0]) * 2.0f * w / mainSceneProjectionDesc._cameraToProjection(0,0);
        float viewSpaceDimsY = 1.0f / float(normalizedScreenResolution[1]) * 2.0f * w / mainSceneProjectionDesc._cameraToProjection(1,1);
        
        // choose the minimum absolute value so ultra widescreen isn't disadvantaged
        float viewSpacePixelDims = std::min(std::abs(viewSpaceDimsX), std::abs(viewSpaceDimsY));

        // We want to project the first frustum so that one shadow map texel maps roughly onto one screen pixel
        // (also, we want to keep the depth precision roughly inline with X & Y precision, so this will also impact
        // the depth range for the shadow projection)

        float projectionDimsXY = viewSpacePixelDims * shadowMapResolution;
        float projectionDimsZ = viewSpacePixelDims * shadowMapDepthResolution;
        projectionDimsZ = std::min(maxProjectionDimsZ, projectionDimsZ);

        Float3 shadowAcross = ExtractForward_Cam(mainSceneProjectionDesc._cameraToWorld);
        auto lightViewToWorld = MakeOrientedShadowViewToWorld(-negativeLightDirection, shadowAcross, Float3{0.f, 0.f, 0.f});
        auto worldToLightView = InvertOrthonormalTransform(lightViewToWorld);

        // The first projection must include the near plane of the camera, as well as the focus point
        Float3 nearPlaneLightViewMins { FLT_MAX, FLT_MAX, FLT_MAX }, nearPlaneLightViewMaxs { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        Float3 focusLightView = TransformPoint(worldToLightView, focusPoint);

        Float3 absFrustumCorners[8];
        CalculateAbsFrustumCorners(absFrustumCorners, mainSceneProjectionDesc._worldToProjection, clipSpaceType);
        for (unsigned c=0; c<4; ++c) {
            auto lightView = TransformPoint(worldToLightView, absFrustumCorners[c]);
            nearPlaneLightViewMins[0] = std::min(nearPlaneLightViewMins[0], lightView[0]);
            nearPlaneLightViewMins[1] = std::min(nearPlaneLightViewMins[1], lightView[1]);
            nearPlaneLightViewMins[2] = std::min(nearPlaneLightViewMins[2], lightView[2]);
            nearPlaneLightViewMaxs[0] = std::max(nearPlaneLightViewMaxs[0], lightView[0]);
            nearPlaneLightViewMaxs[1] = std::max(nearPlaneLightViewMaxs[1], lightView[1]);
            nearPlaneLightViewMaxs[2] = std::max(nearPlaneLightViewMaxs[2], lightView[2]);
        }

        IOrthoShadowProjections::OrthoSubProjection firstSubProjection;
        auto camForwardInOrtho = TransformDirectionVector(worldToLightView, ExtractForward_Cam(mainSceneProjectionDesc._cameraToWorld));
        Float3 centerProjectOrtho = 0.5f * projectionDimsXY * camForwardInOrtho;

        firstSubProjection._leftTopFront[0]     = centerProjectOrtho[0] - 0.5f * projectionDimsXY;
        firstSubProjection._rightBottomBack[0]  = centerProjectOrtho[0] + 0.5f * projectionDimsXY;
        firstSubProjection._leftTopFront[1]     = centerProjectOrtho[1] - 0.5f * projectionDimsXY;
        firstSubProjection._rightBottomBack[1]  = centerProjectOrtho[1] + 0.5f * projectionDimsXY;

        float depthRangeClosest;
        #if !defined(USE_W_FOR_DEPTH_RANGE_COVERED)
            if (clipSpaceType == ClipSpaceType::Positive_ReverseZ || clipSpaceType == ClipSpaceType::PositiveRightHanded_ReverseZ) {
                float n, f;
                std::tie(n,f) = CalculateNearAndFarPlane(cameraMiniProj, clipSpaceType);
                depthRangeClosest = (n*n-(n*f)) / (n-f);
            } else {
                depthRangeClosest = 0.f;
            }
        #else
            depthRangeClosest = 0.f;
        #endif

        // We're assuming that geometry closer to the light than the view frustum will be clamped
        // to zero depth here -- so the shadow projection doesn't need to extend all the way to
        // the light
        // Most of the time we want the largest Z value to be sitting right on the edge of the view frustum
        // and then extend the frustum as far negative as the precision depth allows (recalling that -Z is
        // forward in view space). However, if the camera is facing directly into the light (ie, they are in
        // opposite directions), that will pin the shadow projection to the far clip and it's possible that 
        // the shadow frustum won't reach all of the way to the camera. In that case, we pin the positive side
        // of the shadow frustum to the view and extend backwards
        auto minAndMaxDepth = MinAndMaxOrthoSpaceZ(mainSceneProjectionDesc._worldToProjection, absFrustumCorners, lightViewToWorld, Truncate(firstSubProjection._leftTopFront), Truncate(firstSubProjection._rightBottomBack), cameraMiniProj, depthRangeClosest, clipSpaceType);
        assert(projectionDimsZ > 0);
        bool entireViewFrustumCovered = (minAndMaxDepth[1] - projectionDimsZ) < minAndMaxDepth[0];
        if (!entireViewFrustumCovered) {
            if (TransformDirectionVector(worldToLightView, cameraForward)[2] > 0) {
                // This is a little awkward because of the way -Z in camera space is forward; we have to be very careful of polarity in all these equations
                firstSubProjection._leftTopFront[2] = minAndMaxDepth[0];
                firstSubProjection._rightBottomBack[2] = minAndMaxDepth[0] + projectionDimsZ;
            } else {
                firstSubProjection._leftTopFront[2] = minAndMaxDepth[1] - projectionDimsZ;
                firstSubProjection._rightBottomBack[2] = minAndMaxDepth[1];
            }
        } else {
            firstSubProjection._leftTopFront[2] = minAndMaxDepth[1] - projectionDimsZ;
            firstSubProjection._rightBottomBack[2] = minAndMaxDepth[1];
        }
        assert(firstSubProjection._leftTopFront[2] < firstSubProjection._rightBottomBack[2]);

        OrthoProjections result;
        result._worldToView = worldToLightView;
        Combine_IntoRHS(-extractedCameraPos, result._worldToView);        // note -- camera position added back here
        result._normalProjCount = 1;
        result._orthSubProjections[0] = firstSubProjection;

        float depthRangeCovered = depthRangeClosest;
        while (result._normalProjCount < settings._maxFrustumCount) {
            auto next = CalculateNextFrustum_UnfilledSpace(mainSceneProjectionDesc, absFrustumCorners, lightViewToWorld, result._orthSubProjections[result._normalProjCount-1], cameraMiniProj, maxProjectionDimsZ, depthRangeCovered, clipSpaceType);
            if (!next.first.has_value()) break;

            // Log(Debug) << "DepthRangeCovered: " << depthRangeCovered << std::endl;

            result._orthSubProjections[result._normalProjCount] = next.first.value();
            ++result._normalProjCount;
            depthRangeCovered = next.second;
        }

        for (unsigned c=0; c<result._normalProjCount; ++c) {
            std::swap(result._orthSubProjections[c]._leftTopFront[1], result._orthSubProjections[c]._rightBottomBack[1]);
            result._orthSubProjections[c]._leftTopFront[2] = -result._orthSubProjections[c]._leftTopFront[2];
            result._orthSubProjections[c]._rightBottomBack[2] = -result._orthSubProjections[c]._rightBottomBack[2];
            std::swap(result._orthSubProjections[c]._leftTopFront[2], result._orthSubProjections[c]._rightBottomBack[2]);
        }

        /*
            We can calculate how much of the view frustum has been filled, by calling:
            auto nearest = NearestPointNotInsideOrthoProjection(
                mainSceneProjectionDesc._worldToProjection, absFrustumCorners,
                lightViewToWorld, result._orthSubProjections[result._normalProjCount-1],
                depthRangeCovered);
            if nearest.first.empty(), then the entire view frustum is filled. Otherwise
            nearest.second is the deepest Z in clip space for which the entire XY plane
            is filled (though shadows may actually go deaper in some parts of the plane)
        */

        return result;
    }

    namespace Internal
    {
        std::pair<std::vector<IOrthoShadowProjections::OrthoSubProjection>, Float4x4> TestResolutionNormalizedOrthogonalShadowProjections(
            const Float3& negativeLightDirection,
            const RenderCore::Techniques::ProjectionDesc& mainSceneProjectionDesc,
            const SunSourceFrustumSettings& settings,
            ClipSpaceType clipSpaceType)
        {
            auto midway = BuildResolutionNormalizedOrthogonalShadowProjections(negativeLightDirection, mainSceneProjectionDesc, settings, clipSpaceType);
            return {
                {midway._orthSubProjections, &midway._orthSubProjections[midway._normalProjCount]},
                midway._worldToView};
        }
    }

	ShadowOperatorDesc CalculateShadowOperatorDesc(const SunSourceFrustumSettings& settings)
	{
		ShadowOperatorDesc result;
        result._dominantLight = true;
		result._width   = settings._textureSize;
        result._height  = settings._textureSize;
        if (settings._flags & SunSourceFrustumSettings::Flags::HighPrecisionDepths) {
            // note --  currently having problems in Vulkan with reading from the D24_UNORM_XX format
            //          might be better to move to 32 bit format now, anyway
            result._format = RenderCore::Format::D32_FLOAT;
        } else {
            result._format = RenderCore::Format::D16_UNORM;
        }

		if (settings._flags & SunSourceFrustumSettings::Flags::ArbitraryCascades) {
			result._normalProjCount = 5;
			result._enableNearCascade = false;
			result._projectionMode = ShadowProjectionMode::Arbitrary;
		} else {
			result._normalProjCount = settings._maxFrustumCount;
			// result._enableNearCascade = true;
			result._projectionMode = ShadowProjectionMode::Ortho;
		}

        if (settings._flags & SunSourceFrustumSettings::Flags::RayTraced) {
            result._resolveType = ShadowResolveType::RayTraced;
        } else {
            result._resolveType = ShadowResolveType::DepthTexture;
        }

        if (settings._flags & SunSourceFrustumSettings::Flags::CullFrontFaces) {
            result._cullMode = RenderCore::CullMode::Front;
        } else {
            result._cullMode = RenderCore::CullMode::Back;
        }

        // todo -- setup some configuration settings for these
        /*
        result._slopeScaledBias = settings._slopeScaledBias;
        result._depthBiasClamp = settings._depthBiasClamp;
        result._rasterDepthBias = settings._rasterDepthBias;
        result._dsSlopeScaledBias = settings._dsSlopeScaledBias;
        result._dsDepthBiasClamp = settings._dsDepthBiasClamp;
        result._dsRasterDepthBias = settings._dsRasterDepthBias;
        */

        result._filterModel = settings._filterModel;
        result._enableContactHardening = settings._enableContactHardening;
        result._cullMode = settings._cullMode;

		return result;
	}

    class SunSourceFrustumDriver : public Internal::IShadowProjectionDriver, public ISunSourceShadows, public Internal::ILightBase
    {
    public:
        virtual void UpdateProjections(
			const Techniques::ParsingContext& parsingContext,
            IPositionalLightSource& lightSource,
			IOrthoShadowProjections& destination) override
        {
            auto mainSceneProjectionDesc = parsingContext.GetProjectionDesc();
            if (_fixedCamera)
                mainSceneProjectionDesc = _fixedCamera.value();
            auto negativeLightDirection = Normalize(ExtractTranslation(lightSource.GetLocalToWorld()));

            assert(!(_settings._flags & SunSourceFrustumSettings::Flags::ArbitraryCascades));
            // auto t = BuildSimpleOrthogonalShadowProjections(negativeLightDirection, mainSceneProjectionDesc, settings);
            auto t = BuildResolutionNormalizedOrthogonalShadowProjections(negativeLightDirection, mainSceneProjectionDesc, _settings, RenderCore::Techniques::GetDefaultClipSpaceType());
            assert(t._normalProjCount);
            destination.SetOrthoSubProjections(MakeIteratorRange(t._orthSubProjections, &t._orthSubProjections[t._normalProjCount]));
            destination.SetWorldToOrthoView(t._worldToView);
        }

        SunSourceFrustumSettings GetSettings() const override
        {
            return _settings;
        }

        void SetSettings(const SunSourceFrustumSettings&) override
        {
            assert(0);      // not yet implemennted
        }

        void FixMainSceneCamera(const Techniques::ProjectionDesc& projDesc) override
        {
            _fixedCamera = projDesc;
        }

        void UnfixMainSceneCamera() override
        {
            _fixedCamera = {};
        }

        virtual void* QueryInterface(uint64_t interfaceTypeCode) override
        {
            if (interfaceTypeCode == typeid(Internal::IShadowProjectionDriver).hash_code()) {
                return (Internal::IShadowProjectionDriver*)this;
            } else if (interfaceTypeCode == typeid(ISunSourceShadows).hash_code()) {
                return (ISunSourceShadows*)this;
            }
            return nullptr;
        }

        SunSourceFrustumDriver(const SunSourceFrustumSettings& settings) : _settings(settings) {}
    private:
        SunSourceFrustumSettings _settings;
        std::optional<Techniques::ProjectionDesc> _fixedCamera;
    };

    static void ApplyNonFrustumSettings(
        ILightScene& lightScene,
        ILightScene::ShadowProjectionId shadowProjectionId,
        const SunSourceFrustumSettings& settings)
    {
        auto* preparer = lightScene.TryGetShadowProjectionInterface<IDepthTextureResolve>(shadowProjectionId);
        if (preparer) {
            IDepthTextureResolve::Desc desc;
            desc._worldSpaceResolveBias = settings._worldSpaceResolveBias;
            desc._tanBlurAngle = settings._tanBlurAngle;
            desc._minBlurSearch = settings._minBlurSearch;
            desc._maxBlurSearch = settings._maxBlurSearch;
            desc._casterDistanceExtraBias = settings._casterDistanceExtraBias;
            preparer->SetDesc(desc);
        }
    }

    ILightScene::ShadowProjectionId CreateSunSourceShadows(
        ILightScene& lightScene,
        ILightScene::ShadowOperatorId shadowOperatorId,
        ILightScene::LightSourceId associatedLightId,
        const SunSourceFrustumSettings& settings)
    {
        auto shadowProjectionId = lightScene.CreateShadowProjection(shadowOperatorId, associatedLightId);

        ApplyNonFrustumSettings(lightScene, shadowProjectionId, settings);

        auto* attachDriver = lightScene.TryGetShadowProjectionInterface<Internal::IAttachDriver>(shadowProjectionId);
        if (attachDriver) {
            attachDriver->AttachDriver(
                std::make_shared<SunSourceFrustumDriver>(settings));
        } else {
            assert(0);
        }

		/*result._shadowGeneratorDesc = CalculateShadowGeneratorDesc(settings);
		assert(result._shadowGeneratorDesc._enableNearCascade == result._projections._useNearProj);
		assert(result._shadowGeneratorDesc._arrayCount == result._projections.Count());
		assert(result._shadowGeneratorDesc._projectionMode == result._projections._mode);

        return result;*/

        return shadowProjectionId;
    }

    void ConfigureShadowProjectionImmediately(
        ILightScene& lightScene,
        ILightScene::ShadowProjectionId shadowProjectionId,
        ILightScene::LightSourceId associatedLightId,
        const SunSourceFrustumSettings& settings,
        const Techniques::ProjectionDesc& mainSceneProjectionDesc)
    {
        auto* positionalLightSource = lightScene.TryGetLightSourceInterface<IPositionalLightSource>(associatedLightId);
        if (!positionalLightSource)
            Throw(std::runtime_error("Could not find positional light source information in CreateShadowCascades for a sun light source"));

        auto negativeLightDirection = Normalize(ExtractTranslation(positionalLightSource->GetLocalToWorld()));

        assert(!(settings._flags & SunSourceFrustumSettings::Flags::ArbitraryCascades));
        // auto t = BuildSimpleOrthogonalShadowProjections(negativeLightDirection, mainSceneProjectionDesc, settings);
        auto t = BuildResolutionNormalizedOrthogonalShadowProjections(negativeLightDirection, mainSceneProjectionDesc, settings, RenderCore::Techniques::GetDefaultClipSpaceType());
        assert(t._normalProjCount);
        auto* cascades = lightScene.TryGetShadowProjectionInterface<IOrthoShadowProjections>(shadowProjectionId);
        if (cascades) {
            cascades->SetOrthoSubProjections(
                MakeIteratorRange(t._orthSubProjections, &t._orthSubProjections[t._normalProjCount]));
            cascades->SetWorldToOrthoView(t._worldToView);
        }

        ApplyNonFrustumSettings(lightScene, shadowProjectionId, settings);
    }

    SunSourceFrustumSettings::SunSourceFrustumSettings()
    {
        const unsigned frustumCount = 5;
        const float maxDistanceFromCamera = 500.f;        // need really large distance because some models have a 100.f scale factor!
        const float frustumSizeFactor = 3.8f;
        const float focusDistance = 3.f;

        _maxFrustumCount = frustumCount;
        _maxDistanceFromCamera = maxDistanceFromCamera;
        _frustumSizeFactor = frustumSizeFactor;
        _focusDistance = focusDistance;
        _flags = Flags::HighPrecisionDepths;
        _textureSize = 2048;

        // _slopeScaledBias = Tweakable("ShadowSlopeScaledBias", 1.f);
        // _depthBiasClamp = Tweakable("ShadowDepthBiasClamp", 0.f);
        // _rasterDepthBias = Tweakable("ShadowRasterDepthBias", 600);

        // _dsSlopeScaledBias = _slopeScaledBias;
        // _dsDepthBiasClamp = _depthBiasClamp;
        // _dsRasterDepthBias = _rasterDepthBias;

        _worldSpaceResolveBias = 0.025f; // this is world space, so always positive, ReverseZ doesn't matter
        _tanBlurAngle = 0.00436f;
        _minBlurSearch = 0.5f;
        _maxBlurSearch = 25.f;
        _casterDistanceExtraBias = -0.001f;     // note that this should be negative for ReverseZ modes, but positive for non-ReverseZ modes
        _filterModel = ShadowFilterModel::PoissonDisc;
        _enableContactHardening = false;
        _cullMode = CullMode::Back;
    }

}}

#include "../Utility/Meta/ClassAccessors.h"
#include "../Utility/Meta/ClassAccessorsImpl.h"

template<> const ClassAccessors& Legacy_GetAccessors<RenderCore::LightingEngine::SunSourceFrustumSettings>()
{
    using Obj = RenderCore::LightingEngine::SunSourceFrustumSettings;
    static ClassAccessors props(typeid(Obj).hash_code());
    static bool init = false;
    if (!init) {
        props.Add("MaxCascadeCount",
            [](const Obj& obj) { return obj._maxFrustumCount; },
            [](Obj& obj, unsigned value) { obj._maxFrustumCount = Clamp(value, 1u, RenderCore::LightingEngine::s_staticMaxSubProjections); });
        props.Add("MaxDistanceFromCamera",  &Obj:: _maxDistanceFromCamera);
        props.Add("FrustumSizeFactor", &Obj::_frustumSizeFactor);
        props.Add("FocusDistance", &Obj::_focusDistance);
        props.Add("Flags", &Obj::_flags);
        props.Add("TextureSize",
            [](const Obj& obj) { return obj._textureSize; },
            [](Obj& obj, unsigned value) { obj._textureSize = 1<<(IntegerLog2(value-1)+1); });  // ceil to a power of two
        // props.Add("SingleSidedSlopeScaledBias", &Obj::_slopeScaledBias);
        // props.Add("SingleSidedDepthBiasClamp", &Obj::_depthBiasClamp);
        // props.Add("SingleSidedRasterDepthBias", &Obj::_rasterDepthBias);
        // props.Add("DoubleSidedSlopeScaledBias", &Obj::_dsSlopeScaledBias);
        // props.Add("DoubleSidedDepthBiasClamp", &Obj::_dsDepthBiasClamp);
        // props.Add("DoubleSidedRasterDepthBias", &Obj::_dsRasterDepthBias);
        props.Add("WorldSpaceResolveBias", &Obj::_worldSpaceResolveBias);
        props.Add("BlurAngleDegrees",   
            [](const Obj& obj) { return Rad2Deg(XlATan(obj._tanBlurAngle)); },
            [](Obj& obj, float value) { obj._tanBlurAngle = XlTan(Deg2Rad(value)); } );
        props.Add("MinBlurSearch", &Obj::_minBlurSearch);
        props.Add("MaxBlurSearch", &Obj::_maxBlurSearch);
        props.Add("HighPrecisionDepths",
            [](const Obj& obj) { return !!(obj._flags & Obj::Flags::HighPrecisionDepths); },
            [](Obj& obj, bool value) { 
                if (value) obj._flags |= Obj::Flags::HighPrecisionDepths; 
                else obj._flags &= ~Obj::Flags::HighPrecisionDepths; 
            });
        props.Add("CasterDistanceExtraBias", &Obj::_casterDistanceExtraBias);

        props.Add("EnableContactHardening", &Obj::_enableContactHardening);
        AddStringToEnum<RenderCore::LightingEngine::ShadowFilterModel, RenderCore::LightingEngine::AsString, RenderCore::LightingEngine::AsShadowFilterModel>(props, "FilterModel", &Obj::_filterModel);
        AddStringToEnum<RenderCore::CullMode, RenderCore::AsString, RenderCore::AsCullMode>(props, "CullMode", &Obj::_cullMode);
        init = true;
    }
    return props;
}


