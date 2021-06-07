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

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	class VolumeClipTestingOverlay : public IInteractiveTestOverlay
	{
	public:
		struct BoxObject
		{
			Float4x4 _localToWorld;
			Float3 _mins, _maxs;
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

			return false;
		}
	};

	TEST_CASE( "VolumeClipTesting", "[math]" )
	{
		using namespace RenderCore;

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

		auto tester = std::make_shared<VolumeVsFrustumTestingOverlay>();

		Techniques::CameraDesc sceneCamera;
		auto fwd = Normalize(Float3 { 1.0f, 0.0f, 1.0f });
		sceneCamera._cameraToWorld = MakeCameraToWorld(fwd, Float3{0.f, 1.f, 0.f}, Float3{50.f, 0.f, 50.f} - 45.0f * fwd);
		sceneCamera._projection = Techniques::CameraDesc::Projection::Perspective;
		sceneCamera._verticalFieldOfView = Deg2Rad(35.0f);
		sceneCamera._nearClip = 5.0f;
		sceneCamera._farClip = 75.f;
		tester->_worldToProjection = Techniques::BuildProjectionDesc(sceneCamera, UInt2(1920, 1080))._worldToProjection;

		RenderCore::Techniques::CameraDesc visCamera;
		visCamera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, -1.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, -1.0f}), Float3{0.0f, 200.0f, 0.0f});
		visCamera._projection = Techniques::CameraDesc::Projection::Orthogonal;
		visCamera._nearClip = 0.f;
		visCamera._farClip = 400.f;
		visCamera._left = 0.f;
		visCamera._right = 100.f;
		visCamera._top = 0.f;
		visCamera._bottom = -100.f;

		auto testHelper = CreateInteractiveTestHelper(IInteractiveTestHelper::EnabledComponents::RenderCoreTechniques);
		testHelper->Run(visCamera, tester);
	}
}
