// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InvalidAssetDisplay.h"
#include "../TopBar.h"
#include "../ThemeStaticData.h"
#include "../../RenderOverlays/ShapesRendering.h"
#include "../../RenderOverlays/DrawText.h"
#include "../../RenderOverlays/OverlayEffects.h"
#include "../../Assets/AssetSetManager.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetHeap.h"
#include "../../Assets/OperationContext.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../Tools/EntityInterface/MountedData.h"
#include "../../Utility/Threading/Mutex.h"
#include <sstream>

#pragma clang diagnostic ignored "-Wmicrosoft-sealed"

using namespace Assets::Literals;
using namespace Utility::Literals;

namespace PlatformRig { namespace Overlays
{
	class TrackedAssetList : public ITrackedAssetList
	{
	public:
		// note that callers must lock the asset list using Lock() before calling GetCurrentRecords() and maintain
		// the lock while using the result of that function
		void Lock() override SEALED { _currentRecordsLock.lock(); }
		void Unlock() override SEALED { _currentRecordsLock.unlock(); }

		IteratorRange<const std::pair<TypeCodeAndId,::Assets::AssetHeapRecord>*> GetCurrentRecords() const override SEALED { return _currentRecords; }

		unsigned BindOnChange(std::function<void()>&& fn) override
		{
			return _onChangeSignal.Bind(std::move(fn));
		}

		void UnbindOnChange(unsigned signalId) override
		{
			_onChangeSignal.Unbind(signalId);
		}

		TrackedAssetList(std::shared_ptr<Assets::IAssetTracking> tracking, ::Assets::AssetState trackingState)
		: _tracking(std::move(tracking)), _trackingState(trackingState)
		{
			_signalId = _tracking->BindUpdateSignal(
				[this](IteratorRange<const std::pair<uint64_t, ::Assets::AssetHeapRecord>*> updates) {
					ScopedLock(_currentRecordsLock);
					auto r = _currentRecords.begin();
					TypeCodeAndId lastCode{0,0};
					for (const auto& u:updates) {
						TypeCodeAndId code { u.first, u.second._typeCode };
						assert(code > lastCode); lastCode = code;		// ensure we're in sorted order
						while (r != _currentRecords.end() && r->first < code) ++r;
						if (r != _currentRecords.end() && r->first == code) {
							if (u.second._state == this->_trackingState) {
								r->second = u.second;
							} else {
								r = _currentRecords.erase(r);
							}
						} else if (u.second._state == this->_trackingState) {
							r = _currentRecords.insert(r, std::make_pair(code, u.second));
						}
					}
					_onChangeSignal.Invoke();
				});
		}
		~TrackedAssetList()
		{
			_tracking->UnbindUpdateSignal(_signalId);
		}
	private:
		Threading::Mutex _currentRecordsLock;
		std::vector<std::pair<TypeCodeAndId,::Assets::AssetHeapRecord>> _currentRecords;
		std::shared_ptr<::Assets::IAssetTracking> _tracking;
		unsigned _signalId;
		::Assets::AssetState _trackingState;
		Signal<> _onChangeSignal;
	};

	std::shared_ptr<ITrackedAssetList> CreateTrackedAssetList(std::shared_ptr<Assets::IAssetTracking> tracking, ::Assets::AssetState state)
	{
		return std::make_shared<TrackedAssetList>(std::move(tracking), state);
	}

	ITrackedAssetList::~ITrackedAssetList() = default;

	class InvalidAssetDisplay : public RenderOverlays::DebuggingDisplay::IWidget
	{
	public:
		using IOverlayContext = RenderOverlays::IOverlayContext;
		using Layout = RenderOverlays::DebuggingDisplay::Layout;
		using Interactables = RenderOverlays::DebuggingDisplay::Interactables;
		using InterfaceState = RenderOverlays::DebuggingDisplay::InterfaceState;
		using InputSnapshot = OSServices::InputSnapshot;

		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
		{
			const unsigned lineHeight = 20;
			const auto titleBkground = RenderOverlays::ColorB { 0, 10, 64 }; 

			using namespace Assets;
			_trackedAssetList->Lock();
			TRY {
				for (const auto&r:_trackedAssetList->GetCurrentRecords()) {
					assert(r.second._state == AssetState::Invalid);

					auto titleRect = layout.AllocateFullWidth(lineHeight);
					if (titleRect.Height() < lineHeight) break;
					FillRectangle(context, titleRect, titleBkground);
					RenderOverlays::DrawText().Draw(context, titleRect, r.second._initializer);

					auto msg = std::stringstream{AsString(r.second._actualizationLog)};
					for (std::string line; std::getline(msg, line, '\n');) {
						auto allocation = layout.AllocateFullWidth(lineHeight);
						if (allocation.Height() <= 0) break;
						RenderOverlays::DrawText().Color(0xffcfcfcf).Draw(context, allocation, line);
					}
				}
			} CATCH(...) {
				_trackedAssetList->Unlock();
				throw;
			} CATCH_END
			_trackedAssetList->Unlock();
		}

