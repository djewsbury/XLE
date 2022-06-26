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
#include "../../RenderOverlays/OverlayContext.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../Math/ProjectionMath.h"
#include "../../Math/Transformations.h"
#include "../../Math/Geometry.h"
#include "../../Utility/StringFormat.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <random>

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	class BatchedResourcesDefragOverlay : public IInteractiveTestOverlay
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;

		virtual void Render(
			RenderCore::Techniques::ParsingContext& parserContext,
			IInteractiveTestHelper& testHelper) override
		{
			Update();

			using namespace RenderCore;
			using namespace RenderOverlays;
			auto overlayContext = MakeImmediateOverlayContext(
				parserContext.GetThreadContext(), *testHelper.GetImmediateDrawingApparatus()->_immediateDrawables,
				testHelper.GetImmediateDrawingApparatus()->_fontRenderingManager.get());

			// draw....
			float scale = 32.f;
			Float2 translation = -_cameraCenter * scale;
			Int2 viewport {parserContext.GetViewport()._width, parserContext.GetViewport()._height};
			translation[0] += viewport[0] / 2;
			translation[1] += viewport[1] / 2;
			{
				Float3 gridLines[(_gridHeight-1)*2+(_gridWidth-1)*2];
				Float3* gl = gridLines;
				for (unsigned y=1; y<_gridHeight; ++y) { *gl++ = Float3{translation[0], y*scale+translation[1], 0}; *gl++ = Float3{_gridWidth*scale+translation[0], y*scale+translation[1], 0}; }
				for (unsigned x=1; x<_gridWidth; ++x) { *gl++ = Float3{x*scale+translation[0], translation[1], 0}; *gl++ = Float3{x*scale+translation[0], _gridHeight*scale+translation[1], 0}; }
				overlayContext->DrawLines(ProjectionMode::P2D, gridLines, dimof(gridLines), {64, 64, 64, 128}, 1.f);

				char buffer[64];
				for (unsigned y=0; y<_gridHeight; ++y)
					for (unsigned x=0; x<_gridWidth; ++x) {
						Rect rect{Coord2{x*scale+translation[0], y*scale+translation[1]}, Coord2{(x+1)*scale+translation[0], (y+1)*scale+translation[1]}};
						if (rect._topLeft[0] >= viewport[0] || rect._topLeft[1] >= viewport[1] || rect._bottomRight[0] <= 0 || rect._bottomRight[1] <= 0) continue;
						DebuggingDisplay::DrawText().Alignment(TextAlignment::Center).Draw(
							*overlayContext, rect,
							(StringMeldInPlace(buffer) << _gridAllocations[y*_gridWidth+x]/1024).AsStringSection());
					}
			}

			const unsigned cameraRadiusCells = 16;
			DebuggingDisplay::OutlineEllipse(*overlayContext, Rect{Coord2{(_cameraCenter[0]-cameraRadiusCells)*scale+translation[0], (_cameraCenter[1]-cameraRadiusCells)*scale+translation[1]}, Coord2{(_cameraCenter[0]+cameraRadiusCells)*scale+translation[0], (_cameraCenter[1]+cameraRadiusCells)*scale+translation[1]}}, ColorB::Red);

			auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext, LoadStore::Clear);
			testHelper.GetImmediateDrawingApparatus()->_immediateDrawables->ExecuteDraws(parserContext, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
		}

		virtual bool OnInputEvent(
			const PlatformRig::InputContext& context,
			const PlatformRig::InputSnapshot& evnt,
			IInteractiveTestHelper& testHelper) override
		{
			return false;
		}

		void Update()
		{
			if (_cameraTarget.has_value()) {
				auto perFrameMovement = _movementSpeed / 60.f;
				_cameraCenter += perFrameMovement*Normalize(_cameraTarget.value() - _cameraCenter);
				if (Magnitude(_cameraCenter - _cameraTarget.value()) < perFrameMovement) {
					_cameraCenter = _cameraTarget.value();
					_cameraTarget = {};
				}
			}

			if (!_cameraTarget.has_value()) {
				_cameraTarget = Int2{
					std::uniform_int_distribution<>(0, _gridWidth-1)(_rng),
					std::uniform_int_distribution<>(0, _gridHeight-1)(_rng)};
				_movementSpeed = std::uniform_real_distribution<float>(3.0f, 10.f)(_rng);
			}
		}

		BatchedResourcesDefragOverlay()
		: _rng{5492559264231}
		{
			_gridWidth = _gridHeight = 128;
			_gridAllocations.reserve(_gridWidth*_gridHeight);
			for (unsigned c=0; c<_gridWidth*_gridHeight; ++c) _gridAllocations.push_back(std::uniform_int_distribution<>(4*1024, 64*1024)(_rng));
			_cameraCenter = Float2(_gridWidth/2, _gridHeight/2);
		}

		std::vector<unsigned> _gridAllocations;
		unsigned _gridWidth, _gridHeight;
		Float2 _cameraCenter;

		std::optional<Int2> _cameraTarget;
		float _movementSpeed = 0.f;
		std::mt19937_64 _rng;
	};

	TEST_CASE( "BatchedResourcesDefrag", "[math]" )
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

		auto tester = std::make_shared<BatchedResourcesDefragOverlay>();
		testHelper->Run(visCamera, tester);
	}

}
