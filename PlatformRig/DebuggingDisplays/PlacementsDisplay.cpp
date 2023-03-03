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
#include "../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/LayoutEngine.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../RenderOverlays/Font.h"
#include "../../RenderOverlays/OverlayEffects.h"
#include "../../RenderOverlays/ShapesRendering.h"
#include "../../RenderOverlays/DrawText.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../Tools/ToolsRig/VisualisationUtils.h"
#include "../../Tools/ToolsRig/ToolsRigServices.h"
#include "../../Tools/EntityInterface/EntityInterface.h"
#include "../../Assets/Marker.h"
#include "../../Assets/Assets.h"
#include "../../Math/MathSerialization.h"
#include "../../ConsoleRig/Console.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/FastParseValue.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Formatters/FormatterUtils.h"
#include <sstream>

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

	struct Vertex_PCT { Float3 _position; unsigned _colour; Float2 _texCoord; Vertex_PCT(Float3 position, unsigned colour, Float2 texCoord) : _position(position), _colour(colour), _texCoord(texCoord) {};   static RenderCore::MiniInputElementDesc inputElements2D[];};
	RenderCore::MiniInputElementDesc Vertex_PCT::inputElements2D[] =
	{
		{ RenderCore::Techniques::CommonSemantics::PIXELPOSITION, RenderCore::Format::R32G32B32_FLOAT },
		{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM },
		{ RenderCore::Techniques::CommonSemantics::TEXCOORD, RenderCore::Format::R32G32_FLOAT }
	};

	struct Vertex_PC { Float3 _position; unsigned _colour; Vertex_PC(Float3 position, unsigned colour) : _position(position), _colour(colour) {};   static RenderCore::MiniInputElementDesc inputElements2D[];};
	RenderCore::MiniInputElementDesc Vertex_PC::inputElements2D[] =
	{
		{ RenderCore::Techniques::CommonSemantics::PIXELPOSITION, RenderCore::Format::R32G32B32_FLOAT },
		{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM }
	};

	template<typename T>
		class MountedData
	{
	public:
		operator const T&() const { return _data; }
		const T& get() const { return _data; };

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		MountedData(Formatters::IDynamicInputFormatter& fmttr)
		: _data(fmttr), _depVal(fmttr.GetDependencyValidation())
		{}
		MountedData() = default;

		static void ConstructToPromise(
			std::promise<MountedData>&& promise,
			StringSection<> mountLocation)
		{
			::Assets::WhenAll(ToolsRig::Services::GetEntityMountingTree().BeginFormatter(mountLocation)).ThenConstructToPromise(
				std::move(promise),
				[](auto fmttr) { return MountedData{*fmttr}; });
		}

		static const T& LoadOrDefault(StringSection<> mountLocation)
		{
			auto marker = ::Assets::MakeAssetMarker<MountedData>(mountLocation);
			if (auto* actualized = marker->TryActualize())
				return actualized->get();
			static T def;
			return def;
		}
	private:
		T _data;
		::Assets::DependencyValidation _depVal;
	};

	static ColorB DeserializeColor(Formatters::IDynamicInputFormatter& fmttr)
	{
		IteratorRange<const void*> value;
		ImpliedTyping::TypeDesc typeDesc;
		if (!fmttr.TryRawValue(value, typeDesc))
			Throw(Formatters::FormatException("Expecting color value", fmttr.GetLocation()));

		if (auto intForm = ImpliedTyping::VariantNonRetained{typeDesc, value}.TryCastValue<unsigned>()) {
			return *intForm;
		} else if (auto tripletForm = ImpliedTyping::VariantNonRetained{typeDesc, value}.TryCastValue<UInt3>()) {
			return ColorB{uint8_t((*tripletForm)[0]), uint8_t((*tripletForm)[1]), uint8_t((*tripletForm)[2])};
		} else if (auto quadForm = ImpliedTyping::VariantNonRetained{typeDesc, value}.TryCastValue<UInt4>()) {
			return ColorB{uint8_t((*quadForm)[0]), uint8_t((*quadForm)[1]), uint8_t((*quadForm)[2]), uint8_t((*quadForm)[3])};
		} else {
			Throw(Formatters::FormatException("Could not interpret value as color", fmttr.GetLocation()));
		}
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

	static ImbuedNode* MinimalHeading(LayoutEngine& layoutEngine, std::string&& label)
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

	static YGNodeRef LeftRightMargins(LayoutEngine& layoutEngine, float marginPx)
	{
		auto baseNode = layoutEngine.NewNode();
		layoutEngine.InsertChildToStackTop(baseNode);
		layoutEngine.PushNode(baseNode);

		YGNodeStyleSetMargin(baseNode, YGEdgeLeft, marginPx);
		YGNodeStyleSetMargin(baseNode, YGEdgeRight, marginPx);
		return baseNode;
	}

	ImbuedNode* MinimalLabel(LayoutEngine& layoutEngine, std::string&& str)
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

	static std::string ColouriseFilename(StringSection<> filename)
	{
		auto split = MakeFileNameSplitter(filename);
		std::stringstream str;
		if (!split.DriveAndPath().IsEmpty()) {
			const bool gradualBrightnessChange = true;
			if (!gradualBrightnessChange) {
				str << "{color:9f9f9f}" << split.DriveAndPath();
			} else {
				auto splitPath = MakeSplitPath(split.DriveAndPath());
				if (splitPath.BeginsWithSeparator()) str << "/";
				for (unsigned c=0; c<splitPath.GetSectionCount(); ++c) {
					auto brightness = LinearInterpolate(0x5f, 0xcf, c/float(splitPath.GetSectionCount()));
					if (c != 0) str << "/";
					str << "{color:" << std::hex << brightness << brightness << brightness << std::dec << "}" << splitPath.GetSection(c);
				}
				if (splitPath.EndsWithSeparator()) str << "/";
			}
		}
		if (!split.File().IsEmpty())
			str << "{color:7f8fdf}" << split.File();
		if (!split.ExtensionWithPeriod().IsEmpty())
			str << "{color:df8f7f}" << split.ExtensionWithPeriod();
		if (!split.ParametersWithDivider().IsEmpty())
			str << "{color:7fdf8f}" << split.ParametersWithDivider();
		return str.str();
	}

	class ToolTipStyler
	{
	public:
		YGNodeRef SectionHeader(LayoutEngine& layoutEngine, std::string&& label)
		{
			// we need a container node to put some padding and margins on
			auto headerContainer = layoutEngine.NewNode();
			layoutEngine.InsertChildToStackTop(headerContainer);
			layoutEngine.PushNode(headerContainer);
			YGNodeStyleSetFlexGrow(headerContainer, 1.f);
			YGNodeStyleSetMargin(headerContainer, YGEdgeVertical, (float)_staticData->_sectionHeaderVertMargins);
			YGNodeStyleSetPadding(headerContainer, YGEdgeVertical, (float)_staticData->_sectionHeaderVertPadding);
			YGNodeStyleSetPadding(headerContainer, YGEdgeLeft, 64);
			YGNodeStyleSetFlexDirection(headerContainer, YGFlexDirectionRow);
			YGNodeStyleSetJustifyContent(headerContainer, YGJustifyFlexStart);

			{
				auto labelNode = layoutEngine.NewImbuedNode(0);
				layoutEngine.InsertChildToStackTop(*labelNode);

				const auto heightWithVertPadding = _headingFont->GetFontProperties()._lineHeight + 2*_staticData->_sectionHeaderVertPadding;
				const auto angleWidth = heightWithVertPadding/2;
				const auto extraPadding = heightWithVertPadding;

				YGNodeStyleSetWidth(*labelNode, StringWidth(*_headingFont, MakeStringSection(label)) + 2*(angleWidth+extraPadding));		// width including padding
				YGNodeStyleSetHeight(*labelNode, _headingFont->GetFontProperties()._lineHeight);
				YGNodeStyleSetFlexGrow(*labelNode, 0.f);
				
				YGNodeStyleSetPadding(*labelNode, YGEdgeLeft, float(angleWidth+extraPadding));
				YGNodeStyleSetPadding(*labelNode, YGEdgeRight, float(angleWidth+extraPadding));

				const ColorB headingBkColor = 0xff8ea3d2;

				labelNode->_nodeAttachments._drawDelegate = [label=std::move(label), headingBkColor, angleWidth, extraPadding, font=_headingFont](CommonWidgets::Draw& draw, Rect frame, Rect content) {
					Coord2 pts[] {
						{ frame._topLeft[0] + angleWidth, frame._topLeft[1] },
						{ frame._topLeft[0], (frame._topLeft[1]+frame._bottomRight[1])/2 },
						{ frame._topLeft[0] + angleWidth, frame._bottomRight[1] },

						{ frame._bottomRight[0] - angleWidth, frame._bottomRight[1] },
						{ frame._bottomRight[0], (frame._topLeft[1]+frame._bottomRight[1])/2 },
						{ frame._bottomRight[0] - angleWidth, frame._topLeft[1] }
					};
					unsigned indices[] {
						2, 0, 1,
						3, 0, 2,
						5, 0, 3,
						4, 5, 3
					};
					RenderCore::Techniques::ImmediateDrawableMaterial material;
					auto vertices = draw.GetContext().DrawGeometry(dimof(indices), Vertex_PC::inputElements2D, std::move(material)).Cast<Vertex_PC*>();
					for (unsigned c=0; c<dimof(indices); ++c)
						vertices[c] = Vertex_PC { AsPixelCoords(pts[indices[c]]), HardwareColor(headingBkColor) };

					DrawText().Font(*font).Color(ColorB::Black).Flags(0).Draw(draw.GetContext(), content, label);
				};
			}

			layoutEngine.PopNode();		// header container

			return headerContainer;
		}

		ImbuedNode* SectionContainer(LayoutEngine& layoutEngine, std::string&& label)
		{
			auto outerContainer = layoutEngine.NewImbuedNode(0);
			layoutEngine.InsertChildToStackTop(*outerContainer);
			layoutEngine.PushNode(*outerContainer);

			const ColorB headingBkColor = _staticData->_sectionHeaderBkColor;
			const auto headerHeight = _headingFont->GetFontProperties()._lineHeight + 2*_staticData->_sectionHeaderVertPadding;
			const auto headerLineOffset = _staticData->_sectionHeaderVertMargins + headerHeight/2;
			
			outerContainer->_nodeAttachments._drawDelegate = [headingBkColor, headerLineOffset](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				Float2 linePts[] {
					Float2 { frame._topLeft[0], frame._topLeft[1]+headerLineOffset },
					Float2 { frame._bottomRight[0], frame._topLeft[1]+headerLineOffset }
				};
				RenderOverlays::DashLine(draw.GetContext(), linePts, headingBkColor, 1.f);
			};

			/////////////////

			SectionHeader(layoutEngine, std::move(label));

			return outerContainer;
		}

		std::pair<YGNodeRef, YGNodeRef> DoubleSectionContainer(LayoutEngine& layoutEngine, std::string&& leftLabel, std::string&& rightLabel)
		{
			auto outerContainer = layoutEngine.NewImbuedNode(0);
			layoutEngine.InsertChildToStackTop(*outerContainer);
			layoutEngine.PushNode(*outerContainer);

			YGNodeStyleSetFlexDirection(*outerContainer, YGFlexDirectionRow);
			YGNodeStyleSetJustifyContent(*outerContainer, YGJustifySpaceBetween);

			// containers for left, separator, right
			auto leftOuterContainer = layoutEngine.NewNode();
			layoutEngine.InsertChildToStackTop(leftOuterContainer);
			YGNodeStyleSetFlexDirection(leftOuterContainer, YGFlexDirectionColumn);
			YGNodeStyleSetJustifyContent(leftOuterContainer, YGJustifySpaceBetween);
			YGNodeStyleSetFlexGrow(leftOuterContainer, 1.f);

			auto midSeparator = layoutEngine.NewImbuedNode(0);
			layoutEngine.InsertChildToStackTop(*midSeparator);
			YGNodeStyleSetWidth(*midSeparator, 16);
			YGNodeStyleSetFlexGrow(*midSeparator, 0.f);

			auto rightOuterContainer = layoutEngine.NewNode();
			layoutEngine.InsertChildToStackTop(rightOuterContainer);
			YGNodeStyleSetFlexDirection(rightOuterContainer, YGFlexDirectionColumn);
			YGNodeStyleSetJustifyContent(rightOuterContainer, YGJustifySpaceBetween);
			YGNodeStyleSetFlexGrow(rightOuterContainer, 1.f);

			// headers
			{
				layoutEngine.PushNode(leftOuterContainer);
				SectionHeader(layoutEngine, std::move(leftLabel));
				layoutEngine.PopNode();
			}
			{
				layoutEngine.PushNode(rightOuterContainer);
				SectionHeader(layoutEngine, std::move(rightLabel));
				layoutEngine.PopNode();
			}

			// draw in separator lines
			const ColorB headingBkColor = _staticData->_sectionHeaderBkColor;
			const auto headerHeight = _headingFont->GetFontProperties()._lineHeight + 2*_staticData->_sectionHeaderVertPadding;
			const auto headerLineOffset = _staticData->_sectionHeaderVertMargins + headerHeight/2;
			outerContainer->_nodeAttachments._drawDelegate = [headingBkColor, headerLineOffset](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				Float2 linePts[] {
					Float2 { frame._topLeft[0], frame._topLeft[1]+headerLineOffset },
					Float2 { frame._bottomRight[0], frame._topLeft[1]+headerLineOffset }
				};
				RenderOverlays::DashLine(draw.GetContext(), linePts, headingBkColor, 1.f);
			};

			midSeparator->_nodeAttachments._drawDelegate = [headingBkColor, headerLineOffset](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				Float2 linePts[] {
					Float2 { (frame._topLeft[0] + frame._bottomRight[0])/2, frame._topLeft[1] + headerLineOffset },
					Float2 { (frame._topLeft[0] + frame._bottomRight[0])/2, frame._bottomRight[1] }
				};
				RenderOverlays::DashLine(draw.GetContext(), linePts, headingBkColor, 1.f);
			};

			return {leftOuterContainer, rightOuterContainer};
		}

		YGNodeRef KeyValueGroup(LayoutEngine& layoutEngine)
		{
			auto baseNode = layoutEngine.NewNode();
			layoutEngine.InsertChildToStackTop(baseNode);
			layoutEngine.PushNode(baseNode);
			
			YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
			YGNodeStyleSetJustifyContent(baseNode, YGJustifySpaceBetween);
			YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
			
			YGNodeStyleSetMargin(baseNode, YGEdgeVertical, (float)_staticData->_keyValueGroupVertMargins);
			YGNodeStyleSetFlexGrow(baseNode, 0.f);		// don't grow, because our parent is column direction, and we want to have a fixed height
			return baseNode;
		}

		YGNodeRef VerticalGroup(LayoutEngine& layoutEngine)
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

		ImbuedNode* KeyName(LayoutEngine& layoutEngine, std::string&& label)
		{
			auto labelNode = layoutEngine.NewImbuedNode(0);
			layoutEngine.InsertChildToStackTop(*labelNode);

			YGNodeStyleSetWidth(*labelNode, StringWidth(*_valueFont, MakeStringSection(label)));
			YGNodeStyleSetHeight(*labelNode, _valueFont->GetFontProperties()._lineHeight);
			YGNodeStyleSetMargin(*labelNode, YGEdgeRight, 8);
			YGNodeStyleSetFlexGrow(*labelNode, 0.f);
			YGNodeStyleSetFlexShrink(*labelNode, 0.f);

			labelNode->_nodeAttachments._drawDelegate = [label=std::move(label), font=_valueFont](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				DrawText().Font(*font).Draw(draw.GetContext(), content, label);
			};
			return labelNode;
		}

		YGNodeRef ValueGroup(LayoutEngine& layoutEngine)
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

		ImbuedNode* KeyValue(LayoutEngine& layoutEngine, std::string&& label)
		{
			auto labelNode = layoutEngine.NewImbuedNode(0);
			layoutEngine.InsertChildToStackTop(*labelNode);

			YGNodeStyleSetHeight(*labelNode, _valueFont->GetFontProperties()._lineHeight);
			float maxWidth = StringWidth(*_valueFont, MakeStringSection(label));
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

			labelNode->_nodeAttachments._drawDelegate = [attachedData, font=_valueFont](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				// We don't get a notification after layout is finished -- so typically on the first render we may have to adjust
				// our string to fit
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

		template<typename T, typename std::enable_if<std::is_integral_v<T> || std::is_floating_point_v<T>>::type* =nullptr>
			ImbuedNode* KeyValue(LayoutEngine& layoutEngine, T value)
		{
			auto labelNode = layoutEngine.NewImbuedNode(0);
			layoutEngine.InsertChildToStackTop(*labelNode);

			auto str = std::to_string(value);

			YGNodeStyleSetWidth(*labelNode, StringWidth(*_valueFont, MakeStringSection(str)));
			YGNodeStyleSetHeight(*labelNode, _valueFont->GetFontProperties()._lineHeight);
			YGNodeStyleSetFlexGrow(*labelNode, 0.f);
			YGNodeStyleSetFlexShrink(*labelNode, 0.f);

			labelNode->_nodeAttachments._drawDelegate = [str=std::move(str), font=_valueFont](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				DrawText().Font(*font).Draw(draw.GetContext(), content, str);
			};
			return labelNode;
		}

		template<typename T, int N>
			ImbuedNode* KeyValue(LayoutEngine& layoutEngine, const VectorTT<T, N>& vector)
		{
			auto labelNode = layoutEngine.NewImbuedNode(0);
			layoutEngine.InsertChildToStackTop(*labelNode);

			std::stringstream sstr;
			sstr << vector;
			auto str = sstr.str();

			YGNodeStyleSetWidth(*labelNode, StringWidth(*_valueFont, MakeStringSection(str)));
			YGNodeStyleSetHeight(*labelNode, _valueFont->GetFontProperties()._lineHeight);
			YGNodeStyleSetFlexGrow(*labelNode, 0.f);
			YGNodeStyleSetFlexShrink(*labelNode, 0.f);

			labelNode->_nodeAttachments._drawDelegate = [str=std::move(str), font=_valueFont](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				DrawText().Font(*font).Draw(draw.GetContext(), content, str);
			};
			return labelNode;
		}

		ImbuedNode* KeyValueSimple(LayoutEngine& layoutEngine, std::string&& str)
		{
			auto labelNode = layoutEngine.NewImbuedNode(0);
			layoutEngine.InsertChildToStackTop(*labelNode);

			YGNodeStyleSetWidth(*labelNode, StringWidth(*_valueFont, MakeStringSection(str)));
			YGNodeStyleSetHeight(*labelNode, _valueFont->GetFontProperties()._lineHeight);
			YGNodeStyleSetFlexGrow(*labelNode, 0.f);
			YGNodeStyleSetFlexShrink(*labelNode, 0.f);

			labelNode->_nodeAttachments._drawDelegate = [str=std::move(str), font=_valueFont](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				DrawText().Font(*font).Draw(draw.GetContext(), content, str);
			};
			return labelNode;
		}

		ImbuedNode* ExpandingConnectorLine(LayoutEngine& layoutEngine)
		{
			auto connectorNode = layoutEngine.NewImbuedNode(0);
			layoutEngine.InsertChildToStackTop(*connectorNode);

			YGNodeStyleSetHeight(*connectorNode, _valueFont->GetFontProperties()._lineHeight);
			YGNodeStyleSetMinWidth(*connectorNode, 0.f);
			YGNodeStyleSetFlexGrow(*connectorNode, 1.0f);

			ColorB color = _staticData->_expandingConnectorColor;
			connectorNode->_nodeAttachments._drawDelegate = [font=_valueFont, dotWidth=_dotWidth, color](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				// Rendering these could actually end up a little expensive, because the majority of glyphs could end up being these
				char buffer[256];
				auto count = std::min(unsigned(dimof(buffer)-1), content.Width() / dotWidth);
				std::memset(buffer, '.', count);
				buffer[count] = '\0';
				DrawText().Font(*font).Flags(0).Color(color).Draw(draw.GetContext(), content, buffer);
			};
			return connectorNode;
		}

		ImbuedNode* EventButton(LayoutEngine& layoutEngine, std::string&& label, std::function<void()>&& event)
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

		YGNodeRef PopupBorder(LayoutEngine& layoutEngine)
		{
			auto baseNode = layoutEngine.NewNode();
			layoutEngine.InsertChildToStackTop(baseNode);
			layoutEngine.PushNode(baseNode);
			
			YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionColumn);
			YGNodeStyleSetJustifyContent(baseNode, YGJustifySpaceBetween);
			YGNodeStyleSetAlignItems(baseNode, YGAlignStretch);
			
			YGNodeStyleSetMargin(baseNode, YGEdgeAll, _staticData->_popupMargin);
			return baseNode;
		}

		struct StaticData
		{
			std::string _headingFont;
			std::string _valueFont;
			unsigned _popupMargin = 16;
			unsigned _valueAreaHorzMargins = 64;

			unsigned _sectionHeaderVertMargins = 12;
			unsigned _sectionHeaderVertPadding = 8;
			ColorB _sectionHeaderBkColor = 0xff8ea3d2;

			unsigned _keyValueGroupVertMargins = 4;

			ColorB _expandingConnectorColor = 0xff47476b;

			StaticData() = default;
			template<typename Formatter>
				StaticData(Formatter& fmttr)
			{
				uint64_t keyname;
				while (fmttr.TryKeyedItem(keyname)) {
					switch (keyname) {
					case "HeadingFont"_h: _headingFont = RequireStringValue(fmttr).AsString(); break;
					case "ValueFont"_h: _valueFont = RequireStringValue(fmttr).AsString(); break;
					case "PopupMargin"_h: _popupMargin = Formatters::RequireCastValue<decltype(_popupMargin)>(fmttr); break;
					case "ValueAreaHorizMargins"_h: _valueAreaHorzMargins = Formatters::RequireCastValue<decltype(_valueAreaHorzMargins)>(fmttr); break;

					case "SectionHeaderVertMargins"_h: _sectionHeaderVertMargins = Formatters::RequireCastValue<decltype(_sectionHeaderVertMargins)>(fmttr); break;
					case "SectionHeaderVertPadding"_h: _sectionHeaderVertPadding = Formatters::RequireCastValue<decltype(_sectionHeaderVertPadding)>(fmttr); break;
					case "SectionHeaderBkColor"_h: _sectionHeaderBkColor = DeserializeColor(fmttr); break;

					case "KeyValueGroupVertMargins"_h: _keyValueGroupVertMargins = Formatters::RequireCastValue<decltype(_keyValueGroupVertMargins)>(fmttr); break;

					case "ExpandingConnectorColor"_h: _expandingConnectorColor = DeserializeColor(fmttr); break;

					default: SkipValueOrElement(fmttr); break;
					}
				}
			}
		};
		
		const StaticData* _staticData;
		std::shared_ptr<Font> _headingFont;
		std::shared_ptr<Font> _valueFont;

		ToolTipStyler()
		{
			_staticData = &MountedData<StaticData>::LoadOrDefault("cfg/displays/tooltipstyler");
			_headingFont = ActualizeFont(_staticData->_headingFont);
			_valueFont = ActualizeFont(_staticData->_valueFont);

			_dotWidth = StringWidth(*_valueFont, MakeStringSection("..")) - StringWidth(*_valueFont, MakeStringSection("."));
		}

	private:
		std::shared_ptr<Font> ActualizeFont(StringSection<> name)
		{
			::Assets::PtrToMarkerPtr<Font> futureFont;
			if (name.IsEmpty()) {
				futureFont = MakeFont("Petra", 16);
			} else {
				auto colon = name.end();
				while (colon != name.begin() && *(colon-1) != ':') --colon;
				if (colon != name.begin()) {
					uint32_t fontSize = 0;
					auto* parseEnd = FastParseValue(MakeStringSection(colon, name.end()), fontSize);
					if (parseEnd != name.end())
						Throw(std::runtime_error(StringMeld<128>() << "Could not interpret font name (" << name << ")"));
					futureFont = MakeFont({name.begin(), colon-1}, fontSize);
				} else {
					futureFont = MakeFont(name, 16);
				}
			}

			return futureFont->Actualize();		// stall
		}

		unsigned _dotWidth = 8;
	};
	
	static void SetupToolTipHover(ToolTipHover& hover, const SceneEngine::IntersectionTestResult& testResult, SceneEngine::PlacementsEditor& placementsEditor)
	{
		LayoutEngine le;

		auto& metadataQuery = testResult._metadataQuery;
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
		auto localToCell = TryAnyCast<Float4x4>(metadataQuery("LocalToCell"_h));

		auto rootNode = le.NewNode();
		le.PushRoot(rootNode, {32, 32});
		YGNodeStyleSetMaxWidth(rootNode, 768);
		YGNodeStyleSetMaxHeight(rootNode, 1440);		// we need to set some maximum height to allow the dimensions returned in the layout to adapt to the children

		YGNodeStyleSetFlexDirection(rootNode, YGFlexDirectionColumn);
		YGNodeStyleSetJustifyContent(rootNode, YGJustifyFlexStart);
		YGNodeStyleSetAlignItems(rootNode, YGAlignStretch);		// stretch out each item to fill the entire row

		ToolTipStyler styler;
		styler.PopupBorder(le);

		styler.SectionContainer(le, "Placement Details");
		LeftRightMargins(le, styler._staticData->_valueAreaHorzMargins);

		if (!selectedMaterialName.empty() || !selectedModelName.empty()) {
			styler.KeyValueGroup(le); styler.KeyName(le, "Model Scaffold"); styler.ExpandingConnectorLine(le); styler.KeyValue(le, ColouriseFilename(selectedModelName)); le.PopNode();
			styler.KeyValueGroup(le); styler.KeyName(le, "Material Scaffold"); styler.ExpandingConnectorLine(le); styler.KeyValue(le, ColouriseFilename(selectedMaterialName)); le.PopNode();
		}
		if (drawCallIndex && drawCallCount && indexCount && materialName) {
			styler.KeyValueGroup(le); styler.KeyName(le, "Draw Call Index"); styler.ExpandingConnectorLine(le);
			styler.ValueGroup(le);
			styler.KeyValue(le, *drawCallIndex);
			styler.KeyValueSimple(le, "/");
			styler.KeyValue(le, *drawCallCount);
			le.PopNode();
			le.PopNode();
			styler.KeyValueGroup(le); styler.KeyName(le, "Index Count"); styler.ExpandingConnectorLine(le); styler.KeyValue(le, *indexCount); le.PopNode();
			styler.KeyValueGroup(le); styler.KeyName(le, "Material"); styler.ExpandingConnectorLine(le); styler.KeyValue(le, std::move(*materialName)); le.PopNode();
		}
		if (cellPlacementCount && cellSimilarPlacementCount) {
			styler.KeyValueGroup(le);
			styler.KeyName(le, "Cell Placements (similar/total)"); styler.ExpandingConnectorLine(le);
			styler.ValueGroup(le);
			styler.KeyValue(le, *cellSimilarPlacementCount);
			styler.KeyValueSimple(le, "/");
			styler.KeyValue(le, *cellPlacementCount);
			le.PopNode();
			le.PopNode();
		}

		le.PopNode();		// LeftRightMargins
		le.PopNode();		// TooltipStyleSectionContainer

		auto split = styler.DoubleSectionContainer(le, "Cell", "Intersection");

		{
			le.PushNode(split.first);

			if (placementGuid) {
				styler.VerticalGroup(le);
				styler.KeyValueGroup(le);
				styler.KeyName(le, "Cell");
				auto cellName = placementsEditor.GetCellSet().DehashCellName(placementGuid->first).AsString();
				if (!cellName.empty()) styler.KeyValue(le, ColouriseFilename(cellName));
				else styler.KeyValue(le, placementGuid->first);
				le.PopNode();
				styler.EventButton(le, "Show Quad Tree", [cell=placementGuid->first, cellName]() {
					// switch to another debugging display that will display the quad tree we're interested in
					ConsoleRig::Console::GetInstance().Execute("scene:ShowQuadTree(\"" + cellName + "\")");
				});
				styler.EventButton(le, "Show Placements", [cell=placementGuid->first, cellName]() {
					ConsoleRig::Console::GetInstance().Execute("scene:ShowPlacements(\"" + cellName + "\")");
				});
				le.PopNode();
			}
			if (localToCell) {
				auto* group = styler.VerticalGroup(le);
				YGNodeStyleSetAlignItems(group, YGAlignStretch);
				MinimalHeading(le, "Local to Cell");
				ScaleRotationTranslationM decomposed { *localToCell };
				styler.KeyValueGroup(le); styler.KeyName(le, "Translation"); styler.KeyValue(le, decomposed._translation); le.PopNode();
				if (!Equivalent(decomposed._scale, {1.f, 1.f, 1.f}, 1e-3f)) {
					styler.KeyValueGroup(le); styler.KeyName(le, "Scale"); styler.KeyValue(le, decomposed._scale); le.PopNode();
				}
				const cml::EulerOrder eulerOrder = cml::euler_order_yxz;
				Float3 ypr = cml::matrix_to_euler<Float3x3, Float3x3::value_type>(decomposed._rotation, eulerOrder);
				const char* labels[] = { "Rotate Y", "Rotate X", "Rotate Z" };
				for (unsigned c=0; c<3; ++c) {
					if (Equivalent(ypr[c], 0.f, 1e-3f)) continue;
					styler.KeyValueGroup(le); styler.KeyName(le, labels[c]); styler.KeyValue(le, ypr[c] * 180.f / gPI); le.PopNode();
				}
				le.PopNode();
			}

			le.PopNode();	// split.first
		}

		{
			le.PushNode(split.second);

			{
				auto* group = styler.VerticalGroup(le);
				YGNodeStyleSetAlignItems(group, YGAlignStretch);
				MinimalHeading(le, "Intersection");
				styler.KeyValueGroup(le);
				styler.KeyName(le, "Point");
				styler.KeyValue(le, testResult._worldSpaceIntersectionPt);
				le.PopNode();
				styler.KeyValueGroup(le);
				styler.KeyName(le, "Normal");
				styler.KeyValue(le, testResult._worldSpaceIntersectionNormal);
				le.PopNode();
				le.PopNode();
			}

			le.PopNode();	// split.second
		}

		le.PopNode();	// TooltipStyleDoubleSectionContainer
		le.PopNode();	// PopupBorder
		le.PopNode();	// root node

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

		MinimalHeading(le, "Exception during query");
		MinimalLabel(le, e.what());

		le.PopNode();

		hover = le.BuildLayedOutWidgets();
	}

	class TopBarRenderer
	{
	public:
		enum class SectionType { Heading, FrameRig };
		struct SectionRequest
		{
			SectionType _type = SectionType::FrameRig;
			unsigned _width = 0;
		};

		std::vector<Rect> Render(
			IOverlayContext& context, Interactables& interactables, InterfaceState& interfaceState,
			Rect viewport,
			IteratorRange<const SectionRequest*> sectionRequests);
	};

	struct TopBarStaticData
	{
		unsigned _topMargin = 12;
		unsigned _height = 42;
		unsigned _borderMargin = 4;
		unsigned _borderWidth = 2;
		unsigned _shadowHeight = 12;

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
				case "ShadowHeight"_h: _height = Formatters::RequireCastValue<decltype(_shadowHeight)>(fmttr); break;

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

	struct ThemeStaticData
	{
		ColorB _semiTransparentTint = 0xff2e3440;
		ColorB _topBarBorderColor = 0xffffffff;
		ColorB _headingBkgrnd = 0xffffffff;

		unsigned _shadowOffset0 = 8;
		unsigned _shadowOffset1 = 8;
		unsigned _shadowSoftnessRadius = 16;
		
		ThemeStaticData() = default;

		template<typename Formatter>
			ThemeStaticData(Formatter& fmttr)
		{
			uint64_t keyname;
			while (TryKeyedItem(fmttr, keyname)) {
				switch (keyname) {
				case "SemiTransparentTint"_h: _semiTransparentTint = DeserializeColor(fmttr); break;
				case "TopBarBorderColor"_h: _topBarBorderColor = DeserializeColor(fmttr); break;
				case "HeadingBackground"_h: _headingBkgrnd = DeserializeColor(fmttr); break;
				case "ShadowOffset0"_h: _shadowOffset0 = Formatters::RequireCastValue<decltype(_shadowOffset0)>(fmttr); break;
				case "ShadowOffset1"_h: _shadowOffset1 = Formatters::RequireCastValue<decltype(_shadowOffset1)>(fmttr); break;
				case "ShadowSoftnessRadius"_h: _shadowSoftnessRadius = Formatters::RequireCastValue<decltype(_shadowSoftnessRadius)>(fmttr); break;
				default: SkipValueOrElement(fmttr); break;
				}
			}
		}
	};

	static RenderCore::UniformsStreamInterface CreateTexturedUSI()
	{
		RenderCore::UniformsStreamInterface usi;
		usi.BindResourceView(0, "InputTexture"_h);
		return usi;
	}
	static RenderCore::UniformsStreamInterface s_texturedUSI = CreateTexturedUSI();

	auto TopBarRenderer::Render(
		IOverlayContext& context, Interactables& interactables, InterfaceState& interfaceState,
		Rect viewport,
		IteratorRange<const SectionRequest*> sectionRequests) -> std::vector<Rect>
	{
		// Render the top bar along the top of the viewport, including the areas for the sections 
		// as requested
		auto& topBarStaticData = MountedData<TopBarStaticData>::LoadOrDefault("cfg/displays/topbar");
		auto& themeStaticData = MountedData<ThemeStaticData>::LoadOrDefault("cfg/displays/theme");

		auto xAtPoint = viewport._bottomRight[0] - (topBarStaticData._height + topBarStaticData._frameRigPaddingLeft + topBarStaticData._frameRigPaddingRight + topBarStaticData._frameRigAreaWidth);
		auto xAtShoulder = viewport._bottomRight[0] - (topBarStaticData._frameRigPaddingLeft + topBarStaticData._frameRigPaddingRight + topBarStaticData._frameRigAreaWidth);

		Coord2 vertexPositions[6] {
			{ viewport._topLeft[0], viewport._topLeft[1] + topBarStaticData._topMargin },
			{ viewport._bottomRight[0], viewport._topLeft[1] + topBarStaticData._topMargin },
			{ viewport._topLeft[0], viewport._topLeft[1] + topBarStaticData._topMargin + topBarStaticData._height },
			{ xAtPoint, viewport._topLeft[1] + topBarStaticData._topMargin + topBarStaticData._height },
			{ xAtShoulder, viewport._topLeft[1] + topBarStaticData._topMargin + 2 * topBarStaticData._height },
			{ viewport._bottomRight[0], viewport._topLeft[1] + topBarStaticData._topMargin + 2 * topBarStaticData._height },
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

		auto vertices = context.DrawGeometry(dimof(indices), Vertex_PCT::inputElements2D, std::move(material)).Cast<Vertex_PCT*>();
		for (unsigned c=0; c<dimof(indices); ++c)
			vertices[c] = { AsPixelCoords(vertexPositions[indices[c]]), HardwareColor(themeStaticData._semiTransparentTint), Float2(0,0) };
		if (blurryBackground)
			for (unsigned c=0; c<dimof(indices); ++c)
				vertices[c]._texCoord = blurryBackground->AsTextureCoords(vertexPositions[indices[c]]);

		// render dashed line along the top
		Float2 topDashLine[] {
			Float2 { viewport._topLeft[0], viewport._topLeft[1] + topBarStaticData._topMargin + topBarStaticData._borderMargin },
			Float2 { viewport._bottomRight[0], viewport._topLeft[1] + topBarStaticData._topMargin + topBarStaticData._borderMargin }
		};

		// cosine rule for triangles
		// c^2 = a^2 + b^2 - 2ab.cos(C)
		// c is 45 degrees, and A & b are topBarStaticData._borderMargin
		// float a = topBarStaticData._borderMargin * std::sqrt(2*(1 - std::cos(gPI/4.f)));
		float a = topBarStaticData._borderMargin * std::tan(gPI/8.0f);

		Float2 bottomDashLine[] {
			Float2 { viewport._topLeft[0], viewport._topLeft[1] + topBarStaticData._topMargin + topBarStaticData._height - topBarStaticData._borderMargin },
			Float2 { xAtPoint + a, viewport._topLeft[1] + topBarStaticData._topMargin + topBarStaticData._height - topBarStaticData._borderMargin },

			Float2 { xAtShoulder + a, viewport._topLeft[1] + topBarStaticData._topMargin + 2*topBarStaticData._height - topBarStaticData._borderMargin },
			Float2 { viewport._bottomRight[0], viewport._topLeft[1] + topBarStaticData._topMargin + 2*topBarStaticData._height - topBarStaticData._borderMargin }
		};

		DashLine(context, topDashLine, themeStaticData._topBarBorderColor, (float)topBarStaticData._borderWidth);
		DashLine(context, bottomDashLine, themeStaticData._topBarBorderColor, (float)topBarStaticData._borderWidth);

		std::vector<Rect> result { sectionRequests.size(), Rect{ Coord2{0,0}, Coord2{0,0}} };
		auto headingRequest = std::find_if(sectionRequests.begin(), sectionRequests.end(), [](const auto& q) { return q._type == SectionType::Heading; });
		if (headingRequest != sectionRequests.end()) {

			// allocate a rectangle for the heading
			Rect frame;
			frame._topLeft = { viewport._topLeft[0] + topBarStaticData._preHeadingMargin, viewport._topLeft[1] + topBarStaticData._topMargin + topBarStaticData._height/2 - topBarStaticData._headingHeight/2 };
			frame._bottomRight = { 
				viewport._topLeft[0] + topBarStaticData._preHeadingMargin
				+ topBarStaticData._headingPadding*2 + headingRequest->_width,
				viewport._topLeft[1] + topBarStaticData._topMargin + topBarStaticData._height/2 + topBarStaticData._headingHeight/2 };

			Rect content;
			content._topLeft = frame._topLeft + Coord2{ topBarStaticData._headingPadding, topBarStaticData._headingPadding };
			content._bottomRight = frame._bottomRight - Coord2{ topBarStaticData._headingPadding, topBarStaticData._headingPadding };

			// draw a rhombus around the frame, but with some extra triangles
			RenderCore::Techniques::ImmediateDrawableMaterial material;
			auto vertices = context.DrawGeometry(6, Vertex_PC::inputElements2D, std::move(material)).Cast<Vertex_PC*>();
			Coord2 A = frame._topLeft;
			Coord2 B { frame._topLeft[0] - topBarStaticData._headingHeight, frame._bottomRight[1] };
			Coord2 C = frame._bottomRight;
			Coord2 D { frame._bottomRight[0] + topBarStaticData._headingHeight, frame._topLeft[1] };
			vertices[0] = Vertex_PC { AsPixelCoords(B), HardwareColor(themeStaticData._headingBkgrnd) };
			vertices[1] = Vertex_PC { AsPixelCoords(C), HardwareColor(themeStaticData._headingBkgrnd) };
			vertices[2] = Vertex_PC { AsPixelCoords(A), HardwareColor(themeStaticData._headingBkgrnd) };

			vertices[3] = Vertex_PC { AsPixelCoords(A), HardwareColor(themeStaticData._headingBkgrnd) };
			vertices[4] = Vertex_PC { AsPixelCoords(C), HardwareColor(themeStaticData._headingBkgrnd) };
			vertices[5] = Vertex_PC { AsPixelCoords(D), HardwareColor(themeStaticData._headingBkgrnd) };

			result[headingRequest-sectionRequests.begin()] = content;

		}

		return result;
	}

	class PlacementsDisplay : public IWidget ///////////////////////////////////////////////////////////
	{
	public:
		void Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState)
		{
			const unsigned lineHeight = 20;
			const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };

			const char headingString[] = "Placements Selector";
			float headingWidth = 0.f;
			auto* headingFont = _headingFont->TryActualize();
			if (headingFont)
				headingWidth = StringWidth(**headingFont, MakeStringSection(headingString));

			TopBarRenderer topBarRenderer;
			TopBarRenderer::SectionRequest topBarSections[] { {TopBarRenderer::SectionType::Heading, (unsigned)headingWidth }, {TopBarRenderer::SectionType::FrameRig} };
			auto topBar = topBarRenderer.Render(context, interactables, interfaceState, layout.GetMaximumSize(), topBarSections);
			assert(topBar.size() == dimof(topBarSections));
			if (IsGood(topBar[0]) && headingFont)
				DrawText()
					.Font(**headingFont)
					.Color(ColorB::Black)
					.Alignment(RenderOverlays::TextAlignment::Left)
					.Flags(0)
					.Draw(context, topBar[0], "Placements Selector");

			auto* parsingContext = (RenderCore::Techniques::ParsingContext*)context.GetService(typeid(RenderCore::Techniques::ParsingContext).hash_code());
			if (parsingContext) {
				auto& vp = parsingContext->GetViewport();
				_lastCamera = { parsingContext->GetProjectionDesc(), Float2{vp._x, vp._y}, Float2{vp._x+vp._width, vp._y+vp._height}};
			} else
				_lastCamera = {};

			if (_hasSelectedPlacements && parsingContext) {
				DrawBoundingBox(
					context, _selectedPlacementsLocalBoundary, AsFloat3x4(_selectedPlacementsLocalToWorld),
					ColorB(196, 230, 230));

				// Place the hover either left or right on the screen; depending on which side has more space
				// This causes the popup to jump around a bit; but it will often find a pretty logical place to end up
				UInt2 viewportDims { parsingContext->GetViewport()._width, parsingContext->GetViewport()._height };
				auto projDesc = parsingContext->GetProjectionDesc();
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

					auto& themeStaticData = MountedData<ThemeStaticData>::LoadOrDefault("cfg/displays/theme");
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

					Rect outerRect{Coord2{left, top}, Coord2{left+_hover.GetDimensions()[0], top+_hover.GetDimensions()[1]}};

					SoftShadowRectangle(
						context,
						{outerRect._topLeft + Coord2(themeStaticData._shadowOffset0, themeStaticData._shadowOffset0), outerRect._bottomRight + Coord2(themeStaticData._shadowOffset1, themeStaticData._shadowOffset1)},
						themeStaticData._shadowSoftnessRadius);

					static ColorB borderColor = ColorB(32, 96, 128, 192);
					OutlineRectangle(
						context,
						{outerRect._topLeft, outerRect._bottomRight + Coord2(1,1)},
						borderColor, 1.0f);

					RenderOverlays::BlurryBackgroundEffect* blurryBackground;
					if ((blurryBackground = context.GetService<RenderOverlays::BlurryBackgroundEffect>())) {
						ColorAdjust colAdj;
						colAdj._luminanceOffset = 0.025f; colAdj._saturationMultiplier = 0.65f;
						// auto baseColor = ColorB::FromNormalized(LinearToSRGB_Formal(.55f * .65f), LinearToSRGB_Formal(.55f * .7f), 0.55f);
						
						auto baseColor = themeStaticData._semiTransparentTint;
						ColorAdjustRectangle(
							context, outerRect,
							{outerRect._topLeft[0] / (float)viewportDims[0], outerRect._topLeft[1] / (float)viewportDims[1]}, {outerRect._bottomRight[0] / (float)viewportDims[0], outerRect._bottomRight[1] / (float)viewportDims[1]},
							blurryBackground->GetResourceView(), colAdj, baseColor);
					} else
						FillRectangle(context, outerRect, ColorB(32, 32, 96, 128));
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
				if (_lastCamera.has_value()) {
					auto worldSpaceRay = RenderCore::Techniques::BuildRayUnderCursor(
						input._mousePosition, _lastCamera->_projDesc,
						{_lastCamera->_viewportTopLeft, _lastCamera->_viewportBottomRight});

					_lastRayTest = worldSpaceRay;
					_hasLastRayTest = true;

					auto threadContext = RenderCore::Techniques::GetThreadContext();
					auto techniqueContext = SceneEngine::MakeIntersectionsTechniqueContext(*_drawingApparatus);
					RenderCore::Techniques::ParsingContext parsingContext{techniqueContext, *threadContext};
					parsingContext.SetPipelineAcceleratorsVisibility(techniqueContext._pipelineAccelerators->VisibilityBarrier());
					parsingContext.GetProjectionDesc() = _lastCamera->_projDesc;

					auto firstHit = SceneEngine::FirstRayIntersection(parsingContext, *_placementsEditor, worldSpaceRay, nullptr);
					if (firstHit) {
						if (firstHit->_metadataQuery) {
							TRY {
								_selectedPlacementsLocalBoundary = TryAnyCast(firstHit->_metadataQuery("LocalBoundary"_h), std::make_pair(Zero<Float3>(), Zero<Float3>()));
								_selectedPlacementsLocalToWorld = TryAnyCast(firstHit->_metadataQuery("LocalToWorld"_h), Identity<Float4x4>());
								SetupToolTipHover(_hover, *firstHit, *_placementsEditor);
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
				}

				return ProcessInputResult::Consumed;
			}

			if (input.IsPress_LButton())
				return ProcessInputResult::Consumed;

			return ProcessInputResult::Passthrough;
		}

		PlacementsDisplay(
			std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
			std::shared_ptr<SceneEngine::PlacementsEditor> placements)
		: _drawingApparatus(std::move(drawingApparatus))
		, _placementsEditor(std::move(placements))
		{
			_headingFont = RenderOverlays::MakeFont("OrbitronBlack", 20);
			CommonWidgets::Draw::StallForDefaultFonts();
		}

	private:
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
		std::shared_ptr<SceneEngine::PlacementsEditor> _placementsEditor;

		ToolTipHover _hover;
		std::pair<Float3, Float3> _selectedPlacementsLocalBoundary;
		Float4x4 _selectedPlacementsLocalToWorld;
		bool _hasSelectedPlacements = false;

		std::pair<Float3, Float3> _lastRayTest;
		bool _hasLastRayTest = false;

		struct LastCamera { RenderCore::Techniques::ProjectionDesc _projDesc; Float2 _viewportTopLeft; UInt2 _viewportBottomRight; };
		std::optional<LastCamera> _lastCamera;

		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _headingFont;
	};

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreatePlacementsDisplay(
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
		std::shared_ptr<SceneEngine::PlacementsEditor> placements)
	{
		return std::make_shared<PlacementsDisplay>(std::move(drawingApparatus), std::move(placements));
	}

}}

