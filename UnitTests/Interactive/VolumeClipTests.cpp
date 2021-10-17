// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InteractiveTestHelper.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../PlatformRig/InputListener.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/RenderPassUtils.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/LightingEngine/SunSourceConfiguration.h"
#include "../../RenderOverlays/OverlayContext.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../PlatformRig/CameraManager.h"
#include "../../Tools/ToolsRig/VisualisationGeo.h"
#include "../../Math/ProjectionMath.h"
#include "../../Math/Transformations.h"
#include "../../Math/Geometry.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <random>

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	struct BoxObject
	{
		Float3 _center;
		Float3 _radii;
		float _rotation;
	};

	static void DrawBoxObjects(
		RenderOverlays::IOverlayContext& overlayContext,
		ArbitraryConvexVolumeTester& frustumTester,
		IteratorRange<const BoxObject*> boxObjects)
	{
		for (const auto& obj:boxObjects) {
			Float3x4 localToWorld = AsFloat3x4(AsFloat4x4(UniformScaleYRotTranslation { 1.0f, obj._rotation, obj._center }));
			Float3 mins = -obj._radii, maxs = obj._radii;
			auto result = frustumTester.TestAABB(localToWorld, mins, maxs);
			RenderOverlays::ColorB col;
			switch (result) {
			case CullTestResult::Culled: col = { 255, 100, 100 }; break;
			case CullTestResult::Boundary: col = { 100, 100, 255 }; break;
			case CullTestResult::Within: col = { 100, 255, 100 }; break;
			default: FAIL("Unknown frustum result"); break;
			}

			RenderOverlays::DebuggingDisplay::DrawBoundingBox(
				overlayContext, { mins, maxs },
				localToWorld,
				col);
		}
	}

	static void DrawBoxObjectsShadowVolumes(
		RenderOverlays::IOverlayContext& overlayContext,
		ArbitraryConvexVolumeTester& frustumTester,
		IteratorRange<const BoxObject*> boxObjects,
		Float3 lightDirection, float shadowLength)
	{
		for (const auto& obj:boxObjects) {
			Float3x4 localToWorld = AsFloat3x4(AsFloat4x4(UniformScaleYRotTranslation { 1.0f, obj._rotation, obj._center }));
			Float3 localCorners[] {
				Float3{-obj._radii[0], -obj._radii[1], -obj._radii[2]},
				Float3{ obj._radii[0], -obj._radii[1], -obj._radii[2]},
				Float3{-obj._radii[0],  obj._radii[1], -obj._radii[2]},
				Float3{ obj._radii[0],  obj._radii[1], -obj._radii[2]},

				Float3{-obj._radii[0], -obj._radii[1],  obj._radii[2]},
				Float3{ obj._radii[0], -obj._radii[1],  obj._radii[2]},
				Float3{-obj._radii[0],  obj._radii[1],  obj._radii[2]},
				Float3{ obj._radii[0],  obj._radii[1],  obj._radii[2]}
			};

			Float3 lines[dimof(localCorners)*2];
			for (unsigned c=0; c<dimof(localCorners); c++) {
				lines[c*2+0] = TransformPoint(localToWorld, localCorners[c]);
				lines[c*2+1] = TransformPoint(localToWorld, localCorners[c]) + lightDirection * shadowLength;
			}

			overlayContext.DrawLines(RenderOverlays::ProjectionMode::P3D, lines, dimof(lines), RenderOverlays::ColorB{45, 45, 45});
		}
	}

	class VolumeClipTestingOverlay : public IInteractiveTestOverlay
	{
	public:
		std::vector<BoxObject> _boxObjects;

		struct SphereObject
		{
			Float3 _center;
			float _radius;
		};
		std::vector<SphereObject> _sphereObjects;

		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;

		virtual bool OnInputEvent(
			const PlatformRig::InputContext& context,
			const PlatformRig::InputSnapshot& evnt,
			IInteractiveTestHelper& testHelper) override
		{
			const bool interactWithSphereObject = evnt.IsHeld(PlatformRig::KeyId_Make("shift"));
			if (interactWithSphereObject) {
				if (_sphereObjects.empty())
					_sphereObjects.push_back(VolumeClipTestingOverlay::SphereObject { Zero<Float3>(), 1.0f });

				if (evnt.IsHeld_LButton()) {
					auto ray = testHelper.ScreenToWorldSpaceRay(evnt._mousePosition);
					auto intr = RayVsPlane(ray.first, ray.second, Float4(0,1,0,0));
					(_sphereObjects.end()-1)->_center = LinearInterpolate(ray.first, ray.second, intr);
				}

				if (evnt._wheelDelta != 0) {
					auto& obj = *(_sphereObjects.end()-1);
					obj._radius = std::max(0.5f, obj._radius + (float)evnt._wheelDelta / 128.0f);
				}
			} else {
				if (_boxObjects.empty())
					_boxObjects.push_back(BoxObject { Zero<Float3>(), Float3{1.0f, 1.0f, 1.0f}, 0.f });

				if (evnt.IsHeld_LButton()) {
					auto ray = testHelper.ScreenToWorldSpaceRay(evnt._mousePosition);
					auto intr = RayVsPlane(ray.first, ray.second, Float4(0,1,0,0));
					(_boxObjects.end()-1)->_center = LinearInterpolate(ray.first, ray.second, intr);
				}

				if (evnt._wheelDelta != 0) {
					auto& obj = *(_boxObjects.end()-1);
					if (evnt.IsHeld(PlatformRig::KeyId_Make("control"))) {
						obj._rotation += (float)evnt._wheelDelta / 1024.0f;
					} else
						obj._radii[0] = std::max(0.5f, obj._radii[0] + (float)evnt._wheelDelta / 128.0f);
				}
			}

			return false;
		}
	};

	static ArbitraryConvexVolumeTester MakeArbitraryColumnTester(
		IteratorRange<const Float3*> cutaway,	// must be clockwise when looking along the axis
		Float3 axisDirection, 
		float axisMin, float axisMax)
	{
		std::vector<Float4> planes;
		std::vector<Float3> corners;
		std::vector<ArbitraryConvexVolumeTester::Edge> edges;
		std::vector<unsigned> cornerFaceBitMasks;

		auto minCapPlane = uint64_t(cutaway.size());
		auto maxCapPlane = uint64_t(cutaway.size()+1);

		for (unsigned c=0; c<cutaway.size(); ++c) {
			auto pt0 = cutaway[c];
			auto pt1 = cutaway[(c+1)%unsigned(cutaway.size())];

			pt0 -= axisDirection * Dot(pt0, axisDirection);
			pt1 -= axisDirection * Dot(pt1, axisDirection);

			Float3 pt0_min = pt0 + axisDirection * axisMin;
			Float3 pt1_min = pt1 + axisDirection * axisMin;
			Float3 pt0_max = pt0 + axisDirection * axisMax;
			Float3 pt1_max = pt1 + axisDirection * axisMax;
			auto plane = PlaneFit(pt0_min, pt1_min, pt1_max);

			auto planeIdx = (uint64_t)planes.size();
			auto prevPlaneIdx = uint64_t(planeIdx+cutaway.size()-1)%unsigned(cutaway.size());
			
			planes.push_back(plane);
			corners.push_back(pt0_min);
			corners.push_back(pt0_max);

			auto pt0Idx = c*2;
			auto pt1Idx = (c+1)%unsigned(cutaway.size())*2;
			edges.push_back({pt0Idx, pt1Idx, (1ull<<planeIdx)|(1ull<<minCapPlane)});
			edges.push_back({pt0Idx+1, pt0Idx+1, (1ull<<planeIdx)|(1ull<<maxCapPlane)});
			edges.push_back({pt0Idx, pt0Idx+1, (1ull<<planeIdx)|(1ull<<prevPlaneIdx)});

			cornerFaceBitMasks.push_back((1ull<<planeIdx)|(1ull<<minCapPlane)|(1ull<<prevPlaneIdx));
			cornerFaceBitMasks.push_back((1ull<<planeIdx)|(1ull<<maxCapPlane)|(1ull<<prevPlaneIdx));
		}

		planes.push_back(Float4(-axisDirection, axisMin));
		planes.push_back(Float4(axisDirection, -axisMax));

		return ArbitraryConvexVolumeTester { std::move(planes), std::move(corners), std::move(edges), std::move(cornerFaceBitMasks) };
	}

	TEST_CASE( "VolumeClipTesting", "[math]" )
	{
		using namespace RenderCore;

		auto testHelper = CreateInteractiveTestHelper(IInteractiveTestHelper::EnabledComponents::RenderCoreTechniques);

		RenderCore::Techniques::CameraDesc visCamera;
		visCamera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, -1.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, -1.0f}), Float3{0.0f, 200.0f, 0.0f});
		visCamera._projection = Techniques::CameraDesc::Projection::Orthogonal;
		visCamera._nearClip = 0.f;
		visCamera._farClip = 400.f;
		visCamera._left = 0.f;
		visCamera._right = 100.f;
		visCamera._top = 0.f;
		visCamera._bottom = -100.f;

		class VolumeVsFrustumTestingOverlay : public VolumeClipTestingOverlay
		{
		public:
			virtual void Render(
				RenderCore::Techniques::ParsingContext& parserContext,
				IInteractiveTestHelper& testHelper) override
			{
				auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(
					parserContext.GetThreadContext(), *testHelper.GetImmediateDrawingApparatus()->_immediateDrawables);

				RenderOverlays::DebuggingDisplay::DrawFrustum(
					*overlayContext, _worldToProjection,
					RenderOverlays::ColorB { 255, 255, 255 });

				auto sphereGeo = ToolsRig::BuildGeodesicSphereP(2);

				AccurateFrustumTester frustumTester { _worldToProjection, Techniques::GetDefaultClipSpaceType() };
				for (const auto& obj:_sphereObjects) {
					auto result = frustumTester.TestSphere(obj._center, obj._radius);
					RenderOverlays::ColorB col;
					switch (result) {
					case CullTestResult::Culled: col = { 255, 100, 100 }; break;
					case CullTestResult::Boundary: col = { 100, 100, 255 }; break;
					case CullTestResult::Within: col = { 100, 255, 100 }; break;
					default: FAIL("Unknown frustum result"); break;
					}

					auto transformedGeo = sphereGeo;
					for (auto& p:transformedGeo)
						p = obj._center + obj._radius * p;
					overlayContext->DrawTriangles(RenderOverlays::ProjectionMode::P3D, transformedGeo.data(), transformedGeo.size(), col);
				}

				auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext, LoadStore::Clear);
				testHelper.GetImmediateDrawingApparatus()->_immediateDrawables->ExecuteDraws(parserContext, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
			}

			Float4x4 _worldToProjection;
		};

		if (false) {
			Techniques::CameraDesc sceneCamera;
			auto fwd = Normalize(Float3 { 1.0f, 0.0f, 1.0f });
			sceneCamera._cameraToWorld = MakeCameraToWorld(fwd, Float3{0.f, 1.f, 0.f}, Float3{50.f, 0.f, 50.f} - 45.0f * fwd);
			sceneCamera._projection = Techniques::CameraDesc::Projection::Perspective;
			sceneCamera._verticalFieldOfView = Deg2Rad(35.0f);
			sceneCamera._nearClip = 5.0f;
			sceneCamera._farClip = 75.f;
			auto tester = std::make_shared<VolumeVsFrustumTestingOverlay>();
			tester->_worldToProjection = Techniques::BuildProjectionDesc(sceneCamera, UInt2(1920, 1080))._worldToProjection;
			testHelper->Run(visCamera, tester);
		}

		class ArbitraryColumnCullTestingOverlay : public VolumeClipTestingOverlay
		{
		public:
			virtual void Render(
				RenderCore::Techniques::ParsingContext& parserContext,
				IInteractiveTestHelper& testHelper) override
			{
				auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(
					parserContext.GetThreadContext(), *testHelper.GetImmediateDrawingApparatus()->_immediateDrawables);

				for (unsigned c=0; c<_cutawayPoints.size(); ++c) {
					auto pt0 = _cutawayPoints[c], pt1 = _cutawayPoints[(c+1)%unsigned(_cutawayPoints.size())];
					overlayContext->DrawLine(RenderOverlays::ProjectionMode::P3D, pt0, RenderOverlays::ColorB { 255, 255, 255 }, pt1, RenderOverlays::ColorB { 255, 255, 255 }, 1.0f);
				}

				auto frustumTester = MakeArbitraryColumnTester(MakeIteratorRange(_cutawayPoints), _axisDirection, _axisMin, _axisMax);

				if (!_sphereObjects.empty()) {
					auto sphereGeo = ToolsRig::BuildGeodesicSphereP(2);
					for (const auto& obj:_sphereObjects) {
						auto result = frustumTester.TestSphere(obj._center, obj._radius);
						RenderOverlays::ColorB col;
						switch (result) {
						case CullTestResult::Culled: col = { 255, 100, 100 }; break;
						case CullTestResult::Boundary: col = { 100, 100, 255 }; break;
						case CullTestResult::Within: col = { 100, 255, 100 }; break;
						default: FAIL("Unknown frustum result"); break;
						}

						auto transformedGeo = sphereGeo;
						for (auto& p:transformedGeo)
							p = obj._center + obj._radius * p;
						overlayContext->DrawTriangles(RenderOverlays::ProjectionMode::P3D, transformedGeo.data(), transformedGeo.size(), col);
					}
				}

				DrawBoxObjects(*overlayContext, frustumTester, MakeIteratorRange(_boxObjects));

				auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext, LoadStore::Clear);
				testHelper.GetImmediateDrawingApparatus()->_immediateDrawables->ExecuteDraws(parserContext, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
			}

			std::vector<Float3> _cutawayPoints;
			Float3 _axisDirection; 
			float _axisMin, _axisMax;
		};

		if (true) {
			std::vector<float> divisions;
			float total = 0.f;
			for (unsigned c=0; c<20; ++c) {
				auto A = 1.0f / float(c+1);
				divisions.push_back(A);
				total += A;
			}
			for (auto& A:divisions) A /= total;

			std::mt19937_64 rng(812672572);
			std::shuffle(divisions.begin(), divisions.end(), rng);

			auto tester = std::make_shared<ArbitraryColumnCullTestingOverlay>();
			tester->_axisDirection = Float3(0, 1, 0);
			tester->_axisMin = -1e3f;
			tester->_axisMax = 1e3f;
			float theta = 0.f;
			for (const auto& A:divisions) {
				Float3 pt {
					30.f * std::cos(-theta), 0.f, 30.f * std::sin(-theta)
				};
				tester->_cutawayPoints.push_back(Float3{50, 0, 50} + pt);
				theta += 2.0f * gPI * A;
			}

			testHelper->Run(visCamera, tester);
		}
	}

	TEST_CASE( "ExtrudedFrustumClipTesting", "[math]" )
	{
		using namespace RenderCore;

		class ExtrudedFrustumOverlay : public IInteractiveTestOverlay
		{
		public:
			std::vector<BoxObject> _boxObjects;

			ArbitraryConvexVolumeTester _frustumTester;

			Techniques::CameraDesc _visCamera;
			Techniques::CameraDesc _mainCamera;

			std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
			RenderCore::LightingEngine::SunSourceFrustumSettings _sunSourceSettings;
			Float3 _lightDirection;

			virtual void Render(
				RenderCore::Techniques::ParsingContext& parserContext,
				IInteractiveTestHelper& testHelper) override
			{
				// Split the render area into parts
				// on left: camera view, top-down view
				// on right: 3 frustums from shadows

				using namespace RenderOverlays;
				using namespace RenderOverlays::DebuggingDisplay;
				Layout outerLayout {Rect{Coord2(0,0), Coord2(parserContext.GetViewport()._width, parserContext.GetViewport()._height)}};
				Layout leftLayout { outerLayout.AllocateFullHeightFraction(0.5f) };
				Layout rightLayout { outerLayout.AllocateFullHeightFraction(0.5f) };

				auto topDownRect = leftLayout.AllocateFullWidthFraction(0.5f);
				auto mainCamRect = leftLayout.AllocateFullWidthFraction(0.5f);
				Rect cascadeView[3];
				for (unsigned c=0; c<dimof(cascadeView); ++c)
					cascadeView[c] = rightLayout.AllocateFullWidthFraction(1.0f/(float)dimof(cascadeView));

				auto mainCamProjDesc = MakeProjDesc(_mainCamera, topDownRect);
				auto eyePosition = ExtractTranslation(mainCamProjDesc._cameraToWorld);
				auto cameraToWorldNoTranslation = mainCamProjDesc._cameraToWorld;
            	SetTranslation(cameraToWorldNoTranslation, Float3(0,0,0));
				auto worldToProj = Combine(InvertOrthonormalTransform(cameraToWorldNoTranslation), mainCamProjDesc._cameraToProjection);
				_frustumTester = ExtrudeFrustumOrthogonally(worldToProj, eyePosition, -_lightDirection, 40.f, Techniques::GetDefaultClipSpaceType());

				DrawTopDownView(parserContext.GetThreadContext(), parserContext, testHelper, topDownRect, MakeProjDesc(_visCamera, topDownRect), mainCamProjDesc._worldToProjection);
				DrawMainView(parserContext.GetThreadContext(), parserContext, testHelper, mainCamRect, mainCamProjDesc);

				auto cascades = RenderCore::LightingEngine::Internal::TestResolutionNormalizedOrthogonalShadowProjections(
					-_lightDirection, mainCamProjDesc, _sunSourceSettings, Techniques::GetDefaultClipSpaceType());
				for (unsigned c=0; c<cascades.first.size() && c<3; ++c) {
					auto projDesc = Techniques::BuildOrthogonalProjectionDesc(
						InvertOrthonormalTransform(cascades.second),
						cascades.first[c]._leftTopFront[0], cascades.first[c]._leftTopFront[1], 
						cascades.first[c]._rightBottomBack[0], cascades.first[c]._rightBottomBack[1], 
						cascades.first[c]._leftTopFront[2], cascades.first[c]._rightBottomBack[2]);
					DrawMainView(parserContext.GetThreadContext(), parserContext, testHelper, cascadeView[c], projDesc);
				}
			}

			void DrawTopDownView(
				RenderCore::IThreadContext& threadContext,
				RenderCore::Techniques::ParsingContext& parserContext,
				IInteractiveTestHelper& testHelper,
				const RenderOverlays::Rect& rect,
				const Techniques::ProjectionDesc& projDesc,
				const Float4x4& mainCameraWorldToProjection)
			{
				auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(
					threadContext, *testHelper.GetImmediateDrawingApparatus()->_immediateDrawables);

				using namespace RenderOverlays;
				DebuggingDisplay::OutlineRectangle(*overlayContext, Rect{Coord2(1,1), Coord2(rect.Width(), rect.Height())}, ColorB { 96, 64, 16 });
				DebuggingDisplay::DrawFrustum(
					*overlayContext, mainCameraWorldToProjection,
					ColorB { 127, 192, 192 });
				DrawBoxObjects(*overlayContext, _frustumTester, MakeIteratorRange(_boxObjects));

				parserContext.GetProjectionDesc() = projDesc;
				auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext, LoadStore::Clear);
				SetViewport(threadContext, parserContext, rect);
				testHelper.GetImmediateDrawingApparatus()->_immediateDrawables->ExecuteDraws(parserContext, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
			}

			void DrawMainView(
				RenderCore::IThreadContext& threadContext,
				RenderCore::Techniques::ParsingContext& parserContext,
				IInteractiveTestHelper& testHelper,
				const RenderOverlays::Rect& rect,
				const Techniques::ProjectionDesc& projDesc)
			{
				auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(
					threadContext, *testHelper.GetImmediateDrawingApparatus()->_immediateDrawables);

				using namespace RenderOverlays;
				DebuggingDisplay::OutlineRectangle(*overlayContext, Rect{Coord2(1,1), Coord2(rect.Width(), rect.Height())}, ColorB { 96, 64, 16 });
				DrawBoxObjects(*overlayContext, _frustumTester, MakeIteratorRange(_boxObjects));
				DrawBoxObjectsShadowVolumes(*overlayContext, _frustumTester, MakeIteratorRange(_boxObjects), _lightDirection, 40.f);

				parserContext.GetProjectionDesc() = projDesc;
				auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext, LoadStore::Retain);
				SetViewport(threadContext, parserContext, rect);
				testHelper.GetImmediateDrawingApparatus()->_immediateDrawables->ExecuteDraws(parserContext, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
			}

			Techniques::ProjectionDesc MakeProjDesc(Techniques::CameraDesc& cam, const RenderOverlays::Rect& rect)
			{
				return Techniques::BuildProjectionDesc(cam, {rect.Width(), rect.Height()});
			}

			void SetViewport(
				RenderCore::IThreadContext& threadContext,
				RenderCore::Techniques::ParsingContext& parserContext,
				const RenderOverlays::Rect& rect)
			{
				ViewportDesc viewport { (float)rect._topLeft[0], (float)rect._topLeft[1], (float)rect.Width(), (float)rect.Height() };
				parserContext.GetViewport() = viewport;
			}

			virtual bool OnInputEvent(
				const PlatformRig::InputContext& context,
				const PlatformRig::InputSnapshot& evnt,
				IInteractiveTestHelper& testHelper) override
			{
				PlatformRig::Camera::UpdateCamera_Slew(_mainCamera, 1.f/60.f/100.f, evnt);
				return true;
			}
		};

		auto testHelper = CreateInteractiveTestHelper(IInteractiveTestHelper::EnabledComponents::RenderCoreTechniques);
		auto tester = std::make_shared<ExtrudedFrustumOverlay>();
		tester->_boxObjects.push_back({Float3(4, 5, 2), Float3(1, 2.5, 1.33), 1.4f*gPI});

		tester->_visCamera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, -1.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, -1.0f}), Float3{0.0f, 20.0f, 0.0f});
		tester->_visCamera._projection = Techniques::CameraDesc::Projection::Orthogonal;
		tester->_visCamera._nearClip = 0.f;
		tester->_visCamera._farClip = 40.f;
		tester->_visCamera._left = -20.f;
		tester->_visCamera._right = 20.f;
		tester->_visCamera._top = 20.f;
		tester->_visCamera._bottom = -20.f;

		tester->_mainCamera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{1.f, 0.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, 1.0f}), Float3{-10.0f, 0.0f, 0.0f});
		tester->_mainCamera._projection = Techniques::CameraDesc::Projection::Perspective;
		tester->_mainCamera._nearClip = 0.1f;
		tester->_mainCamera._farClip = 50.f;

		tester->_lightDirection = Normalize(Float3{0.f, -1.f, -1.f});

		tester->_sunSourceSettings._maxFrustumCount = 3;
		tester->_sunSourceSettings._maxDistanceFromCamera = 50;
		tester->_sunSourceSettings._focusDistance = 3.;
		tester->_sunSourceSettings._textureSize = 512;

		testHelper->Run(tester->_mainCamera, tester);
	}


}
