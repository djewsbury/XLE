// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Math/Vector.h"
#include "../Utility/StringFormat.h"

namespace RenderCore { namespace Techniques { class ParsingContext; class IImmediateDrawables; }}
namespace RenderCore { class IThreadContext; }

namespace RenderOverlays
{
	struct Rect;
	class IOverlayContext;
	class OverlayApparatus;
	class FontRenderingManager;
	class ShapesRenderingDelegate;

	void DrawBasisAxes(
		RenderCore::Techniques::IImmediateDrawables&, RenderCore::Techniques::ParsingContext& parserContext, 
		Float2 ssMins = Float2(8,8), Float2 ssMaxs = Float2(64+8,64+8));

	void DrawGrid(
		RenderCore::Techniques::IImmediateDrawables&, RenderCore::Techniques::ParsingContext& parserContext, 
		float gridScale = 1.0f, Float3 origin = Float3(0,0,0));

	void FillScreenWithMsg(
		RenderCore::Techniques::ParsingContext& parsingContext,
		OverlayApparatus& immediateDrawingApparatus,
		StringSection<> msg);

	void DrawBottomOfScreenErrorMsg(
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
		FontRenderingManager& fontRenderingManager,
		ShapesRenderingDelegate& shapesRenderingDelegate,
		StringSection<> msg);

	void DrawBottomOfScreenErrorMsg(
		RenderCore::Techniques::ParsingContext& parsingContext,
		OverlayApparatus& immediateDrawingApparatus,
		StringSection<> msg);

	void RenderLoadingIndicator(
		RenderOverlays::IOverlayContext& context,
		const RenderOverlays::Rect& viewport,
		unsigned animationCounter);
}

