// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PlacementsDisplay.h"
#include "../../SceneEngine/PlacementsManager.h"
#include "../../SceneEngine/RayVsModel.h"
#include "../../SceneEngine/IntersectionTest.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/LayoutEngine.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../RenderOverlays/Font.h"
#include "../../Tools/ToolsRig/VisualisationUtils.h"
#include "../../Assets/Marker.h"
#include "../../ConsoleRig/Console.h"
#include "../../Utility/StringFormat.h"

using namespace Utility::Literals;

namespace PlatformRig { namespace Overlays
{
	using namespace RenderOverlays;
	using namespace RenderOverlays::DebuggingDisplay;

	template<typename T>
		T TryAnyCast(std::any&& any, T defaultValue)
	{
		if (any.has_value() && any.type() == typeid(T))
			return std::any_cast<T>(std::move(any));
		return defaultValue;
	}

	template<typename T>
		std::optional<T> TryAnyCast(std::any&& any)
	{
		if (any.has_value() && any.type() == typeid(T))
			return std::any_cast<T>(std::move(any));
		return {};
	}

	class ToolTipHover
	{
	public:
		void Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState, const Float3x3& transform);
		ProcessInputResult ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input);

		Coord2 GetDimensions() const;

		ToolTipHover(LayedOutWidgets&& layedOutWidgets);
		ToolTipHover() = default;
		ToolTipHover(ToolTipHover&&) = default;
		ToolTipHover& operator=(ToolTipHover&&) = default;
		~ToolTipHover();
	private:
		LayedOutWidgets _layedOutWidgets;
	};

	void ToolTipHover::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState, const Float3x3& transform)
	{
		CommonWidgets::Draw draw{context, interactables, interfaceState};
		_layedOutWidgets.Draw(draw, transform);
	}

	ProcessInputResult ToolTipHover::ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& snapshot)
	{
		CommonWidgets::HoveringLayer hoveringLayer;
		CommonWidgets::Input input { interfaceState, snapshot, hoveringLayer };
		auto result = _layedOutWidgets.ProcessInput(input);
		if (result == LayedOutWidgets::ProcessInputResult::Consumed)
			return ProcessInputResult::Consumed;
		return ProcessInputResult::Passthrough;
	}

	Coord2 ToolTipHover::GetDimensions() const
	{
		return _layedOutWidgets._dimensions;
	}

	ToolTipHover::ToolTipHover(LayedOutWidgets&& layedOutWidgets)
	: _layedOutWidgets(std::move(layedOutWidgets)) {}
	ToolTipHover::~ToolTipHover() {}

	static ImbuedNode* Heading(LayoutEngine& layoutEngine, std::string&& label)
	{
		auto labelNode = layoutEngine.NewImbuedNode(0);
		layoutEngine.InsertChildToStackTop(*labelNode);

		auto* defaultFonts = CommonWidgets::Draw::TryGetDefaultFontsBox();
		assert(defaultFonts);
		YGNodeStyleSetWidth(*labelNode, StringWidth(*defaultFonts->_headingFont, MakeStringSection(label)));
		YGNodeStyleSetHeight(*labelNode, CommonWidgets::Draw::baseLineHeight);
		YGNodeStyleSetFlexGrow(*labelNode, 0.f);		// don't grow, because our parent is column direction, and we want to have a fixed height
		YGNodeStyleSetMargin(*labelNode, YGEdgeAll, 2);
		YGNodeStyleSetAlignSelf(*labelNode, YGAlignCenter);
		
		labelNode->_nodeAttachments._drawDelegate = [label=std::move(label)](CommonWidgets::Draw& draw, Rect frame, Rect content) {
			DrawText().Font(*draw.GetDefaultFontsBox()._headingFont).Draw(draw.GetContext(), content, label);
		};
		return labelNode;
	}

	static YGNodeRef KeyValueGroup(LayoutEngine& layoutEngine)
	{
		auto baseNode = layoutEngine.NewNode();
		layoutEngine.InsertChildToStackTop(baseNode);
		layoutEngine.PushNode(baseNode);
		
		YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
		YGNodeStyleSetJustifyContent(baseNode, YGJustifySpaceBetween);
		YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
		
		YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
		YGNodeStyleSetFlexGrow(baseNode, 0.f);		// don't grow, because our parent is column direction, and we want to have a fixed height
		return baseNode;
	}

	static YGNodeRef VerticalGroup(LayoutEngine& layoutEngine)
	{
		auto baseNode = layoutEngine.NewNode();
		layoutEngine.InsertChildToStackTop(baseNode);
		layoutEngine.PushNode(baseNode);
		
		YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionColumn);
		YGNodeStyleSetJustifyContent(baseNode, YGJustifyFlexStart);
		YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
		
		YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
		YGNodeStyleSetFlexGrow(baseNode, 0.f);
		return baseNode;
	}

	static ImbuedNode* KeyName(LayoutEngine& layoutEngine, std::string&& label)
	{
		auto labelNode = layoutEngine.NewImbuedNode(0);
		layoutEngine.InsertChildToStackTop(*labelNode);

		auto* defaultFonts = CommonWidgets::Draw::TryGetDefaultFontsBox();
		assert(defaultFonts);
		YGNodeStyleSetWidth(*labelNode, StringWidth(*defaultFonts->_buttonFont, MakeStringSection(label)));
		YGNodeStyleSetHeight(*labelNode, defaultFonts->_buttonFont->GetFontProperties()._lineHeight);
		YGNodeStyleSetMargin(*labelNode, YGEdgeRight, 8);
		YGNodeStyleSetFlexGrow(*labelNode, 0.f);
		YGNodeStyleSetFlexShrink(*labelNode, 0.f);

		labelNode->_nodeAttachments._drawDelegate = [label=std::move(label)](CommonWidgets::Draw& draw, Rect frame, Rect content) {
			DrawText().Font(*draw.GetDefaultFontsBox()._buttonFont).Draw(draw.GetContext(), content, label);
		};
		return labelNode;
	}

	static ImbuedNode* KeyValue(LayoutEngine& layoutEngine, std::string&& label)
	{
		auto labelNode = layoutEngine.NewImbuedNode(0);
		layoutEngine.InsertChildToStackTop(*labelNode);

		auto* defaultFonts = CommonWidgets::Draw::TryGetDefaultFontsBox();
		assert(defaultFonts);
		YGNodeStyleSetHeight(*labelNode, defaultFonts->_buttonFont->GetFontProperties()._lineHeight);
		float maxWidth = StringWidth(*defaultFonts->_buttonFont, MakeStringSection(label));
		YGNodeStyleSetWidth(*labelNode, maxWidth);

		// We can't grow, but we can shrink -- our "width" property is the length of the entire string, and if it's shrunk,
		// we'll adjust the string with a ellipsis
		YGNodeStyleSetFlexGrow(*labelNode, 0.f);
		YGNodeStyleSetFlexShrink(*labelNode, 1.f);

		struct AttachedData
		{
			std::string _originalLabel;
			unsigned _cachedWidth = ~0u;
			std::string _fitLabel;
		};
		auto attachedData = std::make_shared<AttachedData>();
		attachedData->_originalLabel = std::move(label);
		attachedData->_fitLabel = attachedData->_originalLabel;
		attachedData->_cachedWidth = (unsigned)maxWidth;

		labelNode->_nodeAttachments._drawDelegate = [attachedData](CommonWidgets::Draw& draw, Rect frame, Rect content) {
			// We don't get a notification after layout is finished -- so typically on the first render we may have to adjust
			// our string to fit
			auto* font = draw.GetDefaultFontsBox()._buttonFont.get();
			if (content.Width() != attachedData->_cachedWidth) {
				attachedData->_cachedWidth = content.Width();
				char buffer[MaxPath];
				auto fitWidth = StringEllipsisDoubleEnded(buffer, dimof(buffer), *font, MakeStringSection(attachedData->_originalLabel), MakeStringSection("/\\"), (float)content.Width());
				attachedData->_fitLabel = buffer;
			}
			DrawText().Font(*font).Alignment(TextAlignment::Right).Draw(draw.GetContext(), content, attachedData->_fitLabel);
		};

		#if 0
			labelNode->_measureDelegate = [attachedData](float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) {
				if (widthMode != YGMeasureMode::YGMeasureModeExactly) {
					auto* defaultFonts = CommonWidgets::Draw::TryGetDefaultFontsBox();
					assert(defaultFonts);
					char buffer[MaxPath];
					auto fitWidth = StringEllipsisDoubleEnded(buffer, dimof(buffer), *defaultFonts->_buttonFont, MakeStringSection(attachedData->_originalLabel), MakeStringSection("/\\"), width);
					attachedData->_fitLabel = buffer;
					return YGSize { fitWidth, defaultFonts->_buttonFont->GetFontProperties()._lineHeight };
				} else {
					return YGSize { width, height };
				}
			};
			labelNode->YGNode()->setContext(labelNode);
			labelNode->YGNode()->setMeasureFunc(
				[](YGNode* node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) -> YGSize {
					return ((ImbuedNode*)node->getContext())->_measureDelegate(width, widthMode, height, heightMode);
				});
		#endif

		return labelNode;
	}

	template<typename T, typename std::enable_if<std::is_integral_v<T>>::type* =nullptr>
		static ImbuedNode* KeyValue(LayoutEngine& layoutEngine, T value)
	{
		auto labelNode = layoutEngine.NewImbuedNode(0);
		layoutEngine.InsertChildToStackTop(*labelNode);

		auto str = std::to_string(value);

		auto* defaultFonts = CommonWidgets::Draw::TryGetDefaultFontsBox();
		assert(defaultFonts);
		YGNodeStyleSetWidth(*labelNode, StringWidth(*defaultFonts->_buttonFont, MakeStringSection(str)));
		YGNodeStyleSetHeight(*labelNode, defaultFonts->_buttonFont->GetFontProperties()._lineHeight);
		YGNodeStyleSetFlexGrow(*labelNode, 0.f);
		YGNodeStyleSetFlexShrink(*labelNode, 0.f);

		labelNode->_nodeAttachments._drawDelegate = [str=std::move(str)](CommonWidgets::Draw& draw, Rect frame, Rect content) {
			DrawText().Font(*draw.GetDefaultFontsBox()._buttonFont).Draw(draw.GetContext(), content, str);
		};
		return labelNode;
	}

	static ImbuedNode* KeyValueSimple(LayoutEngine& layoutEngine, std::string&& str)
	{
		auto labelNode = layoutEngine.NewImbuedNode(0);
		layoutEngine.InsertChildToStackTop(*labelNode);

		auto* defaultFonts = CommonWidgets::Draw::TryGetDefaultFontsBox();
		assert(defaultFonts);
		YGNodeStyleSetWidth(*labelNode, StringWidth(*defaultFonts->_buttonFont, MakeStringSection(str)));
		YGNodeStyleSetHeight(*labelNode, defaultFonts->_buttonFont->GetFontProperties()._lineHeight);
		YGNodeStyleSetFlexGrow(*labelNode, 0.f);
		YGNodeStyleSetFlexShrink(*labelNode, 0.f);

		labelNode->_nodeAttachments._drawDelegate = [str=std::move(str)](CommonWidgets::Draw& draw, Rect frame, Rect content) {
			DrawText().Font(*draw.GetDefaultFontsBox()._buttonFont).Draw(draw.GetContext(), content, str);
		};
		return labelNode;
	}

	static YGNodeRef ValueGroup(LayoutEngine& layoutEngine)
	{
		auto baseNode = layoutEngine.NewNode();
		layoutEngine.InsertChildToStackTop(baseNode);
		layoutEngine.PushNode(baseNode);
		
		YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
		YGNodeStyleSetJustifyContent(baseNode, YGJustifySpaceBetween);
		YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
		
		YGNodeStyleSetMargin(baseNode, YGEdgeLeft, 2);
		YGNodeStyleSetMargin(baseNode, YGEdgeRight, 2);
		return baseNode;
	}

	static ImbuedNode* EventButton(LayoutEngine& layoutEngine, std::string&& label, std::function<void()>&& event)
	{
		uint64_t interactable = layoutEngine.GuidStack().MakeGuid(label);
		auto buttonNode = KeyValue(layoutEngine, std::move(label));

		buttonNode->_nodeAttachments._guid = interactable;
		buttonNode->_nodeAttachments._ioDelegate = [event=std::move(event), interactable](CommonWidgets::Input& input, Rect, Rect) {
			if (input.GetEvent().IsPress_LButton()) {
				input.GetInterfaceState().BeginCapturing(input.GetInterfaceState().TopMostWidget());
			} else if (input.GetEvent().IsRelease_LButton()) {
				if (input.GetInterfaceState().GetCapture()._widget._id == interactable) {
					input.GetInterfaceState().EndCapturing();
					if (Contains(input.GetInterfaceState().TopMostWidget()._rect, input.GetInterfaceState().MousePosition()))
						event();
				}
			}
			return IODelegateResult::Consumed;
		};
		return buttonNode;
	}

	static void SetupToolTipHover(ToolTipHover& hover, SceneEngine::MetadataProvider& metadataQuery, SceneEngine::PlacementsEditor& placementsEditor)
	{
		LayoutEngine le;

		std::string selectedMaterialName, selectedModelName;
		selectedMaterialName = TryAnyCast(metadataQuery("MaterialScaffold"_h), selectedMaterialName);
		selectedModelName = TryAnyCast(metadataQuery("ModelScaffold"_h), selectedModelName);
		auto drawCallIndex = TryAnyCast<unsigned>(metadataQuery("DrawCallIndex"_h));
		auto drawCallCount = TryAnyCast<unsigned>(metadataQuery("DrawCallCount"_h));
		auto indexCount = TryAnyCast<unsigned>(metadataQuery("IndexCount"_h));
		auto materialName = TryAnyCast<std::string>(metadataQuery("ShortMaterialName"_h));
		auto cellPlacementCount = TryAnyCast<unsigned>(metadataQuery("Cell_PlacementCount"_h));
		auto cellSimilarPlacementCount = TryAnyCast<unsigned>(metadataQuery("Cell_SimilarPlacementCount"_h));
		auto placementGuid = TryAnyCast<SceneEngine::PlacementGUID>(metadataQuery("PlacementGUID"_h));

		auto rootNode = le.NewNode();
		le.PushRoot(rootNode, {32, 32});
		YGNodeStyleSetMaxWidth(rootNode, 768);
		YGNodeStyleSetMaxHeight(rootNode, 1440);		// we need to set some maximum height to allow the dimensions returned in the layout to adapt to the children

		YGNodeStyleSetFlexDirection(rootNode, YGFlexDirectionColumn);
		YGNodeStyleSetJustifyContent(rootNode, YGJustifyFlexStart);
		YGNodeStyleSetAlignItems(rootNode, YGAlignStretch);		// stretch out each item to fill the entire row

		Heading(le, "Placement");

		if (!selectedMaterialName.empty() || !selectedModelName.empty()) {
			KeyValueGroup(le); KeyName(le, "Model Scaffold"); KeyValue(le, std::move(selectedModelName)); le.PopNode();
			KeyValueGroup(le); KeyName(le, "Material Scaffold"); KeyValue(le, std::move(selectedMaterialName)); le.PopNode();
		}
		if (drawCallIndex && drawCallCount && indexCount && materialName) {
			KeyValueGroup(le); KeyName(le, "Draw Call Index"); 
			ValueGroup(le);
			KeyValue(le, *drawCallIndex);
			KeyValueSimple(le, "/");
			KeyValue(le, *drawCallCount);
			le.PopNode();
			le.PopNode();
			KeyValueGroup(le); KeyName(le, "Index Count"); KeyValue(le, *indexCount); le.PopNode();
			KeyValueGroup(le); KeyName(le, "Material"); KeyValue(le, std::move(*materialName)); le.PopNode();
		}
		if (cellPlacementCount && cellSimilarPlacementCount) {
			KeyValueGroup(le);
			KeyName(le, "Cell Placements (similar/total)");
			ValueGroup(le);
			KeyValue(le, *cellSimilarPlacementCount);
			KeyValueSimple(le, "/");
			KeyValue(le, *cellPlacementCount);
			le.PopNode();
			le.PopNode();
		}
		if (placementGuid) {
			VerticalGroup(le);
			KeyValueGroup(le);
			KeyName(le, "Cell");
			auto cellName = placementsEditor.GetCellSet().DehashCellName(placementGuid->first).AsString();
			if (!cellName.empty()) KeyValue(le, std::string{cellName});
			else KeyValue(le, placementGuid->first);
			le.PopNode();
			EventButton(le, "Show Quad Tree", [cell=placementGuid->first, cellName]() {
				// switch to another debugging display that will display the quad tree we're interested in
				ConsoleRig::Console::GetInstance().Execute("scene:ShowQuadTree(\"" + cellName + "\")");
			});
			EventButton(le, "Show Placements", [cell=placementGuid->first, cellName]() {
				ConsoleRig::Console::GetInstance().Execute("scene:ShowPlacements(\"" + cellName + "\")");
			});
			le.PopNode();
		}

		le.PopNode();

		hover = le.BuildLayedOutWidgets();
	}

	static void SetupToolTipHover(ToolTipHover& hover, const std::exception& e)
	{
		LayoutEngine le;

		auto rootNode = le.NewNode();
		le.PushRoot(rootNode, {32, 32});
		YGNodeStyleSetFlexDirection(rootNode, YGFlexDirectionColumn);
		YGNodeStyleSetJustifyContent(rootNode, YGJustifyFlexStart);
		YGNodeStyleSetAlignItems(rootNode, YGAlignStretch);		// stretch out each item to fill the entire row

		Heading(le, "Exception during query");
		KeyValueSimple(le, e.what());

		le.PopNode();

		hover = le.BuildLayedOutWidgets();
	}

	class PlacementsDisplay : public IWidget ///////////////////////////////////////////////////////////
	{
	public:
		void Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState)
		{
			const unsigned lineHeight = 20;
			const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };

			auto allocation = layout.AllocateFullWidth(30);
			FillRectangle(context, allocation, titleBkground);
			allocation._topLeft[0] += 8;
			if (auto* font = _headingFont->TryActualize())
				DrawText()
					.Font(**font)
					.Color({ 191, 123, 0 })
					.Alignment(RenderOverlays::TextAlignment::Left)
					.Flags(RenderOverlays::DrawTextFlags::Shadow)
					.Draw(context, allocation, "Placements Selector");
			
			if (_hasSelectedPlacements) {
				DrawBoundingBox(
					context, _selectedPlacementsLocalBoundary, AsFloat3x4(_selectedPlacementsLocalToWorld),
					ColorB(196, 230, 230));

				// Place the hover either left or right on the screen; depending on which side has more space
				// This causes the popup to jump around a bit; but it will often find a pretty logical place to end up
				UInt2 viewportDims { 1920, 1080 };	// todo -- get real values
				auto cameraDesc = ToolsRig::AsCameraDesc(*_camera);
				auto projDesc = RenderCore::Techniques::BuildProjectionDesc(cameraDesc, viewportDims);
				auto localToProj = Combine(_selectedPlacementsLocalToWorld, projDesc._worldToProjection);
				auto projectionSpaceCorners = FindFrustumIntersectionExtremities(
					localToProj,
					_selectedPlacementsLocalBoundary.first, _selectedPlacementsLocalBoundary.second,
					RenderCore::Techniques::GetDefaultClipSpaceType());
				Float2 screenSpaceMins { FLT_MAX, FLT_MAX }, screenSpaceMaxs { -FLT_MAX, -FLT_MAX };
				for (auto proj:projectionSpaceCorners) {
					proj[0] = (proj[0] / proj[3] * .5f + 0.5f) * viewportDims[0];
					proj[1] = (proj[1] / proj[3] * .5f + 0.5f) * viewportDims[1];
					screenSpaceMins[0] = std::min(screenSpaceMins[0], proj[0]);
					screenSpaceMins[1] = std::min(screenSpaceMins[1], proj[1]);
					screenSpaceMaxs[0] = std::max(screenSpaceMaxs[0], proj[0]);
					screenSpaceMaxs[1] = std::max(screenSpaceMaxs[1], proj[1]);
				}

				if (screenSpaceMins[0] < screenSpaceMaxs[0]) {
					// FillRectangle(context, Rect{screenSpaceMins, screenSpaceMaxs}, ColorB(196, 196, 196, 128));

					float spaceLeft = screenSpaceMins[0];
					float spaceRight = viewportDims[0] - screenSpaceMaxs[0];

					float left;
					if (spaceLeft > spaceRight) {
						left = std::max(0.f, screenSpaceMins[0] - _hover.GetDimensions()[0]);
					} else {
						left = screenSpaceMaxs[0];
					}
					float top = std::max(0.f, screenSpaceMins[1]);
					top = std::min(top, (float)viewportDims[1] - std::min(_hover.GetDimensions()[1], (int)viewportDims[1]));
					Float3x3 transform {
						1.f, 0.f, left,
						0.f, 1.f, top,
						0.f, 0.f, 1.f
					};
					_hover.Render(context, layout, interactables, interfaceState, transform);
				}
			}

			if (_hasLastRayTest)
				context.DrawLines(ProjectionMode::P3D, &_lastRayTest.first, 2, ColorB{255, 128, 128});
		}

		virtual ProcessInputResult ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input)
		{
			if (_hover.ProcessInput(interfaceState, input) == ProcessInputResult::Consumed)
				return ProcessInputResult::Consumed;

			// Given the camera & viewport find a ray & perform intersection detection with placements scene
			if (input.IsRelease_LButton()) {
				UInt2 viewportDims { 1920, 1080 };	// todo -- get real values
				auto cameraDesc = ToolsRig::AsCameraDesc(*_camera);
				auto worldSpaceRay = SceneEngine::CalculateWorldSpaceRay(
					cameraDesc, input._mousePosition, {0,0}, viewportDims);

				_lastRayTest = worldSpaceRay;
				_hasLastRayTest = true;

				auto threadContext = RenderCore::Techniques::GetThreadContext();
				auto techniqueContext = SceneEngine::MakeIntersectionsTechniqueContext(*_drawingApparatus);
				RenderCore::Techniques::ParsingContext parsingContext{techniqueContext, *threadContext};
				parsingContext.SetPipelineAcceleratorsVisibility(techniqueContext._pipelineAccelerators->VisibilityBarrier());
				parsingContext.GetProjectionDesc() = RenderCore::Techniques::BuildProjectionDesc(cameraDesc, viewportDims);

				auto firstHit = SceneEngine::FirstRayIntersection(parsingContext, *_placementsEditor, worldSpaceRay, &cameraDesc);
				if (firstHit) {
					if (firstHit->_metadataQuery) {
						TRY {
							_selectedPlacementsLocalBoundary = TryAnyCast(firstHit->_metadataQuery("LocalBoundary"_h), std::make_pair(Zero<Float3>(), Zero<Float3>()));
							_selectedPlacementsLocalToWorld = TryAnyCast(firstHit->_metadataQuery("LocalToWorld"_h), Identity<Float4x4>());
							SetupToolTipHover(_hover, firstHit->_metadataQuery, *_placementsEditor);
						} CATCH (const std::exception& e) {
							_hover = {};
							_selectedPlacementsLocalBoundary = std::make_pair(Zero<Float3>(), Zero<Float3>());
							_selectedPlacementsLocalToWorld = Identity<Float4x4>();
							SetupToolTipHover(_hover, e);
						} CATCH_END
					} else {
						_hover = {};
					}
					_hasSelectedPlacements = true;
				} else {
					_hover = {};
					_hasSelectedPlacements = false;
				}

				return ProcessInputResult::Consumed;
			}

			if (input.IsPress_LButton())
				return ProcessInputResult::Consumed;

			return ProcessInputResult::Passthrough;
		}

		PlacementsDisplay(
			std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
			std::shared_ptr<SceneEngine::PlacementsEditor> placements, 
			std::shared_ptr<ToolsRig::VisCameraSettings> camera)
		: _drawingApparatus(std::move(drawingApparatus))
		, _placementsEditor(std::move(placements))
		, _camera(std::move(camera))
		{
			_headingFont = RenderOverlays::MakeFont("DosisExtraBold", 20);
			CommonWidgets::Draw::StallForDefaultFonts();
		}

	private:
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
		std::shared_ptr<SceneEngine::PlacementsEditor> _placementsEditor;
		std::shared_ptr<ToolsRig::VisCameraSettings> _camera;

		ToolTipHover _hover;
		std::pair<Float3, Float3> _selectedPlacementsLocalBoundary;
		Float4x4 _selectedPlacementsLocalToWorld;
		bool _hasSelectedPlacements = false;

		std::pair<Float3, Float3> _lastRayTest;
		bool _hasLastRayTest = false;

		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _headingFont;
	};

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreatePlacementsDisplay(
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
		std::shared_ptr<SceneEngine::PlacementsEditor> placements,
		std::shared_ptr<ToolsRig::VisCameraSettings> camera)
	{
		return std::make_shared<PlacementsDisplay>(std::move(drawingApparatus), std::move(placements), std::move(camera));
	}

}}

