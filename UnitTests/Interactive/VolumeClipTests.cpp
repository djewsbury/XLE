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
#include "../../RenderOverlays/OverlayContext.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
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
	class VolumeClipTestingOverlay : public IInteractiveTestOverlay
	{
	public:
		struct BoxObject
		{
			Float3 _center;
			Float3 _radii;
			float _rotation;
		};
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
					_boxObjects.push_back(VolumeClipTestingOverlay::BoxObject { Zero<Float3>(), Float3{1.0f, 1.0f, 1.0f}, 0.f });

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
				RenderCore::IThreadContext& threadContext,
				RenderCore::Techniques::ParsingContext& parserContext,
				IInteractiveTestHelper& testHelper) override
			{
				auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(
					threadContext, *testHelper.GetImmediateDrawingApparatus()->_immediateDrawables);

				RenderOverlays::DebuggingDisplay::DrawFrustum(
					overlayContext.get(), _worldToProjection,
					RenderOverlays::ColorB { 255, 255, 255 });

				auto sphereGeo = ToolsRig::BuildGeodesicSphereP(2);

				AccurateFrustumTester frustumTester { _worldToProjection, Techniques::GetDefaultClipSpaceType() };
				for (const auto& obj:_sphereObjects) {
					auto result = frustumTester.TestSphere(obj._center, obj._radius);
					RenderOverlays::ColorB col;
					switch (result) {
					case AABBIntersection::Culled: col = { 255, 100, 100 }; break;
					case AABBIntersection::Boundary: col = { 100, 100, 255 }; break;
					case AABBIntersection::Within: col = { 100, 255, 100 }; break;
					default: FAIL("Unknown frustum result"); break;
					}

					auto transformedGeo = sphereGeo;
					for (auto& p:transformedGeo)
						p = obj._center + obj._radius * p;
					overlayContext->DrawTriangles(RenderOverlays::ProjectionMode::P3D, transformedGeo.data(), transformedGeo.size(), col);
				}

				auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, parserContext, LoadStore::Clear);
				testHelper.GetImmediateDrawingApparatus()->_immediateDrawables->ExecuteDraws(threadContext, parserContext, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
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
				RenderCore::IThreadContext& threadContext,
				RenderCore::Techniques::ParsingContext& parserContext,
				IInteractiveTestHelper& testHelper) override
			{
				auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(
					threadContext, *testHelper.GetImmediateDrawingApparatus()->_immediateDrawables);

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
						case AABBIntersection::Culled: col = { 255, 100, 100 }; break;
						case AABBIntersection::Boundary: col = { 100, 100, 255 }; break;
						case AABBIntersection::Within: col = { 100, 255, 100 }; break;
						default: FAIL("Unknown frustum result"); break;
						}

						auto transformedGeo = sphereGeo;
						for (auto& p:transformedGeo)
							p = obj._center + obj._radius * p;
						overlayContext->DrawTriangles(RenderOverlays::ProjectionMode::P3D, transformedGeo.data(), transformedGeo.size(), col);
					}
				}

				if (!_boxObjects.empty()) {
					for (const auto& obj:_boxObjects) {
						Float3x4 localToWorld = AsFloat3x4(AsFloat4x4(UniformScaleYRotTranslation { 1.0f, obj._rotation, obj._center }));
						Float3 mins = -obj._radii, maxs = obj._radii;
						auto result = frustumTester.TestAABB(localToWorld, mins, maxs);
						RenderOverlays::ColorB col;
						switch (result) {
						case AABBIntersection::Culled: col = { 255, 100, 100 }; break;
						case AABBIntersection::Boundary: col = { 100, 100, 255 }; break;
						case AABBIntersection::Within: col = { 100, 255, 100 }; break;
						default: FAIL("Unknown frustum result"); break;
						}

						RenderOverlays::DebuggingDisplay::DrawBoundingBox(
							overlayContext.get(), { mins, maxs },
							localToWorld,
							col);
					}
				}

				auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, parserContext, LoadStore::Clear);
				testHelper.GetImmediateDrawingApparatus()->_immediateDrawables->ExecuteDraws(threadContext, parserContext, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
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
}
