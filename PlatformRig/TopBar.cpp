// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TopBar.h"
#include "ThemeStaticData.h"
#include "../RenderOverlays/ShapesRendering.h"
#include "../RenderOverlays/OverlayPrimitives.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../RenderOverlays/OverlayEffects.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/UniformsStream.h"
#include "../Tools/EntityInterface/MountedData.h"
#include <vector>

using namespace Utility::Literals;

namespace PlatformRig
{
	using namespace RenderOverlays;
	using namespace RenderOverlays::DebuggingDisplay;

	struct TopBarStaticData
	{
		unsigned _topMargin = 12;
		unsigned _height = 42;
		unsigned _borderMargin = 4;
		unsigned _borderWidth = 2;
		unsigned _underBarMargin = 8;

		unsigned _preHeadingMargin = 64;
		unsigned _headingHeight = 46;
		unsigned _headingPadding = 8;

		unsigned _frameRigAreaWidth = 160;
		unsigned _frameRigPaddingLeft = 20;
		unsigned _frameRigPaddingRight = 20;
		unsigned _frameRigPaddingTop = 2;
		unsigned _frameRigPaddingBottom = 2;

		TopBarStaticData() = default;

		template<typename Formatter>
			TopBarStaticData(Formatter& fmttr)
		{
			uint64_t keyname;
			while (TryKeyedItem(fmttr, keyname)) {
				switch (keyname) {
				case "TopMargin"_h: _topMargin = Formatters::RequireCastValue<decltype(_topMargin)>(fmttr); break;
				case "Height"_h: _height = Formatters::RequireCastValue<decltype(_height)>(fmttr); break;
				case "BorderMargin"_h: _borderMargin = Formatters::RequireCastValue<decltype(_borderMargin)>(fmttr); break;
				case "BorderWidth"_h: _borderWidth = Formatters::RequireCastValue<decltype(_borderWidth)>(fmttr); break;
				case "UnderBarMargin"_h: _underBarMargin = Formatters::RequireCastValue<decltype(_underBarMargin)>(fmttr); break;

				case "PreHeadingMargin"_h: _preHeadingMargin = Formatters::RequireCastValue<decltype(_height)>(fmttr); break;
				case "HeadingHeight"_h: _headingHeight = Formatters::RequireCastValue<decltype(_headingHeight)>(fmttr); break;
				case "HeadingPadding"_h: _headingPadding = Formatters::RequireCastValue<decltype(_headingPadding)>(fmttr); break;

				case "FrameRigAreaWidth"_h: _frameRigAreaWidth = Formatters::RequireCastValue<decltype(_frameRigAreaWidth)>(fmttr); break;
				case "FrameRigPaddingLeft"_h: _frameRigPaddingLeft = Formatters::RequireCastValue<decltype(_frameRigPaddingLeft)>(fmttr); break;
				case "FrameRigPaddingRight"_h: _frameRigPaddingRight = Formatters::RequireCastValue<decltype(_frameRigPaddingRight)>(fmttr); break;
				case "FrameRigPaddingTop"_h: _frameRigPaddingTop = Formatters::RequireCastValue<decltype(_frameRigPaddingTop)>(fmttr); break;
				case "FrameRigPaddingBottom"_h: _frameRigPaddingBottom = Formatters::RequireCastValue<decltype(_frameRigPaddingBottom)>(fmttr); break;
				default: SkipValueOrElement(fmttr); break;
				}
			}
		}
	};

	class TopBarManager : public ITopBarManager
	{
	public:
		void RenderExpandedBar(
			IOverlayContext& context,
			Rect outerRect);

		void RenderMinimalBar(
			IOverlayContext& context,
			Rect outerRect);

		void RenderObjectBkgrnd(
			IOverlayContext& context,
			Rect rect, ColorB col, unsigned height);

		Rect ScreenTitle(RenderOverlays::IOverlayContext&, Layout& layout, float requestedWidth) override;
		Rect Menu(RenderOverlays::IOverlayContext&, float requestedWidth) override;
		Rect FrameRigDisplay(RenderOverlays::IOverlayContext&) override;
		void RenderFrame(IOverlayContext& context) override;

		TopBarManager(const Rect& outerRect);
	private:
		const TopBarStaticData& _topBarStaticData;
		const ThemeStaticData& _themeStaticData;
		Rect _outerRect;
		bool _renderedFrame = false;
		Layout _layout;
		unsigned _menusAllocated = 0;
	};

	static RenderCore::UniformsStreamInterface CreateTexturedUSI()
	{
		RenderCore::UniformsStreamInterface usi;
		usi.BindResourceView(0, "InputTexture"_h);
		return usi;
	}
	static RenderCore::UniformsStreamInterface s_texturedUSI = CreateTexturedUSI();

