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
#include "../../Utility/StringFormat.h"

#include "../../Foreign/yoga/yoga/YGNode.h"

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

	class ToolTipHover
	{
	public:
		void Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState, Coord2 offset);

		ToolTipHover(LayedOutWidgets&& layedOutWidgets);
		ToolTipHover() = default;
		ToolTipHover(ToolTipHover&&) = default;
		ToolTipHover& operator=(ToolTipHover&&) = default;
		~ToolTipHover();
	private:
		LayedOutWidgets _layedOutWidgets;
	};

	void ToolTipHover::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState, Coord2 offset)
	{
		CommonWidgets::Draw draw{context, interactables, interfaceState};
		_layedOutWidgets.Draw(draw);
	}

	ToolTipHover::ToolTipHover(LayedOutWidgets&& layedOutWidgets)
	: _layedOutWidgets(std::move(layedOutWidgets)) {}
	ToolTipHover::~ToolTipHover() {}

	static void Heading(LayoutEngine& layoutEngine, std::string&& label)
	{
		auto labelNode = layoutEngine.NewImbuedNode(0);
		layoutEngine.InsertChildToStackTop(*labelNode);

		YGNodeStyleSetHeight(*labelNode, CommonWidgets::Draw::baseLineHeight);
		YGNodeStyleSetFlexGrow(*labelNode, 0.f);		// don't grow, because our parent is column direction, and we want to have a fixed height
		YGNodeStyleSetMargin(*labelNode, YGEdgeAll, 2);
		
		labelNode->_nodeAttachments._drawDelegate = [label=std::move(label)](CommonWidgets::Draw& draw, Rect frame, Rect content) {
			FillRectangle(draw.GetContext(), frame, 0xff8f8f8f);
			DrawText().Font(*draw.GetDefaultFontsBox()._headingFont).Draw(draw.GetContext(), content, label);
		};
	}

	static void KeyValueGroup(LayoutEngine& layoutEngine)
	{
		auto baseNode = layoutEngine.NewImbuedNode(0);
		layoutEngine.InsertChildToStackTop(*baseNode);
		layoutEngine.PushNode(*baseNode);
		
		YGNodeStyleSetFlexDirection(*baseNode, YGFlexDirectionRow);
		YGNodeStyleSetJustifyContent(*baseNode, YGJustifySpaceBetween);
		YGNodeStyleSetAlignItems(*baseNode, YGAlignCenter);
		
		YGNodeStyleSetMargin(*baseNode, YGEdgeAll, 2);
		YGNodeStyleSetFlexGrow(*baseNode, 0.f);		// don't grow, because our parent is column direction, and we want to have a fixed height

		baseNode->_nodeAttachments._drawDelegate = [](CommonWidgets::Draw& draw, Rect frame, Rect content) {
			FillRectangle(draw.GetContext(), frame, 0xff3f3f8f);
		};
	}

	static void KeyName(LayoutEngine& layoutEngine, std::string&& label)
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
			FillRectangle(draw.GetContext(), frame, 0xff3f8f3f);
			DrawText().Font(*draw.GetDefaultFontsBox()._buttonFont).Draw(draw.GetContext(), content, label);
		};
	}

	static void KeyValue(LayoutEngine& layoutEngine, std::string&& label)
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
			FillRectangle(draw.GetContext(), frame, 0xff8f3f3f);

			// We don't get a notification after layout is finished -- so typically on the first render we may have to adjust
			// our string to fit
			auto* font = draw.GetDefaultFontsBox()._buttonFont.get();
			if (frame.Width() != attachedData->_cachedWidth) {
				attachedData->_cachedWidth = frame.Width();
				char buffer[MaxPath];
				auto fitWidth = StringEllipsisDoubleEnded(buffer, dimof(buffer), *font, MakeStringSection(attachedData->_originalLabel), MakeStringSection("/\\"), (float)frame.Width());
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
	}

	static void SetupToolTipHover(ToolTipHover& hover, SceneEngine::MetadataProvider& metadataQuery)
	{
		LayoutEngine le;

		std::string selectedMaterialName, selectedModelName;
		selectedMaterialName = TryAnyCast(metadataQuery("MaterialName"_h), selectedMaterialName);
		selectedModelName = TryAnyCast(metadataQuery("ModelScaffold"_h), selectedModelName);

		auto rootNode = le.NewImbuedNode(0);
		YGNodeStyleSetFlexDirection(*rootNode, YGFlexDirectionColumn);
		YGNodeStyleSetJustifyContent(*rootNode, YGJustifyFlexStart);
		YGNodeStyleSetAlignItems(*rootNode, YGAlignStretch);		// stretch out each item to fill the entire row

		YGNodeStyleSetMaxWidth(*rootNode, 768);

		rootNode->_nodeAttachments._drawDelegate = [](auto& draw, auto frame, auto context) {
			FillRectangle(draw.GetContext(), frame, 0xff3f3f3f);
		};
		le.PushRoot(*rootNode);
		Heading(le, "Placement");

		KeyValueGroup(le); KeyName(le, "Model"); KeyValue(le, std::move(selectedModelName)); le.PopNode();
		KeyValueGroup(le); KeyName(le, "Material"); KeyValue(le, std::move(selectedMaterialName)); le.PopNode();

		le.PopNode();

		Rect container { {0, 0}, {32, 32} };
		hover = le.BuildLayedOutWidgets(container);
	}

	class PlacementsDisplay : public IWidget ///////////////////////////////////////////////////////////
	{
	public:
		void Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
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
				_hover.Render(context, layout, interactables, interfaceState, {100, 100});
			}

			if (_hasLastRayTest)
				context.DrawLines(ProjectionMode::P3D, &_lastRayTest.first, 2, ColorB{255, 128, 128});
		}

		virtual ProcessInputResult ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input)
		{
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
						SetupToolTipHover(_hover, firstHit->_metadataQuery);
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

