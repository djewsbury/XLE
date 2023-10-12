// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InteractiveTestHelper.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../PlatformRig/InputContext.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/RenderPassUtils.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderOverlays/OverlayContext.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/FontRendering.h"
#include "../../RenderOverlays/OverlayApparatus.h"
#include "../../RenderOverlays/LayoutEngine.h"
#include "../../Math/Transformations.h"
#include "../../Assets/Marker.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/HeapUtils.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <random>

#include "../../RenderCore/Metal/Resource.h"		// required for CompleteInitialization
#include "../../RenderCore/Metal/DeviceContext.h"

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	class FontRenderingManagerDisplay : public RenderOverlays::DebuggingDisplay::IWidget
	{
	public:
		void Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
		{
			using namespace RenderOverlays;
			using namespace RenderOverlays::DebuggingDisplay;
			auto* tex = _renderingManager->GetUnderlyingTextureResource().get();
			auto desc = tex->GetDesc();
			layout.SetDirection(ImmediateLayout::Direction::Row);
			auto rect = layout.AllocateFullHeight(std::min((int)desc._textureDesc._width, layout.GetWidthRemaining()));
			if (rect.Height() > desc._textureDesc._height)
				rect._bottomRight[1] = rect._topLeft[1] + desc._textureDesc._height;
			context.DrawTexturedQuad(
				ProjectionMode::P2D,
				AsPixelCoords(rect._topLeft), AsPixelCoords(rect._bottomRight),
				tex->CreateTextureView(),
				ColorB::White,
				Float2{0.f, 0.f},
				Float2{rect.Width() / (float)desc._textureDesc._width, rect.Height() / (float)desc._textureDesc._height});
		}

		ProcessInputResult ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input)
		{
			return ProcessInputResult::Passthrough;
		}

		FontRenderingManagerDisplay(std::shared_ptr<RenderOverlays::FontRenderingManager> renderingManager) : _renderingManager(renderingManager) {}
		~FontRenderingManagerDisplay() {}
	protected:
		std::shared_ptr<RenderOverlays::FontRenderingManager> _renderingManager;
	};
	
	class FontThrashTestDisplay : public IInteractiveTestOverlay
	{
	public:
		virtual void Render(
			RenderCore::Techniques::ParsingContext& parserContext,
			IInteractiveTestHelper& testHelper) override
		{
			Update(parserContext.GetThreadContext());

			using namespace RenderCore;
			using namespace RenderOverlays;
			auto overlayContext = MakeImmediateOverlayContext(
				parserContext.GetThreadContext(), *testHelper.GetOverlayApparatus()->_immediateDrawables,
				testHelper.GetOverlayApparatus()->_fontRenderingManager.get());

			Int2 viewport {parserContext.GetViewport()._width, parserContext.GetViewport()._height};
			RenderOverlays::ImmediateLayout layout{Rect{Coord2{0, 0}, Coord2{viewport[0], viewport[1]}}};

			// draw....
			if (_mode == Mode::ShowFontTexture) {
				if (_renderingManager->GetMode() == RenderOverlays::FontRenderingManager::Mode::Texture2D) {
					RenderOverlays::DebuggingDisplay::Interactables interactables;
					RenderOverlays::DebuggingDisplay::InterfaceState interfaceState;
					_display->Render(*overlayContext, layout, interactables, interfaceState);
				}
			} else if (_mode == Mode::ScrollInfiniteText) {
				// stress out the system by continuously rendering full screens of random text with different fonts
				for (;;) {
					auto& font = _fonts[std::uniform_int_distribution<>(0, _fonts.size()-1)(_rng)];
					auto fontProps = font->GetFontProperties();
					auto rect = layout.AllocateFullWidth(fontProps._lineHeight);
					if (rect.Height() <= 0) break;
					const auto chrCount = 64;
					ucs4 chrs[chrCount];
					for (auto& c:chrs) c = std::uniform_int_distribution<>(33, 126)(_rng);
					DrawTextFlags::BitField flags = 0;
					RenderOverlays::Draw(
						parserContext.GetThreadContext(), *testHelper.GetOverlayApparatus()->_immediateDrawables,
						*_renderingManager, *font, flags,
						rect._topLeft[0], rect._topLeft[1] + fontProps._ascender, rect._bottomRight[0], rect._bottomRight[1],
						MakeStringSection(chrs, &chrs[chrCount]),
						1.0f, 1.0f, ColorB::White);
				}
			}

			_renderingManager->AddUploadBarrier(parserContext.GetThreadContext());

			auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext, LoadStore::Clear);
			RenderOverlays::ExecuteDraws(
				parserContext, rpi,
				*testHelper.GetOverlayApparatus());
		}

		void Update(RenderCore::IThreadContext& threadContext)
		{
			if (_pause) return;
			
			if (_mode == Mode::ShowFontTexture) {
				if (_renderingManager->GetMode() == RenderOverlays::FontRenderingManager::Mode::Texture2D) {
					const unsigned glyphsPerFrame = 8;
					for (unsigned c=0; c<glyphsPerFrame; ++c) {
						auto font = std::uniform_int_distribution<>(0, _fonts.size()-1)(_rng);
						auto chr = std::uniform_int_distribution<>(33, 126)(_rng);		// main displayable ascii characters [33-126]
						auto& bitmap = _renderingManager->GetBitmap(threadContext, *_fonts[font], chr);
						REQUIRE(bitmap._tcBottomRight[0] != bitmap._tcTopLeft[0]);

						// if (bitmap._tcBottomRight[0] == bitmap._tcTopLeft[0])
							// std::cout << "Failed allocation" << std::endl;
					}
				}
			}
			_renderingManager->OnFrameBarrier();
		}

		virtual bool OnInputEvent(
			const PlatformRig::InputContext& context,
			const OSServices::InputSnapshot& evnt,
			IInteractiveTestHelper& testHelper) override
		{
			if (evnt._pressedChar == ' ') { _pause = !_pause; return true; }
			else if (evnt._pressedChar == 'm') {
				if (_mode == Mode::ShowFontTexture) {
					_mode = Mode::ScrollInfiniteText;
				} else {
					_mode = Mode::ShowFontTexture;
				}
			}
			return false;
		}

		FontThrashTestDisplay(RenderCore::IDevice& device, RenderOverlays::FontRenderingManager::Mode fontRenderingMode)
		: _rng{5492559264231}
		{
			std::pair<StringSection<>, int> fonts[] {
				std::make_pair("Petra", 8),
				std::make_pair("Petra", 10),
				std::make_pair("Petra", 12),
				std::make_pair("Petra", 14),
				std::make_pair("Petra", 16),
				std::make_pair("Petra", 20),
				std::make_pair("Petra", 32),
				std::make_pair("Petra", 38),
				std::make_pair("Petra", 46),

				std::make_pair("Anka", 8),
				std::make_pair("Anka", 10),
				std::make_pair("Anka", 12),
				std::make_pair("Anka", 14),
				std::make_pair("Anka", 16),
				std::make_pair("Anka", 20),
				std::make_pair("Anka", 32),
				std::make_pair("Anka", 38),
				std::make_pair("Anka", 46),

				std::make_pair("DosisExtraBold", 12),
				std::make_pair("DosisExtraBold", 16),
				std::make_pair("DosisExtraBold", 20),
				std::make_pair("DosisExtraBold", 32),
				std::make_pair("DosisExtraBold", 38),
				std::make_pair("DosisExtraBold", 46)
			};
			_fonts.reserve(dimof(fonts));
			for (auto f:fonts) {
				auto m = RenderOverlays::MakeFont(f.first, f.second);
				m->StallWhilePending();
				_fonts.push_back(m->Actualize());
			}

			_renderingManager = std::make_shared<RenderOverlays::FontRenderingManager>(device, fontRenderingMode);
			_display = std::make_shared<FontRenderingManagerDisplay>(_renderingManager);
		}

		std::mt19937_64 _rng;
		bool _pause = false;
		std::vector<std::shared_ptr<RenderOverlays::Font>> _fonts;
		std::shared_ptr<RenderOverlays::FontRenderingManager> _renderingManager;
		std::shared_ptr<FontRenderingManagerDisplay> _display;

		enum class Mode { ShowFontTexture, ScrollInfiniteText };
		Mode _mode = Mode::ScrollInfiniteText;
	};

	TEST_CASE( "FontThrashTest-2D", "[renderoverlays]" )
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

		auto tester = std::make_shared<FontThrashTestDisplay>(*testHelper->GetDevice(), RenderOverlays::FontRenderingManager::Mode::Texture2D);
		testHelper->Run(visCamera, tester);
	}

	TEST_CASE( "FontThrashTest-LinearBuffer", "[renderoverlays]" )
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

		auto tester = std::make_shared<FontThrashTestDisplay>(*testHelper->GetDevice(), RenderOverlays::FontRenderingManager::Mode::LinearBuffer);
		testHelper->Run(visCamera, tester);
	}

}