	void TopBarManager::RenderExpandedBar(
		IOverlayContext& context,
		Rect outerRect)
	{
		// Render the top bar along the top of the viewport, including the areas for the sections 
		// as requested

		auto xAtPoint = outerRect._bottomRight[0] - (_topBarStaticData._height + _topBarStaticData._frameRigPaddingLeft + _topBarStaticData._frameRigPaddingRight + _topBarStaticData._frameRigAreaWidth);
		auto xAtShoulder = outerRect._bottomRight[0] - (_topBarStaticData._frameRigPaddingLeft + _topBarStaticData._frameRigPaddingRight + _topBarStaticData._frameRigAreaWidth);

		Coord2 vertexPositions[6] {
			{ outerRect._topLeft[0], outerRect._topLeft[1] + _topBarStaticData._topMargin },
			{ outerRect._bottomRight[0], outerRect._topLeft[1] + _topBarStaticData._topMargin },
			{ outerRect._topLeft[0], outerRect._topLeft[1] + _topBarStaticData._topMargin + _topBarStaticData._height },
			{ xAtPoint, outerRect._topLeft[1] + _topBarStaticData._topMargin + _topBarStaticData._height },
			{ xAtShoulder, outerRect._topLeft[1] + _topBarStaticData._topMargin + 2 * _topBarStaticData._height },
			{ outerRect._bottomRight[0], outerRect._topLeft[1] + _topBarStaticData._topMargin + 2 * _topBarStaticData._height },
		};
		unsigned indices[] {
			1, 0, 3,
			3, 0, 2,
			3, 4, 1,
			1, 4, 5
		};

		RenderOverlays::BlurryBackgroundEffect* blurryBackground;
		RenderCore::Techniques::ImmediateDrawableMaterial material;
		if ((blurryBackground = context.GetService<RenderOverlays::BlurryBackgroundEffect>()))
			if (auto res = blurryBackground->GetResourceView()) {
				material._uniformStreamInterface = &s_texturedUSI;
				material._uniforms._resourceViews.push_back(std::move(res));
			}

		auto vertices = context.DrawGeometry(dimof(indices), Vertex_PCT::s_inputElements2D, std::move(material)).Cast<Vertex_PCT*>();
		for (unsigned c=0; c<dimof(indices); ++c)
			vertices[c] = { AsPixelCoords(vertexPositions[indices[c]]), HardwareColor(_themeStaticData._semiTransparentTint), Float2(0,0) };
		if (blurryBackground)
			for (unsigned c=0; c<dimof(indices); ++c)
				vertices[c]._texCoord = blurryBackground->AsTextureCoords(vertexPositions[indices[c]]);

		// render dashed line along the top
		Float2 topDashLine[] {
			Float2 { outerRect._topLeft[0], outerRect._topLeft[1] + _topBarStaticData._topMargin + _topBarStaticData._borderMargin },
			Float2 { outerRect._bottomRight[0], outerRect._topLeft[1] + _topBarStaticData._topMargin + _topBarStaticData._borderMargin }
		};

		// cosine rule for triangles
		// c^2 = a^2 + b^2 - 2ab.cos(C)
		// c is 45 degrees, and A & b are topBarStaticData._borderMargin
		// float a = topBarStaticData._borderMargin * std::sqrt(2*(1 - std::cos(gPI/4.f)));
		float a = _topBarStaticData._borderMargin * std::tan(gPI/8.0f);

		Float2 bottomDashLine[] {
			Float2 { outerRect._topLeft[0], outerRect._topLeft[1] + _topBarStaticData._topMargin + _topBarStaticData._height - _topBarStaticData._borderMargin },
			Float2 { xAtPoint + a, outerRect._topLeft[1] + _topBarStaticData._topMargin + _topBarStaticData._height - _topBarStaticData._borderMargin },

			Float2 { xAtShoulder + a, outerRect._topLeft[1] + _topBarStaticData._topMargin + 2*_topBarStaticData._height - _topBarStaticData._borderMargin },
			Float2 { outerRect._bottomRight[0], outerRect._topLeft[1] + _topBarStaticData._topMargin + 2*_topBarStaticData._height - _topBarStaticData._borderMargin }
		};

		DashLine(context, topDashLine, _themeStaticData._topBarBorderColor, (float)_topBarStaticData._borderWidth);
		DashLine(context, bottomDashLine, _themeStaticData._topBarBorderColor, (float)_topBarStaticData._borderWidth);
	}

