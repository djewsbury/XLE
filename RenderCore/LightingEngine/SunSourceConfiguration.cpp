// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SunSourceConfiguration.h"
#include "ShadowPreparer.h"
#include "../Techniques/TechniqueUtils.h"
#include "../Format.h"
#include "../StateDesc.h"
#include "../../ConsoleRig/Console.h"
#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"
#include "../../Math/Transformations.h"
#include "../../Math/ProjectionMath.h"
#include "../../Utility/BitUtils.h"

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

    static std::optional<Float3> NearestPointNotInsideOrthoProjection(
        const Float4x4& cameraWorldToClip,
        const Float3* absFrustumCorners,
        const Float4x4& orthoViewToWorld,
        Float3 orthoLeftTopFront,
        Float3 orthoRightBottomBack)
    {
        // We need to test the edges of ortho box against the camera frustum
        // and the edges of the ortho box against the frustum

        const unsigned edges[] = {
            0, 1,
            1, 2,
            2, 3,
            3, 0,

            4, 5,
            5, 6,
            6, 7,
            7, 4,

            0, 4,
            1, 5,
            2, 6,
            3, 7
        };

        auto orthoToClip = Combine(orthoViewToWorld, cameraWorldToClip);
        auto orthoWorldToView = InvertOrthonormalTransform(orthoViewToWorld);

        float closestDepth = FLT_MAX;
        std::optional<Float4> closestPoint;

        {
            const Float4 clipSpaceCorners[] = {
                orthoToClip * Expand(Float3{orthoLeftTopFront[0], orthoLeftTopFront[1], orthoLeftTopFront[2]}, 1.0f),
                orthoToClip * Expand(Float3{orthoRightBottomBack[0], orthoLeftTopFront[1], orthoLeftTopFront[2]}, 1.0f),
                orthoToClip * Expand(Float3{orthoLeftTopFront[0], orthoRightBottomBack[1], orthoLeftTopFront[2]}, 1.0f),
                orthoToClip * Expand(Float3{orthoRightBottomBack[0], orthoRightBottomBack[1], orthoLeftTopFront[2]}, 1.0f),

                orthoToClip * Expand(Float3{orthoLeftTopFront[0], orthoLeftTopFront[1], orthoRightBottomBack[2]}, 1.0f),
                orthoToClip * Expand(Float3{orthoRightBottomBack[0], orthoLeftTopFront[1], orthoRightBottomBack[2]}, 1.0f),
                orthoToClip * Expand(Float3{orthoLeftTopFront[0], orthoRightBottomBack[1], orthoRightBottomBack[2]}, 1.0f),
                orthoToClip * Expand(Float3{orthoRightBottomBack[0], orthoRightBottomBack[1], orthoRightBottomBack[2]}, 1.0f)
            };

            for (unsigned e=0; e<dimof(edges); e+=2) {
                auto start = clipSpaceCorners[edges[e+0]];
                auto end = clipSpaceCorners[edges[e+1]];

                for (unsigned ele=0; ele<2; ++ele) {
                    float a = start[ele] / start[3], b = start[ele] / start[3]; 
                    if ((a < -1.0f) != (b < -1.0f)) {
                        auto alpha = (-1.0f - a) / (b - a);
                        auto intersection = start + (end-start)*alpha;
                        assert(Equivalent(intersection[ele] / intersection[3], -1.0f, 1e-4f));
                        if (std::abs(intersection[ele^1]) <= intersection[3] && std::abs(intersection[2]) <= intersection[3]) {
                            float depth = intersection[2];
                            if (depth < closestDepth) {
                                closestDepth = depth;
                                closestPoint = intersection;
                            }
                        }
                    }

                    if ((a > 1.0f) != (b > 1.0f)) {
                        auto alpha = (1.0f - a) / (b - a);
                        auto intersection = start + (end-start)*alpha;
                        assert(Equivalent(intersection[ele] / intersection[3], 1.0f, 1e-4f));
                        if (std::abs(intersection[ele^1]) <= intersection[3] && std::abs(intersection[2]) <= intersection[3]) {
                            float depth = intersection[2];
                            if (depth < closestDepth) {
                                closestDepth = depth;
                                closestPoint = intersection;
                            }
                        }
                    }
                }
            }
        }

        {
            assert(orthoLeftTopFront[0] < orthoRightBottomBack[0]);
            assert(orthoLeftTopFront[1] < orthoRightBottomBack[1]);
            assert(orthoLeftTopFront[2] < orthoRightBottomBack[2]);

            for (unsigned e=0; e<dimof(edges); e+=2) {
                auto start = TransformPoint(orthoWorldToView, absFrustumCorners[edges[e+0]]);
                auto end = TransformPoint(orthoWorldToView, absFrustumCorners[edges[e+1]]);

                for (unsigned ele=0; ele<3; ++ele) {
                    if ((start[ele] < orthoLeftTopFront[ele]) != (end[ele] < orthoLeftTopFront[ele])) {
                        float alpha = (orthoLeftTopFront[ele] - start[ele]) / (end[ele] - start[ele]);
                        Float3 intersection = start + (end-start)*alpha;
                        if (intersection[(ele+1)%3] >= orthoLeftTopFront[(ele+1)%3] && intersection[(ele+1)%3] <= orthoRightBottomBack[(ele+1)%3]
                            && intersection[(ele+2)%3] >= orthoLeftTopFront[(ele+2)%3] && intersection[(ele+2)%3] <= orthoRightBottomBack[(ele+2)%3]) {

                            Float4 clipSpace = orthoToClip * Expand(intersection, 1.0f);
                            float depth = clipSpace[2];
                            if (depth < closestDepth) {
                                closestDepth = depth;
                                closestPoint = clipSpace;
                            }
                        }
                    } else if ((start[ele] > orthoRightBottomBack[ele]) != (end[ele] > orthoRightBottomBack[ele])) {
                        float alpha = (orthoRightBottomBack[ele] - start[ele]) / (end[ele] - start[ele]);
                        Float3 intersection = start + (end-start)*alpha;
                        if (intersection[(ele+1)%3] >= orthoLeftTopFront[(ele+1)%3] && intersection[(ele+1)%3] <= orthoRightBottomBack[(ele+1)%3]
                            && intersection[(ele+2)%3] >= orthoLeftTopFront[(ele+2)%3] && intersection[(ele+2)%3] <= orthoRightBottomBack[(ele+2)%3]) {

                            Float4 clipSpace = orthoToClip * Expand(intersection, 1.0f);
                            float depth = clipSpace[2];
                            if (depth < closestDepth) {
                                closestDepth = depth;
                                closestPoint = clipSpace;
                            }
                        }
                    }
                }
            }
        }

        if (closestPoint.has_value())
            return Truncate(Inverse(cameraWorldToClip) * closestPoint.value());

        return {};
    }

    static OrthoProjections BuildResolutionNormalizedOrthogonalShadowProjections(
        const Float3& negativeLightDirection,
        const RenderCore::Techniques::ProjectionDesc& mainSceneProjectionDesc,
        const SunSourceFrustumSettings& settings)
    {
        const UInt2 normalizedScreenResolution(2048, 2048);
        const unsigned shadowMapResolution = 2048;
        const unsigned shadowMapDepthResolution = (1 << 16) - 1;

        Float3 cameraPos = ExtractTranslation(mainSceneProjectionDesc._cameraToWorld);
        Float3 focusPoint = cameraPos + settings._focusDistance * ExtractForward_Cam(mainSceneProjectionDesc._cameraToWorld);

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

        Float3 shadowUp = ExtractRight_Cam(mainSceneProjectionDesc._cameraToWorld);     // we can control if we want the projection to roll here
        auto lightViewToWorld = MakeCameraToWorld(-negativeLightDirection, shadowUp, focusPoint);
        auto worldToLightView = InvertOrthonormalTransform(lightViewToWorld);

        // The first projection must include the near plane of the camera, as well as the focus point
        Float3 nearPlaneLightViewMins { FLT_MAX, FLT_MAX, FLT_MAX }, nearPlaneLightViewMaxs { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        Float3 focusLightView = TransformPoint(worldToLightView, focusPoint);

        Float3 corners[8];
        CalculateAbsFrustumCorners(corners, mainSceneProjectionDesc._worldToProjection, Techniques::GetDefaultClipSpaceType());
        for (unsigned c=0; c<4; ++c) {
            auto lightView = TransformPoint(worldToLightView, corners[c]);
            nearPlaneLightViewMins[0] = std::min(nearPlaneLightViewMins[0], lightView[0]);
            nearPlaneLightViewMins[1] = std::min(nearPlaneLightViewMins[1], lightView[1]);
            nearPlaneLightViewMins[2] = std::min(nearPlaneLightViewMins[2], lightView[2]);
            nearPlaneLightViewMaxs[0] = std::max(nearPlaneLightViewMaxs[0], lightView[0]);
            nearPlaneLightViewMaxs[1] = std::max(nearPlaneLightViewMaxs[1], lightView[1]);
            nearPlaneLightViewMaxs[2] = std::max(nearPlaneLightViewMaxs[2], lightView[2]);
        }

        IOrthoShadowProjections::OrthoSubProjection firstSubProjection;
        if (focusLightView[0] > nearPlaneLightViewMaxs[0]) {
            firstSubProjection._leftTopFront[0] = nearPlaneLightViewMins[0];
            firstSubProjection._rightBottomBack[0] = nearPlaneLightViewMins[0] + projectionDimsXY;
        } else {
            firstSubProjection._leftTopFront[0] = nearPlaneLightViewMaxs[0];
            firstSubProjection._rightBottomBack[0] = nearPlaneLightViewMaxs[0] - projectionDimsXY;
        }

        if (nearPlaneLightViewMins[1] < focusLightView[1] - 0.5f * projectionDimsXY) {
            firstSubProjection._leftTopFront[1] = nearPlaneLightViewMins[1];
            firstSubProjection._rightBottomBack[1] = nearPlaneLightViewMins[1] + projectionDimsXY;
        } else if (nearPlaneLightViewMaxs[1] > focusLightView[1] + 0.5f * projectionDimsXY) {
            firstSubProjection._leftTopFront[1] = nearPlaneLightViewMaxs[1] - projectionDimsXY;
            firstSubProjection._rightBottomBack[1] = nearPlaneLightViewMins[1];
        } else {
            firstSubProjection._leftTopFront[1] = focusLightView[1] - 0.5f * projectionDimsXY;
            firstSubProjection._rightBottomBack[1] = focusLightView[1] + 0.5f * projectionDimsXY;
        }

        // todo -- we should instead set the "near" part to just touch the view frustum, and clamp depth values 
        // less than zero to just zero
        const float depthRangeAmountBeyondFocus = 0.1f;
        firstSubProjection._leftTopFront[2] = focusLightView[2] - (1.f - depthRangeAmountBeyondFocus) * projectionDimsZ;
        firstSubProjection._rightBottomBack[2] = focusLightView[2] + depthRangeAmountBeyondFocus * projectionDimsZ;

        if (firstSubProjection._leftTopFront[0] > firstSubProjection._rightBottomBack[0])
            std::swap(firstSubProjection._leftTopFront[0], firstSubProjection._rightBottomBack[0]);
        if (firstSubProjection._leftTopFront[1] > firstSubProjection._rightBottomBack[1])
            std::swap(firstSubProjection._leftTopFront[1], firstSubProjection._rightBottomBack[1]);
        if (firstSubProjection._leftTopFront[2] > firstSubProjection._rightBottomBack[2])
            std::swap(firstSubProjection._leftTopFront[2], firstSubProjection._rightBottomBack[2]);

        // Find the nearest part of the view frustum that is not included in the ortho projection
        auto closestUncoveredPart = NearestPointNotInsideOrthoProjection(
            mainSceneProjectionDesc._worldToProjection, corners,
            lightViewToWorld,
            firstSubProjection._leftTopFront,
            firstSubProjection._rightBottomBack);

        OrthoProjections result;
        result._worldToView = worldToLightView;
        result._normalProjCount = 1;
        result._orthSubProjections[0] = firstSubProjection;

        if (closestUncoveredPart.has_value()) {

            // One face of the new frustum should on the plane that intersects "closestUncoveredPart",
            // with the normal being the X axis of ortho space
            //
            // But we have freedom to shift the cascade box along the forward and up directions in ortho
            // space. We'll do this by trying to keep aligned with the forward direction of the scene
            // camera
            //
            // let's do this in ortho space, where it's going to be a lot easier

            auto newProjectionDimsXY = 2.0f * projectionDimsXY;

            auto camForwardInOrtho = TransformDirectionVector(worldToLightView, ExtractForward_Cam(mainSceneProjectionDesc._cameraToWorld));
            auto camPositionInOrtho = TransformPoint(worldToLightView, ExtractTranslation(mainSceneProjectionDesc._cameraToWorld));

            auto basisPointInOrtho = TransformPoint(worldToLightView, closestUncoveredPart.value());

            Float3 focusPositionInOrtho;
            if (basisPointInOrtho[0] > camPositionInOrtho[0]) {
                float focusY = basisPointInOrtho[0] + newProjectionDimsXY;
                float alpha = (focusY - camPositionInOrtho[0]) / camForwardInOrtho[0];
                focusPositionInOrtho = camPositionInOrtho + alpha * camForwardInOrtho;
            } else {
                float focusY = basisPointInOrtho[0] - newProjectionDimsXY;
                float alpha = (focusY - camPositionInOrtho[0]) / camForwardInOrtho[0];
                focusPositionInOrtho = camPositionInOrtho + alpha * camForwardInOrtho;
            }

            ++result._normalProjCount;
            result._orthSubProjections[1]._leftTopFront = Float3{ focusPositionInOrtho[0] - newProjectionDimsXY, focusPositionInOrtho[1] - newProjectionDimsXY, firstSubProjection._leftTopFront[2] };
            result._orthSubProjections[1]._rightBottomBack = Float3{ focusPositionInOrtho[0] + newProjectionDimsXY, focusPositionInOrtho[1] + newProjectionDimsXY, firstSubProjection._rightBottomBack[2] };

        }

        return result;
    }

	ShadowOperatorDesc CalculateShadowOperatorDesc(const SunSourceFrustumSettings& settings)
	{
		ShadowOperatorDesc result;
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
			result._normalProjCount = 2; // settings._maxFrustumCount;
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

		return result;
	}

    ILightScene::ShadowProjectionId CreateShadowCascades(
        ILightScene& lightScene,
        ILightScene::ShadowOperatorId shadowOperatorId,
        ILightScene::LightSourceId associatedLightId,
        const RenderCore::Techniques::ProjectionDesc& mainSceneProjectionDesc,
        const SunSourceFrustumSettings& settings)
    {
        auto* positionalLightSource = lightScene.TryGetLightSourceInterface<IPositionalLightSource>(associatedLightId);
        if (!positionalLightSource)
            Throw(std::runtime_error("Could not find positional light source information in CreateShadowCascades for a sun light source"));

        auto negativeLightDirection = Normalize(ExtractTranslation(positionalLightSource->GetLocalToWorld()));

        auto shadowProjectionId = lightScene.CreateShadowProjection(shadowOperatorId, associatedLightId);

        if (settings._flags & SunSourceFrustumSettings::Flags::ArbitraryCascades) {
            auto t = BuildBasicShadowProjections(negativeLightDirection, mainSceneProjectionDesc, settings);
            assert(t._normalProjCount);
            auto* cascades = lightScene.TryGetShadowProjectionInterface<IArbitraryShadowProjections>(shadowProjectionId);
            if (cascades)
                cascades->SetArbitrarySubProjections(
                    MakeIteratorRange(t._worldToCamera, &t._worldToCamera[t._normalProjCount]),
                    MakeIteratorRange(t._cameraToProjection, &t._cameraToProjection[t._normalProjCount]));
        } else {
            // auto t = BuildSimpleOrthogonalShadowProjections(negativeLightDirection, mainSceneProjectionDesc, settings);
            auto t = BuildResolutionNormalizedOrthogonalShadowProjections(negativeLightDirection, mainSceneProjectionDesc, settings);
            assert(t._normalProjCount);
            auto* cascades = lightScene.TryGetShadowProjectionInterface<IOrthoShadowProjections>(shadowProjectionId);
            if (cascades) {
                cascades->SetOrthoSubProjections(
                    MakeIteratorRange(t._orthSubProjections, &t._orthSubProjections[t._normalProjCount]));
                cascades->SetWorldToOrthoView(t._worldToView);
            }
        }

        auto* preparer = lightScene.TryGetShadowProjectionInterface<IShadowPreparer>(shadowProjectionId);
        if (preparer) {
            IShadowPreparer::Desc desc;
            // desc._worldSpaceResolveBias = settings._worldSpaceResolveBias;
            desc._tanBlurAngle = settings._tanBlurAngle;
            desc._minBlurSearch = settings._minBlurSearch;
            desc._maxBlurSearch = settings._maxBlurSearch;
            preparer->SetDesc(desc);
        }

		/*result._shadowGeneratorDesc = CalculateShadowGeneratorDesc(settings);
		assert(result._shadowGeneratorDesc._enableNearCascade == result._projections._useNearProj);
		assert(result._shadowGeneratorDesc._arrayCount == result._projections.Count());
		assert(result._shadowGeneratorDesc._projectionMode == result._projections._mode);

        return result;*/

        return shadowProjectionId;
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

        // _worldSpaceResolveBias = 0.f;   // (-.3f)
        _tanBlurAngle = 0.00436f;
        _minBlurSearch = 0.5f;
        _maxBlurSearch = 25.f;
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
        props.Add("MaxFrustumCount",
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
        // props.Add("WorldSpaceResolveBias", &Obj::_worldSpaceResolveBias);
        props.Add("BlurAngleDegrees",   
            [](const Obj& obj) { return Rad2Deg(XlATan(obj._tanBlurAngle)); },
            [](Obj& obj, float value) { obj._tanBlurAngle = XlTan(Deg2Rad(value)); } );
        props.Add("MinBlurSearch", &Obj::_minBlurSearch);
        props.Add("MaxBlurSearch", &Obj::_maxBlurSearch);
        init = true;
    }
    return props;
}


