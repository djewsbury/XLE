// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InteractiveTestHelper.h"
#include "HexGridUtils.h"
#include "../RenderCore/Metal/MetalTestHelper.h"
#include "../../PlatformRig/IOverlay.h"
#include "../../PlatformRig/InputContext.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/RenderPassUtils.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/DrawableSubmitter.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/Drawables.h"		// unfortunately required for PreparedResourcesVisibility
#include "../../RenderOverlays/OverlayApparatus.h"
#include "../../RenderOverlays/ShapesRendering.h"
#include "../../RenderOverlays/DrawText.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Formatters/TextOutputFormatter.h"
#include "../../Math/Transformations.h"
#include "../../Math/Geometry.h"
#include "../../Math/MathSerialization.h"
#include "catch2/catch_test_macros.hpp"

using namespace OSServices::Literals;
using namespace Utility::Literals;

namespace UnitTests
{
	struct SelectionState
	{
		unsigned _draggingVertex = ~0u;
		unsigned _mouseOverVertex = ~0u;
	};

	T1(Primitive) class Polygon2D
	{
	public:
		std::vector<Vector2T<Primitive>> _vertices;

		static constexpr float vertexRadius = 7;

		void Draw(RenderOverlays::IOverlayContext& context, const Float4x4& localToWorld, const SelectionState& selectionState) const
		{
			ScaleRotationTranslationM srt{localToWorld};
			auto scale = (srt._scale[0]+srt._scale[1]+srt._scale[2])/3;

			using namespace RenderOverlays;
			ColorB edgeColor { 55, 85, 149 };				// rgb(55, 85, 149)

			for (unsigned vIdx=0; vIdx<_vertices.size(); ++vIdx) {
				auto v2 = (vIdx+1)%unsigned(_vertices.size());
				auto p0 = TransformPoint(localToWorld, Expand(_vertices[vIdx], Primitive(0)));
				auto p1 = TransformPoint(localToWorld, Expand(_vertices[v2], Primitive(0)));
				Float2 linePts[] { Truncate(p0), Truncate(p1) };
				DashLine(context, linePts, edgeColor, 2.f);
			}

			ColorB vertexColor { 74, 169, 188 };				// rgb(74, 169, 188)
			ColorB vertexMouseOver { 230, 230, 230 };			// rgb(230, 230, 230)
			ColorB vertexDragging { 184, 159, 51 };				// rgb(184, 159, 51)

			for (unsigned vIdx=0; vIdx<_vertices.size(); ++vIdx) {
				auto color = vertexColor;
				if (vIdx == selectionState._mouseOverVertex) color = vertexMouseOver;
				if (vIdx == selectionState._draggingVertex) color = vertexDragging;
				
				auto pos = TransformPoint(localToWorld, Expand(_vertices[vIdx], Primitive(0)));
				OutlineEllipse(context, Rect{Coord(pos[0]-vertexRadius), Coord(pos[1]-vertexRadius), Coord(pos[0]+vertexRadius), Coord(pos[1]+vertexRadius)}, color, 2.f);
			}
		}

		std::pair<unsigned, unsigned> RayTest(const RenderOverlays::Coord2& pt, const Float4x4& localToWorld)
		{
			using namespace RenderOverlays;
			for (unsigned vIdx=0; vIdx<_vertices.size(); ++vIdx) {
				auto pos = TransformPoint(localToWorld, Expand(_vertices[vIdx], Primitive(0)));
				Rect r{Coord(pos[0]-vertexRadius), Coord(pos[1]-vertexRadius), Coord(pos[0]+vertexRadius), Coord(pos[1]+vertexRadius)};
				if (pt[0] >= r._topLeft[0] && pt[0] < r._bottomRight[0] && pt[1] >= r._topLeft[1] && pt[1] < r._bottomRight[1])
					return {vIdx, vIdx};
			}

			// is this point on an edge of the polygon?
			for (unsigned vIdx=0; vIdx<_vertices.size(); ++vIdx) {
				auto v2 = (vIdx+1)%unsigned(_vertices.size());
				auto p0 = Truncate(TransformPoint(localToWorld, Expand(_vertices[vIdx], Primitive(0))));
				auto p1 = Truncate(TransformPoint(localToWorld, Expand(_vertices[v2], Primitive(0))));
				auto e = p1 - p0;
				auto eMag = Magnitude(e);
				float a = Dot(pt-p0, e) / (eMag*eMag);
				if (a < 0 || a > 1) continue;
				auto linePt = LinearInterpolate(p0, p1, a);
				if (Equivalent(linePt, Vector2T<Primitive>(pt), Primitive(vertexRadius))) return { vIdx, v2 };
			}

			return {~0u, ~0u};
		}

		friend void SerializationOperator(::Formatters::TextOutputFormatter& fmttr, const Polygon2D<Primitive>& poly)
		{
			auto e = fmttr.BeginElement();
			for (auto v:poly._vertices) fmttr.FormatSequencedValue(v);
			fmttr.EndElement(e);
		}

		friend void DeserializationOperator(::Formatters::TextInputFormatter<>& fmttr, Polygon2D<Primitive>& poly)
		{
			Formatters::RequireBeginElement(fmttr);
			while (!fmttr.TryEndElement())
				poly._vertices.emplace_back(Formatters::RequireCastValue<Vector2T<Primitive>>(fmttr));
		}
	};

