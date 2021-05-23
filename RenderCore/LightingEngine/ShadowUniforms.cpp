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
	class CB_OrthoShadowProjection
	{
	public:
		Float3x4    _worldToProj;
		Float4      _minimalProjection;
	};

	struct CB_OrthoShadowNearCascade
	{
		Float3x4    _nearCascade;       // special projection for the area closest to the camera
		Float4      _nearMinimalProjection;
	};

	struct CB_ScreenToShadowProjection_Arbitrary
	{
		Float2      _xyScale;
		Float2      _xyTrans;
		Float4x4    _cameraToShadow[MaxShadowTexturesPerLight];
	};

	struct CB_ScreenToShadowProjection_Ortho
	{
		Float2      _xyScale;
		Float2      _xyTrans;
		Float4x4    _orthoCameraToShadow;
		Float4x4    _orthoNearCameraToShadow;
	};

	SharedPkt BuildShadowConstantBuffers(
		const MultiProjection<MaxShadowTexturesPerLight>& desc)
	{
		if (desc._mode == ShadowProjectionMode::Arbitrary || desc._mode == ShadowProjectionMode::ArbitraryCubeMap) {

			auto result = MakeSharedPktSize((sizeof(Float4)+sizeof(Float4x4))*desc._normalProjCount);
			auto* subProj = (Float4x4*)result.begin();
			auto* miniProj = (Float4*)PtrAdd(result.begin(), sizeof(Float4x4)*desc._normalProjCount);

			for (unsigned c=0; c<desc._normalProjCount; ++c) {
				subProj[c] = desc._fullProj[c]._worldToProjTransform;
				miniProj[c] = desc._minimalProjection[c];
			}

			return result;

		} else if (desc._mode == ShadowProjectionMode::Ortho) {

			size_t pktSize = sizeof(CB_OrthoShadowProjection) + sizeof(Float4) * 2 * desc._normalProjCount + (desc._useNearProj?sizeof(CB_OrthoShadowNearCascade):0);
			auto result = MakeSharedPktSize(pktSize);
			auto baseWorldToView = desc._definitionViewMatrix;

			auto* baseCB = (CB_OrthoShadowProjection*)result.begin();
			auto* cascadeScale = (Float4*)PtrAdd(result.begin(), sizeof(CB_OrthoShadowProjection));
			auto* cascadeTrans = (Float4*)PtrAdd(result.begin(), sizeof(CB_OrthoShadowProjection) + sizeof(Float4) * desc._normalProjCount);
			auto* nearProj = (CB_OrthoShadowNearCascade*)PtrAdd(result.begin(), sizeof(CB_OrthoShadowProjection) + desc._normalProjCount*sizeof(Float4)*2);

			float p22 = 1.f, p23 = 0.f;

			for (unsigned c=0; c<desc._normalProjCount; ++c) {

				auto projMatrix = OrthogonalProjection(
					desc._orthoSub[c]._topLeftFront[0], desc._orthoSub[c]._topLeftFront[1], 
					desc._orthoSub[c]._bottomRightBack[0], desc._orthoSub[c]._bottomRightBack[1], 
					desc._orthoSub[c]._topLeftFront[2], desc._orthoSub[c]._bottomRightBack[2],
					Techniques::GetDefaultClipSpaceType());
				assert(IsOrthogonalProjection(projMatrix));

				cascadeScale[c][0] = projMatrix(0,0);
				cascadeScale[c][1] = projMatrix(1,1);
				cascadeTrans[c][0] = projMatrix(0,3);
				cascadeTrans[c][1] = projMatrix(1,3);

				if (c==0) {
					p22 = projMatrix(2,2);
					p23 = projMatrix(2,3);
				}

					// (unused parts)
				cascadeScale[c][2] = 1.f;
				cascadeScale[c][2] = 0.f;
				cascadeTrans[c][3] = 1.f;
				cascadeTrans[c][3] = 0.f;
			}

				//  Also fill in the constants for ortho projection mode
			baseCB->_minimalProjection = desc._minimalProjection[0];

				//  We merge in the transform for the z component
				//  Every cascade uses the same depth range, which means we only
				//  have to adjust the X and Y components for each cascade
			auto zComponentMerge = Identity<Float4x4>();
			zComponentMerge(2,2) = p22;
			zComponentMerge(2,3) = p23;
			baseCB->_worldToProj = AsFloat3x4(Combine(baseWorldToView, zComponentMerge));

				// the special "near" cascade is reached via the main transform
			if (desc._useNearProj) {
				nearProj->_nearCascade = 
					AsFloat3x4(Combine(
						Inverse(AsFloat4x4(baseCB->_worldToProj)), 
						desc._specialNearProjection));
				nearProj->_nearMinimalProjection = desc._specialNearMinimalProjection;
			}

			return result;

		} else {
			assert(0);
			return {};
		}
	}

	void PreparedShadowFrustum::InitialiseConstants(
		const MultiProjection<MaxShadowTexturesPerLight>& desc)
	{
		_frustumCount = desc._normalProjCount;
		_mode = desc._mode;
		_enableNearCascade = desc._useNearProj;
		_cbSource = BuildShadowConstantBuffers(desc);
	}

	PreparedShadowFrustum::PreparedShadowFrustum()
	: _frustumCount(0) 
	, _mode(ShadowProjectionMode::Arbitrary)
	{}

	PreparedShadowFrustum::PreparedShadowFrustum(PreparedShadowFrustum&& moveFrom) never_throws
	: _cbSource(std::move(moveFrom._cbSource))
	, _frustumCount(moveFrom._frustumCount)
	, _enableNearCascade(moveFrom._enableNearCascade)
	, _mode(moveFrom._mode)
	{}

	PreparedShadowFrustum& PreparedShadowFrustum::operator=(PreparedShadowFrustum&& moveFrom) never_throws
	{
		_cbSource = std::move(moveFrom._cbSource);
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

	SharedPkt BuildScreenToShadowProjection(
		ShadowProjectionMode mode,
		unsigned normalProjCount,
		const SharedPkt& mainUniforms,
		const Float4x4& cameraToWorld,
		const Float4x4& cameraToProjection)
	{
		SharedPkt result;

		if (mode == ShadowProjectionMode::Arbitrary || mode == ShadowProjectionMode::ArbitraryCubeMap) {

			result = MakeSharedPktSize(sizeof(Float2)*2+sizeof(Float4x4)*normalProjCount);
			auto* basis = (CB_ScreenToShadowProjection_Arbitrary*)result.begin();

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

			basis->_xyScale[0] =  2.f / cameraToProjection(0,0);
			basis->_xyTrans[0] = -1.f / cameraToProjection(0,0) + cameraToProjection(0,2) / cameraToProjection(0,0);

			if (RenderCore::Techniques::GetDefaultClipSpaceType() == ClipSpaceType::PositiveRightHanded) {
				basis->_xyScale[1] =  2.f / cameraToProjection(1,1);
				basis->_xyTrans[1] = -1.f / cameraToProjection(1,1) + cameraToProjection(1,2) / cameraToProjection(1,1);
			} else {
				basis->_xyScale[1] = -2.f / cameraToProjection(1,1);
				basis->_xyTrans[1] =  1.f / cameraToProjection(1,1) + cameraToProjection(1,2) / cameraToProjection(1,1);
			}

			auto* subProj = (Float4x4*)mainUniforms.begin();

			for (unsigned c=0; c<unsigned(normalProjCount); ++c) {
				auto& worldToShadowProj = subProj[c];
				auto cameraToShadowProj = Combine(cameraToWorld, worldToShadowProj);
				basis->_cameraToShadow[c] = cameraToShadowProj;
			}

		} else {

			result = MakeSharedPktSize(sizeof(CB_ScreenToShadowProjection_Ortho));
			auto* basis = (CB_ScreenToShadowProjection_Ortho*)result.begin();

			basis->_xyScale[0] =  2.f / cameraToProjection(0,0);
			basis->_xyTrans[0] = -1.f / cameraToProjection(0,0) + cameraToProjection(0,2) / cameraToProjection(0,0);

			if (RenderCore::Techniques::GetDefaultClipSpaceType() == ClipSpaceType::PositiveRightHanded) {
				basis->_xyScale[1] =  2.f / cameraToProjection(1,1);
				basis->_xyTrans[1] = -1.f / cameraToProjection(1,1) + cameraToProjection(1,2) / cameraToProjection(1,1);
			} else {
				basis->_xyScale[1] = -2.f / cameraToProjection(1,1);
				basis->_xyTrans[1] =  1.f / cameraToProjection(1,1) + cameraToProjection(1,2) / cameraToProjection(1,1);
			}

			auto* orthCB = (CB_OrthoShadowProjection*)mainUniforms.begin();
			auto* nearProj = (CB_OrthoShadowNearCascade*)PtrAdd(mainUniforms.begin(), sizeof(CB_OrthoShadowProjection) + normalProjCount*sizeof(Float4)*2);

			auto& worldToShadowProj = orthCB->_worldToProj;
			basis->_orthoCameraToShadow = Combine(cameraToWorld, worldToShadowProj);
			basis->_orthoNearCameraToShadow = Combine(basis->_orthoCameraToShadow, nearProj->_nearCascade);

		}

		return result;
	}

}}}
