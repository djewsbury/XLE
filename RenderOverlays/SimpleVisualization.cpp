// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SimpleVisualization.h"
#include "OverlayContext.h"
#include "Font.h"
#include "DebuggingDisplay.h"
#include "OverlayApparatus.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/Techniques/RenderPassUtils.h"
#include "../RenderCore/Techniques/Apparatuses.h"
#include "../Math/ProjectionMath.h"
#include "../Math/Transformations.h"
#include "../Assets/Marker.h"

namespace RenderOverlays
{
	class InternalVertex
	{
	public:
		Float3      _position;
		unsigned    _color;
	};

	static RenderCore::MiniInputElementDesc s_vertexInputLayout[] = {
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::POSITION, RenderCore::Format::R32G32B32_FLOAT },
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM }
	};

	void DrawBasisAxes(
		RenderCore::Techniques::IImmediateDrawables& immDrawables, RenderCore::Techniques::ParsingContext& parserContext, 
		Float2 ssMins, Float2 ssMaxs)
	{
			//
			//      Draw world space X, Y, Z axes (to make it easier to see what's going on)
			//


		const float pointerLength = 1.f;
		const float pointerRadialWidth = 0.025f;
		const unsigned pointerRadialVerts = 8;

		using namespace RenderCore;
		Techniques::ImmediateDrawableMaterial material;
		auto workingVertices = immDrawables.QueueDraw(
			pointerRadialVerts*6*3, MakeIteratorRange(s_vertexInputLayout), 
			material, Topology::TriangleList).Cast<InternalVertex*>();

		// use a custom projection matrix to put the geometry where we want on screen
		// -1 -> 1 becomes A -> B
		// (x*0.5+0.5)*(B-A)+A = x*0.5*(B-A) + 0.5*(B-A)+A
		auto viewport = parserContext.GetViewport();
		Float2 A { ssMins[0]/viewport._width*2.f-1.f, ssMins[1]/viewport._height*2.f-1.f };
		Float2 B { std::min(ssMaxs[0], viewport._width)/viewport._width*2.f-1.f, std::min(ssMaxs[1], viewport._height)/viewport._height*2.f-1.f };

		Float4x4 projAdjustment = Identity<Float4x4>();
		projAdjustment(0,0) = 0.5f*(B[0]-A[0]);
		projAdjustment(0,3) = 0.5f*(B[0]-A[0])+A[0];
		projAdjustment(1,1) = 0.5f*(B[1]-A[1]);
		projAdjustment(1,3) = 0.5f*(B[1]-A[1])+A[1];

		auto customProjMatrix = PerspectiveProjection(gPI/4.f, 1.0f, 0.01f, 100.f, GeometricCoordinateSpace::RightHanded, Techniques::GetDefaultClipSpaceType());
		customProjMatrix = Combine(customProjMatrix, projAdjustment);
		auto customCameraToWorld = parserContext.GetProjectionDesc()._cameraToWorld;
		SetTranslation(customCameraToWorld, ExtractForward_Cam(customCameraToWorld) * -std::sqrt(4));
		Float4x4 transform = Inverse(parserContext.GetProjectionDesc()._worldToProjection) * customProjMatrix * InvertOrthonormalTransform(customCameraToWorld);
		
		auto i = workingVertices.begin();
		auto utilityTransform = [](const Float3& p, const Float4x4& transform) {
			Float4 t = transform * Float4{p, 1.f};
			t /= t[3];		// fortunately homogeneous divide magic works here, so we can use 3d vectors as input
			return Truncate(t);
		};
		struct Pointer
		{
			Float3 _axis; unsigned _color;
			Float3 _tangent, _bitangent;
		};
		Float3 X { 1.f, 0.f, 0.f }, Y { 0.f, 1.f, 0.f}, Z { 0.f, 0.f, 1.f };
		Pointer pointers[] = {
			Pointer { pointerLength*X, 0xff4f4f9f, pointerRadialWidth*Z, pointerRadialWidth*Y },
			Pointer { pointerLength*Y, 0xff4f9f4f, pointerRadialWidth*X, pointerRadialWidth*Z },
			Pointer { pointerLength*Z, 0xff9f4f4f, pointerRadialWidth*Y, pointerRadialWidth*X }
		};
		for (auto&p:pointers) {
			float prevSinTheta = 0.f, prevCosTheta = 1.f;

			for (unsigned c=0; c<pointerRadialVerts; ++c) {
				float theta = (c+1)/float(pointerRadialVerts)*2.0f*float(M_PI);
				float sinTheta, cosTheta;
				std::tie(sinTheta, cosTheta) = XlSinCos(theta);

				i->_position = utilityTransform(p._tangent * prevCosTheta + p._bitangent * prevSinTheta, transform);
				i->_color = p._color;
				++i;

				i->_position = utilityTransform(p._tangent * prevCosTheta + p._bitangent * prevSinTheta + p._axis, transform);
				i->_color = p._color;
				++i;

				i->_position = utilityTransform(p._tangent * cosTheta + p._bitangent * sinTheta, transform);
				i->_color = p._color;
				++i;

				i->_position = utilityTransform(p._tangent * cosTheta + p._bitangent * sinTheta, transform);
				i->_color = p._color;
				++i;

				i->_position = utilityTransform(p._tangent * prevCosTheta + p._bitangent * prevSinTheta + p._axis, transform);
				i->_color = p._color;
				++i;

				i->_position = utilityTransform(p._tangent * cosTheta + p._bitangent * sinTheta + p._axis, transform);
				i->_color = p._color;
				++i;

				prevSinTheta = sinTheta; prevCosTheta = cosTheta;
			}
		}
		assert((i-workingVertices.begin()) == pointerRadialVerts*6*3);
	}

	static unsigned MakeGridColor(float scaleAlpha)
	{
		auto i = (unsigned)LinearInterpolate(0x8f, 0, scaleAlpha);
		return (i<<24u) | 0x003f3f3f;
	}

	void DrawGrid(
		RenderCore::Techniques::IImmediateDrawables& immDrawables, RenderCore::Techniques::ParsingContext& parserContext,
		float gridScaleFactor, Float3 origin)
	{
		// draw a grid to give some sense of scale
		// gridScaleFactor is a typically just the vertical between the camera at the grid origin. We'll use it to determine the spacing
		// of the grid lines (within some clamped range)
		gridScaleFactor /= 4.0f;
		gridScaleFactor = Clamp(gridScaleFactor, .1f, 1000.f);
		auto logScale = std::log10(gridScaleFactor);
		float gridScale = std::pow(10.0f, std::floor(logScale));
		auto scaleAlpha = logScale - std::floor(logScale);
		scaleAlpha *= scaleAlpha;

		const int radiusInLines = 50;
		auto lineCount = (radiusInLines*2+1) + (radiusInLines*2+1);

		using namespace RenderCore;
		Techniques::ImmediateDrawableMaterial material;
		auto workingVertices = immDrawables.QueueDraw(
			lineCount*2, MakeIteratorRange(s_vertexInputLayout), 
			material, Topology::LineList).Cast<InternalVertex*>();

		{
			auto i = workingVertices.begin();
			for (int x=-radiusInLines; x<=radiusInLines; ++x) {
				i->_position = Float3{x*gridScale, -radiusInLines*gridScale, 0.f};
				i->_color = ((x%10) == 0) ? 0x8f6f6f6f : MakeGridColor(scaleAlpha);
				++i;

				i->_position = Float3{x*gridScale, radiusInLines*gridScale, 0.f};
				i->_color = ((x%10) == 0) ? 0x8f6f6f6f : MakeGridColor(scaleAlpha);
				++i;
			}

			for (int y=-radiusInLines; y<=radiusInLines; ++y) {
				i->_position = Float3{-radiusInLines*gridScale, y*gridScale, 0.f};
				i->_color = ((y%10) == 0) ? 0x8f6f6f6f : MakeGridColor(scaleAlpha);
				++i;

				i->_position = Float3{radiusInLines*gridScale, y*gridScale, 0.f};
				i->_color = ((y%10) == 0) ? 0x8f6f6f6f : MakeGridColor(scaleAlpha);
				++i;
			}

			assert(i == workingVertices.end());
		}

		// draw lines in the cardinal directions, a little thicker to stand out
		{
			const float pointerRadialWidth = 0.0025f;
			const unsigned pointerRadialVerts = 8;

			auto workingVertices = immDrawables.QueueDraw(
				pointerRadialVerts*6*2, MakeIteratorRange(s_vertexInputLayout), 
				material, Topology::TriangleList).Cast<InternalVertex*>();
			
			struct Pointer
			{
				Float3 _start, _end; unsigned _color;
				Float3 _tangent, _bitangent;
			};
			Float3 X { 1.f, 0.f, 0.f }, Y { 0.f, 1.f, 0.f}, Z { 0.f, 0.f, 1.f };
			Pointer pointers[] = {
				Pointer { X*-radiusInLines*gridScale, X*radiusInLines*gridScale, 0xff4f4f9f, pointerRadialWidth*Z, pointerRadialWidth*Y },
				Pointer { Y*-radiusInLines*gridScale, Y*radiusInLines*gridScale, 0xff4f9f4f, pointerRadialWidth*X, pointerRadialWidth*Z },
			};
			auto i = workingVertices.begin();
			for (auto&p:pointers) {
				float prevSinTheta = 0.f, prevCosTheta = 1.f;

				for (unsigned c=0; c<pointerRadialVerts; ++c) {
					float theta = (c+1)/float(pointerRadialVerts)*2.0f*float(M_PI);
					float sinTheta, cosTheta;
					std::tie(sinTheta, cosTheta) = XlSinCos(theta);

					i->_position = p._tangent * prevCosTheta + p._bitangent * prevSinTheta + p._start;
					i->_color = p._color;
					++i;

					i->_position = p._tangent * prevCosTheta + p._bitangent * prevSinTheta + p._end;
					i->_color = p._color;
					++i;

					i->_position = p._tangent * cosTheta + p._bitangent * sinTheta + p._start;
					i->_color = p._color;
					++i;

					i->_position = p._tangent * cosTheta + p._bitangent * sinTheta + p._start;
					i->_color = p._color;
					++i;

					i->_position = p._tangent * prevCosTheta + p._bitangent * prevSinTheta + p._end;
					i->_color = p._color;
					++i;

					i->_position = p._tangent * cosTheta + p._bitangent * sinTheta + p._end;
					i->_color = p._color;
					++i;

					prevSinTheta = sinTheta; prevCosTheta = cosTheta;
				}
			}
			assert((i-workingVertices.begin()) == pointerRadialVerts*6*2);
		}
	}

	void FillScreenWithMsg(
		RenderCore::Techniques::ParsingContext& parsingContext,
		OverlayApparatus& immediateDrawingApparatus,
		StringSection<> msg)
	{
		auto overlayContext = MakeImmediateOverlayContext(parsingContext.GetThreadContext(), immediateDrawingApparatus);
		Int2 viewportDims{ parsingContext.GetViewport()._width, parsingContext.GetViewport()._height };

		auto font = RenderOverlays::MakeFont("DosisBook", 26)->TryActualize();
		if (font) {
			overlayContext->DrawText(
				std::make_tuple(Float3{0.f, 0.f, 0.f}, Float3{viewportDims[0], viewportDims[1], 0.f}),
				**font, 0, 0xffffffff, RenderOverlays::TextAlignment::Center, msg);
		}

		auto rpi = RenderCore::Techniques::RenderPassToPresentationTargetWithOptionalInitialize(parsingContext);
		parsingContext.RequireCommandList(overlayContext->GetRequiredBufferUploadsCommandList());
		ExecuteDraws(parsingContext, rpi, immediateDrawingApparatus);
	}

	void DrawBottomOfScreenErrorMsg(
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
		RenderOverlays::FontRenderingManager& fontRenderingManager,
		ShapesRenderingDelegate& shapesRenderingDelegate,
		StringSection<> msg)
	{
		auto font = RenderOverlays::MakeFont("Petra", 18)->TryActualize();
		if (!font)
			return;

		Int2 viewportDims { parsingContext.GetViewport()._width, parsingContext.GetViewport()._height };
		const unsigned horzPadding = 8;
		const unsigned vertPadding = 8;
		const unsigned horzRectArea = 16;
		const unsigned paddingBetweenLines = 6;

		if (viewportDims[0] < (2 * horzPadding - horzRectArea + 32))
			return;		// no horz space

		auto split = StringSplitByWidth<char>(**font, msg, float(viewportDims[0] - 2 * horzPadding - horzRectArea), " \t", "");
		if (split._sections.empty()) return;

		auto lineHeight = (unsigned)(*font)->GetFontProperties()._lineHeight;

		auto overlayContext = MakeImmediateOverlayContext(parsingContext.GetThreadContext(), immediateDrawables, &fontRenderingManager);

		parsingContext._stringHelpers->_bottomOfScreenErrorMsgTracker += vertPadding;
		auto bottom = viewportDims[1] - parsingContext._stringHelpers->_bottomOfScreenErrorMsgTracker;
		auto top = viewportDims[1] - (parsingContext._stringHelpers->_bottomOfScreenErrorMsgTracker + split._sections.size() * lineHeight + (split._sections.size()-1) * paddingBetweenLines);

		// draw a background quad
		{
			const unsigned bleedOut = 8;
			Float3 bkgrndQuad[] {
				Float3{0, top - bleedOut, 0.f},
				Float3{0, bottom + bleedOut, 0.f},
				Float3{viewportDims[0], top - bleedOut, 0.f},
				Float3{viewportDims[0], top - bleedOut, 0.f},
				Float3{0, bottom + bleedOut, 0.f},
				Float3{viewportDims[0], bottom + bleedOut, 0.f}
			};
			overlayContext->DrawTriangles(ProjectionMode::P2D, bkgrndQuad, dimof(bkgrndQuad), ColorB{0x0f, 0x0f, 0x0f});
		}

		parsingContext._stringHelpers->_bottomOfScreenErrorMsgTracker += unsigned(split._sections.size()) * lineHeight + unsigned(split._sections.size()-1) * paddingBetweenLines;
		auto yIterator = viewportDims[1] - parsingContext._stringHelpers->_bottomOfScreenErrorMsgTracker;

		for (auto s:split._sections) {
			overlayContext->DrawText(
				{ 	Float3{horzPadding + horzRectArea, yIterator, 0.f}, 
					Float3{viewportDims[0] - horzPadding, yIterator + lineHeight, 0.f} },
				**font, 0, 0xffffffff, RenderOverlays::TextAlignment::Left, s);
			yIterator += lineHeight + paddingBetweenLines;
		}

		// draw a little quad to the left, just for completeness
		{
			Float3 littleQuad[] {
				Float3{horzPadding, top, 0.f},
				Float3{horzPadding, bottom, 0.f},
				Float3{horzRectArea, top, 0.f},
				Float3{horzRectArea, top, 0.f},
				Float3{horzPadding, bottom, 0.f},
				Float3{horzRectArea, bottom, 0.f}
			};
			overlayContext->DrawTriangles(ProjectionMode::P2D, littleQuad, dimof(littleQuad), ColorB{0xaf, 0x4f, 0x3f});
		}

		auto rpi = RenderCore::Techniques::RenderPassToPresentationTargetWithOptionalInitialize(parsingContext);
		parsingContext.RequireCommandList(overlayContext->GetRequiredBufferUploadsCommandList());
		ExecuteDraws(parsingContext, rpi, immediateDrawables, shapesRenderingDelegate);
	}

	void DrawBottomOfScreenErrorMsg(
		RenderCore::Techniques::ParsingContext& parsingContext,
		OverlayApparatus& immediateDrawingApparatus,
		StringSection<> msg)
	{
		DrawBottomOfScreenErrorMsg(parsingContext, *immediateDrawingApparatus._immediateDrawables, *immediateDrawingApparatus._fontRenderingManager, *immediateDrawingApparatus._shapeRenderingDelegate, msg);
	}

	static void DrawDiamond(RenderOverlays::IOverlayContext& context, const RenderOverlays::Rect& rect, RenderOverlays::ColorB colour)
	{
		if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1]) {
			return;
		}

		using namespace RenderOverlays;
		using namespace RenderOverlays::DebuggingDisplay;
		context.DrawTriangle(
			ProjectionMode::P2D, 
			AsPixelCoords(Coord2(rect._bottomRight[0],								0.5f * (rect._topLeft[1] + rect._bottomRight[1]))), colour,
			AsPixelCoords(Coord2(0.5f * (rect._topLeft[0] + rect._bottomRight[0]),	rect._topLeft[1])), colour,
			AsPixelCoords(Coord2(rect._topLeft[0],									0.5f * (rect._topLeft[1] + rect._bottomRight[1]))), colour);

		context.DrawTriangle(
			ProjectionMode::P2D, 
			AsPixelCoords(Coord2(rect._topLeft[0],									0.5f * (rect._topLeft[1] + rect._bottomRight[1]))), colour,
			AsPixelCoords(Coord2(0.5f * (rect._topLeft[0] + rect._bottomRight[0]),	rect._bottomRight[1])), colour,
			AsPixelCoords(Coord2(rect._bottomRight[0],								0.5f * (rect._topLeft[1] + rect._bottomRight[1]))), colour);
	}

	void RenderLoadingIndicator(
		RenderOverlays::IOverlayContext& context,
		const RenderOverlays::Rect& viewport,
		unsigned animationCounter)
	{
		using namespace RenderOverlays::DebuggingDisplay;

		const unsigned indicatorWidth = 80;
		const unsigned indicatorHeight = 120;
		RenderOverlays::Rect outerRect;
		outerRect._topLeft[0] = std::max(viewport._topLeft[0]+12u, viewport._bottomRight[0]-indicatorWidth-12u);
		outerRect._topLeft[1] = std::max(viewport._topLeft[1]+12u, viewport._bottomRight[1]-indicatorHeight-12u);
		outerRect._bottomRight[0] = viewport._bottomRight[0]-12u;
		outerRect._bottomRight[1] = viewport._bottomRight[1]-12u;

		Float2 center {
			(outerRect._bottomRight[0] + outerRect._topLeft[0]) / 2.0f,
			(outerRect._bottomRight[1] + outerRect._topLeft[1]) / 2.0f };

		const unsigned cycleCount = 1080;
		// there are always 3 diamonds, distributed evenly throughout the animation....
		unsigned oldestIdx = (unsigned)std::ceil(animationCounter / float(cycleCount/3));
		int oldestStartPoint = -int(animationCounter % (cycleCount/3));
		float phase = -oldestStartPoint / float(cycleCount/3);
		for (unsigned c=0; c<3; ++c) {
			unsigned idx = oldestIdx+c;

			float a = (phase + (2-c)) / 3.0f;
			float a2 = std::fmodf(idx / 10.f, 1.0f);
			a2 = 0.5f + 0.5f * a2;

			RenderOverlays::Rect r;
			r._topLeft[0] = unsigned(center[0] - a * 0.5f * (outerRect._bottomRight[0] - outerRect._topLeft[0]));
			r._topLeft[1] = unsigned(center[1] - a * 0.5f * (outerRect._bottomRight[1] - outerRect._topLeft[1]));
			r._bottomRight[0] = unsigned(center[0] + a * 0.5f * (outerRect._bottomRight[0] - outerRect._topLeft[0]));
			r._bottomRight[1] = unsigned(center[1] + a * 0.5f * (outerRect._bottomRight[1] - outerRect._topLeft[1]));

			using namespace RenderOverlays::DebuggingDisplay;
			float fadeOff = std::min((1.0f - a) * 10.f, 1.0f);
			DrawDiamond(context, r, RenderOverlays::ColorB { uint8_t(0xff * fadeOff * a2), uint8_t(0xff * fadeOff * a2), uint8_t(0xff * fadeOff * a2), 0xff });
		}
	}
}