	void TopBarManager::RenderMinimalBar(
		IOverlayContext& context,
		Rect outerRect)
	{
		auto xAtPoint = outerRect._bottomRight[0] - (_topBarStaticData._height + _topBarStaticData._frameRigPaddingLeft + _topBarStaticData._frameRigPaddingRight + _topBarStaticData._frameRigAreaWidth);
		auto xAtShoulder = outerRect._bottomRight[0] - (_topBarStaticData._frameRigPaddingLeft + _topBarStaticData._frameRigPaddingRight + _topBarStaticData._frameRigAreaWidth);

		Coord2 vertexPositions[6] {
			{ outerRect._bottomRight[0], 	outerRect._topLeft[1] + _topBarStaticData._topMargin },
			{ xAtShoulder, 					outerRect._topLeft[1] + _topBarStaticData._topMargin },
			{ xAtPoint, 					outerRect._topLeft[1] + _topBarStaticData._topMargin + _topBarStaticData._height },
			{ xAtShoulder, 					outerRect._topLeft[1] + _topBarStaticData._topMargin + 2 * _topBarStaticData._height },
			{ outerRect._bottomRight[0], 	outerRect._topLeft[1] + _topBarStaticData._topMargin + 2 * _topBarStaticData._height },
		};
		unsigned indices[] {
			0, 1, 4,
			4, 1, 3,
			3, 1, 2
		};

		RenderOverlays::BlurryBackgroundEffect* blurryBackground;
		RenderCore::Techniques::ImmediateDrawableMaterial material;
		if ((blurryBackground = context.GetService<RenderOverlays::BlurryBackgroundEffect>()))
			if (auto res = blurryBackground->GetResourceView()) {
				material._uniformStreamInterface = &s_texturedUSI;
				material._uniforms._resourceViews.push_back(std::move(res));
			}

		auto vertices = context.DrawGeometry(dimof(indices), Vertex_PCT::s_inputElements2D, std::move(material)).Cast<Vertex_PCT*>();
		for (unsigned c=0; c<dimof(indices); ++c)
			vertices[c] = { AsPixelCoords(vertexPositions[indices[c]]), HardwareColor(_themeStaticData._semiTransparentTint), Float2(0,0) };
		if (blurryBackground)
			for (unsigned c=0; c<dimof(indices); ++c)
				vertices[c]._texCoord = blurryBackground->AsTextureCoords(vertexPositions[indices[c]]);

		float a = _topBarStaticData._borderMargin * std::tan(gPI/8.0f);
		float b = std::sqrt(2.f) * _topBarStaticData._borderMargin;
		Float2 dashLine[] {
			
			Float2 { outerRect._bottomRight[0],		outerRect._topLeft[1] + _topBarStaticData._topMargin + _topBarStaticData._borderMargin },
			Float2 { xAtShoulder + a, 				outerRect._topLeft[1] + _topBarStaticData._topMargin + _topBarStaticData._borderMargin },

			Float2 { xAtPoint + b, 					outerRect._topLeft[1] + _topBarStaticData._topMargin + _topBarStaticData._height },

			Float2 { xAtShoulder + a, 				outerRect._topLeft[1] + _topBarStaticData._topMargin + 2*_topBarStaticData._height - _topBarStaticData._borderMargin },
			Float2 { outerRect._bottomRight[0], 	outerRect._topLeft[1] + _topBarStaticData._topMargin + 2*_topBarStaticData._height - _topBarStaticData._borderMargin }
		};
		DashLine(context, dashLine, _themeStaticData._topBarBorderColor, (float)_topBarStaticData._borderWidth);
	}

	void TopBarManager::RenderObjectBkgrnd(
		IOverlayContext& context,
		Rect frame, ColorB col, unsigned height)
	{
		// draw a rhombus around the frame, but with some extra triangles
		RenderCore::Techniques::ImmediateDrawableMaterial material;
		auto vertices = context.DrawGeometry(6, Vertex_PC::s_inputElements2D, std::move(material)).Cast<Vertex_PC*>();
		Coord2 A { frame._topLeft[0], (frame._topLeft[1] + frame._bottomRight[1] - height) / 2 };
		Coord2 B { frame._topLeft[0] - height, (frame._topLeft[1] + frame._bottomRight[1] + height) / 2 };
		Coord2 C { frame._bottomRight[0], (frame._topLeft[1] + frame._bottomRight[1] + height) / 2 };
		Coord2 D { frame._bottomRight[0] + height, (frame._topLeft[1] + frame._bottomRight[1] - height) / 2 };
		vertices[0] = Vertex_PC { AsPixelCoords(B), HardwareColor(col) };
		vertices[1] = Vertex_PC { AsPixelCoords(C), HardwareColor(col) };
		vertices[2] = Vertex_PC { AsPixelCoords(A), HardwareColor(col) };

		vertices[3] = Vertex_PC { AsPixelCoords(A), HardwareColor(col) };
		vertices[4] = Vertex_PC { AsPixelCoords(C), HardwareColor(col) };
		vertices[5] = Vertex_PC { AsPixelCoords(D), HardwareColor(col) };
	}

