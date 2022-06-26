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
#include "../../PlatformRig/DebuggingDisplays/BufferUploadDisplay.h"
#include "../../BufferUploads/BatchedResources.h"
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

		static constexpr int s_cameraRadiusCells = 16;

		virtual void Render(
			RenderCore::Techniques::ParsingContext& parserContext,
			IInteractiveTestHelper& testHelper) override
		{
			Update();
			AllocateResources();
			{
				static BufferUploads::EventListID lastProcessed = ~0u;
				auto evnt = _batchedResources->EventList_GetPublishedID();
				if (evnt != lastProcessed) {
					BufferUploads::Event_ResourceReposition* begin, *end;
					_batchedResources->EventList_Get(evnt, begin, end);
					_batchedResources->EventList_Release(evnt);
					lastProcessed = evnt;
				}
			}

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
						auto color = _allocatedResources.UnrecordedTest(uint64_t(y) << 32ull | x) ? ColorB{0x3f, 0x3f, 0xaf} : ColorB{0x3f, 0x3f, 0x3f};
						DebuggingDisplay::DrawText().Alignment(TextAlignment::Center).Color(color).Draw(
							*overlayContext, rect,
							(StringMeldInPlace(buffer) << _gridAllocations[y*_gridWidth+x]/1024).AsStringSection());
					}
			}

			DebuggingDisplay::OutlineEllipse(*overlayContext, Rect{Coord2{(_cameraCenter[0]-s_cameraRadiusCells)*scale+translation[0], (_cameraCenter[1]-s_cameraRadiusCells)*scale+translation[1]}, Coord2{(_cameraCenter[0]+s_cameraRadiusCells)*scale+translation[0], (_cameraCenter[1]+s_cameraRadiusCells)*scale+translation[1]}}, ColorB::Red);

			if (_batchingDisplay) {
				RenderOverlays::DebuggingDisplay::Layout layout{Rect{Coord2{0, 0}, viewport}};
				RenderOverlays::DebuggingDisplay::Interactables interactables;
				RenderOverlays::DebuggingDisplay::InterfaceState interfaceState;
				_batchingDisplay->Render(*overlayContext, layout, interactables, interfaceState);
			}

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

			_allocatedResources.OnFrameBarrier();
		}

		void AllocateResources()
		{
			for (unsigned y=std::max(int(_cameraCenter[1]-s_cameraRadiusCells), 0); y<std::min(unsigned(_cameraCenter[1]+s_cameraRadiusCells), _gridHeight); ++y)
				for (unsigned x=std::max(int(_cameraCenter[0]-s_cameraRadiusCells), 0); x<std::min(unsigned(_cameraCenter[0]+s_cameraRadiusCells), _gridHeight); ++x) {
					if (Magnitude(Float2{x-_cameraCenter[0], y-_cameraCenter[1]}) > s_cameraRadiusCells) continue;
					auto q = _allocatedResources.Query(uint64_t(y) << 32ull | x);
					// REQUIRE(q.GetType() != LRUCacheInsertType::Fail);
					if (q.GetType() == LRUCacheInsertType::Fail) continue;			// advancing too fast to let older allocations decay
					if (q.GetType() == LRUCacheInsertType::Update) continue;		// already good

					if (q.GetType() == LRUCacheInsertType::EvictAndReplace) {
						auto existing = std::move(q.GetExisting());
						existing = {};		// release the existing first
					}

					auto newAllocation = _batchedResources->Allocate(_gridAllocations[y*_gridWidth+x], "");
					REQUIRE(!newAllocation.IsEmpty());
					q.Set(std::move(newAllocation));
				}

			if (!_nextLongTermAllocationCountDown) {
				// every now and again, allocate a medium size block that we will retain for some time
				if (_longTermAllocations.size() >= 32) _longTermAllocations.erase(_longTermAllocations.begin());
				_longTermAllocations.push_back(_batchedResources->Allocate(std::uniform_int_distribution<>(32*1024, 64*1024)(_rng), ""));
				_nextLongTermAllocationCountDown = std::uniform_int_distribution<>(16, 64)(_rng);
			} else
				--_nextLongTermAllocationCountDown;

			_batchedResources->Tick();
		}

		BatchedResourcesDefragOverlay()
		: _rng{5492559264231}
		, _allocatedResources(1024)
		{
			_gridWidth = _gridHeight = 128;
			_gridAllocations.reserve(_gridWidth*_gridHeight);
			for (unsigned c=0; c<_gridWidth*_gridHeight; ++c) {
				if (std::uniform_int_distribution<>(0, 16)(_rng) == 0) {
					_gridAllocations.push_back(std::uniform_int_distribution<>(128*1024, 512*1024)(_rng));		// occasional very large allocation
				} else
					_gridAllocations.push_back(std::uniform_int_distribution<>(4*1024, 64*1024)(_rng));
			}
			_cameraCenter = Float2(_gridWidth/2, _gridHeight/2);
			_nextLongTermAllocationCountDown = 0;
		}

		std::vector<unsigned> _gridAllocations;
		unsigned _gridWidth, _gridHeight;
		Float2 _cameraCenter;

		std::optional<Int2> _cameraTarget;
		float _movementSpeed = 0.f;
		std::mt19937_64 _rng;

		std::shared_ptr<BufferUploads::BatchedResources> _batchedResources;
		FrameByFrameLRUHeap<BufferUploads::ResourceLocator> _allocatedResources;

		std::vector<BufferUploads::ResourceLocator> _longTermAllocations;
		unsigned _nextLongTermAllocationCountDown;

		std::shared_ptr<PlatformRig::Overlays::BatchingDisplay> _batchingDisplay;
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
		tester->_batchedResources = std::make_shared<BufferUploads::BatchedResources>(
			*testHelper->GetDevice(), testHelper->GetPrimaryResourcesApparatus()->_bufferUploads,
			RenderCore::CreateDesc(BindFlag::VertexBuffer|BindFlag::TransferDst|BindFlag::TransferSrc, LinearBufferDesc::Create(1024*1024), "batch-test"));
		tester->_batchingDisplay = std::make_shared<PlatformRig::Overlays::BatchingDisplay>(tester->_batchedResources);
		testHelper->Run(visCamera, tester);
	}

}
