// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShadowUniforms.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/TechniqueUtils.h"
#include "../../Math/ProjectionMath.h"
#include "../../Math/Transformations.h"
#include "../../Math/Matrix.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	void BuildShadowConstantBuffers(
		CB_ArbitraryShadowProjection& arbitraryCBSource,
		CB_OrthoShadowProjection& orthoCBSource,
		const MultiProjection<MaxShadowTexturesPerLight>& desc)
	{
		auto frustumCount = desc._normalProjCount;

		XlZeroMemory(arbitraryCBSource);   // unused array elements must be zeroed out
		XlZeroMemory(orthoCBSource);       // unused array elements must be zeroed out

		if (desc._mode == ShadowProjectionMode::Arbitrary || desc._mode == ShadowProjectionMode::ArbitraryCubeMap) {

			arbitraryCBSource._projectionCount = frustumCount;
			for (unsigned c=0; c<frustumCount; ++c) {
				arbitraryCBSource._worldToProj[c] = desc._fullProj[c]._worldToProjTransform;
				arbitraryCBSource._minimalProj[c] = desc._minimalProjection[c];
			}

		} else if (desc._mode == ShadowProjectionMode::Ortho) {

				//  We can still fill in the constant buffer
				//  for arbitrary projection. If we use the right
				//  shader, we shouldn't need this. But we can have it
				//  prepared for comparisons.
			arbitraryCBSource._projectionCount = frustumCount;
			auto baseWorldToView = desc._definitionViewMatrix;

			float p22 = 1.f, p23 = 0.f;

			for (unsigned c=0; c<frustumCount; ++c) {

				auto projMatrix = OrthogonalProjection(
					desc._orthoSub[c]._topLeftFront[0], desc._orthoSub[c]._topLeftFront[1], 
					desc._orthoSub[c]._bottomRightBack[0], desc._orthoSub[c]._bottomRightBack[1], 
					desc._orthoSub[c]._topLeftFront[2], desc._orthoSub[c]._bottomRightBack[2],
					GeometricCoordinateSpace::RightHanded, Techniques::GetDefaultClipSpaceType());
				assert(IsOrthogonalProjection(projMatrix));

				arbitraryCBSource._worldToProj[c] = Combine(baseWorldToView, projMatrix);
				arbitraryCBSource._minimalProj[c] = ExtractMinimalProjection(projMatrix);

				orthoCBSource._cascadeScale[c][0] = projMatrix(0,0);
				orthoCBSource._cascadeScale[c][1] = projMatrix(1,1);
				orthoCBSource._cascadeTrans[c][0] = projMatrix(0,3);
				orthoCBSource._cascadeTrans[c][1] = projMatrix(1,3);

				if (c==0) {
					p22 = projMatrix(2,2);
					p23 = projMatrix(2,3);
				}

					// (unused parts)
				orthoCBSource._cascadeScale[c][2] = 1.f;
				orthoCBSource._cascadeTrans[c][2] = 0.f;
				orthoCBSource._cascadeScale[c][3] = 1.f;
				orthoCBSource._cascadeTrans[c][3] = 0.f;
			}

				// prevent matches on the remaining frustums...
			for (unsigned c=frustumCount; c<MaxShadowTexturesPerLight; ++c) {
				orthoCBSource._cascadeTrans[c][0] = std::numeric_limits<float>::max();
				orthoCBSource._cascadeTrans[c][1] = std::numeric_limits<float>::max();
			}

				//  Also fill in the constants for ortho projection mode
			orthoCBSource._projectionCount = frustumCount;
			orthoCBSource._minimalProjection = desc._minimalProjection[0];

				//  We merge in the transform for the z component
				//  Every cascade uses the same depth range, which means we only
				//  have to adjust the X and Y components for each cascade
			auto zComponentMerge = Identity<Float4x4>();
			zComponentMerge(2,2) = p22;
			zComponentMerge(2,3) = p23;
			orthoCBSource._worldToProj = AsFloat3x4(Combine(baseWorldToView, zComponentMerge));

				// the special "near" cascade is reached via the main transform
			if (desc._useNearProj) {
				orthoCBSource._nearCascade = 
					AsFloat3x4(Combine(
						Inverse(AsFloat4x4(orthoCBSource._worldToProj)), 
						desc._specialNearProjection));
				orthoCBSource._nearMinimalProjection = desc._specialNearMinimalProjection;
			} else {
				orthoCBSource._nearCascade = Identity<Float3x4>();
				orthoCBSource._nearMinimalProjection = Zero<Float4>();
			}
		} else {
			assert(0);
		}
	}

	void PreparedShadowFrustum::InitialiseConstants(
		const MultiProjection<MaxShadowTexturesPerLight>& desc)
	{
		_frustumCount = desc._normalProjCount;
		_mode = desc._mode;
		_enableNearCascade = desc._useNearProj;

		BuildShadowConstantBuffers(_arbitraryCBSource, _orthoCBSource, desc);
	}

	PreparedShadowFrustum::PreparedShadowFrustum()
	: _frustumCount(0) 
	, _mode(ShadowProjectionMode::Arbitrary)
	{}

	PreparedShadowFrustum::PreparedShadowFrustum(PreparedShadowFrustum&& moveFrom) never_throws
	: _arbitraryCBSource(std::move(moveFrom._arbitraryCBSource))
	, _orthoCBSource(std::move(moveFrom._orthoCBSource))
	, _frustumCount(moveFrom._frustumCount)
	, _enableNearCascade(moveFrom._enableNearCascade)
	, _mode(moveFrom._mode)
	{}

	PreparedShadowFrustum& PreparedShadowFrustum::operator=(PreparedShadowFrustum&& moveFrom) never_throws
	{
		_arbitraryCBSource = std::move(moveFrom._arbitraryCBSource);
		_orthoCBSource = std::move(moveFrom._orthoCBSource);
		_frustumCount = moveFrom._frustumCount;
		_enableNearCascade = moveFrom._enableNearCascade;
		_mode = moveFrom._mode;
		return *this;
	}

	bool PreparedDMShadowFrustum::IsReady() const
	{
		return true;
	}

	CB_ShadowResolveParameters::CB_ShadowResolveParameters()
    {
        _worldSpaceBias = -0.03f;
        _tanBlurAngle = 0.00436f;		// tan(.25 degrees)
        _minBlurSearch = 0.5f;
        _maxBlurSearch = 25.f;
        _shadowTextureSize = 1024.f;
        XlZeroMemory(_dummy);
    }

	CB_ScreenToShadowProjection BuildScreenToShadowProjection(
        unsigned frustumCount,
        const CB_ArbitraryShadowProjection& arbitraryCB,
        const CB_OrthoShadowProjection& orthoCB,
        const Float4x4& cameraToWorld,
        const Float4x4& cameraToProjection)
    {
        CB_ScreenToShadowProjection basis;
        XlZeroMemory(basis);    // unused array elements must be zeroed out

            // The input coordinates are texture coordinate ranging from 0->1 across the viewport.
            // So, we must take into account X/Y scale and translation factors in the projection matrix.
            // Typically, this is just aspect ratio.
            // But if we have an unusual projection matrix (for example, when rendering tiles), then
            // we can also have a translation component in the projection matrix.
            // We can't incorporate this viewport/projection matrix scaling stuff into the main
            // cameraToShadow matrix because of the wierd way we transform through with this matrix!
            // So we have separate scale and translation values that are applied to the XY coordinates
            // of the inputs before transform
        #if 0
            auto projInverse = Inverse(cameraToProjection);
            auto viewportCorrection = MakeFloat4x4(
                 2.f,  0.f, 0.f, -1.f,
                 0.f, -2.f, 0.f, +1.f,
                 0.f,  0.f, 1.f,  0.f,
                 0.f,  0.f, 0.f,  1.f);
            auto temp = Combine(viewportCorrection, projInverse);
            basis._xyScale = Float2(temp(0,0), temp(1,1));
            basis._xyTrans = Float2(temp(0,3), temp(1,3));
        #endif

        basis._xyScale[0] =  2.f / cameraToProjection(0,0);
        basis._xyTrans[0] = -1.f / cameraToProjection(0,0) + cameraToProjection(0,2) / cameraToProjection(0,0);

        if (RenderCore::Techniques::GetDefaultClipSpaceType() == ClipSpaceType::PositiveRightHanded) {
            basis._xyScale[1] =  2.f / cameraToProjection(1,1);
            basis._xyTrans[1] = -1.f / cameraToProjection(1,1) + cameraToProjection(1,2) / cameraToProjection(1,1);
        } else {
            basis._xyScale[1] = -2.f / cameraToProjection(1,1);
            basis._xyTrans[1] =  1.f / cameraToProjection(1,1) + cameraToProjection(1,2) / cameraToProjection(1,1);
        }

        for (unsigned c=0; c<unsigned(frustumCount); ++c) {
            auto& worldToShadowProj = arbitraryCB._worldToProj[c];
            auto cameraToShadowProj = Combine(cameraToWorld, worldToShadowProj);
            basis._cameraToShadow[c] = cameraToShadowProj;
        }
        auto& worldToShadowProj = orthoCB._worldToProj;
        basis._orthoCameraToShadow = Combine(cameraToWorld, worldToShadowProj);
        basis._orthoNearCameraToShadow = Combine(basis._orthoCameraToShadow, orthoCB._nearCascade);
        return basis;
    }

}}}