	Rect TopBarManager::ScreenTitle(RenderOverlays::IOverlayContext& overlayContext, Layout& layout, float requestedWidth)
	{
		// awkwardly, we render the parts we're interested in on demand, because we don't actually know the state of the bar until
		// we get to this point
		if (!_renderedFrame) {
			RenderExpandedBar(overlayContext, _outerRect);
			_renderedFrame = true;
		}

		Rect frame = _layout.AllocateFullHeight(_topBarStaticData._headingPadding*2 + requestedWidth);
		RenderObjectBkgrnd(overlayContext, frame, _themeStaticData._headingBkgrnd, _topBarStaticData._headingHeight);
		_layout.AllocateFullHeight(_topBarStaticData._headingHeight);		// extra space for the border

		Rect content;
		content._topLeft = frame._topLeft + Coord2 { _topBarStaticData._headingPadding, _topBarStaticData._headingPadding };
		content._bottomRight = frame._bottomRight - Coord2{ _topBarStaticData._headingPadding, _topBarStaticData._headingPadding };

		// Adjust the layout down, because we may have cut off some of the usable area for the display
		layout._maximumSize._topLeft[1] = std::max(layout._maximumSize._topLeft[1], int(_topBarStaticData._topMargin + 2*_topBarStaticData._height + _topBarStaticData._underBarMargin));
		return content;
	}

	Rect TopBarManager::Menu(RenderOverlays::IOverlayContext& overlayContext, float requestedWidth)
	{
		if (!_renderedFrame) {
			RenderExpandedBar(overlayContext, _outerRect);
			_renderedFrame = true;
		}

		Rect frame = _layout.AllocateFullHeight(_topBarStaticData._headingPadding*2 + requestedWidth);
		RenderObjectBkgrnd(overlayContext, frame, _themeStaticData._menuBkgrnd[std::min(_menusAllocated, (unsigned)dimof(_themeStaticData._menuBkgrnd))], _topBarStaticData._headingHeight);
		_layout.AllocateFullHeight(_topBarStaticData._headingHeight);		// extra space for the border
		++_menusAllocated;

		Rect content;
		content._topLeft = frame._topLeft + Coord2 { _topBarStaticData._headingPadding, _topBarStaticData._headingPadding };
		content._bottomRight = frame._bottomRight - Coord2{ _topBarStaticData._headingPadding, _topBarStaticData._headingPadding };
		return content;
	}

	Rect TopBarManager::FrameRigDisplay(RenderOverlays::IOverlayContext& overlayContext)
	{
		if (!_renderedFrame) {
			RenderMinimalBar(overlayContext, _outerRect);
			_renderedFrame = true;
		}

		return Rect {
			{ _outerRect._bottomRight[0] - _topBarStaticData._frameRigPaddingRight - _topBarStaticData._frameRigAreaWidth, 	_outerRect._topLeft[1] + _topBarStaticData._topMargin },
			{ _outerRect._bottomRight[0] - _topBarStaticData._frameRigPaddingRight, 										_outerRect._topLeft[1] + _topBarStaticData._topMargin + 2*_topBarStaticData._height }
		};
	}

	void TopBarManager::RenderFrame(IOverlayContext& context)
	{
		if (!_renderedFrame) {
			RenderMinimalBar(context, _outerRect);
			_renderedFrame = true;
		}
	}

	TopBarManager::TopBarManager(const Rect& outerRect)
	: _topBarStaticData(EntityInterface::MountedData<TopBarStaticData>::LoadOrDefault("cfg/displays/topbar"))
	, _themeStaticData(EntityInterface::MountedData<ThemeStaticData>::LoadOrDefault("cfg/displays/theme"))
	, _outerRect(outerRect)
	, _layout {
		{
			{
				int(outerRect._topLeft[0] + _topBarStaticData._preHeadingMargin),
				int(outerRect._topLeft[1] + _topBarStaticData._topMargin + _topBarStaticData._height/2 - _topBarStaticData._headingHeight/2)
			},
			{
				int(outerRect._bottomRight[0] - _topBarStaticData._frameRigPaddingRight - _topBarStaticData._frameRigAreaWidth),
				int(outerRect._topLeft[1] + _topBarStaticData._topMargin + _topBarStaticData._height/2 + _topBarStaticData._headingHeight/2)
			}
		}}
	{
		_layout._paddingInternalBorder = 0;
		_layout._paddingBetweenAllocations = 0;
	}

	std::shared_ptr<ITopBarManager> CreateTopBarManager(const RenderOverlays::Rect& outerRect)
	{
		return std::make_shared<TopBarManager>(outerRect);
	}

	ITopBarManager::~ITopBarManager() = default;
}