	static RenderCore::Techniques::CameraDesc StartingCamera(float scale = 1.f, Float2 cameraOffset = Zero<Float2>())
	{
		RenderCore::Techniques::CameraDesc visCamera;
		visCamera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, 0.0f, -1.0f}), Normalize(Float3{0.0f, 1.0f, 0.0f}), Float3{cameraOffset[0], cameraOffset[1], 200.0f});
		visCamera._projection = RenderCore::Techniques::CameraDesc::Projection::Orthogonal;
		visCamera._nearClip = 0.f;
		visCamera._farClip = 400.f;
		visCamera._left = -50.f * scale;
		visCamera._right = 50.f * scale;
		visCamera._top = -50.f * scale;
		visCamera._bottom = 50.f * scale;
		return visCamera;
	}

	static Int2 AsInt2(OSServices::Coord2 xy) { return { xy._x, xy._y }; }

	TEST_CASE( "PolyGeometryPlayground", "[math]" )
	{
		using namespace RenderCore;
		class PolyGeometryPlayground : public IInteractiveTestOverlay
		{
		public:
			void DrawAngularCentroid(RenderOverlays::IOverlayContext& context, const Float4x4& localToWorld)
			{
				VLA(unsigned, indices, _poly._vertices.size());
				VLA_UNSAFE_FORCE(Float3, pts, _poly._vertices.size());
				for (unsigned c=0; c<_poly._vertices.size(); ++c) { indices[c] = c; pts[c] = Expand(Float2(_poly._vertices[c]), 0.f); }
				
				auto centroid = FindAngularCentroidXY<float>(MakeIteratorRange(pts, pts+_poly._vertices.size()), MakeIteratorRange(indices, indices+_poly._vertices.size()));
				if (centroid) {
					auto pos = TransformPoint(localToWorld, *centroid);
					using namespace RenderOverlays;
					float vertexRadius = 12;
					ColorB color { 167, 80, 30 }; // rgb(167, 80, 30)
					OutlineEllipse(context, Rect{Coord(pos[0]-vertexRadius), Coord(pos[1]-vertexRadius), Coord(pos[0]+vertexRadius), Coord(pos[1]+vertexRadius)}, color, 2.f);
				}
			}

			////////////////////////////////////////////////////////////////////////////////////////////////////

			float ZoomFactorToScale() const
			{
				float scale = 1.f * (_zoomFactor + 1.f);
				return scale;
			}

			Float4x4 GetLocalToWorld(Float2 viewportDims)
			{
				auto scale = ZoomFactorToScale();
				return Float4x4{
					scale, 0.f, 0.f, 0.5f * viewportDims[0] + scale * _viewOffset[0],
					0.f, scale, 0.f, 0.5f * viewportDims[1] + scale * _viewOffset[1],
					0.f, 0.f, 1.f, 0.f,
					0.f, 0.f, 0.f, 1.f
				};
			}

			virtual void Render(
				RenderCore::Techniques::ParsingContext& parserContext,
				IInteractiveTestHelper& testHelper) override
			{
				auto localToWorld = GetLocalToWorld({ parserContext.GetViewport()._width, parserContext.GetViewport()._height });

				{
					auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(parserContext.GetThreadContext(), *testHelper.GetOverlayApparatus());
					_poly.Draw(*overlayContext, localToWorld, _selectionState);
					DrawAngularCentroid(*overlayContext, localToWorld);
				}

				auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext, LoadStore::Clear);
				RenderOverlays::ExecuteDraws(
					parserContext, rpi,
					*testHelper.GetOverlayApparatus());
			}

			virtual bool OnInputEvent(
				const PlatformRig::InputContext& context,
				const OSServices::InputSnapshot& evnt,
				IInteractiveTestHelper& testHelper) override
			{
				if (evnt.IsHeld_RButton()) {
					const float mouseSensitivity = 2.0f / ZoomFactorToScale();
					_viewOffset += Float2{evnt._mouseDelta[0] * mouseSensitivity, evnt._mouseDelta[1] * mouseSensitivity};
				}
				if (evnt._wheelDelta) {
					_zoomFactor += evnt._wheelDelta / 180.f;
					_zoomFactor = std::max(1e-4f, _zoomFactor);
				}

				auto localToWorld = GetLocalToWorld({ context._view._viewMaxs[0]-context._view._viewMins[0], context._view._viewMaxs[1]-context._view._viewMins[1] });

				if (evnt.IsRelease_LButton()) _selectionState._draggingVertex = ~0u;

				if (evnt.IsPress_LButton()) {

					_selectionState._draggingVertex = ~0u;
					auto rayTest = _poly.RayTest(AsInt2(evnt._mousePosition), localToWorld);
					if (rayTest.first != ~0u) {

						if (!evnt.IsHeld("shift"_key)) {
							if (rayTest.first == rayTest.second) {
								_selectionState._mouseOverVertex = _selectionState._draggingVertex = rayTest.first;
							} else {
								auto i = rayTest.second;
								auto newPt = TransformPoint(Inverse(localToWorld), Float3{evnt._mousePosition._x, evnt._mousePosition._y, 0});
								_poly._vertices.insert(_poly._vertices.begin()+i, Truncate(newPt));
								_selectionState._mouseOverVertex = _selectionState._draggingVertex = i;
							}
						} else if (rayTest.first == rayTest.second) {
							assert(rayTest.first < _poly._vertices.size());
							if (_poly._vertices.size() > 3)
								_poly._vertices.erase(_poly._vertices.begin()+rayTest.first);
						}

					}

				} else if (evnt.IsHeld_LButton() && (evnt._mouseDelta[0] || evnt._mouseDelta[1]) && _selectionState._draggingVertex < _poly._vertices.size()) {

					auto newPt = TransformPoint(Inverse(localToWorld), Float3{evnt._mousePosition._x, evnt._mousePosition._y, 0});
					_poly._vertices[_selectionState._draggingVertex] = Truncate(newPt);

				} else if (evnt._mouseDelta[0] || evnt._mouseDelta[1]) {

					auto rayTest = _poly.RayTest(AsInt2(evnt._mousePosition), localToWorld);
					if (rayTest.first == rayTest.second) _selectionState._mouseOverVertex = rayTest.first;
					else _selectionState._mouseOverVertex = ~0u;

				}

				return false;
			}

			void Load()
			{
				_poly = {};
				auto fn = "rawos/poly-geo-test.svg";
				size_t fileSize;
				::Assets::FileSnapshot fileState;
				if (auto f = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(fn, &fileSize, &fileState)) {
					Formatters::TextInputFormatter<> fmttr { MakeStringSection(f.get(), PtrAdd(f.get(), fileSize)) };
					auto depVal = ::Assets::GetDepValSys().Make(::Assets::DependentFileState{fn, fileState});
					fmttr >> _poly;
				} else {
					_poly._vertices.emplace_back(Float2{ -50,  -50});
					_poly._vertices.emplace_back(Float2{  50,  -50});
					_poly._vertices.emplace_back(Float2{  50,   50});
					_poly._vertices.emplace_back(Float2{ -50,   50});
				}
			}

			Polygon2D<float> _poly;
			Float2 _viewOffset { 0.f, 0.f };
			float _zoomFactor { 1.0f };
			SelectionState _selectionState;
			
			PolyGeometryPlayground()
			{
				Load();
			}
		};

		auto testHelper = CreateInteractiveTestHelper(IInteractiveTestHelper::EnabledComponents::RenderCoreTechniques);

		{
			auto tester = std::make_shared<PolyGeometryPlayground>();
			testHelper->Run(StartingCamera(0.5f), tester);
		}
	}

}