		InvalidAssetDisplay(std::shared_ptr<Assets::IAssetTracking> tracking)
		{
			_trackedAssetList = std::make_shared<TrackedAssetList>(std::move(tracking), ::Assets::AssetState::Invalid);
		}
	private:
		std::shared_ptr<TrackedAssetList> _trackedAssetList;
	};

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateInvalidAssetDisplay(std::shared_ptr<Assets::IAssetTracking> tracking)
	{
		return std::make_shared<InvalidAssetDisplay>(std::move(tracking));
	}

	struct OperationContextStaticData
	{
		RenderOverlays::ColorB _bright0 = RenderOverlays::ColorB::White;
		RenderOverlays::ColorB _bright1 = 0xffafafaf;
		RenderOverlays::ColorB _incomplete = 0xff000000;
		RenderOverlays::ColorB _complete = 0xff668d84;
		RenderOverlays::ColorB _message = 0xffafafaf;
		RenderOverlays::ColorB _border = RenderOverlays::ColorB::White;
		std::string _font = "Metropolitano:16";
		unsigned _borderWeight = 3;
		float _innerRadius = .75f;
		unsigned _sectionCount = 3*4;
		float _rotationTimeMS = 3000.f;

		OperationContextStaticData() = default;

		template<typename Formatter>
			OperationContextStaticData(Formatter& fmttr)
		{
			uint64_t keyname;
			while (TryKeyedItem(fmttr, keyname)) {
				switch (keyname) {
				case "Bright0"_h: _bright0 = PlatformRig::DeserializeColor(fmttr); break;
				case "Bright1"_h: _bright1 = PlatformRig::DeserializeColor(fmttr); break;
				case "Incomplete"_h: _incomplete = PlatformRig::DeserializeColor(fmttr); break;
				case "Complete"_h: _complete = PlatformRig::DeserializeColor(fmttr); break;
				case "Border"_h: _border = PlatformRig::DeserializeColor(fmttr); break;
				case "Message"_h: _message = PlatformRig::DeserializeColor(fmttr); break;
				case "BorderWeight"_h: _borderWeight = Formatters::RequireCastValue<decltype(_borderWeight)>(fmttr); break;
				case "InnerRadius"_h: _innerRadius = Formatters::RequireCastValue<decltype(_innerRadius)>(fmttr); break;
				case "SectionCount"_h: _sectionCount = Formatters::RequireCastValue<decltype(_sectionCount)>(fmttr); break;
				case "RotationTimeMS"_h: _rotationTimeMS = Formatters::RequireCastValue<decltype(_rotationTimeMS)>(fmttr); break;
				case "Font"_h: _font = Formatters::RequireStringValue(fmttr); break;
				default: SkipValueOrElement(fmttr); break;
				}
			}
		}
	}; 

	static void DrawProgressCircle(RenderOverlays::IOverlayContext& context, RenderOverlays::Rect frame, float progress, std::chrono::steady_clock::duration duration, const OperationContextStaticData& staticData)
	{
		RenderOverlays::Rect innerCircle { frame._topLeft + Coord2(4, 4), frame._bottomRight - Coord2(4, 4) };
		Coord2 center = (innerCircle._topLeft + innerCircle._bottomRight) / 2;
		float radius = std::min(innerCircle.Width(), innerCircle.Height()) / 2.f;
		const float innerRadius = radius * staticData._innerRadius;
		const unsigned sectionCount = staticData._sectionCount;
		const float sectionAngle = 2 * gPI / float(sectionCount);
		auto brightSection = unsigned(std::fmod(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() / staticData._rotationTimeMS, 1.f) * sectionCount);
		for (unsigned s=0; s<sectionCount; ++s) {
			float theta = (s - .5f) * sectionAngle;

			const unsigned divisionCount = 6;
			Float2 trianglePts[(divisionCount-1)*6];
			for (unsigned t=0; t<divisionCount-1; ++t) {
				auto[s0, c0] = XlSinCos(theta + t/float(divisionCount)*sectionAngle);
				auto[s1, c1] = XlSinCos(theta + (t+1)/float(divisionCount)*sectionAngle);

				trianglePts[t*6+0] = center + radius * Float2 { c0, s0 };
				trianglePts[t*6+1] = center + innerRadius * Float2 { c0, s0 };
				trianglePts[t*6+2] = center + radius * Float2 { c1, s1 };

				trianglePts[t*6+3] = center + radius * Float2 { c1, s1 };
				trianglePts[t*6+4] = center + innerRadius * Float2 { c0, s0 };
				trianglePts[t*6+5] = center + innerRadius * Float2 { c1, s1 };
			}

			auto col = staticData._incomplete;
			if (s == brightSection) col = staticData._bright0;
			else if ((s+1)%sectionCount == brightSection) col = staticData._bright1;
			else if ((s+1)/float(sectionCount) <= progress) col = staticData._complete;

			RenderOverlays::DebuggingDisplay::FillTriangles(context, trianglePts, col, (divisionCount-1)*2);
		}

		RenderOverlays::OutlineEllipse(context, frame, staticData._border, staticData._borderWeight);
	}

