// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SimpleVisualization.h"
#include "OverlayContext.h"
#include "Font.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/Techniques/RenderPassUtils.h"
#include "../RenderCore/Techniques/Apparatuses.h"
#include "../Math/ProjectionMath.h"
#include "../Math/Transformations.h"
#include "../Assets/AssetFuture.h"

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
		IOverlayContext& overlayContext, RenderCore::Techniques::ParsingContext& parserContext, 
		Float2 ssMins, Float2 ssMaxs)
	{
			//
			//      Draw world space X, Y, Z axes (to make it easier to see what's going on)
			//


		const float     size = 1.f;
		const InternalVertex    vertices[] = 
		{
			{ Float3(0.f,     0.f,     0.f),  0xff0000ff },
			{ Float3(size,    0.f,     0.f),  0xff0000ff },
			{ Float3(0.f,     0.f,     0.f),  0xff00ff00 },
			{ Float3(0.f,    size,     0.f),  0xff00ff00 },
			{ Float3(0.f,     0.f,     0.f),  0xffff0000 },
			{ Float3(0.f,     0.f,    size),  0xffff0000 }
		};

		using namespace RenderCore;
		Techniques::ImmediateDrawableMaterial material;
		auto workingVertices = overlayContext.GetImmediateDrawables().QueueDraw(
			dimof(vertices), MakeIteratorRange(s_vertexInputLayout), 
			material, Topology::LineList).Cast<InternalVertex*>();

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
		for (const auto&v:vertices) {
			*i = v;
			Float4 t = transform * Float4{i->_position, 1.f};
			t /= t[3];		// fortunately homogeneous divide magic works here, so we can use 3d vectors as input
			i->_position = Truncate(t);
			++i;
		}
	}

	void DrawGrid(
		IOverlayContext& overlayContext, RenderCore::Techniques::ParsingContext& parserContext, 
		float gridScale, Float3 origin)
	{
		// draw a grid to give some sense of scale
		// todo -- we could do this in a better way, and only draw lines that actually intersect the camera frustum
		const int radiusInLines = 50;
		auto lineCount = (radiusInLines*2+1) + (radiusInLines*2+1);

		using namespace RenderCore;
		Techniques::ImmediateDrawableMaterial material;
		auto workingVertices = overlayContext.GetImmediateDrawables().QueueDraw(
			lineCount*2, MakeIteratorRange(s_vertexInputLayout), 
			material, Topology::LineList).Cast<InternalVertex*>();

		auto i = workingVertices.begin();
		for (int x=-radiusInLines; x<=radiusInLines; ++x) {
			i->_position = Float3{x*gridScale, -radiusInLines*gridScale, 0.f};
			if (x == 0) i->_color = 0xff00ff00;
			else i->_color = ((x%10) == 0) ? 0xff6f6f6f : 0xff3f3f3f;
			++i;

			i->_position = Float3{x*gridScale, radiusInLines*gridScale, 0.f};
			if (x == 0) i->_color = 0xff00ff00;
			else i->_color = ((x%10) == 0) ? 0xff6f6f6f : 0xff3f3f3f;
			++i;
		}

		for (int y=-radiusInLines; y<=radiusInLines; ++y) {
			i->_position = Float3{-radiusInLines*gridScale, y*gridScale, 0.f};
			if (y == 0) i->_color = 0xff0000ff;
			else i->_color = ((y%10) == 0) ? 0xff6f6f6f : 0xff3f3f3f;
			++i;

			i->_position = Float3{radiusInLines*gridScale, y*gridScale, 0.f};
			if (y == 0) i->_color = 0xff0000ff;
			else i->_color = ((y%10) == 0) ? 0xff6f6f6f : 0xff3f3f3f;
			++i;
		}

		assert(i == workingVertices.end());
	}

	void FillScreenWithMsg(
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::Techniques::ImmediateDrawingApparatus& immediateDrawingApparatus,
		StringSection<> msg)
	{
		auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(parsingContext.GetThreadContext(), immediateDrawingApparatus);
		Int2 viewportDims{ parsingContext.GetViewport()._width, parsingContext.GetViewport()._height };

		auto font = RenderOverlays::MakeFont("DosisBook", 26)->TryActualize();
		if (font) {
			overlayContext->DrawText(
				std::make_tuple(Float3{0.f, 0.f, 0.f}, Float3{viewportDims[0], viewportDims[1], 0.f}),
				**font, 0, 0xffffffff, RenderOverlays::TextAlignment::Center, msg);
		}

		auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parsingContext, RenderCore::LoadStore::Clear);
		parsingContext.RequireCommandList(overlayContext->GetRequiredBufferUploadsCommandList());
		immediateDrawingApparatus._immediateDrawables->ExecuteDraws(parsingContext, rpi);
	}
}