	void OperationContextDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
	{
		auto activeOperations = _opContext->GetActiveOperations();

		if (auto* topBar = context.GetService<ITopBarManager>()) {
			const char headingString[] = "Active Compiles";
			if (auto* headingFont = _headingFont->TryActualize()) {
				auto rect = topBar->ScreenTitle(context, layout, StringWidth(**headingFont, MakeStringSection(headingString)));
				if (IsGood(rect) && headingFont)
					RenderOverlays::DrawText()
						.Font(**headingFont)
						.Color(RenderOverlays::ColorB::Black)
						.Alignment(RenderOverlays::TextAlignment::Left)
						.Flags(0)
						.Draw(context, rect, headingString);
			}
		}

		const unsigned sectionPadding = 4;
		const unsigned sectionMargin = 6;

		auto now = std::chrono::steady_clock::now();

		auto* blurryBackground = context.GetService<RenderOverlays::BlurryBackgroundEffect>();
		auto& staticData = EntityInterface::MountedData<OperationContextStaticData>::LoadOrDefault("cfg/displays/operationcontext"_initializer);

		auto fnt = RenderOverlays::MakeFont(staticData._font)->TryActualize();
		if (!fnt) return;
		const auto lineHeight = (*fnt)->GetFontProperties()._lineHeight;

		if (_offset) {
			const auto* text = "^ ^ ^";
			RenderOverlays::DrawText()
				.Font(**fnt)
				.Alignment(RenderOverlays::TextAlignment::Center)
				.Draw(context, layout.AllocateFullWidth(lineHeight), MakeStringSection(text));
		}
		
		for (auto op=activeOperations.begin()+std::min(activeOperations.size(), (size_t)_offset); op!=activeOperations.end(); ++op) {
			auto h = lineHeight * 2 + 2 * (sectionPadding + sectionMargin);
			auto section = layout.AllocateFullWidth(h);
			if (section.Height() < h) break;

			if (blurryBackground) {
				RenderOverlays::ColorAdjustAndOutlineRoundedRectangle(
					context, section,
					blurryBackground->AsTextureCoords(section._topLeft), blurryBackground->AsTextureCoords(section._bottomRight),
					blurryBackground->GetResourceView(RenderOverlays::BlurryBackgroundEffect::Type::NarrowAccurateBlur), RenderOverlays::ColorAdjust{}, RenderOverlays::ColorB::White,
					staticData._border, 2.f,
					0.5f);
			} else
				RenderOverlays::OutlineRoundedRectangle(context, section, staticData._border, 2.f, 0.5f);

			auto circleArea = RenderOverlays::Rect { section._topLeft, {section._topLeft[0] + h, section._bottomRight[1]} };
			Coord2 trim = circleArea._bottomRight + circleArea._topLeft;
			circleArea._topLeft = (trim - Coord2{h, h})/2;
			circleArea._bottomRight = (trim + Coord2{h, h})/2;

			Layout textArea = RenderOverlays::Rect { {section._topLeft[0] + h + 16, section._topLeft[1]}, section._bottomRight };
			char buffer[1024];
			{
				auto description = textArea.AllocateFullWidth(lineHeight);
				RenderOverlays::StringEllipsis(buffer, dimof(buffer), **fnt, MakeStringSection(op->_description), description.Width());
				RenderOverlays::DrawText()
					.Font(**fnt)
					.Draw(context, description, buffer);
			}
			if (!op->_msg.empty()) {
				auto msg = textArea.AllocateFullWidth(lineHeight);
				RenderOverlays::StringEllipsis(buffer, dimof(buffer), **fnt, MakeStringSection(op->_msg), msg.Width());
				RenderOverlays::DrawText()
					.Font(**fnt)
					.Color(staticData._message)
					.Draw(context, msg, buffer);
			}

			if (op->_progress && op->_progress->second) {
				DrawProgressCircle(context, circleArea, op->_progress->first / float(op->_progress->second), now - op->_beginTime, staticData);
			} else {
				DrawProgressCircle(context, circleArea, 0.f, now - op->_beginTime, staticData);
			}
		}
	}

	ProcessInputResult OperationContextDisplay::ProcessInput(InterfaceState& interfaceState, const OSServices::InputSnapshot& input)
	{
		if (input._wheelDelta > 0) {
			_offset = std::max(0, _offset-1);
			return ProcessInputResult::Consumed;
		} else if (input._wheelDelta < 0) {
			++_offset;
			return ProcessInputResult::Consumed;
		}
		return ProcessInputResult::Passthrough;
	}

	OperationContextDisplay::OperationContextDisplay(std::shared_ptr<::Assets::OperationContext> opContext) : _opContext(opContext)
	{
		_headingFont = RenderOverlays::MakeFont("OrbitronBlack", 20);
	}
	OperationContextDisplay::~OperationContextDisplay() {}

}}

